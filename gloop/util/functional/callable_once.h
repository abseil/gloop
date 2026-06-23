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

// This library provides functor adaptors CallAtMostOnce() and
// CallExactlyOnce(). Use them for binding move-only arguments and for enforcing
// contracts at runtime.
//
// =========== MOTIVATING EXAMPLE ===========
//
// Suppose we want to call the following function asynchronously.
//
//   void DoStuff(unique_ptr<State> state);
//
//   void ImplementMe() {
//     unique_ptr<State> state = MakeState();
//     // How to call DoStuff(std::move(state)) on the default executor?
//   }
//
// Attempt 1: use a lambda.
//
//   DefaultExecutor()->Schedule([state]() { DoStuff(std::move(state)); });
//
// Doesn't compile because 'state' isn't copyable and thus can't be captured by
// the lambda.
//
// Attempt 2: use std::bind().
//
//   DefaultExecutor()->Schedule(std::bind(DoStuff, std::move(state)));
//
// Doesn't compile because the functor created by std::bind can't be converted
// to std::function<void()>: it's neither copyable nor callable.
//
// Solution: use CallAtMostOnce().
//
//   DefaultExecutor()->Schedule(CallAtMostOnce(DoStuff, std::move(state)));
//
// This works. The functor created by CallAtMostOnce() stores the state and when
// invoked by the executor moves it into the argument of DoStuff().
//
// Naturally, if the functor were called for the second time, we would run into
// problems because the state has already been moved. As its name suggests,
// CallAtMostOnce() avoids this problem by allowing at most one call. The second
// call will abort the process.
//
// If you've ever used NewCallback(), it'll provide you with a good intuition
// into the semantics of CallAtMostOnce(). First, you bind some arguments with
// CallAtMostOnce(bound...) to produce a callable handle, akin to
// NewCallback(bound...). The handle is very cheap to copy regardless of the
// number and the types of the bound arguments. The handle can be called at most
// once (remember non-permanent callbacks?). The similarities end here. Unlike
// NewCallback(), CallAtMostOnce() produces a functor that can be converted to
// std::function. In addition, there are no resource leaks in the absence of
// invocations: the bound arguments are destroyed when the last copy of the
// handle is destroyed.
//
// CallExactlyOnce() differs from CallAtMostOnce() in just one aspect: when
// the last copy of a CallExactlyOnce() functor is destroyed, it'll CHECK-fail
// if the functor hasn't been called.
//
// =========== SPECIFICATION ===========
//
// CallAtMostOnce(bound...) returns a copyable functor that stores the passed
// arguments by shared_ptr. It's cheap to copy.
//
//   const auto f = CallAtMostOnce(DoStuff, std::move(state));
//   const auto g = f; // As cheap as copying one shared_ptr.
//
// It defines operator() const.
//
//   f();
//
// Since it can be called at most once, it's safe for it to apply std::move() to
// all bound arguments. That's why it can bind unique_ptr arguments, such as
// 'state' in the example above.
//
// The operator() can accept any number of arguments, which get
// perfect-forwarded to the target. In general,
// CallAtMostOnce(bound...)(args...) returns Invoke(Decay(bound)..., args...)
// where Decay(x) is static_cast<decay_t<decltype(x)>>(x).
//
//   int sum = CallAtMostOnce(plus<int>(), 3)(2);
//   assert(sum == Invoke(plus<int>(), 3, 2));  // a.k.a. 3 + 2, a.k.a. 5.
//
// If the functor or any of its copies is called for the second time, the
// process aborts.
//
//   auto f = CallAtMostOnce(...);
//   auto g = f;
//   f(); // OK
//   // Calling either f() or g() will CHECK-fail.
//
// Since CallAtMostOnce() is copyable and callable, it converts to
// std::function. The conversion is cheap. The call-at-most-once requirement
// still stands.
//
//   auto f = CallAtMostOnce(...);
//   std::function<void()> g = f;
//   std::function<void()> h = g;
//   f()  // Ok!
//   // Calling either f(), g() or h() will CHECK-fail.
//
// Caution: There is some subtlety with CallAtMostOnce(), std::function, and
// std::move which might produce surprising results:
//   std::unique_ptr<T> arg = ...;
//   std::function<void()> f = CallAtMostOnce(std::move(arg));
//   {
//     std::function<void()> g = std::move(f);
//     DoSomethingWhichMightNotInvoke(g);
//   }
// If f or g are never called, then the object of type T bound into
// CallAtMostOnce() may or may not be destroyed when g is destroyed, in spite of
// being moved. The lifetime of bound args might last until f is also destroyed.
// The C++ specification allows std::function (which is copyable) to implement
// move using a copy without clearing the state of the moved-from object (f
// remaining callable after a move is a valid implementation). As such the
// moved-from object may retain a reference count to the captured arguments. To
// compensate for this, it is advisable to explicitly set moved-from function
// objects containing CallAtMostOnce objects to nullptr after moving them to
// avoid surprising behaviour:
//   std::unique_ptr<T> arg = ...;
//   std::function<void()> f = CallAtMostOnce(std::move(arg));
//   {
//     std::function<void()> g = std::move(f);
//     f = nullptr;  // Ensures `arg` dies with `g`.
//     DoSomethingWhichMightNotInvoke(g);
//   }
//
// Everything written above about CallAtMostOnce() also applies to
// CallExactlyOnce(). In addition, it performs a runtime CHECK in the destructor
// that the functor or any of its copies has been called.
//
//   auto f = CallExactlyOnce(...);
//   {
//     auto g = f;
//     // The destructor of 'g' doesn't CHECK-fail because 'f' is
//     // still alive and might be called.
//   }
//   // If 'f' goes out of scope without being called or copied, the
//   // process aborts.

#ifndef THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_CALLABLE_ONCE_H_
#define THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_CALLABLE_ONCE_H_

#include <type_traits>

#include "absl/base/macros.h"
#include "absl/functional/bind_front.h"
#include "gloop/util/functional/callable_once_internal.h"  // IWYU pragma: export

namespace util {
namespace functional {

template <class Functor>
using CallAtMostOnceT =
    ::util::functional::internal::SharedCallWrapperAtMostOnce<
        ::util::functional::internal::CalledState<std::decay_t<Functor>>>;

template <typename Functor>
CallAtMostOnceT<Functor> CallAtMostOnce(Functor&& functor) {
  return CallAtMostOnceT<Functor>(0, std::forward<Functor>(functor));
}

template <class Functor>
using CallExactlyOnceT =
    ::util::functional::internal::SharedCallWrapperAtMostOnce<
        ::util::functional::internal::CheckCalledState<std::decay_t<Functor>>>;

template <class Functor>
CallExactlyOnceT<Functor> CallExactlyOnce(Functor&& functor) {
  return CallExactlyOnceT<Functor>(0, std::forward<Functor>(functor));
}

template <class... Args>
ABSL_DEPRECATE_AND_INLINE()
decltype(auto) CallExactlyOnce(Args&&... args) {
  return CallExactlyOnce(absl::bind_front(std::forward<Args>(args)...));
}

}  // namespace functional
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_CALLABLE_ONCE_H_
