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
// Function template zip() converts a bunch of tuples into a single tuple of
// tuples. The ith element of the result is a tuple composed of ith elements of
// the original tuples.
//
// assert(zip(make_tuple(1, 2), make_tuple(3, 4)) ==
//        make_tuple(make_tuple(1, 3), make_tuple(2, 4)));
//
// If the input tuples are of different sizes, the longer ones are truncated.
//
// assert(zip(make_tuple(1, 2), make_tuple(3)) == make_tuple(make_tuple(1, 3)));
//
// It's possible to explicitly specify the kind of the resulting tuple.
//
// assert(zip<pair_tag>(make_tuple(1, 2), make_tuple(3, 4)) ==
//        make_pair(make_pair(1, 3), make_pair(2, 4)));
//
// In the current version it's not possible to separately specify the kind of
// the inner and outer tuples.
//
// zip() is often used to iterate several tuples in lockstep. When doing so,
// copies are usually undesirable. They can be avoided either by applying
// ref() to the arguments before passing them to zip() or by using the zip_ref()
// shortcut.
//
//   tuple<string, int> t1;
//   tuple<char, vector<double>> t2;
//   for_each(f, zip_ref(t1, t2));

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_ZIP_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_ZIP_H_

#include <stddef.h>
#include <stdlib.h>

#include <tuple>
#include <utility>

#include "gloop/util/tuple/generate.h"
#include "gloop/util/tuple/int_pack.h"
#include "gloop/util/tuple/intrinsics.h"
#include "gloop/util/tuple/ref.h"
#include "gloop/util/tuple/std_tuple.h"

namespace util {
namespace tuple {

namespace internal_zip {

// All symbols defined within namespace internal_zip are internal
// to zip.h. Do not reference them from outside or your code can break
// without notice.

template <::size_t I, class... T>
using nth = typename element<I, ::std::tuple<T...>>::type;

// To avoid extra copies, we are using a separate functor for computing the
// types of the elements. Note the ampersant in the result type of value_getter
// that doesn't exist in type_getter.
template <class Tag, class Is, class... T>
struct type_getter;

template <class Tag, ::size_t... Is, class... T>
struct type_getter<Tag, int_pack<Is...>, T...> {
  template <::size_t I>
  typename assemble<Tag, typename element<I, nth<Is, T...>>::type...>::type
  operator()() const;
};

template <class Tag, class Is, class... T>
struct value_getter;

template <class Tag, ::size_t... Is, class... T>
struct value_getter<Tag, int_pack<Is...>, T...> {
  template <::size_t I,
            class R = typename assemble<
                Tag, decltype(::util::tuple::get<I>(::util::tuple::get<Is>(
                         ::std::declval<::std::tuple<T&&...>>())))...>::type>
  constexpr R operator()() const {
    return R(::util::tuple::get<I>(util::tuple::get<Is>(::std::move(t)))...);
  }

  ::std::tuple<T&&...>& t;
};

// min() returns 0.
// min(size_t a1, ..., size_t an) returns the smallest value among a1, ..., an.
inline constexpr ::size_t min(::size_t value = 0) { return value; }

template <class... Rest>
constexpr ::size_t min(::size_t first, Rest... rest) {
  return first < min(rest...) ? first : min(rest...);
}

}  // namespace internal_zip

// Function template zip() converts a bunch of tuples into a single tuple of
// tuples. The ith element of the result is a tuple composed of ith elements of
// the original tuples.
//
// If the input tuples are of different sizes, the longer ones are truncated.
template <class Tag, class T, class... Ts>
constexpr decltype(generate_index<Tag, internal_zip::min(size<T>::value,
                                                         size<Ts>::value...)>(
    ::std::declval<internal_zip::type_getter<
        Tag, typename make_int_pack<0, sizeof...(Ts) + 1>::type, T, Ts...>>()))
zip(T&& t, Ts&&... ts) {
  typedef typename make_int_pack<0, sizeof...(Ts) + 1>::type Is;
  typedef decltype(generate_index<Tag, internal_zip::min(size<T>::value,
                                                         size<Ts>::value...)>(
      ::std::declval<internal_zip::type_getter<
          Tag, typename make_int_pack<0, sizeof...(Ts) + 1>::type, T,
          Ts...>>())) R;
  ::std::tuple<T&&, Ts&&...> tuple(std::forward<T>(t),
                                   ::std::forward<Ts>(ts)...);
  return generate_index<R>(
      internal_zip::value_getter<Tag, Is, T, Ts...>{tuple});
}

template <class T, class... Ts>
constexpr auto zip(T&& t, Ts&&... ts)
    -> decltype(zip<typename tag<T>::type>(::std::forward<T>(t),
                                           ::std::forward<Ts>(ts)...)) {
  return zip<typename tag<T>::type>(::std::forward<T>(t),
                                    ::std::forward<Ts>(ts)...);
}

template <class Tag = std_tuple_tag>
constexpr typename assemble<Tag>::type zip() {
  return {};
}

// The same as zip<Tag>(ref(args)...). In other words, creates a tuple of
// tuples of references.
template <class Tag, class... Ts>
decltype(zip<Tag>(::util::tuple::ref<Tag>(::std::declval<Ts>())...)) zip_ref(
    Ts&&... ts) {
  return zip<Tag>(::util::tuple::ref<Tag>(::std::forward<Ts>(ts))...);
}

// The same as zip(ref(args)...). In other words, creates a tuple of tuples of
// references.
template <class... Ts>
decltype(zip(::util::tuple::ref(::std::declval<Ts>())...)) zip_ref(Ts&&... ts) {
  return zip(::util::tuple::ref(::std::forward<Ts>(ts))...);
}

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_ZIP_H_
