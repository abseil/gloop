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

#include "gloop/util/random/acmrandom.h"

#include <cstdint>

#include "absl/log/check.h"
#include "gloop/util/random/random_base.h"

RandomBase* ACMRandom::Clone() const { return new ACMRandom(GetSeed()); }

/* static */
int32_t ACMRandom::HostnamePidTimeSeed() {
  return static_cast<int32_t>(RandomBase::WeakSeed32());
}

int64_t ACMRandom::Next64() {
  // Given: 1 <= Next() <= M-1, where M = 2^31-1
  //
  // Hence: 1 <= (Next() - 1) * (M-1) + Next()
  //          <= (M-2) * (M-1) + (M-1)
  //          =  (M-1) * (M-1)
  //          <  2^62
  //
  // Hence, all results are in the range [1, (2^31-2)^2] and all numbers in
  // the range are equally probable.  The results will never overflow an
  // int64.  Neither will the intermediate results.
  const int64_t next = Next();
  return (next - 1) * (M - 1) + Next();
}

uint8_t ACMRandom::Rand8() {
  return static_cast<uint8_t>((Next() >> 1) & 0x000000ff);
}

uint16_t ACMRandom::Rand16() {
  return static_cast<uint16_t>((Next() >> 1) & 0x0000ffff);
}

// Our range here is [0, 2^31 - 3]
uint32_t ACMRandom::Rand32() { return static_cast<uint32_t>(Next() - 1); }

// Our range here is [0, (2^31-2)^2-1]
uint64_t ACMRandom::Rand64() { return static_cast<uint64_t>(Next64() - 1); }

int32_t ACMRandom::UnbiasedUniform(int32_t n) {
  const uint32_t range = M - 2;
  CHECK_LE(n, static_cast<int32_t>(range));

  if (n == 0) {
    return Next() * 0;
  } else {
    uint32_t rem = range % n;
    uint32_t rnd;
    do {
      rnd = Next();
    } while (rnd <= rem);
    return rnd % n;
  }
}

uint64_t ACMRandom::UnbiasedUniform64(uint64_t n) {
  if (n == 0) {
    return Rand64() * n;  // Consume a value anyway.
  } else if (0 == (n & (n - 1))) {
    // n is a power of two, so just mask off the lower bits.
    return Rand64() & (n - 1);
  } else {
    const uint64_t range = ~static_cast<uint64_t>(0);
    const uint64_t rem = (range % n) + 1;
    uint64_t rnd;
    do {
      uint32_t a =
          (Next() >> 1) & 0x00ffffff;  // mask off lower 24 bits, so that
      uint64_t b = (Next() >> 1) & 0x00ffffff;  // a,b,c are uniform over
      uint64_t c = (Next() >> 1) & 0x00ffffff;  // [0, 2^{24})
      rnd = a | (b << 24) | (c << 48);  // rnd is uniform over [0, 2^{64})
    } while (rnd < rem);  // reject [0, rem)

    // rnd is uniform over [rem, 2^{64}), which contains a
    // multiple of n integers
    return rnd % n;
  }
}
