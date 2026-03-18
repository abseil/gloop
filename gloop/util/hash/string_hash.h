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

//
// These are the core hashing routines which operate on strings. We define
// strings loosely as a sequence of bytes, and these routines are designed to
// work with the most fundamental representations of a string of bytes.
//
// These routines provide "good" hash functions in terms of both quality and
// speed. Their values can and will change as their implementations change and
// evolve.

#ifndef THIRD_PARTY_GLOOP_UTIL_HASH_STRING_HASH_H_
#define THIRD_PARTY_GLOOP_UTIL_HASH_STRING_HASH_H_

#include <limits.h>
#include <stddef.h>

#include <cstdint>

#include "absl/base/attributes.h"
#include "absl/strings/string_view.h"
#include "gloop/util/hash/city.h"

namespace hash_internal {

template <size_t Bits = sizeof(size_t) * CHAR_BIT>
struct Thoroughly;

template <>
struct Thoroughly<64> {
  static size_t Hash(const char* s, size_t len, size_t seed) {
    return static_cast<size_t>(
        util_hash::CityHash64WithSeed(absl::string_view(s, len), seed));
  }
  static size_t Hash(const char* s, size_t len) {
    return static_cast<size_t>(
        util_hash::CityHash64(absl::string_view(s, len)));
  }
  static size_t Hash(const char* s, size_t len, size_t seed0, size_t seed1) {
    return static_cast<size_t>(util_hash::CityHash64WithSeeds(
        absl::string_view(s, len), seed0, seed1));
  }
};

template <>
struct Thoroughly<32> {
  static size_t Hash(const char* s, size_t len, size_t seed) {
    return static_cast<size_t>(util_hash::CityHash32WithSeed(
        absl::string_view(s, len), static_cast<uint32_t>(seed)));
  }
  static size_t Hash(const char* s, size_t len) {
    return static_cast<size_t>(
        util_hash::CityHash32(absl::string_view(s, len)));
  }
  static size_t Hash(const char* s, size_t len, size_t seed0, size_t seed1) {
    seed0 += Hash(s, len / 2, seed1);
    s += len / 2;
    len -= len / 2;
    return Hash(s, len, seed0);
  }
};
}  // namespace hash_internal

// We use different algorithms depending on the size of size_t.
ABSL_DEPRECATED("Use absl::Hash or choose from <link>")
inline size_t HashStringThoroughly(const char* s, size_t len) {
  return hash_internal::Thoroughly<>::Hash(s, len);
}

ABSL_DEPRECATED("Use absl::Hash or choose from <link>")
inline size_t HashStringThoroughlyWithSeed(const char* s, size_t len,
                                           size_t seed) {
  return hash_internal::Thoroughly<>::Hash(s, len, seed);
}

ABSL_DEPRECATED("Use absl::Hash or choose from <link>")
inline size_t HashStringThoroughlyWithSeeds(const char* s, size_t len,
                                            size_t seed0, size_t seed1) {
  return hash_internal::Thoroughly<>::Hash(s, len, seed0, seed1);
}

// Templated wrappers for string-like inputs (e.g. string_view). These wrappers
// also have Python CLIF wrappers (see python/string_hash.clif).
template <typename string_type>
ABSL_DEPRECATED("Use absl::Hash or choose from <link>")
inline size_t HashStringThoroughly(const string_type& sv) {
  static_assert(sizeof(sv.data()[0]) == 1,
                "string_type character size must be one byte");
  return HashStringThoroughly(sv.data(), sv.size());
}

template <typename string_type>
ABSL_DEPRECATED("Use absl::Hash or choose from <link>")
inline size_t HashStringThoroughlyWithSeed(const string_type& sv, size_t seed) {
  static_assert(sizeof(sv.data()[0]) == 1,
                "string_type character size must be one byte");
  return HashStringThoroughlyWithSeed(sv.data(), sv.size(), seed);
}

template <typename string_type>
ABSL_DEPRECATED("Use absl::Hash or choose from <link>")
inline size_t HashStringThoroughlyWithSeeds(const string_type& sv, size_t seed0,
                                            size_t seed1) {
  static_assert(sizeof(sv.data()[0]) == 1,
                "string_type character size must be one byte");
  return HashStringThoroughlyWithSeeds(sv.data(), sv.size(), seed0, seed1);
}

#endif  // THIRD_PARTY_GLOOP_UTIL_HASH_STRING_HASH_H_
