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

// ToCallback/ToPermanentCallback() allow a std::function<> object to
// be converted to Closure/Callback types accepted by older google3
// APIs (like Executor).  In fact any functor, i.e., a pure function
// or an object that has an "operator()", can be converted to Closure/Callback.
//
// Background: C++11 provides a std::function<Result(Args...)> type
// that stands for a typed piece of code that can be invoked.  This is
// similar to the older google3 functionality of Closure/Callback in
// base/callback.h.  Many google3 threading and RPC interfaces rely on
// Closure and Callback.  We plan to transition those interfaces over
// time to instead accept std::function<>.  However this will take
// some time and both types of interfaces will exist concurrently
// during that time.  This module aids inter-operability by allowing
// the conversion of std::function<> (or any functor) to heap-allocated
// Closure/Callback objects.
//
// Some valid conversions to Closure/Callback types:
//   void Nop() {}
//   void Nop1(int unused) {}
//   int Sum(int a, int b) { return a + b; }
//   struct M { void member_nop() {} };
//
//   // Function pointers are directly convertible.
//   Closure* a = ToCallback(Nop);
//   Callback1<int>* b = ToCallback(Nop1);
//
//   // std::bind results are also convertible.
//   Closure* c = ToCallback(std::bind(Nop));
//
//   // Use absl::bind_front to curry (pre-bind) arguments.
//   Closure* d = ToCallback(absl::bind_front(Nop1, 42));
//
//   // Likewise to associate the instance for member-functions.
//   M m;
//   Closure* d = ToCallback(absl::bind_front(&M::member_nop, m));
//
//   // ResultCallback types are fully supported.
//   ResultCallback1<int, int>* g = ToCallback(std::bind(Sum, 5, _1));
//
//   // As are explicit functor objects.
//   struct F { int operator()() { return 1; } };
//   ResultCallback<int>* i = ToCallback(F());
//
//   // As are lambdas.
//   Callback1<std::string>* j =
//       ToCallback([](std::string x) { LOG(INFO) << x; });
//
//   // Empty std::functions and null function pointers are converted to null.
//   std::function<void()> f;
//   Closure* c = ToCallback(f);
//   CHECK_EQ(c, nullptr);

#ifndef THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_TO_CALLBACK_H_
#define THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_TO_CALLBACK_H_

#include <type_traits>
#include <utility>

#include "gloop/base/callback-types.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/trace_source_location.h"
#include "gloop/util/functional/to_callback_internal.h"  // IWYU pragma: export

namespace util {
namespace functional {

// Returns a Closure/Callback object that calls "functor(...)" whenever
// its Run() method is called. The result is suitable for passing to
// any API that expects a NewCallback() result. In particular, the
// result:
//   (a) Stores a copy of the current base::Context (see base/context.h)
//   (b) Calls "functor(...)" under the stored base::Context when
//       the result's Run method is called.
//   (c) Deletes itself after the Run method returns.
//
// Example:
//   Closure* c = ToCallback([] { sleep(1); });
template <typename Functor>
internal::ToCallbackResult<Functor> ToCallback(
    Functor&& functor, perftools::tracing::StringLabel label =
                           perftools::tracing::TraceSourceLocation::current()) {
  return internal::ToCallbackResult<Functor>(std::forward<Functor>(functor),
                                             std::move(label));
}

// Returns a Closure/Callback object that calls "functor(..)" whenever
// its Run() method is called.  The result is suitable for passing to
// any API that expects a NewPermanentCallback().
//
// Note that unlike ToCallback(), the returned object does not delete
// itself after being run, and does not capture the current base::Context.
//
// Example:
//   Closure* c = ToPermanentCallback([] { sleep(1); });
template <typename Functor>
internal::ToPermanentCallbackResult<Functor> ToPermanentCallback(
    Functor&& functor) {
  return internal::ToPermanentCallbackResult<Functor>(
      std::forward<Functor>(functor));
}

// Variant of ToCallback that allows the specification of the
// exact callback type (useful when the type cannot be inferred).
//
// E.g. std::unique_ptr<Closure> closure(ToCallback<Closure>(...));
template <typename CallbackType, typename Functor,
          typename Enabler = std::enable_if_t<
              std::is_base_of_v<::base::internal::CallbackBase, CallbackType>>>
CallbackType* ToCallback(
    Functor&& functor, perftools::tracing::StringLabel label =
                           perftools::tracing::TraceSourceLocation::current()) {
  return internal::ToCallbackResult<Functor>(std::forward<Functor>(functor),
                                             std::move(label));
}

// Variant of ToPermanentCallback that allows the specification of the
// exact callback type (useful when the type cannot be inferred).
//
// E.g. std::unique_ptr<Closure> closure(ToPermanentCallback<Closure>(...));
template <typename CallbackType, typename Functor,
          typename Enabler = std::enable_if_t<
              std::is_base_of_v<::base::internal::CallbackBase, CallbackType>>>
CallbackType* ToPermanentCallback(Functor&& functor) {
  return internal::ToPermanentCallbackResult<Functor>(
      std::forward<Functor>(functor));
}

// Variant of ToCallback that eagerly converts to Closure, to mitigate issues
// with late conversion in bind_front.
template <typename Functor>
  requires(std::is_void_v<std::invoke_result_t<Functor>>)
Closure* ToCallback(Functor&& functor,
                    perftools::tracing::StringLabel label =
                        perftools::tracing::TraceSourceLocation::current()) {
  return ToCallback<Closure>(std::forward<Functor>(functor), std::move(label));
}

// Variant of ToPermanentCallback that eagerly converts to Closure, to mitigate
// issues with late conversion in bind_front.
template <typename Functor>
  requires(std::is_void_v<std::invoke_result_t<Functor>>)
Closure* ToPermanentCallback(Functor&& functor) {
  return ToPermanentCallback<Closure>(std::forward<Functor>(functor));
}

}  // namespace functional
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_TO_CALLBACK_H_
