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

// Internal helpers for c_memcpy.

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_C_MEM_INTERNAL_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_C_MEM_INTERNAL_H_

#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>

#include "absl/algorithm/container.h"
#include "absl/base/internal/hardening.h"

namespace gtl {
namespace c_mem_internal {

// Don't use definitions in this namespace directly. They are subject to change
// without notice.

template <typename C>
using element_type_t =
    std::remove_reference_t<decltype(*std::data(std::declval<C&>()))>;

// A writable, single-dimensional range of trivial elements.
template <typename C>
inline constexpr bool kIsTrivialDestRange =
    std::is_trivial_v<element_type_t<C>> &&
    absl::container_algorithm_internal::IsPermissibleDestinationRange<
        C>::value &&
    !absl::container_algorithm_internal::IsMultidimensionalArray<
        std::remove_reference_t<C>>::value;

// True iff absl::c_copy(src, dest) / absl::c_copy_n(src, n, dest) would select
// their range-to-range overloads.
template <typename S, typename D, typename = void>
struct IsRangeToRangeTransfer : std::false_type {};

template <typename S, typename D>
struct IsRangeToRangeTransfer<
    S, D,
    absl::container_algorithm_internal::ResultOfRangeToRangeTransfer<S, D>>
    : std::true_type {};

// absl::c_copy() can stand in for memcpy when the element types match.
// absl::c_copy() does not constrain on the source having begin()/end(), so
// check it here to avoid a hard error for data()/size()-only sources.
template <typename D, typename S>
inline constexpr bool kCanUseCCopy =
    IsRangeToRangeTransfer<S, D>::value &&
    absl::container_algorithm_internal::HasBeginEnd<const S&>::value &&
    std::is_trivial_v<element_type_t<D>> &&
    std::is_same_v<element_type_t<D>, std::remove_cv_t<element_type_t<S>>>;

// absl::c_copy_n() counts elements, so `num_bytes` is an element count only
// for 1-byte elements.
template <typename D, typename S>
inline constexpr bool kCanUseCCopyN =
    kCanUseCCopy<D, S> && sizeof(element_type_t<D>) == 1;

template <typename C>
constexpr size_t byte_size(const C& c) {
  return std::size(c) * sizeof(*std::data(c));
}

// Enforces the contracts for the destination type and checks that `dest` holds
// at least `num_bytes`.
template <typename D>
constexpr void CheckDestPreconditions(const D& dest, size_t num_bytes) {
  static_assert(!std::is_const_v<element_type_t<D>>,
                "Destination container must not have a const value type.");
  static_assert(std::is_trivial_v<element_type_t<D>>,
                "Destination container must have a trivial value type.");
  absl::base_internal::HardeningAssertLE(num_bytes, byte_size(dest));
}

// Enforces the contracts for input types and checks that both `src` and `dest`
// hold at least `num_bytes`.
template <typename D, typename S>
constexpr void CheckCopyPreconditions(const D& dest, const S& src,
                                      size_t num_bytes) {
  static_assert(std::is_trivially_copyable_v<element_type_t<S>>,
                "Source container must have a trivially copyable value type.");
  absl::base_internal::HardeningAssertLE(num_bytes, byte_size(src));
  CheckDestPreconditions(dest, num_bytes);
}

}  // namespace c_mem_internal
}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_C_MEM_INTERNAL_H_
