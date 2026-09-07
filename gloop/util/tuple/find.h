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

// Metafunction find_index<> returns the index of the first element in the
// tuple with the matching type. If the tuple doesn't contain elements of
// the specified type, the result is static_cast<size_t>(-1).
//
//   assert((find_index<char, tuple<char, int>>::value == 0));
//   assert((find_index<int, tuple<char, int>>::value == 1));
//   assert((find_index<double, tuple<char, int>>::value == -1));
//
// Function template find() returns a reference to the first element in the
// tuple with the matching type.
//
//   tuple<char, int> t('A', 42);
//   find<char>(t) = 'B';
//   find<int>(t) = 24;
//   assert(t == make_tuple('B', 24));

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_FIND_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_FIND_H_

#include <stddef.h>

#include <type_traits>
#include <utility>

#include "gloop/util/tuple/find_if.h"

namespace util {
namespace tuple {

namespace internal_find {

template <class T>
struct type_equal_to {
  template <class U>
  struct apply : ::std::is_same<T, U> {};
};

}  // namespace internal_find

// The "invalid index" constant. Its value will not change. It's OK to use -1
// explicitly instead of this constant.
typedef ::std::integral_constant<::size_t, static_cast<::size_t>(-1)> npos;

// Returns the index of the first element in the tuple with the matching type or
// static_cast<size_t>(-1) if not found.
template <class V, class T>
struct find_index : find_index_if<internal_find::type_equal_to<V>, T> {};

// Returns a reference to the first element in the tuple with the matching type.
template <class V, class T>
auto find(T&& t)
    -> decltype(get<find_index<V, T>::value>(::std::forward<T>(t))) {
  return get<find_index<V, T>::value>(::std::forward<T>(t));
}

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_FIND_H_
