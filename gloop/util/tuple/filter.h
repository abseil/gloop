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
// Function templates filter() and filter_range() return a subset of the
// original tuple based on the supplied predicate.

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_FILTER_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_FILTER_H_

#include <stddef.h>

#include <type_traits>
#include <utility>

#include "gloop/util/tuple/array.h"
#include "gloop/util/tuple/int_pack.h"
#include "gloop/util/tuple/intrinsics.h"
#include "gloop/util/tuple/pair.h"
#include "gloop/util/tuple/slice.h"
#include "gloop/util/tuple/std_tuple.h"

namespace util {
namespace tuple {

namespace internal_filter {

// All symbols defined within namespace internal_filter are internal
// to erase.h. Do not reference them from outside or your code can break
// without notice.

// Adds an element to the front of an int_pack.
template <::size_t I, class Pack>
struct prepend;

template <::size_t I, ::size_t... Is>
struct prepend<I, int_pack<Is...>> : int_pack<I, Is...> {};

// Given a predicate, a tuple and a list of indices, returns a subset of the
// indices for which the predicate is true.
template <class Pred, class T, class Is>
struct filter_indices;

template <class Pred, class T>
struct filter_indices<Pred, T, int_pack<>> {
  typedef int_pack<> type;
};

template <class Pred, class T, ::size_t I, ::size_t... Is>
struct filter_indices<Pred, T, int_pack<I, Is...>> {
  typedef typename filter_indices<Pred, T, int_pack<Is...>>::type rest;
  static constexpr bool kMatch =
      Pred::template apply<I, typename element<I, T>::type>::value;
  typedef typename ::std::conditional<kMatch, typename prepend<I, rest>::type,
                                      rest>::type type;
};

template <class T, ::size_t... Is>
constexpr auto slice_pack(T&& t, int_pack<Is...> is)
    -> decltype(slice<Is...>(::std::forward<T>(t))) {
  return slice<Is...>(::std::forward<T>(t));
}

}  // namespace internal_filter

// T must be a tuple type. Predicate must be a type such that
// Predicate::template apply<I, U>::value is true if and only if I-th tuple
// element of type U satisfies the predicate.
//
// The function returns a subset of the tuple with only those elements left
// that satisfy the predicate.
//
//   struct even_and_big {
//     template <size_t I, class Elem>
//     struct apply {
//       static const bool value = (I % 2 == 0) && (sizeof(Elem) > 2);
//     };
//   };
//
//   tuple<char, int32, int64> t('A', 42, 24);
//   tuple<int64> q = filter_index<even_and_big>(t);
//   assert(q == make_tuple(24));
template <class Predicate, class T>
constexpr auto filter_index(T&& t) -> decltype(internal_filter::slice_pack(
    ::std::forward<T>(t),
    typename internal_filter::filter_indices<
        Predicate, T,
        typename make_int_pack<0, size<T>::value>::type>::type())) {
  return internal_filter::slice_pack(
      ::std::forward<T>(t),
      typename internal_filter::filter_indices<
          Predicate, T,
          typename make_int_pack<0, size<T>::value>::type>::type());
}

// The same as filter_index() above but the predicate is passed only the type
// of the element, without the index.
//
//   struct big {
//     template <class Elem>
//     struct apply {
//       static const bool value = (sizeof(Elem) > 2);
//     };
//   };
//
//   tuple<char, int32, int64> t('A', 42, 24);
//   tuple<int32, int64> q = filter<big>(t);
//   assert(q == make_tuple(42, 24));
template <class Predicate, class T>
constexpr auto filter(T&& t)
    -> decltype(filter_index<ignore_predicate_index<Predicate>>(
        ::std::forward<T>(t))) {
  return filter_index<ignore_predicate_index<Predicate>>(::std::forward<T>(t));
}

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_FILTER_H_
