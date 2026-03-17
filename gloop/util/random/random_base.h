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

#ifndef THIRD_PARTY_GLOOP_UTIL_RANDOM_RANDOM_BASE_H_
#define THIRD_PARTY_GLOOP_UTIL_RANDOM_RANDOM_BASE_H_

#include <assert.h>

#include <cstdint>
#include <limits>
#include <string>

#include "absl/base/attributes.h"

// Provides a base class with common operations for random number
// generators.  This class does not include any routines that maintain
// any state information.
//
// This is derived from the SecureRandom code.
class RandomBase {
 public:
  // constructors.  Don't do too much.
  RandomBase() = default;
  virtual ~RandomBase() = default;

  // Clone: generate a direct copy of this pseudorandom number generator.
  // NB: Returns NULL if Clone is not implemented/available.
  virtual RandomBase* Clone() const = 0;

  // Generate pseudorandom output of various sizes.  Output must be
  // *uniformly* random for all possible values of the various output
  // sizes.  Some generators naturally output more than 8 bits at a
  // time, and have to buffer.  We leave these as virtual so that such
  // generators could output natural sizes if the request is for
  // greater than the natural size is requested, and draw from
  // buffered output for fractional output.  E.g., AES in counter mode
  // with key = seed would be one such generator.
  //
  // WARNING: ACMRandom does not obey the requirement above; e.g., it will never
  // return a 32-bit number with the top bit set.
  ABSL_DEPRECATED("Use absl::Uniform<uint8_t>(gen) instead.")
  virtual uint8_t Rand8() = 0;
  ABSL_DEPRECATED("Use absl::Uniform<uint16_t>(gen) instead.")
  virtual uint16_t Rand16() = 0;
  ABSL_DEPRECATED("Use absl::Uniform<uint32_t>(gen) instead.")
  virtual uint32_t Rand32() = 0;
  ABSL_DEPRECATED("Use absl::Uniform<uint64_t>(gen) instead.")
  virtual uint64_t Rand64() = 0;

  // Returns a string of random bytes of a given desired length,
  // constructed by invoking Rand8 repeatedly.
  //
  // Note, however, that for secure random number generators based on
  // block ciphers, extracting output from the generator one byte at a
  // time is somewhat inefficient.  This is a virtual function to
  // permit block-cipher--based implementations to override the
  // provided definition.
  ABSL_DEPRECATED("Use util_random::RandomBytes(&gen, desired_len) instead.")
  virtual std::string RandString(int desired_len);

  // Returns a uniformly distributed pseudorandom integer in [0, n)
  // where n >= 0.
  ABSL_DEPRECATED("Use absl::Uniform(gen, 0, n) instead.")
  virtual int32_t UnbiasedUniform(int32_t n);
  ABSL_DEPRECATED("Use absl::Uniform<uint64_t>(gen, 0, n) instead.")
  virtual uint64_t UnbiasedUniform64(uint64_t n);

  // ------------------------------------------------------------
  // General output utilities for all random number generators.

  // RandFloat: returns a uniformly distributed random float in the
  // range [0.0, 1.0), for the following notion of uniform: All
  // floating point numbers that are distinguishable within 2^-m where
  // m is the number of bits in the mantissa are uniformly generated.
  //
  // We generate numbers by creating pseudorandom numbers y where y is
  // uniform between [1.0, 2.0), that is, uniform probability for all
  // floating point number y satisfying 1.0 <= y < 2.0.  This is
  // "natural" with the implicit 1 in the mantissa.  To get
  // pseudorandom numbers in [0.0, 1.0), we simply set x = y - 1.0.
  // This means that some floating point numbers, e.g., 1.0e-40, will
  // never be output.
  ABSL_DEPRECATED("Use absl::Uniform<float>(gen, 0, 1) instead.")
  virtual float RandFloat();

  ABSL_DEPRECATED("Use absl::Uniform(gen, 0, 1.0) instead.")
  virtual double RandDouble();

  // RandExponential: Generate a random number conforming to an
  // exponential distribution with parameter lambda=1.0.
  // The output is in [0, +inf), and has mean 1.0.
  ABSL_DEPRECATED("Use absl::Exponential<double>(gen) instead.")
  double RandExponential();

  // Return a pseudorandom integer in [0, n). Warning: this method has a bias
  // toward low values for large n (above 2**20 or so), so UnbiasedUniform
  // should be preferred.
  ABSL_DEPRECATED("Use absl::Uniform<int32_t>(gen, 0, n) instead.")
  virtual int32_t Uniform(int32_t n) {
    assert(n >= 0);  // runtime check for negative modulus since % neg
                     // is not well defined and mod'ing by large
                     // values will not be uniform anyways.
    if (0 == n) {
      return Rand32() * 0;  // consume an output in any case
    } else {
      return Rand32() % n;
    }
  }

  // UniformFloat: return a pseudorandom float in [0,x] for positive x, or
  // [x,0] for negative x. The return value is undefined if x is not finite.
  // The end-point value x may or may not be included in the range depending
  // on esoteric details of rounding in floating point arithmetic.
  ABSL_DEPRECATED("Use absl::Uniform<float>(gen, 0, x) instead.")
  float UniformFloat(float x) { return RandFloat() * x; }

  // UniformDouble: return a pseudorandom double in [0,x] for positive x, or
  // [x,0] for negative x. The return value is undefined if x is not finite.
  // The end-point value x may or may not be included in the range depending
  // on esoteric details of rounding in floating point arithmetic.
  ABSL_DEPRECATED("Use absl::Uniform<double>(gen, 0, x) instead.")
  double UniformDouble(double x) { return RandDouble() * x; }

  // UniformFloat: return a pseudorandom float in [a,b] for a <= b, or [b,a]
  // for a > b. The return value is undefined if a - b is not finite.
  // The end-point value b may or may not be included in the range depending
  // on esoteric details of rounding in floating point arithmetic.
  ABSL_DEPRECATED("Use absl::Uniform<float>(gen, a, b) instead.")
  float UniformFloat(float a, float b) { return a + UniformFloat(b - a); }

  // UniformDouble: return a pseudorandom double in [a,b] for a <= b, or [b,a]
  // for a > b. The return value is undefined if a - b is not finite.
  // The end-point value b may or may not be included in the range depending
  // on esoteric details of rounding in floating point arithmetic.
  ABSL_DEPRECATED("Use absl::Uniform<double>(gen, a, b) instead.")
  double UniformDouble(double a, double b) { return a + UniformDouble(b - a); }

  // An implementation of STL's UniformRandomNumberGenerator concept.
  typedef uint32_t result_type;

  // Since C++11, the C++ Standard requires min() and max() to be compile-time
  // expressions, see [rand.req.urng].  That's not possible prior to C++11.
  static constexpr result_type min() { return 0; }
  static constexpr result_type max() {
    return std::numeric_limits<uint32_t>::max();
  }

  virtual result_type operator()() { return Rand32(); }

  // A functor-style version of Uniform, so a generator can be a model of
  // STL's RandomNumberGenerator concept.  Example usage:
  //   ACMRandom rand(FLAGS_random_seed);
  //   random_shuffle(myvec.begin(), myvec.end(), rand);
  ABSL_DEPRECATED("Use absl::Uniform<int32_t>(gen, 0, n) instead.")
  virtual int32_t operator()(int32_t n) { return Uniform(n); }

  ABSL_DEPRECATED("Use absl::Bernoulli(gen, 1.0 / X) instead.")
  bool OneIn(int X) { return Uniform(X) == 0; }

  // Skewed: pick "base" uniformly from range [0,max_log] and then
  // return "base" random bits.  The effect is to pick a number in the
  // range [0,2^max_log-1] with bias towards smaller numbers.
  //
  // This supports values of max_log up to 32.
  ABSL_DEPRECATED(
      "Use util_random::SkewedLow<uint32_t>(gen, 0, (1 << max_log) - 1) "
      "instead.")
  int32_t Skewed(int max_log) {
    const int32_t base = Rand32() % (max_log + 1);
    // this distribution differs slightly from ACMRandom's Skewed,
    // since 0 occurs approximately 3 times more than 1 here, and
    // ACMRandom's Skewed never outputs 0.
    const uint32_t mask = ((base < 32) ? (1u << base) : 0u) - 1u;
    return Rand32() & mask;
  }

  // Utility methods to generate weak seeds for various RNG
  // implementations. (IE. MTRandom, ACMRandom).
  ABSL_DEPRECATED("If seeding nondeterministically, use absl::BitGen.")
  static uint32_t WeakSeed32();

 protected:
  // Utility method to generate a weak seed string. Return value is the
  // number of bytes written to buffer. (Always less than or equal to length.)
  ABSL_DEPRECATED("If seeding nondeterministically, use absl::BitGen.")
  static int WeakSeed(uint8_t* buffer, int length);
};

#endif  // THIRD_PARTY_GLOOP_UTIL_RANDOM_RANDOM_BASE_H_
