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

#ifndef THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_AUTO_FUNCTION_RUNNER_H_
#define THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_AUTO_FUNCTION_RUNNER_H_

#include <functional>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/functional/any_invocable.h"

namespace util::functional {

// AutoFunctionRunner executes a callable upon destruction. This class is
// similar to std::unique_ptr: it is typically stack-allocated and can be used
// to perform some type of cleanup upon exiting a block.
//
// Note: use of AutoFunctionRunner with callables that must be executed at
// specific points is discouraged. For example, consider a callback that should
// execute after a mutex has been released.  The following code looks correct,
// but executes too early (before release):
// {
//   MutexLock l(...);
//   AutoFunctionRunner r(run_after_unlock);
//   ...
// }
// AutoFunctionRunner is primarily intended for cleanup operations that are
// relatively independent from other code.
class ABSL_MUST_USE_RESULT AutoFunctionRunner {
 public:
  AutoFunctionRunner() = default;

  template <typename F, typename = std::enable_if_t<std::is_invocable_v<F>>>
  explicit AutoFunctionRunner(F&& f) : callable_(std::forward<F>(f)) {}

  // Explicit overload for std::function so that we can convert emptiness
  // correctly.
  explicit AutoFunctionRunner(std::function<void()> function)
      : callable_(function ? absl::AnyInvocable<void() &&>(std::move(function))
                           : nullptr) {}

  AutoFunctionRunner(const AutoFunctionRunner&) = delete;
  AutoFunctionRunner& operator=(const AutoFunctionRunner&) = delete;

  AutoFunctionRunner(AutoFunctionRunner&& other) noexcept
      : callable_(std::exchange(other.callable_, {})) {}

  AutoFunctionRunner& operator=(AutoFunctionRunner&& other) noexcept {
    AutoFunctionRunner(std::move(other)).swap(*this);
    return *this;
  }

  ~AutoFunctionRunner() { Invoke(); }

  void swap(AutoFunctionRunner& other) noexcept {
    callable_.swap(other.callable_);
  }
  friend void swap(AutoFunctionRunner& x, AutoFunctionRunner& y) noexcept {
    x.swap(y);
  }

  // Cancel the execution of the underlying callable.
  void Cancel() { callable_ = {}; }

  void Invoke() {
    if (!empty()) {
      std::exchange(callable_, nullptr)();
    }
  }

  template <typename F, typename = std::enable_if_t<std::is_invocable_v<F>>>
  void Reset(F&& f) {
    callable_ = {};
    *this = AutoFunctionRunner(std::forward<F>(f));
  }

  // Release the contained callable, leaving the AutoFunctionRunner in an
  // empty state. If empty() is true when ReleaseInvocable is called, return
  // an empty AnyInvocable.
  //
  // If you do not intend to run the returned callable, use Cancel() instead.
  ABSL_MUST_USE_RESULT absl::AnyInvocable<void() &&> ReleaseInvocable() {
    return std::exchange(callable_, nullptr);
  }

  bool empty() const { return callable_ == nullptr; }
  explicit operator bool() const { return !empty(); }

 private:
  absl::AnyInvocable<void() &&> callable_;
};

}  // namespace util::functional

#endif  // THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_AUTO_FUNCTION_RUNNER_H_
