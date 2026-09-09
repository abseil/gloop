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

// This file defines a free function ValueOrDie that can be used to safely
// dereference absl::StatusOr, std::optional and pointer values (or in fact any
// value that provides a compatible 'operator*' and is contextually convertible
// to bool).
//
// Example usage:
//
// int UnwrapNestedValueOrDie(absl::StatusOr<std::optional<int>> value) {
//   return gtl::ValueOrDie(gtl::ValueOrDie(value));
// }

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_VALUE_OR_DIE_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_VALUE_OR_DIE_H_

#include <type_traits>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/source_location.h"

namespace gtl {
namespace internal_value_or_die {

// LOG(FATAL), with a source location and an optional 'status' for details.
ABSL_ATTRIBUTE_NORETURN void DieBecauseEmptyValue(
    absl::SourceLocation loc, const absl::Status* status = nullptr);

// SFINAE helper that checks whether StatusOr<T>'s T satisfies the given Trait.
template <template <class> class Trait, class T>
Trait<T> IsStatusOrOf(const absl::StatusOr<T>&);

}  // namespace internal_value_or_die

template <int&... kDoNotSpecify, typename T>
std::enable_if_t<decltype(internal_value_or_die::IsStatusOrOf<std::is_object>(
                     std::declval<T>()))::value,
                 decltype(*std::declval<T>())>
ValueOrDie(T&& value ABSL_ATTRIBUTE_LIFETIME_BOUND,
           absl::SourceLocation loc = absl::SourceLocation::current()) {
  if (ABSL_PREDICT_FALSE(!value.ok())) {
    internal_value_or_die::DieBecauseEmptyValue(loc, &value.status());
  }
  return *std::forward<T>(value);
}

// As above, but without ABSL_ATTRIBUTE_LIFETIME_BOUND.
template <int&... kDoNotSpecify, typename T>
std::enable_if_t<decltype(internal_value_or_die::IsStatusOrOf<
                          std::is_reference>(std::declval<T>()))::value,
                 decltype(*std::declval<T>())>
ValueOrDie(T&& value,
           absl::SourceLocation loc = absl::SourceLocation::current()) {
  if (ABSL_PREDICT_FALSE(!value.ok())) {
    internal_value_or_die::DieBecauseEmptyValue(loc, &value.status());
  }
  return *std::forward<T>(value);
}

template <int&... kDoNotSpecify, typename T>
std::enable_if_t<
    std::is_object_v<decltype(static_cast<bool>(std::declval<T>()))>,
    decltype(*std::declval<T>())>
ValueOrDie(T&& value ABSL_ATTRIBUTE_LIFETIME_BOUND,
           absl::SourceLocation loc = absl::SourceLocation::current()) {
  if (ABSL_PREDICT_FALSE(!value)) {
    internal_value_or_die::DieBecauseEmptyValue(loc);
  }
  return *std::forward<T>(value);
}

template <int&... kDoNotSpecify, typename T>
T& ValueOrDie(T* value ABSL_ATTRIBUTE_LIFETIME_BOUND,
              absl::SourceLocation loc = absl::SourceLocation::current()) {
  if (ABSL_PREDICT_FALSE(!value)) {
    internal_value_or_die::DieBecauseEmptyValue(loc);
  }
  return *value;
}

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_VALUE_OR_DIE_H_
