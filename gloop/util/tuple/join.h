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

// Function template join() takes a tuple of tuples as input and concatenates
// all elements.
//
//   auto a = make_tuple(42, 'A');
//   auto b = make_tuple(24, 0.5);
//   auto c = join(make_tuple(a, b));
//   assert(c == make_tuple(42, 'A', 24, 0.5);
//
// Inner tuples aren't flattened recursively.
//
//   auto a = make_tuple(42, make_tuple('A'));
//   assert(join(make_tuple(a)) == a);
//
// The category of the resulting tuple can be specified explicitly.
//
//   auto a = std::make_tuple(1);
//   auto b = std::make_pair(2, 3);
//   std::tuple<int, int, int> = join<std_tuple>(std::make_tuple(a, b));

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_JOIN_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_JOIN_H_

#include <stddef.h>

#include <utility>

#include "gloop/util/tuple/array.h"
#include "gloop/util/tuple/cat.h"
#include "gloop/util/tuple/int_pack.h"
#include "gloop/util/tuple/intrinsics.h"
#include "gloop/util/tuple/pair.h"
#include "gloop/util/tuple/std_tuple.h"

namespace util {
namespace tuple {

namespace internal_join {

// All symbols defined within namespace internal_join are internal
// to join.h. Do not reference them from outside or your code can break
// without notice.

template <class Tag, class T, ::size_t... Is>
auto join_impl(T&& t, int_pack<Is...>)
    -> decltype(cat<Tag>(get<Is>(::std::forward<T>(t))...)) {
  return cat<Tag>(get<Is>(::std::forward<T>(t))...);
}

}  // namespace internal_join

// Takes a tuple of tuples as input and concatenates all elements.
template <class Tag, class T>
auto join(T&& t) -> decltype(internal_join::join_impl<Tag>(
    ::std::forward<T>(t), make_int_pack<0, size<T>::value>())) {
  return internal_join::join_impl<Tag>(::std::forward<T>(t),
                                       make_int_pack<0, size<T>::value>());
}

// The same as above with the result tuple having the same category as the
// input tuple.
template <class T>
auto join(T&& t)
    -> decltype(join<typename tag<T>::type>(::std::forward<T>(t))) {
  return join<typename tag<T>::type>(::std::forward<T>(t));
}

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_JOIN_H_
