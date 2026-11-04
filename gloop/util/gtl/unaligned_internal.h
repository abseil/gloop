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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_UNALIGNED_INTERNAL_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_UNALIGNED_INTERNAL_H_

#include <array>
#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>

#include "absl/algorithm/container.h"
#include "absl/base/casts.h"
#include "gloop/util/gtl/requires.h"

namespace gtl {

template <typename T>
class Unaligned;

namespace internal_unaligned {

// TODO: b/448630816 - Delete IsBitCastableTo when b/308168698 (Upgrade hexagon
// for C++20 Support) has been fixed.  Replace `absl::bit_cast` with
// `std::bit_cast` and replace `IsBitCastableTo` with
// `std::is_trivially_copyable`.
template <typename T>
constexpr bool IsBitCastableTo = gtl::Requires<std::array<char, sizeof(T)>>(
    [](auto&& x) -> decltype(absl::bit_cast<T>(x)) {});

template <typename C, typename = void>
struct HasDataAndSize : std::false_type {};

template <typename C>
struct HasDataAndSize<C, std::void_t<decltype(std::data(std::declval<C>())),
                                     decltype(std::size(std::declval<C>()))>>
    : std::bool_constant<
          std::is_pointer_v<decltype(std::data(std::declval<C>()))> &&
          std::is_convertible_v<decltype(std::size(std::declval<C>())),
                                size_t>> {};

template <typename T>
inline constexpr bool IsByteElement =
    std::is_same_v<T, char> || std::is_same_v<T, unsigned char> ||
    std::is_same_v<T, signed char> || std::is_same_v<T, std::byte>;

template <typename C,
          bool = HasDataAndSize<const std::remove_reference_t<C>&>::value>
struct IsReadableByteRangeImpl : std::false_type {};

template <typename C>
struct IsReadableByteRangeImpl<C, true> {
  using RawC = std::remove_cv_t<std::remove_reference_t<C>>;
  using Elem = std::remove_cv_t<std::remove_pointer_t<decltype(std::data(
      std::declval<const std::remove_reference_t<C>&>()))>>;
  static constexpr bool value =
      !std::is_array_v<RawC> && !std::is_pointer_v<RawC> && IsByteElement<Elem>;
};

template <typename C>
using IsReadableByteRange = IsReadableByteRangeImpl<C>;

template <typename C,
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
      !std::is_const_v<Pointee> && IsByteElement<Elem>;
};

template <typename C>
using IsWritableByteRange = IsWritableByteRangeImpl<C>;

struct NoDefaultConstructor {
  constexpr explicit NoDefaultConstructor(std::in_place_t) {}
};
struct TrivialDefaultConstructor {
  TrivialDefaultConstructor() = default;
  constexpr explicit TrivialDefaultConstructor(std::in_place_t) {}
};
template <typename Derived>
struct UserDefaultConstructor {
  UserDefaultConstructor() { static_cast<Derived*>(this)->Store({}); }
  explicit UserDefaultConstructor(std::in_place_t) {}
};

template <typename T>
using UnalignedBase = std::conditional_t<
    std::is_default_constructible_v<T>,
    std::conditional_t<std::is_trivially_default_constructible_v<T>,
                       TrivialDefaultConstructor,
                       UserDefaultConstructor<Unaligned<T>>>,
    NoDefaultConstructor>;

}  // namespace internal_unaligned
}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_UNALIGNED_INTERNAL_H_
