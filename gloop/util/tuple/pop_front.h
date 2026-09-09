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

// Function template pop_front() returns a copy of the tuple without the
// first element. The type type of the Nth element in the resulting tuple is the
// same as the (N+1)th element in the original tuple.
//
//   tuple<char, int, string> a('A', 42, "hello");
//   tuple<int, string> b = pop_front(a);
//   assert(b == make_tuple(42, "hello");

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_POP_FRONT_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_POP_FRONT_H_

#include <stddef.h>

#include <utility>

#include "gloop/util/tuple/erase.h"

namespace util {
namespace tuple {

template <class T>
constexpr auto pop_front(T&& t) -> decltype(erase<0>(::std::forward<T>(t))) {
  return erase<0>(::std::forward<T>(t));
}

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_POP_FRONT_H_
