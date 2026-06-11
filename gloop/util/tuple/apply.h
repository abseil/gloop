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
// apply(a1, ..., aN, make_tuple(b1, ..., bM)) is equivalent to
// std::invoke(a1, ..., aN, b1, ..., bM).
//
// In a simple case when N is 1, we get the following:
// apply(f, make_tuple(b1, ..., bM)) evaluates to f(b1, ..., bM).
//
//   int Minus(int a, int b) { return a - b; }
//   assert(apply(Minus, make_tuple(3, 2)) == 1);
//
// Another example:
//
//   class Calculator {
//    public:
//     int Minus(int a, int b) { return a - b; }
//   };
//
//   Calculator c;
//   assert(apply(&Calculator::Minus, &c, make_tuple(3, 2)) == 1);

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_APPLY_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_APPLY_H_

#include <stddef.h>

#include <cstddef>
#include <functional>
#include <tuple>
#include <utility>

#include "gloop/util/tuple/array.h"
#include "gloop/util/tuple/back.h"
#include "gloop/util/tuple/cat.h"
#include "gloop/util/tuple/int_pack.h"
#include "gloop/util/tuple/intrinsics.h"
#include "gloop/util/tuple/pair.h"
#include "gloop/util/tuple/pop_back.h"
#include "gloop/util/tuple/ref.h"
#include "gloop/util/tuple/std_tuple.h"

namespace util {
namespace tuple {

namespace internal_apply {

// All symbols defined within namespace internal_apply are internal
// to apply.h. Do not reference them from outside or your code can break
// without notice.

template <class Args, ::size_t... Is>
constexpr auto do_apply(Args&& args, int_pack<Is...> indices)
    -> decltype(std::invoke(get<Is>(::std::forward<Args>(args))...)) {
  return std::invoke(get<Is>(::std::forward<Args>(args))...);
}

template <class Args>
constexpr auto apply_impl(Args&& args) -> decltype(internal_apply::do_apply(
    ::std::forward<Args>(args), make_int_pack<0, size<Args>::value>())) {
  return internal_apply::do_apply(::std::forward<Args>(args),
                                  make_int_pack<0, size<Args>::value>());
}

template <typename F, typename Tuple, std::size_t... Is>
constexpr bool is_applicable_impl(std::index_sequence<Is...>) {
  return std::is_invocable_v<F, std::tuple_element_t<Is, Tuple>...>;
}

}  // namespace internal_apply

// The last argument must be a tuple.
//
// apply(a1, ..., aN, make_tuple(b1, ..., bM)) is equivalent to
// std::invoke(a1, ..., aN, b1, ..., bM).
template <class... Args>
constexpr auto apply(Args&&... args)
    -> decltype(internal_apply::apply_impl(tuple::cat(
        tuple::pop_back(::std::forward_as_tuple(::std::forward<Args>(args)...)),
        tuple::ref<std_tuple_tag>(tuple::back(
            ::std::forward_as_tuple(::std::forward<Args>(args)...)))))) {
  return internal_apply::apply_impl(tuple::cat(
      tuple::pop_back(::std::forward_as_tuple(::std::forward<Args>(args)...)),
      tuple::ref<std_tuple_tag>(tuple::back(
          ::std::forward_as_tuple(::std::forward<Args>(args)...)))));
}

// Returns whether `F(f1, f2, ..., fM)` is a valid call.
template <typename F, typename Tuple>
constexpr bool is_applicable() {
  return internal_apply::is_applicable_impl<F, Tuple>(
      std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_APPLY_H_
