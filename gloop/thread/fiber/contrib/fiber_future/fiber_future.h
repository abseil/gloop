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

// A lightweight Future which uses Fibers to propagate cancellation.
//
// FiberFuture supports creating Futures that run using a child Fiber or a new
// Fiber tree.
//
// Example:
//   auto func = absl::bind_front(&SomeFunction);
//   FiberFuture<T> future(func);
//   ...
//   T value = future.Get();
//
// In case of FiberFutures running as a separate tree, cancellation can be
// explicitly propagated using FiberFuture<>::Cancel(). This is useful when the
// Future has different lifetime and cancellation semantics compared to the
// Fiber initiating the Future (See FiberFuture<T>::NewTree() for more details).
//
// FiberFutures have semantics similar to ResultFibers.  It is possible that in
// the future we will converge to use only one of these types.  One important
// difference is that ResultFibers must always be explicitly Join()-ed, whereas
// FiberFutures implicitly manage the lifetime of the underlying fiber.  Even
// if the value is not consumed.

#ifndef THIRD_PARTY_GLOOP_THREAD_FIBER_CONTRIB_FIBER_FUTURE_FIBER_FUTURE_H_
#define THIRD_PARTY_GLOOP_THREAD_FIBER_CONTRIB_FIBER_FUTURE_FIBER_FUTURE_H_

#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/time/clock_interface.h"
#include "absl/time/time.h"
#include "gloop/thread/fiber/fiber-options.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/select.h"
#include "gloop/thread/fiber/selectables.h"

namespace thread {
namespace contrib {

template <typename T, typename FiberType = Fiber>
  requires std::derived_from<FiberType, Fiber>
class FiberFuture {
 public:
  using FutureFunc = absl::AnyInvocable<T() &&>;

  // Creates a new FiberFuture that will produce its value within a child Fiber.
  // It maintains all regular properties of child fibers, including inheritance
  // of cancellation and deadlines.
  //
  // The passed function "func" will be freed once a result is available.
  //
  // Example:
  //   FiberFuture<int> result([] { ...; return expensive_result; });
  //   ...
  //   // result.Get() will return "expensive_result" when available.
  explicit FiberFuture(FutureFunc func)
      : fiber_(
            std::make_unique<FiberType>([this, f = std::move(func)]() mutable {
              value_ = std::exchange(f, nullptr)();
            })) {}

  // Equivalent to the semantics above, except that options may be specified.
  FiberFuture(const FiberOptions& options, FutureFunc func)
      : fiber_(std::make_unique<FiberType>(
            options, [this, f = std::move(func)]() mutable {
              value_ = std::exchange(f, nullptr)();
            })) {}

  // Constructs an immediate future without a fiber object. Calls to Get() will
  // return immediately with the given value, OnFinished() will always return
  // immediately with IsFinished() returning true.
  //
  // Example:
  // FiberFuture<int> future(std::in_place, 42);
  //
  FiberFuture(std::in_place_t, T value) : value_(std::move(value)) {}

  FiberFuture(const FiberFuture&) = delete;
  FiberFuture(FiberFuture&&) = delete;
  FiberFuture& operator=(const FiberFuture&) = delete;
  FiberFuture& operator=(FiberFuture&&) = delete;

  // Creates a new FiberFuture that will be evaluated within an independent
  // fiber (i.e. thread::NewRoot) tree.  Appropriate for producing values which
  // are independent of an individual request.
  //
  // Note: that this FiberFuture will not automatically inherit cancellation or
  // deadlines from any client .  If desired, cancellation must be manually
  // forwarded using "Cancel()".  "OnFinished()" may be used to synchronize
  // waiting for a result while waiting for cancellation. See thread::NewTree()
  // for more discussion of how these properties are handled.
  //
  // As in the child FiberFuture case, the passed function will be freed once
  // evaluation has completed.
  //
  // Example:
  // std::unique_ptr<FiberFuture<ExpensiveObject>> future_;
  //
  // // Called during initialization, or potentially on state changes.
  // void PrepareExpensiveObject() {
  //     // Note:  Releasing any previous FiberFuture synchronizes with
  //     // its completion.
  //     future_.reset(
  //         FiberFuture<util::Status>>::NewTree({}, std::bind(&SomeFunction)));
  //  }
  //  ...
  //  void Request() {
  //     // Request can now consume ExpensiveObject directly, via
  //     // future_->Get(), or synchronize on its availability by
  //     // composing OnFinished() with Select(...).
  //  }
  //
  // See `FiberFutureInNewTree()` for a version with type inference.
  static std::unique_ptr<FiberFuture> NewTree(const TreeOptions& tree_options,
                                              FutureFunc func) {
    static_assert(std::same_as<FiberType, Fiber>,
                  "NewTree only supports `thread::Fiber` as fiber type.");
    return std::unique_ptr<FiberFuture>(
        new FiberFuture(tree_options, std::move(func)));
  }

  // If the future is not complete, the underlying Fiber is cancelled when *this
  // is destroyed.
  ~FiberFuture() {
    if (fiber_ != nullptr) {
      fiber_->Cancel();
      fiber_->Join();
    }
  }

  // Returns a case that becomes ready when the future function returns and the
  // value is ready.  Any future Get() call will not block.
  Case OnFinished() const {
    return fiber_ != nullptr ? fiber_->OnJoinable() : AlwaysSelectableCase();
  }

  // Returns true if the future has returned and the value is ready.  In that
  // case, any future Get() call will not block.
  bool IsFinished() const {
    return SelectUntil(absl::InfinitePast(), {OnFinished()}) == 0;
  }

  // Cancels the underlying Fiber responsible for this future.  It will still
  // produce a result via Get().  For FiberFutures which return nothing on
  // cancellation consider composition with Optional<T>.  This operation is a
  // no-op for immediate futures.
  void Cancel() {
    if (fiber_ != nullptr) {
      fiber_->Cancel();
    }
  }

  // Returns whether the Fiber responsible for this future is cancelled.
  bool Cancelled() const {
    return fiber_ != nullptr ? fiber_->Cancelled() : false;
  }

  // Blocks until IsFinished() is true.  Returns the T produced by *this.
  T& Get() & {
    thread::Select({OnFinished()});
    return *value_;
  }

  // Blocks until IsFinished() is true.  Returns the T produced by *this.
  const T& Get() const& {
    thread::Select({OnFinished()});
    return *value_;
  }

  // Blocks until IsFinished() is true.  Returns the T produced by *this.
  T&& Get() && {
    thread::Select({OnFinished()});
    return *std::move(value_);
  }

  // Blocks until IsFinished() is true.  Returns the T produced by *this.
  const T&& Get() const&& {
    thread::Select({OnFinished()});
    return *std::move(value_);
  }

  // Blocks until IsFinished() is true or the deadline runs out according to the
  // given clock. Note that a nullptr `clock` is equivalent to
  // &absl::Clock::GetRealClock().
  // Returns a present T produced by *this or absl::nullopt.
  ::std::optional<std::reference_wrapper<const T>> GetUntil(
      ::absl::Time deadline, absl::Clock* clock = nullptr) const {
    if (SelectUntil(clock, deadline, {OnFinished()}) != -1) {
      return value_;
    }
    return ::std::nullopt;
  }

  // Blocks until IsFinished() is true.  Moves the value produced by *this
  // out of this object and returns it.
  // Prefer using Get()&& which enables compile-time warnings on reusing the
  // object after the FiberFuture has been moved from.
  T Consume() {
    thread::Select({OnFinished()});
    return *std::move(value_);
  }

  // Blocks until IsFinished() is true or the deadline runs out according to the
  // given clock. Note that a nullptr `clock` is equivalent to
  // &absl::Clock::GetRealClock().
  // Returns a present value produced by *this or absl::nullopt.
  ::std::optional<T> ConsumeUntil(::absl::Time deadline,
                                  absl::Clock* clock = nullptr) {
    if (SelectUntil(clock, deadline, {OnFinished()}) != -1) {
      return std::move(value_);
    }
    return ::std::nullopt;
  }

 private:
  FiberFuture(const TreeOptions& tree_options, FutureFunc func)
      : fiber_(::thread::NewTree(tree_options,
                                 [this, f = std::move(func)]() mutable {
                                   value_ = std::move(f)();
                                   f = nullptr;
                                 })) {}

  std::optional<T> value_ = std::nullopt;

  // The 'fiber_' object will be null in cases where this object was constructed
  // with an immediate value.
  std::unique_ptr<FiberType> fiber_;
};

// CTAD (https://abseil.io/tips/184) rules to not have to specify the type of a
// fiber when obvious from context.
template <typename F>
FiberFuture(F f) -> FiberFuture<decltype(std::move(f)())>;
template <typename F>
FiberFuture(const FiberOptions&, F f) -> FiberFuture<decltype(std::move(f)())>;
template <typename T>
FiberFuture(std::in_place_t, T) -> FiberFuture<T>;

// Same as `FiberFuture<T>::NewTree(tree_options, func)`, but infers the type
// `T` from the return type of `func`.
template <int&... DoNotSpecify, typename Func,
          typename T = decltype(std::declval<Func>()())>
std::unique_ptr<FiberFuture<T>> FiberFutureInNewTree(
    const TreeOptions& tree_options, Func func) {
  return FiberFuture<T>::NewTree(tree_options, std::move(func));
}

}  // namespace contrib
}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_FIBER_CONTRIB_FIBER_FUTURE_FIBER_FUTURE_H_
