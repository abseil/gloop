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

#ifndef THIRD_PARTY_GLOOP_UTIL_HASH_FARMHASHTE_H_
#define THIRD_PARTY_GLOOP_UTIL_HASH_FARMHASHTE_H_

// This file offers high-quality hash functions that will never change.
// However, it is not suitable for cryptography.
//
// Caveat: At the moment the implementation of these functions require
// a 64-bit CPU with SSE4.1.  It would be easy to create a portable
// implementation, if needed.
//
// If you don't need a forever-unchanging function then a better choice is
// probably farmhash::Hash64{,WithSeed,WithSeeds}.

#include <stddef.h>

#include <cstdint>

namespace farmhashte {

uint64_t Fingerprint64WithSeeds(const char* s, size_t len, uint64_t seed0,
                                uint64_t seed1);

inline uint64_t Fingerprint64WithSeed(const char* s, size_t len,
                                      uint64_t seed) {
  // No thought went into picking 0xf36e56d33cca87c4.  Nothing is up my sleeve.
  return Fingerprint64WithSeeds(s, len, seed, uint64_t{0xf36e56d33cca87c4});
}

inline uint64_t Fingerprint64(const char* s, size_t len) {
  // No thought went into picking the seeds.  Nothing is up my sleeve.
  return Fingerprint64WithSeeds(s, len, uint64_t{0xd429dbe03d0fe188},
                                uint64_t{0x57ff04bbed01750a});
}

}  // namespace farmhashte

#endif  // THIRD_PARTY_GLOOP_UTIL_HASH_FARMHASHTE_H_
