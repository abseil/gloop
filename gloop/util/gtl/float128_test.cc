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
#include "fuzztest/fuzztest.h"
#include "gloop/util/gtl/float128_fuzztest_domain.h"
#include "gloop/util/gtl/float128_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace gtl {
namespace {

using ::fuzztest::Arbitrary;
using ::fuzztest::Filter;
using ::fuzztest::Finite;
using ::fuzztest::InRange;
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

void IntRoundTripsFuzz(int x) { EXPECT_EQ(static_cast<int>(Float128(x)), x); }
FUZZ_TEST(Float128ConversionTest, IntRoundTripsFuzz);

TEST(Float128ConversionTest, UnsignedIntRoundTrips) {
  EXPECT_EQ(static_cast<unsigned int>(Float128(0U)), 0U);
  EXPECT_EQ(static_cast<unsigned int>(
                Float128(std::numeric_limits<unsigned int>::max())),
            std::numeric_limits<unsigned int>::max());
}

void UnsignedIntRoundTripsFuzz(unsigned int x) {
  EXPECT_EQ(static_cast<unsigned int>(Float128(x)), x);
}
FUZZ_TEST(Float128ConversionTest, UnsignedIntRoundTripsFuzz);

TEST(Float128ConversionTest, LongRoundTrips) {
  EXPECT_EQ(static_cast<long>(Float128(std::numeric_limits<long>::lowest())),
            std::numeric_limits<long>::lowest());
  EXPECT_EQ(static_cast<long>(Float128(0L)), 0L);
  EXPECT_EQ(static_cast<long>(Float128(std::numeric_limits<long>::max())),
            std::numeric_limits<long>::max());
}

void LongRoundTripsFuzz(long x) {
  EXPECT_EQ(static_cast<long>(Float128(x)), x);
}
FUZZ_TEST(Float128ConversionTest, LongRoundTripsFuzz);

TEST(Float128ConversionTest, UnsignedLongRoundTrips) {
  EXPECT_EQ(static_cast<unsigned long>(Float128(0UL)), 0UL);
  EXPECT_EQ(static_cast<unsigned long>(
                Float128(std::numeric_limits<unsigned long>::max())),
            std::numeric_limits<unsigned long>::max());
}

void UnsignedLongRoundTripsFuzz(unsigned long x) {
  EXPECT_EQ(static_cast<unsigned long>(Float128(x)), x);
}
FUZZ_TEST(Float128ConversionTest, UnsignedLongRoundTripsFuzz);

TEST(Float128ConversionTest, LongLongRoundTrips) {
  EXPECT_EQ(static_cast<long long>(
                Float128(std::numeric_limits<long long>::lowest())),
            std::numeric_limits<long long>::lowest());
  EXPECT_EQ(static_cast<long long>(Float128(0LL)), 0LL);
  EXPECT_EQ(
      static_cast<long long>(Float128(std::numeric_limits<long long>::max())),
      std::numeric_limits<long long>::max());
}

void LongLongRoundTripsFuzz(long long x) {
  EXPECT_EQ(static_cast<long long>(Float128(x)), x);
}
FUZZ_TEST(Float128ConversionTest, LongLongRoundTripsFuzz);

TEST(Float128ConversionTest, UnsignedLongLongRoundTrips) {
  EXPECT_EQ(static_cast<unsigned long long>(Float128(0ULL)), 0ULL);
  EXPECT_EQ(static_cast<unsigned long long>(
                Float128(std::numeric_limits<unsigned long long>::max())),
            std::numeric_limits<unsigned long long>::max());
}

void UnsignedLongLongRoundTripsFuzz(unsigned long long x) {
  EXPECT_EQ(static_cast<unsigned long long>(Float128(x)), x);
}
FUZZ_TEST(Float128ConversionTest, UnsignedLongLongRoundTripsFuzz);

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

template <typename T>
auto NonNan() {
  if constexpr (std::is_same_v<T, Float128>) {
    return Filter([](Float128 x) { return !isnan(x); }, ArbitraryFloat128());
  } else {
    return Filter([](T x) { return !std::isnan(x); }, Arbitrary<T>());
  }
}

void FloatRoundTripsFuzz(float x) {
  EXPECT_EQ(static_cast<float>(Float128(x)), x);
}
FUZZ_TEST(Float128ConversionTest, FloatRoundTripsFuzz)
    .WithDomains(NonNan<float>());

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

void DoubleRoundTripsFuzz(double x) {
  EXPECT_EQ(static_cast<double>(Float128(x)), x);
}
FUZZ_TEST(Float128ConversionTest, DoubleRoundTripsFuzz)
    .WithDomains(NonNan<double>());

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

void PrintfDoubleAsFloat128(double x) {
  EXPECT_EQ(absl::StrFormat("%.15g", Float128(x)), absl::StrFormat("%.15g", x));
}
FUZZ_TEST(Float128FormatTest, PrintfDoubleAsFloat128);

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
FUZZ_TEST(Float128StreamTest, StreamDoubleAsFloat128);

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

// Arithmetic is mostly tested via fuzz testing (property-based testing over the
// domain of `Float128`s). Explicit tests are included as necessary to verify

void UnaryAdditionIsIdentity(Float128 x) {
  EXPECT_THAT(+x, NanSensitiveFloat128Eq(x));
}
FUZZ_TEST(Float128ArithmeticTest, UnaryAdditionIsIdentity)
    .WithDomains(ArbitraryFloat128());

void NegationChangesSignButNotValue(Float128 x) {
  EXPECT_NE(signbit(x), signbit(-x));
  EXPECT_THAT(fabs(x), NanSensitiveFloat128Eq(fabs(-x)));
}
FUZZ_TEST(Float128ArithmeticTest, NegationChangesSignButNotValue)
    .WithDomains(ArbitraryFloat128());

void AdditionCommutative(Float128 x, Float128 y) {
  EXPECT_THAT(x + y, NanSensitiveFloat128Eq(y + x));
}
FUZZ_TEST(Float128ArithmeticTest, AdditionCommutative)
    .WithDomains(ArbitraryFloat128(), ArbitraryFloat128());

void SubtractionAnticommutative(Float128 x, Float128 y) {
  EXPECT_THAT(x - y, NanSensitiveFloat128Eq(-(y - x)));
}
FUZZ_TEST(Float128ArithmeticTest, SubtractionAnticommutative)
    .WithDomains(ArbitraryFloat128(), ArbitraryFloat128());

void MultiplicationCommutative(Float128 x, Float128 y) {
  EXPECT_THAT(x * y, NanSensitiveFloat128Eq(y * x));
}
FUZZ_TEST(Float128ArithmeticTest, MultiplicationCommutative)
    .WithDomains(ArbitraryFloat128(), ArbitraryFloat128());

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

void ArithmeticPropagatesNan(Float128 x) {
  constexpr Float128 quiet_nan = std::numeric_limits<Float128>::quiet_NaN();
  constexpr Float128 signaling_nan =
      std::numeric_limits<Float128>::signaling_NaN();

  EXPECT_TRUE(isnan(x + quiet_nan));
  EXPECT_TRUE(isnan(quiet_nan + x));
  EXPECT_TRUE(isnan(x - quiet_nan));
  EXPECT_TRUE(isnan(quiet_nan - x));
  EXPECT_TRUE(isnan(x * quiet_nan));
  EXPECT_TRUE(isnan(quiet_nan * x));
  EXPECT_TRUE(isnan(x / quiet_nan));
  EXPECT_TRUE(isnan(quiet_nan / x));

  EXPECT_TRUE(isnan(x + signaling_nan));
  EXPECT_TRUE(isnan(signaling_nan + x));
  EXPECT_TRUE(isnan(x - signaling_nan));
  EXPECT_TRUE(isnan(signaling_nan - x));
  EXPECT_TRUE(isnan(x * signaling_nan));
  EXPECT_TRUE(isnan(signaling_nan * x));
  EXPECT_TRUE(isnan(x / signaling_nan));
  EXPECT_TRUE(isnan(signaling_nan / x));
}
FUZZ_TEST(Float128ArithmeticTest, ArithmeticPropagatesNan)
    .WithDomains(ArbitraryFloat128());

void DoubleConversionCommutesWithArithmetic(double x, double y) {
  Float128 fx = x, fy = y;
  EXPECT_THAT(static_cast<double>(+fx), NanSensitiveDoubleEq(x));
  EXPECT_THAT(static_cast<double>(-fx), NanSensitiveDoubleEq(-x));
  EXPECT_THAT(static_cast<double>(fx + fy), NanSensitiveDoubleEq(x + y));
  EXPECT_THAT(static_cast<double>(fx - fy), NanSensitiveDoubleEq(x - y));
  EXPECT_THAT(static_cast<double>(fx * fy), NanSensitiveDoubleEq(x * y));
  EXPECT_THAT(static_cast<double>(fx / fy), NanSensitiveDoubleEq(x / y));
}
FUZZ_TEST(Float128ArithmeticTest, DoubleConversionCommutesWithArithmetic);

void ModifyingOperatorsMatchNonModifyingOperators(const Float128 x,
                                                  const Float128 y) {
  Float128 modified_x = x;
  EXPECT_THAT(modified_x += y, NanSensitiveFloat128Eq(x + y));
  EXPECT_THAT(modified_x, NanSensitiveFloat128Eq(x + y));

  modified_x = x;
  EXPECT_THAT(modified_x -= y, NanSensitiveFloat128Eq(x - y));
  EXPECT_THAT(modified_x, NanSensitiveFloat128Eq(x - y));

  modified_x = x;
  EXPECT_THAT(modified_x *= y, NanSensitiveFloat128Eq(x * y));
  EXPECT_THAT(modified_x, NanSensitiveFloat128Eq(x * y));

  modified_x = x;
  EXPECT_THAT(modified_x /= y, NanSensitiveFloat128Eq(x / y));
  EXPECT_THAT(modified_x, NanSensitiveFloat128Eq(x / y));
}
FUZZ_TEST(Float128ArithmeticTest, ModifyingOperatorsMatchNonModifyingOperators)
    .WithDomains(ArbitraryFloat128(), ArbitraryFloat128());

void IncrementMatchesModifyingAddition(const Float128 x) {
  Float128 modified_x = x;
  EXPECT_THAT(++modified_x, NanSensitiveFloat128Eq(x + 1.0));
  EXPECT_THAT(modified_x, NanSensitiveFloat128Eq(x + 1.0));

  modified_x = x;
  EXPECT_THAT(modified_x++, NanSensitiveFloat128Eq(x));
  EXPECT_THAT(modified_x, NanSensitiveFloat128Eq(x + 1.0));
}
FUZZ_TEST(Float128ArithmeticTest, IncrementMatchesModifyingAddition)
    .WithDomains(ArbitraryFloat128());

void DecrementMatchesModifyingSubtraction(const Float128 x) {
  Float128 modified_x = x;
  EXPECT_THAT(--modified_x, NanSensitiveFloat128Eq(x - 1.0));
  EXPECT_THAT(modified_x, NanSensitiveFloat128Eq(x - 1.0));

  modified_x = x;
  EXPECT_THAT(modified_x--, NanSensitiveFloat128Eq(x));
  EXPECT_THAT(modified_x, NanSensitiveFloat128Eq(x - 1.0));
}
FUZZ_TEST(Float128ArithmeticTest, DecrementMatchesModifyingSubtraction)
    .WithDomains(ArbitraryFloat128());

void SameSignedAdditionPreservesSign(Float128 x, Float128 y) {
  EXPECT_EQ(signbit(x + copysign(y, x)), signbit(x));
}
FUZZ_TEST(Float128ArithmeticTest, SameSignedAdditionPreservesSign)
    .WithDomains(ArbitraryFloat128(), ArbitraryFloat128());

void MultiplicationHandlesSignsCorrectly(Float128 x, Float128 y) {
  EXPECT_EQ(signbit(x * y), signbit(x) ^ signbit(y));
}
FUZZ_TEST(Float128ArithmeticTest, MultiplicationHandlesSignsCorrectly)
    .WithDomains(FiniteFloat128(), FiniteFloat128());

// Relations are mostly tested via fuzz testing (property-based testing over the
// domain of `Float128`s). Explicit tests are included as necessary to verify
// that some relations correspond to real-world expectations (e.g., that < and >
// are not swapped).

// `operator==` is an equivalence relation (transitive, symmetric, and
// reflexive).

void EqualTransitive(Float128 x, Float128 y, Float128 z) {
  // This is the logical implication x = y ∧ y = z ⇒ x = z. It is an
  // implication, rather than an equivalence, because the converse is not
  // universally true (consider x = 0.0, y = 1.0, z = -0.0).
  EXPECT_TRUE(!(x == y && y == z) || x == z);
}
FUZZ_TEST(Float128RelationTest, EqualTransitive)
    .WithDomains(ArbitraryFloat128(), ArbitraryFloat128(), ArbitraryFloat128());

void EqualSymmetric(Float128 x, Float128 y) { EXPECT_EQ(x == y, y == x); }
FUZZ_TEST(Float128RelationTest, EqualSymmetric)
    .WithDomains(ArbitraryFloat128(), ArbitraryFloat128());

void EqualReflexive(Float128 x) { EXPECT_TRUE(x == x); }
FUZZ_TEST(Float128RelationTest, EqualReflexive).WithDomains(NonNan<Float128>());

// `operator!=` is symmetric and irreflexive.

void NotEqualSymmetric(Float128 x, Float128 y) { EXPECT_EQ(x != y, y != x); }
FUZZ_TEST(Float128RelationTest, NotEqualSymmetric)
    .WithDomains(ArbitraryFloat128(), ArbitraryFloat128());

void NotEqualIrreflexive(Float128 x) { EXPECT_FALSE(x != x); }
FUZZ_TEST(Float128RelationTest, NotEqualIrreflexive)
    .WithDomains(NonNan<Float128>());

// `operator>` is a strict partial order (transitive, asymmetric, and
// irreflexive).

void GreaterThanTransitive(Float128 x, Float128 y, Float128 z) {
  EXPECT_TRUE(!(x > y && y > z) || x > z);  // x > y ∧ y > z ⇒ x > z
}
FUZZ_TEST(Float128RelationTest, GreaterThanTransitive)
    .WithDomains(ArbitraryFloat128(), ArbitraryFloat128(), ArbitraryFloat128());

void GreaterThanAsymmetric(Float128 x, Float128 y) {
  EXPECT_TRUE(!(x > y) || !(y > x));  // x > y ⇒ ¬(y > x)
}
FUZZ_TEST(Float128RelationTest, GreaterThanAsymmetric)
    .WithDomains(NonNan<Float128>(), NonNan<Float128>());

void GreaterThanIrreflexive(Float128 x) { EXPECT_FALSE(x > x); }
FUZZ_TEST(Float128RelationTest, GreaterThanIrreflexive)
    .WithDomains(ArbitraryFloat128());

// `operator>=` is a nonstrict partial order (transitive, antisymmetric, and
// reflexive).

void GreaterThanOrEqualToTransitive(Float128 x, Float128 y, Float128 z) {
  EXPECT_TRUE(!(x >= y && y >= z) || x >= z);  // x ≥ y ∧ y ≥ z ⇒ x ≥ z
}
FUZZ_TEST(Float128RelationTest, GreaterThanOrEqualToTransitive)
    .WithDomains(ArbitraryFloat128(), ArbitraryFloat128(), ArbitraryFloat128());

void GreaterThanOrEqualToAntisymmetric(Float128 x, Float128 y) {
  EXPECT_EQ(x >= y && y >= x, x == y);
}
FUZZ_TEST(Float128RelationTest, GreaterThanOrEqualToAntisymmetric)
    .WithDomains(NonNan<Float128>(), NonNan<Float128>());

void GreaterThanOrEqualToReflexive(Float128 x) { EXPECT_TRUE(x >= x); }
FUZZ_TEST(Float128RelationTest, GreaterThanOrEqualToReflexive)
    .WithDomains(NonNan<Float128>());

// `operator<` is a strict partial order (transitive, asymmetric, and
// irreflexive).

void LessThanTransitive(Float128 x, Float128 y, Float128 z) {
  EXPECT_TRUE(!(x < y && y < z) || x < z);  // x < y ∧ y < z ⇒ x < z
}
FUZZ_TEST(Float128RelationTest, LessThanTransitive)
    .WithDomains(ArbitraryFloat128(), ArbitraryFloat128(), ArbitraryFloat128());

void LessThanAsymmetric(Float128 x, Float128 y) {
  EXPECT_TRUE(!(x < y) || !(y < x));  // x < y ⇒ ¬(y < x)
}
FUZZ_TEST(Float128RelationTest, LessThanAsymmetric)
    .WithDomains(NonNan<Float128>(), NonNan<Float128>());

void LessThanIrreflexive(Float128 x) { EXPECT_FALSE(x < x); }
FUZZ_TEST(Float128RelationTest, LessThanIrreflexive)
    .WithDomains(ArbitraryFloat128());

// `operator<=` is a nonstrict partial order (transitive, antisymmetric, and
// reflexive).

void LessThanOrEqualToTransitive(Float128 x, Float128 y, Float128 z) {
  EXPECT_TRUE(!(x <= y && y <= z) || x <= z);  // x ≤ y ∧ y ≤ z ⇒ x ≤ z
}
FUZZ_TEST(Float128RelationTest, LessThanOrEqualToTransitive)
    .WithDomains(ArbitraryFloat128(), ArbitraryFloat128(), ArbitraryFloat128());

void LessThanOrEqualToAntisymmetric(Float128 x, Float128 y) {
  EXPECT_EQ(x <= y && y <= x, x == y);
}
FUZZ_TEST(Float128RelationTest, LessThanOrEqualToAntisymmetric)
    .WithDomains(NonNan<Float128>(), NonNan<Float128>());

void LessThanOrEqualToReflexive(Float128 x) { EXPECT_TRUE(x <= x); }
FUZZ_TEST(Float128RelationTest, LessThanOrEqualToReflexive)
    .WithDomains(NonNan<Float128>());

// `operator==` and `operator!=` are exclusive, even in the case of NaNs.
void EqualXorNotEqual(Float128 x, Float128 y) { EXPECT_NE(x == y, x != y); }
FUZZ_TEST(Float128RelationTest, EqualXorNotEqual)
    .WithDomains(ArbitraryFloat128(), ArbitraryFloat128());

// `operator>` and `operator<=` are exclusive, except in the case of NaNs.
void GreaterThanXorLessThanOrEqualTo(Float128 x, Float128 y) {
  EXPECT_NE(x > y, x <= y);
}
FUZZ_TEST(Float128RelationTest, GreaterThanXorLessThanOrEqualTo)
    .WithDomains(NonNan<Float128>(), NonNan<Float128>());

// `operator<` and `operator>=` are exclusive, except in the case of NaNs.
void LessThanXorGreaterThanOrEqualTo(Float128 x, Float128 y) {
  EXPECT_NE(x < y, x >= y);
}
FUZZ_TEST(Float128RelationTest, LessThanXorGreaterThanOrEqualTo)
    .WithDomains(NonNan<Float128>(), NonNan<Float128>());

// Nonstrict orders are supersets of strict orders.

void GreaterThanImpliesGreaterThanOrEqualTo(Float128 x, Float128 y) {
  EXPECT_TRUE(!(x > y) || x >= y);  // x > y ⇒ x ≥ y
}
FUZZ_TEST(Float128RelationTest, GreaterThanImpliesGreaterThanOrEqualTo)
    .WithDomains(ArbitraryFloat128(), ArbitraryFloat128());

void LessThanImpliesLessThanOrEqualTo(Float128 x, Float128 y) {
  EXPECT_TRUE(!(x < y) || x <= y);  // x < y ⇒ x ≤ y
}
FUZZ_TEST(Float128RelationTest, LessThanImpliesLessThanOrEqualTo)
    .WithDomains(ArbitraryFloat128(), ArbitraryFloat128());

// Except for NaNs, the `Float128` domain is totally ordered. In other words,
// given any two non-NaN `Float128`s x and y, exactly one of x < y, x = y, and
// x > y holds.
void TotallyOrdered(Float128 x, Float128 y) {
  EXPECT_TRUE((x < y && !(x == y) && !(x > y)) ||
              (!(x < y) && x == y && !(x > y)) ||
              (!(x < y) && !(x == y) && x > y));
}
FUZZ_TEST(Float128RelationTest, TotallyOrdered)
    .WithDomains(NonNan<Float128>(), NonNan<Float128>());

// `operator<=>` matches the behavior of the other operators.
void SpaceshipConsistentWithOtherOperators(Float128 x, Float128 y) {
  if (std::partial_ordering order = x <=> y;
      order == std::partial_ordering::less) {
    EXPECT_FALSE(x == y);
    EXPECT_TRUE(x != y);
    EXPECT_FALSE(x > y);
    EXPECT_FALSE(x >= y);
    EXPECT_TRUE(x < y);
    EXPECT_TRUE(x <= y);
  } else if (order == std::partial_ordering::equivalent) {
    EXPECT_TRUE(x == y);
    EXPECT_FALSE(x != y);
    EXPECT_FALSE(x > y);
    EXPECT_TRUE(x >= y);
    EXPECT_FALSE(x < y);
    EXPECT_TRUE(x <= y);
  } else if (order == std::partial_ordering::greater) {
    EXPECT_FALSE(x == y);
    EXPECT_TRUE(x != y);
    EXPECT_TRUE(x > y);
    EXPECT_TRUE(x >= y);
    EXPECT_FALSE(x < y);
    EXPECT_FALSE(x <= y);
  } else if (order == std::partial_ordering::unordered) {
    EXPECT_FALSE(x == y);
    EXPECT_TRUE(x != y);
    EXPECT_FALSE(x > y);
    EXPECT_FALSE(x >= y);
    EXPECT_FALSE(x < y);
    EXPECT_FALSE(x <= y);
  }
}
FUZZ_TEST(Float128RelationTest, SpaceshipConsistentWithOtherOperators)
    .WithDomains(ArbitraryFloat128(), ArbitraryFloat128());

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

void ConversionFromDoublePreservesRelations(double x, double y) {
  Float128 fx = x, fy = y;
  EXPECT_EQ(x == y, fx == fy);
  EXPECT_EQ(x != y, fx != fy);
  EXPECT_EQ(x > y, fx > fy);
  EXPECT_EQ(x < y, fx < fy);
  EXPECT_EQ(x >= y, fx >= fy);
  EXPECT_EQ(x <= y, fx <= fy);
  EXPECT_EQ(x <=> y, fx <=> fy);
}
FUZZ_TEST(Float128RelationTest, ConversionFromDoublePreservesRelations);

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

void NothingEqualsNan(Float128 x) {
  constexpr Float128 quiet_nan = std::numeric_limits<Float128>::quiet_NaN();
  constexpr Float128 signaling_nan =
      std::numeric_limits<Float128>::signaling_NaN();

  EXPECT_FALSE(x == quiet_nan);
  EXPECT_FALSE(quiet_nan == x);
  EXPECT_FALSE(x == signaling_nan);
  EXPECT_FALSE(signaling_nan == x);

  EXPECT_TRUE(x != quiet_nan);
  EXPECT_TRUE(quiet_nan != x);
  EXPECT_TRUE(x != signaling_nan);
  EXPECT_TRUE(signaling_nan != x);
}
FUZZ_TEST(Float128RelationTest, NothingEqualsNan)
    .WithDomains(ArbitraryFloat128());

void NothingIsOrderedWithNan(Float128 x) {
  constexpr Float128 quiet_nan = std::numeric_limits<Float128>::quiet_NaN();
  constexpr Float128 signaling_nan =
      std::numeric_limits<Float128>::signaling_NaN();

  EXPECT_FALSE(x > quiet_nan);
  EXPECT_FALSE(quiet_nan > x);
  EXPECT_FALSE(x > signaling_nan);
  EXPECT_FALSE(signaling_nan > x);

  EXPECT_FALSE(x >= quiet_nan);
  EXPECT_FALSE(quiet_nan >= x);
  EXPECT_FALSE(x >= signaling_nan);
  EXPECT_FALSE(signaling_nan >= x);

  EXPECT_FALSE(x < quiet_nan);
  EXPECT_FALSE(quiet_nan < x);
  EXPECT_FALSE(x < signaling_nan);
  EXPECT_FALSE(signaling_nan < x);

  EXPECT_FALSE(x <= quiet_nan);
  EXPECT_FALSE(quiet_nan <= x);
  EXPECT_FALSE(x <= signaling_nan);
  EXPECT_FALSE(signaling_nan <= x);
}
FUZZ_TEST(Float128RelationTest, NothingIsOrderedWithNan)
    .WithDomains(ArbitraryFloat128());

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

void ConversionFromDoublePreservesClass(double x) {
  EXPECT_EQ(fpclassify(Float128(x)), std::fpclassify(x));
  EXPECT_EQ(isfinite(Float128(x)), std::isfinite(x));
  EXPECT_EQ(isinf(Float128(x)), std::isinf(x));
  EXPECT_EQ(isnan(Float128(x)), std::isnan(x));
  EXPECT_EQ(isnormal(Float128(x)), std::isnormal(x));
  EXPECT_EQ(signbit(Float128(x)), std::signbit(x));
}
FUZZ_TEST(Float128ClassifyTest, ConversionFromDoublePreservesClass);

void FabsPreservesMagnitudeAndMakesNonnegative(Float128 x) {
  EXPECT_THAT(fabs(x), NanSensitiveFloat128Eq(signbit(x) ? -x : x));
}
FUZZ_TEST(Float128FunctionTest, FabsPreservesMagnitudeAndMakesNonnegative)
    .WithDomains(ArbitraryFloat128());

void FabsBehavesLikeStdFabs(double x) {
  EXPECT_THAT(static_cast<double>(fabs(Float128(x))),
              NanSensitiveDoubleEq(std::fabs(x)));
}
FUZZ_TEST(Float128FunctionTest, FabsBehavesLikeStdFabs);

void FmodBehavesLikeStdFmod(double x, double y) {
  EXPECT_THAT(static_cast<double>(fmod(Float128(x), Float128(y))),
              NanSensitiveDoubleEq(std::fmod(x, y)));
}
FUZZ_TEST(Float128FunctionTest, FmodBehavesLikeStdFmod);

void RemainderBehavesLikeStdRemainder(double x, double y) {
  EXPECT_THAT(static_cast<double>(remainder(Float128(x), Float128(y))),
              NanSensitiveDoubleEq(std::remainder(x, y)));
}
FUZZ_TEST(Float128FunctionTest, RemainderBehavesLikeStdRemainder);

void RemquoBehavesLikeStdRemquo(double x, double y) {
  int float128_quotient = 0, double_quotient = 0;
  EXPECT_THAT(
      static_cast<double>(remquo(Float128(x), Float128(y), &float128_quotient)),
      NanSensitiveDoubleEq(std::remquo(x, y, &double_quotient)));
  EXPECT_EQ(float128_quotient, double_quotient);
}
FUZZ_TEST(Float128FunctionTest, RemquoBehavesLikeStdRemquo);

void FmaBehavesLikeStdFma(double x, double y, double z) {
  EXPECT_THAT(static_cast<double>(fma(Float128(x), Float128(y), Float128(z))),
              NanSensitiveDoubleEq(std::fma(x, y, z)));
}
FUZZ_TEST(Float128FunctionTest, FmaBehavesLikeStdFma);

void FmaxBehavesLikeStdFmax(double x, double y) {
  EXPECT_THAT(static_cast<double>(fmax(Float128(x), Float128(y))),
              NanSensitiveDoubleEq(std::fmax(x, y)));
}
FUZZ_TEST(Float128FunctionTest, FmaxBehavesLikeStdFmax);

void FmaxBehavesLikeStdFmin(double x, double y) {
  EXPECT_THAT(static_cast<double>(fmin(Float128(x), Float128(y))),
              NanSensitiveDoubleEq(std::fmin(x, y)));
}
FUZZ_TEST(Float128FunctionTest, FmaxBehavesLikeStdFmin);

void FdimBehavesLikeStdFdim(double x, double y) {
  EXPECT_THAT(static_cast<double>(fdim(Float128(x), Float128(y))),
              NanSensitiveDoubleEq(std::fdim(x, y)));
}
FUZZ_TEST(Float128FunctionTest, FdimBehavesLikeStdFdim);

TEST(Float128FunctionTest, NanGeneratesNan) {
  EXPECT_TRUE(isnan(nan("")));
  EXPECT_THAT(static_cast<double>(nan("")), IsNan());
}

void ExpBehavesLikeStdExp(double x) {
  EXPECT_THAT(static_cast<double>(exp(Float128(x))),
              NanSensitiveDoubleEq(std::exp(x)));
}
FUZZ_TEST(Float128FunctionTest, ExpBehavesLikeStdExp);

void Exp2BehavesLikeStdExp2(double x) {
  EXPECT_THAT(static_cast<double>(exp2(Float128(x))),
              NanSensitiveDoubleEq(std::exp2(x)));
}
FUZZ_TEST(Float128FunctionTest, Exp2BehavesLikeStdExp2);

void Expm1BehavesLikeStdExpm1(double x) {
  EXPECT_THAT(static_cast<double>(expm1(Float128(x))),
              NanSensitiveDoubleEq(std::expm1(x)));
}
FUZZ_TEST(Float128FunctionTest, Expm1BehavesLikeStdExpm1);

void LogBehavesLikeStdLog(double x) {
  EXPECT_THAT(static_cast<double>(log(Float128(x))),
              NanSensitiveDoubleEq(std::log(x)));
}
FUZZ_TEST(Float128FunctionTest, LogBehavesLikeStdLog);

void Log10BehavesLikeStdLog10(double x) {
  EXPECT_THAT(static_cast<double>(log10(Float128(x))),
              NanSensitiveDoubleEq(std::log10(x)));
}
FUZZ_TEST(Float128FunctionTest, Log10BehavesLikeStdLog10);

void Log2BehavesLikeStdLog2(double x) {
  EXPECT_THAT(static_cast<double>(log2(Float128(x))),
              NanSensitiveDoubleEq(std::log2(x)));
}
FUZZ_TEST(Float128FunctionTest, Log2BehavesLikeStdLog2);

void Log1pBehavesLikeStdLog1p(double x) {
  EXPECT_THAT(static_cast<double>(log1p(Float128(x))),
              NanSensitiveDoubleEq(std::log1p(x)));
}
FUZZ_TEST(Float128FunctionTest, Log1pBehavesLikeStdLog1p);

void SqrtBehavesLikeStdSqrt(double x) {
  EXPECT_THAT(static_cast<double>(sqrt(Float128(x))),
              NanSensitiveDoubleEq(std::sqrt(x)));
}
FUZZ_TEST(Float128FunctionTest, SqrtBehavesLikeStdSqrt);

void CbrtBehavesLikeStdCbrt(double x) {
  EXPECT_THAT(static_cast<double>(cbrt(Float128(x))),
              NanSensitiveDoubleEq(std::cbrt(x)));
}
FUZZ_TEST(Float128FunctionTest, CbrtBehavesLikeStdCbrt);

void PowBehavesLikeStdPow(double x, double y) {
  EXPECT_THAT(static_cast<double>(pow(Float128(x), Float128(y))),
              NanSensitiveDoubleEq(std::pow(x, y)));
}
FUZZ_TEST(Float128FunctionTest, PowBehavesLikeStdPow);

void AcoshBehavesLikeStdAcosh(double x) {
  EXPECT_THAT(static_cast<double>(acosh(Float128(x))),
              NanSensitiveDoubleEq(std::acosh(x)));
}
FUZZ_TEST(Float128FunctionTest, AcoshBehavesLikeStdAcosh)
    .WithDomains(InRange(1.0, std::numeric_limits<double>::max()));

void AsinhBehavesLikeStdAsinh(double x) {
  EXPECT_THAT(static_cast<double>(asinh(Float128(x))),
              NanSensitiveDoubleEq(std::asinh(x)));
}
FUZZ_TEST(Float128FunctionTest, AsinhBehavesLikeStdAsinh);

void AtanhBehavesLikeStdAtanh(double x) {
  EXPECT_THAT(static_cast<double>(atanh(Float128(x))),
              NanSensitiveDoubleEq(std::atanh(x)));
}
FUZZ_TEST(Float128FunctionTest, AtanhBehavesLikeStdAtanh)
    .WithDomains(InRange(-1.0, 1.0));

void CosBehavesLikeStdCos(double x) {
  EXPECT_THAT(static_cast<double>(cos(Float128(x))),
              NanSensitiveDoubleEq(std::cos(x)));
}
FUZZ_TEST(Float128FunctionTest, CosBehavesLikeStdCos);

void SinBehavesLikeStdSin(double x) {
  EXPECT_THAT(static_cast<double>(sin(Float128(x))),
              NanSensitiveDoubleEq(std::sin(x)));
}
FUZZ_TEST(Float128FunctionTest, SinBehavesLikeStdSin);

void TanBehavesLikeStdTan(double x) {
  EXPECT_THAT(static_cast<double>(tan(Float128(x))),
              NanSensitiveDoubleEq(std::tan(x)));
}
FUZZ_TEST(Float128FunctionTest, TanBehavesLikeStdTan);

void CeilBehavesLikeStdCeil(double x) {
  EXPECT_THAT(static_cast<double>(ceil(Float128(x))),
              NanSensitiveDoubleEq(std::ceil(x)));
}
FUZZ_TEST(Float128FunctionTest, CeilBehavesLikeStdCeil);

void FloorBehavesLikeStdFloor(double x) {
  EXPECT_THAT(static_cast<double>(floor(Float128(x))),
              NanSensitiveDoubleEq(std::floor(x)));
}
FUZZ_TEST(Float128FunctionTest, FloorBehavesLikeStdFloor);

void TruncBehavesLikeStdTrunc(double x) {
  EXPECT_THAT(static_cast<double>(trunc(Float128(x))),
              NanSensitiveDoubleEq(std::trunc(x)));
}
FUZZ_TEST(Float128FunctionTest, TruncBehavesLikeStdTrunc);

// We need to test functions like `lrint` and `llrint` that are defined to
// return `long` and `long long`.
// NOLINTBEGIN(google-runtime-int)
// NOLINTBEGIN(runtime/int)

void RoundBehavesLikeStdRound(double x) {
  EXPECT_THAT(static_cast<double>(round(Float128(x))),
              NanSensitiveDoubleEq(std::round(x)));
}
FUZZ_TEST(Float128FunctionTest, RoundBehavesLikeStdRound);

void LroundBehavesLikeStdLround(double x) {
  EXPECT_EQ(lround(Float128(x)), std::lround(x));
}
FUZZ_TEST(Float128FunctionTest, LroundBehavesLikeStdLround)
    .WithDomains(
        InRange(std::nextafter(std::numeric_limits<long>::lowest(), 0.0),
                std::nextafter(std::numeric_limits<long>::max(), 0.0)));

void LlroundBehavesLikeStdLlround(double x) {
  EXPECT_EQ(llround(Float128(x)), std::llround(x));
}
FUZZ_TEST(Float128FunctionTest, LlroundBehavesLikeStdLlround)
    .WithDomains(
        InRange(std::nextafter(std::numeric_limits<long long>::lowest(), 0.0),
                std::nextafter(std::numeric_limits<long long>::max(), 0.0)));

void RintBehavesLikeStdRint(double x) {
  EXPECT_THAT(static_cast<double>(rint(Float128(x))),
              NanSensitiveDoubleEq(std::rint(x)));
}
FUZZ_TEST(Float128FunctionTest, RintBehavesLikeStdRint);

void LrintBehavesLikeStdLrint(double x) {
  EXPECT_EQ(lrint(Float128(x)), std::lrint(x));
}
FUZZ_TEST(Float128FunctionTest, LrintBehavesLikeStdLrint)
    .WithDomains(
        InRange(std::nextafter(std::numeric_limits<long>::lowest(), 0.0),
                std::nextafter(std::numeric_limits<long>::max(), 0.0)));

void LlrintBehavesLikeStdLlrint(double x) {
  EXPECT_EQ(llrint(Float128(x)), std::llrint(x));
}
FUZZ_TEST(Float128FunctionTest, LlrintBehavesLikeStdLlrint)
    .WithDomains(
        InRange(std::nextafter(std::numeric_limits<long long>::lowest(), 0.0),
                std::nextafter(std::numeric_limits<long long>::max(), 0.0)));

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

void FrexpBehavesLikeStdFrexp(double x) {
  int quad_exponent, double_exponent;
  EXPECT_THAT(static_cast<double>(frexp(Float128(x), &quad_exponent)),
              NanSensitiveDoubleEq(std::frexp(x, &double_exponent)));
  EXPECT_EQ(quad_exponent, double_exponent);
}
FUZZ_TEST(Float128Functiontest, FrexpBehavesLikeStdFrexp)
    .WithDomains(Finite<double>());

void LdexpBehavesLikeStdLdexp(double x, int exponent) {
  EXPECT_THAT(static_cast<double>(ldexp(Float128(x), exponent)),
              NanSensitiveDoubleEq(std::ldexp(x, exponent)));
}
FUZZ_TEST(Float128Functiontest, LdexpBehavesLikeStdLdexp);

void ModfBehavesLikeStdModf(double x) {
  Float128 quad_integral_part = 0;
  double double_integral_part = 0;
  EXPECT_THAT(static_cast<double>(modf(Float128(x), &quad_integral_part)),
              NanSensitiveDoubleEq(std::modf(x, &double_integral_part)));
  EXPECT_THAT(static_cast<double>(quad_integral_part),
              NanSensitiveDoubleEq(double_integral_part));
}
FUZZ_TEST(Float128Functiontest, ModfBehavesLikeStdModf);

void IlogbBehavesLikeStdIlogb(double x) {
  EXPECT_EQ(ilogb(Float128(x)), std::ilogb(x));
}
FUZZ_TEST(Float128Functiontest, IlogbBehavesLikeStdIlogb);

void LogbBehavesLikeStdLogb(double x) {
  EXPECT_THAT(static_cast<double>(logb(Float128(x))),
              NanSensitiveDoubleEq(std::logb(x)));
}
FUZZ_TEST(Float128Functiontest, LogbBehavesLikeStdLogb);

void NextafterBehavesLikeStdNextafter(double from, double to) {
  EXPECT_THAT(static_cast<double>(nextafter(Float128(from), Float128(to))),
              NanSensitiveDoubleEq(std::nextafter(from, to)));
}
FUZZ_TEST(Float128Functiontest, NextafterBehavesLikeStdNextafter);

void CopysignBehavesLikeStdCopysign(double x, double y) {
  EXPECT_THAT(static_cast<double>(copysign(Float128(x), Float128(y))),
              NanSensitiveDoubleEq(std::copysign(x, y)));
}
FUZZ_TEST(Float128FunctionTest, CopysignBehavesLikeStdCopysign);

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
