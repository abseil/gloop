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
// This tests common/bits.{cc,h}

#include "gloop/util/bits/bits.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log_streamer.h"
#include "absl/numeric/bits.h"
#include "absl/numeric/int128.h"
#include "absl/random/random.h"
#include "absl/strings/numbers.h"
#include "absl/types/span.h"
#include "benchmark/benchmark.h"
#include "gtest/gtest.h"

ABSL_FLAG(int32_t, num_iterations, 10000, "Number of test iterations to run.");
ABSL_FLAG(int32_t, max_bytes, 100, "Maximum number of bytes to use in tests.");

static const int kMaxBytes = 128;
static const int kNumReverseBitsRandomTests = 10;

namespace {

absl::BitGen MakeTestBitGen() { return absl::BitGen(); }

// Generate a random number of type T with the same range as that of T.
template <typename T>
T RandomBits(absl::BitGen* random) {
  return absl::Uniform<typename std::make_unsigned<T>::type>(*random);
}

template <>
absl::uint128 RandomBits<absl::uint128>(absl::BitGen* random) {
  return absl::MakeUint128(RandomBits<uint64_t>(random),
                           RandomBits<uint64_t>(random));
}

}  // namespace

class BitsTest : public testing::Test {
 public:
  static void SetUpTestCase() {
    ASSERT_LE(absl::GetFlag(FLAGS_max_bytes), kMaxBytes);
  }

  BitsTest() {}

 protected:
  template <typename T>
  static void CheckUnsignedType() {
    typedef typename Bits::UnsignedType<T>::Type UnsignedT;
    EXPECT_EQ(sizeof(T), sizeof(UnsignedT));
    EXPECT_FALSE(std::numeric_limits<UnsignedT>::is_signed);
  }

  // Wrapper for Bits::SetBits with a slightly different interface for
  // testing.  Instead of modifying a scalar, it returns a new value
  // with some bits replaced.
  template <typename T>
  static T SetBits(T dest, const typename Bits::UnsignedType<T>::Type src,
                   const int offset, const int nbits) {
    Bits::SetBits(src, offset, nbits, &dest);
    return dest;
  }

  // Specialize RandomCopyBitsTestTwoTypes to work with uint128. In particular,
  // since function template partial specialization is not allowed, make a
  // special struct that does the same thing. Only run the test if it is
  // enabled, since certain types cannot be converted to uint128 -- but these
  // can be caught at compile-time.
  template <typename DestType, typename SrcType, bool enable>
  struct RandomCopyBitsTester {
    static void Impl(absl::BitGen* random);
  };

  template <typename DestType, typename SrcType>
  struct RandomCopyBitsTester<DestType, SrcType, true> {
    static void Impl(absl::BitGen* random);
  };

  template <typename DestType, typename SrcType, bool enable>
  void RandomCopyBitsTestTwoTypes();

  template <typename DestType>
  void RandomCopyBitsTestDestType();

  template <typename T>
  void TestGetLowBitsType();

  absl::BitGen random_;
};

// Randomly test Bits::CopyBits of two scalar types.
template <typename DestType, typename SrcType, bool enable>
void BitsTest::RandomCopyBitsTestTwoTypes() {
  RandomCopyBitsTester<DestType, SrcType, enable>::Impl(&random_);
}

template <typename DestType, typename SrcType, bool enable>
void BitsTest::RandomCopyBitsTester<DestType, SrcType, enable>::Impl(
    absl::BitGen* random) {
  // Do nothing -- not enabled.
}

template <typename DestType, typename SrcType>
void BitsTest::RandomCopyBitsTester<DestType, SrcType, true>::Impl(
    absl::BitGen* random) {
  const int kNumIterations = 2000;
  const int dest_bits = sizeof(DestType) * 8;
  const int src_bits = sizeof(SrcType) * 8;

  for (int i = 0; i < kNumIterations; ++i) {
    DestType dest = RandomBits<DestType>(random);
    DestType dest2 = dest;
    const int dest_offset = absl::Uniform<uint32_t>(*random) % dest_bits;
    const SrcType src = RandomBits<SrcType>(random);
    const int src_offset = absl::Uniform<uint32_t>(*random) % src_bits;
    const int nbits_max =
        std::min(dest_bits - dest_offset, src_bits - src_offset);
    const int nbits = absl::Uniform<uint32_t>(*random) % (nbits_max + 1);

    Bits::CopyBits(&dest, dest_offset, src, src_offset, nbits);
    EXPECT_EQ(Bits::GetBits(src, src_offset, nbits),
              Bits::GetBits(dest, dest_offset, nbits));

    // Reference implementation: Copying bits one at a time.
    typedef typename Bits::UnsignedType<SrcType>::Type SrcUnsignedType;
    typedef typename Bits::UnsignedType<DestType>::Type DestUnsignedType;
    const SrcUnsignedType unsigned_src = static_cast<SrcUnsignedType>(src);
    for (int j = 0; j < nbits; ++j) {
      const SrcUnsignedType src_bit = static_cast<SrcUnsignedType>(1)
                                      << (src_offset + j);
      const DestUnsignedType dest_bit = static_cast<DestUnsignedType>(1)
                                        << (dest_offset + j);
      if ((unsigned_src & src_bit) != 0) {
        dest2 |= dest_bit;
      } else {
        dest2 &= ~dest_bit;
      }
    }

    EXPECT_EQ(dest, dest2);
  }
}

// Helper template to test all source types. Don't test [u]int(8|16) against
// uint128, as these constructors are disabled in the uint128 class.
template <typename DestType>
void BitsTest::RandomCopyBitsTestDestType() {
  RandomCopyBitsTestTwoTypes<DestType, int8_t,
                             !std::is_same<DestType, absl::uint128>::value>();
  RandomCopyBitsTestTwoTypes<DestType, uint8_t,
                             !std::is_same<DestType, absl::uint128>::value>();
  RandomCopyBitsTestTwoTypes<DestType, int16_t,
                             !std::is_same<DestType, absl::uint128>::value>();
  RandomCopyBitsTestTwoTypes<DestType, uint16_t,
                             !std::is_same<DestType, absl::uint128>::value>();
  RandomCopyBitsTestTwoTypes<DestType, int32_t, true>();
  RandomCopyBitsTestTwoTypes<DestType, uint32_t, true>();
  RandomCopyBitsTestTwoTypes<DestType, int64_t, true>();
  RandomCopyBitsTestTwoTypes<DestType, uint64_t, true>();
}

// For each type, test GetLowBits 0b1111..., for each bit position.
template <typename T>
void BitsTest::TestGetLowBitsType() {
  typedef typename Bits::UnsignedType<T>::Type UnsignedT;
  constexpr size_t bit_size = sizeof(UnsignedT) * 8;
  UnsignedT n = std::numeric_limits<UnsignedT>::max();
  UnsignedT mask = 0;
  for (int idx = 0; idx <= bit_size; ++idx) {
    EXPECT_EQ(Bits::GetLowBits(n, idx), n & mask);
    mask = (mask << 1) | 1;
  }
}

TEST_F(BitsTest, BitCountingEdgeCases) {
  std::cout << "TestBitCountingEdgeCases" << std::endl;
  EXPECT_EQ(0, absl::popcount(static_cast<uint32_t>(0)));
  EXPECT_EQ(1, absl::popcount(static_cast<uint32_t>(1)));
  EXPECT_EQ(32, absl::popcount(static_cast<uint32_t>(~0U)));
  EXPECT_EQ(1, absl::popcount(static_cast<uint32_t>(0x8000000)));

  for (int i = 0; i < 32; i++) {
    EXPECT_EQ(1, absl::popcount(1U << i));
    EXPECT_EQ(31, absl::popcount(static_cast<uint32_t>(~0U) ^ (1U << i)));
  }

  EXPECT_EQ(0, absl::popcount(static_cast<uint64_t>(int64_t{0})));
  EXPECT_EQ(1, absl::popcount(static_cast<uint64_t>(int64_t{1})));
  EXPECT_EQ(64, absl::popcount(static_cast<uint64_t>(~0ULL)));
  EXPECT_EQ(1, absl::popcount(static_cast<uint64_t>(int64_t{0x8000000})));

  for (int i = 0; i < 64; i++) {
    EXPECT_EQ(1, absl::popcount(uint64_t{1} << i));
    EXPECT_EQ(63, absl::popcount(static_cast<uint64_t>(~(uint64_t{1} << i))));
  }

  EXPECT_EQ(0, Bits::CountOnes128(absl::uint128(0)));
  EXPECT_EQ(1, Bits::CountOnes128(absl::uint128(1)));
  EXPECT_EQ(128, Bits::CountOnes128(~absl::uint128(0)));

  for (int i = 0; i < 128; i++) {
    EXPECT_EQ(1, Bits::CountOnes128(absl::uint128(1) << i));
    EXPECT_EQ(127,
              Bits::CountOnes128(~absl::uint128(0) ^ (absl::uint128(1) << i)));
  }

  EXPECT_EQ(0, Bits::Count("", 0));
  for (int i = 0; i <= std::numeric_limits<int8_t>::max(); i++) {
    uint8_t b[1];
    b[0] = i;
    EXPECT_EQ(Bits::Count(b, 1), absl::popcount(static_cast<uint32_t>(i)));
  }
}

TEST_F(BitsTest, BitCountingRandom) {
  std::cout << "TestBitCountingRandom" << std::endl;
  for (int i = 0; i < absl::GetFlag(FLAGS_num_iterations); i++) {
    float p = absl::Uniform<float>(random_, 0, 1);
    int nbits = 0;
    uint32_t n = 0;
    for (int i = 0; i < 32; i++) {
      if (absl::Uniform<float>(random_, 0, 1) < p) {
        n |= (1U << i);
        nbits++;
      }
    }
    EXPECT_EQ(nbits, absl::popcount(n));
  }
}

TEST_F(BitsTest, BitCountingRandom64) {
  std::cout << "TestBitCountingRandom64" << std::endl;
  for (int i = 0; i < absl::GetFlag(FLAGS_num_iterations); i++) {
    float p = absl::Uniform<float>(random_, 0, 1);
    int nbits = 0;
    uint64_t n = 0;
    for (int i = 0; i < 64; i++) {
      if (absl::Uniform<float>(random_, 0, 1) < p) {
        n |= (1LL << i);
        nbits++;
      }
    }
    EXPECT_EQ(nbits, absl::popcount(n));
  }
}

TEST_F(BitsTest, BitCountingRandom128) {
  std::cout << "TestBitCountingRandom128" << std::endl;
  for (int i = 0; i < absl::GetFlag(FLAGS_num_iterations); i++) {
    float p = absl::Uniform<float>(random_, 0, 1);
    int nbits = 0;
    absl::uint128 n = 0;
    for (int i = 0; i < 128; i++) {
      if (absl::Uniform<float>(random_, 0, 1) < p) {
        n |= (absl::uint128(1) << i);
        nbits++;
      }
    }
    EXPECT_EQ(nbits, Bits::CountOnes128(n));
  }
}

TEST_F(BitsTest, BitCountingRandomArray) {
  for (int i = 0; i < absl::GetFlag(FLAGS_num_iterations); i++) {
    float p = absl::Uniform<float>(random_, 0, 1);
    int num_bytes =
        absl::Uniform<int32_t>(random_, 0, absl::GetFlag(FLAGS_max_bytes));
    uint8_t b[kMaxBytes];
    memset(b, 0, kMaxBytes);
    int num_bits = num_bytes * 8;
    int nbits = 0;
    for (int j = 0; j < num_bits; j++) {
      if (absl::Uniform<float>(random_, 0, 1) < p) {
        b[j / 8] |= 1U << (j % 8);
        nbits++;
      }
    }
    EXPECT_EQ(nbits, Bits::Count(b, num_bytes));
  }
}

TEST_F(BitsTest, BitCountLeadingZeros) {
  EXPECT_EQ(128, Bits::CountLeadingZeros128(absl::uint128(0)));
  EXPECT_EQ(0, Bits::CountLeadingZeros128(~absl::uint128(0)));

  for (int i = 0; i < 128; i++) {
    EXPECT_EQ(127 - i, Bits::CountLeadingZeros128(absl::uint128(1) << i));
  }
}

TEST_F(BitsTest, BitCountLeadingZerosRandom) {
  std::cout << "TestBitCountLeadingZerosRandom" << std::endl;

  for (int i = 0; i < absl::GetFlag(FLAGS_num_iterations); i++) {
    int count = absl::Uniform<int32_t>(random_, 0, 128);
    absl::uint128 random = absl::MakeUint128(absl::Uniform<uint64_t>(random_),
                                             absl::Uniform<uint64_t>(random_));
    absl::uint128 n = (random | (absl::uint128(1) << 127)) >> count;
    EXPECT_EQ(count, Bits::CountLeadingZeros128(n));
  }
}

TEST_F(BitsTest, BitDifferenceRandom) {
  std::cout << "TestBitDifferenceRandom" << std::endl;
  for (int i = 0; i < absl::GetFlag(FLAGS_num_iterations); i++) {
    float p = absl::Uniform<float>(random_, 0, 1);
    int num_bytes =
        absl::Uniform<int32_t>(random_, 0, absl::GetFlag(FLAGS_max_bytes));
    uint8_t b1[kMaxBytes];
    uint8_t b2[kMaxBytes];
    memset(b1, 0, kMaxBytes);
    memset(b2, 0, kMaxBytes);
    for (int j = 0; j < num_bytes; j++) {
      b1[j] = absl::Uniform<int32_t>(random_, 0,
                                     std::numeric_limits<int8_t>::max());
      b2[j] = b1[j];
    }
    int num_bits = num_bytes * 8;
    int nbits = 0;
    for (int j = 0; j < num_bits; j++) {
      if (absl::Uniform<float>(random_, 0, 1) < p) {
        b2[j / 8] ^= 1U << (j % 8);
        nbits++;
      }
    }
    EXPECT_EQ(nbits, Bits::Difference(b1, b2, num_bytes));
    EXPECT_EQ(nbits, Bits::CappedDifference(b1, b2, num_bytes, nbits * 3));
    int capped = Bits::CappedDifference(b1, b2, num_bytes, nbits / 2);
    EXPECT_GE(nbits, capped);
    EXPECT_LE(nbits / 2, capped);
  }
}

TEST(IsPowerOfTwo, IsPowerOfTwo) {
  EXPECT_FALSE(Bits::IsPowerOfTwo(-1));
  EXPECT_FALSE(Bits::IsPowerOfTwo(0));
  EXPECT_TRUE(Bits::IsPowerOfTwo(1));
  EXPECT_TRUE(Bits::IsPowerOfTwo(2));
  EXPECT_FALSE(Bits::IsPowerOfTwo(3));
  EXPECT_TRUE(Bits::IsPowerOfTwo(4));

  EXPECT_FALSE(Bits::IsPowerOfTwo(std::numeric_limits<int64_t>::min()));
  EXPECT_FALSE(Bits::IsPowerOfTwo(std::numeric_limits<int64_t>::max()));
  EXPECT_TRUE(Bits::IsPowerOfTwo(std::numeric_limits<int64_t>::max() / 2 + 1));

  EXPECT_FALSE(absl::has_single_bit(std::numeric_limits<uint64_t>::min()));
  EXPECT_FALSE(absl::has_single_bit(std::numeric_limits<uint64_t>::max()));
  EXPECT_TRUE(
      absl::has_single_bit(std::numeric_limits<uint64_t>::max() / 2 + 1));
}

static bool SlowBytesContainByte(uint32_t x, uint8_t b) {
  return (x & 0xff) == b || ((x >> 8) & 0xff) == b || ((x >> 16) & 0xff) == b ||
         ((x >> 24) & 0xff) == b;
}

static bool SlowBytesContainByte(uint64_t x, uint8_t b) {
  uint32_t u = x;
  uint32_t v = x >> 32;
  return SlowBytesContainByte(u, b) || SlowBytesContainByte(v, b);
}

static bool SlowBytesContainByteLessThan(uint32_t x, uint8_t b) {
  return (x & 0xff) < b || ((x >> 8) & 0xff) < b || ((x >> 16) & 0xff) < b ||
         ((x >> 24) & 0xff) < b;
}

static bool SlowBytesContainByteLessThan(uint64_t x, uint8_t b) {
  uint32_t u = x;
  uint32_t v = x >> 32;
  return SlowBytesContainByteLessThan(u, b) ||
         SlowBytesContainByteLessThan(v, b);
}

TEST_F(BitsTest, BytesContainByte) {
  std::cout << "TestBytesContainByte" << std::endl;
  for (int i = 0; i < absl::GetFlag(FLAGS_num_iterations); i++) {
    uint32_t u32 = absl::Uniform<uint32_t>(random_);
    uint64_t u64 = absl::Uniform<uint64_t>(random_);
    int64_t s64 = u64;
    uint8_t b = absl::Uniform<uint32_t>(random_) >> 13;

    EXPECT_EQ(Bits::BytesContainByte<uint32_t>(u32, b),
              SlowBytesContainByte(u32, b));
    EXPECT_EQ(Bits::BytesContainByte<uint64_t>(u64, b),
              SlowBytesContainByte(u64, b));
    EXPECT_EQ(Bits::BytesContainByte<uint64_t>(u64, b),
              Bits::BytesContainByte<int64_t>(s64, b));

    EXPECT_EQ(Bits::BytesContainByteLessThan<uint32_t>(u32, b),
              SlowBytesContainByteLessThan(u32, b));
    EXPECT_EQ(Bits::BytesContainByteLessThan<uint64_t>(u64, b),
              SlowBytesContainByteLessThan(u64, b));
    EXPECT_EQ(Bits::BytesContainByteLessThan<uint64_t>(u64, b),
              Bits::BytesContainByteLessThan<int64_t>(s64, b));
  }
}

static bool ByteInRange(uint8_t x, uint8_t lo, uint8_t hi) {
  return lo <= x && x <= hi;
}

// True if all bytes in x are in [lo, hi].
static bool SlowBytesAllInRange(uint32_t x, uint8_t lo, uint8_t hi) {
  return ByteInRange(x, lo, hi) && ByteInRange(x >> 8, lo, hi) &&
         ByteInRange(x >> 16, lo, hi) && ByteInRange(x >> 24, lo, hi);
}

// True if all bytes in x are in [lo, hi].
static bool SlowBytesAllInRange(uint64_t x, uint8_t lo, uint8_t hi) {
  uint32_t u = x;
  uint32_t v = x >> 32;
  return SlowBytesAllInRange(u, lo, hi) && SlowBytesAllInRange(v, lo, hi);
}

TEST_F(BitsTest, BytesAllInRange) {
  std::cout << "TestBytesAllInRange" << std::endl;
  for (int i = 0; i < absl::GetFlag(FLAGS_num_iterations); i++) {
    uint32_t u32 = absl::Uniform<uint32_t>(random_);
    uint64_t u64 = absl::Uniform<uint64_t>(random_);
    int64_t s64 = u64;
    uint8_t lo = absl::Uniform<uint32_t>(random_) >> 13;
    uint8_t hi = absl::Uniform<uint32_t>(random_) >> 13;
    if (i > 5 && lo > hi) {
      std::swap(lo, hi);
    }

    EXPECT_EQ(Bits::BytesAllInRange<uint32_t>(u32, lo, hi),
              SlowBytesAllInRange(u32, lo, hi));
    EXPECT_EQ(Bits::BytesAllInRange<uint64_t>(u64, lo, hi),
              SlowBytesAllInRange(u64, lo, hi));
    EXPECT_EQ(Bits::BytesAllInRange<uint64_t>(u64, lo, hi),
              Bits::BytesAllInRange<int64_t>(s64, lo, hi));
  }
}

template <class T>
static int BytesContainByteLessThanAggregatePercentage(absl::Span<const T> v,
                                                       uint8_t b) {
  int yes = 0;
  for (int i = 0; i < v.size(); i++) {
    yes += Bits::BytesContainByteLessThan<T>(v[i], b);
  }
  return yes * 100 / v.size();
}

template <class T, bool F(T, uint8_t)>
static void BM_BytesContainByteLessThan(benchmark::State& state,
                                        int min_percent_false) {
  static const int kNumRandomNumbers = 4096;
  static const uint8_t k0 = 20;
  static const uint8_t k1 = k0 + 128;
  const int max_percent_true = 100 - min_percent_false;
  int percent_to_change = 0;
  std::vector<T> v;
  std::vector<T> w;
  absl::BitGen random = MakeTestBitGen();
  for (int i = 0; i < kNumRandomNumbers; i++) {
    v.push_back(absl::Uniform<uint64_t>(random));
    w.push_back(absl::Uniform<uint64_t>(random));
  }
  do {
    percent_to_change = BytesContainByteLessThanAggregatePercentage<T>(v, k0) -
                        max_percent_true;
    for (int i = 0; i < kNumRandomNumbers * percent_to_change / 100; i++) {
      v[absl::Uniform<uint32_t>(random, 0, kNumRandomNumbers)] |=
          absl::Uniform<uint64_t>(random);
    }
  } while (percent_to_change > 0);
  do {
    percent_to_change = BytesContainByteLessThanAggregatePercentage<T>(w, k1) -
                        max_percent_true;
    for (int i = 0; i < kNumRandomNumbers * percent_to_change / 100; i++) {
      w[absl::Uniform<uint32_t>(random, 0, kNumRandomNumbers)] |=
          absl::Uniform<uint64_t>(random);
    }
  } while (percent_to_change > 0);

  int result = 0;
  int j = 0;
  for (auto _ : state) {
    if (j >= state.max_iterations) break;
    int stop_index = std::min<int>(j, kNumRandomNumbers);
    for (int i = 0; i < stop_index; i++) {
      result += F(v[i], k0) ? 3 : -1;
      result += F(w[i], k1) ? 3 : -1;
    }
    j += stop_index;
  }
  benchmark::DoNotOptimize(result);
  state.SetBytesProcessed(sizeof(T) * 2 * state.iterations());
}

// The expected use cases should have Bits::ContainsByteLessThan<T>(x) returning
// false most of the time; otherwise, why not use a brute force search?
#define PERCENT_FALSE0 90
#define PERCENT_FALSE1 98

void BM_Uint32ContainsByteLessThan(benchmark::State& state) {
  BM_BytesContainByteLessThan<uint32_t, Bits::BytesContainByteLessThan>(
      state, state.range(0));
}
BENCHMARK(BM_Uint32ContainsByteLessThan)
    ->Arg(PERCENT_FALSE0)
    ->Arg(PERCENT_FALSE1);

void BM_Uint64ContainsByteLessThan(benchmark::State& state) {
  BM_BytesContainByteLessThan<uint64_t, Bits::BytesContainByteLessThan>(
      state, state.range(0));
}
BENCHMARK(BM_Uint64ContainsByteLessThan)
    ->Arg(PERCENT_FALSE0)
    ->Arg(PERCENT_FALSE1);

TEST_F(BitsTest, Log2EdgeCases) {
  std::cout << "TestLog2EdgeCases" << std::endl;

  EXPECT_EQ(-1, (absl::bit_width(static_cast<uint32_t>(0)) - 1));
  EXPECT_EQ(-1, (absl::bit_width(static_cast<uint64_t>(0)) - 1));
  EXPECT_EQ(-1, Bits::Log2Floor128(absl::uint128(0)));
  EXPECT_EQ(-1, Bits::Log2Ceiling(0));
  EXPECT_EQ(-1, Bits::Log2Ceiling64(0));
  EXPECT_EQ(-1, Bits::Log2Ceiling128(absl::uint128(0)));

  for (int i = 0; i < 32; i++) {
    uint32_t n = 1U << i;
    EXPECT_EQ(i, (absl::bit_width(n) - 1));
    EXPECT_EQ(i, Bits::Log2FloorNonZero(n));
    EXPECT_EQ(i, Bits::Log2Ceiling(n));
    if (n > 2) {
      EXPECT_EQ(i - 1, (absl::bit_width(n - 1) - 1));
      EXPECT_EQ(i, (absl::bit_width(n + 1) - 1));
      EXPECT_EQ(i - 1, Bits::Log2FloorNonZero(n - 1));
      EXPECT_EQ(i, Bits::Log2FloorNonZero(n + 1));
      EXPECT_EQ(i, Bits::Log2Ceiling(n - 1));
      EXPECT_EQ(i + 1, Bits::Log2Ceiling(n + 1));
    }
  }

  for (int i = 0; i < 64; i++) {
    uint64_t n = 1ULL << i;
    EXPECT_EQ(i, (absl::bit_width(n) - 1));
    EXPECT_EQ(i, Bits::Log2FloorNonZero64(n));
    EXPECT_EQ(i, Bits::Log2Ceiling64(n));
    if (n > 2) {
      EXPECT_EQ(i - 1, (absl::bit_width(n - 1) - 1));
      EXPECT_EQ(i, (absl::bit_width(n + 1) - 1));
      EXPECT_EQ(i - 1, Bits::Log2FloorNonZero64(n - 1));
      EXPECT_EQ(i, Bits::Log2FloorNonZero64(n + 1));
      EXPECT_EQ(i, Bits::Log2Ceiling64(n - 1));
      EXPECT_EQ(i + 1, Bits::Log2Ceiling64(n + 1));
    }
  }

  for (int i = 0; i < 128; i++) {
    absl::uint128 n = absl::uint128(1) << i;
    EXPECT_EQ(i, Bits::Log2Floor128(n));
    EXPECT_EQ(i, Bits::Log2Ceiling128(n));
    if (n > 2) {
      EXPECT_EQ(i - 1, Bits::Log2Floor128(n - 1));
      EXPECT_EQ(i, Bits::Log2Floor128(n + 1));
      EXPECT_EQ(i, Bits::Log2Ceiling128(n - 1));
      EXPECT_EQ(i + 1, Bits::Log2Ceiling128(n + 1));
    }
  }
}

TEST_F(BitsTest, Log2Random) {
  std::cout << "TestLog2Random" << std::endl;

  for (int i = 0; i < absl::GetFlag(FLAGS_num_iterations); i++) {
    int maxbit = -1;
    uint32_t n = 0;
    while (!absl::Bernoulli(random_, 1.0 / 32)) {
      int bit = absl::Uniform<int32_t>(random_, 0, 32);
      n |= (1U << bit);
      maxbit = std::max(bit, maxbit);
    }
    EXPECT_EQ(maxbit, (absl::bit_width(n) - 1));
    if (n != 0) {
      EXPECT_EQ(maxbit, Bits::Log2FloorNonZero(n));
    }
  }
}

TEST_F(BitsTest, Log2Random64) {
  std::cout << "TestLog2Random64" << std::endl;

  for (int i = 0; i < absl::GetFlag(FLAGS_num_iterations); i++) {
    int maxbit = -1;
    uint64_t n = 0;
    while (!absl::Bernoulli(random_, 1.0 / 64)) {
      int bit = absl::Uniform<int32_t>(random_, 0, 64);
      n |= (1ULL << bit);
      maxbit = std::max(bit, maxbit);
    }
    EXPECT_EQ(maxbit, (absl::bit_width(n) - 1));
    if (n != 0) {
      EXPECT_EQ(maxbit, Bits::Log2FloorNonZero64(n));
    }
  }
}

TEST_F(BitsTest, Log2Random128) {
  std::cout << "TestLog2Random128" << std::endl;

  for (int i = 0; i < absl::GetFlag(FLAGS_num_iterations); i++) {
    int maxbit = -1;
    absl::uint128 n = absl::uint128(0);
    while (!absl::Bernoulli(random_, 1.0 / 128)) {
      int bit = absl::Uniform<int32_t>(random_, 0, 128);
      n |= (absl::uint128(1) << bit);
      maxbit = std::max(bit, maxbit);
    }
    EXPECT_EQ(maxbit, Bits::Log2Floor128(n));
  }
}

TEST_F(BitsTest, GetBits) {
  const int8_t s8_src = 0x12;
  EXPECT_EQ(0x2, Bits::GetBits(s8_src, 0, 4));
  EXPECT_EQ(0x1, Bits::GetBits(s8_src, 4, 4));
  EXPECT_EQ(0, Bits::GetBits(s8_src, 0, 0));

  const uint8_t u8_src = 0x12;
  EXPECT_EQ(0x2, Bits::GetBits(u8_src, 0, 4));
  EXPECT_EQ(0x1, Bits::GetBits(u8_src, 4, 4));
  EXPECT_EQ(0, Bits::GetBits(u8_src, 0, 0));
  EXPECT_EQ(u8_src, Bits::GetBits(u8_src, 0, 8));

  const int16_t s16_src = 0x1234;
  EXPECT_EQ(0x34, Bits::GetBits(s16_src, 0, 8));
  EXPECT_EQ(0x23, Bits::GetBits(s16_src, 4, 8));
  EXPECT_EQ(0x12, Bits::GetBits(s16_src, 8, 8));
  EXPECT_EQ(0, Bits::GetBits(s16_src, 0, 0));

  const uint16_t u16_src = 0x1234;
  EXPECT_EQ(0x34, Bits::GetBits(u16_src, 0, 8));
  EXPECT_EQ(0x23, Bits::GetBits(u16_src, 4, 8));
  EXPECT_EQ(0x12, Bits::GetBits(u16_src, 8, 8));
  EXPECT_EQ(0, Bits::GetBits(u16_src, 0, 0));
  EXPECT_EQ(u16_src, Bits::GetBits(u16_src, 0, 16));

  const int32_t s32_src = 0x12345678;
  EXPECT_EQ(0x5678, Bits::GetBits(s32_src, 0, 16));
  EXPECT_EQ(0x3456, Bits::GetBits(s32_src, 8, 16));
  EXPECT_EQ(0x1234, Bits::GetBits(s32_src, 16, 16));
  EXPECT_EQ(0, Bits::GetBits(s32_src, 0, 0));

  const uint32_t u32_src = 0x12345678;
  EXPECT_EQ(0x5678, Bits::GetBits(u32_src, 0, 16));
  EXPECT_EQ(0x3456, Bits::GetBits(u32_src, 8, 16));
  EXPECT_EQ(0x1234, Bits::GetBits(u32_src, 16, 16));
  EXPECT_EQ(0, Bits::GetBits(u32_src, 0, 0));
  EXPECT_EQ(u32_src, Bits::GetBits(u32_src, 0, 32));

  const int64_t s64_src = 0x123456789abcdef0LL;
  EXPECT_EQ(0x9abcdef0, Bits::GetBits(s64_src, 0, 32));
  EXPECT_EQ(0x56789abc, Bits::GetBits(s64_src, 16, 32));
  EXPECT_EQ(0x12345678, Bits::GetBits(s64_src, 32, 32));
  EXPECT_EQ(0, Bits::GetBits(s64_src, 0, 0));

  const uint64_t u64_src = 0x123456789abcdef0ULL;
  EXPECT_EQ(0x9abcdef0, Bits::GetBits(u64_src, 0, 32));
  EXPECT_EQ(0x56789abc, Bits::GetBits(u64_src, 16, 32));
  EXPECT_EQ(0x12345678, Bits::GetBits(u64_src, 32, 32));
  EXPECT_EQ(0, Bits::GetBits(u64_src, 0, 0));
  EXPECT_EQ(u64_src, Bits::GetBits(u64_src, 0, 64));

  const absl::uint128 u128_src =
      absl::MakeUint128(0x123456789abcdef0ULL, 0x123456789abcdef0ULL);
  EXPECT_EQ(absl::uint128(0x9abcdef0), Bits::GetBits(u128_src, 0, 32));
  EXPECT_EQ(absl::uint128(0x56789abc), Bits::GetBits(u128_src, 16, 32));
  EXPECT_EQ(absl::uint128(0x12345678), Bits::GetBits(u128_src, 32, 32));
  EXPECT_EQ(absl::uint128(0x9abcdef012345678ULL),
            Bits::GetBits(u128_src, 32, 64));
  EXPECT_EQ(absl::uint128(0x123456789abcdef0ULL),
            Bits::GetBits(u128_src, 64, 64));
  EXPECT_EQ(u128_src, Bits::GetBits(u128_src, 0, 128));
  EXPECT_EQ(0, Bits::GetBits(u128_src, 0, 0));
  EXPECT_EQ(absl::MakeUint128(0x000000009abcdef0ULL, 0x123456789abcdef0ULL),
            Bits::GetBits(u128_src, 0, 96));
}

TEST_F(BitsTest, SetBitsTest) {
  const int8_t s8_dest = 0x12;
  EXPECT_EQ(0, SetBits(s8_dest, 0, 0, 8));
  EXPECT_EQ(-1, SetBits(s8_dest, 0xff, 0, 8));
  EXPECT_EQ(0x32, SetBits(s8_dest, 0xf3, 4, 4));

  const uint8_t u8_dest = 0x12;
  EXPECT_EQ(0, SetBits(u8_dest, 0, 0, 8));
  EXPECT_EQ(0xff, SetBits(u8_dest, 0xff, 0, 8));
  // Should only write the lower 4 bits of value.
  EXPECT_EQ(0x32, SetBits(u8_dest, 0xf3, 4, 4));

  const int16_t s16_dest = 0x1234;
  EXPECT_EQ(0, SetBits(s16_dest, 0, 0, 16));
  EXPECT_EQ(-1, SetBits(s16_dest, 0xffff, 0, 16));
  EXPECT_EQ(0x1254, SetBits(s16_dest, 0xf5, 4, 4));

  const uint16_t u16_dest = 0x1234;
  EXPECT_EQ(0, SetBits(u16_dest, 0, 0, 16));
  EXPECT_EQ(0xffff, SetBits(u16_dest, 0xffff, 0, 16));
  EXPECT_EQ(0x1254, SetBits(u16_dest, 0xf5, 4, 4));

  const int32_t s32_dest = 0x12345678;
  EXPECT_EQ(0, SetBits(s32_dest, 0, 0, 32));
  EXPECT_EQ(-1, SetBits(s32_dest, 0xffffffff, 0, 32));
  EXPECT_EQ(0x12345698, SetBits(s32_dest, 0xf9, 4, 4));

  const uint32_t u32_dest = 0x12345678;
  EXPECT_EQ(0, SetBits(u32_dest, 0, 0, 32));
  EXPECT_EQ(0xffffffff, SetBits(u32_dest, 0xffffffff, 0, 32));
  EXPECT_EQ(0x12345698, SetBits(u32_dest, 0xf9, 4, 4));

  const int64_t s64_dest = 0x123456789abcdef0LL;
  EXPECT_EQ(int64_t{0x0000000000000000}, SetBits(s64_dest, uint64_t{0}, 0, 64));
  EXPECT_EQ(-1, SetBits(s64_dest, uint64_t{0xffffffffffffffffu}, 0, 64));
  EXPECT_EQ(int64_t{0x123456789abcde10}, SetBits(s64_dest, 0xf1, 4, 4));

  const uint64_t u64_dest = 0x123456789abcdef0ULL;
  EXPECT_EQ(0, SetBits(u64_dest, 0x00000000, 0, 64));
  EXPECT_EQ(uint64_t{0xffffffffffffffffu},
            SetBits(u64_dest, uint64_t{0xffffffffffffffffu}, 0, 64));
  EXPECT_EQ(0x123456789abcde10, SetBits(u64_dest, 0xf1, 4, 4));

  const absl::uint128 u128_dest =
      absl::MakeUint128(0x123456789abcdef0ULL, 0x123456789abcdef0ULL);
  EXPECT_EQ(0, SetBits(u128_dest, absl::uint128(0), 0, 128));
  const absl::uint128 u128_all_bits =
      absl::MakeUint128(0xffffffffffffffffULL, 0xffffffffffffffffULL);
  EXPECT_EQ(absl::MakeUint128(0xffffffffffffffffULL, 0xffffffffffffffffULL),
            SetBits(u128_dest, u128_all_bits, 0, 128));
  EXPECT_EQ(absl::MakeUint128(0x123456789abcdef0ULL, 0x123456789abcde10),
            SetBits(u128_dest, absl::uint128(0xf1), 4, 4));
}

TEST_F(BitsTest, CopyBits) {
  int8_t s8_dest = 0x12;
  Bits::CopyBits(&s8_dest, 0, 0, 0, 8);
  EXPECT_EQ(0, s8_dest);
  s8_dest = 0x12;
  Bits::CopyBits(&s8_dest, 0, -1, 0, 8);
  EXPECT_EQ(-1, s8_dest);
  s8_dest = 0x12;
  Bits::CopyBits(&s8_dest, 4, 0xf3ff, 8, 4);
  EXPECT_EQ(0x32, s8_dest);

  int16_t s16_dest = 0x1234;
  Bits::CopyBits(&s16_dest, 0, 0, 0, 16);
  EXPECT_EQ(0, s16_dest);
  s16_dest = 0x1234;
  Bits::CopyBits(&s16_dest, 0, -1, 0, 16);
  EXPECT_EQ(-1, s16_dest);
  s16_dest = 0x1234;
  Bits::CopyBits(&s16_dest, 8, 0xf5fff, 12, 4);
  EXPECT_EQ(0x1534, s16_dest);

  int32_t s32_dest = 0x12345678;
  Bits::CopyBits(&s32_dest, 0, 0, 0, 32);
  EXPECT_EQ(0, s32_dest);
  s32_dest = 0x12345678;
  Bits::CopyBits(&s32_dest, 0, -1, 0, 32);
  EXPECT_EQ(-1, s32_dest);
  s32_dest = 0x12345678;
  Bits::CopyBits(&s32_dest, 12, 0xf9ffff, 16, 4);
  EXPECT_EQ(0x12349678, s32_dest);

  int64_t s64_dest = 0x123456789abcdef0LL;
  Bits::CopyBits(&s64_dest, 0, int64_t{0}, 0, 64);
  EXPECT_EQ(0, s64_dest);
  s64_dest = 0x123456789abcdef0LL;
  Bits::CopyBits(&s64_dest, 0, int64_t{-1}, 0, 64);
  EXPECT_EQ(-1, s64_dest);
  s64_dest = 0x123456789abcdef0LL;
  Bits::CopyBits(&s64_dest, 16, 0xf1fffff, 20, 4);
  EXPECT_EQ(0x123456789ab1def0, s64_dest);
}

TEST_F(BitsTest, RandomCopyBitsTest) {
  RandomCopyBitsTestDestType<int8_t>();
  RandomCopyBitsTestDestType<uint8_t>();
  RandomCopyBitsTestDestType<int16_t>();
  RandomCopyBitsTestDestType<uint16_t>();
  RandomCopyBitsTestDestType<int32_t>();
  RandomCopyBitsTestDestType<uint32_t>();
  RandomCopyBitsTestDestType<int64_t>();
  RandomCopyBitsTestDestType<uint64_t>();
  RandomCopyBitsTestDestType<absl::uint128>();
  RandomCopyBitsTestTwoTypes<absl::uint128, absl::uint128, true>();
}

TEST_F(BitsTest, GetLowBits) {
  TestGetLowBitsType<int8_t>();
  TestGetLowBitsType<uint8_t>();
  TestGetLowBitsType<int16_t>();
  TestGetLowBitsType<uint16_t>();
  TestGetLowBitsType<int32_t>();
  TestGetLowBitsType<uint32_t>();
  TestGetLowBitsType<int64_t>();
  TestGetLowBitsType<uint64_t>();
  TestGetLowBitsType<absl::int128>();
  TestGetLowBitsType<absl::uint128>();
}

TEST(FindLSBSetNonZero, OneAllOrSomeBitsSet) {
  uint32_t testone = 0x00000001;
  uint32_t testall = 0xFFFFFFFF;
  uint32_t testsome = 0x87654321;
  for (int i = 0; i < 32; ++i) {
    EXPECT_EQ(i, Bits::FindLSBSetNonZero(testone));
    EXPECT_EQ(i, Bits::FindLSBSetNonZero(testall));
    EXPECT_EQ(i, Bits::FindLSBSetNonZero(testsome));
    testone <<= 1;
    testall <<= 1;
    testsome <<= 1;
  }
}

TEST(FindLSBSetNonZero64, OneAllOrSomeBitsSet) {
  uint64_t testone = 0x0000000000000001ULL;
  uint64_t testall = 0xFFFFFFFFFFFFFFFFULL;
  uint64_t testsome = 0x0FEDCBA987654321ULL;
  for (int i = 0; i < 64; ++i) {
    EXPECT_EQ(i, Bits::FindLSBSetNonZero64(testone));
    EXPECT_EQ(i, Bits::FindLSBSetNonZero64(testall));
    EXPECT_EQ(i, Bits::FindLSBSetNonZero64(testsome));
    testone <<= 1;
    testall <<= 1;
    testsome <<= 1;
  }
}

TEST(FindLSBSetNonZero128, OneAllOrSomeBitsSet) {
  absl::uint128 testone = absl::uint128(1);
  absl::uint128 testall = ~absl::uint128(0);
  absl::uint128 testsome =
      absl::MakeUint128(0x0FEDCBA987654321ULL, 0x0FEDCBA987654321ULL);
  for (int i = 0; i < 128; ++i) {
    EXPECT_EQ(i, Bits::FindLSBSetNonZero128(testone));
    EXPECT_EQ(i, Bits::FindLSBSetNonZero128(testall));
    EXPECT_EQ(i, Bits::FindLSBSetNonZero128(testsome));
    testone <<= 1;
    testall <<= 1;
    testsome <<= 1;
  }
}

TEST(FindMSBSetNonZero, OneAllOrSomeBitsSet) {
  uint32_t testone = 0x80000000;
  uint32_t testall = 0xFFFFFFFF;
  uint32_t testsome = 0x87654321;
  for (int i = 31; i >= 0; --i) {
    EXPECT_EQ(i, Bits::FindMSBSetNonZero(testone));
    EXPECT_EQ(i, Bits::FindMSBSetNonZero(testall));
    EXPECT_EQ(i, Bits::FindMSBSetNonZero(testsome));
    testone >>= 1;
    testall >>= 1;
    testsome >>= 1;
  }
}

TEST(FindMSBSetNonZero64, OneAllOrSomeBitsSet) {
  uint64_t testone = 0x8000000000000000ULL;
  uint64_t testall = 0xFFFFFFFFFFFFFFFFULL;
  uint64_t testsome = 0xFEDCBA9876543210ULL;
  for (int i = 63; i >= 0; --i) {
    EXPECT_EQ(i, Bits::FindMSBSetNonZero64(testone));
    EXPECT_EQ(i, Bits::FindMSBSetNonZero64(testall));
    EXPECT_EQ(i, Bits::FindMSBSetNonZero64(testsome));
    testone >>= 1;
    testall >>= 1;
    testsome >>= 1;
  }
}

TEST(FindMSBSetNonZero128, OneAllOrSomeBitsSet) {
  absl::uint128 testone = absl::MakeUint128(0x8000000000000000ULL, 0);
  absl::uint128 testall = ~absl::uint128(0);
  absl::uint128 testsome =
      absl::MakeUint128(0xFEDCBA9876543210ULL, 0xFEDCBA9876543210ULL);
  for (int i = 127; i >= 0; --i) {
    EXPECT_EQ(i, Bits::FindMSBSetNonZero128(testone));
    EXPECT_EQ(i, Bits::FindMSBSetNonZero128(testall));
    EXPECT_EQ(i, Bits::FindMSBSetNonZero128(testsome));
    testone >>= 1;
    testall >>= 1;
    testsome >>= 1;
  }
}

template <typename T, int func(T)>
void BM_FindXSBSetNonZero(benchmark::State& state) {
  // This is volatile so that a compiler cannot optimize repeated calls to a
  // pure function with a loop-invariant argument by only calling the function
  // once and multiply result with loop count.
  volatile T word = static_cast<T>(1) << state.range(0);
  int64_t sum = 0;
  for (auto _ : state) {
    sum += func(word);
  }
  CHECK_EQ(sum, state.range(0) * state.iterations());
}

void BM_FindLSBSetNonZero(benchmark::State& state) {
  BM_FindXSBSetNonZero<uint32_t, Bits::FindLSBSetNonZero>(state);
}

void BM_FindLSBSetNonZero64(benchmark::State& state) {
  BM_FindXSBSetNonZero<uint64_t, Bits::FindLSBSetNonZero64>(state);
}

void BM_FindMSBSetNonZero(benchmark::State& state) {
  BM_FindXSBSetNonZero<uint32_t, Bits::FindMSBSetNonZero>(state);
}

void BM_FindMSBSetNonZero64(benchmark::State& state) {
  BM_FindXSBSetNonZero<uint64_t, Bits::FindMSBSetNonZero64>(state);
}

BENCHMARK(BM_FindLSBSetNonZero)->Arg(0)->Arg(4)->Arg(8)->Arg(16)->Arg(31);
BENCHMARK(BM_FindLSBSetNonZero64)
    ->Arg(0)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)
    ->Arg(32)
    ->Arg(48)
    ->Arg(63);
BENCHMARK(BM_FindMSBSetNonZero)->Arg(0)->Arg(4)->Arg(8)->Arg(16)->Arg(31);
BENCHMARK(BM_FindMSBSetNonZero64)
    ->Arg(0)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)
    ->Arg(32)
    ->Arg(48)
    ->Arg(63);

// Function that does what ReverseBits*() do, but doing a bit-by-bit walk.
// The ReverseBits*() functions are much more efficient.
template <class T>
static T ExpectedReverseBits(T n) {
  T r = 0;
  for (int i = 0; i < sizeof(T) << 3; ++i) {
    r = (r << 1) | (n & 1);
    n >>= 1;
  }
  return r;
}

TEST(ReverseBits, InByte) {
  EXPECT_EQ(0, Bits::ReverseBits8(0));
  EXPECT_EQ(0xff, Bits::ReverseBits8(0xff));
  EXPECT_EQ(0x80, Bits::ReverseBits8(0x01));
  EXPECT_EQ(0x01, Bits::ReverseBits8(0x80));

  absl::BitGen random = MakeTestBitGen();
  for (int i = 0; i < kNumReverseBitsRandomTests; ++i) {
    const uint8_t n = absl::Uniform<uint8_t>(random);
    const uint8_t r = Bits::ReverseBits8(n);
    EXPECT_EQ(n, Bits::ReverseBits8(r)) << n;
    EXPECT_EQ(ExpectedReverseBits<uint8_t>(n), r) << n;
    EXPECT_EQ(absl::popcount(n), absl::popcount(r)) << n;
  }
}

TEST(ReverseBits, In16BitWord) {
  EXPECT_EQ(0, Bits::ReverseBits16(0));
  EXPECT_EQ(0xffff, Bits::ReverseBits16(0xffff));
  EXPECT_EQ(0x8000, Bits::ReverseBits16(0x0001));
  EXPECT_EQ(0x0001, Bits::ReverseBits16(0x8000));
  EXPECT_EQ(0x5555, Bits::ReverseBits16(0xaaaa));
  EXPECT_EQ(0xaaaa, Bits::ReverseBits16(0x5555));
  EXPECT_EQ(0xcafe, Bits::ReverseBits16(0x7f53));
  EXPECT_EQ(0x7f53, Bits::ReverseBits16(0xcafe));

  absl::BitGen random = MakeTestBitGen();
  for (int i = 0; i < kNumReverseBitsRandomTests; ++i) {
    const uint16_t n = absl::Uniform<uint16_t>(random);
    const uint16_t r = Bits::ReverseBits16(n);
    EXPECT_EQ(n, Bits::ReverseBits16(r)) << n;
    EXPECT_EQ(ExpectedReverseBits<uint16_t>(n), r) << n;
    EXPECT_EQ(absl::popcount(n), absl::popcount(r)) << n;
  }
}

TEST(ReverseBits, In32BitWord) {
  EXPECT_EQ(0, Bits::ReverseBits32(0));
  EXPECT_EQ(0xffffffff, Bits::ReverseBits32(0xffffffff));
  EXPECT_EQ(0x80000000, Bits::ReverseBits32(0x00000001));
  EXPECT_EQ(0x00000001, Bits::ReverseBits32(0x80000000));
  EXPECT_EQ(0x55555555, Bits::ReverseBits32(0xaaaaaaaa));
  EXPECT_EQ(0xaaaaaaaa, Bits::ReverseBits32(0x55555555));
  EXPECT_EQ(0xcafebabe, Bits::ReverseBits32(0x7d5d7f53));
  EXPECT_EQ(0x7d5d7f53, Bits::ReverseBits32(0xcafebabe));

  absl::BitGen random = MakeTestBitGen();
  for (int i = 0; i < kNumReverseBitsRandomTests; ++i) {
    const uint32_t n = absl::Uniform<uint32_t>(random);
    const uint32_t r = Bits::ReverseBits32(n);
    EXPECT_EQ(n, Bits::ReverseBits32(r)) << n;
    EXPECT_EQ(ExpectedReverseBits<uint32_t>(n), r) << n;
    EXPECT_EQ(absl::popcount(n), absl::popcount(r)) << n;
  }
}

TEST(ReverseBits, In64BitWord) {
  EXPECT_EQ(uint64_t{0}, Bits::ReverseBits64(uint64_t{0}));
  EXPECT_EQ(uint64_t{0xffffffffffffffffu},
            Bits::ReverseBits64(uint64_t{0xffffffffffffffffu}));
  EXPECT_EQ(uint64_t{0x8000000000000000u},
            Bits::ReverseBits64(uint64_t{0x0000000000000001}));
  EXPECT_EQ(uint64_t{0x0000000000000001},
            Bits::ReverseBits64(uint64_t{0x8000000000000000u}));
  EXPECT_EQ(uint64_t{0x5555555555555555},
            Bits::ReverseBits64(uint64_t{0xaaaaaaaaaaaaaaaau}));
  EXPECT_EQ(uint64_t{0xaaaaaaaaaaaaaaaau},
            Bits::ReverseBits64(uint64_t{0x5555555555555555}));
  // Use a non-constant expression to avoid constant-folding optimizations.
  uint64_t not_constant;
  ASSERT_TRUE(absl::SimpleHexAtoi("084c2a6e195d3b7f", &not_constant));
  EXPECT_EQ(uint64_t{0xfedcba9876543210u}, Bits::ReverseBits64(not_constant));

  absl::BitGen random = MakeTestBitGen();
  for (int i = 0; i < kNumReverseBitsRandomTests; ++i) {
    const uint64_t n = absl::Uniform<uint64_t>(random);
    const uint64_t r = Bits::ReverseBits64(n);
    EXPECT_EQ(n, Bits::ReverseBits64(r)) << n;
    EXPECT_EQ(ExpectedReverseBits<uint64_t>(n), r) << n;
    EXPECT_EQ(absl::popcount(n), absl::popcount(Bits::ReverseBits64(n))) << n;
  }
}

TEST(ReverseBits, In128BitWord) {
  EXPECT_EQ(absl::uint128(0), Bits::ReverseBits128(absl::uint128(0)));
  EXPECT_EQ(~absl::uint128(0), Bits::ReverseBits128(~absl::uint128(0)));
  EXPECT_EQ(absl::MakeUint128(0x8000000000000000ull, 0),
            Bits::ReverseBits128(absl::uint128(1)));
  EXPECT_EQ(absl::uint128(1),
            Bits::ReverseBits128(absl::MakeUint128(0x8000000000000000ull, 0)));
  EXPECT_EQ(absl::MakeUint128(0x5555555555555555ull, 0x5555555555555555ull),
            Bits::ReverseBits128(absl::MakeUint128(0xaaaaaaaaaaaaaaaaull,
                                                   0xaaaaaaaaaaaaaaaaull)));
  EXPECT_EQ(absl::MakeUint128(0xaaaaaaaaaaaaaaaaull, 0xaaaaaaaaaaaaaaaaull),
            Bits::ReverseBits128(absl::MakeUint128(0x5555555555555555ull,
                                                   0x5555555555555555ull)));

  absl::BitGen random = MakeTestBitGen();
  for (int i = 0; i < kNumReverseBitsRandomTests; ++i) {
    const absl::uint128 n = absl::MakeUint128(absl::Uniform<uint64_t>(random),
                                              absl::Uniform<uint64_t>(random));
    const absl::uint128 r = Bits::ReverseBits128(n);
    EXPECT_EQ(n, Bits::ReverseBits128(r)) << n;
    EXPECT_EQ(ExpectedReverseBits<absl::uint128>(n), r) << n;
    EXPECT_EQ(Bits::CountOnes128(n), Bits::CountOnes128(r)) << n;
  }
}

// This must be an unsigned power of 2 so that % kNumRandomNumbersForBenchmark
// can be computed cheaply.  Otherwise, we skew our results by doing signed
// division in benchmarks loops.
static constexpr uint32_t kNumRandomNumbersForBenchmark = 16;

template <class T>
static void RandomNumbersForBenchmark(std::vector<T>* nums) {
  absl::BitGen random = MakeTestBitGen();
  nums->clear();
  nums->resize(kNumRandomNumbersForBenchmark, 0);
  for (int i = 0; i < kNumRandomNumbersForBenchmark; ++i) {
    (*nums)[i] = static_cast<T>(absl::Uniform<uint64_t>(random) >>
                                (64 - (sizeof(T) << 3)));
  }
}

template <class T>
static void RandomNumbersForLeadingZerosBenchmark(std::vector<T>* nums) {
  absl::BitGen random = MakeTestBitGen();
  nums->clear();
  nums->resize(kNumRandomNumbersForBenchmark, 0);
  const uint64_t top_bit = static_cast<uint64_t>(1) << 63;
  for (int i = 0; i < kNumRandomNumbersForBenchmark; ++i) {
    int count = absl::Uniform<uint32_t>(random, 0, sizeof(T) * 8 + 1);
    (*nums)[i] =
        (count == sizeof(T) * 8
             ? 0
             : static_cast<T>(absl::Uniform<uint64_t>(random) | top_bit) >>
                   count);
  }
}

void BM_ReverseBits8(benchmark::State& state) {
  std::vector<uint8_t> nums;
  RandomNumbersForBenchmark<uint8_t>(&nums);
  uint8_t x = 0;
  int i = 0;
  for (auto _ : state) {
    x += Bits::ReverseBits8(nums[i++ % kNumRandomNumbersForBenchmark]);
  }
  benchmark::DoNotOptimize(x);
}

void BM_ReverseBits16(benchmark::State& state) {
  std::vector<uint16_t> nums;
  RandomNumbersForBenchmark<uint16_t>(&nums);
  uint16_t x = 0;
  int i = 0;
  for (auto _ : state) {
    x += Bits::ReverseBits16(nums[i++ % kNumRandomNumbersForBenchmark]);
  }
  benchmark::DoNotOptimize(x);
}

void BM_ReverseBits32(benchmark::State& state) {
  std::vector<uint32_t> nums;
  RandomNumbersForBenchmark<uint32_t>(&nums);
  uint32_t x = 0;
  int i = 0;
  for (auto _ : state) {
    x += Bits::ReverseBits32(nums[i++ % kNumRandomNumbersForBenchmark]);
  }
  benchmark::DoNotOptimize(x);
}

void BM_ReverseBits64(benchmark::State& state) {
  std::vector<uint64_t> nums;
  RandomNumbersForBenchmark<uint64_t>(&nums);
  uint64_t x = 0;
  int i = 0;
  for (auto _ : state) {
    x += Bits::ReverseBits64(nums[i++ % kNumRandomNumbersForBenchmark]);
  }
  benchmark::DoNotOptimize(x);
}

BENCHMARK(BM_ReverseBits8);
BENCHMARK(BM_ReverseBits16);
BENCHMARK(BM_ReverseBits32);
BENCHMARK(BM_ReverseBits64);

// Mutated versions of sideways addition used only for benchmarking against the
// optimized builtins.
//
// LLVM has gotten clever enough to recognize commonly-used versions of this
// code; it silently replaces them with popcount:
//  https://reviews.llvm.org/D68189
//
// These versions replace ((n >> 2) & 0x333[...]) with ((n >> 2) & 0xB33[...]),
// which is equivalent but throws off LLVM's pattern matching. This enables a
// valid comparison of the naive sideways versions against popcount.
static inline int CountOnes32Sideways(uint32_t n) {
  n -= ((n >> 1) & 0x55555555);
  n = ((n >> 2) & 0xB3333333) + (n & 0x33333333);
  return (((n + (n >> 4)) & 0xF0F0F0F) * 0x1010101) >> 24;
}

static inline int CountOnes64Sideways(uint64_t n) {
#if defined(_LP64)
  n -= (n >> 1) & 0x5555555555555555ULL;
  n = ((n >> 2) & 0xB333333333333333ULL) + (n & 0x3333333333333333ULL);
  return (((n + (n >> 4)) & 0xF0F0F0F0F0F0F0FULL) * 0x101010101010101ULL) >> 56;
#else
  return CountOnes32Sideways(n >> 32) + CountOnes32Sideways(n & 0xffffffff);
#endif
}

void BM_CountOnes32(benchmark::State& state) {
  std::vector<uint32_t> nums;
  RandomNumbersForBenchmark<uint32_t>(&nums);
  uint32_t x = 0;
  int i = 0;
  for (auto _ : state) {
    x += absl::popcount(nums[i++ % kNumRandomNumbersForBenchmark]);
  }
  benchmark::DoNotOptimize(x);
}

// Explicitly benchmark the sideways addition (no popcnt builtin) version.
void BM_CountOnes32Sideways(benchmark::State& state) {
  std::vector<uint32_t> nums;
  RandomNumbersForBenchmark<uint32_t>(&nums);
  uint32_t x = 0;
  int i = 0;
  for (auto _ : state) {
    x += CountOnes32Sideways(nums[i++ % kNumRandomNumbersForBenchmark]);
  }
  benchmark::DoNotOptimize(x);
}

void BM_CountOnes64(benchmark::State& state) {
  std::vector<uint64_t> nums;
  RandomNumbersForBenchmark<uint64_t>(&nums);
  uint32_t x = 0;
  int i = 0;
  for (auto _ : state) {
    x += absl::popcount(nums[i++ % kNumRandomNumbersForBenchmark]);
  }
  benchmark::DoNotOptimize(x);
}

void BM_CountOnes64Sideways(benchmark::State& state) {
  std::vector<uint64_t> nums;
  RandomNumbersForBenchmark<uint64_t>(&nums);
  uint32_t x = 0;
  int i = 0;
  for (auto _ : state) {
    x += CountOnes64Sideways(nums[i++ % kNumRandomNumbersForBenchmark]);
  }
  benchmark::DoNotOptimize(x);
}

BENCHMARK(BM_CountOnes32);
BENCHMARK(BM_CountOnes32Sideways);
BENCHMARK(BM_CountOnes64);
BENCHMARK(BM_CountOnes64Sideways);

inline void RunDifferenceBenchmark(int num_bytes_per_call,
                                   benchmark::State& state) {
  // Creates a vector of kNumCases + kNumBytes + 1 random bytes, then repeatedly
  // calls Difference(bytes[i], bytes[i+1]).
  CHECK(num_bytes_per_call <= kMaxBytes)
      << "BM_difference does not support more than " << kMaxBytes << " bytes";

  constexpr int kNumCases = 100;

  // We need 1 extra byte at the end since the second sample for each case is
  // at i + 1.
  constexpr int kNumBytes = kNumCases + kMaxBytes + 1;

  absl::BitGen random;
  uint8_t bytes[kNumBytes];
  memset(bytes, 0, kNumBytes);
  for (int i = 0; i < kNumBytes; ++i) {
    bytes[i] =
        absl::Uniform<int32_t>(random, 0, std::numeric_limits<int8_t>::max());
  }

  for (auto s : state) {
    for (int i = 0; i < kNumCases; ++i) {
      benchmark::DoNotOptimize(
          Bits::Difference(&bytes[i], &bytes[i + 1], num_bytes_per_call));
    }
  }
}

// In these benchmarks, the compiler can determine whether or not num_bytes is
// a multiple of 8.
void BM_Difference_1(benchmark::State& state) {
  RunDifferenceBenchmark(1, state);
}
void BM_Difference_8(benchmark::State& state) {
  RunDifferenceBenchmark(8, state);
}
void BM_Difference_Sizeof(benchmark::State& state) {
  RunDifferenceBenchmark(sizeof(uint64_t), state);
}
void BM_Difference_Sizeof_2(benchmark::State& state) {
  RunDifferenceBenchmark(sizeof(uint64_t) * 2, state);
}

// In this one, it cannot.
void BM_Difference_Range(benchmark::State& state) {
  RunDifferenceBenchmark(state.range(0), state);
}

BENCHMARK(BM_Difference_1);
BENCHMARK(BM_Difference_8);
BENCHMARK(BM_Difference_Sizeof);
BENCHMARK(BM_Difference_Sizeof_2);

BENCHMARK(BM_Difference_Range)
    ->Arg(1)
    ->Arg(8)
    ->Arg(16)
    ->Arg(32)
    ->Arg(64)
    ->Arg(128);
