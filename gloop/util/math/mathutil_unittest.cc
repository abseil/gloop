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

// Test functions in MathUtil.
// TODO: Add tests for other functions in MathUtil

#include "gloop/util/math/mathutil.h"

#include <stdio.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <iterator>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "absl/log/log.h"
#include "absl/numeric/int128.h"
#include "benchmark/benchmark.h"
#include "fuzztest/fuzztest.h"
#include "gloop/util/math/mathlimits.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using ::fuzztest::InRange;
using ::fuzztest::Just;
using ::fuzztest::NonNegative;
using ::fuzztest::Positive;
using ::fuzztest::StructOf;

using ::testing::Eq;

using int128_t = __int128;
using uint128_t = unsigned __int128;

// A struct for testing quad equation solver.
// r1 and r2 are expected roots if has_roots is true for the equation.
// r1 <= r2.
struct QuadEqTestCase {
  long double a;
  long double b;
  long double c;
  int num_roots;
  double r1;
  double r2;
};

bool BothInfinite(double a, double b) { return a == HUGE_VAL && b == HUGE_VAL; }

// 'long double' not supported on WASM.
#if !defined(__wasm__) && !defined(__EMSCRIPTEN__)
TEST(MathUtil, QuadEqSolver) {
  // Test RealRootsForQuadratic().  Validate a variety of special cases
  // since in a previous implementation a==0 and b==0 failed.
  QuadEqTestCase testcases[] = {
      // a,   b,  c,     n, r1, r2        // Case tested
      {0, 0, 0, 1, 0, 0},          // a, b, c vanish: treated as a==epsilon.
      {0, 0, 1, 0, 0, 0},          // a, b vanish: no roots.
      {0, 1, 1, 2, -1, HUGE_VAL},  // a vanishes: 2nd root at infinity.
      {1, 0, 0, 1, 0, 0},          // b, c vanish: double root at 0.
      {1, 0, -1, 2, -1, 1},        // b vanishes, c negative: 2 roots.
      {1, 0, 1, 0, 0, 0},          // b vanishes, c positive: no roots.
      {1, 1, 0, 2, -1, 0},         // c vanishes: 2 roots, one at 0.
      {1, 1, 1, 0, 0, 0},          // Negative discriminant: no roots.
      {1e30, 1, 1e-15, 0, 0, 0},   // Negative discriminant: no roots.
      {1, -2, 1, 1, 1, 1},         // Discriminant 0: double root.
      {1, 0.2, 0.01 + 1e-18, 1, -0.1, -0.1},  // Discriminant ~0: double root.
      {1, -5, 6, 2, 2, 3}                     // Positive discriminant: 2 roots.
  };
  long double r1 = 0, r2 = 0;
  int num_tcs = sizeof(testcases) / sizeof(*testcases);
  for (int i = 0; i < num_tcs; i++) {
    const QuadEqTestCase& tc = testcases[i];
    // Validate scale-invariance by checking each test case at many scales.
    for (long double scale = 1e-30; scale < 1e30; scale *= 1e3) {
      EXPECT_EQ(tc.num_roots,
                MathUtil::RealRootsForQuadratic(tc.a * scale, tc.b * scale,
                                                tc.c * scale, &r1, &r2))
          << " i = " << i << " scale = " << scale;
      if (tc.num_roots) {
        if (r1 > r2) {
          using std::swap;
          swap(r1, r2);
        }
        EXPECT_TRUE(MathUtil::AlmostEquals(static_cast<double>(r1), tc.r1) ||
                    BothInfinite(tc.r1, r1))
            << " i = " << i << " expect=" << tc.r1 << " actual=" << r1;
        EXPECT_TRUE(MathUtil::AlmostEquals(static_cast<double>(r2), tc.r2) ||
                    BothInfinite(tc.r2, r2))
            << " i = " << i << " expect=" << tc.r2 << " actual=" << r2;
      }
    }
  }
}
#endif

void BM_RealRootsForQuadratic(benchmark::State& state) {
  const std::size_t num_test_cases = 100;
  QuadEqTestCase test_cases[num_test_cases];
  // Use polynomial 0 == (x-i)*(x-2*i) = x^2 - (3*i)*x + 2*i^2 in test case i
  for (int i = 0; i < num_test_cases; ++i) {
    test_cases[i].r1 = i;
    test_cases[i].r2 = 2 * i;
    test_cases[i].a = 1;
    test_cases[i].b = -3 * i;
    test_cases[i].c = 2 * i * i;
    test_cases[i].num_roots = (i == 0) ? 1 : 2;
  }
  long double r1, r2;

  for (auto _ : state) {
    const std::size_t i = state.iterations() % num_test_cases;
    EXPECT_EQ(test_cases[i].num_roots,
              MathUtil::RealRootsForQuadratic(test_cases[i].a, test_cases[i].b,
                                              test_cases[i].c, &r1, &r2))
        << i;
  }
}
BENCHMARK(BM_RealRootsForQuadratic);

namespace quartic {

// If two adjacent elements of 'vp' are wihin 'tol' of each other,
// remove them both and insert their mean instead.
void MergeCloseValues(std::vector<long double>* vp, long double tolerance) {
  std::vector<long double>& v = *vp;
  for (size_t i = 1; i > 0 && i < v.size(); ++i) {
    long double a = v[i - 1];
    long double b = v[i];
    if (std::fabs(b - a) > tolerance) continue;
    long double avg = .5 * (a + b);
    v.erase(v.begin() + i);
    --i;
    v[i] = avg;
  }
}

template <typename Iter>
std::string JoinVector(Iter first, Iter last) {
  std::ostringstream os;
  os << "{";
  const char* sep = "";
  for (; first != last; ++first) {
    os << sep << *first;
    sep = ", ";
  }
  os << "}";
  return os.str();
}

struct Spec {
  friend std::ostream& operator<<(std::ostream& os, const Spec& s) {
    return os << "coeff:" << JoinVector(s.poly + 0, s.poly + 4)
              << ", roots:" << JoinVector(s.roots.begin(), s.roots.end());
  }

  long double poly[4];
  std::vector<long double> roots;
};

const std::vector<Spec>& GetSpecs() {
  static const std::vector<Spec>& v = *new std::vector<Spec>{
      // x^4 - 1: 2 real roots {-1, 1}.
      {{0.0, 0.0, 0.0, -1.0}, {-1.0, 1.0}},
      // x^4 + x^2 - 1: 2 real roots {+/- sqrt((-1 + sqrt(5)) / 2))}.
      {{0.0, 1.0, 0.0, -1.0},
       {-sqrt((-1 + sqrt(5)) / 2), sqrt((-1 + sqrt(5)) / 2)}},
      // x^4 - x^3 - 5x^2 - x - 6: 2 real roots {-2, 3}.
      {{-1.0, -5.0, -1.0, -6.0}, {-2.0, 3.0}},
      // x^4 - 10x^3 + 35x^2 - 50x - 24: 4 real roots {1, 2, 3, 4}.
      {{-10.0, 35.0, -50.0, 24.0}, {1.0, 2.0, 3.0, 4.0}},
      // x^4 - 5x^3 + 4x^2 + 3x + (9-eps): two close real roots {3-eps,3+eps}.
      {{-5.0, 4.0, 3.0, 9.0 - 1e-9}, {3.0}},
      // x^4 + 6x^3 + 16x^2 + 22x + 15: no real roots.
      {{6.0, 16.0, 22.0, 15.0}, {}},
      // Special case where Q in RealRootsForCubic is small and negative.
      {{0.90, 0.80, -0.20, -0.10}, {-0.270156211871642, 0.370156211871642}}};
  return v;
}

// "long double" not supported on WASM.
#if !defined(__wasm__) && !defined(__EMSCRIPTEN__)
struct QuarticEqSolverTest : testing::TestWithParam<Spec> {};

MATCHER_P(AreAlmostEqual, tol, "") {
  double a = static_cast<double>(testing::get<0>(arg));
  double b = static_cast<double>(testing::get<1>(arg));
  if (result_listener) {
    std::ostringstream os;
    os << std::setprecision(16) << a << " within " << tol << " of " << b;
    *result_listener << os.str();
  }
  return std::fabs(b - a) < tol;
}

TEST_P(QuarticEqSolverTest, Basic) {
  const Spec& s = GetParam();
  // Tests that the Quartic equation x^4 + ax^3 + bx^2 + cx + d has
  // num_expected_roots roots, and that the found roots are equal to the
  // passed expected roots. The expected roots must be sorted. Any expected
  // root values after num_expected_roots are ignored.
  std::vector<long double> found_roots(4);
  found_roots.resize(MathUtil::RealRootsForQuartic(
      s.poly[0], s.poly[1], s.poly[2], s.poly[3], &found_roots[0]));
  std::sort(found_roots.begin(), found_roots.end());
  static const double kTol = 1e-4;
  MergeCloseValues(&found_roots, kTol);
  EXPECT_THAT(found_roots, testing::Pointwise(AreAlmostEqual(kTol), s.roots));
}

INSTANTIATE_TEST_SUITE_P(QuarticEqSolverTests, QuarticEqSolverTest,
                         testing::ValuesIn(GetSpecs()));
#endif

}  // namespace quartic

TEST(MathUtil, Sigmoid) {
  for (double lambda = 0.1; lambda < 2.0; lambda += 0.1) {
    EXPECT_NEAR(MathUtil::Sigmoid(0.0, lambda), 0.5, 1e-8);
    for (double x = -10.0; x < 10.0; x += 0.4)
      EXPECT_NEAR(
          MathUtil::InverseSigmoid(MathUtil::Sigmoid(x, lambda), lambda), x,
          1e-8)
          << " where x=" << x << " and lambda=" << lambda;
  }
}

TEST(MathUtil, SigmoidFloat) {
  for (float lambda = 0.1f; lambda < 1.5f; lambda += 0.1f) {
    EXPECT_NEAR(MathUtil::Sigmoid(0.0f, lambda), 0.5f, 1e-6f);
    for (float x = -5.0f; x < 5.0f; x += 0.4f)
      EXPECT_NEAR(MathUtil::InverseSigmoidFloat(
                      MathUtil::SigmoidFloat(x, lambda), lambda),
                  x, 1e-4f)
          << " where x=" << x << " and lambda=" << lambda;
  }
}

struct GCDTestCase {
  unsigned int x;
  unsigned int y;
  unsigned int gcd;
};

TEST(MathUtil, GCD) {
  GCDTestCase testcases[] = {
      {10, 20, 10}, {27, 8, 1}, {4, 3, 1}, {6, 8, 2},
      {5, 0, 5},    {5, 5, 5},  {0, 0, 0}, {2147483649U, 3U, 3U}};
  int num_tcs = sizeof(testcases) / sizeof(*testcases);
  for (int i = 0; i < num_tcs; i++) {
    const GCDTestCase& tc = testcases[i];
    EXPECT_EQ(tc.gcd, std::gcd(tc.x, tc.y));
    EXPECT_EQ(tc.gcd, std::gcd(tc.y, tc.x));
    EXPECT_EQ(tc.gcd, std::gcd(tc.x, tc.y));
    EXPECT_EQ(tc.gcd, std::gcd(tc.y, tc.x));
    int a, b;
    EXPECT_EQ(tc.gcd, MathUtil::ExtendedGCD(tc.x, tc.y, &a, &b));
    EXPECT_EQ(tc.gcd, a * tc.x + b * tc.y);
    EXPECT_EQ(tc.gcd, MathUtil::ExtendedGCD(tc.y, tc.x, &b, &a));
    EXPECT_EQ(tc.gcd, a * tc.x + b * tc.y);
  }
  static const uint64_t biggish_prime = 1666666667;
  EXPECT_EQ(biggish_prime, std::gcd(biggish_prime * 3, biggish_prime * 4));
}

struct LCMTestCase {
  unsigned int x;
  unsigned int y;
  unsigned int lcm;
};

TEST(MathUtil, LeastCommonMultiple) {
  LCMTestCase testcases[] = {
      {161, 35, 805}, {162, 35, 5670},   {4, 16, 16}, {15, 60, 60},
      {15, 65, 195},  {1052, 52, 13676}, {4, 4, 4},   {4, 0, 0},
      {0, 1, 0},      {0, 0, 0},
  };
  int num_tcs = sizeof(testcases) / sizeof(*testcases);
  for (int i = 0; i < num_tcs; i++) {
    const LCMTestCase& tc = testcases[i];
    EXPECT_EQ(tc.lcm, std::lcm(tc.x, tc.y));
    EXPECT_EQ(tc.lcm, std::lcm(tc.y, tc.x));
    EXPECT_EQ(tc.lcm, std::lcm(tc.x, tc.y));
    EXPECT_EQ(tc.lcm, std::lcm(tc.y, tc.x));
  }
  EXPECT_EQ(std::numeric_limits<uint64_t>::max(),
            std::lcm(uint64_t{2753074036095}, 6700417));
  EXPECT_EQ(std::numeric_limits<int64_t>::max(),
            std::lcm(uint64_t{2028184990993}, 31833193));
}

// compute 2^i for i < 0
double two_to_the(int i) {
  EXPECT_LT(i, 0);
  double v = 1.0;
  while (i < 0) {
    v /= 2.0;
    i++;
  }
  return v;
}

void TestRoundFloat(const float f) {
  VLOG(1) << "orig " << std::hex << std::bit_cast<int32_t>(f);
  for (int i = 1; i <= 23; i++) {
    const float g = MathUtil::RoundOffBits(f, i);
    VLOG(1) << i << " " << std::hex << std::bit_cast<int32_t>(g);
    const double allowed_error = two_to_the(i - 24);
    if (f < 0) {
      EXPECT_GE(g, f * (1.0 + allowed_error));
      EXPECT_LE(g, f * (1.0 - allowed_error));
    } else {
      EXPECT_LE(g, f * (1.0 + allowed_error));
      EXPECT_GE(g, f * (1.0 - allowed_error));
    }
    EXPECT_EQ(std::bit_cast<int32_t>(g) & ((1 << i) - 1), 0);
  }
}

void TestRoundDouble(const double f) {
  VLOG(1) << "orig " << std::hex << std::bit_cast<int64_t>(f);
  for (int i = 1; i <= 52; i++) {
    const double g = MathUtil::RoundOffBits(f, i);
    VLOG(1) << i << " " << std::hex << std::bit_cast<int64_t>(g);
    const double allowed_error = two_to_the(i - 53);
    if (f < 0) {
      EXPECT_GE(g, f * (1.0 + allowed_error));
      EXPECT_LE(g, f * (1.0 - allowed_error));
    } else {
      EXPECT_LE(g, f * (1.0 + allowed_error));
      EXPECT_GE(g, f * (1.0 - allowed_error));
    }
    EXPECT_EQ(std::bit_cast<int64_t>(g) & ((int64_t{1} << i) - 1), 0);
  }
}

void TestRoundNum(double f) {
  TestRoundFloat(f);
  TestRoundDouble(f);
}

TEST(MathUtil, Round) {
  TestRoundNum(3.14159265358979323846);
  TestRoundNum(.0001);
  TestRoundNum(83743873.0);
  TestRoundNum(-87);

  // specific cases
  int32_t x, y;
  float f, g;

  // test carry-into-exponent
  x = 0x407FFFF0;
  f = std::bit_cast<float>(x);
  TestRoundNum(f);

  // test rounding to infinity
  x = 0x7F7FFFF0;
  f = std::bit_cast<float>(x);
  y = 0x7F800000;
  g = std::bit_cast<float>(y);
  EXPECT_EQ(MathUtil::RoundOffBits(f, 4), f);
  EXPECT_EQ(MathUtil::RoundOffBits(f, 5), g);

  // test round-to-even in this case
  x = 0x7F7FFFD0;
  f = std::bit_cast<float>(x);
  y = 0x7F7FFFC0;
  g = std::bit_cast<float>(y);
  EXPECT_EQ(MathUtil::RoundOffBits(f, 5), g);

  // test denormalized numbers
  x = 0x0000FFF0;
  f = std::bit_cast<float>(x);
  y = 0x00010000;
  g = std::bit_cast<float>(y);
  EXPECT_EQ(MathUtil::RoundOffBits(f, 4), f);
  EXPECT_EQ(MathUtil::RoundOffBits(f, 5), g);
  EXPECT_EQ(MathUtil::RoundOffBits(0.0f, 5), 0.0f);

  // test float rounding
  EXPECT_EQ(MathUtil::Round<int32_t>(0.7f), 1);
  EXPECT_EQ(MathUtil::Round<int32_t>(5.7f), 6);
  EXPECT_EQ(MathUtil::Round<int32_t>(6.3f), 6);
  EXPECT_EQ(MathUtil::Round<int32_t>(1000000.7f), 1000001);

  // test that largest representable number below 0.5 rounds to zero.
  // this is important because naive implementation of round:
  // static_cast<int>(r + 0.5f) is 1 due to implicit rounding in operator+
  float rf = std::nextafter(0.5f, .0f);
  EXPECT_LT(rf, 0.5f);
  EXPECT_EQ(MathUtil::Round<int>(rf), 0);

  // same test for double
  double rd = std::nextafter(0.5, 0.0);
  EXPECT_LT(rd, 0.5);
  EXPECT_EQ(MathUtil::Round<int>(rd), 0);

  // same test for long double
  long double rl = std::nextafter(0.5l, 0.0l);
  EXPECT_LT(rl, 0.5l);
  EXPECT_EQ(MathUtil::Round<int>(rl), 0);
}

TEST(MathUtil, RoundToInt8BelowUpperBound) {
  EXPECT_EQ(MathUtil::Round<int8_t>(0x7F.7p0f), 0x7F);
  EXPECT_EQ(MathUtil::Round<int8_t>(0x7F.7p0), 0x7F);
  EXPECT_EQ(MathUtil::Round<int8_t>(0x7F.7p0l), 0x7F);
}

TEST(MathUtil, RoundToInt8TooLarge) {
#if UNDEFINED_BEHAVIOR_SANITIZER
  GTEST_SKIP() << "Skipping test because it purposely invokes UB and ubsan "
               << "is enabled.";
#elif GTEST_HAS_DEATH_TEST
  EXPECT_DEBUG_DEATH(MathUtil::Round<int8_t>(0x7F.9p0f),
                     R"re(rounded < kExclusiveUpperBound)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<int8_t>(0x7F.9p0),
                     R"re(rounded < kExclusiveUpperBound)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<int8_t>(0x7F.9p0l),
                     R"re(rounded < kExclusiveUpperBound)re");
#else   // GTEST_HAS_DEATH_TEST
  GTEST_SKIP() << "Skipping death test because it is unsupported by googletest";
#endif  // GTEST_HAS_DEATH_TEST
}

TEST(MathUtil, RoundToInt8AboveLowerBound) {
  EXPECT_EQ(MathUtil::Round<int8_t>(-0x80.7p0f), -0x80);
  EXPECT_EQ(MathUtil::Round<int8_t>(-0x80.7p0), -0x80);
  EXPECT_EQ(MathUtil::Round<int8_t>(-0x80.7p0l), -0x80);
}

TEST(MathUtil, RoundToInt8TooSmall) {
#if UNDEFINED_BEHAVIOR_SANITIZER
  GTEST_SKIP() << "Skipping test because it purposely invokes UB and ubsan "
               << "is enabled.";
#elif GTEST_HAS_DEATH_TEST
  EXPECT_DEBUG_DEATH(MathUtil::Round<int8_t>(-0x80.9p0f),
                     R"re(kInclusiveLowerBound <= rounded)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<int8_t>(-0x80.9p0),
                     R"re(kInclusiveLowerBound <= rounded)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<int8_t>(-0x80.9p0l),
                     R"re(kInclusiveLowerBound <= rounded)re");
#else   // GTEST_HAS_DEATH_TEST
  GTEST_SKIP() << "Skipping death test because it is unsupported by googletest";
#endif  // GTEST_HAS_DEATH_TEST
}

TEST(MathUtil, RoundToUInt8BelowUpperBound) {
  EXPECT_EQ(MathUtil::Round<uint8_t>(0xFF.7p0f), 0xFF);
  EXPECT_EQ(MathUtil::Round<uint8_t>(0xFF.7p0), 0xFF);
  EXPECT_EQ(MathUtil::Round<uint8_t>(0xFF.7p0l), 0xFF);
}

TEST(MathUtil, RoundToUInt8TooLarge) {
#if UNDEFINED_BEHAVIOR_SANITIZER
  GTEST_SKIP() << "Skipping test because it purposely invokes UB and ubsan "
               << "is enabled.";
#elif GTEST_HAS_DEATH_TEST
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint8_t>(0xFF.9p0f),
                     R"re(rounded < kExclusiveUpperBound)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint8_t>(0xFF.9p0),
                     R"re(rounded < kExclusiveUpperBound)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint8_t>(0xFF.9p0l),
                     R"re(rounded < kExclusiveUpperBound)re");
#else   // GTEST_HAS_DEATH_TEST
  GTEST_SKIP() << "Skipping death test because it is unsupported by googletest";
#endif  // GTEST_HAS_DEATH_TEST
}

TEST(MathUtil, RoundToUInt8AboveLowerBound) {
  EXPECT_EQ(MathUtil::Round<uint8_t>(-0.4f), 0);
  EXPECT_EQ(MathUtil::Round<uint8_t>(-0.4), 0);
  EXPECT_EQ(MathUtil::Round<uint8_t>(-0.4l), 0);
}

TEST(MathUtil, RoundToUInt8TooSmall) {
#if UNDEFINED_BEHAVIOR_SANITIZER
  GTEST_SKIP() << "Skipping test because it purposely invokes UB and ubsan "
               << "is enabled.";
#elif GTEST_HAS_DEATH_TEST
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint8_t>(-0.6f),
                     R"re(kInclusiveLowerBound <= rounded)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint8_t>(-0.6),
                     R"re(kInclusiveLowerBound <= rounded)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint8_t>(-0.6l),
                     R"re(kInclusiveLowerBound <= rounded)re");
#else   // GTEST_HAS_DEATH_TEST
  GTEST_SKIP() << "Skipping death test because it is unsupported by googletest";
#endif  // GTEST_HAS_DEATH_TEST
}

TEST(MathUtil, RoundToInt16BelowUpperBound) {
  EXPECT_EQ(MathUtil::Round<int16_t>(0x7FFF.7p0f), 0x7FFF);
  EXPECT_EQ(MathUtil::Round<int16_t>(0x7FFF.7p0), 0x7FFF);
  EXPECT_EQ(MathUtil::Round<int16_t>(0x7FFF.7p0l), 0x7FFF);
}

TEST(MathUtil, RoundToInt16TooLarge) {
#if UNDEFINED_BEHAVIOR_SANITIZER
  GTEST_SKIP() << "Skipping test because it purposely invokes UB and ubsan "
               << "is enabled.";
#elif GTEST_HAS_DEATH_TEST
  EXPECT_DEBUG_DEATH(MathUtil::Round<int16_t>(0x7FFF.9p0f),
                     R"re(rounded < kExclusiveUpperBound)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<int16_t>(0x7FFF.9p0),
                     R"re(rounded < kExclusiveUpperBound)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<int16_t>(0x7FFF.9p0l),
                     R"re(rounded < kExclusiveUpperBound)re");
#else   // GTEST_HAS_DEATH_TEST
  GTEST_SKIP() << "Skipping death test because it is unsupported by googletest";
#endif  // GTEST_HAS_DEATH_TEST
}

TEST(MathUtil, RoundToInt16AboveLowerBound) {
  EXPECT_EQ(MathUtil::Round<int16_t>(-0x8000.7p0f), -0x8000);
  EXPECT_EQ(MathUtil::Round<int16_t>(-0x8000.7p0), -0x8000);
  EXPECT_EQ(MathUtil::Round<int16_t>(-0x8000.7p0l), -0x8000);
}

TEST(MathUtil, RoundToInt16TooSmall) {
#if UNDEFINED_BEHAVIOR_SANITIZER
  GTEST_SKIP() << "Skipping test because it purposely invokes UB and ubsan "
               << "is enabled.";
#elif GTEST_HAS_DEATH_TEST
  EXPECT_DEBUG_DEATH(MathUtil::Round<int16_t>(-0x8000.9p0f),
                     R"re(kInclusiveLowerBound <= rounded)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<int16_t>(-0x8000.9p0),
                     R"re(kInclusiveLowerBound <= rounded)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<int16_t>(-0x8000.9p0l),
                     R"re(kInclusiveLowerBound <= rounded)re");
#else   // GTEST_HAS_DEATH_TEST
  GTEST_SKIP() << "Skipping death test because it is unsupported by googletest";
#endif  // GTEST_HAS_DEATH_TEST
}

TEST(MathUtil, RoundToUInt16BelowUpperBound) {
  EXPECT_EQ(MathUtil::Round<uint16_t>(0xFFFF.7p0f), 0xFFFF);
  EXPECT_EQ(MathUtil::Round<uint16_t>(0xFFFF.7p0), 0xFFFF);
  EXPECT_EQ(MathUtil::Round<uint16_t>(0xFFFF.7p0l), 0xFFFF);
}

TEST(MathUtil, RoundToUInt16TooLarge) {
#if UNDEFINED_BEHAVIOR_SANITIZER
  GTEST_SKIP() << "Skipping test because it purposely invokes UB and ubsan "
               << "is enabled.";
#elif GTEST_HAS_DEATH_TEST
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint16_t>(0xFFFF.9p0f),
                     R"re(rounded < kExclusiveUpperBound)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint16_t>(0xFFFF.9p0),
                     R"re(rounded < kExclusiveUpperBound)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint16_t>(0xFFFF.9p0l),
                     R"re(rounded < kExclusiveUpperBound)re");
#else   // GTEST_HAS_DEATH_TEST
  GTEST_SKIP() << "Skipping death test because it is unsupported by googletest";
#endif  // GTEST_HAS_DEATH_TEST
}

TEST(MathUtil, RoundToUInt16AboveLowerBound) {
  EXPECT_EQ(MathUtil::Round<uint16_t>(-0.4f), 0);
  EXPECT_EQ(MathUtil::Round<uint16_t>(-0.4), 0);
  EXPECT_EQ(MathUtil::Round<uint16_t>(-0.4l), 0);
}

TEST(MathUtil, RoundToUInt16TooSmall) {
#if UNDEFINED_BEHAVIOR_SANITIZER
  GTEST_SKIP() << "Skipping test because it purposely invokes UB and ubsan "
               << "is enabled.";
#elif GTEST_HAS_DEATH_TEST
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint16_t>(-0.6f),
                     R"re(kInclusiveLowerBound <= rounded)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint16_t>(-0.6),
                     R"re(kInclusiveLowerBound <= rounded)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint16_t>(-0.6l),
                     R"re(kInclusiveLowerBound <= rounded)re");
#else   // GTEST_HAS_DEATH_TEST
  GTEST_SKIP() << "Skipping death test because it is unsupported by googletest";
#endif  // GTEST_HAS_DEATH_TEST
}

TEST(MathUtil, RoundToInt32BelowUpperBound) {
  // `float` has only 24 bits of mantissa, so 2^31 - 1 is not representable.
  EXPECT_EQ(
      MathUtil::Round<int32_t>(std::nextafter(std::pow(2.0f, 31.0f), 0.0f)),
      0x7FFF'FF80);
  EXPECT_EQ(MathUtil::Round<int32_t>(0x7FFF'FFFF.7p0), 0x7FFF'FFFF);
  EXPECT_EQ(MathUtil::Round<int32_t>(0x7FFF'FFFF.7p0l), 0x7FFF'FFFF);
}

TEST(MathUtil, RoundToInt32TooLarge) {
#if UNDEFINED_BEHAVIOR_SANITIZER
  GTEST_SKIP() << "Skipping test because it purposely invokes UB and ubsan "
               << "is enabled.";
#elif GTEST_HAS_DEATH_TEST
  EXPECT_DEBUG_DEATH(MathUtil::Round<int32_t>(std::pow(2.0f, 31.0f)),
                     R"re(rounded < kExclusiveUpperBound)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<int32_t>(0x7FFF'FFFF.9p0),
                     R"re(rounded < kExclusiveUpperBound)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<int32_t>(0x7FFF'FFFF.9p0l),
                     R"re(rounded < kExclusiveUpperBound)re");
#else   // GTEST_HAS_DEATH_TEST
  GTEST_SKIP() << "Skipping death test because it is unsupported by googletest";
#endif  // GTEST_HAS_DEATH_TEST
}

TEST(MathUtil, RoundToInt32AboveLowerBound) {
  EXPECT_EQ(MathUtil::Round<int32_t>(-std::pow(2.0f, 31.0f)), -0x8000'0000);
  EXPECT_EQ(MathUtil::Round<int32_t>(-0x8000'0000.7p0), -0x8000'0000);
  EXPECT_EQ(MathUtil::Round<int32_t>(-0x8000'0000.7p0l), -0x8000'0000);
}

TEST(MathUtil, RoundToInt32TooSmall) {
#if UNDEFINED_BEHAVIOR_SANITIZER
  GTEST_SKIP() << "Skipping test because it purposely invokes UB and ubsan "
               << "is enabled.";
#elif GTEST_HAS_DEATH_TEST
  EXPECT_DEBUG_DEATH(
      MathUtil::Round<int32_t>(std::nextafter(
          -std::pow(2.0f, 31.0f), -std::numeric_limits<float>::infinity())),
      R"re(kInclusiveLowerBound <= rounded)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<int32_t>(-0x8000'0000.9p0),
                     R"re(kInclusiveLowerBound <= rounded)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<int32_t>(-0x8000'0000.9p0l),
                     R"re(kInclusiveLowerBound <= rounded)re");
#else   // GTEST_HAS_DEATH_TEST
  GTEST_SKIP() << "Skipping death test because it is unsupported by googletest";
#endif  // GTEST_HAS_DEATH_TEST
}

TEST(MathUtil, RoundToUInt32BelowUpperBound) {
  EXPECT_EQ(
      MathUtil::Round<uint32_t>(std::nextafter(std::pow(2.0f, 32.0f), 0.0f)),
      0xFFFF'FF00);
  EXPECT_EQ(MathUtil::Round<uint32_t>(0xFFFF'FFFF.7p0), 0xFFFF'FFFF);
  EXPECT_EQ(MathUtil::Round<uint32_t>(0xFFFF'FFFF.7p0l), 0xFFFF'FFFF);
}

TEST(MathUtil, RoundToUInt32TooLarge) {
#if UNDEFINED_BEHAVIOR_SANITIZER
  GTEST_SKIP() << "Skipping test because it purposely invokes UB and ubsan "
               << "is enabled.";
#elif GTEST_HAS_DEATH_TEST
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint32_t>(std::pow(2.0f, 32.0f)),
                     R"re(rounded < kExclusiveUpperBound)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint32_t>(0xFFFF'FFFF.9p0),
                     R"re(rounded < kExclusiveUpperBound)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint32_t>(0xFFFF'FFFF.9p0l),
                     R"re(rounded < kExclusiveUpperBound)re");
#else   // GTEST_HAS_DEATH_TEST
  GTEST_SKIP() << "Skipping death test because it is unsupported by googletest";
#endif  // GTEST_HAS_DEATH_TEST
}

TEST(MathUtil, RoundToUInt32AboveLowerBound) {
  EXPECT_EQ(MathUtil::Round<uint32_t>(-0.4f), 0);
  EXPECT_EQ(MathUtil::Round<uint32_t>(-0.4), 0);
  EXPECT_EQ(MathUtil::Round<uint32_t>(-0.4l), 0);
}

TEST(MathUtil, RoundToUInt32TooSmall) {
#if UNDEFINED_BEHAVIOR_SANITIZER
  GTEST_SKIP() << "Skipping test because it purposely invokes UB and ubsan "
               << "is enabled.";
#elif GTEST_HAS_DEATH_TEST
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint32_t>(-0.6f),
                     R"re(kInclusiveLowerBound <= rounded)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint32_t>(-0.6),
                     R"re(kInclusiveLowerBound <= rounded)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint32_t>(-0.6l),
                     R"re(kInclusiveLowerBound <= rounded)re");
#else   // GTEST_HAS_DEATH_TEST
  GTEST_SKIP() << "Skipping death test because it is unsupported by googletest";
#endif  // GTEST_HAS_DEATH_TEST
}

TEST(MathUtil, RoundToInt64BelowUpperBound) {
  EXPECT_EQ(
      MathUtil::Round<int64_t>(std::nextafter(std::pow(2.0f, 63.0f), 0.0f)),
      0x7FFF'FF80'0000'0000);
  EXPECT_EQ(MathUtil::Round<int64_t>(std::nextafter(std::pow(2.0, 63.0), 0.0)),
            0x7FFF'FFFF'FFFF'FC00);
  EXPECT_EQ(MathUtil::Round<int64_t>(0x7FFF'FFFF'FFFF'FFFF.0p0l),
            0x7FFF'FFFF'FFFF'FFFF);
}

TEST(MathUtil, RoundToInt64TooLarge) {
#if UNDEFINED_BEHAVIOR_SANITIZER
  GTEST_SKIP() << "Skipping test because it purposely invokes UB and ubsan "
               << "is enabled.";
#elif GTEST_HAS_DEATH_TEST
  EXPECT_DEBUG_DEATH(MathUtil::Round<int64_t>(std::pow(2.0f, 63.0f)),
                     R"re(rounded < kExclusiveUpperBound)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<int64_t>(0x7FFF'FFFF'FFFF'FFFF.9p0),
                     R"re(rounded < kExclusiveUpperBound)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<int64_t>(0x7FFF'FFFF'FFFF'FFFF.9p0l),
                     R"re(rounded < kExclusiveUpperBound)re");
#else   // GTEST_HAS_DEATH_TEST
  GTEST_SKIP() << "Skipping death test because it is unsupported by googletest";
#endif  // GTEST_HAS_DEATH_TEST
}

TEST(MathUtil, RoundToInt64AboveLowerBound) {
  EXPECT_EQ(MathUtil::Round<int64_t>(-std::pow(2.0f, 63.0f)),
            -0x8000'0000'0000'0000);
  EXPECT_EQ(MathUtil::Round<int64_t>(-0x8000'0000'0000'0000.0p0),
            -0x8000'0000'0000'0000);
  EXPECT_EQ(MathUtil::Round<int64_t>(-0x8000'0000'0000'0000.0p0l),
            -0x8000'0000'0000'0000);
}

TEST(MathUtil, RoundToInt64TooSmall) {
#if UNDEFINED_BEHAVIOR_SANITIZER
  GTEST_SKIP() << "Skipping test because it purposely invokes UB and ubsan "
               << "is enabled.";
#elif GTEST_HAS_DEATH_TEST
  EXPECT_DEBUG_DEATH(
      MathUtil::Round<int64_t>(std::nextafter(
          -std::pow(2.0f, 63.0f), -std::numeric_limits<float>::infinity())),
      R"re(kInclusiveLowerBound <= rounded)re");
  EXPECT_DEBUG_DEATH(
      MathUtil::Round<int64_t>(std::nextafter(
          -std::pow(2.0, 63.0), -std::numeric_limits<double>::infinity())),
      R"re(kInclusiveLowerBound <= rounded)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<int64_t>(-0x8000'0000'0000'0000.9p0l),
                     R"re(kInclusiveLowerBound <= rounded)re");
#else   // GTEST_HAS_DEATH_TEST
  GTEST_SKIP() << "Skipping death test because it is unsupported by googletest";
#endif  // GTEST_HAS_DEATH_TEST
}

TEST(MathUtil, RoundToUInt64BelowUpperBound) {
  EXPECT_EQ(
      MathUtil::Round<uint64_t>(std::nextafter(std::pow(2.0f, 64.0f), 0.0f)),
      0xFFFF'FF00'0000'0000);
  EXPECT_EQ(MathUtil::Round<uint64_t>(std::nextafter(std::pow(2.0, 64.0), 0.0)),
            0xFFFF'FFFF'FFFF'F800);
  EXPECT_EQ(MathUtil::Round<uint64_t>(0xFFFF'FFFF'FFFF'FFFF.0p0l),
            0xFFFF'FFFF'FFFF'FFFF);
}

TEST(MathUtil, RoundToUInt64TooLarge) {
#if UNDEFINED_BEHAVIOR_SANITIZER
  GTEST_SKIP() << "Skipping test because it purposely invokes UB and ubsan "
               << "is enabled.";
#elif GTEST_HAS_DEATH_TEST
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint64_t>(std::pow(2.0f, 64.0f)),
                     R"re(rounded < kExclusiveUpperBound)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint64_t>(std::pow(2.0, 64.0)),
                     R"re(rounded < kExclusiveUpperBound)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint64_t>(0xFFFF'FFFF'FFFF'FFFF.9p0l),
                     R"re(rounded < kExclusiveUpperBound)re");
#else   // GTEST_HAS_DEATH_TEST
  GTEST_SKIP() << "Skipping death test because it is unsupported by googletest";
#endif  // GTEST_HAS_DEATH_TEST
}

TEST(MathUtil, RoundToUInt64AboveLowerBound) {
  EXPECT_EQ(MathUtil::Round<uint64_t>(-0.4f), 0);
  EXPECT_EQ(MathUtil::Round<uint64_t>(-0.4), 0);
  EXPECT_EQ(MathUtil::Round<uint64_t>(-0.4l), 0);
}

TEST(MathUtil, RoundToUInt64TooSmall) {
#if UNDEFINED_BEHAVIOR_SANITIZER
  GTEST_SKIP() << "Skipping test because it purposely invokes UB and ubsan "
               << "is enabled.";
#elif GTEST_HAS_DEATH_TEST
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint64_t>(-0.6f),
                     R"re(kInclusiveLowerBound <= rounded)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint64_t>(-0.6),
                     R"re(kInclusiveLowerBound <= rounded)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint64_t>(-0.6l),
                     R"re(kInclusiveLowerBound <= rounded)re");
#else   // GTEST_HAS_DEATH_TEST
  GTEST_SKIP() << "Skipping death test because it is unsupported by googletest";
#endif  // GTEST_HAS_DEATH_TEST
}

TEST(MathUtil, RoundToInt128BelowUpperBound) {
  // There are no __int128 constants, we so we need to build them up.
  EXPECT_EQ(
      MathUtil::Round<int128_t>(std::nextafter(std::pow(2.0f, 127.0f), 0.0f)),
      int128_t{0x7FFF'FF80'0000'0000} << 64);
  EXPECT_EQ(
      MathUtil::Round<int128_t>(std::nextafter(std::pow(2.0, 127.0), 0.0)),
      int128_t{0x7FFF'FFFF'FFFF'FC00} << 64);
  if constexpr (std::numeric_limits<long double>::digits == 113) {
    EXPECT_EQ(
        MathUtil::Round<int128_t>(std::nextafter(std::pow(2.0l, 127.0l), 0.0l)),
        int128_t{0x7FFF'FFFF'FFFF'FFFF} << 64 | 0xFFFF'FFFF'FFFF'C000);
  } else {
    ASSERT_EQ(64, std::numeric_limits<long double>::digits);
    EXPECT_EQ(
        MathUtil::Round<int128_t>(std::nextafter(std::pow(2.0l, 127.0l), 0.0l)),
        int128_t{0x7FFF'FFFF'FFFF'FFFF} << 64 | 0x8000'0000'0000'0000);
  }
}

TEST(MathUtil, RoundToInt128TooLarge) {
#if UNDEFINED_BEHAVIOR_SANITIZER
  GTEST_SKIP() << "Skipping test because it purposely invokes UB and ubsan "
               << "is enabled.";
#elif GTEST_HAS_DEATH_TEST
  EXPECT_DEBUG_DEATH(MathUtil::Round<int128_t>(std::pow(2.0f, 127.0f)),
                     R"re(rounded < kExclusiveUpperBound)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<int128_t>(std::pow(2.0, 127.0)),
                     R"re(rounded < kExclusiveUpperBound)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<int128_t>(std::pow(2.0l, 127.0l)),
                     R"re(rounded < kExclusiveUpperBound)re");
#else   // GTEST_HAS_DEATH_TEST
  GTEST_SKIP() << "Skipping death test because it is unsupported by googletest";
#endif  // GTEST_HAS_DEATH_TEST
}

TEST(MathUtil, RoundToInt128AboveLowerBound) {
  EXPECT_EQ(MathUtil::Round<int128_t>(-std::pow(2.0f, 127.0f)),
            -int128_t{0x8000'0000'0000'0000} << 64);
  EXPECT_EQ(MathUtil::Round<int128_t>(-std::pow(2.0, 127.0)),
            -int128_t{0x8000'0000'0000'0000} << 64);
  EXPECT_EQ(MathUtil::Round<int128_t>(-std::pow(2.0l, 127.0l)),
            -int128_t{0x8000'0000'0000'0000} << 64);
}

TEST(MathUtil, RoundToInt128TooSmall) {
#if UNDEFINED_BEHAVIOR_SANITIZER
  GTEST_SKIP() << "Skipping test because it purposely invokes UB and ubsan "
               << "is enabled.";
#elif GTEST_HAS_DEATH_TEST
  EXPECT_DEBUG_DEATH(
      MathUtil::Round<int128_t>(std::nextafter(
          -std::pow(2.0f, 127.0f), -std::numeric_limits<float>::infinity())),
      R"re(kInclusiveLowerBound <= rounded)re");
  EXPECT_DEBUG_DEATH(
      MathUtil::Round<int128_t>(std::nextafter(
          -std::pow(2.0, 127.0), -std::numeric_limits<double>::infinity())),
      R"re(kInclusiveLowerBound <= rounded)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<int128_t>(std::nextafter(
                         -std::pow(2.0l, 127.0l),
                         -std::numeric_limits<long double>::infinity())),
                     R"re(kInclusiveLowerBound <= rounded)re");
#else   // GTEST_HAS_DEATH_TEST
  GTEST_SKIP() << "Skipping death test because it is unsupported by googletest";
#endif  // GTEST_HAS_DEATH_TEST
}

TEST(MathUtil, RoundToUInt128BelowUpperBound) {
  // The largest float fits in a `uint128_t`.
  EXPECT_EQ(MathUtil::Round<uint128_t>(std::numeric_limits<float>::max()),
            int128_t{0xFFFF'FF00'0000'0000} << 64);
  EXPECT_EQ(
      MathUtil::Round<uint128_t>(std::nextafter(std::pow(2.0, 128.0), 0.0)),
      int128_t{0xFFFF'FFFF'FFFF'F800} << 64);
  if constexpr (std::numeric_limits<long double>::digits == 113) {
    EXPECT_EQ(MathUtil::Round<uint128_t>(
                  std::nextafter(std::pow(2.0l, 128.0l), 0.0l)),
              int128_t{0xFFFF'FFFF'FFFF'FFFF} << 64 | 0xFFFF'FFFF'FFFF'8000);
  } else {
    ASSERT_EQ(64, std::numeric_limits<long double>::digits);
    EXPECT_EQ(MathUtil::Round<uint128_t>(
                  std::nextafter(std::pow(2.0l, 128.0l), 0.0l)),
              int128_t{0xFFFF'FFFF'FFFF'FFFF} << 64);
  }
}

TEST(MathUtil, RoundToUInt128TooLarge) {
#if UNDEFINED_BEHAVIOR_SANITIZER
  GTEST_SKIP() << "Skipping test because it purposely invokes UB and ubsan "
               << "is enabled.";
#elif GTEST_HAS_DEATH_TEST
  // There is no "too large" test for `float` -> `uint128_t`, since the
  // largest float fits.
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint128_t>(std::pow(2.0, 128.0)),
                     R"re(rounded < kExclusiveUpperBound)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint128_t>(std::pow(2.0l, 128.0l)),
                     R"re(rounded < kExclusiveUpperBound)re");
#else   // GTEST_HAS_DEATH_TEST
  GTEST_SKIP() << "Skipping death test because it is unsupported by googletest";
#endif  // GTEST_HAS_DEATH_TEST
}

TEST(MathUtil, RoundToUInt128AboveLowerBound) {
  EXPECT_EQ(MathUtil::Round<uint128_t>(-0.4f), 0);
  EXPECT_EQ(MathUtil::Round<uint128_t>(-0.4), 0);
  EXPECT_EQ(MathUtil::Round<uint128_t>(-0.4l), 0);
}

TEST(MathUtil, RoundToUInt128TooSmall) {
#if UNDEFINED_BEHAVIOR_SANITIZER
  GTEST_SKIP() << "Skipping test because it purposely invokes UB and ubsan "
               << "is enabled.";
#elif GTEST_HAS_DEATH_TEST
  // `float` has a different error message.
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint128_t>(-0.6f),
                     R"re(rounded >= FloatIn{0})re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint128_t>(-0.6),
                     R"re(kInclusiveLowerBound <= rounded)re");
  EXPECT_DEBUG_DEATH(MathUtil::Round<uint128_t>(-0.6l),
                     R"re(kInclusiveLowerBound <= rounded)re");
#else   // GTEST_HAS_DEATH_TEST
  GTEST_SKIP() << "Skipping death test because it is unsupported by googletest";
#endif  // GTEST_HAS_DEATH_TEST
}

TEST(MathUtil, FastIntRoundDoubleMax) {
  // All `int32_t`s are exactly representable by `double`.
  EXPECT_EQ(MathUtil::Round<int32_t>(
                static_cast<double>(std::numeric_limits<int32_t>::max())),
            std::numeric_limits<int32_t>::max());
}

TEST(MathUtil, FastIntRoundDoubleMin) {
  EXPECT_EQ(MathUtil::Round<int32_t>(
                static_cast<double>(std::numeric_limits<int32_t>::lowest())),
            std::numeric_limits<int32_t>::lowest());
}

TEST(MathUtil, FastInt64RoundDoubleMax) {
#if GTEST_HAS_DEATH_TEST
  // `2^63 - 1` is not exactly representable as a double.  The nearest double
  // is larger, so it is not in-range and we will DCHECK-fail.
  EXPECT_DEBUG_DEATH(MathUtil::Round<int64_t>(static_cast<double>(
                         std::numeric_limits<int64_t>::max())),
                     R"re(rounded < kExclusiveUpperBound)re");
#else   // GTEST_HAS_DEATH_TEST
  GTEST_SKIP() << "Skipping death test because it is unsupported by googletest";
#endif  // GTEST_HAS_DEATH_TEST
}

TEST(MathUtil, FastInt64RoundDoubleBeforeMax) {
  // Test with the value before the one `2^63 - 1` converts to.  This has 53
  // bits set (so not the low 10 bits).
  EXPECT_EQ(MathUtil::Round<int64_t>(std::nextafter(
                static_cast<double>(std::numeric_limits<int64_t>::max()), 0.0)),
            0x7fff'ffff'ffff'fc00);
}

TEST(MathUtil, FastInt64RoundDoubleMin) {
  EXPECT_EQ(MathUtil::Round<int64_t>(
                static_cast<double>(std::numeric_limits<int64_t>::lowest())),
            std::numeric_limits<int64_t>::lowest());
}

TEST(MathUtil, FastIntRoundFloatMax) {
#if GTEST_HAS_DEATH_TEST
  // `2^32 - 1` is not exactly representable as a float.  As in
  // `FastInt64RoundDoubleMax` above, we will DCHECK-fail.
  EXPECT_DEBUG_DEATH(MathUtil::Round<int32_t>(static_cast<float>(
                         std::numeric_limits<int32_t>::max())),
                     R"re(rounded < kExclusiveUpperBound)re");
#else   // GTEST_HAS_DEATH_TEST
  GTEST_SKIP() << "Skipping death test because it is unsupported by googletest";
#endif  // GTEST_HAS_DEATH_TEST
}

TEST(MathUtil, FastIntRoundFloatMin) {
  EXPECT_EQ(MathUtil::Round<int32_t>(
                static_cast<float>(std::numeric_limits<int32_t>::lowest())),
            std::numeric_limits<int32_t>::lowest());
}

TEST(MathUtil, FastInt64RoundFloatMax) {
#if GTEST_HAS_DEATH_TEST
  EXPECT_DEBUG_DEATH(MathUtil::Round<int64_t>(double{static_cast<float>(
                         std::numeric_limits<int64_t>::max())}),
                     R"re(rounded < kExclusiveUpperBound)re");
#else   // GTEST_HAS_DEATH_TEST
  GTEST_SKIP() << "Skipping death test because it is unsupported by googletest";
#endif  // GTEST_HAS_DEATH_TEST
}

TEST(MathUtil, FastInt64RoundFloatBeforeMax) {
  EXPECT_EQ(MathUtil::Round<int64_t>(double{
                std::nextafterf(std::numeric_limits<int64_t>::max(), 0.0f)}),
            0x7fff'ff80'0000'0000);
}

TEST(MathUtil, FastInt64RoundFloatMin) {
  EXPECT_EQ(MathUtil::Round<int64_t>(double{
                static_cast<float>(std::numeric_limits<int64_t>::lowest())}),
            std::numeric_limits<int64_t>::lowest());
}

TEST(MathUtil, FastIntRoundDoubleOverflow) {
#if GTEST_HAS_DEATH_TEST
  EXPECT_DEBUG_DEATH(MathUtil::Round<int32_t>(static_cast<double>(
                         int64_t{std::numeric_limits<int32_t>::max()} + 99999)),
                     R"re(rounded < kExclusiveUpperBound)re");
  EXPECT_DEBUG_DEATH(
      MathUtil::Round<int32_t>(static_cast<double>(
          int64_t{std::numeric_limits<int32_t>::lowest()} - 99999)),
      R"re(kInclusiveLowerBound <= rounded)re");
#else   // GTEST_HAS_DEATH_TEST
  GTEST_SKIP() << "Skipping death test because it is unsupported by googletest";
#endif  // GTEST_HAS_DEATH_TEST
}

TEST(MathUtil, RoundDownTo) {
  EXPECT_EQ(MathUtil::RoundDownTo<int64_t>(10, 1), 10);
  EXPECT_EQ(MathUtil::RoundDownTo<int64_t>(10, 2), 10);
  EXPECT_EQ(MathUtil::RoundDownTo<int64_t>(10, 5), 10);
  EXPECT_EQ(MathUtil::RoundDownTo<int64_t>(10, 10), 10);
  EXPECT_EQ(MathUtil::RoundDownTo<int64_t>(10, 3), 9);
  EXPECT_EQ(MathUtil::RoundDownTo<int32_t>(9, 10), 0);
  EXPECT_EQ(MathUtil::RoundDownTo<int32_t>(11, 10), 10);
  EXPECT_EQ(MathUtil::RoundDownTo<int32_t>(0, 10), 0);
  EXPECT_EQ(MathUtil::RoundDownTo<int64_t>(10000000001, 10000000000),
            10000000000);
  EXPECT_EQ(MathUtil::RoundDownTo<int64_t>(9999999999, 10000000000), 0);
  EXPECT_EQ(MathUtil::RoundDownTo<int64_t>(1, 10000000000), 0);

#if GTEST_HAS_DEATH_TEST
#ifndef NDEBUG
  EXPECT_DEATH(MathUtil::RoundDownTo<int32_t>(10, 0),
               "rounding_value > IntType\\(0\\)");
  EXPECT_DEATH(MathUtil::RoundDownTo<int32_t>(10, -1),
               "rounding_value > IntType\\(0\\)");
  EXPECT_DEATH(MathUtil::RoundDownTo<int64_t>(-10, 10),
               "input_value >= IntType\\(0\\)");
#endif  // NDEBUG
#endif  // GTEST_HAS_DEATH_TEST
}

TEST(MathUtil, RoundUpTo) {
  EXPECT_EQ(MathUtil::RoundUpTo<int64_t>(10, 1), 10);
  EXPECT_EQ(MathUtil::RoundUpTo<int64_t>(10, 2), 10);
  EXPECT_EQ(MathUtil::RoundUpTo<int64_t>(10, 5), 10);
  EXPECT_EQ(MathUtil::RoundUpTo<int64_t>(10, 10), 10);
  EXPECT_EQ(MathUtil::RoundUpTo<int64_t>(10, 3), 12);
  EXPECT_EQ(MathUtil::RoundUpTo<int32_t>(9, 10), 10);
  EXPECT_EQ(MathUtil::RoundUpTo<int32_t>(11, 10), 20);
  EXPECT_EQ(MathUtil::RoundUpTo<int32_t>(0, 10), 0);
  EXPECT_EQ(MathUtil::RoundUpTo<int64_t>(10000000001, 10000000000),
            20000000000);
  EXPECT_EQ(MathUtil::RoundUpTo<int64_t>(9999999999, 10000000000), 10000000000);
  EXPECT_EQ(MathUtil::RoundUpTo<int64_t>(1, 10000000000), 10000000000);

  constexpr int64_t maxint64 = std::numeric_limits<int64_t>::max();
  EXPECT_EQ(MathUtil::RoundUpTo<int64_t>(maxint64, 1), maxint64);
  EXPECT_EQ(MathUtil::RoundUpTo<int64_t>(maxint64, maxint64), maxint64);
  EXPECT_EQ(MathUtil::RoundUpTo<int64_t>(maxint64, 7), maxint64);
  EXPECT_EQ(MathUtil::RoundUpTo<int64_t>(maxint64, 649657), maxint64);

  constexpr int64_t premaxint64 = maxint64 - 1;
  EXPECT_EQ(MathUtil::RoundUpTo<int64_t>(premaxint64, 1), premaxint64);
  EXPECT_EQ(MathUtil::RoundUpTo<int64_t>(premaxint64, 2), premaxint64);
  EXPECT_EQ(MathUtil::RoundUpTo<int64_t>(premaxint64, 3), premaxint64);
  EXPECT_EQ(MathUtil::RoundUpTo<int64_t>(premaxint64, premaxint64),
            premaxint64);

#if GTEST_HAS_DEATH_TEST
#ifndef NDEBUG
  EXPECT_DEATH(MathUtil::RoundUpTo<int32_t>(10, 0),
               "rounding_value > IntType\\(0\\)");
  EXPECT_DEATH(MathUtil::RoundUpTo<int32_t>(10, -1),
               "rounding_value > IntType\\(0\\)");
  EXPECT_DEATH(MathUtil::RoundUpTo<int64_t>(-10, 10),
               "input_value >= IntType\\(0\\)");
#endif  // NDEBUG
#endif  // GTEST_HAS_DEATH_TEST
}

void BM_RoundUpTo(benchmark::State& state) {
  int64_t x = 100, y = 1;
  for (auto s : state) {
    benchmark::DoNotOptimize(MathUtil::RoundUpTo<int64_t>(x, y));
    x += 1;
    y += 1;
  }
}
BENCHMARK(BM_RoundUpTo);

void BM_IntCast(benchmark::State& state) {
  double x = 0.1;
  while (state.KeepRunningBatch(5)) {
    benchmark::DoNotOptimize(static_cast<int>(x));
    x += 0.1;
    benchmark::DoNotOptimize(static_cast<int>(x));
    x += 0.1;
    benchmark::DoNotOptimize(static_cast<int>(x));
    x += 0.1;
    benchmark::DoNotOptimize(static_cast<int>(x));
    x += 0.1;
    benchmark::DoNotOptimize(static_cast<int>(x));
    x += 0.1;
  }
}
BENCHMARK(BM_IntCast);

void BM_IntCastLatency(benchmark::State& state) {
  // The initial value cannot round to 0, or the optimizer will know `x`
  // won't change.
  double x = 10.1;
  while (state.KeepRunningBatch(5)) {
    x = std::bit_cast<double>(static_cast<int64_t>(static_cast<int>(x)));
    x = std::bit_cast<double>(static_cast<int64_t>(static_cast<int>(x)));
    x = std::bit_cast<double>(static_cast<int64_t>(static_cast<int>(x)));
    x = std::bit_cast<double>(static_cast<int64_t>(static_cast<int>(x)));
    x = std::bit_cast<double>(static_cast<int64_t>(static_cast<int>(x)));
  }
  benchmark::DoNotOptimize(x);
}
BENCHMARK(BM_IntCastLatency);

void BM_Int64Cast(benchmark::State& state) {
  double x = 0.1;
  while (state.KeepRunningBatch(5)) {
    benchmark::DoNotOptimize(static_cast<int64_t>(x));
    x += 0.1;
    benchmark::DoNotOptimize(static_cast<int64_t>(x));
    x += 0.1;
    benchmark::DoNotOptimize(static_cast<int64_t>(x));
    x += 0.1;
    benchmark::DoNotOptimize(static_cast<int64_t>(x));
    x += 0.1;
    benchmark::DoNotOptimize(static_cast<int64_t>(x));
    x += 0.1;
  }
}
BENCHMARK(BM_Int64Cast);

void BM_Int64CastLatency(benchmark::State& state) {
  double x = 10.1;
  while (state.KeepRunningBatch(5)) {
    x = std::bit_cast<double>(static_cast<int64_t>(x));
    x = std::bit_cast<double>(static_cast<int64_t>(x));
    x = std::bit_cast<double>(static_cast<int64_t>(x));
    x = std::bit_cast<double>(static_cast<int64_t>(x));
    x = std::bit_cast<double>(static_cast<int64_t>(x));
  }
  benchmark::DoNotOptimize(x);
}
BENCHMARK(BM_Int64CastLatency);

void BM_StdLLRound(benchmark::State& state) {
  // `long` is 32-bit on 32-bit platforms, so use `long long` / `llround`.
  static_assert(sizeof(long long) == sizeof(int64_t));  // NOLINT(runtime/int)
  double x = 0.1;
  while (state.KeepRunningBatch(5)) {
    benchmark::DoNotOptimize(std::llround(x));
    x += 0.1;
    benchmark::DoNotOptimize(std::llround(x));
    x += 0.1;
    benchmark::DoNotOptimize(std::llround(x));
    x += 0.1;
    benchmark::DoNotOptimize(std::llround(x));
    x += 0.1;
    benchmark::DoNotOptimize(std::llround(x));
    x += 0.1;
  }
  benchmark::DoNotOptimize(x);
}
BENCHMARK(BM_StdLLRound);

void BM_StdLLRoundLatency(benchmark::State& state) {
  double x = 10.1;
  while (state.KeepRunningBatch(5)) {
    x = std::bit_cast<double>(std::llround(x));
    x = std::bit_cast<double>(std::llround(x));
    x = std::bit_cast<double>(std::llround(x));
    x = std::bit_cast<double>(std::llround(x));
    x = std::bit_cast<double>(std::llround(x));
  }
  benchmark::DoNotOptimize(x);
}
BENCHMARK(BM_StdLLRoundLatency);

void BM_IntRound(benchmark::State& state) {
  double x = 0.1;
  while (state.KeepRunningBatch(5)) {
    benchmark::DoNotOptimize(MathUtil::Round<int>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::Round<int>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::Round<int>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::Round<int>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::Round<int>(x));
    x += 0.1;
  }
}
BENCHMARK(BM_IntRound);

void BM_IntRoundLatency(benchmark::State& state) {
  double x = 10.1;
  while (state.KeepRunningBatch(5)) {
    x = std::bit_cast<double>(static_cast<int64_t>(MathUtil::Round<int>(x)));
    x = std::bit_cast<double>(static_cast<int64_t>(MathUtil::Round<int>(x)));
    x = std::bit_cast<double>(static_cast<int64_t>(MathUtil::Round<int>(x)));
    x = std::bit_cast<double>(static_cast<int64_t>(MathUtil::Round<int>(x)));
    x = std::bit_cast<double>(static_cast<int64_t>(MathUtil::Round<int>(x)));
  }
  benchmark::DoNotOptimize(x);
}
BENCHMARK(BM_IntRoundLatency);

void BM_FastIntRound(benchmark::State& state) {
  double x = 0.1;
  while (state.KeepRunningBatch(5)) {
    benchmark::DoNotOptimize(MathUtil::Round<int32_t>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::Round<int32_t>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::Round<int32_t>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::Round<int32_t>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::Round<int32_t>(x));
    x += 0.1;
  }
}
BENCHMARK(BM_FastIntRound);

void BM_FastIntRoundLatency(benchmark::State& state) {
  double x = 10.1;
  while (state.KeepRunningBatch(5)) {
    x = std::bit_cast<double>(
        static_cast<int64_t>(MathUtil::Round<int32_t>(x)));
    x = std::bit_cast<double>(
        static_cast<int64_t>(MathUtil::Round<int32_t>(x)));
    x = std::bit_cast<double>(
        static_cast<int64_t>(MathUtil::Round<int32_t>(x)));
    x = std::bit_cast<double>(
        static_cast<int64_t>(MathUtil::Round<int32_t>(x)));
    x = std::bit_cast<double>(
        static_cast<int64_t>(MathUtil::Round<int32_t>(x)));
  }
  benchmark::DoNotOptimize(x);
}
BENCHMARK(BM_FastIntRoundLatency);

void BM_Int64Round(benchmark::State& state) {
  double x = 0.1;
  while (state.KeepRunningBatch(5)) {
    benchmark::DoNotOptimize(MathUtil::Round<int64_t>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::Round<int64_t>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::Round<int64_t>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::Round<int64_t>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::Round<int64_t>(x));
    x += 0.1;
  }
}
BENCHMARK(BM_Int64Round);

void BM_Int64RoundLatency(benchmark::State& state) {
  double x = 10.1;
  while (state.KeepRunningBatch(5)) {
    x = std::bit_cast<double>(MathUtil::Round<int64_t>(x));
    x = std::bit_cast<double>(MathUtil::Round<int64_t>(x));
    x = std::bit_cast<double>(MathUtil::Round<int64_t>(x));
    x = std::bit_cast<double>(MathUtil::Round<int64_t>(x));
    x = std::bit_cast<double>(MathUtil::Round<int64_t>(x));
  }
  benchmark::DoNotOptimize(x);
}
BENCHMARK(BM_Int64RoundLatency);

void BM_UintRound(benchmark::State& state) {
  double x = 0.1;
  while (state.KeepRunningBatch(5)) {
    benchmark::DoNotOptimize(MathUtil::Round<uint32_t>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::Round<uint32_t>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::Round<uint32_t>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::Round<uint32_t>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::Round<uint32_t>(x));
    x += 0.1;
  }
}
BENCHMARK(BM_UintRound);

void BM_UintRoundLatency(benchmark::State& state) {
  double x = 10.1;
  while (state.KeepRunningBatch(5)) {
    x = std::bit_cast<double>(
        static_cast<uint64_t>(MathUtil::Round<uint32_t>(x)));
    x = std::bit_cast<double>(
        static_cast<uint64_t>(MathUtil::Round<uint32_t>(x)));
    x = std::bit_cast<double>(
        static_cast<uint64_t>(MathUtil::Round<uint32_t>(x)));
    x = std::bit_cast<double>(
        static_cast<uint64_t>(MathUtil::Round<uint32_t>(x)));
    x = std::bit_cast<double>(
        static_cast<uint64_t>(MathUtil::Round<uint32_t>(x)));
  }
  benchmark::DoNotOptimize(x);
}
BENCHMARK(BM_UintRoundLatency);

void BM_SafeIntCast(benchmark::State& state) {
  double x = 0.1;
  while (state.KeepRunningBatch(5)) {
    benchmark::DoNotOptimize(MathUtil::SafeCast<int>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::SafeCast<int>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::SafeCast<int>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::SafeCast<int>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::SafeCast<int>(x));
    x += 0.1;
  }
}
BENCHMARK(BM_SafeIntCast);

void BM_SafeIntCastLatency(benchmark::State& state) {
  // The "Safe" functions are branchy.  The first call returns 10, which is
  // bit-casted back to a small value.  This returns 0 on the next cast, but
  // the optimizer (currently as of 2024-06) does not know this.  We are
  // benchmarking a normal (not overflow/NaN) code path.
  double x = 10.1;
  while (state.KeepRunningBatch(5)) {
    x = std::bit_cast<double>(static_cast<int64_t>(MathUtil::SafeCast<int>(x)));
    x = std::bit_cast<double>(static_cast<int64_t>(MathUtil::SafeCast<int>(x)));
    x = std::bit_cast<double>(static_cast<int64_t>(MathUtil::SafeCast<int>(x)));
    x = std::bit_cast<double>(static_cast<int64_t>(MathUtil::SafeCast<int>(x)));
    x = std::bit_cast<double>(static_cast<int64_t>(MathUtil::SafeCast<int>(x)));
  }
  benchmark::DoNotOptimize(x);
}
BENCHMARK(BM_SafeIntCastLatency);

void BM_SafeInt64Cast(benchmark::State& state) {
  double x = 0.1;
  while (state.KeepRunningBatch(5)) {
    benchmark::DoNotOptimize(MathUtil::SafeCast<int64_t>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::SafeCast<int64_t>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::SafeCast<int64_t>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::SafeCast<int64_t>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::SafeCast<int64_t>(x));
    x += 0.1;
  }
}
BENCHMARK(BM_SafeInt64Cast);

void BM_SafeInt64CastLatency(benchmark::State& state) {
  double x = 10.1;
  while (state.KeepRunningBatch(5)) {
    x = std::bit_cast<double>(MathUtil::SafeCast<int64_t>(x));
    x = std::bit_cast<double>(MathUtil::SafeCast<int64_t>(x));
    x = std::bit_cast<double>(MathUtil::SafeCast<int64_t>(x));
    x = std::bit_cast<double>(MathUtil::SafeCast<int64_t>(x));
    x = std::bit_cast<double>(MathUtil::SafeCast<int64_t>(x));
  }
  benchmark::DoNotOptimize(x);
}
BENCHMARK(BM_SafeInt64CastLatency);

void BM_SafeIntRound(benchmark::State& state) {
  double x = 0.1;
  while (state.KeepRunningBatch(5)) {
    benchmark::DoNotOptimize(MathUtil::SafeRound<int>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::SafeRound<int>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::SafeRound<int>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::SafeRound<int>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::SafeRound<int>(x));
    x += 0.1;
  }
}
BENCHMARK(BM_SafeIntRound);

void BM_SafeIntRoundLatency(benchmark::State& state) {
  double x = 10.1;
  while (state.KeepRunningBatch(5)) {
    x = std::bit_cast<double>(
        static_cast<int64_t>(MathUtil::SafeRound<int>(x)));
    x = std::bit_cast<double>(
        static_cast<int64_t>(MathUtil::SafeRound<int>(x)));
    x = std::bit_cast<double>(
        static_cast<int64_t>(MathUtil::SafeRound<int>(x)));
    x = std::bit_cast<double>(
        static_cast<int64_t>(MathUtil::SafeRound<int>(x)));
    x = std::bit_cast<double>(
        static_cast<int64_t>(MathUtil::SafeRound<int>(x)));
  }
  benchmark::DoNotOptimize(x);
}
BENCHMARK(BM_SafeIntRoundLatency);

void BM_SafeInt64Round(benchmark::State& state) {
  double x = 0.1;
  while (state.KeepRunningBatch(5)) {
    benchmark::DoNotOptimize(MathUtil::SafeRound<int64_t>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::SafeRound<int64_t>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::SafeRound<int64_t>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::SafeRound<int64_t>(x));
    x += 0.1;
    benchmark::DoNotOptimize(MathUtil::SafeRound<int64_t>(x));
    x += 0.1;
  }
}
BENCHMARK(BM_SafeInt64Round);

void BM_SafeInt64RoundLatency(benchmark::State& state) {
  double x = 10.1;
  while (state.KeepRunningBatch(5)) {
    x = std::bit_cast<double>(MathUtil::SafeRound<int64_t>(x));
    x = std::bit_cast<double>(MathUtil::SafeRound<int64_t>(x));
    x = std::bit_cast<double>(MathUtil::SafeRound<int64_t>(x));
    x = std::bit_cast<double>(MathUtil::SafeRound<int64_t>(x));
    x = std::bit_cast<double>(MathUtil::SafeRound<int64_t>(x));
  }
  benchmark::DoNotOptimize(x);
}
BENCHMARK(BM_SafeInt64RoundLatency);

TEST(MathUtil, IntRound) {
  EXPECT_EQ(MathUtil::Round<int>(0.0), 0);
  EXPECT_EQ(MathUtil::Round<int>(0.49), 0);
  EXPECT_EQ(MathUtil::Round<int>(1.49), 1);
  EXPECT_EQ(MathUtil::Round<int>(-0.49), 0);
  EXPECT_EQ(MathUtil::Round<int>(-1.49), -1);

  // Either adjacent integer is an acceptable result.
  EXPECT_EQ(fabs(MathUtil::Round<int>(0.5) - 0.5), 0.5);
  EXPECT_EQ(fabs(MathUtil::Round<int>(1.5) - 1.5), 0.5);
  EXPECT_EQ(fabs(MathUtil::Round<int>(-0.5) + 0.5), 0.5);
  EXPECT_EQ(fabs(MathUtil::Round<int>(-1.5) + 1.5), 0.5);

  EXPECT_EQ(MathUtil::Round<int>(static_cast<double>(0x76543210)), 0x76543210);

  // A double-precision number has a 53-bit mantissa (52 fraction bits),
  // so the following value can be represented exactly.
  int64_t value64 = uint64_t{0x1234567890abcd00};
  EXPECT_EQ(MathUtil::Round<int64_t>(static_cast<double>(value64)), value64);
}

template <class F>
F NextAfter(F x, F y);

template <>
float NextAfter(float x, float y) {
  return nextafterf(x, y);
}

template <>
double NextAfter(double x, double y) {
  return nextafter(x, y);
}

template <class FloatIn, class IntOut>
class SafeCastTester {
 public:
  static void Run() {
    constexpr IntOut imax = std::numeric_limits<IntOut>::max();
    static_assert(imax > 0);
    constexpr IntOut imin = std::numeric_limits<IntOut>::min();
    constexpr bool s = std::numeric_limits<IntOut>::is_signed;
    if constexpr (s) {
      static_assert(imin < 0);
    } else {
      static_assert(imin == 0);
    }

    // Some basic tests.
    EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(0.0)), 0);
    EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(-0.0)), 0);
    EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(0.99)), 0);
    EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(1.0)), 1);
    EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(1.01)), 1);
    EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(1.99)), 1);
    EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(2.0)), 2);
    EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(2.01)), 2);
    EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(-0.99)), 0);
    EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(-1.0)),
              s ? -1 : 0);
    EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(-1.01)),
              s ? -1 : 0);
    EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(-1.99)),
              s ? -1 : 0);
    EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(-2.0)),
              s ? -2 : 0);
    EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(-2.01)),
              s ? -2 : 0);
    EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(117.9)), 117);
    EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(118.0)), 118);
    EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(118.1)), 118);
    EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(-117.9)),
              s ? -117 : 0);
    EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(-118.0)),
              s ? -118 : 0);
    EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(-118.1)),
              s ? -118 : 0);

    // Some edge cases.
    // We hard-code whether we expect all finite value to fit in `IntOut`.
    // Otherwise, we'd be duplicating the logic to determine this from
    // `SafeCast`.  Additional types such as `_Float16` should be added here
    // if needed.
    constexpr bool kAllFiniteValuesFit =
        std::is_same_v<FloatIn, float> && std::is_same_v<IntOut, uint128_t>;
    if constexpr (kAllFiniteValuesFit) {
      // Compare both as floats and ints.
      EXPECT_EQ(static_cast<FloatIn>(MathUtil::SafeCast<IntOut>(
                    std::numeric_limits<FloatIn>::max())),
                std::numeric_limits<FloatIn>::max());
      // The `static_cast` will be a ubsan error and probably an illegal
      // instruction if `max()` does not actually fit in `IntOut`.
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(std::numeric_limits<FloatIn>::max()),
                static_cast<IntOut>(std::numeric_limits<FloatIn>::max()));
    } else {
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(std::numeric_limits<FloatIn>::max()),
                imax);
    }
    EXPECT_EQ(
        MathUtil::SafeCast<IntOut>(std::numeric_limits<FloatIn>::lowest()),
        imin);
    EXPECT_EQ(
        MathUtil::SafeCast<IntOut>(std::numeric_limits<FloatIn>::infinity()),
        imax);
    EXPECT_EQ(
        MathUtil::SafeCast<IntOut>(-std::numeric_limits<FloatIn>::infinity()),
        imin);
    EXPECT_EQ(
        MathUtil::SafeCast<IntOut>(std::numeric_limits<FloatIn>::quiet_NaN()),
        0);

    // Some larger numbers.
    if (sizeof(IntOut) >= 4 && sizeof(FloatIn) >= 8) {
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(0x76543210)),
                0x76543210);
    }

    if (sizeof(FloatIn) >= 8) {
      // A double-precision number has a 53-bit mantissa (52 fraction bits),
      // so the following value can be represented exactly by a double.
      int64_t value64 = uint64_t{0x1234567890abcd00};
      const IntOut expected =
          (sizeof(IntOut) >= 8) ? static_cast<IntOut>(value64) : imax;
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(value64)),
                expected);
    }

    // Check values near imin and imax
    static const int kLoopCount = 10;

    // If all finite values fit in `IntOut`, there `imax` is larger than the
    // largest finite `FloatIn`, so there is nothing to test.
    if constexpr (!kAllFiniteValuesFit) {
      // Values greater than or equal to imax should convert to imax.
      FloatIn v = static_cast<FloatIn>(imax);
      for (int i = 0; i < kLoopCount; i++) {
        EXPECT_EQ(MathUtil::SafeCast<IntOut>(v), imax);
        EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(v + 10000.)),
                  imax);
        v = NextAfter(v, std::numeric_limits<FloatIn>::max());
      }
    }

    {
      // Values less than or equal to imin should convert to imin
      FloatIn v = static_cast<FloatIn>(imin);
      for (int i = 0; i < kLoopCount; i++) {
        EXPECT_EQ(MathUtil::SafeCast<IntOut>(v), imin);
        EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(v - 10000.)),
                  imin);
        v = NextAfter(v, std::numeric_limits<FloatIn>::lowest());
      }
    }

    {
      // Values slightly less than imax which can be exactly represented as a
      // FloatIn should convert exactly to themselves.
      IntOut v = imax;
      for (int i = 0; i < kLoopCount; i++) {
        v = MathUtil::Min<IntOut>(
            v - 1, NextAfter(static_cast<FloatIn>(v),
                             std::numeric_limits<FloatIn>::lowest()));
        EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(v)), v);
      }
    }

    {
      // Values slightly greater than imin which can be exactly represented as a
      // FloatIn should convert exactly to themselves.
      IntOut v = imin;
      for (int i = 0; i < kLoopCount; i++) {
        v = MathUtil::Max<IntOut>(
            v + 1, NextAfter(static_cast<FloatIn>(v),
                             std::numeric_limits<FloatIn>::max()));
        EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(v)), v);
      }
    }

    // When FloatIn is wider than IntOut, we can test that fractional conversion
    // near imax works as expected.
    if (sizeof(FloatIn) > sizeof(IntOut)) {
      {
        // Values slightly less than imax should convert to imax - 1
        FloatIn v = static_cast<FloatIn>(imax);
        for (int i = 0; i < kLoopCount; i++) {
          v = NextAfter(static_cast<FloatIn>(v),
                        std::numeric_limits<FloatIn>::lowest());
          EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(v)),
                    imax - 1);
        }
      }
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imax) + 0.1)),
                imax);
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imax) + 0.99)),
                imax);
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imax) + 1.0)),
                imax);
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imax) + 1.99)),
                imax);
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imax) + 2.0)),
                imax);
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imax) - 0.1)),
                imax - 1);
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imax) - 0.99)),
                imax - 1);
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imax) - 1.0)),
                imax - 1);
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imax) - 1.01)),
                imax - 2);
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imax) - 1.99)),
                imax - 2);
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imax) - 2.0)),
                imax - 2);
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imax) - 2.01)),
                imax - 3);
    }
    // When FloatIn is wider than IntOut, and IntOut is signed, we can test
    // that fractional conversion near imin works as expected.
    if (s && (sizeof(FloatIn) > sizeof(IntOut))) {
      {
        // Values just over imin should convert to imin + 1
        FloatIn v = static_cast<FloatIn>(imin);
        for (int i = 0; i < kLoopCount; i++) {
          v = NextAfter(static_cast<FloatIn>(v),
                        std::numeric_limits<FloatIn>::max());
          EXPECT_EQ(MathUtil::SafeCast<IntOut>(static_cast<FloatIn>(v)),
                    imin + 1);
        }
      }
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imin) - 0.1)),
                imin);
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imin) - 0.99)),
                imin);
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imin) - 1.0)),
                imin);
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imin) - 0.99)),
                imin);
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imin) - 2.0)),
                imin);
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imin) + 0.1)),
                imin + 1);
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imin) + 0.99)),
                imin + 1);
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imin) + 1.0)),
                imin + 1);
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imin) + 1.01)),
                imin + 2);
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imin) + 1.99)),
                imin + 2);
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imin) + 2.0)),
                imin + 2);
      EXPECT_EQ(MathUtil::SafeCast<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imin) + 2.01)),
                imin + 3);
    }
  }
};

TEST(MathUtil, SafeCast) {
  SafeCastTester<float, int8_t>::Run();
  SafeCastTester<double, int8_t>::Run();
  SafeCastTester<float, int16_t>::Run();
  SafeCastTester<double, int16_t>::Run();
  SafeCastTester<float, int32_t>::Run();
  SafeCastTester<double, int32_t>::Run();
  SafeCastTester<float, int64_t>::Run();
  SafeCastTester<double, int64_t>::Run();
#ifndef GLOOP_UNSUPPORTED_LIBSTDCXX  // std::isnan<int128> is ambiguous
  // As of 2024-06, `absl::int128` / `absl::uint128` cannot be used here due
  // to missing `std::isnan` overload, lack of implicit conversions, and
  // possibly more problems.
  SafeCastTester<float, int128_t>::Run();
  SafeCastTester<double, int128_t>::Run();
#endif  // GLOOP_UNSUPPORTED_LIBSTDCXX

  SafeCastTester<float, uint8_t>::Run();
  SafeCastTester<double, uint8_t>::Run();
  SafeCastTester<float, uint16_t>::Run();
  SafeCastTester<double, uint16_t>::Run();
  SafeCastTester<float, uint32_t>::Run();
  SafeCastTester<double, uint32_t>::Run();
  SafeCastTester<float, uint64_t>::Run();
  SafeCastTester<double, uint64_t>::Run();
#ifndef GLOOP_UNSUPPORTED_LIBSTDCXX  // std::isnan<int128> is ambiguous
  SafeCastTester<float, uint128_t>::Run();
  SafeCastTester<double, uint128_t>::Run();
#endif  // GLOOP_UNSUPPORTED_LIBSTDCXX

  // Spot-check SafeCast<int>
  EXPECT_EQ(MathUtil::SafeCast<int>(static_cast<float>(12345.678)), 12345);
  EXPECT_EQ(MathUtil::SafeCast<int>(static_cast<float>(12345.4321)), 12345);
  EXPECT_EQ(MathUtil::SafeCast<int>(static_cast<double>(-12345.678)), -12345);
  EXPECT_EQ(MathUtil::SafeCast<int>(static_cast<double>(-12345.4321)), -12345);
  EXPECT_EQ(MathUtil::SafeCast<int>(1E47), 2147483647);
  EXPECT_EQ(MathUtil::SafeCast<int>(-1E47), int64_t{-2147483648});
}

template <class FloatIn, class IntOut>
class SafeRoundTester {
 public:
  static void Run() {
    constexpr IntOut imax = std::numeric_limits<IntOut>::max();
    static_assert(imax > 0);
    constexpr IntOut imin = std::numeric_limits<IntOut>::min();
    constexpr bool s = std::numeric_limits<IntOut>::is_signed;
    if constexpr (s) {
      static_assert(imin < 0);
    } else {
      static_assert(imin == 0);
    }

    // Some basic tests.
    EXPECT_EQ(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(0.0)), 0);
    EXPECT_EQ(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(-0.0)), 0);
    EXPECT_EQ(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(0.49)), 0);
    EXPECT_EQ(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(0.51)), 1);
    EXPECT_EQ(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(1.49)), 1);
    EXPECT_EQ(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(1.51)), 2);
    EXPECT_EQ(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(-0.49)), 0);
    EXPECT_EQ(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(-0.51)),
              s ? -1 : 0);
    EXPECT_EQ(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(-1.49)),
              s ? -1 : 0);
    EXPECT_EQ(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(-1.51)),
              s ? -2 : 0);
    EXPECT_EQ(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(117.4)), 117);
    EXPECT_EQ(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(117.6)), 118);
    EXPECT_EQ(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(-117.4)),
              s ? -117 : 0);
    EXPECT_EQ(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(-117.6)),
              s ? -118 : 0);

    // At the midpoint between ints, either adjacent int is an acceptable
    // result.
    EXPECT_EQ(
        fabs(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(0.5)) - 0.5),
        0.5);
    EXPECT_EQ(
        fabs(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(1.5)) - 1.5),
        0.5);
    EXPECT_EQ(
        fabs(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(117.5)) - 117.5),
        0.5);
    if (s) {
      EXPECT_EQ(
          fabs(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(-0.5)) + 0.5),
          0.5);
      EXPECT_EQ(
          fabs(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(-1.5)) + 1.5),
          0.5);
      EXPECT_EQ(fabs(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(-117.5)) +
                     117.5),
                0.5);
    } else {
      EXPECT_EQ(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(-0.5)), 0);
      EXPECT_EQ(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(-1.5)), 0);
      EXPECT_EQ(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(-117.5)), 0);
    }

    // Some edge cases.
    constexpr bool kAllFiniteValuesFit =
        std::is_same_v<FloatIn, float> && std::is_same_v<IntOut, uint128_t>;
    if constexpr (kAllFiniteValuesFit) {
      // Compare both as floats and ints.
      EXPECT_EQ(static_cast<FloatIn>(MathUtil::SafeRound<IntOut>(
                    std::numeric_limits<FloatIn>::max())),
                std::numeric_limits<FloatIn>::max());
      EXPECT_EQ(
          MathUtil::SafeRound<IntOut>(std::numeric_limits<FloatIn>::max()),
          static_cast<IntOut>(std::numeric_limits<FloatIn>::max()));
    } else {
      EXPECT_EQ(
          MathUtil::SafeRound<IntOut>(std::numeric_limits<FloatIn>::max()),
          imax);
    }
    EXPECT_EQ(
        MathUtil::SafeRound<IntOut>(std::numeric_limits<FloatIn>::lowest()),
        imin);
    EXPECT_EQ(
        MathUtil::SafeRound<IntOut>(std::numeric_limits<FloatIn>::infinity()),
        imax);
    EXPECT_EQ(
        MathUtil::SafeRound<IntOut>(-std::numeric_limits<FloatIn>::infinity()),
        imin);
    EXPECT_EQ(
        MathUtil::SafeRound<IntOut>(std::numeric_limits<FloatIn>::quiet_NaN()),
        0);
    EXPECT_EQ(MathUtil::SafeRound<IntOut>(std::nextafter(
                  FloatIn{0.5}, -std::numeric_limits<FloatIn>::infinity())),
              IntOut{0});

    // Some larger numbers.
    if (sizeof(IntOut) >= 4 && sizeof(FloatIn) >= 8) {
      EXPECT_EQ(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(0x76543210)),
                0x76543210);
    }

    if (sizeof(FloatIn) >= 8) {
      // A double-precision number has a 53-bit mantissa (52 fraction bits),
      // so the following value can be represented exactly by a double.
      int64_t value64 = uint64_t{0x1234567890abcd00};
      const IntOut expected =
          (sizeof(IntOut) >= 8) ? static_cast<IntOut>(value64) : imax;
      EXPECT_EQ(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(value64)),
                expected);
    }

    // Check values near imin and imax
    static const int kLoopCount = 10;

    if constexpr (!kAllFiniteValuesFit) {
      // Values greater than or equal to imax should round to imax
      FloatIn v = static_cast<FloatIn>(imax);
      for (int i = 0; i < kLoopCount; i++) {
        EXPECT_EQ(MathUtil::SafeRound<IntOut>(v), imax);
        EXPECT_EQ(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(v + 10000.)),
                  imax);
        v = NextAfter(v, std::numeric_limits<FloatIn>::max());
      }
    }

    {
      // Values less than or equal to imin should round to imin
      FloatIn v = static_cast<FloatIn>(imin);
      for (int i = 0; i < kLoopCount; i++) {
        EXPECT_EQ(MathUtil::SafeRound<IntOut>(v), imin);
        EXPECT_EQ(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(v - 10000.)),
                  imin);
        v = NextAfter(v, std::numeric_limits<FloatIn>::lowest());
      }
    }

    {
      // Values slightly less than imax which can be exactly represented as a
      // FloatIn should round exactly to themselves.
      IntOut v = imax;
      for (int i = 0; i < kLoopCount; i++) {
        v = MathUtil::Min<IntOut>(
            v - 1, NextAfter(static_cast<FloatIn>(v),
                             std::numeric_limits<FloatIn>::lowest()));
        EXPECT_EQ(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(v)), v);
      }
    }

    {
      // Values slightly greater than imin which can be exactly represented as a
      // FloatIn should round exactly to themselves.
      IntOut v = imin;
      for (int i = 0; i < kLoopCount; i++) {
        v = MathUtil::Max<IntOut>(
            v + 1, NextAfter(static_cast<FloatIn>(v),
                             std::numeric_limits<FloatIn>::max()));
        EXPECT_EQ(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(v)), v);
      }
    }

    // When FloatIn is wider than IntOut, we can test that fractional rounding
    // near imax works as expected.
    if (sizeof(FloatIn) > sizeof(IntOut)) {
      {
        // Values slightly less than imax should round to imax
        FloatIn v = static_cast<FloatIn>(imax);
        for (int i = 0; i < kLoopCount; i++) {
          v = NextAfter(static_cast<FloatIn>(v),
                        std::numeric_limits<FloatIn>::lowest());
          EXPECT_EQ(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(v)), imax);
        }
      }
      EXPECT_EQ(MathUtil::SafeRound<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imax) + 0.1)),
                imax);
      EXPECT_EQ(MathUtil::SafeRound<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imax) + 0.49)),
                imax);
      EXPECT_EQ(MathUtil::SafeRound<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imax) + 0.5)),
                imax);
      EXPECT_EQ(MathUtil::SafeRound<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imax) + 0.51)),
                imax);
      EXPECT_EQ(MathUtil::SafeRound<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imax) + 0.99)),
                imax);
      EXPECT_EQ(MathUtil::SafeRound<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imax) - 0.1)),
                imax);
      EXPECT_EQ(MathUtil::SafeRound<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imax) - 0.49)),
                imax);
      EXPECT_EQ(MathUtil::SafeRound<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imax) - 0.51)),
                imax - 1);
      EXPECT_EQ(MathUtil::SafeRound<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imax) - 0.99)),
                imax - 1);
      EXPECT_EQ(MathUtil::SafeRound<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imax) - 1.49)),
                imax - 1);
      EXPECT_EQ(MathUtil::SafeRound<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imax) - 1.51)),
                imax - 2);
    }
    // When FloatIn is wider than IntOut, or if IntOut is unsigned, we can test
    // that fractional rounding near imin works as expected.
    if (!s || (sizeof(FloatIn) > sizeof(IntOut))) {
      {
        // Values slightly greater than imin should round to imin
        FloatIn v = static_cast<FloatIn>(imin);
        for (int i = 0; i < kLoopCount; i++) {
          v = NextAfter(static_cast<FloatIn>(v),
                        std::numeric_limits<FloatIn>::max());
          EXPECT_EQ(MathUtil::SafeRound<IntOut>(static_cast<FloatIn>(v)), imin);
        }
      }
      EXPECT_EQ(MathUtil::SafeRound<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imin) - 0.1)),
                imin);
      EXPECT_EQ(MathUtil::SafeRound<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imin) - 0.49)),
                imin);
      EXPECT_EQ(MathUtil::SafeRound<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imin) - 0.5)),
                imin);
      EXPECT_EQ(MathUtil::SafeRound<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imin) - 0.51)),
                imin);
      EXPECT_EQ(MathUtil::SafeRound<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imin) - 0.99)),
                imin);
      EXPECT_EQ(MathUtil::SafeRound<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imin) + 0.1)),
                imin);
      EXPECT_EQ(MathUtil::SafeRound<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imin) + 0.49)),
                imin);
      EXPECT_EQ(MathUtil::SafeRound<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imin) + 0.51)),
                imin + 1);
      EXPECT_EQ(MathUtil::SafeRound<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imin) + 0.99)),
                imin + 1);
      EXPECT_EQ(MathUtil::SafeRound<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imin) + 1.49)),
                imin + 1);
      EXPECT_EQ(MathUtil::SafeRound<IntOut>(
                    static_cast<FloatIn>(static_cast<FloatIn>(imin) + 1.51)),
                imin + 2);
    }
  }
};

TEST(MathUtil, SafeRound) {
  SafeRoundTester<float, int8_t>::Run();
  SafeRoundTester<double, int8_t>::Run();
  SafeRoundTester<float, int16_t>::Run();
  SafeRoundTester<double, int16_t>::Run();
  SafeRoundTester<float, int32_t>::Run();
  SafeRoundTester<double, int32_t>::Run();
  SafeRoundTester<float, int64_t>::Run();
  SafeRoundTester<double, int64_t>::Run();
#ifndef GLOOP_UNSUPPORTED_LIBSTDCXX  // std::isnan<int128> is ambiguous
  SafeRoundTester<float, int128_t>::Run();
  SafeRoundTester<double, int128_t>::Run();
#endif  // GLOOP_UNSUPPORTED_LIBSTDCXX

  SafeRoundTester<float, uint8_t>::Run();
  SafeRoundTester<double, uint8_t>::Run();
  SafeRoundTester<float, uint16_t>::Run();
  SafeRoundTester<double, uint16_t>::Run();
  SafeRoundTester<float, uint32_t>::Run();
  SafeRoundTester<double, uint32_t>::Run();
  SafeRoundTester<float, uint64_t>::Run();
  SafeRoundTester<double, uint64_t>::Run();
#ifndef GLOOP_UNSUPPORTED_LIBSTDCXX  // std::isnan<int128> is ambiguous
  SafeRoundTester<float, uint128_t>::Run();
  SafeRoundTester<double, uint128_t>::Run();
#endif  // GLOOP_UNSUPPORTED_LIBSTDCXX

  // Spot-check SafeRound<int>
  EXPECT_EQ(MathUtil::SafeRound<int>(static_cast<float>(12345.678)), 12346);
  EXPECT_EQ(MathUtil::SafeRound<int>(static_cast<float>(12345.4321)), 12345);
  EXPECT_EQ(MathUtil::SafeRound<int>(static_cast<double>(-12345.678)), -12346);
  EXPECT_EQ(MathUtil::SafeRound<int>(static_cast<double>(-12345.4321)), -12345);
  EXPECT_EQ(MathUtil::SafeRound<int>(1E47), 2147483647);
  EXPECT_EQ(MathUtil::SafeRound<int>(-1E47), int64_t{-2147483648});
}

template <typename Type>
void TestSquareForUnsigned() {
  LOG(INFO) << "testing Square for " << sizeof(Type);

  const Type kZero = 0;
  const Type kFinite = static_cast<Type>(3.67);

  EXPECT_TRUE(MathUtil::Square(kZero) == kZero);
  EXPECT_TRUE(MathUtil::Square(kFinite) == (kFinite * kFinite));
}

template <typename Type>
void TestSquareForSigned() {
  TestSquareForUnsigned<Type>();
  LOG(INFO) << "testing Square for signed " << sizeof(Type);

  const Type kZero = 0;
  const Type kNegZero = -0;
  const Type kFinite = static_cast<Type>(3.67);

  EXPECT_TRUE(MathUtil::Square(kNegZero) == kZero);
  EXPECT_TRUE(MathUtil::Square(-kFinite) == (kFinite * kFinite));
}

template <typename Type>
void TestSquareForFP() {
  TestSquareForSigned<Type>();
  LOG(INFO) << "testing Square for FP " << sizeof(Type);

  const Type kNaN = std::numeric_limits<Type>::quiet_NaN();
  const Type kPosInf = std::numeric_limits<Type>::infinity();
  const Type kNegInf = -std::numeric_limits<Type>::infinity();

  EXPECT_TRUE(std::isnan(MathUtil::Square(kNaN)));
  EXPECT_TRUE(MathUtil::Square(kPosInf) == kPosInf);
  EXPECT_TRUE(MathUtil::Square(kNegInf) == kPosInf);
}

TEST(MathUtil, Square) {
  TestSquareForUnsigned<uint8_t>();
  TestSquareForUnsigned<uint16_t>();
  TestSquareForUnsigned<uint32_t>();
  TestSquareForUnsigned<uint64_t>();

  TestSquareForSigned<int8_t>();
  TestSquareForSigned<int16_t>();
  TestSquareForSigned<int32_t>();
  TestSquareForSigned<int64_t>();

  TestSquareForFP<float>();
  TestSquareForFP<double>();
  TestSquareForFP<long double>();
}

template <typename Type>
void TestSimpleUtils() {
  LOG(INFO) << "testing Max Min Abs Sign AbsDiff for " << sizeof(Type);

  const Type kZero = 0.0;
  const Type kNegZero = -0.0;
  const Type kFinite = 2.67;
  const Type kNaN = std::numeric_limits<Type>::quiet_NaN();
  const Type kPosInf = std::numeric_limits<Type>::infinity();
  const Type kNegInf = -std::numeric_limits<Type>::infinity();

  EXPECT_TRUE(MathUtil::Max(kZero, kNegZero) == kZero);
  EXPECT_TRUE(MathUtil::Max(kNegZero, kZero) == kZero);

  EXPECT_TRUE(MathUtil::Min(kZero, kNegZero) == kNegZero);
  EXPECT_TRUE(MathUtil::Min(kNegZero, kZero) == kNegZero);
  // these four tests above work because kNegZero and kZero
  // are indistinguishable w.r.t. == :
  EXPECT_TRUE(kNegZero == kZero);

  EXPECT_TRUE(std::isnan(MathUtil::Max(kNaN, kFinite)));
  EXPECT_TRUE(std::isnan(MathUtil::Max(kFinite, kNaN)));
  EXPECT_TRUE(std::isnan(MathUtil::Max(kNaN, kNaN)));
  EXPECT_TRUE(std::isnan(MathUtil::Max(kNaN, -kNaN)));
  EXPECT_TRUE(std::isnan(MathUtil::Max(kNaN, kPosInf)));
  EXPECT_TRUE(std::isnan(MathUtil::Max(kNaN, kNegInf)));
  EXPECT_TRUE(std::isnan(MathUtil::Max(kPosInf, kNaN)));
  EXPECT_TRUE(std::isnan(MathUtil::Max(kNegInf, kNaN)));

  EXPECT_TRUE(std::isnan(MathUtil::Min(kNaN, kFinite)));
  EXPECT_TRUE(std::isnan(MathUtil::Min(kFinite, kNaN)));
  EXPECT_TRUE(std::isnan(MathUtil::Min(kNaN, kNaN)));
  EXPECT_TRUE(std::isnan(MathUtil::Min(kNaN, -kNaN)));
  EXPECT_TRUE(std::isnan(MathUtil::Min(kNaN, kPosInf)));
  EXPECT_TRUE(std::isnan(MathUtil::Min(kNaN, kNegInf)));
  EXPECT_TRUE(std::isnan(MathUtil::Min(kPosInf, kNaN)));
  EXPECT_TRUE(std::isnan(MathUtil::Min(kNegInf, kNaN)));

  EXPECT_EQ(kPosInf, MathUtil::Max(kPosInf, kFinite));
  EXPECT_EQ(kPosInf, MathUtil::Max(kFinite, kPosInf));
  EXPECT_EQ(kPosInf, MathUtil::Max(kPosInf, kPosInf));
  EXPECT_EQ(kPosInf, MathUtil::Max(kPosInf, kNegInf));
  EXPECT_EQ(kPosInf, MathUtil::Max(kNegInf, kPosInf));

  EXPECT_TRUE(MathUtil::Min(kPosInf, kFinite) == kFinite);
  EXPECT_TRUE(MathUtil::Min(kFinite, kPosInf) == kFinite);
  EXPECT_EQ(kPosInf, MathUtil::Min(kPosInf, kPosInf));
  EXPECT_EQ(kNegInf, MathUtil::Min(kPosInf, kNegInf));
  EXPECT_EQ(kNegInf, MathUtil::Min(kNegInf, kPosInf));

  EXPECT_TRUE(MathUtil::Max(kNegInf, kFinite) == kFinite);
  EXPECT_TRUE(MathUtil::Max(kFinite, kNegInf) == kFinite);
  EXPECT_EQ(kNegInf, MathUtil::Max(kNegInf, kNegInf));

  EXPECT_EQ(kNegInf, MathUtil::Min(kNegInf, kFinite));
  EXPECT_EQ(kNegInf, MathUtil::Min(kFinite, kNegInf));
  EXPECT_EQ(kNegInf, MathUtil::Min(kNegInf, kNegInf));

  EXPECT_TRUE(MathUtil::Abs(kZero) == kZero);
  EXPECT_TRUE(MathUtil::Abs(kNegZero) == kZero);
  EXPECT_TRUE(MathUtil::Abs(kFinite) == kFinite);
  EXPECT_TRUE(MathUtil::Abs(-kFinite) == kFinite);
  EXPECT_TRUE(std::isnan(MathUtil::Abs(kNaN)));
  EXPECT_EQ(kPosInf, MathUtil::Abs(kPosInf));
  EXPECT_EQ(kPosInf, MathUtil::Abs(kNegInf));

  EXPECT_EQ(MathUtil::Sign(kZero), 0);
  EXPECT_EQ(MathUtil::Sign(kNegZero), 0);
  EXPECT_EQ(MathUtil::Sign(kFinite), 1);
  EXPECT_EQ(MathUtil::Sign(-kFinite), -1);
  EXPECT_EQ(MathUtil::Sign(kPosInf), 1);
  EXPECT_EQ(MathUtil::Sign(kNegInf), -1);
  EXPECT_TRUE(std::isnan(MathUtil::Sign(kNaN)));

  EXPECT_TRUE(std::isnan(MathUtil::AbsDiff(kNaN, kFinite)));
  EXPECT_TRUE(std::isnan(MathUtil::AbsDiff(kFinite, kNaN)));
  EXPECT_TRUE(std::isnan(MathUtil::AbsDiff(kNaN, kNaN)));
  EXPECT_TRUE(std::isnan(MathUtil::AbsDiff(kNaN, -kNaN)));
  EXPECT_TRUE(std::isnan(MathUtil::AbsDiff(kNaN, kPosInf)));
  EXPECT_TRUE(std::isnan(MathUtil::AbsDiff(kNaN, kNegInf)));
  EXPECT_TRUE(std::isnan(MathUtil::AbsDiff(kPosInf, kNaN)));
  EXPECT_TRUE(std::isnan(MathUtil::AbsDiff(kNegInf, kNaN)));

  EXPECT_EQ(kPosInf, MathUtil::AbsDiff(kPosInf, kFinite));
  EXPECT_EQ(kPosInf, MathUtil::AbsDiff(kFinite, kPosInf));
  EXPECT_TRUE(std::isnan(MathUtil::AbsDiff(kPosInf, kPosInf)));
  EXPECT_EQ(kPosInf, MathUtil::AbsDiff(kPosInf, kNegInf));
  EXPECT_EQ(kPosInf, MathUtil::AbsDiff(kNegInf, kPosInf));

  EXPECT_EQ(kPosInf, MathUtil::AbsDiff(kNegInf, kFinite));
  EXPECT_EQ(kPosInf, MathUtil::AbsDiff(kFinite, kNegInf));
  EXPECT_TRUE(std::isnan(MathUtil::AbsDiff(kNegInf, kNegInf)));
}

TEST(MathUtil, SimpleUtils) {
  TestSimpleUtils<float>();
  TestSimpleUtils<double>();
  TestSimpleUtils<long double>();
}

template <typename Type>
void TestAbsDiffUnsigned() {
  for (const Type v : {std::numeric_limits<Type>::min(), Type{0}, Type{5},
                       Type{15}, std::numeric_limits<Type>::max()}) {
    EXPECT_EQ(0, MathUtil::AbsDiff(v, v));
  }
  EXPECT_EQ(Type{5}, MathUtil::AbsDiff(Type{15}, Type{20}));
  EXPECT_EQ(Type{5}, MathUtil::AbsDiff(Type{20}, Type{15}));
}

TEST(MathUtil, AbsDiffUnsigned) {
  TestAbsDiffUnsigned<uint64_t>();
  TestAbsDiffUnsigned<uint8_t>();
}

template <typename Type>
void TestWithinFor() {
  LOG(INFO) << "testing Within* and NearBy* for " << sizeof(Type);

  const Type kZero = 0;
  const Type kOne = 1;
  const Type kEpsilon = std::numeric_limits<Type>::is_integer
                            ? kZero
                            : std::numeric_limits<Type>::epsilon();

  // NearBy*

  EXPECT_TRUE(MathUtil::NearByMargin<Type>(kOne, kOne));
  EXPECT_TRUE(MathUtil::NearByMargin<Type>(kZero, kZero));
  EXPECT_TRUE(MathUtil::NearByMargin<Type>(-kOne, -kOne));
  EXPECT_TRUE(MathUtil::NearByMargin<Type>(kOne, kOne + kEpsilon));
  EXPECT_TRUE(MathUtil::NearByMargin<Type>(kOne, kOne - kEpsilon));

  EXPECT_TRUE(MathUtil::NearByFraction<Type>(kOne, kOne));
  EXPECT_TRUE(MathUtil::NearByFraction<Type>(kZero, kZero));
  EXPECT_TRUE(MathUtil::NearByFraction<Type>(-kOne, -kOne));
  EXPECT_TRUE(MathUtil::NearByFraction<Type>(kOne, kOne + kEpsilon));
  EXPECT_TRUE(MathUtil::NearByFraction<Type>(kOne, kOne - kEpsilon));
}

template <typename Type>
void TestWithinForFP() {
  TestWithinFor<Type>();
  LOG(INFO) << "testing Within* and NearBy* for FP " << sizeof(Type);

  // Within*

  EXPECT_TRUE(MathUtil::WithinFraction<Type>(123456e0, 123459e0, 1e-4));
  EXPECT_TRUE(MathUtil::WithinFraction<Type>(123456e2, 123459e2, 1e-4));
  EXPECT_TRUE(MathUtil::WithinFraction<Type>(123456e10, 123459e10, 1e-4));
  EXPECT_TRUE(MathUtil::WithinFraction<Type>(123456e20, 123459e20, 1e-4));
  EXPECT_TRUE(MathUtil::WithinFraction<Type>(123456e-2, 123459e-2, 1e-4));
  EXPECT_TRUE(MathUtil::WithinFraction<Type>(123456e-10, 123459e-10, 1e-4));
  EXPECT_TRUE(MathUtil::WithinFraction<Type>(123456e-20, 123459e-20, 1e-4));
  EXPECT_TRUE(MathUtil::WithinFraction<Type>(-123456e0, -123459e0, 1e-4));
  EXPECT_TRUE(MathUtil::WithinFraction<Type>(-123456e2, -123459e2, 1e-4));
  EXPECT_TRUE(MathUtil::WithinFraction<Type>(-123456e10, -123459e10, 1e-4));
  EXPECT_TRUE(MathUtil::WithinFraction<Type>(-123456e20, -123459e20, 1e-4));
  EXPECT_TRUE(MathUtil::WithinFraction<Type>(-123456e-2, -123459e-2, 1e-4));
  EXPECT_TRUE(MathUtil::WithinFraction<Type>(-123456e-10, -123459e-10, 1e-4));
  EXPECT_TRUE(MathUtil::WithinFraction<Type>(-123456e-20, -123459e-20, 1e-4));
  EXPECT_FALSE(MathUtil::WithinFraction<Type>(1e-20, 123456e-10, 1e-4));
  EXPECT_FALSE(MathUtil::WithinFraction<Type>(0.0, 123456e-10, 1e-4));
  EXPECT_TRUE(
      MathUtil::WithinFractionOrMargin<Type>(1e-20, 123456e-10, 1e-4, 1e-4));
  EXPECT_TRUE(
      MathUtil::WithinFractionOrMargin<Type>(1e-20, -123456e-10, 1e-4, 1e-4));
  EXPECT_TRUE(
      MathUtil::WithinFractionOrMargin<Type>(-1e-20, 12345e-10, 1e-4, 1e-4));
  EXPECT_TRUE(
      MathUtil::WithinFractionOrMargin<Type>(-1e-20, -12345e-10, 1e-4, 1e-4));
  EXPECT_TRUE(
      MathUtil::WithinFractionOrMargin<Type>(0.0, 123456e-10, 1e-4, 1e-4));
  EXPECT_TRUE(
      MathUtil::WithinFractionOrMargin<Type>(0.0, -123456e-10, 1e-4, 1e-4));

  EXPECT_FALSE(MathUtil::WithinFraction<Type>(0.0, 1e-10, 1e-5));
  EXPECT_TRUE(MathUtil::WithinFractionOrMargin<Type>(0.0, 1e-10, 1e-5, 1e-5));

  const Type kZero = 0;
  const Type kFinite = 2.67;
  const Type kNaN = std::numeric_limits<Type>::quiet_NaN();
  const Type kPosInf = std::numeric_limits<Type>::infinity();
  const Type kNegInf = -std::numeric_limits<Type>::infinity();

  EXPECT_FALSE(MathUtil::NearByMargin<Type>(kNaN, kZero));
  EXPECT_FALSE(MathUtil::NearByMargin<Type>(kNaN, kFinite));
  EXPECT_FALSE(MathUtil::NearByMargin<Type>(kNaN, kNaN));
  EXPECT_FALSE(MathUtil::NearByMargin<Type>(kNaN, kPosInf));
  EXPECT_FALSE(MathUtil::NearByMargin<Type>(kNaN, kNegInf));

  EXPECT_FALSE(MathUtil::NearByMargin<Type>(kPosInf, kZero));
  EXPECT_FALSE(MathUtil::NearByMargin<Type>(kPosInf, kFinite));
  EXPECT_FALSE(MathUtil::NearByMargin<Type>(kPosInf, kPosInf));
  EXPECT_FALSE(MathUtil::NearByMargin<Type>(kPosInf, kNegInf));

  EXPECT_FALSE(MathUtil::NearByMargin<Type>(kNegInf, kZero));
  EXPECT_FALSE(MathUtil::NearByMargin<Type>(kNegInf, kFinite));
  EXPECT_FALSE(MathUtil::NearByMargin<Type>(kNegInf, kNegInf));

  EXPECT_FALSE(MathUtil::NearByFraction<Type>(kNaN, kZero));
  EXPECT_FALSE(MathUtil::NearByFraction<Type>(kNaN, kFinite));
  EXPECT_FALSE(MathUtil::NearByFraction<Type>(kNaN, kNaN));
  EXPECT_FALSE(MathUtil::NearByFraction<Type>(kNaN, kPosInf));
  EXPECT_FALSE(MathUtil::NearByFraction<Type>(kNaN, kNegInf));

  EXPECT_FALSE(MathUtil::NearByFraction<Type>(kPosInf, kZero));
  EXPECT_FALSE(MathUtil::NearByFraction<Type>(kPosInf, kFinite));
  EXPECT_FALSE(MathUtil::NearByFraction<Type>(kPosInf, kPosInf));
  EXPECT_FALSE(MathUtil::NearByFraction<Type>(kPosInf, kNegInf));

  EXPECT_FALSE(MathUtil::NearByFraction<Type>(kNegInf, kZero));
  EXPECT_FALSE(MathUtil::NearByFraction<Type>(kNegInf, kFinite));
  EXPECT_FALSE(MathUtil::NearByFraction<Type>(kNegInf, kNegInf));

  EXPECT_FALSE(MathUtil::NearByFractionOrMargin<Type>(kNaN, kZero));
  EXPECT_FALSE(MathUtil::NearByFractionOrMargin<Type>(kNaN, kFinite));
  EXPECT_FALSE(MathUtil::NearByFractionOrMargin<Type>(kNaN, kNaN));
  EXPECT_FALSE(MathUtil::NearByFractionOrMargin<Type>(kNaN, kPosInf));
  EXPECT_FALSE(MathUtil::NearByFractionOrMargin<Type>(kNaN, kNegInf));

  EXPECT_FALSE(MathUtil::NearByFractionOrMargin<Type>(kPosInf, kZero));
  EXPECT_FALSE(MathUtil::NearByFractionOrMargin<Type>(kPosInf, kFinite));
  EXPECT_FALSE(MathUtil::NearByFractionOrMargin<Type>(kPosInf, kPosInf));
  EXPECT_FALSE(MathUtil::NearByFractionOrMargin<Type>(kPosInf, kNegInf));

  EXPECT_FALSE(MathUtil::NearByFractionOrMargin<Type>(kNegInf, kZero));
  EXPECT_FALSE(MathUtil::NearByFractionOrMargin<Type>(kNegInf, kFinite));
  EXPECT_FALSE(MathUtil::NearByFractionOrMargin<Type>(kNegInf, kNegInf));
}

template <typename Type>
void TestWithinForInt() {
  TestWithinFor<Type>();
  LOG(INFO) << "testing Within* and NearBy* for Int " << sizeof(Type);

  const Type kZero = 0;
  const Type kOne = 1;
  const Type kMax = std::numeric_limits<Type>::max();
  const Type kMin = std::numeric_limits<Type>::lowest();

  // Integer behavior.

  EXPECT_TRUE(MathUtil::NearByMargin<Type>(kZero, kZero));
  EXPECT_TRUE(MathUtil::NearByMargin<Type>(kOne, kOne));
  EXPECT_TRUE(MathUtil::NearByMargin<Type>(-kOne, -kOne));
  EXPECT_TRUE(MathUtil::NearByMargin<Type>(kMax, kMax));
  EXPECT_TRUE(MathUtil::NearByMargin<Type>(kMin, kMin));
  EXPECT_FALSE(MathUtil::NearByMargin<Type>(kMax, kMin));
  EXPECT_FALSE(MathUtil::NearByMargin<Type>(kMax, kMax - kOne));
  EXPECT_FALSE(MathUtil::NearByMargin<Type>(kMin, kMin + kOne));

  EXPECT_TRUE(MathUtil::NearByFraction<Type>(kZero, kZero));
  EXPECT_TRUE(MathUtil::NearByFraction<Type>(kOne, kOne));
  EXPECT_TRUE(MathUtil::NearByFraction<Type>(-kOne, -kOne));
  EXPECT_TRUE(MathUtil::NearByFraction<Type>(kMax, kMax));
  EXPECT_TRUE(MathUtil::NearByFraction<Type>(kMin, kMin));
  EXPECT_FALSE(MathUtil::NearByFraction<Type>(kMax, kMin));
  EXPECT_FALSE(MathUtil::NearByFraction<Type>(kMax, kMax - kOne));
  EXPECT_FALSE(MathUtil::NearByFraction<Type>(kMin, kMin + kOne));
}

TEST(MathUtil, Within) {
  TestWithinForInt<int8_t>();
  TestWithinForInt<int16_t>();
  TestWithinForInt<int32_t>();
  TestWithinForInt<int64_t>();

  TestWithinForInt<uint8_t>();
  TestWithinForInt<uint16_t>();
  TestWithinForInt<uint32_t>();
  TestWithinForInt<uint64_t>();

  TestWithinForFP<float>();
  TestWithinForFP<double>();
  TestWithinForFP<long double>();
}

// Tests AlmostEquals() on the integer type Type.
template <typename Type>
void TestAlmostEqualsInt() {
  LOG(INFO) << "testing AlmostEquals on integer type size " << sizeof(Type);

  EXPECT_TRUE(MathUtil::AlmostEquals<Type>(0, 0));
  EXPECT_FALSE(MathUtil::AlmostEquals<Type>(0, 1));
  EXPECT_FALSE(MathUtil::AlmostEquals<Type>(1, 0));

  for (Type i = -10; i <= 10; i++) {
    if (i != 0) {
      EXPECT_TRUE(MathUtil::AlmostEquals<Type>(i, i));
      EXPECT_FALSE(MathUtil::AlmostEquals<Type>(0, i));
      EXPECT_FALSE(MathUtil::AlmostEquals<Type>(-i, 0));
      EXPECT_FALSE(MathUtil::AlmostEquals<Type>(i, i + 1));
      EXPECT_FALSE(MathUtil::AlmostEquals<Type>(i, 2 * i));
    }
  }
}

// Tests AlmostEquals() on the floating-point type Type.
template <typename Type>
void TestAlmostEqualsFP() {
  LOG(INFO) << "testing AlmostEquals on floating point size " << sizeof(Type);

  static const Type kZero = 0;
  static const Type kOne = 1;
  static const Type kError = MathLimits<Type>::kStdError;
  static const Type kTwoThirdsError = (2 * kError) / 3;
  static const Type kOneAndHalfError = (3 * kError) / 2;

  // Test {x, x} pairs
  EXPECT_TRUE(MathUtil::AlmostEquals<Type>(kZero, kZero));
  EXPECT_TRUE(MathUtil::AlmostEquals<Type>(kTwoThirdsError, kTwoThirdsError));
  EXPECT_TRUE(MathUtil::AlmostEquals<Type>(kError, kError));
  EXPECT_TRUE(MathUtil::AlmostEquals<Type>(kOneAndHalfError, kOneAndHalfError));
  EXPECT_TRUE(MathUtil::AlmostEquals<Type>(kOne, kOne));

  // Test {0, x} pairs
  EXPECT_TRUE(MathUtil::AlmostEquals<Type>(kZero, kTwoThirdsError));
  EXPECT_TRUE(MathUtil::AlmostEquals<Type>(kZero, kError));
  EXPECT_FALSE(MathUtil::AlmostEquals<Type>(kZero, kOneAndHalfError));
  EXPECT_FALSE(MathUtil::AlmostEquals<Type>(kZero, kOne));

  // Test {x, 0} pairs (should be the same as the above result, but just
  // to be sure).
  EXPECT_TRUE(MathUtil::AlmostEquals<Type>(kTwoThirdsError, kZero));
  EXPECT_TRUE(MathUtil::AlmostEquals<Type>(kError, kZero));
  EXPECT_FALSE(MathUtil::AlmostEquals<Type>(kOneAndHalfError, kZero));
  EXPECT_FALSE(MathUtil::AlmostEquals<Type>(kOne, kZero));

  // Test {-x, x} pairs
  EXPECT_TRUE(MathUtil::AlmostEquals<Type>(-kTwoThirdsError, kTwoThirdsError));
  EXPECT_TRUE(MathUtil::AlmostEquals<Type>(-kError, kError));
  EXPECT_TRUE(
      !MathUtil::AlmostEquals<Type>(-kOneAndHalfError, kOneAndHalfError));
  EXPECT_FALSE(MathUtil::AlmostEquals<Type>(-kOne, kOne));

  // Test {x, y} pairs: x > 0 and y > 0
  EXPECT_TRUE(MathUtil::AlmostEquals<Type>(kTwoThirdsError, kError));
  EXPECT_TRUE(MathUtil::AlmostEquals<Type>(kTwoThirdsError, kTwoThirdsError));
  EXPECT_TRUE(MathUtil::AlmostEquals<Type>(kTwoThirdsError, kOneAndHalfError));
  EXPECT_FALSE(MathUtil::AlmostEquals<Type>(kTwoThirdsError, kOne));
  EXPECT_TRUE(MathUtil::AlmostEquals<Type>(kError, kOneAndHalfError));
  EXPECT_FALSE(MathUtil::AlmostEquals<Type>(kError, kOne));
  EXPECT_FALSE(MathUtil::AlmostEquals<Type>(kOneAndHalfError, kOne));

  // Test values <= 1.
  for (int exponent = -std::numeric_limits<Type>::digits10; exponent <= 0;
       exponent++) {
    const Type x = pow(10.0, exponent);
    EXPECT_TRUE(MathUtil::AlmostEquals<Type>(x, x));
    EXPECT_TRUE(MathUtil::AlmostEquals<Type>(x, x + kTwoThirdsError));
    EXPECT_TRUE(MathUtil::AlmostEquals<Type>(x - kTwoThirdsError, x));
    EXPECT_FALSE(MathUtil::AlmostEquals<Type>(x, x + kOneAndHalfError));
  }

  // Test values > 1.
  for (int exponent = 1; exponent <= std::numeric_limits<Type>::digits10;
       exponent++) {
    const Type x = pow(10.0, exponent);
    EXPECT_TRUE(MathUtil::AlmostEquals<Type>(x, x));
    EXPECT_FALSE(MathUtil::AlmostEquals<Type>(kOne, x));

    // The relative error is less than kError, although the absolute error is
    // larger than kError.
    EXPECT_TRUE(MathUtil::AlmostEquals<Type>(x, x + kOneAndHalfError));

    // Test relative errors smaller and larger than the threshold.
    EXPECT_TRUE(MathUtil::AlmostEquals<Type>(x, x * (kOne + kTwoThirdsError)));
    EXPECT_TRUE(
        !MathUtil::AlmostEquals<Type>(x * (kOne + kOneAndHalfError), x));
  }

  // Test comparisons involving special floating-point values.
  static const Type kPosInf = std::numeric_limits<Type>::infinity();
  static const Type kNegInf = -std::numeric_limits<Type>::infinity();
  static const Type kNaN = std::numeric_limits<Type>::quiet_NaN();

  EXPECT_TRUE(MathUtil::AlmostEquals<Type>(kPosInf, kPosInf));
  EXPECT_TRUE(MathUtil::AlmostEquals<Type>(kNegInf, kNegInf));
  EXPECT_FALSE(MathUtil::AlmostEquals<Type>(kNaN, kNaN));

  EXPECT_FALSE(MathUtil::AlmostEquals<Type>(kPosInf, kNegInf));
  EXPECT_FALSE(MathUtil::AlmostEquals<Type>(kPosInf, kNaN));
  EXPECT_TRUE(
      !MathUtil::AlmostEquals<Type>(kPosInf, std::numeric_limits<Type>::max()));
  EXPECT_FALSE(MathUtil::AlmostEquals<Type>(kPosInf, kZero));

  EXPECT_FALSE(MathUtil::AlmostEquals<Type>(kNegInf, kPosInf));
  EXPECT_FALSE(MathUtil::AlmostEquals<Type>(kNegInf, kNaN));
  EXPECT_TRUE(!MathUtil::AlmostEquals<Type>(
      kNegInf, std::numeric_limits<Type>::lowest()));
  EXPECT_FALSE(MathUtil::AlmostEquals<Type>(kNegInf, kZero));

  EXPECT_FALSE(MathUtil::AlmostEquals<Type>(kNaN, kPosInf));
  EXPECT_FALSE(MathUtil::AlmostEquals<Type>(kNaN, kNegInf));
  EXPECT_FALSE(MathUtil::AlmostEquals<Type>(kNaN, kZero));

  EXPECT_TRUE(
      !MathUtil::AlmostEquals<Type>(std::numeric_limits<Type>::max(), kPosInf));
  EXPECT_TRUE(!MathUtil::AlmostEquals<Type>(std::numeric_limits<Type>::lowest(),
                                            kNegInf));
  EXPECT_FALSE(MathUtil::AlmostEquals<Type>(kZero, kPosInf));
  EXPECT_FALSE(MathUtil::AlmostEquals<Type>(kZero, kNegInf));
  EXPECT_FALSE(MathUtil::AlmostEquals<Type>(kZero, kNaN));
}

TEST(MathUtil, AlmostEquals) {
  TestAlmostEqualsInt<int8_t>();
  TestAlmostEqualsInt<int16_t>();
  TestAlmostEqualsInt<int32_t>();
  TestAlmostEqualsInt<int64_t>();

  TestAlmostEqualsFP<float>();
  TestAlmostEqualsFP<double>();
  TestAlmostEqualsFP<long double>();
}

//
uint64_t Combinations(uint64_t n, uint64_t k) {
  // use symmetry to pick the shorter calculation
  if (k > n / 2) {
    k = n - k;
  }
  uint64_t result = 1;
  // Order of calculation is critical.  The loop below can be unrolled
  // as this:
  //   result *= n;
  //   result /= 1;
  //   result *= n - 1;
  //   result /= 2;
  //   ...
  //   result *= n - k + 1;
  //   result /= k;
  // This order guarantees that the divide has no modulo, and that
  // 'result' increases as slowly as possible so as to overflow as late
  // as possible.
  for (int i = 1; i <= k; ++i) {
    result *= n - (i - 1);
    EXPECT_EQ(0, result % i);
    result /= i;
  }
  return result;
}

TEST(MathUtil, Stirling) {
  double log_factorial = 0;
  for (int n = 1; n <= 20; ++n) {
    log_factorial += log(n);
    const double kErrorBound = 1.0 / (1260.0 * (n * n * n * n * n));
    EXPECT_TRUE(MathUtil::WithinMargin(log_factorial, MathUtil::Stirling(n),
                                       kErrorBound));
  }
}

TEST(MathUtil, Combinations) {
  // edge cases
  for (int i = 1; i < 10; ++i) {
    EXPECT_EQ(1, Combinations(i, i))
        << "Combinations(" << i << ", " << i << ")";
    EXPECT_EQ(1, Combinations(i, 0))
        << "Combinations(" << i << ", " << 0 << ")";
  }
  for (int i = 2; i < 10; ++i) {
    EXPECT_EQ(i, Combinations(i, i - 1))
        << "Combinations(" << i << ", " << i - 1 << ")";
    EXPECT_EQ(i, Combinations(i, 1))
        << "Combinations(" << i << ", " << 1 << ")";
  }

  // various test cases that I calculated by hand
  EXPECT_EQ(10, Combinations(5, 2));
  EXPECT_EQ(10, Combinations(5, 3));
  EXPECT_EQ(35, Combinations(7, 3));
  EXPECT_EQ(35, Combinations(7, 4));
}

TEST(MathUtil, LogCombinations) {
  EXPECT_DOUBLE_EQ(MathUtil::LogCombinations(0, 0), 0);

  const double kFraction = 8e-11;
  // C(63, 29) overflows uint64
  for (int n = 0; n < 62; ++n) {
    for (int k = 1; k <= n; ++k) {
      EXPECT_TRUE(MathUtil::WithinFraction(
          log(Combinations(n, k)), MathUtil::LogCombinations(n, k), kFraction))
          << "C(" << n << ", " << k << ") = " << Combinations(n, k);
    }
  }
}

template <typename T>  // T models LessThanComparable.
T CallClampValue(T low, T high, T* result) {
  *result = std::clamp(*result, low, high);
  return *result;
}

TEST(MathUtil, Clamping) {
  constexpr int kLow = 7;
  constexpr int kHigh = 42;

  EXPECT_EQ(33, MathUtil::Clamp(kLow, kHigh, 33));
  EXPECT_EQ(kLow, MathUtil::Clamp(kLow, kHigh, kLow));
  EXPECT_EQ(kHigh, MathUtil::Clamp(kLow, kHigh, kHigh));
  EXPECT_EQ(kLow, MathUtil::Clamp(kLow, kHigh, kLow - 1));
  EXPECT_EQ(kHigh, MathUtil::Clamp(kLow, kHigh, kHigh + 1));

  EXPECT_EQ(kLow, MathUtil::Clamp(kLow, kLow, kLow - 1));
  EXPECT_EQ(kLow, MathUtil::Clamp(kLow, kLow, kLow));
  EXPECT_EQ(kLow, MathUtil::Clamp(kLow, kLow, kLow + 1));

  int result = 33;
  EXPECT_EQ(33, CallClampValue(kLow, kHigh, &result));
  result = kLow;
  EXPECT_EQ(kLow, CallClampValue(kLow, kHigh, &result));
  result = kHigh;
  EXPECT_EQ(kHigh, CallClampValue(kLow, kHigh, &result));
  result = kLow - 1;
  EXPECT_EQ(kLow, CallClampValue(kLow, kHigh, &result));
  result = kHigh + 1;
  EXPECT_EQ(kHigh, CallClampValue(kLow, kHigh, &result));
  result = kLow - 1;

  EXPECT_EQ(kLow, CallClampValue(kLow, kLow, &result));
  result = kLow;
  EXPECT_EQ(kLow, CallClampValue(kLow, kLow, &result));
  result = kLow + 1;
  EXPECT_EQ(kLow, CallClampValue(kLow, kLow, &result));
}

TEST(MathUtil, FloatClamping) {
  constexpr float kLow = 7.0f;
  constexpr float kHigh = 42.0f;
  constexpr float kInf = std::numeric_limits<float>::infinity();
  const float kNan = std::nanf("");

  EXPECT_EQ(MathUtil::Clamp(kLow, kHigh, 33.0f), 33.0f);
  EXPECT_EQ(MathUtil::Clamp(kLow, kHigh, kLow), kLow);
  EXPECT_EQ(MathUtil::Clamp(kLow, kHigh, kHigh), kHigh);
  EXPECT_EQ(MathUtil::Clamp(kLow, kHigh, kLow - 1.0f), kLow);
  EXPECT_EQ(MathUtil::Clamp(kLow, kHigh, kHigh + 1.0f), kHigh);
  EXPECT_EQ(MathUtil::Clamp(kLow, kHigh, -kInf), kLow);
  EXPECT_EQ(MathUtil::Clamp(kLow, kHigh, kInf), kHigh);
  EXPECT_TRUE(std::isnan(MathUtil::Clamp(kLow, kHigh, kNan)));

  float result = 33.0f;
  EXPECT_EQ(CallClampValue(kLow, kHigh, &result), 33.0f);
  result = kLow;
  EXPECT_EQ(CallClampValue(kLow, kHigh, &result), kLow);
  result = kHigh;
  EXPECT_EQ(CallClampValue(kLow, kHigh, &result), kHigh);
  result = kLow - 1.0f;
  EXPECT_EQ(CallClampValue(kLow, kHigh, &result), kLow);
  result = kHigh + 1.0f;
  EXPECT_EQ(CallClampValue(kLow, kHigh, &result), kHigh);
  result = -kInf;
  EXPECT_EQ(CallClampValue(kLow, kHigh, &result), kLow);
  result = kInf;
  EXPECT_EQ(CallClampValue(kLow, kHigh, &result), kHigh);
  result = kNan;
  EXPECT_TRUE(std::isnan(CallClampValue(kLow, kHigh, &result)));
}

// Type that defines operator< but not <=, >=, or >.
class LessThanType {
 public:
  explicit LessThanType(int value) : value_(value) {}
  friend bool operator<(const LessThanType& x, const LessThanType& y) {
    return x.value_ < y.value_;
  }
  friend bool operator==(const LessThanType& x, const LessThanType& y) {
    return x.value_ == y.value_;
  }

 private:
  int value_;
};

// Many types have only operator< defined.  Clamp should work for them.
TEST(MathUtil, ClampingLessThanType) {
  const LessThanType kLow(7);
  const LessThanType kHigh(42);

  EXPECT_EQ(LessThanType(33), MathUtil::Clamp(kLow, kHigh, LessThanType(33)));
  EXPECT_EQ(kLow, MathUtil::Clamp(kLow, kHigh, kLow));
  EXPECT_EQ(kHigh, MathUtil::Clamp(kLow, kHigh, kHigh));
  EXPECT_EQ(kLow, MathUtil::Clamp(kLow, kHigh, LessThanType(6)));
  EXPECT_EQ(kHigh, MathUtil::Clamp(kLow, kHigh, LessThanType(43)));

  LessThanType result(33);
  EXPECT_EQ(LessThanType(33), CallClampValue(kLow, kHigh, &result));
  result = kLow;
  EXPECT_EQ(kLow, CallClampValue(kLow, kHigh, &result));
  result = kHigh;
  EXPECT_EQ(kHigh, CallClampValue(kLow, kHigh, &result));
  result = LessThanType(6);
  EXPECT_EQ(kLow, CallClampValue(kLow, kHigh, &result));
  result = LessThanType(43);
  EXPECT_EQ(kHigh, CallClampValue(kLow, kHigh, &result));
}

template <typename T>
void TestOneSmoothstep(T low, T high) {
  const T step = (high - low) / 10;
  const T epsilon = T(1e-7);
  EXPECT_EQ(T(0.0), MathUtil::Smoothstep(low, high, low - step));
  EXPECT_EQ(T(0.0), MathUtil::Smoothstep(low, high, low));
  EXPECT_EQ(T(1.0), MathUtil::Smoothstep(low, high, high));
  EXPECT_EQ(T(1.0), MathUtil::Smoothstep(low, high, high + step));
  EXPECT_NEAR(T(0.5), MathUtil::Smoothstep<T>(low, high, (low + high) / 2),
              epsilon);
  EXPECT_NEAR(T(0.15625),
              MathUtil::Smoothstep<T>(low, high, 0.75 * low + 0.25 * high),
              epsilon);
  EXPECT_NEAR(T(0.84375),
              MathUtil::Smoothstep<T>(low, high, 0.25 * low + 0.75 * high),
              epsilon);
}

template <typename T>
void TestAllSmoothstep() {
  // Smoothstep should work with any range, small or large, even when low is
  // larger than high.
  TestOneSmoothstep<T>(0.0, 1.0);
  TestOneSmoothstep<T>(-1.0, 0.0);
  TestOneSmoothstep<T>(5.0, 20.0);
  TestOneSmoothstep<T>(-10.0, -2.0);
  TestOneSmoothstep<T>(45.0, -123.0);
  TestOneSmoothstep<T>(0.5, 0.5 + 1e-5);
  TestOneSmoothstep<T>(1024, 1024 - 1e-3);

  // Test numeric stability on huge numbers.
  const T O = T(0);
  const T I = T(1);
  const T max = std::numeric_limits<T>::max();
  const T lowest = std::numeric_limits<T>::lowest();
  const T denorm_min = std::numeric_limits<T>::denorm_min();
  const T min = std::numeric_limits<T>::min();
  EXPECT_EQ(O, MathUtil::Smoothstep(O, max, lowest));
  EXPECT_EQ(O, MathUtil::Smoothstep(O, max, O));
  EXPECT_EQ(I, MathUtil::Smoothstep(O, max, max));
  EXPECT_EQ(O, MathUtil::Smoothstep(lowest, O, lowest));
  EXPECT_EQ(I, MathUtil::Smoothstep(lowest, O, O));
  EXPECT_EQ(I, MathUtil::Smoothstep(lowest, O, max));

  // Test numeric stability on tiny numbers.
  EXPECT_EQ(O, MathUtil::Smoothstep(O, min, -min));
  EXPECT_EQ(O, MathUtil::Smoothstep(O, min, O));
  EXPECT_EQ(I, MathUtil::Smoothstep(O, min, min));
  EXPECT_EQ(I, MathUtil::Smoothstep(O, min, 2 * min));
  EXPECT_EQ(T(0.5), MathUtil::Smoothstep(-min, min, O));
  EXPECT_EQ(O, MathUtil::Smoothstep(O, denorm_min, -denorm_min));
  EXPECT_EQ(O, MathUtil::Smoothstep(O, denorm_min, O));
  EXPECT_EQ(I, MathUtil::Smoothstep(O, denorm_min, denorm_min));
  EXPECT_EQ(I, MathUtil::Smoothstep(O, denorm_min, 2 * denorm_min));
  EXPECT_EQ(T(0.5), MathUtil::Smoothstep(-denorm_min, denorm_min, O));
}

TEST(MathUtil, Smoothstep) {
  TestAllSmoothstep<float>();
  TestAllSmoothstep<double>();
}

// Number of arguments for each test of the CeilOfRatio methods
const int kNumRatioTestArguments = 4;

template <typename IntegralType>
void TestCeilOfRatio(const IntegralType test_data[][kNumRatioTestArguments],
                     int num_tests) {
  for (int i = 0; i < num_tests; ++i) {
    const IntegralType numerator = test_data[i][0];
    const IntegralType denominator = test_data[i][1];
    const IntegralType expected_floor = test_data[i][2];
    const IntegralType expected_ceil = test_data[i][3];
    // Make sure the two ways to compute the floor return the same thing.
    IntegralType floor_1 = MathUtil::FloorOfRatio(numerator, denominator);
    IntegralType floor_2 = MathUtil::CeilOrFloorOfRatio<IntegralType, false>(
        numerator, denominator);
    EXPECT_EQ(floor_1, floor_2);
    EXPECT_EQ(expected_floor, floor_1)
        << "FloorOfRatio fails with numerator = " << numerator
        << ", denominator = " << denominator
        << (std::numeric_limits<IntegralType>::is_signed ? "signed "
                                                         : "unsigned ")
        << (8 * sizeof(IntegralType)) << " bits";
    IntegralType ceil_1 = MathUtil::CeilOfRatio(numerator, denominator);
    IntegralType ceil_2 = MathUtil::CeilOrFloorOfRatio<IntegralType, true>(
        numerator, denominator);
    EXPECT_EQ(ceil_1, ceil_2);
    EXPECT_EQ(expected_ceil, ceil_1)
        << "CeilOfRatio fails with numerator = " << numerator
        << ", denominator = " << denominator
        << (std::numeric_limits<IntegralType>::is_signed ? "signed "
                                                         : "unsigned ")
        << (8 * sizeof(IntegralType)) << " bits";
  }
}

template <typename UnsignedIntegralType>
void TestCeilOfRatioUnsigned() {
  static_assert(std::numeric_limits<UnsignedIntegralType>::is_integer);
  static_assert(!std::numeric_limits<UnsignedIntegralType>::is_signed);
  constexpr UnsignedIntegralType kMax =
      std::numeric_limits<UnsignedIntegralType>::max();
  // clang-format off
  constexpr UnsignedIntegralType kTestData[][kNumRatioTestArguments] = {
// Numerator  | Denominator | Expected floor of ratio | Expected ceil of ratio |
      // When numerator = 0, the result is always zero
      {      0,            1,                        0,                     0 },
      {      0,            2,                        0,                     0 },
      {      0,         kMax,                        0,                     0 },
      // Try some non-extreme cases
      {      1,            1,                        1,                     1 },
      {      5,            2,                        2,                     3 },
      // Try with huge positive numerator
      {   kMax,            1,                     kMax,                  kMax },
      {   kMax,            2, kMax / 2,  kMax / 2 + ((kMax % 2 != 0) ? 1 : 0) },
      {   kMax,            3, kMax / 3,  kMax / 3 + ((kMax % 3 != 0) ? 1 : 0) },
      // Try with a huge positive denominator
      {      1,         kMax,                        0,                     1 },
      {      2,         kMax,                        0,                     1 },
      {      3,         kMax,                        0,                     1 },
      // Try with a huge numerator and a huge denominator
      {   kMax,         kMax,                        1,                     1 },
  };
  // clang-format on
  const int kNumTests = std::size(kTestData);
  TestCeilOfRatio<UnsignedIntegralType>(kTestData, kNumTests);
}

template <typename SignedInteger>
void TestCeilOfRatioSigned() {
  static_assert(std::numeric_limits<SignedInteger>::is_integer);
  static_assert(std::numeric_limits<SignedInteger>::is_signed);
  constexpr SignedInteger kMin = std::numeric_limits<SignedInteger>::min();
  constexpr SignedInteger kMax = std::numeric_limits<SignedInteger>::max();
  // clang-format off
  constexpr SignedInteger kTestData[][kNumRatioTestArguments] = {
// Numerator  | Denominator | Expected floor of ratio | Expected ceil of ratio |
      // When numerator = 0, the result is always zero
      {      0,            1,                        0,                     0 },
      {      0,           -1,                        0,                     0 },
      {      0,            2,                        0,                     0 },
      {      0,         kMin,                        0,                     0 },
      {      0,         kMax,                        0,                     0 },
      // Try all four combinations of 1 and -1
      {      1,            1,                        1,                     1 },
      {     -1,            1,                       -1,                    -1 },
      {      1,           -1,                       -1,                    -1 },
      {     -1,           -1,                        1,                     1 },
      // Try all four combinations of +/-5 divided by +/- 2
      {      5,            2,                        2,                     3 },
      {     -5,            2,                       -3,                    -2 },
      {      5,           -2,                       -3,                    -2 },
      {     -5,           -2,                        2,                     3 },
      // Try with huge positive numerator
      {   kMax,            1,                     kMax,                  kMax },
      {   kMax,           -1,                    -kMax,                 -kMax },
      {   kMax,            2, kMax / 2,  kMax / 2 + ((kMax % 2 != 0) ? 1 : 0) },
      {   kMax,            3, kMax / 3,  kMax / 3 + ((kMax % 3 != 0) ? 1 : 0) },
      // Try with huge negative numerator
      {   kMin,            1,                     kMin,                  kMin },
      {   kMin,            2, kMin / 2 - ((kMin % 2 != 0) ? 1 : 0),  kMin / 2 },
      {   kMin,            3, kMin / 3 - ((kMin % 3 != 0) ? 1 : 0),  kMin / 3 },
      // Try with a huge positive denominator
      {      1,         kMax,                        0,                     1 },
      {      2,         kMax,                        0,                     1 },
      {      3,         kMax,                        0,                     1 },
      // Try with a huge negative denominator
      {      1,         kMin,                       -1,                     0 },
      {      2,         kMin,                       -1,                     0 },
      {      3,         kMin,                       -1,                     0 },
      // Try with a huge numerator and a huge denominator
      {   kMin,         kMin,                        1,                     1 },
      {   kMin,         kMax,                       -2,                    -1 },
      {   kMax,         kMin,                       -1,                     0 },
      {   kMax,         kMax,                        1,                     1 },
  };
  // clang-format on
  const int kNumTests = std::size(kTestData);
  TestCeilOfRatio<SignedInteger>(kTestData, kNumTests);
}

// ------------------------------------------------------------------------ //
// Benchmarking CeilOrFloorOfRatio
//
// We compare with other implementations that are unsafe in general.
// ------------------------------------------------------------------------ //

// An implementation of CeilOfRatio that is correct for small enough values,
// and provided that the numerator and denominator are both positive
template <typename IntegralType>
IntegralType CeilOfRatioDenomMinusOne(IntegralType numerator,
                                      IntegralType denominator) {
  const IntegralType kOne(1);
  return (numerator + denominator - kOne) / denominator;
}

// An implementation of FloorOfRatio that is correct when the denominator is
// positive and the numerator non-negative
template <typename IntegralType>
IntegralType FloorOfRatioByDivision(IntegralType numerator,
                                    IntegralType denominator) {
  return numerator / denominator;
}

template <typename Integer, bool ComputeCeil>
Integer CeilOrFloorOfRatioArithmetic(Integer numerator, Integer denominator) {
  if (ComputeCeil) {
    return CeilOfRatioDenomMinusOne(numerator, denominator);
  } else {
    return FloorOfRatioByDivision(numerator, denominator);
  }
}

// Implementations of the CeilOrRatio function.
enum Implementation {
  PROVIDED,   // The method provided by MathUtil in the .h file
  ARITHMETIC  // Using arithmetic tricks
};

template <typename Integer, bool ComputeCeil, Implementation Implementation>
Integer CeilOrFloorOfRatio(Integer numerator, Integer denominator) {
  switch (Implementation) {  // Compile time switch
    case PROVIDED:
      return MathUtil::CeilOrFloorOfRatio<Integer, ComputeCeil>(numerator,
                                                                denominator);
    case ARITHMETIC:
      return CeilOrFloorOfRatioArithmetic<Integer, ComputeCeil>(numerator,
                                                                denominator);
    default:
      LOG(FATAL) << "This statement was supposed to be unreachable";
  }
}

// Microbenchmark for CeilOrFloorOfRatio for signed types
template <typename SignedInteger, bool ComputeCeil,
          Implementation Implementation>
void BM_CeilOrFloorOfRatio(benchmark::State& state) {
  static_assert(std::numeric_limits<SignedInteger>::is_signed,
                "type_should_be_signed");
  static_assert(std::numeric_limits<SignedInteger>::is_integer,
                "type_should_be_integer");
  // We iterate on odd denominators to avoid zero
  SignedInteger denominator =
      -((static_cast<SignedInteger>(state.max_iterations / 2)) | 1LL);
  // To make sure there's a good mix of combination of arguments, while the
  // denominators will be first negative for half the test, then positive for
  // the other half, the sign of the numerator will change at every call.
  const SignedInteger kSignBit = std::numeric_limits<SignedInteger>::min();
  SignedInteger sign_bit = kSignBit;
  int n = state.max_iterations;
  for (auto _ : state) {
    // Change the arguments
    denominator += 2;
    sign_bit ^= kSignBit;  // Swap between negative and positive
    const SignedInteger numerator = --n | sign_bit;
    benchmark::DoNotOptimize(
        CeilOrFloorOfRatio<SignedInteger, ComputeCeil, Implementation>(
            numerator, denominator));
  }
}

// Instantiations and BENCHMARK calls
// The BENCHMARK macro does not work with a templated function as argument, so
// we first need to wrap the calls into non-templated functions
void BM_CeilOfRatio_Provided_int64(benchmark::State& state) {
  BM_CeilOrFloorOfRatio<int64_t, true, PROVIDED>(state);
}
BENCHMARK(BM_CeilOfRatio_Provided_int64);
void BM_CeilOfRatio_Arithmetic_int64(benchmark::State& state) {
  BM_CeilOrFloorOfRatio<int64_t, true, ARITHMETIC>(state);
}
BENCHMARK(BM_CeilOfRatio_Arithmetic_int64);
void BM_CeilOfRatio_Provided_int32(benchmark::State& state) {
  BM_CeilOrFloorOfRatio<int32_t, true, PROVIDED>(state);
}
BENCHMARK(BM_CeilOfRatio_Provided_int32);
void BM_CeilOfRatio_Arithmetic_int32(benchmark::State& state) {
  BM_CeilOrFloorOfRatio<int32_t, true, ARITHMETIC>(state);
}
BENCHMARK(BM_CeilOfRatio_Arithmetic_int32);
void BM_CeilOfRatio_Provided_int16(benchmark::State& state) {
  BM_CeilOrFloorOfRatio<int16_t, true, PROVIDED>(state);
}
BENCHMARK(BM_CeilOfRatio_Provided_int16);
void BM_CeilOfRatio_Arithmetic_int16(benchmark::State& state) {
  BM_CeilOrFloorOfRatio<int16_t, true, ARITHMETIC>(state);
}
BENCHMARK(BM_CeilOfRatio_Arithmetic_int16);

void BM_FloorOfRatio_Provided_int64(benchmark::State& state) {
  BM_CeilOrFloorOfRatio<int64_t, false, PROVIDED>(state);
}
BENCHMARK(BM_FloorOfRatio_Provided_int64);
void BM_FloorOfRatio_Arithmetic_int64(benchmark::State& state) {
  BM_CeilOrFloorOfRatio<int64_t, false, ARITHMETIC>(state);
}
BENCHMARK(BM_FloorOfRatio_Arithmetic_int64);
void BM_FloorOfRatio_Provided_int32(benchmark::State& state) {
  BM_CeilOrFloorOfRatio<int32_t, false, PROVIDED>(state);
}
BENCHMARK(BM_FloorOfRatio_Provided_int32);
void BM_FloorOfRatio_Arithmetic_int32(benchmark::State& state) {
  BM_CeilOrFloorOfRatio<int32_t, false, ARITHMETIC>(state);
}
BENCHMARK(BM_FloorOfRatio_Arithmetic_int32);
void BM_FloorOfRatio_Provided_int16(benchmark::State& state) {
  BM_CeilOrFloorOfRatio<int16_t, false, PROVIDED>(state);
}
BENCHMARK(BM_FloorOfRatio_Provided_int16);
void BM_FloorOfRatio_Arithmetic_int16(benchmark::State& state) {
  BM_CeilOrFloorOfRatio<int16_t, false, ARITHMETIC>(state);
}
BENCHMARK(BM_FloorOfRatio_Arithmetic_int16);

// Microbenchmark for CeilOrFloorOfRatio for unsigned types
template <typename UnsignedInt, bool ComputeCeil, Implementation Implementation>
void BM_CeilOrFloorOfRatioUnsigned(benchmark::State& state) {
  static_assert(!std::numeric_limits<UnsignedInt>::is_signed,
                "type_should_be_unsigned");
  static_assert(std::numeric_limits<UnsignedInt>::is_integer,
                "type_should_be_integer");
  // We iterate on odd denominators to avoid zero
  UnsignedInt denominator = 1;
  int n = state.max_iterations;
  for (auto _ : state) {
    // Change the arguments
    denominator += 2;  // We increase by two to avoid zero in case we overflow
    benchmark::DoNotOptimize(
        CeilOrFloorOfRatio<UnsignedInt, ComputeCeil, Implementation>(
            --n, denominator));
  }
}

void BM_CeilOfRatio_Provided_uint64(benchmark::State& state) {
  BM_CeilOrFloorOfRatioUnsigned<uint64_t, true, PROVIDED>(state);
}
BENCHMARK(BM_CeilOfRatio_Provided_uint64);
void BM_CeilOfRatio_Arithmetic_uint64(benchmark::State& state) {
  BM_CeilOrFloorOfRatioUnsigned<uint64_t, true, ARITHMETIC>(state);
}
BENCHMARK(BM_CeilOfRatio_Arithmetic_uint64);
void BM_CeilOfRatio_Provided_uint32(benchmark::State& state) {
  BM_CeilOrFloorOfRatioUnsigned<uint32_t, true, PROVIDED>(state);
}
BENCHMARK(BM_CeilOfRatio_Provided_uint32);
void BM_CeilOfRatio_Arithmetic_uint32(benchmark::State& state) {
  BM_CeilOrFloorOfRatioUnsigned<uint32_t, true, ARITHMETIC>(state);
}
BENCHMARK(BM_CeilOfRatio_Arithmetic_uint32);
void BM_CeilOfRatio_Provided_uint16(benchmark::State& state) {
  BM_CeilOrFloorOfRatioUnsigned<uint16_t, true, PROVIDED>(state);
}
BENCHMARK(BM_CeilOfRatio_Provided_uint16);
void BM_CeilOfRatio_Arithmetic_uint16(benchmark::State& state) {
  BM_CeilOrFloorOfRatioUnsigned<uint16_t, true, ARITHMETIC>(state);
}
BENCHMARK(BM_CeilOfRatio_Arithmetic_uint16);

void BM_FloorOfRatio_Provided_uint64(benchmark::State& state) {
  BM_CeilOrFloorOfRatioUnsigned<uint64_t, false, PROVIDED>(state);
}
BENCHMARK(BM_FloorOfRatio_Provided_uint64);
void BM_FloorOfRatio_Arithmetic_uint64(benchmark::State& state) {
  BM_CeilOrFloorOfRatioUnsigned<uint64_t, false, ARITHMETIC>(state);
}
BENCHMARK(BM_FloorOfRatio_Arithmetic_uint64);
void BM_FloorOfRatio_Provided_uint32(benchmark::State& state) {
  BM_CeilOrFloorOfRatioUnsigned<uint32_t, false, PROVIDED>(state);
}
BENCHMARK(BM_FloorOfRatio_Provided_uint32);
void BM_FloorOfRatio_Arithmetic_uint32(benchmark::State& state) {
  BM_CeilOrFloorOfRatioUnsigned<uint32_t, false, ARITHMETIC>(state);
}
BENCHMARK(BM_FloorOfRatio_Arithmetic_uint32);
void BM_FloorOfRatio_Provided_uint16(benchmark::State& state) {
  BM_CeilOrFloorOfRatioUnsigned<uint16_t, false, PROVIDED>(state);
}
BENCHMARK(BM_FloorOfRatio_Provided_uint16);
void BM_FloorOfRatio_Arithmetic_uint16(benchmark::State& state) {
  BM_CeilOrFloorOfRatioUnsigned<uint16_t, false, ARITHMETIC>(state);
}
BENCHMARK(BM_FloorOfRatio_Arithmetic_uint16);

void TestThatCeilOfRatioDenomMinusOneIsIncorrect(int64_t numerator,
                                                 int64_t denominator,
                                                 int64_t expected_error) {
  const int64_t correct_result = MathUtil::CeilOfRatio(numerator, denominator);
  const int64_t result_by_denom_minus_one =
      CeilOfRatioDenomMinusOne(numerator, denominator);
  EXPECT_EQ(result_by_denom_minus_one + expected_error, correct_result)
      << "numerator = " << numerator << " denominator = " << denominator
      << " expected error = " << expected_error
      << " Actual difference: " << (correct_result - result_by_denom_minus_one);
}

TEST(MathUtil, DegRadConversion) {
  EXPECT_EQ(MathUtil::DegToRad(0.0), 0.0);
  EXPECT_EQ(MathUtil::DegToRad(90.0), M_PI_2);
  EXPECT_EQ(MathUtil::DegToRad(180.0), M_PI);
  EXPECT_EQ(MathUtil::DegToRad(45.0), M_PI_4);
  EXPECT_EQ(MathUtil::DegToRad(270.0), 3 * M_PI_2);
  EXPECT_EQ(MathUtil::DegToRad(-90.0), -M_PI_2);

  EXPECT_EQ(MathUtil::RadToDeg(0.0), 0.0);
  EXPECT_EQ(MathUtil::RadToDeg(M_PI), 180.0);
  EXPECT_EQ(MathUtil::RadToDeg(M_PI_2), 90.0);
  EXPECT_EQ(MathUtil::RadToDeg(M_PI_4), 45.0);
  EXPECT_EQ(MathUtil::RadToDeg(3 * M_PI_2), 270.0);
  EXPECT_EQ(MathUtil::RadToDeg(-M_PI_2), -90.0);
}

// Here we demonstrate why not to use CeilOfRatioDenomMinusOne
void TestThatCeilOfRatioDenomMinusOneIsIncorrect() {
  // It does not work with negative values
  TestThatCeilOfRatioDenomMinusOneIsIncorrect(int64_t{-1}, int64_t{-2},
                                              int64_t{-1});

  // This would also fail if given kint64max because of signed integer overflow.
}

TEST(MathUtil, CeilOfRatio) {
  TestCeilOfRatioUnsigned<uint8_t>();
  TestCeilOfRatioUnsigned<uint16_t>();
  TestCeilOfRatioUnsigned<uint32_t>();
  TestCeilOfRatioUnsigned<uint64_t>();
  TestCeilOfRatioSigned<int8_t>();
  TestCeilOfRatioSigned<int16_t>();
  TestCeilOfRatioSigned<int32_t>();
  TestCeilOfRatioSigned<int64_t>();
  TestThatCeilOfRatioDenomMinusOneIsIncorrect();
}

TEST(MathUtil, NaN) {
  EXPECT_TRUE(std::isnan(std::numeric_limits<double>::quiet_NaN()));
}

struct NormalizeRangeParams {
  double input_value;
  double range_centre;
  double expected_value;
};

TEST(MathUtil, NormalizeDegrees) {
  const double epsilon = 1e-6;

  const NormalizeRangeParams params[] = {
      {
          280.0,
          0.0,
          -80.0,
      },
      {
          280.0,
          180.0,
          280,
      },
      {
          1081.0,
          0.0,
          1.0,
      },
      {
          -729.0,
          0.0,
          -9.0,
      },
      {
          -729.0,
          180.0,
          351.0,
      },
      {
          360.0 - 1e-12,
          0.0,
          1e-12,
      },
      {
          1e-12,
          360.0,
          360.0 - 1e-12,
      },
  };

  for (const auto& test : params) {
    EXPECT_NEAR(MathUtil::NormalizeDegrees(test.input_value, test.range_centre),
                test.expected_value, epsilon);
  }
}

void BM_NormalizeDegrees0(::benchmark::State& state) {
  const double input_value = (state.range(0) - 10) * 100.5;
  for (const auto _ : state) {
    benchmark::DoNotOptimize(input_value);
    const auto result = MathUtil::NormalizeDegrees(input_value, 0.0);
    benchmark::DoNotOptimize(result);
  }
}

void BM_NormalizeDegrees180(::benchmark::State& state) {
  const double input_value = (state.range(0) - 10) * 100.5;
  for (const auto _ : state) {
    benchmark::DoNotOptimize(input_value);
    const auto result = MathUtil::NormalizeDegrees(input_value, 180.0);
    benchmark::DoNotOptimize(result);
  }
}

BENCHMARK(BM_NormalizeDegrees0)->DenseRange(0, 20);
BENCHMARK(BM_NormalizeDegrees180)->DenseRange(0, 20);

TEST(MathUtil, NormalizeRadians) {
  const double epsilon = 1e-6;

  const NormalizeRangeParams params[] = {
      {
          MathUtil::kPi + 1.0,
          0.0,
          -MathUtil::kPi + 1.0,
      },
      {
          -3.15 * MathUtil::kPi,
          MathUtil::kPi / 2.0,
          0.85 * MathUtil::kPi,
      },
      {
          6.67 * MathUtil::kPi,
          0.0,
          0.67 * MathUtil::kPi,
      },
      {
          3.0,
          0.0,
          3.0,
      },
      {
          2.0 * MathUtil::kPi - 1e-12,
          0.0,
          1e-12,
      },
      {
          1e-12,
          2.0 * MathUtil::kPi,
          2.0 * MathUtil::kPi - 1e-12,
      },
  };

  for (const auto& test : params) {
    EXPECT_NEAR(MathUtil::NormalizeRadians(test.input_value, test.range_centre),
                test.expected_value, epsilon);
  }
}

TEST(MathUtil, NonnegativeMod) {
  // Test compliance with the standard -- integer division is supposed to
  // truncate towards zero, and integer mod is defined accordingly.
  ASSERT_THAT(-8 / 5, Eq(-1));
  ASSERT_THAT(-8 % 5, Eq(-3));
  // Now test the function itself.
  EXPECT_THAT(MathUtil::NonnegativeMod(4, 3), Eq(1));
  EXPECT_THAT(MathUtil::NonnegativeMod(-5, 3), Eq(1));
  EXPECT_THAT(MathUtil::NonnegativeMod(9, 3), Eq(0));
  EXPECT_THAT(MathUtil::NonnegativeMod(-9, 3), Eq(0));
  EXPECT_THAT(MathUtil::NonnegativeMod(0, 3), Eq(0));
  EXPECT_THAT(MathUtil::NonnegativeMod(12U, 5U), Eq(2U));
  int64_t a_int64 = 10000000002;
  int64_t b_int64 = 10000000000;
  int64_t two_int64 = 2;
  EXPECT_THAT(MathUtil::NonnegativeMod(a_int64, b_int64), Eq(two_int64));
  EXPECT_THAT(MathUtil::NonnegativeMod(-a_int64, b_int64),
              Eq(b_int64 - two_int64));
  EXPECT_THAT(MathUtil::NonnegativeMod(b_int64, b_int64), Eq(0));
  EXPECT_THAT(MathUtil::NonnegativeMod(-b_int64, b_int64), Eq(0));
}

TEST(MathUtil, DecomposeDouble) {
  using Limits = std::numeric_limits<double>;

  struct TestItem {
    double value;
    int64_t expected_mantissa;
    int expected_exponent;
  };

  static const TestItem kItems[] = {
      {0.0, 0, -1074},
      {Limits::denorm_min(), 1, -1074},
      {Limits::min() - Limits::denorm_min(), (1LL << 52) - 1, -1074},
      {Limits::min(), (1LL << 52), -1074},
      {1.0 / (1LL << 52), (1LL << 52), -104},
      {1.0, (1LL << 52), -52},
      {MathUtil::kPi, 7074237752028440, -51},
      {100, (100LL << 46), -46},
      {static_cast<double>(std::numeric_limits<uint64_t>::max()),
       (std::numeric_limits<uint64_t>::max() >> 12) + 1, 12},
      {Limits::max(), (1LL << 53) - 1, 971},
      {Limits::infinity(), std::numeric_limits<int64_t>::max(),
       std::numeric_limits<int>::max()},
  };
  for (const TestItem& item : kItems) {
    LOG(INFO) << "Testing ExtractMantissaAndExponent(" << item.value << ")...";

    MathUtil::DoubleParts parts = MathUtil::Decompose(item.value);
    EXPECT_EQ(item.expected_mantissa, parts.mantissa);
    EXPECT_EQ(item.expected_exponent, parts.exponent);
    EXPECT_EQ(item.value,
              std::ldexp(static_cast<double>(parts.mantissa), parts.exponent));
    EXPECT_EQ(item.value, static_cast<double>(parts.mantissa) *
                              std::ldexp(1.0, parts.exponent));

    parts = MathUtil::Decompose(-item.value);
    EXPECT_EQ(-item.expected_mantissa, parts.mantissa);
    EXPECT_EQ(item.expected_exponent, parts.exponent);
    EXPECT_EQ(-item.value,
              std::ldexp(static_cast<double>(parts.mantissa), parts.exponent));
    EXPECT_EQ(-item.value, static_cast<double>(parts.mantissa) *
                               std::ldexp(1.0, parts.exponent));
  }

  MathUtil::DoubleParts parts = MathUtil::Decompose(Limits::quiet_NaN());
  EXPECT_EQ(0, parts.mantissa);
  EXPECT_EQ(std::numeric_limits<int>::max(), parts.exponent);
  EXPECT_TRUE(std::isnan(static_cast<double>(parts.mantissa) *
                         std::ldexp(1.0, parts.exponent)));
}

TEST(MathUtil, DecomposeFloat) {
  using Limits = std::numeric_limits<float>;

  struct TestItem {
    float value;
    int32_t expected_mantissa;
    int expected_exponent;
  };

  static const TestItem kItems[] = {
      {0.0, 0, -149},
      {1.2, 10066330, -23},
      {-1.2, -10066330, -23},
      {100, 100 << 17, -17},
      {MathUtil::kPi, 13176795, -22},
      {Limits::denorm_min(), 1, -149},
      {Limits::min() - Limits::denorm_min(), (1 << 23) - 1, -149},
      {Limits::min(), 1 << 23, -149},
      {Limits::max(), 16777215, 104},
      {Limits::infinity(), std::numeric_limits<int32_t>::max(),
       std::numeric_limits<int>::max()},
  };
  for (const TestItem& item : kItems) {
    LOG(INFO) << "Testing ExtractMantissaAndExponent(" << item.value << ")...";

    MathUtil::FloatParts parts = MathUtil::Decompose(item.value);
    EXPECT_EQ(item.expected_mantissa, parts.mantissa);
    EXPECT_EQ(item.expected_exponent, parts.exponent);
    EXPECT_EQ(item.value,
              std::ldexp(static_cast<float>(parts.mantissa), parts.exponent));
    EXPECT_EQ(item.value, static_cast<float>(parts.mantissa) *
                              std::ldexp(1.0, parts.exponent));

    parts = MathUtil::Decompose(-item.value);
    EXPECT_EQ(-item.expected_mantissa, parts.mantissa);
    EXPECT_EQ(item.expected_exponent, parts.exponent);
    EXPECT_EQ(-item.value,
              std::ldexp(static_cast<float>(parts.mantissa), parts.exponent));
    EXPECT_EQ(-item.value, static_cast<float>(parts.mantissa) *
                               std::ldexp(1.0, parts.exponent));
  }

  MathUtil::FloatParts parts = MathUtil::Decompose(Limits::quiet_NaN());
  EXPECT_EQ(0, parts.mantissa);
  EXPECT_EQ(std::numeric_limits<int>::max(), parts.exponent);
  EXPECT_TRUE(std::isnan(static_cast<float>(parts.mantissa) *
                         std::ldexp(1.0, parts.exponent)));
}

template <typename T>
void TestOneIPowN() {
  const T one{1};
  for (int i = 0; i < 1024; ++i) {
    // Computations are exact.
    EXPECT_EQ(MathUtil::IPow(one, i), one);
  }
}

template <typename T>
void TestTwoIPowN() {
  int limit = std::is_integral<T>::value ? std::numeric_limits<T>::digits : 63;
  for (int i = 0; i < limit; ++i) {
    // Computations are exact.
    EXPECT_EQ(MathUtil::IPow(T{2}, i), static_cast<T>(uint64_t{1} << i));
  }
}

template <typename T>
void TestTenIPowN() {
  LOG(INFO) << "Testing 10^N for type " << typeid(T).name();
  int limit = std::is_integral<T>::value
                  ? std::numeric_limits<T>::digits10
                  : std::numeric_limits<uint64_t>::digits10;
  LOG(INFO) << "IPow10 of " << limit << " is " << MathUtil::IPow10(limit);
  for (int i = 0; i <= limit; ++i) {
    // Computations are exact.
    EXPECT_EQ(MathUtil::IPow(T{10}, i), static_cast<T>(MathUtil::IPow10(i)));
  }
}

template <typename T>
void TestFloatIPow(const int max_exponent, const T start, const T end,
                   const T step) {
  for (T f = start; f < end; f += step) {
    for (int i = 0; i < max_exponent; ++i) {
      EXPECT_FLOAT_EQ(MathUtil::IPow(f, i), std::pow(f, i));
    }
  }
}

TEST(MathUtil, IPow) {
  TestOneIPowN<double>();
  TestOneIPowN<float>();
  TestOneIPowN<int>();
  TestOneIPowN<int64_t>();
  TestTwoIPowN<double>();
  TestTwoIPowN<float>();
  TestTwoIPowN<int>();
  TestTwoIPowN<int64_t>();
  TestTenIPowN<double>();
  TestTenIPowN<float>();
  TestTenIPowN<int>();
  TestTenIPowN<int64_t>();

  EXPECT_EQ(MathUtil::IPow(3, 0), 1);
  EXPECT_EQ(MathUtil::IPow(3, 1), 3);
  EXPECT_EQ(MathUtil::IPow(3, 2), 9);
  EXPECT_EQ(MathUtil::IPow(3, 3), 27);
  EXPECT_EQ(MathUtil::IPow(3, 4), 81);
  EXPECT_EQ(MathUtil::IPow(3, 5), 243);

  TestFloatIPow<float>(13, -16.0f, 16.0f, 1.0f / 8);
  TestFloatIPow<double>(13, -16.0, 16.0, 1.0 / 8);

  TestFloatIPow<float>(13, -1.0f / (1 << 12), -1.0f / (1 << 12),
                       1.0f / (1 << 16));
  TestFloatIPow<double>(13, -1.0 / (1 << 12), -1.0 / (1 << 12),
                        1.0 / (1 << 16));
}

TEST(MathUtil, IPowEdgeCases) {
  constexpr const double kInf = std::numeric_limits<double>::infinity();

  EXPECT_EQ(MathUtil::IPow(-12345.0, 79), -kInf);
  EXPECT_EQ(MathUtil::IPow(-12345.0, 80), +kInf);

  // The semantics of the edge cases that follow  are defined in the standard:
  // http://en.cppreference.com/w/cpp/numeric/math/pow for a summary.

  // 1 - These edge cases apply.
  // pow(+0, exp), where exp is a positive odd integer, returns +0
  EXPECT_EQ(MathUtil::IPow(+0.0, 3), +0.0);
  // pow(-0, exp), where exp is a positive odd integer, returns -0
  EXPECT_EQ(MathUtil::IPow(-0.0, 3), -0.0);
  // pow(±0, exp), where exp is positive non-integer or a positive even integer,
  // returns +0
  EXPECT_EQ(MathUtil::IPow(+0.0, 42), +0.0);
  EXPECT_EQ(MathUtil::IPow(-0.0, 42), +0.0);
  // pow(base, ±0) returns 1 for any base, even when base is NaN
  EXPECT_EQ(MathUtil::IPow(-kInf, 0.0), 1.0);
  EXPECT_EQ(MathUtil::IPow(-2.0, 0.0), 1.0);
  EXPECT_EQ(MathUtil::IPow(-1.0, 0.0), 1.0);
  EXPECT_EQ(MathUtil::IPow(-0.0, 0.0), 1.0);
  EXPECT_EQ(MathUtil::IPow(+0.0, 0.0), 1.0);
  EXPECT_EQ(MathUtil::IPow(+1.0, 0.0), 1.0);
  EXPECT_EQ(MathUtil::IPow(+2.0, 0.0), 1.0);
  EXPECT_EQ(MathUtil::IPow(+kInf, 0.0), 1.0);
  EXPECT_EQ(MathUtil::IPow(std::numeric_limits<double>::quiet_NaN(), 0.0), 1.0);
  // pow(-∞, exp) returns -∞ if exp is a positive odd integer
  EXPECT_EQ(MathUtil::IPow(-kInf, 43), -kInf);
  // pow(-∞, exp) returns +∞ if exp is a positive non-integer or even integer
  EXPECT_EQ(MathUtil::IPow(-kInf, 42), +kInf);
  // pow(+∞, exp) returns +∞ for any positive exp
  EXPECT_EQ(MathUtil::IPow(+kInf, 42), +kInf);
  EXPECT_EQ(MathUtil::IPow(+kInf, 43), +kInf);

  EXPECT_EQ(pow(std::numeric_limits<int>::max() >> 0, 1),
            MathUtil::IPow(std::numeric_limits<int>::max() >> 0, 1));
  EXPECT_EQ(pow(std::numeric_limits<int>::max() >> 16, 2),
            MathUtil::IPow(std::numeric_limits<int>::max() >> 16, 2));

  // 2 - These do not apply due to the restricted exp range.
  // pow(+0, exp), where exp is a negative odd integer, returns +∞ and raises
  // FE_DIVBYZERO pow(-0, exp), where exp is a negative odd integer, returns -∞
  // and raises FE_DIVBYZERO pow(±0, exp), where exp is negative, finite, and is
  // an even integer or a non-integer, returns +∞ and raises FE_DIVBYZERO
  // pow(-1, ±∞) returns 1
  // pow(+1, exp) returns 1 for any exp, even when exp is NaN
  // pow(±0, -∞) returns +∞ and may raise FE_DIVBYZERO
  // pow(base, exp) returns NaN and raises FE_INVALID if base is finite and
  // negative and exp is finite and non-integer. pow(base, -∞) returns +∞ for
  // any |base|<1 pow(base, -∞) returns +0 for any |base|>1 pow(base, +∞)
  // returns +0 for any |base|<1 pow(base, +∞) returns +∞ for any |base|>1
  // pow(-∞, exp) returns -0 if exp is a negative odd integer
  // pow(-∞, exp) returns +0 if exp is a negative non-integer or even integer
  // pow(+∞, exp) returns +0 for any negative exp
}

void BM_IPow(::benchmark::State& state) {
  const int exp = state.range(0);
  double base = 1.012345;
  for (const auto _ : state) {
    benchmark::DoNotOptimize(base);
    const auto result = MathUtil::IPow(base, exp);
    benchmark::DoNotOptimize(result);
  }
}

void BM_pow(::benchmark::State& state) {
  const int exp = state.range(0);
  double base = 1.012345;
  for (const auto _ : state) {
    benchmark::DoNotOptimize(base);
    const auto result = pow(base, exp);
    benchmark::DoNotOptimize(result);
  }
}

BENCHMARK(BM_pow)->DenseRange(0, 64);
BENCHMARK(BM_IPow)->DenseRange(0, 64);

// Number of arguments for each test of MulDiv
const int kMulDivTestArguments = 5;

template <typename IntegralType>
void TestMulDiv(const IntegralType test_data[][kMulDivTestArguments],
                int num_tests) {
  for (int i = 0; i < num_tests; ++i) {
    const IntegralType a = test_data[i][0];
    const IntegralType b = test_data[i][1];
    const IntegralType d = test_data[i][2];
    const MathUtil::DivisionResult<IntegralType> expected = {test_data[i][3],
                                                             test_data[i][4]};
    auto result = MathUtil::MulDiv(a, b, d);
    EXPECT_EQ(result, expected)
        << "MulDiv fails with a = " << +a << ", b = " << +b << ", d = " << +d;
  }
}

template <typename IntegralType>
void TestMulDiv() {
  static_assert(std::numeric_limits<IntegralType>::is_integer);
  constexpr IntegralType kMax = std::numeric_limits<IntegralType>::max();
  // clang-format off
  constexpr IntegralType kTestData[][kMulDivTestArguments] = {
     // |    a    |    b    |    d    | Expected quotient | Expected remainder |
      // When a = 0 or b = 0, the result is always zero.
      {          0,        1,        8,                  0,                 0 },
      {          0,        2,        1,                  0,                 0 },
      {          0,     kMax,        3,                  0,                 0 },
      {          1,        0,        8,                  0,                 0 },
      {          2,        0,     kMax,                  0,                 0 },
      {       kMax,        0,        3,                  0,                 0 },
      // Try with assorted two-digit values (taken from pi's first 60 digits,
      // except cases where the quotient would overflow a signed byte)
      {         31,       41,       59,                 21,                32 },
      {         26,       53,       58,                 23,                44 },
      {         84,       62,       64,                 81,                24 },
      {         33,       83,       27,                101,                12 },
      {         95,        2,       88,                  2,                14 },
      {         51,        5,       82,                  3,                 9 },
      {          9,       74,       94,                  7,                 8 },
      // Try with huge positive numerator (not overflowing)
      {   kMax/100,      100,        1,     (kMax/100)*100,                 0 },
      {        100, kMax/100,        1,     (kMax/100)*100,                 0 },
      {   kMax/100,      100,        2,      (kMax/100)*50,                 0 },
      {        100, kMax/100,        2,      (kMax/100)*50,                 0 },
      // Try with huge positive denominator
      {         10,       10,     kMax,                  0,               100 },
      // Try with huge positive numerator and denominator
      {       kMax,       10,     kMax,                 10,                 0 },
      {         10,     kMax,     kMax,                 10,                 0 },
      {   kMax - 1, kMax - 1,     kMax,           kMax - 2,                 1 },
      {   kMax - 1, kMax - 2,     kMax,           kMax - 3,                 2 },
      {   kMax - 3, kMax - 2,     kMax,           kMax - 5,                 6 },
  };
  // clang-format on
  const int kNumTests = std::size(kTestData);
  TestMulDiv<IntegralType>(kTestData, kNumTests);
}

TEST(MathUtil, MulDiv) {
  TestMulDiv<int8_t>();
  TestMulDiv<uint8_t>();
  TestMulDiv<int16_t>();
  TestMulDiv<uint16_t>();
  TestMulDiv<int32_t>();
  TestMulDiv<uint32_t>();
  TestMulDiv<int64_t>();
  TestMulDiv<uint64_t>();
}

// Number of arguments for each test of the Ceil/FloorOfPercentage methods
const int kNumPercentageTestArguments = 4;

template <typename IntegralType>
void TestCeilAndFloorOfPercentage(
    const IntegralType test_data[][kNumPercentageTestArguments],
    int num_tests) {
  for (int i = 0; i < num_tests; ++i) {
    const IntegralType numerator = test_data[i][0];
    const IntegralType denominator = test_data[i][1];
    const IntegralType expected_floor = test_data[i][2];
    const IntegralType expected_ceil = test_data[i][3];
    IntegralType floor =
        MathUtil::FloorOfPercentage<IntegralType>(numerator, denominator);
    EXPECT_EQ(expected_floor, floor)
        << "FloorOfPercentage fails with numerator = " << +numerator
        << ", denominator = " << +denominator;
    IntegralType ceil =
        MathUtil::CeilOfPercentage<IntegralType>(numerator, denominator);
    EXPECT_EQ(expected_ceil, ceil)
        << "CeilOfPercentage fails with numerator = " << +numerator
        << ", denominator = " << +denominator;
  }
}

template <typename IntegralType>
void TestCeilAndFloorOfPercentage() {
  static_assert(std::numeric_limits<IntegralType>::is_integer);
  constexpr IntegralType kMax = std::numeric_limits<IntegralType>::max();
  // clang-format off
  constexpr IntegralType kTestData[][kNumPercentageTestArguments] = {
// Numerator  | Denominator | Expected floor of ratio | Expected ceil of ratio |
      // When numerator = 0, the result is always zero
      {      0,            1,                        0,                     0 },
      {      0,            2,                        0,                     0 },
      // Try 1, 5/10, 2/100, and 99/100.
      {      1,            1,                      100,                   100 },
      {      5,           10,                       50,                    50 },
      {      2,          100,                        2,                     2 },
      {     99,          100,                       99,                    99 },
      // Try 3/7, 5/7, and 3/70.
      {      3,            7,                       42,                    43 },
      {      5,            7,                       71,                    72 },
      {      3,           70,                        4,                     5 },
      // Try with huge positive numerator
      { kMax/100,          1,           (kMax/100)*100,        (kMax/100)*100 },
      { kMax/100,          2,            (kMax/100)*50,         (kMax/100)*50 },
      // Try with a huge positive denominator
      {      1,         kMax,                        0,                     1 },
      {      2,         kMax,                 200/kMax,          200/kMax + 1 },
      {      3,         kMax,                 300/kMax,          300/kMax + 1 },
      // Try with a huge numerator and a huge denominator
      {   kMax,         kMax,                      100,                   100 },
      { kMax - 1,       kMax,                       99,                   100 },
  };
  // clang-format on
  const int kNumTests = std::size(kTestData);
  TestCeilAndFloorOfPercentage<IntegralType>(kTestData, kNumTests);
}

TEST(MathUtil, CeilAndFloorOfPercentage) {
  TestCeilAndFloorOfPercentage<int8_t>();
  TestCeilAndFloorOfPercentage<uint8_t>();
  TestCeilAndFloorOfPercentage<int16_t>();
  TestCeilAndFloorOfPercentage<uint16_t>();
  TestCeilAndFloorOfPercentage<int32_t>();
  TestCeilAndFloorOfPercentage<uint32_t>();
  TestCeilAndFloorOfPercentage<int64_t>();
  TestCeilAndFloorOfPercentage<uint64_t>();
}

// Microbenchmark for Ceil/FloorOfPercentage
template <typename IntegralType, bool ComputeCeil>
void BM_CeilOrFloorOfPercentage(benchmark::State& state) {
  static_assert(std::numeric_limits<IntegralType>::is_integer,
                "type_should_be_integer");
  const IntegralType max_numerator = MathUtil::Clamp<IntegralType>(
      0, std::numeric_limits<IntegralType>::max() / 100, state.max_iterations);
  // To make sure there's a good mix of combination of arguments, while the
  // denominators will increase steadily through the test, the numerator will
  // alternate between large and small.
  bool small = false;
  uint64_t n = 1;
  IntegralType denominator = IntegralType{2};
  IntegralType numerator = max_numerator - IntegralType{1};
  for (auto _ : state) {
    if constexpr (ComputeCeil) {
      benchmark::DoNotOptimize(
          MathUtil::CeilOfPercentage<IntegralType>(numerator, denominator));
    } else {
      benchmark::DoNotOptimize(
          MathUtil::FloorOfPercentage<IntegralType>(numerator, denominator));
    }
    // Change the arguments
    //   numerator: max - 1 -> 2 -> max - 3 -> 4 -> ...
    // denominator:       2 -> 3 ->       4 -> 5 -> ...
    state.PauseTiming();
    small = !small;  // Swap between large and small
    n++;
    if (n >= std::numeric_limits<IntegralType>::max()) {
      n = 1;
    }
    numerator = MathUtil::Clamp<IntegralType>(1, max_numerator,
                                              small ? n : max_numerator - n);
    denominator = static_cast<IntegralType>(n + 1);
    state.ResumeTiming();
  }
}

// Instantiations and BENCHMARK calls
// The BENCHMARK macro does not work with a templated function as argument, so
// we first need to wrap the calls into non-templated functions
void BM_CeilOfPercentage_int64(benchmark::State& state) {
  BM_CeilOrFloorOfPercentage<int64_t, true>(state);
}
BENCHMARK(BM_CeilOfPercentage_int64);
void BM_CeilOfPercentage_int32(benchmark::State& state) {
  BM_CeilOrFloorOfPercentage<int32_t, true>(state);
}
BENCHMARK(BM_CeilOfPercentage_int32);
void BM_CeilOfPercentage_int16(benchmark::State& state) {
  BM_CeilOrFloorOfPercentage<int16_t, true>(state);
}
BENCHMARK(BM_CeilOfPercentage_int16);

void BM_FloorOfPercentage_int64(benchmark::State& state) {
  BM_CeilOrFloorOfPercentage<int64_t, false>(state);
}
BENCHMARK(BM_FloorOfPercentage_int64);
void BM_FloorOfPercentage_int32(benchmark::State& state) {
  BM_CeilOrFloorOfPercentage<int32_t, false>(state);
}
BENCHMARK(BM_FloorOfPercentage_int32);
void BM_FloorOfPercentage_int16(benchmark::State& state) {
  BM_CeilOrFloorOfPercentage<int16_t, false>(state);
}
BENCHMARK(BM_FloorOfPercentage_int16);

template <typename IntegralType>
struct MulDivInstance {
  IntegralType a;
  IntegralType b;
  IntegralType d;
};

template <typename IntegralType>
auto SafeMulDivInstance() {
  // A safe MulDiv invocation has parameters (a, b, d) such that:
  //   1. a,b >= 0,
  //   2. d > 0, and
  //   3. floor(a*b/d) does not overflow IntegralType (e.g., a*b/d < maxint+1).
  // We ensure this by restricting b to the range [0, ((maxint+1)*d - 1)/a].
  return fuzztest::FlatMap(
      [](IntegralType a, IntegralType d) {
        if (a == 0) {
          return StructOf<MulDivInstance<IntegralType>>(
              Just(a), NonNegative<IntegralType>(), Just(d));
        }

        constexpr auto kPosMaxInt =
            static_cast<absl::int128>(std::numeric_limits<IntegralType>::max());
        const absl::int128 max_numerator =
            (kPosMaxInt + 1) * static_cast<absl::int128>(d) - 1;
        const auto max_b = static_cast<IntegralType>(
            std::clamp(max_numerator / a, absl::int128{0}, kPosMaxInt));
        return StructOf<MulDivInstance<IntegralType>>(
            Just(a), InRange(IntegralType{0}, static_cast<IntegralType>(max_b)),
            Just(d));
      },
      NonNegative<IntegralType>(), Positive<IntegralType>());
}

template <typename IntegralType>
void CheckMulDiv(MulDivInstance<IntegralType> instance) {
  const auto& [a, b, d] = instance;
  const absl::int128 numerator =
      static_cast<absl::int128>(a) * static_cast<absl::int128>(b);
  const absl::int128 denominator = static_cast<absl::int128>(d);

  MathUtil::DivisionResult<IntegralType> result = MathUtil::MulDiv(a, b, d);
  EXPECT_EQ(result.quotient, numerator / denominator);
  EXPECT_EQ(result.remainder, numerator % denominator);
}

constexpr auto& FuzzMulDiv_int8 = CheckMulDiv<int8_t>;
FUZZ_TEST(MathUtil, FuzzMulDiv_int8).WithDomains(SafeMulDivInstance<int8_t>());

constexpr auto& FuzzMulDiv_int16 = CheckMulDiv<int16_t>;
FUZZ_TEST(MathUtil, FuzzMulDiv_int16)
    .WithDomains(SafeMulDivInstance<int16_t>());

constexpr auto& FuzzMulDiv_int32 = CheckMulDiv<int32_t>;
FUZZ_TEST(MathUtil, FuzzMulDiv_int32)
    .WithDomains(SafeMulDivInstance<int32_t>());

constexpr auto& FuzzMulDiv_int64 = CheckMulDiv<int64_t>;
FUZZ_TEST(MathUtil, FuzzMulDiv_int64)
    .WithDomains(SafeMulDivInstance<int64_t>());

}  // namespace
