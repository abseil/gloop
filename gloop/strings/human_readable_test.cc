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

#include "gloop/strings/human_readable.h"

#include <float.h>

#include <cstdint>
#include <limits>
#include <string>

#include "absl/log/log.h"
#include "absl/numeric/int128.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "benchmark/benchmark.h"
#include "gtest/gtest.h"

namespace strings {
namespace {

// Wrapper to test LessThan() with a pair of strings, where the first
// string is supposed to be less than the second.
// Based on AutoCompare in strutil_unittest.cc.
void HumanReadableBytesCompare(const std::string& a, const std::string& b) {
  VLOG(1) << "         \"" << a << "\" < \"" << b << "\"";
  ASSERT_TRUE(HumanReadableNumBytes::LessThan(a, b)) << ": " << a << "/" << b;
  ASSERT_FALSE(HumanReadableNumBytes::LessThan(b, a)) << ": " << a << "/" << b;
  ASSERT_FALSE(HumanReadableNumBytes::LessThan(a, a)) << ": " << a;
  ASSERT_FALSE(HumanReadableNumBytes::LessThan(b, b)) << ": " << b;
  std::string minus_a = absl::StrFormat("-%s", a);
  std::string minus_b = absl::StrFormat("-%s", b);
  ASSERT_TRUE(HumanReadableNumBytes::LessThan(minus_b, minus_a))
      << ": " << minus_a << "/" << minus_b;
  ASSERT_FALSE(HumanReadableNumBytes::LessThan(minus_a, minus_b))
      << ": " << minus_a << "/" << minus_b;
}

TEST(HumanReadableNumBytesTest, LessThan) {
  HumanReadableBytesCompare("", "3B");
  HumanReadableBytesCompare("3B", ".06K");
  HumanReadableBytesCompare(".06K", "0.03M");
  HumanReadableBytesCompare(".03M", "10000G");
  HumanReadableBytesCompare("10000G", "10T");
  HumanReadableBytesCompare("10T", "3.01P");
  HumanReadableBytesCompare("3.01P", "3.02P");
  HumanReadableBytesCompare("3.02P", "0.007E");
}

// Based on TestHumanReadableString() in strutil_unittest.cc.
TEST(HumanReadableNumBytesTest, Int64ToString) {
#define TST(a, b)                                          \
  do {                                                     \
    EXPECT_EQ(b, HumanReadableNumBytes::ToString(a));      \
    EXPECT_EQ("-" b, HumanReadableNumBytes::ToString(-a)); \
  } while (0)
  TST(823, "823B");
  TST(1024, "1.0K");
  TST(4000, "3.9K");
  // This following case seems wrong, but I guess it's an artifact of rounding?
  TST(1048575, "1024.0K");
  TST(1048576, "1.00M");
  TST(23956812342, "22.31G");
  TST(123456789012345678, "109.65P");
#undef TST
  EXPECT_EQ("0B", HumanReadableNumBytes::ToString(int64_t{0}));
  EXPECT_EQ("-8E", HumanReadableNumBytes::ToString(
                       std::numeric_limits<int64_t>::min()));
}

TEST(HumanReadableNumBytesTest, Int128ToString) {
#define TST(a, b)                                                              \
  do {                                                                         \
    EXPECT_EQ(b, HumanReadableNumBytes::Int128ToString(absl::int128{a}));      \
    EXPECT_EQ("-" b, HumanReadableNumBytes::Int128ToString(-absl::int128{a})); \
  } while (0)
  TST(823, "823B");
  TST(1024, "1.0K");
  TST(4000, "3.9K");
  // This following case seems wrong, but I guess it's an artifact of rounding?
  TST(1048575, "1024.0K");
  TST(1048576, "1.00M");
  TST(23956812342, "22.31G");
  TST(123456789012345678, "109.65P");
  TST(5.0, "5B");
  TST(85.0, "85B");
  TST(1445.0, "1.4K");
  TST(24565.0, "24.0K");
  TST(417605.0, "407.8K");
  TST(7099285.0, "6.77M");
  TST(120687845.0, "115.10M");
  TST(2051693365.0, "1.91G");
  TST(34878787205.0, "32.48G");
  TST(592939382485.0, "552.22G");
  TST(10079969502245.0, "9.17T");
  TST(171359481538165.0, "155.85T");
  TST(2913111186148805.0, "2.59P");
  TST(49522890164529688.0, "43.99P");
  TST(841889132797004672.0, "747.75P");
  TST(14312115257549078528.0, "12.41E");
  TST(243305959378334318592.0, "211.03E");
  TST(4136201309431683612672.0, "3.50Z");
  TST(70315422260338620366848.0, "59.56Z");
  TST(1195362178425756579790848.0, "1012.51Z");
  TST(20321157033237861185355776.0, "16.81Y");
  TST(345459669565043627266146304.0, "285.76Y");
  TST(5872814382605742007121870848.0, "4857.88Y");
  TST(99837844504297617419606687744.0, "82583.93Y");
  TST(1697243356573059619278616002560.0, "1403926.80Y");
  TST(28853137061742013246261495332864.0, "23866755.59Y");
  TST(490503330049614243200843930140672.0, "405734845.01Y");
  TST(8338556610843442206471940850319360.0, "6897492365.17Y");
  TST(141755462384338512898336976028041216.0, "117257370207.81Y");
  TST(2409842860533754590144520076509839360.0, "1993375293532.85Y");
  TST(40967328629073828032456841300667269120.0, "33887379990058.48Y");

#undef TST
  EXPECT_EQ("0B", HumanReadableNumBytes::Int128ToString(absl::int128{0}));
  EXPECT_EQ("-140737488355328Y", HumanReadableNumBytes::Int128ToString(
                                     std::numeric_limits<absl::int128>::min()));
}

TEST(HumanReadableNumBytesTest, DoubleToString) {
#define TST(a, b)                                                \
  do {                                                           \
    EXPECT_EQ(b, HumanReadableNumBytes::DoubleToString(a));      \
    EXPECT_EQ("-" b, HumanReadableNumBytes::DoubleToString(-a)); \
  } while (0)
  TST(823, "823.00B");
  TST(823.5, "823.50B");
  TST(1024, "1.00K");
  TST(4000., "3.91K");
  // This following case seems wrong, but I guess it's an artifact of rounding?
  TST(1048575., "1024.00K");
  TST(1048576., "1.00M");
  TST(23956812342., "22.31G");
  TST(123456789012345678., "109.65P");
  TST(1.23e56, "1.23e+56");
#undef TST
  // Make sure we can round-trip to string and back, within 5 digits.
  // (Values in general only get 3 digits of precision here, but this
  // loop happens to test powers of two, which should fare much better
  // because we're using 1024-based abbreviations, not 1000-based.)
  for (double d = 1; d <= 1e+51; d += d) {
    for (double test : {d, -d}) {
      std::string s = HumanReadableNumBytes::DoubleToString(test);
      double val = 0;
      HumanReadableNumBytes::ToDouble(s, &val);
      EXPECT_NEAR(test, val, d / 1e5) << " DTS produced " << s;
    }
  }
}

// Based on TestHumanReadableString() in strutil_unittest.cc.
TEST(HumanReadableNumBytesTest, ToInt64) {
#define TST_REV(a, b)                                         \
  do {                                                        \
    int64_t val;                                              \
    EXPECT_TRUE(HumanReadableNumBytes::ToInt64(a, &val));     \
    EXPECT_EQ(val, b);                                        \
    EXPECT_TRUE(HumanReadableNumBytes::ToInt64("-" a, &val)); \
    EXPECT_EQ(-val, b);                                       \
  } while (0)
  TST_REV("823B", 823);
  TST_REV("1.0K", 1024);
  TST_REV("3.9K", 3994);
  TST_REV("1.00M", 1048576);
  TST_REV("22.31G", 23955180093);
  TST_REV("109.65P", 123454924785293728);
#undef TST_REV
  int64_t val;
  EXPECT_FALSE(HumanReadableNumBytes::ToInt64("nan", &val));
  EXPECT_FALSE(HumanReadableNumBytes::ToInt64("foo", &val));
  EXPECT_FALSE(HumanReadableNumBytes::ToInt64("823BC", &val));
  EXPECT_FALSE(HumanReadableNumBytes::ToInt64("K823", &val));

  // We lose precision when converting strings representing large int64
  // values using ToInt64() because the conversion goes through a double.
  // The first failure for a 64-bit double is 2^53-1 (9007199254740991).
  EXPECT_TRUE(HumanReadableNumBytes::ToInt64("9007199254740990", &val));
  EXPECT_EQ(val, int64_t{9007199254740990});  // OK
  EXPECT_TRUE(HumanReadableNumBytes::ToInt64("9007199254740991", &val));
  EXPECT_EQ(val, int64_t{9007199254740992});  // round up
  EXPECT_TRUE(HumanReadableNumBytes::ToInt64("9007199254740992", &val));
  EXPECT_EQ(val, int64_t{9007199254740992});  // OK
  EXPECT_TRUE(HumanReadableNumBytes::ToInt64("9007199254740993", &val));
  EXPECT_EQ(val, int64_t{9007199254740992});  // round down
}

TEST(HumanReadableNumBytesTest, ToDouble) {
#define TST_REV(a, b)                                          \
  do {                                                         \
    double val;                                                \
    EXPECT_TRUE(HumanReadableNumBytes::ToDouble(a, &val));     \
    EXPECT_DOUBLE_EQ(b, val);                                  \
    EXPECT_TRUE(HumanReadableNumBytes::ToDouble("-" a, &val)); \
    EXPECT_DOUBLE_EQ(b, -val);                                 \
  } while (0)

  TST_REV("123", 123.);
  TST_REV("823.00B", 823.);
  TST_REV("1.00K", 1024.);
  TST_REV("3.91K", 4003.84);
  TST_REV("1.00M", 1048576.);
  TST_REV("22.31G", 23955180093.44);
  TST_REV("109.65P", 123454924785293728.);
  TST_REV("10E", 1.152921504606847e+19);
  TST_REV("10Z", 1.1805916207174113e22);
  TST_REV("10Y", 1.2089258196146292e+25);
  TST_REV("1.23e56", 1.23e56);
#undef TST_REV
}

TEST(HumanReadableNumBytesTest, ToStringWithoutRounding) {
  struct {
    int64_t num_bytes;
    std::string bytes_string;
  } test_cases[] = {
      {0, "0B"},
      {1, "1B"},
      // Just under
      {1023, "1023B"},
      {1024, "1K"},
      // Just over
      {1025, "1025B"},
      // A multiple
      {2 << 20, "2M"},
      // Use the largest common unit.
      {(3 << 10) + (2 << 20), "2051K"},
      {(static_cast<int64_t>(200) << 30), "200G"},
      {(static_cast<int64_t>(400) << 40), "400T"},
      {(static_cast<int64_t>(800) << 50), "800P"},
      {(static_cast<int64_t>(7) << 60), "7E"},
  };
  for (const auto& test_case : test_cases) {
    EXPECT_EQ(
        HumanReadableNumBytes::ToStringWithoutRounding(test_case.num_bytes),
        test_case.bytes_string);
    // Converting back should be exact.
    int64_t num_bytes;
    EXPECT_TRUE(
        HumanReadableNumBytes::ToInt64(test_case.bytes_string, &num_bytes));
    EXPECT_EQ(test_case.num_bytes, num_bytes);

    // Same test, but negative.
    std::string neg = "-";
    neg.append(test_case.bytes_string);
    EXPECT_TRUE(HumanReadableNumBytes::ToInt64(neg, &num_bytes)) << neg;
    EXPECT_EQ(-test_case.num_bytes, num_bytes);
  }
  // Limit of positive int64 values
  EXPECT_EQ("-8E",
            HumanReadableNumBytes::ToStringWithoutRounding((int64_t{8} << 60)));
}

// Test that ToString(ToInt64()) is isomorphic for the target space of
// HumanReadableNumBytes::ToString (we obviously only check a subset).
TEST(HumanReadableNumBytesTest, TestRoundTrip) {
  for (int n = 0; n < 63; ++n) {
    int64_t i = static_cast<int64_t>(1) << n;
    SCOPED_TRACE(absl::StrFormat("%d", i));
    // i will assume all values of 2^n between zero and kint64max exclusive.
    std::string readable = HumanReadableNumBytes::ToString(i);
    int64_t value;
    EXPECT_TRUE(HumanReadableNumBytes::ToInt64(readable, &value));
    // Note that value may not equal i due to rounding
    EXPECT_EQ(HumanReadableNumBytes::ToString(value), readable);
  }

  // Same test, but negative.
  for (int n = 0; n < 64; ++n) {
    int64_t i = static_cast<uint64_t>(-1) << n;
    SCOPED_TRACE(absl::StrFormat("%d", i));
    // i will assume all values of -2^n between -1 and kint64min inclusive.
    std::string readable = HumanReadableNumBytes::ToString(i);
    int64_t value;
    EXPECT_TRUE(HumanReadableNumBytes::ToInt64(readable, &value))
        << readable << ": " << i;
    // Note that value may not equal i due to rounding
    EXPECT_EQ(HumanReadableNumBytes::ToString(value), readable);
  }
}

TEST(HumanReadableIntTest, TestToString) {
#define TST(a, b)                                     \
  do {                                                \
    EXPECT_EQ(HumanReadableInt::ToString(a), b);      \
    EXPECT_EQ(HumanReadableInt::ToString(-a), "-" b); \
  } while (0)
  TST(823, "823");
  TST(1024, "1.02k");
  TST(4000, "4.00k");
  TST(999499, "999.50k");
  TST(1000000, "1.00M");
  TST(1048575, "1.05M");
  TST(1048576, "1.05M");
  TST(23956812342, "23.96B");
  TST(123456789012345678, "1.23E+17");
  TST(9223372036854775807, "9.22E+18");  // 2^63 - 1
#undef TST
  // Test min int64.
  EXPECT_EQ(HumanReadableInt::ToString(std::numeric_limits<int64_t>::min()),
            "-9.22E+18");
}

TEST(HumanReadableIntTest, TestInt128ToString) {
#define TST(a, b)                                                         \
  do {                                                                    \
    EXPECT_EQ(HumanReadableInt::Int128ToString(absl::int128{a}), b);      \
    EXPECT_EQ(HumanReadableInt::Int128ToString(-absl::int128{a}), "-" b); \
  } while (0)
  TST(823, "823");
  TST(1024, "1.02k");
  TST(4000, "4.00k");
  TST(999499, "999.50k");
  TST(1000000, "1.00M");
  TST(1048575, "1.05M");
  TST(1048576, "1.05M");
  TST(23956812342, "23.96B");
  TST(123456789012345678, "1.23E+17");
  TST(9223372036854775807, "9.22E+18");  // 2^63 - 1
  TST(123456789123456789123456.0, "1.23E+23");
  TST(std::numeric_limits<absl::int128>::max(), "1.7E+38");  // 2^127 - 1
#undef TST
  // Test min int128.
  EXPECT_EQ(HumanReadableInt::Int128ToString(
                std::numeric_limits<absl::int128>::min()),
            "-1.7E+38");
}

TEST(HumanReadableNumTest, TestDoubleToString) {
#define TST(a, b)                                           \
  do {                                                      \
    EXPECT_EQ(HumanReadableNum::DoubleToString(a), b);      \
    EXPECT_EQ(HumanReadableNum::DoubleToString(-a), "-" b); \
  } while (0)
  TST(.9, "0.900");
  TST(1.9, "1.90");
  TST(9.929, "9.93");
  TST(10.929, "10.9");
  TST(99.9, "99.9");
  TST(823.0, "823");
  TST(1024.0, "1.02k");
  TST(4000.0, "4.00k");
  TST(999499.0, "999.50k");
  TST(1000000.0, "1.00M");
  TST(1048575.0, "1.05M");
  TST(1048576.0, "1.05M");
  TST(23956812342.0, "23.96B");
  TST(123456789012345678.0, "1.23E+17");
#undef TST

  // Make sure we can round-trip to string and back, within 5 digits.
  // (Values in general only get 3 digits of precision here, but this
  // loop happens to test 1.01 times powers of ten, which fares much
  // better.)
  for (double d = 1.01; d <= 1e+51; d *= 10) {
    for (double test : {d, -d}) {
      std::string s = HumanReadableNum::DoubleToString(test);
      double val = 0;
      HumanReadableNum::ToDouble(s, &val);
      EXPECT_NEAR(test, val, d / 1e5) << " DTS produced " << s;
    }
  }
}

TEST(HumanReadableNumTest, ToDoubleSucceeds) {
  double v;
  EXPECT_TRUE(HumanReadableNum::ToDouble("0", &v));
  EXPECT_DOUBLE_EQ(0, v);

  EXPECT_TRUE(HumanReadableNum::ToDouble("1.25", &v));
  EXPECT_DOUBLE_EQ(1.25, v);

  EXPECT_TRUE(HumanReadableNum::ToDouble("-1.25", &v));
  EXPECT_DOUBLE_EQ(-1.25, v);

  EXPECT_TRUE(HumanReadableNum::ToDouble("345", &v));
  EXPECT_DOUBLE_EQ(345, v);

  EXPECT_TRUE(HumanReadableNum::ToDouble("1K", &v));
  EXPECT_DOUBLE_EQ(1000, v);

  EXPECT_TRUE(HumanReadableNum::ToDouble("5.3k", &v));
  EXPECT_DOUBLE_EQ(5300, v);

  EXPECT_TRUE(HumanReadableNum::ToDouble("10.22M", &v));
  EXPECT_DOUBLE_EQ(10220000, v);

  EXPECT_TRUE(HumanReadableNum::ToDouble("-107.99B", &v));
  EXPECT_DOUBLE_EQ(-107990000000LL, v);

  EXPECT_TRUE(HumanReadableNum::ToDouble("-107.99G", &v));
  EXPECT_DOUBLE_EQ(-107990000000LL, v);

  EXPECT_TRUE(HumanReadableNum::ToDouble("8.3T", &v));
  EXPECT_DOUBLE_EQ(8.3e12, v);

  EXPECT_FALSE(HumanReadableNum::ToDouble("", &v));
}

TEST(HumanReadableNumTest, ToDoubleFails) {
  double v;
  EXPECT_FALSE(HumanReadableNum::ToDouble("k0", &v));
  EXPECT_FALSE(HumanReadableNum::ToDouble("100kk", &v));
  EXPECT_FALSE(HumanReadableNum::ToDouble("1.1F", &v));
  EXPECT_FALSE(HumanReadableNum::ToDouble("ggg", &v));
}

TEST(HumanReadableIntTest, ToInt64Succeeds) {
  int64_t v;
  EXPECT_TRUE(HumanReadableInt::ToInt64("0", &v));
  EXPECT_EQ(0, v);

  EXPECT_TRUE(HumanReadableInt::ToInt64("1.25", &v));
  EXPECT_EQ(1, v);

  EXPECT_TRUE(HumanReadableInt::ToInt64("-1.25", &v));
  EXPECT_EQ(-1, v);

  EXPECT_TRUE(HumanReadableInt::ToInt64("345", &v));
  EXPECT_EQ(345, v);

  EXPECT_TRUE(HumanReadableInt::ToInt64("5.3k", &v));
  EXPECT_EQ(5300, v);

  EXPECT_TRUE(HumanReadableInt::ToInt64("10.22M", &v));
  EXPECT_EQ(10220000, v);

  EXPECT_TRUE(HumanReadableInt::ToInt64("-107.99B", &v));
  EXPECT_EQ(int64_t{-107990000000}, v);

  EXPECT_TRUE(HumanReadableInt::ToInt64("-107.99G", &v));
  EXPECT_EQ(int64_t{-107990000000}, v);

  EXPECT_TRUE(HumanReadableInt::ToInt64("8.3T", &v));
  EXPECT_EQ(8.3e12, v);

  // Maximum int64_t value representable in double without losing precision.
  // We require `double` to be ieee754 with 53 bits of mantissa.
  static_assert(std::numeric_limits<double>::digits == 53, "");
  EXPECT_TRUE(HumanReadableInt::ToInt64("0x7ffffffffffffc00", &v));
  EXPECT_EQ(0x7ffffffffffffc00, v);

  // These might succeed or not depending on the behavior of double rounding,
  // but they should not have UB (ASan should not fail here).
  for (int64_t value = 0x7ffffffffffffc00;; value |= value >> 1) {
    if (HumanReadableInt::ToInt64(absl::StrCat(value), &v)) {
      EXPECT_GE(v, 0x7ffffffffffffc00);
    }
    if (value & 1) {
      // This was the last one. Stop.
      break;
    }
  }

  EXPECT_FALSE(HumanReadableInt::ToInt64("", &v));
}

TEST(HumanReadableIntTest, ToInt64Fails) {
  int64_t v;
  EXPECT_FALSE(HumanReadableInt::ToInt64("k0", &v));
  EXPECT_FALSE(HumanReadableInt::ToInt64("100kk", &v));
  EXPECT_FALSE(HumanReadableInt::ToInt64("1.1F", &v));
  EXPECT_FALSE(HumanReadableInt::ToInt64("ggg", &v));
  EXPECT_FALSE(HumanReadableInt::ToInt64("0x8000000000000000", &v));
  EXPECT_FALSE(HumanReadableInt::ToInt64("nan", &v));
}

TEST(HumanReadableElapsedTime, ConversionWithRoundingWorks) {
  EXPECT_EQ("1 ms",
            HumanReadableElapsedTime::ToShortString(0.00099999999999592385));
  EXPECT_EQ("1 ms", HumanReadableElapsedTime::ToShortString(0.0009995));
  EXPECT_EQ("999 us", HumanReadableElapsedTime::ToShortString(0.0009994));
  EXPECT_EQ("1 s",
            HumanReadableElapsedTime::ToShortString(0.99999999999592385));
  EXPECT_EQ("1 s", HumanReadableElapsedTime::ToShortString(0.9995));
  EXPECT_EQ("999 ms", HumanReadableElapsedTime::ToShortString(0.9994));
}

TEST(HumanReadableElapsedTime, ConversionWorks) {
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(-10), "-10 s");
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(-0.001), "-1 ms");
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(-60.0), "-1 min");
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(0.00000001), "0.01 us");
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(0.0000012), "1.2 us");
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(0.0012), "1.2 ms");
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(0.12), "120 ms");
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(1.12), "1.12 s");
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(90.0), "1.5 min");
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(600.0), "10 min");
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(9000.0), "2.5 h");
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(87480.0), "1.01 days");
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(30.1 * 86400.0),
            "30.1 days");
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(7776000.0), "2.96 months");
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(78840000.0), "2.5 years");
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(382386614.40),
            "12.1 years");
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(DBL_MAX), "5.7e+300 years");

  // The same tests as above but for absl::Duration values < 24h.
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(absl::Seconds(-10)),
            "-10 s");
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(absl::Milliseconds(-1)),
            "-1 ms");
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(absl::Minutes(-1)),
            "-1 min");
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(absl::Microseconds(0.01)),
            "0.01 us");
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(absl::Microseconds(1.2)),
            "1.2 us");
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(absl::Milliseconds(1.2)),
            "1.2 ms");
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(absl::Milliseconds(120)),
            "120 ms");
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(absl::Seconds(1.12)),
            "1.12 s");
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(absl::Minutes(1.5)),
            "1.5 min");
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(absl::Minutes(10)),
            "10 min");
  EXPECT_EQ(HumanReadableElapsedTime::ToShortString(absl::Hours(2.5)), "2.5 h");
}

void TestElapsedConversion(double expected_value, absl::string_view str) {
  double actual_value;
  EXPECT_TRUE(HumanReadableElapsedTime::ToDouble(str, &actual_value))
      << "Conversion of " << str << " failed";
  EXPECT_DOUBLE_EQ(expected_value, actual_value)
      << "Conversion of " << str << " mismatched";

  absl::Duration actual_duration;
  EXPECT_TRUE(HumanReadableElapsedTime::ToDuration(str, &actual_duration))
      << "Conversion of " << str << " failed";
  EXPECT_EQ(absl::Seconds(expected_value), actual_duration)
      << "Conversion of " << str << " mismatched";
}

TEST(HumanReadableElapsedTime, ConversionToWorks) {
  // Test that each ending works properly.
  TestElapsedConversion(-10, "-10 s");
  TestElapsedConversion(-0.001, "-1 ms");
  TestElapsedConversion(-60.0, "-1 min");
  TestElapsedConversion(60.0, "1 m");
  TestElapsedConversion(60.0, "1 minute");
  TestElapsedConversion(60.0, "1 minutes");
  TestElapsedConversion(0.00000001, "0.01 us");
  TestElapsedConversion(0.00000001, "0.01 uss");
  TestElapsedConversion(0.00000001, "0.01 usec");
  TestElapsedConversion(0.00000001, "0.01 usecs");
  TestElapsedConversion(0.00000001, "0.01 microsecs");
  TestElapsedConversion(0.00000001, "0.01 microsec");
  TestElapsedConversion(0.00000001, "0.01 microsecond");
  TestElapsedConversion(0.00000001, "0.01 microseconds");
  TestElapsedConversion(0.0012, "1.2 ms");
  TestElapsedConversion(0.0012, "1.2 mss");
  TestElapsedConversion(0.0012, "1.2 msec");
  TestElapsedConversion(0.0012, "1.2 msecs");
  TestElapsedConversion(0.0012, "1.2 millisec");
  TestElapsedConversion(0.0012, "1.2 millisecs");
  TestElapsedConversion(0.0012, "1.2 millisecond");
  TestElapsedConversion(0.0012, "1.2 milliseconds");
  TestElapsedConversion(0.12, "120 ms");
  TestElapsedConversion(1.12, "1.12 s");
  TestElapsedConversion(1.12, "1.12 ss");
  TestElapsedConversion(1.12, "1.12 sec");
  TestElapsedConversion(1.12, "1.12 secs");
  TestElapsedConversion(1.12, "1.12 second");
  TestElapsedConversion(1.12, "1.12 seconds");
  TestElapsedConversion(90.0, "1.5 min");
  TestElapsedConversion(90.0, "1.5 mins");
  TestElapsedConversion(90.0, "1.5 minute");
  TestElapsedConversion(90.0, "1.5 minutes");
  TestElapsedConversion(90.0, "1.5 m");
  // ms means milliseconds, not minutes
  TestElapsedConversion(600.0, "10 min");
  TestElapsedConversion(9000.0, "2.5 h");
  TestElapsedConversion(9000.0, "2.5 hs");
  TestElapsedConversion(9000.0, "2.5 hr");
  TestElapsedConversion(9000.0, "2.5 hrs");
  TestElapsedConversion(9000.0, "2.5 hour");
  TestElapsedConversion(9000.0, "2.5 hours");
  TestElapsedConversion(87264.0, "1.01 day");
  TestElapsedConversion(87264.0, "1.01 days");
  TestElapsedConversion(87264.0, "1.01 d");
  TestElapsedConversion(87264.0, "1.01 ds");
  TestElapsedConversion(1209600.0, "2 week");
  TestElapsedConversion(1209600.0, "2 weeks");
  TestElapsedConversion(1209600.0, "2 wk");
  TestElapsedConversion(1209600.0, "2 wks");
  TestElapsedConversion(1209600.0, "2 w");
  TestElapsedConversion(1209600.0, "2 ws");
  TestElapsedConversion(7672320.00, "2.96 month");
  TestElapsedConversion(7672320.00, "2.96 months");
  TestElapsedConversion(7672320.00, "2.96 mon");
  TestElapsedConversion(7672320.00, "2.96 mons");
  TestElapsedConversion(7672320.00, "2.96 M");
  TestElapsedConversion(7672320.00, "2.96 Ms");
  TestElapsedConversion(78840000.0, "2.5 year");
  TestElapsedConversion(78840000.0, "2.5 years");
  TestElapsedConversion(78840000.0, "2.5 yr");
  TestElapsedConversion(78840000.0, "2.5 yrs");
  TestElapsedConversion(78840000.0, "2.5 y");
  TestElapsedConversion(78840000.0, "2.5 ys");
  TestElapsedConversion(381585600.0, "12.1 years");
  // Exponential notation okay as well
  TestElapsedConversion(31536000e+4, "1e+4 yrs");
  // Test combining units
  TestElapsedConversion(90.0, "1m 30s");
  TestElapsedConversion(90.0, "1m30s");
  TestElapsedConversion(90.0, "30s1m");
  TestElapsedConversion(90.0, "+30s1m");
  // A leading minus sign applies to entrie expression
  TestElapsedConversion(-90.0, "-1m30s");
  // Extra white space is generally okay.
  TestElapsedConversion(-1, "  -\t1    s\t");

  // Negative tests
  double value;
  EXPECT_FALSE(HumanReadableElapsedTime::ToDouble("10 dayz", &value));
  EXPECT_FALSE(HumanReadableElapsedTime::ToDouble("1mmm", &value));
  EXPECT_FALSE(HumanReadableElapsedTime::ToDouble("3.5 u", &value));
  EXPECT_FALSE(HumanReadableElapsedTime::ToDouble("1 football", &value));
  EXPECT_FALSE(HumanReadableElapsedTime::ToDouble("3.5f+3 sec", &value));
  EXPECT_FALSE(HumanReadableElapsedTime::ToDouble("", &value));
  EXPECT_FALSE(HumanReadableElapsedTime::ToDouble("-", &value));
  EXPECT_FALSE(HumanReadableElapsedTime::ToDouble(" ", &value));
  EXPECT_FALSE(HumanReadableElapsedTime::ToDouble(" - ", &value));
  EXPECT_FALSE(HumanReadableElapsedTime::ToDouble("sec", &value));
  EXPECT_FALSE(HumanReadableElapsedTime::ToDouble("-sec", &value));
  EXPECT_FALSE(HumanReadableElapsedTime::ToDouble("1m-30sec", &value));
  EXPECT_FALSE(HumanReadableElapsedTime::ToDouble("1m+30sec", &value));
  EXPECT_FALSE(HumanReadableElapsedTime::ToDouble("-1m-30sec", &value));
  EXPECT_FALSE(HumanReadableElapsedTime::ToDouble("1", &value));
}

void BM_WithoutRounding(benchmark::State& state) {
  for (auto s : state) {
    HumanReadableNumBytes::ToStringWithoutRounding(8 << 20);
  }
}

BENCHMARK(BM_WithoutRounding);

}  // namespace
}  // namespace strings
