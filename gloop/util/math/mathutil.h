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

// Copyright 2001 and onwards Google Inc.

//
// This class is intended to contain a collection of useful (static)
// mathematical functions, properly coded (by consulting numerical
// recipes or another authoritative source first).

#ifndef THIRD_PARTY_GLOOP_UTIL_MATH_MATHUTIL_H_
#define THIRD_PARTY_GLOOP_UTIL_MATH_MATHUTIL_H_

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <type_traits>

#include "absl/base/attributes.h"
#include "absl/base/casts.h"
#include "absl/base/config.h"
#include "absl/base/macros.h"
#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/meta/type_traits.h"
#include "absl/numeric/bits.h"
#include "gloop/util/math/mathlimits.h"

#if defined __SSE2__
#include <immintrin.h>
#elif defined __aarch64__
#include <arm_neon.h>
#endif

// Returns the sign of x:
//   -1 if x < 0,
//   +1 if x > 0,
//    0 if x = 0.
// Consider instead using MathUtil::Sign below for readability
// and floating-point correctness.
template <class T>
inline T sgn(const T x) {
  return (x == 0 ? 0 : (x < 0 ? -1 : 1));
}

// ========================================================================= //

class MathUtil {
 public:
  // Return type of RealRootsForQuadratic (below).  The enum values are
  // chosen to be sensible if converted to bool or int, and should not be
  // changed lightly.
  enum QuadraticRootType {
    kNoRealRoots = 0,
    kAmbiguous = 1,
    kTwoRealRoots = 2
  };

  // Pi, as defined in <cmath> from GRTE v3.
  static constexpr double kPi = 3.14159265358979323846;

  // Returns the QuadraticRootType of the equation a * x^2 + b * x + c = 0.
  // Normal cases are kNoRealRoots, in which case *r1 and *r2 are not
  // changed; and kTwoRealRoots, in which case the root(s) are placed in
  // *r1 and *r2, order not specified.  The kAmbiguous return value
  // indicates that the disciminant is zero, within floating-point error
  // (i.e. that changing an input by epsilon<double> would change the sign
  // of the discriminant). The resulting roots are equal, as if the
  // discriminant were exactly zero.
  //
  // A special case occurs when a==0; see DegenerateQuadraticRoots().
  // See also QuadraticIsAmbiguous() and RealQuadraticRoots().
  static inline QuadraticRootType RealRootsForQuadratic(long double a,
                                                        long double b,
                                                        long double c,
                                                        long double* r1,
                                                        long double* r2) {
    // Deal with degenerate cases where leading coefficients vanish.
    if (a == 0.0) {
      return DegenerateQuadraticRoots(b, c, r1, r2);
    }

    // General case: the quadratic formula, rearranged for greater numerical
    // stability.

    // If the discriminant is zero to numerical precision, regardless of
    // sign, treat it as zero and return kAmbiguous.  We use the double
    // rather than long double value for epsilon because in practice inputs
    // are generally calculated in double precision.
    const long double discriminant = QuadraticDiscriminant(a, b, c);
    if (QuadraticIsAmbiguous(a, b, c, discriminant,
                             std::numeric_limits<double>::epsilon())) {
      *r2 = *r1 = -b / 2 / a;  // The quadratic is (2*a*x + b)^2 = 0.
      return kAmbiguous;
    }

    if (discriminant < 0) {
      // The discriminant is definitely negative so there are no real roots.
      return kNoRealRoots;
    }

    RealQuadraticRoots(a, b, c, discriminant, r1, r2);
    return kTwoRealRoots;
  }

  // Returns the discriminant of the quadratic equation a * x^2 + b * x + c = 0.
  static inline long double QuadraticDiscriminant(long double a, long double b,
                                                  long double c) {
    return b * b - 4 * a * c;
  }

  // Returns true if the discriminant is zero within floating-point error,
  // in the sense that changing one of the coefficients by epsilon (e.g. a
  // -> a + a*epsilon) could change the sign of the discriminant. [When the
  // discriminant is exactly 0 the quadratic is (2*a*x + b)^2 = 0 and the
  // root is - b / (2*a).]
  static inline bool QuadraticIsAmbiguous(long double a, long double b,
                                          long double c,
                                          long double discriminant,
                                          long double epsilon) {
    // Discriminants below kTolerance in absolute value are considered zero
    // because changing the final bit of one of the inputs can change the
    // sign of the discriminant.
    const long double kTolerance =
        epsilon * std::max(2 * b * b, fabsl(4 * a * c));
    return (std::abs(discriminant) <= kTolerance);
  }

  // Returns in *r1 and *r2 the roots of a "normal" quadratic equation
  // whose discriminant (b*b - 4*a*c) is known and positive.  Preconditions
  // (will DCHECK and return false if not satisfied): a != 0, discriminant > 0.
  static inline bool RealQuadraticRoots(long double a, long double b,
                                        long double c, long double discriminant,
                                        long double* r1, long double* r2) {
    if (discriminant <= 0 || a == 0) {
      // A case that should have been excluded by the caller.
      DLOG(FATAL);
      return false;
    }

    // The discriminant is positive so there are two distinct roots.
    // According to Numerical Recipes (p. 184), it would be a mistake to
    // solve for the roots using
    //
    //     r1 = 2c / (-b + sqrt(b^2 - 4ac)),
    //     r2 = 2c / (-b - sqrt(b^2 - 4ac)).
    //
    // If a*c is small, then one of the roots above will involve the
    // subtraction of b from a very nearly equal quantity (the discriminant),
    // producing a very inaccurate root.  Avoid the risk of cancellation with
    // the following rearrangement.  (Note we don't use sgn(b) because we
    // need sgn(0) = +1 or -1.)
    long double const q = -0.5 * (b + ((b >= 0) ? std::sqrt(discriminant)
                                                : -std::sqrt(discriminant)));
    *r1 = q / a;  // If a is very small this produces +/- HUGE_VAL.
    *r2 = c / q;  // q cannot be too small.
    return true;
  }

  // Returns the root of the degenerate quadratic equation b * x + c = 0,
  // following the interface of RealRootsForQuadratic. To be consistent
  // with that function as a->0, the degenerate quadratic is considered to
  // have two real roots, one of which is +/- HUGE_VAL and one of which is
  // -c / b.  If both a and b are 0, so the equation is c = 0, the response
  // is kNoRealRoots if c != 0 or kAmbiguous if c == 0 (since the
  // discriminant is zero).
  static inline QuadraticRootType DegenerateQuadraticRoots(long double b,
                                                           long double c,
                                                           long double* r1,
                                                           long double* r2) {
    // This degenerate quadratic is really a linear equation b * x = -c.
    if (b == 0.0) {
      // The equation is constant, c == 0.
      if (c == 0.0) {
        // Quadratic equation is 0==0; treat as ambiguous, as if a==epsilon.
        *r1 = *r2 = 0.0;
        return kAmbiguous;
      }
      return kNoRealRoots;
    }
    // The linear equation has a single root at x = -c / b, not a double
    // one.  Respond as if a==epsilon: The other root is at "infinity",
    // which we signal with HUGE_VAL so that the behavior stays consistent
    // as a->0.
    *r1 = -c / b;
    *r2 = HUGE_VAL;
    return kTwoRealRoots;
  }

  // Solves for the real roots of x^3+ax^2+bx+c=0, returns true iff
  // all three are real, in which case the roots are stored (in any
  // order) in r1, r2, r3; otherwise, exactly one real root exists and
  // it is stored in r1.
  static bool RealRootsForCubic(long double a, long double b, long double c,
                                long double* r1, long double* r2,
                                long double* r3);

  // Solves for the real roots of the quartic equation, x^4+ax^3+bx^2+cx+d=0,
  // returns the number of real roots found.
  static int RealRootsForQuartic(long double a, long double b, long double c,
                                 long double d, long double* roots);

  // ----------------------------------------------------------------------
  // Sigmoid
  //   A sigmoid function is a differentiable s curve that ranges between
  //   0 and 1:
  //   f(x) = 1/(1+e^(-lambda x))
  // --------------------------------------------------------------------
  static double Sigmoid(double x, double lambda) {
    return 1 / (1 + std::exp(-lambda * x));
  }

  // ----------------------------------------------------------------------
  // InverseSigmoid
  //   Inverts Sigmoid such that InverseSigmoid(Sigmoid(x)) == x for all x.
  //   x must be in (0, 1), and lambda must not be 0.
  // ----------------------------------------------------------------------
  static double InverseSigmoid(double const x, double const lambda) {
    return -std::log((1.0 / x - 1)) / lambda;
  }

  // ----------------------------------------------------------------------
  // SigmoidFloat
  //   A sigmoid function is a differentiable s curve that ranges between
  //   0 and 1:
  //   f(x) = 1/(1+e^(-lambda x))
  // --------------------------------------------------------------------
  static double SigmoidFloat(float x, float lambda) {
    return 1.0f / (1.0f + std::exp(-lambda * x));
  }

  // ----------------------------------------------------------------------
  // InverseSigmoidFloat
  //   Inverts Sigmoid such that InverseSigmoidFloat(SigmoidFloat(x)) == x
  //   for all x.
  //   x must be in (0, 1), and lambda must not be 0.
  // ----------------------------------------------------------------------
  static double InverseSigmoidFloat(float const x, float const lambda) {
    return -std::log((1.0f / x - 1.0f)) / lambda;
  }

  // ----------------------------------------------------------------------
  // Sigmoid2
  //   A nicer way of specifying a sigmoid. A sigmoid is a smooth s curve
  //   that ranges from 0 to 1. We describe a sigmoid with three values:
  //
  //   start: the x value at which f(x) = tolerance
  //   finish: the x value at which f(x) = 1-tolerance
  //
  // So if we want a smoothly transitioning function from, say, x=1 to
  // x=10 with the property that anything outside the domain [1, 10]
  // will still be within 10% of either f(1) or f(10), then we set: start
  // = 1, finish = 10, tolerance = 0.1.
  // See <link> for more details.
  // --------------------------------------------------------------------
  static double Sigmoid2(double x, double start_x, double finish_x,
                         double tolerance) {
    DCHECK_GT(tolerance, 0);
    DCHECK_LT(tolerance, 1);
    DCHECK_NE(finish_x - start_x, 0);
    double lambda =
        std::log((1 - tolerance) / tolerance) * 2 / (finish_x - start_x);
    return Sigmoid(x - 0.5 * (start_x + finish_x), lambda);
  }

  // ----------------------------------------------------------------------
  // CeilOfRatio<IntegralType>
  // FloorOfRatio<IntegralType>
  //   Returns the ceil (resp. floor) of the ratio of two integers.
  //
  //  * IntegralType: any integral type, whether signed or not.
  //  * numerator: any integer: positive, negative, or zero.
  //  * denominator: a non-zero integer, positive or negative.
  //
  // This implementation is correct, meaning there is never any precision loss,
  // and there is never an overflow. However, if the type is signed, having
  // numerator == std::numeric_limits<IntegralType>::lowest() and denominator ==
  // -1 is not a valid input, because lowest() has a greater absolute value than
  // kMax.
  //
  // Input validity is DCHECKed. When not in debug mode, invalid inputs raise
  // SIGFPE.
  //
  // This method has been designed and tested so that it should always be
  // preferred to alternatives. Indeed, there exist popular recipes to compute
  // the result, such as casting to double, but they are in general incorrect.
  // In cases where an alternative technique is correct, performance measurement
  // showed the provided implementation is faster.
  //
  // Details of these alternative (dangerous) techniques, examples showing their
  // incorrectness, and performance comparisons are in the unit tests. Here is
  // a perflab result comparing the provided implementation with two others:
  // http://perflab.mtv/120301135354310524030
  // ----------------------------------------------------------------------
  template <typename IntegralType>
  static IntegralType CeilOfRatio(IntegralType numerator,
                                  IntegralType denominator) {
    return CeilOrFloorOfRatio<IntegralType, true>(numerator, denominator);
  }
  template <typename IntegralType>
  static IntegralType FloorOfRatio(IntegralType numerator,
                                   IntegralType denominator) {
    return CeilOrFloorOfRatio<IntegralType, false>(numerator, denominator);
  }
  template <typename IntegralType, bool ceil>
  static IntegralType CeilOrFloorOfRatio(IntegralType numerator,
                                         IntegralType denominator);

  // ----------------------------------------------------------------------
  // MulDiv<IntegralType>
  //   Returns the integral quotient and remainder from dividing a*b by d:
  //     ((a*b)/d, (a*b)%d)
  //
  //  * IntegralType: any integral type, whether signed or not.
  //  * a: a positive integer or zero.
  //  * b: a positive integer or zero.
  //  * d: a non-zero positive integer.
  //
  // This implementation is correct, meaning there is never any precision loss,
  // and overflow only occurs if (a*b)/d cannot fit in IntegralType.
  // ----------------------------------------------------------------------
  template <typename IntegralType>
  struct DivisionResult {
    IntegralType quotient;
    IntegralType remainder;

    inline bool operator==(DivisionResult<IntegralType> other) const {
      return quotient == other.quotient && remainder == other.remainder;
    }
  };
  template <typename IntegralType>
  static DivisionResult<IntegralType> MulDiv(IntegralType a, IntegralType b,
                                             IntegralType d);

  // ----------------------------------------------------------------------
  // CeilOfScaledRatio<IntegralType>
  // FloorOfScaledRatio<IntegralType>
  //   Returns the ceil (resp. floor) of the scaled ratio of integers:
  //     scale_factor * numerator / denominator
  //
  //  * IntegralType: any integral type, whether signed or not.
  //  * numerator: any non-negative integer, positive or zero.
  //  * denominator: a positive integer, non-zero.
  //  * scale_factor: any non-negative integer, positive or zero.
  //
  // This implementation is correct, meaning there is never any precision loss,
  // and overflow only occurs if the result does not fit in IntegralType.
  //
  // Input validity is DCHECKed.
  // ----------------------------------------------------------------------
  template <typename IntegralType>
  static IntegralType CeilOfScaledRatio(IntegralType numerator,
                                        IntegralType denominator,
                                        IntegralType scale_factor) {
    return CeilOrFloorOfScaledRatio<IntegralType, true>(numerator, denominator,
                                                        scale_factor);
  }
  template <typename IntegralType>
  static IntegralType FloorOfScaledRatio(IntegralType numerator,
                                         IntegralType denominator,
                                         IntegralType scale_factor) {
    return CeilOrFloorOfScaledRatio<IntegralType, false>(numerator, denominator,
                                                         scale_factor);
  }
  template <typename IntegralType, bool ceil>
  static IntegralType CeilOrFloorOfScaledRatio(IntegralType numerator,
                                               IntegralType denominator,
                                               IntegralType scale_factor);

  // ----------------------------------------------------------------------
  // CeilOfPercentage<IntegralType>
  // FloorOfPercentage<IntegralType>
  //   Returns the ceil (resp. floor) of the ratio of two integers as a
  //   percentage; e.g., ceil/floor(100*numerator/denominator).
  //
  //  * IntegralType: any integral type, whether signed or not.
  //  * numerator: any integer: positive, negative, or zero.
  //  * denominator: a non-zero integer, positive or negative.
  //
  // This implementation is correct, meaning there is never any precision loss,
  // and overflow only occurs if the result does not fit in IntegralType.
  //
  // Input validity is DCHECKed.
  // ----------------------------------------------------------------------
  template <typename IntegralType>
  static IntegralType CeilOfPercentage(IntegralType numerator,
                                       IntegralType denominator) {
    return CeilOfScaledRatio<IntegralType>(numerator, denominator,
                                           /*scale_factor=*/100);
  }
  template <typename IntegralType>
  static IntegralType FloorOfPercentage(IntegralType numerator,
                                        IntegralType denominator) {
    return FloorOfScaledRatio<IntegralType>(numerator, denominator,
                                            /*scale_factor=*/100);
  }

  // Euclid's Algorithm.
  // Returns: the greatest common divisor of two unsigned integers x and y
  ABSL_DEPRECATE_AND_INLINE()
  static unsigned int GCD(unsigned int x, unsigned int y) {
    return std::gcd(x, y);
  }

  // Euclid's Algorithm.
  // Returns: the greatest common divisor of two integers x and y
  ABSL_DEPRECATE_AND_INLINE()
  static uint64_t GCD64(uint64_t x, uint64_t y) { return std::gcd(x, y); }

  // Returns the greatest common divisor of two unsigned integers x and y,
  // and assigns a, and b such that a*x + b*y = gcd(x, y).
  static unsigned int ExtendedGCD(unsigned int x, unsigned int y, int* a,
                                  int* b);

  // Returns the least common multiple of two unsigned integers.  Returns zero
  // if either is zero.
  ABSL_DEPRECATE_AND_INLINE()
  static unsigned int LeastCommonMultiple(unsigned int a, unsigned int b) {
    return std::lcm(a, b);
  }

  // Returns the least common multiple of two 64 bit integers.  Returns zero
  // if either is zero.
  ABSL_DEPRECATE_AND_INLINE()
  static uint64_t LeastCommonMultiple64(uint64_t a, uint64_t b) {
    return std::lcm(a, b);
  }

  // Returns the nonnegative remainder when one integer is divided by another.
  // The modulus must be positive.  Use integral types only (no float or
  // double).
  template <class T>
  static T NonnegativeMod(T a, T b) {
    static_assert(std::is_integral_v<T>, "Integral types only.");
    DCHECK_GT(b, 0);
    // As of C++11 (per [expr.mul]/4), a%b is in (-b,0] for a<0, b>0.
    T c = a % b;
    return c + (c < 0) * b;
  }

  // Converts a non-zero double value representing an odds into its
  // probability value.
  static double OddsToProbability(double odds) {
    DCHECK_GE(odds, 0.0);
    return odds / (1.0 + odds);
  }

  // Converts a probability with range [0-1.0) into its odds value.
  static double ProbabilityToOdds(double prob) {
    DCHECK_GE(prob, 0.0);
    DCHECK_LT(prob, 1.0);
    return prob / (1.0 - prob);
  }

  // --------------------------------------------------------------------
  // Round
  //   This function rounds a floating-point number to an integer. It
  //   works for positive or negative numbers.  Requires that the
  //   **rounded** value fits in the target type.
  //
  //   Values that are halfway between two integers may be rounded up or
  //   down, for example Round<int>(0.5) == 0 and Round<int>(1.5) == 2.
  //   This allows the function to be implemented efficiently on multiple
  //   hardware platforms (see the template specializations at the bottom
  //   of this file). You should not use this function if you care about which
  //   way such half-integers are rounded.
  //
  //   Example usage:
  //     double y, z;
  //     int x = Round<int>(y + 3.7);
  //     int64_t b = Round<int64_t>(0.3 * z);
  //
  //   Note that the floating-point template parameter is typically inferred
  //   from the argument type, i.e. there is no need to specify it explicitly.
  // --------------------------------------------------------------------
  template <class IntOut, class FloatIn>
  static IntOut Round(FloatIn x) {
    static_assert(!std::numeric_limits<FloatIn>::is_integer,
                  "FloatIn is integer");
    static_assert(std::numeric_limits<IntOut>::is_integer,
                  "IntOut is not integer");
    DCheckFloatIsInRange<IntOut>(x);
    // This base implementation uses std::round to turn `FloatIn` into an
    // integer and then performs a conversion to integer.
    // Clang will generate pretty nice code for this without resorting to
    // trickery, making it easy for the compiler to choose an instruction which
    // has the exactly desired semantics (fcvtau on AArch64, fcvt.wu.s on
    // RISC-V, xsrdpi on POWER, etc.) or otherwise a branchless sequence like:
    //   nudge = copysign(x, nextdown(0.5))
    //   abs_x = abs(x)
    //   abs_x += nudge
    //   rounded = trunc(abs_x)
    //   f_result = copysign(rounded, x)
    //   i_result = static_cast<IntOut>(f_result)
    return static_cast<IntOut>(std::round(x));
  }

  // Returns the maximum integer value which is a multiple of rounding_value,
  // and less than or equal to input_value.
  // The input_value must be greater than or equal to zero, and the
  // rounding_value must be greater than zero.
  template <typename IntType>
  static IntType RoundDownTo(IntType input_value, IntType rounding_value) {
    static_assert(std::numeric_limits<IntType>::is_integer,
                  "RoundDownTo() operation type is not integer");
    DCHECK_GE(input_value, IntType(0));
    DCHECK_GT(rounding_value, IntType(0));
    return (input_value / rounding_value) * rounding_value;
  }

  // Returns the minimum integer value which is a multiple of rounding_value,
  // and greater than or equal to input_value.
  // The input_value must be greater than or equal to zero, and the
  // rounding_value must be greater than zero.
  template <typename IntType>
  static IntType RoundUpTo(IntType input_value, IntType rounding_value) {
    static_assert(std::numeric_limits<IntType>::is_integer,
                  "RoundUpTo() operation type is not integer");
    DCHECK_GE(input_value, IntType(0));
    DCHECK_GT(rounding_value, IntType(0));
    const IntType remainder = input_value % rounding_value;
    return (remainder == IntType(0))
               ? input_value
               : (input_value - remainder + rounding_value);
  }

  // Convert a floating-point number to an integer. For all inputs x where
  // static_cast<IntOut>(x) is legal according to the C++ standard, the result
  // is identical to that cast (i.e. the result is x with its fractional part
  // truncated whenever that is representable as IntOut).
  //
  // static_cast would cause undefined behavior for the following cases, which
  // have well-defined behavior for this function:
  //
  //  1. If x is NaN, the result is zero.
  //
  //  2. If the truncated form of x is above the representable range of IntOut,
  //     the result is std::numeric_limits<IntOut>::max().
  //
  //  3. If the truncated form of x is below the representable range of IntOut,
  //     the result is std::numeric_limits<IntOut>::lowest().
  //
  // Note that cases #2 and #3 cover infinities as well as finite numbers.
  template <class IntOut, class FloatIn>
  static IntOut SafeCast(FloatIn x) {
    static_assert(std::numeric_limits<IntOut>::is_specialized &&
                  std::numeric_limits<IntOut>::is_integer);
    static_assert(std::numeric_limits<FloatIn>::is_specialized &&
                  !std::numeric_limits<FloatIn>::is_integer);
    static_assert(std::numeric_limits<FloatIn>::radix == 2);

    // Special case NaN, for which the logic below doesn't work.
    if (std::isnan(x)) {
      return 0;
    }

    // We want to determine if there are any finite values of `FloatIn` that
    // need to be clamped to the range of `IntOut`.  The max value of
    // `IntOut` is `2 ** int_digits - 1`.  The max finite value of `FloatIn`
    // is `2.0 ** max_exponent - 2.0 ** (max_exponent - float_digits)`.
    // Therefore, we need to clamp some `FloatIn` values if
    // `max_exponent > int_digits`.  (This does not hold for double-`double`s
    // like POWER64 `long double`s, so this should be carefully examined
    // if we need to support those.)
    //
    // If the max exponent is at least the number of float digits, then the
    // largest finite `FloatIn` value is an integer.  This is the case for
    // all known floating point types, even fp4/fp8 formats.
    static_assert(std::numeric_limits<FloatIn>::max_exponent >=
                  std::numeric_limits<FloatIn>::digits);
    if constexpr (std::numeric_limits<FloatIn>::max_exponent >
                  std::numeric_limits<IntOut>::digits) {
      // This branch is the common case.  `2 ** int_digits` can be represented
      // exactly by `FloatIn`.

      // The inclusive lower bound for `x` is 0 for unsigned types and
      // `-2 ** int_digits` for signed types.  These are both representable
      // in `FloatIn`.
      constexpr FloatIn kInclusiveLowerBound =
          FloatIn{std::numeric_limits<IntOut>::lowest()};
      // The exclusive upper bound for `x` is `2 ** int_digits` which is
      // representable in `FloatIn`.
      constexpr FloatIn kExclusiveUpperBound =
          FloatIn{IntOut{1} << (std::numeric_limits<IntOut>::digits - 1)} *
          FloatIn{2};
      if (x >= kExclusiveUpperBound) {
        return std::numeric_limits<IntOut>::max();
      }
      if (x < kInclusiveLowerBound) {
        return std::numeric_limits<IntOut>::lowest();
      }
    } else {
      // This branch is for `float -> uint128` and `_Float16 -> int32`,
      // for example.

      // Negative values all clip to zero for unsigned results.
      if (!std::numeric_limits<IntOut>::is_signed && x < 0) {
        return 0;
      }

      // Handle infinities.
      if (std::isinf(x)) {
        return x < 0 ? std::numeric_limits<IntOut>::lowest()
                     : std::numeric_limits<IntOut>::max();
      }
    }

    return static_cast<IntOut>(x);
  }

  // --------------------------------------------------------------------
  // SafeRound
  //   These functions round a floating-point number to an integer.
  //   Results are identical to Round, except in cases where
  //   the argument is NaN, or when the rounded value would overflow the
  //   return type. In those cases, Round has undefined
  //   behavior. SafeRound returns 0 when the argument is
  //   NaN, and returns the closest possible integer value otherwise (i.e.
  //   std::numeric_limits<IntOut>::max() for large positive values, and
  //   std::numeric_limits<IntOut>::lowest() for large negative values).
  //   The range of FloatIn must include the range of IntOut, otherwise
  //   the results are undefined.
  // --------------------------------------------------------------------
  template <class IntOut, class FloatIn>
  static IntOut SafeRound(FloatIn x) {
    static_assert(!std::numeric_limits<FloatIn>::is_integer,
                  "FloatIn is integer");
    static_assert(std::numeric_limits<IntOut>::is_integer,
                  "IntOut is not integer");

    // When x is NaN, std::round returns NaN as well, which then is handled by
    // SafeCast.
    return SafeCast<IntOut>(std::round(x));
  }

  // --------------------------------------------------------------------
  // FastIntRound, FastInt64Round
  //   Deprecated aliases for `Round<int32_t>` and `Round<int64_t>`.  Call
  //   those functions directly.
  //   --------------------------------------------------------------------
  static int32_t FastIntRound(float x);
  static int32_t FastIntRound(double x);
  static int64_t FastInt64Round(float x);
  static int64_t FastInt64Round(double x);

  // Return Not a Number.
  ABSL_DEPRECATE_AND_INLINE()
  static double NaN() { return std::numeric_limits<double>::quiet_NaN(); }

  // the sine cardinal function
  static double Sinc(double x) {
    if (std::fabs(x) < 1E-8) return 1.0;
    return std::sin(x) / x;
  }

  // Returns an approximation An for the n-th element of the harmonic
  // series Hn = 1 + ... + 1/n.  Sets error e such that |An-Hn| < e.
  static double Harmonic(int64_t n, double* e);

  // Returns Stirling's Approximation for log(n!) which has an error
  // of at worst 1/(1260*n^5).
  static double Stirling(double n);

  // Returns the log of the binomial coefficient C(n, k), known in the
  // vernacular as "N choose K".  Why log?  Because the integer number
  // for non-trivial N and K would overflow.
  // Note that if k > 15, this uses Stirling's approximation of log(n!).
  // The relative error is about 1/(1260*k^5) (which is 7.6e-10 when k=16).
  static double LogCombinations(int n, int k);

  // Rounds "f" to the nearest float which has its last "bits" bits of
  // the mantissa set to zero.  This rounding will introduce a
  // fractional error of at most 2^(bits - 24).  Useful for values
  // stored in compressed files, when super-accurate numbers aren't
  // needed and the random-looking low-order bits foil compressors.
  // This routine should be really fast when inlined with "bits" set
  // to a constant.
  // Precondition: 1 <= bits <= 23, f != NaN
  static float RoundOffBits(const float f, const int bits) {
    const int32_t f_rep = absl::bit_cast<int32_t>(f);

    // Set low-order "bits" bits to zero.
    int32_t g_rep = f_rep & ~((1 << bits) - 1);

    // Round mantissa up if we need to.  Note that we do round-to-even,
    // a.k.a. round-up-if-odd.
    const int32_t lowbits = f_rep & ((1 << bits) - 1);
    if (lowbits > (1 << (bits - 1)) ||
        (lowbits == (1 << (bits - 1)) && (f_rep & (1 << bits)))) {
      g_rep += (1 << bits);
      // NOTE: overflow does a really nice thing here - if all the
      // rest of the mantissa bits are 1, the carry carries over into
      // the exponent and increments it by 1, which is exactly what we
      // want.  It even gets to +/-INF properly.
    }
    return absl::bit_cast<float>(g_rep);
  }
  // Same, but for doubles.  1 <= bits <= 52, error at most 2^(bits - 53).
  static double RoundOffBits(const double f, const int bits) {
    const int64_t f_rep = absl::bit_cast<int64_t>(f);
    int64_t g_rep = f_rep & ~((1LL << bits) - 1);
    const int64_t lowbits = f_rep & ((1LL << bits) - 1);
    if (lowbits > (1LL << (bits - 1)) ||
        (lowbits == (1LL << (bits - 1)) && (f_rep & (1LL << bits)))) {
      g_rep += (1LL << bits);
    }
    return absl::bit_cast<double>(g_rep);
  }

  // Largest of two values.
  // Works correctly for special floating point values.
  // Note: 0.0 and -0.0 are not differentiated by Max (Max(0.0, -0.0) is -0.0),
  // which should be OK because, although they (can) have different
  // bit representation, they are observably the same when examined
  // with arithmetic and (in)equality operators.
  template <typename T>
  static T Max(const T x, const T y) {
    return std::isnan(x) || x > y ? x : y;
  }

  // Smallest of two values
  // Works correctly for special floating point values.
  // Note: 0.0 and -0.0 are not differentiated by Min (Min(-0.0, 0.0) is 0.0),
  // which should be OK: see the comment for Max above.
  template <typename T>
  static T Min(const T x, const T y) {
    return std::isnan(x) || x < y ? x : y;
  }

  // Absolute value of x
  // Works correctly for unsigned types and
  // for special floating point values.
  // Note: 0.0 and -0.0 are not differentiated by Abs (Abs(0.0) is -0.0),
  // which should be OK: see the comment for Max above.
  template <typename T>
  static T Abs(const T x) {
    return x > T(0) ? x : -x;
  }

  // The sign of x
  // (works for unsigned types and special floating point values as well):
  //   -1 if x < 0,
  //   +1 if x > 0,
  //    0 if x = 0.
  //  nan if x is nan.
  template <typename T>
  static T Sign(const T x) {
    return std::isnan(x) ? x : (x == T(0) ? T(0) : (x > T(0) ? T(1) : T(-1)));
  }

  // Returns the square of x
  template <typename T>
  static T Square(const T x) {
    return x * x;
  }

  // Absolute value of the difference between two floating point numbers.
  // Works correctly for special floating point values.  Only use this
  // in dependent template contexts where `T` may be either floating point or
  // integral.  If it is always floating point, just use `std::abs(x - y)`.
  template <typename T,
            std::enable_if_t<std::is_floating_point_v<T>, bool> = true>
  ABSL_DEPRECATE_AND_INLINE()
  static T AbsDiff(const T x, const T y) {
    return std::abs(x - y);
  }

  // Absolute value of the difference between two signed or unsigned integers.
  // The return type is always unsigned.
  template <typename T,
            std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>,
                             bool> = true>
  static std::make_unsigned_t<T> AbsDiff(const T x, const T y) {
    // Carries out arithmetic as unsigned to avoid overflow.
    using R = std::make_unsigned_t<T>;
    return x > y ? R(x) - R(y) : R(y) - R(x);
  }

  // CAVEAT: Floating point computation instability for x86 CPUs
  // can frequently stem from the difference of when floating point values
  // are transferred from registers to memory and back,
  // which can depend simply on the level of optimization.
  // The reason is that the registers use a higher-precision representation.
  // Hence, instead of relying on approximate floating point equality below
  // it might be better to reorganize the code with volatile modifiers
  // for the floating point variables so as to control when
  // the loss of precision occurs.

  // If two (usually floating point) numbers are within a certain
  // absolute margin of error.
  // NOTE: this "misbehaves" if one is trying to capture provisons for errors
  // that are relative, i.e. larger if the numbers involved are larger.
  // Consider using WithinFraction or WithinFractionOrMargin below.
  //
  // This and other Within* NearBy* functions below
  // work correctly for signed types and special floating point values.
  template <typename T>
  static bool WithinMargin(const T x, const T y, const T margin) {
    DCHECK_GE(margin, 0);
    // this is a little faster than x <= y + margin  &&  x >= y - margin
    return AbsDiff(x, y) <= margin;
  }

  // If two (usually floating point) numbers are within a certain
  // fraction of their magnitude. Returns false if either number is not finite.
  // CAVEAT: Although this works well if computation errors are relative
  // both for large magnitude numbers > 1 and for small magnitude numbers < 1,
  // zero is never within a fraction of any
  // non-zero finite number (fraction is required to be < 1).
  template <typename T>
  static bool WithinFraction(T x, T y, T fraction);

  // If two (usually floating point) numbers are within a certain
  // fraction of their magnitude or within a certain absolute margin of error.
  // This is the same as the following but faster:
  //   WithinFraction(x, y, fraction)  ||  WithinMargin(x, y, margin)
  // E.g. WithinFraction(0.0, 1e-10, 1e-5) is false but
  //      WithinFractionOrMargin(0.0, 1e-10, 1e-5, 1e-5) is true.
  template <typename T>
  static bool WithinFractionOrMargin(T x, T y, T fraction, T margin);

  // NearBy* functions below are geared as replacements for CHECK_EQ()
  // over floating-point numbers.

  // Same as WithinMargin(x, y, MathLimits<T>::kStdError)
  // Works as == for integer types.
  template <typename T>
  static bool NearByMargin(const T x, const T y) {
    static_assert(std::is_arithmetic_v<T>);
    if constexpr (std::numeric_limits<T>::is_integer) {
      return x == y;
    } else {
      return AbsDiff(x, y) <= MathLimits<T>::kStdError;
    }
  }

  // Same as WithinFraction(x, y, MathLimits<T>::kStdError)
  // Works as == for integer types.
  template <typename T>
  static bool NearByFraction(const T x, const T y) {
    static_assert(std::is_arithmetic_v<T>);
    if constexpr (std::numeric_limits<T>::is_integer) {
      return x == y;
    } else {
      return WithinFraction(x, y, MathLimits<T>::kStdError);
    }
  }

  // Same as WithinFractionOrMargin(x, y, MathLimits<T>::kStdError,
  //                                      MathLimits<T>::kStdError)
  // Works as == for integer types.
  template <typename T>
  static bool NearByFractionOrMargin(const T x, const T y) {
    static_assert(std::is_arithmetic_v<T>);
    if constexpr (std::numeric_limits<T>::is_integer) {
      return x == y;
    } else {
      return WithinFractionOrMargin(x, y, MathLimits<T>::kStdError,
                                    MathLimits<T>::kStdError);
    }
  }

  // Tests whether two values are close enough to each other to be considered
  // equal. This function is intended to be used mainly as a replacement for
  // equality tests of floating point values in CHECK()s, and as a replacement
  // for equality comparison against golden files. It is the same as == for
  // integer types. The purpose of AlmostEquals() is to avoid false positive
  // error reports in unit tests and regression tests due to minute differences
  // in floating point arithmetic (for example, due to a different compiler).
  //
  // We cannot use simple equality to compare floating point values
  // because floating point expressions often accumulate inaccuracies, and
  // new compilers may introduce further variations in the values.
  //
  // Two values x and y are considered "almost equals" if:
  // (a) Both values are very close to zero: x and y are in the range
  //     [-standard_error, standard_error]
  //     Normal calculations producing these values are likely to be dealing
  //     with meaningless residual values.
  // -or-
  // (b) The difference between the values is small:
  //     abs(x - y) <= standard_error
  // -or-
  // (c) The values are finite and the relative difference between them is
  //     small:
  //     abs (x - y) <= standard_error * max(abs(x), abs(y))
  // -or-
  // (d) The values are both positive infinity or both negative infinity.
  //
  // Cases (b) and (c) are the same as MathUtils::NearByFractionOrMargin(x, y).
  //
  // standard_error is the corresponding MathLimits<T>::kStdError constant.
  // It is equivalent to 5 bits of mantissa error. See util/math/mathlimits.cc.
  //
  // Caveat:
  // AlmostEquals() is not appropriate for checking long sequences of
  // operations where errors may cascade (like extended sums or matrix
  // computations), or where significant cancellation may occur
  // (e.g., the expression (x+y)-x, where x is much larger than y).
  // Both cases may produce errors in excess of standard_error.
  // In any case, you should not test the results of calculations which have
  // not been vetted for possible cancellation errors and the like.
  template <typename T>
  static bool AlmostEquals(const T x, const T y) {
    static_assert(std::is_arithmetic_v<T>);
    if constexpr (std::numeric_limits<T>::is_integer) {
      return x == y;
    } else {
      if (x == y)  // Covers +inf and -inf, and is a shortcut for finite values.
        return true;

      if (MathUtil::Abs<T>(x) <= MathLimits<T>::kStdError &&
          MathUtil::Abs<T>(y) <= MathLimits<T>::kStdError)
        return true;

      return MathUtil::NearByFractionOrMargin<T>(x, y);
    }
  }

  template <typename T>
  ABSL_DEPRECATE_AND_INLINE()
  ABSL_MUST_USE_RESULT
      static const T& Clamp(const absl::type_identity_t<T>& low,
                            const absl::type_identity_t<T>& high,
                            const absl::type_identity_t<T>& value) {
    return std::clamp<T>(value, low, high);
  }

  // S-curve interpolation per https://en.wikipedia.org/wiki/Smoothstep.
  // Returns 0 for values beyond the low threshold.
  // Returns 1 for values beyond the high threshold.
  // Smoothly interpolates between 0 and 1 for values in-between the thresholds.
  // The two endpoints shouldn't be equal (low != high), but their ordering is
  // not restricted (low > high works).
  template <typename FloatType>
  static FloatType Smoothstep(FloatType low, FloatType high, FloatType value) {
    static_assert(std::is_floating_point_v<FloatType>,
                  "MathUtil::Smoothstep arguments must be floating point type");
    DCHECK_NE(low, high);
    const FloatType x =
        std::clamp((value - low) / (high - low), FloatType{0}, FloatType{1});
    return x * x * (3 - 2 * x);
  }

  // Functions for converting between degrees and radians.
  // Note: no clamping is done on the output.
  static constexpr double DegToRad(const double angle_degrees) {
    return angle_degrees * (kPi / 180.0);
  }

  static constexpr double RadToDeg(const double angle_radians) {
    return angle_radians * (180.0 / kPi);
  }

  // Re-normalizes an angle to be centred around a given target.
  // This is useful if you want to test if two angles match.
  // The centre parameter specifies the 'focal' region of the range.
  // This is useful for doing absolute comparisons, as it can avoid false
  // negatives when comparing values that are near 0 and near 360 respectively.
  static inline double NormalizeDegrees(const double angle_degrees,
                                        const double centre = 0.0) {
    return Wrap(angle_degrees, centre - 180.0, centre + 180.0);
  }

  // Same as NormalizeDegrees, except for angles in expressed in radians
  static inline double NormalizeRadians(const double angle_radians,
                                        const double centre = 0.0) {
    return Wrap(angle_radians, centre - kPi, centre + kPi);
  }

  // Decomposes `value` to the form `mantissa * pow(2, exponent)`.  Similar to
  // `std::frexp`, but returns `mantissa` as an integer without normalization.
  //
  // The returned `mantissa` might be a power of 2; this method does not shift
  // the trailing 0 bits out.
  //
  // If `value` is inf, then `mantissa = kint64max` and `exponent = intmax`.
  // If `value` is -inf, then `mantissa = -kint64max` and `exponent = intmax`.
  // If `value` is NaN, then `mantissa = 0` and `exponent = intmax`.
  // If `value` is 0, then `mantissa = 0` and `exponent < 0`.
  //
  // For all cases, `value` is equivalent to
  // `static_cast<double>(mantissa) * std::ldexp(1.0, exponent)`, though the
  // bits might differ (e.g., `-0.0` vs `0.0`, signaling NaN vs quiet NaN).
  //
  // For all cases except NaN,
  // `value = std::ldexp(static_cast<double>(mantissa), exponent)`.
  struct FloatParts {
    int32_t mantissa;
    int exponent;
  };
  static FloatParts Decompose(float value);
  struct DoubleParts {
    int64_t mantissa;
    int exponent;
  };
  static DoubleParts Decompose(double value);

  // Computes v^i, where i is a non-negative integer.
  // When T is a floating point type, this has the same semantics as pow(), but
  // is much faster.
  // T can also be any integral type, in which case computations will be
  // performed in the value domain of this integral type, and overflow semantics
  // will be those of T.
  // You can also use any type for which operator*= is defined.
  template <typename T>
  static T IPow(T base, int exp) {
    DCHECK_GE(exp, 0);
    uint32_t uexp = static_cast<uint32_t>(exp);

    if (uexp < 16) {
      T result = (uexp & 1) ? base : static_cast<T>(1);
      if (uexp >= 2) {
        base *= base;
        if (uexp & 2) {
          result *= base;
        }
        if (uexp >= 4) {
          base *= base;
          if (uexp & 4) {
            result *= base;
          }
          if (uexp >= 8) {
            base *= base;
            result *= base;
          }
        }
      }
      return result;
    }

    T result = base;
    ABSL_ASSUME(uexp != 0);
    int count = 32 - absl::bit_width(uexp);

    uexp <<= count;
    count ^= 31;

    while (count--) {
      uexp <<= 1;
      result *= result;
      if (uexp >= 0x80000000) {
        result *= base;
      }
    }

    return result;
  }

  static inline uint64_t IPow10(int exponent) {
    ABSL_CACHELINE_ALIGNED static constexpr uint64_t kPowersOfTen[]{
        1ULL,
        10ULL,
        100ULL,
        1000ULL,
        10000ULL,
        100000ULL,
        1000000ULL,
        10000000ULL,
        100000000ULL,
        1000000000ULL,
        10000000000ULL,
        100000000000ULL,
        1000000000000ULL,
        10000000000000ULL,
        100000000000000ULL,
        1000000000000000ULL,
        10000000000000000ULL,
        100000000000000000ULL,
        1000000000000000000ULL,
        10000000000000000000ULL};
    assert(exponent >= 0 && exponent <= 19);
    return kPowersOfTen[exponent];
  }

 private:
  // `DCHECK`-fails if the rounded value of `x` does not fit in `IntOut`.
  template <class IntOut, class FloatIn>
  static void DCheckFloatIsInRange(FloatIn x);

  // Wraps `x` to the periodic range `[low, high)`
  static double Wrap(double x, double low, double high);

  // Checks whether the square of `x` fits in IntegralType.
  template <typename IntegralType>
  static bool CanSquare(IntegralType x);

  // Computes (q, r) = divmod(k*N, D) for 0 < k <= N < D.
  // Assumes that (k-1)^2 fits in IntegralType.
  template <typename IntegralType>
  static DivisionResult<IntegralType> MulDiv_SmallK(IntegralType k,
                                                    IntegralType n,
                                                    IntegralType d);

  // Computes (q, r) = divmod(k*N, D) for 0 < k <= N < D.
  // Works without overflow for any values of k, N, and D.
  template <typename IntegralType>
  static DivisionResult<IntegralType> MulDiv_LargeK(IntegralType k,
                                                    IntegralType n,
                                                    IntegralType d);

  // Computes (q, r) = divmod(k*N, D) for 0 < k <= N < D.
  // Dispatches to the optimal method given the size of k.
  template <typename IntegralType>
  static DivisionResult<IntegralType> MulDiv_Optimal(IntegralType k,
                                                     IntegralType n,
                                                     IntegralType d);
};

// ========================================================================= //

#ifdef __aarch64__

// SafeRound

template <>
inline int32_t MathUtil::SafeRound(float x) {
  return vcvtas_s32_f32(x);
}

template <>
inline uint32_t MathUtil::SafeRound(float x) {
  return vcvtas_u32_f32(x);
}

// `__asm__` syntax may be different for other compilers.
#ifdef __GNUC__

template <>
inline int32_t MathUtil::SafeRound(double x) {
  // There is no `vcvtad_s32_f64`.
  int32_t result;
  __asm__("fcvtas %w0, %d1" : "=r"(result) : "w"(x));
  return result;
}

template <>
inline uint32_t MathUtil::SafeRound(double x) {
  // There is no `vcvtad_u32_f64`.
  uint32_t result;
  __asm__("fcvtau %w0, %d1" : "=r"(result) : "w"(x));
  return result;
}

template <>
inline int64_t MathUtil::SafeRound(float x) {
  // This could be done with `vcvtad_s64_f64(double{x})`, but that generates
  // an extra instruction, so use inline asm.
  int64_t result;
  __asm__("fcvtas %x0, %s1" : "=r"(result) : "w"(x));
  return result;
}

template <>
inline uint64_t MathUtil::SafeRound(float x) {
  uint64_t result;
  __asm__("fcvtau %x0, %s1" : "=r"(result) : "w"(x));
  return result;
}

#endif  // defined(__GNUC__)

template <>
inline int64_t MathUtil::SafeRound(double x) {
  return vcvtad_s64_f64(x);
}

template <>
inline uint64_t MathUtil::SafeRound(double x) {
  return vcvtad_u64_f64(x);
}

// SafeCast

template <>
inline int32_t MathUtil::SafeCast(float x) {
  return vcvts_s32_f32(x);
}

template <>
inline uint32_t MathUtil::SafeCast(float x) {
  return vcvts_u32_f32(x);
}

#ifdef __GNUC__

template <>
inline int32_t MathUtil::SafeCast(double x) {
  // There is no `vcvts_s32_f64`.
  int32_t result;
  __asm__("fcvtzs %w0, %d1" : "=r"(result) : "w"(x));
  return result;
}

template <>
inline uint32_t MathUtil::SafeCast(double x) {
  // There is no `vcvts_u32_f64`.
  uint32_t result;
  __asm__("fcvtzu %w0, %d1" : "=r"(result) : "w"(x));
  return result;
}

template <>
inline int64_t MathUtil::SafeCast(float x) {
  int64_t result;
  __asm__("fcvtzs %x0, %s1" : "=r"(result) : "w"(x));
  return result;
}

template <>
inline uint64_t MathUtil::SafeCast(float x) {
  uint64_t result;
  __asm__("fcvtzu %x0, %s1" : "=r"(result) : "w"(x));
  return result;
}

#endif  // defined(__GNUC__)

template <>
inline int64_t MathUtil::SafeCast(double x) {
  return vcvtd_s64_f64(x);
}

template <>
inline uint64_t MathUtil::SafeCast(double x) {
  return vcvtd_u64_f64(x);
}

#endif  // __aarch64__

// `Round` specializations using SSE2 instructions.  These routines are
// approximately 3 times faster than the default implementation of
// `Round<int32_t>` on Intel Skylake and AMD Milan processors.
#if defined __SSE2__

template <>
inline int32_t MathUtil::Round(double x) {
  DCheckFloatIsInRange<int32_t>(x);
  return _mm_cvtsd_si32(_mm_set_sd(x));
}

template <>
inline int32_t MathUtil::Round(float x) {
  DCheckFloatIsInRange<int32_t>(x);
  return _mm_cvtss_si32(_mm_set_ss(x));
}

#ifdef __x86_64__
template <>
inline int64_t MathUtil::Round(double x) {
  DCheckFloatIsInRange<int64_t>(x);
  // `_mm_cvtsd_si64` is only available for __x86_64__ targets.
  return _mm_cvtsd_si64(_mm_set_sd(x));
}
#endif  // __x86_64__

#elif defined __GNUC__ && defined __i386__

template <>
inline int32_t MathUtil::Round(double x) {
  DCheckFloatIsInRange<int32_t>(x);
  // FPU stack.  Adapted from /usr/include/bits/mathinline.h.
  int32_t result;
  __asm__ __volatile__("fistpl %0"
                       : "=m"(result)  // Output operand is a memory location
                       : "t"(x)        // Input operand is top of FP stack
                       : "st");        // Clobbers (pops) top of FP stack
  return result;
}

template <>
inline int32_t MathUtil::Round(float x) {
  DCheckFloatIsInRange<int32_t>(x);
  // FPU stack.  Adapted from /usr/include/bits/mathinline.h.
  int32_t result;
  __asm__ __volatile__("fistpl %0"
                       : "=m"(result)  // Output operand is a memory location
                       : "t"(x)        // Input operand is top of FP stack
                       : "st");        // Clobbers (pops) top of FP stack
  return result;
}

template <>
inline int64_t MathUtil::Round(double x) {
  DCheckFloatIsInRange<int64_t>(x);
  // FPU stack.  Adapted from /usr/include/bits/mathinline.h.
  int64_t result;
  __asm__ __volatile__("fistpll %0"
                       : "=m"(result)  // Output operand is a memory location
                       : "t"(x)        // Input operand is top of FP stack
                       : "st");        // Clobbers (pops) top of FP stack
  return result;
}

#elif defined __aarch64__

// The generic `Round` generates the same code as of 2024-08.  These
// specializations are here to guard against compiler regressions.

template <>
inline int32_t MathUtil::Round(double x) {
  DCheckFloatIsInRange<int32_t>(x);
  return static_cast<int32_t>(vcvtnd_s64_f64(x));
}

template <>
inline int32_t MathUtil::Round(float x) {
  DCheckFloatIsInRange<int32_t>(x);
  return vcvtns_s32_f32(x);
}

template <>
inline int64_t MathUtil::Round(double x) {
  DCheckFloatIsInRange<int64_t>(x);
  // Floating-point convert to signed integer, rounding to nearest with
  // ties to even.
  return vcvtnd_s64_f64(x);
}

#endif

// These must be defined after the specializations of Round.
ABSL_DEPRECATE_AND_INLINE()
inline int32_t MathUtil::FastIntRound(float x) { return Round<int32_t>(x); }
ABSL_DEPRECATE_AND_INLINE()
inline int32_t MathUtil::FastIntRound(double x) { return Round<int32_t>(x); }

ABSL_DEPRECATE_AND_INLINE()
inline int64_t MathUtil::FastInt64Round(float x) {
  return Round<int64_t>(double{x});
}
ABSL_DEPRECATE_AND_INLINE()
inline int64_t MathUtil::FastInt64Round(double x) { return Round<int64_t>(x); }

template <class IntOut, class FloatIn>
void MathUtil::DCheckFloatIsInRange(FloatIn x) {
  // `std::round` rounds away from zero.  We conservatively reject half-way
  // cases that would be too large or too small.  This should always be
  // `std::round`, even if we switch to `roundeven` in the actual rounding
  // functions.
  const FloatIn rounded = std::round(x);

  DCHECK(std::isfinite(rounded)) << x;
  if constexpr (std::numeric_limits<FloatIn>::max_exponent >
                std::numeric_limits<IntOut>::digits) {
    // This branch is the common case.  `2 ** int_digits` can be represented
    // exactly by `FloatIn`.

    // The inclusive lower bound for `rounded` is 0 for unsigned types and
    // `-2 ** int_digits` for signed types.  These are both representable
    // in `FloatIn`.
    constexpr FloatIn kInclusiveLowerBound =
        FloatIn{std::numeric_limits<IntOut>::lowest()};
    // The exclusive upper bound for `rounded` is `2 ** int_digits` which is
    // representable in `FloatIn`.  C++26 has `constexpr` `std::pow`, so we
    // can switch to that when available.
    constexpr FloatIn kExclusiveUpperBound =
        FloatIn{IntOut{1} << (std::numeric_limits<IntOut>::digits - 1)} *
        FloatIn{2};
    DCHECK_LE(kInclusiveLowerBound, rounded) << x;
    DCHECK_LT(rounded, kExclusiveUpperBound) << x;
  } else {
    // This branch is for `float -> uint128` and `_Float16 -> int32`,
    // for example.

    // We only need to check for negative values when converting to unsigned,
    // otherwise everything fits.  (Infinity/NaN were checked above.)
    if constexpr (!std::numeric_limits<IntOut>::is_signed) {
      DCHECK_GE(rounded, FloatIn{0}) << x;
    }
  }
}

template <typename T>
bool MathUtil::WithinFraction(const T x, const T y, const T fraction) {
  // not just "0 <= fraction" to fool the compiler for unsigned types
  DCHECK((0 < fraction || 0 == fraction) && fraction < 1);

  // Avoid a compiler warning about a potential integer overflow in crosstool
  // v12 (gcc 4.3.1).
  if constexpr (std::numeric_limits<T>::is_integer) {
    return x == y;
  } else {
    return (std::isfinite(x) && std::isfinite(y)) &&
           (AbsDiff(x, y) <= fraction * Max(Abs(x), Abs(y)));
  }
}

template <typename T>
bool MathUtil::WithinFractionOrMargin(const T x, const T y, const T fraction,
                                      const T margin) {
  // Not just "0 <= fraction" to fool the compiler for unsigned types.
  DCHECK((T(0) < fraction || T(0) == fraction) && fraction < T(1) &&
         margin >= T(0));

  // Avoid a compiler warning about a potential integer overflow in crosstool
  // v12 (gcc 4.3.1).
  if constexpr (std::numeric_limits<T>::is_integer) {
    return x == y;
  } else {
    if (!std::isfinite(x) || !std::isfinite(y)) {
      return false;
    }
    const T relative_margin = static_cast<T>(fraction * Max(Abs(x), Abs(y)));
    return AbsDiff(x, y) <= Max(margin, relative_margin);
  }
}

// ---- CeilOrFloorOfRatio ----
// This is a branching-free, cast-to-double-free implementation.
//
// Casting to double is in general incorrect because of loss of precision
// when casting an int64_t into a double.
//
// There's a bunch of 'recipes' to compute a integer ceil (or floor) on the web,
// and most of them are incorrect.
template <typename IntegralType, bool ceil>
IntegralType MathUtil::CeilOrFloorOfRatio(IntegralType numerator,
                                          IntegralType denominator) {
  static_assert(std::numeric_limits<IntegralType>::is_integer,
                "CeilOfRatio is only defined for integral types");
  DCHECK_NE(IntegralType(0), denominator)
      << "Division by zero is not supported.";
  DCHECK(!std::numeric_limits<IntegralType>::is_signed ||
         numerator != std::numeric_limits<IntegralType>::lowest() ||
         denominator != IntegralType(-1))
      << "Dividing " << numerator << "by -1 is not supported: it would SIGFPE";

  const IntegralType rounded_toward_zero(numerator / denominator);
  const bool needs_round = (numerator % denominator) != IntegralType(0);
  // It is important to use >= here, even for the denominator, to ensure that
  // this value is a compile-time constant for unsigned types.
  const bool same_sign =
      (numerator >= IntegralType(0)) == (denominator >= IntegralType(0));

  if constexpr (ceil) {
    return rounded_toward_zero +
           static_cast<IntegralType>(same_sign && needs_round);
  } else {
    return rounded_toward_zero -
           static_cast<IntegralType>(!same_sign && needs_round);
  }
}

// ---- CeilOrFloorOfScaledRatio ----
// This is a cast-to-double-free implementation.
//
// Casting to double is in general incorrect because of loss of precision
// when casting an int64_t into a double.
template <typename IntegralType, bool ceil>
IntegralType MathUtil::CeilOrFloorOfScaledRatio(IntegralType numerator,
                                                IntegralType denominator,
                                                IntegralType scale_factor) {
  static_assert(std::numeric_limits<IntegralType>::is_integer,
                "MulDiv is only defined for integral types");
  const auto result =
      MulDiv<IntegralType>(scale_factor, numerator, denominator);
  if constexpr (ceil) {
    return result.quotient + (result.remainder > IntegralType(0));
  } else {
    return result.quotient;
  }
}

template <typename IntegralType>
MathUtil::DivisionResult<IntegralType> MathUtil::MulDiv(IntegralType a,
                                                        IntegralType b,
                                                        IntegralType d) {
  static_assert(std::numeric_limits<IntegralType>::is_integer,
                "MulDiv is only defined for integral types");
  DCHECK_NE(IntegralType(0), d) << "Division by zero is not supported.";
  DCHECK_LT(IntegralType(0), d) << "Negative numbers are not supported.";
  DCHECK_LE(IntegralType(0), a) << "Negative numbers are not supported.";
  DCHECK_LE(IntegralType(0), b) << "Negative numbers are not supported.";

  IntegralType q(b * (a / d));
  a %= d;
  q += a * (b / d);
  b %= d;
  const IntegralType k(std::min(a, b));
  if (k == IntegralType(0)) {
    return {.quotient = q, .remainder = IntegralType(0)};
  }
  const IntegralType n(std::max(a, b));

  auto result = MulDiv_Optimal<IntegralType>(k, n, d);
  result.quotient += q;
  return result;
}

template <typename IntegralType>
MathUtil::DivisionResult<IntegralType> MathUtil::MulDiv_Optimal(
    IntegralType k, IntegralType n, IntegralType d) {
  DCHECK_GT(k, 0) << "This method assumes k is positive.";
  DCHECK_LE(k, n) << "This method assumes N is not smaller than k.";
  DCHECK_GT(d, 0) << "Negative and zero denominators are not supported.";

  // Dispatch to the most optimal implementation.
#if ABSL_HAVE_BUILTIN(__builtin_mul_overflow)
  IntegralType numerator(0);
  if (!__builtin_mul_overflow(k, n, &numerator)) {
    return {.quotient = static_cast<IntegralType>(numerator / d),
            .remainder = static_cast<IntegralType>(numerator % d)};
  }
#endif  // ABSL_HAVE_BUILTIN(__builtin_mul_overflow)
  if (CanSquare<IntegralType>(k - 1)) {
    return MulDiv_SmallK(k, n, d);
  }
  return MulDiv_LargeK(k, n, d);
}

template <typename IntegralType>
MathUtil::DivisionResult<IntegralType> MathUtil::MulDiv_SmallK(IntegralType k,
                                                               IntegralType n,
                                                               IntegralType d) {
  DCHECK_GT(k, IntegralType(0)) << "This method assumes k is positive.";
  DCHECK_LE(k, n) << "This method assumes N is not smaller than k.";
  DCHECK_GT(d, IntegralType(0))
      << "Negative and zero denominators are not supported.";
  DCHECK(CanSquare<IntegralType>(k - 1))
      << "k is too large for this method to support.";

  // We first approximate the ratio from below: k*N / D ~= N / ceil(D/k).
  // Taking D' = ceil(D/k), we let (P', R') = divmod(N, D'), so N = P'D' + R'.
  const IntegralType d_prime(CeilOfRatio(d, k));
  const IntegralType p_prime(n / d_prime);
  const IntegralType r_prime(n % d_prime);

  // We note that
  //   kN/D = P' + (kN - P'D)/D
  //        = P' + (P'(kD' - D) + kR') / D,
  // and reduce our problem to finding divmod(P'(kD' - D) + kR', D), which we
  // compute by combining those of its two terms to avoid overflow on addition.
  MathUtil::DivisionResult<IntegralType> result = {.quotient = p_prime,
                                                   .remainder = 0};

  // kD' can overflow, but k(D' - 1) < D, so we instead compute
  //   kD' - D = k(D' - 1) + k - D
  //           = k - (D - k(D' - 1)).
  // Clearly, kD' - D < k, and P' < k by construction, so
  //   P'(kD' - D) <= (k-1)^2.
  // By our assumption that (k-1)^2 fits in IntegralType, the product can't
  // overflow.
  const IntegralType t1(p_prime * (k - (d - k * (d_prime - 1))));
  result.quotient += t1 / d;
  const IntegralType r1(t1 % d);

  // kR' can't overflow, since R' <= D' - 1 <= floor(D/k).
  const IntegralType t2(k * r_prime);
  result.quotient += t2 / d;
  const IntegralType r2(t2 % d);

  // We compute R_1 + R_2 - D as R_1 - (D - R_2) to avoid potential overflow.
  if (r1 < d - r2) {
    result.remainder = r1 + r2;
  } else {
    result.quotient++;
    result.remainder = r1 - (d - r2);
  }
  return result;
}

template <typename IntegralType>
MathUtil::DivisionResult<IntegralType> MathUtil::MulDiv_LargeK(IntegralType k,
                                                               IntegralType n,
                                                               IntegralType d) {
  DCHECK_GT(k, IntegralType(0)) << "This method assumes k is positive.";
  DCHECK_LE(k, n) << "This method assumes N is not smaller than k.";
  DCHECK_GT(d, IntegralType(0))
      << "Negative and zero denominators are not supported.";

  if (k == IntegralType(1)) {
    return {.quotient = IntegralType(0), .remainder = n};
  }

  const IntegralType kp(k >> 1);
  auto result = MulDiv_Optimal(kp, n, d);
  // Double k: add [q, r] into itself.
  result.quotient <<= 1;
  if (result.remainder >= d - result.remainder) {
    result.remainder -= (d - result.remainder);
    result.quotient++;
  } else {
    result.remainder <<= 1;
  }
  if ((k & 1) != IntegralType(0)) {
    // Adding 1 to k: add [0, n] into [q, r].
    if (result.remainder >= d - n) {
      result.remainder -= d - n;
      result.quotient++;
    } else {
      result.remainder += n;
    }
  }
  return result;
}

inline double MathUtil::Wrap(double x, double low, double high) {
  DCHECK_LT(low, high);
  const double range = high - low;
  // Avoid `floor` when `x` is within `[low - range, high + range)`.
  if (x < low) x += range;
  if (x >= high) x -= range;
  if (ABSL_PREDICT_TRUE(low <= x && x < high)) return x;
  return x - range * std::floor((x - low) / range);
}

template <typename IntegralType>
inline bool MathUtil::CanSquare(IntegralType x) {
#if ABSL_HAVE_BUILTIN(__builtin_mul_overflow)
  IntegralType t;
  return !__builtin_mul_overflow(x, x, &t);
#else   // !ABSL_HAVE_BUILTIN(__builtin_mul_overflow)
  return x <= std::sqrt(std::numeric_limits<IntegralType>::max());
#endif  // !ABSL_HAVE_BUILTIN(__builtin_mul_overflow)
}

#endif  // THIRD_PARTY_GLOOP_UTIL_MATH_MATHUTIL_H_
