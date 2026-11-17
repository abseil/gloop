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

#ifndef THIRD_PARTY_GLOOP_UTIL_CODING_ELIAS_FANO_NTHBIT_H_
#define THIRD_PARTY_GLOOP_UTIL_CODING_ELIAS_FANO_NTHBIT_H_

#include <bit>
#include <cstdint>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/numeric/bits.h"

// Exclude Android: Armv9 LITTLE cores with HWCAP2_SVEBITPERM microcode 64-bit
// `bdep z.d` non-pipelined (unfilterable at static init on big.LITTLE), and
// older mobile kernels discard SVE state across syscalls.
#if defined(__aarch64__) && defined(__linux__) && !defined(__ANDROID__)
#define ELIAS_FANO_INTERNAL_HAVE_ARM_SVE2_BDEP 1
#endif

namespace elias_fano {

namespace nthbit_internal {

// Portable fallback. Prefer Nthbit(); this is declared here only so that the
// inline fast path below can fall back to it.
uint64_t NthbitSlow(uint64_t x, uint64_t n);

#if defined(__x86_64__) || defined(ELIAS_FANO_INTERNAL_HAVE_ARM_SVE2_BDEP)
// Whether this CPU has a fast hardware bit-deposit instruction (BMI2 `pdep` on
// x86-64, FEAT_SVE_BitPerm `bdep` on AArch64). Decided once, during static
// initialization.
//
// Reading this before its initializer has run yields false, which selects
// NthbitSlow: correct, merely slower. Nthbit() is therefore safe to call from
// another translation unit's static initializer regardless of ordering.
extern const bool kUseFastPath;
#endif

#if defined(ELIAS_FANO_INTERNAL_HAVE_ARM_SVE2_BDEP)
// Scatters contiguous low bits of `src` into set-bit positions of `mask` using
// SVE2-BitPerm `bdep z.d`. Emitted with a local `.arch_extension` directive so
// baseline Armv8-A builds remain compatible with CPUs lacking SVE2-BitPerm.
// Returns 0 if `popcount(mask)` is too small to place any set bit of `src`.
// REQUIRES: kUseFastPath is true.
ABSL_ATTRIBUTE_ALWAYS_INLINE inline uint64_t ArmSve2Bdep64(uint64_t src,
                                                           uint64_t mask) {
  double z = std::bit_cast<double>(src);
  const double m = std::bit_cast<double>(mask);
  asm(".arch_extension sve2-bitperm\n\tbdep %z[z].d, %z[z].d, %z[m].d"
      : [z] "+w"(z)
      : [m] "w"(m));
  return std::bit_cast<uint64_t>(z);
}
#endif

#if defined(__x86_64__) || defined(ELIAS_FANO_INTERNAL_HAVE_ARM_SVE2_BDEP)
// Returns `uint64_t{1} << Nthbit(x, n)`.
// REQUIRES: kUseFastPath, n < 64, and popcount(x) > n.
ABSL_ATTRIBUTE_ALWAYS_INLINE inline uint64_t DepositBit(uint64_t x,
                                                        uint64_t n) {
  uint64_t p;
#if defined(__x86_64__)
  // shlx and pdep are emitted as a single asm block so that the shift count
  // does not have to be routed through %cl, as a plain `uint64_t{1} << n`
  // would require.
  asm("shlx %[n], %[one], %[p]\n\tpdep %[x], %[p], %[p]"
      : [p] "=&r"(p)
      : [one] "r"(uint64_t{1}), [n] "r"(n), [x] "r"(x));
#else
  p = ArmSve2Bdep64(uint64_t{1} << n, x);
#endif
  // x is required to hold at least n+1 set bits, so the deposited bit exists.
  ABSL_ASSUME(p != 0);
  return p;
}
#endif

}  // namespace nthbit_internal

// Returns `{b, after}` where `b == Nthbit(x, n)` and `after` holds the bits of
// `x` strictly above bit `b` (0 if `b` was the highest set bit). UB if `x` does
// not contain `n + 1` set bits.
ABSL_ATTRIBUTE_ALWAYS_INLINE inline std::pair<uint64_t, uint64_t>
NthbitAndAfter(uint64_t x, uint64_t n) {
#if defined(__x86_64__) || defined(ELIAS_FANO_INTERNAL_HAVE_ARM_SVE2_BDEP)
  if (ABSL_PREDICT_TRUE(nthbit_internal::kUseFastPath)) {
    DCHECK_LT(n, 64);
    DCHECK_GT(absl::popcount(x), n);
    const uint64_t p = nthbit_internal::DepositBit(x, n);
    return {static_cast<uint64_t>(absl::countr_zero(p)),
            x & (uint64_t{0} - (p + p))};
  }
#endif
  const uint64_t b = nthbit_internal::NthbitSlow(x, n);
  return {b, x & ((~uint64_t{1}) << b)};
}

// Nthbit(x, n)
//
// Returns the index of the n'th 1 in x. UB if x does not contain n+1 1 bits.
//
// Defined inline because the body is a handful of instructions: an
// out-of-line call costs more than the work it performs.
ABSL_ATTRIBUTE_ALWAYS_INLINE inline uint64_t Nthbit(uint64_t x, uint64_t n) {
  return NthbitAndAfter(x, n).first;
}

// Returns `Nthbit(x, next_n)` given `after == NthbitAndAfter(x, next_n -
// 1).second` with `after != 0`. Reuses `after` on AArch64 and `NthbitSlow`,
// while issuing an independent overlapping `pdep` on x86-64 fast-pdep CPUs
// (letting DCE drop `after` on that branch).
ABSL_ATTRIBUTE_ALWAYS_INLINE inline uint64_t NthbitNext(uint64_t x,
                                                        uint64_t next_n,
                                                        uint64_t after) {
  DCHECK_NE(after, 0u);
#if defined(__x86_64__)
  if (ABSL_PREDICT_TRUE(nthbit_internal::kUseFastPath)) {
    return static_cast<uint64_t>(
        absl::countr_zero(nthbit_internal::DepositBit(x, next_n)));
  }
#else
  (void)x;
  (void)next_n;
#endif
  ABSL_ASSUME(after != 0);
  return static_cast<uint64_t>(absl::countr_zero(after));
}

// Whether TryDepositNthbit() evaluates the hardware bit-deposit path (so a 0
// return guarantees `popcount(x) <= n`). Always false on architectures where
// scalar `popcount` is already cheap enough to guard the first word directly.
ABSL_ATTRIBUTE_ALWAYS_INLINE inline bool HasTryDepositNthbit() {
#if defined(ELIAS_FANO_INTERNAL_HAVE_ARM_SVE2_BDEP)
  return nthbit_internal::kUseFastPath;
#else
  return false;
#endif
}

// Speculatively computes `uint64_t{1} << Nthbit(x, n)` on AArch64 CPUs with
// SVE2-BitPerm without a prior `popcount(x)` check. Returns 0 when
// `!HasTryDepositNthbit()`, `n >= 64`, or `popcount(x) <= n`.
ABSL_ATTRIBUTE_ALWAYS_INLINE inline uint64_t TryDepositNthbit(uint64_t x,
                                                              uint64_t n) {
#if defined(ELIAS_FANO_INTERNAL_HAVE_ARM_SVE2_BDEP)
  if (ABSL_PREDICT_TRUE(nthbit_internal::kUseFastPath && n < 64)) {
    return nthbit_internal::ArmSve2Bdep64(uint64_t{1} << n, x);
  }
#else
  (void)x;
  (void)n;
#endif
  return 0;
}

}  // namespace elias_fano

#endif  // THIRD_PARTY_GLOOP_UTIL_CODING_ELIAS_FANO_NTHBIT_H_
