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

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_BINDINGS_BINDINGS_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_BINDINGS_BINDINGS_H_

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/meta/type_traits.h"
#include "absl/utility/utility.h"

namespace util {
namespace tuple {
namespace bindings {

#if defined(__clang__) && __cplusplus >= 201703L
#define UTIL_TUPLE_INTERNAL_BINDINGS_TRAITS 1
#else
#define UTIL_TUPLE_INTERNAL_BINDINGS_TRAITS 0
#endif

#if !UTIL_TUPLE_INTERNAL_BINDINGS_TRAITS

template <class T, size_t N>
struct bindings_traits_with_size {
  using num_bindings_t = std::integral_constant<size_t, 0>;
  static constexpr bool has_bindings = false;
  using field_types = std::tuple<>;
  template <class A>
  static field_types field_refs(A&& a) {
    static_assert(sizeof(A) == 0, "Should not be instantiated.");
    return {};
  }
  template <class A>
  static field_types fields(A&& a) {
    static_assert(sizeof(A) == 0, "Should not be instantiated.");
    return {};
  }
};

#else

namespace internal {

template <class From, class To>
struct copy_qualifiers {
  static_assert(
      std::is_same<absl::remove_cv_t<absl::remove_reference_t<From> >, From>());
  using type = To;
};

template <class From, class To>
struct copy_qualifiers<From&, To> {
  using type = typename copy_qualifiers<From, To>::type&;
};

template <class From, class To>
struct copy_qualifiers<From&&, To> {
  using type = typename copy_qualifiers<From, To>::type&&;
};

template <class From, class To>
struct copy_qualifiers<const From, To> {
  using type = const typename copy_qualifiers<From, To>::type;
};

template <class From, class To>
struct copy_qualifiers<volatile From, To> {
  using type = volatile typename copy_qualifiers<From, To>::type;
};

template <class From, class To>
struct copy_qualifiers<const volatile From, To> {
  using type = const volatile typename copy_qualifiers<From, To>::type;
};

template <class T, class R>
struct ref {
  using type = T;
  R&& r;
  constexpr R&& get() && { return std::forward<R>(r); }
};

template <class T>
T& remove_cv(T& val) {
  return val;
}

template <class T>
T& remove_cv(const T& val) {
  return const_cast<T&>(val);
}

template <class T>
T& remove_cv(volatile T& val) {
  return const_cast<T&>(val);
}

template <class T>
T& remove_cv(const volatile T& val) {
  return const_cast<T&>(val);
}

template <class Aggregate, class Field, class T>
auto make_ref(T& val) {
  using R = typename copy_qualifiers<Aggregate&&, Field>::type;
  return ref<Field, R>{static_cast<R>(val)};
}

template <class T>
auto get_fields_impl(T&& t, std::integral_constant<size_t, 0>) {
  return std::make_tuple();
}

template <class T>
auto get_fields_impl(T&& t, std::integral_constant<size_t, 1>) {
  auto&& [_1] = internal::remove_cv(std::forward<T>(t));
  return std::make_tuple(internal::make_ref<T, decltype(_1)>(_1));
}

template <class T>
auto get_fields_impl(T&& t, std::integral_constant<size_t, 2>) {
  auto&& [_1, _2] = internal::remove_cv(std::forward<T>(t));
  return std::make_tuple(internal::make_ref<T, decltype(_1)>(_1),
                         internal::make_ref<T, decltype(_2)>(_2));
}

template <class T>
auto get_fields_impl(T&& t, std::integral_constant<size_t, 3>) {
  auto&& [_1, _2, _3] = internal::remove_cv(std::forward<T>(t));
  return std::make_tuple(internal::make_ref<T, decltype(_1)>(_1),
                         internal::make_ref<T, decltype(_2)>(_2),
                         internal::make_ref<T, decltype(_3)>(_3));
}

template <class T>
auto get_fields_impl(T&& t, std::integral_constant<size_t, 4>) {
  auto&& [_1, _2, _3, _4] = internal::remove_cv(std::forward<T>(t));
  return std::make_tuple(internal::make_ref<T, decltype(_1)>(_1),
                         internal::make_ref<T, decltype(_2)>(_2),
                         internal::make_ref<T, decltype(_3)>(_3),
                         internal::make_ref<T, decltype(_4)>(_4));
}

template <class T>
auto get_fields_impl(T&& t, std::integral_constant<size_t, 5>) {
  auto&& [_1, _2, _3, _4, _5] = internal::remove_cv(std::forward<T>(t));
  return std::make_tuple(internal::make_ref<T, decltype(_1)>(_1),
                         internal::make_ref<T, decltype(_2)>(_2),
                         internal::make_ref<T, decltype(_3)>(_3),
                         internal::make_ref<T, decltype(_4)>(_4),
                         internal::make_ref<T, decltype(_5)>(_5));
}

template <class T>
auto get_fields_impl(T&& t, std::integral_constant<size_t, 6>) {
  auto&& [_1, _2, _3, _4, _5, _6] = internal::remove_cv(std::forward<T>(t));
  return std::make_tuple(internal::make_ref<T, decltype(_1)>(_1),
                         internal::make_ref<T, decltype(_2)>(_2),
                         internal::make_ref<T, decltype(_3)>(_3),
                         internal::make_ref<T, decltype(_4)>(_4),
                         internal::make_ref<T, decltype(_5)>(_5),
                         internal::make_ref<T, decltype(_6)>(_6));
}

template <class T>
auto get_fields_impl(T&& t, std::integral_constant<size_t, 7>) {
  auto&& [_1, _2, _3, _4, _5, _6, _7] = internal::remove_cv(std::forward<T>(t));
  return std::make_tuple(internal::make_ref<T, decltype(_1)>(_1),
                         internal::make_ref<T, decltype(_2)>(_2),
                         internal::make_ref<T, decltype(_3)>(_3),
                         internal::make_ref<T, decltype(_4)>(_4),
                         internal::make_ref<T, decltype(_5)>(_5),
                         internal::make_ref<T, decltype(_6)>(_6),
                         internal::make_ref<T, decltype(_7)>(_7));
}

template <class T>
auto get_fields_impl(T&& t, std::integral_constant<size_t, 8>) {
  auto&& [_1, _2, _3, _4, _5, _6, _7, _8] =
      internal::remove_cv(std::forward<T>(t));
  return std::make_tuple(internal::make_ref<T, decltype(_1)>(_1),
                         internal::make_ref<T, decltype(_2)>(_2),
                         internal::make_ref<T, decltype(_3)>(_3),
                         internal::make_ref<T, decltype(_4)>(_4),
                         internal::make_ref<T, decltype(_5)>(_5),
                         internal::make_ref<T, decltype(_6)>(_6),
                         internal::make_ref<T, decltype(_7)>(_7),
                         internal::make_ref<T, decltype(_8)>(_8));
}

template <class T>
auto get_fields_impl(T&& t, std::integral_constant<size_t, 9>) {
  auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9] =
      internal::remove_cv(std::forward<T>(t));
  return std::make_tuple(internal::make_ref<T, decltype(_1)>(_1),
                         internal::make_ref<T, decltype(_2)>(_2),
                         internal::make_ref<T, decltype(_3)>(_3),
                         internal::make_ref<T, decltype(_4)>(_4),
                         internal::make_ref<T, decltype(_5)>(_5),
                         internal::make_ref<T, decltype(_6)>(_6),
                         internal::make_ref<T, decltype(_7)>(_7),
                         internal::make_ref<T, decltype(_8)>(_8),
                         internal::make_ref<T, decltype(_9)>(_9));
}

template <class T>
auto get_fields_impl(T&& t, std::integral_constant<size_t, 10>) {
  auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10] =
      internal::remove_cv(std::forward<T>(t));
  return std::make_tuple(internal::make_ref<T, decltype(_1)>(_1),
                         internal::make_ref<T, decltype(_2)>(_2),
                         internal::make_ref<T, decltype(_3)>(_3),
                         internal::make_ref<T, decltype(_4)>(_4),
                         internal::make_ref<T, decltype(_5)>(_5),
                         internal::make_ref<T, decltype(_6)>(_6),
                         internal::make_ref<T, decltype(_7)>(_7),
                         internal::make_ref<T, decltype(_8)>(_8),
                         internal::make_ref<T, decltype(_9)>(_9),
                         internal::make_ref<T, decltype(_10)>(_10));
}

template <class T>
auto get_fields_impl(T&& t, std::integral_constant<size_t, 11>) {
  auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11] =
      internal::remove_cv(std::forward<T>(t));
  return std::make_tuple(internal::make_ref<T, decltype(_1)>(_1),
                         internal::make_ref<T, decltype(_2)>(_2),
                         internal::make_ref<T, decltype(_3)>(_3),
                         internal::make_ref<T, decltype(_4)>(_4),
                         internal::make_ref<T, decltype(_5)>(_5),
                         internal::make_ref<T, decltype(_6)>(_6),
                         internal::make_ref<T, decltype(_7)>(_7),
                         internal::make_ref<T, decltype(_8)>(_8),
                         internal::make_ref<T, decltype(_9)>(_9),
                         internal::make_ref<T, decltype(_10)>(_10),
                         internal::make_ref<T, decltype(_11)>(_11));
}

template <class T>
auto get_fields_impl(T&& t, std::integral_constant<size_t, 12>) {
  auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12] =
      internal::remove_cv(std::forward<T>(t));
  return std::make_tuple(internal::make_ref<T, decltype(_1)>(_1),
                         internal::make_ref<T, decltype(_2)>(_2),
                         internal::make_ref<T, decltype(_3)>(_3),
                         internal::make_ref<T, decltype(_4)>(_4),
                         internal::make_ref<T, decltype(_5)>(_5),
                         internal::make_ref<T, decltype(_6)>(_6),
                         internal::make_ref<T, decltype(_7)>(_7),
                         internal::make_ref<T, decltype(_8)>(_8),
                         internal::make_ref<T, decltype(_9)>(_9),
                         internal::make_ref<T, decltype(_10)>(_10),
                         internal::make_ref<T, decltype(_11)>(_11),
                         internal::make_ref<T, decltype(_12)>(_12));
}

template <class T>
auto get_fields_impl(T&& t, std::integral_constant<size_t, 13>) {
  auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13] =
      internal::remove_cv(std::forward<T>(t));
  return std::make_tuple(internal::make_ref<T, decltype(_1)>(_1),
                         internal::make_ref<T, decltype(_2)>(_2),
                         internal::make_ref<T, decltype(_3)>(_3),
                         internal::make_ref<T, decltype(_4)>(_4),
                         internal::make_ref<T, decltype(_5)>(_5),
                         internal::make_ref<T, decltype(_6)>(_6),
                         internal::make_ref<T, decltype(_7)>(_7),
                         internal::make_ref<T, decltype(_8)>(_8),
                         internal::make_ref<T, decltype(_9)>(_9),
                         internal::make_ref<T, decltype(_10)>(_10),
                         internal::make_ref<T, decltype(_11)>(_11),
                         internal::make_ref<T, decltype(_12)>(_12),
                         internal::make_ref<T, decltype(_13)>(_13));
}

template <class T>
auto get_fields_impl(T&& t, std::integral_constant<size_t, 14>) {
  auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14] =
      internal::remove_cv(std::forward<T>(t));
  return std::make_tuple(internal::make_ref<T, decltype(_1)>(_1),
                         internal::make_ref<T, decltype(_2)>(_2),
                         internal::make_ref<T, decltype(_3)>(_3),
                         internal::make_ref<T, decltype(_4)>(_4),
                         internal::make_ref<T, decltype(_5)>(_5),
                         internal::make_ref<T, decltype(_6)>(_6),
                         internal::make_ref<T, decltype(_7)>(_7),
                         internal::make_ref<T, decltype(_8)>(_8),
                         internal::make_ref<T, decltype(_9)>(_9),
                         internal::make_ref<T, decltype(_10)>(_10),
                         internal::make_ref<T, decltype(_11)>(_11),
                         internal::make_ref<T, decltype(_12)>(_12),
                         internal::make_ref<T, decltype(_13)>(_13),
                         internal::make_ref<T, decltype(_14)>(_14));
}

template <class T>
auto get_fields_impl(T&& t, std::integral_constant<size_t, 15>) {
  auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15] =
      internal::remove_cv(std::forward<T>(t));
  return std::make_tuple(internal::make_ref<T, decltype(_1)>(_1),
                         internal::make_ref<T, decltype(_2)>(_2),
                         internal::make_ref<T, decltype(_3)>(_3),
                         internal::make_ref<T, decltype(_4)>(_4),
                         internal::make_ref<T, decltype(_5)>(_5),
                         internal::make_ref<T, decltype(_6)>(_6),
                         internal::make_ref<T, decltype(_7)>(_7),
                         internal::make_ref<T, decltype(_8)>(_8),
                         internal::make_ref<T, decltype(_9)>(_9),
                         internal::make_ref<T, decltype(_10)>(_10),
                         internal::make_ref<T, decltype(_11)>(_11),
                         internal::make_ref<T, decltype(_12)>(_12),
                         internal::make_ref<T, decltype(_13)>(_13),
                         internal::make_ref<T, decltype(_14)>(_14),
                         internal::make_ref<T, decltype(_15)>(_15));
}

template <class T>
auto get_fields_impl(T&& t, std::integral_constant<size_t, 16>) {
  auto&& [_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15,
          _16] = internal::remove_cv(std::forward<T>(t));
  return std::make_tuple(internal::make_ref<T, decltype(_1)>(_1),
                         internal::make_ref<T, decltype(_2)>(_2),
                         internal::make_ref<T, decltype(_3)>(_3),
                         internal::make_ref<T, decltype(_4)>(_4),
                         internal::make_ref<T, decltype(_5)>(_5),
                         internal::make_ref<T, decltype(_6)>(_6),
                         internal::make_ref<T, decltype(_7)>(_7),
                         internal::make_ref<T, decltype(_8)>(_8),
                         internal::make_ref<T, decltype(_9)>(_9),
                         internal::make_ref<T, decltype(_10)>(_10),
                         internal::make_ref<T, decltype(_11)>(_11),
                         internal::make_ref<T, decltype(_12)>(_12),
                         internal::make_ref<T, decltype(_13)>(_13),
                         internal::make_ref<T, decltype(_14)>(_14),
                         internal::make_ref<T, decltype(_15)>(_15),
                         internal::make_ref<T, decltype(_16)>(_16));
}

template <class T, size_t N>
auto get_fields_impl(T&&, std::integral_constant<size_t, N>) {
  return std::make_tuple();
}

template <class... Ts, size_t... Is>
auto unpack(std::tuple<Ts...>&& t, absl::index_sequence<Is...>) {
  return std::forward_as_tuple(std::get<Is>(std::move(t)).get()...);
}

template <class A, class I,
          class T = decltype(internal::get_fields_impl(std::declval<A>(),
                                                       std::declval<I>()))>
struct field_types_impl;

template <class A, size_t N, class... Ts>
struct field_types_impl<A, std::integral_constant<size_t, N>,
                        std::tuple<Ts...> > {
  static_assert(
      std::is_same<absl::remove_cv_t<absl::remove_reference_t<A> >, A>());
  using type = std::tuple<typename Ts::type...>;
};

}  // namespace internal

template <class T, size_t N>
struct bindings_traits_with_size {
  using Tr = absl::remove_cv_t<absl::remove_reference_t<T> >;
  using num_bindings_t = std::integral_constant<size_t, N>;
  static constexpr bool has_bindings =
      (num_bindings_t::value > 0 && num_bindings_t::value < 16) ||
      std::is_empty<Tr>::value;
  using field_types =
      typename internal::field_types_impl<Tr, num_bindings_t>::type;
  template <class A>
  static auto field_refs(A&& a) {
    auto fs = internal::get_fields_impl(std::forward<A>(a), num_bindings_t{});
    return internal::unpack(std::move(fs),
                            absl::make_index_sequence<num_bindings_t::value>{});
  }
  template <class A>
  static field_types fields(A&& a) {
    return field_refs(std::forward<A>(a));
  }
};

#endif  // UTIL_TUPLE_INTERNAL_BINDINGS_TRAITS

}  // namespace bindings
}  // namespace tuple
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_BINDINGS_BINDINGS_H_
