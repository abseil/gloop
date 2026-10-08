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

// Function template transform() applies a polymorphic functor to all elements
// of the tuple and returns all results packed in a tuple of the same size.
//
//   struct Addr {
//     template <class T>
//     const T* operator()(const T& elem) const { return &elem; }
//   };
//
//   const tuple<string, int> t("hello", 42);
//   const tuple<const string*, const int*> q = transform(Addr(), t);
//   assert(q == make_tuple(&get<0>(t), &get<1>(t)));
//
// Function template transform_index() passes indices of the elements to the
// functor in addition to the element values.
//
//   struct MakePair {
//     template <size_t N, class T>
//     pair<size_t, T> operator()(const T& elem) const {
//       return make_pair(N, elem);
//     }
//   };
//
//   const tuple<string, int> t("hello", 42);
//   const tuple<pair<size_t, string>, pair<size_t, int>> q =
//       transform_index(MakePair(), t);
//   assert(q == make_tuple(make_pair(0, "hello"), make_pair(1, 42)));

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_TRANSFORM_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_TRANSFORM_H_

#include <cstddef>
#include <utility>

#include "gloop/util/tuple/array.h"
#include "gloop/util/tuple/generate.h"
#include "gloop/util/tuple/ignore_index.h"
#include "gloop/util/tuple/intrinsics.h"
#include "gloop/util/tuple/pair.h"
#include "gloop/util/tuple/std_tuple.h"

namespace util {
namespace tuple {

namespace internal_transform {

// All symbols defined within namespace internal_transform are internal
// to transform.h. Do not reference them from outside or your code can break
// without notice.

template <class F, class T>
struct by_value_generator {
  template <::size_t N>
  constexpr decltype(::std::declval<const F&>().template operator()<N>(
      get<N>(::std::declval<T>()))) operator()() const {
    return f.template operator()<N>(get<N>(::std::forward<T>(t)));
  }
  const F& f;
  T&& t;
};

template <class F, class T>
struct by_type_generator {
  template <::size_t N>
  constexpr decltype(::std::declval<const F&>().template
                     operator()<N, typename element<N, T>::type>()) operator()()
      const {
    return f.template operator()<N, typename element<N, T>::type>();
  }
  const F& f;
};

}  // namespace internal_transform

// Applies polymorphic functor F to all elements of the tuple and returns all
// results packed in a tuple of the same size.
//
// The functor F, in addition to the tuple values, should accept one explicit
// template parameter of type size_t.
//
//   struct F {
//     template <size_t N, class T>
//     R operator()(const T& element) const;
//   };
//
// The category of the resulting tuple can be either specified explicitly
// or inferred from the input tuple.
template <class Tag, class T, class F>
constexpr decltype(generate_index<Tag, size<T>::value>(
    ::std::declval<internal_transform::by_value_generator<F, T>>()))
transform_index(const F& f, T&& t) {
  return generate_index<Tag, size<T>::value>(
      internal_transform::by_value_generator<F, T>{f, ::std::forward<T>(t)});
}

template <class T, class F>
constexpr auto transform_index(const F& f, T&& t)
    -> decltype(transform_index<typename tag<T>::type>(f,
                                                       ::std::forward<T>(t))) {
  return transform_index<typename tag<T>::type>(f, ::std::forward<T>(t));
}

// The version of transform_index() where there is no instance of the input
// tuple, only its type. The functor F should look as follows:
//
//   struct F {
//     template <size_t N, class T>
//     R operator()() const;
//   };
template <class Tag, class T, class F>
constexpr decltype(generate_index<Tag, size<T>::value>(
    ::std::declval<internal_transform::by_type_generator<F, T>>()))
transform_index(const F& f) {
  return generate_index<Tag, size<T>::value>(
      internal_transform::by_type_generator<F, T>{f});
}
template <class T, class F>
constexpr decltype(transform_index<typename tag<T>::type, T>(
    ::std::declval<const F&>()))
transform_index(const F& f) {
  return transform_index<typename tag<T>::type, T>(f);
}

// The same as transform_index() above but element index isn't passed to
// the functor. Thus, the functor F should look as follows:
//
//   struct F {
//     template <class T>
//     R operator()(const T& element) const;
//   };
template <class Tag, class T, class F>
constexpr auto transform(const F& f, T&& t)
    -> decltype(transform_index<Tag>(ignore_index(&f), ::std::forward<T>(t))) {
  return transform_index<Tag>(ignore_index(&f), ::std::forward<T>(t));
}
template <class T, class F>
constexpr auto transform(const F& f, T&& t)
    -> decltype(transform_index(ignore_index(&f), ::std::forward<T>(t))) {
  return transform_index(ignore_index(&f), ::std::forward<T>(t));
}

// The version of transform() where there is no instance of the input tuple,
// only its type. The functor F should look as follows:
//
//   struct F {
//     template <class T>
//     R operator()() const;
//   };
template <class Tag, class T, class F>
constexpr decltype(transform_index<Tag, T>(
    ignore_index_no_args(::std::declval<const F*>())))
transform(const F& f) {
  return transform_index<Tag, T>(ignore_index_no_args(&f));
}
template <class T, class F>
constexpr decltype(transform_index<T>(
    ignore_index_no_args(::std::declval<const F*>())))
transform(const F& f) {
  return transform_index<T>(ignore_index_no_args(&f));
}

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_TRANSFORM_H_
