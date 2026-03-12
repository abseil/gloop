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

// Copyright 2010 Google Inc. All Rights Reserved.

//
// This file provides a few functions for hashing strings.  On x86-64
// hardware as of early 2010, CityHash64() is much faster than
// MurmurHash64(), and passes the quality-of-hash tests in
// ./hasheval/hasheval_test.cc, among others, with flying colors.  The
// difference in speed can be a factor of two for strings of 50 to 64
// bytes, and sometimes even more for cache-resident longer strings.
//
// CityHash128() is optimized for relatively long strings and returns
// a 128-bit hash.  For strings more than about 2000 bytes it can be
// faster than CityHash64().
//
// Functions in the CityHash family are not suitable for cryptography.
//
// By the way, for some hash functions, given strings a and b, the hash
// of a+b is easily derived from the hashes of a and b.  This property
// doesn't hold for any hash functions in this file.
//
// Note that the hash functions defined here are related to, but NOT the same
// as the functions defined in the published version of cityhash in
// https://github.com/google/cityhash.
//
// Some functions below are commented with "mapping will never change."  Those
// will return the same mathematical function forever, on all architectures.
// Other functions here aren't guaranteed consistent across different
// processes, or different executions of the same binary (related: HashSeed
// in builtin_type_hash.h).

#ifndef THIRD_PARTY_GLOOP_UTIL_HASH_CITY_H_
#define THIRD_PARTY_GLOOP_UTIL_HASH_CITY_H_

#include <stddef.h>  // for size_t.

#include <cstdint>

#include "absl/base/macros.h"
#include "absl/numeric/int128.h"
#include "absl/strings/string_view.h"

namespace util_hash {

// Hash function for a byte array.  The mapping will never change.
//
uint64_t CityHash64(absl::string_view s);

ABSL_DEPRECATE_AND_INLINE()
inline uint64_t CityHash64(const char* s, size_t len) {
  return CityHash64(absl::string_view(s, len));
}

// Hash function for a byte array.  For convenience, a 64-bit seed is also
// hashed into the result.  The mapping will never change.
//
uint64_t CityHash64WithSeed(absl::string_view s, uint64_t seed);

// Hash function for a byte array.  For convenience, two seeds are also
// hashed into the result.  The mapping will never change.
//
uint64_t CityHash64WithSeeds(absl::string_view s, uint64_t seed0,
                             uint64_t seed1);

ABSL_DEPRECATE_AND_INLINE()
inline uint64_t CityHash64WithSeeds(const char* s, size_t len, uint64_t seed0,
                                    uint64_t seed1) {
  return CityHash64WithSeeds(absl::string_view(s, len), seed0, seed1);
}

// Hash function for a byte array.  The mapping will never change.
//
absl::uint128 CityHash128(absl::string_view s);

ABSL_DEPRECATE_AND_INLINE()
inline absl::uint128 CityHash128(const char* s, size_t len) {
  return CityHash128(absl::string_view(s, len));
}

// Hash function for a byte array.  For convenience, a 128-bit seed is also
// hashed into the result.  The mapping will never change.
//
absl::uint128 CityHash128WithSeed(absl::string_view s, absl::uint128 seed);

ABSL_DEPRECATE_AND_INLINE()
inline absl::uint128 CityHash128WithSeed(const char* s, size_t len,
                                         absl::uint128 seed) {
  return CityHash128WithSeed(absl::string_view(s, len), seed);
}

// Hash function for a byte array.
// The mapping may change from time to time.
uint32_t CityHash32(absl::string_view s);

ABSL_DEPRECATE_AND_INLINE()
inline uint32_t CityHash32(const char* s, size_t len) {
  return CityHash32(absl::string_view(s, len));
}

// Hash function for a byte array.
// The mapping may change from time to time.
uint32_t CityHash32WithSeed(absl::string_view s, uint32_t seed);

ABSL_DEPRECATE_AND_INLINE()
inline uint32_t CityHash32WithSeed(const char* s, size_t len, uint32_t seed) {
  return CityHash32WithSeed(absl::string_view(s, len), seed);
}

}  // namespace util_hash

#endif  // THIRD_PARTY_GLOOP_UTIL_HASH_CITY_H_
