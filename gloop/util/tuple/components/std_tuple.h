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

//
// This file defines tuple intrinsics for std::tuple thus making it usable with

// IWYU pragma: private, include "util/tuple/std_tuple.h"

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_STD_TUPLE_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_STD_TUPLE_H_

#include <stddef.h>

#include <tuple>
#include <type_traits>
#include <utility>

#include "gloop/util/tuple/components/intrinsics.h"

namespace util {
namespace tuple {

namespace internal_std_tuple {

// All symbols defined within namespace internal_std_tuple are internal
// to std_tuple.h. Do not reference them from outside or your code can break
// without notice.

template <class... Ts>
::std::integral_constant<::size_t, sizeof...(Ts)> size(
    const ::std::tuple<Ts...>&);

template <::size_t N, class... Ts>
::std::tuple_element<N, ::std::tuple<Ts...>> element(
    const ::std::tuple<Ts...>&);

template <class... Ts>
void accept_tuple(::std::tuple<Ts...>*);

}  // namespace internal_std_tuple

struct std_tuple_tag {};

template <class T>
struct tag<T, decltype(internal_std_tuple::accept_tuple(
                  static_cast<T*>(nullptr)))> {
  typedef std_tuple_tag type;
};

template <>
struct intrinsics<std_tuple_tag> {
  using has_all_elements = ::std::true_type;

  template <class... T>
  struct assemble {
    typedef ::std::tuple<T...> type;
  };

  template <::size_t N, class T>
  using element = decltype(internal_std_tuple::element<N>(::std::declval<T>()));

  template <class T>
  using size = decltype(internal_std_tuple::size(::std::declval<T>()));

  template <::size_t N, class T>
  static constexpr decltype(::std::get<N>(::std::declval<T>())) get(T&& t) {
    return ::std::get<N>(::std::forward<T>(t));
  }

  // std::get() is lacking an overload for const rvalues. We fix this oversight.
  template <::size_t N, class T>
  static const typename element<N, T>::type&& get(const T&& t) {
    return ::std::forward<const typename element<N, T>::type>(
        ::std::get<N>(::std::move(t)));
  }
};

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_STD_TUPLE_H_
