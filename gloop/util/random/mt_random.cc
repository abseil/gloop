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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

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

const int MTRandom::kMTNumWords;

namespace {

// The golden values used in MT19937.
constexpr int kMT_M = 397;
constexpr int kMT_NM = MTRandom::kMTNumWords - kMT_M;  // 227
constexpr uint32_t kUpperMask = 0x80000000u;
constexpr uint32_t kLowerMask = 0x7fffffffu;
constexpr uint32_t kMatrixA = 0x9908b0dfu;
constexpr uint32_t kSeedMul = 1664525u;
constexpr uint32_t kInitArrayMul = 1812433253u;
constexpr uint32_t kArrayLoop3Mul = 1566083941u;

// Branchless step 2 and right half of step 3 so the compiler can auto-vectorize
// Cycle().
inline uint32_t Twist(const uint32_t a, const uint32_t b) {
  return (((a & kUpperMask) | (b & kLowerMask)) >> 1) ^
         (kMatrixA & (0u - (b & 1u)));
}

constexpr uint32_t mix30(const uint32_t a) {
  // Mix the high bits back in with the low bits.
  return a ^ (a >> 30);
}

// Precomputed Loop 1 state of Matsumoto's init_by_array (independent of seed).
constexpr std::array<uint32_t, MTRandom::kMTNumWords> MakeInitArrayBase() {
  std::array<uint32_t, MTRandom::kMTNumWords> mt{};
  mt[0] = 19650218u;
  for (int k = 1; k < MTRandom::kMTNumWords; ++k) {
    mt[k] = kInitArrayMul * mix30(mt[k - 1]) + static_cast<uint32_t>(k);
  }
  return mt;
}

constexpr std::array<uint32_t, MTRandom::kMTNumWords> kInitArrayBase =
    MakeInitArrayBase();

inline uint32_t LoadPartialWordLE(const char* const data, const size_t rem) {
  const auto* const bytes = reinterpret_cast<const uint8_t*>(data);
  uint32_t word = bytes[0];
  if (rem >= 2) {
    word |= static_cast<uint32_t>(bytes[1]) << 8;
    if (rem == 3) {
      word |= static_cast<uint32_t>(bytes[2]) << 16;
    }
  }
  return word;
}

}  // namespace

// Implements all of step 2 and step 3.
void MTRandom::Cycle() {
  uint32_t* const mt = context_.buffer;
  for (int i = 0; i < kMT_NM; ++i) {
    mt[i] = mt[i + kMT_M] ^ Twist(mt[i], mt[i + 1]);
  }
  for (int i = kMT_NM; i < kMTNumWords - 1; ++i) {
    mt[i] = mt[i - kMT_NM] ^ Twist(mt[i], mt[i + 1]);
  }
  mt[kMTNumWords - 1] = mt[kMT_M - 1] ^ Twist(mt[kMTNumWords - 1], mt[0]);
  context_.randcnt = 0;
}

// Raw copy into the mt array.
void MTRandom::InitRaw(const uint32_t* seed, int num_words) {
  CHECK_EQ(num_words, kMTNumWords) << ": InitRaw num_words mismatch.";
  context_.poolsize = 0;
  memcpy(context_.buffer, seed, kMTNumWords * sizeof(*seed));
  context_.randcnt = kMTNumWords;
}

void MTRandom::InitSeed(const uint32_t seed) {
  // Modified Knuth LCG: mt[i] = kSeedMul * mix30(mt[i - 1]) + i (mod 2^32).
  // Compute two words per iteration so both multiplies run in parallel:
  // with y = mix30(mt[i - 1]) and kSeedMul == 1 (mod 4),
  //   mt[i]     = A * y + i
  //   mt[i + 1] = A^2 * y + (A * i + i + 1) + A * (mix30(mt[i]) - mt[i]),
  // where the correction depends only on the top 2 bits of mt[i] and its low
  // 2 bits (y + i) & 3. Biasing the bit-30 correction by +2*A (subtracted from
  // base modulo 2^32) keeps both select arms non-zero so the compiler emits
  // conditional moves instead of a serial shift/mask chain.
  static_assert(kMTNumWords % 2 == 0,
                "InitSeed processes pairs plus one trailing word");
  constexpr uint32_t kSeedMul2 = kSeedMul * kSeedMul;
  constexpr uint32_t kBias = 2u * kSeedMul;
  context_.poolsize = 0;
  uint32_t* const mt = context_.buffer;
  mt[0] = seed;
  uint32_t x = seed;
  uint32_t i = 1;
  for (int pair = 0; pair < kMTNumWords / 2 - 1; ++pair) {
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
    mt[i] = x1;
    mt[i + 1] = x2;
    x = x2;
    i += 2;
  }
  mt[kMTNumWords - 1] =
      kSeedMul * mix30(x) + static_cast<uint32_t>(kMTNumWords - 1);
  context_.randcnt = kMTNumWords;
}

// Array initialization is identical to the Matsumoto version.
void MTRandom::InitArray(const uint32_t* seed, const int seed_length) {
  CHECK_GT(seed_length, 0);
  context_.pool = 0;
  uint32_t* mt = context_.buffer;

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

// Matsumoto init_by_array() directly from a little-endian byte string without
// heap allocation, using the compile-time Loop 1 table kInitArrayBase.
void MTRandom::InitArray(const absl::string_view seed_str) {
  const size_t len = seed_str.length();
  const char* const data = seed_str.data();
  const size_t num_full_words = len >> 2;
  const size_t rem = len & 3u;
  const size_t num_words = num_full_words + (rem != 0 ? 1u : 0u);

  context_.poolsize = 0;
  uint32_t* const mt = context_.buffer;

  uint32_t prev = kInitArrayBase[0];
  int i = 1;
  if (num_words <= static_cast<size_t>(kMTNumWords)) {
    uint32_t addend[kMTNumWords];
    for (size_t j = 0; j < num_full_words; ++j) {
      addend[j] = LittleEndian::Load32(data + j * 4) + static_cast<uint32_t>(j);
    }
    if (rem != 0) {
      addend[num_full_words] =
          LoadPartialWordLE(data + num_full_words * 4, rem) +
          static_cast<uint32_t>(num_full_words);
    }
    size_t j = 0;
    for (int idx = 1; idx < kMTNumWords; ++idx) {
      prev = (kInitArrayBase[idx] ^ (mix30(prev) * kSeedMul)) + addend[j];
      mt[idx] = prev;
      if (++j == num_words) {
        j = 0;
      }
    }
    // 624th iteration wraps to index 1.
    prev = (mt[1] ^ (mix30(prev) * kSeedMul)) + addend[j];
    mt[1] = prev;
    i = 2;
  } else {
    for (int idx = 1; idx < kMTNumWords; ++idx) {
      const size_t j = static_cast<size_t>(idx - 1);
      prev = (kInitArrayBase[idx] ^ (mix30(prev) * kSeedMul)) +
             (LittleEndian::Load32(data + j * 4) + static_cast<uint32_t>(j));
      mt[idx] = prev;
    }
    for (size_t j = static_cast<size_t>(kMTNumWords - 1); j < num_full_words;
         ++j) {
      prev = (mt[i] ^ (mix30(prev) * kSeedMul)) +
             (LittleEndian::Load32(data + j * 4) + static_cast<uint32_t>(j));
      mt[i] = prev;
      if (++i == kMTNumWords) {
        i = 1;
      }
    }
    if (rem != 0) {
      const uint32_t last_word =
          LoadPartialWordLE(data + num_full_words * 4, rem);
      prev = (mt[i] ^ (mix30(prev) * kSeedMul)) +
             (last_word + static_cast<uint32_t>(num_words - 1));
      mt[i] = prev;
      if (++i == kMTNumWords) {
        i = 1;
      }
    }
  }

  // Loop 3: 623 mixing iterations starting at `i`.
  for (int idx = i; idx < kMTNumWords; ++idx) {
    prev =
        (mt[idx] ^ (mix30(prev) * kArrayLoop3Mul)) - static_cast<uint32_t>(idx);
    mt[idx] = prev;
  }
  for (int idx = 1; idx < i; ++idx) {
    prev =
        (mt[idx] ^ (mix30(prev) * kArrayLoop3Mul)) - static_cast<uint32_t>(idx);
    mt[idx] = prev;
  }

  // Ensures that the state array is non-zero.
  mt[0] = kUpperMask;
  context_.randcnt = kMTNumWords;
}

// -------------------------------------------------------------------
//
// Everything below here is framework code.

MTRandom::MTRandom(uint32_t seed) { Reset(seed); }

MTRandom::MTRandom(const absl::string_view seed_str) { Reset(seed_str); }

// Seed MTRandom using an array of uint32.
MTRandom::MTRandom(const uint32_t* seed, int num_words) {
  Reset(seed, num_words);
}

MTRandom::MTRandom() {
  memset(&context_, 0, sizeof(MTContext));
  uint32_t buffer[32];
  memset(buffer, 0, sizeof(buffer));
  int len =
      RandomBase::WeakSeed(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
  InitArray(buffer,
            static_cast<int>((len + sizeof(uint32_t) - 1) / sizeof(uint32_t)));
}

MTRandom::MTRandom(const MTContext& context) : context_(context) {}

MTRandom* MTRandom::Clone() const { return new MTRandom(context_); }

void MTRandom::Reset(uint32_t seed) { InitSeed(seed); }

void MTRandom::Reset(const absl::string_view seed_str) {
  CHECK_GT(seed_str.length(), 0u)
      << ": MTRandom can not be initialized with an empty string";
  InitArray(seed_str);
}

void MTRandom::Reset(const uint32_t* seed, int num_words) {
  CHECK_EQ(num_words, kMTNumWords)
      << ": MTRandom must be initialized with a buffer of size " << kMTNumWords;
  InitRaw(seed, num_words);
}

static_assert(sizeof(void*) != 8 || sizeof(MTRandom) == 2520,
              "sizeof(MTRandom) must stay 2520 bytes on 64-bit platforms");

#undef GLOOP_MT_RANDOM_UNPREDICTABLE
