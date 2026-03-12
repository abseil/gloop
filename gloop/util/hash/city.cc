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
// This file provides CityHash64() and related functions.
//
// The externally visible functions follow the naming conventions of
// hash.h, where the size of the output is part of the name.  For
// example, CityHash64 returns a 64-bit hash.  The internal helpers do
// not have the return type in their name, but instead have names like
// HashLenXX or HashLenXXtoYY, where XX and YY are input string lengths.
//
// Most of the constants and tricks here were copied from murmur.cc or
// hash.h, or discovered by trial and error.  It's probably possible to further
// optimize the code here by writing a program that systematically explores
// more of the space of possible hash functions, or by using SIMD instructions.

#include "gloop/util/hash/city.h"

#include <cstdint>

#include "absl/numeric/int128.h"
#include "gloop/util/hash/builtin_type_hash.h"  // for HashSeed()
#include "gloop/util/hash/farmhash.h"

namespace util_hash {

// The value returned by Seed() may change from time to time or even
// from run to run.  Please do not assume otherwise.
namespace {
static size_t Seed() { return HashSeed() + 91; }
}  // namespace

uint64_t CityHash64(absl::string_view s) {
  return farmhash::Hash64(s) ^ Seed();
}

uint64_t CityHash64WithSeed(absl::string_view s, uint64_t seed) {
  return farmhash::Hash64WithSeed(s.data(), s.size(), seed + Seed());
}

uint64_t CityHash64WithSeeds(absl::string_view s, uint64_t seed0,
                             uint64_t seed1) {
  return farmhash::Hash64WithSeeds(s.data(), s.size(), seed0, seed1 + Seed());
}

absl::uint128 CityHash128(absl::string_view s) {
  // This function will never change, so Seed() is not used.
  return CityHash128WithSeed(s.data(), s.size(), absl::MakeUint128(6, 555));
}

absl::uint128 CityHash128WithSeed(absl::string_view s, absl::uint128 seed) {
  // This function will never change, so Seed() is not used.
  return farmhash::ToGoogleU128(farmhash::farmhashcc::CityHash128WithSeed(
      s.data(), s.size(), farmhash::ToFarmHashU128(seed)));
}

uint32_t CityHash32(absl::string_view s) {
  return static_cast<uint32_t>(farmhash::Hash32(s) ^ Seed());
}

uint32_t CityHash32WithSeed(absl::string_view s, uint32_t seed) {
  return static_cast<uint32_t>(farmhash::Hash32WithSeed(
      s.data(), s.size(), static_cast<uint32_t>(seed - Seed())));
}

}  // namespace util_hash
