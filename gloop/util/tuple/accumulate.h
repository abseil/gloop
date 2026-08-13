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
// Accumulate, also known as left fold and reduce, applies the binary operation
// to elements of the tuple continuously until only one value remains.
//
// accumulate(f, make_tuple(t0, t1, ..., tn), t) is equivalent to
// f(...(f(f(t, t0), t1), ...), tn). Thus, accumulate(f, make_tuple(), t) is
// t and accumulate(f, make_tuple(t0), t) is f(t, t0).
//
// accumulate(f, make_tuple(t0, t1, t2, ..., tn)) is equivalent to
// f(...(f(f(t0, t1), t2), ...), tn). Naturally, n must be positive.
//
// accumulate<tuple<T0, T1, ..., TN>>(f, t) is equivalent to
// f.operator()<TN>(...(f.operator()<T1>(f.operator()<T0>(t)) ...).
//
// accumulate_index() is similar to accumulate(). The only difference is that
// it passes the indices of the elements to the functor as the first template
// parameter of type size_t. In other words,
// accumulate_index(f, make_tuple(t0, t1, ..., tn), t) is equivalent to
// f.operator()<n>(...(f.operator()<1>(f.operator()<0>(t, t0), t1), ...), tn).
//
// The implementation is optimized to elide copies of intermediate state to
// make it efficient to accept the state argument by value. The number of
// state copies performed by accumulate() and accumulate_index() is 1 for the
// initial state plus floor((N - 1) / 9).
//
// struct Append {
//  template <class T>
//  string operator()(string state, const T& element) const {
//    if (!state.empty()) StrAppend(&state, ", ");
//    StrAppend(&state, element);
//    return state;
//  }
// };
//
// auto t = make_tuple("Hello", 42, 0.5);
// // Efficient. Doesn't call string's copy constructor.
// string s = accumulate(Append(), t, "");
// assert(s == "Hello, 42, 0.5");

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_ACCUMULATE_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_ACCUMULATE_H_

#include <stddef.h>

#include <utility>

#include "gloop/util/tuple/array.h"
#include "gloop/util/tuple/intrinsics.h"
#include "gloop/util/tuple/pair.h"
#include "gloop/util/tuple/std_tuple.h"

#define IN_UTIL_TUPLE_ACCUMULATE_H
#include "gloop/util/tuple/internal_accumulate.h"
#undef IN_UTIL_TUPLE_ACCUMULATE_H

namespace util {
namespace tuple {

// accumulate_index(f, make_tuple(t0, t1, ..., tn), t) is equivalent to
// f.operator()<n>(...(f.operator()<1>(f.operator()<0>(t, t0), t1), ...), tn).
template <class T, class F, class S>
constexpr auto accumulate_index(const F& f, T&& t, S&& state)
    -> decltype(internal_accumulate::value_index_folder<size<T>::value>()
                    .template operator()<0>(f, ::std::forward<T>(t),
                                            ::std::forward<S>(state))) {
  return internal_accumulate::value_index_folder<size<T>::value>().template
  operator()<0>(f, ::std::forward<T>(t), ::std::forward<S>(state));
}

// accumulate(f, make_tuple(t0, t1, ..., tn), t) is equivalent to
// f(...(f(f(t, t0), t1), ...), tn). Thus, accumulate(f, make_tuple(), t) is
// t and accumulate(f, make_tuple(t0), t) is f(t, t0).
template <class T, class F, class S>
constexpr auto accumulate(const F& f, T&& t, S&& state)
    -> decltype(internal_accumulate::value_folder<size<T>::value>()
                    .template operator()<0>(f, ::std::forward<T>(t),
                                            ::std::forward<S>(state))) {
  return internal_accumulate::value_folder<size<T>::value>().template
  operator()<0>(f, ::std::forward<T>(t), ::std::forward<S>(state));
}

// accumulate<tuple<T0, T1, ..., TN>>(f, t) is equivalent to
// f.operator()<TN>(...(f.operator()<T1>(f.operator()<T0>(t)) ...).
template <class T, class F, class S>
constexpr auto accumulate(const F& f, S&& state)
    -> decltype(internal_accumulate::folder<size<T>::value>()
                    .template operator()<0, T>(f, ::std::forward<S>(state))) {
  return internal_accumulate::folder<size<T>::value>()
      .template operator()<0, T>(f, ::std::forward<S>(state));
}

// accumulate_index<tuple<T0, T1, ..., TN>>(f, t) is equivalent to
// f.operator()<N, TN>(...(f.operator()<1, T1>(f.operator()<0, T0>(t)) ...).
template <class T, class F, class S>
constexpr auto accumulate_index(const F& f, S&& state)
    -> decltype(internal_accumulate::index_folder<size<T>::value>()
                    .template operator()<0, T>(f, ::std::forward<S>(state))) {
  return internal_accumulate::index_folder<size<T>::value>()
      .template operator()<0, T>(f, ::std::forward<S>(state));
}

// accumulate(f, make_tuple(t0, t1, t2, ..., tn)) is equivalent to
// f(...(f(f(t0, t1), t2), ...), tn). Naturally, n must be positive.
template <class T, class F>
constexpr auto accumulate(const F& f, T&& t)
    -> decltype(internal_accumulate::value_folder<size<T>::value - 1>()
                    .template operator()<1>(f, ::std::forward<T>(t),
                                            get<0>(::std::forward<T>(t)))) {
  return internal_accumulate::value_folder<size<T>::value - 1>().template
  operator()<1>(f, ::std::forward<T>(t), get<0>(::std::forward<T>(t)));
}

// accumulate_index(f, make_tuple(t0, t1, t2, ..., tn)) is equivalent to
// f.operator()<n>(...(f.operator()<2>(f.operator()<1>(t0, t1), t2), ...), tn).
// Naturally, n must be positive.
template <class T, class F>
constexpr auto accumulate_index(const F& f, T&& t)
    -> decltype(internal_accumulate::value_index_folder<size<T>::value - 1>()
                    .template operator()<1>(f, ::std::forward<T>(t),
                                            get<0>(::std::forward<T>(t)))) {
  return internal_accumulate::value_index_folder<size<T>::value - 1>().template
  operator()<1>(f, ::std::forward<T>(t), get<0>(::std::forward<T>(t)));
}

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_ACCUMULATE_H_
