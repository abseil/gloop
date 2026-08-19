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

// Function templates direct_initialize and brace_initialize create an instance
// of an object by passing all elements from the supplied tuple to the objects
// constructor. The difference between the two functions is the initialization
// type they use.
//
// direct_initialize<R>(args_packed_in_tuple) creates an object of type R by
// means of direct initialization with arguments from the supplied tuple.
// direct_initialize<R>(make_tuple(t1, ..., tn)) evaluates to R(t1, ..., tn).
//
//   vector<int> v = direct_initialize<vector<int>>(make_tuple(4, 2));
//   assert(v == {2, 2, 2, 2});
//
// brace_initialize<R>(args_packed_in_tuple) creates an object of type R by
// means of brace initialization with arguments from the supplied tuple.
// brace_initialize<R>(make_tuple(t1, ..., tn)) evaluates to R{t1, ..., tn}.
//
//   vector<int> v = brace_initialize<vector<int>>(make_tuple(4, 2));
//   assert(v == {4, 2});
//
// direct_initialize_t and brace_initialize_t are the functor versions of
// direct_initialize and brace_initialize.
//
//   auto a = make_tuple(make_tuple(42, 'x'), make_tuple(1337, 'y'));
//   auto b = transform(direct_initialize_t<string>(), a);
//   assert(b == make_tuple(string(42, 'x'), string(1337, 'y')));

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_CONSTRUCT_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_CONSTRUCT_H_

#include <type_traits>
#include <utility>

#include "gloop/util/tuple/apply.h"

namespace util {
namespace tuple {

namespace internal_construct {

// All symbols defined within namespace internal_construct are internal
// to construct.h. Do not reference them from outside or your code can break
// without notice.

template <class R>
struct direct_initializer {
  template <class... Args>
  R operator()(Args&&... args) const {
    static_assert(std::is_constructible<R, Args...>(),
                  "Can't direct-initialize R from Args...");
    return R(::std::forward<Args>(args)...);
  }
};

template <class R>
struct brace_initializer {
  template <class... Args>
  R operator()(Args&&... args) const {
    return R{::std::forward<Args>(args)...};
  }
};

}  // namespace internal_construct

// Creates an object of type R by means of direct initialization with
// arguments from the supplied tuple.
//
// direct_initialize<R>(make_tuple(t1, ..., tn)) evaluates to R(t1, ..., tn).
template <class R, class T>
R direct_initialize(T&& args) {
  return ::util::tuple::apply(internal_construct::direct_initializer<R>(),
                              ::std::forward<T>(args));
}

// Creates an object of type R by means of brace initialization with
// arguments from the supplied tuple.
//
// brace_initialize<R>(make_tuple(t1, ..., tn)) evaluates to R{t1, ..., tn}.
template <class R, class T>
R brace_initialize(T&& args) {
  return ::util::tuple::apply(internal_construct::brace_initializer<R>(),
                              ::std::forward<T>(args));
}

// The same as direct_initialize but in a functor form.
// direct_initialize_t<R>()(t) evaluates to direct_initialize<R>(t).
template <class R>
struct direct_initialize_t {
  template <class T>
  R operator()(T&& args) const {
    return ::util::tuple::direct_initialize<R>(::std::forward<T>(args));
  }
};

// The same as brace_initialize but in a functor form.
// brace_initialize_t<R>()(t) evaluates to brace_initialize<R>(t).
template <class R>
struct brace_initialize_t {
  template <class T>
  R operator()(T&& args) const {
    return ::util::tuple::brace_initialize<R>(::std::forward<T>(args));
  }
};

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_CONSTRUCT_H_
