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

//
// The core Jenkins Lookup2-based hashing routines. These are legacy hashing
// routines and should be avoided in new code. Their implementations are dated
// and cannot be changed due to values being recorded and breaking if not
// preserved. New code which explicitly desires this property should use the
// consistent hashing libraries. New code which does not explicitly desire this
// behavior should use the generic hashing routines in hash.h.

#ifndef THIRD_PARTY_GLOOP_UTIL_HASH_JENKINS_H_
#define THIRD_PARTY_GLOOP_UTIL_HASH_JENKINS_H_

#include <stdlib.h>

#include <cstdint>

#include "absl/base/macros.h"
#include "absl/strings/string_view.h"

static const uint32_t MIX32 = 0x12b9b0a1UL;  // pi; an arbitrary number
static const uint64_t MIX64 = uint64_t{0x2b992ddfa23249d6};  // more of pi

const uint32_t kIllegalHash32 = static_cast<uint32_t>(0xffffffffU);
const uint16_t kIllegalHash16 = static_cast<uint16_t>(0xffffU);

// ----------------------------------------------------------------------
// Hash32StringWithSeed()
// Hash64StringWithSeed()
// Hash32NumWithSeed()
// Hash64NumWithSeed()
//   These are Bob Jenkins' hash functions, one for 32 bit numbers
//   and one for 64 bit numbers.  Each takes a string as input and
//   a start seed.  Hashing the same string with two different seeds
//   should give two independent hash values.
//      The *Num*() functions just do a single mix, in order to
//   convert the given number into something *random*.
//
// Note that these methods may return any value for the given size, while
// the corresponding HashToXX() methods avoids certain reserved values.
// ----------------------------------------------------------------------
// These hash functions are no longer recommended.
// See <link>
uint32_t Hash32StringWithSeed(absl::string_view sv, uint32_t seed);
ABSL_DEPRECATE_AND_INLINE()
inline uint32_t Hash32StringWithSeed(const char* s, size_t len, uint32_t c) {
  return Hash32StringWithSeed(absl::string_view(s, len), c);
}

uint64_t Hash64StringWithSeed(const char* s, size_t len, uint64_t c);

#endif  // THIRD_PARTY_GLOOP_UTIL_HASH_JENKINS_H_
