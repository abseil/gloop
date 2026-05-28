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

#include "gloop/util/math/kendalls-tau.h"

#include <float.h>

#include <functional>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/strings/str_join.h"
#include "absl/types/span.h"
#include "benchmark/benchmark.h"
#include "gtest/gtest.h"

namespace {

// Sorts `x` and `y` by `x`.
void ZipSort(absl::Span<double> x, absl::Span<double> y,
             KendallsTau::InputOrdering ordering) {
  using XY = std::pair<double, double>;
  std::function<bool(XY, XY)> cmp;
  switch (ordering) {
    case KendallsTau::InputOrdering::kUnsorted:
      return;
    case KendallsTau::InputOrdering::kSortedByX:
      cmp = [](const XY& a, const XY& b) { return a.first < b.first; };
      break;
    case KendallsTau::InputOrdering::kSortedByXThenY:
      cmp = [](const XY& a, const XY& b) { return a < b; };
      break;
  }
  std::vector<std::pair<double, double>> zipped;
  zipped.reserve(x.size());
  for (int i = 0; i < x.size(); ++i) {
    zipped.push_back(std::make_pair(x[i], y[i]));
  }
  absl::c_stable_sort(zipped, cmp);
  for (int i = 0; i < x.size(); ++i) {
    x[i] = zipped[i].first;
    y[i] = zipped[i].second;
  }
}

struct TestParams {
  KendallsTau::Algorithm algorithm;
  KendallsTau::InputOrdering ordering;
};

class KendallsTauAlgorithmTest : public ::testing::TestWithParam<TestParams> {
 protected:
  KendallsTau TestMake(absl::Span<double> x, absl::Span<double> y) {
    ZipSort(x, y, GetParam().ordering);
    return KendallsTau::Make(x, y, GetParam().algorithm, GetParam().ordering);
  }
};

TEST(KendallsTauTest, ConcordantAndDiscordantZero) {
  // This test verifies that Kendall's tau is 0 when both concordant = 0 and
  // discordant = 0.
  KendallsTau kendalls_tau(0, 0, 0, 0, 0, 0, 0);
  EXPECT_EQ(0.0, kendalls_tau.GetCorrelationEstimate());
}

TEST_P(KendallsTauAlgorithmTest, CalculatesCorrelationFromVector) {
  // This test verifies KendallsTau::GetCorrelationEstimate correctness when
  // instantiated with vectors.
  std::vector<double> x = {12, 14, 14, 17, 19, 19, 19, 19, 19, 20, 21, 21,
                           21, 21, 21, 22, 23, 24, 24, 24, 26, 26, 27, 27};
  std::vector<double> y = {11, 4., 4., 2., 0., 0., 0., 0., 0., 0., 4., 0.,
                           4., 0., 0., 0., 0., 4., 0., 0., 0., 0., 1., 0.};
  KendallsTau tau = TestMake(absl::MakeSpan(x), absl::MakeSpan(y));
  EXPECT_NEAR(-0.28788315742461751, tau.GetCorrelationEstimate(), DBL_EPSILON);
}

TEST_P(KendallsTauAlgorithmTest, CalculatesCorrelationFromSubVectors) {
  // This test verifies KendallsTau::GetCorrelationEstimate correctness when
  // instantiated with a subset of vectors.
  std::vector<double> x = {100, 12, 14, 14, 17, 19, 19, 19, 19, 19, 20, 21, 21,
                           21,  21, 21, 22, 23, 24, 24, 24, 26, 26, 27, -1.};
  std::vector<double> y = {-4., 11, 4., 4., 2., 0., 0., 0., 0., 0., 0., 4., 0.,
                           4.,  0., 0., 0., 0., 4., 0., 0., 0., 0., 0., 100};
  KendallsTau tau = TestMake(absl::MakeSpan(&x[1], x.size() - 2),
                             absl::MakeSpan(&y[1], y.size() - 2));
  EXPECT_NEAR(-0.37620154104750974655, tau.GetCorrelationEstimate(),
              DBL_EPSILON);
}

TEST_P(KendallsTauAlgorithmTest, HandlesEmptyVectors) {
  // This test verifies that KendallsTau::Make handles empty vectors.
  std::vector<double> x;
  std::vector<double> y;
  KendallsTau tau = TestMake(absl::MakeSpan(x), absl::MakeSpan(y));
  EXPECT_EQ(0.0, tau.GetCorrelationEstimate());
}

TEST_P(KendallsTauAlgorithmTest, HandlesSizeOneVectors) {
  // This test verifies that KendallsTau::Make handles size one vectors.
  std::vector<double> x = {10};
  std::vector<double> y = {20};
  KendallsTau tau = TestMake(absl::MakeSpan(x), absl::MakeSpan(y));
  EXPECT_EQ(0.0, tau.GetCorrelationEstimate());
}

TEST_P(KendallsTauAlgorithmTest, ExtraX) {
  std::vector<double> x = {0.0, 5.0, 5.0};
  std::vector<double> y = {5.0, 1.0, 5.0};
  KendallsTau tau = TestMake(absl::MakeSpan(x), absl::MakeSpan(y));
  EXPECT_EQ(tau, KendallsTau(0, 1, 0, 1, 1, 0, 0));
}

INSTANTIATE_TEST_SUITE_P(
    Algorithms, KendallsTauAlgorithmTest,
    testing::Values(TestParams{KendallsTau::Algorithm::kBruteForce,
                               KendallsTau::InputOrdering::kUnsorted},
                    TestParams{KendallsTau::Algorithm::kBruteForce,
                               KendallsTau::InputOrdering::kSortedByX},
                    TestParams{KendallsTau::Algorithm::kBruteForce,
                               KendallsTau::InputOrdering::kSortedByXThenY},
                    TestParams{KendallsTau::Algorithm::kKnight,
                               KendallsTau::InputOrdering::kUnsorted},
                    TestParams{KendallsTau::Algorithm::kKnight,
                               KendallsTau::InputOrdering::kSortedByX},
                    TestParams{KendallsTau::Algorithm::kKnight,
                               KendallsTau::InputOrdering::kSortedByXThenY}));

class KendallsTauIncrementalTest
    : public ::testing::TestWithParam<KendallsTau::InputOrdering> {
 protected:
  KendallsTau TestMake(absl::Span<double> x, absl::Span<double> y) {
    ZipSort(x, y, GetParam());
    return KendallsTau::Make(x, y, KendallsTau::Algorithm::kBruteForce,
                             GetParam());
  }
};

TEST_P(KendallsTauIncrementalTest, DropFront) {
  std::vector<double> x_values = {0.0, 5.0, 5.0, 3.0};
  std::vector<double> y_values = {5.0, 1.0, 5.0, 2.0};
  absl::Span<double> x = absl::MakeSpan(x_values);
  absl::Span<double> y = absl::MakeSpan(y_values);

  EXPECT_EQ(TestMake(x, y).DropFront(x, y, 0, GetParam()),
            TestMake(x.subspan(0), y.subspan(0)));

  EXPECT_EQ(TestMake(x, y).DropFront(x, y, 1, GetParam()),
            TestMake(x.subspan(1), y.subspan(1)));

  EXPECT_EQ(TestMake(x, y).DropFront(x, y, 2, GetParam()),
            TestMake(x.subspan(2), y.subspan(2)));

  EXPECT_EQ(TestMake(x, y).DropFront(x, y, 3, GetParam()),
            TestMake(x.subspan(3), y.subspan(3)));
}

TEST_P(KendallsTauIncrementalTest, DropBack) {
  std::vector<double> x_values = {0.0, 5.0, 5.0, 3.0};
  std::vector<double> y_values = {5.0, 1.0, 5.0, 2.0};
  absl::Span<double> x = absl::MakeSpan(x_values);
  absl::Span<double> y = absl::MakeSpan(y_values);

  EXPECT_EQ(TestMake(x, y).DropBack(x, y, 0, GetParam()),
            TestMake(x.subspan(0, x.size() - 0), y.subspan(0, x.size() - 0)));

  EXPECT_EQ(TestMake(x, y).DropBack(x, y, 1, GetParam()),
            TestMake(x.subspan(0, x.size() - 1), y.subspan(0, x.size() - 1)));

  EXPECT_EQ(TestMake(x, y).DropBack(x, y, 2, GetParam()),
            TestMake(x.subspan(0, x.size() - 2), y.subspan(0, x.size() - 2)));

  EXPECT_EQ(TestMake(x, y).DropBack(x, y, 3, GetParam()),
            TestMake(x.subspan(0, x.size() - 3), y.subspan(0, x.size() - 3)));
}

INSTANTIATE_TEST_SUITE_P(
    Orderings, KendallsTauIncrementalTest,
    testing::Values(KendallsTau::InputOrdering::kUnsorted,
                    KendallsTau::InputOrdering::kSortedByX,
                    KendallsTau::InputOrdering::kSortedByXThenY));

// 60 random numbers generated for x and y.
// We need to use the exact same vectors for all three benchmarks below,
// but the numbers do not have any significance.
std::pair<std::vector<double>, std::vector<double>> BenchmarkVectors(
    KendallsTau::InputOrdering ordering) {
  std::pair<std::vector<double>, std::vector<double>> xy = {
      {85, 90, 5,  2,  80, 70, 46, 13, 46, 79, 90, 20, 3,  61, 40,
       86, 43, 13, 5,  18, 2,  27, 98, 69, 88, 25, 17, 62, 34, 55,
       66, 19, 73, 17, 70, 35, 39, 16, 14, 67, 92, 15, 9,  73, 66,
       31, 54, 65, 2,  50, 16, 57, 38, 49, 68, 61, 86, 48, 39, 77},
      {67, 33, 35, 13, 96, 83, 66, 41,  43, 39, 52, 52, 33, 72, 80,
       1,  34, 85, 48, 28, 56, 45, 60,  89, 11, 28, 15, 45, 31, 31,
       27, 2,  41, 41, 73, 66, 58, 87,  45, 50, 34, 84, 73, 71, 19,
       7,  40, 78, 12, 11, 28, 39, 100, 56, 50, 53, 0,  9,  3,  63}};
  ZipSort(absl::MakeSpan(xy.first), absl::MakeSpan(xy.second), ordering);
  return xy;
}

// Type aliases to have benchmarks display `BM_Make<kBruteForce, ...>` rather
// than `BM_Make<KendallsTau::Algorithm::kBruteForce, ...>`.
static constexpr KendallsTau::Algorithm kBruteForce =
    KendallsTau::Algorithm::kBruteForce;
static constexpr KendallsTau::Algorithm kKnight =
    KendallsTau::Algorithm::kKnight;
static constexpr KendallsTau::InputOrdering kUnsorted =
    KendallsTau::InputOrdering::kUnsorted;
static constexpr KendallsTau::InputOrdering kSortedByX =
    KendallsTau::InputOrdering::kSortedByX;
static constexpr KendallsTau::InputOrdering kSortedByXThenY =
    KendallsTau::InputOrdering::kSortedByXThenY;

template <KendallsTau::Algorithm algorithm,
          KendallsTau::InputOrdering input_ordering>
void BM_Make(benchmark::State& state) {
  const int size = state.range(0);
  auto [x, y] = BenchmarkVectors(input_ordering);

  for (auto _ : state) {
    KendallsTau tau(KendallsTau::Make(absl::MakeSpan(x.data(), size),
                                      absl::MakeSpan(y.data(), size), algorithm,
                                      input_ordering));
    benchmark::DoNotOptimize(tau);
  }
}
BENCHMARK(BM_Make<kBruteForce, kUnsorted>)->Arg(1)->Arg(10)->Arg(30)->Arg(60);
BENCHMARK(BM_Make<kBruteForce, kSortedByX>)->Arg(1)->Arg(10)->Arg(30)->Arg(60);
BENCHMARK(BM_Make<kBruteForce, kSortedByXThenY>)
    ->Arg(1)
    ->Arg(10)
    ->Arg(30)
    ->Arg(60);
BENCHMARK(BM_Make<kKnight, kUnsorted>)->Arg(1)->Arg(10)->Arg(30)->Arg(60);
BENCHMARK(BM_Make<kKnight, kSortedByX>)->Arg(1)->Arg(10)->Arg(30)->Arg(60);
BENCHMARK(BM_Make<kKnight, kSortedByXThenY>)->Arg(1)->Arg(10)->Arg(30)->Arg(60);

template <KendallsTau::InputOrdering input_ordering>
void BM_DropFront(benchmark::State& state) {
  const int size = state.range(0);
  auto [x, y] = BenchmarkVectors(input_ordering);
  const auto xspan = absl::MakeSpan(x.data(), size);
  const auto yspan = absl::MakeSpan(y.data(), size);
  KendallsTau full =
      KendallsTau::Make(xspan, yspan, KendallsTau::Algorithm::kKnight);

  for (auto _ : state) {
    KendallsTau partial = full.DropFront(xspan, yspan, 1, input_ordering);
    benchmark::DoNotOptimize(partial);
  }
}
BENCHMARK(BM_DropFront<kUnsorted>)->Arg(10)->Arg(30)->Arg(60);
BENCHMARK(BM_DropFront<kSortedByX>)->Arg(10)->Arg(30)->Arg(60);

template <KendallsTau::InputOrdering input_ordering>
void BM_DropBack(benchmark::State& state) {
  const int size = state.range(0);
  auto [x, y] = BenchmarkVectors(input_ordering);
  const auto xspan = absl::MakeSpan(x.data(), size);
  const auto yspan = absl::MakeSpan(y.data(), size);
  KendallsTau full =
      KendallsTau::Make(xspan, yspan, KendallsTau::Algorithm::kKnight);

  for (auto _ : state) {
    KendallsTau partial = full.DropBack(xspan, yspan, 1, input_ordering);
    benchmark::DoNotOptimize(partial);
  }
}
BENCHMARK(BM_DropBack<kUnsorted>)->Arg(10)->Arg(30)->Arg(60);
BENCHMARK(BM_DropBack<kSortedByX>)->Arg(10)->Arg(30)->Arg(60);

void BM_EstimateCorrelation(benchmark::State& state) {
  auto [x, y] = BenchmarkVectors(KendallsTau::InputOrdering::kUnsorted);

  KendallsTau tau(KendallsTau::Make(x, y));
  for (auto _ : state) {
    tau.GetCorrelationEstimate();
  }
}
BENCHMARK(BM_EstimateCorrelation);

void BM_MakeAndEstimateCorrelation(benchmark::State& state) {
  auto [x, y] = BenchmarkVectors(KendallsTau::InputOrdering::kUnsorted);

  for (auto _ : state) {
    KendallsTau tau(KendallsTau::Make(x, y));
    tau.GetCorrelationEstimate();
  }
}
BENCHMARK(BM_MakeAndEstimateCorrelation);

}  // anonymous namespace
