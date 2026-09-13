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

// Function template push_back() inserts a copy of the object to the back
// of the tuple; push_back_ref() inserts a reference.
//
//   tuple<int, string> a(42, "hello");
//   tuple<int, string, double> b = push_back(a, 0.5);
//   assert(b == make_tuple(42, "hello", 0.5));
//
//   double d = 0.5;
//   tuple<int, string, double&> c = push_back_ref(a, d);
//   assert(&get<2>(c) == &d);

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_PUSH_BACK_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_PUSH_BACK_H_

#include <utility>

#include "gloop/util/tuple/insert.h"
#include "gloop/util/tuple/intrinsics.h"

namespace util {
namespace tuple {

template <class V, class T>
constexpr auto push_back(T&& t, V&& v)
    -> decltype(insert<size<T>::value>(::std::forward<T>(t),
                                       ::std::forward<V>(v))) {
  return insert<size<T>::value>(::std::forward<T>(t), ::std::forward<V>(v));
}

template <class V, class T>
constexpr auto push_back_ref(T&& t, V&& v)
    -> decltype(insert_ref<size<T>::value>(::std::forward<T>(t),
                                           ::std::forward<V>(v))) {
  return insert_ref<size<T>::value>(::std::forward<T>(t), ::std::forward<V>(v));
}

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_PUSH_BACK_H_
