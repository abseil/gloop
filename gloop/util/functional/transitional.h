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

// Functions which assist implementations in migrating from Callback to
// gtl::Invocable or std::function.

#ifndef THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_TRANSITIONAL_H_
#define THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_TRANSITIONAL_H_

#include <utility>

#include "gloop/util/functional/with_context.h"

namespace util {
namespace functional {
namespace transitional {

// Optionally captures the current Context for future use.  This function is
// intended to help implementations which take a Callback (or Closure)
// as an argument migrate to an absl::AnyInvocable implementation, while still
// temporarily maintaining compatibility with existing Callback semantics.
//
// Historically, Callback captured the ambient Context object at creation time
// (i.e., during the call to NewCallback) for later use when the wrapped
// function was invoked.  std::function, and later absl::AnyInvocable do not
// capture the ambient context at creation time and instead use the context
// available when the function is invoked (see <link>).  In most
// cases, this distinction is not important, but this cannot be guaranteed in
// all cases.
//
// As implementations which take and store Callbacks for future execution
// migrate to absl::AnyInvocable, they need to take care to temporarily maintain
// existing behavior around their inputs.  To do so, wrap inputs using
// MaybeWithCurrentContext and a locally defined flag:
//
// ABSL_FLAG(bool, my_system_legacy_capture_context, true, "....");
//
// class MySystem {
//  public:
//   void Add(Callback* cb) {
//     callbacks.push_back(util::functional::FromCallbackWithOwnership(cb));
//   }
//
//   void Add(absl::AnyInvocable<void() &&> func) {
//    callbacks.push_back(
//        util::functional::transitional::MaybeWithCurrentContext(
//            absl::GetFlag(FLAGS_my_executor_legacy_capture_context),
//            std::move(func)));
//    }
//
//  private:
//   std::vector<AnyInvocable<void() &&>> callbacks;
// };
//
// As callers migrate to the absl::AnyInvocable API, they maintain the existing
// semantics, and can test the change in Context capturing by toggling the flag
// value.  Eventually, the owners of MySystem should change the flag value to
// false by default, and then remove it altogether, along with the call to
// MaybeWithCurrentContext.  Users who need to capture the creation-time context
// with the AnyInvocable API can do so explicitly with
// util::functional::WithCurrentContext.

template <typename Functor>
auto MaybeWithCurrentContext(bool capture_context, Functor&& invocable)
    -> decltype(capture_context
                    ? WithCurrentContext(std::forward<Functor>(invocable))
                    : std::forward<Functor>(invocable)) {
  return capture_context ? WithCurrentContext(std::forward<Functor>(invocable))
                         : std::forward<Functor>(invocable);
}

}  // namespace transitional
}  // namespace functional
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_TRANSITIONAL_H_
