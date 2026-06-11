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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_FLOAT128_FUZZTEST_DOMAIN_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_FLOAT128_FUZZTEST_DOMAIN_H_

#include <bit>
#include <limits>
#include <optional>
#include <tuple>

#include "absl/numeric/int128.h"
#include "fuzztest/fuzztest.h"
#include "gloop/util/gtl/float128.h"

namespace gtl {

// The equivalent of `fuzztest::Arbitrary<gtl::Float128>()`. This is a separate
// function since `fuzztest::Arbitrary` can't be extended to support
// user-defined types.
inline auto ArbitraryFloat128() {
  using ::fuzztest::Just;
  return fuzztest::OneOf(
      Just(Float128(0.0)), Just(Float128(-0.0)), Just(Float128(1.0)),
      Just(Float128(-1.0)), Just(std::numeric_limits<Float128>::max()),
      Just(std::numeric_limits<Float128>::infinity()),
      Just(-std::numeric_limits<Float128>::infinity()),
      Just(std::numeric_limits<Float128>::quiet_NaN()),
      Just(std::numeric_limits<Float128>::signaling_NaN()),
      fuzztest::ReversibleMap(
          [](absl::uint128 bits) {
            return Float128(Float128::FromCompilerQuadTag{},
                            std::bit_cast<Float128::CompilerQuad>(bits));
          },
          [](Float128 x) {
            return std::optional(
                std::tuple(std::bit_cast<absl::uint128>(x.data_)));
          },
          fuzztest::Arbitrary<absl::uint128>()));
}

// The equivalent of `fuzztest::Finite<gtl::Float128>()`.
inline auto FiniteFloat128() {
  return fuzztest::Filter(static_cast<bool (*)(gtl::Float128)>(isfinite),
                          ArbitraryFloat128());
}

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_FLOAT128_FUZZTEST_DOMAIN_H_
