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

// A function wrapper which captures the current ambient context and then
// restores it when the given functor is run.
//
// Historically, Google's custom Callback template captured the current
// ambient Context at Callback creation time (i.e., when NewCallback was
// invoked).  The standardized version of std::function<> (and its more generic
// cousin absl::AnyInvocable<>) does not capture the Context at object creation,
// but just uses the Context available when the function is executed.
//
// In most cases, capture and storage of the Context at Callback creation time
// added overhead, but did not materially affect the behavior of the program.
// For callers who need to explicitly capture the ambient context at Callback
// creation time, use WithCurrentContext to wrap the invocable:
//
// {
//   std::function<...> f = ...;
//   SomeFunctionAcceptingMethod(WithCurrentContext(f));
// }
//
// If the argument will not be used after the call to WithCurrentContext, it
// should be moved into the wrapper:
//
// {
//   absl::AnyInvocable<...> f = ...;
//   SomeInvocableAcceptingMethod(WithCurrentContext(std::move(f)));
// }

#ifndef THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_WITH_CONTEXT_H_
#define THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_WITH_CONTEXT_H_

#include <type_traits>
#include <utility>

#include "gloop/base/context.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/trace_source_location.h"
#include "gloop/util/functional/with_context_internal.h"  // IWYU pragma: export

namespace util {
namespace functional {

// Takes an invocable of some kind and returns an invocable that will use
// `context` to invoke `invocable`.
template <typename Functor, typename Context>
internal::WithContextFunctor<std::decay_t<Functor>> WithContext(
    Functor&& invocable, Context&& context) {
  return internal::WithContextFunctor<std::decay_t<Functor>>(
      std::forward<Functor>(invocable), std::forward<Context>(context));
}

// Takes an invocable of some kind and returns an invocable that will restore
// the context of the current thread before invoking `invocable`.
template <typename Functor>
internal::WithContextFunctor<std::decay_t<Functor>> WithCurrentContext(
    Functor&& invocable,
    ::perftools::tracing::StringRef label =
        ::perftools::tracing::TraceSourceLocation::current()) {
  return internal::WithContextFunctor<std::decay_t<Functor>>(
      std::forward<Functor>(invocable),
      ::base::Context(::base::Context::kThread, label));
}

// `WithContext` implementation capturing 'base::Context::kThread' calls.
// Applications should prefer to use `WithCurrentContext()` instead.
template <typename Functor>
internal::WithContextFunctor<std::decay_t<Functor>> WithContext(
    Functor&& invocable, base::Context::ThreadInitType,
    ::perftools::tracing::StringRef label =
        ::perftools::tracing::TraceSourceLocation::current()) {
  return internal::WithContextFunctor<std::decay_t<Functor>>(
      std::forward<Functor>(invocable),
      ::base::Context(::base::Context::kThread, label));
}

}  // namespace functional
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_WITH_CONTEXT_H_
