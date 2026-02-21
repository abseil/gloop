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

// IWYU pragma: private

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_UNCHECKED_TAGS_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_UNCHECKED_TAGS_H_

namespace gtl {

// Tag types for unsafe constructors.
struct sorted_container_t {
  constexpr explicit sorted_container_t() = default;
};
inline constexpr sorted_container_t sorted_container;

struct sorted_unique_container_t : sorted_container_t {
  constexpr explicit sorted_unique_container_t() = default;
};
inline constexpr sorted_unique_container_t sorted_unique_container;

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_UNCHECKED_TAGS_H_
