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

// Convert strings to/from integral values in human or engineering format
// If you need SI or IEC prefixes, consider strings/si_prefix.h instead.
//
// 1) NumberFormat: num to string, e.g. 12345 => "12.35K"
// string s = strings::DecimalEng(12345);  // "12.35K"
//
// 2) ParseSuffixedFoo(): string to num, "12.3K" => 12.3 * 1024
// double i = strings::ParseSuffixedDouble("12.3K", 1.0);  // 1.0 as default
// int64 j = strings::ParseSuffixedInt64("bad", 1);  // 1 as default

#ifndef THIRD_PARTY_GLOOP_STRINGS_NUMBERFORMAT_H__
#define THIRD_PARTY_GLOOP_STRINGS_NUMBERFORMAT_H__

#include <cstdint>
#include <limits>
#include <string>

#include "absl/strings/string_view.h"

namespace strings {

// Convert VAL to a human readable string, using the units K, M, G, etc.
// Note: Units are powers of ten, thus 1M == 1,000,000, not 2^20

// THRESH indicates many multiples of unit we must have to use that unit
// E.g.    THRESH=5   4321 => "4321", as 4321 < 5K, but 5432 => "5.4K"
//         THRESH=2   4321 => "4.3K", as 4321 >= 2K,
// If however VAL is an exact multiple of (non-zero) UNITs, we use UNIT.
// E.g.    1000 => "1K"    always, independent of THRESH
//
// PRECISION is how many digits to show right of the decimal point.
// E.g.    PRECISION=1  4321 => "4.3K"
//         PRECISION=3  4321 => "4.321K"
//
// A PRECISION of greater than 10 is not guaranteed to work properly.
//   (due to round off errors and limited accuracy of internals.)
// Require that PRECISION >= 0.
//
// If val < 0, the returned string is
//     "-" + DecimalEng(-val, thresh, prec);
std::string DecimalEng(int64_t val, double thresh, uint32_t precision);

// convenience, thresh=1, precision=2
inline std::string DecimalEng(int64_t val) { return DecimalEng(val, 1, 2); }

// Convert VAL to a string, using the units K=2^10, M=2^20, etc.
// Note: Units are powers of two, thus 1M == 2^20 not 10^6.
//
// THRESH and PRECISION have the same meaning as in DecimalEng
//
std::string BinaryEng(int64_t val, double thresh, uint32_t precision = 1);

// convenience function in which thresh=1, precision=2
inline std::string BinaryEng(int64_t val) { return BinaryEng(val, 1, 2); }

// The underlying base conversion routine, which does all the work
// From a *decreasing* array of "units" and pick the largest appropriate one
// E.g. for std eng notation
// unit[2] = 1,000,000    unit[1] = 1000    unit[0] = 0
//
// @param val      int value to be converted
// @param unit_arr *DECREASING* array of units to convert to
//                 the last entry should result in a 1.
//                 Thus, if base = 0, the last entry is 1
//                       if base != 0, the last entry is a 0 (base^0 => 1)
// @param base     0     => use unit_arr as is
//                 else  => unit[j] = base^unit_arr[j]
// @param suff_arr append suffix[j] if we use unit[j],
//                 the last entry should be ""
// @param length   # elements in unit_arr and suff_arr
// @param thresh   require (val >= thresh*unit[j]) to use unit[j]
// @param precision  # of digits past the decimal point we use.
//
// Thus, if val was seconds we could convert to min, hr, days, year via:
//   unit_arr={365*24*3600, 24*3600, 3600, 60, 1}
//   suff_arr={       "yr",   "day", "hr", "min", "" };
std::string UnitConvert(int64_t val, const int64_t unit_arr[], int64_t base,
                        const char* suff_arr[], int length, double thresh,
                        uint32_t precision);

// Convert a string containing a suffixed number into a numeric value.
//
// E.g.
//   double dval = ParseSuffixedDouble("1.23456K", 0.0);
//   int64 lval = ParseSuffixedInt64("1.23456K", 0);
//
// Warning: ParseSuffixedInt64 is implemented by producing a double
// value and converting it to an integer. It could cause a rounding
// error if you parse a string with too many significant digits. If
// need an exact int64 value, use strtoll(3) instead.
//
// @return
//   the converted value, or
//   defValue, if there is a problem parsing the number
//
// @param str       the value to be converted, e.g. "-18K" or "2.71828M"
// @param defValue  default value returned if the string cannot be converted
//
double ParseSuffixedDouble(absl::string_view str, double defValue);

inline int64_t ParseSuffixedInt64(absl::string_view str, int64_t defValue) {
  double d = ParseSuffixedDouble(str, static_cast<double>(defValue));
  double min_double = static_cast<double>(std::numeric_limits<int64_t>::min());
  double max_double = -min_double;
  if (d >= min_double && d < max_double) {
    return static_cast<int64_t>(d);
  }
  if (d == max_double) {
    return std::numeric_limits<int64_t>::max();
  }
  return defValue;
}

// As ParseSuffixed{Double,Int64} but working with power-of-ten units
double ParseDecimalSuffixedDouble(absl::string_view str, double defValue);

inline int64_t ParseDecimalSuffixedInt64(absl::string_view str,
                                         int64_t defValue) {
  double d = ParseDecimalSuffixedDouble(str, static_cast<double>(defValue));
  double min_double = static_cast<double>(std::numeric_limits<int64_t>::min());
  double max_double = -min_double;
  if (d >= min_double && d < max_double) {
    return static_cast<int64_t>(d);
  }
  if (d == max_double) {
    return std::numeric_limits<int64_t>::max();
  }
  return defValue;
}

// Also, internally we use a template function, ParseSuffixedNum,
// allowing for different return types.
// But to simplify compilation, this is hidden.

}  // namespace strings

#endif  // THIRD_PARTY_GLOOP_STRINGS_NUMBERFORMAT_H__
