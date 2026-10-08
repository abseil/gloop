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
// Function templates erase() and erase_range() return a subset of the original
// tuple with the elements with the specified indices removed.
//
// Function templates erase_if() and erase_if_index() erase elements based on
// the supplied predicate.

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_ERASE_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_ERASE_H_

#include <stddef.h>

#include <type_traits>
#include <utility>

#include "gloop/util/tuple/filter.h"
#include "gloop/util/tuple/ignore_index.h"
#include "gloop/util/tuple/int_pack.h"
#include "gloop/util/tuple/intrinsics.h"
#include "gloop/util/tuple/slice.h"

namespace util {
namespace tuple {

namespace internal_erase {

// All symbols defined within namespace internal_erase are internal
// to erase.h. Do not reference them from outside or your code can break
// without notice.

// Does the int_pack<> contain number N?
template <class Is, ::size_t N>
struct contains;

template <::size_t I, ::size_t... Is, ::size_t N>
struct contains<int_pack<I, Is...>, N>
    : ::std::conditional<I == N, ::std::true_type,
                         contains<int_pack<Is...>, N>>::type {};

template <::size_t N>
struct contains<int_pack<>, N> : ::std::false_type {};

// Given int_pack<...> and number I constructs int_pack<I, ...>.
template <class Is, ::size_t I>
struct prepend;

template <::size_t... Is, ::size_t I>
struct prepend<int_pack<Is...>, I> : int_pack<I, Is...> {};

// Returns a subset of int_pack<> A with all the elements found in int_pack<> B
// removed. The original order is preserved.
//
// set_difference<int_pack<1, 2, 2, 1>, int_pack<2, 3>>::type is int_pack<1, 1>.
template <class A, class B>
struct set_difference;

template <::size_t A, ::size_t... As, class B>
struct set_difference<int_pack<A, As...>, B>
    : ::std::conditional<
          contains<B, A>::value, set_difference<int_pack<As...>, B>,
          prepend<typename set_difference<int_pack<As...>, B>::type, A>>::type {
};

template <class B>
struct set_difference<int_pack<>, B> : int_pack<> {};

// T must be a tuple type. Returns a subset of T with Nth element being equal to
// the Is[N]th element of the original tuple.
template <class T, ::size_t... Is>
constexpr auto slice_pack(T&& t, int_pack<Is...>)
    -> decltype(slice<Is...>(::std::forward<T>(t))) {
  return slice<Is...>(::std::forward<T>(t));
}

// T must be a tuple type and Is must be an instance of int_pack<>.
// Returns a subset of T with all elements with indices in Is removed.
template <class Is, class T>
constexpr auto erase_pack(T&& t) -> decltype(slice_pack(
    ::std::forward<T>(t),
    typename set_difference<typename make_int_pack<0, size<T>::value>::type,
                            Is>::type())) {
  return slice_pack(
      ::std::forward<T>(t),
      typename set_difference<typename make_int_pack<0, size<T>::value>::type,
                              Is>::type());
}

template <class Predicate>
struct negate {
  template <::size_t I, class T>
  struct apply
      : std::integral_constant<bool, !Predicate::template apply<I, T>::value> {
  };
};

}  // namespace internal_erase

// T must be a tuple type.
//
// Returns a subset of T with all elements with the specified indices removed.
// The indices don't have to be sorted and may contain duplicates.
//
//   tuple<char, int, string> a('A', 42, "hello");
//   tuple<int> b = erase<0, 2>(a);  // Remove elements 0 and 2.
//   assert(b == make_tuple(42));
template <::size_t... Is, class T>
constexpr auto erase(T&& t)
    -> decltype(internal_erase::erase_pack<int_pack<Is...>>(
        ::std::forward<T>(t))) {
  return internal_erase::erase_pack<int_pack<Is...>>(::std::forward<T>(t));
}

// T must be a tuple type.
//
// Returns a subset of T with all elements with the indices [I, J) removed.
//
//   tuple<int, char, string, double> a(42, 'A', "hello", 0.5);
//   tuple<int> b = erase_range<1, 4>(a);  // Remove elements [1, 4).
//   assert(b == make_tuple(42));
template <::size_t I, ::size_t J, class T>
constexpr auto erase_range(T&& t)
    -> decltype(internal_erase::erase_pack<typename make_int_pack<I, J>::type>(
        ::std::forward<T>(t))) {
  return internal_erase::erase_pack<typename make_int_pack<I, J>::type>(
      ::std::forward<T>(t));
}

// T must be a tuple type. Predicate must be a type such that
// Predicate::template apply<I, U>::value is true if and only if I-th tuple
// element of type U satisfies the predicate.
//
// The function returns a subset of the tuple without those elements that
// satisfy the predicate.
//
//   struct even_and_big {
//     template <size_t I, class Elem>
//     struct apply {
//       static const bool value = (I % 2 == 0) && (sizeof(Elem) > 2);
//     };
//   };
//
//   tuple<char, int32, int64> t('A', 42, 24);
//   tuple<char, int32> q = erase_if_index<even_and_big>(t);
//   assert(q == make_tuple('A', 42));
template <class Predicate, class T>
constexpr auto erase_if_index(T&& t)
    -> decltype(filter_index<internal_erase::negate<Predicate>>(
        ::std::forward<T>(t))) {
  return filter_index<internal_erase::negate<Predicate>>(::std::forward<T>(t));
}

// The same as erase_if_index() above but the predicate is passed only the type
// of the element without the index.
//
//   struct big {
//     template <class Elem>
//     struct apply {
//       static const bool value = (sizeof(Elem) > 2);
//     };
//   };
//
//   tuple<char, int32, int64> t('A', 42, 24);
//   tuple<char> q = erase_if<big>(t);
//   assert(q == make_tuple('A'));
template <class Predicate, class T>
constexpr auto erase_if(T&& t)
    -> decltype(erase_if_index<ignore_predicate_index<Predicate>>(
        ::std::forward<T>(t))) {
  return erase_if_index<ignore_predicate_index<Predicate>>(
      ::std::forward<T>(t));
}

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_ERASE_H_
