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

#ifndef THIRD_PARTY_GLOOP_UTIL_RANDOM_INTERNAL_SMALL_PRIME_DISTRIBUTION_H_
#define THIRD_PARTY_GLOOP_UTIL_RANDOM_INTERNAL_SMALL_PRIME_DISTRIBUTION_H_

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <limits>
#include <optional>
#include <ostream>
#include <type_traits>

#include "absl/log/log.h"
#include "absl/numeric/int128.h"
#include "absl/random/internal/iostream_state_saver.h"
#include "absl/random/uniform_int_distribution.h"

namespace util_random {
namespace small_prime_distribution_internal {

// Helper template for Miller-Rabin primality test.
// Only supports uint32_t and uint64_t.
template <typename T, size_t N>
bool MillerRabinIsPrime(T n, const T (&bases)[N]) {
  using WideT =
      std::conditional_t<std::is_same_v<T, uint64_t>, absl::uint128, uint64_t>;

  if (n < 2) return false;
  if (n == 2 || n == 3) return true;
  if (n % 2 == 0) return false;

  T d = n - 1;
  int s = 0;
  while (d % 2 == 0) {
    d /= 2;
    s++;
  }

  for (T a : bases) {
    if (a % n == 0) continue;

    // Modular exponentiation using the wider integer type WideT to avoid
    // multiplication overflow.
    uint64_t base = a % n;
    uint64_t exp = d;
    uint64_t x = 1;
    while (exp > 0) {
      if (exp % 2 == 1) {
        x = static_cast<uint64_t>(static_cast<WideT>(x) * base % n);
      }
      base = static_cast<uint64_t>(static_cast<WideT>(base) * base % n);
      exp /= 2;
    }

    if (x == 1 || x == n - 1) continue;

    bool composite = true;
    for (int r = 1; r < s; r++) {
      x = static_cast<uint64_t>(static_cast<WideT>(x) * x % n);
      if (x == n - 1) {
        composite = false;
        break;
      }
    }
    if (composite) return false;
  }
  return true;
}

// Deterministic Miller-Rabin Primality Test for primality.
inline bool IsPrime(uint64_t n) {
  if (n >> 32) {
    // 64-bit integers (7 bases)
    static const uint64_t bases[] = {
        2, 325, 9375, 28178, 450775, 9780504, 1795265022,
    };
    return MillerRabinIsPrime(n, bases);
  } else {
    // 32-bit integers (3 bases)
    static const uint32_t bases[] = {2, 7, 61};
    return MillerRabinIsPrime(static_cast<uint32_t>(n), bases);
  }
}

template <typename T>
inline std::optional<T> FindNextPrime(T x, T inclusive_max) {
  if (x <= 2) {
    return inclusive_max >= 2 ? std::optional<T>(2) : std::optional<T>();
  }
  x |= 1;
  for (; x <= inclusive_max; x += 2) {
    if (IsPrime(x)) return x;
  }
  return std::nullopt;
}

}  // namespace small_prime_distribution_internal

// util_random::small_prime_distribution
//
// A random distribution that generates prime numbers uniformly in the closed
// interval [min, max].
//
// Selection uses the Miller-Rabin primality test with rejection sampling,
// which typically requires multiple iterations.
// When the range is smaller than about 2^15, and if efficiency is critical,
// it would likely be more efficient to enumerate all the prime values in the
// range and selecting one randomly.
template <typename IntType = int>
class small_prime_distribution {
 public:
  using result_type = IntType;

  class param_type {
   public:
    using distribution_type = small_prime_distribution;

    explicit param_type(
        result_type min = 2,
        result_type max = (std::numeric_limits<result_type>::max)())
        : min_(min), max_(max), checked_min_(0) {
      assert(min_ >= 2);
      assert(max_ >= min_);
      // The largest gap between primes in the 64-bit range is 1550.
      // https://math.stackexchange.com/questions/1701960/largest-prime-gap-under-264
      if (max_ - min_ < 1551) {
        auto next_prime =
            small_prime_distribution_internal::FindNextPrime(min_, max_);
        checked_min_ = next_prime.value_or(0);
      }
    }

    result_type(min)() const { return min_; }
    result_type(max)() const { return max_; }

    friend bool operator==(const param_type& a, const param_type& b) {
      return a.min_ == b.min_ && a.max_ == b.max_;
    }

    friend bool operator!=(const param_type& a, const param_type& b) {
      return !(a == b);
    }

   private:
    friend class small_prime_distribution;

    result_type min_;
    result_type max_;
    result_type checked_min_;

    static_assert(std::is_integral_v<IntType>,
                  "Class-template util_random::small_prime_distribution<> "
                  "must be parameterized using an integral type.");
    static_assert(sizeof(IntType) <= sizeof(uint64_t),
                  "Class-template util_random::small_prime_distribution<> "
                  "only supports integral types up to 64 bits.");
  };

  explicit small_prime_distribution(result_type min) : param_(min) {}
  explicit small_prime_distribution(const param_type& p) : param_(p) {}

  small_prime_distribution() : param_() {}
  small_prime_distribution(result_type min, result_type max)
      : param_(min, max) {}

  void reset() {}

  // Generating functions
  template <typename URNG>
  result_type operator()(URNG& g) {  // NOLINT(runtime/references)
    return (*this)(g, param_);
  }

  template <typename URNG>
  result_type operator()(URNG& g,  // NOLINT(runtime/references)
                         const param_type& p);

  param_type param() const { return param_; }
  void param(const param_type& p) { param_ = p; }

  result_type(min)() const { return (param_.min)(); }
  result_type(max)() const { return (param_.max)(); }

  friend bool operator==(const small_prime_distribution& a,
                         const small_prime_distribution& b) {
    return a.param_ == b.param_;
  }
  friend bool operator!=(const small_prime_distribution& a,
                         const small_prime_distribution& b) {
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
typename small_prime_distribution<IntType>::result_type
small_prime_distribution<IntType>::operator()(
    URBG& g,  // NOLINT(runtime/references)
    const param_type& p) {
  IntType lo = p.checked_min_ != 0 ? p.checked_min_ : (p.min)();
  if ((p.max)() - (p.min)() < 1551 && p.checked_min_ == 0) {
    LOG(FATAL) << "No primes in the specified range [" << p.min() << ", "
               << p.max() << "]";
  }

  absl::uniform_int_distribution<result_type> dis(lo, (p.max)());
  while (true) {
    result_type candidate = dis(g);
    if (small_prime_distribution_internal::IsPrime(
            static_cast<uint64_t>(candidate))) {
      return candidate;
    }
  }
}

template <typename CharT, typename Traits, typename IntType>
std::basic_ostream<CharT, Traits>& operator<<(
    std::basic_ostream<CharT, Traits>& os,  // NOLINT(runtime/references)
    const small_prime_distribution<IntType>& x) {
  using stream_type =
      typename absl::random_internal::stream_format_type<IntType>::type;
  auto saver = absl::random_internal::make_ostream_state_saver(os);
  os << static_cast<stream_type>((x.min)()) << os.fill()
     << static_cast<stream_type>((x.max)());
  return os;
}

template <typename CharT, typename Traits, typename IntType>
std::basic_istream<CharT, Traits>& operator>>(
    std::basic_istream<CharT, Traits>& is,   // NOLINT(runtime/references)
    small_prime_distribution<IntType>& x) {  // NOLINT(runtime/references)
  using param_type = typename small_prime_distribution<IntType>::param_type;
  using result_type = typename small_prime_distribution<IntType>::result_type;
  using stream_type =
      typename absl::random_internal::stream_format_type<IntType>::type;

  stream_type min;
  stream_type max;

  auto saver = absl::random_internal::make_istream_state_saver(is);
  is >> min >> max;
  if (!is.fail()) {
    x.param(param_type(static_cast<result_type>(min),
                       static_cast<result_type>(max)));
  }
  return is;
}

}  // namespace util_random

#endif  // THIRD_PARTY_GLOOP_UTIL_RANDOM_INTERNAL_SMALL_PRIME_DISTRIBUTION_H_
