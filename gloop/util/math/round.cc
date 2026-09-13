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

#include "gloop/util/math/round.h"

#include <cstdint>
#include <limits>

#include "absl/base/call_once.h"
#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "gloop/util/bits/bits.h"
#include "gloop/util/math/mathutil.h"

namespace util {
namespace math {

uint64_t RoundToNSignificantBitsUint64(uint64_t val, int n) {
  if (ABSL_PREDICT_FALSE(val == 0 || n <= 0)) {
    // Zero always rounds to zero, and any value rounded to zero or fewer
    // significant bits always rounds to zero.
    return 0ULL;
  }
  int shift = Bits::Log2FloorNonZero64(val) - n;
  if (shift < 0) {
    // The value already has n or fewer significant bits.
    return val;
  }
  DCHECK_LT(shift, 63);
  // Shift out all but one of the unwanted low-order bits.
  val = val >> shift;
  DCHECK_NE(0, val);
  // If the most significant unwanted bit is set, round up.
  val += (val & 0x1);
  // Shift back up to restore the proper magnitude
  val = val << shift;
  if (ABSL_PREDICT_TRUE(val != 0)) {
    return val;
  }
  // Oops. We rounded up, causing overflow. This is easy to fix: just return
  // the largest possible value: n leading bits.
  shift = 64 - n;
  DCHECK_LT(shift, 64);
  val = (std::numeric_limits<uint64_t>::max() >> shift) << shift;
  DCHECK_NE(0, val);
  return val;
}

int64_t RoundToNSignificantBitsInt64(int64_t val, int n) {
  if (val >= 0) {
    // We can't just delegate to the unsigned version of the function since
    // large values could be rounded up to be greater than int64max. However if
    // we shift the value left by one bit, then we will be protected against
    // overflow by the same logic used to handle overflow in unsigned values.
    return static_cast<int64_t>(
        RoundToNSignificantBitsUint64(static_cast<uint64_t>(val) << 1, n) >> 1);
  } else {
    // Negative numbers are allowed to overflow into the 64th bit, so we just
    // need to fix the sign. The ternary check in the first argument is to avoid
    // undefined behavior by negating kint64min.
    return -RoundToNSignificantBitsUint64(
        (val == std::numeric_limits<int64_t>::min() ? val : -val), n);
  }
}

uint32_t RoundToNSignificantBitsUint32(uint32_t val, int n) {
  // Protect against overflow by shifting the value up before rounding.
  return static_cast<uint32_t>(
      RoundToNSignificantBitsUint64(static_cast<uint64_t>(val) << 32, n) >> 32);
}

int32_t RoundToNSignificantBitsInt32(int32_t val, int n) {
  if (val >= 0) {
    return static_cast<int32_t>(
        RoundToNSignificantBitsUint32(static_cast<uint32_t>(val) << 1, n) >> 1);
  } else {
    // The ternary check in the first argument is to avoid undefined behavior by
    // negating kint32min.
    return -RoundToNSignificantBitsUint32(
        (val == std::numeric_limits<int32_t>::min() ? val : -val), n);
  }
}

namespace {

const int kMaxDigitsUint64 = 20;  // The number of decimal digits in kuint64max.
const int kMaxDigitsInt64 = 19;   // The number of decimal digits in kint64min
                                  // and kint64max.

// Precompute a lookup table of values needed by
// RoundToNSignificantDigitsGeneric.

struct RoundingLookupEntry {
  uint64_t overflow_cutoff;  // the smallest value which might cause overflow
                             // when the 'idx' least significant digits are
                             // rounded away.
};

struct RoundingLookupEntry
    uint64_lookup_table[kMaxDigitsUint64] ABSL_CACHELINE_ALIGNED;
struct RoundingLookupEntry
    int64_lookup_table[kMaxDigitsInt64] ABSL_CACHELINE_ALIGNED;

void InitDecimalRoundingTables() {
  uint64_t val = 1;
  for (int i = 0; i < kMaxDigitsUint64; i++) {
    uint64_lookup_table[i].overflow_cutoff =
        ((std::numeric_limits<uint64_t>::max() / val) * val) - (val / 2);
    if (i < kMaxDigitsInt64) {
      int64_lookup_table[i].overflow_cutoff =
          ((std::numeric_limits<int64_t>::max() / val) * val) - (val / 2);
    }
    val *= 10;
  }
}

}  // anonymous namespace

namespace {

// Round 'val' to 'n' significant digits, using the lookup table 'table' to
// find the maximum possible value. This lets us use the same logic for uint64
// and int64 types.
uint64_t RoundToNSignificantDigitsGeneric(uint64_t val, int n,
                                          struct RoundingLookupEntry* table) {
  if (ABSL_PREDICT_FALSE(val == 0 || n <= 0)) {
    // Zero always rounds to zero, and any value rounded to 0 or a negative
    // number of digits also rounds to 0.
    return 0;
  }

  const int scale = Log10FloorNonZero(val) - n + 1;
  if (scale <= 0) {
    // 'val' already has n or fewer digits.
    return val;
  }
  const uint64_t factor = MathUtil::IPow10(scale);

  // Initialize the RoundingLookupEntry tables.
  static absl::once_flag once;
  absl::call_once(once, InitDecimalRoundingTables);

  DCHECK_LT(scale, kMaxDigitsUint64);
  table += scale;
  const uint64_t overflow_cutoff = table->overflow_cutoff;
  const uint64_t half_factor = factor >> 1;
  if (ABSL_PREDICT_FALSE(val >= overflow_cutoff)) {
    return overflow_cutoff + half_factor;
  }
  val += half_factor;
  val -= val % factor;
  return val;
}

}  // anonymous namespace

uint64_t RoundToNSignificantDigitsUint64(uint64_t val, int n) {
  if (ABSL_PREDICT_FALSE(n >= kMaxDigitsUint64)) {
    return val;
  }
  return RoundToNSignificantDigitsGeneric(val, n, uint64_lookup_table);
}

int64_t RoundToNSignificantDigitsInt64(int64_t val, int n) {
  if (ABSL_PREDICT_FALSE(n >= kMaxDigitsInt64)) {
    return val;
  }
  if (val >= 0) {
    return static_cast<int64_t>(RoundToNSignificantDigitsGeneric(
        static_cast<uint64_t>(val), n, int64_lookup_table));
  } else {
    // The ternary check in the first argument is to avoid undefined when
    // negating kint64min.
    return -RoundToNSignificantDigitsGeneric(
        (val == std::numeric_limits<int64_t>::min() ? val : -val), n,
        int64_lookup_table);
  }
}

}  // namespace math
}  // namespace util
