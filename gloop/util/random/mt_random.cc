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
// Implementation of the Mersenne Twister random number generator. See:
// http://scholar.google.com/scholar?q=mersenne+twister&ie=UTF-8&oe=UTF-8&hl=en&btnG=Search
// (see page 9 for the algorithm).
//
// The author's implementation (with initialization routines) is available at:
// http://www.math.sci.hiroshima-u.ac.jp/~m-mat/MT/emt.html
// Paper: http://www.math.sci.hiroshima-u.ac.jp/~m-mat/MT/ARTICLES/mt.pdf
//
// This implementation generates the same values when seeded identically,
// but none of the routines use the BSD code from page 20.

#include "gloop/util/random/mt_random.h"

#include <string.h>

#include <algorithm>  // For max.
#include <cstdint>
#include <memory>

#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "gloop/util/endian/endian.h"
#include "gloop/util/random/random_base.h"

#if defined(__has_builtin)
#if __has_builtin(__builtin_unpredictable)
#define GLOOP_MT_RANDOM_UNPREDICTABLE(x) __builtin_unpredictable(x)
#endif
#endif
#ifndef GLOOP_MT_RANDOM_UNPREDICTABLE
#define GLOOP_MT_RANDOM_UNPREDICTABLE(x) (x)
#endif

// The golden values used in MT
static constexpr int kMT_M = 397;
static constexpr uint32_t kUpperMask = 0x80000000u;
static constexpr uint32_t kLowerMask = 0x7fffffffu;
static constexpr uint32_t kMatrixA = 0x9908b0dfu;
static constexpr uint32_t kSeedMul = 1664525u;

const int MTRandom::kMTNumWords;

// This implements step 2 and the right half of step 3 (branchless so the
// compiler can auto-vectorize Cycle()).
static inline uint32_t twist(const uint32_t a, const uint32_t b) {
  // y <- (x[i] & U) | (x[i+1] & L)
  // y1 <- XOR (y>>1)
  // return y1
  return (((a & kUpperMask) | (b & kLowerMask)) >> 1) ^
         (kMatrixA & (0u - (b & 1u)));
}

static inline uint32_t mix30(const uint32_t a) {
  // Mix the high bits back in with the low bits.
  return a ^ (a >> 30);
}

// Implements all of step 2 and step 3.
void MTRandom::Cycle() {
  // for i in 0 .. n
  // x[i] <- x[(i + m) % n] ^ twist(x[i], x[(i+1) % n])
  // Use two index-based loops + residual to avoid the mod operation and enable
  // compiler auto-vectorization.
  constexpr int kNM = kMTNumWords - kMT_M;
  uint32_t* const mt = context_.buffer;
  for (int i = 0; i < kNM; ++i) {
    mt[i] = mt[i + kMT_M] ^ twist(mt[i], mt[i + 1]);
  }
  for (int i = kNM; i < kMTNumWords - 1; ++i) {
    mt[i] = mt[i - kNM] ^ twist(mt[i], mt[i + 1]);
  }
  mt[kMTNumWords - 1] = mt[kMT_M - 1] ^ twist(mt[kMTNumWords - 1], mt[0]);
  context_.randcnt = 0;
}

// Raw copy into the mt array.
void MTRandom::InitRaw(const uint32_t* const seed, const int num_words) {
  CHECK_EQ(num_words, kMTNumWords) << ": InitRaw num_words mismatch.";
  context_.poolsize = 0;
  context_.pool = 0;
  memcpy(context_.buffer, seed, kMTNumWords * sizeof(*seed));
  context_.randcnt = kMTNumWords;
}

void MTRandom::InitSeed(const uint32_t seed) {
  // The original version of initialize is based on Knuth, Vol 2., 3rd ed.
  // p. 102-106. Linear congruential generator:  x1 = ( x0 * A ) % M.
  // A = 69069, M = 2^32
  //
  // The second variant of initialize uses a modified linear congruential
  // generator, which is supposed to more effectively distribute the MSB
  // of the seed across the key space.
  // x[i] = ( mix30(x[i-1]) * A + i) % M
  // M = 2^32, A = 1812433253
  //
  // We use a variant of the second, with the parameters:
  // M = 2^32, A = 1664525
  //
  // Compute two words per iteration so both multiplies run in parallel:
  // with y = mix30(mt[i - 1]) and A = 1 (mod 4),
  //   mt[i]     = A * y + i
  //   mt[i + 1] = A * mix30(mt[i]) + (i + 1)
  //             = A^2 * y + (A * i + i + 1) + A * (mix30(mt[i]) - mt[i]),
  // where the correction depends only on the top 2 bits of mt[i] and its low
  // 2 bits (y + i) & 3. Biasing the bit-30 correction by +2*A (subtracted from
  // base modulo 2^32) keeps both select arms non-zero so the compiler emits
  // conditional moves instead of a serial shift/mask chain.
  constexpr uint32_t kSeedMul2 = kSeedMul * kSeedMul;
  constexpr uint32_t kBias = 2u * kSeedMul;
  context_.poolsize = 0;
  context_.pool = 0;
  uint32_t* const mt = context_.buffer;
  mt[0] = seed;
  uint32_t x = seed;
  uint32_t i = 1;
  uint32_t* p = mt + 1;
  const uint32_t* const end = mt + (kMTNumWords - 1);
  do {
    const uint32_t y = mix30(x);
    const uint32_t low = y + i;
    const uint32_t base = kSeedMul * i + i + (1u - kBias);
    const uint32_t hi_base =
        (low & 2u) ? base - 2u * kSeedMul : base + 2u * kSeedMul;
    const uint32_t lo_true = (low & 1u) ? kSeedMul : 3u * kSeedMul;
    const uint32_t x1 = kSeedMul * y + i;
    const uint32_t hi =
        GLOOP_MT_RANDOM_UNPREDICTABLE(static_cast<int32_t>(x1) < 0) ? hi_base
                                                                    : base;
    const uint32_t lo =
        GLOOP_MT_RANDOM_UNPREDICTABLE((x1 & (kUpperMask >> 1)) != 0u) ? lo_true
                                                                      : kBias;
    const uint32_t x2 = (kSeedMul2 * y + hi) + lo;
    p[0] = x1;
    p[1] = x2;
    x = x2;
    i += 2;
    p += 2;
  } while (p < end);
  mt[kMTNumWords - 1] =
      kSeedMul * mix30(x) + static_cast<uint32_t>(kMTNumWords - 1);
  context_.randcnt = kMTNumWords;
}

// Array initialization is identical to the Matsumoto version.
void MTRandom::InitArray(const uint32_t* const seed, const int seed_length) {
  CHECK_GT(seed_length, 0);
  context_.poolsize = 0;
  context_.pool = 0;
  uint32_t* const mt = context_.buffer;

  // Initial linear congruential generator, using parameters
  // M = 2^32, A = 1812433253UL
  mt[0] = 19650218UL;
  for (int k = 1; k < kMTNumWords; ++k) {
    mt[k] = 1812433253UL * mix30(mt[k - 1]) + k;
  }

  // Mix and incorporate seed array.
  int i = 1;
  for (int j = 0, k = std::max(kMTNumWords, seed_length); k > 0; k--) {
    mt[i] = (mt[i] ^ (mix30(mt[i - 1]) * 1664525UL)) + seed[j] + j;
    j = (j + 1) % seed_length;
    if (++i >= kMTNumWords) {
      mt[0] = mt[kMTNumWords - 1];
      i = 1;
    }
  }
  for (int k = kMTNumWords - 1; k > 0; k--) {
    mt[i] = (mt[i] ^ (mix30(mt[i - 1]) * 1566083941UL)) - i;
    if (++i >= kMTNumWords) {
      mt[0] = mt[kMTNumWords - 1];
      i = 1;
    }
  }

  // Ensures that the state array is non-zero.
  mt[0] = 0x80000000UL;

  // By setting randcnt to kMTNumWords, Cycle() will be called
  // the first time a number is required.
  context_.randcnt = kMTNumWords;
}

// Peel off bytes one at a time from the pool.  If the
// byte pool is empty, fetch another uint32.
uint8_t MTRandom::Rand8() {
  if (context_.poolsize == 0) {
    context_.pool = NextWord();
    context_.poolsize = 3;
  } else {
    context_.pool >>= 8;
    --context_.poolsize;
  }
  return context_.pool & 0x000000ff;
}

uint16_t MTRandom::Rand16() {
  const uint16_t rv = MTRandom::Rand8();
  return (rv << 8) | MTRandom::Rand8();
}

// -------------------------------------------------------------------
//
// Everything below here is framework code.

MTRandom::MTRandom(const uint32_t seed) { Reset(seed); }

MTRandom::MTRandom(const absl::string_view seed_str) { Reset(seed_str); }

// Seed MTRandom using an array of uint32.
MTRandom::MTRandom(const uint32_t* const seed, const int num_words) {
  Reset(seed, num_words);
}

MTRandom::MTRandom() {
  uint32_t buffer[32];
  memset(buffer, 0, sizeof(buffer));
  const int len =
      RandomBase::WeakSeed(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
  InitArray(buffer,
            static_cast<int>((len + sizeof(uint32_t) - 1) / sizeof(uint32_t)));
}

MTRandom::MTRandom(const MTContext& context) : context_(context) {}

MTRandom* MTRandom::Clone() const { return new MTRandom(context_); }

void MTRandom::Reset(const uint32_t seed) { InitSeed(seed); }

void MTRandom::Reset(const absl::string_view seed_str) {
  CHECK_GT(seed_str.length(), 0u)
      << ": MTRandom can not be initialized with an empty string";

  // Allocate a buffer of uint32 and copy our seed string, zero filling the
  // final bytes in the buffer.
  const int num_words = static_cast<int>(
      (seed_str.length() + sizeof(uint32_t) - 1) / sizeof(uint32_t));
  std::unique_ptr<uint32_t[]> seed(new uint32_t[num_words]);
  seed[num_words - 1] = 0;
  memcpy(seed.get(), seed_str.data(), seed_str.length());
  for (int i = 0; i < num_words; ++i) {
    seed[i] = LittleEndian::ToHost32(seed[i]);
  }
  InitArray(seed.get(), num_words);

  // Clear the seed array before we destroy it.
  memset(seed.get(), 0, num_words * sizeof(uint32_t));
}

void MTRandom::Reset(const uint32_t* const seed, const int num_words) {
  CHECK_EQ(num_words, kMTNumWords)
      << ": MTRandom must be initialized with a buffer of size " << kMTNumWords;

  InitRaw(seed, num_words);
}

#undef GLOOP_MT_RANDOM_UNPREDICTABLE
