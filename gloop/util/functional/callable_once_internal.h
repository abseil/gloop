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

#ifndef THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_CALLABLE_ONCE_INTERNAL_H_
#define THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_CALLABLE_ONCE_INTERNAL_H_

#include <atomic>
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/log/check.h"

namespace util {
namespace functional {
namespace internal {

// Encapsulates a functor and whether it was called or not.
template <class Functor>
struct CalledState {
  template <class F>
  explicit CalledState(F&& f) : functor(std::forward<F>(f)) {}

  std::atomic<bool> called{false};
  Functor functor;
};

// Encapsulates a functor and whether it was called or not, it also check fails
// at destruction if never called.
template <class Functor>
struct CheckCalledState : public CalledState<Functor> {
  using CalledState<Functor>::CalledState;
  ~CheckCalledState() {
    const bool check = this->called.load(std::memory_order_relaxed);
    CHECK(check) << "Functor was never called.";
  }
};

// Encapsulates a Functor state giving it shared semantics and forwards
// operator() const to operator() &&.
template <class State>
class SharedCallWrapperAtMostOnce {
 public:
  // The int is to limit overtriggering and to ensure that the copy/move
  // constructors are called when we want to copy/move, rather than this one.
  template <class Functor>
  explicit SharedCallWrapperAtMostOnce(int dummy, Functor&& functor)  // NOLINT
      : internal_(
            std::shared_ptr<State>(new State(std::forward<Functor>(functor)))) {
  }

  // We use the std::void_t and decltype(auto) to avoid having the
  // decltype() expression of the return type as part of the mangled name.
  template <class... Args,
            typename = std::enable_if_t<std::is_invocable_v<
                decltype(std::declval<State>().functor), Args...>>>
  decltype(auto) operator()(Args&&... args) const {  // NOLINT
    const bool called =
        internal_->called.exchange(true, std::memory_order_relaxed);
    CHECK(!called) << "Functor was already called";
    return std::move(internal_->functor)(std::forward<Args>(args)...);
  }

 private:
  std::shared_ptr<State> internal_;
};

}  // namespace internal
}  // namespace functional
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_FUNCTIONAL_CALLABLE_ONCE_INTERNAL_H_
