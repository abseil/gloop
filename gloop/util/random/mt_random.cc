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

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "gloop/util/endian/endian.h"
#include "gloop/util/random/random_base.h"

#if !defined(MTRANDOM_FORCE_SCALAR)
#if defined(__AVX2__)
#include <immintrin.h>
#define GLOOP_MT_RANDOM_USE_AVX2 1
#elif defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#define GLOOP_MT_RANDOM_USE_NEON 1
#endif
#endif

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
constexpr int kMT_N = MTRandom::kMTNumWords;
constexpr int kMT_M = 397;
constexpr int kMT_NM = kMT_N - kMT_M;  // 227
constexpr uint32_t kUpperMask = 0x80000000u;
constexpr uint32_t kLowerMask = 0x7fffffffu;
constexpr uint32_t kMatrixA = 0x9908b0dfu;
constexpr uint32_t kSeedMul = 1664525u;
constexpr uint32_t kArrayLoop3Mul = 1566083941u;
constexpr uint64_t kGamma0 = 0x9e3779b97f4a7c15ULL;
constexpr uint64_t kGamma1 = 0xbf58476d1ce4e5b9ULL;

// Tempering masks and GF(2)^32 inverse constants.
constexpr uint32_t kTemperB = 0x9d2c5680u;
constexpr uint32_t kTemperC = 0xefc60000u;
constexpr uint32_t kTemperB2 = kTemperB & (kTemperB << 7);
constexpr uint32_t kTemperB4 = kTemperB2 & (kTemperB2 << 14);

constexpr inline uint32_t Mix30(const uint32_t a) { return a ^ (a >> 30); }

inline uint64_t Mix64(uint64_t z) {
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

constexpr inline uint32_t TemperScalar(uint32_t y) {
  y ^= (y >> 11);
  y ^= (y << 7) & kTemperB;
  y ^= (y << 15) & kTemperC;
  y ^= (y >> 18);
  return y;
}

constexpr inline uint32_t UntemperScalar(uint32_t y) {
  y ^= (y >> 18);
  y ^= (y << 15) & kTemperC;
  y ^= (y << 7) & kTemperB;
  y ^= (y << 14) & kTemperB2;
  y ^= (y << 28) & kTemperB4;
  y ^= (y >> 11);
  y ^= (y >> 22);
  return y;
}

constexpr bool VerifyTemperBijectionOnBasis() {
  for (int bit = 0; bit < 32; ++bit) {
    const uint32_t v = 1u << bit;
    if (UntemperScalar(TemperScalar(v)) != v) return false;
    if (TemperScalar(UntemperScalar(v)) != v) return false;
  }
  return true;
}
static_assert(VerifyTemperBijectionOnBasis(),
              "UntemperScalar must be the exact GF(2)^32 inverse of Temper");

// Precomputed Loop 1 state of Matsumoto's init_by_array (independent of seed).
constexpr std::array<uint32_t, kMT_N> MakeInitArrayBase() {
  std::array<uint32_t, kMT_N> mt{};
  mt[0] = 19650218u;
  for (int k = 1; k < kMT_N; ++k) {
    mt[k] = 1812433253u * Mix30(mt[k - 1]) + static_cast<uint32_t>(k);
  }
  return mt;
}

constexpr std::array<uint32_t, kMT_N> kInitArrayBase = MakeInitArrayBase();

ABSL_ATTRIBUTE_ALWAYS_INLINE inline uint32_t TwistScalar(const uint32_t a,
                                                         const uint32_t b) {
  const uint32_t y = (a & kUpperMask) | (b & kLowerMask);
  return (y >> 1) ^ ((0u - (b & 1u)) & kMatrixA);
}

inline uint32_t LoadPartialWordLE(const char* const data,
                                  const size_t num_bytes) {
  uint32_t word = 0;
  memcpy(&word, data, num_bytes);
  return LittleEndian::ToHost32(word);
}

// ---------------------------------------------------------------------------
// Full 624-word fused Twist + Pre-Temper (AVX2 / NEON / scalar).
// Indices [400, 624) are never read back within the same Cycle(), so tempering
// is fused directly into their twist store; only [0, 400) needs a second pass.
// On subsequent cycles (already_tempered == true), the state is untempered
// with the exact GF(2)^32 inverse before twisting.
// ---------------------------------------------------------------------------

#if defined(GLOOP_MT_RANDOM_USE_AVX2)
ABSL_ATTRIBUTE_ALWAYS_INLINE inline __m256i TwistVecAvx2(const __m256i a,
                                                         const __m256i b,
                                                         const __m256i m,
                                                         const __m256i v_upper,
                                                         const __m256 v_table) {
  const __m256i y = _mm256_or_si256(_mm256_and_si256(a, v_upper),
                                    _mm256_andnot_si256(v_upper, b));
  const __m256i mag = _mm256_castps_si256(_mm256_permutevar_ps(v_table, b));
  return _mm256_xor_si256(m, _mm256_xor_si256(_mm256_srli_epi32(y, 1), mag));
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline __m256i TemperVecAvx2(__m256i y,
                                                          const __m256i v_b,
                                                          const __m256i v_c) {
  y = _mm256_xor_si256(y, _mm256_srli_epi32(y, 11));
  y = _mm256_xor_si256(y, _mm256_and_si256(_mm256_slli_epi32(y, 7), v_b));
  y = _mm256_xor_si256(y, _mm256_and_si256(_mm256_slli_epi32(y, 15), v_c));
  return _mm256_xor_si256(y, _mm256_srli_epi32(y, 18));
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline __m256i UntemperVecAvx2(__m256i y,
                                                            const __m256i v_b,
                                                            const __m256i v_b2,
                                                            const __m256i v_b4,
                                                            const __m256i v_c) {
  y = _mm256_xor_si256(y, _mm256_srli_epi32(y, 18));
  y = _mm256_xor_si256(y, _mm256_and_si256(_mm256_slli_epi32(y, 15), v_c));
  y = _mm256_xor_si256(y, _mm256_and_si256(_mm256_slli_epi32(y, 7), v_b));
  y = _mm256_xor_si256(y, _mm256_and_si256(_mm256_slli_epi32(y, 14), v_b2));
  y = _mm256_xor_si256(y, _mm256_and_si256(_mm256_slli_epi32(y, 28), v_b4));
  y = _mm256_xor_si256(y, _mm256_srli_epi32(y, 11));
  return _mm256_xor_si256(y, _mm256_srli_epi32(y, 22));
}

void TwistAndTemperAll624Avx2(uint32_t* const mt, const bool already_tempered) {
  const __m256i v_upper = _mm256_set1_epi32(static_cast<int>(kUpperMask));
  const __m256 v_table = _mm256_castsi256_ps(_mm256_setr_epi32(
      0, static_cast<int>(kMatrixA), 0, static_cast<int>(kMatrixA), 0,
      static_cast<int>(kMatrixA), 0, static_cast<int>(kMatrixA)));
  const __m256i v_b = _mm256_set1_epi32(static_cast<int>(kTemperB));
  const __m256i v_c = _mm256_set1_epi32(static_cast<int>(kTemperC));

  if (already_tempered) {
    const __m256i v_b2 = _mm256_set1_epi32(static_cast<int>(kTemperB2));
    const __m256i v_b4 = _mm256_set1_epi32(static_cast<int>(kTemperB4));
    for (int i = 0; i < kMT_N; i += 8) {
      const __m256i v =
          _mm256_loadu_si256(reinterpret_cast<const __m256i*>(mt + i));
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(mt + i),
                          UntemperVecAvx2(v, v_b, v_b2, v_b4, v_c));
    }
  }

  // Block 1 [0, 224): 28 * 8 words (untempered; read back by block 2).
  for (int i = 0; i < 224; i += 8) {
    const __m256i a =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(mt + i));
    const __m256i b =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(mt + i + 1));
    const __m256i m =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(mt + i + kMT_M));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(mt + i),
                        TwistVecAvx2(a, b, m, v_upper, v_table));
  }
  mt[224] = mt[224 + kMT_M] ^ TwistScalar(mt[224], mt[225]);
  mt[225] = mt[225 + kMT_M] ^ TwistScalar(mt[225], mt[226]);
  mt[226] = mt[226 + kMT_M] ^ TwistScalar(mt[226], mt[227]);

  // Block 2 untempered prefix [227, 400): needed by [454, 624).
  for (int i = kMT_NM; i < 395; i += 8) {
    const __m256i a =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(mt + i));
    const __m256i b =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(mt + i + 1));
    const __m256i m =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(mt + i - kMT_NM));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(mt + i),
                        TwistVecAvx2(a, b, m, v_upper, v_table));
  }
  {
    const __m128i s_upper = _mm256_castsi256_si128(v_upper);
    const __m128 s_table = _mm256_castps256_ps128(v_table);
    const __m128i a =
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(mt + 395));
    const __m128i b =
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(mt + 396));
    const __m128i m =
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(mt + 395 - kMT_NM));
    const __m128i y =
        _mm_or_si128(_mm_and_si128(a, s_upper), _mm_andnot_si128(s_upper, b));
    const __m128i mag = _mm_castps_si128(_mm_permutevar_ps(s_table, b));
    const __m128i r =
        _mm_xor_si128(m, _mm_xor_si128(_mm_srli_epi32(y, 1), mag));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(mt + 395), r);
  }
  mt[399] = mt[399 - kMT_NM] ^ TwistScalar(mt[399], mt[400]);

  // Block 2 fused twist + temper [400, 616): 27 * 8 words.
  for (int i = 400; i < 616; i += 8) {
    const __m256i a =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(mt + i));
    const __m256i b =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(mt + i + 1));
    const __m256i m =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(mt + i - kMT_NM));
    _mm256_storeu_si256(
        reinterpret_cast<__m256i*>(mt + i),
        TemperVecAvx2(TwistVecAvx2(a, b, m, v_upper, v_table), v_b, v_c));
  }
  // Final 8 words [616, 624) including wraparound mt[0] (still untempered).
  {
    const __m256i a =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(mt + 616));
    const __m128i b_lo =
        _mm_loadu_si128(reinterpret_cast<const __m128i*>(mt + 617));
    const __m128i a_hi = _mm256_extracti128_si256(a, 1);
    const __m128i b_hi =
        _mm_alignr_epi8(_mm_cvtsi32_si128(static_cast<int>(mt[0])), a_hi, 4);
    const __m256i b =
        _mm256_inserti128_si256(_mm256_castsi128_si256(b_lo), b_hi, 1);
    const __m256i m =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(mt + 616 - kMT_NM));
    _mm256_storeu_si256(
        reinterpret_cast<__m256i*>(mt + 616),
        TemperVecAvx2(TwistVecAvx2(a, b, m, v_upper, v_table), v_b, v_c));
  }

  // Temper [0, 400): 50 * 8 words.
  for (int i = 0; i < 400; i += 8) {
    const __m256i v =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(mt + i));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(mt + i),
                        TemperVecAvx2(v, v_b, v_c));
  }
}
#endif

#if defined(GLOOP_MT_RANDOM_USE_NEON)
ABSL_ATTRIBUTE_ALWAYS_INLINE inline uint32x4_t TwistVecNeon(
    const uint32x4_t a, const uint32x4_t b, const uint32x4_t m,
    const uint32x4_t v_upper, const uint32x4_t v_matrix) {
  const uint32x4_t y = vbslq_u32(v_upper, a, b);
  const uint32x4_t mask = vreinterpretq_u32_s32(
      vshrq_n_s32(vshlq_n_s32(vreinterpretq_s32_u32(b), 31), 31));
  return veorq_u32(m, veorq_u32(vshrq_n_u32(y, 1), vandq_u32(mask, v_matrix)));
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline uint32x4_t TemperVecNeon(
    uint32x4_t y, const uint32x4_t v_b, const uint32x4_t v_c) {
  y = veorq_u32(y, vshrq_n_u32(y, 11));
  y = veorq_u32(y, vandq_u32(vshlq_n_u32(y, 7), v_b));
  y = veorq_u32(y, vandq_u32(vshlq_n_u32(y, 15), v_c));
  return veorq_u32(y, vshrq_n_u32(y, 18));
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline uint32x4_t UntemperVecNeon(
    uint32x4_t y, const uint32x4_t v_b, const uint32x4_t v_b2,
    const uint32x4_t v_b4, const uint32x4_t v_c) {
  y = veorq_u32(y, vshrq_n_u32(y, 18));
  y = veorq_u32(y, vandq_u32(vshlq_n_u32(y, 15), v_c));
  y = veorq_u32(y, vandq_u32(vshlq_n_u32(y, 7), v_b));
  y = veorq_u32(y, vandq_u32(vshlq_n_u32(y, 14), v_b2));
  y = veorq_u32(y, vandq_u32(vshlq_n_u32(y, 28), v_b4));
  y = veorq_u32(y, vshrq_n_u32(y, 11));
  return veorq_u32(y, vshrq_n_u32(y, 22));
}

void TwistAndTemperAll624Neon(uint32_t* const mt, const bool already_tempered) {
  const uint32x4_t v_upper = vdupq_n_u32(kUpperMask);
  const uint32x4_t v_matrix = vdupq_n_u32(kMatrixA);
  const uint32x4_t v_b = vdupq_n_u32(kTemperB);
  const uint32x4_t v_c = vdupq_n_u32(kTemperC);

  if (already_tempered) {
    const uint32x4_t v_b2 = vdupq_n_u32(kTemperB2);
    const uint32x4_t v_b4 = vdupq_n_u32(kTemperB4);
    for (int i = 0; i < kMT_N; i += 4) {
      vst1q_u32(mt + i,
                UntemperVecNeon(vld1q_u32(mt + i), v_b, v_b2, v_b4, v_c));
    }
  }
  for (int i = 0; i < 224; i += 4) {
    vst1q_u32(mt + i,
              TwistVecNeon(vld1q_u32(mt + i), vld1q_u32(mt + i + 1),
                           vld1q_u32(mt + i + kMT_M), v_upper, v_matrix));
  }
  for (int i = 224; i < kMT_NM; ++i) {
    mt[i] = mt[i + kMT_M] ^ TwistScalar(mt[i], mt[i + 1]);
  }
  for (int i = kMT_NM; i < 399; i += 4) {
    vst1q_u32(mt + i,
              TwistVecNeon(vld1q_u32(mt + i), vld1q_u32(mt + i + 1),
                           vld1q_u32(mt + i - kMT_NM), v_upper, v_matrix));
  }
  mt[399] = mt[399 - kMT_NM] ^ TwistScalar(mt[399], mt[400]);
  for (int i = 400; i < 620; i += 4) {
    vst1q_u32(mt + i,
              TemperVecNeon(
                  TwistVecNeon(vld1q_u32(mt + i), vld1q_u32(mt + i + 1),
                               vld1q_u32(mt + i - kMT_NM), v_upper, v_matrix),
                  v_b, v_c));
  }
  for (int i = 620; i < kMT_N - 1; ++i) {
    mt[i] = TemperScalar(mt[i - kMT_NM] ^ TwistScalar(mt[i], mt[i + 1]));
  }
  mt[kMT_N - 1] =
      TemperScalar(mt[kMT_M - 1] ^ TwistScalar(mt[kMT_N - 1], mt[0]));
  for (int i = 0; i < 400; i += 4) {
    vst1q_u32(mt + i, TemperVecNeon(vld1q_u32(mt + i), v_b, v_c));
  }
}
#endif

#if !defined(GLOOP_MT_RANDOM_USE_AVX2) && !defined(GLOOP_MT_RANDOM_USE_NEON)
void TwistAndTemperAll624Scalar(uint32_t* const mt,
                                const bool already_tempered) {
  if (already_tempered) {
    for (int i = 0; i < kMT_N; ++i) {
      mt[i] = UntemperScalar(mt[i]);
    }
  }
  for (int i = 0; i < kMT_NM; ++i) {
    mt[i] = mt[i + kMT_M] ^ TwistScalar(mt[i], mt[i + 1]);
  }
  for (int i = kMT_NM; i < 400; ++i) {
    mt[i] = mt[i - kMT_NM] ^ TwistScalar(mt[i], mt[i + 1]);
  }
  for (int i = 400; i < kMT_N - 1; ++i) {
    mt[i] = TemperScalar(mt[i - kMT_NM] ^ TwistScalar(mt[i], mt[i + 1]));
  }
  mt[kMT_N - 1] =
      TemperScalar(mt[kMT_M - 1] ^ TwistScalar(mt[kMT_N - 1], mt[0]));
  for (int i = 0; i < 400; ++i) {
    mt[i] = TemperScalar(mt[i]);
  }
}
#endif

ABSL_ATTRIBUTE_ALWAYS_INLINE inline void TwistAndTemperAll624(
    uint32_t* const mt, const bool already_tempered) {
#if defined(GLOOP_MT_RANDOM_USE_AVX2)
  TwistAndTemperAll624Avx2(mt, already_tempered);
#elif defined(GLOOP_MT_RANDOM_USE_NEON)
  TwistAndTemperAll624Neon(mt, already_tempered);
#else
  TwistAndTemperAll624Scalar(mt, already_tempered);
#endif
}

// ---------------------------------------------------------------------------
// Eager 624-word InitSeed(uint32_t) — pure portable C++20 (zero inline asm):
// Zero-bias 8-cycle/pair speculative y-carried 2-step LCG with decoupled
// t1/t2 verification and cold exact scalar fallback.
// ---------------------------------------------------------------------------

ABSL_ATTRIBUTE_NOINLINE
#if defined(__GNUC__) || defined(__clang__)
__attribute__((cold))
#endif
void InitSeedFallbackExact(uint32_t* const mt) {
  uint32_t prev = mt[0];
  for (uint32_t j = 1u; j < static_cast<uint32_t>(kMT_N); ++j) {
    prev = kSeedMul * Mix30(prev) + j;
    mt[j] = prev;
  }
}

void InitSeedAll624(uint32_t* const mt, const uint32_t seed) {
  constexpr uint32_t kA = kSeedMul;
  constexpr uint32_t kA2 = kA * kA;
  constexpr uint32_t k2A = 2u * kA;
  constexpr uint32_t k3A = 3u * kA;
  constexpr uint32_t kNegA = 0u - kA;
  constexpr uint32_t kNeg2A = 0u - k2A;
  constexpr uint32_t kBaseStep = 2u * (kA + 1u);
  constexpr uint32_t kLow30Mask = (kUpperMask >> 1) - 1u;  // 0x3fffffffu

  mt[0] = seed;
  uint32_t y = Mix30(seed);
  uint32_t base = (kA + 1u) * 1u + 1u;
  uint32_t* p = mt + 1;
  uint32_t i = 1u;
#if defined(__clang__)
#pragma clang loop unroll(disable)
#endif
  do {
    const uint32_t low = y + i;
    const uint32_t hi_d = (low & 2u) ? kNegA : k3A;
    const uint32_t lo_d = (y & 1u) ? 0u : kNeg2A;

    const uint32_t p1 = kA * y;
    const uint32_t p2 = kA2 * y;
    const uint32_t b2 = p2 + base;
    const uint32_t t2 = b2 >> 30;

    const uint32_t hi =
        GLOOP_MT_RANDOM_UNPREDICTABLE(static_cast<int32_t>(p1) < 0) ? hi_d : kA;
    const uint32_t lo =
        GLOOP_MT_RANDOM_UNPREDICTABLE((p1 & (kUpperMask >> 1)) != 0u) ? lo_d
                                                                      : kNegA;
    const uint32_t x1 = p1 + i;
    const uint32_t x2 = b2 + (hi + lo);
    p[0] = x1;
    p[1] = x2;
    y = x2 ^ t2;
    if (ABSL_PREDICT_FALSE((x2 ^ b2) >= (kUpperMask >> 1))) {
      y = Mix30(x2);
    }
    p += 2;
    base += kBaseStep;
    i += 2u;
  } while (i != static_cast<uint32_t>(kMT_N - 1));
  mt[kMT_N - 1] = kA * y + static_cast<uint32_t>(kMT_N - 1);

  uint32_t min_low30 = kLow30Mask;
  for (int k = 4; k < kMT_N; ++k) {
    const uint32_t v = mt[k] & kLow30Mask;
    if (v < min_low30) min_low30 = v;
  }
  const bool early_carry =
      ((mt[1] & kLow30Mask) == 0u) | ((mt[3] & kLow30Mask) < 3u);
  if (ABSL_PREDICT_FALSE((min_low30 < static_cast<uint32_t>(kMT_N - 2)) |
                         early_carry)) {
    InitSeedFallbackExact(mt);
  }
}

// ---------------------------------------------------------------------------
// Eager 624-word InitArray Loop 3 (zero-asm speculative q >> 30).
// ---------------------------------------------------------------------------

uint32_t InitArrayLoop3Range(uint32_t* const mt, const int start_idx,
                             const int end_idx, uint32_t prev) {
  uint32_t x = Mix30(prev);
  for (int idx = start_idx; idx < end_idx; ++idx) {
    const uint32_t q = mt[idx] ^ (x * kArrayLoop3Mul);
    const uint32_t t = q >> 30;
    prev = q - static_cast<uint32_t>(idx);
    mt[idx] = prev;
    x = prev ^ t;
    if (ABSL_PREDICT_FALSE((prev ^ q) >= (kUpperMask >> 1))) {
      x = Mix30(prev);
    }
  }
  return prev;
}

// ---------------------------------------------------------------------------
// Eager 624-word Default Constructor State Filler (AVX2 / NEON / scalar).
// Uses 128-bit weak seed from RandomBase::WeakSeed() (fork-safe, zero
// thread_local).
// ---------------------------------------------------------------------------

#if defined(GLOOP_MT_RANDOM_USE_AVX2)
void FillDefaultStateAvx2(uint32_t* const mt, const uint64_t seed0,
                          const uint64_t seed1) {
  const __m256i inc0 =
      _mm256_setr_epi64x(static_cast<long long>(0x9e3779b97f4a7c15ULL),
                         static_cast<long long>(0xbf58476d1ce4e5b9ULL),
                         static_cast<long long>(0x94d049bb133111ebULL),
                         static_cast<long long>(0x3c6ef372fe94f82bULL));
  const __m256i inc1 =
      _mm256_setr_epi64x(static_cast<long long>(0xa54ff53a5f1d36f1ULL),
                         static_cast<long long>(0x510e527fade682d1ULL),
                         static_cast<long long>(0x9b05688c2b3e6c1fULL),
                         static_cast<long long>(0x1f83d9abfb41bd6bULL));
  const __m256i inc2 =
      _mm256_setr_epi64x(static_cast<long long>(0x5be0cd19137e2179ULL),
                         static_cast<long long>(0x6a09e667f3bcc909ULL),
                         static_cast<long long>(0xbb67ae8584caa73bULL),
                         static_cast<long long>(0x243f6a8885a308d3ULL));
  const __m256i mult = _mm256_setr_epi32(
      static_cast<int>(0x85ebca6bu), 0, static_cast<int>(0xc2b2ae35u), 0,
      static_cast<int>(0x9e3779b1u), 0, static_cast<int>(0x27d4eb2fu), 0);
  const __m256i shuf =
      _mm256_setr_epi8(7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8, 7,
                       6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8);
  const __m256i vs0 = _mm256_set1_epi64x(static_cast<long long>(seed0));
  const __m256i vs1 = _mm256_set1_epi64x(static_cast<long long>(seed1));

  __m256i s0 = _mm256_xor_si256(vs0, inc0);
  __m256i s1 = _mm256_xor_si256(vs1, inc1);
  __m256i s2 = _mm256_xor_si256(_mm256_add_epi64(vs0, vs1), inc2);

  for (int i = 0; i < kMT_N; i += 24) {
    s0 = _mm256_add_epi64(s0, inc0);
    s1 = _mm256_add_epi64(s1, inc1);
    s2 = _mm256_add_epi64(s2, inc2);
    const __m256i x0 = _mm256_xor_si256(s0, _mm256_srli_epi64(s0, 29));
    const __m256i x1 = _mm256_xor_si256(s1, _mm256_srli_epi64(s1, 29));
    const __m256i x2 = _mm256_xor_si256(s2, _mm256_srli_epi64(s2, 29));
    const __m256i y0 = _mm256_mul_epu32(x0, mult);
    const __m256i y1 = _mm256_mul_epu32(x1, mult);
    const __m256i y2 = _mm256_mul_epu32(x2, mult);
    const __m256i z0 = _mm256_xor_si256(_mm256_xor_si256(y0, x0),
                                        _mm256_shuffle_epi8(y0, shuf));
    const __m256i z1 = _mm256_xor_si256(_mm256_xor_si256(y1, x1),
                                        _mm256_shuffle_epi8(y1, shuf));
    const __m256i z2 = _mm256_xor_si256(_mm256_xor_si256(y2, x2),
                                        _mm256_shuffle_epi8(y2, shuf));
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(mt + i), z0);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(mt + i + 8), z1);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(mt + i + 16), z2);
  }
  mt[0] |= kUpperMask;
}
#endif

#if defined(GLOOP_MT_RANDOM_USE_NEON)
void FillDefaultStateNeon(uint32_t* const mt, const uint64_t seed0,
                          const uint64_t seed1) {
  const uint64_t kInc0[2] = {0x9e3779b97f4a7c15ULL, 0xbf58476d1ce4e5b9ULL};
  const uint64_t kInc1[2] = {0x94d049bb133111ebULL, 0x3c6ef372fe94f82bULL};
  const uint32_t kMult[2] = {0x85ebca6bu, 0xc2b2ae35u};
  const uint64x2_t inc0 = vld1q_u64(kInc0);
  const uint64x2_t inc1 = vld1q_u64(kInc1);
  const uint32x2_t mult = vld1_u32(kMult);
  const uint64x2_t vs0 = vdupq_n_u64(seed0);
  const uint64x2_t vs1 = vdupq_n_u64(seed1);

  uint64x2_t s0 = veorq_u64(vs0, inc0);
  uint64x2_t s1 = veorq_u64(vs1, inc1);

  for (int i = 0; i < kMT_N; i += 8) {
    s0 = vaddq_u64(s0, inc0);
    s1 = vaddq_u64(s1, inc1);
    const uint64x2_t x0 = veorq_u64(s0, vshrq_n_u64(s0, 29));
    const uint64x2_t x1 = veorq_u64(s1, vshrq_n_u64(s1, 29));
    const uint64x2_t y0 = vmull_u32(vmovn_u64(x0), mult);
    const uint64x2_t y1 = vmull_u32(vmovn_u64(x1), mult);
    const uint64x2_t r0 =
        vreinterpretq_u64_u8(vrev64q_u8(vreinterpretq_u8_u64(y0)));
    const uint64x2_t r1 =
        vreinterpretq_u64_u8(vrev64q_u8(vreinterpretq_u8_u64(y1)));
    const uint64x2_t z0 = veorq_u64(veorq_u64(y0, x0), r0);
    const uint64x2_t z1 = veorq_u64(veorq_u64(y1, x1), r1);
    vst1q_u32(mt + i, vreinterpretq_u32_u64(z0));
    vst1q_u32(mt + i + 4, vreinterpretq_u32_u64(z1));
  }
  mt[0] |= kUpperMask;
}
#endif

#if !defined(GLOOP_MT_RANDOM_USE_AVX2) && !defined(GLOOP_MT_RANDOM_USE_NEON)
void FillDefaultStateScalar(uint32_t* const mt, const uint64_t seed0,
                            const uint64_t seed1) {
  uint64_t s0 = seed0 ^ kGamma0;
  uint64_t s1 = seed1 ^ kGamma1;
  for (int i = 0; i < kMT_N; i += 4) {
    s0 += kGamma0;
    s1 += kGamma1;
    const uint64_t r0 = Mix64(s0);
    const uint64_t r1 = Mix64(s1);
    mt[i] = static_cast<uint32_t>(r0);
    mt[i + 1] = static_cast<uint32_t>(r0 >> 32);
    mt[i + 2] = static_cast<uint32_t>(r1);
    mt[i + 3] = static_cast<uint32_t>(r1 >> 32);
  }
  mt[0] |= kUpperMask;
}
#endif

ABSL_ATTRIBUTE_ALWAYS_INLINE inline void FillDefaultState(
    uint32_t* const mt, const uint64_t seed0, const uint64_t seed1) {
#if defined(GLOOP_MT_RANDOM_USE_AVX2)
  FillDefaultStateAvx2(mt, seed0, seed1);
#elif defined(GLOOP_MT_RANDOM_USE_NEON)
  FillDefaultStateNeon(mt, seed0, seed1);
#else
  FillDefaultStateScalar(mt, seed0, seed1);
#endif
}

}  // namespace

ABSL_ATTRIBUTE_NOINLINE uint32_t MTRandom::CycleAndRand32() {
  Cycle();
  context_.randcnt = 1;
  return context_.buffer[0];
}

ABSL_ATTRIBUTE_NOINLINE uint64_t MTRandom::CycleAndRand64() {
  if (context_.randcnt == kMTNumWords - 1) {
    const uint64_t hi = context_.buffer[kMTNumWords - 1];
    return (hi << 32) | CycleAndRand32();
  }
  Cycle();
  context_.randcnt = 2;
  return (uint64_t{context_.buffer[0]} << 32) | context_.buffer[1];
}

// Implements all of step 2, step 3, and pre-tempering (step 5).
void MTRandom::Cycle() {
  TwistAndTemperAll624(context_.buffer, context_.tempered);
  context_.tempered = true;
  context_.randcnt = 0;
}

// Raw copy into the mt array.
void MTRandom::InitRaw(const uint32_t* const seed, const int num_words) {
  CHECK_EQ(num_words, kMTNumWords) << ": InitRaw num_words mismatch.";
  context_.poolsize = 0;
  context_.tempered = false;
  context_.pool = 0;
  memcpy(context_.buffer, seed, kMTSizeBytes);
  context_.randcnt = kMTNumWords;
}

void MTRandom::InitSeed(const uint32_t seed) {
  context_.poolsize = 0;
  context_.tempered = false;
  context_.pool = 0;
  InitSeedAll624(context_.buffer, seed);
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
  context_.tempered = false;
  context_.pool = 0;
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
      prev = (kInitArrayBase[idx] ^ (Mix30(prev) * kSeedMul)) + addend[j];
      mt[idx] = prev;
      if (++j == num_words) j = 0;
    }
    mt[0] = prev;
    prev = (mt[1] ^ (Mix30(prev) * kSeedMul)) + addend[j];
    mt[1] = prev;
    i = 2;
  } else {
    for (int idx = 1; idx < kMTNumWords; ++idx) {
      const size_t j = static_cast<size_t>(idx - 1);
      prev = (kInitArrayBase[idx] ^ (Mix30(prev) * kSeedMul)) +
             (LittleEndian::Load32(data + j * 4) + static_cast<uint32_t>(j));
      mt[idx] = prev;
    }
    mt[0] = prev;
    i = 1;
    size_t j = static_cast<size_t>(kMTNumWords - 1);
    while (j < num_full_words) {
      const size_t chunk =
          std::min(static_cast<size_t>(kMTNumWords - i), num_full_words - j);
      for (size_t c = 0; c < chunk; ++c, ++i, ++j) {
        prev = (mt[i] ^ (Mix30(prev) * kSeedMul)) +
               (LittleEndian::Load32(data + j * 4) + static_cast<uint32_t>(j));
        mt[i] = prev;
      }
      if (i == kMTNumWords) {
        mt[0] = prev;
        i = 1;
      }
    }
    if (rem != 0) {
      const uint32_t last_word =
          LoadPartialWordLE(data + num_full_words * 4, rem);
      prev = (mt[i] ^ (Mix30(prev) * kSeedMul)) +
             (last_word + static_cast<uint32_t>(num_words - 1));
      mt[i] = prev;
      if (++i == kMTNumWords) {
        mt[0] = prev;
        i = 1;
      }
    }
  }

  prev = InitArrayLoop3Range(mt, i, kMTNumWords, prev);
  mt[0] = prev;
  InitArrayLoop3Range(mt, 1, i, prev);
  mt[0] = kUpperMask;
  context_.randcnt = kMTNumWords;
}

// -------------------------------------------------------------------
//
// Everything below here is framework code.

MTRandom::MTRandom(const uint32_t seed) { Reset(seed); }

MTRandom::MTRandom(const absl::string_view seed_str) { Reset(seed_str); }

MTRandom::MTRandom(const uint32_t* const seed, const int num_words) {
  Reset(seed, num_words);
}

MTRandom::MTRandom() {
  uint64_t buffer[16];
  memset(buffer, 0, sizeof(buffer));
  const int len =
      RandomBase::WeakSeed(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
  uint64_t seed0 = Mix64(kGamma0 * static_cast<uint64_t>(len + 1));
  uint64_t seed1 = Mix64(kGamma1 * static_cast<uint64_t>(len + 2));
  const int num_words64 = (len + 7) / 8;
#if defined(__clang__)
#pragma clang loop vectorize(disable)
#endif
  for (int i = 0; i < num_words64; ++i) {
    seed0 += Mix64(buffer[i] ^ (kGamma0 * static_cast<uint64_t>(i + 3)));
    seed1 ^= Mix64(buffer[i] + (kGamma1 * static_cast<uint64_t>(i + 5)));
  }

  context_.poolsize = 0;
  context_.tempered = false;
  context_.pool = 0;
  FillDefaultState(context_.buffer, seed0, seed1);
  context_.randcnt = kMTNumWords;
}

MTRandom::MTRandom(const MTContext& context) : context_(context) {}

MTRandom* MTRandom::Clone() const { return new MTRandom(context_); }

void MTRandom::Reset(const uint32_t seed) { InitSeed(seed); }

void MTRandom::Reset(const absl::string_view seed_str) {
  CHECK_GT(seed_str.length(), 0u)
      << ": MTRandom can not be initialized with an empty string";
  InitArray(seed_str);
}

void MTRandom::Reset(const uint32_t* const seed, const int num_words) {
  CHECK_EQ(num_words, kMTNumWords)
      << ": MTRandom must be initialized with a buffer of size " << kMTNumWords;
  InitRaw(seed, num_words);
}

static_assert(sizeof(void*) != 8 || sizeof(MTRandom) == 2520,
              "sizeof(MTRandom) must stay 2520 bytes on 64-bit platforms");

#undef GLOOP_MT_RANDOM_USE_AVX2
#undef GLOOP_MT_RANDOM_USE_NEON
#undef GLOOP_MT_RANDOM_UNPREDICTABLE
