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
// Function template ref() creates a tuple of references to the elements in the
// original tuple. It's handy when you need to do manipulations with tuples
// without copying their elements.
//
//   tuple<int, string> t(42, "hello");
//   tuple<double> q(0.5);
//
//   tuple<int&, string&, q&> s = cat(ref(t), ref(q));
//   assert(s == make_tuple(42, "hello", 0.5));
//   assert(&util::tuple::get<0>(s) == &util::tuple::get<0>(t));

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_REF_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_REF_H_

#include <stddef.h>

#include <utility>

#include "gloop/util/tuple/array.h"
#include "gloop/util/tuple/generate.h"
#include "gloop/util/tuple/intrinsics.h"
#include "gloop/util/tuple/pair.h"
#include "gloop/util/tuple/std_tuple.h"

namespace util {
namespace tuple {

namespace internal_ref {

// All symbols defined within namespace internal_ref are internal
// to ref.h. Do not reference them from outside or your code can break
// without notice.

template <class T>
struct getter {
  template <::size_t N>
  constexpr decltype(::util::tuple::get<N>(::std::declval<T>())) operator()()
      const {
    return ::util::tuple::get<N>(::std::forward<T>(t));
  }
  T&& t;
};

}  // namespace internal_ref

// Creates a tuple of references to the elements in the original tuple.
template <class Tag, class T>
constexpr auto ref(T&& t)
    -> decltype(generate_index<Tag, size<T>::value>(internal_ref::getter<T>{
        ::std::forward<T>(t)})) {
  return generate_index<Tag, size<T>::value>(
      internal_ref::getter<T>{::std::forward<T>(t)});
}

template <class T>
constexpr auto ref(T&& t) -> decltype(::util::tuple::ref<typename tag<T>::type>(
    ::std::forward<T>(t))) {
  return ::util::tuple::ref<typename tag<T>::type>(::std::forward<T>(t));
}

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_REF_H_
