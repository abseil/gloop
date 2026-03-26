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

#include <stddef.h>

#include "absl/base/macros.h"
#include "absl/types/any_span.h"

namespace gtl {

// Temporary migration aid for subspan() calls that are not intended truncate.
// Use this while the old truncating subspan() is still present, to indicate
// that truncation is undesired. Once the old subspan() is gone, your call
// will be migrated to the new (non-truncating) subspan().
//
// TODO: Remove this once the old subspan() is gone.
template <class T>
constexpr absl::AnySpan<T> Subspan(
    absl::AnySpan<T> span, typename absl::AnySpan<T>::size_type pos,
    typename absl::AnySpan<T>::size_type len = absl::AnySpan<T>::npos) {
  // No need to check for position being in-bounds since that is already checked
  // by the subspan() call.
  ABSL_HARDENING_ASSERT(len == absl::AnySpan<T>::npos ||
                        len <= span.size() - pos);
  return span.subspan(pos, len);
}

// Temporary migration aid for subspan() calls that are intended to truncate.
// Use this while the old truncating subspan() is still present, to indicate
// that truncation is desired.
//
// TODO: Remove this once the old subspan() is gone.
template <class T>
constexpr absl::AnySpan<T> SubspanOrTruncate(
    absl::AnySpan<T> span, typename absl::AnySpan<T>::size_type pos,
    typename absl::AnySpan<T>::size_type len = absl::AnySpan<T>::npos) {
  return span.subspan(pos, (std::min)(len, span.size() - pos));
}

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_SUBSPAN_H_
