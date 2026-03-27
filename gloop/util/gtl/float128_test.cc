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

#include "gloop/util/gtl/float128.h"

#include <bit>
#include <cmath>
#include <compare>
#include <iomanip>
#include <ios>
#include <limits>
#include <optional>
#include <sstream>
#include <type_traits>

#include "absl/base/config.h"
#include "absl/numeric/int128.h"
#include "absl/strings/str_format.h"
#include "gloop/util/gtl/float128_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace gtl {
namespace {

using ::testing::Gt;
using ::testing::IsNan;
using ::testing::NanSensitiveDoubleEq;
using ::testing::Optional;
using ::testing::ResultOf;

constexpr float kPiFloat = 3.1415927f;
constexpr double kPi = 3.1415926535897932;
constexpr long double kPiLong = 3.14159265358979323846264338327950288L;

// NOLINTBEGIN(google-runtime-int)
// NOLINTBEGIN(runtime/int)

TEST(Float128FromStringTest, SimpleValues) {
  EXPECT_THAT(Float128::FromString("0"), Optional(Float128(0.0)));
  EXPECT_THAT(Float128::FromString("1.25"), Optional(Float128(1.25)));
  EXPECT_THAT(Float128::FromString("+10"), Optional(Float128(10.0)));
  EXPECT_THAT(Float128::FromString("-1532.625e5"),
              Optional(Float128(-1532.625e5)));
  EXPECT_THAT(Float128::FromString("100e10"), Optional(Float128(100.0e10)));
  EXPECT_THAT(Float128::FromString("100E+10"), Optional(Float128(100.0e10)));
  EXPECT_THAT(Float128::FromString("100E-2"), Optional(Float128(100.0e-2)));
}

TEST(Float128FromStringTest, AsciiWhitespaceCanSurround) {
  EXPECT_THAT(Float128::FromString("  123  "), Optional(Float128(123.0)));
}

TEST(Float128FromStringTest, HexFloat) {
  EXPECT_THAT(Float128::FromString("0x1.2453348p+27"),
              Optional(Float128(1532.625e5)));
  EXPECT_THAT(Float128::FromString("0X1.2453348P+27"),
              Optional(Float128(1532.625e5)));
}

TEST(Float128FromStringTest, Infinity) {
  constexpr bool (*IsInfinite)(gtl::Float128) = isinf;  // pick overload
  EXPECT_THAT(Float128::FromString("inf"),
              Optional(ResultOf(IsInfinite, true)));
  EXPECT_THAT(Float128::FromString("INF"),
              Optional(ResultOf(IsInfinite, true)));
  EXPECT_THAT(Float128::FromString("infinity"),
              Optional(ResultOf(IsInfinite, true)));
  EXPECT_THAT(Float128::FromString("INFINITY"),
              Optional(ResultOf(IsInfinite, true)));
  EXPECT_THAT(Float128::FromString("Infinity"),
              Optional(ResultOf(IsInfinite, true)));

  EXPECT_THAT(Float128::FromString("+inf"),
              Optional(ResultOf(IsInfinite, true)));

  EXPECT_THAT(Float128::FromString("-inf"),
              Optional(ResultOf(IsInfinite, true)));
}

TEST(Float128FromStringTest, Nan) {
  constexpr bool (*IsNan)(gtl::Float128) = isnan;  // pick overload
  EXPECT_THAT(Float128::FromString("nan"), Optional(ResultOf(IsNan, true)));
  EXPECT_THAT(Float128::FromString("NAN"), Optional(ResultOf(IsNan, true)));
  EXPECT_THAT(Float128::FromString("NaN"), Optional(ResultOf(IsNan, true)));
  EXPECT_THAT(Float128::FromString("nan(foobar)"),
              Optional(ResultOf(IsNan, true)));
  EXPECT_THAT(Float128::FromString("NAN(foobar)"),
              Optional(ResultOf(IsNan, true)));
  EXPECT_THAT(Float128::FromString("NaN(foobar)"),
              Optional(ResultOf(IsNan, true)));

  EXPECT_THAT(Float128::FromString("+nan"), Optional(ResultOf(IsNan, true)));

  EXPECT_THAT(Float128::FromString("-nan"), Optional(ResultOf(IsNan, true)));
}

TEST(Float128FromStringTest, Fail) {
  EXPECT_EQ(Float128::FromString("This isn't a floating-point value!"),
            std::nullopt);
  EXPECT_EQ(Float128::FromString("blah blah blah 1.25 blah blah blah"),
            std::nullopt);
}

TEST(Float128FromStringTest, ParsesFullPrecision) {
  std::optional<Float128> pi =
      Float128::FromString("3.14159265358979323846264338327950288");
  ASSERT_TRUE(pi.has_value());

  EXPECT_GT(pi, kPi);
  EXPECT_LT(pi, std::nextafter(kPi, std::numeric_limits<double>::infinity()));
}

TEST(Float128FromStringTest, ParsesFullRange) {
  EXPECT_THAT(Float128::FromString("1.0e400"),
              Optional(Gt(std::numeric_limits<double>::max())));
}

TEST(Float128ConstructorTest, ValueInitializationZeroizes) {
  EXPECT_EQ(Float128{}, 0.0);
}

TEST(Float128ConversionTest, IntRoundTrips) {
  EXPECT_EQ(static_cast<int>(Float128(std::numeric_limits<int>::lowest())),
            std::numeric_limits<int>::lowest());
  EXPECT_EQ(static_cast<int>(Float128(0)), 0);
  EXPECT_EQ(static_cast<int>(Float128(std::numeric_limits<int>::max())),
            std::numeric_limits<int>::max());
}

TEST(Float128ConversionTest, UnsignedIntRoundTrips) {
  EXPECT_EQ(static_cast<unsigned int>(Float128(0U)), 0U);
  EXPECT_EQ(static_cast<unsigned int>(
                Float128(std::numeric_limits<unsigned int>::max())),
            std::numeric_limits<unsigned int>::max());
}

TEST(Float128ConversionTest, LongRoundTrips) {
  EXPECT_EQ(static_cast<long>(Float128(std::numeric_limits<long>::lowest())),
            std::numeric_limits<long>::lowest());
  EXPECT_EQ(static_cast<long>(Float128(0L)), 0L);
  EXPECT_EQ(static_cast<long>(Float128(std::numeric_limits<long>::max())),
            std::numeric_limits<long>::max());
}

TEST(Float128ConversionTest, UnsignedLongRoundTrips) {
  EXPECT_EQ(static_cast<unsigned long>(Float128(0UL)), 0UL);
  EXPECT_EQ(static_cast<unsigned long>(
                Float128(std::numeric_limits<unsigned long>::max())),
            std::numeric_limits<unsigned long>::max());
}

TEST(Float128ConversionTest, LongLongRoundTrips) {
  EXPECT_EQ(static_cast<long long>(
                Float128(std::numeric_limits<long long>::lowest())),
            std::numeric_limits<long long>::lowest());
  EXPECT_EQ(static_cast<long long>(Float128(0LL)), 0LL);
  EXPECT_EQ(
      static_cast<long long>(Float128(std::numeric_limits<long long>::max())),
      std::numeric_limits<long long>::max());
}

TEST(Float128ConversionTest, UnsignedLongLongRoundTrips) {
  EXPECT_EQ(static_cast<unsigned long long>(Float128(0ULL)), 0ULL);
  EXPECT_EQ(static_cast<unsigned long long>(
                Float128(std::numeric_limits<unsigned long long>::max())),
            std::numeric_limits<unsigned long long>::max());
}

TEST(Float128ConversionTest, Int128RoundTrips) {
#ifdef ABSL_HAVE_INTRINSIC_INT128
  EXPECT_EQ(static_cast<__int128>(Float128((__int128{1} << 64) + 1)),
            (__int128{1} << 64) + 1);
  EXPECT_EQ(static_cast<__int128>(Float128(-((__int128{1} << 64) + 1))),
            -((__int128{1} << 64) + 1));
  EXPECT_EQ(static_cast<__int128>(Float128(__int128{0})), 0);
  EXPECT_EQ(static_cast<__int128>(Float128(__int128{-1})), -1);
#else
  GTEST_SKIP() << "__int128 is unavailable";
#endif
}

TEST(Float128ConversionTest, UnsignedInt128RoundTrips) {
#ifdef ABSL_HAVE_INTRINSIC_INT128
  using Uint128 = unsigned __int128;
  EXPECT_EQ(static_cast<Uint128>(Float128((Uint128{1} << 64) + 1)),
            (Uint128{1} << 64) + 1);
  EXPECT_EQ(static_cast<Uint128>(Float128(Uint128{0})), 0);
  EXPECT_EQ(static_cast<Uint128>(Float128(Uint128{1} << 127)),
            Uint128{1} << 127);
#else
  GTEST_SKIP() << "unsigned __int128 is unavailable";
#endif
}

TEST(Float128ConversionTest, AbslInt128RoundTrips) {
  EXPECT_EQ(static_cast<absl::int128>(Float128((absl::int128{1} << 64) + 1)),
            (absl::int128{1} << 64) + 1);
  EXPECT_EQ(static_cast<absl::int128>(Float128(-((absl::int128{1} << 64) + 1))),
            -((absl::int128{1} << 64) + 1));
  EXPECT_EQ(static_cast<absl::int128>(Float128(absl::int128{0})), 0);
  EXPECT_EQ(static_cast<absl::int128>(Float128(absl::int128{-1})), -1);
}

TEST(Float128ConversionTest, AbslUint128RoundTrips) {
  EXPECT_EQ(static_cast<absl::uint128>(Float128((absl::uint128{1} << 64) + 1)),
            (absl::uint128{1} << 64) + 1);
  EXPECT_EQ(static_cast<absl::uint128>(Float128(absl::uint128{0})), 0);
  EXPECT_EQ(static_cast<absl::uint128>(Float128(absl::uint128{1} << 127)),
            absl::uint128{1} << 127);
}

TEST(Float128ConversionTest, FloatRoundTrips) {
  EXPECT_EQ(
      static_cast<float>(Float128(-std::numeric_limits<float>::infinity())),
      -std::numeric_limits<float>::infinity());
  EXPECT_EQ(static_cast<float>(Float128(-kPiFloat)), -kPiFloat);
  EXPECT_EQ(static_cast<float>(Float128(0.0f)), 0.0f);
  EXPECT_EQ(static_cast<float>(Float128(kPiFloat)), kPiFloat);
  EXPECT_EQ(
      static_cast<float>(Float128(std::numeric_limits<float>::infinity())),
      std::numeric_limits<float>::infinity());
  EXPECT_TRUE(std::isnan(
      static_cast<float>(Float128(std::numeric_limits<float>::quiet_NaN()))));
}

TEST(Float128ConversionTest, DoubleRoundTrips) {
  EXPECT_EQ(
      static_cast<double>(Float128(-std::numeric_limits<double>::infinity())),
      -std::numeric_limits<double>::infinity());
  EXPECT_EQ(static_cast<double>(Float128(-kPi)), -kPi);
  EXPECT_EQ(static_cast<double>(Float128(0.0)), 0.0);
  EXPECT_EQ(static_cast<double>(Float128(kPi)), kPi);
  EXPECT_EQ(
      static_cast<double>(Float128(std::numeric_limits<double>::infinity())),
      std::numeric_limits<double>::infinity());
  EXPECT_TRUE(std::isnan(
      static_cast<double>(Float128(std::numeric_limits<double>::quiet_NaN()))));
}

TEST(Float128ConversionTest, LongDoubleRoundTrips) {
  EXPECT_EQ(static_cast<long double>(
                Float128(-std::numeric_limits<long double>::infinity())),
            -std::numeric_limits<long double>::infinity());
  EXPECT_EQ(static_cast<long double>(Float128(-kPiLong)), -kPiLong);
  EXPECT_EQ(static_cast<long double>(Float128(0.0L)), 0.0L);
  EXPECT_EQ(static_cast<long double>(Float128(kPiLong)), kPiLong);
  EXPECT_EQ(static_cast<long double>(
                Float128(std::numeric_limits<long double>::infinity())),
            std::numeric_limits<long double>::infinity());
  EXPECT_TRUE(std::isnan(static_cast<long double>(
      Float128(std::numeric_limits<long double>::quiet_NaN()))));
}

TEST(Float128FormatTest, FloatSpecifiers) {
  EXPECT_EQ(absl::StrFormat("%e", Float128(1234.56789)), "1.234568e+03");
  EXPECT_EQ(absl::StrFormat("%E", Float128(1234.56789)), "1.234568E+03");
  EXPECT_EQ(absl::StrFormat("%f", Float128(1234.56789)), "1234.567890");
  EXPECT_EQ(absl::StrFormat("%F", Float128(1234.56789)), "1234.567890");
  EXPECT_EQ(absl::StrFormat("%g", Float128(1234.56789)), "1234.57");
  EXPECT_EQ(absl::StrFormat("%G", Float128(1234.56789)), "1234.57");
  EXPECT_EQ(absl::StrFormat("%a", Float128(1234.56789)),
            "0x1.34a4584f4c6e7p+10");
  EXPECT_EQ(absl::StrFormat("%A", Float128(1234.56789)),
            "0X1.34A4584F4C6E7P+10");
}

TEST(Float128FormatTest, ExplicitWidth) {
  EXPECT_EQ(absl::StrFormat("%15f", Float128(123.456)), "     123.456000");
}

TEST(Float128FormatTest, ExplicitWidthLarge) {
  EXPECT_EQ(absl::StrFormat("%95f", Float128(123.456)),
            "                                                                  "
            "                   123.456000");
}

TEST(Float128FormatTest, ExplicitPrecision) {
  EXPECT_EQ(absl::StrFormat("%.10f", Float128(123.456)), "123.4560000000");
}

TEST(Float128FormatTest, VSpecifier) {
  Float128 x = 1234.56789;
  EXPECT_EQ(absl::StrFormat("%v", x), absl::StrFormat("%g", x));
}

TEST(Float128StreamTest, Flags) {
  EXPECT_EQ(
      (std::ostringstream() << std::scientific << Float128(1234.56789)).str(),
      "1.234568e+03");
  EXPECT_EQ((std::ostringstream()
             << std::scientific << std::uppercase << Float128(1234.56789))
                .str(),
            "1.234568E+03");
  EXPECT_EQ((std::ostringstream() << std::fixed << Float128(1234.56789)).str(),
            "1234.567890");
  EXPECT_EQ((std::ostringstream() << Float128(1234.56789)).str(), "1234.57");
  EXPECT_EQ(
      (std::ostringstream() << std::hexfloat << Float128(1234.56789)).str(),
      "0x1.34a4584f4c6e7p+10");
  EXPECT_EQ((std::ostringstream()
             << std::hexfloat << std::uppercase << Float128(1234.56789))
                .str(),
            "0X1.34A4584F4C6E7P+10");
}

TEST(Float128StreamTest, ExplicitWidth) {
  EXPECT_EQ((std::ostringstream() << std::setw(15) << Float128(123.456)).str(),
            "        123.456");
}

TEST(Float128StreamTest, ExplicitWidthLarge) {
  EXPECT_EQ((std::ostringstream() << std::setw(95) << Float128(123.456)).str(),
            "                                                                  "
            "                      123.456");
}

TEST(Float128StreamTest, NonstandardFill) {
  EXPECT_EQ((std::ostringstream()
             << std::setfill('x') << std::setw(35) << Float128(123.456))
                .str(),
            "xxxxxxxxxxxxxxxxxxxxxxxxxxxx123.456");
}

TEST(Float128StreamTest, ExplicitPrecision) {
  EXPECT_EQ((std::ostringstream()
             << std::fixed << std::setprecision(10) << Float128(123.456))
                .str(),
            "123.4560000000");
}

void StreamDoubleAsFloat128(double x) {
  EXPECT_EQ(
      (std::ostringstream()
       << std::fixed << std::setprecision(15) << Float128(x))
          .str(),
      (std::ostringstream() << std::fixed << std::setprecision(15) << x).str());
}

TEST(Float128StreamTest, StreamDoubleAsFloat128RegressionNoTruncation) {
  StreamDoubleAsFloat128(-10195112285.535555);
}

TEST(Float128AssignmentTest, Int) {
  Float128 x = std::numeric_limits<int>::max();
  EXPECT_EQ(static_cast<int>(x), std::numeric_limits<int>::max());
}

TEST(Float128AssignmentTest, UnsignedInt) {
  Float128 x = std::numeric_limits<unsigned int>::max();
  EXPECT_EQ(static_cast<unsigned int>(x),
            std::numeric_limits<unsigned int>::max());
}

TEST(Float128AssignmentTest, Long) {
  Float128 x = std::numeric_limits<long>::max();
  EXPECT_EQ(static_cast<long>(x), std::numeric_limits<long>::max());
}

TEST(Float128AssignmentTest, UnsignedLong) {
  Float128 x = std::numeric_limits<unsigned long>::max();
  EXPECT_EQ(static_cast<unsigned long>(x),
            std::numeric_limits<unsigned long>::max());
}

TEST(Float128AssignmentTest, LongLong) {
  Float128 x = std::numeric_limits<long long>::max();
  EXPECT_EQ(static_cast<long long>(x), std::numeric_limits<long long>::max());
}

TEST(Float128AssignmentTest, UnsignedLongLong) {
  Float128 x = std::numeric_limits<unsigned long long>::max();
  EXPECT_EQ(static_cast<unsigned long long>(x),
            std::numeric_limits<unsigned long long>::max());
}

TEST(Float128AssignmentTest, Float) {
  Float128 x = kPiFloat;
  EXPECT_EQ(static_cast<float>(x), kPiFloat);
}

TEST(Float128AssignmentTest, Double) {
  Float128 x = kPi;
  EXPECT_EQ(static_cast<double>(x), kPi);
}

// NOLINTEND(runtime/int)
// NOLINTEND(google-runtime-int)

// Fuzz-testing division is hard, so just do a quick check to make sure we get
// something reasonable.
TEST(Float128ArithmeticTest, ExactDivision) {
  EXPECT_EQ(Float128(714.0) / Float128(84.0), Float128(8.5));
}

TEST(Float128ArithmeticTest, UnaryOperationsPropagateNan) {
  EXPECT_TRUE(isnan(+std::numeric_limits<Float128>::quiet_NaN()));
  EXPECT_TRUE(isnan(+std::numeric_limits<Float128>::signaling_NaN()));
  EXPECT_TRUE(isnan(-std::numeric_limits<Float128>::quiet_NaN()));
  EXPECT_TRUE(isnan(-std::numeric_limits<Float128>::signaling_NaN()));
}

// This test checks that operator< does the right thing; if it passes, the tests
// above imply the other operators also do the right thing.
TEST(Float128RelationTest, LessThanIsCorrectlyOrdered) {
  EXPECT_TRUE(-std::numeric_limits<Float128>::infinity() <
              std::numeric_limits<Float128>::lowest());
  EXPECT_TRUE(std::numeric_limits<Float128>::lowest() < Float128(-1.0));
  EXPECT_TRUE(Float128(-1.0) < Float128(0.0));
  EXPECT_TRUE(Float128(0.0) < std::numeric_limits<Float128>::denorm_min());
  EXPECT_TRUE(std::numeric_limits<Float128>::denorm_min() <
              std::numeric_limits<Float128>::min());
  EXPECT_TRUE(std::numeric_limits<Float128>::min() < Float128(1.0));
  EXPECT_TRUE(Float128(1.0) < std::numeric_limits<Float128>::max());
  EXPECT_TRUE(std::numeric_limits<Float128>::max() <
              std::numeric_limits<Float128>::infinity());
}

TEST(Float128RelationTest, ZeroEqualsNegativeZero) {
  EXPECT_TRUE(Float128(0.0) == Float128(-0.0));
}

// NaN behavior is unusual enough that it merits its own set of tests. Some of
// these tests overlap with tests above, but having NaN behavior tested
// explicitly is worth it.

TEST(Float128RelationTest, NanComparesFalseWithNan) {
  constexpr Float128 quiet_nan = std::numeric_limits<Float128>::quiet_NaN();
  constexpr Float128 signaling_nan =
      std::numeric_limits<Float128>::signaling_NaN();

  EXPECT_FALSE(quiet_nan == quiet_nan);
  EXPECT_FALSE(signaling_nan == signaling_nan);
  EXPECT_FALSE(quiet_nan == signaling_nan);
  EXPECT_FALSE(signaling_nan == quiet_nan);

  EXPECT_TRUE(quiet_nan != quiet_nan);
  EXPECT_TRUE(signaling_nan != signaling_nan);
  EXPECT_TRUE(quiet_nan != signaling_nan);
  EXPECT_TRUE(signaling_nan != quiet_nan);

  EXPECT_FALSE(quiet_nan > quiet_nan);
  EXPECT_FALSE(signaling_nan > signaling_nan);
  EXPECT_FALSE(quiet_nan > signaling_nan);
  EXPECT_FALSE(signaling_nan > quiet_nan);

  EXPECT_FALSE(quiet_nan >= quiet_nan);
  EXPECT_FALSE(signaling_nan >= signaling_nan);
  EXPECT_FALSE(quiet_nan >= signaling_nan);
  EXPECT_FALSE(signaling_nan >= quiet_nan);

  EXPECT_FALSE(quiet_nan < quiet_nan);
  EXPECT_FALSE(signaling_nan < signaling_nan);
  EXPECT_FALSE(quiet_nan < signaling_nan);
  EXPECT_FALSE(signaling_nan < quiet_nan);

  EXPECT_FALSE(quiet_nan <= quiet_nan);
  EXPECT_FALSE(signaling_nan <= signaling_nan);
  EXPECT_FALSE(quiet_nan <= signaling_nan);
  EXPECT_FALSE(signaling_nan <= quiet_nan);
}

TEST(Float128ClassifyTest, Zero) {
  EXPECT_EQ(fpclassify(Float128(0.0)), FP_ZERO);
  EXPECT_TRUE(isfinite(Float128(0.0)));
  EXPECT_FALSE(isinf(Float128(0.0)));
  EXPECT_FALSE(isnan(Float128(0.0)));
  EXPECT_FALSE(isnormal(Float128(0.0)));
  EXPECT_FALSE(signbit(Float128(0.0)));

  EXPECT_EQ(fpclassify(Float128(-0.0)), FP_ZERO);
  EXPECT_TRUE(isfinite(Float128(-0.0)));
  EXPECT_FALSE(isinf(Float128(-0.0)));
  EXPECT_FALSE(isnan(Float128(-0.0)));
  EXPECT_FALSE(isnormal(Float128(-0.0)));
  EXPECT_TRUE(signbit(Float128(-0.0)));
}

TEST(Float128ClassifyTest, Subnormal) {
  EXPECT_EQ(fpclassify(std::numeric_limits<Float128>::denorm_min()),
            FP_SUBNORMAL);
  EXPECT_TRUE(isfinite(std::numeric_limits<Float128>::denorm_min()));
  EXPECT_FALSE(isinf(std::numeric_limits<Float128>::denorm_min()));
  EXPECT_FALSE(isnan(std::numeric_limits<Float128>::denorm_min()));
  EXPECT_FALSE(isnormal(std::numeric_limits<Float128>::denorm_min()));
  EXPECT_FALSE(signbit(std::numeric_limits<Float128>::denorm_min()));
}

TEST(Float128ClassifyTest, Normal) {
  EXPECT_EQ(fpclassify(std::numeric_limits<Float128>::min()), FP_NORMAL);
  EXPECT_TRUE(isfinite(Float128(std::numeric_limits<Float128>::min())));
  EXPECT_FALSE(isinf(Float128(std::numeric_limits<Float128>::min())));
  EXPECT_FALSE(isnan(Float128(std::numeric_limits<Float128>::min())));
  EXPECT_TRUE(isnormal(Float128(std::numeric_limits<Float128>::min())));
  EXPECT_FALSE(signbit(Float128(std::numeric_limits<Float128>::min())));

  EXPECT_EQ(fpclassify(std::numeric_limits<Float128>::max()), FP_NORMAL);
  EXPECT_TRUE(isfinite(Float128(std::numeric_limits<Float128>::max())));
  EXPECT_FALSE(isinf(Float128(std::numeric_limits<Float128>::max())));
  EXPECT_FALSE(isnan(Float128(std::numeric_limits<Float128>::max())));
  EXPECT_TRUE(isnormal(Float128(std::numeric_limits<Float128>::max())));
  EXPECT_FALSE(signbit(Float128(std::numeric_limits<Float128>::max())));

  EXPECT_EQ(fpclassify(std::numeric_limits<Float128>::lowest()), FP_NORMAL);
  EXPECT_TRUE(isfinite(Float128(std::numeric_limits<Float128>::lowest())));
  EXPECT_FALSE(isinf(Float128(std::numeric_limits<Float128>::lowest())));
  EXPECT_FALSE(isnan(Float128(std::numeric_limits<Float128>::lowest())));
  EXPECT_TRUE(isnormal(Float128(std::numeric_limits<Float128>::lowest())));
  EXPECT_TRUE(signbit(Float128(std::numeric_limits<Float128>::lowest())));

  EXPECT_EQ(fpclassify(Float128(kPi)), FP_NORMAL);
  EXPECT_TRUE(isfinite(Float128(kPi)));
  EXPECT_FALSE(isinf(Float128(kPi)));
  EXPECT_FALSE(isnan(Float128(kPi)));
  EXPECT_TRUE(isnormal(Float128(kPi)));
  EXPECT_FALSE(signbit(Float128(kPi)));

  EXPECT_EQ(fpclassify(Float128(-kPi)), FP_NORMAL);
  EXPECT_TRUE(isfinite(Float128(-kPi)));
  EXPECT_FALSE(isinf(Float128(-kPi)));
  EXPECT_FALSE(isnan(Float128(-kPi)));
  EXPECT_TRUE(isnormal(Float128(-kPi)));
  EXPECT_TRUE(signbit(Float128(-kPi)));
}

TEST(Float128ClassifyTest, Infinite) {
  EXPECT_EQ(fpclassify(Float128(std::numeric_limits<Float128>::infinity())),
            FP_INFINITE);
  EXPECT_EQ(fpclassify(Float128(-std::numeric_limits<Float128>::infinity())),
            FP_INFINITE);
  EXPECT_FALSE(isfinite(Float128(std::numeric_limits<Float128>::infinity())));
  EXPECT_TRUE(isinf(Float128(std::numeric_limits<Float128>::infinity())));
  EXPECT_FALSE(isnan(Float128(std::numeric_limits<Float128>::infinity())));
  EXPECT_FALSE(isnormal(Float128(std::numeric_limits<Float128>::infinity())));
  EXPECT_FALSE(signbit(Float128(std::numeric_limits<Float128>::infinity())));
}

TEST(Float128ClassifyTest, Nan) {
  EXPECT_EQ(fpclassify(Float128(std::numeric_limits<Float128>::quiet_NaN())),
            FP_NAN);
  EXPECT_FALSE(isfinite(Float128(std::numeric_limits<Float128>::quiet_NaN())));
  EXPECT_FALSE(isinf(Float128(std::numeric_limits<Float128>::quiet_NaN())));
  EXPECT_TRUE(isnan(Float128(std::numeric_limits<Float128>::quiet_NaN())));
  EXPECT_FALSE(isnormal(Float128(std::numeric_limits<Float128>::quiet_NaN())));
  EXPECT_FALSE(signbit(Float128(std::numeric_limits<Float128>::quiet_NaN())));

  EXPECT_EQ(
      fpclassify(Float128(std::numeric_limits<Float128>::signaling_NaN())),
      FP_NAN);
  EXPECT_FALSE(
      isfinite(Float128(std::numeric_limits<Float128>::signaling_NaN())));
  EXPECT_FALSE(isinf(Float128(std::numeric_limits<Float128>::signaling_NaN())));
  EXPECT_TRUE(isnan(Float128(std::numeric_limits<Float128>::signaling_NaN())));
  EXPECT_FALSE(
      isnormal(Float128(std::numeric_limits<Float128>::signaling_NaN())));
  EXPECT_FALSE(
      signbit(Float128(std::numeric_limits<Float128>::signaling_NaN())));
}

TEST(Float128FunctionTest, NanGeneratesNan) {
  EXPECT_TRUE(isnan(nan("")));
  EXPECT_THAT(static_cast<double>(nan("")), IsNan());
}

// NOLINTEND(runtime/int)
// NOLINTEND(google-runtime-int)

TEST(Float128Functiontest, FrexpHandlesObviousResults) {
  int exponent;
  EXPECT_EQ(frexp(Float128(0.0), &exponent), 0.0);

  EXPECT_EQ(frexp(Float128(1.0), &exponent), 0.5);
  EXPECT_EQ(exponent, 1);

  EXPECT_EQ(frexp(Float128(1024.0), &exponent), 0.5);
  EXPECT_EQ(exponent, 11);

  EXPECT_EQ(frexp(Float128(1280.0), &exponent), 0.625);
  EXPECT_EQ(exponent, 11);
}

TEST(Float128FunctionTest, FrexpInfinity) {
  // Calling frexp on an infinity writes an unspecified value into `exponent`.
  int exponent;
  EXPECT_EQ(frexp(std::numeric_limits<Float128>::infinity(), &exponent),
            std::numeric_limits<Float128>::infinity());
  EXPECT_EQ(frexp(-std::numeric_limits<Float128>::infinity(), &exponent),
            -std::numeric_limits<Float128>::infinity());
}

TEST(Float128FunctionTest, FrexpNan) {
  // Calling frexp on a NaN writes an unspecified value into `exponent`.
  int exponent;
  EXPECT_TRUE(
      isnan(frexp(std::numeric_limits<Float128>::quiet_NaN(), &exponent)));
}

}  // namespace

TEST(Float128NumericLimitsTest, LimitsAreUnchanged) {
  ASSERT_TRUE(std::numeric_limits<Float128>::is_specialized);

  EXPECT_TRUE(std::numeric_limits<Float128>::is_signed);
  EXPECT_FALSE(std::numeric_limits<Float128>::is_integer);
  EXPECT_FALSE(std::numeric_limits<Float128>::is_exact);
  EXPECT_TRUE(std::numeric_limits<Float128>::has_infinity);
  EXPECT_TRUE(std::numeric_limits<Float128>::has_quiet_NaN);
  EXPECT_TRUE(std::numeric_limits<Float128>::has_signaling_NaN);
  EXPECT_EQ(std::numeric_limits<Float128>::has_denorm, std::denorm_present);
  EXPECT_FALSE(std::numeric_limits<Float128>::has_denorm_loss);
  EXPECT_EQ(std::numeric_limits<Float128>::round_style, std::round_to_nearest);
  EXPECT_TRUE(std::numeric_limits<Float128>::is_iec559);
  EXPECT_TRUE(std::numeric_limits<Float128>::is_bounded);
  EXPECT_FALSE(std::numeric_limits<Float128>::is_modulo);
  EXPECT_EQ(std::numeric_limits<Float128>::digits, 113);
  EXPECT_EQ(std::numeric_limits<Float128>::digits10, 33);

  EXPECT_EQ(std::numeric_limits<Float128>::min().data_, 0x1.0p-16'382q);
  EXPECT_EQ(std::numeric_limits<Float128>::lowest().data_,
            -0x1.ffff'ffff'ffff'ffff'ffff'ffff'ffffp16'383q);
  EXPECT_EQ(std::numeric_limits<Float128>::max().data_,
            0x1.ffff'ffff'ffff'ffff'ffff'ffff'ffffp16'383q);
  EXPECT_EQ(std::numeric_limits<Float128>::epsilon().data_, 0x1.0p-112q);
  EXPECT_EQ(std::numeric_limits<Float128>::round_error().data_, 0.5q);
  EXPECT_EQ(std::bit_cast<absl::uint128>(
                std::numeric_limits<Float128>::infinity().data_),
            absl::MakeUint128(0x7fff'0000'0000'0000, 0));
  EXPECT_EQ(std::bit_cast<absl::uint128>(
                std::numeric_limits<Float128>::quiet_NaN().data_),
            absl::MakeUint128(0x7fff'8000'0000'0000, 0));
  EXPECT_EQ(std::bit_cast<absl::uint128>(
                std::numeric_limits<Float128>::signaling_NaN().data_),
            absl::MakeUint128(0x7fff'4000'0000'0000, 0));
  EXPECT_EQ(std::numeric_limits<Float128>::denorm_min().data_, 0x1.0p-16'494q);
}

}  // namespace gtl
