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

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_BINDINGS_TUPLIFY_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_BINDINGS_TUPLIFY_H_

#include <cstddef>
#include <tuple>
#include <type_traits>

#include "gloop/util/tuple/bindings/bindings.h"
#include "gloop/util/tuple/intrinsics.h"

// util::Tuple::Tuplify allows a bindable struct -- See
// https://en.cppreference.com/w/cpp/language/structured_binding#Case_3:_binding_to_data_members
// -- to act as a tuple for purposes of util/tuple algorithms, streamable, flume
// serialization etc. Think of it as quasi-transparent TUPLE_DEFINE_STRUCT.
//
// Advantages over TUPLE_DEFINE_STRUCT:
// - smaller interface, just define a type alias
//     using tuplify = util::tuple::Tuplify<N>;
//   inside the bindable struct.
// Cons:
// - Lack of named fields.
// - Restriction that the struct is bindable.
//
// Example:
//
//   struct Person {
//     string name;
//     int age = 0;
//     using tuplify = util::tuple::Tuplify<2>;
//     friend TUPLE_DEFINE_OP(Person, eq);
//     friend TUPLE_DEFINE_OP(Person, ne);
//     friend TUPLE_DEFINE_OP(Person, ostream);
//     friend TUPLE_DEFINE_OP(Person, absl_hash);
//   };
//
// This interface is experimental and subject to change. Probable directions are
// a macro TUPLE_DEFINE_OPS(Person, ...) which can define the operations more
// concisely, or the inclusion of the number of bindings in the type alias, like
// using tuplify = util::tuple::Tuplify<2>;

namespace util::tuple {

template <size_t N>
struct Tuplify {
  constexpr static size_t value = N;
};

namespace internal_tuplify {

template <size_t N, class T>
struct bindings_tag {};

template <class T>
struct IsTuplify : std::false_type {};

template <size_t N>
struct IsTuplify<Tuplify<N>> : std::true_type {};

}  // namespace internal_tuplify

template <class T>
struct tag<T, typename ::std::enable_if_t<
                  ::std::is_same_v<T, internal_intrinsics::remove_cvref_t<T>> &&
                  !internal_intrinsics::has_get_tuple_tag<T>::value &&
                  internal_tuplify::IsTuplify<typename T::tuplify>::value>> {
  using type = internal_tuplify::bindings_tag<T::tuplify::value, T>;
};

template <size_t M, class S>
struct intrinsics<internal_tuplify::bindings_tag<M, S>> {
  using has_all_elements = std::true_type;

  using bindings_t = bindings::bindings_traits_with_size<S, M>;
  static_assert(bindings_t::has_bindings);

  template <class... T>
  struct assemble {
    static_assert(
        ::std::is_same_v<::std::tuple<T...>, typename bindings_t::field_types>,
        "Type mismatch");
    typedef S type;
  };

  template <::size_t N, class T>
  struct element
      : ::std::tuple_element<N, typename bindings_t::field_types>::type {
    static_assert(::std::is_same_v<S, T>, "Type mismatch");
  };

  template <class T>
  struct size : bindings_t::num_bindings_t {};

  template <::size_t N, class T>
  static decltype(auto) get(T&& t) {
    static_assert(::std::is_same_v<S, typename ::std::decay<T>::type>,
                  "Type mismatch");
    return ::std::get<N>(bindings_t::field_refs(std::forward<T>(t)));
  }
};

}  // namespace util::tuple

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_BINDINGS_TUPLIFY_H_
