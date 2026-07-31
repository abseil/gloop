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

#include "gloop/strings/si_prefix.h"

#include <stdlib.h>
#include <string.h>

#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <system_error>  // NOLINT(build/c++11)

#include "absl/base/attributes.h"
#include "absl/log/check.h"
#include "absl/strings/charconv.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "gloop/util/math/mathutil.h"

namespace strings {
namespace si_prefix {

namespace {

// A note on the μ (micro) character.
// Unicode defines two different code points for this:
// * U+00B5 is the MICRO SIGN.
// * U+03BC is the GREEK SMALL LETTER MU.
// The Greek letter is preferred even when used as a micro sign (see
// http://www.unicode.org/reports/tr25/ section 2.5).
// For parsing, we accept both (and the 'u' character too).
constexpr absl::string_view kMicroSign = "µ";           // U+00B5
constexpr absl::string_view kGreekSmallLetterMu = "μ";  // U+03BC

// The prefixes used by SI for powers of 10^3.
constexpr absl::string_view kSIPositivePrefixes[] = {
    "k",  // 10^3, kilo
    "M",  // 10^6, mega
    "G",  // 10^9, giga
    "T",  // 10^12, tera
    "P",  // 10^15, peta
    "E",  // 10^18, exa
    "Z",  // 10^21, zetta
    "Y",  // 10^24, yotta
};

// The maximum positive exponent we have a defined abbreviation for.
constexpr int kMaxExponent = std::size(kSIPositivePrefixes);

// The prefixes used by SI for powers of 10^-3.
//
// In the case of micro, we list here the preferred representation (see note
// above) for generating output; for parsing we accept both.
constexpr absl::string_view kSINegativePrefixes[] = {
    "m",                  // 10^-3, milli
    kGreekSmallLetterMu,  // 10^-6, micro
    "n",                  // 10^-9, nano
    "p",                  // 10^-12, pico
    "f",                  // 10^-15, femto
    "a",                  // 10^-18, atto
    "z",                  // 10^-21, zepto
    "y",                  // 10^-24, yocto
};

// The minimum negative exponent we have a defined abbreviation for.
constexpr int kMinExponent = -static_cast<int>(std::size(kSINegativePrefixes));

double FastPower(double base, int exponent) {
  double res = 1.0;
  double mult = base;
  // I know the style guide prohibits "unsigned", but this really does need
  // it. (Shifting signed ints around is asking for it. Esp since "-1 >>1"
  // is -1. So this can go into an infinite loop otherwise.)
  unsigned int abs_exponent;
  if (exponent < 0) {
    mult = 1.0 / mult;
    abs_exponent = -exponent;
  } else {
    abs_exponent = exponent;
  }

  while (abs_exponent != 0) {
    if (abs_exponent % 2 == 1) {
      res *= mult;
    }
    mult *= mult;
    abs_exponent >>= 1;
  }
  return res;
}

}  // namespace

static absl::string_view SignStr(const double value) {
  return std::signbit(value) ? "-" : "";
}

// Return a scientific notation version of the supplied value, which is assumed
// to satisfy this equality:
//
//     scaled == |value| / exponent_base^(E_int)
//
static std::string ToScientific(const double value, const int precision,
                                const ExponentBase exponent_base,
                                const int32_t E_int, const double scaled) {
  // Don't attempt anything fancy for infinite or NaN values.
  //
  // We check 'scaled' here rather than 'value' because that's what we use
  // below. If the value is very tiny (for example the smallest denormalized
  // number) then we may have a finite value but an infinite scaled value.
  if (!std::isfinite(scaled)) {
    return absl::StrFormat("%.*e", precision, value);
  }

  // Convert E_int to a larger type so that it's guaranteed to be safe to
  // multiply it by 3 or 10 below.
  const int64_t large_E_int = E_int;

  // Format appropriately based on the exponent base.
  switch (exponent_base) {
    case ExponentBase::k1000:
      return absl::StrFormat("%s%.*fe%+d", SignStr(value), precision, scaled,
                             large_E_int * 3);

    case ExponentBase::k1024:
      return absl::StrFormat("%s%.*f*2^%d", SignStr(value), precision, scaled,
                             large_E_int * 10);
  }
}

// Make the supplied value slightly larger, but not enough to be detectable when
// printing with the supplied precision.
//
// This makes the rounding "go our way" for values that are interesting to
// ten-fingered humans, so that e.g. 1e-3 becomes "1m" rather than "1000.0µ".
static double AdjustValueForRounding(const double value, const int precision) {
  // Multiply by a factor of 1 + 10^-(3+precision+1), which is motivated by the
  // ExponentBase::k1000 case, but is probably close enough for the k1024 case.
  const double result = value * (1 + FastPower(10, -(3 + precision + 1)));

  // If the input value was very large to begin with, we may have overflowed to
  // infinity. Don't adjust the value in this case.
  if (std::isfinite(value) && !std::isfinite(result)) {
    return value;
  }

  return result;
}

void ToExponentAndMantissa(const double value, const double threshold,
                           const int precision,
                           const ExponentBase exponent_base,
                           std::string* const mantissa, int* const exponent) {
  // The implementation below assumes IEC 559 (IEEE 754) semantics.
  static_assert(std::numeric_limits<double>::is_iec559, "");

  // Compute the absolute value of the input, for use throughput below.
  //
  // Note that std::abs is defined for all inputs on IEEE platforms.
  const double abs_value = std::abs(value);

  // Increase the magnitude of the value very slightly in order to make rounding
  // of decimal fractions work the way humans expect. See the comments on
  // AdjustValueForRounding.
  //
  // Because the input wasn't negative and we simply increased its magnitude,
  // the output should not be negative either. Note that the output may still be
  // NaN; that is handled below.
  const double adjusted_value = AdjustValueForRounding(abs_value, precision);
  DCHECK(0.0 <= adjusted_value || std::isnan(adjusted_value));

  // Further divide by the threshold before taking the logarithm below, so that
  // we don't roll over to the next prefix until we've passed the threshold.
  //
  // We require the user to ensure that 0 < threshold, so we will not divide by
  // zero here. The result is the ratio of two numbers that are not negative, so
  // it is also not negative. (As above, it may still be NaN.)
  DCHECK_LT(0.0, threshold);
  const double thresholded_value = adjusted_value / threshold;
  DCHECK(0.0 <= thresholded_value || std::isnan(thresholded_value));

  // Avoid calling std::log2 with ±0.0, which raises FE_DIVBYZERO.
  if (thresholded_value == 0.0) {
    *exponent = 0;
    *mantissa = absl::StrFormat("%s%.*f", SignStr(value), precision, 0.0);
    return;
  }

  // Find (approximately) that number E such that
  //
  //     exponent_base^E == |value / threshold|
  //
  // Once we convert E to an integer below, this will be our index into the
  // array of SI prefixes.
  //
  // std::log2 raises errors only on zero and negative values. But we proved
  // above that thresholded_value is not negative or zero.
  const double E =
      std::log2(thresholded_value) / std::log2(static_cast<int>(exponent_base));

  // Round E toward negative infinity (so that e.g. a value of 0.5 winds up
  // using exponent -1, representing milli) and then safely convert to an
  // integer, clipping out of bounds values and converting NaN to zero.
  //
  // Use int32, which is plenty large enough to represent all finite double
  // values and won't result in truncation when calling ToScientific below.
  const int32_t E_int = MathUtil::SafeCast<int32_t>(std::floor(E));

  // Scale the absolute value by the derived unit we wound up on.
  const double scaled =
      abs_value / FastPower(static_cast<int>(exponent_base), E_int);

  // If the exponent is outside the range of SI prefixes, bail out. Instead
  // return a zero exponent and appropriate scientific notation.
  if (E_int < kMinExponent || E_int > kMaxExponent) {
    *exponent = 0;
    *mantissa = ToScientific(value, precision, exponent_base, E_int, scaled);
    return;
  }

  // Otherwise E_int is the index into our array of prefixes.
  *exponent = E_int;
  *mantissa = absl::StrFormat("%s%.*f", SignStr(value), precision, scaled);
}

std::string ExponentToPrefix(const int exponent, const bool iec) {
  // Zero means no prefix.
  if (exponent == 0) {
    return "";
  }

  // Special case: IEC's kibi prefix is abbreviated with a capital K, as opposed
  // to lower-case k for SI's kilo.
  if (exponent == 1 && iec) {
    return "Ki";
  }

  // If the exponent is out of range, we also use the empty string.
  if (exponent < kMinExponent || exponent > kMaxExponent) {
    return "";
  }

  // Common case: use the prefix from the appropriate array.
  std::string result;
  if (exponent < 0) {
    const absl::string_view si_prefix = kSINegativePrefixes[-exponent - 1];
    result.append(si_prefix.data(), si_prefix.size());
  } else {
    const absl::string_view si_prefix = kSIPositivePrefixes[exponent - 1];
    result.append(si_prefix.data(), si_prefix.size());
  }

  // If this is for an IEC abbreviation, we additionally add "i" (as in KiB,
  // MiB, etc).
  if (iec) {
    result.push_back('i');
  }

  return result;
}

std::string ToDecimalStringFullySpecified(double value, double threshold,
                                          int precision) {
  std::string mantissa;
  int exponent;
  ToExponentAndMantissa(value, threshold, precision, ExponentBase::k1000,
                        &mantissa, &exponent);
  return mantissa + ExponentToPrefix(exponent, false);
}

std::string ToBinaryStringFullySpecified(double value, double threshold,
                                         int precision) {
  std::string mantissa;
  int exponent;
  ToExponentAndMantissa(value, threshold, precision, ExponentBase::k1024,
                        &mantissa, &exponent);
  return mantissa + ExponentToPrefix(exponent, false);
}

std::string ToIECBinaryStringFullySpecified(double value, double threshold,
                                            int precision) {
  std::string mantissa;
  int exponent;
  ToExponentAndMantissa(value, threshold, precision, ExponentBase::k1024,
                        &mantissa, &exponent);
  return mantissa + ExponentToPrefix(exponent, true);
}

std::optional<ParseResult> Parse(const absl::string_view str) {
  // We are successful iff we can consume the entire input.
  const std::optional<ParseResult> result = Consume(str);
  if (result == std::nullopt || !result->remaining.empty()) {
    return std::nullopt;
  }

  return result;
}

// Consume a leading double value.
static bool ABSL_MUST_USE_RESULT ConsumeDouble(absl::string_view* const str,
                                               double* const value) {
  const absl::from_chars_result result =
      absl::from_chars(str->data(), str->data() + str->size(), *value);

  if (result.ec != std::errc()) {
    return false;
  }

  str->remove_prefix(result.ptr - str->data());
  return true;
}

// Consume a prefix of the supplied string as an SI or IEC prefix abbreviation,
// setting *exponent. On failure, return false and leave the string and
// *exponent unmodified.
static ABSL_MUST_USE_RESULT bool ConsumePrefixAbbreviation(
    absl::string_view* const str, int* const exponent) {
  if (str->empty()) {
    return false;
  }

  // Handle multi-byte micro symbols as a special case.
  if (absl::ConsumePrefix(str, kMicroSign) ||
      absl::ConsumePrefix(str, kGreekSmallLetterMu)) {
    *exponent = -2;
    return true;
  }

  // Otherwise inspect the next character.
  switch ((*str)[0]) {
    case 'Y':
      *exponent = 8;
      break;
    case 'Z':
      *exponent = 7;
      break;
    case 'E':
      *exponent = 6;
      break;
    case 'P':
      *exponent = 5;
      break;
    case 'T':
      *exponent = 4;
      break;
    case 'G':
      *exponent = 3;
      break;
    case 'M':
      *exponent = 2;
      break;
    case 'k':
      ABSL_FALLTHROUGH_INTENDED;
    case 'K':
      *exponent = 1;
      break;
    case 'm':
      *exponent = -1;
      break;
    case 'u':
      *exponent = -2;
      break;
    case 'n':
      *exponent = -3;
      break;
    case 'p':
      *exponent = -4;
      break;
    case 'f':
      *exponent = -5;
      break;
    case 'a':
      *exponent = -6;
      break;
    case 'z':
      *exponent = -7;
      break;
    case 'y':
      *exponent = -8;
      break;

    default:
      return false;
  }

  str->remove_prefix(1);
  return true;
}

std::optional<ParseResult> Consume(const absl::string_view str) {
  ParseResult result;
  result.remaining = str;

  // Attempt to consume the mantissa.
  if (!ConsumeDouble(&result.remaining, &result.mantissa)) {
    return std::nullopt;
  }

  // And an optional SI prefix abbreviation, determining the exponent.
  if (!ConsumePrefixAbbreviation(&result.remaining, &result.exponent)) {
    result.exponent = 0;
    result.iec = false;
    return result;
  }

  // At this point we've seen an abbreviation. If it's followed by "i", we
  // treat it as an IEC one.
  result.iec = absl::ConsumePrefix(&result.remaining, "i");

  return result;
}

// A wrapper around the modern Consume function that implements the bonkers
// strtod-like error handling used by some of our older functions.
static void Consume(const char* str, double* mantissa, int* exponent, bool* iec,
                    const char** suffix) {
  // Call through to the modern function.
  const std::optional<ParseResult> result = Consume(str);
  if (!result.has_value()) {
    *mantissa = 0;
    *exponent = 0;
    *iec = false;

    if (suffix != nullptr) {
      *suffix = str;
    }

    return;
  }

  *mantissa = result->mantissa;
  *exponent = result->exponent;
  *iec = result->iec;

  if (suffix != nullptr) {
    *suffix = result->remaining.data();
  }
}

double ExponentToMultiplier(int exponent, bool binary) {
  return FastPower((binary ? 1024. : 1000.), exponent);
}

double ParseDecimalDouble(const char* str, const char** suffix) {
  double mantissa;
  int exponent;
  bool iec;
  Consume(str, &mantissa, &exponent, &iec, suffix);
  // If this is IEC, unconsume the 'i'; we were told explicitly that this is
  // not IEC.
  if (iec && suffix != nullptr) {
    *suffix -= 1;
  }
  return mantissa * FastPower(1000., exponent);
}

double ParseBinaryDouble(const char* str, const char** suffix) {
  double mantissa;
  int exponent;
  bool iec;
  Consume(str, &mantissa, &exponent, &iec, suffix);
  return mantissa * FastPower(1024., exponent);
}

double ParseDouble(const char* str, const char** suffix) {
  double mantissa;
  int exponent;
  bool iec;
  Consume(str, &mantissa, &exponent, &iec, suffix);
  return mantissa * FastPower((iec ? 1024. : 1000.), exponent);
}

std::optional<double> ParseOptionalDecimalDouble(absl::string_view str,
                                                 std::string* suffix) {
  if (suffix == nullptr) return std::nullopt;
  std::optional<ParseResult> opt_result = Consume(str);
  if (!opt_result) return std::nullopt;
  ParseResult& result = *opt_result;
  // Because the user asked this to me parsed as a decimal, we know this must
  // not be IEC.  Therefore, if the parser thinks it's IEC, we must have
  // consumed an 'i'.  Thus, we rewind the remaining string_view one character.
  if (result.iec) {
    result.remaining = absl::string_view(result.remaining.data() - 1,
                                         result.remaining.size() + 1);
  }
  suffix->assign(result.remaining.data(), result.remaining.size());
  return result.mantissa * FastPower(1000., result.exponent);
}

std::optional<double> ParseOptionalBinaryDouble(absl::string_view str,
                                                std::string* suffix) {
  if (suffix == nullptr) return std::nullopt;
  std::optional<ParseResult> opt_result = Consume(str);
  if (!opt_result) return std::nullopt;
  const ParseResult& result = *opt_result;
  suffix->assign(result.remaining.data(), result.remaining.size());
  return result.mantissa * FastPower(1024., result.exponent);
}

std::optional<double> ParseOptionalDouble(absl::string_view str,
                                          std::string* suffix) {
  if (suffix == nullptr) return std::nullopt;
  std::optional<ParseResult> opt_result = Consume(str);
  if (!opt_result) return std::nullopt;
  const ParseResult& result = *opt_result;
  suffix->assign(result.remaining.data(), result.remaining.size());
  return result.mantissa *
         FastPower(result.iec ? 1024 : 1000., result.exponent);
}

bool LessThan(const std::string& a, const std::string& b) {
  const char* end1;
  const double d1 = ParseDouble(a.c_str(), &end1);
  const char* end2;
  const double d2 = ParseDouble(b.c_str(), &end2);

  // Compare them numerically
  if (d1 != d2) return d1 < d2;

  // Otherwise, sort lexicographically by suffix
  return (strcmp(end1, end2) < 0);
}

};  // namespace si_prefix
};  // namespace strings
