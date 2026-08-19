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

// Fast approximations to log, exp, pow.
// The following functions are implemented.
// The "vf" versions are based on a table lookup.
// The "f" versions are based on interpolation between values in the table.
// The "2" versions indicate log or exp base 2.  These are faster.
// The "d" versions run on doubles and are slower.
//
// The functions produce undefined results on overflow/underflow.
// Bounds checking is only done in debug mode.
//
// vfpow and vfpowd are approximations to pow() using the table lookup method.
//
// * Choosing between "fast" and "very fast"?
// Very Fast: kBits = 7 implies that values less than 1+1/128
//            truncate to 1, leading to errors of up to about 0.8% in the arg
//            of vflog2, and comparable errors in the result of vfexp2.
// Fast:      Linearly interpolate over those small intervals, making a decent
//            continuous function with typically several digits of accuracy.
//
// For the d versions, I've had all kinds of trouble with getting incorrect
// answers when the compiler badly reordered the operations.  I moved some of
// them to the .cc file to prevent inlining, and they seem to be working now.
//

// To see current timing information, look at the unit test output
#ifndef THIRD_PARTY_GLOOP_UTIL_MATH_FASTMATH_H_
#define THIRD_PARTY_GLOOP_UTIL_MATH_FASTMATH_H_

#include <cassert>
#include <cmath>
#include <cstddef>

#include "absl/base/casts.h"
#include "hwy/highway.h"

#ifdef M_LN2
const float FASTMATH_LOG_2_F = M_LN2;
const double FASTMATH_LOG_2_D = M_LN2;
#else
const float FASTMATH_LOG_2_F = 0.693147180559945309417;
const double FASTMATH_LOG_2_D = 0.693147180559945309417;
#endif
// 1/ln(2) = ln(e)/ln(2) = log_2(e)
#ifdef M_LOG2E
const float FASTMATH_INV_LOG_2_F = M_LOG2E;
const double FASTMATH_INV_LOG_2_D = M_LOG2E;
#else
const float FASTMATH_INV_LOG_2_F = 1.44269504088896340736;
const double FASTMATH_INV_LOG_2_D = 1.44269504088896340736;
#endif

namespace fastmath_internal {
namespace hn = hwy::HWY_NAMESPACE;

// Highway Remez minimax vector approximation for 2^x
template <class D, class V = hn::VFromD<D>>
HWY_INLINE V FastExp2Vec(D d, V x) {
  const hn::RebindToSigned<D> di;
  const auto q_i = hn::NearestInt(x);
  const auto q_f = hn::ConvertTo(d, q_i);
  const auto r = hn::Sub(x, q_f);

  // Remez minimax polynomial for 2^r on [-0.5, 0.5]
  const auto c0 = hn::Set(d, 1.0000000015f);
  const auto c1 = hn::Set(d, 0.6931471806f);
  const auto c2 = hn::Set(d, 0.2402265070f);
  const auto c3 = hn::Set(d, 0.0555041087f);
  const auto c4 = hn::Set(d, 0.0096181291f);
  const auto c5 = hn::Set(d, 0.0013333558f);

  auto p = hn::MulAdd(c5, r, c4);
  p = hn::MulAdd(p, r, c3);
  p = hn::MulAdd(p, r, c2);
  p = hn::MulAdd(p, r, c1);
  p = hn::MulAdd(p, r, c0);

  // IEEE 754 exponent bit synthesis: 2^q = (q + 127) << 23
  const auto exp_bits = hn::ShiftLeft<23>(hn::Add(q_i, hn::Set(di, 127)));
  const auto scale = hn::BitCast(d, exp_bits);

  return hn::Mul(p, scale);
}

// Highway Remez minimax vector approximation for e^x
template <class D, class V = hn::VFromD<D>>
HWY_INLINE V FastExpVec(D d, V x) {
  const auto kInvLn2 = hn::Set(d, FASTMATH_INV_LOG_2_F);
  return FastExp2Vec(d, hn::Mul(x, kInvLn2));
}

// Highway Remez minimax vector approximation for log2(x)
template <class D, class V = hn::VFromD<D>>
HWY_INLINE V FastLog2Vec(D d, V x) {
  const hn::RebindToSigned<D> di;
  const hn::RebindToUnsigned<D> du;

  // Range reduction: shift exponent boundary by sqrt(0.5) ~ 0.70710678
  // kExpMagicDiff = 0x3F800000 - 0x3F3504F3 = 0x004AFB0D
  const auto kExpMagicDiff = hn::Set(di, 0x004AFB0D);
  const auto u = hn::BitCast(di, x);
  const auto exp_bits = hn::Add(u, kExpMagicDiff);
  const auto kBias = hn::Set(di, 127);
  const auto q_i = hn::Sub(
      hn::BitCast(di, hn::ShiftRight<23>(hn::BitCast(du, exp_bits))), kBias);
  const auto exp_int_shifted = hn::ShiftLeft<23>(q_i);
  const auto y_bits = hn::Sub(u, exp_int_shifted);
  const auto y = hn::BitCast(d, y_bits);

  const auto z = hn::Sub(y, hn::Set(d, 1.0f));

  // Remez minimax polynomial for log2(1+z) / z on [-0.2929, 0.4142]
  const auto c1 = hn::Set(d, 1.4426975712963104f);
  const auto c2 = hn::Set(d, -0.7213638661111215f);
  const auto c3 = hn::Set(d, 0.48057606259606056f);
  const auto c4 = hn::Set(d, -0.3593981546865238f);
  const auto c5 = hn::Set(d, 0.29628281298406994f);
  const auto c6 = hn::Set(d, -0.26890557881560556f);
  const auto c7 = hn::Set(d, 0.16918863644276905f);

  auto p = hn::MulAdd(c7, z, c6);
  p = hn::MulAdd(p, z, c5);
  p = hn::MulAdd(p, z, c4);
  p = hn::MulAdd(p, z, c3);
  p = hn::MulAdd(p, z, c2);
  p = hn::MulAdd(p, z, c1);

  const auto q_f = hn::ConvertTo(d, q_i);
  return hn::MulAdd(z, p, q_f);
}

// Highway Remez minimax vector approximation for ln(x)
template <class D, class V = hn::VFromD<D>>
HWY_INLINE V FastLogVec(D d, V x) {
  const auto kLn2 = hn::Set(d, FASTMATH_LOG_2_F);
  return hn::Mul(FastLog2Vec(d, x), kLn2);
}

}  // namespace fastmath_internal

// fast logs and exps on floating point numbers.
class FastMathClass {
 private:
  static constexpr int kBits = 7;
  static constexpr int kMask1 = (1 << kBits) - 1;
  static constexpr int kMask2 = 0xFF << kBits;
  static constexpr int kMask3 = (1 << (23 - kBits)) - 1;
  static constexpr int kMask4 = (1 << (15 - kBits)) - 1;

  struct Table {
    float log[1 << kBits];
    float log_diff[1 << kBits];
    int exp1[1 << kBits];
    float exp2[1 << (15 - kBits)];
  };

 public:
  // fast binary log (~7ns).
  float FastLog2(float f) const {
    assert(f > 0);
    const hwy::HWY_NAMESPACE::ScalableTag<float> d;
    return hwy::HWY_NAMESPACE::GetLane(
        fastmath_internal::FastLog2Vec(d, hwy::HWY_NAMESPACE::Set(d, f)));
  }

  // really fast and inaccurate binary log (~3ns)
  float VeryFastLog2(float f) const {
    assert(f > 0);
    const int x = absl::bit_cast<int>(f);
    const int y = x >> (23 - kBits) & kMask1;
    return ((x >> 23) & 0xFF) - 127 + cache_.log[y];
  }

  // fast 2^f (~9ns)
  float FastExp2(float f) const {
    assert(fabs(f) <= 126);
    const hwy::HWY_NAMESPACE::ScalableTag<float> d;
    return hwy::HWY_NAMESPACE::GetLane(
        fastmath_internal::FastExp2Vec(d, hwy::HWY_NAMESPACE::Set(d, f)));
  }

  // really fast and inaccurate 2^f (~4ns)
  float VeryFastExp2(float f) const {
    assert(fabs(f) <= 126);
    const float g = f + (127 + (1 << (23 - kBits)));
    const int x = absl::bit_cast<int>(g);
    int ret = ((x & kMask2) << (23 - kBits)) | cache_.exp1[x & kMask1];
    return absl::bit_cast<float>(ret);
  }

  // versions of the above for natural logs.
  float FastExp(float f) const { return FastExp2(f * FASTMATH_INV_LOG_2_F); }
  float VeryFastExp(float f) const {
    return VeryFastExp2(f * FASTMATH_INV_LOG_2_F);
  }
  float FastLog(float f) const { return FastLog2(f) * FASTMATH_LOG_2_F; }
  float VeryFastLog(float f) const {
    return VeryFastLog2(f) * FASTMATH_LOG_2_F;
  }

  // Vectorized batch evaluation functions
  void FastExpVector(const float* HWY_RESTRICT in, float* HWY_RESTRICT out,
                     size_t size) const {
    const hwy::HWY_NAMESPACE::ScalableTag<float> d;
    const size_t N = hwy::HWY_NAMESPACE::Lanes(d);
    size_t i = 0;
    for (; i + N <= size; i += N) {
      const auto vx = hwy::HWY_NAMESPACE::LoadU(d, in + i);
      const auto vy = fastmath_internal::FastExpVec(d, vx);
      hwy::HWY_NAMESPACE::StoreU(vy, d, out + i);
    }
    for (; i < size; ++i) {
      out[i] = FastExp(in[i]);
    }
  }

  void FastLogVector(const float* HWY_RESTRICT in, float* HWY_RESTRICT out,
                     size_t size) const {
    const hwy::HWY_NAMESPACE::ScalableTag<float> d;
    const size_t N = hwy::HWY_NAMESPACE::Lanes(d);
    size_t i = 0;
    for (; i + N <= size; i += N) {
      const auto vx = hwy::HWY_NAMESPACE::LoadU(d, in + i);
      const auto vy = fastmath_internal::FastLogVec(d, vx);
      hwy::HWY_NAMESPACE::StoreU(vy, d, out + i);
    }
    for (; i < size; ++i) {
      out[i] = FastLog(in[i]);
    }
  }

  void FastExp2Vector(const float* HWY_RESTRICT in, float* HWY_RESTRICT out,
                      size_t size) const {
    const hwy::HWY_NAMESPACE::ScalableTag<float> d;
    const size_t N = hwy::HWY_NAMESPACE::Lanes(d);
    size_t i = 0;
    for (; i + N <= size; i += N) {
      const auto vx = hwy::HWY_NAMESPACE::LoadU(d, in + i);
      const auto vy = fastmath_internal::FastExp2Vec(d, vx);
      hwy::HWY_NAMESPACE::StoreU(vy, d, out + i);
    }
    for (; i < size; ++i) {
      out[i] = FastExp2(in[i]);
    }
  }

  void FastLog2Vector(const float* HWY_RESTRICT in, float* HWY_RESTRICT out,
                      size_t size) const {
    const hwy::HWY_NAMESPACE::ScalableTag<float> d;
    const size_t N = hwy::HWY_NAMESPACE::Lanes(d);
    size_t i = 0;
    for (; i + N <= size; i += N) {
      const auto vx = hwy::HWY_NAMESPACE::LoadU(d, in + i);
      const auto vy = fastmath_internal::FastLog2Vec(d, vx);
      hwy::HWY_NAMESPACE::StoreU(vy, d, out + i);
    }
    for (; i < size; ++i) {
      out[i] = FastLog2(in[i]);
    }
  }

  struct InternalTestAccess {
    static Table actual() { return cache_; }
    static Table expected() { return GenerateTable(); }
  };

 private:
  static Table GenerateTable();

  static const Table cache_;
};

// fast logs and exps on doubles.
class FastMathDClass {
 private:
  static constexpr int kBits = 7;
  static constexpr int kMask1 = (1 << kBits) - 1;
  static constexpr int kMask2 = (1 << (20 - kBits)) - 1;
  static constexpr int kMask3 = 0x7FF << kBits;

  struct Table {
    double log[1 << kBits];
    double log_diff1[1 << kBits];
    double log_diff2[1 << kBits];
    int exp1[1 << kBits];
    double exp2[1 << kBits];
    double magic;
  };

 public:
  // fast binary log (~10ns).
  double FastLog2(double d) const;

  // really fast and inaccurate binary log (~3ns)
  double VeryFastLog2(double d) const;

  // fast 2^f (~9ns)
  double FastExp2(double d) const;

  // really fast and inaccurate 2^f (~4ns)
  double VeryFastExp2(double d) const;

  // versions of the above for natural logs.
  double FastExp(double f) const { return FastExp2(f * FASTMATH_INV_LOG_2_D); }
  double VeryFastExp(double f) const {
    return VeryFastExp2(f * FASTMATH_INV_LOG_2_D);
  }
  double FastLog(double f) const { return FastLog2(f) * FASTMATH_LOG_2_D; }
  double VeryFastLog(double f) const {
    return VeryFastLog2(f) * FASTMATH_LOG_2_D;
  }

  struct InternalTestAccess {
    static Table actual() { return cache_; }
    static Table expected() { return GenerateTable(); }
  };

 private:
  static Table GenerateTable();
  static const Table cache_;
};

extern FastMathClass FastMathInstance;
extern FastMathDClass FastMathDInstance;

inline float vflog2(float f) { return FastMathInstance.VeryFastLog2(f); }
inline float flog2(float f) { return FastMathInstance.FastLog2(f); }
inline float vfexp2(float f) { return FastMathInstance.VeryFastExp2(f); }
inline float fexp2(float f) { return FastMathInstance.FastExp2(f); }
inline float vflog(float f) { return FastMathInstance.VeryFastLog(f); }
inline float flog(float f) { return FastMathInstance.FastLog(f); }
inline float vfexp(float f) { return FastMathInstance.VeryFastExp(f); }
inline float fexp(float f) { return FastMathInstance.FastExp(f); }

inline void FastExpVector(const float* in, float* out, size_t size) {
  FastMathInstance.FastExpVector(in, out, size);
}
inline void FastLogVector(const float* in, float* out, size_t size) {
  FastMathInstance.FastLogVector(in, out, size);
}
inline void FastExp2Vector(const float* in, float* out, size_t size) {
  FastMathInstance.FastExp2Vector(in, out, size);
}
inline void FastLog2Vector(const float* in, float* out, size_t size) {
  FastMathInstance.FastLog2Vector(in, out, size);
}

inline double vflog2d(double d) { return FastMathDInstance.VeryFastLog2(d); }
inline double flog2d(double d) { return FastMathDInstance.FastLog2(d); }
inline double vfexp2d(double d) { return FastMathDInstance.VeryFastExp2(d); }
inline double fexp2d(double d) { return FastMathDInstance.FastExp2(d); }
inline double vflogd(double d) { return FastMathDInstance.VeryFastLog(d); }
inline double flogd(double d) { return FastMathDInstance.FastLog(d); }
inline double vfexpd(double d) { return FastMathDInstance.VeryFastExp(d); }
inline double fexpd(double d) { return FastMathDInstance.FastExp(d); }

// Note that for all the implementations below, pow(0, ...) behaves badly.
// See bug http://b/3461293
inline float vfpow(float a, float b) { return vfexp2(vflog2(a) * b); }
inline float fpow(float a, float b) { return fexp2(flog2(a) * b); }
inline float vfpowd(double a, double b) { return vfexp2d(vflog2d(a) * b); }
inline float fpowd(double a, double b) { return fexp2d(flog2d(a) * b); }
inline double LogOdds2Prob(double log_odds) {
  return 1.0 / (1.0 + exp(-log_odds));
}
inline float fLogOdds2Prob(float log_odds) {
  if (log_odds >= 17) {
    // Here, we're concerned with the addition 1 + fexp() rounding back to 1.
    // 17 = -floor(log(MathUtil<float>::kEpsilon)).
    return 1;
  } else if (log_odds <= -87) {
    // Here, we're concerned about overflow in fexp().
    // 87 = floor(126/ln(2)). 126 is the limit for FastExp2
    return 0;
  } else {
    return 1.0f / (1.0f + fexp(-log_odds));
  }
}
inline double Prob2LogOdds(double prob) { return log(prob / (1.0 - prob)); }
inline float fProb2LogOdds(float prob) {
  return flog(prob) - flog(1.0f - prob);
}

#endif  // THIRD_PARTY_GLOOP_UTIL_MATH_FASTMATH_H_
