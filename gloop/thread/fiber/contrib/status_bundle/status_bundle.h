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

#ifndef THIRD_PARTY_GLOOP_THREAD_FIBER_CONTRIB_STATUS_BUNDLE_STATUS_BUNDLE_H_
#define THIRD_PARTY_GLOOP_THREAD_FIBER_CONTRIB_STATUS_BUNDLE_STATUS_BUNDLE_H_

#include <optional>
#include <type_traits>
#include <utility>

#include "absl/base/call_once.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "gloop/thread/fiber/bundle.h"
#include "gloop/thread/fiber/fiber-options.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/select.h"
#include "gloop/util/functional/from_callback.h"

namespace thread {

// StatusBundle allows a parent fiber to create N children (where N may not be
// known in advance), each of which is running a function that returns
// absl::Status, with early exit on failure.
//
// Children are added with the Add method. If one child fails, all current and
// future children are cancelled (see <link> for
// cancellation details). Join will return the first error encountered, or OK if
// all children succeeded. Children observe cancellation of the parent fiber as
// usual.
//
// Status bundles are not intended for use from multiple fibers; the only
// methods that are safe to access from anything but the fiber that created the
// bundle are Cancel and Cancelled. This includes the destructor.
//
// If you find yourself needing to call Add from any other fiber, among other
// design issues it could be a sign that you've failed to bound your parallelism
// and risk unbounded memory usage. Consider instead starting N worker fibers in
// a status bundle, each reading work from a channel, and making the code that
// generates work a sibling in the same bundle.
class StatusBundle {
 public:
  // Create an initially-empty bundle using the current fiber's options.
  // The bundle must be joined before it is destroyed.
  StatusBundle() = default;

  // As above, but use the supplied options for creating all child fibers.
  explicit StatusBundle(const thread::FiberOptions& options);

  // This type is neither copyable nor movable.
  StatusBundle(const StatusBundle&) = delete;
  StatusBundle& operator=(const StatusBundle&) = delete;

  // Add an operation to the bundle. The bundle must not yet have been joined.
  //
  // The operation will be called exactly once. If a previous operation has
  // already returned an error, the operation may immediately observe that its
  // calling fiber is cancelled.
  //
  // 'op' may be any functor that returns a Status. This includes
  // std::function<absl::Status>, absl::AnyInvocable<absl::Status() &&>, a
  // Status-returning lambda, etc.
  //
  // The functor only needs to be moveable and callable once. For example,
  // lambdas with move-only arguments are valid:
  //
  // Add([x = std::move(move_only_object)]() { ... });
  //
  // Don't specify the template parameters; they're meant to be inferred
  // automatically.
  template <typename F,
            // Force this overload to only bind to callable objects, as opposed
            // to OpCallback* (defined below).
            typename = std::invoke_result_t<F>>
  void Add(F&& op);

  // A legacy version of the overload above. Please do not use in new code.
  using OpCallback = ::util::functional::ResultCallbackFunctor<absl::Status>;
  void Add(OpCallback op);

  // Cancel all present and future operations. Safe to call from any fiber, and
  // guaranteed not to block.
  //
  // Note that Cancel itself does not affect the result of Join beyond the
  // manner in which any operations react to cancellation.
  void Cancel();

  // Returns whether the bundle has been cancelled (either Cancel was called, or
  // the parent fiber was cancelled) or one of the operations returned an error.
  bool Cancelled() const;

  // Return a token that may be used to select against the cancellation of all
  // fibers belonging to this bundle. An operation returning an error also
  // causes the bundle to be cancelled.
  Case OnCancel() const;

  // Wait for all previously-added operations to complete, and return an error
  // if and only if any of them return an error. For the semantics of which
  // error is returned, see class comments.
  //
  // It is legal to call Join multiple times.
  absl::Status Join();

  // Return a token that may be used to select against the completion of all
  // fibers belonging to this bundle.
  //
  // Users must still ensure that Join is called.
  Case OnJoinable();

 private:
  /////////////////////////////////////////////
  // State
  /////////////////////////////////////////////

  // The fiber on which this object was constructed.
  thread::Fiber* const parent_fiber_{thread::Fiber::Current()};

  // The bundle containing the underlying fibers.
  thread::Bundle bundle_;

  // The first error we saw, or nullopt if we haven't yet seen an error.
  std::optional<absl::Status> first_error_;

  // A once flag that decides which error is the first error.
  absl::once_flag once_flag_;
};

template <typename F, typename>
void StatusBundle::Add(F&& op) {
  static_assert(std::is_invocable_r_v<absl::Status, F&&>,
                "Operation must return an absl::Status");

  CHECK_EQ(thread::Fiber::Current(), parent_fiber_)
      << "StatusBundle::Add called on illegal fiber.";

  bundle_.Add([this, op = std::forward<F>(op)]() mutable {
    // Run the op. If it succeeds, we are done.
    absl::Status status = std::move(op)();
    if (status.ok()) {
      return;
    }

    // The op failed. Attempt to store its status as the first error and cancel
    // the other ops.
    absl::call_once(once_flag_, [&] {
      DCHECK(!first_error_.has_value());
      first_error_.emplace(std::move(status));

      // If this was the first error, it's our responsibility to cancel all of
      // the other operations.
      //
      // Doing this from within the call_once callback means that other ops will
      // block on it completing, but that's fine because it should be fast and
      // doesn't do anything but set the cancellation signal inline. In
      // particular this can't cause a deadlock.
      Cancel();
    });
  });
}

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_FIBER_CONTRIB_STATUS_BUNDLE_STATUS_BUNDLE_H_
