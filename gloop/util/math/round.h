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

#ifndef THIRD_PARTY_GLOOP_UTIL_MATH_ROUND_H_
#define THIRD_PARTY_GLOOP_UTIL_MATH_ROUND_H_

#include <cstdint>

#include "gloop/util/bits/bits.h"
#include "gloop/util/math/mathutil.h"

namespace util {
namespace math {

// The round_mod_n and round_to_3_sig_figs functions are factored from
// gws/doc_handler.cc. They are used e.g. to round the total number of
// result digits to 3 significant figures, eliminating unwarranted precision
// in statistics.

// Round the input number up to a multiple of n
inline void round_mod_n(int64_t* number, int n) {
  *number += (n - 1);
  *number -= (*number % n);
}

// Round the input number up to 3 significant figures. Results are undefined if
// 'number' is negative or greater than 10000000000.
inline void round_to_3_sig_figs(int64_t* number) {
  if (*number >= 1000) {
    if (*number < 10000)
      round_mod_n(number, 10);
    else if (*number < 100000)
      round_mod_n(number, 100);
    else if (*number < 1000000)
      round_mod_n(number, 1000);
    else if (*number < 10000000)
      round_mod_n(number, 10000);
    else if (*number < 100000000)
      round_mod_n(number, 100000);
    else if (*number < 1000000000)
      round_mod_n(number, 1000000);
    else
      round_mod_n(number, 10000000);
  }
}

// n / d, rounded up. If d == 0, returns 0.
inline int divide_rounded_up(unsigned int n, unsigned int d) {
  if (n == 0 || d == 0) {
    return 0;
  }
  return (n - 1) / d + 1;
}

// n / d, rounded up. If d == 0, returns 0.
inline uint64_t divide_rounded_up_uint64(uint64_t n, uint64_t d) {
  if (n == 0 || d == 0) {
    return 0;
  }
  return (n - 1) / d + 1;
}

// n / d, rounded to the nearest integer (with half-up). If d == 0, returns 0.
// It is overflow safe.
inline uint64_t divide_rounded_uint64(uint64_t n, uint64_t d) {
  if (d == 0) {
    return 0;
  }
  if ((n % d) >= d - (n % d)) {
    return (n / d) + 1;
  }
  return n / d;
}

// Round the value 'val' to 'n' significant bits. Rounds half up (away from
// zero) when breaking ties. In cases where rounding up would cause overflow,
// it always rounds down (towards zero). Returns 0 if 'n' is <= 0.
// Four functions are provided, each of which operates on a different argument
// type (uint64, int64, uint32, int32). These differ primarily in how they
// handle large values, since each type has its own min/max values which are
// guaranteed not to be overflowed when rounding.
uint64_t RoundToNSignificantBitsUint64(uint64_t val, int n);

int64_t RoundToNSignificantBitsInt64(int64_t val, int n);

uint32_t RoundToNSignificantBitsUint32(uint32_t val, int n);

int32_t RoundToNSignificantBitsInt32(int32_t val, int n);

// Return floor(log10(val)). Results are undefined if val == 0.
// Heavily tuned: 4 instructions on x86.
inline int Log10FloorNonZero(uint64_t val) {
  uint32_t log2 = Bits::Log2FloorNonZero64(val);
  // log base 10 of 2^(index+1), that is, # digits in 2^(index+1), minus 1
  static constexpr char kNumDigitsOf2ToN1[64] = {
      0,  0,  0,  1,  1,  1,  2,  2,  2,  3,   //
      3,  3,  3,  4,  4,  4,  5,  5,  5,  6,   //
      6,  6,  6,  7,  7,  7,  8,  8,  8,  9,   //
      9,  9,  9,  10, 10, 10, 11, 11, 11, 12,  //
      12, 12, 12, 13, 13, 13, 14, 14, 14, 15,  //
      15, 15, 15, 16, 16, 16, 17, 17, 17, 18,  //
      18, 18, 18, 19};
  uint32_t guess = kNumDigitsOf2ToN1[log2];
  return guess - (val < MathUtil::IPow10(guess) ? 1 : 0);
}

// Return floor(log10(val)). Returns -1 if 'val' is 0.
inline int Log10Floor(uint64_t val) {
  return val == 0 ? -1 : Log10FloorNonZero(val);
}

// Round 'val' to 'n' decimal digits. Rounds half up (away from zero) when
// breaking ties. In cases where rounding up would cause overflow, it always
// rounds down. Returns 0 if 'n' is <= 0.
// Examples:
//   RoundToNSignificantDigitsInt64(13579, 1) -> 10000
//   RoundToNSignificantDigitsInt64(13579, 2) -> 14000
//   RoundToNSignificantDigitsInt64(13579, 3) -> 13600
//   RoundToNSignificantDigitsInt64(13579, 4) -> 13580
//   RoundToNSignificantDigitsInt64(13579, 5) -> 13579
//   RoundToNSignificantDigitsInt64(13579, 6) -> 13579
//   RoundToNSignificantDigitsInt64(97531, 1) -> 100000
//   RoundToNSignificantDigitsInt64(97531, 2) -> 98000
//   RoundToNSignificantDigitsInt64(97531, 3) -> 97500
//   RoundToNSignificantDigitsInt64(97531, 4) -> 97530
//   RoundToNSignificantDigitsInt64(97531, 5) -> 97531
//   RoundToNSignificantDigitsInt64(-97531, 1) -> -100000
//   RoundToNSignificantDigitsInt64(-97531, 2) -> -98000
//   RoundToNSignificantDigitsInt64(-97531, 3) -> -97500
//   RoundToNSignificantDigitsInt64(-97531, 4) -> -97530
//   RoundToNSignificantDigitsInt64(-97531, 5) -> -97531
uint64_t RoundToNSignificantDigitsUint64(uint64_t val, int n);

int64_t RoundToNSignificantDigitsInt64(int64_t val, int n);

// ------------------------ Quantization ------------------------
//
// The Quantize() function template defined below implements
// quantization by linearly mapping a floating-point value from the
// closed range [0, 1] to an integer value in the closed integer
// range [0, max_level] for a given max_level >= 0.
//
// Sample usage:
//
// static const uint8 kMaxLevel = 255;
// uint8 quantized_value = util::math::Quantize(0.876f, kMaxLevel);
//
// Note: A lot of existing code in google3 contains ad-hoc
// quantization formulas that are similar to the one provided here,
// but are not quite linear (they underpopulate the largest
// level). The purpose of this function is to provide a shared
// implementation that is correct and efficient.

// Linear quantization of a floating-point value in the range [0, 1]
// to an integer in the range [0, max_level].
template <typename InFloat, typename OutInt>
OutInt Quantize(InFloat val, OutInt max_level) {
  return val >= InFloat(1)
             ? max_level
             : static_cast<OutInt>(val * (max_level + InFloat(1)));
}

}  // namespace math
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_MATH_ROUND_H_
