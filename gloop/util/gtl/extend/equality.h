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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_EQUALITY_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_EQUALITY_H_

#include <type_traits>

#include "gloop/util/gtl/extend/extend.h"
#include "gloop/util/gtl/extend/internal/is_tuple_hashable.h"

namespace gtl {

// EqualityExtension
//
// A gtl::Extend extension that enables operator==, operator!=, and (if
// possible) absl::Hash for the struct. Each struct field must be
// equality-comparable.
//
// absl::Hash support is provided when all of the fields are themselves
// hashable.
//
// Example:
//
// struct Point : gtl::Extend<Point>::With<EqualityExtension> {
//   int x;
//   int y;
// };
//
// Point p1 = {.x = 3, .y = 4};
// Point p2 = {.x = 3, .y = 4};
// Point p3 = {.x = 3, .y = 5};
// assert(p1 == p2);
// assert(p1 != p3);
// assert(absl::HashOf(p1) == absl::HashOf(p2));

template <typename T>
struct EqualityExtension : Extension<EqualityExtension, T> {
  friend constexpr bool operator==(const T& lhs, const T& rhs) {
    return EqualityExtension::Unpack(lhs) == EqualityExtension::Unpack(rhs);
  }

  friend constexpr bool operator!=(const T& lhs, const T& rhs) {
    return !(lhs == rhs);
  }

  // This template uses decltype() in its return value to enable SFINAE; if any
  // field is not hashable, the extendable struct won't be, either.  We use
  // `Unpack((h, t))` and not a simple `Unpack(t)` in this check to make the
  // expression dependent on H; we want SFINAE to apply when AbslHashValue is
  // instantiated, not when EqualityExtension is instantiated.
  template <typename H>
  friend auto AbslHashValue(H h, const T& t) -> std::enable_if_t<
      internal_extend::IsTupleHashable<
          decltype(EqualityExtension::Unpack((h, t)))>::value,
      H> {
    return H::combine(std::move(h), EqualityExtension::Unpack(t));
  }

  constexpr void ForceInstantiateDependentExpressions() const {
    (void)(static_cast<const T&>(*this) == static_cast<const T&>(*this));
    (void)(static_cast<const T&>(*this) != static_cast<const T&>(*this));
  }

 private:
  using Extension<EqualityExtension, T>::Unpack;
};
}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_EQUALITY_H_
