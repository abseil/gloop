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
// Function template push_front() inserts a copy of the object to the front
// of the tuple; push_front_ref() inserts a reference.
//
//   tuple<int, string> a(42, "hello");
//   tuple<double, int, string> b = push_front(a, 0.5);
//   assert(b == make_tuple(0.5, 42, "hello"));
//
//   double d = 0.5;
//   tuple<double&, int, string> c = push_front_ref(a, d);
//   assert(&get<0>(c) == &d);

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_PUSH_FRONT_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_PUSH_FRONT_H_

#include <utility>

#include "gloop/util/tuple/insert.h"

namespace util {
namespace tuple {

template <class V, class T>
auto push_front(T&& t, V&& v)
    -> decltype(insert<0>(::std::forward<T>(t), ::std::forward<V>(v))) {
  return insert<0>(::std::forward<T>(t), ::std::forward<V>(v));
}

template <class V, class T>
auto push_front_ref(T&& t, V&& v)
    -> decltype(insert_ref<0>(::std::forward<T>(t), ::std::forward<V>(v))) {
  return insert_ref<0>(::std::forward<T>(t), ::std::forward<V>(v));
}

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_PUSH_FRONT_H_
