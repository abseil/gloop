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

// Function template any_of(predicate, t) returns true if the predicate is
// true for at least one element in the tuple.
//
// any_of(p, make_tuple(t1, t2, ..., tn)) is equivalent to
// p(t1) || p(t2) || ... || p(tn).
//
// struct IsPositive {
//   template <class T>
//   bool operator()(const T& value) const {
//     return value > 0;
//   }
// };
//
// assert(!any_of(IsPositive(), make_tuple(-1, -0.5)));
// assert(any_of(IsPositive(), make_tuple(-7, 3.14)));
// assert(!any_of(IsPositive(), make_tuple()));
//
// any_of<tuple<T1, T2, ..., Tn>>(p) is equivalent to
// p.operator()<T1>() || p.operator()<T2>() || ... || p.operator()<Tn>().

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_ANY_OF_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_ANY_OF_H_

#include <type_traits>
#include <utility>

#include "gloop/util/tuple/accumulate.h"

namespace util {
namespace tuple {

namespace internal_any_of {

// All symbols defined within namespace internal_any_of are internal
// to any_of.h. Do not reference them from outside or your code can break
// without notice.

template <class Pred>
struct value_op {
  template <class Elem>
  bool operator()(bool state, Elem&& elem) const {
    return state || pred(::std::forward<Elem>(elem));
  }
  const Pred& pred;
};

template <class Pred>
struct type_op {
  template <class Elem>
  bool operator()(bool state) const {
    return state || pred.template operator()<Elem>();
  }
  const Pred& pred;
};

template <class Pred>
struct meta_op {
  template <class Elem, bool State>
  ::std::integral_constant<bool, State || Pred::template apply<Elem>::value>
  operator()(::std::integral_constant<bool, State>) const;
};

}  // namespace internal_any_of

template <class T, class Pred>
bool any_of(const Pred& pred, T&& t) {
  return accumulate(internal_any_of::value_op<Pred>{pred}, ::std::forward<T>(t),
                    false);
}

template <class T, class Pred>
bool any_of(const Pred& pred) {
  return accumulate<T>(internal_any_of::type_op<Pred>{pred}, false);
}

// TODO: make any_of<T>(pred) above constexpr and remove this.
template <class Pred, class T>
struct any_type_of
    : ::std::remove_cv<typename ::std::remove_reference<decltype(accumulate<T>(
          internal_any_of::meta_op<Pred>(), ::std::false_type()))>::type>::
          type {};

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_ANY_OF_H_
