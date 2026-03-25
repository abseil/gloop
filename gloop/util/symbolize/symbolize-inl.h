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

// A class for allowing clients to iterate over all the symbols in
// SymbolMap from lower to higher addresses.  An intended use is to
// generate a nm-like symbols list.
//
// If you don't need to iterate over all the symbols, you don't need
// to include this file at all!  You do need to include it (in your
// own .cc file) if you want to iterate over them.
//

#ifndef UTIL_SYMBOLIZE_SYMBOLIZE_INL_H__
#define UTIL_SYMBOLIZE_SYMBOLIZE_INL_H__

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/log/check.h"
#include "gloop/util/coding/shrunk-array.h"
#include "gloop/util/symbolize/symbolize.h"

namespace util {
class SymbolMapIterator {
 public:
  // This type is neither copyable nor movable.
  SymbolMapIterator(const SymbolMapIterator&) = delete;
  SymbolMapIterator& operator=(const SymbolMapIterator&) = delete;

  virtual ~SymbolMapIterator();
  // Returns true iff the iterator is past the end of the map.
  bool done() const {
    if (IsCompressed()) {
      return compressed_index_ == compressed_end_;
    } else {
      return iter_ == end_;
    }
  }
  // Advances to the next symbol.
  void Next() {
    CHECK(!done());
    if (IsCompressed()) {
      ++compressed_index_;
    } else {
      ++iter_;
    }
  }
  // Returns the start address of the current symbol.
  uint64_t start() const {
    CHECK(!done());
    if (IsCompressed()) {
      return addr_shrunk_array_reader_bias_ +
             addr_shrunk_array_reader_->Get(compressed_index_);
    } else {
      return iter_->addr;
    }
  }
  // Returns the size of the current symbol.
  uint64_t size() const {
    CHECK(!done());
    if (IsCompressed()) {
      return size_shrunk_array_reader_bias_ +
             size_shrunk_array_reader_->Get(compressed_index_);
    } else {
      return iter_->size;
    }
  }
  // Returns the name of the current symbol.
  const char* name() const {
    CHECK(!done());
    if (IsCompressed()) {
      return reinterpret_cast<const char*>(
          name_shrunk_array_reader_bias_ +
          name_shrunk_array_reader_->Get(compressed_index_));
    } else {
      return iter_->name;
    }
  }

 private:
  friend class SymbolMap;
  // This class can only be instantiated by SymbolMap#GetIterator().
  // Start iterating on "map" from "start".
  SymbolMapIterator(std::vector<SymbolMap::Symbol>::const_iterator iter,
                    std::vector<SymbolMap::Symbol>::const_iterator end)
      : iter_(iter), end_(end) {}
  // Constructor called when when compressed symbol maps are enabled.
  SymbolMapIterator(const SymbolMap* symbol_map, size_t begin, size_t end)
      : compressed_index_(begin),
        compressed_end_(end),
        addr_shrunk_array_reader_(symbol_map->addr_shrunk_array_.Bind()),
        addr_shrunk_array_reader_bias_(
            symbol_map->addr_shrunk_array_.GetBias()),
        size_shrunk_array_reader_(symbol_map->size_shrunk_array_.Bind()),
        size_shrunk_array_reader_bias_(
            symbol_map->size_shrunk_array_.GetBias()),
        name_shrunk_array_reader_(symbol_map->name_shrunk_array_.Bind()),
        name_shrunk_array_reader_bias_(
            symbol_map->name_shrunk_array_.GetBias()) {}

  bool IsCompressed() const { return addr_shrunk_array_reader_ != nullptr; }

  // Variables used to read symbols when compression is disabled.
  std::vector<SymbolMap::Symbol>::const_iterator iter_;
  std::vector<SymbolMap::Symbol>::const_iterator end_;

  // Variables used to read symbols when compression is enabled.
  size_t compressed_index_ = 0;
  size_t compressed_end_ = 0;
  // Use ShrunkArray::Reader::Get to optimize obtaining sequential values in the
  // array. This is not thread-safe but iterators are normally used within one
  // thread.
  std::unique_ptr<ShrunkArray::Reader> addr_shrunk_array_reader_;
  uint64_t addr_shrunk_array_reader_bias_ = 0;
  std::unique_ptr<ShrunkArray::Reader> size_shrunk_array_reader_;
  uint64_t size_shrunk_array_reader_bias_ = 0;
  std::unique_ptr<ShrunkArray::Reader> name_shrunk_array_reader_;
  uint64_t name_shrunk_array_reader_bias_ = 0;
};

}  // namespace util

#endif  // UTIL_SYMBOLIZE_SYMBOLIZE_INL_H__
