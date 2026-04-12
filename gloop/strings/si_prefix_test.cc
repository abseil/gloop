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

#include <cmath>
#include <limits>
#include <optional>
#include <string>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "gloop/gloop_test.h"

namespace strings {
namespace si_prefix {

using ::testing::Eq;
using ::testing::Optional;

TEST(ToExponentAndMantissa, ExponentBase_1000) {
  struct TestCase {
    double value;

    std::string expected_mantissa;
    int expected_exponent;
  };

  const TestCase test_cases[] = {
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
  };

  for (const TestCase& tc : test_cases) {
    std::string mantissa;
    int exponent;
    ToExponentAndMantissa(tc.value, 1.0, 2, ExponentBase::k1000, &mantissa,
                          &exponent);

    EXPECT_EQ(tc.expected_mantissa, mantissa);
    EXPECT_EQ(tc.expected_exponent, exponent);
  }
}

TEST(ToExponentAndMantissa, ExponentBase_1024) {
  struct TestCase {
    double value;

    std::string expected_mantissa;
    int expected_exponent;
  };

  const TestCase test_cases[] = {
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
  };

  for (const TestCase& tc : test_cases) {
    std::string mantissa;
    int exponent;
    ToExponentAndMantissa(tc.value, 1.0, 2, ExponentBase::k1024, &mantissa,
                          &exponent);

    EXPECT_EQ(tc.expected_mantissa, mantissa);
    EXPECT_EQ(tc.expected_exponent, exponent);
  }
}

TEST(ToExponentAndMantissa, Threshold) {
  constexpr auto k1000 = ExponentBase::k1000;

  std::string mantissa;
  int exponent;

  // With a threshold of 1.0, anything at or above a power of 1k should roll
  // over the exponent.
  ToExponentAndMantissa(999, 1.0, 3, k1000, &mantissa, &exponent);
  EXPECT_EQ("999.000", mantissa);
  EXPECT_EQ(0, exponent);

  ToExponentAndMantissa(1000, 1.0, 3, k1000, &mantissa, &exponent);
  EXPECT_EQ("1.000", mantissa);
  EXPECT_EQ(1, exponent);

  ToExponentAndMantissa(1001, 1.0, 3, k1000, &mantissa, &exponent);
  EXPECT_EQ("1.001", mantissa);
  EXPECT_EQ(1, exponent);

  // But with a threshold of 2.0 this roll-over should happen higher.
  ToExponentAndMantissa(1999, 2.0, 3, k1000, &mantissa, &exponent);
  EXPECT_EQ("1999.000", mantissa);
  EXPECT_EQ(0, exponent);

  ToExponentAndMantissa(2000, 2.0, 3, k1000, &mantissa, &exponent);
  EXPECT_EQ("2.000", mantissa);
  EXPECT_EQ(1, exponent);

  ToExponentAndMantissa(2001, 2.0, 3, k1000, &mantissa, &exponent);
  EXPECT_EQ("2.001", mantissa);
  EXPECT_EQ(1, exponent);
}

TEST(LowLevel, SIExponentToPrefix) {
  EXPECT_EQ("Y", ExponentToPrefix(8, false));
  EXPECT_EQ("Z", ExponentToPrefix(7, false));
  EXPECT_EQ("E", ExponentToPrefix(6, false));
  EXPECT_EQ("P", ExponentToPrefix(5, false));
  EXPECT_EQ("T", ExponentToPrefix(4, false));
  EXPECT_EQ("G", ExponentToPrefix(3, false));
  EXPECT_EQ("M", ExponentToPrefix(2, false));
  EXPECT_EQ("k", ExponentToPrefix(1, false));
  EXPECT_EQ("m", ExponentToPrefix(-1, false));
  EXPECT_EQ("μ", ExponentToPrefix(-2, false));
  EXPECT_EQ("n", ExponentToPrefix(-3, false));
  EXPECT_EQ("p", ExponentToPrefix(-4, false));
  EXPECT_EQ("f", ExponentToPrefix(-5, false));
  EXPECT_EQ("a", ExponentToPrefix(-6, false));
  EXPECT_EQ("z", ExponentToPrefix(-7, false));
  EXPECT_EQ("y", ExponentToPrefix(-8, false));
}

TEST(LowLevel, IECExponentToPrefix) {
  EXPECT_EQ("Yi", ExponentToPrefix(8, true));
  EXPECT_EQ("Zi", ExponentToPrefix(7, true));
  EXPECT_EQ("Ei", ExponentToPrefix(6, true));
  EXPECT_EQ("Pi", ExponentToPrefix(5, true));
  EXPECT_EQ("Ti", ExponentToPrefix(4, true));
  EXPECT_EQ("Gi", ExponentToPrefix(3, true));
  EXPECT_EQ("Mi", ExponentToPrefix(2, true));
  EXPECT_EQ("Ki", ExponentToPrefix(1, true));
  EXPECT_EQ("mi", ExponentToPrefix(-1, true));
  EXPECT_EQ("μi", ExponentToPrefix(-2, true));
  EXPECT_EQ("ni", ExponentToPrefix(-3, true));
  EXPECT_EQ("pi", ExponentToPrefix(-4, true));
  EXPECT_EQ("fi", ExponentToPrefix(-5, true));
  EXPECT_EQ("ai", ExponentToPrefix(-6, true));
  EXPECT_EQ("zi", ExponentToPrefix(-7, true));
  EXPECT_EQ("yi", ExponentToPrefix(-8, true));
}

TEST(ToString, Decimal) {
  EXPECT_EQ("1.01k", ToDecimalString(1011));
  EXPECT_EQ("1011.00", ToDecimalStringFullySpecified(1011, 2.0, 2));
  EXPECT_EQ("1234000.00k", ToDecimalStringFullySpecified(1234000000., 5000, 2));
  EXPECT_EQ("-876.12M", ToDecimalString(-876123123));
  EXPECT_EQ("54.34n", ToDecimalString(5.434e-8));
  EXPECT_EQ("-9.70m", ToDecimalString(-9.7e-3));
  EXPECT_EQ("1.00m", ToDecimalString(1e-3));
  EXPECT_EQ("1.00m", ToDecimalString(0.00100000000000000000001));
  EXPECT_EQ("1.00m", ToDecimalString(0.0010000000001));
  EXPECT_EQ("1.00m", ToDecimalString(0.00099999999));
  EXPECT_EQ("0.00", ToDecimalString(0));
  EXPECT_EQ("1.00", ToDecimalString(1));
  EXPECT_EQ("2.00", ToDecimalString(2));
  EXPECT_EQ("nan", ToDecimalString(std::numeric_limits<double>::quiet_NaN()));
  EXPECT_EQ("inf", ToDecimalString(std::numeric_limits<double>::infinity()));
  EXPECT_EQ("-inf", ToDecimalString(-std::numeric_limits<double>::infinity()));
  // Largest / smallest prefix tests.
  EXPECT_EQ("999.99Y", ToDecimalString(99999e22));
  EXPECT_EQ("1.00e+27", ToDecimalString(1e27));
  // Eng notation tests (exponent should be factor of three)
  EXPECT_EQ("1.00e+45", ToDecimalString(1e45));
  EXPECT_EQ("10.00e+45", ToDecimalString(1e46));
  EXPECT_EQ("100.00e+45", ToDecimalString(1e47));
  EXPECT_EQ("1.00e-45", ToDecimalString(1e-45));
  EXPECT_EQ("10.00e-45", ToDecimalString(1e-44));
  EXPECT_EQ("100.00e-45", ToDecimalString(1e-43));
  EXPECT_EQ("-10.00e+45", ToDecimalString(-1e46));
  EXPECT_EQ("1.00e+300", ToDecimalString(1e300));
  EXPECT_EQ("1.00y", ToDecimalString(1e-24));
  EXPECT_EQ("1.00e-27", ToDecimalString(1e-27));
}

TEST(ToString, Binary) {
  EXPECT_EQ("1.01k", ToBinaryString(1034));
  EXPECT_EQ("83.59M", ToBinaryString(87654321));
  EXPECT_EQ("129.45μ", ToBinaryString(0.000123456));
}

TEST(ToString, IEC) {
  EXPECT_EQ("1.01Ki", ToIECBinaryString(1034));
  EXPECT_EQ("83.59Mi", ToIECBinaryString(87654321));
  // This next one is actually wrong (No small IEC prefixes)
  EXPECT_EQ("129.45μi", ToIECBinaryString(0.000123456));

  EXPECT_EQ("1.00Ki", ToIECBinaryString(1024));
  EXPECT_EQ("1023.00", ToIECBinaryString(1023));
  EXPECT_EQ("1.00Ti", ToIECBinaryString(pow(2.0, 40.0)));
  EXPECT_EQ("1.00Ei", ToIECBinaryString(pow(2.0, 60.0)));
  EXPECT_EQ("1023.00Pi", ToIECBinaryString(pow(2.0, 60.0) - pow(2.0, 50)));
  EXPECT_EQ("1023.99Pi", ToIECBinaryString(pow(2.0, 60.0) - pow(2.0, 43)));
  // Out of prefix range. These are in a kind of analog to eng notation,
  // where the exponent is always a multiple of 10.
  EXPECT_EQ("1.00*2^90", ToIECBinaryString(pow(2.0, 90.0)));
  EXPECT_EQ("4.00*2^90", ToIECBinaryString(pow(2.0, 92.0)));
  EXPECT_EQ("1.00*2^-90", ToIECBinaryString(pow(2.0, -90.0)));
  EXPECT_EQ("4.00*2^-90", ToIECBinaryString(pow(2.0, -88.0)));
  // _Really_ out of prefix range.
  EXPECT_EQ("1.99*2^-300", ToIECBinaryString(1.99 * pow(2.0, -300.0)));
  EXPECT_EQ("4.36*2^700", ToIECBinaryString(4.36 * pow(2.0, 700.0)));
}

TEST(Consume, InvalidInput) {
  const std::string inputs[] = {
      "",     "-",         "+",   "+1",   "+1m",   std::string{1, '\0'},
      "a",    "foobarbaz", "x17", "x17M", "x17MB", "x17MiB",
      "aMiB", "-aMiB",
  };

  for (const absl::string_view input : inputs) {
    // We expect the call to fail.
    EXPECT_EQ(std::nullopt, Consume(input));
  }
}

TEST(Consume, ValidInput) {
  struct TestCase {
    std::string input;

    double expected_mantissa;
    int expected_exponent;
    bool expected_iec;
    std::string expected_suffix;
  };

  const TestCase test_cases[] = {
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
  };

  for (const TestCase& tc : test_cases) {
    const std::optional<ParseResult> result = Consume(tc.input);
    ASSERT_TRUE(result.has_value());

    if (std::isnan(tc.expected_mantissa)) {
      EXPECT_TRUE(
          std::isnan(result->mantissa));  // NOLINT(whitespace/semicolon)
    } else {
      EXPECT_EQ(tc.expected_mantissa, result->mantissa);
    }

    EXPECT_EQ(tc.expected_exponent, result->exponent);
    EXPECT_EQ(tc.expected_iec, result->iec);
    EXPECT_EQ(tc.expected_suffix, result->remaining);
  }
}

TEST(Parse, InvalidInput) {
  const std::string inputs[] = {
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
      "x17MB"
      "x17MiB",
      "aMiB",
      "-aMiB",

      // Valid, but with trailing contents
      "0g",
      "17x",
      "3mx",
      "17i",
  };

  for (const absl::string_view input : inputs) {
    // Expect Parse to fail.
    EXPECT_EQ(std::nullopt, Parse(input));
  }
}

TEST(Parse, ValidInput) {
  struct TestCase {
    std::string input;

    double expected_mantissa;
    int expected_exponent;
    bool expected_iec;
  };

  const TestCase test_cases[] = {
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
  };

  for (const TestCase& tc : test_cases) {
    const std::optional<ParseResult> result = Parse(tc.input);
    ASSERT_TRUE(result.has_value());

    if (std::isnan(tc.expected_mantissa)) {
      EXPECT_TRUE(
          std::isnan(result->mantissa));  // NOLINT(whitespace/semicolon)
    } else {
      EXPECT_EQ(tc.expected_mantissa, result->mantissa);
    }

    EXPECT_EQ(tc.expected_exponent, result->exponent);
    EXPECT_EQ(tc.expected_iec, result->iec);
    EXPECT_EQ("", result->remaining);
  }
}

TEST(ParseDecimalDouble, BasicCases) {
  const char* suffix;
  EXPECT_EQ(83200000, ParseDecimalDouble("83.2MB", &suffix));
  EXPECT_STREQ("B", suffix);

  EXPECT_EQ(-8.32e-5, ParseDecimalDouble("-83.2u", &suffix));
  EXPECT_STREQ("", suffix);

  // ParseDecimalDouble ignores IEC suffixes.
  EXPECT_EQ(83200000, ParseDecimalDouble("83.2MiB", &suffix));
  EXPECT_STREQ("iB", suffix);
}

TEST(ParseDecimalDouble, OptionalCases) {
  std::string suffix;
  EXPECT_THAT(ParseOptionalDecimalDouble("83.2MB", &suffix),
              Optional(Eq(83200000)));
  EXPECT_THAT("B", Eq(suffix));

  EXPECT_THAT(ParseOptionalDecimalDouble("-83.2u", &suffix),
              Optional(Eq(-8.32e-5)));
  EXPECT_THAT("", Eq(suffix));

  // ParseDecimalDouble ignores IEC suffixes.
  EXPECT_THAT(ParseOptionalDecimalDouble("83.2MiB", &suffix),
              Optional(Eq(83200000)));
  EXPECT_THAT("iB", Eq(suffix));
}

TEST(ParseDecimalDouble, OptionalBadCases) {
  EXPECT_THAT(ParseOptionalDecimalDouble("80M", nullptr), Eq(std::nullopt));

  std::string suffix;
  EXPECT_THAT(ParseOptionalDecimalDouble("x0MB", &suffix), Eq(std::nullopt));
  EXPECT_THAT("", Eq(suffix));
}

TEST(ParseBinaryDouble, BasicCases) {
  const char* suffix;
  EXPECT_EQ(83.2 * 1024 * 1024, ParseBinaryDouble("83.2M", &suffix));
  EXPECT_STREQ("", suffix);

  EXPECT_EQ(83.2 * 1024 * 1024, ParseBinaryDouble("83.2Mi", &suffix));
  EXPECT_STREQ("", suffix);

  EXPECT_EQ(83.2 / (1024 * 1024), ParseBinaryDouble("83.2u", &suffix));
  EXPECT_STREQ("", suffix);
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
  EXPECT_THAT("", Eq(suffix));
}

TEST(ParseDouble, BasicCases) {
  const char* suffix;
  EXPECT_EQ(83.2 * 1000 * 1000, ParseDouble("83.2MB", &suffix));
  EXPECT_STREQ("B", suffix);

  EXPECT_EQ(83.2 * 1024 * 1024, ParseDouble("83.2MiB", &suffix));
  EXPECT_STREQ("B", suffix);
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
  EXPECT_THAT("", Eq(suffix));
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

}  // namespace si_prefix
}  // namespace strings
