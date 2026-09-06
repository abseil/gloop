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

#include "gloop/util/math/round.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <string>

#include "absl/log/check.h"
#include "absl/numeric/bits.h"
#include "absl/random/distributions.h"
#include "absl/strings/str_format.h"
#include "benchmark/benchmark.h"
#include "gloop/strings/numbers.h"
#include "gloop/util/bits/bits.h"
#include "gtest/gtest.h"

namespace util {
namespace math {
namespace {

TEST(DivideRoundedUp, Sanity) {
  EXPECT_EQ(11, divide_rounded_up(31, 3));
  EXPECT_EQ(10, divide_rounded_up(30, 3));
  EXPECT_EQ(10, divide_rounded_up(29, 3));
  EXPECT_EQ(10, divide_rounded_up(28, 3));
  EXPECT_EQ(9, divide_rounded_up(27, 3));
}

TEST(DivideRoundedUp, Zero) {
  EXPECT_EQ(0, divide_rounded_up(0, 5));
  EXPECT_EQ(0, divide_rounded_up(5, 0));
  EXPECT_EQ(0, divide_rounded_up(0, 0));
}

TEST(DivideRoundedUp, One) {
  for (int i = 0; i <= 10; ++i) {
    EXPECT_EQ(i, divide_rounded_up(i, 1));
  }
}

TEST(DivideRounded, Sanity) {
  EXPECT_EQ(10, divide_rounded_uint64(29, 3));
  EXPECT_EQ(9, divide_rounded_uint64(28, 3));
  EXPECT_EQ(9, divide_rounded_uint64(27, 3));
  EXPECT_EQ(11, divide_rounded_uint64(43, 4));
  EXPECT_EQ(11, divide_rounded_uint64(42, 4));
  EXPECT_EQ(10, divide_rounded_uint64(41, 4));
  EXPECT_EQ(10, divide_rounded_uint64(40, 4));
}

TEST(DivideRounded, Zero) {
  EXPECT_EQ(0, divide_rounded_uint64(5, 0));
  EXPECT_EQ(0, divide_rounded_uint64(0, 0));
}

TEST(DivideRounded, Overflow) {
  EXPECT_EQ(1, divide_rounded_uint64(std::numeric_limits<uint64_t>::max() - 1,
                                     std::numeric_limits<uint64_t>::max()));
}

TEST(DivideRounded, One) {
  for (int i = 0; i <= 10; ++i) {
    EXPECT_EQ(i, divide_rounded_uint64(i, 1));
  }
}

// Check all variants of RoundToNSignificantBits for v and n, and also check
// the signed variants using -v and n.
void BinarySpotCheck(int64_t v, int n, int64_t expected) {
  EXPECT_EQ(expected, RoundToNSignificantBitsUint64(v, n)) << v << " " << n;
  EXPECT_EQ(expected, RoundToNSignificantBitsInt64(v, n)) << v << " " << n;
  EXPECT_EQ(-expected, RoundToNSignificantBitsInt64(-v, n)) << v << " " << n;
  EXPECT_EQ(expected, RoundToNSignificantBitsUint32(v, n)) << v << " " << n;
  EXPECT_EQ(expected, RoundToNSignificantBitsInt32(v, n)) << v << " " << n;
  EXPECT_EQ(-expected, RoundToNSignificantBitsInt32(-v, n)) << v << " " << n;
}

// Hand-check a few simple cases with small numbers.
TEST(BinaryRounding, SpotCheck) {
  BinarySpotCheck(0, 7, 0);
  BinarySpotCheck(1, 3, 1);
  BinarySpotCheck(2, 1, 2);
  BinarySpotCheck(2, 2, 2);
  BinarySpotCheck(2, 3, 2);
  BinarySpotCheck(2, 63, 2);
  BinarySpotCheck(15, 4, 15);
  BinarySpotCheck(16, 4, 16);
  BinarySpotCheck(17, 4, 18);
  BinarySpotCheck(18, 4, 18);
  BinarySpotCheck(19, 4, 20);
  BinarySpotCheck(75, 4, 72);
  BinarySpotCheck(76, 4, 80);
}

// Rounding to zero significant bits always returns 0.
TEST(BinaryRounding, ZeroN) {
  EXPECT_EQ(0, RoundToNSignificantBitsUint64(17, 0));
  EXPECT_EQ(0, RoundToNSignificantBitsInt64(17, 0));
  EXPECT_EQ(0, RoundToNSignificantBitsUint32(17, 0));
  EXPECT_EQ(0, RoundToNSignificantBitsInt32(17, 0));
}

// Rounding to a negative number of significant bits always returns 0.
TEST(BinaryRounding, NegativeN) {
  EXPECT_EQ(0, RoundToNSignificantBitsUint64(17, -3));
  EXPECT_EQ(0, RoundToNSignificantBitsInt64(17, -3));
  EXPECT_EQ(0, RoundToNSignificantBitsUint32(17, -3));
  EXPECT_EQ(0, RoundToNSignificantBitsInt32(17, -3));
}

// Rounding to a large number of significant bits always returns the exact
// argument (with no rounding).
TEST(BinaryRounding, HugeN) {
  EXPECT_EQ(17, RoundToNSignificantBitsUint64(
                    17, std::numeric_limits<int32_t>::max()));
  EXPECT_EQ(17, RoundToNSignificantBitsInt64(
                    17, std::numeric_limits<int32_t>::max()));
  EXPECT_EQ(17, RoundToNSignificantBitsUint32(
                    17, std::numeric_limits<int32_t>::max()));
  EXPECT_EQ(17, RoundToNSignificantBitsInt32(
                    17, std::numeric_limits<int32_t>::max()));
}

// Rounding to a negative number of significant bits always returns 0.
TEST(BinaryRounding, HugeNegativeN) {
  EXPECT_EQ(0, RoundToNSignificantBitsUint64(
                   17, std::numeric_limits<int32_t>::min()));
  EXPECT_EQ(
      0, RoundToNSignificantBitsInt64(17, std::numeric_limits<int32_t>::min()));
  EXPECT_EQ(0, RoundToNSignificantBitsUint32(
                   17, std::numeric_limits<int32_t>::min()));
  EXPECT_EQ(
      0, RoundToNSignificantBitsInt32(17, std::numeric_limits<int32_t>::min()));
}

// Rounding 0 to any number of bits always returns 0.
TEST(BinaryRounding, Zero) {
  for (int i = -3; i <= 65; i++) {
    EXPECT_EQ(0, RoundToNSignificantBitsUint64(0, i)) << i;
    EXPECT_EQ(0, RoundToNSignificantBitsInt64(0, i)) << i;
    EXPECT_EQ(0, RoundToNSignificantBitsUint32(0, i)) << i;
    EXPECT_EQ(0, RoundToNSignificantBitsInt32(0, i)) << i;
  }
}

// Rounding 1 to any number of bits >= 1 returns 1.
TEST(BinaryRounding, One) {
  for (int i = 1; i <= 65; i++) {
    EXPECT_EQ(1, RoundToNSignificantBitsUint64(1, i)) << i;
    EXPECT_EQ(1, RoundToNSignificantBitsInt64(1, i)) << i;
    EXPECT_EQ(1, RoundToNSignificantBitsUint32(1, i)) << i;
    EXPECT_EQ(1, RoundToNSignificantBitsInt32(1, i)) << i;
  }
  EXPECT_EQ(0, RoundToNSignificantBitsUint64(1, 0));
  EXPECT_EQ(0, RoundToNSignificantBitsInt64(1, 0));
  EXPECT_EQ(0, RoundToNSignificantBitsUint32(1, -1));
  EXPECT_EQ(0, RoundToNSignificantBitsInt32(1, -1));
}

// Rounding -1 to any number of bits >= -1 returns 1.
TEST(BinaryRounding, MinusOne) {
  for (int i = 1; i <= 65; i++) {
    EXPECT_EQ(-1, RoundToNSignificantBitsInt64(-1, i)) << i;
    EXPECT_EQ(-1, RoundToNSignificantBitsInt32(-1, i)) << i;
  }
  EXPECT_EQ(0, RoundToNSignificantBitsInt64(-1, 0));
  EXPECT_EQ(0, RoundToNSignificantBitsInt32(-1, -1));
}

// Rounding kuint64max to n bits returns a bit pattern of n leading ones.
TEST(BinaryRounding, Uint64Max) {
  for (int i = 1; i <= 64; i++) {
    int64_t v =
        RoundToNSignificantBitsUint64(std::numeric_limits<uint64_t>::max(), i);
    CHECK_NE(0, v);
    EXPECT_EQ(63, Bits::FindMSBSetNonZero64(v));
    EXPECT_EQ(i, absl::popcount(static_cast<uint64_t>(v)));
  }
  EXPECT_EQ(
      std::numeric_limits<uint64_t>::max(),
      RoundToNSignificantBitsInt64(std::numeric_limits<uint64_t>::max(), 64));
  EXPECT_EQ(
      std::numeric_limits<uint64_t>::max(),
      RoundToNSignificantBitsInt64(std::numeric_limits<uint64_t>::max(), 65));
  EXPECT_EQ(
      std::numeric_limits<uint64_t>::max(),
      RoundToNSignificantBitsInt64(std::numeric_limits<uint64_t>::max(), 66));
  EXPECT_EQ(std::numeric_limits<uint64_t>::max(),
            RoundToNSignificantBitsInt64(std::numeric_limits<uint64_t>::max(),
                                         12345));
  EXPECT_EQ(0, RoundToNSignificantBitsUint64(
                   std::numeric_limits<uint64_t>::max(), 0));
  EXPECT_EQ(0, RoundToNSignificantBitsUint64(
                   std::numeric_limits<uint64_t>::max(), -1));
  EXPECT_EQ(0, RoundToNSignificantBitsUint64(
                   std::numeric_limits<uint64_t>::max(), -12345));
}

// Rounding kint64max to n bits returns a bit pattern of a zero bit followed by
// n ones.
TEST(BinaryRounding, Int64Max) {
  for (int i = 1; i <= 65; i++) {
    int64_t v =
        RoundToNSignificantBitsInt64(std::numeric_limits<int64_t>::max(), i);
    CHECK_GT(v, 0) << i;
    EXPECT_EQ(62, Bits::FindMSBSetNonZero64(v));
    if (i >= 63) {
      EXPECT_EQ(63, absl::popcount(static_cast<uint64_t>(v)));
    } else {
      EXPECT_EQ(i, absl::popcount(static_cast<uint64_t>(v)));
    }
  }
  EXPECT_EQ(
      0, RoundToNSignificantBitsInt64(std::numeric_limits<int64_t>::max(), 0));
  EXPECT_EQ(
      0, RoundToNSignificantBitsInt64(std::numeric_limits<int64_t>::max(), -1));
  EXPECT_EQ(0, RoundToNSignificantBitsInt64(std::numeric_limits<int64_t>::max(),
                                            -12345));
}

// Rounding kuint32max to n bits returns a bit pattern of n leading ones.
TEST(BinaryRounding, Uint32Max) {
  for (int i = 1; i <= 32; i++) {
    int64_t v =
        RoundToNSignificantBitsUint32(std::numeric_limits<uint32_t>::max(), i);
    CHECK_NE(0, v);
    EXPECT_EQ(31, Bits::FindMSBSetNonZero(v));
    EXPECT_EQ(i, absl::popcount(static_cast<uint32_t>(v)));
  }
  EXPECT_EQ(
      std::numeric_limits<uint32_t>::max(),
      RoundToNSignificantBitsInt32(std::numeric_limits<uint32_t>::max(), 64));
  EXPECT_EQ(
      std::numeric_limits<uint32_t>::max(),
      RoundToNSignificantBitsInt32(std::numeric_limits<uint32_t>::max(), 65));
  EXPECT_EQ(
      std::numeric_limits<uint32_t>::max(),
      RoundToNSignificantBitsInt32(std::numeric_limits<uint32_t>::max(), 66));
  EXPECT_EQ(std::numeric_limits<uint32_t>::max(),
            RoundToNSignificantBitsInt32(std::numeric_limits<uint32_t>::max(),
                                         12345));
  EXPECT_EQ(0, RoundToNSignificantBitsUint32(
                   std::numeric_limits<uint32_t>::max(), 0));
  EXPECT_EQ(0, RoundToNSignificantBitsUint32(
                   std::numeric_limits<uint32_t>::max(), -1));
  EXPECT_EQ(0, RoundToNSignificantBitsUint32(
                   std::numeric_limits<uint32_t>::max(), -12345));
}

// Rounding int32max to n bits returns a bit pattern of a zero bit followed by
// n ones.
TEST(BinaryRounding, Int32Max) {
  for (int i = 1; i <= 33; i++) {
    int64_t v =
        RoundToNSignificantBitsInt32(std::numeric_limits<int32_t>::max(), i);
    CHECK_GT(v, 0);
    EXPECT_EQ(30, Bits::FindMSBSetNonZero(v));
    if (i >= 31) {
      EXPECT_EQ(31, absl::popcount(static_cast<uint32_t>(v)));
    } else {
      EXPECT_EQ(i, absl::popcount(static_cast<uint32_t>(v)));
    }
  }
  EXPECT_EQ(
      0, RoundToNSignificantBitsInt32(std::numeric_limits<int32_t>::max(), 0));
  EXPECT_EQ(
      0, RoundToNSignificantBitsInt32(std::numeric_limits<int32_t>::max(), -1));
  EXPECT_EQ(0, RoundToNSignificantBitsInt32(std::numeric_limits<int32_t>::max(),
                                            -12345));
}

// Rounding int64min returns int64min.
TEST(BinaryRounding, Int64Min) {
  for (int i = 1; i <= 65; i++) {
    EXPECT_EQ(
        std::numeric_limits<int64_t>::min(),
        RoundToNSignificantBitsInt64(std::numeric_limits<int64_t>::min(), i))
        << i;
  }
  EXPECT_EQ(
      0, RoundToNSignificantBitsInt64(std::numeric_limits<int64_t>::min(), 0));
  EXPECT_EQ(
      0, RoundToNSignificantBitsInt64(std::numeric_limits<int64_t>::min(), -1));
  EXPECT_EQ(0, RoundToNSignificantBitsInt64(std::numeric_limits<int64_t>::min(),
                                            -12345));
}

// Rounding int32min returns int32min.
TEST(BinaryRounding, Int32Min) {
  for (int i = 1; i <= 33; i++) {
    EXPECT_EQ(
        std::numeric_limits<int32_t>::min(),
        RoundToNSignificantBitsInt32(std::numeric_limits<int32_t>::min(), i))
        << i;
  }
  EXPECT_EQ(
      0, RoundToNSignificantBitsInt32(std::numeric_limits<int32_t>::min(), 0));
  EXPECT_EQ(
      0, RoundToNSignificantBitsInt32(std::numeric_limits<int32_t>::min(), -1));
  EXPECT_EQ(0, RoundToNSignificantBitsInt32(std::numeric_limits<int32_t>::min(),
                                            -12345));
}

// Rounding a value close to uint64max should not overflow.
TEST(BinaryRounding, AlmostUint64Max) {
  for (int i = 1; i <= 65; i++) {
    uint64_t v = RoundToNSignificantBitsUint64(
        std::numeric_limits<uint64_t>::max() - 1, i);
    CHECK_NE(0, v);
    EXPECT_EQ(63, Bits::FindMSBSetNonZero64(v));
    if (i >= 63) {
      EXPECT_EQ(63, absl::popcount(v));
      EXPECT_EQ(1, Bits::FindLSBSetNonZero64(v));
    } else {
      EXPECT_EQ(i, absl::popcount(v));
      EXPECT_EQ(64 - i, Bits::FindLSBSetNonZero64(v));
    }
  }
  EXPECT_EQ(0, RoundToNSignificantBitsUint64(
                   std::numeric_limits<uint64_t>::max() - 1, 0));
  EXPECT_EQ(0, RoundToNSignificantBitsUint64(
                   std::numeric_limits<uint64_t>::max() - 1, -1));
  EXPECT_EQ(0, RoundToNSignificantBitsUint64(
                   std::numeric_limits<uint64_t>::max() - 1, -12345));
}

// Rounding a value close to int64max should not overflow.
TEST(BinaryRounding, AlmostInt64Max) {
  for (int i = 1; i <= 65; i++) {
    int64_t v = RoundToNSignificantBitsInt64(
        std::numeric_limits<int64_t>::max() - 1, i);
    CHECK_GT(v, 0);
    EXPECT_EQ(62, Bits::FindMSBSetNonZero64(v));
    if (i >= 62) {
      EXPECT_EQ(62, absl::popcount(static_cast<uint64_t>(v)));
      EXPECT_EQ(1, Bits::FindLSBSetNonZero64(v));
    } else {
      EXPECT_EQ(i, absl::popcount(static_cast<uint64_t>(v)));
      EXPECT_EQ(63 - i, Bits::FindLSBSetNonZero64(v));
    }
  }
  EXPECT_EQ(0, RoundToNSignificantBitsInt64(
                   std::numeric_limits<int64_t>::max() - 1, 0));
  EXPECT_EQ(0, RoundToNSignificantBitsInt64(
                   std::numeric_limits<int64_t>::max() - 1, -1));
  EXPECT_EQ(0, RoundToNSignificantBitsInt64(
                   std::numeric_limits<int64_t>::max() - 1, -12345));
}

// Rounding a value close to uint32max should not overflow.
TEST(BinaryRounding, AlmostUint32Max) {
  for (int i = 1; i <= 33; i++) {
    uint32_t v = RoundToNSignificantBitsUint32(
        std::numeric_limits<uint32_t>::max() - 1, i);
    CHECK_NE(0, v);
    EXPECT_EQ(31, Bits::FindMSBSetNonZero(v));
    if (i >= 31) {
      EXPECT_EQ(31, absl::popcount(v));
      EXPECT_EQ(1, Bits::FindLSBSetNonZero(v));
    } else {
      EXPECT_EQ(i, absl::popcount(v));
      EXPECT_EQ(32 - i, Bits::FindLSBSetNonZero(v));
    }
  }
  EXPECT_EQ(0, RoundToNSignificantBitsUint32(
                   std::numeric_limits<uint32_t>::max() - 1, 0));
  EXPECT_EQ(0, RoundToNSignificantBitsUint32(
                   std::numeric_limits<uint32_t>::max() - 1, -1));
  EXPECT_EQ(0, RoundToNSignificantBitsUint32(
                   std::numeric_limits<uint32_t>::max() - 1, -12345));
}

// Rounding a value close to int32max should not overflow.
TEST(BinaryRounding, AlmostInt32Max) {
  for (int i = 1; i <= 33; i++) {
    int32_t v = RoundToNSignificantBitsInt32(
        std::numeric_limits<int32_t>::max() - 1, i);
    CHECK_GT(v, 0);
    EXPECT_EQ(30, Bits::FindMSBSetNonZero(v));
    if (i >= 30) {
      EXPECT_EQ(30, absl::popcount(static_cast<uint32_t>(v)));
      EXPECT_EQ(1, Bits::FindLSBSetNonZero(v));
    } else {
      EXPECT_EQ(i, absl::popcount(static_cast<uint32_t>(v)));
      EXPECT_EQ(31 - i, Bits::FindLSBSetNonZero(v));
    }
  }
  EXPECT_EQ(0, RoundToNSignificantBitsInt64(
                   std::numeric_limits<int64_t>::max() - 1, 0));
  EXPECT_EQ(0, RoundToNSignificantBitsInt64(
                   std::numeric_limits<int64_t>::max() - 1, -1));
  EXPECT_EQ(0, RoundToNSignificantBitsInt64(
                   std::numeric_limits<int64_t>::max() - 1, -12345));
}

// Rounding a value close to int64min should not overflow.
TEST(BinaryRounding, AlmostInt64Min) {
  for (int i = 1; i <= 62; i++) {
    EXPECT_EQ(std::numeric_limits<int64_t>::min(),
              RoundToNSignificantBitsInt64(
                  std::numeric_limits<int64_t>::min() + 1, i))
        << i;
  }
  EXPECT_EQ(std::numeric_limits<int64_t>::min() + 1,
            RoundToNSignificantBitsInt64(
                std::numeric_limits<int64_t>::min() + 1, 63));
  EXPECT_EQ(std::numeric_limits<int64_t>::min() + 1,
            RoundToNSignificantBitsInt64(
                std::numeric_limits<int64_t>::min() + 1, 64));
  EXPECT_EQ(std::numeric_limits<int64_t>::min() + 1,
            RoundToNSignificantBitsInt64(
                std::numeric_limits<int64_t>::min() + 1, 65));
  EXPECT_EQ(0, RoundToNSignificantBitsInt64(
                   std::numeric_limits<int32_t>::min() + 1, 0));
  EXPECT_EQ(0, RoundToNSignificantBitsInt64(
                   std::numeric_limits<int32_t>::min() + 1, -1));
  EXPECT_EQ(0, RoundToNSignificantBitsInt64(
                   std::numeric_limits<int32_t>::min() + 1, -12345));
}

// Rounding a value close to int32min should not overflow.
TEST(BinaryRounding, AlmostInt32Min) {
  for (int i = 1; i <= 30; i++) {
    EXPECT_EQ(std::numeric_limits<int32_t>::min(),
              RoundToNSignificantBitsInt32(
                  std::numeric_limits<int32_t>::min() + 1, i))
        << i;
  }
  EXPECT_EQ(std::numeric_limits<int32_t>::min() + 1,
            RoundToNSignificantBitsInt32(
                std::numeric_limits<int32_t>::min() + 1, 31));
  EXPECT_EQ(std::numeric_limits<int32_t>::min() + 1,
            RoundToNSignificantBitsInt32(
                std::numeric_limits<int32_t>::min() + 1, 32));
  EXPECT_EQ(std::numeric_limits<int32_t>::min() + 1,
            RoundToNSignificantBitsInt32(
                std::numeric_limits<int32_t>::min() + 1, 33));
  EXPECT_EQ(0, RoundToNSignificantBitsInt32(
                   std::numeric_limits<int32_t>::min() + 1, 0));
  EXPECT_EQ(0, RoundToNSignificantBitsInt32(
                   std::numeric_limits<int32_t>::min() + 1, -1));
  EXPECT_EQ(0, RoundToNSignificantBitsInt32(
                   std::numeric_limits<int32_t>::min() + 1, -12345));
}

// Rounding an exact power of two to any number of bits (>0) returns itself.
TEST(BinaryRounding, PowersOfTwo) {
  for (int i = 1; i <= 64; i++) {
    for (int j = 0; j < 64; j++) {
      uint64_t v = 1ULL << j;
      EXPECT_EQ(v, RoundToNSignificantBitsUint64(v, i)) << i << " " << j;
      EXPECT_EQ(v, RoundToNSignificantBitsInt64(v, i)) << i << " " << j;
      if (j < 32) {
        EXPECT_EQ(v, RoundToNSignificantBitsUint32(v, i)) << i << " " << j;
        EXPECT_EQ(v, static_cast<uint32_t>(RoundToNSignificantBitsInt32(v, i)))
            << i << " " << j;
      }
    }
  }
}

// Rounding uses round-half-up to break ties.
TEST(BinaryRounding, HalfUp) {
  for (int i = 0; i < 62; i++) {
    uint64_t a = 3ULL << i;
    uint64_t b = 4ULL << i;
    uint64_t c = 5ULL << i;
    EXPECT_EQ(b, RoundToNSignificantBitsUint64(a, 1)) << i;
    EXPECT_EQ(b, RoundToNSignificantBitsUint64(c, 1)) << i;
    if (i < 61) {
      EXPECT_EQ(b, RoundToNSignificantBitsInt64(a, 1)) << i;
      EXPECT_EQ(b, RoundToNSignificantBitsInt64(c, 1)) << i;
      EXPECT_EQ(-b, RoundToNSignificantBitsInt64(-a, 1)) << i;
      EXPECT_EQ(-b, RoundToNSignificantBitsInt64(-c, 1)) << i;
    }
    if (i < 30) {
      EXPECT_EQ(b, RoundToNSignificantBitsUint32(a, 1)) << i;
      EXPECT_EQ(b, RoundToNSignificantBitsUint32(c, 1)) << i;
    }
    if (i < 29) {
      EXPECT_EQ(b, RoundToNSignificantBitsInt32(a, 1)) << i;
      EXPECT_EQ(b, RoundToNSignificantBitsInt32(c, 1)) << i;
      EXPECT_EQ(-b, RoundToNSignificantBitsInt32(-a, 1)) << i;
      EXPECT_EQ(-b, RoundToNSignificantBitsInt32(-c, 1)) << i;
    }
  }
}

// Confirm that r is a reasonable value for RoundToNSignificantBits(v, n)
void CheckBinaryRounding(uint64_t v, int n, uint64_t r) {
  if (v == 0) {
    EXPECT_EQ(0, r) << v << " " << n;
    return;
  }
  EXPECT_NE(0, r);
  int vmsb = Bits::FindMSBSetNonZero64(v);
  int rmsb = Bits::FindMSBSetNonZero64(r);
  int rlsb = Bits::FindLSBSetNonZero64(r);
  CHECK_GE(rmsb, rlsb);
  if (vmsb != rmsb) {
    EXPECT_EQ(vmsb + 1, rmsb) << v << " " << n << " " << r;
    EXPECT_EQ(rmsb, rlsb) << v << " " << n << " " << r;
  } else {
    EXPECT_LT(rmsb - rlsb, n) << v << " " << n << " " << r;
  }
  uint64_t vs, rs;
  if (vmsb >= n) {
    vs = v >> (vmsb + 1 - n);
    rs = r >> (vmsb + 1 - n);
  } else {
    vs = v;
    rs = r;
  }
  if (vs != rs) {
    EXPECT_EQ(vs + 1, rs) << v << " " << n << " " << r;
  }
}

// Confirm that the value of RoundToNSignificantBitsUint64(v, n) is
// reasonable.
void CheckBinaryRoundingUint64(uint64_t v, int n) {
  uint64_t r = RoundToNSignificantBitsUint64(v, n);
  if (n <= 0) {
    EXPECT_EQ(0, r) << v << " " << n;
    return;
  }
  if (n >= 64) {
    EXPECT_EQ(v, r) << n;
    return;
  }
  CheckBinaryRounding(v, n, r);
}

// Confirm that the value of RoundToNSignificantBitsInt64(v, n) is reasonable.
void CheckBinaryRoundingInt64(int64_t v, int n) {
  int64_t r = RoundToNSignificantBitsInt64(v, n);
  if (n <= 0) {
    EXPECT_EQ(0, r) << v << " " << n;
    return;
  }
  if (n >= 63) {
    EXPECT_EQ(v, r) << n;
    return;
  }
  // The following block can't be run when r is kint64min because -kint64min is
  // an undefined operation.
  if (r > std::numeric_limits<int64_t>::min()) {
    EXPECT_EQ((v > 0), (r > 0)) << v << " " << n;
    if (v < 0) {
      v = -v;
      r = -r;
    }
    CheckBinaryRounding(v, n, r);
  }
}

// Confirm that the value of RoundToNSignificantBitsUint32(v, n) is
// reasonable.
void CheckBinaryRoundingUint32(uint32_t v, int n) {
  uint32_t r = RoundToNSignificantBitsUint32(v, n);
  if (n <= 0) {
    EXPECT_EQ(0, r) << v << " " << n;
    return;
  }
  if (n >= 32) {
    EXPECT_EQ(v, r) << n;
    return;
  }
  CheckBinaryRounding(v, n, r);
}

// Confirm that the value of RoundToNSignificantBitsInt32(v, n) is reasonable.
void CheckBinaryRoundingInt32(int32_t v, int n) {
  int32_t r = RoundToNSignificantBitsInt32(v, n);
  if (n <= 0) {
    EXPECT_EQ(0, r) << v << " " << n;
    return;
  }
  if (n >= 31) {
    EXPECT_EQ(v, r) << n;
    return;
  }
  EXPECT_EQ((v > 0), (r > 0)) << v << " " << n;
  if (v < 0) {
    v = -static_cast<uint32_t>(v);
    r = -static_cast<uint32_t>(r);
  }
  CheckBinaryRounding(static_cast<uint32_t>(v), n, static_cast<uint32_t>(r));
}

// Use a reproducible pseudo-random number generator to check a large number
// of calls to all variants of RoundToNSignificantBits.
TEST(BinaryRounding, Random) {
  std::mt19937 r(12345678);
  for (int i = 0; i < 1000000; i++) {
    int64_t v = absl::Uniform<uint64_t>(r);
    int n = absl::Uniform<uint8_t>(r);
    CheckBinaryRoundingUint64(v, n & 63);
    CheckBinaryRoundingInt64(v, n & 63);
    CheckBinaryRoundingUint32(v, n & 31);
    CheckBinaryRoundingInt32(v, n & 31);
  }
}

// Hand-check a few simple and edge cases
TEST(Log10Floor, SpotCheck) {
  EXPECT_EQ(-1, Log10Floor(0));
  EXPECT_EQ(0, Log10Floor(1));
  EXPECT_EQ(0, Log10Floor(2));
  EXPECT_EQ(0, Log10Floor(8));
  EXPECT_EQ(0, Log10Floor(9));
  EXPECT_EQ(1, Log10Floor(10));
  EXPECT_EQ(1, Log10Floor(99));
  EXPECT_EQ(2, Log10Floor(100));
  EXPECT_EQ(18, Log10Floor(1000000000000000000LL));
  EXPECT_EQ(18, Log10Floor(1000000000000000001LL));
  EXPECT_EQ(18, Log10Floor(std::numeric_limits<int64_t>::max()));
  EXPECT_EQ(18, Log10Floor(9999999999999999998ULL));
  EXPECT_EQ(18, Log10Floor(9999999999999999999ULL));
  EXPECT_EQ(19, Log10Floor(10000000000000000000ULL));
  EXPECT_EQ(19, Log10Floor(10000000000000000001ULL));
  EXPECT_EQ(19, Log10Floor(10000000000000000002ULL));
  EXPECT_EQ(19, Log10Floor(std::numeric_limits<uint64_t>::max()));
}

// Check the result of Log10Floor applied to values close to exact powers of 10
// (e.g. 999, 1000, 1001, 9999, 10000, 10001)
TEST(Log10Floor, TransitionPoints) {
  int power = 1;
  uint64_t decade = 10;
  for (;;) {
    EXPECT_EQ(power - 1, Log10Floor(decade - 2)) << decade;
    EXPECT_EQ(power - 1, Log10Floor(decade - 1)) << decade;
    EXPECT_EQ(power, Log10Floor(decade)) << decade;
    EXPECT_EQ(power, Log10Floor(decade + 1)) << decade;
    EXPECT_EQ(power, Log10Floor(decade + 2)) << decade;
    if (power == 18) {
      break;
    }
    power++;
    decade *= 10ULL;
  }
}

// Check all variants of RoundToNSignificantDigits for v and n, and also check
// the signed variant using -v and n.
void DecimalSpotCheck(int64_t v, int n, int64_t expected) {
  EXPECT_EQ(expected, RoundToNSignificantDigitsUint64(v, n));
  EXPECT_EQ(expected, RoundToNSignificantDigitsInt64(v, n));
  EXPECT_EQ(-expected, RoundToNSignificantDigitsInt64(-v, n));
}

// Hand-check a few simple cases
TEST(DecimalRounding, SpotCheck) {
  DecimalSpotCheck(0, 7, 0);
  DecimalSpotCheck(4, 3, 4);
  DecimalSpotCheck(4, 1, 4);
  DecimalSpotCheck(4, 0, 0);
  DecimalSpotCheck(17, 2, 17);
  DecimalSpotCheck(17, 1, 20);
  DecimalSpotCheck(123456, 1, 100000);
  DecimalSpotCheck(123456, 2, 120000);
  DecimalSpotCheck(123456, 3, 123000);
  DecimalSpotCheck(123456, 4, 123500);
  DecimalSpotCheck(123456, 5, 123460);
  DecimalSpotCheck(123456, 6, 123456);
  DecimalSpotCheck(123456, 7, 123456);
  DecimalSpotCheck(13579, 0, 0);
  DecimalSpotCheck(13579, 1, 10000);
  DecimalSpotCheck(13579, 2, 14000);
  DecimalSpotCheck(13579, 3, 13600);
  DecimalSpotCheck(13579, 4, 13580);
  DecimalSpotCheck(13579, 5, 13579);
  DecimalSpotCheck(13579, 6, 13579);
  DecimalSpotCheck(13579, 10, 13579);
  DecimalSpotCheck(13579, 100, 13579);
  DecimalSpotCheck(97531, 1, 100000);
  DecimalSpotCheck(97531, 2, 98000);
  DecimalSpotCheck(97531, 3, 97500);
  DecimalSpotCheck(97531, 4, 97530);
  DecimalSpotCheck(97531, 5, 97531);
  DecimalSpotCheck(97531, 6, 97531);
  DecimalSpotCheck(97531, 10, 97531);
  DecimalSpotCheck(97531, 100, 97531);
}

// Rounding to zero significant digits always returns 0.
TEST(DecimalRounding, ZeroN) {
  EXPECT_EQ(0, RoundToNSignificantDigitsUint64(17, 0));
  EXPECT_EQ(0, RoundToNSignificantDigitsInt64(17, 0));
}

// Rounding to a negative number of significant digits always returns 0.
TEST(DecimalRounding, NegativeN) {
  EXPECT_EQ(0, RoundToNSignificantDigitsUint64(17, -3));
  EXPECT_EQ(0, RoundToNSignificantDigitsInt64(17, -3));
}

// Rounding to a large number of significant digits always returns the exact
// argument (with no rounding).
TEST(DecimalRounding, HugeN) {
  EXPECT_EQ(17, RoundToNSignificantDigitsUint64(
                    17, std::numeric_limits<int32_t>::max()));
  EXPECT_EQ(17, RoundToNSignificantDigitsInt64(
                    17, std::numeric_limits<int32_t>::max()));
}

// Rounding to a negative number of significant digits always returns 0.
TEST(DecimalRounding, HugeNegativeN) {
  EXPECT_EQ(0, RoundToNSignificantDigitsUint64(
                   17, std::numeric_limits<int32_t>::min()));
  EXPECT_EQ(0, RoundToNSignificantDigitsInt64(
                   17, std::numeric_limits<int32_t>::min()));
}

// Rounding 0 to any number of digits always returns 0.
TEST(DecimalRounding, Zero) {
  for (int i = -3; i <= 22; i++) {
    EXPECT_EQ(0, RoundToNSignificantDigitsUint64(0, i)) << i;
    EXPECT_EQ(0, RoundToNSignificantDigitsInt64(0, i)) << i;
  }
}

// Rounding 1 to any number of digits >= 1 returns 1.
TEST(DecimalRounding, One) {
  for (int i = 1; i <= 22; i++) {
    EXPECT_EQ(1, RoundToNSignificantDigitsUint64(1, i)) << i;
    EXPECT_EQ(1, RoundToNSignificantDigitsInt64(1, i)) << i;
  }
  EXPECT_EQ(0, RoundToNSignificantDigitsUint64(1, 0));
  EXPECT_EQ(0, RoundToNSignificantDigitsInt64(1, 0));
}

// Rounding -1 to any number of digits >= -1 returns 1.
TEST(DecimalRounding, MinusOne) {
  for (int i = 1; i <= 22; i++) {
    EXPECT_EQ(-1, RoundToNSignificantDigitsInt64(-1, i)) << i;
  }
  EXPECT_EQ(0, RoundToNSignificantDigitsInt64(-1, 0));
}

// Rounding uint64max should not overflow.
TEST(DecimalRounding, Uint64Max) {
  EXPECT_EQ(uint64_t{18446744073709551615u},
            RoundToNSignificantDigitsUint64(
                std::numeric_limits<uint64_t>::max(), 22));
  EXPECT_EQ(uint64_t{18446744073709551615u},
            RoundToNSignificantDigitsUint64(
                std::numeric_limits<uint64_t>::max(), 21));
  EXPECT_EQ(uint64_t{18446744073709551615u},
            RoundToNSignificantDigitsUint64(
                std::numeric_limits<uint64_t>::max(), 20));
  EXPECT_EQ(uint64_t{18446744073709551610u},
            RoundToNSignificantDigitsUint64(
                std::numeric_limits<uint64_t>::max(), 19));
  EXPECT_EQ(uint64_t{18446744073709551600u},
            RoundToNSignificantDigitsUint64(
                std::numeric_limits<uint64_t>::max(), 18));
  EXPECT_EQ(uint64_t{18446744073709551000u},
            RoundToNSignificantDigitsUint64(
                std::numeric_limits<uint64_t>::max(), 17));
  EXPECT_EQ(uint64_t{18446744073709550000u},
            RoundToNSignificantDigitsUint64(
                std::numeric_limits<uint64_t>::max(), 16));
  EXPECT_EQ(uint64_t{18446744073709500000u},
            RoundToNSignificantDigitsUint64(
                std::numeric_limits<uint64_t>::max(), 15));
  EXPECT_EQ(uint64_t{18446744073709000000u},
            RoundToNSignificantDigitsUint64(
                std::numeric_limits<uint64_t>::max(), 14));
  EXPECT_EQ(uint64_t{18446744073700000000u},
            RoundToNSignificantDigitsUint64(
                std::numeric_limits<uint64_t>::max(), 13));
  EXPECT_EQ(uint64_t{18446744073700000000u},
            RoundToNSignificantDigitsUint64(
                std::numeric_limits<uint64_t>::max(), 12));
  EXPECT_EQ(uint64_t{18446744073000000000u},
            RoundToNSignificantDigitsUint64(
                std::numeric_limits<uint64_t>::max(), 11));
  EXPECT_EQ(uint64_t{18446744070000000000u},
            RoundToNSignificantDigitsUint64(
                std::numeric_limits<uint64_t>::max(), 10));
  EXPECT_EQ(
      uint64_t{18446744000000000000u},
      RoundToNSignificantDigitsUint64(std::numeric_limits<uint64_t>::max(), 9));
  EXPECT_EQ(
      uint64_t{18446744000000000000u},
      RoundToNSignificantDigitsUint64(std::numeric_limits<uint64_t>::max(), 8));
  EXPECT_EQ(
      uint64_t{18446740000000000000u},
      RoundToNSignificantDigitsUint64(std::numeric_limits<uint64_t>::max(), 7));
  EXPECT_EQ(
      uint64_t{18446700000000000000u},
      RoundToNSignificantDigitsUint64(std::numeric_limits<uint64_t>::max(), 6));
  EXPECT_EQ(
      uint64_t{18446000000000000000u},
      RoundToNSignificantDigitsUint64(std::numeric_limits<uint64_t>::max(), 5));
  EXPECT_EQ(
      uint64_t{18440000000000000000u},
      RoundToNSignificantDigitsUint64(std::numeric_limits<uint64_t>::max(), 4));
  EXPECT_EQ(
      uint64_t{18400000000000000000u},
      RoundToNSignificantDigitsUint64(std::numeric_limits<uint64_t>::max(), 3));
  EXPECT_EQ(
      uint64_t{18000000000000000000u},
      RoundToNSignificantDigitsUint64(std::numeric_limits<uint64_t>::max(), 2));
  EXPECT_EQ(
      uint64_t{10000000000000000000u},
      RoundToNSignificantDigitsUint64(std::numeric_limits<uint64_t>::max(), 1));
  EXPECT_EQ(0, RoundToNSignificantDigitsUint64(
                   std::numeric_limits<uint64_t>::max(), 0));
  EXPECT_EQ(0, RoundToNSignificantDigitsUint64(
                   std::numeric_limits<uint64_t>::max(), -1));
}

// Rounding int64max should not overflow.
TEST(DecimalRounding, Int64Max) {
  EXPECT_EQ(
      int64_t{9223372036854775807},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::max(), 21));
  EXPECT_EQ(
      int64_t{9223372036854775807},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::max(), 20));
  EXPECT_EQ(
      int64_t{9223372036854775807},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::max(), 19));
  EXPECT_EQ(
      int64_t{9223372036854775800},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::max(), 18));
  EXPECT_EQ(
      int64_t{9223372036854775800},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::max(), 17));
  EXPECT_EQ(
      int64_t{9223372036854775000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::max(), 16));
  EXPECT_EQ(
      int64_t{9223372036854770000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::max(), 15));
  EXPECT_EQ(
      int64_t{9223372036854700000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::max(), 14));
  EXPECT_EQ(
      int64_t{9223372036854000000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::max(), 13));
  EXPECT_EQ(
      int64_t{9223372036850000000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::max(), 12));
  EXPECT_EQ(
      int64_t{9223372036800000000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::max(), 11));
  EXPECT_EQ(
      int64_t{9223372036000000000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::max(), 10));
  EXPECT_EQ(
      int64_t{9223372030000000000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::max(), 9));
  EXPECT_EQ(
      int64_t{9223372000000000000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::max(), 8));
  EXPECT_EQ(
      int64_t{9223372000000000000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::max(), 7));
  EXPECT_EQ(
      int64_t{9223370000000000000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::max(), 6));
  EXPECT_EQ(
      int64_t{9223300000000000000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::max(), 5));
  EXPECT_EQ(
      int64_t{9223000000000000000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::max(), 4));
  EXPECT_EQ(
      int64_t{9220000000000000000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::max(), 3));
  EXPECT_EQ(
      int64_t{9200000000000000000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::max(), 2));
  EXPECT_EQ(
      int64_t{9000000000000000000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::max(), 1));
  EXPECT_EQ(0, RoundToNSignificantDigitsInt64(
                   std::numeric_limits<int64_t>::max(), 0));
  EXPECT_EQ(0, RoundToNSignificantDigitsInt64(
                   std::numeric_limits<int64_t>::max(), -1));
}

// Rounding int64min should not overflow.
TEST(DecimalRounding, Int64Min) {
  EXPECT_EQ(
      std::numeric_limits<int64_t>::min(),
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::min(), 21));
  EXPECT_EQ(
      std::numeric_limits<int64_t>::min(),
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::min(), 20));
  EXPECT_EQ(
      std::numeric_limits<int64_t>::min(),
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::min(), 19));
  EXPECT_EQ(
      int64_t{-9223372036854775800},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::min(), 18));
  EXPECT_EQ(
      int64_t{-9223372036854775800},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::min(), 17));
  EXPECT_EQ(
      int64_t{-9223372036854775000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::min(), 16));
  EXPECT_EQ(
      int64_t{-9223372036854770000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::min(), 15));
  EXPECT_EQ(
      int64_t{-9223372036854700000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::min(), 14));
  EXPECT_EQ(
      int64_t{-9223372036854000000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::min(), 13));
  EXPECT_EQ(
      int64_t{-9223372036850000000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::min(), 12));
  EXPECT_EQ(
      int64_t{-9223372036800000000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::min(), 11));
  EXPECT_EQ(
      int64_t{-9223372036000000000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::min(), 10));
  EXPECT_EQ(
      int64_t{-9223372030000000000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::min(), 9));
  EXPECT_EQ(
      int64_t{-9223372000000000000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::min(), 8));
  EXPECT_EQ(
      int64_t{-9223372000000000000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::min(), 7));
  EXPECT_EQ(
      int64_t{-9223370000000000000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::min(), 6));
  EXPECT_EQ(
      int64_t{-9223300000000000000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::min(), 5));
  EXPECT_EQ(
      int64_t{-9223000000000000000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::min(), 4));
  EXPECT_EQ(
      int64_t{-9220000000000000000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::min(), 3));
  EXPECT_EQ(
      int64_t{-9200000000000000000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::min(), 2));
  EXPECT_EQ(
      int64_t{-9000000000000000000},
      RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::min(), 1));
  EXPECT_EQ(0, RoundToNSignificantDigitsInt64(
                   std::numeric_limits<int64_t>::min(), 0));
  EXPECT_EQ(0, RoundToNSignificantDigitsInt64(
                   std::numeric_limits<int64_t>::min(), -1));
}

// return 10 ** n
uint64_t pow10(int n) {
  uint64_t v = 1;
  for (int i = 0; i < n; i++) {
    v *= 10ULL;
  }
  return v;
}

// Rounding values close to uint64max should not overflow.
TEST(DecimalRounding, AlmostUint64Max) {
  EXPECT_EQ(std::numeric_limits<uint64_t>::max() - 1,
            RoundToNSignificantDigitsUint64(
                std::numeric_limits<uint64_t>::max() - 1, 22));
  EXPECT_EQ(std::numeric_limits<uint64_t>::max() - 1,
            RoundToNSignificantDigitsUint64(
                std::numeric_limits<uint64_t>::max() - 1, 21));
  EXPECT_EQ(std::numeric_limits<uint64_t>::max() - 1,
            RoundToNSignificantDigitsUint64(
                std::numeric_limits<uint64_t>::max() - 1, 20));
  EXPECT_EQ(0, RoundToNSignificantDigitsUint64(
                   std::numeric_limits<uint64_t>::max() - 1, 0));
  EXPECT_EQ(0, RoundToNSignificantDigitsUint64(
                   std::numeric_limits<uint64_t>::max() - 1, -1));
  for (int n = 1; n < 20; n++) {
    // we know r is good based on an earlier test
    const uint64_t r = RoundToNSignificantDigitsUint64(
        std::numeric_limits<uint64_t>::max(), n);
    const uint64_t mag = pow10(20 - ((n == 1) ? 2 : n));
    // 'min' is the smallest value that should round up to r
    const uint64_t min = r - (mag >> 1);
    // everything between kuint64max and 'min' should round to r
    EXPECT_EQ(r, RoundToNSignificantDigitsUint64(
                     std::numeric_limits<uint64_t>::max() - 1, n));
    EXPECT_EQ(r, RoundToNSignificantDigitsUint64(
                     std::numeric_limits<uint64_t>::max() - 2, n));
    EXPECT_EQ(r, RoundToNSignificantDigitsUint64(
                     std::numeric_limits<uint64_t>::max() - 3, n));
    EXPECT_EQ(r, RoundToNSignificantDigitsUint64(r + 1, n));
    EXPECT_EQ(r, RoundToNSignificantDigitsUint64(r, n));
    EXPECT_EQ(r, RoundToNSignificantDigitsUint64(r - 1, n));
    EXPECT_EQ(r, RoundToNSignificantDigitsUint64(min + 2, n));
    EXPECT_EQ(r, RoundToNSignificantDigitsUint64(min + 1, n));
    EXPECT_EQ(r, RoundToNSignificantDigitsUint64(min, n));
    //    LOG(INFO) << min << " -> " << r;
    // one less than 'min' should round down
    EXPECT_EQ(r - mag, RoundToNSignificantDigitsUint64(min - 1, n));
    //    LOG(INFO) << (min - 1) << " -> " << (r - mag);
  }
}

// Rounding values close to int64max should not overflow.
TEST(DecimalRounding, AlmostInt64Max) {
  EXPECT_EQ(std::numeric_limits<int64_t>::max() - 1,
            RoundToNSignificantDigitsInt64(
                std::numeric_limits<int64_t>::max() - 1, 21));
  EXPECT_EQ(std::numeric_limits<int64_t>::max() - 1,
            RoundToNSignificantDigitsInt64(
                std::numeric_limits<int64_t>::max() - 1, 20));
  EXPECT_EQ(std::numeric_limits<int64_t>::max() - 1,
            RoundToNSignificantDigitsInt64(
                std::numeric_limits<int64_t>::max() - 1, 19));
  for (int n = 1; n < 19; n++) {
    // we know r is good based on an earlier test
    const int64_t r =
        RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::max(), n);
    const int64_t mag = pow10(19 - n);
    // 'min' is the smallest value that should round up to r
    const int64_t min = r - (mag >> 1);
    // everything between kint64max and 'min' should round to r
    EXPECT_EQ(r, RoundToNSignificantDigitsInt64(
                     std::numeric_limits<int64_t>::max() - 1, n));
    EXPECT_EQ(r, RoundToNSignificantDigitsInt64(
                     std::numeric_limits<int64_t>::max() - 2, n));
    EXPECT_EQ(r, RoundToNSignificantDigitsInt64(
                     std::numeric_limits<int64_t>::max() - 3, n));
    EXPECT_EQ(r, RoundToNSignificantDigitsInt64(r + 1, n));
    EXPECT_EQ(r, RoundToNSignificantDigitsInt64(r, n));
    EXPECT_EQ(r, RoundToNSignificantDigitsInt64(r - 1, n));
    EXPECT_EQ(r, RoundToNSignificantDigitsInt64(min + 2, n));
    EXPECT_EQ(r, RoundToNSignificantDigitsInt64(min + 1, n));
    EXPECT_EQ(r, RoundToNSignificantDigitsInt64(min, n));
    //    LOG(INFO) << min << " -> " << r;
    // one less than 'min' should round down
    EXPECT_EQ(r - mag, RoundToNSignificantDigitsInt64(min - 1, n));
    //    LOG(INFO) << (min - 1) << " -> " << (r - mag);
  }
  EXPECT_EQ(0, RoundToNSignificantDigitsInt64(
                   std::numeric_limits<int64_t>::max() - 1, 0));
  EXPECT_EQ(0, RoundToNSignificantDigitsInt64(
                   std::numeric_limits<int64_t>::max() - 1, -1));
}

// Rounding values close to int64min should not overflow.
TEST(DecimalRounding, AlmostInt64Min) {
  EXPECT_EQ(std::numeric_limits<int64_t>::min() + 1,
            RoundToNSignificantDigitsInt64(
                std::numeric_limits<int64_t>::min() + 1, 21));
  EXPECT_EQ(std::numeric_limits<int64_t>::min() + 1,
            RoundToNSignificantDigitsInt64(
                std::numeric_limits<int64_t>::min() + 1, 20));
  EXPECT_EQ(std::numeric_limits<int64_t>::min() + 1,
            RoundToNSignificantDigitsInt64(
                std::numeric_limits<int64_t>::min() + 1, 19));
  for (int n = 1; n < 19; n++) {
    // we know r is good based on an earlier test
    const int64_t r =
        RoundToNSignificantDigitsInt64(std::numeric_limits<int64_t>::min(), n);
    const int64_t mag = pow10(19 - n);
    // 'min' is the smallest (magnitude) value that should round up to r
    const int64_t min = r + (mag >> 1);
    // everything between kint64min and 'min' should round to r
    EXPECT_EQ(r, RoundToNSignificantDigitsInt64(
                     std::numeric_limits<int64_t>::min() + 1, n));
    EXPECT_EQ(r, RoundToNSignificantDigitsInt64(
                     std::numeric_limits<int64_t>::min() + 2, n));
    EXPECT_EQ(r, RoundToNSignificantDigitsInt64(
                     std::numeric_limits<int64_t>::min() + 3, n));
    EXPECT_EQ(r, RoundToNSignificantDigitsInt64(r - 1, n));
    EXPECT_EQ(r, RoundToNSignificantDigitsInt64(r, n));
    EXPECT_EQ(r, RoundToNSignificantDigitsInt64(r + 1, n));
    EXPECT_EQ(r, RoundToNSignificantDigitsInt64(min - 2, n));
    EXPECT_EQ(r, RoundToNSignificantDigitsInt64(min - 1, n));
    EXPECT_EQ(r, RoundToNSignificantDigitsInt64(min, n));
    //    LOG(INFO) << min << " -> " << r;
    // one more than 'min' should round down (towards zero)
    EXPECT_EQ(r + mag, RoundToNSignificantDigitsInt64(min + 1, n));
    //    LOG(INFO) << (min + 1) << " -> " << (r + mag);
  }
  EXPECT_EQ(0, RoundToNSignificantDigitsInt64(
                   std::numeric_limits<int64_t>::min() + 1, 0));
  EXPECT_EQ(0, RoundToNSignificantDigitsInt64(
                   std::numeric_limits<int64_t>::min() + 1, -1));
}

// An alternative implementation of RoundToNSignificantDigitsUint64 which is
// slower, but is useful to cross-check the fast version.
uint64_t SlowRoundToNSignificantDigits(uint64_t v, int n) {
  if (n <= 0) {
    return 0;
  }
  // print v to a string
  std::string vs;
  absl::StrAppendFormat(&vs, "%u", v);
  if (vs.length() <= n) {
    return v;
  }
  // erase the unwanted low-order digits from the string, parse it back into
  // an integer, then multiply by 10 once for each deleted digit in order to
  // restore the magnitude.
  const int zeroes = vs.length() - n;
  bool up = (vs[n] >= '5');  // round half up if the top discarded digit is >= 5
  vs.erase(n);
  const uint64_t r = strings::ParseLeadingUDec64Value(vs, 0);
  CHECK_NE(0, r) << v << " " << n;
  uint64_t r2 = r;
  if (up) {
    r2++;
  }
  int z2 = zeroes;
  while (z2 > 0) {
    if (r2 > (std::numeric_limits<uint64_t>::max() / 10)) {
      // Oops. overflow. start over, but this time, don't round up.
      CHECK(up) << v << " " << n;
      r2 = r;
      z2 = zeroes;
    } else {
      r2 *= 10;
      z2--;
    }
  }
  return r2;
}

// Cross-check the results of both versions of RoundToNSignificantDigits using
// SlowRoundToNSignificantDigits.
void CheckDecimalRounding(uint64_t v, int n) {
  EXPECT_EQ(SlowRoundToNSignificantDigits(v, n),
            RoundToNSignificantDigitsUint64(v, n))
      << v << " " << n;
  int64_t v2 = static_cast<int64_t>(v) & std::numeric_limits<int64_t>::max();
  CHECK_GE(v2, 0);
  int64_t r2 = SlowRoundToNSignificantDigits(v2, n);
  EXPECT_EQ(r2, RoundToNSignificantDigitsInt64(v2, n)) << v2 << " " << n;
  EXPECT_EQ(-r2, RoundToNSignificantDigitsInt64(-v2, n)) << -v2 << " " << n;
}

// Use a reproducible pseudo-random number generator to check a large number
// of calls to all variants of RoundToNSignificantDigits.
TEST(DecimalRounding, Random) {
  std::mt19937 r(12345678);
  for (int i = 0; i < 100000; i++) {
    int64_t v = absl::Uniform<uint64_t>(r);
    int n = absl::Uniform<int>(absl::IntervalClosed, r, 1, 20);
    CheckDecimalRounding(v, n);
  }
}

void BM_round_to_3_sig_figs(benchmark::State& state) {
  std::mt19937 r(12345678);
  for (auto _ : state) {
    int64_t v =
        absl::Uniform<uint64_t>(r) & std::numeric_limits<int64_t>::max();
    util::math::round_to_3_sig_figs(&v);
    CHECK_GE(v, 0);
  }
}

BENCHMARK(BM_round_to_3_sig_figs);

void BM_RoundToNSignificantDigitsUint64(benchmark::State& state) {
  std::mt19937 r(12345678);
  for (auto _ : state) {
    int64_t v =
        absl::Uniform<uint64_t>(r) & std::numeric_limits<int64_t>::max();
    v = RoundToNSignificantDigitsUint64(v, 3);
    CHECK_GE(v, 0);
  }
}

BENCHMARK(BM_RoundToNSignificantDigitsUint64);

void BM_Log10Uint64Naive(benchmark::State& state) {
  std::mt19937 r(12345678);
  for (auto _ : state) {
    // Overhead of Rand64 is high, so for each random value, use several
    // trivial variations that are known to be non-zero.
    int64_t v =
        absl::Uniform<uint64_t>(r) & std::numeric_limits<int64_t>::max();
    int64_t sum = std::log10(v | 1);
    sum += std::log10(v | 4);
    sum += std::log10(v | 16);
    sum += std::log10((v << 2) | 1);
    sum += std::log10((v << 4) | 1);
    sum += std::log10((v << 8) | 1);
    sum += std::log10((v >> 16) | 1);
    sum += std::log10((v >> 8) | 1);
    sum += std::log10((v >> 4) | 1);
    sum += std::log10((v >> 2) | 1);
    CHECK_GE(v, 9);
    benchmark::DoNotOptimize(sum);
  }
}
BENCHMARK(BM_Log10Uint64Naive);

void BM_Log10Uint64(benchmark::State& state) {
  std::mt19937 r(12345678);
  for (auto _ : state) {
    // Overhead of Rand64 is high, so for each random value, use several
    // trivial variations that are known to be non-zero.
    int64_t v =
        absl::Uniform<uint64_t>(r) & std::numeric_limits<int64_t>::max();
    int64_t sum = Log10FloorNonZero(v | 1);
    sum += Log10FloorNonZero(v | 4);
    sum += Log10FloorNonZero(v | 16);
    sum += Log10FloorNonZero((v << 2) | 1);
    sum += Log10FloorNonZero((v << 4) | 1);
    sum += Log10FloorNonZero((v << 8) | 1);
    sum += Log10FloorNonZero((v >> 16) | 1);
    sum += Log10FloorNonZero((v >> 8) | 1);
    sum += Log10FloorNonZero((v >> 4) | 1);
    sum += Log10FloorNonZero((v >> 2) | 1);
    CHECK_GE(v, 9);
    benchmark::DoNotOptimize(sum);
  }
}
BENCHMARK(BM_Log10Uint64);

// ------------------------ Quantization ------------------------

TEST(QuantizeTest, QuantizeFloat_0_0) {
  const float v = 0.f;
  EXPECT_EQ(0, Quantize(v, 0));
  EXPECT_EQ(0, Quantize(v, 1));
  EXPECT_EQ(0, Quantize(v, 3));
  EXPECT_EQ(0, Quantize(v, 10));
  EXPECT_EQ(0, Quantize(v, 255));
  EXPECT_EQ(0, Quantize(v, std::numeric_limits<uint64_t>::max()));
}

TEST(QuantizeTest, QuantizeFloat_0_1) {
  const float v = 0.1f;
  EXPECT_EQ(0, Quantize(v, 0));
  EXPECT_EQ(0, Quantize(v, 1));
  EXPECT_EQ(0, Quantize(v, 3));
  EXPECT_EQ(1, Quantize(v, 10));
  EXPECT_EQ(25, Quantize(v, 255));
  // Quantizing to kuint64max levels uses a tolerance test against against
  // an exact target computed externally with:
  // python -c 'print "%#x" % ((2 ** 64 - 1) * 10 / 100)'
  const uint64_t kTarget = 0x1999999999999999ULL;
  const uint64_t kTolerance = std::numeric_limits<float>::epsilon() *
                              std::numeric_limits<uint64_t>::max();
  uint64_t quantized = Quantize(v, std::numeric_limits<uint64_t>::max());
  EXPECT_LT(kTarget - kTolerance, quantized);
  EXPECT_GT(kTarget + kTolerance, quantized);
}

TEST(QuantizeTest, QuantizeFloat_0_76) {
  const float v = 0.76f;
  EXPECT_EQ(0, Quantize(v, 0));
  EXPECT_EQ(1, Quantize(v, 1));
  EXPECT_EQ(3, Quantize(v, 3));
  EXPECT_EQ(8, Quantize(v, 10));
  EXPECT_EQ(194, Quantize(v, 255));
  // Quantizing to kuint64max levels uses a tolerance test against against
  // an exact target computed externally with:
  // python -c 'print "%#x" % ((2 ** 64 - 1) * 76 / 100)'
  const uint64_t kTarget = 0xc28f5c28f5c28f5bULL;
  const uint64_t kTolerance = std::numeric_limits<float>::epsilon() *
                              std::numeric_limits<uint64_t>::max();
  uint64_t quantized = Quantize(v, std::numeric_limits<uint64_t>::max());
  EXPECT_LT(kTarget - kTolerance, quantized);
  EXPECT_GT(kTarget + kTolerance, quantized);
}

TEST(QuantizeTest, QuantizeFloat_0_99) {
  const float v = 0.99f;
  EXPECT_EQ(0, Quantize(v, 0));
  EXPECT_EQ(1, Quantize(v, 1));
  EXPECT_EQ(3, Quantize(v, 3));
  EXPECT_EQ(10, Quantize(v, 10));
  EXPECT_EQ(253, Quantize(v, 255));
  // Quantizing to kuint64max levels uses a tolerance test against against
  // an exact target computed externally with:
  // python -c 'print "%#x" % ((2 ** 64 - 1) * 99 / 100)'
  const uint64_t kTarget = 0xfd70a3d70a3d70a2ULL;
  const uint64_t kTolerance = std::numeric_limits<float>::epsilon() *
                              std::numeric_limits<uint64_t>::max();
  uint64_t quantized = Quantize(v, std::numeric_limits<uint64_t>::max());
  EXPECT_LT(kTarget - kTolerance, quantized);
  EXPECT_GT(kTarget + kTolerance, quantized);
}

TEST(QuantizeTest, QuantizeFloat_1_0) {
  const float v = 1.f;
  EXPECT_EQ(0, Quantize(v, 0));
  EXPECT_EQ(1, Quantize(v, 1));
  EXPECT_EQ(3, Quantize(v, 3));
  EXPECT_EQ(10, Quantize(v, 10));
  EXPECT_EQ(255, Quantize(v, 255));
  EXPECT_EQ(std::numeric_limits<uint64_t>::max(),
            Quantize(v, std::numeric_limits<uint64_t>::max()));
}

TEST(QuantizeTest, QuantizeDouble_0_0) {
  const double v = 0.;
  EXPECT_EQ(0, Quantize(v, 0));
  EXPECT_EQ(0, Quantize(v, 1));
  EXPECT_EQ(0, Quantize(v, 3));
  EXPECT_EQ(0, Quantize(v, 10));
  EXPECT_EQ(0, Quantize(v, 255));
  EXPECT_EQ(0, Quantize(v, std::numeric_limits<uint64_t>::max()));
}

TEST(QuantizeTest, QuantizeDouble_0_1) {
  const double v = 0.1;
  EXPECT_EQ(0, Quantize(v, 0));
  EXPECT_EQ(0, Quantize(v, 1));
  EXPECT_EQ(0, Quantize(v, 3));
  EXPECT_EQ(1, Quantize(v, 10));
  EXPECT_EQ(25, Quantize(v, 255));
  // Quantizing to kuint64max levels uses a tolerance test against against
  // an exact target computed externally with:
  // python -c 'print "%#x" % ((2 ** 64 - 1) * 10 / 100)'
  const uint64_t kTarget = 0x1999999999999999ULL;
  const uint64_t kTolerance = std::numeric_limits<double>::epsilon() *
                              std::numeric_limits<uint64_t>::max();
  uint64_t quantized = Quantize(v, std::numeric_limits<uint64_t>::max());
  EXPECT_LT(kTarget - kTolerance, quantized);
  EXPECT_GT(kTarget + kTolerance, quantized);
}

TEST(QuantizeTest, QuantizeDouble_0_76) {
  const double v = 0.76;
  EXPECT_EQ(0, Quantize(v, 0));
  EXPECT_EQ(1, Quantize(v, 1));
  EXPECT_EQ(3, Quantize(v, 3));
  EXPECT_EQ(8, Quantize(v, 10));
  EXPECT_EQ(194, Quantize(v, 255));
  // Quantizing to kuint64max levels uses a tolerance test against against
  // an exact target computed externally with:
  // python -c 'print "%#x" % ((2 ** 64 - 1) * 76 / 100)'
  const uint64_t kTarget = 0xc28f5c28f5c28f5bULL;
  const uint64_t kTolerance = std::numeric_limits<double>::epsilon() *
                              std::numeric_limits<uint64_t>::max();
  uint64_t quantized = Quantize(v, std::numeric_limits<uint64_t>::max());
  EXPECT_LT(kTarget - kTolerance, quantized);
  EXPECT_GT(kTarget + kTolerance, quantized);
}

TEST(QuantizeTest, QuantizeDouble_0_99) {
  const double v = 0.99;
  EXPECT_EQ(0, Quantize(v, 0));
  EXPECT_EQ(1, Quantize(v, 1));
  EXPECT_EQ(3, Quantize(v, 3));
  EXPECT_EQ(10, Quantize(v, 10));
  EXPECT_EQ(253, Quantize(v, 255));
  // Quantizing to kuint64max levels uses a tolerance test against against
  // an exact target computed externally with:
  // python -c 'print "%#x" % ((2 ** 64 - 1) * 99 / 100)'
  const uint64_t kTarget = 0xfd70a3d70a3d70a2ULL;
  const uint64_t kTolerance = std::numeric_limits<double>::epsilon() *
                              std::numeric_limits<uint64_t>::max();
  uint64_t quantized = Quantize(v, std::numeric_limits<uint64_t>::max());
  EXPECT_LT(kTarget - kTolerance, quantized);
  EXPECT_GT(kTarget + kTolerance, quantized);
}

TEST(QuantizeTest, QuantizeDouble_1_0) {
  const double v = 1.;
  EXPECT_EQ(0, Quantize(v, 0));
  EXPECT_EQ(1, Quantize(v, 1));
  EXPECT_EQ(3, Quantize(v, 3));
  EXPECT_EQ(10, Quantize(v, 10));
  EXPECT_EQ(255, Quantize(v, 255));
  EXPECT_EQ(std::numeric_limits<uint64_t>::max(),
            Quantize(v, std::numeric_limits<uint64_t>::max()));
}

// Generates num_tests random pairs of FloatType in the range [0, 1]
// and uint64 in the range [0, uint64(-1)].  For each pair, the former
// is quantized to the latter maximum level.  The result is validated
// by comparing the pre-quantized floating-point value against the
// computed percentile of the quantized value. The epsilon_scale
// parameter allows variable precision between float and double types,
// for reasons described below.
template <typename FloatType>
void QuantizeRandomSamples(FloatType epsilon_scale, size_t num_tests) {
  FloatType normalize_64 = FloatType(1) / std::numeric_limits<uint64_t>::max();
  std::mt19937 random(GTEST_FLAG_GET(random_seed));
  for (size_t i = 0; i < num_tests; ++i) {
    uint64_t max_level = absl::Uniform<uint64_t>(random);
    FloatType val = absl::Uniform<uint64_t>(random) * normalize_64;

    uint64_t got_result = Quantize(val, max_level);
    EXPECT_LE(got_result, max_level);

    uint64_t num_levels = max_level < std::numeric_limits<uint64_t>::max()
                              ? max_level + 1
                              : max_level;
    // When using FloatType double, the following intermediate
    // arithmetic, itself being in double precision, can overestimate
    // the error relative to double's epsilon. This is why doubles,
    // unlike floats, require epsilon_scale > 1.
    double expected_val = (got_result + 0.5) / num_levels;
    double abs_diff = fabs(expected_val - val);
    double allowed_diff = std::max<double>(
        1.0 / (max_level + 1),
        epsilon_scale * std::numeric_limits<FloatType>::epsilon());
    EXPECT_LE(abs_diff, allowed_diff)
        << "num_levels = " << num_levels << "; val = " << val
        << "; got_result = " << got_result;
  }
}

TEST(QuantizeTest, RandomFloatSamples) { QuantizeRandomSamples(1.0f, 1000000); }

TEST(QuantizeTest, RandomDoubleSamples) { QuantizeRandomSamples(2.0, 1000000); }

}  // namespace
}  // namespace math
}  // namespace util
