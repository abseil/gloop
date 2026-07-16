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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_SUBSPAN_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_SUBSPAN_H_

// TODO: Remove this file once all callers migrate to subspan().

#include <stddef.h>

#include "absl/base/macros.h"
#include "absl/types/any_span.h"

namespace gtl {

template <class T>
[[deprecated("Use .subspan() instead, manually truncating if needed.")]]
constexpr absl::AnySpan<T> SubspanOrTruncate(
    absl::AnySpan<T> span, typename absl::AnySpan<T>::size_type pos,
    typename absl::AnySpan<T>::size_type len) {
  return span.subspan(pos, (std::min)(len, span.size() - pos));
}

template <class T>
[[deprecated("Use .subspan() instead.")]]
ABSL_REFACTOR_INLINE constexpr absl::AnySpan<T> SubspanOrTruncate(
    absl::AnySpan<T> span, typename absl::AnySpan<T>::size_type pos) {
  return span.subspan(pos);
}

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_SUBSPAN_H_
