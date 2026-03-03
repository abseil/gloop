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

// Copyright 2006 Google Inc. All Rights Reserved.

//
// Based on code from SecureRandom
#include "gloop/util/random/random_base.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/casts.h"
#include "absl/base/internal/cycleclock.h"
#include "absl/log/check.h"
#include "absl/time/clock.h"
#include "gloop/base/atomic_sequence_num.h"
#include "gloop/base/port.h"

namespace {
ABSL_CONST_INIT base::SequenceNumber weak_seed_sequence;

// clang-format off
// Use the mixing function from hash.h
void hash_mix(uint32_t& a, uint32_t& b, uint32_t& c) {
  a -= b; a -= c; a ^= (c >> 13);
  b -= c; b -= a; b ^= (a << 8);
  c -= a; c -= b; c ^= (b >> 13);
  a -= b; a -= c; a ^= (c >> 12);
  b -= c; b -= a; b ^= (a << 16);
  c -= a; c -= b; c ^= (b >> 5);
  a -= b; a -= c; a ^= (c >> 3);
  b -= c; b -= a; b ^= (a << 10);
  c -= a; c -= b; c ^= (b >> 15);
}

uint32_t Word32At(const uint8_t* ptr) {
  return ((static_cast<uint32_t>(ptr[0])) +
          (static_cast<uint32_t>(ptr[1]) << 8) +
          (static_cast<uint32_t>(ptr[2]) << 16) +
          (static_cast<uint32_t>(ptr[3]) << 24));
}
// clang-format on
}  // namespace

uint32_t RandomBase::WeakSeed32() {
  constexpr int kBufferSize = PATH_MAX + 3 * sizeof(uint32_t);
  std::vector<uint8_t> buffer(kBufferSize);

  // Initialize the buffer with weak random contents, leaving room
  // for 3 uint32 guard values (0) past the end of the buffer.
  int len = WeakSeed(buffer.data(), PATH_MAX);

  // Ensure that we still have at least 3 * sizeof(uint32) bytes
  // in the buffer after len, and zero those values.
  int remaining = buffer.size() - len;
  CHECK_GT(remaining, static_cast<int>(3 * sizeof(uint32_t)));
  memset(buffer.data() + len, 0, 3 * sizeof(uint32_t));

  uint32_t a = Word32At(buffer.data());
  uint32_t b = Word32At(buffer.data() + sizeof(uint32_t));
  uint32_t c = 0;
  for (int i = sizeof(uint32_t) * 2; i < len; i += sizeof(uint32_t) * 3) {
    hash_mix(a, b, c);
    a += Word32At(buffer.data() + i);
    b += Word32At(buffer.data() + i + sizeof(uint32_t));
    c += Word32At(buffer.data() + i + sizeof(uint32_t) + sizeof(uint32_t));
  }
  c += len;
  hash_mix(a, b, c);
  return c;
}

int RandomBase::WeakSeed(uint8_t* buffer, int length) {
  int offset = 0;
  char* seed_buffer = reinterpret_cast<char*>(buffer);

  // The low order 16 bits of a sequence.
  if (length >= offset + 2) {
    UNALIGNED_STORE16(seed_buffer + offset, weak_seed_sequence.GetNext());
    offset += 2;
  }

  // CycleClock
  if (length >= offset + 8) {
    UNALIGNED_STORE64(
        seed_buffer + offset,
        absl::base_internal::CycleClock::Now()  // NOLINT(cycleclock)
    );
    offset += 8;
  }

  // Time of day.
  if (length > offset) {
    auto nanos = absl::GetCurrentTimeNanos();
    size_t len = std::min(sizeof(nanos), static_cast<size_t>(length - offset));
    memcpy(seed_buffer + offset, &nanos, len);
    offset += static_cast<int>(len);
  }

  // Get the hostname.
  if (length > offset &&
      gethostname(seed_buffer + offset, length - offset) == 0) {
    offset += static_cast<int>(strlen(seed_buffer + offset));
  }
  return offset;
}

// Note that for secure random number generators based on
// block ciphers, extracting output from the generator one byte at a
// time is somewhat inefficient.  Block-cipher--based RNGs may override
// this definition.
std::string RandomBase::RandString(int desired_len) {
  CHECK_GE(desired_len, 0);

  std::string result;
  result.resize(desired_len);
  for (std::string::iterator it = result.begin(); it != result.end(); ++it) {
    *it = Rand8();
  }
  return result;
}

// precondition: n >= 0.
int32_t RandomBase::UnbiasedUniform(int32_t n) {
  CHECK_LE(0, n);
  const uint32_t range = ~static_cast<uint32_t>(0);
  if (n == 0) {
    return Rand32() * n;
  }
  if (0 == (n & (n - 1))) {
    // N is a power of two, so just mask off the lower bits.
    return Rand32() & (n - 1);
  }

  // Reject all numbers that skew the distribution towards 0.

  // Rand32's output is uniform in the half-open interval [0, 2^{32}).
  // For any interval [m,n), the number of elements in it is n-m.

  const uint32_t rem = (range % n) + 1;
  uint32_t rnd;

  // rem = ((2^{32}-1) \bmod n) + 1
  // 1 <= rem <= n

  // NB: rem == n is impossible, since n is not a power of 2 (from
  // earlier check).

  do {
    rnd = Rand32();  // rnd uniform over [0, 2^{32})
  } while (rnd < rem);  // reject [0, rem)
  // rnd is uniform over [rem, 2^{32})
  //
  // The number of elements in the half-open interval is
  //
  //  2^{32} - rem = 2^{32} - ((2^{32}-1) \bmod n) - 1
  //               = 2^{32}-1 - ((2^{32}-1) \bmod n)
  //               = n \cdot \lfloor (2^{32}-1)/n \rfloor
  //
  // therefore n evenly divides the number of integers in the
  // interval.
  //
  // The function v \rightarrow v % n takes values from [bias,
  // 2^{32}) to [0, n).  Each integer in the range interval [0, n)
  // will have exactly \lfloor (2^{32}-1)/n \rfloor preimages from
  // the domain interval.
  //
  // Therefore, v % n is uniform over [0, n).  QED.

  return rnd % n;
}

uint64_t RandomBase::UnbiasedUniform64(uint64_t n) {
  if (n == 0) {
    return Rand64() * n;  // Consume a value anyway.
  }
  if (0 == (n & (n - 1))) {
    // n is a power of two, so just mask off the lower bits.
    return Rand64() & (n - 1);
  }
  const uint64_t range = ~static_cast<uint64_t>(0);
  const uint64_t rem = (range % n) + 1;
  uint64_t rnd;

  do {
    rnd = Rand64();  // rnd is uniform over [0, 2^{64})
  } while (rnd < rem);  // reject [0, rem)
  // rnd is uniform over [rem, 2^{64}), which contains a multiple of
  // n integers
  return rnd % n;
}

float RandomBase::RandFloat() {
  // IEEE754 floats are formatted as follows (MSB first)
  //    sign(1) exponent(8) mantissa(23)
  // Conceptually construct the following:
  //    sign == 0
  //    exponent == 127  -- an excess 127 representation of a zero exponent
  //    mantissa == 23 random bits
  uint32_t man = Rand32() & 0x7fffffu;  // 23 bit mantissa
  uint32_t exp = static_cast<uint32_t>(127);
  uint32_t val = (exp << 23) | man;

  // Assumes that endian-ness is same for float and uint32.
  return absl::bit_cast<float>(val) - static_cast<float>(1.0);
}

double RandomBase::RandDouble() {
  // IEEE754 doubles are formatted as follows (MSB first)
  //    sign(1) exponent(11) mantissa(52)
  // Conceptually construct the following:
  //    sign == 0
  //    exponent == 1023  -- an excess 1023 representation of a zero exponent
  //    mantissa == 52 random bits
  uint32_t mhi = Rand32() & 0xfffffu;  // upper 20 bits of mantissa
  uint32_t mlo = Rand32();             // lower 32 bits of mantissa
  uint64_t man = (static_cast<uint64_t>(mhi) << 32) | mlo;  // mantissa
  uint64_t exp = static_cast<uint64_t>(1023);
  uint64_t val = (exp << 52) | man;

  // Assumes that endian-ness is same for double and uint64.
  return absl::bit_cast<double>(val) - static_cast<double>(1.0);
}

double RandomBase::RandExponential() { return -log1p(-RandDouble()); }
