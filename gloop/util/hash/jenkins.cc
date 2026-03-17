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

// Contains the legacy hashing routines from mobile/util/hash/hash.cc,
// adapted for use in place of the legacy Jenkins newhash-based routines.

#include "gloop/util/hash/jenkins.h"

#include <cstddef>
#include <cstdint>

#include "absl/strings/string_view.h"
#include "gloop/util/hash/jenkins_lookup2.h"

// These functions can be updated using farmhash later.
// The current version is copied from mobile/util/hash/hash.cc.
static const uint32_t kPrimes32[16] = {
    65537, 65539, 65543, 65551, 65557, 65563, 65579, 65581,
    65587, 65599, 65609, 65617, 65629, 65633, 65647, 65651,
};

static const uint64_t kPrimes64[] = {
    uint64_t{4294967311}, uint64_t{4294967357}, uint64_t{4294967371},
    uint64_t{4294967377}, uint64_t{4294967387}, uint64_t{4294967389},
    uint64_t{4294967459}, uint64_t{4294967477}, uint64_t{4294967497},
    uint64_t{4294967513}, uint64_t{4294967539}, uint64_t{4294967543},
    uint64_t{4294967549}, uint64_t{4294967561}, uint64_t{4294967563},
    uint64_t{4294967569}};

uint32_t Hash32StringWithSeedReferenceImplementation(const char* s, size_t len,
                                                     uint32_t seed) {
  uint32_t n = seed;
  size_t prime1 = 0, prime2 = 8;  // Indices into kPrimes32
  union {
    uint16_t n;
    char bytes[sizeof(uint16_t)];
  } chunk;
  for (const char *i = s, *const end = s + len; i != end;) {
    chunk.bytes[0] = *i++;
    chunk.bytes[1] = i == end ? 0 : *i++;
    n = n * kPrimes32[prime1++] ^ chunk.n * kPrimes32[prime2++];
    prime1 &= 0x0F;
    prime2 &= 0x0F;
  }
  return n;
}

uint32_t Hash32StringWithSeed(absl::string_view sv, uint32_t seed) {
  const char* s = sv.data();
  size_t len = sv.size();

  uint32_t a, b;
  b = 0x12f905ffUL;
  uint32_t c = seed;
  while (len > 12) {
    a = Hash32StringWithSeedReferenceImplementation(s, 12, c);
    mix(a, b, c);
    s += 12;
    len -= 12;
  }
  if (len > 0) {
    a = Hash32StringWithSeedReferenceImplementation(s, len, c);
    mix(a, b, c);
  }
  return c;
}

uint64_t Hash64StringWithSeedReferenceImplementation(const char* s, size_t len,
                                                     uint64_t seed) {
  uint64_t n = seed;
  size_t prime1 = 0, prime2 = 8;  // Indices into kPrimes64
  union {
    uint32_t n;
    char bytes[sizeof(uint32_t)];
  } chunk;
  for (const char *i = s, *const end = s + len; i != end;) {
    chunk.bytes[0] = *i++;
    chunk.bytes[1] = i == end ? 0 : *i++;
    chunk.bytes[2] = i == end ? 0 : *i++;
    chunk.bytes[3] = i == end ? 0 : *i++;
    n = n * kPrimes64[prime1++] ^ chunk.n * kPrimes64[prime2++];
    prime1 &= 0x0F;
    prime2 &= 0x0F;
  }
  return n;
}

uint64_t Hash64StringWithSeed(const char* s, size_t len, uint64_t seed) {
  uint64_t a, b;
  b = 0x2c2ca38cd0cc731bULL;
  uint64_t c = seed;
  while (len > 24) {
    a = Hash64StringWithSeedReferenceImplementation(s, 24, c);
    mix(a, b, c);
    s += 24;
    len -= 24;
  }
  if (len > 0) {
    a = Hash64StringWithSeedReferenceImplementation(s, len, c);
    mix(a, b, c);
  }
  return c;
}
