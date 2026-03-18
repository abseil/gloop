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
// This is the legacy unified hash library implementation. Its components are
// being split up into smaller, dedicated libraries. What remains here are
// things still being migrated.
//
// To find the implementation of the core Bob Jenkins lookup2 hash, look in
// jenkins.cc.

#include <cstdint>
#ifdef __SSE2__
#include <emmintrin.h>
#endif  // __SSE2__

#ifdef __ALTIVEC__
#include <altivec.h>
#endif  // __ALTIVEC__

#include "gloop/util/hash/hash.h"

static const uint32_t kFingerprintSeed0 = 0xabc;
static const uint32_t kFingerprintSeed1 = 0xdef;

uint64_t FingerprintReferenceImplementation(const char* s, size_t len) {
  uint32_t hi = Hash32StringWithSeed(s, len, kFingerprintSeed0);
  uint32_t lo = Hash32StringWithSeed(s, len, kFingerprintSeed1);
  return CombineFingerprintHalves(hi, lo);
}

uint64_t FingerprintInterleavedImplementation(const char* s, size_t len) {
  uint32_t hi = Hash32StringWithSeed(s, len, kFingerprintSeed0);
  uint32_t lo = Hash32StringWithSeed(s, len, kFingerprintSeed1);
  return CombineFingerprintHalves(hi, lo);
}

#if defined(__GNUC__) && !defined(__QNX__)
HASH_NAMESPACE_DECLARATION_START
template class __gnu_cxx::hash_set<std::string>;
template class __gnu_cxx::hash_map<std::string, std::string>;
HASH_NAMESPACE_DECLARATION_END
#endif
