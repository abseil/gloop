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

#include "gloop/util/random/internal/yule_simon_distribution.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/random/internal/distribution_test_util.h"
#include "absl/random/internal/pcg_engine.h"
#include "absl/random/internal/sequence_urbg.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using ::util_random::yule_simon_distribution;

template <typename IntType>
class YuleSimonDistributionTypedTest : public ::testing::Test {};

using IntTypes = ::testing::Types<int, int8_t, int16_t, int32_t, int64_t,
                                  uint8_t, uint16_t, uint32_t, uint64_t>;
TYPED_TEST_SUITE(YuleSimonDistributionTypedTest, IntTypes);

TYPED_TEST(YuleSimonDistributionTypedTest, SerializeTest) {
  using param_type = typename yule_simon_distribution<TypeParam>::param_type;

  constexpr int kCount = 1000;
  std::mt19937_64 gen;
  for (const auto& param : {
           param_type(1.1),
           param_type(2.0),
           param_type(3.0),
           param_type(100.0),
           param_type(1e30),
       }) {
    // Validate parameters.
    const auto a = param.a();
    yule_simon_distribution<TypeParam> before(a);
    EXPECT_EQ(before.a(), param.a());

    {
      yule_simon_distribution<TypeParam> via_param(param);
      EXPECT_EQ(via_param, before);
    }

    // Validate stream serialization.
    std::stringstream ss;
    ss << before;
    yule_simon_distribution<TypeParam> after(2.345);

    EXPECT_NE(before.a(), after.a());
    EXPECT_NE(before.param(), after.param());
    EXPECT_NE(before, after);

    ss >> after;

    EXPECT_EQ(before.a(), after.a());
    EXPECT_EQ(before.param(), after.param());
    EXPECT_EQ(before, after);

    // Smoke test.
    auto sample_min = after.max();
    auto sample_max = after.min();
    for (int i = 0; i < kCount; i++) {
      auto sample = after(gen);
      EXPECT_GE(sample, after.min());
      EXPECT_LE(sample, after.max());
      if (sample > sample_max) sample_max = sample;
      if (sample < sample_min) sample_min = sample;
    }
    LOG(INFO) << "Range: " << sample_min << ", " << sample_max;
  }
}

class YuleSimonModel {
 public:
  explicit YuleSimonModel(double a) : a_(a) {
    const double p = a_ - 1;
    const double p1 = p - 1;
    const double p2 = p - 2;
    const double p3 = p - 3;
    const double pp = p * p;
    const double p1p1 = p1 * p1;
    mean_ = (p <= 1) ? 0 : (p / p1);
    variance_ = (p <= 2) ? 0 : (pp / (p1p1 * p2));
    skewness_ = (p <= 3) ? 0 : ((p1p1 * std::sqrt(p2)) / (p * p3));
    kurtosis_ =
        (p <= 4)
            ? 0
            : (p + 3 + ((11 * pp * p) - (49 * p) - 22) / ((p - 4) * p3 * p));
  }

  double mean() const { return mean_; }
  double variance() const { return variance_; }
  double stddev() const { return std::sqrt(variance()); }
  double skew() const { return skewness_; }
  double kurtosis() const { return kurtosis_; }

  // Returns the probability that any single invocation returns k.
  double PMF(size_t k) {
    return (a_ - 1) * absl::random_internal::beta(k + 1, a_);
  };
  double CDF(size_t k) {
    return 1.0 -
           static_cast<double>(k * absl::random_internal::beta(k + 1, a_));
  }

 private:
  const double a_;
  double mean_;
  double variance_;
  double skewness_;
  double kurtosis_;
};

using yule_u64 = yule_simon_distribution<uint64_t>;

class YuleSimonTest : public testing::TestWithParam<yule_u64::param_type>,
                      public YuleSimonModel {
 public:
  YuleSimonTest() : YuleSimonModel(GetParam().a()) {}

  // We use a fixed bit generator for distribution accuracy tests.  This allows
  // these tests to be deterministic, while still testing the qualify of the
  // implementation.
  absl::random_internal::pcg64_2018_engine rng_{0x2B7E151628AED2A6};
};

TEST_P(YuleSimonTest, TestMoments) {
  constexpr int kN = 1000000;
  std::vector<double> values(kN);
  const auto& param = GetParam();

  yule_u64 dist(param);
  for (int i = 0; i < kN; i++) {
    auto x = dist(rng_);
    ASSERT_LT(x, ~static_cast<uint64_t>(0));
    ASSERT_GE(x, 0);
    values[i] = 1.0 + static_cast<double>(x);
  }

  const auto moments =
      absl::random_internal::ComputeDistributionMoments(values);
  const double p = param.a() - 1;
  if (p > 1) {
    EXPECT_NEAR(mean(), moments.mean, 0.1);
  }
  if (p > 2) {
    EXPECT_NEAR(variance(), moments.variance, 1.1);
  }
  // Do not check Skewness and kurtosis as they converge slowly, causing
  // tests to either timeout for large numbers of iterations and flake for small
  // numbers of iterations.
}

TEST_P(YuleSimonTest, Probability) {
  const size_t trials = 1000000;
  const auto& param = GetParam();

  // Generate n variates and fill the counts vector with the count of their
  // occurrences.
  const int bucket_count = 2000;
  std::vector<int64_t> buckets(bucket_count, 0);
  {
    yule_u64 dis(param);
    for (size_t i = 0; i < trials; i++) {
      auto x = dis(rng_);
      if (x < bucket_count) {
        ++buckets[x];
      }
    }
  }

  // Treat the output as a non-uniform multinomial distribution with
  // probabilities constructed based on the pmf() function, above.  (This might
  // be improved by computing the parameters using a skew-normal distribution.)
  const double inv_sqrt_trials = 1.0 / std::sqrt(trials);
  for (size_t i = 0; i < buckets.size(); ++i) {
    const double p = PMF(i);
    if (p < 0.005) {
      // Skip validation once the probability drops very low.
      break;
    }
    const double stddev_p = inv_sqrt_trials * std::sqrt(p * (1.0 - p));
    const double expected_count = p * trials;
    const double stddev = stddev_p * trials;

    // 6 sigma, approved by Louis de Broglie
    EXPECT_NEAR(buckets[i], expected_count, 6 * stddev)
        << "[" << i << "] @" << p << ", "
        << std::abs(static_cast<double>(buckets[i]) - expected_count) / stddev
        << " stddev";
  }
}

std::vector<yule_u64::param_type> GenParams() {
  return std::vector<yule_u64::param_type>{
      yule_u64::param_type{1.5}, yule_u64::param_type{2.0},
      yule_u64::param_type{5.0}, yule_u64::param_type{10.0}};
}

std::string ParamName(
    const ::testing::TestParamInfo<yule_u64::param_type>& info) {
  std::string name = absl::StrCat("a_", absl::SixDigits(info.param.a()));
  return absl::StrReplaceAll(name, {{"+", "_"}, {"-", "_"}, {".", "_"}});
}

INSTANTIATE_TEST_SUITE_P(All, YuleSimonTest, ::testing::ValuesIn(GenParams()),
                         ParamName);

// NOTE: yule_simon_distribution is not guaranteed to be stable.
TEST(YuleSimonDistributionTest, StabilityTest) {
  using testing::ElementsAre;
  // yule_simon_distribution stability relies on
  // absl::uniform_real_distribution, std::log, std::exp, std::log1p
  absl::random_internal::sequence_urbg urbg(
      {0x0003eb76f6f7f755ull, 0xFFCEA50FDB2F953Bull, 0xC332DDEFBE6C5AA5ull,
       0x6558218568AB9702ull, 0x2AEF7DAD5B6E2F84ull, 0x1521B62829076170ull,
       0xECDD4775619F1510ull, 0x13CCA830EB61BD96ull, 0x0334FE1EAA0363CFull,
       0xB5735C904C70A239ull, 0xD59E9E0BCBAADE14ull, 0xEECC86BC60622CA7ull});

  std::vector<int> output(6);

  {
    yule_simon_distribution<int32_t> dist;
    std::generate(std::begin(output), std::end(output),
                  [&] { return dist(urbg); });
    EXPECT_THAT(output, ElementsAre(1, 0, 20, 0, 3, 0));
  }
  urbg.reset();
  {
    yule_simon_distribution<int32_t> dist(3.3);
    std::generate(std::begin(output), std::end(output),
                  [&] { return dist(urbg); });
    EXPECT_THAT(output, ElementsAre(1, 0, 4, 0, 2, 0));
  }
}

TEST(YuleSimonDistributionTest, AlgorithmBounds) {
  yule_simon_distribution<int32_t> dist;

  // NOTE: yule_simon_distribution uses rejection sampling, so it is necessary
  // to add some values at the end of the urbg to avoid allowing it to get stuck
  // in a loop.
  constexpr uint64_t kA = 0xECDD4775619F1510ull;
  constexpr uint64_t kB = 0x2AEF7DAD5B6E2F84ull;
  constexpr uint64_t kC = 0x0334FE1EAA0363CFull;

  // 0-result cases.
  {
    // values near {1.0, ~1.0}
    absl::random_internal::sequence_urbg urbg(
        {0xffffffffffffffefull, 0xffffffffffffffefull, kA, kB, kC});
    EXPECT_EQ(0, dist(urbg));
  }
  {
    // some values near {0.5-epsilon, 0.5+epsilon}
    absl::random_internal::sequence_urbg urbg(
        {0x7fffffffffffffefull, 0x800000000000ffeeull, kA, kB, kC});
    EXPECT_EQ(0, dist(urbg));
  }
  {
    // some values near {1e-16, ~1.0}
    absl::random_internal::sequence_urbg urbg(
        {0xfffull, 0xffffffffffffffefull, kA, kB, kC});
    EXPECT_EQ(0, dist(urbg));
  }

  // 1-result cases
  {
    // some values near {1e-20, ~1.0}
    absl::random_internal::sequence_urbg urbg(
        {0x1ull, 0xffffffffffff0002ull, kA, kB, kC});
    EXPECT_EQ(1, dist(urbg));
  }
  {
    // values near {0.5-epsilon, 0.5+epsilon}
    absl::random_internal::sequence_urbg urbg(
        {0x7fffffffffffffefull, 0x80000000000000eeull, kA, kB, kC});
    EXPECT_EQ(1, dist(urbg));
  }

  // Increasing as {v0 => 0...0.5, v1 ~0.5}
  {
    absl::random_internal::sequence_urbg urbg(
        {0x1ull, 0x7fffffffffffffefull, kA, kB, kC});
    EXPECT_EQ(0x40, dist(urbg));
  }
  {
    absl::random_internal::sequence_urbg urbg(
        {0xfull, 0x7fffffffffffffefull, kA, kB, kC});
    EXPECT_EQ(0x3c, dist(urbg));
  }
  {
    absl::random_internal::sequence_urbg urbg(
        {0xffffull, 0x7fffffffffffffefull, kA, kB, kC});
    EXPECT_EQ(0x30, dist(urbg));
  }

  // Increasing as {~1.0, v1 => .. 0}.
  {
    absl::random_internal::sequence_urbg urbg(
        {0xffffffffffffffefull, 0x4ull, kA, kB, kC});
    EXPECT_EQ(0x200, dist(urbg));
  }
  {
    absl::random_internal::sequence_urbg urbg(
        {0xffffffffffffffefull, 0x2ull, kA, kB, kC});
    EXPECT_EQ(0x400, dist(urbg));
  }
  {
    absl::random_internal::sequence_urbg urbg(
        {0xffffffffffffffefull, 0x1ull, kA, kB, kC});
    EXPECT_EQ(0x800, dist(urbg));
  }
}

// std::geometric_distribution is similar to the yule-simon/zipf
// distributions. The algorithm for the geometric_distribution is,
// basically, floor(log(1-X) / log(1-p))
//
// The algorithm for the yule_simon_distribution is, basically,
// floor(log(X) / log1p(exp(log(Y)/a)))

template <typename Engine, typename T>
void BM_YuleSimon(benchmark::State& state) {
  volatile double a = 3.0;

  Engine rng;
  yule_simon_distribution<T> dis(a);
  for (auto _ : state) {
    benchmark::DoNotOptimize(dis(rng));
  }
  state.SetBytesProcessed(
      sizeof(typename yule_simon_distribution<T>::result_type) *
      state.iterations());
}

BENCHMARK_TEMPLATE(BM_YuleSimon, absl::InsecureBitGen, int32_t);
BENCHMARK_TEMPLATE(BM_YuleSimon, absl::BitGen, int32_t);
BENCHMARK_TEMPLATE(BM_YuleSimon, absl::InsecureBitGen, int64_t);
BENCHMARK_TEMPLATE(BM_YuleSimon, absl::BitGen, int64_t);

}  // namespace
