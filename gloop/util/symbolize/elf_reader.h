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

// ElfReader handles reading in ELF. It can extract symbols from the
// current process, which may be used to symbolize stack traces
// without having to make a potentially dangerous call to fork().
//
// ElfReader dynamically allocates memory, so it is not appropriate to
// use once the address space might be corrupted, such as during
// process death.
//
// ElfReader supports both 32-bit and 64-bit ELF binaries.

#ifndef THIRD_PARTY_GLOOP_UTIL_SYMBOLIZE_ELF_READER_H__
#define THIRD_PARTY_GLOOP_UTIL_SYMBOLIZE_ELF_READER_H__

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "gloop/util/symbolize/symbol_map_sink.h"

namespace util {

class Elf32;
class Elf64;
template <typename ElfArch>
class ElfReaderImpl;

class ElfReader {
 public:
  explicit ElfReader(absl::string_view path, size_t off = 0,
                     bool leak_strtab = false, bool mmap_entire_file = false);

  // This type is neither copyable nor movable.
  ElfReader(const ElfReader&) = delete;
  ElfReader& operator=(const ElfReader&) = delete;

  ~ElfReader();

  // Parse the ELF prologue of this file and return whether it was
  // successfully parsed and matches the word size and byte order of
  // the current process.
  bool IsNativeElfFile() const;

  // Similar to IsNativeElfFile but checks if it's a 32-bit ELF file.
  bool IsElf32File() const;

  // Similar to IsNativeElfFile but checks if it's a 64-bit ELF file.
  bool IsElf64File() const;

  // Return ET_EXEC, ET_DYN, etc.
  int FileType();

  // Checks if it's an ELF file of type ET_DYN (shared object file).
  bool IsDynamicSharedObject();

  // Checks if it's an ELF file of type ET_REL (relocatable file).
  bool IsRelocatableFile();

  // For symmetry with above. Note: PIE binaries answer false.
  bool IsExecutableFile();

  bool IsCoreFile();

  // Returns the entry point of an executable. Will be unrelocated for PIE
  // binaries.
  uint64_t GetEntryPoint();

  // Checks if there is a .strtab (symbol names) section -- i.e. if the ELF
  // file is unstripped. (Note that .dynstr sections, containing the names of
  // exported symbols, must always be present in a dynamically linked ELF
  // binary and do not count for this purpose.)
  // TODO: it is technically possible to strip the useful symbol
  // names (functions) from a binary but not remove the entire .symtab/.strtab
  // sections; strip --keep_fi_symbols will do this, for example. We could
  // instead set a flag if we encounter any symbols whose type is STT_FUNC
  // that have names if this turns out to be a problem.
  bool HasSymbolNames();

  // Add symbols in the given ELF file into the provided SymbolMap,
  // assuming that the file has been loaded into the specified
  // offset.
  //
  // The remaining arguments are typically taken from a
  // ProcMapsIterator (base/sysinfo.h) and describe which portions of
  // the ELF file are mapped into which parts of memory:
  //
  // mem_offset - position at which the segment is mapped into memory
  // file_offset - offset in the file where the mapping begins
  // length - length of the mapped segment
  void AddSymbols(internal::SymbolMapSink* symbols, uint64_t mem_offset,
                  uint64_t file_offset, uint64_t length);

  struct SymbolSink {
    virtual ~SymbolSink();
    virtual void AddSymbol(const char* name, uint64_t address, uint64_t size,
                           int binding, int type, int section) = 0;

    // If "filter" is set, only entries for which it returns true are added.
    std::function<bool(const char* name, uint64_t address, uint64_t size,
                       int binding, int type, int section)>
        filter;
  };

  // Like AddSymbols above, but with no address correction.
  // Processes any SHT_SYMTAB section, followed by any SHT_DYNSYM section.
  void VisitSymbols(SymbolSink* sink);

  // p_vaddr of the first PT_LOAD segment (if any), or 0 if no PT_LOAD
  // segments are present. This is the address an ELF image was linked
  // (by static linker) to be loaded at. Usually (but not always) 0 for
  // shared libraries and position-independent executables.
  uint64_t VaddrOfFirstLoadSegment();

  // Returns the index of the first section of the given type, starting at the
  // specified start_index.  Returns -1 if no section matches the type.
  int GetSectionIndexByType(uint32_t type, int start_index);

  // Returns the index of the first section of the given name. Returns -1 if the
  // section name is not found.
  int GetSectionIndexByName(absl::string_view section_name);

  // Return the name of section "shndx".  Returns nullptr if the section
  // is not found.
  const char* GetSectionName(int shndx);

  // Return the number of sections in the given ELF file.
  uint64_t GetNumSections();

  // Get section "shndx" from the given ELF file.  On success, return
  // the pointer to the section and store the size in "size".
  // On error, return nullptr.  The returned section data is only valid
  // until the ElfReader gets destroyed.
  const char* GetSectionByIndex(int shndx, size_t* size);

  // Get section with "section_name" (ex. ".text", ".symtab") in the
  // given ELF file.  On success, return the pointer to the section
  // and store the size in "size".  On error, return nullptr.  The
  // returned section data is only valid until the ElfReader gets
  // destroyed.
  const char* GetSectionByName(absl::string_view section_name, size_t* size);

  // Gets the buildid of the binary.
  std::string GetBuildId();

  // The SectionInfo structure is almost identical to the typedef struct
  // Elf64_Shdr defined in <elf.h>, but is redefined here so that the many
  // short macro names in <elf.h> don't have to be added to our already
  // cluttered namespace.
  struct SectionInfo {
    uint32_t type;       // Section type (SHT_xxx constant from elf.h).
    uint64_t flags;      // Section flags (SHF_xxx constants from elf.h).
    uint64_t addr;       // Unrelocated (link-time) section address.
    uint64_t offset;     // Section file offset.
    uint64_t size;       // Section size in bytes.
    uint32_t link;       // Link to another section.
    uint32_t info;       // Additional section information.
    uint64_t addralign;  // Section alignment.
    uint64_t entsize;    // Entry size if section holds a table.
  };

  // The SegmentInfo structure is almost identical to the typedef struct
  // Elf64_Phdr defined in <elf.h>, but is redefined here so that the many
  // short macro names in <elf.h> don't have to be added to our already
  // cluttered namespace.
  struct SegmentInfo {
    uint32_t type;    // Segment type (PT_xxx constant from elf.h).
    uint32_t flags;   // Segment flags (PF_xxx constants from elf.h).
    uint64_t offset;  // Segment file offset.
    uint64_t vaddr;   // Segment virtual address.
    uint64_t paddr;   // Segment physical address.
    uint64_t filesz;  // Segment size in file.
    uint64_t memsz;   // Segment size in memory.
    uint64_t align;   // Segment alignment.
  };

  // Return the vector of SegmentInfo for all the segments in program header.
  std::vector<ElfReader::SegmentInfo> GetSegmentInfo();

  // This is like GetSectionByIndex() but it returns a lot of extra information
  // about the section.
  const char* GetSectionInfoByIndex(int shndx, SectionInfo* info);

  // Gets only the SectionInfo (skips reading the content) for the section at
  // the given index. Returns std::nullopt if the section is not found.
  std::optional<SectionInfo> GetSectionInfoByIndex(int shndx);

  // This is like GetSectionByName() but it returns a lot of extra information
  // about the section.
  const char* GetSectionInfoByName(absl::string_view section_name,
                                   SectionInfo* info);

  // Gets only the SectionInfo (skips reading the content) for the section with
  // the given name. Returns std::nullopt if the section is not found.
  std::optional<SectionInfo> GetSectionInfoByName(
      absl::string_view section_name);

  // Returns the Ehdr's e_shoff.
  std::optional<uint64_t> GetSectionHeaderOffset();

  // Check if "path" is an ELF binary that has not been stripped of symbol
  // tables.  This function supports both 32-bit and 64-bit ELF binaries.
  static bool IsNonStrippedELFBinary(const std::string& path);

  // Check if "path" is an ELF binary that has not been stripped of debug
  // info. Unlike IsNonStrippedELFBinary, this function will return
  // false for binaries passed through "strip -S".
  static bool IsNonDebugStrippedELFBinary(const std::string& path);

 private:
  // Lazily initialize impl32_ and return it.
  ElfReaderImpl<Elf32>* GetImpl32();
  // Ditto for impl64_.
  ElfReaderImpl<Elf64>* GetImpl64();

  // Path of the file we're reading.
  const std::string path_;
  // Read-only file descriptor for the file. May be -1 if there was an
  // error during open.
  int fd_;
  // Should we keep symbol names (STRTABs) mmaped after ElfReader is destructed?
  const bool leak_strtabs_ = false;
  // Offset of ElfXX_Ehdr in the file (non-0 when using dlopen_with_offset).
  const size_t off_;

  // The next two members are non-zero if the whole file was mmaped.
  void* whole_file_ = nullptr;
  size_t whole_file_size_ = 0;

  std::unique_ptr<ElfReaderImpl<Elf32>> impl32_;
  std::unique_ptr<ElfReaderImpl<Elf64>> impl64_;

  mutable absl::Mutex mutex_;
};

}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_SYMBOLIZE_ELF_READER_H__
