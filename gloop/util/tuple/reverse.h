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

// Function template reverse() reverses elements in the tuple.
//
//   tuple<int, string, double> a(42, "hello", 0.5);
//   tuple<double, string, int> b = reverse(a);
//   assert(b == make_tuple(0.5, "hello", 42));

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_REVERSE_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_REVERSE_H_

#include <stddef.h>
#include <stdlib.h>

#include <utility>

#include "gloop/util/tuple/array.h"
#include "gloop/util/tuple/generate.h"
#include "gloop/util/tuple/intrinsics.h"
#include "gloop/util/tuple/pair.h"
#include "gloop/util/tuple/std_tuple.h"

namespace util {
namespace tuple {

namespace internal_reverse {

// All symbols defined within namespace internal_reverse are internal
// to reverse.h. Do not reference them from outside or your code can break
// without notice.

// To avoid extra copies, we are using a separate functor for computing the
// types of the elements instead of just putting element<...> as the result type
// in value_getter.
template <class T>
struct type_getter {
  template <::size_t N>
  typename element<size<T>::value - N - 1, T>::type operator()() const;
};

template <class T>
struct value_getter {
  template <::size_t N, class R>
  decltype(get<size<T>::value - N - 1>(::std::declval<T>())) operator()()
      const {
    return get<size<T>::value - N - 1>(::std::forward<T>(t));
  }
  T&& t;
};

}  // namespace internal_reverse

// Reverses elements in the tuple.
template <class T>
decltype(generate_index<typename tag<T>::type, size<T>::value>(
    internal_reverse::type_getter<T>()))
reverse(T&& t) {
  typedef decltype(generate_index<typename tag<T>::type, size<T>::value>(
      internal_reverse::type_getter<T>())) R;
  return generate_index<R>(
      internal_reverse::value_getter<T>{::std::forward<T>(t)});
}

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_REVERSE_H_
