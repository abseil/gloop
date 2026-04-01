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

#ifndef THIRD_PARTY_GLOOP_STRINGS_SI_PREFIX_H_
#define THIRD_PARTY_GLOOP_STRINGS_SI_PREFIX_H_

#include <optional>
#include <string>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

//                              IMPORTANT WARNING
// This file supports both decimal (1k=1000) and binary (1k=1024) prefixes.
// For reasons of backwards-compatibility, the same set of prefixes are often
// used for both; this can be a recipe for confusion, so unless it's clear from
// context, be careful. IEC units for binary powers have also been defined (see
// the functions below) which use distinct names for binary and decimal powers,
// so if you see those, you know it's binary, but if you see an ordinary
// prefix, don't make assumptions.
//
// _Memory_ amounts are always expressed in binary units.
// _Disk_ amounts are expressed in binary units by everyone except for hard
// disk manufacturers, who inconsistently use binary, decimal, or "other"
// units. (e.g., 1M = 1024000) Within Google code, always use binary units for
// this and all other storage quantities, unless there's a really Good Reason.
// _Network-related parameters_, such as bandwidths, are ALWAYS reported using
// decimal. Do not use binary prefixes for these on pain of Bad Things
// happening.
// _Physical_ parameters, such as clock speeds, are always reported using
// decimal.
//        See: <link>

namespace strings {
namespace si_prefix {

/////////////////////////////////////////////////////////////////////////////
// Printing functions

// In the functions below, 'threshold' states how many multiples of a unit
// we must have before using that unit; e.g., if threshold = 1.0,
// 1010 will be rendered as 1.01k, but if threshold = 2.0, it will be
// rendered as 1010, because 1010 < (2.0 * 1k). 'precision' is the number of
// decimal places to show, in the sense of printf's precision arguments.

// ToDecimalString renders this number using decimal SI prefixes.
// (1M = 1000 * 1000)
std::string ToDecimalStringFullySpecified(double value, double threshold,
                                          int precision);

inline std::string ToDecimalString(double value) {
  return ToDecimalStringFullySpecified(value, 1.0, 2);
}

// ToBinaryString renders this number using binary math but SI prefixes.
// (eg 1M = 1024 * 1024)
// Never use this in new code, it causes serious confusion. (see top comment)
// Use the toIEC[...] variants if you want to use the "binary" units.
std::string ToBinaryStringFullySpecified(double value, double threshold,
                                         int precision);

inline std::string ToBinaryString(double value) {
  return ToBinaryStringFullySpecified(value, 1.0, 2);
}

// ToIECBinaryString renders this number using IEC 60027-2 A.2 binary
// prefixes. (1Mi = 1024 * 1024)
std::string ToIECBinaryStringFullySpecified(double value, double threshold,
                                            int precision);

inline std::string ToIECBinaryString(double value) {
  return ToIECBinaryStringFullySpecified(value, 1.0, 2);
}

// Lower-level printing functions

enum class ExponentBase {
  // k = 10^3, M = 10^6, etc.
  k1000 = 1000,

  // k = 2^10, M = 2^20, etc.
  k1024 = 1024,
};

// Convert a value into its mantissa and exponent, expressing the mantissa
// as a string, and the exponent as an index into the SI units. (k = 1,
// M = 2, u = -2, etc.)
//
// REQUIRES: 0.0 < threshold
void ToExponentAndMantissa(double value, double threshold, int precision,
                           ExponentBase exponent_base, std::string* mantissa,
                           int* exponent);

// Convert an integer exponent index (k = 1, etc) to a string prefix, either
// SI or IEC depending on the flag.
std::string ExponentToPrefix(int exponent, bool iec);

///////////////////////////////////////////////////////////////////////////
// Parsing functions which reverse the above printers.

struct ParseResult {
  double mantissa;
  int exponent;

  // Was the unit prefix parsed an IEC binary prefix (Ki, Mi, Gi, etc.)?
  bool iec;

  // In the case of Consume: the remaining portion of the string.
  absl::string_view remaining;
};

// Parse the supplied string as an SI/IEC quantity, returning nullopt if the
// input was invalid.
std::optional<ParseResult> Parse(absl::string_view str);

// Like Parse, but instead consume a leading SI/IEC quantity from the input,
// setting ParseResult::remaining to the trailing material on success.
std::optional<ParseResult> Consume(absl::string_view str);

// Error handling for the `const char** suffix` functions below is in the same
// manner as strtod: if *suffix == str, nothing was parsed, and the resulting
// parsed number will be zero. If **suffix == \0, all of str was consumed. The
// suffix parameter is allowed to be null, but in this case errors cannot be
// detected.

// Parse a string into a number, assuming that it is decimal. Suffix is
// filled in as by Parse.
double ParseDecimalDouble(const char* str, const char** suffix);

// Parse a string into a number, assuming that it is binary.
double ParseBinaryDouble(const char* str, const char** suffix);

// Parse a string into a number, assuming that SI prefixes are decimal and
// IEC prefixes are binary.
double ParseDouble(const char* str, const char** suffix);

// These are as above, but with a few more ergonomics:  The double is wrapped in
// an optional value, returning `nullopt` on errors, and copies the suffix
// into the provided string.  This returns `nullopt` if the suffix string is
// `nullopt`.
std::optional<double> ParseOptionalDecimalDouble(absl::string_view str,
                                                 std::string* suffix);
std::optional<double> ParseOptionalBinaryDouble(absl::string_view str,
                                                std::string* suffix);
std::optional<double> ParseOptionalDouble(absl::string_view str,
                                          std::string* suffix);

// Convert an exponent (k = 1, etc) to the appropriate multiplier. (Either a
// power of 1000 or of 1024, depending on whether binary is true)
double ExponentToMultiplier(int exponent, bool binary);

////////////////////////////////////////////////////////////////////////////
// Sorting function for strings with SI prefixes.
bool LessThan(const std::string& a, const std::string& b);

};  // namespace si_prefix
};  // namespace strings

#endif  // THIRD_PARTY_GLOOP_STRINGS_SI_PREFIX_H_
