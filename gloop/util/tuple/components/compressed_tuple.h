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

// This file defines tuple intrinsics for gtl::CompressedTuple thus making it
// usable with algorithms in //gloop/util/tuple.

// IWYU pragma: private, include "util/tuple/compressed_tuple.h"

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_COMPRESSED_TUPLE_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_COMPRESSED_TUPLE_H_

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include "gloop/util/gtl/compressed_tuple.h"
#include "gloop/util/tuple/components/intrinsics.h"

namespace util::tuple {

namespace internal_compressed_tuple {

// All symbols defined within namespace internal_compressed_tuple are internal
// to compressed_tuple.h. Do not reference them from outside or your code can
// break without notice.

template <class... Ts>
std::integral_constant<::size_t, sizeof...(Ts)> size(
    const gtl::CompressedTuple<Ts...>&);

template <::size_t N, class... Ts>
std::tuple_element<N, std::tuple<Ts...>> element(
    const gtl::CompressedTuple<Ts...>&);

struct Accept {
  template <class... Ts>
  void operator()(gtl::CompressedTuple<Ts...>*);
};

}  // namespace internal_compressed_tuple

struct compressed_tuple_tag {};

template <class T>
  requires std::is_invocable_v<internal_compressed_tuple::Accept, T*>
struct tag<T> {
  using type = compressed_tuple_tag;
};

template <>
struct intrinsics<compressed_tuple_tag> {
  using has_all_elements = std::true_type;

  template <class... T>
  struct assemble {
    using type = gtl::CompressedTuple<T...>;
  };

  template <::size_t N, class T>
  using element =
      decltype(internal_compressed_tuple::element<N>(std::declval<T>()));

  template <class T>
  using size = decltype(internal_compressed_tuple::size(std::declval<T>()));

  template <::size_t N, class T>
  static constexpr decltype(auto) get(T&& t) {
    return std::forward<T>(t).template get<N>();
  }
};

}  // namespace util::tuple

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_COMPRESSED_TUPLE_H_
