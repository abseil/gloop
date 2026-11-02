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

#include <utility>

#include "absl/base/attributes.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"

#if __ELF__
#include <elf.h>
#include <link.h>
#endif

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ios>
#include <limits>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "absl/base/call_once.h"
#include "absl/base/optimization.h"
#include "absl/debugging/internal/symbolize.h"
#include "absl/debugging/internal/vdso_support.h"
#include "absl/flags/flag.h"
#include "absl/memory/memory.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "gloop/base/arena.h"
#include "gloop/base/proc_maps.h"
#include "gloop/util/coding/shrunk-array.h"
#include "gloop/util/symbolize/demangle.h"
#if __ELF__
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

// Declared extern and weak so will be null if dlopen_with_offset isn't linked
// into the binary. This avoids carrying a build-time dependency on
// dlopen_with_offset.
namespace dlopen_with_offset {
extern absl::StatusOr<std::string> ABSL_ATTRIBUTE_WEAK
    GetDlFdPath(absl::string_view);
}

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
         size_shrunk_array_.shrunk_array_byte_length() +
         addr_block_bases_.capacity() * sizeof(addr_block_bases_[0]) +
         addr_overflow_.capacity() * sizeof(addr_overflow_[0]);
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
    index = UpperBoundAddr(pos);
  }
  if (index == 0) return false;
  --index;

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
  auto get_addr = [this](size_t i) {
    return (symbol_map_compression_level_ == 0) ? symbols_[i].addr : GetAddr(i);
  };
  const uint64_t sym_addr = get_addr(index);
  const uint64_t sym_size = (symbol_map_compression_level_ == 0)
                                ? symbols_[index].size
                                : GetSizeWithAddr(index, sym_addr);
  if ((sym_size == 0) || (pos >= sym_addr && pos - sym_addr < sym_size)) {
    if (start) *start = sym_addr;
    if (size) *size = sym_size;

    // Multiple symbols may have the same address. We prefer the
    // lexicographically greatest name. (We assume all symbols with the same
    // start also have the same size, since otherwise would imply an ODR
    // violation.)
    if (name) {
      auto get_name = [this](size_t i) {
        return (symbol_map_compression_level_ == 0) ? symbols_[i].name
                                                    : GetName(i);
      };
      *name = get_name(index);
      size_t eq_index = index;
      while (eq_index != 0) {
        --eq_index;
        if (get_addr(eq_index) != sym_addr) {
          break;
        }
        const char* prev_name = get_name(eq_index);
        if (strcmp(prev_name, *name) > 0) {
          *name = prev_name;
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
    const size_t index = LowerBoundAddr(pos);
    return new Iterator(this, index, addr_shrunk_array_.size());
  }
}

void SymbolMap::EnsureFinalized() const {
  absl::call_once(once_, &SymbolMap::Finalize, const_cast<SymbolMap*>(this));
}

// At level 1, ShrunkArray::Write stores every value at the bit width of the
// largest one (and builds no frequency table), so the address column costs
// n * bit_width(largest token) bits, plus 64 bits per addr_overflow_ entry.
// This picks the cheapest power-of-two cap from a histogram of the bit widths
// of the in-block offsets. Tokens are 32-bit, so the cap is at most 2^31; if
// every offset is below that, keeping them all inline is one of the choices.
uint64_t SymbolMap::Level1AddrInlierCap(absl::Span<const Symbol> symbols) {
  // count[w] and max_offset[w] describe the offsets of bit width w.
  uint64_t count[65] = {};
  uint64_t max_offset[65] = {};
  for (size_t begin = 0; begin < symbols.size(); begin += kAddrBlockSize) {
    const size_t end = std::min(begin + kAddrBlockSize, symbols.size());
    for (size_t i = begin; i < end; ++i) {
      const uint64_t offset = symbols[i].addr - symbols[begin].addr;
      const int width = std::bit_width(offset);
      ++count[width];
      max_offset[width] = std::max(max_offset[width], offset);
    }
  }
  // Cap 2^k keeps the offsets of bit width <= k inline. The overflow tokens
  // follow the largest inline offset.
  const uint64_t n = symbols.size();
  uint64_t overflows = n;
  uint64_t max_inlier = 0;
  uint64_t best_cap = 1;
  uint64_t best_bits = std::numeric_limits<uint64_t>::max();
  for (int k = 0; k <= 31; ++k) {
    overflows -= count[k];
    max_inlier = std::max(max_inlier, max_offset[k]);
    const uint64_t bits =
        n * std::bit_width(max_inlier + overflows) + 64 * overflows;
    if (bits < best_bits) {
      best_bits = bits;
      best_cap = 1ULL << k;
    }
  }
  return best_cap;
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
    const size_t n = symbols_.size();
    // Address offsets at or above addr_inlier_cap go to addr_overflow_. At
    // level 1 the cap is the cheapest one for this map (see
    // Level1AddrInlierCap()). At levels 2-3 the cap is the largest power of
    // two <= n, clamped to [2^16, 2^20]: ShrunkArray::Write may count value
    // frequencies in a dense table of 8 bytes per possible value, and tying
    // the cap to n keeps that table within a small multiple of the column
    // buffers, so mid-sized tables do not get a higher build peak than the
    // plain encoding.
    uint64_t addr_inlier_cap = 1ULL << 16;
    if (symbol_map_compression_level_ == 1) {
      addr_inlier_cap = Level1AddrInlierCap(symbols_);
    } else {
      while (addr_inlier_cap < (1ULL << 20) && 2 * addr_inlier_cap <= n) {
        addr_inlier_cap *= 2;
      }
    }

    // All three columns are encoded into temporary buffers first, and
    // symbols_ is released before any ShrunkArray is built, so the
    // ShrunkArray::Write temporaries never coexist with symbols_.

    // 1. Encode symbol sizes as ZigZag residuals relative to the adjacent
    // symbol address span (symbols_[i + 1].addr - symbols_[i].addr) when that
    // span is in (0, kMaxPredictedSizeSpan].
    auto encode_size = [&](size_t i) -> uint64_t {
      if (i + 1 < n) {
        const uint64_t span = symbols_[i + 1].addr - symbols_[i].addr;
        if (span > 0 && span <= kMaxPredictedSizeSpan) {
          return ZigZagEncode64(static_cast<int64_t>(span - symbols_[i].size));
        }
      }
      return symbols_[i].size;
    };
    std::vector<uint32_t> size_buf(n);
    std::vector<uint64_t> size_buf64;
    for (size_t i = 0; i < n; ++i) {
      const uint64_t enc_size = encode_size(i);
      if (ABSL_PREDICT_FALSE(enc_size > std::numeric_limits<uint32_t>::max())) {
        // Rare: store the whole column as 64-bit values instead.
        std::vector<uint32_t>().swap(size_buf);
        size_buf64.resize(n);
        for (size_t j = 0; j < n; ++j) {
          size_buf64[j] = encode_size(j);
        }
        break;
      }
      size_buf[i] = static_cast<uint32_t>(enc_size);
    }

    // 2. Encode sorted symbol start addresses as offsets from the first
    // address of their block of kAddrBlockSize symbols, placing overflow
    // tokens right above the largest inlier offset.
    std::vector<uint32_t> addr_buf(n);
    std::vector<uint32_t> overflow_pos;
    const size_t num_addr_blocks = (n + kAddrBlockSize - 1) / kAddrBlockSize;
    addr_block_bases_.resize(num_addr_blocks);
    uint32_t max_addr_inlier = 0;
    for (size_t b = 0; b < num_addr_blocks; ++b) {
      const size_t start_idx = b * kAddrBlockSize;
      const size_t end_idx = std::min(start_idx + kAddrBlockSize, n);
      const uint64_t base = symbols_[start_idx].addr;
      addr_block_bases_[b] = base;
      for (size_t i = start_idx; i < end_idx; ++i) {
        const uint64_t offset = symbols_[i].addr - base;
        if (ABSL_PREDICT_FALSE(offset >= addr_inlier_cap)) {
          overflow_pos.push_back(static_cast<uint32_t>(i));
          addr_overflow_.push_back(symbols_[i].addr);
        } else {
          const uint32_t off32 = static_cast<uint32_t>(offset);
          addr_buf[i] = off32;
          max_addr_inlier = std::max(max_addr_inlier, off32);
        }
      }
    }
    addr_overflow_threshold_ = static_cast<uint64_t>(max_addr_inlier) + 1;
    uint32_t overflow_token = static_cast<uint32_t>(addr_overflow_threshold_);
    for (uint32_t pos : overflow_pos) {
      addr_buf[pos] = overflow_token++;
    }
    std::vector<uint32_t>().swap(overflow_pos);
    addr_overflow_.shrink_to_fit();

    // 3. Name pointers keep the global-bias encoding (CompressedUintArray
    // subtracts the smallest pointer).
    std::vector<uint64_t> name_buf(n);
    for (size_t i = 0; i < n; ++i) {
      name_buf[i] = reinterpret_cast<uint64_t>(symbols_[i].name);
    }

    // symbols_ is no longer needed: release it before building the shrunk
    // arrays, and release each buffer as soon as its array is built.
    std::vector<Symbol>().swap(symbols_);
    if (ABSL_PREDICT_FALSE(!size_buf64.empty())) {
      size_shrunk_array_.Assign(absl::MakeSpan(size_buf64),
                                symbol_map_compression_level_);
      std::vector<uint64_t>().swap(size_buf64);
    } else {
      size_shrunk_array_.Assign(absl::MakeSpan(size_buf),
                                symbol_map_compression_level_);
      std::vector<uint32_t>().swap(size_buf);
    }
    addr_shrunk_array_.Assign(absl::MakeSpan(addr_buf),
                              symbol_map_compression_level_);
    std::vector<uint32_t>().swap(addr_buf);
    name_shrunk_array_.Assign(absl::MakeSpan(name_buf),
                              symbol_map_compression_level_);

    VLOG(1) << "Symbol table compression finished. name_shrunk_array_ "
            << name_shrunk_array_.shrunk_array_byte_length()
            << ", addr_shrunk_array_ "
            << addr_shrunk_array_.shrunk_array_byte_length()
            << ", size_shrunk_array_ "
            << size_shrunk_array_.shrunk_array_byte_length() << ", total size "
            << name_shrunk_array_.shrunk_array_byte_length() +
                   addr_shrunk_array_.shrunk_array_byte_length() +
                   size_shrunk_array_.shrunk_array_byte_length() +
                   addr_block_bases_.capacity() * sizeof(addr_block_bases_[0]) +
                   addr_overflow_.capacity() * sizeof(addr_overflow_[0])
            << " versus " << n * sizeof(Symbol);
  }

  finalized_ = true;
}

#if __ELF__

// Callback for dl_iterate_phdr. Used to identify the main binary, by
// exploiting the fact that dl_iterate_phdr calls the callback for the main
// program first, and then each dynamic library in turn (except not here, we
// return 1 to abort iteration on the first call).
static int GetMainExecutableBaseAddressCB(struct dl_phdr_info* info,
                                          size_t size, void* data) {
  uint64_t* out = static_cast<uint64_t*>(data);
  for (int i = 0; i < info->dlpi_phnum; ++i) {
    if (info->dlpi_phdr[i].p_type == PT_LOAD) {
      *out = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr;
      return 1;
    }
  }
  // Failed to find a load address -- shouldn't happen, but we still don't
  // want keep iterating through dynamic libraries, so we still return 1.
  return 1;
}

// Returns the base address of the main program.
static uint64_t GetMainExecutableBaseAddress() {
  uint64_t ret = 0;
#if !defined(__ANDROID_API__) || !defined(__arm__) || __ANDROID_API__ >= 21
  // <path> only
  // declares dl_iterate_phdr for __arm__ when __ANDROID_API__ is >= 21.
  dl_iterate_phdr(GetMainExecutableBaseAddressCB, &ret);
#endif
  return ret;
}

#endif  // __ELF__

void SymbolMap::PopulateSymbols(SymbolMap& symbols) {
  // For non-ELF platforms, we currently have no mechanism to enumerate our own
  // symbols. In this case we just leave the map empty, and all operations will
  // fail to resolve anything useful.
#if __ELF__
  struct Info {
    std::string file_name;
    uint64_t file_offset;
    uint64_t mem_offset;
    uint64_t length;
  };
  auto info_lt = [](const Info& a, const Info& b) {
    return std::tie(a.file_name, a.file_offset, a.mem_offset, a.length) <
           std::tie(b.file_name, b.file_offset, b.mem_offset, b.length);
  };

  ProcMapsIterator it(0);  // 0 -> current process.

  // When copy_symbol_names_ == true,  ElfReader will create new /proc/.../maps
  // mappings. We don't want these mappings to interfere with ProcMapsIterator,
  // so we first accumulate iteration results into image_info, and then process
  // all images without any further ProcMapsIterator involvement.
  std::vector<Info> image_info;
  if (it.Valid()) {
    Info info;
    uint64_t end;
    char *file_name, *flags;
    std::string memfd_name;
    while (it.Next(&info.mem_offset, &end, &flags, &info.file_offset, nullptr,
                   &file_name)) {
      VLOG(10) << "[" << std::hex << info.mem_offset << ", " << std::hex << end
               << "] file offset: " << std::hex << info.file_offset
               << " flags: " << flags << " name: '" << file_name << "'";
      // We only collect "r-xp" maps (i.e. readonly and executable).
      // We don't collect writable maps.  They are most likely for data.
      if (flags[0] == 'r' && flags[1] == '-' && flags[2] == 'x') {
        static const char memfd_tag[] = "/memfd:google_dlopen_";
        if (&dlopen_with_offset::GetDlFdPath != nullptr &&
            strncmp(file_name, memfd_tag, sizeof(memfd_tag) - 1) == 0) {
          auto fd_path = dlopen_with_offset::GetDlFdPath(file_name);
          if (fd_path.ok()) {
            memfd_name = *std::move(fd_path);
            file_name = memfd_name.data();
          }
        }
        auto is_tagged_anon = [](const char* file_name) -> bool {
          // This is an anonymous VMA that has been tagged using
          // prctl(PR_SET_VMA).
          static const char anon_tag[] = "[anon:";
          return strncmp(file_name, anon_tag, sizeof(anon_tag) - 1) == 0;
        };
        if (info.file_offset == 0 &&
            (file_name == nullptr || file_name[0] == '\0' ||
             is_tagged_anon(file_name) ||
             strstr(file_name, " (deleted)") != nullptr)) {
          // Special case: file text may have been remapped to hugepages.
          // b/28220908. Only do that when iterating over our own map.
          absl::debugging_internal::GetFileMappingHint(
              reinterpret_cast<const void**>(&info.mem_offset),
              reinterpret_cast<const void**>(&end), &info.file_offset,
              const_cast<const char**>(&file_name));
        }
        if (file_name == nullptr || file_name[0] == '\0' ||
            is_tagged_anon(file_name)) {
          continue;
        }
        info.length = end - info.mem_offset;
        info.file_name = file_name;
        image_info.push_back(info);
      }
    }
    // With --mlock_style=all, we may have duplicates in the image_info.
    // b/196670643. Filter them out.
    std::sort(image_info.begin(), image_info.end(), info_lt);
    const Info* prev = nullptr;

    uint64_t main_base = GetMainExecutableBaseAddress();
    for (const auto& info : image_info) {
      if (prev != nullptr && !info_lt(info, *prev) && !info_lt(*prev, info)) {
        VLOG(5) << "Discarding duplicate entry for " << info.file_name;
        continue;
      }
      prev = &info;

      uint64_t file_offset = 0;
      ElfReader reader(info.file_name, file_offset,
                       !symbols.copy_symbol_names_);
      if (reader.IsElf32File() || reader.IsElf64File()) {
        // TODO: ask the ELF reader in advance how many symbols there
        // will be, so we can avoid the need for repeated reallocations.
        reader.AddSymbols(&symbols, info.mem_offset, info.file_offset,
                          info.length);
        // If this is the main binary, check if is stripped.
        if (info.mem_offset == main_base) {
          symbols.stripped_ = !reader.HasSymbolNames();
        }
      }
    }
  }
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

size_t SymbolMap::CompressedUintArray::shrunk_array_byte_length() const {
  return shrunk_array_.capacity() * sizeof(uint64_t);
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

// Returns the first symbol index in [0, n) for which `pred(addr)` is true, or
// `n` if none (`pred` must be false before true across sorted addresses).
template <typename Pred>
size_t SymbolMap::PartitionPointAddr(Pred pred) const {
  const size_t block_idx = partition_point_n(
      0, addr_block_bases_.size(),
      [this, &pred](size_t b) { return pred(addr_block_bases_[b]); });
  if (block_idx == 0) {
    return 0;
  }
  const size_t begin = (block_idx - 1) * kAddrBlockSize + 1;
  const size_t end =
      std::min(block_idx * kAddrBlockSize, addr_shrunk_array_.size());
  return partition_point_n(begin, end - begin, [this, &pred](size_t index) {
    return pred(GetAddr(index));
  });
}

size_t SymbolMap::UpperBoundAddr(uint64_t pos) const {
  return PartitionPointAddr([pos](uint64_t addr) { return pos < addr; });
}

size_t SymbolMap::LowerBoundAddr(uint64_t pos) const {
  return PartitionPointAddr([pos](uint64_t addr) { return pos <= addr; });
}

}  // namespace util
