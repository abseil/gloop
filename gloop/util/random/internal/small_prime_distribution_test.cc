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

#include "gloop/util/random/internal/small_prime_distribution.h"

#include <cstddef>
#include <cstdint>
#include <random>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/random/random.h"
#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using ::util_random::small_prime_distribution;
using ::util_random::small_prime_distribution_internal::IsPrime;

template <typename IntType>
class SmallPrimeDistributionTypedTest : public ::testing::Test {};

using IntTypes = ::testing::Types<int, int8_t, int16_t, int32_t, int64_t,
                                  uint8_t, uint16_t, uint32_t, uint64_t>;
TYPED_TEST_SUITE(SmallPrimeDistributionTypedTest, IntTypes);

TYPED_TEST(SmallPrimeDistributionTypedTest, SerializeTest) {
  using param_type = typename small_prime_distribution<TypeParam>::param_type;

  constexpr int kCount = 100;
  std::mt19937_64 gen;
  for (const auto& param : {
           param_type(3, 10),
           param_type(4, 50),
           param_type(2, 127),
       }) {
    // Validate parameters.
    const auto min_val = param.min();
    const auto max_val = param.max();
    small_prime_distribution<TypeParam> before(min_val, max_val);
    EXPECT_EQ(before.min(), param.min());
    EXPECT_EQ(before.max(), param.max());

    {
      small_prime_distribution<TypeParam> via_param(param);
      EXPECT_EQ(via_param, before);
    }

    // Validate stream serialization.
    std::stringstream ss;
    ss << before;
    small_prime_distribution<TypeParam> after(3, 5);

    EXPECT_NE(before.max(), after.max());
    EXPECT_NE(before.param(), after.param());
    EXPECT_NE(before, after);

    ss >> after;

    EXPECT_EQ(before.min(), after.min());
    EXPECT_EQ(before.max(), after.max());
    EXPECT_EQ(before.param(), after.param());
    EXPECT_EQ(before, after);

    // Smoke test.
    for (int i = 0; i < kCount; i++) {
      auto sample = after(gen);
      EXPECT_GE(sample, after.min());
      EXPECT_LE(sample, after.max());
      EXPECT_TRUE(IsPrime(sample)) << sample;
    }
  }
}

TEST(SmallPrimeDistributionTest, EdgeCases) {
  absl::InsecureBitGen gen;

  EXPECT_EQ(small_prime_distribution<int>(14, 17)(gen), 17);
  EXPECT_EQ(small_prime_distribution<int>(3, 4)(gen), 3);
  EXPECT_EQ(small_prime_distribution<int>(4, 6)(gen), 5);
}

TEST(SmallPrimeDistributionTest, KnownPrimesList) {
  absl::InsecureBitGen gen;

  // Explicit list of primes under 100 (excluding 2).
  const std::vector<int> kPrimesUnder100 = {3,  5,  7,  11, 13, 17, 19, 23,
                                            29, 31, 37, 41, 43, 47, 53, 59,
                                            61, 67, 71, 73, 79, 83, 89, 97};

  small_prime_distribution<int> dist(3, 100);
  for (int i = 0; i < 1000; ++i) {
    int sample = dist(gen);
    EXPECT_TRUE(absl::c_binary_search(kPrimesUnder100, sample))
        << "Generated non-prime or out-of-range: " << sample;
  }
}

TEST(SmallPrimeDistributionTest, ShuffleEquivalent) {
  // Demonstration of how to use small_prime_distribution to create a
  // proper shuffled iteration over a container.
  auto MyShuffle = [](auto& bitgen, const auto& container) {
    using T = std::remove_cvref_t<decltype(container[0])>;
    size_t n = container.size();

    // Pick i and x randomly, where x is prime.
    size_t i = absl::Uniform(bitgen, 0u, n);
    size_t x = util_random::small_prime_distribution<size_t>(n + 1)(bitgen);
    std::vector<T> result;
    result.reserve(n);
    for (size_t j = 0; j < container.size(); ++j) {
      result.push_back(container[i]);
      i = (i + x) % container.size();
    }
    return result;
  };

  std::vector<int> v = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

  // Verify that multiple invocations produce a permutation of the input.
  absl::InsecureBitGen gen;
  for (int i = 0; i < 3; ++i) {
    EXPECT_THAT(MyShuffle(gen, v),
                testing::UnorderedElementsAre(1, 2, 3, 4, 5, 6, 7, 8, 9, 10));
  }
}

TEST(SmallPrimeDistributionTest, IsPrime) {
  // Verify small numbers and edge cases
  EXPECT_FALSE(IsPrime(0));
  EXPECT_FALSE(IsPrime(1));
  EXPECT_TRUE(IsPrime(2));
  EXPECT_TRUE(IsPrime(3));
  EXPECT_FALSE(IsPrime(4));
  EXPECT_TRUE(IsPrime(5));

  // Verify larger known primes and composites
  EXPECT_TRUE(IsPrime(1000000007));
  EXPECT_FALSE(IsPrime(1000000005));
  EXPECT_TRUE(IsPrime(2147483647));  // 2^31-1
  EXPECT_FALSE(IsPrime(2147483645));

  EXPECT_TRUE(IsPrime(18446744073709551557ull));  // 2^61-1
  EXPECT_FALSE(IsPrime(18446744073709551555ull));
}

// List of known gaps between primes filtered so that only the smallest prime
// with the indicated gap is included.
//
// https://warwick.ac.uk/fac/sci/maths/people/staff/visser/large_gaps_between_primes.pdf
// https://web.archive.org/web/20191118035255/http://www.trnicely.net/gaps/gaplist.html
constexpr std::pair<int, uint64_t> kGapFollowingPrime[] = {
    // 1551 is too large for a 64-bit value.
    {1550, 18361375334787046697ull},
    {1530, 17678654157568189057ull},
    {1526, 15570628755536096243ull},
    {1510, 6787988999657777797ull},
    {1488, 5733241593241196731ull},
    {1476, 1425172824437699411ull},
    {1442, 804212830686677669ull},
    {1370, 418032645936712127ull},
    {1356, 401429925999153707ull},
    {1328, 352521223451364323ull},
    {1272, 305405826521087869ull},
    {1248, 218034721194214273ull},
    {1224, 203986478517455989ull},
    {1220, 80873624627234849ull},
    {1198, 55350776431903243ull},
    {1184, 43841547845541059ull},
    {1132, 1693182318746371ull},
    {924, 1686994940955803ull},
    {916, 1189459969825483ull},
    {906, 218209405436543ull},
    {806, 171231342420521ull},
    {804, 90874329411493ull},
    {778, 42842283925351ull},
    {766, 19581334192423ull},
    {716, 13829048559701ull},
    {674, 7177162611713ull},
    {652, 2614941710599ull},
    {602, 1968188556461ull},
    {588, 1408695493609ull},
    {582, 1346294310749ull},
    {540, 738832927927ull},
    {534, 614487453523ull},
    {532, 461690510011ull},
    {516, 416608695821ull},
    {514, 304599508537ull},
    {500, 303371455241ull},
    {490, 297501075799ull},
    {486, 241160624143ull},
    {474, 182226896239ull},
    {468, 127976334671ull},
    {464, 42652618343ull},
    {456, 25056082087ull},
    {394, 22367084959ull},
    {384, 20678048297ull},
    {382, 10726904659ull},
    {354, 4302407359ull},
    /* 32-bit below here */
    {336, 3842610773ull},
    {320, 2300942549ull},
    {292, 1453168141ull},
    {288, 1294268491ull},
    {282, 436273009ull},
    {250, 387096133ull},
    {248, 191912783ull},
    {234, 189695659ull},
    {222, 122164747ull},
    {220, 47326693ull},
    {210, 20831323ull},
    {180, 17051707ull},
    {154, 4652353ull},
    {148, 2010733ull},
    {132, 1357201ull},
    {118, 1349533ull},
    {114, 492113ull},
    {112, 370261ull},
    {96, 360653ull},
    {86, 155921ull},
    /* 16-bit below here */
    {72, 31397ull},
    {52, 19609ull},
    {44, 15683ull},
    {36, 9551ull},
    {34, 1327ull},
    {22, 1129ull},
    {20, 887ull},
    {18, 523ull},
    {14, 113ull},
    {8, 89ull},
    {6, 23ull},
    {4, 7ull},
    {2, 3ull},
    {1, 2ull},
};

TEST(SmallPrimeDistributionTest, FindNextPrime) {
  using ::util_random::small_prime_distribution_internal::FindNextPrime;

  for (const auto& [gap, prime] : kGapFollowingPrime) {
    EXPECT_TRUE(IsPrime(prime));
    EXPECT_TRUE(IsPrime(prime + gap));
    EXPECT_THAT(FindNextPrime(prime + 1, prime + gap), prime + gap)
        << "gap: " << gap << " prime: " << prime;
  }
}

TEST(SmallPrimeDistributionTest, NoPrimesInRangeTerminates) {
  absl::InsecureBitGen gen;
  // [14, 16] holds only {14, 15, 16}, none of which are prime.
  EXPECT_DEATH(small_prime_distribution<int>(14, 16)(gen), "No primes");

  // Maximal gap in 32-bit range is 336.
  EXPECT_DEATH(
      small_prime_distribution<uint32_t>(3842610773 + 1, 3842610773 + 335)(gen),
      "No primes");

  // Maximal gap in 64-bit range is 1550.
  EXPECT_DEATH(
      small_prime_distribution<uint64_t>(18361375334787046697ull + 1,
                                         18361375334787046697ull + 1549)(gen),
      "No primes");
}

template <typename Engine, typename T>
void BM_SmallPrime(benchmark::State& state) {
  volatile T kMin = 3;
  volatile T kMax = 10000;

  Engine rng;
  small_prime_distribution<T> dis(kMin, kMax);
  for (auto _ : state) {
    benchmark::DoNotOptimize(dis(rng));
  }
  state.SetBytesProcessed(
      sizeof(typename small_prime_distribution<T>::result_type) *
      state.iterations());
}

BENCHMARK_TEMPLATE(BM_SmallPrime, absl::InsecureBitGen, int32_t);
BENCHMARK_TEMPLATE(BM_SmallPrime, absl::InsecureBitGen, int64_t);

}  // namespace
