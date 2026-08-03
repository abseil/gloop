// Copyright 2026 Google LLC.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Removing the following header is prohibited as it can introduce undefined
// behavior.
// clang-format off
#include "gloop/enforce_gloop_support.h"
// clang-format on

// Code for reading in ELF files.
//
// For information on the ELF format, see
// http://www.x86.org/ftp/manuals/tools/elf.pdf
//
// I also liked:
// http://www.caldera.com/developers/gabi/1998-04-29/contents.html
//
// A note about types: When dealing with the file format, we use types
// like Elf32_Word, but in the public interfaces we treat all
// addresses as uint64_t. As a result, we should be able to symbolize
// 64-bit binaries from a 32-bit process (which we don't do,
// anyway). size_t should therefore be avoided, except where required
// by things like mmap().
//
// Although most of this code can deal with arbitrary ELF files of
// either word size, the public ElfReader interface only examines
// files loaded into the current address space. This code cannot handle
// ELF files with a non-native byte ordering.
//
// TODO: It would be nice if we could accomplish this task
// without using malloc(), so we could use it as the process is dying.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE  // needed for pread()
#endif

#include "gloop/util/symbolize/elf_reader.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/prctl.h>
#endif  // __linux__

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/numeric/bits.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "gloop/base/commandlineflags.h"
#include "gloop/base/port.h"  // IWYU pragma: keep
#include "gloop/testing/production_stub/testvalue.h"
#include "gloop/util/symbolize/symbol_map_sink.h"
#include "zlib.h"

// elf.h is too old to define SHF_COMPRESSED and Elf64_Chdr
#ifndef SHF_COMPRESSED
#define SHF_COMPRESSED (1 << 11) /* Section with compressed data. */
#define ELFCOMPRESS_ZLIB 1       /* ZLIB/DEFLATE algorithm.  */

struct Elf32_Chdr {
  Elf32_Word ch_type;      /* Compression format.  */
  Elf32_Word ch_size;      /* Uncompressed data size.  */
  Elf32_Word ch_addralign; /* Uncompressed data alignment.  */
};

struct Elf64_Chdr {
  Elf64_Word ch_type; /* Compression format.  */
  Elf64_Word ch_reserved;
  Elf64_Xword ch_size;      /* Uncompressed data size.  */
  Elf64_Xword ch_addralign; /* Uncompressed data alignment.  */
};
#endif
// elf.h does not define ELFCOMPRESS_ZSTD
#define ELFCOMPRESS_ZSTD 2

// TODO: Can be removed once all Java code is using the Google3
// launcher. We need to avoid processing PLT functions as it causes memory
// fragmentation in malloc, which is fixed in tcmalloc - and if the Google3
// launcher is used the JVM will then use tcmalloc. b/13735638
ABSL_FLAG(bool, elfreader_process_dynsyms, true,
          "Activate PLT function processing");

#ifndef PR_SET_VMA
#define PR_SET_VMA 0x53564d41
#endif

#ifndef PR_SET_VMA_ANON_NAME
#define PR_SET_VMA_ANON_NAME 0
#endif

namespace {

// The lowest bit of an ARM symbol value is used to indicate a Thumb address.
constexpr int kARMThumbBitOffset = 0;

// Converts an ARM Thumb symbol value to a true aligned address value.
template <typename T>
T AdjustARMThumbSymbolValue(const T& symbol_table_value) {
  return symbol_table_value & ~(1 << kARMThumbBitOffset);
}

void AnnotateVMAName(void* ptr, size_t size) {
#ifdef __linux__
  // Make a best-effort attempt to name the allocated region.  The call may
  // fail, but we ignore this.
  const int old_errno = errno;
  constexpr char kName[] = "elf_reader";
  (void)prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, ptr, size, kName);
  errno = old_errno;
#endif  // __linux__
}

// Names of PLT-related sections.
constexpr absl::string_view kElfPLTRelSectionName =
    ".rel.plt";  // Use Rel struct.
constexpr absl::string_view kElfPLTRelaSectionName =
    ".rela.plt";  // Use Rela struct.
constexpr absl::string_view kElfPLTSectionName = ".plt";
constexpr absl::string_view kElfDynSymSectionName = ".dynsym";

constexpr int kX86PLTCodeSize = 0x10;  // Size of one x86 PLT function in bytes.
constexpr int kARMPLTCodeSize = 0xc;
constexpr int kAARCH64PLTCodeSize = 0x10;

constexpr int kX86PLT0Size = 0x10;  // Size of the special PLT0 entry.
constexpr int kARMPLT0Size = 0x14;
constexpr int kAARCH64PLT0Size = 0x20;

// Suffix for PLT functions when it needs to be explicitly identified as such.
constexpr absl::string_view kPLTFunctionSuffix = "@plt";

}  // namespace

namespace util {

template <class ElfArch>
class ElfReaderImpl;

// 32-bit and 64-bit ELF files are processed exactly the same, except
// for various field sizes. Elf32 and Elf64 encompass all of the
// differences between the two formats, and all format-specific code
// in this file is templated on one of them.
class Elf32 {
 public:
  typedef Elf32_Chdr Chdr;
  typedef Elf32_Ehdr Ehdr;
  typedef Elf32_Shdr Shdr;
  typedef Elf32_Phdr Phdr;
  typedef Elf32_Word Word;
  typedef Elf32_Sym Sym;
  typedef Elf32_Rel Rel;
  typedef Elf32_Rela Rela;

  // What should be in the EI_CLASS header.
  static constexpr int kElfClass = ELFCLASS32;

  // Given a symbol pointer, return the binding type (eg STB_WEAK).
  static char Bind(const Elf32_Sym* sym) { return ELF32_ST_BIND(sym->st_info); }
  // Given a symbol pointer, return the symbol type (eg STT_FUNC).
  static char Type(const Elf32_Sym* sym) { return ELF32_ST_TYPE(sym->st_info); }

  // Extract the symbol index from the r_info field of a relocation.
  static int r_sym(const Elf32_Word r_info) { return ELF32_R_SYM(r_info); }
};

class Elf64 {
 public:
  typedef Elf64_Chdr Chdr;
  typedef Elf64_Ehdr Ehdr;
  typedef Elf64_Shdr Shdr;
  typedef Elf64_Phdr Phdr;
  typedef Elf64_Word Word;
  typedef Elf64_Sym Sym;
  typedef Elf64_Rel Rel;
  typedef Elf64_Rela Rela;

  // What should be in the EI_CLASS header.
  static constexpr int kElfClass = ELFCLASS64;

  static char Bind(const Elf64_Sym* sym) { return ELF64_ST_BIND(sym->st_info); }
  static char Type(const Elf64_Sym* sym) { return ELF64_ST_TYPE(sym->st_info); }
  static int r_sym(const Elf64_Xword r_info) { return ELF64_R_SYM(r_info); }
};

// ElfSectionReader mmaps a section of an ELF file ("section" is ELF
// terminology). The ElfReaderImpl object providing the section header
// must exist for the lifetime of this object.
//
// The motivation for mmapping individual sections of the file is that
// many Google executables are large enough when unstripped that we
// have to worry about running out of virtual address space.
//
// For compressed sections we have no choice but to allocate memory.
template <class ElfArch>
class ElfSectionReader {
 public:
  // Any single section over 2GiB is impossible (on x86_64) when
  // -mcmodel=medium (default).
  // For now assume that any section larger than 16GiB is bogus.
  // On 32-bit systems, assume anything over 3GiB is bogus.
  static constexpr size_t kSectionSizeMax = sizeof(size_t) == 4
                                                ? static_cast<size_t>(3) << 30
                                                : static_cast<size_t>(16) << 30;

  // Create a reader when the whole file is already mmapped.
  ElfSectionReader(const absl::string_view name, const absl::string_view path,
                   size_t off, void* whole_file, size_t whole_file_size,
                   const typename ElfArch::Shdr& section_header)
      : delete_contents_(false),
        read_contents_(true),
        contents_aligned_(nullptr),
        contents_(nullptr),
        size_aligned_(0),
        section_size_(0),
        header_(section_header) {
    const size_t contents_start = off + header_.sh_offset;
    if (contents_start > whole_file_size) {
      LOG(ERROR) << "Bogus section offset " << contents_start << " in " << path;
      return;  // Leave section_size_ = 0.
    }
    const size_t contents_end = contents_start + header_.sh_size;
    if (contents_end > whole_file_size) {
      LOG(ERROR) << "Bogus section offset+size " << contents_end << " in "
                 << path;
      return;  // Leave section_size_ = 0.
    }
    if (header_.sh_size >= kSectionSizeMax) {
      LOG(ERROR) << "Section " << name << " is too large: " << header_.sh_size
                 << " (limit " << kSectionSizeMax << ")";
      return;  // Leave section_size_ = 0.
    }
    contents_ = reinterpret_cast<char*>(whole_file) + header_.sh_offset + off;
    section_size_ = header_.sh_size;
    CompressionInit(name, false);
  }

  // Create a reader which will read just this one section.
  //
  // The section's contents will be mmapped if read_contents is true, otherwise
  // contents() will always be null.
  ElfSectionReader(const absl::string_view name, const absl::string_view path,
                   int fd, size_t off, bool leak_mmap, bool read_contents,
                   const typename ElfArch::Shdr& section_header)
      : delete_contents_(false),
        read_contents_(read_contents),
        contents_aligned_(nullptr),
        contents_(nullptr),
        size_aligned_(0),
        section_size_(0),
        header_(section_header) {
    // Perform some sanity checks.
    struct stat statbuf;
    if (fstat(fd, &statbuf) == 0) {
      const size_t contents_start = off + header_.sh_offset;
      if (contents_start > statbuf.st_size) {
        LOG(ERROR) << "Bogus section offset " << contents_start << " in "
                   << path;
        return;  // Leave section_size_ = 0.
      }
      const size_t contents_end = contents_start + header_.sh_size;
      if (contents_end > statbuf.st_size) {
        if (header_.sh_type != SHT_NOBITS) {
          LOG(ERROR) << "Section offset+size " << contents_end
                     << " is beyond end of file " << statbuf.st_size << " in "
                     << path;
        }
        return;  // Leave section_size_ = 0.
      }
    } else {
      LOG(ERROR) << "Unale to fstat " << fd << " (" << path << ") "
                 << strerror(errno);
      return;  // Leave section_size_ = 0.
    }

    // Back up to the beginning of the page we're interested in.
    const size_t additional = (off + header_.sh_offset) % getpagesize();
    const size_t offset_aligned = off + header_.sh_offset - additional;
    section_size_ = header_.sh_size;
    size_aligned_ = section_size_ + additional;
    // If the section has been stripped or is empty, do not attempt
    // to process its contents.
    if (is_empty()) return;

    if (header_.sh_size >= kSectionSizeMax) {
      LOG(ERROR) << "Section " << name << " is too large: " << header_.sh_size
                 << " (limit " << kSectionSizeMax << ")";
      return;
    }
    if (!read_contents) {
      return;
    }
    contents_aligned_ = mmap(nullptr, size_aligned_, PROT_READ, MAP_PRIVATE, fd,
                             offset_aligned);
    if (contents_aligned_ == MAP_FAILED)
      PLOG(FATAL) << "Could not mmap " << path << " with size " << size_aligned_
                  << " at offset " << offset_aligned;

    AnnotateVMAName(contents_aligned_, size_aligned_);

    // Set where the offset really should begin.
    contents_ = reinterpret_cast<char*>(contents_aligned_) +
                (header_.sh_offset + off - offset_aligned);

    CompressionInit(name, leak_mmap);
    if (leak_mmap) {
      contents_aligned_ = nullptr;
    }
  }

  // This type is neither copyable nor movable.
  ElfSectionReader(const ElfSectionReader&) = delete;
  ElfSectionReader& operator=(const ElfSectionReader&) = delete;

  void CompressionInit(const absl::string_view name, bool leak_mmap) {
    // Check for and handle any compressed contents.
    if (header_.sh_flags & SHF_COMPRESSED) {
      if (leak_mmap) {
        CHECK_NE(header_.sh_type, SHT_STRTAB)
            << "Can't handle compressed STRTAB with leak_mmap==true";
      }
      if (!DecompressZlibContents()) {
        LOG(ERROR) << "Unable to decompress section " << name;
        // Pretend the section is empty. Subsequent attempts to read it
        // will fail.
        if (delete_contents_) {
          ::operator delete[](contents_, section_size_);
          contents_ = nullptr;
          delete_contents_ = false;
        }
        section_size_ = 0;
      } else if (contents_aligned_ != nullptr) {
        PCHECK(munmap(contents_aligned_, size_aligned_) != -1);
        contents_aligned_ = nullptr;
      }
    }
    // TODO: Add support for proposed elf-section flag
    // "SHF_COMPRESS".
  }

  ~ElfSectionReader() {
    if (contents_aligned_ != nullptr) {
      PCHECK(munmap(contents_aligned_, size_aligned_) != -1);
    }
    if (delete_contents_) {
      ::operator delete[](contents_, section_size_);
    }
  }

  // Return the section header for this section.
  typename ElfArch::Shdr const& header() const { return header_; }

  // Is this section reported to be empty according to its header?
  bool is_empty() const {
    return header().sh_type == SHT_NOBITS || header().sh_size == 0;
  }

  // Return memory at the given offset within this section.
  const char* GetOffset(typename ElfArch::Word offset) const {
    if (contents_ == nullptr || offset >= section_size_) return nullptr;
    return contents_ + offset;
  }

  const char* contents() const { return contents_; }
  size_t section_size() const { return section_size_; }

  // Return true if this reader was created with read_contents requested.
  bool read_contents() const { return read_contents_; }

 private:
  bool DecompressZlibContents() {
    typename ElfArch::Chdr chdr;
    if (section_size_ < sizeof(chdr)) {
      LOG(ERROR) << "Unexpected section size: " << section_size_
                 << " < sizeof(ElfW(Chdr)) == " << sizeof(chdr);
      return false;
    }
    memcpy(&chdr, contents_, sizeof(chdr));
    if (absl::endian::native != absl::endian::little) {
      LOG(ERROR) << "Cross-endian decoding of zlib-gabi not yet implemented";
      return false;
    }
    auto* data_field = reinterpret_cast<const Bytef*>(contents_) + sizeof(chdr);
    auto data_length = section_size_ - sizeof(chdr);
    uLongf uncompressed_size = chdr.ch_size;

    if (chdr.ch_size >= kSectionSizeMax) {
      LOG(ERROR) << "Decompressed section size " << chdr.ch_size
                 << " is too large (limit " << kSectionSizeMax << ")";
      return false;
    }
    // Avoid resource exhaustion from highly compressed bogus data.
    // 100x compression ratio is extremely high for legitimate ELF sections.
    if (data_length > 0 && chdr.ch_size / 100 > data_length) {
      LOG(ERROR) << "Bogus compression ratio: decompressed=" << chdr.ch_size
                 << ", compressed=" << data_length;
      return false;
    }

    contents_ = new (std::nothrow) char[chdr.ch_size];
    if (contents_ == nullptr) {
      LOG(ERROR) << "Failed to allocate memory for decompressed section ("
                 << chdr.ch_size << " bytes)";
      return false;
    }
    section_size_ = chdr.ch_size;
    delete_contents_ = true;

    if (chdr.ch_type == ELFCOMPRESS_ZLIB) {
      int rv = uncompress(reinterpret_cast<Bytef*>(contents_),
                          &uncompressed_size, data_field, data_length);
      if (rv != Z_OK) {
        LOG(ERROR) << "Could not decompress section: " << rv;
        return false;
      }
    } else {
      LOG(ERROR) << "Unexpected chdr.ch_type: " << chdr.ch_type;
      return false;
    }

    if (uncompressed_size != section_size_) {
      LOG(ERROR) << "Unexpected decompressed size: " << uncompressed_size
                 << " vs. " << section_size_;
      return false;
    }
    return true;
  }

  // Should we delete contents_?
  bool delete_contents_;
  // Was this reader created with contents requested or not?
  const bool read_contents_;
  // Section contents mmapped and page-aligned. May be nullptr.
  void* contents_aligned_;
  // contents as usable by the client. For non-compressed sections,
  // pointer within contents_aligned_ to where the section data
  // begins; for compressed sections, pointer to the decompressed
  // data.
  char* contents_;
  // size of contents_aligned_ (needed for munmap).
  size_t size_aligned_;

  // Size of contents. Note: for compressed sections may be smaller or larger
  // than header_.sh_size.
  size_t section_size_;
  const typename ElfArch::Shdr header_;
};

// An iterator over symbols in a given section. It handles walking
// through the entries in the specified section and mapping symbol
// entries to their names in the appropriate string table (in
// another section).
template <class ElfArch>
class SymbolIterator {
 public:
  SymbolIterator(ElfReaderImpl<ElfArch>* reader,
                 typename ElfArch::Word section_type)
      : symbol_section_(
            reader->GetSectionByType(section_type, /*read_contents=*/true)),
        string_section_(nullptr),
        num_symbols_in_section_(0),
        symbol_within_section_(0) {
    CHECK(section_type == SHT_SYMTAB || section_type == SHT_DYNSYM);

    // If this section type doesn't exist, or sh_link doesn't pass sanity
    // checks, leave num_symbols_in_section_ as zero, so this iterator is
    // already done().
    if (symbol_section_ == nullptr) {
      // This can happen for fully-stripped fully-static a.out or for a
      // fully-stipped DSO when section_type==SHT_SYMTAB.
      if (section_type == SHT_SYMTAB) {
        // Fully-stripped binaries are uncommon in google3, but do happen
        // in embedded environments. Warning about them is a bit annoying.
        VLOG(1) << "Unable to find SYMTAB section in " << reader->path_;
      } else {
        LOG(INFO) << "Unable to find DYNSYM section in " << reader->path_;
      }
      return;
    }
    const auto& sh_header = symbol_section_->header();
    if (sh_header.sh_link == 0 ||
        sh_header.sh_link >= reader->GetNumSections()) {
      LOG(ERROR) << "Unexpected sh_link: " << sh_header.sh_link;
      return;
    }
    const int expected_sh_entsize = sizeof(typename ElfArch::Sym);
    if (sh_header.sh_entsize != expected_sh_entsize) {
      LOG(ERROR) << "Unexpected sh_entsize: " << sh_header.sh_entsize
                 << " expected: " << expected_sh_entsize;
      return;
    }
    if ((sh_header.sh_size % expected_sh_entsize) != 0) {
      LOG(ERROR) << "Unexpected sh_size " << sh_header.sh_size
                 << " not divisible by: " << expected_sh_entsize;
      return;
    }

    // Symbol sections have sh_link set to the section number of
    // the string section containing the symbol names.
    string_section_ =
        reader->GetSection(sh_header.sh_link, /*read_contents=*/true);
    if (string_section_ == nullptr) {
      LOG(ERROR) << "Unable to get string section " << sh_header.sh_link;
      return;
    }

    // Section header looks sane.
    num_symbols_in_section_ = sh_header.sh_size / sh_header.sh_entsize;
  }

  // This type is neither copyable nor movable.
  SymbolIterator(const SymbolIterator&) = delete;
  SymbolIterator& operator=(const SymbolIterator&) = delete;

  // Return true iff we have passed all symbols in this section.
  bool done() const {
    return symbol_within_section_ >= num_symbols_in_section_;
  }

  // Advance to the next symbol in this section.
  // REQUIRES: !done()
  void Next() { ++symbol_within_section_; }

  // Return a pointer to the current symbol.
  // REQUIRES: !done()
  const typename ElfArch::Sym* GetSymbol() const {
    CHECK(!done());
    return reinterpret_cast<const typename ElfArch::Sym*>(
        symbol_section_->GetOffset(symbol_within_section_ *
                                   symbol_section_->header().sh_entsize));
  }

  // Return the name of the current symbol, nullptr if it has none.
  // REQUIRES: !done()
  const char* GetSymbolName() const {
    const auto* const sym = GetSymbol();
    if (sym == nullptr) return nullptr;
    const int name_offset = sym->st_name;
    if (name_offset == 0) return nullptr;
    return string_section_->GetOffset(name_offset);
  }

  int GetCurrentSymbolIndex() const { return symbol_within_section_; }

 private:
  const ElfSectionReader<ElfArch>* const symbol_section_;
  const ElfSectionReader<ElfArch>* string_section_;
  int num_symbols_in_section_;
  int symbol_within_section_;
};

// ElfReader loads an ELF binary and can provide information about its
// contents. It is most useful for matching addresses to function
// names. It does not understand debugging formats (eg dwarf2), so it
// can't print line numbers. It takes a path to an elf file and a
// readable file descriptor for that file, which it does not assume
// ownership of.
template <class ElfArch>
class ElfReaderImpl {
 public:
  explicit ElfReaderImpl(const absl::string_view path, int fd, size_t off,
                         bool leak_strtabs, void* whole_file,
                         size_t whole_file_size)
      : path_(path),
        fd_(fd),
        off_(off),
        leak_strtabs_(leak_strtabs),
        whole_file_(whole_file),  // May be null.
        whole_file_size_(whole_file_size),
        is_dwp_(absl::EndsWith(path, ".dwp")),
        opd_section_(nullptr),
        base_for_text_(0),
        plts_supported_(false),
        plt_code_size_(0),
        plt0_size_(0),
        plt_shndx_(-1),
        visited_relocation_entries_(false) {
    CHECK_GE(fd_, 0);
    std::string error;
    CHECK(IsArchElfFile(fd, off, &error)) << " Could not parse file: " << error;
    if (!ParseHeaders(fd, path)) {
      LOG(ERROR) << "Failed to parse headers for " << path;
      return;
    }
    // Currently we need some extra information for PowerPC64 binaries
    // including a way to read the .opd section for function descriptors and a
    // way to find the linked base for function symbols.
    if (header_.e_machine == EM_PPC64) {
      // "opd_section_" must always be checked for nullptr before use.
      opd_section_ = GetSectionInfoByName(".opd", &opd_info_);
      for (int k = 1, num_sections = GetNumSections(); k < num_sections; ++k) {
        const char* name = GetSectionName(section_headers_[k].sh_name);
        if (name == nullptr) {
          LOG(ERROR) << "NULL section name for section " << k << " in " << path;
          return;
        }
        if (strncmp(name, ".text", strlen(".text")) == 0) {
          base_for_text_ =
              section_headers_[k].sh_addr - section_headers_[k].sh_offset;
          break;
        }
      }
    }
    // Turn on PLTs.
    if (header_.e_machine == EM_386 || header_.e_machine == EM_X86_64) {
      plt_code_size_ = kX86PLTCodeSize;
      plt0_size_ = kX86PLT0Size;
      plts_supported_ = true;
    } else if (header_.e_machine == EM_ARM) {
      plt_code_size_ = kARMPLTCodeSize;
      plt0_size_ = kARMPLT0Size;
      plts_supported_ = true;
    } else if (header_.e_machine == EM_AARCH64) {
      plt_code_size_ = kAARCH64PLTCodeSize;
      plt0_size_ = kAARCH64PLT0Size;
      plts_supported_ = true;
    }
  }

  // This type is neither copyable nor movable.
  ElfReaderImpl(const ElfReaderImpl&) = delete;
  ElfReaderImpl& operator=(const ElfReaderImpl&) = delete;

  int FileType() { return header_.e_type; }

  // Examine the headers of the file and return whether the file looks
  // like an ELF file for this architecture. Takes an already-open
  // file descriptor for the candidate file, reading in the prologue
  // to see if the ELF file appears to match the current
  // architecture. If error is non-nullptr, it will be set with a reason
  // in case of failure.
  static bool IsArchElfFile(int fd, size_t off, std::string* error) {
    absl::string_view error_out;
    absl::Cleanup cleanup = [&] {
      if (error != nullptr) {
        *error = error_out;
      }
    };
    typename ElfArch::Ehdr header;
    if (TEMP_FAILURE_RETRY(pread(fd, &header, sizeof(header), off)) !=
        sizeof(header)) {
      error_out = "Could not read header";
      return false;
    }

    if (memcmp(header.e_ident, ELFMAG, SELFMAG) != 0) {
      error_out = "Missing ELF magic";
      return false;
    }

    if (header.e_ident[EI_CLASS] != ElfArch::kElfClass) {
      error_out = "Different word size";
      return false;
    }

    absl::endian endianness;
    if (header.e_ident[EI_DATA] == ELFDATA2LSB) {
      endianness = absl::endian::little;
    } else if (header.e_ident[EI_DATA] == ELFDATA2MSB) {
      endianness = absl::endian::big;
    } else {
      error_out = "Unknown endianness";
      return false;
    }
    if (endianness != absl::endian::native) {
      error_out = "Different byte order";
      return false;
    }
    if (header.e_ident[EI_VERSION] != EV_CURRENT) {
      error_out = "Wrong version";
      return false;
    }
    if ((header.e_phoff == 0) ^ (header.e_phnum == 0)) {
      error_out = "Invalid phoff / phnum";
      return false;
    }
    if (header.e_phoff < 0 ||
        (header.e_phoff != 0 && header.e_phoff < sizeof(header))) {
      error_out = "Invalid phoff";
      return false;
    }
    if (header.e_phentsize == 0) {
      if (header.e_phnum != 0) {
        error_out = "Invalid phnum / phentsize";
        return false;
      }
    } else if (header.e_phentsize != sizeof(typename ElfArch::Phdr)) {
      error_out = "Invalid phentsize";
      return false;
    }
    if ((header.e_shoff == 0) ^ (header.e_shnum == 0)) {
      error_out = "Invalid shoff / shnum";
      return false;
    }
    if (header.e_shoff < 0 ||
        (header.e_shoff != 0 && header.e_shoff < sizeof(header))) {
      error_out = "Invalid shoff";
      return false;
    }
    if (header.e_shentsize == 0) {
      if (header.e_shnum != 0) {
        error_out = "Invalid shnum / shentsize";
        return false;
      }
    } else if (header.e_shentsize != sizeof(typename ElfArch::Shdr)) {
      error_out = "Invalid shentsize";
      return false;
    }
    struct stat stat;
    if (TEMP_FAILURE_RETRY(fstat(fd, &stat)) != 0) {
      error_out = "Could not fstat fd";
      return false;
    }
    if (header.e_phoff >= stat.st_size || header.e_shoff >= stat.st_size) {
      error_out = "File too small or invalid phoff / shoff";
      return false;
    }
    if (header.e_phnum > (stat.st_size - header.e_phoff - off) /
                             sizeof(typename ElfArch::Phdr)) {
      error_out = "File too small or invalid phnum";
      return false;
    }
    if (header.e_shnum > (stat.st_size - header.e_shoff - off) /
                             sizeof(typename ElfArch::Shdr)) {
      error_out = "File too small or invalid phnum";
      return false;
    }
    return true;
  }

  // Return true if we can use this symbol in Address-to-Symbol map.
  bool CanUseSymbol(const typename ElfArch::Sym* sym) {
    const char type = ElfArch::Type(sym);
    // For now we only save FUNC and NOTYPE symbols. For now we just
    // care about functions, but some functions written in assembler
    // don't have a proper ELF type attached to them, so we store
    // NOTYPE symbols as well. The remaining significant type is
    // OBJECT (eg global variables), which represent about 25% of
    // the symbols in a typical google3 binary.
    if (type != STT_FUNC && type != STT_NOTYPE) {
      return false;
    }

    // Most symbols with st_size=0 are not really functions, but rather
    // labels. There are a few exceptions -- most notably glibc's __restore_rt
    // -- which are probably bugs (a missing .size directive in hand-coded
    // assembly), but which we still need to handle properly. Fortunately, all
    // these examples seem to have STT_FUNC type, whereas real throwaway
    // symbols have STT_NOTYPE type.
    if (type != STT_FUNC && sym->st_size == 0) {
      return false;
    }

    return true;
  }

  // Iterate over the symbols in a section, either SHT_DYNSYM or SHT_SYMTAB.
  // Add all symbols to the given SymbolMap. Returns true if any symbols were
  // found.
  bool GetSymbolPositions(internal::SymbolMapSink* symbols,
                          typename ElfArch::Word section_type,
                          uint64_t mem_offset, uint64_t file_offset) {
    // This map is used to filter out "nested" functions.
    // See comment below.
    AddrToSymMap addr_to_sym_map;
    const int num_sections = GetNumSections();
    for (SymbolIterator<ElfArch> it(this, section_type); !it.done();
         it.Next()) {
      if (it.GetSymbolName() == nullptr) continue;
      const typename ElfArch::Sym* sym = it.GetSymbol();
      if (CanUseSymbol(sym)) {
        const int sec = sym->st_shndx;

        // We don't support special section indices. The most common
        // is SHN_ABS, for absolute symbols used deep in the bowels of
        // glibc. Also ignore any undefined symbols.
        if (sec == SHN_UNDEF ||
            (sec >= SHN_LORESERVE && sec <= SHN_HIRESERVE)) {
          continue;
        }
        if (sec >= num_sections) {
          LOG(ERROR) << "Ignoring symbol @"
                     << reinterpret_cast<void*>(sym->st_value)
                     << " due to bogus section index " << sec;
          continue;
        }

        const typename ElfArch::Shdr& hdr = section_headers_[sec];

        // Adjust for difference between where we expected to mmap
        // this section, and where it was actually mmapped.
        const uint64_t expected_base = hdr.sh_addr - hdr.sh_offset;
        const uint64_t real_base = mem_offset - (file_offset - off_);
        const uint64_t adjust = real_base - expected_base;

        uint64_t start = sym->st_value + adjust;

        // Adjust function symbols for PowerPC64 by dereferencing and adjusting
        // the function descriptor to get the function address.
        if (header_.e_machine == EM_PPC64 && ElfArch::Type(sym) == STT_FUNC) {
          const uint64_t opd_addr =
              AdjustPPC64FunctionDescriptorSymbolValue(sym->st_value);
          // Only adjust the returned value if the function address was found.
          if (opd_addr != sym->st_value) {
            const int64_t adjust_function_symbols = real_base - base_for_text_;
            start = opd_addr + adjust_function_symbols;
          }
        }

        addr_to_sym_map.push_back(std::make_pair(start, sym));
      }
    }
    std::sort(addr_to_sym_map.begin(), addr_to_sym_map.end(), &AddrToSymSorter);
    addr_to_sym_map.erase(std::unique(addr_to_sym_map.begin(),
                                      addr_to_sym_map.end(), &AddrToSymEquals),
                          addr_to_sym_map.end());

    // Squeeze out any "nested functions".
    // Nested functions are not allowed in C, but libc plays tricks.
    //
    // For example, here is disassembly of /lib64/tls/libc-2.3.5.so:
    //   0x00000000000aa380 <read+0>:             cmpl   $0x0,0x2781b9(%rip)
    //   0x00000000000aa387 <read+7>:             jne    0xaa39b <read+27>
    //   0x00000000000aa389 <__read_nocancel+0>:  mov    $0x0,%rax
    //   0x00000000000aa390 <__read_nocancel+7>:  syscall
    //   0x00000000000aa392 <__read_nocancel+9>:  cmp $0xfffffffffffff001,%rax
    //   0x00000000000aa398 <__read_nocancel+15>: jae    0xaa3ef <read+111>
    //   0x00000000000aa39a <__read_nocancel+17>: retq
    //   0x00000000000aa39b <read+27>:            sub    $0x28,%rsp
    //   0x00000000000aa39f <read+31>:            mov    %rdi,0x8(%rsp)
    //   ...
    // Without removing __read_nocancel, symbolizer will return nullptr
    // given e.g. 0xaa39f (because the lower bound is __read_nocancel,
    // but 0xaa39f is beyond its end.
    if (addr_to_sym_map.empty()) {
      return false;
    }
    const ElfSectionReader<ElfArch>* const symbol_section =
        this->GetSectionByType(section_type, /*read_contents=*/true);
    const ElfSectionReader<ElfArch>* const string_section = this->GetSection(
        symbol_section->header().sh_link, /*read_contents=*/true);

    typename AddrToSymMap::iterator curr = addr_to_sym_map.begin();
    const char* const string_section_end =
        string_section->contents() + string_section->section_size();
    // Always insert the first symbol.
    if (!symbols->AddSymbol(string_section->GetOffset(curr->second->st_name),
                            string_section_end, curr->first,
                            curr->second->st_size)) {
      return false;
    }
    typename AddrToSymMap::iterator prev = curr++;
    for (; curr != addr_to_sym_map.end(); ++curr) {
      const uint64_t prev_addr = prev->first;
      const uint64_t curr_addr = curr->first;
      const typename ElfArch::Sym* const prev_sym = prev->second;
      const typename ElfArch::Sym* const curr_sym = curr->second;
      if (prev_addr + prev_sym->st_size <= curr_addr ||
          // The next condition is true if two symbols overlap like this:
          //
          //   Previous symbol  |----------------------------|
          //   Current symbol     |-------------------------------|
          //
          // These symbols are not found in google3 codebase, but in
          // jdk1.6.0_01_gg1/jre/lib/i386/server/libjvm.so.
          //
          // 0619e040 00000046 t CardTableModRefBS::write_region_work()
          // 0619e070 00000046 t CardTableModRefBS::write_ref_array_work()
          //
          // We allow overlapped symbols rather than ignore these.
          // Due to the way SymbolMap::GetSymbolAtPosition() works,
          // lookup for any address in [curr_addr, curr_addr + its size)
          // (e.g. 0619e071) will produce the current symbol,
          // which is the desired outcome.
          prev_addr + prev_sym->st_size < curr_addr + curr_sym->st_size) {
        const char* name = string_section->GetOffset(curr_sym->st_name);
        if (!symbols->AddSymbol(name, string_section_end, curr_addr,
                                curr_sym->st_size)) {
          return false;
        }
        prev = curr;
      } else {
        // Current symbol is "nested" inside previous one like this:
        //
        //   Previous symbol  |----------------------------|
        //   Current symbol     |---------------------|
        //
        // This happens within glibc, e.g. __read_nocancel is nested
        // "inside" __read. Ignore "inner" symbol.
        DCHECK_LE(curr_addr + curr_sym->st_size, prev_addr + prev_sym->st_size);
      }
    }
    return true;
  }

  void VisitSymbols(typename ElfArch::Word section_type,
                    ElfReader::SymbolSink* sink) {
    const bool process_dynsyms = absl::GetFlag(FLAGS_elfreader_process_dynsyms);
    for (SymbolIterator<ElfArch> it(this, section_type); !it.done();
         it.Next()) {
      const char* name = it.GetSymbolName();
      if (!name) continue;
      const typename ElfArch::Sym* sym = it.GetSymbol();

      // Add a PLT symbol in addition to the main undefined symbol.
      // Only do this for SHT_DYNSYM, because PLT symbols are dynamic.
      const int symbol_index = it.GetCurrentSymbolIndex();
      // TODO: Can be removed once all Java code is using the
      // Google3 launcher.
      if (!process_dynsyms) {
        // Do not perform PLT function processing for JVM code. We need to
        // avoid processing PLT functions in Java as it causes memory
        // fragmentation in malloc, which is fixed in tcmalloc - and if the
        // Google3 launcher is used the JVM will then use tcmalloc. b/13735638
      } else if (section_type == SHT_DYNSYM &&
                 symbol_index < symbols_plt_offsets_.size() &&
                 symbols_plt_offsets_[symbol_index] != 0) {
        const std::string plt_name = absl::StrCat(name, kPLTFunctionSuffix);
        std::string& plt_name_ref = plt_function_names_[symbol_index];

        if (plt_name_ref.empty()) {
          plt_name_ref = plt_name;
        } else if (plt_name_ref != plt_name) {
          LOG(WARNING) << "Current PLT name " << plt_name
                       << " does not match previously visited PLT name "
                       << plt_name_ref << " for dynamic symbol with index "
                       << symbol_index;
        }
        DCHECK_GE(plt_shndx_, 0);
        if (!sink->filter ||
            sink->filter(plt_name_ref.c_str(),
                         symbols_plt_offsets_[symbol_index], plt_code_size_,
                         ElfArch::Bind(sym), ElfArch::Type(sym), plt_shndx_)) {
          sink->AddSymbol(plt_name_ref.c_str(),
                          symbols_plt_offsets_[symbol_index], plt_code_size_,
                          ElfArch::Bind(sym), ElfArch::Type(sym), plt_shndx_);
        }
      }
      if (!sink->filter ||
          sink->filter(name, sym->st_value, sym->st_size, ElfArch::Bind(sym),
                       ElfArch::Type(sym), sym->st_shndx)) {
        typename ElfArch::Sym symbol = *sym;
        AdjustSymbolValue(&symbol);
        sink->AddSymbol(name, symbol.st_value, symbol.st_size,
                        ElfArch::Bind(sym), ElfArch::Type(sym), sym->st_shndx);
      }
    }
  }

  void VisitRelocationEntries() {
    if (visited_relocation_entries_) {
      return;
    }
    visited_relocation_entries_ = true;

    if (!plts_supported_) {
      return;
    }
    // First determine if PLTs exist. If not, then there is nothing to do.
    plt_shndx_ = GetSectionIndexByName(kElfPLTSectionName);
    if (plt_shndx_ < 0) {
      return;
    }

    ElfReader::SectionInfo plt_section_info{};
    GetSectionInfoByIndex(plt_shndx_, &plt_section_info,
                          /*read_contents=*/false);
    if (plt_section_info.size == 0 && plt_section_info.offset == 0) {
      // Section not found.
      return;
    }
    if (plt_section_info.size == 0) {
      LOG(ERROR) << "Section " << kElfPLTSectionName << " is empty.";
      return;
    }

    // The PLTs could be referenced by either a Rel or Rela (Rel with Addend)
    // section.
    ElfReader::SectionInfo rel_section_info;
    ElfReader::SectionInfo rela_section_info;
    const char* rel_section =
        GetSectionInfoByName(kElfPLTRelSectionName, &rel_section_info);
    const char* rela_section =
        GetSectionInfoByName(kElfPLTRelaSectionName, &rela_section_info);

    const typename ElfArch::Rel* rel =
        reinterpret_cast<const typename ElfArch::Rel*>(rel_section);
    const typename ElfArch::Rela* rela =
        reinterpret_cast<const typename ElfArch::Rela*>(rela_section);

    if (!rel_section && !rela_section) {
      LOG(ERROR) << "Could not find either " << kElfPLTRelSectionName << " or "
                 << kElfPLTRelaSectionName << " sections.";
      return;
    }

    if (rel_section && rela_section) {
      LOG(WARNING) << "Both " << kElfPLTRelSectionName << " and "
                   << kElfPLTRelaSectionName << " sections exist, using "
                   << kElfPLTRelSectionName << " by default.";
    }

    // Use either Rel or Rela section, depending on which one exists.
    size_t section_size =
        rel_section ? rel_section_info.size : rela_section_info.size;
    size_t entry_size = rel_section ? sizeof(typename ElfArch::Rel)
                                    : sizeof(typename ElfArch::Rela);

    // Determine the number of entries in the dynamic symbol table.
    ElfReader::SectionInfo dynsym_section_info;
    const char* dynsym_section =
        GetSectionInfoByName(kElfDynSymSectionName, &dynsym_section_info);
    // The dynsym section might not exist, or it might be empty. In either case
    // there is nothing to be done so return.
    if (!dynsym_section || dynsym_section_info.size == 0) {
      LOG(WARNING) << "Could not find section " << kElfDynSymSectionName
                   << " in file containing relocation and PLT info.";
      return;
    }
    // This CHECK avoids a potential division by zero.
    CHECK_NE(dynsym_section_info.entsize, 0)
        << kElfDynSymSectionName << " exists but has entsize=0.";
    size_t num_dynamic_symbols =
        dynsym_section_info.size / dynsym_section_info.entsize;
    symbols_plt_offsets_.resize(num_dynamic_symbols, 0);

    // TODO: Can be removed once all Java code is using the
    // Google3 launcher.
    if (!absl::GetFlag(FLAGS_elfreader_process_dynsyms)) {
      // Do not perform PLT function processing for JVM code. We need to avoid
      // processing PLT functions in Java as it causes memory fragmentation in
      // malloc, which is fixed in tcmalloc - and if the Google3 launcher is
      // used the JVM will then use tcmalloc. b/13735638
    } else {
      // Make storage room for PLT function name strings.
      plt_function_names_.resize(num_dynamic_symbols);
    }

    for (size_t i = 0; i < section_size / entry_size; ++i) {
      // Determine symbol index from the |r_info| field.
      int sym_index =
          ElfArch::r_sym(rel_section ? rel[i].r_info : rela[i].r_info);
      if (sym_index >= symbols_plt_offsets_.size()) {
        LOG(ERROR) << "Relocation table references symbol with index "
                   << sym_index << " but only " << symbols_plt_offsets_.size()
                   << " dynamic symbols exist.";
        continue;
      }
      symbols_plt_offsets_[sym_index] =
          plt_section_info.addr + plt0_size_ + i * plt_code_size_;
    }
  }

  // Return an ElfSectionReader for the first section of the given
  // type by iterating through all section headers. Returns nullptr if
  // the section type is not found.
  const ElfSectionReader<ElfArch>* GetSectionByType(
      typename ElfArch::Word section_type, bool read_contents) {
    for (int k = 0, num_sections = GetNumSections(); k < num_sections; ++k) {
      if (section_headers_[k].sh_type == section_type) {
        return GetSection(k, read_contents);
      }
    }
    return nullptr;
  }

  // Return the name of section "shndx".  Returns nullptr if the section
  // is not found.
  const char* GetSectionNameByIndex(int shndx) {
    return GetSectionName(section_headers_[shndx].sh_name);
  }

  // Return a pointer to section "shndx", and store the size in
  // "size".  Returns nullptr if the section is not found.
  const char* GetSectionContentsByIndex(int shndx, size_t* size) {
    const ElfSectionReader<ElfArch>* section =
        GetSection(shndx, /*read_contents=*/true);
    if (section != nullptr) {
      *size = section->section_size();
      return section->contents();
    }
    return nullptr;
  }

  // Return the index of the first section of the given type by iterating
  // through all section headers, starting at the specified start_index.
  // Returns -1 if the section type is not found.
  int GetSectionIndexByType(uint32_t type, int start_index) {
    const int num_sections = GetNumSections();
    for (int shndx = start_index; shndx < num_sections; ++shndx) {
      if (section_headers_[shndx].sh_type == type) {
        return shndx;
      }
    }
    return -1;
  }

  // Returns the index of the first section of the given name. Returns -1 if the
  // section name is not found.
  int GetSectionIndexByName(const absl::string_view section_name) {
    for (int k = 0, num_sections = GetNumSections(); k < num_sections; ++k) {
      // When searching for sections in a .dwp file, the sections
      // we're looking for will always be at the end of the section
      // table, so reverse the direction of iteration.
      int shndx = is_dwp_ ? num_sections - k - 1 : k;
      const char* name = GetSectionName(section_headers_[shndx].sh_name);
      if (name != nullptr && section_name == name) {
        return shndx;
      }
    }
    return -1;
  }

  // Return a pointer to the first section of the given name by
  // iterating through all section headers, and store the size in
  // "size".  Returns nullptr if the section name is not found.
  const char* GetSectionContentsByName(const absl::string_view section_name,
                                       size_t* size) {
    const int shndx = GetSectionIndexByName(section_name);
    if (shndx < 0) {
      return nullptr;
    }
    const ElfSectionReader<ElfArch>* section =
        GetSection(shndx, /*read_contents=*/true);
    if (section == nullptr) {
      return nullptr;
    }
    *size = section->section_size();
    return section->contents();
  }

  // This is like GetSectionContentsByName() but it returns a lot of extra
  // information about the section.
  const char* GetSectionInfoByIndex(int shndx, ElfReader::SectionInfo* info,
                                    bool read_contents) {
    const ElfSectionReader<ElfArch>* section = GetSection(shndx, read_contents);
    if (section == nullptr) {
      return nullptr;
    }

    const auto& header = section->header();
    info->type = header.sh_type;
    info->flags = header.sh_flags;
    info->addr = header.sh_addr;
    info->offset = header.sh_offset;
    info->link = header.sh_link;
    info->info = header.sh_info;
    info->addralign = header.sh_addralign;
    info->entsize = header.sh_entsize;

    // Note: section->size() may not be the same as header.sh_size!
    // See b/537132456.
    info->size = section->section_size();
    return section->contents();
  }

  // This is like GetSectionContentsByName() but it returns a lot of extra
  // information about the section.
  const char* GetSectionInfoByName(const absl::string_view section_name,
                                   ElfReader::SectionInfo* info,
                                   bool read_contents = true) {
    const int shndx = GetSectionIndexByName(section_name);
    if (shndx < 0) {
      return nullptr;
    }
    return GetSectionInfoByIndex(shndx, info, read_contents);
  }

  // Return SegmentInfo vector read from program header.
  std::vector<ElfReader::SegmentInfo> GetSegmentInfo() const {
    const int num_phdr = GetNumProgramHeaders();
    std::vector<ElfReader::SegmentInfo> si_vec;
    si_vec.reserve(num_phdr);
    for (int i = 0; i < num_phdr; ++i) {
      auto& info = si_vec.emplace_back();
      const auto& phdr = program_headers_[i];
      info.type = phdr.p_type;
      info.flags = phdr.p_flags;
      info.offset = phdr.p_offset;
      info.vaddr = phdr.p_vaddr;
      info.paddr = phdr.p_paddr;
      info.filesz = phdr.p_filesz;
      info.memsz = phdr.p_memsz;
      info.align = phdr.p_align;
    }
    return si_vec;
  }

  // p_vaddr of the first PT_LOAD segment (if any), or 0 if no PT_LOAD
  // segments are present. This is the address an ELF image was linked
  // (by static linker) to be loaded at. Usually (but not always) 0 for
  // shared libraries and position-independent executables.
  uint64_t VaddrOfFirstLoadSegment() const {
    // Relocatable objects (of type ET_REL) do not have LOAD segments.
    if (header_.e_type == ET_REL) {
      return 0;
    }
    for (const auto& info : GetSegmentInfo()) {
      if (info.type == PT_LOAD) return info.vaddr;
    }
    LOG(ERROR) << "Could not find LOAD from program header: " << path_;
    return 0;
  }

  // According to the LSB ("ELF special sections"), sections with debug
  // info are prefixed by ".debug".  The names are not specified, but they
  // look like ".debug_line", ".debug_info", etc.
  bool HasDebugSections() {
    // Debug sections are likely to be near the end, so reverse the
    // direction of iteration.
    for (int k = GetNumSections() - 1; k >= 0; --k) {
      const char* name = GetSectionName(section_headers_[k].sh_name);
      if (name != nullptr && strncmp(name, ".debug", strlen(".debug")) == 0)
        return true;
    }
    return false;
  }

  uint64_t GetEntryPoint() const {
    return static_cast<uint64_t>(header_.e_entry);
  }

  bool HasSymbolNames() const {
    const int n = GetNumSections();
    for (int k = 0; k < n; ++k) {
      if (section_headers_[k].sh_type == SHT_SYMTAB) {
        int l = section_headers_[k].sh_link;
        return l != SHN_UNDEF && section_headers_[l].sh_type == SHT_STRTAB;
      }
    }
    return false;
  }

  // Return the number of sections.
  uint64_t GetNumSections() const {
    if (HasManySections()) return first_section_header_.sh_size;
    return header_.e_shnum;
  }

  // Return the offset to the section headers
  uint64_t GetSectionHeaderOffset() { return header_.e_shoff; }

 private:
  typedef std::vector<std::pair<uint64_t, const typename ElfArch::Sym*>>
      AddrToSymMap;

  static bool AddrToSymSorter(const typename AddrToSymMap::value_type& lhs,
                              const typename AddrToSymMap::value_type& rhs) {
    return lhs.first < rhs.first;
  }

  static bool AddrToSymEquals(const typename AddrToSymMap::value_type& lhs,
                              const typename AddrToSymMap::value_type& rhs) {
    return lhs.first == rhs.first;
  }

  // Does this ELF file have too many sections to fit in the program header?
  bool HasManySections() const { return header_.e_shnum == SHN_UNDEF; }

  // Return the number of program headers.
  int GetNumProgramHeaders() const {
    if (header_.e_phnum == PN_XNUM) {
      // When the number of program headers is >= PN_XNUM, the actual number is
      // encoded in .sh_info of the first (and only) section header.
      return first_section_header_.sh_info;
    }
    return header_.e_phnum;
  }

  // Return the index of the string table.
  int GetStringTableIndex() const {
    if (HasManySections()) {
      if (header_.e_shstrndx == 0xffff)
        return first_section_header_.sh_link;
      else if (header_.e_shstrndx >= GetNumSections())
        return 0;
    }
    return header_.e_shstrndx;
  }

  // Given an offset into the section header string table, return the
  // section name.
  const char* GetSectionName(typename ElfArch::Word sh_name) {
    const ElfSectionReader<ElfArch>* shstrtab =
        GetSection(GetStringTableIndex(), /*read_contents=*/true);
    if (shstrtab != nullptr && shstrtab->section_size() > sh_name) {
      const char* const name = shstrtab->GetOffset(sh_name);
      // b/173794424 -- make sure there is a terminating NUL in there somewhere.
      if (name == nullptr ||
          memchr(name, '\0', shstrtab->section_size() - sh_name) == nullptr) {
        // Malformed shstrtab.
        return nullptr;
      }
      return name;
    }
    return nullptr;
  }

  // Return an ElfSectionReader for the given section. The reader will
  // be freed when this object is destroyed.
  //
  // If read_contents is true, the section contents will be mapped into memory.
  //
  // If read_contents is false, reader.contents() *may* be always nullptr,
  // unless some other operation has already accessed the contents.
  // IMPORTANT: Do not store the result of GetSection with read_contents=false,
  // it might get deallocated even before its parent ElfReaderImpl is destroyed.
  const ElfSectionReader<ElfArch>* GetSection(int num, bool read_contents) {
    // Section 0 is always SHN_UNDEF.
    if (num < 1 || sections_.size() <= num) {
      VLOG(2) << "section " << num << " is out of bounds";
      return nullptr;
    }

    std::unique_ptr<ElfSectionReader<ElfArch>>& reader = sections_[num];

    if (
        // If this section was not accessed before, or
        reader == nullptr ||
        // it was accessed without contents but contents is now being requested
        (read_contents && !reader->read_contents())) {
      const char* name;
      // Hard-coding the name for the section-name string table prevents
      // infinite recursion.
      const bool is_section_index = num == GetStringTableIndex();
      if (is_section_index) {
        name = ".shstrtab";
      } else {
        name = GetSectionNameByIndex(num);
        if (name == nullptr) {
          LOG(ERROR) << "No name for STRTAB section " << num;
          return nullptr;
        }
      }

      const auto& shdr = section_headers_[num];

      // Sanity check: b/461165219
      if (shdr.sh_addralign == 0) {
        VLOG(2) << "Invalid sh_addralign: 0";
        return nullptr;
      }
      // Allocatable section contents should be properly aligned if the section
      // has any file contents.
      if ((shdr.sh_flags & SHF_ALLOC) && shdr.sh_type != SHT_NOBITS &&
          (shdr.sh_offset & (shdr.sh_addralign - 1)) != 0) {
        VLOG(2) << "Invalid sh_addralign: " << shdr.sh_addralign
                << " sh_offset: " << shdr.sh_offset;
        return nullptr;
      }

      if (whole_file_ == nullptr) {
        // Only STRTAB sections get special "maybe munmap" treatment.
        // There is no point in keeping ".shstrtab", so we exclude it as well.
        const bool leak_mmap = (!is_section_index && shdr.sh_type == SHT_STRTAB)
                                   ? leak_strtabs_
                                   : false;
        reader = std::make_unique<ElfSectionReader<ElfArch>>(
            name, path_, fd_, off_, leak_mmap, read_contents, shdr);
      } else {
        reader = std::make_unique<ElfSectionReader<ElfArch>>(
            name, path_, off_, whole_file_, whole_file_size_, shdr);
      }
    }
    return reader.get();
  }

  bool ValidSectionHeader(const typename ElfArch::Shdr& shdr, size_t file_size,
                          int num_shdr) {
    if (shdr.sh_name == 0 || shdr.sh_name >= file_size) return false;

    // Allow SHT_NOBITS sections to start anywhere (b/181031572)
    // and have any size (e.g. .bss can be larger than file size).
    if (shdr.sh_type != SHT_NOBITS) {
      if (shdr.sh_offset < sizeof(typename ElfArch::Ehdr) ||
          shdr.sh_offset >= file_size) {
        // No section can overlap Ehdr or start past file end.
        return false;
      }

      // .note can be empty.
      // .tm_clone_table is PROGBITS but empty in one of the test binaries.
      // Skip .sh_size check.
      if (shdr.sh_offset + shdr.sh_size > file_size) {
        return false;
      }
    }
    // sh_addralign should be 1 or a power of 2.
    if ((shdr.sh_addralign & (shdr.sh_addralign - 1)) != 0) return false;
    // sh_addralign > 128MiB is not reasonable.
    if (shdr.sh_addralign > (128 << 20)) return false;
    // sh_link should be < num sections.
    if (shdr.sh_link < 0 || shdr.sh_link >= num_shdr) return false;

    // An SHT_SYMTAB section should be at least word-aligned -- we are going
    // to create ElfArch::Sym pointers into it.
    if (shdr.sh_type == SHT_SYMTAB &&
        (shdr.sh_offset & (sizeof(typename ElfArch::Word) - 1)) != 0) {
      return false;
    }

    return true;
  }

  bool ValidSectionHeaders(const std::vector<typename ElfArch::Shdr>& shdrs,
                           size_t file_size, const absl::string_view path) {
    size_t num_shdr = shdrs.size();
    // A core dump with lots of program headers may have only one section,
    // and that section will have non-zero st_info value.
    if (header_.e_type == ET_CORE && num_shdr == 1) return true;

    if (num_shdr <= 1 || num_shdr >= (128 << 10)) return false;

    // First section should be all 0s.
    const char* shdr0_s = reinterpret_cast<const char*>(&shdrs[0]);
    const char* shdr0_e = shdr0_s + sizeof(shdrs[0]);
    if (std::any_of(shdr0_s, shdr0_e, [](const char c) { return c != '\0'; })) {
      return false;
    }

    // Other sections should have a valid name, non-zero and reasonable offset,
    // etc.
    for (int j = 1; j < num_shdr; j++) {
      if (!ValidSectionHeader(shdrs[j], file_size, num_shdr)) {
        LOG(INFO) << "Rejected " << path << " section [" << j << "]";
        return false;
      }
    }
    return true;
  }

  // Parse out the overall header information from the file and assert
  // that it looks sane. This contains information like the magic
  // number and target architecture.
  bool ParseHeadersInternal(int fd, const absl::string_view path) {
    // Read in the global ELF header.
    if (TEMP_FAILURE_RETRY(pread(fd, &header_, sizeof(header_), off_)) !=
        sizeof(header_)) {
      LOG(ERROR) << "Could not read ELF header: " << path;
      return false;
    }

    // Must be an executable, dynamic shared object, relocatable object
    // or a core dump.
    if (header_.e_type != ET_EXEC && header_.e_type != ET_DYN &&
        header_.e_type != ET_REL && header_.e_type != ET_CORE) {
      LOG(ERROR) << "Not an executable, shared object or relocatable object "
                    "file: "
                 << path;
      return false;
    }
    if (header_.e_shoff != 0) {
      // Section header is present.
      if (header_.e_shnum == SHN_UNDEF || header_.e_phnum == PN_XNUM) {
        // The number of sections in the program header is only a 16-bit value.
        // In the event of overflow (greater than SHN_LORESERVE sections),
        // e_shnum will read SHN_UNDEF and the true number of section header
        // table entries is found in the sh_size field of the first section
        // header. See:
        // http://www.sco.com/developers/gabi/2003-12-17/ch4.sheader.html
        //
        // Likewise e_phnum is a uint16_t and the actual size is encoded
        // in .sh_info of the first section header.
        if (TEMP_FAILURE_RETRY(pread(
                fd, &first_section_header_, sizeof(first_section_header_),
                off_ + header_.e_shoff)) != sizeof(first_section_header_)) {
          LOG(ERROR) << "Failed to read first section header: " << path;
          return false;
        }
      }

      // Dynamically allocate enough space to store the section headers
      // and read them out of the file.
      const int num_sections = GetNumSections();
      const int section_headers_size =
          num_sections * sizeof(typename ElfArch::Shdr);
      section_headers_.resize(num_sections);
      if (TEMP_FAILURE_RETRY(
              pread(fd, section_headers_.data(), section_headers_size,
                    off_ + header_.e_shoff)) != section_headers_size) {
        LOG(ERROR) << "Could not read section headers: " << path
                   << " off: " << off_ + header_.e_shoff;
        return false;
      }
      size_t file_size = whole_file_size_;
      if (file_size == 0) {
        // whole_file_size_ is only set when we mmap entire file.
        struct stat st_buf;
        PCHECK(fstat(fd, &st_buf) != -1);
        file_size = st_buf.st_size;
      }
      if (!ValidSectionHeaders(section_headers_, file_size, path)) {
        LOG(ERROR) << "Corrupt / invalid section headers in " << path;
        section_headers_.clear();
        return false;
      }
    }

    // Dynamically allocate enough space to store the program headers
    // and read them out of the file.
    const int program_headers_size =
        GetNumProgramHeaders() * sizeof(typename ElfArch::Phdr);
    program_headers_.resize(GetNumProgramHeaders());
    if (TEMP_FAILURE_RETRY(
            pread(fd, program_headers_.data(), program_headers_size,
                  header_.e_phoff + off_)) != program_headers_size) {
      LOG(ERROR) << "Could not read program headers: " << path
                 << " Continue anyway";
    }

    // Presize the sections array for efficiency.
    sections_.resize(GetNumSections());
    return true;
  }

  bool ParseHeaders(int fd, const absl::string_view path) {
    if (ParseHeadersInternal(fd, path)) return true;
    // Clean up state so subsequent attemts to read sections, etc.
    // don't just crash.
    header_.e_shnum = SHN_UNDEF;
    section_headers_.clear();
    // This is necessary because header_.e_shnum==SHN_UNDEF implies
    // HasManySections()==true.
    first_section_header_ = {};

    header_.e_phnum = 0;
    program_headers_.clear();

    return false;
  }

  // Given the "value" of a function descriptor return the address of the
  // function (i.e. the dereferenced value). Otherwise return "value".
  uint64_t AdjustPPC64FunctionDescriptorSymbolValue(uint64_t value) {
    if (opd_section_ != nullptr && opd_info_.addr <= value &&
        value < opd_info_.addr + opd_info_.size) {
      if (opd_info_.entsize != 0) {
        CHECK_EQ(value, opd_info_.entsize * (value / opd_info_.entsize))
            << "mis-aligned symbol";
      }
      uint64_t offset = value - opd_info_.addr;
      return (*reinterpret_cast<const uint64_t*>(opd_section_ + offset));
    }
    return value;
  }

  void AdjustSymbolValue(typename ElfArch::Sym* sym) {
    switch (header_.e_machine) {
      case EM_ARM:
        // For ARM architecture, if the LSB of the function symbol offset is
        // set, it indicates a Thumb function.  This bit should not be taken
        // literally. Clear it.
        if (ElfArch::Type(sym) == STT_FUNC)
          sym->st_value = AdjustARMThumbSymbolValue(sym->st_value);
        break;
      case EM_386:
        // No adjustment needed for Intel x86 architecture.  However, explicitly
        // define this case as we use it quite often.
        break;
      case EM_PPC64:
        // PowerPC64 currently has function descriptors as part of the ABI.
        // Function symbols need to be adjusted accordingly.
        if (ElfArch::Type(sym) == STT_FUNC)
          sym->st_value =
              AdjustPPC64FunctionDescriptorSymbolValue(sym->st_value);
        break;
      default:
        break;
    }
  }

  friend class SymbolIterator<ElfArch>;

  // The file we're reading.
  const std::string path_;
  // Open file descriptor for path_. Not owned by this object.
  const int fd_;
  // Offset to the mapping (support for dlopen_with_offset)
  const size_t off_;

  // Should we keep symbol names (STRTABs) mmaped after destruction?
  const bool leak_strtabs_ = false;

  // The next two members are non-zero if the whole file was mmaped.
  void* const whole_file_;
  const size_t whole_file_size_;

  // True if this is a .dwp file.
  const bool is_dwp_;

  // The header of the ELF file.
  typename ElfArch::Ehdr header_ = {};

  // The header of the first section.
  // This is used to supplement the ELF file header in some circumstances.
  typename ElfArch::Shdr first_section_header_ = {};

  // Array of GetNumSections() section headers, allocated when we read
  // in the global header.
  std::vector<typename ElfArch::Shdr> section_headers_;

  // Array of GetNumProgramHeaders() program headers, allocated when we read
  // in the global header.
  std::vector<typename ElfArch::Phdr> program_headers_;

  // An array of pointers to ElfSectionReaders. Sections are
  // mmaped as they're needed and not released until this object is
  // destroyed.
  std::vector<std::unique_ptr<ElfSectionReader<ElfArch>>> sections_;

  // For PowerPC64 we need to keep track of function descriptors when looking up
  // values for function symbols values. Function descriptors are kept in the
  // .opd section and are dereferenced to find the function address.
  ElfReader::SectionInfo opd_info_;
  const char* opd_section_;  // Must be checked for nullptr before use.
  int64_t base_for_text_;

  // Read PLT-related sections for the current architecture.
  bool plts_supported_;
  // Code size of each PLT function for the current architecture.
  size_t plt_code_size_;
  // Size of the special first entry in the .plt section that calls the runtime
  // loader resolution routine, and that all other entries jump to when doing
  // lazy symbol binding.
  size_t plt0_size_;

  // The index (shndx) of the .plt section in the ELF file. It is set to -1 if
  // a .plt section is not present.
  int plt_shndx_;

  // Maps a dynamic symbol index to a PLT offset.
  // The vector entry index is the dynamic symbol index.
  std::vector<uint64_t> symbols_plt_offsets_;

  // Container for PLT function name strings. These strings are passed by
  // reference to SymbolSink::AddSymbol() so they need to be stored somewhere.
  std::vector<std::string> plt_function_names_;

  bool visited_relocation_entries_;
};

template <typename ElfArch>
static bool IsElfFile(const int fd, size_t off) {
  if (fd < 0) return false;
  if (!ElfReaderImpl<ElfArch>::IsArchElfFile(fd, off, nullptr)) {
    // No error message here.  IsElfFile gets called many times.
    return false;
  }
  return true;
}

// Helper function to open the main executable from /proc/self/cmdline.
// Returns the file descriptor on success, or -1 on failure.
static int OpenExeFromCmdline() {
  int fd = TEMP_FAILURE_RETRY(open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC));
  if (fd == -1) return -1;

  char buf[4096];
  ssize_t n = TEMP_FAILURE_RETRY(read(fd, buf, sizeof(buf) - 1));
  close(fd);

  if (n <= 0 || buf[0] == '\0') return -1;

  buf[n] = '\0';
  // /proc/self/cmdline is a list of null-terminated strings.
  // The first string is exactly the original argv[0].
  return TEMP_FAILURE_RETRY(open(buf, O_RDONLY | O_CLOEXEC));
}

ElfReader::ElfReader(const absl::string_view path, size_t off,
                     bool leak_strtabs, bool mmap_entire_file)
    : path_(path),
      fd_(-1),
      leak_strtabs_(leak_strtabs),
      off_(off),
      impl32_(nullptr),
      impl64_(nullptr) {
  // linux 2.6.XX kernel can show deleted files like this:
  //   /var/run/nscd/dbYLJYaE (deleted)
  // and the kernel-supplied vdso and vsyscall mappings like this:
  //   [vdso]
  //   [vsyscall]
  if (absl::EndsWith(path, " (deleted)")) return;
  if (path == "[vdso]") return;
  if (path == "[vsyscall]") return;

  fd_ = TEMP_FAILURE_RETRY(open(path_.c_str(), O_RDONLY));
  std::string proc_self_exe = "/proc/self/exe";
  testing::testvalue::Adjust("elf_reader_proc_self_exe", &proc_self_exe);
  if (fd_ == -1 && path == proc_self_exe) {
    // /proc/self/exe may be inaccessible (due to setuid, broken symlinks in
    // bind-mounted /proc inside chroot/NSJail sandboxes, etc.), so try
    // accessing the binary via argv0. First check base::GetArgv0(), and if
    // unavailable (e.g., in Deploy JAR launchers that omit base::InitGoogle),
    // fall back to reading argv[0] directly from /proc/self/cmdline.
    const char* argv0 = base::GetArgv0();
    if (argv0 != nullptr && *argv0 != '\0') {
      fd_ = TEMP_FAILURE_RETRY(open(argv0, O_RDONLY | O_CLOEXEC));
    }
    if (fd_ == -1) {
      fd_ = OpenExeFromCmdline();
    }
  }
  if (fd_ == -1) {
    // Not ERROR, since this gets called with things like "[heap]".
    PLOG(INFO) << "Could not open " << path_;
  } else if (mmap_entire_file) {
    struct stat statbuf;
    if (fstat(fd_, &statbuf) == 0) {
      whole_file_size_ = statbuf.st_size;
      whole_file_ =
          mmap(nullptr, whole_file_size_, PROT_READ, MAP_PRIVATE, fd_, 0);
      if (whole_file_ == MAP_FAILED) {
        PLOG(ERROR) << "Could not mmap " << path << " with size "
                    << whole_file_size_
                    << ". Will fall back to per-section mmap.";
        whole_file_ = nullptr;
        whole_file_size_ = 0;
      } else {
        AnnotateVMAName(whole_file_, whole_file_size_);
      }
    }
  }
  if (IsElfFile<Elf32>(fd_, off_)) {
    impl32_ = std::make_unique<ElfReaderImpl<Elf32>>(
        path_, fd_, off_, leak_strtabs_, whole_file_, whole_file_size_);
  } else if (IsElfFile<Elf64>(fd_, off_)) {
    impl64_ = std::make_unique<ElfReaderImpl<Elf64>>(
        path_, fd_, off_, leak_strtabs_, whole_file_, whole_file_size_);
  } else {
    VLOG(2) << "not an elf binary: " << path_;
  }
}

ElfReader::~ElfReader() {
  if (fd_ != -1) PCHECK(close(fd_) != -1);
}

bool ElfReader::IsNativeElfFile() const {
  static_assert(sizeof(void*) == 4 || sizeof(void*) == 8,
                "unsupported pointer size");

  switch (sizeof(void*)) {
    case 4:
      return IsElf32File();
    case 8:
      return IsElf64File();
    default:
      abort();  // unreachable
  }
}

bool ElfReader::IsElf32File() const { return impl32_ != nullptr; }

bool ElfReader::IsElf64File() const { return impl64_ != nullptr; }

uint64_t ElfReader::GetEntryPoint() {
  if (IsElf32File()) {
    return GetImpl32()->GetEntryPoint();
  } else if (IsElf64File()) {
    return GetImpl64()->GetEntryPoint();
  } else {
    return 0;
  }
}

void ElfReader::AddSymbols(internal::SymbolMapSink* symbols,
                           uint64_t mem_offset, uint64_t file_offset,
                           uint64_t length) {
  if (fd_ < 0) return;
  // TODO: Actually use the information about file offset and
  // the length of the mapped section. On some machines the data
  // section gets mapped as executable, and we'll end up reading the
  // file twice and getting some of the offsets wrong.

  // Technically, nothing in the ELF specification *requires* the symbols in
  // .dynsym to be a subset of those in .symtab, but in practice, that is
  // always the case except when the entire .symtab has been removed (by
  // stripping the binary). Therefore, we only inspect the .dynsym symbols if
  // there are no usable symbols found in the .symtab section (so that, in the
  // case of stripped binaries, we can at least symbolize any exported
  // functions).
  if (IsElf32File()) {
    auto* const impl = GetImpl32();
    if (impl->GetNumSections() <= 0) return;
    if (!impl->GetSymbolPositions(symbols, SHT_SYMTAB, mem_offset,
                                  file_offset)) {
      if (absl::GetFlag(FLAGS_elfreader_process_dynsyms) &&
          !IsRelocatableFile()) {
        impl->GetSymbolPositions(symbols, SHT_DYNSYM, mem_offset, file_offset);
      }
    }
  } else if (IsElf64File()) {
    auto* const impl = GetImpl64();
    if (impl->GetNumSections() <= 0) return;
    if (!impl->GetSymbolPositions(symbols, SHT_SYMTAB, mem_offset,
                                  file_offset)) {
      if (absl::GetFlag(FLAGS_elfreader_process_dynsyms) &&
          !IsRelocatableFile()) {
        impl->GetSymbolPositions(symbols, SHT_DYNSYM, mem_offset, file_offset);
      }
    }
  } else {
    VLOG(2) << "not an elf binary: " << path_;
  }
}

void ElfReader::VisitSymbols(ElfReader::SymbolSink* sink) {
  if (IsElf32File()) {
    auto* const impl = GetImpl32();
    impl->VisitRelocationEntries();
    impl->VisitSymbols(SHT_SYMTAB, sink);
    if (absl::GetFlag(FLAGS_elfreader_process_dynsyms) &&
        !IsRelocatableFile()) {
      impl->VisitSymbols(SHT_DYNSYM, sink);
    }
  } else if (IsElf64File()) {
    auto* const impl = GetImpl64();
    impl->VisitRelocationEntries();
    impl->VisitSymbols(SHT_SYMTAB, sink);
    if (absl::GetFlag(FLAGS_elfreader_process_dynsyms) &&
        !IsRelocatableFile()) {
      impl->VisitSymbols(SHT_DYNSYM, sink);
    }
  }
}

uint64_t ElfReader::VaddrOfFirstLoadSegment() {
  if (IsElf32File()) {
    return GetImpl32()->VaddrOfFirstLoadSegment();
  } else if (IsElf64File()) {
    return GetImpl64()->VaddrOfFirstLoadSegment();
  } else {
    return 0;
  }
}

std::vector<ElfReader::SegmentInfo> ElfReader::GetSegmentInfo() {
  if (IsElf32File()) {
    return GetImpl32()->GetSegmentInfo();
  } else if (IsElf64File()) {
    return GetImpl64()->GetSegmentInfo();
  } else {
    return {};
  }
}

const char* ElfReader::GetSectionName(int shndx) {
  if (shndx < 0) return nullptr;
  if (IsElf32File()) {
    auto* impl = GetImpl32();
    if (shndx >= impl->GetNumSections()) return nullptr;
    return impl->GetSectionNameByIndex(shndx);
  } else if (IsElf64File()) {
    auto* impl = GetImpl64();
    if (shndx >= impl->GetNumSections()) return nullptr;
    return impl->GetSectionNameByIndex(shndx);
  } else {
    return nullptr;
  }
}

uint64_t ElfReader::GetNumSections() {
  if (IsElf32File()) {
    return GetImpl32()->GetNumSections();
  } else if (IsElf64File()) {
    return GetImpl64()->GetNumSections();
  } else {
    return 0;
  }
}

const char* ElfReader::GetSectionByIndex(int shndx, size_t* size) {
  if (IsElf32File()) {
    return GetImpl32()->GetSectionContentsByIndex(shndx, size);
  } else if (IsElf64File()) {
    return GetImpl64()->GetSectionContentsByIndex(shndx, size);
  } else {
    return nullptr;
  }
}

int ElfReader::GetSectionIndexByType(uint32_t type, int start_index) {
  if (IsElf32File()) {
    return GetImpl32()->GetSectionIndexByType(type, start_index);
  } else if (IsElf64File()) {
    return GetImpl64()->GetSectionIndexByType(type, start_index);
  } else {
    return -1;
  }
}

int ElfReader::GetSectionIndexByName(const absl::string_view section_name) {
  if (IsElf32File()) {
    return GetImpl32()->GetSectionIndexByName(section_name);
  } else if (IsElf64File()) {
    return GetImpl64()->GetSectionIndexByName(section_name);
  } else {
    return -1;
  }
}

const char* ElfReader::GetSectionByName(const absl::string_view section_name,
                                        size_t* size) {
  if (IsElf32File()) {
    return GetImpl32()->GetSectionContentsByName(section_name, size);
  } else if (IsElf64File()) {
    return GetImpl64()->GetSectionContentsByName(section_name, size);
  } else {
    return nullptr;
  }
}

const char* ElfReader::GetSectionInfoByName(
    const absl::string_view section_name, SectionInfo* info) {
  if (IsElf32File()) {
    return GetImpl32()->GetSectionInfoByName(section_name, info, true);
  } else if (IsElf64File()) {
    return GetImpl64()->GetSectionInfoByName(section_name, info, true);
  } else {
    return nullptr;
  }
}

std::optional<ElfReader::SectionInfo> ElfReader::GetSectionInfoByName(
    const absl::string_view section_name) {
  SectionInfo info{};
  if (IsElf32File()) {
    GetImpl32()->GetSectionInfoByName(section_name, &info, false);
  } else if (IsElf64File()) {
    GetImpl64()->GetSectionInfoByName(section_name, &info, false);
  } else {
    VLOG(2) << "not an elf binary: " << path_;
  }
  if (info.offset == 0 && info.type == 0) {
    return std::nullopt;
  }
  return info;
}

const char* ElfReader::GetSectionInfoByIndex(int shndx, SectionInfo* info) {
  if (IsElf32File()) {
    return GetImpl32()->GetSectionInfoByIndex(shndx, info, true);
  } else if (IsElf64File()) {
    return GetImpl64()->GetSectionInfoByIndex(shndx, info, true);
  } else {
    return nullptr;
  }
}

std::optional<ElfReader::SectionInfo> ElfReader::GetSectionInfoByIndex(
    int shndx) {
  SectionInfo info{};
  if (IsElf32File()) {
    GetImpl32()->GetSectionInfoByIndex(shndx, &info, false);
  } else if (IsElf64File()) {
    GetImpl64()->GetSectionInfoByIndex(shndx, &info, false);
  } else {
    VLOG(2) << "not an elf binary: " << path_;
  }
  if (info.offset == 0 && info.type == 0) {
    return std::nullopt;
  }
  return info;
}

std::optional<uint64_t> ElfReader::GetSectionHeaderOffset() {
  if (IsElf32File()) {
    return GetImpl32()->GetSectionHeaderOffset();
  } else if (IsElf64File()) {
    return GetImpl64()->GetSectionHeaderOffset();
  }
  return std::nullopt;
}

// Note: Elf64_Nhdr and Elf32_Nhdr are actually exactly the same, so rather
// than doing dispatch to 32 or 64 bit implementation (which would result in
// repeating identical code), we cheat with ElfW(Nhdr).
std::string ElfReader::GetBuildId() {
  std::vector<std::string> build_ids;
  static constexpr size_t kNoteHeaderSize = sizeof(ElfW(Nhdr));
  static constexpr auto round_up_to_4 = [](size_t sz) { return (sz + 3) & ~3; };

  for (int nindex = GetSectionIndexByType(SHT_NOTE, 0); nindex >= 0;
       nindex = GetSectionIndexByType(SHT_NOTE, nindex + 1)) {
    size_t size;
    const char* c_note = GetSectionByIndex(nindex, &size);
    if (c_note == nullptr) continue;

    const char* c_note_end = c_note + size;
    while (c_note + kNoteHeaderSize < c_note_end) {
      const auto* note = reinterpret_cast<const ElfW(Nhdr)*>(c_note);
      if (kNoteHeaderSize + static_cast<uint64_t>(note->n_namesz) +
              note->n_descsz >
          static_cast<size_t>(c_note_end - c_note)) {
        break;
      }
      // Name immediately follows the note.
      const char* note_name = c_note + kNoteHeaderSize;
      if (kNoteHeaderSize < size && note->n_type == NT_GNU_BUILD_ID &&
          note->n_namesz == 4 && memcmp(note_name, "GNU\0", 4) == 0) {
        if (note->n_descsz <= 64) {
          std::string build_id(static_cast<size_t>(note->n_descsz) * 2, '0');
          const char hexdigits[] = "0123456789abcdef";
          // Note data follows name.
          const char* note_desc = note_name + note->n_namesz;
          for (size_t i = 0; i < note->n_descsz; i++) {
            build_id[i * 2] = hexdigits[(note_desc[i]) >> 4];
            build_id[i * 2 + 1] = hexdigits[note_desc[i] & 0x0f];
          }
          build_ids.push_back(build_id);
        }
      }

      // There could be multiple ELF notes in the .note section.
      // Advance to the next note.
      c_note += kNoteHeaderSize + round_up_to_4(note->n_namesz) +
                round_up_to_4(note->n_descsz);
    }
  }

  switch (build_ids.size()) {
    case 0:
      return "";
    case 1:
      return build_ids[0];
    default:
      // Repeated builds-ids. Complain and ignore them.
      LOG(ERROR) << "Ignoring multiple GNU_BUILD_ID notes: "
                 << absl::StrJoin(build_ids, ", ");
      return "";
  }
}

int ElfReader::FileType() {
  if (IsElf32File()) {
    return GetImpl32()->FileType();
  } else if (IsElf64File()) {
    return GetImpl64()->FileType();
  } else {
    return -1;
  }
}

bool ElfReader::IsDynamicSharedObject() { return FileType() == ET_DYN; }
bool ElfReader::IsExecutableFile() { return FileType() == ET_EXEC; }
bool ElfReader::IsRelocatableFile() { return FileType() == ET_REL; }
bool ElfReader::IsCoreFile() { return FileType() == ET_CORE; }

bool ElfReader::HasSymbolNames() {
  if (IsElf32File()) {
    return GetImpl32()->HasSymbolNames();
  } else if (IsElf64File()) {
    return GetImpl64()->HasSymbolNames();
  } else {
    return false;
  }
}

ElfReaderImpl<Elf32>* ElfReader::GetImpl32() { return impl32_.get(); }

ElfReaderImpl<Elf64>* ElfReader::GetImpl64() { return impl64_.get(); }

// Return true if file is an ELF binary of ElfArch, with unstripped
// debug info (debug_only=true) or symbol table (debug_only=false).
// Otherwise, return false.
template <typename ElfArch>
static bool IsNonStrippedELFBinaryImpl(const absl::string_view path,
                                       const int fd, bool debug_only) {
  if (!ElfReaderImpl<ElfArch>::IsArchElfFile(fd, 0, nullptr)) return false;
  ElfReaderImpl<ElfArch> elf_reader(path, fd, 0, /*leak_strtabs=*/false,
                                    nullptr, 0);
  return debug_only ? elf_reader.HasDebugSections()
                    : (elf_reader.GetSectionByType(
                           SHT_SYMTAB, /*read_contents=*/false) != nullptr);
}

// Helper for the IsNon[Debug]StrippedELFBinary functions.
static bool IsNonStrippedELFBinaryHelper(const std::string& path,
                                         bool debug_only) {
  const int fd = TEMP_FAILURE_RETRY(open(path.c_str(), O_RDONLY));
  if (fd == -1) {
    return false;
  }

  if (IsNonStrippedELFBinaryImpl<Elf32>(path, fd, debug_only) ||
      IsNonStrippedELFBinaryImpl<Elf64>(path, fd, debug_only)) {
    close(fd);
    return true;
  }
  close(fd);
  return false;
}

bool ElfReader::IsNonStrippedELFBinary(const std::string& path) {
  return IsNonStrippedELFBinaryHelper(path, false);
}

bool ElfReader::IsNonDebugStrippedELFBinary(const std::string& path) {
  return IsNonStrippedELFBinaryHelper(path, true);
}

ElfReader::SymbolSink::~SymbolSink() = default;

}  // namespace util
