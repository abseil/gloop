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

#ifndef THIRD_PARTY_GLOOP_UTIL_RANDOM_INTERNAL_YULE_SIMON_DISTRIBUTION_H_
#define THIRD_PARTY_GLOOP_UTIL_RANDOM_INTERNAL_YULE_SIMON_DISTRIBUTION_H_

#include <cassert>
#include <cmath>
#include <istream>
#include <limits>
#include <ostream>

#include "absl/random/internal/iostream_state_saver.h"
#include "absl/random/internal/traits.h"
#include "absl/random/uniform_real_distribution.h"

namespace util_random {

// util_random::yule_simon_distribution: Generate a number conforming to a
// Yule-Simon distribution in the interval [0,
// std::numeric_limits<IntType>::max()/std::numeric_limits<IntType>::max()].
// This may be useful for modeling document word frequencies.
//
// The `a` parameter determines the skew.  Larger `a` values yield distributions
// skewed towards 0.  `a` must be greater than 1.0; the behavior is undefined
// for values less than or equal 1.0.  In general, for identical values of `a`,
// a Yule distribution will be to the right of a Zipf distribution, and have a
// longer tail.
template <typename IntType = int>
class yule_simon_distribution {
 public:
  using result_type = IntType;

  class param_type {
   public:
    using distribution_type = yule_simon_distribution;

    explicit param_type(double a = 2.0) : a_(a) { assert(a > 1.0); }

    double a() const { return a_; }

    friend bool operator==(const param_type& a, const param_type& b) {
      return a.a_ == b.a_;
    }

    friend bool operator!=(const param_type& a, const param_type& b) {
      return !(a == b);
    }

   private:
    double a_;

    static_assert(absl::random_internal::IsIntegral<IntType>::value,
                  "Class-template absl::yule_simon_distribution<> must be "
                  "parameterized using an integral type.");
  };

  yule_simon_distribution() : yule_simon_distribution(2.0) {}

  explicit yule_simon_distribution(double a) : param_(a) {}

  explicit yule_simon_distribution(const param_type& p) : param_(p) {}

  void reset() {}

  // generating functions
  template <typename URBG>
  result_type operator()(URBG& g) {  // NOLINT(runtime/references)
    return (*this)(g, param_);
  }

  template <typename URBG>
  result_type operator()(URBG& g,  // NOLINT(runtime/references)
                         const param_type& p);

  double a() const { return param_.a(); }

  param_type param() const { return param_; }
  void param(const param_type& p) { param_ = p; }

  result_type(min)() const { return 0; }
  result_type(max)() const { return (std::numeric_limits<result_type>::max)(); }

  friend bool operator==(const yule_simon_distribution& a,
                         const yule_simon_distribution& b) {
    return a.param_ == b.param_;
  }
  friend bool operator!=(const yule_simon_distribution& a,
                         const yule_simon_distribution& b) {
    return a.param_ != b.param_;
  }

 private:
  param_type param_;
};

// --------------------------------------------------------------------------
// Implementation details follow
// --------------------------------------------------------------------------

template <typename IntType>
template <typename URBG>
typename yule_simon_distribution<IntType>::result_type
yule_simon_distribution<IntType>::operator()(
    URBG& g, const param_type& p) {  // NOLINT(runtime/references)
  // The basic algorithm follows [6.3] of the book
  // Non-Uniform Random Variate Generation.
  //
  // N, E <- exponential random variates
  // X <- floor(N / log(1-exp(-E/alpha -1)))
  absl::uniform_real_distribution<double> gen;

  const double a1 = p.a() - 1.0;
  double r = 0;
  do {
    const double u = gen(g);
    if (u == 0.0) {
      // Returning 0 here skews the output slightly towards 0, by approximately
      // 1 in 2^60 calls, but otherwise we need logic to correct for NaN.
      return 0;
    }
    const double n = std::log(u);
    const double v = gen(g);
    const double e = (a1 == 1.0) ? v : std::exp(std::log(v) / a1);
    const double d = std::log1p(-e);

    // Result should be [0..max int)
    r = std::floor(n / d);
  } while (r > static_cast<double>((max)()));

  return static_cast<result_type>(r);
}

template <typename CharT, typename Traits, typename IntType>
std::basic_ostream<CharT, Traits>& operator<<(
    std::basic_ostream<CharT, Traits>& os,  // NOLINT(runtime/references)
    const yule_simon_distribution<IntType>& x) {
  auto saver = absl::random_internal::make_ostream_state_saver(os);
  os.precision(
      absl::random_internal::stream_precision_helper<double>::kPrecision);
  os << x.a();
  return os;
}

template <typename CharT, typename Traits, typename IntType>
std::basic_istream<CharT, Traits>& operator>>(
    std::basic_istream<CharT, Traits>& is,  // NOLINT(runtime/references)
    yule_simon_distribution<IntType>& x) {  // NOLINT(runtime/references)
  using param_type = typename yule_simon_distribution<IntType>::param_type;
  double a;

  auto saver = absl::random_internal::make_istream_state_saver(is);
  is >> a;
  if (!is.fail()) {
    x.param(param_type(a));
  }
  return is;
}

}  // namespace util_random

#endif  // THIRD_PARTY_GLOOP_UTIL_RANDOM_INTERNAL_YULE_SIMON_DISTRIBUTION_H_
