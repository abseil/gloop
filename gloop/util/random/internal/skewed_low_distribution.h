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

#ifndef THIRD_PARTY_GLOOP_UTIL_RANDOM_INTERNAL_SKEWED_LOW_DISTRIBUTION_H_
#define THIRD_PARTY_GLOOP_UTIL_RANDOM_INTERNAL_SKEWED_LOW_DISTRIBUTION_H_

#include <cassert>
#include <cmath>
#include <istream>
#include <limits>
#include <ostream>

#include "absl/random/internal/iostream_state_saver.h"
#include "absl/random/internal/traits.h"
#include "absl/random/uniform_int_distribution.h"

namespace util_random {

// util_random::skewed_low_distribution
//
// Returns a random variate R in range [min, max]. The values are selected
// from the set of uniform distributions with the boundaries [min, min +
// base^0), [min, min + base^1), [min, min + base^2), ... [min, min + (base^n)),
// and finally [min, max].
template <typename IntType = int>
class skewed_low_distribution {
 private:
  using unsigned_type =
      typename absl::random_internal::make_unsigned_bits<IntType>::type;

 public:
  using result_type = IntType;

  class param_type {
   public:
    using distribution_type = skewed_low_distribution;

    explicit param_type(
        result_type min = 0,
        result_type max = (std::numeric_limits<result_type>::max)(),
        result_type base = 2);

    result_type(min)() const { return min_; }
    result_type(max)() const { return max_; }
    result_type base() const { return base_; }

    friend bool operator==(const param_type& a, const param_type& b) {
      return a.min_ == b.min_ && a.max_ == b.max_ && a.base_ == b.base_;
    }

    friend bool operator!=(const param_type& a, const param_type& b) {
      return !(a == b);
    }

   private:
    friend class skewed_low_distribution;

    unsigned_type range() const { return range_; }
    int log_range() const { return log_range_; }

    result_type min_;
    result_type max_;
    result_type base_;
    unsigned_type range_;  // max - min
    int log_range_;        // ceil(logN(range_))

    static_assert(absl::random_internal::IsIntegral<IntType>::value,
                  "Class-template absl::skewed_low_distribution<> must be "
                  "parameterized using an integral type.");
  };

  skewed_low_distribution() : skewed_low_distribution(0) {}

  explicit skewed_low_distribution(
      result_type min,
      result_type max = (std::numeric_limits<result_type>::max)(),
      result_type base = 2)
      : param_(min, max, base) {}

  explicit skewed_low_distribution(const param_type& p) : param_(p) {}

  void reset() {}

  // Generating functions
  template <typename URNG>
  result_type operator()(URNG& g) {  // NOLINT(runtime/references)
    return (*this)(g, param_);
  }

  template <typename URNG>
  result_type operator()(URNG& g,  // NOLINT(runtime/references)
                         const param_type& p) {
    return static_cast<result_type>((p.min)() + Generate(g, p));
  }

  param_type param() const { return param_; }
  void param(const param_type& p) { param_ = p; }

  result_type(min)() const { return (param_.min)(); }
  result_type(max)() const { return (param_.max)(); }
  result_type base() const { return param_.base(); }

  friend bool operator==(const skewed_low_distribution& a,
                         const skewed_low_distribution& b) {
    return a.param_ == b.param_;
  }
  friend bool operator!=(const skewed_low_distribution& a,
                         const skewed_low_distribution& b) {
    return a.param_ != b.param_;
  }

 private:
  // Generates returns an unsigned value in the range [0, p.range()],
  // distributed according to the log boundaries.
  template <typename URNG>
  unsigned_type Generate(URNG& g,  // NOLINT(runtime/references)
                         const param_type& p);

  param_type param_;
};

// --------------------------------------------------------------------------
// Implementation details follow
// --------------------------------------------------------------------------

template <typename IntType>
skewed_low_distribution<IntType>::param_type::param_type(result_type min,
                                                         result_type max,
                                                         result_type base)
    : min_(min),
      max_(max),
      base_(base),
      range_(static_cast<unsigned_type>(max) - static_cast<unsigned_type>(min)),
      log_range_(0) {
  assert(max_ >= min_);
  assert(base_ > 1);

  if (base_ == 2) {
    // Determine where the first set bit is on range(), giving a log2(range)
    // value which can be used to construct bounds.
    log_range_ = absl::random_internal::BitWidth(range());
  } else {
    // NOTE: Computing the logN(x) introduces error from 2 sources:
    // 1. Conversion of int to double loses precision for values >=
    // 2^53, which may cause some log() computations to operate on
    // different values.
    // 2. The error introduced by the division will cause the result
    // to differ from the expected value.
    //
    // Thus a result which should equal K may equal K +/- epsilon,
    // which can skew the results.
    const double inv_log_base = 1.0 / std::log(static_cast<double>(base_));
    const double log_range = std::log(static_cast<double>(range()) + 0.5);
    log_range_ = static_cast<int>(std::ceil(inv_log_base * log_range));
  }
}

template <typename IntType>
template <typename URBG>
typename skewed_low_distribution<IntType>::unsigned_type
skewed_low_distribution<IntType>::Generate(
    URBG& g,  // NOLINT(runtime/references)
    const param_type& p) {
  // Sample e over [0, log_range]. Map the results of e to this:
  // 0 => [0, b^0)
  // 1 => [0, b^1)
  // 2 => [0, b^2)
  // n => [0, b^n)
  const int e = absl::uniform_int_distribution<int>(0, p.log_range())(g);
  if (e == 0) {
    return 0;
  }

  unsigned_type base_e = 1;
  if (p.base() == 2) {
    base_e = (e >= std::numeric_limits<unsigned_type>::digits)
                 ? (std::numeric_limits<unsigned_type>::max)()
                 : (static_cast<unsigned_type>(1) << e) - 1;
  } else {
    const double r = std::pow(static_cast<double>(p.base()), e) - 1.0;
    base_e =
        (r > static_cast<double>((std::numeric_limits<unsigned_type>::max)()))
            ? (std::numeric_limits<unsigned_type>::max)()
            : static_cast<unsigned_type>(r);
  }

  const unsigned_type hi = (base_e >= p.range()) ? p.range() : base_e;

  // Choose uniformly over [0, hi]
  return absl::uniform_int_distribution<unsigned_type>(0, hi)(g);
}

template <typename CharT, typename Traits, typename IntType>
std::basic_ostream<CharT, Traits>& operator<<(
    std::basic_ostream<CharT, Traits>& os,  // NOLINT(runtime/references)
    const skewed_low_distribution<IntType>& x) {
  using stream_type =
      typename absl::random_internal::stream_format_type<IntType>::type;
  auto saver = absl::random_internal::make_ostream_state_saver(os);
  os << static_cast<stream_type>((x.min)()) << os.fill()
     << static_cast<stream_type>((x.max)()) << os.fill()
     << static_cast<stream_type>(x.base());
  return os;
}

template <typename CharT, typename Traits, typename IntType>
std::basic_istream<CharT, Traits>& operator>>(
    std::basic_istream<CharT, Traits>& is,  // NOLINT(runtime/references)
    skewed_low_distribution<IntType>& x) {  // NOLINT(runtime/references)
  using param_type = typename skewed_low_distribution<IntType>::param_type;
  using result_type = typename skewed_low_distribution<IntType>::result_type;
  using stream_type =
      typename absl::random_internal::stream_format_type<IntType>::type;

  stream_type min;
  stream_type max;
  stream_type base;

  auto saver = absl::random_internal::make_istream_state_saver(is);
  is >> min >> max >> base;
  if (!is.fail()) {
    x.param(param_type(static_cast<result_type>(min),
                       static_cast<result_type>(max),
                       static_cast<result_type>(base)));
  }
  return is;
}

}  // namespace util_random

#endif  // THIRD_PARTY_GLOOP_UTIL_RANDOM_INTERNAL_SKEWED_LOW_DISTRIBUTION_H_
