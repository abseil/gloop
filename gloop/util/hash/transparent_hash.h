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

// This file defines "transparent" hashers and comparators which hash/compare
// different types in a consistent way.

#ifndef THIRD_PARTY_GLOOP_UTIL_HASH_TRANSPARENT_HASH_H_
#define THIRD_PARTY_GLOOP_UTIL_HASH_TRANSPARENT_HASH_H_

#include <stddef.h>
#include <string.h>

#include "absl/strings/string_view.h"
#include "gloop/util/hash/string_hash.h"

namespace util_hash {

namespace internal_transparent_hash {

// Assume that T has data() and size() with the same semantics as in
// StringPiece. Among other types, this overloads triggers for vector<char>.
template <class T>
absl::string_view ToString(const T& s) {
  return absl::string_view(s.data(), s.size());
}

inline absl::string_view ToString(absl::string_view a) { return a; }
inline const char* ToString(char* s) { return s; }
inline const char* ToString(const char* s) { return s; }

}  // namespace internal_transparent_hash

// Check two string-like objects for equality.
struct StringEq {
  using is_transparent = void;

  template <class T, class U>
  bool operator()(const T& a, const U& b) const {
    return EqImpl(internal_transparent_hash::ToString(a),
                  internal_transparent_hash::ToString(b));
  }

 private:
  bool EqImpl(absl::string_view a, absl::string_view b) const { return a == b; }
  bool EqImpl(absl::string_view a, const char* b) const {
    size_t i = 0;
    for (; i != a.size() && *b != 0; ++i)
      if (a[i] != *b++) return false;
    return *b == 0 && i == a.size();
  }
  bool EqImpl(const char* a, absl::string_view b) const { return EqImpl(b, a); }
  bool EqImpl(const char* a, const char* b) const { return strcmp(a, b) == 0; }
};

// Given an object which has a data() method returning a char* and a size()
// method, return a hash of the bytes in [data(), data() + size()).
//
struct StringHash {
  using is_transparent = void;

  template <class T>
  size_t operator()(const T& t) const {
    return HashImpl(internal_transparent_hash::ToString(t));
  }

 private:
  size_t HashImpl(absl::string_view s) const {
    return HashStringThoroughly(s.data(), s.size());
  }
  size_t HashImpl(const char* s) const {
    return HashStringThoroughly(s, strlen(s));
  }
};

// TransparentEq transparently compares two arbitrary objects using operator==.
// (It's similar to C++14's std::equal_to<>.)
struct TransparentEq {
  using is_transparent = void;

  template <class T, class U>
  bool operator()(const T& t, const U& u) const {
    return t == u;
  }
};

}  // namespace util_hash

#endif  // THIRD_PARTY_GLOOP_UTIL_HASH_TRANSPARENT_HASH_H_
