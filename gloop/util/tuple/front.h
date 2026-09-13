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

// Function template front() returns the first element of the tuple.
// front(t) is equivalent to get<0>(t); consequently, it can't be applied to
// an empty tuple.
//
//   tuple<int, string> t;
//   front(t) = 42;
//   assert(front(t) == 42);

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_FRONT_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_FRONT_H_

#include <utility>

#include "gloop/util/tuple/array.h"
#include "gloop/util/tuple/intrinsics.h"
#include "gloop/util/tuple/pair.h"
#include "gloop/util/tuple/std_tuple.h"

namespace util {
namespace tuple {

template <class T>
auto front(T&& t) -> decltype(get<0>(::std::forward<T>(t))) {
  return get<0>(::std::forward<T>(t));
}

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_FRONT_H_
