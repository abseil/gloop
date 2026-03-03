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

#include "gloop/util/random/random_base.h"

#include <math.h>
#include <string.h>
#include <sys/types.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/numeric/bits.h"
#include "gloop/util/random/mt_random.h"
#include "gloop/util/random/test_random-inl.h"
#include "gtest/gtest.h"

namespace {
const std::vector<int64_t> common_seq = {78, 90, 54, 39, 84, 72, 108, 0};

TEST(RandomBase, UniformRandomNumberGenerator) {
  int a = 0, b = 10;
  std::uniform_int_distribution<int> dist(a, b);

  RandomBase::result_type range = (b - a);
  RandomBase::result_type e_range = range + 1;
  RandomBase::result_type rng_range = RandomBase::max() - RandomBase::min();
  RandomBase::result_type scaling = rng_range / e_range;

  std::vector<uint> seq = {0, 1 * scaling, 2 * scaling, 3 * scaling};
  random_test::VectorSequence rand_gen(seq);

#if false
  // This is an invalid test for std::uniform_int_distribution.
  // It essentially assumes a specific implementation.  All we can
  // really test is that the code compiles; the specific results
  // are non-portable.
  EXPECT_EQ(0, dist(rand_gen));
  EXPECT_EQ(1, dist(rand_gen));
  EXPECT_EQ(2, dist(rand_gen));
  EXPECT_EQ(3, dist(rand_gen));
#endif
}

TEST(RandomBase, RandomNumberGenerator) {
  std::vector<int> v = {1, 2, 3};

  random_test::VectorSequence rand_gen(common_seq);
  EXPECT_EQ(78, rand_gen(100));

  std::shuffle(v.begin(), v.end(), rand_gen);

#if false
  // This is an invalid test; it assumes that it can predict the
  // outcome of std::random_shuffle, i.e., in effect it relies on
  // libstdc++'s current implementation choices.  All we can
  // really test is that the code compiles; the specific results
  // are non-portable.
  EXPECT_THAT(v, ::testing::ElementsAre(3, 1, 2));
#endif
}

// some simple tests to verify that the various methods
// in RandomBase work.  For more rigorus testing, see
// random_base_testutils.
TEST(RandomBase, WeakSeed32) {
  uint32_t a = RandomBase::WeakSeed32();
  uint32_t b = RandomBase::WeakSeed32();
  EXPECT_NE(a, b);
}

class MyRandomBase : public RandomBase {
 public:
  MyRandomBase() : RandomBase() {}
  ~MyRandomBase() override = default;

  static int WeakSeed(uint8_t* buffer, int length) {
    return RandomBase::WeakSeed(buffer, length);
  }
};

TEST(RandomBase, WeakSeedString) {
  const int kMaxLen = 128;
  uint8_t buf1[kMaxLen];
  uint8_t buf2[kMaxLen];
  const int len1 = MyRandomBase::WeakSeed(buf1, kMaxLen);
  const int len2 = MyRandomBase::WeakSeed(buf2, kMaxLen);

  EXPECT_EQ(len1, len2);
  EXPECT_NE(0, memcmp(buf1, buf2, std::min(len1, len2)));
}

TEST(RandomBase, Rand32) {
  random_test::VectorSequence rand_gen(common_seq);
  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(rand_gen.Rand32(), common_seq[i]);
  }
}

TEST(RandomBase, Float) {
  random_test::VectorSequence rand_gen(common_seq);
  float expected[] = {9.2983246e-06, 1.0728836e-05, 6.4373016e-06,
                      4.6491623e-06};
  for (int i = 0; i < 4; ++i) {
    EXPECT_FLOAT_EQ(rand_gen.RandFloat(), expected[i]);
  }
}

TEST(RandomBase, Double) {
  random_test::VectorSequence rand_gen(common_seq);
  double expected[] = {7.4386596699671514e-05, 5.149841309459724e-05,
                       8.0108642594112212e-05};
  for (int i = 0; i < 3; ++i) {
    EXPECT_DOUBLE_EQ(rand_gen.RandDouble(), expected[i]);
  }
}

// Tests for RandDouble() values produced using the doubles derived from
// 'common_seq'.  Relies on knowledge of implementation of RandExponential().
TEST(RandomBase, Exponential) {
  // Since RandDouble() returns numbers very close to 0.0 (see the 'Double'
  // test), RandExponential()'s result -log1p(-RandDouble()) is approximately
  // equal to (slightly larger than) the input.
  random_test::VectorSequence rand_gen(common_seq);
  double expected[] = {7.4389363519766205e-05, 5.149973918340071e-05,
                       8.0111851462794564e-05};
  for (int i = 0; i < 3; ++i) {
    EXPECT_DOUBLE_EQ(expected[i], rand_gen.RandExponential());
  }
}

// Tests for a few specific RandDouble() values.
// Relies on knowledge of implementation of RandExponential().
TEST(RandomBase, ExponentialSpecificValues) {
  // Smallest possible RandDouble():
  // -log1p(-0.0) = log(1.0) = 0.0
  random_test::RandDoubleSequence rand_gen_1({0.0});
  EXPECT_DOUBLE_EQ(0.0, rand_gen_1.RandExponential());

  // Sanity-check:
  // -log1p(-0.5) = log(2.0) ~~ 0.693
  random_test::RandDoubleSequence rand_gen_2({0.5});
  EXPECT_DOUBLE_EQ(0.69314718055994529, rand_gen_2.RandExponential());

  // Greatest possible RandDouble():
  // -log1p(-(1.0 - 2^-53)) = 53 * log(2) ~~ 36.7368
  random_test::RandDoubleSequence rand_gen_3({nextafter(1.0, 0.0)});
  EXPECT_DOUBLE_EQ(36.736800569677101, rand_gen_3.RandExponential());
}

TEST(RandomBase, Skewed) {
  random_test::VectorSequence rand_gen(common_seq);
  int expected[] = {0, 3, 8, 0};
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(rand_gen.Skewed(12), expected[i]);
  }
}

TEST(RandomBase, Skewed_Distribution) {
  for (int max_log = 1; max_log <= 32; ++max_log) {
    // Use a real RNG and check the distribution of L=floor(log2(Skewed()))
    // where we define log2(0) = -1. We don't have a simple formula for the
    // distribution of L, but we do know that for max_log > 0:
    //   Pr[L = -1] > Pr[L = 0] > Pr[L = 1] > ... > Pr[L = (max_log - 1)]
    //      = 1/(2 * (max_log + 1))
    //   Pr[L >= max_log] = 0.
    std::vector<int> buckets;
    buckets.resize(max_log + 1);
    MTRandom rand_gen(0);
    const int kSqrtItersPerBucket = 100;
    const int kItersPerBucket = kSqrtItersPerBucket * kSqrtItersPerBucket;
    for (int i = 0; i < buckets.size() * kItersPerBucket; ++i) {
      uint32_t v = rand_gen.Skewed(max_log);
      int logPlusOne = absl::bit_width(v);
      CHECK_LT(logPlusOne, buckets.size()) << "v: " << v;
      ++buckets[logPlusOne];
    }
    uint64_t last_count = std::numeric_limits<uint64_t>::max();
    const int kSlack = 5 * kSqrtItersPerBucket;  // 5 standard deviations
    for (int count : buckets) {
      EXPECT_LT(count, last_count);
      last_count = count + kSlack;
    }
    if (max_log > 0) {
      EXPECT_LT(buckets.back(), (kItersPerBucket / 2) + kSlack);
      EXPECT_GT(buckets.back(), (kItersPerBucket / 2) - kSlack);
    }
  }
}

TEST(RandomBase, RandString) {
  random_test::VectorSequence rand_gen(common_seq);

  EXPECT_EQ("", rand_gen.RandString(0));

  std::string randstring = rand_gen.RandString(8);
  EXPECT_EQ(randstring.length(), 8);
  EXPECT_EQ(std::string("NZ6'THl\0", 8), randstring);
}

TEST(RandomBase, Uniform) {
  random_test::VectorSequence rand_gen(common_seq);
  int32_t first = rand_gen.Rand32();

  // Test Uniform(0)
  rand_gen.Reset();
  int32_t zero = rand_gen.Uniform(0);
  EXPECT_EQ(zero, 0);

  // Test Uniform(value);
  // Uniform(n) returns first%n after Reset().
  rand_gen.Reset();
  int32_t uniform = rand_gen.Uniform(100);
  EXPECT_EQ(first % 100, uniform);

  // Test OneIn.
  // Uniform(n) returns first%n after Reset().
  rand_gen.Reset();
  EXPECT_TRUE(rand_gen.OneIn(first));

  rand_gen.Reset();
  EXPECT_FALSE(rand_gen.OneIn(first + 1));
}

TEST(RandomBase, UniformFloat) {
  random_test::VectorSequence rand_gen(common_seq);
  float first = rand_gen.RandFloat();
  // Because random_test::VectorSequence is deterministic,
  // we know the first value it returns will be non-zero.
  // The tests below rely on this fact in order to produce
  // meaningful results, so we CHECK it here.
  CHECK_GT(first, 0.0);
  CHECK_LT(first, 1.0);

  rand_gen.Reset();
  float uniform = rand_gen.UniformFloat(50.0);
  EXPECT_LE(fabs(uniform - (first * 50.0)), 0.0001);

  rand_gen.Reset();
  uniform = rand_gen.UniformFloat(-50.0);
  EXPECT_LE(fabs(uniform - (first * -50.0)), 0.0001);

  rand_gen.Reset();
  uniform = rand_gen.UniformFloat(10.0, 12.0);
  EXPECT_LE(fabs(uniform - (first * 2.0 + 10)), 0.0001);

  rand_gen.Reset();
  uniform = rand_gen.UniformFloat(12.0, 10.0);
  EXPECT_LE(fabs(uniform - (12.0 - first * 2.0)), 0.0001);

  rand_gen.Reset();
  uniform = rand_gen.UniformFloat(0.0);
  EXPECT_EQ(0.0, uniform);

  rand_gen.Reset();
  uniform = rand_gen.UniformFloat(0.0, 0.0);
  EXPECT_EQ(0.0, uniform);
}

TEST(RandomBase, UniformDouble) {
  random_test::VectorSequence rand_gen(common_seq);
  double first = rand_gen.RandDouble();
  // Because random_test::VectorSequence is deterministic, we know the first
  // value it returns will be non-zero. The tests below rely on this fact in
  // order to produce meaningful results, so we CHECK it here.
  CHECK_GT(first, 0.0);
  CHECK_LT(first, 1.0);

  rand_gen.Reset();
  double uniform = rand_gen.UniformDouble(50.0);
  EXPECT_LE(fabs(uniform - (first * 50.0)), 0.0001);

  rand_gen.Reset();
  uniform = rand_gen.UniformDouble(-50.0);
  EXPECT_LE(fabs(uniform - (first * -50.0)), 0.0001);

  rand_gen.Reset();
  uniform = rand_gen.UniformDouble(10.0, 12.0);
  EXPECT_LE(fabs(uniform - (first * 2.0 + 10)), 0.0001);

  rand_gen.Reset();
  uniform = rand_gen.UniformDouble(12.0, 10.0);
  EXPECT_LE(fabs(uniform - (12.0 - first * 2.0)), 0.0001);

  rand_gen.Reset();
  uniform = rand_gen.UniformDouble(0.0);
  EXPECT_EQ(0.0, uniform);

  rand_gen.Reset();
  uniform = rand_gen.UniformDouble(0.0, 0.0);
  EXPECT_EQ(0.0, uniform);
}

}  // namespace
