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
// The family of generate algorithms calls a polymorphic functor a number of
// times and returns all results packed in a tuple.

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_GENERATE_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_GENERATE_H_

#include <stddef.h>

#include <utility>

#include "gloop/util/tuple/ignore_index.h"
#include "gloop/util/tuple/int_pack.h"
#include "gloop/util/tuple/intrinsics.h"

namespace util {
namespace tuple {

namespace internal_generate {

// All symbols defined within namespace internal_generate are internal
// to generate.h. Do not reference them from outside or your code can break
// without notice.

// For every integer I in Is calls f.operator()<I, element<I, T>::type>() and
// packs the results in tuple T.
template <class T, class F, ::size_t... Is>
constexpr T generate_from_pack(const F& f, int_pack<Is...>) {
  return T{f.template operator()<Is, typename element<Is, T>::type>()...};
}

// For every integer I in Is calls f.operator()<I>() and packs the results
// in a tuple of the kind defined by Tag.
//
// generate_from_tag_and_pack<std_tuple>(f, int_pack<0, 1>()) evaluates to
// std::tuple<decltype(f.operator()<0>()), decltype(f.operator()<1>())>(
//     f.operator()<0>(),
//     f.operator()<1>()).
template <class Tag, class F, ::size_t... Is>
constexpr
    typename assemble<Tag, decltype(::std::declval<const F&>()
                                        .template operator()<Is>())...>::type
        generate_from_tag_and_pack(const F& f, int_pack<Is...>) {
  typedef
      typename assemble<Tag, decltype(f.template operator()<Is>())...>::type T;
  return T{f.template operator()<Is>()...};
}

}  // namespace internal_generate

// T must be a tuple and F must be a nullary functor with one template
// parameter of type size_t.
//
// generate_index<T>(f) evaluates to:
//
//   T(f.operator()<0, element<0, T>::type>(),
//     f.operator()<1, element<1, T>::type>(),
//     ...,
//     f.operator()<size<T>::value - 1, element<size<T>::value - 1, T>::type>())
//
// Example: Reversing elements of a tuple.
//
//   template <class T>
//   struct Reverse {
//     template <size_t I, class E>
//     typename E operator()() const {
//       return get<size<T>::value - I - 1>(*t);
//     }
//     const T* t;
//   };
//
//   tuple<int, string> a(42, "hello");
//   Reverse<tuple<int, string>> f = {&a};
//   tuple<string, int> b = generate_index<tuple<string, int>>(f);
//   assert(b == make_tuple("hello", 42);
template <class T, class F>
constexpr T generate_index(const F& f) {
  return internal_generate::generate_from_pack<T>(
      f, make_int_pack<0, size<T>::value>());
}

// Tag must be a tuple type tag and F must be a nullary functor with one
// template parameter of type size_t.
//
// generate_index<Tag, N>(f) evaluates to:
//
//   T(f.operator()<0>(),
//     f.operator()<1>(),
//     ...,
//     f.operator()<N - 1>())
//
// NOTE: The functor calling order is not defined.
//
// Where T is a tuple of the kind defined by Tag with element types deduced from
// the results of calls to 'f'.
//
// Example: Reversing elements of a tuple.
//
//   template <class T>
//   struct Reverse {
//     template <size_t I>
//     typename element<size<T>::value - I - 1, T>::type operator()() const {
//       return get<size<T>::value - I - 1>(*t);
//     }
//     const T* t;
//   };
//
//   tuple<int, string> a(42, "hello");
//   Reverse<tuple<int, string>> f = {&a};
//   tuple<string, int> b = generate_index<std_tuple, 2>(f);
//   assert(b == make_tuple("hello", 42);
template <class Tag, ::size_t N, class F>
constexpr decltype(internal_generate::generate_from_tag_and_pack<Tag>(
    ::std::declval<const F&>(), make_int_pack<0, N>()))
generate_index(const F& f) {
  return internal_generate::generate_from_tag_and_pack<Tag>(
      f, make_int_pack<0, N>());
}

// T must be a tuple and F must be a nullary functor with one template
// parameter.
//
// generate<T>(f) evaluates to:
//
//   T(f.operator()<element<0, T>::type>(),
//     f.operator()<element<1, T>::type>(),
//     ...,
//     f.operator()<element<size<T>::value - 1, T>::type>()
//
// NOTE: The functor calling order is not defined.
//
// Example:
//
//   struct InitWith42 {
//     template <class T>
//     T operator()() const {
//       return 42;
//     }
//   };
//
//   tuple<int, double> t = generate<tuple<int, double>>(InitWith42());
//
template <class T, class F>
T generate(const F& f) {
  return generate_index<T>(ignore_index_no_args(&f));
}

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_GENERATE_H_
