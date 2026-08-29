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

#include "gloop/util/math/mathutil.h"

#include <stdlib.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>

#include "absl/base/casts.h"
#include "absl/log/check.h"

constexpr double MathUtil::kPi;

bool MathUtil::RealRootsForCubic(long double const a, long double const b,
                                 long double const c, long double* const r1,
                                 long double* const r2, long double* const r3) {
  // According to Numerical Recipes (pp. 184-5), what
  // follows is an arrangement of computations to
  // compute the roots of a cubic that minimizes
  // roundoff error (as pointed out by A.J. Glassman).

  long double const a_squared = a * a, a_third = a / 3.0, b_tripled = 3.0 * b;
  long double const Q = (a_squared - b_tripled) / 9.0;
  long double const R =
      (2.0 * a_squared * a - 3.0 * a * b_tripled + 27.0 * c) / 54.0;

  long double const R_squared = R * R;
  long double const Q_cubed = Q * Q * Q;

  if (R_squared < Q_cubed) {
    long double const root_Q = std::sqrt(Q);
    long double const two_pi_third = 2.0 * M_PI / 3.0;
    long double const theta_third = std::acos(R / std::sqrt(Q_cubed)) / 3.0;
    long double const minus_two_root_Q = -2.0 * root_Q;

    *r1 = minus_two_root_Q * std::cos(theta_third) - a_third;
    *r2 = minus_two_root_Q * std::cos(theta_third + two_pi_third) - a_third;
    *r3 = minus_two_root_Q * std::cos(theta_third - two_pi_third) - a_third;

    return true;
  }

  long double const A =
      -sgn(R) *
      std::pow(std::abs(R) + std::sqrt(R_squared - Q_cubed), 1.0 / 3.0L);

  if (A != 0.0) {  // in which case, B from NR is zero
    *r1 = A + Q / A - a_third;
    return false;
  }

  *r1 = *r2 = *r3 = -a_third;
  return true;
}

namespace {

inline bool IsCloseToZero(long double x) {
  static const long double kEqnEPS = 1e-9;
  return x > -kEqnEPS && x < kEqnEPS;
}

}  // namespace

int MathUtil::RealRootsForQuartic(long double a, long double b, long double c,
                                  long double d, long double* roots) {
  // This is converted from code in Graphics Gems 1,
  // Schwarze, Jochen, Cubic and Quartic Roots, p. 404-407, code: p. 738-786.
  int number_of_roots = 0;

  // Converts to normal form: x^4 + Ax^3 + Bx^2 + Cx + D = 0.

  // Substitutes x = y - a/4 to eliminate cubic term: x^4 + px^2 + qx + r = 0.

  const long double sq_A = a * a;
  const long double p = -(3.0 / 8) * sq_A + b;
  const long double q = (1.0 / 8) * sq_A * a - 0.5 * a * b + c;
  const long double r =
      -(3.0 / 256) * sq_A * sq_A + (1.0 / 16) * sq_A * b - 0.25 * a * c + d;

  if (IsCloseToZero(r)) {
    // Handles case where there is no absolute term: y(y^3 + py + q) = 0.
    if (RealRootsForCubic(0, p, q, roots, roots + 1, roots + 2)) {
      number_of_roots = 3;
    } else {
      number_of_roots = 1;
    }
  } else {
    RealRootsForCubic(-0.5 * p, -r, 0.5 * r * p - (1.0 / 8) * q * q, roots,
                      roots + 1, roots + 2);

    // Take one of the real roots...if there are multiple roots it doesn't seem
    // to matter which one is taken.
    const long double z = roots[0];

    long double u = z * z - r;

    if (IsCloseToZero(u))
      u = 0.0;
    else if (u > 0)
      u = std::sqrt(u);
    else
      return 0;

    long double v = 2.0 * z - p;

    if (IsCloseToZero(v))
      v = 0.0;
    else if (v > 0)
      v = std::sqrt(v);
    else
      return 0;

    number_of_roots = static_cast<int>(
        RealRootsForQuadratic(1.0, q < 0 ? -v : v, z - u, roots, roots + 1));

    number_of_roots += static_cast<int>(RealRootsForQuadratic(
        1.0, q < 0 ? v : -v, z + u, roots + number_of_roots,
        roots + number_of_roots + 1));
  }

  // Resubstitutes into original equation.
  const long double sub = 0.25 * a;

  for (int i = 0; i < number_of_roots; ++i) {
    roots[i] -= sub;
  }
  return number_of_roots;
}

// Returns the greatest common divisor of two unsigned integers x and y,
// and assigns a, and b such that a*x + b*y = gcd(x, y).
unsigned int MathUtil::ExtendedGCD(unsigned int x, unsigned int y, int* a,
                                   int* b) {
  int64_t local_a = 1;
  int64_t local_b = 0;
  int64_t c = 0;
  int64_t d = 1;
  // before and after each loop:
  // current_x == local_a * original_x + local_b * original_y
  // current_y == c * original_x + d * original_y
  while (y != 0) {
    int64_t quot = x / y;
    unsigned int rem = x % y;
    x = y;
    y = rem;

    int64_t tmp = c;
    c = local_a - quot * c;
    local_a = tmp;

    tmp = d;
    d = local_b - quot * d;
    local_b = tmp;
  }

  DCHECK_GE(local_a, std::numeric_limits<int>::min());
  DCHECK_LE(local_a, std::numeric_limits<int>::max());
  DCHECK_GE(local_b, std::numeric_limits<int>::min());
  DCHECK_LE(local_b, std::numeric_limits<int>::max());

  *a = static_cast<int>(local_a);
  *b = static_cast<int>(local_b);
  return x;
}

double MathUtil::Harmonic(int64_t const n, double* const e) {
  CHECK_GT(n, 0);

  //   Hn ~ ln(n) + 0.5772156649 +
  //        + 1/(2n) - 1/(12n^2) + 1/(120n^4) - error,
  //   with 0 < error < 1/(256*n^4).

  double const d = static_cast<double>(n), d2 = d * d, d4 = d2 * d2;

  return (log(d) + 0.5772156649)  // ln + Gamma constant
         + 1 / (2 * d) - 1 / (12 * d2) + 1 / (120 * d4) - (*e = 1 / (256 * d4));
}

// The formula is extracted from the following page
// http://en.wikipedia.org/w/index.php?title=Stirling%27s_approximation
double MathUtil::Stirling(double n) {
  static const double kLog2Pi = log(2 * M_PI);
  const double logN = log(n);
  return (n * logN - n + 0.5 * (kLog2Pi + logN)  // 0.5 * log(2 * M_PI * n)
          + 1 / (12 * n) - 1 / (360 * n * n * n));
}

double MathUtil::LogCombinations(int n, int k) {
  CHECK_GE(n, k);
  CHECK_GE(n, 0);
  CHECK_GE(k, 0);

  // use symmetry to pick the shorter calculation
  if (k > n / 2) {
    k = n - k;
  }

  // If we have more than 30 logarithms to calculate, we'll use
  // Stirling's approximation for log(n!).
  if (k > 15) {
    return Stirling(n) - Stirling(k) - Stirling(n - k);
  } else {
    double result = 0;
    for (int i = 1; i <= k; i++) {
      result +=
          log(static_cast<double>(n - k + i)) - log(static_cast<double>(i));
    }
    return result;
  }
}

namespace {

template <typename R, typename F, typename Rep>
R DecomposeImpl(const F value) {
  using Limits = std::numeric_limits<F>;
  using Mantissa = decltype(R{}.mantissa);
  using Exponent = decltype(R{}.exponent);
  // The leading one-bit in 1.xxxxxx is not stored.
  constexpr int kMantissaBits = Limits::digits - 1;
  constexpr Rep kSignMask = ~(std::numeric_limits<Rep>::max() >> 1);
  constexpr Rep kMantissaMask = (Rep{1} << kMantissaBits) - 1;
  static_assert(Limits::is_iec559,
                "Floating point must follow IEC 559 (IEEE 754) standard.");
  static_assert(Limits::has_denorm == std::denorm_present,
                "Floating point must support denormal values.");
  static_assert(sizeof(F) * 8 - kMantissaBits <= sizeof(Exponent) * 8,
                "Exponent bits do not fit in Exponent's type");

  if (!std::isfinite(value)) {
    const int exponent = std::numeric_limits<Exponent>::max();
    if (value == Limits::infinity()) {
      return {std::numeric_limits<Mantissa>::max(), exponent};
    }
    if (value == -Limits::infinity()) {
      return {-std::numeric_limits<Mantissa>::max(), exponent};
    }
    // value is NaN.
    return {0, exponent};
  }

  const Rep bits = absl::bit_cast<Mantissa>(value);

  R parts;
  parts.mantissa = static_cast<Mantissa>(bits & kMantissaMask);
  parts.exponent = static_cast<Exponent>((bits & ~kSignMask) >> kMantissaBits);
  if (parts.exponent > 0) {
    // The value is normal, and represented as
    //   1.(mantissa bits) * pow(2, exponent - exponent_bias)
    // We need to add the implicit leading one-bit to mantissa.
    // The exponent offset is different from the denormal case by one,
    // so we account for it here as well.
    --parts.exponent;
    parts.mantissa |= (kMantissaMask + 1);
  }
  // Else, value is 0 or denormal.
  // The value is 0.(mantissa bits) * pow(2, 0 - (exponent_bias - 1)).

  if ((bits & kSignMask) != 0) {
    parts.mantissa = -parts.mantissa;
  }
  parts.exponent -= Limits::digits - Limits::min_exponent;
  return parts;
}

}  // namespace

MathUtil::FloatParts MathUtil::Decompose(float value) {
  return DecomposeImpl<FloatParts, float, uint32_t>(value);
}

MathUtil::DoubleParts MathUtil::Decompose(double value) {
  return DecomposeImpl<DoubleParts, double, uint64_t>(value);
}
