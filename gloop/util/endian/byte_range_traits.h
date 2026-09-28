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

#ifndef THIRD_PARTY_GLOOP_UTIL_ENDIAN_BYTE_RANGE_TRAITS_H_
#define THIRD_PARTY_GLOOP_UTIL_ENDIAN_BYTE_RANGE_TRAITS_H_

#include <cstddef>
#include <iterator>
#include <type_traits>

#include "absl/algorithm/container.h"

namespace endian_internal {

// True if std::data(c) returns a pointer and std::size(c) converts to size_t.
template <typename C, typename = void>
struct HasDataAndSize : std::false_type {};

template <typename C>
struct HasDataAndSize<C, std::void_t<decltype(std::data(std::declval<C>())),
                                     decltype(std::size(std::declval<C>()))>>
    : std::bool_constant<
          std::is_pointer_v<decltype(std::data(std::declval<C>()))> &&
          std::is_convertible_v<decltype(std::size(std::declval<C>())),
                                size_t>> {};

template <typename C,
          bool = HasDataAndSize<const std::remove_reference_t<C>&>::value>
struct IsReadableByteRangeImpl : std::false_type {};

template <typename C>
struct IsReadableByteRangeImpl<C, true> {
  using RawC = std::remove_cv_t<std::remove_reference_t<C>>;
  using Elem = std::remove_cv_t<std::remove_pointer_t<decltype(std::data(
      std::declval<const std::remove_reference_t<C>&>()))>>;
  static constexpr bool value =
      !std::is_array_v<RawC> && !std::is_pointer_v<RawC> &&
      !std::is_void_v<Elem> && !std::is_pointer_v<Elem> &&
      !std::is_array_v<Elem> && std::is_trivially_copyable_v<Elem>;
};

// True if const C& is a contiguous range of trivially copyable elements.
template <typename C>
using IsReadableByteRange = IsReadableByteRangeImpl<C>;

template <typename C,
          // Abseil and util/endian are friends owned by the same team.
          // NOLINTNEXTLINE(abseil-no-internal-dependencies)
          bool = absl::container_algorithm_internal::
                     IsPermissibleDestinationRange<C>::value &&
                 HasDataAndSize<std::add_lvalue_reference_t<C>>::value>
struct IsWritableByteRangeImpl : std::false_type {};

template <typename C>
struct IsWritableByteRangeImpl<C, true> {
  using RawC = std::remove_cv_t<std::remove_reference_t<C>>;
  using Pointee = std::remove_pointer_t<decltype(std::data(
      std::declval<std::add_lvalue_reference_t<C>>()))>;
  using Elem = std::remove_cv_t<Pointee>;
  static constexpr bool value =
      !std::is_array_v<RawC> && !std::is_pointer_v<RawC> &&
      !std::is_const_v<Pointee> && !std::is_void_v<Elem> &&
      !std::is_pointer_v<Elem> && !std::is_array_v<Elem> &&
      std::is_trivially_copyable_v<Elem>;
};

// True if C is a mutable lvalue range or mutable span of bytes.
template <typename C>
using IsWritableByteRange = IsWritableByteRangeImpl<C>;

// Returns the size of contiguous range 'c' in bytes.
template <typename C>
constexpr size_t ByteSize(const C& c) noexcept {
  return std::size(c) * sizeof(*std::data(c));
}

}  // namespace endian_internal

#endif  // THIRD_PARTY_GLOOP_UTIL_ENDIAN_BYTE_RANGE_TRAITS_H_
