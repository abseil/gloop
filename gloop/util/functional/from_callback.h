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

// FromCallback allows legacy Closure/Callback types used by older google3 APIs
// to be converted to std::function<> compatible objects.
//
// All callback types may be converted to std::functions with equivalent
// parameter/result types.
//
// For more background, see util/functional/to_callback.h
//
// Usage Notes
// -----------
// FromCallback returns a functor proxy capable of forwarding to any passed
// Callback-type.  The returned object should be considered a value-type which
// allows the underlying Callback's "Run(...)" method to be invoked via
// operator()(...); compatible with c++11's std::function.
//
// The returned functor behaves exactly as a pointer to the original Callback.
// Specifically, it:
//  (a) Is only valid as long as the underlying Callback object exists.
//  (b) Defines no destructor (the original Callback object continues to be
//      maintained on the heap).
//  (c) Performs only shallow copies.  (Copying the converted functor duplicates
//      only the underlying Callback pointer.  There is never any interaction
//      with pre-bound arguments.)
//
// With respect to (a), recall that temporary callbacks (i.e. those created by
// NewCallback) are always self-deleting in the case that Run(...) is called.
// Such callbacks may only ever be invoked a single time, whether by Run(...)
// directly or via a FromCallback() functor. Invoking Run() twice on a self-
// deleting callback, whether directly or via a FromCallback() wrapper, yields
// undefined behavior.
//
// Example 1:
// ----------
// Some valid Closure/Callback conversions, there are no restrictions on whether
// the underlying callback is temporary or permanent beyond that the underlying
// Callback must still exist for the functor to be invoked:
//   static void Nop1(int unused) {}
//   Closure* a = NewCallback(Nop1, 2);
//   Callback1<int>* b = NewCallback(Nop1);
//   Callback1<int>* c = NewPermanentCallback(Nop1);
//   Callback1<int>* d = NewCallback(Nop1);
//
//   std::function<void()> f_a = FromCallback(a);
//   std::function<void(int)> f_b = FromCallback(b);
//   std::function<void(int)> f_c = FromCallback(c);
//   std::function<void(int)> f_d = FromCallback(d);
//
//   f_a();   // Equivalent to a->Run()  [ i.e. Nop1(2) ]
//   f_b(3);  // Equivalent to b->Run(3) [ i.e. Nop1(3) ]
//   [ f_c() encapsulates a permanent callback, it may be called repeatedly. ]
//   f_c(4);  // Equivalent to c->Run(4) [ i.e. Nop1(4) ]
//   f_c(5);  // Equivalent to c->Run(5) [ i.e. Nop1(5) ]
//
//   [ Underlying permanent callback c must still be freed. ]
//   delete c;
//
//   [ As neither d->Run() [nor f_d()] were called, we must still free d. ]
//   delete d;
//
// Example 2:
// ----------
// ResultCallback types may also be converted:
//   static int Inc(int a) { return a + 1; }
//   ResultCallback1<int, int>* inc = NewCallback(Inc);
//   std::function<int(int)> f_inc = FromCallback(inc);
//   EXPECT_EQ(5, f_inc(3));  // Equivalent to inc->Run(3)
//
// Example 3:
// ----------
// Forwarding to function-compatible executor:
//   void LegacyInterfaceAcceptingClosure(Closure* c) {
//     thread_pool_.Schedule(FromCallback(c));
//   }
//
// Example 4:
// ----------
// Creating a functor that takes ownership of a callback:
// std::function<void()> nop1 = FromCallbackWithOwnership(NewCallback(Nop));
// std::function<void()> nop2 = FromCallbackWithOwnership(NewCallback(Nop));
// std::function<void()> nop3(nop2);
// std::function<void()> nop4 = FromCallbackWithOwnership(
//                                  NewPermanentCallback(Nop));
// nop1();
// nop4();
// Upon going of scope:
// ~nop1(): nop1 holds a self-deleting callback that has been deleted.
//          The destructor will not attempt to free the same pointer again.
// ~nop2(): nop2 and nop3 share ownership of a self-deleting callback that
// ~nop3(): has not been invoked. The callback will be deleted when both
//          nop2 and nop3 have been destroyed.
// ~nop4(): nop4 holds a permanent callback. It will be deleted when the last
//          copy of the functor returned by FromCallbackWithOwnership() has
//          been destroyed.

#ifndef THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_FROM_CALLBACK_H_
#define THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_FROM_CALLBACK_H_

#include <type_traits>
#include <utility>

#include "absl/base/macros.h"
#include "gloop/util/functional/from_callback_internal.h"  // IWYU pragma: export

namespace util {
namespace functional {

// Returns an std::function-compatible functor that invokes "callback->Run(...)"
// when called.  Does not take ownership of "callback".
//
// Example:
//   Callback2<int, float>* callback = ...;
//   std::function<void(int, float)> func = FromCallback(callback);
//   func(1, 3.14)  // Equivalent to callback->Run(1, 3.14).
template <typename CallbackType>
internal::CallbackFunctor<CallbackType> FromCallback(CallbackType* callback) {
  return internal::CallbackFunctor<CallbackType>(callback);
}

// A helper type for migration purposes to split caller and callee migration of
// current users of callback types. This type is implicitly convertible from raw
// callback pointers, meaning that changing a callee from ResultCallbackN<R, A>*
// to ResultCallbackFunctor<R, A> should be a no-op for many callers.
template <typename R, typename... Args>
using ResultCallbackFunctor = internal::ResultCallbackFunctorImpl<R, Args...>;

template <typename... Args>
using CallbackFunctor = ResultCallbackFunctor<void, Args...>;

// TODO: Remove
#if __cplusplus >= 202002L

// Overload for migration that returns the functor as-is. This can show up when
// migrating code to CallbackFunctor.
//
// This doesn't directly accept a ResultCallbackFunctor since the inliner
// doesn't work with parameter packs, see b/288454561.
template <typename F>
ABSL_DEPRECATE_AND_INLINE()
inline F FromCallback(F&& f)
  requires internal::IsResultCallbackFunctor<std::decay_t<F>>
{
  return std::forward<F>(f);
}

// Overload for migration that returns the functor as-is. This can show up when
// migrating code to CallbackFunctor.
//
// This doesn't directly accept a ResultCallbackFunctor since the inliner
// doesn't work with parameter packs, see b/288454561.
template <typename F>
  requires internal::IsResultCallbackFunctor<std::decay_t<F>>
ABSL_DEPRECATE_AND_INLINE()
F FromCallbackWithOwnership(F&& f) {
  return std::forward<F>(f);
}

#endif  // __cplusplus >= 202002L

}  // namespace functional
}  // namespace util

// TODO: Remove
#if __cplusplus >= 202002L

namespace absl {

// Overload for migration that returns the functor as-is. This can show up when
// migrating code to CallbackFunctor.
//
// This doesn't directly accept a ResultCallbackFunctor since the inliner
// doesn't work with parameter packs, see b/288454561.
//
// TODO: Remove this once <link> is complete.
template <typename F>
ABSL_DEPRECATE_AND_INLINE()
inline F WrapUnique(F&& f)
  requires ::util::functional::internal::IsResultCallbackFunctor<
      std::decay_t<F>>
{
  return std::forward<F>(f);
}

// Overload for migration that returns the functor as-is. This can show up when
// migrating code to CallbackFunctor.
//
// This doesn't directly accept a ResultCallbackFunctor since the inliner
// doesn't work with parameter packs, see b/288454561.
//
// TODO: Remove this once <link> is complete.
template <typename F>
ABSL_DEPRECATE_AND_INLINE()
inline F IgnoreLeak(F&& f)
  requires ::util::functional::internal::IsResultCallbackFunctor<
      std::decay_t<F>>
{
  return std::forward<F>(f);
}

}  // namespace absl

#endif  // __cplusplus >= 202002L

// Equivalent to "FromCallback", except that the resulting functor takes
// ownership of "callback".  The resultant functor maintains an internal
// reference count; when no copies remain (or possibly sooner, if it's a
// self-deleting callback), "callback" will be automatically deleted.
//
// Defined outside of the namespace to only be a definition, not a declaration.
template <typename CallbackType>
::util::functional::internal::OwningCallbackFunctor<CallbackType>
util::functional::FromCallbackWithOwnership(CallbackType* callback) {
  return internal::OwningCallbackFunctor<CallbackType>(callback);
}

#endif  // THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_FROM_CALLBACK_H_
