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

#ifndef THIRD_PARTY_GLOOP_UTIL_HASH_HASH128TO64_H_
#define THIRD_PARTY_GLOOP_UTIL_HASH_HASH128TO64_H_

#include <cstdint>

#include "absl/numeric/int128.h"

// Hash 128 input bits down to 64 bits of output.
// This is intended to be a reasonably good hash function.
// It may change from time to time.
inline uint64_t Hash128to64(const absl::uint128 x) {
  // Murmur-inspired hashing.
  const uint64_t kMul = 0xc6a4a7935bd1e995ULL;
  uint64_t a = (absl::Uint128Low64(x) ^ absl::Uint128High64(x)) * kMul;
  a ^= (a >> 47);
  uint64_t b = (absl::Uint128High64(x) ^ a) * kMul;
  b ^= (b >> 47);
  b *= kMul;
  return b;
}

#endif  // THIRD_PARTY_GLOOP_UTIL_HASH_HASH128TO64_H_
