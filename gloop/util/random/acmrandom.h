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
// ACM minimal standard random number generator.
// 1. Thoroughly tested in the literature
// 2. Period of 2^31-2
// 3. Fast: about 60 cycles per value
// 4. Compact: 4 bytes of state
// 5. Thread-compatible

#ifndef THIRD_PARTY_GLOOP_UTIL_RANDOM_ACMRANDOM_H_
#define THIRD_PARTY_GLOOP_UTIL_RANDOM_ACMRANDOM_H_

#include <cstdint>
#include <random>

#include "absl/base/attributes.h"
#include "gloop/util/random/random_base.h"

// DO NOT use ACMRandom for any application where security --
// unpredictability of subsequent output and previous output -- is
// needed.  ACMRandom is in *NO* *WAY* a cryptographically secure
// pseudorandom number generator, and using it where recipients of its
// output may wish to guess earlier/later output values would be very
// bad.  For cryptographically secure PRNGs see secure_random.h.
//
// MTRandom is usually a better choice of generator (but is still not a secure
// RNG!).  Except in cases of memory restriction, MTRandom is preferred over
// ACMRandom.  Only use ACMRandom if memory is extremely tight AND you are
// going to use a small number of samples.  (See the GDH entry on Pseudorandom
// Number Generators, currently at
// <link>Main/GdhPseudorandomNumberGenerators .)
//
// ACMRandom is a multiplicative generator and outputs A^k mod M,
// where A = 16807, M = 2^31-1.  A is a generator for Z_M^*.  So, (0)
// forms a 1-cycle (disallowed by Reset()), and (1, A, A^2, A^3, ...)
// forms an M-1 cycle, and the seed determines the location in this
// cycle from which the generator starts outputting values.
//
// BE VERY CAREFUL when using ACMRandom for applications where very
// good statistical randomness is required in extended monte carlo
// experiments.  While ACMRandom is completely uniform over its range
// of [1, 2^31-1), its (relatively) short cycle length of 2^31-2 can
// cause problems in some situations.
//
// On modern hardware (e.g., my 2.7GHz desktop), it takes only 96
// seconds to cycle through this entire range (measured by running
// "time arc4_perf_test -acmcycle"; cycle length can be checked by
// "arc4_perf_test -floyd"), so the kinds of monte carlo experiments
// that can exhaust the output cycle is quite feasible.  For these
// extended monte carlo experiments, this relatively short cycle
// length could have an effect on the results -- esp if the LCM of the
// number of Next() invocations per sample in the experiment and the
// cycle length is no greater than the cycle length, then running the
// monte carlo experiment for anything longer than one cycle of the
// generator is useless.  Since 2^31-2 = 2 3 3 7 11 31 151 331, so
// there are many small factors, which increases the odds of this
// happening.  If there are K invocations of Next() per experimental
// sample, the experiment cycles after LCM(K,M-1)/K samples.
//
// In particular Next64() is just two invocations of Next(), so it has
// a cycle length of LCM(2,2^31-2)/2 = 2^30-1, not anything like
// approximately 2^64 that one might otherwise expect.  (We could
// think of Next64() as an experiment where we use Next() to sample
// the space of 64-bit numbers.)
//
// The use of ACMRandom::Next64() to generate google cookies have
// resulted in problems in practice, so the output is biased and
// statistically distinguishable from a uniform random number
// generator.
//
// Other generators such as bsd's random() has a longer cycle length
// of 16*(2^31-1), which though longer, does not improve things that
// much wrt cycling in the space of experimental samples.
//
// See secure_random's ARC4Random class.  It is not a completely
// plug-compatible replacement -- in particular, the size of seed
// material required is larger -- but should be usable in lieu of
// ACMRandom with very minor changes.  ARC4Random does NOT duplicate
// some of the corner cases of ACMRandom, e.g., Rand32() could output
// zero.
//
// DO use ACMRandom for situations where extreme statistical quality
// is not important.  ACMRandom would be fine for some test codes,
// where we are measuring the performance of code that needs a random
// number generator but where we want to ignore the overhead of
// pseudorandom number generation (rather than measuring it separately
// and discounting it).
//
// 2004/06/18, bsy.

// This class is not a proper implementation of RandomBase.
// In particular, the high-order bit of Rand32() and the two high-order
// bits of Rand64() are always zero, and the methods do not return all
// possible values in the remaining bits.
//
// This class is thread-compatible: see <link> .
class ACMRandom : public RandomBase {
 public:
  // This class does not have a zero-argument constructor.
  // Do not add one.
  // <link>Main/FixAcmRandom

  // You must choose a seed.
  inline explicit ACMRandom(int32_t seed);

  // Seed initializer with determinism.
  static int32_t DeterministicSeed() { return 301; }

  // Seed initializer with some initial entropy.
  // Note that this just calls through to RandomBase::WeakSeed32() and is
  // retained for compatibility.
  ABSL_DEPRECATED(
      "If seeding nondeterministically, use absl::BitGen or "
      "absl::InsecureBitGen in place of ACMRandom")
  static int32_t HostnamePidTimeSeed();

  // If 'seed' is not in [1, 2^31-2], the range of numbers normally
  // generated, it will be rewritten to be in that range.
  inline void Reset(int32_t seed);
  int32_t GetSeed() const { return seed_; }

  // Checkpoint the state of this RNG.
  RandomBase* Clone() const override;

  // Returns a pseudo-random number in the range [1, 2^31-2].
  // Note that this is one number short on both ends of the full range of
  // non-negative 32-bit integers, which range from 0 to 2^31-1.
  ABSL_DEPRECATED("Use absl::Uniform<uint32>(gen) instead.")
  inline int32_t Next();

  // DO NOT USE Next64() IF A SHORT CYCLE LENGTH IS IMPORTANT
  //
  // Returns a pseudo-random number in the range [1, (2^31-2)^2].
  // Note that this does not cover all non-negative values of int64, which
  // range from 0 to 2^63-1.  The top two bits are ALWAYS ZERO.
  ABSL_DEPRECATED("Use absl::Uniform<uint64>(gen) instead.")
  int64_t Next64();

  // If n == 0, returns the next pseudo-random number in the range [0 .. 0]
  // If n != 0, returns the next pseudo-random number in the range [0 .. n)
  // This definition overrides RandomBase::Uniform() to ensure compatibility
  // with previous implementation of ACMRandom::Uniform().
  ABSL_DEPRECATED("Use absl::Uniform<int32>(gen, 0, n) instead.")
  int32_t Uniform(int32_t n) override {
    if (n == 0) {
      return Next() * 0;
    } else {
      return Next() % n;
    }
  }

  // If n == 0, returns the next pseudo-random number in the range [0 .. 0]
  // If n != 0, returns the next pseudo-random number in the range [0 .. n)
  // Slightly less efficient than Uniform but generates more accurate
  // uniform distribution for big n.
  // This function can call Next more than once.
  // n must be in the range [0 .. 2^31-1).
  ABSL_DEPRECATED("Use absl::Uniform<int32>(gen, 0, n) instead.")
  int32_t UnbiasedUniform(int32_t n) override;

  // n may be in the range [0 .. 2^{64}).
  ABSL_DEPRECATED("Use absl::Uniform<int64>(gen, 0, n) instead.")
  uint64_t UnbiasedUniform64(uint64_t n) override;

  // Returns a floating-point number in the range (0, 1).  Note that
  // neither 0 nor 1 is a possible value.
  ABSL_DEPRECATED(
      "Use absl::Uniform<float>(absl::IntervalOpen, gen, 0, 1) "
      "instead.")
  float RndFloat() {
    return Next() * 0.000000000465661273646;  // x: x * (M-1) = 1 - eps
  }

  // RandXX: Generate random numbers conforming to the RandomBase interface.
  // Since we do not have a pool, Rand8 and Rand16 do a full RNG cycle
  // and mask off the appropriate bits.
  // Rand32 and Rand64 also generate numbers within a reduced range.
  // Rand32 generates a subset with the range [0, 2^31-3].
  // Rand64 generates a subset with the range [0, (2^31-2)^2-1].
  ABSL_DEPRECATED("Use absl::Uniform<uint8>(gen) instead.")
  uint8_t Rand8() override;
  ABSL_DEPRECATED("Use absl::Uniform<uint16>(gen) instead.")
  uint16_t Rand16() override;
  ABSL_DEPRECATED("Use absl::Uniform<uint32>(gen) instead.")
  uint32_t Rand32() override;
  ABSL_DEPRECATED("Use absl::Uniform<uint64>(gen) instead.")
  uint64_t Rand64() override;

 private:
  // The following is a hack to ensure that ACMRandom::operator() maps to the
  // entire range of uint32. Otherwise, distributions will not understand how to
  // properly map its values.
  struct wrapper {
    wrapper(ACMRandom* instance) : acm_(instance) {}

    using result_type = RandomBase::result_type;
    static constexpr result_type min() { return 1; }
    static constexpr result_type max() { return ACMRandom::M; }
    result_type operator()() { return acm_->Rand32(); }

    ACMRandom* acm_;
  };

 public:
  result_type operator()() override {
    static std::uniform_int_distribution<result_type> uid(RandomBase::min(),
                                                          RandomBase::max());

    wrapper acm_wrapper(this);
    return uid(acm_wrapper);
  }

  ABSL_DEPRECATED("Use absl::Uniform<int32_t>(gen, 0, n) instead.")
  int32_t operator()(int32_t n) override { return Uniform(n); }

 private:
  static constexpr uint32_t M = 2147483647L;  // 2^31-1
  uint32_t seed_;
};

inline void ACMRandom::Reset(int32_t s) {
  seed_ = s & 0x7fffffff;  // make this a non-negative number
  if (seed_ == 0 || seed_ == M) {
    seed_ = 1;
  }
}

inline ACMRandom::ACMRandom(int32_t s) { Reset(s); }

inline int32_t ACMRandom::Next() {
  static const uint64_t A = 16807;  // bits 14, 8, 7, 5, 2, 1, 0
  // We are computing
  //       seed_ = (seed_ * A) % M,    where M = 2^31-1
  //
  // seed_ must not be zero or M, or else all subsequent computed values
  // will be zero or M respectively.  For all other values, seed_ will end
  // up cycling through every number in [1,M-1]
  uint64_t product = seed_ * A;

  // Compute (product % M) using the fact that ((x << 31) % M) == x.
  seed_ = static_cast<uint32_t>((product >> 31) + (product & M));
  // The first reduction may overflow by 1 bit, so we may need to repeat.
  // mod == M is not possible; using > allows the faster sign-bit-based test.
  if (seed_ > M) {
    seed_ -= M;
  }
  return seed_;
}

#endif  // THIRD_PARTY_GLOOP_UTIL_RANDOM_ACMRANDOM_H_
