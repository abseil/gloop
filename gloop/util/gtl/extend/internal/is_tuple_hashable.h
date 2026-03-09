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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_INTERNAL_IS_TUPLE_HASHABLE_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_INTERNAL_IS_TUPLE_HASHABLE_H_

#include <tuple>
#include <type_traits>

#include "absl/hash/hash.h"

namespace gtl::internal_extend {

// Trait to check if all members of a tuple are absl::Hash-able.
//
template <typename T>
struct IsTupleHashable {};

// TODO: We wouldn't need absl::Hash if the hasher object provided
// an "is_hashable" trait.
template <typename... Ts>
struct IsTupleHashable<std::tuple<Ts...>>
    : std::integral_constant<bool,
                             (std::is_constructible_v<absl::Hash<Ts>> && ...)> {
};

}  // namespace gtl::internal_extend

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_INTERNAL_IS_TUPLE_HASHABLE_H_
