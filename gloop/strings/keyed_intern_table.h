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

#ifndef THIRD_PARTY_GLOOP_STRINGS_KEYED_INTERN_TABLE_H_
#define THIRD_PARTY_GLOOP_STRINGS_KEYED_INTERN_TABLE_H_

#include <cstddef>
#include <limits>
#include <optional>
#include <type_traits>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/hash/hash.h"
#include "absl/log/check.h"
#include "absl/meta/type_traits.h"
#include "absl/strings/string_view.h"
#include "gloop/base/arena.h"
#include "gloop/strings/arena-string.h"

namespace strings {
namespace keyed_intern_table_internal {

template <class E>
using is_scoped_enum =
    std::integral_constant<bool, std::is_enum<E>::value &&
                                     !std::is_convertible<E, int>::value>;

// Range check integral values against std::numeric_limits.
template <typename T>
absl::enable_if_t<std::is_integral<T>::value> AssertWithinRange(size_t count) {
  CHECK_LE(count, static_cast<size_t>(std::numeric_limits<T>::max()));
}

// Use std::underlying_type for scoped enum types.
//
// Arguably this also works for unscoped enum types with fixed underlying type
// (i.e., explicitly specified).  However, that doesn't buy us much as an
// abstraction.  Scoped enums, on the other hand, give us strict integer-like
// types that don't allow accidental arithmetic, implicit conversion, etc.
template <typename T>
absl::enable_if_t<is_scoped_enum<T>::value> AssertWithinRange(size_t count) {
  AssertWithinRange<typename std::underlying_type<T>::type>(count);
}

// For non-integral types we rely on the conversion from size_t to perform the
// range-check (e.g. with SafeInts).
template <typename T>
absl::enable_if_t<!std::is_integral<T>::value && !is_scoped_enum<T>::value>
AssertWithinRange(size_t count) {
  const T ctor_check = static_cast<T>(count);
  (void)ctor_check;
}

}  // namespace keyed_intern_table_internal

// Provides a string intern table, using values of type Int as the key. This may
// be preferable to using InternTable as absl::string_view is two pointers
// whereas Int can be sized to fit the domain.
//
// Another advantage of holding an integer is that comparisons and hashing are
// simple integer operations, whereas the default behaviour of absl::string_view
// is string operations.
//
// The downside is that converting Int back to an absl::string_view takes a call
// to ToString, but this is a relatively cheap index into an array (unless the
// KeyedInternTable may be concurrently mutated, in which case a read-lock is
// required).
//
// Therefore given a choice between using InternTable and KeyedInternTable one
// should weigh up the cost of converting the Int back to an absl::string_view
// with the benefit of a key representation perhaps 1/4 the size.
//
// Int must be integer-like, convertible to/from size_t. Negative values are
// never used, so using an unsigned type may be preferable.  A type-safe example
// that is usable as Int, but prevents accidental arithmetic is:
//
//    enum class FooId : uint32 {};
//
// Indexes are allocated contiguously, with the next unique string being mapped
// to KeyedInternTable::size() (i.e. it starts at Int(0)).
//
// The class is thread-compatible.
template <typename Int>
class KeyedInternTable {
 public:
  explicit KeyedInternTable(size_t block_size) : arena_(block_size) {}
  KeyedInternTable() : KeyedInternTable(4096) {}
  KeyedInternTable(const KeyedInternTable&) = delete;
  KeyedInternTable& operator=(const KeyedInternTable&) = delete;

  // Inserts (or finds, if it already exists) the given string_view into the
  // table.
  Int Insert(absl::string_view s) {
    return *keys_.lazy_emplace(
        s, [s, this](const typename KeySet::constructor& ctor) {
          keyed_intern_table_internal::AssertWithinRange<Int>(values_.size());
          const Int allocated_int = static_cast<Int>(values_.size());
          values_.push_back(strings::ArenaString(s, &arena_));
          ctor(allocated_int);
        });
  }

  // Attempts to find the given string within the intern table.
  std::optional<Int> ToKey(absl::string_view s) const {
    auto iter = keys_.find(s);
    if (iter == keys_.end()) return std::nullopt;
    return *iter;
  }

  // Finds the string relating to the given identifier.
  // It is undefined behavior unless i was returned by a previous call to Insert
  // or ToKey on *this.
  absl::string_view ToStringView(Int i) const {
    return values_[static_cast<size_t>(i)].str();
  }

  // Returns the number of distinct strings stored, which also happens to be the
  // number allocated when the next unique string is inserted (when wrapped in
  // Int).
  size_t size() const { return values_.size(); }

  // Reserves space for the given number of strings.
  void Reserve(size_t count) {
    keyed_intern_table_internal::AssertWithinRange<Int>(count);
    values_.reserve(count);
    keys_.reserve(count);
  }

  // Returns the number of bytes allocated by the underlying arena. This method
  // is thread safe. Useful for debugging or tuning the arena block size.
  size_t ArenaBytesAllocated() const {
    return arena_.status().bytes_allocated();
  }

 private:
  struct Hash {
    using is_transparent = void;
    const std::vector<strings::ArenaString>& strings;
    size_t operator()(absl::string_view x) const {
      return absl::Hash<absl::string_view>()(x);
    }
    size_t operator()(Int i) const {
      return (*this)(strings[static_cast<size_t>(i)].str());
    }
  };

  struct Eq {
    using is_transparent = void;
    const std::vector<strings::ArenaString>& strings;
    bool operator()(Int a, Int b) const { return a == b; }
    bool operator()(Int a, absl::string_view b) const {
      return strings[static_cast<size_t>(a)].str() == b;
    }
  };
  using KeySet = absl::flat_hash_set<Int, Hash, Eq>;

  UnsafeArena arena_;
  std::vector<strings::ArenaString> values_;
  KeySet keys_{0, Hash{values_}, Eq{values_}};
};

}  // namespace strings

#endif  // THIRD_PARTY_GLOOP_STRINGS_KEYED_INTERN_TABLE_H_
