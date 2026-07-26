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

// Classes for symbolizing stack traces of the current process. This
// works by parsing the currently running executable and its shared
// libraries.

#ifndef THIRD_PARTY_GLOOP_UTIL_SYMBOLIZE_SYMBOLIZE_H__
#define THIRD_PARTY_GLOOP_UTIL_SYMBOLIZE_SYMBOLIZE_H__

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/base/call_once.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/declare.h"
#include "absl/hash/hash.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "gloop/base/arena.h"
#include "gloop/base/proc_maps.h"
#include "gloop/strings/util.h"
#include "gloop/util/coding/shrunk-array.h"
#include "gloop/util/symbolize/symbol_map_sink.h"

extern absl::Flag<int> FLAGS_symbol_map_compression_level;

class ProcMapsIterator;

namespace util {
class SymbolMapIterator;
// SymbolMap stores the symbols of the current process and translates
// memory addresses into symbol names.
class SymbolMap : public internal::SymbolMapSink {
 public:
  // This type is neither copyable nor movable.
  SymbolMap(const SymbolMap&) = delete;
  SymbolMap& operator=(const SymbolMap&) = delete;

  // Return a SymbolMap containing the symbols for the current
  // process. The result is only computed once, so repeated accesses
  // should be fast, but may be stale if objects have been dynamically
  // loaded.
  // Finalizes the map. See also symbolize.cc description of
  // --symbol_map_copy_names flag, which affects global behavior.
  static const SymbolMap& GetCached();

  // Indicates if the cached symbol map was already built (by a GetCached()
  // call).
  static bool IsCacheInitialized();

  // Return a SymbolMap containing all symbols for the current
  // process. It is computed on demand and belongs to the caller.
  // `compression_level` must be in the range 0..3 inclusive.
  // Default to *not compressing* the SymbolMap; the caller will likely
  // destroy the result soon after obtaining it.
  static std::unique_ptr<SymbolMap> Create(bool copy_symbol_names = true,
                                           int compression_level = 0) {
    return CreateInternal(copy_symbol_names, compression_level);
  }

  // Return an empty SymbolMap. Used for testing.
  // Default to *not compressing* the SymbolMap.
  static SymbolMap* GetEmpty(int compression_level = 0);

  // Return the number of symbols loaded into this map.
  // Finalizes the map.
  int num_symbols() const;
  // Finalizes the map.
  size_t bytes_allocated() const;

  // Given a memory address, fetch information about the symbol which
  // contains that address. For example, it can find the function
  // pointed to by a program counter. Returns true if a match is found
  // and stores the name of the symbol, the starting address, and the
  // total size at the locations specified. If any output pointers are
  // nullptr, that particular information is not stored. Returns false
  // and does not update any values if it can not find a matching
  // symbol.
  // Finalizes the map.
  bool GetSymbolInfoAtPosition(uint64_t pos, const char** name, uint64_t* start,
                               uint64_t* size) const;

  // Similar to GetSymbolInfoAtPosition, but simply looks up the
  // symbol name and returns it directly.  Returns nullptr if it does not
  // know the name of the symbol.
  // Finalizes the map.
  const char* GetSymbolAtPosition(uint64_t pos) const;

  // Similar to GetSymbolAtPosition() but it returns the demangled
  // name as a 'string' object.  If demangling is not supported, it
  // just returns the original (not demangled) name as string.
  // Returns an empty string it does not know the name of the symbol.
  // Finalizes the map.
  std::string GetDemangledSymbolAtPosition(uint64_t pos) const;

  // Same function as above but appends result to 'out'.
  // Finalizes the map.
  void GetDemangledSymbolAtPositionToString(uint64_t pos,
                                            std::string* out) const;

  // Add given named symbol to the symbol map, which must not have
  // been finalized. Returns false if the symbol is invalid.
  bool AddSymbol(const char* name, uint64_t start, uint64_t size) {
    return AddSymbol(name, reinterpret_cast<const char*>(~0L), start, size);
  }
  // Same as above, but don't read memory past section_end.
  bool AddSymbol(const char* name, const char* section_end, uint64_t start,
                 uint64_t size) override;

  // Return true if the binary appears to be stripped. Even a stripped
  // binary may be able to collect symbol names from glibc, so the mere
  // presence of some symbol names is not sufficient; we rely on the dynamic
  // linker to identify the main program and test directly whether it has a
  // symbol table.
  bool binary_is_stripped() const { return stripped_; }

  // Returns an iterator for this symbol map.  It is the client's
  // responsibility to delete the iterator when it is done with it.
  // Finalizes the map.
  using Iterator = SymbolMapIterator;
  Iterator* GetIterator() const;

  // Returns an iterator for this symbol map starting from the first symbol
  // beginning at or after address pos.  It is the client's responsibility to
  // delete the iterator when it is done with it.
  // Finalizes the map.
  Iterator* GetIteratorFrom(uint64_t pos) const;

  // Register a library generated on-the-fly (not listed in /proc/self/map).
  // If used at all, AddGeneratedLibrary() needs to be called before the first
  // call to GetCached() to have any effect.
  static void AddGeneratedLibrary(std::string elf_name, uint64_t map_beg,
                                  uint64_t map_end, uint64_t map_offset);

 private:
  friend class SymbolMapIterator;

  SymbolMap(bool copy_symbol_names, int symbol_map_compression_level);

  static void OnceInit();
  static std::unique_ptr<SymbolMap> CreateInternal(bool copy_symbol_names,
                                                   int compression_level);

  static void PopulateSymbols(SymbolMap& symbols);

  // Ensures that Finalize has been called for this instance.
  void EnsureFinalized() const;

  // Must be called through the once_ mechanism.
  void Finalize();

  // Contains information about a given symbol, the value_type in
  // SymbolMap::Map.
  struct Symbol {
    Symbol(const char* name, uint64_t addr, uint64_t size)
        : name(name), addr(addr), size(size) {}
    Symbol() = default;

    const char* name = nullptr;
    uint64_t addr = 0;
    uint64_t size = 0;
  };

  struct SymbolCmp {
    bool operator()(const Symbol& sym, uint64_t pos) const {
      return sym.addr < pos;
    }
    bool operator()(uint64_t pos, const Symbol& sym) const {
      return pos < sym.addr;
    }
  };

  // Wrapper around ShrunkArray
  class CompressedUintArray {
   public:
    CompressedUintArray() = default;

    // Assigns the contents of this object.
    // compression_level must be in the range 1..3 inclusive.
    void Assign(absl::Span<uint64_t> value, int compression_level);
    void Assign(absl::Span<uint32_t> value, int compression_level);

    // Returns the number of values in the shrunk array.
    size_t size() const;

    // Returns the number of bytes used to hold the shrunk array.
    size_t shrunk_array_byte_length() const;

    // Returns the first index which has a greater value than `value`.
    // For this to make sense, CompressedUintArray must have been assigned with
    // strictly increasing values.
    size_t UpperBound(uint64_t value) const;

    // Returns the first index which has a value not less than `value`.
    // For this to make sense, CompressedUintArray must have been assigned with
    // strictly increasing values.
    size_t LowerBound(uint64_t value) const;

    // Obtains value at the given index.
    // `index` must be < this->size().
    uint64_t Get(size_t index) const;

    // Returns a reader bound to the contained shrunk_array_.
    // Assign must have been called beforehand.
    // Note that subsequent calls to Assign will implicitly invalidate the
    // reader.
    // Note that users must add the bias onto values returned by the reader.
    std::unique_ptr<ShrunkArray::Reader> Bind() const;

    uint64_t GetBias() const { return bias_; }

   private:
    // Implements the Assign function, indirection used to restrict IntegerType
    // to uint32 and uint64.
    template <typename IntegerType>
    void AssignImpl(absl::Span<IntegerType> value, int compression_level);

    // const ShrunkArray::Reader to ensure thread-safety.
    std::unique_ptr<const ShrunkArray::Reader> reader_;
    size_t number_of_items_ = 0;
    std::vector<uint64_t> shrunk_array_;
    uint64_t decode_key_[2] = {0, 0};
    uint64_t bias_ = 0;
    bool is_sorted_ = true;
  };

  // Arena for symbol names. Only used if copy_symbol_names is true.
  std::unique_ptr<UnsafeArena> name_arena_;

  // Used to filter out duplicate strings. Cleared after we are done
  // building the map. Only used if copy_symbol_names is true.
  using NameHashSet = absl::flat_hash_set<absl::string_view>;
  NameHashSet names_;

  // Vector of symbols (used when symbol_map_compression_level_==0).
  // Sorted by symbol start address when finalized.
  std::vector<Symbol> symbols_;

  mutable absl::once_flag once_;

  // Whether we believe the binary to be stripped.
  bool stripped_ = false;

  // Modified only within Finalize().
  bool finalized_ = false;

  // If true: we copy symbol names in AddSymbol into private memory.
  // If false: we keep a read-only mmap into STRTABs (this reduces overall
  // memory requirement IFF there are multiple copies of the same binary
  // running on a single host). See b/34521730 for motivation.
  const bool copy_symbol_names_ = true;
  // If 0 then the symbols should be stored uncompressed (within symbols_),
  // otherwise the *_shrunk_array_ are used to store the symbols in compressed
  // form.
  int symbol_map_compression_level_;

  CompressedUintArray name_shrunk_array_;
  CompressedUintArray addr_shrunk_array_;
  CompressedUintArray size_shrunk_array_;
};

}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_SYMBOLIZE_SYMBOLIZE_H__
