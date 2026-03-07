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

//
// <link>
// SafeIntOps provides functions to add, subtract, multiply, or divide
// integers while protecting callers from the nuances of two's complement
// arithmetic and implicit C/C++ type conversions.
//
// These functions succeed and return true only if the result can be computed
// and stored in the destination argument in a value-preserving way. The
// functions will return false if the result can't be computed or is outside
// the range of the destination type.
//
// Sample usage:
//   void* AllocateMyStructs(int num) {
//     size_t bytes_needed;
//     if (!SafeIntOps::Multiply(num,
//                               sizeof(my_struct),
//                               &bytes_needed)) {
//       return NULL;
//     }
//     return malloc(bytes_needed);
//   }
//

#ifndef THIRD_PARTY_GLOOP_UTIL_INTOPS_SAFE_INT_OPS_H_
#define THIRD_PARTY_GLOOP_UTIL_INTOPS_SAFE_INT_OPS_H_

#include <limits.h>

#include <cstdint>
#include <limits>

#include "absl/numeric/int128.h"
#include "gloop/util/intops/lossless_convert.h"

namespace util_intops {

// These functions perform the specified operation on the source operands and
// store the value in the result. If the value fits in the result, then the
// operation will succeed and return true, even if the source operands would
// normally overflow or cause an error. Otherwise, it will leave the result
// unchanged and return false to indicate error. See <link> for more
// information and examples.
template <typename T, typename U, typename V>
bool SafeAdd(T lhs, U rhs, V* result);

template <typename T, typename U, typename V>
bool SafeSubtract(T lhs, U rhs, V* result);

template <typename T, typename U, typename V>
bool SafeMultiply(T lhs, U rhs, V* result);

template <typename T, typename U, typename V>
bool SafeDivide(T lhs, U rhs, V* result);

template <typename T, typename U, typename V>
bool SafeModulo(T lhs, U rhs, V* result);

// This succeeds any time the result can hold a faithful representation of
// lhs * 2^rhs. Shifting 0 by any amount will succeed, even though the C
// standard considers this undefined behavior. Negative values for the shift
// amount always return false to indicate failure.
template <typename T, typename V>
bool SafeLeftShift(T lhs, int64_t rhs, V* result);

// This succeeds any time the result can store a faithful representation of
// lhs / 2^rhs (rounded to negative infinity). Shifting by an arbitrarily large
// number will succeed because the result can be calculated, even though this
// behavior is undefined as per the C spec. Negative values for the shift
// amount always return false to indicate failure.
template <typename T, typename V>
bool SafeRightShift(T lhs, int64_t rhs, V* result);

// ----------------------------------------------------------------------
// Implementation details -- clients should ignore
namespace safe_int_ops_internal {

template <typename T>
void Check64BitIntOrSmaller() {
  static_assert(sizeof(T) <= 8,
                "SafeIntOps can not operate on types larger than 64 bits");
  static_assert(std::numeric_limits<T>::is_integer,
                "SafeIntOps can not operate on non-integer types");
}

template <typename T>
void ConvertToUint128(T from, absl::uint128* to) {
  if (from < 0) {
    *to = absl::MakeUint128(0xFFFFFFFFFFFFFFFF, from);
  } else {
    *to = absl::MakeUint128(0, from);
  }
}

template <typename U>
bool Assign(absl::uint128 from, U* to) {
  Check64BitIntOrSmaller<U>();

  // This library relies on behavior that is implementation-defined.
  // Specifically, it casts an unsigned number into a signed type that can not
  // fit the result and expects a negative result. GCC defines this behavior,
  // but this library may not work properly with another compiler.
  static_assert(
      static_cast<int64_t>(0xFFFFFFFFFFFFFFFF) == -1LL,
      "SafeIntOps will not work with this compiler, because it does type "
      "conversion in an unexpected manner");

  uint64_t high = absl::Uint128High64(from);
  uint64_t low = absl::Uint128Low64(from);

  if (high == 0) {
    // Result is positive.
    if (low <= std::numeric_limits<U>::max()) {
      *to = static_cast<U>(low);
      return true;
    }
  } else if (high == 0xFFFFFFFFFFFFFFFF && (low & 0x8000000000000000)) {
    // Result is negative.
    if (static_cast<int64_t>(low) >=
        static_cast<int64_t>(std::numeric_limits<U>::min())) {
      *to = static_cast<U>(low);
      return true;
    }
  }
  return false;
}

template <typename T, typename U>
void DivideModuloHelper(T lhs, U rhs, absl::uint128* quotient,
                        absl::uint128* remainder) {
  Check64BitIntOrSmaller<T>();
  Check64BitIntOrSmaller<U>();

  absl::uint128 big_lhs;
  absl::uint128 big_rhs;
  // True if the operands have different signs.
  bool negate_result = false;

  // Division and mod require special handling because we can't use unsigned
  // division on signed numbers. This makes the operands positive and then
  // negates the result if necessary.
  if (lhs < 0) {
    // int64 min is a special case because multiplying by -1 results in
    // a signed integer overflow, which is undefined behavior.
    // No possibility of signed/unsigned mismatch because lhs < 0.
    if (lhs == std::numeric_limits<int64_t>::min()) {
      ConvertToUint128(uint64_t{1} + std::numeric_limits<int64_t>::max(),
                       &big_lhs);
    } else {
      ConvertToUint128(lhs * -1LL, &big_lhs);
    }
    negate_result = !negate_result;
  } else {
    ConvertToUint128(lhs, &big_lhs);
  }

  if (rhs < 0) {
    if (rhs == std::numeric_limits<int64_t>::min()) {
      ConvertToUint128(uint64_t{1} + std::numeric_limits<int64_t>::max(),
                       &big_rhs);
    } else {
      ConvertToUint128(rhs * -1LL, &big_rhs);
    }
    negate_result = !negate_result;
  } else {
    ConvertToUint128(rhs, &big_rhs);
  }

  *quotient = big_lhs / big_rhs;
  if (negate_result) {
    *quotient *= -1;
  }

  ConvertToUint128(lhs, &big_lhs);
  ConvertToUint128(rhs, &big_rhs);
  *remainder = big_lhs - big_rhs * (*quotient);
}

template <typename T, typename V>
bool LeftShiftHelper(T lhs, int64_t rhs, V* result) {
  T temp_value = lhs << rhs;
  if (temp_value >> rhs != lhs) {
    return false;
  }

  return LosslessConvert(temp_value, result);
}

}  // namespace safe_int_ops_internal

template <typename T, typename U, typename V>
bool SafeAdd(T lhs, U rhs, V* result) {
  safe_int_ops_internal::Check64BitIntOrSmaller<T>();
  safe_int_ops_internal::Check64BitIntOrSmaller<U>();

  absl::uint128 big_lhs;
  absl::uint128 big_rhs;
  safe_int_ops_internal::ConvertToUint128(lhs, &big_lhs);
  safe_int_ops_internal::ConvertToUint128(rhs, &big_rhs);

  absl::uint128 provisional_result = big_lhs + big_rhs;
  return safe_int_ops_internal::Assign(provisional_result, result);
}

template <typename T, typename U, typename V>
bool SafeSubtract(T lhs, U rhs, V* result) {
  safe_int_ops_internal::Check64BitIntOrSmaller<T>();
  safe_int_ops_internal::Check64BitIntOrSmaller<U>();

  absl::uint128 big_lhs;
  absl::uint128 big_rhs;
  safe_int_ops_internal::ConvertToUint128(lhs, &big_lhs);
  safe_int_ops_internal::ConvertToUint128(rhs, &big_rhs);
  absl::uint128 provisional_result = big_lhs - big_rhs;
  return safe_int_ops_internal::Assign(provisional_result, result);
}

template <typename T, typename U, typename V>
bool SafeMultiply(T lhs, U rhs, V* result) {
  safe_int_ops_internal::Check64BitIntOrSmaller<T>();
  safe_int_ops_internal::Check64BitIntOrSmaller<U>();

  absl::uint128 big_lhs;
  absl::uint128 big_rhs;
  safe_int_ops_internal::ConvertToUint128(lhs, &big_lhs);
  safe_int_ops_internal::ConvertToUint128(rhs, &big_rhs);
  absl::uint128 provisional_result = big_lhs * big_rhs;
  return safe_int_ops_internal::Assign(provisional_result, result);
}

template <typename T, typename U, typename V>
bool SafeDivide(T lhs, U rhs, V* result) {
  if (rhs == 0) {
    return false;
  }

  absl::uint128 quotient;
  absl::uint128 remainder;
  safe_int_ops_internal::DivideModuloHelper(lhs, rhs, &quotient, &remainder);
  return safe_int_ops_internal::Assign(quotient, result);
}

template <typename T, typename U, typename V>
bool SafeModulo(T lhs, U rhs, V* result) {
  if (rhs == 0) {
    return false;
  }

  absl::uint128 quotient;
  absl::uint128 remainder;
  safe_int_ops_internal::DivideModuloHelper(lhs, rhs, &quotient, &remainder);
  return safe_int_ops_internal::Assign(remainder, result);
}

template <typename T, typename V>
bool SafeLeftShift(T lhs, int64_t rhs, V* result) {
  safe_int_ops_internal::Check64BitIntOrSmaller<T>();

  if (rhs < 0) {
    return false;
  }

  // A left shift of 64-bits or more will overflow, unless lhs is 0.
  // We allow shifts of larger size than the original source type because
  // the result might still fit in the destination type.
  if (rhs >= 64) {
    if (lhs == 0) {
      *result = 0;
      return true;
    } else {
      return false;
    }
  }

  // This library relies on behavior that is implementation-defined.
  // Specifically, it shifts signed integers which may be negative. GCC
  // defines this behavior to operate on the bits while ignoring their
  // interpretation but this library may not work properly with another
  // compiler.
  static_assert(
      static_cast<int64_t>(0x4000000000000000LL << 1) ==
          std::numeric_limits<int64_t>::min(),
      "SafeIntOps will not work with this compiler, because it does type "
      "conversion in an unexpected manner");

  // The signedness of the operand used for source of LeftShiftHelper is based
  // on the value of the source, not the type.  This allows for the use of all
  // 64 bits when left-shifting a signed positive number that would overflow if
  // using an int64, but can be stored in a uint64 result.
  if (lhs >= 0) {
    return safe_int_ops_internal::LeftShiftHelper(static_cast<uint64_t>(lhs),
                                                  rhs, result);
  } else {
    return safe_int_ops_internal::LeftShiftHelper(static_cast<int64_t>(lhs),
                                                  rhs, result);
  }
}

template <typename T, typename V>
bool SafeRightShift(T lhs, int64_t rhs, V* result) {
  safe_int_ops_internal::Check64BitIntOrSmaller<T>();

  // This function relies on implementation-defined behavior that theoretically
  // could differ when using a new compiler. We check for that here, even
  // though it's not very likely.
  static_assert(
      -1LL >> 63 == -1,
      "SafeIntOps will not work with this compiler, because it performs right "
      "shifts in an unexpected manner");

  if (rhs < 0) {
    return false;
  } else if (rhs >= sizeof(T) * CHAR_BIT) {
    // Shifting by more bits than exist in the source operand is undefined
    // according to the C spec, but this implements mathematically-expected
    // behavior.
    if (lhs < 0) {
      lhs = -1;
    } else {
      lhs = 0;
    }
  } else {
    // Overflow can't happen in this case.
    lhs = lhs >> rhs;
  }

  return LosslessConvert(lhs, result);
}

}  // namespace util_intops

#endif  // THIRD_PARTY_GLOOP_UTIL_INTOPS_SAFE_INT_OPS_H_
