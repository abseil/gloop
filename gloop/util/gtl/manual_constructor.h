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

// ManualConstructor statically-allocates space in which to store some
// object, but does not initialize it.  You can then call the constructor
// and destructor for the object yourself as you see fit.  This is useful
// for memory management optimizations, where you want to initialize and
// destroy an object multiple times but only allocate it once.
//
// (When I say ManualConstructor statically allocates space, I mean that
// the ManualConstructor object itself is forced to be the right size.)
//
// For example usage, check out util/gtl/small_map.h.

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_MANUAL_CONSTRUCTOR_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_MANUAL_CONSTRUCTOR_H_

#include <stddef.h>

#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace gtl {

template <typename Type>
class ManualConstructor {
  // WARNING: ManualConstructor<T> is trivially copyable, allowing it to be
  // byte-copied (e.g. via memcpy or implicit class copy), even if T is
  // non-copyable or non-movable. Such copies bypass T's constructors and
  // assignment operators. If T is not trivially copyable, byte-copying
  // ManualConstructor<T> is UNDEFINED BEHAVIOR (e.g. double-free).
  // If T is not trivially copyable, users must make containing types
  // non-copyable/movable or implement copy/move to use Init/Destroy.
  using Payload = std::conditional_t<std::is_reference_v<Type>,
                                     std::remove_reference_t<Type>*, Type>;

 public:
  // No constructor or destructor because one of the most useful uses of
  // this class is as part of a union, and members of a union could not have
  // constructors or destructors till C++11.  And, anyway, the whole point of
  // this class is to bypass constructor and destructor.

  constexpr auto* get() {
    auto&& ref = **this;
    return std::addressof(ref);
  }
  constexpr auto* get() const {
    auto&& ref = **this;
    return std::addressof(ref);
  }

  constexpr auto* operator->() { return get(); }
  constexpr auto* operator->() const { return get(); }

  constexpr std::conditional_t<std::is_reference_v<Type>, Type, Type&>
  operator*() {
    return const_cast<
        std::conditional_t<std::is_reference_v<Type>, Type, Type&>>(
        *std::as_const(*this));
  }

  constexpr std::conditional_t<std::is_reference_v<Type>, Type, const Type&>
  operator*() const {
    const Payload& wrapped = *GetPayload();
    if constexpr (std::is_reference_v<Type>) {
      return static_cast<Type>(*wrapped);
    } else {
      return wrapped;
    }
  }

  constexpr void Init() { DefaultInit(); }

  // Init() constructs the Type instance using the given arguments
  // (which are forwarded to Type's constructor).
  //
  // WARNING: invoking Init() with no arguments does *not* select this overload.
  // It instead calls overload performing default-initialization.
  // (i.e. it will behave like "new Type;", and not "new Type();").
  // Therefore, it will leave non-class types uninitialized.
  //
  // Rvalue references and std::forward are used by arbiter permission;
  // see cl/62366994.
  template <typename... Ts>
  constexpr void Init(Ts&&... args) {
    if constexpr (std::is_reference_v<Type>) {
      Type r(std::forward<Ts>(args)...);
      new (&space_) Payload(std::addressof(r));
    } else {
      new (&space_) Payload(std::forward<Ts>(args)...);
    }
  }

  // DefaultInit() constructs the Type instance using default-initialization
  // (i.e it behaves the same as "new Type;", not "new Type();"), so it will
  // leave trivial types uninitialized.
  constexpr void DefaultInit() { new (&space_) Payload; }

  // ValueInit() constructs the Type instance using value-initialization (i.e.
  // it behaves the same as "new Type();", not "new Type;"), so it will
  // zero-initialize trivial types.
  constexpr void ValueInit() { this->Init<>(); }

  // Init() that is equivalent to copy and move construction.
  // Enables usage like this:
  //   ManualConstructor<std::vector<int>> v;
  //   v.Init({1, 2, 3});
  constexpr void Init(const Type& x)
    requires(!std::is_reference_v<Type>)
  {
    this->template Init<const Type&>(x);
  }
  constexpr void Init(Type&& x) {
    this->template Init<Type>(std::forward<Type>(x));
  }

  // Constructs the Type instance by calling the functor.
  template <typename F, typename... ExtraArgs>
  constexpr void InitViaCallable(F&& functor, ExtraArgs&&... extra_args) {
    if constexpr (std::is_reference_v<Type>) {
      Type r(std::forward<F>(functor)(std::forward<ExtraArgs>(extra_args)...));
      new (&space_) Payload(std::addressof(r));
    } else {
      new (&space_) Payload(
          std::forward<F>(functor)(std::forward<ExtraArgs>(extra_args)...));
    }
  }

  constexpr void Destroy() { GetPayload()->~Payload(); }

 private:
  constexpr const Payload* GetPayload() const {
    return std::launder(reinterpret_cast<const Payload*>(&space_));
  }

  alignas(Payload) char space_[sizeof(Payload)];
};

}  // namespace gtl

// Old names for <link>:
using ::gtl::ManualConstructor;  // NOLINT

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_MANUAL_CONSTRUCTOR_H_
