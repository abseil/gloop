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
// Metafunction is_tuple<T> returns true if T is a tuple (more specifically,
// if util::tuple::tag<T>::type is defined).
//
//   assert((is_tuple<tuple<int, char, string>>::value));
//   assert((is_tuple<pair<int, char>>::value));
//   assert((!is_tuple<int>::value));

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_IS_TUPLE_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_IS_TUPLE_H_

#include <type_traits>

#include "gloop/util/tuple/array.h"
#include "gloop/util/tuple/intrinsics.h"
#include "gloop/util/tuple/pair.h"
#include "gloop/util/tuple/std_tuple.h"

namespace util {
namespace tuple {

namespace internal_is_tuple {

// All symbols defined within namespace internal_is_tuple are internal
// to is_tuple.h. Do not reference them from outside or your code can break
// without notice.

template <class T>
struct mk_void {
  typedef void type;
};

template <class T, class E = void>
struct has_inner_type : ::std::false_type {};

template <class T>
struct has_inner_type<T, typename mk_void<typename T::type>::type>
    : ::std::true_type {};

}  // namespace internal_is_tuple

// Is T a tuple?
template <class T>
struct is_tuple : internal_is_tuple::has_inner_type<tag<T>> {};

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_IS_TUPLE_H_
