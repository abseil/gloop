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

// This small piece of code tests acm random. It provides some simple
// checking plus a collision test
//
#include "gloop/util/random/acmrandom.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <string>

#include "absl/container/fixed_array.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "benchmark/benchmark.h"
#include "fuzztest/fuzztest.h"
#include "gloop/util/math/mathutil.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

ABSL_FLAG(int32_t, num_urns, 1000, "Number of urns for collision test");

namespace {

using ::testing::AllOf;
using ::testing::Each;
using ::testing::Gt;
using ::testing::Lt;

// We asume that we that after 300000 experiments we are within 1% of
// steady state (i.e. the average of the numbers generated so far are
// withing 1% of the average steady state average of 2^30)
constexpr int kSteadyStateReached = 300000;
constexpr float kSteadyStateWithin = 0.01f;

// Standard constants for ACMRandom
constexpr int M = 0x7FFFFFFF;
constexpr int A = 16807;

// Seed values -1, 0 and M must be collapsed as seed value 1.
TEST(ACMRandomTest, TestSeedSpecialCases) {
  ACMRandom rndMinus1(-1);
  ACMRandom rnd0(0);
  ACMRandom rnd1(1);
  ACMRandom rndM(M);
  constexpr int32_t kExpected = 1 * A % M;
  EXPECT_EQ(rndMinus1.Next(), kExpected);
  EXPECT_EQ(rnd0.Next(), kExpected);
  EXPECT_EQ(rnd1.Next(), kExpected);
  EXPECT_EQ(rndM.Next(), kExpected);
}

// Test expected 32bit randoms for the default seed.
TEST(ACMRandomTest, TestExpected32) {
  constexpr int M = 0x7FFFFFFF;
  constexpr int A = 16807;
  absl::PrintF("Testing expected randoms...\n");
  ACMRandom rnd(301);
  int64_t r = 301;
  for (int i = 0; i < 1000; i++) {
    SCOPED_TRACE(absl::StrCat("iteration=", i));
    // This is what the random number generator is doing in effect
    r = r * A % M;
    EXPECT_EQ(rnd.Next(), r);
  }
}

// Test expected 64bit randoms for the default seed
TEST(ACMRandomTest, TestExpected64) {
  absl::PrintF("Testing expected 64 bit randoms...\n");
  ACMRandom rnd(301);
  ACMRandom ref_rnd(301);
  constexpr int64_t expected[] = {
      0x002698ad4b48ead0ull, 0x1bfb1e0316f2d5deull, 0x173a623c9725b477ull,
      0x0a447a02823ad868ull, 0x1df74948b3fbea7eull, 0x1bc8b594bcf01a39ull,
      0x07b767ca9520e99aull, 0x05e28b4320bfd20eull, 0x0105906a24823f57ull,
      0x1a1e7d14a6d24384ull, 0x2a7326df322e084dull, 0x120bc9cc3fac4ec7ull,
      0x2c8f193a1b46a9c5ull, 0x2b9c95743bbe3f90ull, 0x0dcfc5b1d0398b46ull,
      0x006ba47b3448bea3ull, 0x3fe4fbf9a522891bull, 0x23e1a50ad6aebca3ull,
      0x1b263d39ea62be44ull, 0x13581d282e643b0eull};
  for (int i = 0; i < 1000; i++) {
    SCOPED_TRACE(absl::StrCat("iteration=", i));
    // This is just repeating the Next64() implementation, and checking it
    // against a table of expected values.
    int64_t ref_value = ref_rnd.Next();
    ref_value = (ref_value - 1) * (M - 1) + ref_rnd.Next();
    if (i < std::size(expected)) {
      EXPECT_EQ(ref_value, expected[i]);
    }
    EXPECT_EQ(rnd.Next64(), ref_value);
  }
}

TEST(ACMRandomTest, TestUnbiasedUniform) {
  // Test UnbiasedUniform
  ACMRandom prng(691965);

  constexpr int32_t range = 3 * (1L << 29);
  constexpr int32_t thd = 1L << 30;

  size_t countubu = 0;
  for (int i = 0; i < 100000; ++i) {
    int32_t rnd = prng.UnbiasedUniform(range);
    if (rnd < thd) {
      ++countubu;
    }
  }

  EXPECT_NEAR(static_cast<double>(countubu) / 100000.0,
              static_cast<double>(thd) / static_cast<double>(range), 0.005);
}

TEST(ACMRandomTest, UnbiasedUniform64InfiniteLoop) {
  constexpr int32_t seed = 0x37f63c08;
  constexpr uint64_t bound = uint64_t{0xbffffbb2b62a339};
  ACMRandom rnd(seed);

  // All we care about here is that the algorithm terminates.
  EXPECT_NE(rnd.UnbiasedUniform64(bound), 0);
}

// Functionality tests
TEST(ACMRandomTest, TestFunctionality) {
  absl::PrintF("Testing functionality...\n");
  ACMRandom rnd(GTEST_FLAG_GET(random_seed));

  // Check our initializer seed.

  // Get the first Random
  int32_t next = rnd.Next();

  // Test Uniform
  rnd.Reset(GTEST_FLAG_GET(random_seed));
  EXPECT_EQ(rnd.Uniform(0), 0);

  rnd.Reset(GTEST_FLAG_GET(random_seed));
  // Uniform(n) returns next%n after Reset().
  EXPECT_EQ(rnd.Uniform(10000), next % 10000) << " next: " << next;

  // Test RndFloat
  rnd.Reset(GTEST_FLAG_GET(random_seed));
  float rnd_float = rnd.RndFloat();
  float rnd_cmp = static_cast<float>(next) / static_cast<float>(0x80000000);
  EXPECT_TRUE(MathUtil::AlmostEquals<float>(rnd_float, rnd_cmp));

  // Test OneIn.
  // Uniform(n) returns next%n after Reset().
  rnd.Reset(GTEST_FLAG_GET(random_seed));
  EXPECT_TRUE(rnd.OneIn(next)) << " next: " << next;

  rnd.Reset(GTEST_FLAG_GET(random_seed));
  EXPECT_FALSE(rnd.OneIn(next + 1)) << " next: " << next;
}

// Since the the actual random number generator is well tested in the
// literature, out tests cannot reveal much, so we do just a simple
// collision test
// Simple collision test - distributes the numbers in urns and
// computes the distribution at the end
TEST(ACMRandomTest, TestCollisions) {
  absl::PrintF("Testing collisions...\n");
  ACMRandom rnd(GTEST_FLAG_GET(random_seed));
  int32_t num_urns = absl::GetFlag(FLAGS_num_urns);
  int32_t num_tests = absl::GetFlag(FLAGS_num_urns) * kSteadyStateReached;

  absl::FixedArray<int, 0> urns(num_urns);
  memset(urns.data(), 0, num_urns * sizeof(int));
  for (int i = 0; i < num_tests; i++) {
    urns[rnd.Uniform(num_urns)]++;
  }

  const float expected =
      static_cast<float>(num_tests) / static_cast<float>(num_urns);
  EXPECT_THAT(urns, Each(AllOf(Gt(expected * (1.0f - kSteadyStateWithin)),
                               Lt(expected * (1.0f + kSteadyStateWithin)))));
}

// Check that RndFloat in fact returns floats in the range (0, 1).
TEST(ACMRandomTest, TestRndFloatRange) {
  constexpr int one_is_next = 1407677000L;  // rnd.Next() == 1
  ACMRandom rnd(one_is_next);
  EXPECT_EQ(rnd.Next(), 1);
  rnd.Reset(one_is_next);
  float low_value = rnd.RndFloat();
  EXPECT_THAT(low_value, AllOf(Gt(0.0f), Lt(1.0f)));

  constexpr int negative_one_is_next = 739806647L;  // rnd.Next() == M - 1
  rnd.Reset(negative_one_is_next);
  EXPECT_EQ(rnd.Next(), 0x7FFFFFFE);  // 0x7FFFFFFE = M - 1 = 2^31 - 2
  rnd.Reset(negative_one_is_next);
  float high_value = rnd.RndFloat();
  EXPECT_GT(low_value, 0.0f);
  EXPECT_THAT(high_value, AllOf(Gt(0.0f), Lt(1.0f)));

#ifndef GLOOP_UNSUPPORTED_LIBSTDCXX  // Missing std::make_unsigned<double>
  EXPECT_NEAR(high_value - low_value, 1.0f,
              1e-7f);  // Within float precision.
#endif                 // GLOOP_UNSUPPORTED_LIBSTDCXX
}

TEST(ACMRandomTest, TestExpectedValues) {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  for (int i = 0; i < 1000; i++) {
    // ACMRandom needs warming up
    rnd.Next();
  }

  EXPECT_EQ(rnd.Rand8(), static_cast<uint8_t>('\x80'));
  EXPECT_EQ(rnd.Rand16(), 23206u);
  EXPECT_EQ(rnd.Rand32(), 2010298588ul);
  EXPECT_EQ(rnd.Rand64(), uint64_t{1563726878470379363});
  EXPECT_NEAR(rnd.RandFloat(), 0.688814, 0.000001);
  EXPECT_NEAR(rnd.RandDouble(), 0.165865, 0.000001);
  EXPECT_EQ(rnd.UnbiasedUniform(1000000), 673287);
  EXPECT_EQ(rnd.UnbiasedUniform64(1000000), 7754);
  EXPECT_EQ(rnd.Uniform(1000000), 905178);
  EXPECT_EQ(rnd.RandString(3), std::string("\x86\xa8"
                                           "5"));
}

// Microbenchmark of the seed update method.
void BM_ACMRandomNext(benchmark::State& state) {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  uint32_t r = 0;
  for (auto s : state) {
    r += rnd.Next();
  }
  VLOG(2) << r;
}
BENCHMARK(BM_ACMRandomNext);

void BM_ACMRandomUniform(benchmark::State& state) {
  const int arg = state.range(0);

  ACMRandom rnd(ACMRandom::DeterministicSeed());
  uint32_t r = 0;
  for (auto s : state) {
    r += rnd.Uniform(arg);
  }
  VLOG(2) << r;
}
BENCHMARK(BM_ACMRandomUniform)->Arg(24000);

void BM_ACMRandDouble(benchmark::State& state) {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  double r = 0;
  for (auto s : state) {
    r += rnd.RandDouble();
  }
  VLOG(2) << r;
}
BENCHMARK(BM_ACMRandDouble);

void BM_ACMHostnamePidTimeSeed(benchmark::State& state) {
  double r = 0;
  for (auto s : state) {
    r += ACMRandom::HostnamePidTimeSeed();
  }
  VLOG(2) << r;
}
BENCHMARK(BM_ACMHostnamePidTimeSeed);

// ---------------------------------------------------------------------------
// Fuzz tests for ACMRandom.
// ---------------------------------------------------------------------------

void ACMRandomNextIsInRange(int32_t seed) {
  ACMRandom rng(seed);
  for (int i = 0; i < 100; ++i) {
    int32_t val = rng.Next();
    EXPECT_GE(val, 1);
    EXPECT_LE(val, 2147483646);  // M - 1 = 2^31 - 2
  }
}
FUZZ_TEST(RandomFuzzTest, ACMRandomNextIsInRange);

void ACMRandomUnbiasedUniformIsInRange(int32_t seed, int32_t n) {
  ACMRandom rng(seed);
  for (int i = 0; i < 50; ++i) {
    int32_t val = rng.UnbiasedUniform(n);
    EXPECT_GE(val, 0);
    EXPECT_LT(val, n);
  }
}
FUZZ_TEST(RandomFuzzTest, ACMRandomUnbiasedUniformIsInRange)
    .WithDomains(fuzztest::Arbitrary<int32_t>(),
                 fuzztest::InRange(1, 2147483645));

void ACMRandomRand32NeverCrashes(int32_t seed) {
  ACMRandom rng(seed);
  for (int i = 0; i < 100; ++i) {
    rng.Rand32();
  }
}
FUZZ_TEST(RandomFuzzTest, ACMRandomRand32NeverCrashes);

}  // namespace
