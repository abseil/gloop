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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_C_MEM_INTERNAL_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_C_MEM_INTERNAL_H_

#include <cstddef>
#include <iterator>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/algorithm/container.h"
#include "absl/base/internal/hardening.h"
#include "absl/log/check.h"

namespace gtl {
namespace c_mem_internal {

// The cv-qualified element type of the contiguous buffer of `C`.
template <typename C>
using element_type_t =
    std::remove_reference_t<decltype(*std::data(std::declval<C&>()))>;

// A writable, single-dimensional range of trivial elements.
template <typename C>
inline constexpr bool kIsTrivialDestRange =
    !std::is_const_v<element_type_t<C>> &&
    std::is_trivial_v<element_type_t<C>> &&
    absl::container_algorithm_internal::IsBorrowedRange<C>::value &&
    !absl::container_algorithm_internal::IsMultidimensionalArray<
        std::remove_reference_t<C>>::value;

// True iff absl::c_fill(c, ch) can stand in for memset on `C`.
template <typename C>
inline constexpr bool kCanUseCFill =
    kIsTrivialDestRange<C> && sizeof(element_type_t<C>) == 1 &&
    std::is_trivially_copy_assignable_v<element_type_t<C>>;

// True iff absl::c_copy(src, dest) / absl::c_copy_n(src, n, dest) would select
// their range-to-range overloads, as opposed to range-to-iterator overloads
template <typename S, typename D, typename = void>
struct IsRangeToRangeTransfer : std::false_type {};

template <typename S, typename D>
struct IsRangeToRangeTransfer<
    S, D,
    absl::container_algorithm_internal::ResultOfRangeToRangeTransfer<S, D>>
    : std::true_type {};

// absl::c_copy() can stand in for memcpy when the element types match and can
// be assigned (a trivial type may still delete its copy assignment).
// absl::c_copy() does not constrain on the source having begin()/end(), so
// check it here to avoid a hard error for data()/size()-only sources.
template <typename D, typename S>
inline constexpr bool kCanUseCCopy =
    IsRangeToRangeTransfer<S, D>::value &&
    absl::container_algorithm_internal::HasBeginEnd<const S&>::value &&
    std::is_trivial_v<element_type_t<D>> &&
    std::is_trivially_copy_assignable_v<element_type_t<D>> &&
    std::is_same_v<element_type_t<D>, std::remove_cv_t<element_type_t<S>>>;

// True iff `T` (ignoring cvref qualifiers) is a `std::basic_string`
// specialization, whose `data()` buffer includes a trailing null terminator at
// index `size()`.
template <typename T>
struct IsBasicString : std::false_type {};

template <typename CharT, typename Traits, typename Alloc>
struct IsBasicString<std::basic_string<CharT, Traits, Alloc>> : std::true_type {
};

template <typename T>
inline constexpr bool kIsBasicString =
    IsBasicString<std::remove_cv_t<std::remove_reference_t<T>>>::value;

// absl::c_copy_n() counts elements, so `num_bytes` is an element count only
// for 1-byte elements. `std::basic_string` sources are excluded because
// callers may pass `src.size() + 1` to copy the trailing null terminator,
// which is in bounds for `src.data()` but out of bounds for `src.begin()`.
template <typename D, typename S>
inline constexpr bool kCanUseCCopyN =
    kCanUseCCopy<D, S> && sizeof(element_type_t<D>) == 1 && !kIsBasicString<S>;

// Returns the total size in bytes of the elements in `c`.
template <typename C>
constexpr size_t byte_size(const C& c) {
  return std::size(c) * sizeof(*std::data(c));
}

// Returns the total number of bytes that may be safely read from
// `std::data(c)`. For `std::basic_string`, `data()` points to a buffer of
// `size() + 1` elements that includes the trailing null terminator.
template <typename C>
constexpr size_t readable_byte_size(const C& c) {
  if constexpr (kIsBasicString<C>) {
    return (std::size(c) + 1) * sizeof(*std::data(c));
  } else {
    return byte_size(c);
  }
}

// Enforces the contracts for the destination type and checks that `dest` holds
// at least `num_bytes` (bounded by `max_dest_bytes`). Writing a partial
// trailing element is permitted, as with memcpy(), but is almost always a unit
// mix-up, so debug builds reject it.
template <typename D>
void CheckDestPreconditions(D& dest, size_t num_bytes, size_t max_dest_bytes) {
  static_assert(!std::is_const_v<element_type_t<D>>,
                "Destination container must not have a const value type.");
  static_assert(std::is_trivial_v<element_type_t<D>>,
                "Destination container must have a trivial value type.");
  absl::base_internal::HardeningAssertLE(num_bytes, max_dest_bytes);
  // Multidimensional arrays count in their innermost element type.
  using Scalar = std::remove_all_extents_t<element_type_t<D>>;
  DCHECK_EQ(num_bytes % sizeof(Scalar), 0)
      << "num_bytes (" << num_bytes << ") is not a multiple of the destination "
      << "element size (" << sizeof(Scalar) << ")";
}

template <typename D>
void CheckDestPreconditions(D& dest, size_t num_bytes) {
  CheckDestPreconditions(dest, num_bytes, byte_size(dest));
}

// Enforces the contracts for input types and checks that both `src` and `dest`
// hold at least `num_bytes`.
template <typename D, typename S>
void CheckCopyPreconditions(D& dest, const S& src, size_t num_bytes) {
  static_assert(std::is_trivially_copyable_v<element_type_t<S>>,
                "Source container must have a trivially copyable value type.");
  const size_t src_bytes = readable_byte_size(src);
  absl::base_internal::HardeningAssertLE(num_bytes, src_bytes);
  size_t dest_bytes = byte_size(dest);
  if constexpr (kIsBasicString<D> && kIsBasicString<S> &&
                sizeof(element_type_t<D>) == sizeof(element_type_t<S>)) {
    // Modifying `dest.data()[dest.size()]` on a `std::basic_string` is
    // well-defined when the value written is `CharT()`, which holds when the
    // last copied element is `src`'s trailing null terminator.
    if (num_bytes == src_bytes) {
      dest_bytes = readable_byte_size(dest);
    }
  }
  CheckDestPreconditions(dest, num_bytes, dest_bytes);
}

// Enforces the contracts for input types and checks that both `lhs` and `rhs`
// hold at least `num_bytes`.
template <typename L, typename R>
constexpr void CheckComparePreconditions(const L& lhs, const R& rhs,
                                         size_t num_bytes) {
  static_assert(
      std::is_trivially_copyable_v<element_type_t<L>>,
      "Compared container must have a trivially copyable value type.");
  static_assert(
      std::is_trivially_copyable_v<element_type_t<R>>,
      "Compared container must have a trivially copyable value type.");
  absl::base_internal::HardeningAssertLE(num_bytes, readable_byte_size(lhs));
  absl::base_internal::HardeningAssertLE(num_bytes, readable_byte_size(rhs));
}

}  // namespace c_mem_internal
}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_C_MEM_INTERNAL_H_
