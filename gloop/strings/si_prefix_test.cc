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

#include "gloop/strings/si_prefix.h"

#include <cctype>
#include <cmath>
#include <iosfwd>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "gloop/util/tuple/components/dump_vars.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace strings {
namespace si_prefix {
namespace {

using ::testing::Eq;
using ::testing::Optional;

template <typename T>
std::string StructParamName(const ::testing::TestParamInfo<T>& info) {
  std::ostringstream ss;
  PrintTo(info.param, &ss);
  std::string raw = ss.str();
  std::string name;
  for (char c : raw) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      name += c;
    } else if (c == '-' || c == '.' || c == '+' || c == '_' || c == ' ') {
      if (name.empty() || name.back() != '_') {
        name += '_';
      }
    }
  }
  while (!name.empty() && name.back() == '_') {
    name.pop_back();
  }
  if (name.empty()) {
    name = std::to_string(info.index);
  } else {
    name += "_" + std::to_string(info.index);
  }
  return name;
}

std::string StringParamName(const ::testing::TestParamInfo<std::string>& info) {
  if (info.param.empty()) return "EmptyString_" + std::to_string(info.index);
  std::string name;
  for (char c : info.param) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      name += c;
    } else {
      name += '_';
    }
  }
  name += "_" + std::to_string(info.index);
  return name;
}

struct ToExponentAndMantissaTestCase {
  double value;

  std::string expected_mantissa;
  int expected_exponent;
};

void PrintTo(const ToExponentAndMantissaTestCase& tc, std::ostream* os) {
  *os << "{value: " << tc.value << ", expected_mantissa: \""
      << tc.expected_mantissa
      << "\", expected_exponent: " << tc.expected_exponent << "}";
}

class ToExponentAndMantissa1000Test
    : public ::testing::TestWithParam<ToExponentAndMantissaTestCase> {};

TEST_P(ToExponentAndMantissa1000Test, ExponentBase_1000) {
  const ToExponentAndMantissaTestCase& tc = GetParam();
  std::string mantissa;
  int exponent;
  ToExponentAndMantissa(tc.value, 1.0, 2, ExponentBase::k1000, &mantissa,
                        &exponent);

  EXPECT_EQ(mantissa, tc.expected_mantissa);
  EXPECT_EQ(exponent, tc.expected_exponent);
}

INSTANTIATE_TEST_SUITE_P(
    All, ToExponentAndMantissa1000Test,
    ::testing::ValuesIn(std::vector<ToExponentAndMantissaTestCase>{
        // Zero
        {-0.0, "-0.00", 0},
        {0.0, "0.00", 0},

        // Negative exponents
        {1e-12, "1.00", -4},
        {1e-10, "100.00", -4},
        {8.7654e-8, "87.65", -3},
        {1e-3, "1.00", -1},
        {0.99997, "999.97", -1},

        // Non-negative exponents
        {1, "1.00", 0},
        {1.5, "1.50", 0},
        {999.99, "999.99", 0},
        {1000, "1.00", 1},
        {1e6, "1.00", 2},
        {87654321, "87.65", 2},
        {1e10, "10.00", 3},
        {1e12, "1.00", 4},
        {1e15, "1.00", 5},
        {1e18, "1.00", 6},
        {1e21, "1.00", 7},
        {1e24, "1.00", 8},

        // Outside of SI prefix range
        {1.5e-310, "150.00e-312", 0},
        {1e-42, "1.00e-42", 0},
        {1e-40, "100.00e-42", 0},
        {1e-28, "100.00e-30", 0},
        {1e-27, "1.00e-27", 0},

        {1e27, "1.00e+27", 0},
        {1e28, "10.00e+27", 0},
        {1e40, "10.00e+39", 0},
        {1e42, "1.00e+42", 0},
        {1e306, "1.00e+306", 0},
        {1.5e307, "15.00e+306", 0},
        {1.5e308, "150.00e+306", 0},

        {-1e27, "-1.00e+27", 0},
        {-1e28, "-10.00e+27", 0},
        {-1e40, "-10.00e+39", 0},
        {-1e42, "-1.00e+42", 0},
        {-1e306, "-1.00e+306", 0},
        {-1.5e307, "-15.00e+306", 0},
        {-1.5e308, "-150.00e+306", 0},

        // Edge cases
        {-std::numeric_limits<double>::infinity(), "-inf", 0},
        {std::numeric_limits<double>::lowest(), "-179.77e+306", 0},
        {-std::numeric_limits<double>::denorm_min(), "-4.94e-324", 0},
        {std::numeric_limits<double>::denorm_min(), "4.94e-324", 0},
        {std::numeric_limits<double>::min(), "22.25e-309", 0},
        {std::numeric_limits<double>::max(), "179.77e+306", 0},
        {std::numeric_limits<double>::infinity(), "inf", 0},
        {std::numeric_limits<double>::quiet_NaN(), "nan", 0},
    }),
    StructParamName<ToExponentAndMantissaTestCase>);

class ToExponentAndMantissa1024Test
    : public ::testing::TestWithParam<ToExponentAndMantissaTestCase> {};

TEST_P(ToExponentAndMantissa1024Test, ExponentBase_1024) {
  const ToExponentAndMantissaTestCase& tc = GetParam();
  std::string mantissa;
  int exponent;
  ToExponentAndMantissa(tc.value, 1.0, 2, ExponentBase::k1024, &mantissa,
                        &exponent);

  EXPECT_EQ(mantissa, tc.expected_mantissa);
  EXPECT_EQ(exponent, tc.expected_exponent);
}

INSTANTIATE_TEST_SUITE_P(
    All, ToExponentAndMantissa1024Test,
    ::testing::ValuesIn(std::vector<ToExponentAndMantissaTestCase>{
        // Zero
        {-0.0, "-0.00", 0},
        {0.0, "0.00", 0},

        // Negative exponents
        {1.00 * std::pow(2.0, -40), "1.00", -4},
        {1.50 * std::pow(2.0, -30), "1.50", -3},
        {8.76 * std::pow(2.0, -20), "8.76", -2},
        {100.00 * std::pow(2.0, -10), "100.00", -1},
        {1.00 * std::pow(2.0, -10), "1.00", -1},
        {1 - 1 / 1024.0, "1023.00", -1},

        // Non-negative exponents
        {1, "1.00", 0},
        {1.5, "1.50", 0},
        {1023, "1023.00", 0},
        {1024, "1.00", 1},
        {1 * std::pow(2.0, 20), "1.00", 2},
        {35.7 * std::pow(2.0, 20), "35.70", 2},
        {1 * std::pow(2.0, 30), "1.00", 3},
        {1 * std::pow(2.0, 40), "1.00", 4},
        {1 * std::pow(2.0, 50), "1.00", 5},
        {1 * std::pow(2.0, 60), "1.00", 6},
        {1 * std::pow(2.0, 70), "1.00", 7},
        {1 * std::pow(2.0, 80), "1.00", 8},
        {1023 * std::pow(2.0, 80), "1023.00", 8},

        // Outside of IEC binary prefix range
        {1.5 * std::pow(2.0, -1030), "1.50*2^-1030", 0},
        {1.5 * std::pow(2.0, -1028), "6.00*2^-1030", 0},
        {1 * std::pow(2.0, -150), "1.00*2^-150", 0},
        {1 * std::pow(2.0, -95), "32.00*2^-100", 0},
        {1 * std::pow(2.0, -90), "1.00*2^-90", 0},

        {1 * std::pow(2.0, 90), "1.00*2^90", 0},
        {1 * std::pow(2.0, 95), "32.00*2^90", 0},
        {1 * std::pow(2.0, 150), "1.00*2^150", 0},
        {1 * std::pow(2.0, 900), "1.00*2^900", 0},
        {1.5 * std::pow(2.0, 1023), "12.00*2^1020", 0},

        {-1 * std::pow(2.0, 90), "-1.00*2^90", 0},
        {-1 * std::pow(2.0, 95), "-32.00*2^90", 0},
        {-1 * std::pow(2.0, 150), "-1.00*2^150", 0},
        {-1 * std::pow(2.0, 900), "-1.00*2^900", 0},
        {-1.5 * std::pow(2.0, 1023), "-12.00*2^1020", 0},

        // Edge cases
        {-std::numeric_limits<double>::infinity(), "-inf", 0},
        {std::numeric_limits<double>::lowest(), "-16.00*2^1020", 0},
        {-std::numeric_limits<double>::denorm_min(), "-4.94e-324", 0},
        {std::numeric_limits<double>::denorm_min(), "4.94e-324", 0},
        {std::numeric_limits<double>::min(), "256.00*2^-1030", 0},
        {std::numeric_limits<double>::max(), "16.00*2^1020", 0},
        {std::numeric_limits<double>::infinity(), "inf", 0},
        {std::numeric_limits<double>::quiet_NaN(), "nan", 0},
    }),
    StructParamName<ToExponentAndMantissaTestCase>);

TEST(ToExponentAndMantissa, Threshold) {
  constexpr auto k1000 = ExponentBase::k1000;

  std::string mantissa;
  int exponent;

  // With a threshold of 1.0, anything at or above a power of 1k should roll
  // over the exponent.
  ToExponentAndMantissa(999, 1.0, 3, k1000, &mantissa, &exponent);
  EXPECT_EQ(mantissa, "999.000");
  EXPECT_EQ(exponent, 0);

  ToExponentAndMantissa(1000, 1.0, 3, k1000, &mantissa, &exponent);
  EXPECT_EQ(mantissa, "1.000");
  EXPECT_EQ(exponent, 1);

  ToExponentAndMantissa(1001, 1.0, 3, k1000, &mantissa, &exponent);
  EXPECT_EQ(mantissa, "1.001");
  EXPECT_EQ(exponent, 1);

  // But with a threshold of 2.0 this roll-over should happen higher.
  ToExponentAndMantissa(1999, 2.0, 3, k1000, &mantissa, &exponent);
  EXPECT_EQ(mantissa, "1999.000");
  EXPECT_EQ(exponent, 0);

  ToExponentAndMantissa(2000, 2.0, 3, k1000, &mantissa, &exponent);
  EXPECT_EQ(mantissa, "2.000");
  EXPECT_EQ(exponent, 1);

  ToExponentAndMantissa(2001, 2.0, 3, k1000, &mantissa, &exponent);
  EXPECT_EQ(mantissa, "2.001");
  EXPECT_EQ(exponent, 1);
}

TEST(LowLevel, SIExponentToPrefix) {
  EXPECT_EQ(ExponentToPrefix(8, false), "Y");
  EXPECT_EQ(ExponentToPrefix(7, false), "Z");
  EXPECT_EQ(ExponentToPrefix(6, false), "E");
  EXPECT_EQ(ExponentToPrefix(5, false), "P");
  EXPECT_EQ(ExponentToPrefix(4, false), "T");
  EXPECT_EQ(ExponentToPrefix(3, false), "G");
  EXPECT_EQ(ExponentToPrefix(2, false), "M");
  EXPECT_EQ(ExponentToPrefix(1, false), "k");
  EXPECT_EQ(ExponentToPrefix(-1, false), "m");
  EXPECT_EQ(ExponentToPrefix(-2, false), "μ");
  EXPECT_EQ(ExponentToPrefix(-3, false), "n");
  EXPECT_EQ(ExponentToPrefix(-4, false), "p");
  EXPECT_EQ(ExponentToPrefix(-5, false), "f");
  EXPECT_EQ(ExponentToPrefix(-6, false), "a");
  EXPECT_EQ(ExponentToPrefix(-7, false), "z");
  EXPECT_EQ(ExponentToPrefix(-8, false), "y");
}

TEST(LowLevel, IECExponentToPrefix) {
  EXPECT_EQ(ExponentToPrefix(8, true), "Yi");
  EXPECT_EQ(ExponentToPrefix(7, true), "Zi");
  EXPECT_EQ(ExponentToPrefix(6, true), "Ei");
  EXPECT_EQ(ExponentToPrefix(5, true), "Pi");
  EXPECT_EQ(ExponentToPrefix(4, true), "Ti");
  EXPECT_EQ(ExponentToPrefix(3, true), "Gi");
  EXPECT_EQ(ExponentToPrefix(2, true), "Mi");
  EXPECT_EQ(ExponentToPrefix(1, true), "Ki");
  EXPECT_EQ(ExponentToPrefix(-1, true), "mi");
  EXPECT_EQ(ExponentToPrefix(-2, true), "μi");
  EXPECT_EQ(ExponentToPrefix(-3, true), "ni");
  EXPECT_EQ(ExponentToPrefix(-4, true), "pi");
  EXPECT_EQ(ExponentToPrefix(-5, true), "fi");
  EXPECT_EQ(ExponentToPrefix(-6, true), "ai");
  EXPECT_EQ(ExponentToPrefix(-7, true), "zi");
  EXPECT_EQ(ExponentToPrefix(-8, true), "yi");
}

TEST(ToString, Decimal) {
  EXPECT_EQ(ToDecimalString(1011), "1.01k");
  EXPECT_EQ(ToDecimalStringFullySpecified(1011, 2.0, 2), "1011.00");
  EXPECT_EQ(ToDecimalStringFullySpecified(1234000000., 5000, 2), "1234000.00k");
  EXPECT_EQ(ToDecimalString(-876123123), "-876.12M");
  EXPECT_EQ(ToDecimalString(5.434e-8), "54.34n");
  EXPECT_EQ(ToDecimalString(-9.7e-3), "-9.70m");
  EXPECT_EQ(ToDecimalString(1e-3), "1.00m");
  EXPECT_EQ(ToDecimalString(0.00100000000000000000001), "1.00m");
  EXPECT_EQ(ToDecimalString(0.0010000000001), "1.00m");
  EXPECT_EQ(ToDecimalString(0.00099999999), "1.00m");
  EXPECT_EQ(ToDecimalString(0), "0.00");
  EXPECT_EQ(ToDecimalString(1), "1.00");
  EXPECT_EQ(ToDecimalString(2), "2.00");
  EXPECT_EQ(ToDecimalString(std::numeric_limits<double>::quiet_NaN()), "nan");
  EXPECT_EQ(ToDecimalString(std::numeric_limits<double>::infinity()), "inf");
  EXPECT_EQ(ToDecimalString(-std::numeric_limits<double>::infinity()), "-inf");
  // Largest / smallest prefix tests.
  EXPECT_EQ(ToDecimalString(99999e22), "999.99Y");
  EXPECT_EQ(ToDecimalString(1e27), "1.00e+27");
  // Eng notation tests (exponent should be factor of three)
  EXPECT_EQ(ToDecimalString(1e45), "1.00e+45");
  EXPECT_EQ(ToDecimalString(1e46), "10.00e+45");
  EXPECT_EQ(ToDecimalString(1e47), "100.00e+45");
  EXPECT_EQ(ToDecimalString(1e-45), "1.00e-45");
  EXPECT_EQ(ToDecimalString(1e-44), "10.00e-45");
  EXPECT_EQ(ToDecimalString(1e-43), "100.00e-45");
  EXPECT_EQ(ToDecimalString(-1e46), "-10.00e+45");
  EXPECT_EQ(ToDecimalString(1e300), "1.00e+300");
  EXPECT_EQ(ToDecimalString(1e-24), "1.00y");
  EXPECT_EQ(ToDecimalString(1e-27), "1.00e-27");
}

TEST(ToString, Binary) {
  EXPECT_EQ(ToBinaryString(1034), "1.01k");
  EXPECT_EQ(ToBinaryString(87654321), "83.59M");
  EXPECT_EQ(ToBinaryString(0.000123456), "129.45μ");
}

TEST(ToString, IEC) {
  EXPECT_EQ(ToIECBinaryString(1034), "1.01Ki");
  EXPECT_EQ(ToIECBinaryString(87654321), "83.59Mi");
  // This next one is actually wrong (No small IEC prefixes)
  EXPECT_EQ(ToIECBinaryString(0.000123456), "129.45μi");

  EXPECT_EQ(ToIECBinaryString(1024), "1.00Ki");
  EXPECT_EQ(ToIECBinaryString(1023), "1023.00");
  EXPECT_EQ(ToIECBinaryString(std::pow(2.0, 40.0)), "1.00Ti");
  EXPECT_EQ(ToIECBinaryString(std::pow(2.0, 60.0)), "1.00Ei");
  EXPECT_EQ(ToIECBinaryString(std::pow(2.0, 60.0) - std::pow(2.0, 50)),
            "1023.00Pi");
  EXPECT_EQ(ToIECBinaryString(std::pow(2.0, 60.0) - std::pow(2.0, 43)),
            "1023.99Pi");
  // Out of prefix range. These are in a kind of analog to eng notation,
  // where the exponent is always a multiple of 10.
  EXPECT_EQ(ToIECBinaryString(std::pow(2.0, 90.0)), "1.00*2^90");
  EXPECT_EQ(ToIECBinaryString(std::pow(2.0, 92.0)), "4.00*2^90");
  EXPECT_EQ(ToIECBinaryString(std::pow(2.0, -90.0)), "1.00*2^-90");
  EXPECT_EQ(ToIECBinaryString(std::pow(2.0, -88.0)), "4.00*2^-90");
  // _Really_ out of prefix range.
  EXPECT_EQ(ToIECBinaryString(1.99 * std::pow(2.0, -300.0)), "1.99*2^-300");
  EXPECT_EQ(ToIECBinaryString(4.36 * std::pow(2.0, 700.0)), "4.36*2^700");
}

class ConsumeInvalidInputTest : public ::testing::TestWithParam<std::string> {};

TEST_P(ConsumeInvalidInputTest, InvalidInput) {
  EXPECT_THAT(Consume(GetParam()), Eq(std::nullopt));
}

INSTANTIATE_TEST_SUITE_P(All, ConsumeInvalidInputTest,
                         ::testing::ValuesIn(std::vector<std::string>{
                             "",
                             "-",
                             "+",
                             "+1",
                             "+1m",
                             std::string{1, '\0'},
                             "a",
                             "foobarbaz",
                             "x17",
                             "x17M",
                             "x17MB",
                             "x17MiB",
                             "aMiB",
                             "-aMiB",
                         }),
                         StringParamName);

struct ConsumeTestCase {
  std::string input;

  double expected_mantissa;
  int expected_exponent = 0;
  bool expected_iec = false;
  std::string expected_suffix;
};

void PrintTo(const ConsumeTestCase& tc, std::ostream* os) {
  *os << "{input: \"" << tc.input
      << "\", expected_mantissa: " << tc.expected_mantissa
      << ", expected_exponent: " << tc.expected_exponent
      << ", expected_iec: " << (tc.expected_iec ? "true" : "false")
      << ", expected_suffix: \"" << tc.expected_suffix << "\"}";
}

class ConsumeValidInputTest : public ::testing::TestWithParam<ConsumeTestCase> {
};

TEST_P(ConsumeValidInputTest, ValidInput) {
  const ConsumeTestCase& tc = GetParam();
  const std::optional<ParseResult> result = Consume(tc.input);
  ASSERT_TRUE(result.has_value());

  if (std::isnan(tc.expected_mantissa)) {
    EXPECT_TRUE(std::isnan(result->mantissa)) << DUMP_VARS(result->mantissa);
  } else {
    EXPECT_EQ(result->mantissa, tc.expected_mantissa);
  }

  EXPECT_EQ(result->exponent, tc.expected_exponent);
  EXPECT_EQ(result->iec, tc.expected_iec);
  EXPECT_EQ(result->remaining, tc.expected_suffix);
}

INSTANTIATE_TEST_SUITE_P(
    All, ConsumeValidInputTest,
    ::testing::ValuesIn(std::vector<ConsumeTestCase>{
        // Unadorned positive numbers
        {"0", 0},
        {"0.0", 0},
        {"0e6", 0},
        {"1", 1},
        {"1.00", 1},
        {"17.32", 17.32},
        {"1732", 1732},
        {"1732987", 1732987},
        {"1e6", 1e6},
        {"1E6", 1e6},
        {"1e-6", 1e-6},
        {"1e300", 1e300},
        {"1.23e10", 1.23e10},

        // Unadorned negative numbers
        {"-0", -0},
        {"-0.0", -0},
        {"-0e6", -0},
        {"-1", -1},
        {"-1.00", -1},
        {"-17.32", -17.32},
        {"-1732", -1732},
        {"-1732987", -1732987},
        {"-1e6", -1e6},
        {"-1E6", -1e6},
        {"-1e-6", -1e-6},
        {"-1e300", -1e300},
        {"-1.23e10", -1.23e10},

        // Unadorned floating point special cases
        {"inf", std::numeric_limits<double>::infinity()},
        {"-inf", -std::numeric_limits<double>::infinity()},
        {"nan", std::numeric_limits<double>::quiet_NaN()},

        // With positive SI prefix
        {"0k", 0, 1},
        {"17M", 17, 2},
        {"1.2e9G", 1.2e9, 3},
        {"0.3T", 0.3, 4},
        {"9P", 9, 5},
        {"-3E", -3, 6},
        {"-3.2Z", -3.2, 7},
        {"-0Y", 0, 8},

        // With negative SI prefix
        {"0m", 0, -1},
        {"17µ", 17, -2},
        {"17μ", 17, -2},
        {"1.2e9n", 1.2e9, -3},
        {"0.3p", 0.3, -4},
        {"9f", 9, -5},
        {"-3a", -3, -6},
        {"-3.2z", -3.2, -7},
        {"-0y", 0, -8},

        // With trailing material
        {"17g", 17, 0, false, "g"},
        {"17µm", 17, -2, false, "m"},
        {"17μm", 17, -2, false, "m"},
        {"17µm/s", 17, -2, false, "m/s"},
        {"17μm/s", 17, -2, false, "m/s"},
        {"2.4e6l/s", 2.4e6, 0, false, "l/s"},
        {"2.3ns per iteration", 2.3, -3, false, "s per iteration"},
        {"17i", 17, 0, false, "i"},

        // Positive IEC prefixes
        {"0Ki", 0, 1, true},
        {"17Mi", 17, 2, true},
        {"1.2e9Gi", 1.2e9, 3, true},
        {"0.3Ti", 0.3, 4, true},
        {"9PiB", 9, 5, true, "B"},
        {"-3Ei", -3, 6, true},
        {"-3.2Zi", -3.2, 7, true},
        {"-0YiB", 0, 8, true, "B"},

        // Incorrect but supported negative base 1024 prefixes
        {"0mi", 0, -1, true},
        {"17µi", 17, -2, true},
        {"17μi", 17, -2, true},
        {"17µiB", 17, -2, true, "B"},
        {"17μiB", 17, -2, true, "B"},
        {"1.2e9ni", 1.2e9, -3, true},
        {"0.3pi", 0.3, -4, true},
        {"9fiB", 9, -5, true, "B"},
        {"-3ai", -3, -6, true},
        {"-3.2zi", -3.2, -7, true},
        {"-0yiB", 0, -8, true, "B"},

        // Incorrect but supported micro symbol
        {"17u", 17, -2, false, ""},
        {"17um", 17, -2, false, "m"},
        {"17uiB", 17, -2, true, "B"},
        {"17uiB/s", 17, -2, true, "B/s"},

        // Incorrect but supported "K" base 1000 prefix
        {"17K", 17, 1, false, ""},
        {"1.7KB", 1.7, 1, false, "B"},

        // Incorrect but supported "k" base 1024 prefix
        {"17ki", 17, 1, true, ""},
        {"1.7kiB", 1.7, 1, true, "B"},
    }),
    StructParamName<ConsumeTestCase>);

class ParseInvalidInputTest : public ::testing::TestWithParam<std::string> {};

TEST_P(ParseInvalidInputTest, InvalidInput) {
  EXPECT_THAT(Parse(GetParam()), Eq(std::nullopt));
}

INSTANTIATE_TEST_SUITE_P(All, ParseInvalidInputTest,
                         ::testing::ValuesIn(std::vector<std::string>{
                             // Junk
                             "",
                             "-",
                             "+",
                             "+1",
                             "+1,",
                             std::string{1, '\0'},
                             "a",
                             "foobarbaz",
                             "x17",
                             "x17M",
                             "x17MB",
                             "x17MiB",
                             "aMiB",
                             "-aMiB",

                             // Valid, but with trailing contents
                             "0g",
                             "17x",
                             "3mx",
                             "17i",
                         }),
                         StringParamName);

struct ParseTestCase {
  std::string input;

  double expected_mantissa;
  int expected_exponent = 0;
  bool expected_iec = false;
};

void PrintTo(const ParseTestCase& tc, std::ostream* os) {
  *os << "{input: \"" << tc.input
      << "\", expected_mantissa: " << tc.expected_mantissa
      << ", expected_exponent: " << tc.expected_exponent
      << ", expected_iec: " << (tc.expected_iec ? "true" : "false") << "}";
}

class ParseValidInputTest : public ::testing::TestWithParam<ParseTestCase> {};

TEST_P(ParseValidInputTest, ValidInput) {
  const ParseTestCase& tc = GetParam();
  const std::optional<ParseResult> result = Parse(tc.input);
  ASSERT_TRUE(result.has_value());

  if (std::isnan(tc.expected_mantissa)) {
    EXPECT_TRUE(std::isnan(result->mantissa)) << DUMP_VARS(result->mantissa);
  } else {
    EXPECT_EQ(result->mantissa, tc.expected_mantissa);
  }

  EXPECT_EQ(result->exponent, tc.expected_exponent);
  EXPECT_EQ(result->iec, tc.expected_iec);
  EXPECT_EQ(result->remaining, "");
}

INSTANTIATE_TEST_SUITE_P(
    All, ParseValidInputTest,
    ::testing::ValuesIn(std::vector<ParseTestCase>{
        // Unadorned positive numbers
        {"0", 0},
        {"0.0", 0},
        {"0e6", 0},
        {"1", 1},
        {"1.00", 1},
        {"17.32", 17.32},
        {"1732", 1732},
        {"1732987", 1732987},
        {"1e6", 1e6},
        {"1E6", 1e6},
        {"1e-6", 1e-6},
        {"1e300", 1e300},
        {"1.23e10", 1.23e10},

        // Unadorned negative numbers
        {"-0", -0},
        {"-0.0", -0},
        {"-0e6", -0},
        {"-1", -1},
        {"-1.00", -1},
        {"-17.32", -17.32},
        {"-1732", -1732},
        {"-1732987", -1732987},
        {"-1e6", -1e6},
        {"-1E6", -1e6},
        {"-1e-6", -1e-6},
        {"-1e300", -1e300},
        {"-1.23e10", -1.23e10},

        // Unadorned floating point special cases
        {"inf", std::numeric_limits<double>::infinity()},
        {"-inf", -std::numeric_limits<double>::infinity()},
        {"nan", std::numeric_limits<double>::quiet_NaN()},

        // With positive SI prefix
        {"0k", 0, 1},
        {"17M", 17, 2},
        {"1.2e9G", 1.2e9, 3},
        {"0.3T", 0.3, 4},
        {"9P", 9, 5},
        {"-3E", -3, 6},
        {"-3.2Z", -3.2, 7},
        {"-0Y", 0, 8},

        // With negative SI prefix
        {"0m", 0, -1},
        {"17µ", 17, -2},
        {"17μ", 17, -2},
        {"1.2e9n", 1.2e9, -3},
        {"0.3p", 0.3, -4},
        {"9f", 9, -5},
        {"-3a", -3, -6},
        {"-3.2z", -3.2, -7},
        {"-0y", 0, -8},

        // Positive IEC prefixes
        {"0Ki", 0, 1, true},
        {"17Mi", 17, 2, true},
        {"1.2e9Gi", 1.2e9, 3, true},
        {"0.3Ti", 0.3, 4, true},
        {"9Pi", 9, 5, true},
        {"-3Ei", -3, 6, true},
        {"-3.2Zi", -3.2, 7, true},
        {"-0Yi", 0, 8, true},

        // Incorrect but supported negative base 1024 prefixes
        {"0mi", 0, -1, true},
        {"17µi", 17, -2, true},
        {"17μi", 17, -2, true},
        {"1.2e9ni", 1.2e9, -3, true},
        {"0.3pi", 0.3, -4, true},
        {"9fi", 9, -5, true},
        {"-3ai", -3, -6, true},
        {"-3.2zi", -3.2, -7, true},
        {"-0yi", 0, -8, true},

        // Incorrect but supported micro symbol
        {"17u", 17, -2, false},
        {"17ui", 17, -2, true},

        // Incorrect but supported "K" base 1000 prefix
        {"17K", 17, 1, false},

        // Incorrect but supported "k" base 1024 prefix
        {"17ki", 17, 1, true},
    }),
    StructParamName<ParseTestCase>);

TEST(ParseDecimalDouble, BasicCases) {
  const char* suffix;
  EXPECT_EQ(ParseDecimalDouble("83.2MB", &suffix), 83200000);
  EXPECT_STREQ(suffix, "B");

  EXPECT_EQ(ParseDecimalDouble("-83.2u", &suffix), -8.32e-5);
  EXPECT_STREQ(suffix, "");

  // ParseDecimalDouble ignores IEC suffixes.
  EXPECT_EQ(ParseDecimalDouble("83.2MiB", &suffix), 83200000);
  EXPECT_STREQ(suffix, "iB");
}

TEST(ParseDecimalDouble, OptionalCases) {
  std::string suffix;
  EXPECT_THAT(ParseOptionalDecimalDouble("83.2MB", &suffix),
              Optional(Eq(83200000)));
  EXPECT_THAT(suffix, Eq("B"));

  EXPECT_THAT(ParseOptionalDecimalDouble("-83.2u", &suffix),
              Optional(Eq(-8.32e-5)));
  EXPECT_THAT(suffix, Eq(""));

  // ParseDecimalDouble ignores IEC suffixes.
  EXPECT_THAT(ParseOptionalDecimalDouble("83.2MiB", &suffix),
              Optional(Eq(83200000)));
  EXPECT_THAT(suffix, Eq("iB"));
}

TEST(ParseDecimalDouble, OptionalBadCases) {
  EXPECT_THAT(ParseOptionalDecimalDouble("80M", nullptr), Eq(std::nullopt));

  std::string suffix;
  EXPECT_THAT(ParseOptionalDecimalDouble("x0MB", &suffix), Eq(std::nullopt));
  EXPECT_THAT(suffix, Eq(""));
}

TEST(ParseBinaryDouble, BasicCases) {
  const char* suffix;
  EXPECT_EQ(ParseBinaryDouble("83.2M", &suffix), 83.2 * 1024 * 1024);
  EXPECT_STREQ(suffix, "");

  EXPECT_EQ(ParseBinaryDouble("83.2Mi", &suffix), 83.2 * 1024 * 1024);
  EXPECT_STREQ(suffix, "");

  EXPECT_EQ(ParseBinaryDouble("83.2u", &suffix), 83.2 / (1024 * 1024));
  EXPECT_STREQ(suffix, "");
}

TEST(ParseBinaryDouble, OptionalCases) {
  std::string suffix;
  EXPECT_THAT(ParseOptionalBinaryDouble("83.2MO", &suffix),
              Optional(Eq(83.2 * 1024 * 1024)));
  EXPECT_THAT(suffix, Eq("O"));

  EXPECT_THAT(ParseOptionalBinaryDouble("83.2Mi", &suffix),
              Optional(Eq(83.2 * 1024 * 1024)));
  EXPECT_THAT(suffix, Eq(""));

  EXPECT_THAT(ParseOptionalBinaryDouble("83.2u", &suffix),
              Optional(Eq(83.2 / (1024 * 1024))));
  EXPECT_THAT(suffix, Eq(""));
}

TEST(ParseBinaryDouble, OptionalBadCases) {
  EXPECT_THAT(ParseOptionalBinaryDouble("80M", nullptr), Eq(std::nullopt));

  std::string suffix;
  EXPECT_THAT(ParseOptionalBinaryDouble("x0MB", &suffix), Eq(std::nullopt));
  EXPECT_THAT(suffix, Eq(""));
}

TEST(ParseDouble, BasicCases) {
  const char* suffix;
  EXPECT_EQ(ParseDouble("83.2MB", &suffix), 83.2 * 1000 * 1000);
  EXPECT_STREQ(suffix, "B");

  EXPECT_EQ(ParseDouble("83.2MiB", &suffix), 83.2 * 1024 * 1024);
  EXPECT_STREQ(suffix, "B");
}

TEST(ParseDouble, OptionalCases) {
  std::string suffix;
  EXPECT_THAT(ParseOptionalDouble("83.2MB", &suffix),
              Optional(Eq(83.2 * 1000 * 1000)));
  EXPECT_THAT(suffix, Eq("B"));

  EXPECT_THAT(ParseOptionalDouble("83.2MiB", &suffix),
              Optional(Eq(83.2 * 1024 * 1024)));
  EXPECT_THAT(suffix, Eq("B"));
}

TEST(ParseDouble, OptionalBadCases) {
  EXPECT_THAT(ParseOptionalDouble("80M", nullptr), Eq(std::nullopt));

  std::string suffix;
  EXPECT_THAT(ParseOptionalDouble("x0MB", &suffix), Eq(std::nullopt));
  EXPECT_THAT(suffix, Eq(""));
}

TEST(LessThan, Basic) {
  EXPECT_TRUE(LessThan("10", "11"));
  EXPECT_TRUE(LessThan("999", "1k"));
  EXPECT_TRUE(LessThan("1k", "1050"));
  EXPECT_TRUE(LessThan("1k", "1Ki"));
  EXPECT_TRUE(LessThan("-10k", "1k"));
  EXPECT_TRUE(LessThan("1.2m", "0.1"));
  EXPECT_TRUE(LessThan("1A", "1B"));             // Lexicographical by suffix
  EXPECT_TRUE(LessThan("1.024kA", "1.000kiB"));  // Lexicographical by suffix
}

static void BM_ToDecimalString_avg(benchmark::State& state) {
  for (auto s : state) {
    for (double i = 1e-30; i < 1e30; i *= 10.) {
      benchmark::DoNotOptimize(ToDecimalString(i));
    }
  }
}

static void BM_ToDecimalString_kilo(benchmark::State& state) {
  for (auto s : state) {
    for (double i = 1e3; i < 1e6; i += 1e4) {
      benchmark::DoNotOptimize(ToDecimalString(i));
    }
  }
}

static void BM_ToDecimalString_peta(benchmark::State& state) {
  for (auto s : state) {
    for (double i = 1e24; i < 1e27; i += 1e25) {
      benchmark::DoNotOptimize(ToDecimalString(i));
    }
  }
}

BENCHMARK(BM_ToDecimalString_avg);
BENCHMARK(BM_ToDecimalString_kilo);
BENCHMARK(BM_ToDecimalString_peta);

}  // namespace
}  // namespace si_prefix
}  // namespace strings
