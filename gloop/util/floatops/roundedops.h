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

#ifndef THIRD_PARTY_GLOOP_UTIL_FLOATOPS_ROUNDEDOPS_H_
#define THIRD_PARTY_GLOOP_UTIL_FLOATOPS_ROUNDEDOPS_H_

#include <fenv.h>

#include <cmath>

#include "absl/base/config.h"

namespace util {
namespace floatops {

enum RoundingMode : int {
  kToNearest = FE_TONEAREST,
  kUpward = FE_UPWARD,
  kDownward = FE_DOWNWARD,
  kTowardZero = FE_TOWARDZERO
};

namespace detail {

// Launder a value so that the compiler cannot constant-propagate through it.
template <typename FloatType>
FloatType OptimizationBarrier(FloatType operand) {
  // TODO: The "+X" constraint causes GCC to ICE, so only use it
  // for LLVM codegen for now.
#ifdef __clang__
  asm volatile("" : "+X"(operand) :);
#else
  asm volatile("" : "+m"(operand) :);
#endif
  return operand;
}

// An RAII class to temporarily switch the rounding mode.
class ScopedRoundingModeChange {
 public:
  explicit ScopedRoundingModeChange(RoundingMode rounding_mode)
      : old_rounding_mode_(static_cast<RoundingMode>(fegetround())) {
    fesetround(rounding_mode);
  }

  // This type is neither copyable nor movable.
  ScopedRoundingModeChange(const ScopedRoundingModeChange&) = delete;
  ScopedRoundingModeChange& operator=(const ScopedRoundingModeChange&) = delete;
  ~ScopedRoundingModeChange() { fesetround(old_rounding_mode_); }

 private:
  RoundingMode old_rounding_mode_;
};

}  // namespace detail

// Perform a floating-point addition with a specified rounding mode, in a way
// which is immune to compiler optimizations.
//
// Usage:
//   float g = util::floatops::RoundedAdd<util::floatops::kUpward>(1.0f, f);
template <RoundingMode rounding_mode, typename FloatType>
FloatType RoundedAdd(FloatType lhs, FloatType rhs) {
  detail::ScopedRoundingModeChange set_rounding_mode(rounding_mode);
  return detail::OptimizationBarrier(detail::OptimizationBarrier(lhs) +
                                     detail::OptimizationBarrier(rhs));
}

// Perform a floating-point subtraction with a specified rounding mode, in
// a way which is immune to compiler optimizations.
template <RoundingMode rounding_mode, typename FloatType>
FloatType RoundedSubtract(FloatType lhs, FloatType rhs) {
  detail::ScopedRoundingModeChange set_rounding_mode(rounding_mode);
  return detail::OptimizationBarrier(detail::OptimizationBarrier(lhs) -
                                     detail::OptimizationBarrier(rhs));
}

// Perform a floating-point multiplication with a specified rounding mode, in
// a way which is immune to compiler optimizations.
template <RoundingMode rounding_mode, typename FloatType>
FloatType RoundedMultiply(FloatType lhs, FloatType rhs) {
  detail::ScopedRoundingModeChange set_rounding_mode(rounding_mode);
  return detail::OptimizationBarrier(detail::OptimizationBarrier(lhs) *
                                     detail::OptimizationBarrier(rhs));
}

// Perform a floating-point division with a specified rounding mode, in a way
// which is immune to compiler optimizations.
template <RoundingMode rounding_mode, typename FloatType>
FloatType RoundedDivide(FloatType lhs, FloatType rhs) {
  detail::ScopedRoundingModeChange set_rounding_mode(rounding_mode);
  return detail::OptimizationBarrier(detail::OptimizationBarrier(lhs) /
                                     detail::OptimizationBarrier(rhs));
}

// Rounds a floating-point value to an integer. Equivalent to std::lround except
// for halfway cases which are rounded to the nearest even integer.
//
// Note this returns long since it's a wrapper for std::lrint, which is itself
// declared as returning long in the STL.
template <typename FloatType>
// NOLINTNEXTLINE: std::lrint declared as returning long.
long RoundToNearestIntegerTiesToEven(FloatType x) {
  static_assert(
      // NOLINTNEXTLINE: std::lrint declared as returning long.
      std::is_same_v<long, decltype(std::lrint(x))>,
      "Expected std::lrint return type to be long");
#if ABSL_HAVE_BUILTIN(__builtin_elementwise_roundeven)
  // The `roundeven` function always has roundTiesToEven semantics regardless of
  // the prevailing rounding mode. For more details on
  // `__builtin_elementwise_roundeven`, see:
  // https://clang.llvm.org/docs/LanguageExtensions.html#vector-builtins
  return __builtin_elementwise_roundeven(x);
#else
  // The default rounding mode in google3 is FE_TONEAREST and should not have
  // been modified outside //gloop/util/floatops. Bail out via CHECK otherwise.
  CHECK_EQ(fegetround(), kToNearest);
  return std::lrint(x);
#endif
}

// Rounds a floating-point value. Equivalent to std::round except for halfway
// cases which are rounded to the nearest even integer.
template <typename FloatType>
FloatType RoundToNearestTiesToEven(FloatType x) {
#if ABSL_HAVE_BUILTIN(__builtin_elementwise_roundeven)
  // The `roundeven` function always has roundTiesToEven semantics regardless of
  // the prevailing rounding mode. For more details on
  // `__builtin_elementwise_roundeven`, see:
  // https://clang.llvm.org/docs/LanguageExtensions.html#vector-builtins
  return __builtin_elementwise_roundeven(x);
#else
  // The default rounding mode in google3 is FE_TONEAREST and should not have
  // been modified outside //gloop/util/floatops. Bail out via CHECK otherwise.
  CHECK_EQ(fegetround(), kToNearest);
  return std::rint(x);
#endif
}

}  // namespace floatops
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_FLOATOPS_ROUNDEDOPS_H_
