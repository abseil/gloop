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

// -----------------------------------------------------------------------------
// File: mock_distributions.h
// -----------------------------------------------------------------------------
//
// This file contains mock distribution functions for use alongside an
// `absl::MockingBitGen` object within the Googletest testing framework. Such
// mocks are useful to provide deterministic values as return values within
// (otherwise random) Abseil distribution functions.
//
// The return type of each function is a mock expectation object which
// is used to set the match result.
//
// More information about the Googletest testing framework is available at
// https://github.com/google/googletest
//
// EXPECT_CALL and ON_CALL need to be made within the same DLL component as
// the call to absl::Uniform and related methods, otherwise mocking will fail
// since the  underlying implementation creates a type-specific pointer which
// will be distinct across different DLL boundaries.
//
// Internal Googletest information is available at <link>
//
// See <link> for information about the Abseil random
// library
//

#ifndef THIRD_PARTY_GLOOP_UTIL_RANDOM_MOCK_DISTRIBUTIONS_H_
#define THIRD_PARTY_GLOOP_UTIL_RANDOM_MOCK_DISTRIBUTIONS_H_

#include <cstdint>

#include "absl/log/absl_check.h"
#include "absl/random/internal/mock_overload_set.h"
#include "absl/random/mocking_bit_gen.h"
#include "gloop/util/random/internal/skewed_low_distribution.h"
#include "gloop/util/random/internal/small_prime_distribution.h"
#include "gloop/util/random/internal/yule_simon_distribution.h"

namespace util_random {

template <typename IntType>
struct SmallPrimeValidator {
  static void Validate(IntType result, IntType lo, IntType hi) {
    ABSL_CHECK(result >= lo && result <= hi);
    ABSL_CHECK(small_prime_distribution_internal::IsPrime(
        static_cast<uint64_t>(result)));
  }
};

// -----------------------------------------------------------------------------
// util_random::MockSkewedLow
// -----------------------------------------------------------------------------
//
// Matches calls to util_random::SkewedLow.
//
// `util_random::MockSkewedLow` is a class template used in conjunction with
// Googletest's `ON_CALL()` and `EXPECT_CALL()` macros. To use it,
// default-construct an instance of it inside `ON_CALL()` or `EXPECT_CALL()`,
// and use `Call(...)` the same way one would define mocks on a
// Googletest `MockFunction()`.
//
// Example:
//
//  absl::MockingBitGen mock;
//  EXPECT_CALL(util_random::MockSkewedLow<int>(), Call(mock, 1, 10000, 3))
//     .WillOnce(Return(1221));
//  auto x = util_random::SkewedLow<int>(mock, 1, 10000, 3);
//  assert(x == 1221)
//
template <typename IntType>
using MockSkewedLow = absl::random_internal::MockOverloadSet<
    util_random::skewed_low_distribution<IntType>,
    IntType(absl::MockingBitGen&, IntType, IntType, IntType)>;

// -----------------------------------------------------------------------------
// util_random::MockYuleSimon
// -----------------------------------------------------------------------------
//
// Matches calls to util_random::YuleSimon.
//
// `util_random::MockYuleSimon` is a class template used in conjunction with
// Googletest's `ON_CALL()` and `EXPECT_CALL()` macros. To use it,
// default-construct an instance of it inside `ON_CALL()` or `EXPECT_CALL()`,
// and use `Call(...)` the same way one would define mocks on a
// Googletest `MockFunction()`.
//
// Example:
//
//  absl::MockingBitGen mock;
//  EXPECT_CALL(util_random::MockYuleSimon<int>(), Call(mock, 3.0))
//     .WillOnce(Return(1221));
//  auto x = util_random::YuleSimon<int>(mock, 3.0);
//  assert(x == 1221)
//
template <typename IntType>
using MockYuleSimon = absl::random_internal::MockOverloadSet<
    util_random::yule_simon_distribution<IntType>,
    IntType(absl::MockingBitGen&, double)>;

// -----------------------------------------------------------------------------
// util_random::MockSmallPrime
// -----------------------------------------------------------------------------
//
// Matches calls to util_random::SmallPrime.
//
// `util_random::MockSmallPrime` is a class template used in conjunction with
// Googletest's `ON_CALL()` and `EXPECT_CALL()` macros. To use it,
// default-construct an instance of it inside `ON_CALL()` or `EXPECT_CALL()`,
// and use `Call(...)` the same way one would define mocks on a
// Googletest `MockFunction()`.
//
// Example:
//
//  absl::MockingBitGen mock;
//  EXPECT_CALL(util_random::MockSmallPrime<int>(), Call(mock, 2, 1000))
//     .WillOnce(Return(7));
//  auto x = util_random::SmallPrime<int>(mock, 2, 1000);
//  assert(x == 7)
//
template <typename IntType>
using MockSmallPrime = absl::random_internal::MockOverloadSetWithValidator<
    util_random::small_prime_distribution<IntType>,
    SmallPrimeValidator<IntType>,
    IntType(absl::MockingBitGen&, IntType, IntType)>;

}  // namespace util_random

#endif  // THIRD_PARTY_GLOOP_UTIL_RANDOM_MOCK_DISTRIBUTIONS_H_
