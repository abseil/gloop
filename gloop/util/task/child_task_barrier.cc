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

#include "gloop/util/task/child_task_barrier.h"

#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/functional/bind_front.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "gloop/base/callback.h"
#include "gloop/thread/executor.h"
#include "gloop/util/functional/to_callback.h"
#include "gloop/util/task/task.h"

namespace util {

class CustomChildTaskBarrier::State {
 public:
  State(util::Task* task, absl::Status initial_status,
        CustomChildTaskBarrier::PolicyFunction acc_fn)
      : parent_task_(task),
        accumulated_status_(std::move(initial_status)),
        accumulator_function_(std::move(acc_fn)) {}

  void OneChildDoneWithCallback(
      absl::AnyInvocable<void(util::Task*) &&> child_done,
      Closure* barrier_done, util::Task* child) {
    std::move(child_done)(child);
    OneChildDone(barrier_done, child);
  }

  void OneChildDone(Closure* barrier_done, util::Task* child) {
    bool return_early = false;
    absl::Status return_early_status;
    {
      absl::MutexLock lock(accumulated_status_lock_);
      if (accumulator_function_(&accumulated_status_, child)) {
        return_early = true;
        return_early_status = accumulated_status_;
      }
    }
    if (return_early) parent_task_->Return(return_early_status);
    barrier_done->Run();  // This must be last. The last child deletes 'this'.
  }

  void AllChildrenDone() {
    // Since this is running during completion of a child, we can guarantee that
    // we still have a hold on 'parent_task_' here.
    absl::Status s;
    {
      absl::MutexLock lock(accumulated_status_lock_);
      s = accumulated_status_;
    }
    // NB: State::OneChildDone may have already called Return().
    parent_task_->Return(s);
    delete this;
  }

  util::Task* parent_task() { return parent_task_; }

 private:
  util::Task* parent_task_;  // Unowned, returned by AllChildrenDone.
  absl::Mutex accumulated_status_lock_;
  absl::Status accumulated_status_ ABSL_GUARDED_BY(accumulated_status_lock_);
  CustomChildTaskBarrier::PolicyFunction accumulator_function_
      ABSL_GUARDED_BY(accumulated_status_lock_);
};

CustomChildTaskBarrier::CustomChildTaskBarrier(
    util::Task* parent_task, absl::Status initial_status,
    CustomChildTaskBarrier::PolicyFunction policy_fn)
    : state_(new State(parent_task, std::move(initial_status),
                       std::move(policy_fn))),
      hold_(state_->parent_task()),
      barrier_(absl::bind_front(&State::AllChildrenDone, state_)) {}

CustomChildTaskBarrier::~CustomChildTaskBarrier() {
  // Order of destruction matters, to be sure that all Children have been added
  // before destroying the IncrementalBarrier.

  // destroy barrier.
  // release hold.
}

util::Task* CustomChildTaskBarrier::DoAddChildTaskWithExecutor(
    thread::Executor* executor) {
  return state_->parent_task()->AddChildWithExecutor(
      absl::bind_front(&State::OneChildDone, state_,
                       util::functional::ToCallback(barrier_.InvocableInc())),
      executor);
}

util::Task* CustomChildTaskBarrier::DoAddChildTaskWithCallbackAndExecutor(
    absl::AnyInvocable<void(util::Task*) &&> child_done,
    thread::Executor* executor) {
  return state_->parent_task()->AddChildWithExecutor(
      absl::bind_front(&State::OneChildDoneWithCallback, state_,
                       std::move(child_done),
                       util::functional::ToCallback(barrier_.InvocableInc())),
      executor);
}

ChildTaskBarrier::ChildTaskBarrier(util::Task* parent_task)
    : CustomChildTaskBarrier(
          parent_task, absl::OkStatus(),
          [](absl::Status* accumulated_status, util::Task* child) {
            // Propagate the first failure, or let others finish and return OK.
            if (!child->status().ok()) {
              *accumulated_status = child->status();
              return true;  // Return parent on first error.
            }
            return false;  // Let siblings finish.
          }) {}

NeglectedChildTaskBarrier::NeglectedChildTaskBarrier(util::Task* parent_task)
    : CustomChildTaskBarrier(
          parent_task, absl::OkStatus(),
          [](absl::Status* accumulated_status, util::Task* child) {
            // NeglectedChildTaskBarrier doesn't pay attention to its children.
            return false;  // Never return early.
          }) {}

RedundantChildTaskBarrier::RedundantChildTaskBarrier(util::Task* parent_task)
    : CustomChildTaskBarrier(
          parent_task, absl::UnknownError(""),
          [](absl::Status* accumulated_status, util::Task* child) {
            // Propagate the first OK return, or report the last failure.
            *accumulated_status = child->status();
            return child->status().ok();  // Return parent on first success.
          }) {}

}  // namespace util
