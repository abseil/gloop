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

//
// Function templates slice() and slice_range() return a subset of the original
// tuple with only the elements with the specified indices.

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_SLICE_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_SLICE_H_

#include <stddef.h>

#include <utility>

#include "gloop/util/tuple/array.h"
#include "gloop/util/tuple/generate.h"
#include "gloop/util/tuple/ignore_index.h"
#include "gloop/util/tuple/int_pack.h"
#include "gloop/util/tuple/intrinsics.h"
#include "gloop/util/tuple/pair.h"
#include "gloop/util/tuple/std_tuple.h"

namespace util {
namespace tuple {

namespace internal_slice {

// All symbols defined within namespace internal_slice are internal
// to slice.h. Do not reference them from outside or your code can break
// without notice.

// Returns the Nth element of an int_pack<>.
template <::size_t N, class Is>
struct nth;

template <::size_t N, ::size_t I, ::size_t... Is>
struct nth<N, int_pack<I, Is...>> : nth<N - 1, int_pack<Is...>> {};

template <::size_t I, ::size_t... Is>
struct nth<0, int_pack<I, Is...>> {
  static constexpr ::size_t value = I;
};

// The result type of slice_pack<Is, T>().
template <class T, class Is>
struct result;

template <class T, ::size_t... Is>
struct result<T, int_pack<Is...>>
    : assemble<typename tag<T>::type, typename element<Is, T>::type...> {};

// T must be a tuple type and Is must be an instance of int_pack<>.
template <class T, class Is>
struct get_indirected {
  // Returns the Is[I]th element of the tuple.
  template <::size_t I, class R>
  constexpr decltype(get<nth<I, Is>::value>(::std::declval<T>())) operator()()
      const {
    return get<nth<I, Is>::value>(::std::forward<T>(t));
  }
  T&& t;
};

// T must be a tuple type and Is must be an instance of int_pack<>.
// Returns a subset of T with Nth element being equal to the Is[N]th element
// of the original tuple.
template <class Is, class T>
constexpr typename result<T, Is>::type slice_pack(T&& t) {
  return generate_index<typename result<T, Is>::type>(
      get_indirected<T, Is>{::std::forward<T>(t)});
}

}  // namespace internal_slice

// T must be a tuple type.
//
// Returns a subset of T with Nth element being equal to the Is[N]th element
// of the original tuple. Indices don't have to be sorted and may contain
// duplicates.
//
//   tuple<int, char, string> a(42, 'A', "hello");
//   tuple<int, string> b = slice<0, 2>(a);  // Select 0th and 2nd elements.
//   assert(b == make_tuple(42, "hello"));
template <::size_t... Is, class T>
constexpr auto slice(T&& t)
    -> decltype(internal_slice::slice_pack<int_pack<Is...>>(
        ::std::forward<T>(t))) {
  return internal_slice::slice_pack<int_pack<Is...>>(::std::forward<T>(t));
}

// T must be a tuple type.
//
// Returns a (J-I)-size tuple with Nth element being equal to the (N+I)th
// element of the original tuple.
//
//   tuple<int, char, string, double> a(42, 'A', "hello", 0.5);
//   tuple<int, char, string> b = slice_range<0, 3>(a);  // Select [0, 3).
//   assert(b == make_tuple(42, 'A', "hello"));
template <::size_t I, ::size_t J, class T>
constexpr auto slice_range(T&& t)
    -> decltype(internal_slice::slice_pack<typename make_int_pack<I, J>::type>(
        ::std::forward<T>(t))) {
  return internal_slice::slice_pack<typename make_int_pack<I, J>::type>(
      ::std::forward<T>(t));
}

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_SLICE_H_
