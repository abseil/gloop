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

// `gtl::Extend` allows you to extend your type with functionality such as
// equality, stringification, hashing, etc.
//
// ## Using `gtl::Extend` with structs:
//
// To use `gtl::Extend`, write a struct that inherits from
// `gtl::Extend<MyStruct>::With<MyExtensions...>` following this example:
//
// struct Person :
//     gtl::Extend<Person>::With<gtl::EqualityExtension> {
//   std::string name;
//   int age;
// };
//
// `gtl::Extend` supports almost any aggregate type, so long as it
// * only inherits from gtl::Extend<>::With<> (multiple inheritance is not
//   supported)
// * has no C-array members. If an array member is necessary, use a std::array.
//
// However, it may not be able to automatically count the number of fields in
// the struct if the struct contains (mutable) l-value references or bit-fields.
// In such cases, you may need to specify the number of fields explicitly, as
// with classes (below).
//
// Notice that the template argument for `gtl::Extend<>` is the struct
// itself. (This is known as the curiously recurring template pattern. If
// you're interested in learning more:
// https://en.wikipedia.org/wiki/Curiously_recurring_template_pattern).
//
// The `Person` struct will automatically provide equality, inequality, and
// Abseil hashing support, because it uses the EqualityExtension.
//
// A partial list of available extensions:
// * EqualityExtension - provides element-wise operator==, operator!=, and
//   absl::Hash support
// * OrderingExtension - provides element-wise operator<, operator<=, etc.
//   Implies EqualityExtension.
// * DebugPrintingExtension - provides debug printing compatible with
//   <link> and operator<<
//
// Multiple Extensions
//
// Multiple extensions can be enabled by listing each in the `With<>` block.
// Example usage:
//
// ```
// struct Person : gtl::Extend<Person>::With<gtl::EqualityExtension,
//                                           gtl::DebugPrintingExtension> {
//   std::string name;
//   int age;
// };
// ```
//
// Also see https://abseil.io/tips/205
//
// ## Using `gtl::Extend` with classes:
//
// `gtl::Extend` can also be used with classes. Usage is largely
// the same with two notable changes:
// * You must explicitly specify the number of fields in your class. It is not
//   possible for this number to be accidentally incorrect; if the value does
//   not match the number of fields, your code will not compile.
// * You must add `friend gtl::EnableExtensions;` to your class.
//
// Example usage:
// ```
// class Person : public gtl::Extend<Person, 2>::With<gtl::EqualityExtension> {
//  public:
//   // ...
//
//  private:
//   friend gtl::EnableExtensions;
//
//   std::string name_;
//   int age_;
// };
// ```
//
#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_EXTEND_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_EXTEND_H_

#include <stddef.h>

#include <type_traits>

#include "gloop/util/gtl/internal/dependencies.h"
// TODO: Remove once all `Extension`s are migrated to new header.
#include "gloop/util/gtl/extend/extension.h"  // IWYU pragma: export
#include "gloop/util/gtl/extend/internal/extend.h"

namespace gtl {

// Add `friend gtl::EnableExtensions` to non-aggregate structs and classes
// with private fields when extending them.
using EnableExtensions = ::gtl::internal_extend::EnableExtensions;

// Returns the number of fields in the extendable struct, provided that the
// number was deduced. If the number could not be deduced `FieldCount` will not
// be available. (This can occur with bit fields or mutable l-value references,
// as well as with private fields.) This avoids accidentally exposing the number
// of fields in cases where a type author may not have intended to expose this
// detail (such as with private fields). In other words, `FieldCount` is only
// available in cases where there are already other public mechanisms for
// detecting the number of fields.
template <typename T>
constexpr std::enable_if_t<
    ::gtl::internal_extend::ExtractFieldCountTemplateParameter(
        static_cast<T*>(nullptr)) == -1,
    size_t>
FieldCount() {
  return ::gtl::internal_extend::FieldCount(static_cast<T*>(nullptr));
}

template <typename T, int FieldCount = -1>
struct Extend final {
  template <template <typename> typename... Tags>
  struct With
      : internal_extend::ExtensionSet<
            T, FieldCount, internal_dependencies::Dependencies<Tags<T>...>> {};
};

// Constructs an extendable type T from the provided arguments, each argument
// used to initialize each field.  This is equivalent to a pointwise constructor
// as if T were not an extendable class.
template <typename T, typename... VarT>
constexpr std::enable_if_t<
    std::is_base_of_v<gtl::internal_extend::ExtendableTypedStruct<T>, T>, T>
ConstructExtend(VarT&&... vars) {
  if constexpr (std::is_constructible_v<T, VarT...>) {
    return T(std::forward<VarT>(vars)...);
  } else {
    return T{{}, std::forward<VarT>(vars)...};
  }
}

// `MakeFromTuple<T>` constructs a `T` from the tuple of field-types provided.
// The type `T` must be an extendable struct or class.
// If `T` is a class, it must be constructible from the tuple of field types.
// field_tuple can be any type that supports std::get and std::tuple_size.
template <typename T, typename TupleT>
constexpr std::enable_if_t<
    std::is_base_of_v<gtl::internal_extend::ExtendableTypedStruct<T>, T>, T>
MakeFromTuple(TupleT&& field_tuple) {
  return std::apply(
      [](auto&&... fields) {
        return ConstructExtend<T>(std::forward<decltype(fields)>(fields)...);
      },
      std::forward<TupleT>(field_tuple));
}

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_EXTEND_H_
