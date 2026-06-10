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

// Function template switch_index() converts runtime integers to compile time.
//
//     switch_index<From, To>(f, idx)
// evaluates to
//     f(std::integral_constant<int, I>())
// where I == idx.
//
// Requires: From <= idx < To (or behavior is otherwise undefined).
// Requires: decltype(f(std::integral_constant<int, I>())) is the same for
//           all I in [From, To).
//
// The implementation is equivalent to the following pseudo code:
//
//   switch (idx) {
//     case From + 0: return f(std::integral_constant<int, From + 0>());
//     case From + 1: return f(std::integral_constant<int, From + 1>());
//     case From + 2: return f(std::integral_constant<int, From + 2>());
//     ...
//     case To - 1: return f(std::integral_constant<int, To - 1>());
//     default: DLOG(FATAL) << "Index out of bounds";
//   }
//
// Example:
//
//   DEFINE_int32(array_size, 10, "Array size, must be in [0, 1000)");
//
//   struct F {
//     template <int N>
//     T operator()(std::integral_constant<int, N>) const {
//       assert(N == FLAGS_array_size);
//       std::array<int, N> buffer;
//       ...
//     }
//   };
//
//   CHECK(FLAGS_array_size >= 0 && FLAGS_array_size < 1000);
//   T val = switch_index<0, 1000>(F(), FLAGS_array_size);

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_SWITCH_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_SWITCH_H_

#include <stddef.h>

#include <cassert>
#include <type_traits>
#include <utility>

#include "absl/utility/utility.h"

namespace gtl {

namespace internal_switch {

// All symbols defined within namespace internal_switch are internal
// to switch.h. Do not reference them from outside or your code can break
// without notice.

template <int N>
using static_int = std::integral_constant<int, N>;

// The return type of invoking the functor for I.
template <typename F, int I>
using ReturnType = decltype(std::declval<F>()(static_int<I>()));

// MSVC doesn't like int pack expansions, so we use a different, slightly less
// efficient implementation.
#if defined(COMPILER_MSVC)
template <typename Target, typename F, int From>
constexpr bool SameReturnTypesImpl(static_int<0>) {
  return true;
}
template <typename Target, typename F, int From>
constexpr bool SameReturnTypesImpl(static_int<1>) {
  return std::is_same<Target, ReturnType<F, From>>::value;
}
template <typename Target, typename F, int From, int N>
constexpr bool SameReturnTypesImpl(static_int<N>) {
  return SameReturnTypesImpl<Target, F, From>(static_int<N / 2>()) &&
         SameReturnTypesImpl<Target, F, From + N / 2>(static_int<N - N / 2>());
}
template <typename Target, typename F, int From, int N>
constexpr bool SameReturnTypes() {
  return SameReturnTypesImpl<Target, F, From>(static_int<N>());
}
#else  // COMPILER_MSVC
// Returns true if ReturnType<F, From + Is> are all the same as Target.
template <typename Target, typename F, int From, int... Is>
constexpr bool SameReturnTypesImpl(absl::integer_sequence<int, Is...>) {
  return std::is_same<
      absl::integer_sequence<bool, (true || Is)...>,
      absl::integer_sequence<
          bool,
          std::is_same<Target, ReturnType<F, From + Is>>::value...>>::value;
}
template <typename Target, typename F, int From, int N>
constexpr bool SameReturnTypes() {
  return SameReturnTypesImpl<Target, F, From>(
      absl::make_integer_sequence<int, N>());
}
#endif

// Evaluates to f(std::integral_constant<int, I>()) where I == idx.
//
// Requires: From <= idx < From + n (or behavior is otherwise undefined).
// Requires: decltype(f(std::integral_constant<int, I>())) is the same for
//           all I in [From, From + n).
//
// There are two implementations:
//  - If n <= 8, the overloads use a direct switch() statement.
//  - Otherwise, it uses binary search to reduce the search space.
//
// Why do we do this?
// A simple recursive function would do the job correctly, but it would run
// slower.
// When using a switch(), the compiler can choose to use a chain of if()
// statements or a jump table.
// On the other hand, gcc 4.9 will compile the recursive function always as a
// chain of if() statements, even if a jump table is better.
// clang 3.5 will do it correctly and use a jump table when appropriate.
template <typename R, int From, typename F>
inline R switch_impl(F&& f, static_int<From>, static_int<1>,  // NOLINT
                     int idx) {
  (void)idx;
  assert(idx == From);
  return std::forward<F>(f)(static_int<From>());
}

template <typename R, int From, typename F>
inline R switch_impl(F&& f, static_int<From>, static_int<2>,  // NOLINT
                     int idx) {
  if (idx == From) return std::forward<F>(f)(static_int<From>());
  assert(idx == From + 1);
  return std::forward<F>(f)(static_int<From + 1>());
}

template <typename R, int From, typename F>
inline R switch_impl(F&& f, static_int<From>, static_int<3>,  // NOLINT
                     int idx) {
  switch (idx) {
    case From:
      return std::forward<F>(f)(static_int<From>());
    case From + 1:
      return std::forward<F>(f)(static_int<From + 1>());
    default:
      assert(idx == From + 2);
      return std::forward<F>(f)(static_int<From + 2>());
  }
}

template <typename R, int From, typename F>
inline R switch_impl(F&& f, static_int<From>, static_int<4>,  // NOLINT
                     int idx) {
  switch (idx) {
    case From:
      return std::forward<F>(f)(static_int<From>());
    case From + 1:
      return std::forward<F>(f)(static_int<From + 1>());
    case From + 2:
      return std::forward<F>(f)(static_int<From + 2>());
    default:
      assert(idx == From + 3);
      return std::forward<F>(f)(static_int<From + 3>());
  }
}

template <typename R, int From, typename F>
inline R switch_impl(F&& f, static_int<From>, static_int<5>,  // NOLINT
                     int idx) {
  switch (idx) {
    case From:
      return std::forward<F>(f)(static_int<From>());
    case From + 1:
      return std::forward<F>(f)(static_int<From + 1>());
    case From + 2:
      return std::forward<F>(f)(static_int<From + 2>());
    case From + 3:
      return std::forward<F>(f)(static_int<From + 3>());
    default:
      assert(idx == From + 4);
      return std::forward<F>(f)(static_int<From + 4>());
  }
}

template <typename R, int From, typename F>
inline R switch_impl(F&& f, static_int<From>, static_int<6>,  // NOLINT
                     int idx) {
  switch (idx) {
    case From:
      return std::forward<F>(f)(static_int<From>());
    case From + 1:
      return std::forward<F>(f)(static_int<From + 1>());
    case From + 2:
      return std::forward<F>(f)(static_int<From + 2>());
    case From + 3:
      return std::forward<F>(f)(static_int<From + 3>());
    case From + 4:
      return std::forward<F>(f)(static_int<From + 4>());
    default:
      assert(idx == From + 5);
      return std::forward<F>(f)(static_int<From + 5>());
  }
}

template <typename R, int From, typename F>
inline R switch_impl(F&& f, static_int<From>, static_int<7>,  // NOLINT
                     int idx) {
  switch (idx) {
    case From:
      return std::forward<F>(f)(static_int<From>());
    case From + 1:
      return std::forward<F>(f)(static_int<From + 1>());
    case From + 2:
      return std::forward<F>(f)(static_int<From + 2>());
    case From + 3:
      return std::forward<F>(f)(static_int<From + 3>());
    case From + 4:
      return std::forward<F>(f)(static_int<From + 4>());
    case From + 5:
      return std::forward<F>(f)(static_int<From + 5>());
    default:
      assert(idx == From + 6);
      return std::forward<F>(f)(static_int<From + 6>());
  }
}

template <typename R, int From, typename F>
inline R switch_impl(F&& f, static_int<From>, static_int<8>,  // NOLINT
                     int idx) {
  switch (idx) {
    case From:
      return std::forward<F>(f)(static_int<From>());
    case From + 1:
      return std::forward<F>(f)(static_int<From + 1>());
    case From + 2:
      return std::forward<F>(f)(static_int<From + 2>());
    case From + 3:
      return std::forward<F>(f)(static_int<From + 3>());
    case From + 4:
      return std::forward<F>(f)(static_int<From + 4>());
    case From + 5:
      return std::forward<F>(f)(static_int<From + 5>());
    case From + 6:
      return std::forward<F>(f)(static_int<From + 6>());
    default:
      assert(idx == From + 7);
      return std::forward<F>(f)(static_int<From + 7>());
  }
}

// Fallback implementation when N > 8.
template <typename R, int From, int N, typename F>
inline R switch_impl(F&& f, static_int<From>, static_int<N>,  // NOLINT
                     int idx) {
  constexpr int A = N / 2;
  static_assert(A > 0, "");
  return (idx < From + A)
             ? switch_impl<R>(std::forward<F>(f), static_int<From>(),
                              static_int<A>(), idx)
             : switch_impl<R>(std::forward<F>(f), static_int<From + A>(),
                              static_int<N - A>(), idx);
}

}  // namespace internal_switch

// Evaluates to f(std::integral_constant<int, I>()) where I == idx.
//
// Requires: From <= idx < To (or behavior is otherwise undefined).
// Requires: decltype(f(std::integral_constant<int, I>())) is the same for
//           all I in [From, To).
template <int From, int To, typename F,
          typename R = internal_switch::ReturnType<F, From>>
R switch_index(F&& f, int idx) {  // NOLINT
  using internal_switch::static_int;
  static_assert(From < To, "The range must be a non-empty range.");
  static_assert(internal_switch::SameReturnTypes<R, F, From, To - From>(),
                "All calls to F() must have the same return type.");
  assert(idx >= From);
  assert(idx < To);
  return internal_switch::switch_impl<R>(std::forward<F>(f), static_int<From>(),
                                         static_int<To - From>(), idx);
}

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_SWITCH_H_
