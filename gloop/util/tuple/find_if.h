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

// Metafunction find_index_if<> returns the index of the first element in the
// tuple with the type whose type satisfies the predicate. If the tuple doesn't
// contain elements of such types, the result is static_cast<size_t>(-1).
//
// The predicate must be a type such that Predicate::template apply<T>::value
// is true if and only if type T satisfies the predicate.
//
//   template <class T>
//   struct type_equal_to {
//     template <class U>
//     struct apply : std::is_same<T, U> {};
//   };
//
//   struct size_four {
//     template <class T>
//     struct apply : std::integral_constant<bool, sizeof(T) == 4> {};
//   };
//
//   typedef tuple<int8, int32> T;
//   assert((find_index_if<type_equal_to<int8>, T>::value == 0));
//   assert((find_index_if<type_equal_to<int32>, T>::value == 1));
//   assert((find_index_if<type_equal_to<int64>, T>::value == -1));
//
//   assert((find_index_if<size_four, T>::value == 1));
//
// Function template find_if() returns a reference to the first element in the
// tuple whose type satisfies the predicate.
//
//   tuple<char, int> t('A', 42);
//   find_if<type_equal_to<char>>(t) = 'B';
//   find_if<type_equal_to<int>>(t) = 24;
//   assert(t == make_tuple('B', 24));

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_FIND_IF_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_FIND_IF_H_

#include <stddef.h>

#include <type_traits>
#include <utility>

#include "gloop/util/tuple/array.h"
#include "gloop/util/tuple/intrinsics.h"
#include "gloop/util/tuple/pair.h"
#include "gloop/util/tuple/std_tuple.h"

namespace util {
namespace tuple {

// The "invalid index" constant. Its value will not change. It's OK to use -1
// explicitly instead of this constant.
typedef ::std::integral_constant<::size_t, static_cast<::size_t>(-1)> npos;

namespace internal_find_if {

// All symbols defined within namespace internal_find_if are internal
// to find_if.h. Do not reference them from outside or your code can break
// without notice.

template <class Predicate, class T, ::size_t I, ::size_t N>
struct find_impl
    : ::std::conditional<
          Predicate::template apply<typename element<I, T>::type>::value,
          ::std::integral_constant<::size_t, I>,
          find_impl<Predicate, T, I + 1, N - 1>>::type {};

template <class Predicate, class T, ::size_t I>
struct find_impl<Predicate, T, I, 0> : npos {};

}  // namespace internal_find_if

// Returns the index of the first element in the tuple whose type satisfies the
// predicate or static_cast<size_t>(-1) if not found.
template <class Predicate, class T>
struct find_index_if
    : internal_find_if::find_impl<Predicate, T, 0, size<T>::value> {};

// Returns a reference to the first element in the tuple whose type satisfies
// the predicate.
template <class Predicate, class T>
auto find_if(T&& t)
    -> decltype(get<find_index_if<Predicate, T>::value>(::std::forward<T>(t))) {
  return get<find_index_if<Predicate, T>::value>(::std::forward<T>(t));
}

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_FIND_IF_H_
