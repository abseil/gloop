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

//
// Implementation of the Mersenne Twister RNG.  (MT19937). MT is a very
// good RNG, and is intended to be a replacement for users of ACMRandom.
// It generates sequences that have more apparent randomness, and is
// much faster (approximately 50%, optimized).
// (Rand32() MTRandom: 13ns vs. 17 ns. on a P4 3.2 Ghz)
// MTRandom maintain about 2Kb of state.
//
// DO NOT use MTRandom for any application where security --
// unpredictability of subsequent output and previous output -- is
// needed.  MTRandom is in *NO* *WAY* a cryptographically secure
// pseudorandom number generator, and using it where recipients of its
// output may wish to guess earlier/later output values would be very
// bad.  For cryptographically secure PRNGs see secure_random.h.
//
// The MT random number generator has a period of 2^19937-1.
//
// NOTE: Statistical analysis of MT has demonstrated detectable
// bias in sequences that it generates.  It is a good candidate for
// monte-carlo simulation, but should not be used where security
// is required.
//
// Also, our initialization routine may not be identical to other publicly
// available implementations, and methods that generate real numbers also
// differ from other publicly available implementations, so take care when
// comparing with non-Google implementations.
#ifndef THIRD_PARTY_GLOOP_UTIL_RANDOM_MT_RANDOM_H_
#define THIRD_PARTY_GLOOP_UTIL_RANDOM_MT_RANDOM_H_

#include <cstdint>

#include "absl/base/attributes.h"
#include "absl/strings/string_view.h"
#include "gloop/util/random/random_base.h"

// MTRandom: An implementation of the MT19937 RNG class.  Implements
// the RandomBase interface.
//
// Example:
//   MTRandom b;
//   std::cout << " Hello, a random number is: " << b.Rand32() << std::endl;
//
// This class is thread-compatible: see <link>.
//
// NOTE: This class is deprecated. Use absl::BitGen instead.
// If an MT19937 implementation is required, use std::mt19937.
class MTRandom : public RandomBase {
 public:
  // Create an instance of MTRandom using a single seed value.
  // Calls InitSeed() to initialize the context.
  explicit MTRandom(uint32_t seed);

  // Seed MTRandom using a string as seed.  Uses InitArray().
  explicit MTRandom(const absl::string_view seed);

  // Seed MTRandom using an array of uint32.  When using this initializer,
  // 'seed' should be well distributed random data of kMTSizeBytes bytes
  // aligned to a uint32, since no additional mixing is done.
  // Requires: num_words == kMTNumWords
  // TODO: Change num_words by size (in bytes), as it is done in
  // SecureRandom.
  MTRandom(const uint32_t* seed, int num_words);

  // Creates an MTRandom generator object that has been seeded using
  // some weak random data.  (time of day, hostname, etc.).  Uses InitArray().
  MTRandom();

  // Disallow copy, however allow move.
  MTRandom(const MTRandom&) = delete;
  MTRandom& operator=(const MTRandom&) = delete;
  MTRandom(MTRandom&&) = default;
  MTRandom& operator=(MTRandom&&) = default;

  ~MTRandom() override;

  MTRandom* Clone() const override;

  ABSL_DEPRECATED("Use absl::Uniform<uint8>(gen) instead.")
  uint8_t Rand8() override;
  ABSL_DEPRECATED("Use absl::Uniform<uint16>(gen) instead.")
  uint16_t Rand16() override;
  ABSL_DEPRECATED("Use absl::Uniform<uint32>(gen) instead.")
  uint32_t Rand32() override;
  ABSL_DEPRECATED("Use absl::Uniform<uint64>(gen) instead.")
  uint64_t Rand64() override;

  static int SeedSize() { return kMTSizeBytes; }

  // The log2 of the RNG buffers (based on uint32).
  static constexpr int kMTNumWords = 624;

  // The size of the RNG buffers in bytes.
  static constexpr int kMTSizeBytes = kMTNumWords * sizeof(uint32_t);

  // Reseeds the RNG, as if it had been constructed from this seed.
  // The semantics and restrictions are identical to those in the
  // corresponding constructors.
  void Reset(uint32_t seed);
  void Reset(const absl::string_view seed);
  void Reset(const uint32_t* seed, int num_words);

 private:
  struct MTContext;

  // Create an MTRandom from an MTContext. Does not call InitSeed.
  explicit MTRandom(const MTContext& context);

  // InitRaw: Initialize the MTRandom context using an array of raw
  // uint32 values.  Requires length == SeedSize().
  void InitRaw(const uint32_t* seed, int length);

  // Initialize the MTRandom context using a 32-bit seed.  The seed
  // is distributed across the initial space.
  // NOTE: This will not seed the generator with identical values as
  // either of the seed algorithms in the original paper.  If an
  // identical sequence is required, use InitRaw.
  void InitSeed(uint32_t seed);

  // InitArray: Initialize the MTRandom context using an array of
  // uint32 values.  The values will be mixed to form an initial seed.
  void InitArray(const uint32_t* seed, int length);

  // The MT context. This holds our RNG state and the current generation
  // of generated numbers.
  struct MTContext {
    int8_t poolsize;  // For giving back bytes.
    int32_t randcnt;  // Count of remaining bytes.
    uint32_t pool;
    uint32_t buffer[kMTNumWords];
  };

  // This method cycles the MTContext and generates the next set of random
  // numbers.
  void Cycle();

  MTContext context_;
};

#endif  // THIRD_PARTY_GLOOP_UTIL_RANDOM_MT_RANDOM_H_
