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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_INTERNAL_EXTEND_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_INTERNAL_EXTEND_H_

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include "gloop/util/gtl/extend/extension.h"  // IWYU pragma: keep
#include "gloop/util/gtl/internal/aggregate.h"

namespace gtl::internal_extend {

using EnableExtensions = ::gtl::internal_aggregate::FieldGetter;

template <typename>
static constexpr bool AlwaysFalse() {
  return false;
}

// Call the extension's CheckDependentExpressionValidity() function if it's
// provided (in order to have compilation fail early).
template <class T>
constexpr auto ForceInstantiateExtensionDependentExpressions(T* t)
    -> decltype(t->ForceInstantiateDependentExpressions()) {
  return t->ForceInstantiateDependentExpressions();
}
constexpr auto ForceInstantiateExtensionDependentExpressions(void*) {}

// An extension set needs to know the derived type `T` which inherits from
// gtl::Extend<>::With<>, because calls to the `Unpack()` method
// internally inspect the type being unpacked to determine the return type.
template <typename T, int NumFields, typename Deps>
struct ExtensionSet;

// `ExtendableTypedStruct` is an empty struct that allows us to determine
// whether a struct is an extendable struct.
template <typename T>
struct ExtendableTypedStruct {};

template <typename T>
std::enable_if_t<!std::is_base_of_v<ExtendableTypedStruct<T>, T>, T>
GetExtendableBase(T* p);

template <typename T>
T GetExtendableBase(internal_extend::ExtendableTypedStruct<T>* p);

template <typename T>
constexpr bool Validate() {
  using Base = decltype(GetExtendableBase(static_cast<T*>(nullptr)));
  static_assert(
      std::is_same_v<Base, std::decay_t<T>> || std::is_base_of_v<Base, T>,
      "T must inherit from gtl::Extend<T>::With<...>. "
      "Perhaps there is a copy-paste error in the base class list?");
  return true;
}

template <typename T>
constexpr size_t FieldCount(T* t) {
  return std::tuple_size_v<decltype(t->Unpack())>;
}

template <typename T, int NumFields, typename... Extensions>
struct ExtensionSet<T, NumFields, void (*)(Extensions...)>
    : ExtendableTypedStruct<T>, Extensions... {
  constexpr ExtensionSet() : ExtendableTypedStruct<T>{}, Extensions()... {
    // Force all `Extensions` to instantiate their constraint checking extension
    // point, so we fail to compile early if requirements are unmet.
    if (false) {  // NOLINT
      (ForceInstantiateExtensionDependentExpressions(
           static_cast<Extensions*>(this)),
       ...);
    }
  }

 private:
  // Note: In both `Unpack()` overloads, we call internal_aggregate::Unpack<1>.
  // The integer template parameter is the number of base classes to assume `T`
  // has. Because `gtl::Extend` requires a base class and does not support
  // multiple inheritance, we hard-code this value to 1.
  constexpr auto Unpack() const {
    auto result =
        ::gtl::internal_extend::EnableExtensions::Unpack<1, NumFields>(
            static_cast<const T&>(*this));
    if constexpr (std::is_same_v<
                      decltype(result),
                      ::gtl::internal_extend::EnableExtensions::Error>) {
      constexpr bool kFailedToCountFields =
          ::gtl::internal_extend::AlwaysFalse<T>();
      // This line as written in source is shown as part of the error message.
      // Clang-Format wants to put `kFailedToCountFields` on a separate line,
      // but that will mean that it is not shown as part of the error message,
      // leaving a confusing stray "static_assert(". To ensure we see we
      // "static_assert(kFailedToCountFields,", we turn of clang-format for this
      // one line.
      // clang-format off
      static_assert(kFailedToCountFields,
          // clang-format on
          R"(Could not detect the number of fields. If you're using a class with private fields or have defined/defaulted/deleted constructors, you must specify the number of fields as a second parameter template to `gtl::Extend`. For example:
    ```
    class MyType : public gtl::Extend<MyType, FIELD_COUNT_GOES_HERE>::With<...> {
     public:
      ...
     private:
      friend gtl::EnableExtensions;  // Don't forget this too!
     ...
    };
    ```
See <link> for a worked example.
    )");
      // Because we already have a `static_assert`, what we return here has no
      // effect. We simply want to choose a type that provides the best possible
      // error messages. We choose to return an empty `std::tuple`, because we
      // expect that to work with nearly every extension.
      return std::tuple();
    } else {
      return result;
    }
  }
  constexpr auto Unpack() {
    return ::gtl::internal_extend::EnableExtensions::Unpack<1, NumFields>(
        static_cast<T&>(*this));
  }

  template <template <typename> typename, typename>
  friend class ::gtl::Extension;

  friend constexpr size_t FieldCount<T>(T*);
};

// Extracts the field count template parameter from `ExtensionSet`.
template <typename T, int FieldCount, typename... Deps>
constexpr size_t ExtractFieldCountTemplateParameter(
    ExtensionSet<T, FieldCount, Deps...>*) {
  return FieldCount;
}

// Test that all fields of `std::tuple` `TupleT` satisfy type predicate `PredT`.
template <template <typename> typename PredT, typename TupleT>
bool AllSatisfyTuple;
template <template <typename> typename PredT, typename... ArgT>
constexpr bool AllSatisfyTuple<PredT, std::tuple<ArgT...>> =
    std::conjunction<PredT<ArgT>...>::value;

// Access the type of the tuple of an extendable type of `ExtensionT`.
//
// This is, to enable `AllFieldsSatisfy` and `AllConstFieldsSatisfy` for your
// extension, add:
// ```
// template <typename T>
// struct MyExtension : gtl::Extension<MyExtendion, T> {
//  private:
//   friend FieldsSatisfyKey<MyExtension>;
//   using gtl::Extension<MyExtension, T>::Unpack;
// };
// ```
template <template <typename> typename ExtT, typename T>
struct FieldsSatisfyKey {
  using TupleType = decltype(std::declval<T&>().ExtT<T>::UnpackThis());
  using ConstTupleType =
      decltype(std::declval<const T&>().ExtT<T>::UnpackThis());
};

// Requires all fields (as references) satisfy `CondT`.  See `AllSatisfy`.
template <template <typename> typename PredT, template <typename> typename ExtT,
          typename T>
constexpr bool AllFieldsSatisfy =
    AllSatisfyTuple<PredT, typename FieldsSatisfyKey<ExtT, T>::TupleType>;

// Requires all fields (as const&) satisfy `CondT`.  See `AllSatisfy`.
template <template <typename> typename PredT, template <typename> typename ExtT,
          typename T>
constexpr bool AllConstFieldsSatisfy =
    AllSatisfyTuple<PredT, typename FieldsSatisfyKey<ExtT, T>::ConstTupleType>;

}  // namespace gtl::internal_extend

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_EXTEND_INTERNAL_EXTEND_H_
