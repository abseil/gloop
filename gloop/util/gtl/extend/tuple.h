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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_TUPLE_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_TUPLE_H_

#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "gloop/util/gtl/extend/extend.h"
#include "gloop/util/gtl/internal/aggregate.h"

namespace gtl {

// TupleExtension
//
// A `gtl::Extend` extension that enables converting the struct to a
// `std::tuple` where each element of the tuple is a const reference to a field
// of the struct in order. I.e., this is equivalent to
// `std::tie(field1, field2, ..., fieldN)`.
//
// ```
// struct Person : gtl::Extend<Person>::With<TupleExtension> {
//   std::string name;
//   int age = 0;
// };
//
// Person p2{.name = "Mabel", .age = 13};
// const Person p1{.name = "Dan", .age = 13};
// std::tuple<const std::string&, const int&> p1_tup = p1.AsTuple();
// std::apply(foo, p2.AsTuple());
// ```
//
// However, it's usually more convenient to declare such variables with `auto`.
//
// Note: This is intended uniquely for extended *structs*.  Though this can be
// used with extended *classes*, this opens read access for every field to
// to the world invalidating any attempted encapsulation.
template <class T>
struct TupleExtension : Extension<TupleExtension, T> {
  constexpr auto AsTuple() const { return this->UnpackThis(); }
};

// MutableTupleExtension
//
// This is similar to `TupleExtension`, but adds `AsMutableTuple` which produces
// the same tuple, except with *mutable* references.
//
// ```
// struct Node : gtl::Extend<Node>::With<MutableTupleExtension> {
//   uint64_t id;
//   std::string data;
// };
//
// void SetIdFromData(uint64_t& id, const std::string& data);
// Node node {.id = 0, .data = "BaSE64"};
// std::apply(SetIdFromData, node.AsMutableTuple());
// id_to_data.insert(node.AsTuple());
// ```
//
// Note: As before, this exposes potentially hidden fields, and now even gives
// external code write access.  Using this with extended classes is *strongly*
// discouraged.
template <class T>
struct MutableTupleExtension : Extension<MutableTupleExtension, T> {
  using deps = void(TupleExtension<T>);

  auto constexpr AsMutableTuple() { return this->UnpackThis(); }

  // Assigns the fields of the struct pointwise from any tuple-like object.
  template <typename U>
      T& AssignTupleToFields(U&& u) & ABSL_ATTRIBUTE_LIFETIME_BOUND {
    AsMutableTuple() = std::forward<U>(u);
    return static_cast<T&>(*this);
  }
  // Same but for rvalue referenced objects.
  template <typename U>
      T&& AssignTupleToFields(U&& u) && ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return static_cast<T&&>(AssignTupleToFields(std::forward<U>(u)));
  }
};

namespace internal_extend {

template <typename T>
struct IsConstRefTuple : std::false_type {};
template <typename... Ts>
struct IsConstRefTuple<std::tuple<const Ts&...>> : std::true_type {};

template <typename T>
struct IsRefTuple : std::false_type {};
template <typename... Ts>
struct IsRefTuple<std::tuple<Ts&...>> : std::true_type {};

template <typename T>
decltype(std::declval<const T>().AsTuple()) HasAsTupleImpl(T*);
template <typename T>
void HasAsTupleImpl(...);

template <typename T>
decltype(std::declval<T>().AsMutableTuple()) HasAsMutableTupleImpl(T*);
template <typename T>
void HasAsMutableTupleImpl(...);

}  // namespace internal_extend

// Returns whether a type has an `.AsTuple()` that returns a `std::tuple` of
// const& types. This incidentally and importantly will always be true for types
// extended with `TupleExtension`.
template <class T>
inline constexpr bool HasAsTuple =
    internal_extend::Validate<T>() &&
    internal_extend::IsConstRefTuple<
        decltype(internal_extend::HasAsTupleImpl<T>(nullptr))>::value;

// Returns whether a type has an `.AsMutableTuple()` that returns a `std::tuple`
// of reference types. It also requires `HasAsTuple`. This incidentally and
// importantly will always be true for types extended with
// `MutableTupleExtension`.
template <class T>
inline constexpr bool HasAsMutableTuple =
    HasAsTuple<T> &&
    internal_extend::IsRefTuple<
        decltype(internal_extend::HasAsMutableTupleImpl<T>(nullptr))>::value;

// The type of tuple for a given struct which satisfies `HasAsTuple`, e.g. any
// type extended with `TupleExtension`.
template <class T>
using AsTupleType = decltype(std::declval<T>().AsTuple());

// The type of tuple for a given struct which satisfies `HasAsMutableTuple`,
// e.g. any type extended with `MutableTupleExtension`.
template <class T>
using AsMutableTupleType = decltype(std::declval<T>().AsMutableTuple());

// The type of tuple for a given struct with const and volatile qualifiers
// removed from the underlying type of each reference type within the tuple.
//
// One potential use case for this is to allow using structs as test case
// parameters for parameterized tests that use
// ::testing::Combine. See <link>.
template <class T>
using AsTupleOfRawTypes =
    typename internal_aggregate::RemoveQualifiersAndReferencesFromTuple<
        AsTupleType<T>>::type;

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_TUPLE_H_
