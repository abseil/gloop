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
// rotate<M>(make_tuple(a1, a2, ..., aM, ..., aN)) evaluates to
// make_tuple(aM, ..., aN, a1, ..., aM-1). M must be in [0, N]. It's similar to
// std::rotate() but for tuples instead of sequences.
//
//   tuple<int, string, void*> a(42, "hello", nullptr);
//   tuple<string, void*, int> b = rotate<1>(src);
//   assert(b == make_tuple("hello", nullptr, 42));

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_ROTATE_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_ROTATE_H_

#include <stddef.h>

#include <utility>

#include "gloop/util/tuple/cat.h"
#include "gloop/util/tuple/intrinsics.h"
#include "gloop/util/tuple/slice.h"

namespace util {
namespace tuple {

// This version of rotate() allows the category of the resulting tuple to be
// explicitly specified.
//
//   struct S {
//     TUPLE_DEFINE_STRUCT(S, (),
//                         (int, n),
//                         (string, s));
//   };
//
//   auto p = make_pair("hello", 42);
//   S s = rotate<tag<S>::type, 1>(p);
template <class Tag, ::size_t M, class T>
auto rotate(T&& t) -> decltype(::util::tuple::cat<Tag>(
    ::util::tuple::slice_range<M, ::util::tuple::size<T>{}>(
        ::std::forward<T>(t)),
    ::util::tuple::slice_range<0, M>(::std::forward<T>(t)))) {
  return ::util::tuple::cat<Tag>(
      ::util::tuple::slice_range<M, ::util::tuple::size<T>{}>(
          ::std::forward<T>(t)),
      ::util::tuple::slice_range<0, M>(::std::forward<T>(t)));
}

// This version of rotate() returns the same category of tuple as the input.
//
//   auto a = make_pair(string("hello"), 42);
//   auto b = rotate<1>(p);  // returns pair<int, string>
//
//   auto c = make_tuple(string("hello"), 42);
//   auto d = rotate<1>(p);  // returns tuple<int, string>
template <::size_t M, class T>
auto rotate(T&& t)
    -> decltype(::util::tuple::rotate<typename ::util::tuple::tag<T>::type, M>(
        ::std::forward<T>(t))) {
  return ::util::tuple::rotate<typename ::util::tuple::tag<T>::type, M>(
      ::std::forward<T>(t));
}

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_ROTATE_H_
