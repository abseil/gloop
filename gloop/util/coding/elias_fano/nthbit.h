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

#include <cstdint>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/numeric/bits.h"

namespace elias_fano {

namespace nthbit_internal {

// Portable fallback. Prefer Nthbit(); this is declared here only so that the
// inline fast path below can fall back to it.
uint64_t NthbitSlow(uint64_t x, uint64_t n);

#if defined(__x86_64__)
// Whether this CPU has a pdep instruction worth using. Decided once, during
// static initialization.
//
// Reading this before its initializer has run yields false, which selects
// NthbitSlow: correct, merely slower. Nthbit() is therefore safe to call from
// another translation unit's static initializer regardless of ordering.
extern const bool kUseFastPath;
#endif

}  // namespace nthbit_internal

// Nthbit(x, n)
//
// Returns the index of the n'th 1 in x. UB if x does not contain n+1 1 bits.
//
// Defined inline because the body is a handful of instructions: an
// out-of-line call costs more than the work it performs.
inline uint64_t Nthbit(uint64_t x, uint64_t n) {
#if defined(__x86_64__)
  if (ABSL_PREDICT_TRUE(nthbit_internal::kUseFastPath)) {
    DCHECK_LT(n, 64);
    DCHECK_GT(absl::popcount(x), n);

    uint64_t p;
    // shlx and pdep are emitted as a single asm block so that the shift count
    // does not have to be routed through %cl, as a plain `uint64_t{1} << n`
    // would require.
    asm("shlx %[n], %[one], %[p]\n\tpdep %[x], %[p], %[p]"
        : [p] "=&r"(p)
        : [one] "r"(uint64_t{1}), [n] "r"(n), [x] "r"(x));

    // x is required to hold at least n+1 set bits, so the deposited bit exists.
    ABSL_ASSUME(p != 0);
    return absl::countr_zero(p);
  }
#endif
  return nthbit_internal::NthbitSlow(x, n);
}

}  // namespace elias_fano

#endif  // THIRD_PARTY_GLOOP_UTIL_CODING_ELIAS_FANO_NTHBIT_H_
