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
// Hash functions for C++ builtin types. These are all of the fundamental
// integral and floating point types in the language as well as pointers. This
// library provides a minimal set of interfaces for hashing these values.

#ifndef THIRD_PARTY_GLOOP_UTIL_HASH_BUILTIN_TYPE_HASH_H_
#define THIRD_PARTY_GLOOP_UTIL_HASH_BUILTIN_TYPE_HASH_H_

#include <stddef.h>
#include <stdint.h>

#include <cstdint>

#include "absl/base/casts.h"
#include "gloop/util/hash/jenkins_lookup2.h"

namespace util_hash {

// At the moment this returns a constant, but in the future it may return a
// different constant, or even a value that changes from run to run.  The
// only guarantee is that the value will be constant within a process.
// If you assume some other behavior and our change to this routine breaks
// your assumption, that's your bug.
inline size_t HashSeed() { return 0; }

}  // namespace util_hash

inline uint32_t Hash32NumWithSeed(uint32_t num, uint32_t c) {
  uint32_t b = 0x9e3779b9UL;  // the golden ratio; an arbitrary value
  mix(num, b, c);
  return c;
}

inline uint64_t Hash64NumWithSeed(uint64_t num, uint64_t c) {
  uint64_t b = uint64_t{0xe08c1d668b756f82};  // more of the golden ratio
  mix(num, b, c);
  return c;
}

// This function hashes pointer sized items and returns a 32b hash,
// convenienty hiding the fact that pointers may be 32b or 64b,
// depending on the architecture.
inline uint32_t Hash32PointerWithSeed(const void* p, uint32_t seed) {
  uintptr_t pvalue = reinterpret_cast<uintptr_t>(p);
  uint32_t h = seed;
  // Hash the pointer 32b at a time.
  for (size_t i = 0; i < sizeof(pvalue); i += 4) {
    h = Hash32NumWithSeed(static_cast<uint32_t>(pvalue >> (i * 8)), h);
  }
  return h;
}

// ----------------------------------------------------------------------
// Hash64FloatWithSeed
// Hash64DoubleWithSeed
//   Functions for computing a hash value of floating-point numbers.
//   On systems where float and double comply with IEEE 754, these hashes
//   guarantee that if a == b, Hash64FloatWithSeed(a, c) ==
//   Hash64FloatWithSeed(b, c). Note that NaN does not compare equal to
//   itself, so two NaN inputs will not necessarily hash to the same value.
//
//   It is often a mistake to compare floating-point values for equality,
//   since floating-point computations do not produce exact values, due to
//   rounding. If equality comparison doesn't make sense in your situation,
//   hashing almost certainly doesn't make sense either.
//
//   Not guaranteed to return the same value in different builds, or to
//   avoid any reserved values.
// ----------------------------------------------------------------------
inline uint64_t Hash64FloatWithSeed(float num, uint64_t seed) {
  // +0 and -0 are the only floating point numbers which compare equal but
  // have distinct bitwise representations in IEEE 754. To work around this,
  // we force 0 to be +0.
  if (num == 0) {
    num = 0;
  }
  static_assert(sizeof(float) == sizeof(uint32_t), "float has wrong size");

  const uint64_t kMul = 0xc6a4a7935bd1e995ULL;

  uint64_t a = (absl::bit_cast<uint32_t>(num) + seed) * kMul;
  a ^= (a >> 47);
  a *= kMul;
  a ^= (a >> 47);
  a *= kMul;
  return a;
}

inline uint64_t Hash64DoubleWithSeed(double num, uint64_t seed) {
  if (num == 0) {
    num = 0;
  }
  static_assert(sizeof(double) == sizeof(uint64_t), "double has wrong size");

  const uint64_t kMul = 0xc6a4a7935bd1e995ULL;

  uint64_t a = (absl::bit_cast<uint64_t>(num) + seed) * kMul;
  a ^= (a >> 47);
  a *= kMul;
  a ^= (a >> 47);
  a *= kMul;
  return a;
}

#endif  // THIRD_PARTY_GLOOP_UTIL_HASH_BUILTIN_TYPE_HASH_H_
