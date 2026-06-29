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
// Function template insert() inserts copies of the objects at a given position
// in the tuple. insert_ref() inserts reference to the objects. insert_tuple()
// inserts all elements from the tuple.
//
//   tuple<int, char, string> a(42, 'A', "hello");
//   tuple<int, char, double, int, string> b = insert<2>(a, 0.5, 24);
//   assert(b == make_tuple(42, 'A', 0.5, 24, "hello");
//
//   double d = 0.5;
//   const int n = 24;
//   tuple<int, char, double&, const int&, string> c = insert_ref<2>(a, d, n);
//   assert(&get<2>(c) == &d);
//   assert(&get<3>(c) == &n);
//
//   tuple<int, char, double, int, string> d =
//       insert_tuple<2>(a, make_tuple(0.5, 24));
//   assert(d == make_tuple(42, 'A', 0.5, 24, "hello"));

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_INSERT_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_INSERT_H_

#include <stddef.h>

#include <tuple>
#include <type_traits>
#include <utility>

#include "gloop/util/tuple/array.h"
#include "gloop/util/tuple/cat.h"
#include "gloop/util/tuple/intrinsics.h"
#include "gloop/util/tuple/pair.h"
#include "gloop/util/tuple/slice.h"
#include "gloop/util/tuple/std_tuple.h"

namespace util {
namespace tuple {

namespace internal_insert {

// All symbols defined within namespace internal_insert are internal
// to insert.h. Do not reference them from outside or your code can break
// without notice.

// When inserting a tuple of size N in a tuple of size M at position P, the
// elements in the resulting tuple belong to one of the three categories.
//
//   kBefore: [0, P)
//   kMiddle: [P, P + N)
//   kAfter:  [P + N, M + N)
enum pos_t { kBefore, kMiddle, kAfter };

template <pos_t P, ::size_t N, ::size_t I>
struct get_impl;

template <::size_t N, ::size_t I>
struct get_impl<kBefore, N, I> {
  template <class T, class U>
  constexpr auto operator()(T&& t, U&& u) const
      -> decltype(get<I>(::std::forward<T>(t))) {
    return get<I>(::std::forward<T>(t));
  }
};

template <::size_t N, ::size_t I>
struct get_impl<kMiddle, N, I> {
  template <class T, class U>
  constexpr auto operator()(T&& t, U&& u) const
      -> decltype(get<I - N>(::std::forward<U>(u))) {
    return get<I - N>(::std::forward<U>(u));
  }
};

template <::size_t N, ::size_t I>
struct get_impl<kAfter, N, I> {
  template <class T, class U>
  constexpr auto operator()(T&& t, U&& u) const
      -> decltype(get<I - size<U>::value>(::std::forward<T>(t))) {
    return get<I - size<U>::value>(::std::forward<T>(t));
  }
};

// T and U are tuples. N is the position at which U is inserted into T. I is the
// index of the element in the resulting tuple. Function object getter returns
// that element.
template <::size_t N, class T, class U>
struct getter {
  template <::size_t I, class R>
  constexpr decltype(get_impl<(I < N)                    ? kBefore
                              : (I < N + size<U>::value) ? kMiddle
                                                         : kAfter,
                              N, I>()(::std::declval<T>(), ::std::declval<U>()))
  operator()() const {
    return get_impl<(I < N)                    ? kBefore
                    : (I < N + size<U>::value) ? kMiddle
                                               : kAfter,
                    N, I>()(::std::forward<T>(t), ::std::forward<U>(u));
  }

  T&& t;
  U&& u;
};

// Inserts all elements of a tuple E into a tuple of type T at positition N.
// While the types of insterted elements are taken from tuple E, their values
// are taken from from tuple V. This is done to avoid extra copies. E.g.,
// to insert a single element of type string, we could do:
//
//   string s;
//   tuple<int> b;
//   tuple<string, int> b = insert_impl<0, tuple<string>>(a, tie(s));
template <::size_t N, class E, class T, class V>
constexpr decltype(cat(slice_range<0, N>(::std::declval<T>()),
                       ::std::declval<E>(),
                       slice_range<N, size<T>::value>(::std::declval<T>())))
insert_impl(T&& t, V&& v) {
  // We use this cat(...) formula only to compute the type of the result.
  // We could also return cat(...) here and the value would be correct,
  // but that would do more copies than necessary.
  typedef decltype(cat(slice_range<0, N>(::std::declval<T>()),
                       ::std::declval<E>(),
                       slice_range<N, size<T>::value>(::std::declval<T>()))) R;
  getter<N, T, V> op = {::std::forward<T>(t), ::std::forward<V>(v)};
  return generate_index<R>(op);
}

}  // namespace internal_insert

// Inserts copy of the objects at the given position in the tuple.
template <::size_t N, class... V, class T>
constexpr auto insert(T&& t, V&&... v)
    -> decltype(internal_insert::insert_impl<
                N, ::std::tuple<typename ::std::decay<V>::type...>>(
        ::std::forward<T>(t),
        ::std::forward_as_tuple(::std::forward<V>(v)...))) {
  return internal_insert::insert_impl<
      N, ::std::tuple<typename ::std::decay<V>::type...>>(
      ::std::forward<T>(t), ::std::forward_as_tuple(::std::forward<V>(v)...));
}

// Inserts references to the object at the given position in the tuple.
template <::size_t N, class... V, class T>
constexpr auto insert_ref(T&& t, V&&... v)
    -> decltype(internal_insert::insert_impl<N, ::std::tuple<V&&...>>(
        ::std::forward<T>(t),
        ::std::forward_as_tuple(::std::forward<V>(v)...))) {
  return internal_insert::insert_impl<N, ::std::tuple<V&&...>>(
      ::std::forward<T>(t), ::std::forward_as_tuple(::std::forward<V>(v)...));
}

// Inserts all elements from tuple U at the given position in tuple T.
template <::size_t N, class U, class T>
constexpr auto insert_tuple(T&& t, U&& u)
    -> decltype(internal_insert::insert_impl<N, U>(::std::forward<T>(t),
                                                   ::std::forward<U>(u))) {
  return internal_insert::insert_impl<N, U>(::std::forward<T>(t),
                                            ::std::forward<U>(u));
}

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_INSERT_H_
