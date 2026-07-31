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

#include "gloop/strings/numberformat.h"

#include <stdio.h>

#include <cstdint>
#include <string>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "gloop/util/math/fastmath.h"

// Calculate base^exponent in log(exponent) time.
template <typename T>
static T FastPower(T base, int exponent) {
  T res = 1;
  if (exponent != 0) {
    T mult = base;
    while (exponent != 1) {
      if ((exponent & 1) != 0) res *= mult;
      exponent >>= 1;
      mult *= mult;
    }
    res *= mult;
  }
  return res;
} /* FastPower */

std::string strings::UnitConvert(int64_t val_signed, const int64_t unit_arr[],
                                 const int64_t base, const char* suff_arr[],
                                 const int length, const double thresh,
                                 const uint32_t precision) {
  // The floating point calculations below can't handle a precision
  // greater than 37.
  CHECK_LE(precision, 37);

  // Special case zero so that we get "0B" instead of "0TB"
  if (val_signed == 0) {
    return absl::StrFormat("0%s", suff_arr[length - 1]);
  }

  const char* prefix = "";
  uint64_t val = val_signed;

  if (val_signed < 0) {
    prefix = "-";
    val = -val;
  }

  // If thresh is less than the smallest number that we can represent
  // given 'precision', bump thresh up.  Handles case where thresh == 0.0.
  double adjusted_thresh = thresh;
  const double min_precision = 1.0 / vfpowd(10.0, precision);

  if (min_precision > thresh) {
    adjusted_thresh = min_precision;
  }

  for (int j = 0; j < length; j++) {
    const int64_t vj = unit_arr[j];
    const int64_t unit = (base == 0)   ? vj
                         : (base == 2) ? (1LL << vj)
                                       : FastPower(base, vj);
    const char* suff = suff_arr[j];  // E.g. "M"
    if (val % unit == 0) {
      return absl::StrFormat("%s%u%s", prefix, val / unit, suff);
    } else if (val >= adjusted_thresh * unit) {
      const double coeff = (static_cast<double>(val)) / unit;
      return absl::StrFormat("%s%.*f%s", prefix, static_cast<int>(precision),
                             coeff, suff);
    }
  }
  LOG(ERROR) << " strings::UnitConvert(" << val << ") failed to format\n";
  return absl::StrFormat("%s%u", prefix, val);
} /* strings::UnitConvert */

std::string strings::BinaryEng(int64_t val, double thresh, uint32_t precision) {
  static const int64_t kSHIFT[] = {60, 50, 40, 30, 20, 10, 0};
  static const char* kSUFF[] = {"E", "P", "T", "G", "M", "K", ""};

  return UnitConvert(val, kSHIFT, 2, kSUFF, sizeof(kSHIFT) / sizeof(kSHIFT[0]),
                     thresh, precision);
} /* strings::BinaryEng */

std::string strings::DecimalEng(int64_t val, double thresh,
                                uint32_t precision) {
  static const int64_t kEXP[] = {18, 15, 12, 9, 6, 3, 0};
  static const char* kSUFF[] = {"E", "P", "T", "G", "M", "K", ""};

  return UnitConvert(val, kEXP, 10, kSUFF, sizeof(kEXP) / sizeof(kEXP[0]),
                     thresh, precision);
} /* strings::DecimalEng */

template <typename T>
static T ParseSuffixedNum(absl::string_view str, const T defValue,
                          double one_k) {
  double d = static_cast<double>(defValue);  // in case str is ""
  char suff;
  int n = sscanf(std::string(str).c_str(), "%lf%1c", &d, &suff);
  if (n == 0) {
    return defValue;
  } else if (n == 1) {
    // do nothing
  } else if (n >= 2) {
    if (suff > 'a') {
      suff -= ('a' - 'A');  // convert to uppercase
    }
    switch (suff) {
      case 'K':
        d = d * one_k;
        break;
      case 'M':
        d = d * one_k * one_k;
        break;
      case 'G':
        d = d * one_k * one_k * one_k;
        break;
      case 'T':
        d = d * one_k * one_k * one_k * one_k;
        break;
      case 'P':
        d = d * one_k * one_k * one_k * one_k * one_k;
        break;
      default:
        // ignore the suffix
        break;
    }
  }
  return static_cast<T>(d);
}

/* static */
double strings::ParseSuffixedDouble(absl::string_view str, double defValue) {
  return ::ParseSuffixedNum(str, defValue, 1024);
}

double strings::ParseDecimalSuffixedDouble(absl::string_view str,
                                           double defValue) {
  return ::ParseSuffixedNum(str, defValue, 1000);
}
