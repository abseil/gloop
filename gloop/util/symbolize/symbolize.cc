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

#include "gloop/util/symbolize/symbolize.h"

#include "absl/log/check.h"
#include "absl/log/log.h"

#if __ELF__
#include <elf.h>
#include <link.h>
#ifdef __linux__
#include <sys/auxv.h>
#endif
#endif

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ios>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "absl/base/call_once.h"
#include "absl/debugging/internal/vdso_support.h"
#include "absl/flags/flag.h"
#include "absl/memory/memory.h"
#include "absl/types/span.h"
#include "gloop/base/arena.h"
#include "gloop/util/coding/shrunk-array.h"
#include "gloop/util/symbolize/demangle.h"
#if __ELF__
#include "absl/strings/string_view.h"
#include "gloop/util/symbolize/elf_reader.h"
#endif
#include "gloop/util/symbolize/symbolize-inl.h"

// SymbolMap copies string names into an arena, since we don't want to
// free anything until the SymbolMap is destroyed. We make a copy of
// symbol names so we can un-mmap the files once we're done extracting
// symbols. The copy behavior for GetCached is controlled by
// symbol_map_copy_names flag (see comment below).
static const int kNameArenaBlockSize = 64 << 10;  // 64K

// When symbol_map_copy_names is false, we keep a read-only mappings into
// STRTABs (.strtab and .dynstr sections) of every ELF binary loaded into the
// process. This reduces overall memory requirement, but may increase overall
// VM size of the process, especially one with lots of shared libraries.
//
// Note that changing this flag after the first call to SymbolMap::GetCached
// has no effect.
ABSL_RETIRED_FLAG(bool, symbol_map_copy_names, false,
                  "Copy symbol names into private arena.");

// We use level 0 in tests because:
// 1. Compressing symbol table in tests is generally a waste of CPU, and
// 2. Tests that run in fastbuild mode get un-optimized compression code,
//    and can trigger flaky timeouts.
// We use level 0 in non-opt builds because non-opt binary is likely being
// used interactively or for debugging, and binary startup time matters.
// See b/119699200.
namespace {
int GetCompressionLevel() {
  if (getenv("TEST_SRCDIR") != nullptr) return 0;  // unit test
#ifdef DEBUG  // Note: this is file is optimized in all builds,
              // so we can't key off __OPTIMIZE__ here (as it's always on).
  return 0;   // non-opt build
#endif
  return 3;
}
}  // namespace

// Please raise a bug on http://b/issues/new?component=27794 if you need to
// disable (or reduce the level of) symbol map compression.
ABSL_FLAG(int, symbol_map_compression_level, GetCompressionLevel(),
          "Compression level of SymbolMap information; 0 = off, 1..3 "
          "inclusive are increasing levels of compression.");

namespace util {
static std::atomic<SymbolMap*> g_cached_symbol_map = nullptr;
static absl::once_flag once;

struct GeneratedLibrary {
  std::string elf_name;
  uint64_t map_beg;
  uint64_t map_end;
  uint64_t map_offset;
  GeneratedLibrary(std::string name, uint64_t beg, uint64_t end,
                   uint64_t offset)
      : elf_name(name), map_beg(beg), map_end(end), map_offset(offset) {}
};

static std::vector<GeneratedLibrary>* g_genlibs = nullptr;

void SymbolMap::AddGeneratedLibrary(std::string elf_name, uint64_t map_beg,
                                    uint64_t map_end, uint64_t map_offset) {
  if (g_genlibs == nullptr) {
    g_genlibs = new std::vector<GeneratedLibrary>;
  }
  g_genlibs->emplace_back(elf_name, map_beg, map_end, map_offset);
}

// static
void SymbolMap::OnceInit() {
  CHECK(!IsCacheInitialized());  // Paranoia.
  // Get the flag and map outside of any locks (b/36422185#comment21).
  const int compression_level =
      absl::GetFlag(FLAGS_symbol_map_compression_level);
  auto symbol_map =
      SymbolMap::CreateInternal(/*copy_symbol_names=*/false, compression_level);

  // Install it.
  g_cached_symbol_map.store(symbol_map.release(), std::memory_order_release);
}

// static
const SymbolMap& SymbolMap::GetCached() {
  absl::call_once(once, &SymbolMap::OnceInit);
  return *g_cached_symbol_map.load(std::memory_order_acquire);
}

bool SymbolMap::IsCacheInitialized() {
  return g_cached_symbol_map.load(std::memory_order_acquire) != nullptr;
}

std::unique_ptr<SymbolMap> SymbolMap::CreateInternal(bool copy_symbol_names,
                                                     int compression_level) {
  auto symbols =
      absl::WrapUnique(new SymbolMap(copy_symbol_names, compression_level));
  PopulateSymbols(*symbols);
  return symbols;
}

SymbolMap* SymbolMap::GetEmpty(int compression_level) {
  return new SymbolMap(/*copy_symbol_names*/ true, compression_level);
}

SymbolMap::SymbolMap(bool copy_symbol_names, int symbol_map_compression_level)
    : name_arena_(copy_symbol_names ? new UnsafeArena(kNameArenaBlockSize)
                                    : nullptr),
      copy_symbol_names_(copy_symbol_names),
      symbol_map_compression_level_(
          std::max(std::min(symbol_map_compression_level, 3), 0)) {
  DCHECK_GE(symbol_map_compression_level, 0);
  DCHECK_LE(symbol_map_compression_level, 3);
}

int SymbolMap::num_symbols() const {
  EnsureFinalized();
  if (symbol_map_compression_level_ == 0) {
    return symbols_.size();
  } else {
    return name_shrunk_array_.size();
  }
}

size_t SymbolMap::bytes_allocated() const {
  EnsureFinalized();
  const size_t names_size =
      name_arena_ ? name_arena_->status().bytes_allocated() : 0;
  return names_size + symbols_.capacity() * sizeof(symbols_[0]) +
         name_shrunk_array_.shrunk_array_byte_length() +
         addr_shrunk_array_.shrunk_array_byte_length() +
         size_shrunk_array_.shrunk_array_byte_length();
}

bool SymbolMap::AddSymbol(const char* name, const char* section_end,
                          uint64_t start, uint64_t size) {
  // We only verify that `name` is NUL-terminated when `copy_symbol_names_` is
  // true. It's important not to treat `name` as a C-strring until that's done.
  const void* unverified_name = static_cast<const void*>(name);

  if (section_end <= name) {
    LOG(ERROR) << "Rejecting invalid symbol: " << unverified_name
               << " is past string section end: "
               << static_cast<const void*>(section_end);
    return false;
  }
  if (name == nullptr || start + size < start) {
    LOG(ERROR) << "Rejecting invalid symbol: " << unverified_name
               << " start=" << std::hex << start << " size: " << size;
    return false;
  }
  CHECK(!finalized_) << " already finalized";
  VLOG(10) << "[" << std::hex << start << ", " << std::hex << start + size
           << "): " << unverified_name;

  if (copy_symbol_names_) {
    const void* p_name_end = memchr(name, '\0', section_end - name);
    if (p_name_end == nullptr) {
      LOG(ERROR) << "Rejecting invalid symbol: name not NUL-terminated within ["
                 << unverified_name << ", "
                 << static_cast<const void*>(section_end) << ") range";
      return false;
    }
    size_t name_size = 0;  // Includes terminating NUL
    if (const char* dot = strchr(name, '.')) {
      // Name like foo.part.14 or bar.clone.1
      // The demangler removes them, so should we.
      name_size = dot - name + 1;
      char* name2 = name_arena_->Memdup(name, name_size);
      name2[name_size - 1] = '\0';
      name = name2;
    } else {
      name_size = reinterpret_cast<const char*>(p_name_end) - name + 1;
      name = name_arena_->Memdup(name, name_size);
    }
    const auto it = names_.find(name);
    if (it == names_.end()) {
      names_.insert(name);
    } else {
      name_arena_->Free(const_cast<char*>(name), name_size);
      name = it->data();
    }
  }
  symbols_.emplace_back(name, start, size);
  return true;
}

bool SymbolMap::GetSymbolInfoAtPosition(uint64_t pos, const char** name,
                                        uint64_t* start, uint64_t* size) const {
  EnsureFinalized();

  // Find the symbol which starts strictly after the given
  // position, then back up one symbol.
  size_t index = 0;
  if (symbol_map_compression_level_ == 0) {
    index =
        std::upper_bound(symbols_.begin(), symbols_.end(), pos, SymbolCmp()) -
        symbols_.begin();
  } else {
    index = addr_shrunk_array_.UpperBound(pos);
  }
  if (index == 0) return false;
  --index;

  auto get_symbol = [&](size_t index) {
    if (symbol_map_compression_level_ == 0) {
      return symbols_[index];
    } else {
      Symbol s;
      s.name = reinterpret_cast<const char*>(name_shrunk_array_.Get(index));
      s.addr = addr_shrunk_array_.Get(index);
      s.size = size_shrunk_array_.Get(index);
      return s;
    }
  };
  // If the symbol has no size information, we assume that "pos" is
  // associated with the symbol. This happens with rare symbols such
  // as __restore in glibc.  Otherwise, check and see if the symbol is
  // long enough to include "pos".
  //
  // Note that the assumption might produce wrong symbol names in very
  // rare cases.  Suppose the last symbol in the symbol map has no
  // size information and the program counter is pointing to an
  // address after the start address of the symbol?  If necessary we
  // might want to add sanity checks, such as not believing that a
  // procedure without size information is more than a few hundred
  // kilobytes in size.
  const Symbol symbol = get_symbol(index);
  if ((symbol.size == 0) ||
      (pos >= symbol.addr && pos - symbol.addr < symbol.size)) {
    if (start) *start = symbol.addr;
    if (size) *size = symbol.size;

    // Multiple symbols may have the same address. We prefer the
    // lexicographically greatest name. (We assume all symbols with the same
    // start also have the same size, since otherwise would imply an ODR
    // violation.)
    if (name) {
      *name = symbol.name;
      size_t eq_index = index;
      while (eq_index != 0) {
        --eq_index;
        const Symbol prev_symbol = get_symbol(eq_index);
        if (prev_symbol.addr != symbol.addr) {
          break;
        }
        if (strcmp(prev_symbol.name, *name) > 0) {
          *name = prev_symbol.name;
        }
      }
    }

    return true;
  }
  return false;
}

const char* SymbolMap::GetSymbolAtPosition(uint64_t pos) const {
  const char* name = nullptr;
  if (GetSymbolInfoAtPosition(pos, &name, nullptr, nullptr))
    return name;
  else
    return nullptr;
}

std::string SymbolMap::GetDemangledSymbolAtPosition(uint64_t pos) const {
  std::string demangled;
  GetDemangledSymbolAtPositionToString(pos, &demangled);
  return demangled;
}

void SymbolMap::GetDemangledSymbolAtPositionToString(uint64_t pos,
                                                     std::string* out) const {
  const char* name = GetSymbolAtPosition(pos);
  if (name == nullptr) return;
  util::DemangleToString(name, out);
}

SymbolMap::Iterator* SymbolMap::GetIterator() const {
  EnsureFinalized();
  if (symbol_map_compression_level_ == 0) {
    return new Iterator(symbols_.begin(), symbols_.end());
  } else {
    return new Iterator(this, 0, name_shrunk_array_.size());
  }
}

SymbolMap::Iterator* SymbolMap::GetIteratorFrom(uint64_t pos) const {
  EnsureFinalized();
  if (symbol_map_compression_level_ == 0) {
    auto it =
        std::lower_bound(symbols_.begin(), symbols_.end(), pos, SymbolCmp());
    return new Iterator(it, symbols_.end());
  } else {
    const size_t index = addr_shrunk_array_.LowerBound(pos);
    return new Iterator(this, index, addr_shrunk_array_.size());
  }
}

void SymbolMap::EnsureFinalized() const {
  absl::call_once(once_, &SymbolMap::Finalize, const_cast<SymbolMap*>(this));
}

void SymbolMap::Finalize() {
  CHECK(!finalized_);
  // names_ are not needed anymore.
  NameHashSet().swap(names_);  // Also free storage

  std::sort(symbols_.begin(), symbols_.end(),
            [](Symbol const& a, Symbol const& b) { return a.addr < b.addr; });

  if (symbol_map_compression_level_ == 0) {
    // Free any excess storage used by exponential resizing as the symbols_
    // vector was being built.
    symbols_.shrink_to_fit();
  } else {
    VLOG(1) << "Symbol table compression started";
    {
      std::vector<uint64_t> buffer;
      buffer.reserve(symbols_.size());

      // TODO Single ELF images are smaller than 4 GiB; make use of
      // this by using uint32.
      for (const Symbol& symbol : symbols_) {
        buffer.push_back(reinterpret_cast<uint64_t>(symbol.name));
      }
      name_shrunk_array_.Assign(absl::MakeSpan(buffer),
                                symbol_map_compression_level_);

      // We can re-use buffer which is of the correct length.
      uint64_t* write_iterator = buffer.data();
      for (const Symbol& symbol : symbols_) {
        *write_iterator = symbol.addr;
        ++write_iterator;
      }
      addr_shrunk_array_.Assign(absl::MakeSpan(buffer),
                                symbol_map_compression_level_);
    }

    {
      std::vector<uint32_t> buffer;
      buffer.reserve(symbols_.size());

      for (const Symbol& symbol : symbols_) {
        DCHECK_LE(symbol.size, std::numeric_limits<uint32_t>::max());
        buffer.push_back(symbol.size);
      }
      size_shrunk_array_.Assign(absl::MakeSpan(buffer),
                                symbol_map_compression_level_);
    }

    VLOG(1) << "Symbol table compression finished. name_shrunk_array_ "
            << name_shrunk_array_.shrunk_array_byte_length()
            << ", addr_shrunk_array_ "
            << addr_shrunk_array_.shrunk_array_byte_length()
            << ", size_shrunk_array_ "
            << size_shrunk_array_.shrunk_array_byte_length() << ", total size "
            << name_shrunk_array_.shrunk_array_byte_length() +
                   addr_shrunk_array_.shrunk_array_byte_length() +
                   size_shrunk_array_.shrunk_array_byte_length()
            << " versus " << symbols_.size() * sizeof(Symbol);
    // Clear out symbols_ as we no longer need them.
    std::vector<Symbol>().swap(symbols_);
  }

  finalized_ = true;
}

void SymbolMap::PopulateSymbols(SymbolMap& symbols) {
  // For non-ELF platforms, we currently have no mechanism to enumerate our own
  // symbols. In this case we just leave the map empty, and all operations will
  // fail to resolve anything useful.
#if __ELF__
  // <path> only
  // declares dl_iterate_phdr for __arm__ when __ANDROID_API__ is >= 21.
#if !defined(__ANDROID_API__) || !defined(__arm__) || __ANDROID_API__ >= 21
  struct Info {
    std::string file_name;
    uint64_t elf_offset;   // Offset of ELF header within file_name.
    uint64_t mem_offset;   // Runtime address of executable PT_LOAD segment.
    uint64_t file_offset;  // File offset of segment (elf_offset + p_offset).
    uint64_t length;       // p_filesz of the segment.
    uint64_t vaddr;        // p_vaddr of the segment.
    bool is_main;
    // True if another executable PT_LOAD segment has a different
    // p_vaddr - p_offset. ElfReader::AddSymbols applies a single address
    // adjustment to all symbols, so functions in such a segment get misplaced.
    bool inconsistent_exec_segments;
  };
  struct CallbackData {
    int count = 0;
    const ElfW(Phdr) * vdso_phdr = nullptr;
    std::vector<Info> images;
  } cb_data;

#if defined(__linux__) && defined(AT_SYSINFO_EHDR)
  if (const auto* vdso_ehdr =
          reinterpret_cast<const ElfW(Ehdr)*>(getauxval(AT_SYSINFO_EHDR));
      vdso_ehdr != nullptr) {
    cb_data.vdso_phdr = reinterpret_cast<const ElfW(Phdr)*>(
        reinterpret_cast<const char*>(vdso_ehdr) + vdso_ehdr->e_phoff);
  }
#endif

  // Collect loaded object metadata first so we don't perform file I/O, mmap,
  // decompression, or logging while holding dl_iterate_phdr's loader lock.
  auto callback =
      +[](struct dl_phdr_info* info, size_t /*size*/, void* data) -> int {
    auto* cb = static_cast<CallbackData*>(data);
    const bool is_main = (cb->count++ == 0);
    if (info->dlpi_phdr == nullptr || info->dlpi_phnum == 0) {
      return 0;
    }
    if (cb->vdso_phdr != nullptr && info->dlpi_phdr == cb->vdso_phdr) {
      // VDSO doesn't exist as a file on disk; handled via VDSOSupport below.
      return 0;
    }

    uint64_t elf_offset = 0;
    absl::string_view name =
        info->dlpi_name != nullptr ? info->dlpi_name : absl::string_view();
    if (name.empty()) {
      if (!is_main) {
        return 0;
      }
      name = "/proc/self/exe";
    }

    // ElfReader::AddSymbols walks the entire symbol table on each call
    // (ignoring `length`), so record each ELF image at most once, using its
    // first executable segment.
    const ElfW(Phdr)* exec_phdr = nullptr;
    bool inconsistent_exec_segments = false;
    for (int j = 0; j < info->dlpi_phnum; ++j) {
      const ElfW(Phdr) & phdr = info->dlpi_phdr[j];
      if (phdr.p_type != PT_LOAD || (phdr.p_flags & PF_X) == 0) continue;
      if (exec_phdr == nullptr) {
        exec_phdr = &phdr;
      } else if (phdr.p_vaddr - phdr.p_offset !=
                 exec_phdr->p_vaddr - exec_phdr->p_offset) {
        inconsistent_exec_segments = true;
      }
    }
    if (exec_phdr != nullptr) {
      cb->images.push_back(Info{
          .file_name = std::string(name),
          .elf_offset = elf_offset,
          .mem_offset = info->dlpi_addr + exec_phdr->p_vaddr,
          .file_offset = elf_offset + exec_phdr->p_offset,
          .length = exec_phdr->p_filesz,
          .vaddr = exec_phdr->p_vaddr,
          .is_main = is_main,
          .inconsistent_exec_segments = inconsistent_exec_segments,
      });
    }
    return 0;
  };
  dl_iterate_phdr(callback, &cb_data);

  for (const Info& info : cb_data.images) {
    VLOG(5) << "Trying " << info.file_name << " at elf_offset 0x" << std::hex
            << info.elf_offset << " file_offset 0x" << info.file_offset
            << " mem_offset 0x" << info.mem_offset;
    ElfReader reader(info.file_name, info.elf_offset,
                     !symbols.copy_symbol_names_);
    if (!reader.IsElf32File() && !reader.IsElf64File()) {
      continue;
    }
    // The file is opened by the name it was loaded from, possibly long after
    // it was loaded. Make sure that it still contains the loaded image: it may
    // have been replaced on disk, or a relative name may now resolve to a
    // different file.
    const std::vector<ElfReader::SegmentInfo> segments =
        reader.GetSegmentInfo();
    if (std::none_of(segments.begin(), segments.end(),
                     [&info](const ElfReader::SegmentInfo& segment) {
                       return segment.type == PT_LOAD &&
                              (segment.flags & PF_X) != 0 &&
                              segment.vaddr == info.vaddr &&
                              info.elf_offset + segment.offset ==
                                  info.file_offset &&
                              segment.filesz == info.length;
                     })) {
      LOG(WARNING) << "Not using symbols from " << info.file_name
                   << ": its executable segment does not match the loaded "
                      "image";
      continue;
    }
    if (info.inconsistent_exec_segments) {
      LOG_FIRST_N(WARNING, 1)
          << info.file_name
          << " has executable segments with different address-to-offset "
             "deltas; symbols outside the first one will be misplaced";
    }
    // TODO: ask the ELF reader in advance how many symbols there
    // will be, so we can avoid the need for repeated reallocations.
    reader.AddSymbols(&symbols, info.mem_offset, info.file_offset, info.length);
    if (info.is_main) {
      symbols.stripped_ = !reader.HasSymbolNames();
    }
  }
#endif  // !__ANDROID_API__ || !__arm__ || __ANDROID_API__ >= 21

  if (g_genlibs != nullptr) {
    for (const auto& lib : *g_genlibs) {
      ElfReader reader(lib.elf_name, 0, !symbols.copy_symbol_names_);
      if (reader.IsNativeElfFile()) {
        const uint64_t length = lib.map_end - lib.map_beg;
        reader.AddSymbols(&symbols, lib.map_beg, lib.map_offset, length);
      }
    }
  }
#endif  // __ELF__

#ifdef ABSL_HAVE_VDSO_SUPPORT  // defined in vdso_support.h
  absl::debugging_internal::VDSOSupport vdso;
  if (vdso.IsPresent()) {
    absl::debugging_internal::VDSOSupport::SymbolIterator it = vdso.begin();
    for (; it != vdso.end(); ++it) {
      const absl::debugging_internal::VDSOSupport::SymbolInfo& info = *it;
      int symbol_type = (sizeof(info.address) == 4)
                            ? ELF32_ST_TYPE(info.symbol->st_info)
                            : ELF64_ST_TYPE(info.symbol->st_info);
      // Add only STT_FUNC symbols. Ignore OBJECT, SECTION, etc.
      if (symbol_type == STT_FUNC) {
        symbols.AddSymbol(info.name, reinterpret_cast<uintptr_t>(info.address),
                          info.symbol->st_size);
      }
    }
  }
#endif  // ABSL_HAVE_VDSO_SUPPORT
  symbols.EnsureFinalized();
}

SymbolMapIterator::~SymbolMapIterator() = default;

namespace {

// Finds the position x at which p(x) first returns true.
// [f, f + length) is a partitioned range where there is at most one offset m
// in [1, length) such that p(f + m - 1) == false and p(f + m) == true.
// Note that [a, b) is a semi-open range, where b is not included.
template <typename Pred>
size_t partition_point_n(size_t f, size_t length, Pred p) {
  while (length != 0) {
    const size_t half_length = length / 2;
    const size_t i_middle = f + half_length;
    if (p(i_middle)) {
      // i_middle is >= the partition_point.
      length = half_length;
    } else {
      // i_middle is < the partition point.
      length -= (half_length + 1);
      f = i_middle + 1;
    }
  }
  return f;
}

}  // namespace

template <typename IntegerType>
void SymbolMap::CompressedUintArray::AssignImpl(absl::Span<IntegerType> value,
                                                int compression_level) {
  DCHECK_GT(compression_level, 0);
  DCHECK_LE(compression_level, 3);
  number_of_items_ = value.size();
  const IntegerType minimum_value =
      value.empty() ? 0 : *std::min_element(value.begin(), value.end());
  for (IntegerType& v : value) {
    v -= minimum_value;
  }
#ifndef NDEBUG
  is_sorted_ = std::is_sorted(value.begin(), value.end());
#endif
  ShrunkArray::Write(value.data(), number_of_items_, compression_level,
                     decode_key_, &shrunk_array_);
  shrunk_array_.shrink_to_fit();
  reader_ = Bind();
  bias_ = minimum_value;
}

void SymbolMap::CompressedUintArray::Assign(absl::Span<uint64_t> value,
                                            int compression_level) {
  AssignImpl(value, compression_level);
}

void SymbolMap::CompressedUintArray::Assign(absl::Span<uint32_t> value,
                                            int compression_level) {
  AssignImpl(value, compression_level);
}

size_t SymbolMap::CompressedUintArray::size() const { return number_of_items_; }

size_t SymbolMap::CompressedUintArray::shrunk_array_byte_length() const {
  return shrunk_array_.capacity() * sizeof(uint64_t);
}

size_t SymbolMap::CompressedUintArray::UpperBound(uint64_t value) const {
  DCHECK(is_sorted_);
  return partition_point_n(0, number_of_items_, [value, this](size_t index) {
    return value < Get(index);
  });
}

size_t SymbolMap::CompressedUintArray::LowerBound(uint64_t value) const {
  DCHECK(is_sorted_);
  return partition_point_n(0, number_of_items_, [value, this](size_t index) {
    return value <= Get(index);
  });
}

uint64_t SymbolMap::CompressedUintArray::Get(size_t index) const {
  DCHECK_LT(index, size());
  return bias_ + reader_->SharedGet(index);
}

std::unique_ptr<ShrunkArray::Reader> SymbolMap::CompressedUintArray::Bind()
    const {
  auto reader = absl::WrapUnique(ShrunkArray::Reader::New());
  reader->Bind(shrunk_array_.data(), decode_key_);
  return reader;
}

}  // namespace util
