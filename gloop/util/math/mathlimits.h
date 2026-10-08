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
// Legacy Google-specific variation on `<cmath>` `std::is*` predicates.
// New code should prefer to use the standard form.

#ifndef THIRD_PARTY_GLOOP_UTIL_MATH_MATHLIMITS_H_
#define THIRD_PARTY_GLOOP_UTIL_MATH_MATHLIMITS_H_

// <cfloat> is not used, but there are too many IWYU violations to remove it.
#include <cmath>
#include <limits>

#include "absl/base/macros.h"

// Useful floating point limits.
template <typename Type>
struct MathLimits {
  static_assert(std::is_floating_point_v<Type>);

  // We do not support `double double`.  `epsilon` is very small for this
  // representation.
  static_assert(std::numeric_limits<Type>::is_iec559);

  // Typical rounding error that is enough to cover a few simple
  // floating-point operations.  It is slightly larger than machine epsilon
  // to account for a few rounding errors.
  static constexpr Type kStdError = 32 * std::numeric_limits<Type>::epsilon();

  // Special floating point value testers.
  ABSL_DEPRECATE_AND_INLINE()
  static bool IsFinite(Type x) { return std::isfinite(x); }
  ABSL_DEPRECATE_AND_INLINE()
  static bool IsInf(Type x) { return std::isinf(x); }
  ABSL_DEPRECATE_AND_INLINE()
  static bool IsPosInf(Type x) {
    return x == std::numeric_limits<Type>::infinity();
  }
};

#endif  // THIRD_PARTY_GLOOP_UTIL_MATH_MATHLIMITS_H_
