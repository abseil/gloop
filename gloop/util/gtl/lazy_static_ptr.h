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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_LAZY_STATIC_PTR_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_LAZY_STATIC_PTR_H_

#include <tuple>
#include <utility>

#include "absl/base/call_once.h"
#include "absl/base/nullability.h"
#include "absl/utility/utility.h"

namespace gtl {

// LazyStaticPtr<Type, ...>:
// An on-demand constructed object of type Type that is intended to be
// a safe version of global/static object with non-trivial c-tor.
//
// The Type's c-tor is (thread-safely) called once on the first use
// of the LazyStaticPtr and gets passed all arguments (must have literal type)
// that were given at the definition of the LazyStaticPtr object, e.g.
//   static LazyStaticPtr<T> x;  // will call T()
//   static LazyStaticPtr<T, A1> y = { a1 };  // will call T(a1)
//   static LazyStaticPtr<T, A1, A2> z = { a1, a2 };  // will call T(a1, a2)
//   static LazyStaticPtr<T, A1, A2, A3> q = { a1, a2, a3 };
//     // will call T(a1, a2, a3)
//   static auto m = gtl::MakeLazyPtr<T>(a1, a2, a3, a4);
//     // will call T(a1, a2, a3, a4)
//
// Notes:
// * To initialize global instances of LazyStaticPtr use constant expressions
// only.
// * Forward declarations also work, e.g.
//     class Foo;
//     extern LazyStaticPtr<Foo, MyArgType> foo;
// * Arrays can be lazily constructed using std::array.
//     static LazyStaticPtr<std::array<T, 10>> x;
// * The actual object of type Type is stored on the heap.
// * The Type's d-tor is never called, so that threads that are still alive
//   at program's exit can work with the undestroyed object.
// * LazyStaticPtr<T, ...> behaves like T*,
//   LazyStaticPtr<const T, ...> behaves like const T*.
// * You can't delete the pointer value given out by a LazyStaticPtr.
// * You can't create a LazyStaticPtr object on stack or on the heap:
//   both cases will result in a memory leak when LazyStaticPtr is destroyed
//   without destroying or freeing the underlying object.
// * LazyStaticPtr is immutable: it can never be re-pointed somewhere else.
//   Declaring one to be const is therefore unnecessary but supported.
// * LazyStaticPtr is fast: after initial construction, each call
//   to access the underlying object takes no more than two or three cycles.
//
// Usage example:
//   // in file scope:
//   static LazyStaticPtr<const RE2, const char*> kMyRE = { "x*" };
//   // instead of unsafe and style-banned
//   static const RE2 kMyRE("x*");
//   // Then we do
//   if (RE2::FullMatch(a, *kMyRE)) ...
//   // instead of
//   if (RE2::FullMatch(a, kMyRE)) ...
//

// ========================================================================= //
// Public interface:

template <typename Type, typename... Args>
class LazyStaticPtr {
 public:
  using element_type = Type;  // per smart pointer convention

  // LazyStaticPtr makes possible constant initialization. If you are planning
  // to use LazyStaticPtr as a global value with a non-trivial initialization,
  // it's your responsibility to make sure that all requirements of constant
  // initialization are met (otherwise, the code will still compile but dynamic
  // initialization will be used instead which might lead to initialization
  // order fiasco). See
  // https://en.cppreference.com/w/cpp/language/constant_initialization for
  // details.
  // Note: disabling clang-tidy to ignore google-explicit-constructor. There is
  // a lot of code that uses LazyStaticPtr and had existed before the
  // constructor was introduced - marking it as explicit would break the old
  // code.
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr LazyStaticPtr(const Args&... args)  // NOLINT
      : ptr_{nullptr}, once_{}, args_{args...} {}

  // Not copyable or movable.
  LazyStaticPtr& operator=(const LazyStaticPtr&) = delete;
  LazyStaticPtr(const LazyStaticPtr&) = delete;
  LazyStaticPtr& operator=(LazyStaticPtr&&) = delete;
  LazyStaticPtr(LazyStaticPtr&&) = delete;

  // Pretend to be a pointer to Type (never returns a null pointer due to
  // on-demand creation):
  Type& operator*() const { return *get(); }
  Type* absl_nonnull operator->() const { return get(); }

  // Named accessor/initializer:
  Type* absl_nonnull get() const {
    absl::call_once(once_, [this]() { Initialize(this); });
    return ptr_;
  }

 private:
  // Using `Factory` method instead of Type's constructor. It's impossible to
  // take a pointer to constructor as it does not have a name, and thus,
  // constructor cannot be passed as a function (which is what we want to do in
  // `Initialize` method).
  static Type* absl_nonnull Factory(const Args&... args) {
    return new Type(args...);
  }

  static void Initialize(const LazyStaticPtr* lsp) {
    lsp->ptr_ = absl::apply(&Factory, lsp->args_);
  }

  // The object we create and show. We rely on once_ to assign it properly.
  mutable Type* ptr_;

  mutable absl::once_flag once_;

  // Arguments for Type's c-tor
  const std::tuple<Args...> args_;
};

// A convenience function to save you some typing. It deduces constructor
// argument types which means that you only need to provide the pointer type.
// Example usage:
//   constexpr static auto kPtr = gtl::MakeLazyPtr<string>("Hello, world!");
//   std::cout << *kPtr << std::endl;
// Note that in the example above the actual type of kPtr is
//   gtl::LazyStaticPtr<string, char const (&)[14]>
template <typename Type, typename... Args>
constexpr LazyStaticPtr<Type, Args...> MakeLazyPtr(Args&&... args) {
  return LazyStaticPtr<Type, Args...>(std::forward<Args>(args)...);
}

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_LAZY_STATIC_PTR_H_
