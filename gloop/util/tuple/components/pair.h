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

// Copyright 2013 Google Inc. All Rights Reserved.

//
// This file defines tuple intrinsics for std::pair thus making it usable with

// IWYU pragma: private, include "util/tuple/pair.h"

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_PAIR_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_PAIR_H_

#include <stddef.h>

#include <tuple>
#include <type_traits>
#include <utility>

#include "gloop/util/tuple/components/intrinsics.h"

namespace util {
namespace tuple {

namespace internal_pair {

// All symbols defined within namespace internal_pair are internal
// to pair.h. Do not reference them from outside or your code can break
// without notice.

template <::size_t N, class T, class U>
::std::tuple_element<N, std::pair<T, U>> element(const std::pair<T, U>&);

template <class T, class U>
void accept_pair(::std::pair<T, U>*);

}  // namespace internal_pair

struct pair_tag {};

template <class T>
struct tag<T, decltype(internal_pair::accept_pair(static_cast<T*>(nullptr)))> {
  typedef pair_tag type;
};

template <>
struct intrinsics<pair_tag> {
  using has_all_elements = std::true_type;

  template <class T, class U>
  struct assemble {
    typedef std::pair<T, U> type;
  };

  template <::size_t N, class T>
  using element = decltype(internal_pair::element<N>(::std::declval<T>()));

  template <class T>
  using size = ::std::integral_constant<::size_t, 2>;

  template <::size_t N, class T>
  static decltype(::std::get<N>(::std::declval<T>())) get(T&& t) {
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

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_PAIR_H_
