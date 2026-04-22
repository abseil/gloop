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

// Utility classes to complete a parent task once all child tasks complete.

#ifndef THIRD_PARTY_GLOOP_UTIL_TASK_CHILD_TASK_BARRIER_H_
#define THIRD_PARTY_GLOOP_UTIL_TASK_CHILD_TASK_BARRIER_H_

#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "gloop/concurrent/barrier/incremental_barrier.h"
#include "gloop/thread/executor.h"
#include "gloop/util/task/task.h"

namespace util {

// Common methods for all ChildTaskBarrier classes.
class AbstractChildTaskBarrier {
 public:
  AbstractChildTaskBarrier() = default;
  virtual ~AbstractChildTaskBarrier() = default;

  AbstractChildTaskBarrier(const AbstractChildTaskBarrier&) = delete;
  AbstractChildTaskBarrier& operator=(const AbstractChildTaskBarrier&) = delete;

  // Returns a new child task of 'parent'. Errors are propagated from child to
  // parent, and cancellations are propagated from parent to child.
  util::Task* AddChildTask() { return DoAddChildTaskWithExecutor(nullptr); }

  // Returns a new child task of 'parent'. If `executor` is not null,
  // the child callback will run in the provided executor; otherwise
  // the behaviour is equivalent to AddChild().
  util::Task* AddChildTaskWithExecutor(thread::Executor* executor) {
    return DoAddChildTaskWithExecutor(executor);
  }

  // Returns a new child task of 'parent' invoking 'child_done' when the
  // child completes. 'child_done' may not be disengaged.
  util::Task* AddChildTaskWithCallback(
      absl::AnyInvocable<void(util::Task*) &&> child_done) {
    CHECK(child_done != nullptr);
    return DoAddChildTaskWithCallbackAndExecutor(std::move(child_done),
                                                 nullptr);
  }

  // Returns a new child task of 'parent' invoking 'child_done' on the provided
  // executor when the child completes. The executor may be null; 'child_done'
  // may not be disengaged.
  util::Task* AddChildTaskWithCallbackAndExecutor(
      absl::AnyInvocable<void(util::Task*) &&> child_done,
      thread::Executor* executor) {
    CHECK(child_done != nullptr);
    return DoAddChildTaskWithCallbackAndExecutor(std::move(child_done),
                                                 executor);
  }

 private:
  virtual util::Task* DoAddChildTaskWithExecutor(
      thread::Executor* executor) = 0;
  virtual util::Task* DoAddChildTaskWithCallbackAndExecutor(
      absl::AnyInvocable<void(util::Task*) &&> child_done,
      thread::Executor* executor) = 0;
};

// A ChildTaskBarrier class with a customizable propagation policy.
// For example, you may want to collect temporary errors (like a
// RedundantChildTaskBarrier would) but not terminate early (thereby canceling
// other children), while wanting to propagate permanent errors (like a
// ChildTaskBarrier would). You probably want to report the most severe error.
// This would be:
//   CustomChildTaskBarrier barrier(
//       parent_task,
//       absl::OkStatus(),
//       [](absl::Status *accumulated_status, util::Task *child) {
//         *accumulated_status = ReturnMostSevere(accumulated_status,
//                                                child->status());
//         return IsPermanent(child->status());  // Return on permanent errors.
//       });
class CustomChildTaskBarrier : public AbstractChildTaskBarrier {
 public:
  // Given the current 'accumulated_status' and a newly returned 'child_task',
  // return a pair with the new accumulated_status and true if the parent
  // should return immediately.
  using PolicyFunction = absl::AnyInvocable<bool(
      absl::Status* accumulated_status, util::Task* child_task)>;

  // 'initial_status' is the original value for the 'accumulated_status' passed
  // to 'policy_function'. 'parent' is returned with 'accumulated_status' when
  // 'policy_function' returns true or all children have returned.
  // NB: 'policy_function' will be called while holding a Mutex.
  explicit CustomChildTaskBarrier(util::Task* parent,
                                  absl::Status initial_status,
                                  PolicyFunction policy_function);

  ~CustomChildTaskBarrier() override;

 private:
  util::Task* DoAddChildTaskWithExecutor(thread::Executor* executor) override;
  util::Task* DoAddChildTaskWithCallbackAndExecutor(
      absl::AnyInvocable<void(util::Task*) &&> child_done,
      thread::Executor* executor) override;

  class State;  // Internal implementation details.
  State* state_;

  // Barrier must be destroyed before the task hold.
  util::TaskHold hold_;
  concurrent::IncrementalBarrier barrier_;
};

// This class helps avoid the common error of forgetting to Hold the parent task
// when adding children to an IncrementalBarrier.  It also wraps up the common
// case of propagating success / failure from the children to the parent.
//
// If a child fails, the error is returned on the parent task (resulting in the
// remaining children being cancelled). Otherwise the parent task is returned on
// once all children finish.
//
// Example usage:
//   ChildTaskBarrier barrier(parent);
//   for (int i = 0; i < 10; ++i) {
//     util::Task *child = barrier.AddChildTask();
//     AsyncOperation *op = new AsyncOperation(child);
//     child->DeleteWhenDone(op);
//     op->Run();
//   }
//   // barrier goes out of scope somewhere, which allows parent to be
//   // Return()ed upon at some point after all children complete.
class ChildTaskBarrier : public CustomChildTaskBarrier {
 public:
  // The 'parent' task that the child tasks are based on.
  // See class comments for details.
  explicit ChildTaskBarrier(util::Task* parent);

  ~ChildTaskBarrier() override = default;
};

// Holds the parent task until all child tasks have completed but does not
// propagate the children's status to the parent (always returns OK to parent).
// Cancellations are propagated from parent to child.
//
// Note that this object also needs to be destroyed before parent will be
// Return()ed upon after all children complete.
class NeglectedChildTaskBarrier : public CustomChildTaskBarrier {
 public:
  // The 'parent' task that the child tasks are based on.  Does not assume
  // ownership of 'parent'.
  explicit NeglectedChildTaskBarrier(util::Task* parent);

  ~NeglectedChildTaskBarrier() override = default;
};

// Holds the parent task until either all children return !OK status (in
// which case the last failure status is returned to parent), or any
// child returns with OK status (in which case parent is Return()ed with OK,
// cancelling the remaining children).  If no child tasks are ever created,
// default to an UNKNOWN failure.
class RedundantChildTaskBarrier : public CustomChildTaskBarrier {
 public:
  // The 'parent' task that the child tasks are based on.  Does not assume
  // ownership of 'parent'.
  explicit RedundantChildTaskBarrier(util::Task* parent);

  ~RedundantChildTaskBarrier() override = default;
};

}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TASK_CHILD_TASK_BARRIER_H_
