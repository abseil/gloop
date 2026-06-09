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

// -----------------------------------------------------------------------------
// File: distributions.h
// -----------------------------------------------------------------------------
//
// This header defines functions representing distributions, which you use in
// combination with an Abseil random bit generator to produce random values
// according to the rules of that distribution.
//
//   * `util_random::SkewedLow` for discrete probability distributions skewed
//      based on the skewed_low_distribution type.
//   * `util_random::YuleSimon` for discrete probability distributions skewed
//      based on the yule_simon_distribution type.
//   * `util_random::SmallPrime` for discrete probability distributions
//      generating prime numbers based on the small_prime_distribution type.
//
// Prefer use of these distribution function classes over manual construction of
// your own distribution classes, as it allows library maintainers greater
// flexibility to change the underlying implementation in the future.

#ifndef THIRD_PARTY_GLOOP_UTIL_RANDOM_DISTRIBUTIONS_H_
#define THIRD_PARTY_GLOOP_UTIL_RANDOM_DISTRIBUTIONS_H_

#include <limits>

#include "absl/meta/type_traits.h"
#include "absl/random/internal/distribution_caller.h"  // IWYU pragma: export
#include "absl/random/internal/traits.h"
#include "gloop/util/random/internal/skewed_low_distribution.h"
#include "gloop/util/random/internal/small_prime_distribution.h"
#include "gloop/util/random/internal/yule_simon_distribution.h"

namespace util_random {

// -----------------------------------------------------------------------------
// util_random::SkewedLow<T>(urbg, lo, hi, base = 2)
// -----------------------------------------------------------------------------
//
// Produces an integral number according to the skewed_low_distribution in
// the closed interval [lo..hi]. `T` must be an integral type, but may be
// inferred from the types of `lo` and `hi`.
//
// Requires:
// * `hi` must be greater than or equal to `lo`.
// * `base` must be greater than 1.
//
// Example:
//
//   util_random::SharedBitGen bitgen;
//   ...
//   int v = util_random::SkewedLow(bitgen, 0, 1000);
//
template <typename IntType, typename URBG>
IntType SkewedLow(URBG&& urbg,  // NOLINT(runtime/references)
                  IntType lo, IntType hi, IntType base = 2) {
  static_assert(absl::random_internal::IsIntegral<IntType>::value,
                "Template-argument 'IntType' must be an integral type, in "
                "util_random::SkewedLow<IntType, URBG>(...)");

  using gen_t = absl::decay_t<URBG>;
  using distribution_t = util_random::skewed_low_distribution<IntType>;

  return absl::random_internal::DistributionCaller<gen_t>::template Call<
      distribution_t>(&urbg, lo, hi, base);
}

// -----------------------------------------------------------------------------
// util_random::YuleSimon<T>(bitgen, a = 2)
// -----------------------------------------------------------------------------
//
// `util_random::YuleSimon` produces discrete probabilities skewed based on an
// existing prior distribution. `T` must be an integral type.
//
// Requires:
// * `a` must be greater than 1.0.
//
// See https://en.wikipedia.org/wiki/Yule%E2%80%93Simon_distribution
//
// Example:
//
//   absl::Bitgen bitgen;
//   ...
//   int v = util_random::YuleSimon<int>(bitgen);
//
template <typename IntType, typename URBG>
IntType YuleSimon(URBG&& urbg,  // NOLINT(runtime/references)
                  double a = 2.0) {
  static_assert(absl::random_internal::IsIntegral<IntType>::value,
                "Template-argument 'IntType' must be an integral type, in "
                "util_random::YuleSimon<IntType, URBG>(...)");

  using gen_t = absl::decay_t<URBG>;
  using distribution_t = util_random::yule_simon_distribution<IntType>;

  return absl::random_internal::DistributionCaller<gen_t>::template Call<
      distribution_t>(&urbg, a);
}

// -----------------------------------------------------------------------------
// util_random::SmallPrime<T>(urbg, lo, hi)
// -----------------------------------------------------------------------------
//
// Produces a "small" (up to 64 bit) prime number in the closed interval
// [lo..hi]. `T` must be an integral type, but may be inferred from the types of
// `lo` and `hi`.
//
// Requires:
// * `lo` must be greater than or equal to 2.
// * `hi` must be greater than or equal to `lo`.
// * the closed range [`lo`, `hi`] must contain at least one prime.
// * `T` must be an integral type up to 64 bits.
//
// Example:
//
//   util_random::SharedBitGen bitgen;
//   ...
//   int v = util_random::SmallPrime(bitgen, 2, 1000);
//
template <typename IntType, typename URBG>
IntType SmallPrime(URBG&& urbg, IntType lo = 2,
                   IntType hi = (std::numeric_limits<IntType>::max)()) {
  static_assert(absl::random_internal::IsIntegral<IntType>::value,
                "Template-argument 'IntType' must be an integral type, in "
                "util_random::SmallPrime<IntType, URBG>(...)");

  using gen_t = absl::decay_t<URBG>;
  using distribution_t = util_random::small_prime_distribution<IntType>;

  return absl::random_internal::DistributionCaller<gen_t>::template Call<
      distribution_t>(&urbg, lo, hi);
}

}  // namespace util_random

#endif  // THIRD_PARTY_GLOOP_UTIL_RANDOM_DISTRIBUTIONS_H_
