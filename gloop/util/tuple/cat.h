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

// Copyright 2013 Google Inc. All Rights Reserved.

//
// Function template cat() constructs a tuple that is a concatenation of all
// tuples passed as arguments. It's similar to std::tuple_cat.
//
//   tuple<int, char> a(42, 'A');
//   tuple<double, string> b(0.5, "hello");
//   tuple<int, char, double, string> c = cat(a, b);
//   assert(c == make_tuple(42, 'A', 0.5, "hello"));
//
// It's possible to explicitly specify the category of the resulting tuple.
//
//   std::tuple<int> a(42);
//   std::pair<char, string> b('A', "hello");
//   std::tuple<int, char, string> c = cat<std_tuple_tag>(a, b);
//
// If the category isn't specified, it's inferred from the first argument.

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_CAT_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_CAT_H_

#include <stddef.h>

#include <tuple>
#include <utility>

#include "gloop/util/tuple/array.h"
#include "gloop/util/tuple/generate.h"
#include "gloop/util/tuple/int_pack.h"
#include "gloop/util/tuple/intrinsics.h"
#include "gloop/util/tuple/pair.h"
#include "gloop/util/tuple/std_tuple.h"

namespace util {
namespace tuple {

namespace internal_cat {

// All symbols defined within namespace internal_cat are internal
// to cat.h. Do not reference them from outside or your code can break
// without notice.

// Returns the N-th type of the template parameter pack.
template <::size_t N, class... Ts>
struct nth_type;

template <::size_t N, class T, class... Ts>
struct nth_type<N, T, Ts...> : nth_type<N - 1, Ts...> {};

template <class T, class... Ts>
struct nth_type<0, T, Ts...> {
  typedef T type;
};

// Given the pack of tuples Ts and the index of the element in the joined tuple,
// returns two integers, outer and inner, such that Ts[outer][inner] corresponds
// to the I-th element in the joined tuple.
template <::size_t I, class... Ts>
struct pos;

template <::size_t I, class T, class... Ts>
struct pos<I, T, Ts...> {
 private:
  static constexpr ::size_t limit = size<T>::value;
  typedef pos<I - limit, Ts...> next;

 public:
  static constexpr ::size_t outer = I < limit ? 0 : next::outer + 1;
  static constexpr ::size_t inner = I < limit ? I : next::inner;
};

template <::size_t I>
struct pos<I> {
  // Values below don't affect the computation.
  static constexpr ::size_t outer = 0;
  static constexpr ::size_t inner = 0;
};

// Given the pack of tuples and the index of the element in the joined tuple,
// returns the type of the element.
template <::size_t I, class... Ts>
struct joined_elem {
 private:
  typedef pos<I, Ts...> p;

 public:
  typedef
      typename element<p::inner, typename nth_type<p::outer, Ts...>::type>::type
          type;
};

// Given the pack of tuples, returns the size of the joined tuple.
template <class... Ts>
struct joined_size;

template <class T, class... Ts>
struct joined_size<T, Ts...> {
  static constexpr ::size_t value = size<T>::value + joined_size<Ts...>::value;
};

template <>
struct joined_size<> {
  static constexpr ::size_t value = 0;
};

template <class Tag, class Is, class... Ts>
struct result_impl;

template <class Tag, ::size_t... Is, class... Ts>
struct result_impl<Tag, int_pack<Is...>, Ts...>
    : assemble<Tag, typename joined_elem<Is, Ts...>::type...> {};

// Given the pack of tuples, returns the type of the joined tuple.
template <class Tag, class... Ts>
struct result
    : result_impl<Tag,
                  typename make_int_pack<0, joined_size<Ts...>::value>::type,
                  Ts...> {};

// Given the tuple of tuples and the index of the element in the joined tuple,
// returns that element.
template <class... Ts>
struct getter {
  template <::size_t I, class T>
  auto operator()() const -> decltype(get<pos<I, Ts...>::inner>(
      get<pos<I, Ts...>::outer>(::std::declval<::std::tuple<Ts&&...>>()))) {
    typedef pos<I, Ts...> p;
    return get<p::inner>(get<p::outer>(std::move(t)));
  }

  ::std::tuple<Ts&&...>& t;
};

}  // namespace internal_cat

// Constructs a tuple that is a concatenation of all tuples passed as arguments.
template <class Tag, class... Ts>
typename internal_cat::result<Tag, Ts...>::type cat(Ts&&... ts) {
  ::std::tuple<Ts&&...> t(::std::forward<Ts>(ts)...);
  internal_cat::getter<Ts...> op = {t};
  return generate_index<typename internal_cat::result<Tag, Ts...>::type>(op);
}

template <class T, class... Ts>
typename internal_cat::result<typename tag<T>::type, T, Ts...>::type cat(
    T&& t, Ts&&... ts) {
  return cat<typename tag<T>::type>(::std::forward<T>(t),
                                    ::std::forward<Ts>(ts)...);
}

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_CAT_H_
