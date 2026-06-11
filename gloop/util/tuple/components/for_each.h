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
// Algorithm for_each() applies the unary function to all elements of the tuple,
// from first to the last.
//
// for_each(f, make_tuple(t1, t2, ..., tn)) is equivalent to
// f(t1), f(t2), ..., f(tn) and evaluates to void. for_each(f, make_tuple())
// has no effect.
//
// for_each_index(f, make_tuple(t1, t2, ..., tn)) is equivalent to
// f.operator()<0>(t1), f.operator()<1>(t2), ..., f.operator()<N-1>(tn).
//
// for_each<tuple<T1, T2, ..., TN>>(f) is equivalent to
// f<T1>(), f<T2>(), ..., f<TN>().
//
// for_each_index<tuple<T1, T2, ..., TN>>(f) is equivalent to
// f.operator()<0, T1>(), f.operator()<1, T2>(), ..., f.operator()<N-1, TN>().
//
// struct Logger {
//   template <class T>
//   void operator()(const T& element) const {
//     LOG(INFO) << element;
//   }
// };
//
// auto t = make_tuple("Hello", 42, 0.5);
// for_each(Logger(), t);
//
// IWYU pragma: private, include "util/tuple/tuple.h"

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_FOR_EACH_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_FOR_EACH_H_

#include <stddef.h>

#include <utility>

#include "gloop/util/tuple/components/ignore_index.h"
#include "gloop/util/tuple/components/intrinsics.h"
#include "gloop/util/tuple/components/iterate.h"

namespace util {
namespace tuple {

namespace internal_for_each {

// All symbols defined within namespace internal_for_each are internal
// to for_each.h. Do not reference them from outside or your code can break
// without notice.

template <class T, class F>
struct value_op {
  template <::size_t N>
  constexpr void operator()() const {
    f.template operator()<N>(get<N>(::std::forward<T>(t)));
  }
  T&& t;
  const F& f;
};

template <class T, class F>
struct type_op {
  template <::size_t N>
  constexpr void operator()() const {
    f.template operator()<N, typename element<N, T>::type>();
  }
  const F& f;
};

}  // namespace internal_for_each

template <class T, class F>
constexpr void for_each_index(const F& f, T&& t) {
  ::util::tuple::iterate_index<size<T>::value>(
      internal_for_each::value_op<T, F>{::std::forward<T>(t), f});
}

template <class T, class F>
constexpr void for_each(const F& f, T&& t) {
  ::util::tuple::for_each_index(ignore_index(&f), ::std::forward<T>(t));
}

template <class T, class F>
constexpr void for_each_index(const F& f) {
  ::util::tuple::iterate_index<size<T>::value>(
      internal_for_each::type_op<T, F>{f});
}

template <class T, class F>
constexpr void for_each(const F& f) {
  ::util::tuple::for_each_index<T>(ignore_index_no_args(&f));
}

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_COMPONENTS_FOR_EACH_H_
