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

#include "gloop/concurrent/executors/loop_executor.h"

#include <stddef.h>

#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/functional/bind_front.h"
#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gloop/base/callback.h"
#include "gloop/base/varsetter.h"
#include "gloop/thread/executor.h"
#include "gloop/util/functional/to_callback.h"

namespace concurrent {

class ABSL_SCOPED_LOCKABLE LoopExecutor::ScopedLoopRunningRegion {
 public:
  explicit ScopedLoopRunningRegion(LoopExecutor* executor)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(&executor->mu_)
      : lock_(executor->mu_),
        loop_was_running_(executor->loop_running_),
        replace_loop_running_(&executor->loop_running_, true),
        replace_current_executor_(Executor::CurrentExecutorPointerInternal(),
                                  executor) {
    if (loop_was_running_) {
      LOG(DFATAL) << "It's illegal to run LoopExecutor closures"
                  << " recursively or from more than one thread at a time.";
    }
  }

  ~ScopedLoopRunningRegion() ABSL_UNLOCK_FUNCTION() {}

  // Returns true if this or another thread is already executing tasks
  // on behalf of the LoopExecutor.
  bool SomebodyRunningClosuresAlready() { return loop_was_running_; }

 private:
  absl::MutexLock lock_;
  const bool loop_was_running_;
  VarSetter<bool> replace_loop_running_;
  VarSetter<Executor*> replace_current_executor_;
};

LoopExecutor::LoopExecutor()
    : loop_running_(false),
      loop_should_exit_(false),
      ready_to_run_(false),
      add_after_helper_(
          nullptr, absl::bind_front(&LoopExecutor::CompleteAddAfter, this)) {}

namespace {
template <class C>
void STLDeleteNonPermanentCallbacks(C* container) {
  for (typename C::const_iterator it = container->begin();
       it != container->end(); ++it) {
    if (*it != nullptr && !(*it)->IsRepeatable()) {
      delete *it;
    }
  }
}
}  // namespace

LoopExecutor::~LoopExecutor() {
  // We need to cancel any AddAfter() calls and then delete any
  // closures that have been pushed onto work_queue_.  The order is
  // important because running AddAfter() calls can continue to push
  // work onto the queue.
  add_after_helper_.ShutdownAndRunPendingImmediately();

  // Now the TimedCall thread won't touch this object anymore. Users
  // have to make sure they don't touch the object anymore after the
  // destructor starts, so we don't have to re-acquire the lock.
  STLDeleteNonPermanentCallbacks(&work_queue_);
}

void LoopExecutor::RunFrontTask() {
  if (work_queue_.empty()) {
    LOG(DFATAL) << "RunFrontTask called with empty work queue.";
    return;
  }
  Closure* task = work_queue_.front();
  work_queue_.pop_front();
  mu_.unlock();
  task->Run();
  mu_.lock();
}

bool LoopExecutor::TryRunOneClosure() {
  ScopedLoopRunningRegion running(this);
  if (running.SomebodyRunningClosuresAlready()) {
    return false;
  }
  if (work_queue_.empty()) {
    return false;
  }
  RunFrontTask();
  return true;
}

void LoopExecutor::RunQueuedClosures() {
  Schedule([this] { MakeLoopExit(); });
  Loop();
}

bool LoopExecutor::WorkAvailable() {
  return !work_queue_.empty() || loop_should_exit_;
}

void LoopExecutor::Loop() {
  ScopedLoopRunningRegion running(this);
  if (running.SomebodyRunningClosuresAlready()) {
    return;
  }
  while (true) {
    ready_to_run_ = true;
    mu_.Await(absl::Condition(this, &LoopExecutor::WorkAvailable));
    ready_to_run_ = false;
    if (loop_should_exit_) {
      // Reset for next loop.
      loop_should_exit_ = false;
      return;
    }
    RunFrontTask();
  }
}

void LoopExecutor::MakeLoopExit() {
  absl::MutexLock l(mu_);
  loop_should_exit_ = true;
}

void LoopExecutor::Add_Prelocked(absl::AnyInvocable<void() &&> task) {
  work_queue_.push_back(util::functional::ToCallback(std::move(task)));
}

void LoopExecutor::Schedule(absl::AnyInvocable<void() &&> callback) {
  if (callback == nullptr) {
    LOG(DFATAL) << "Cannot add NULL task to LoopExecutor.";
    return;
  }
  absl::MutexLock l(mu_);
  Add_Prelocked(std::move(callback));
}

void LoopExecutor::ScheduleAt(absl::Time when,
                              absl::AnyInvocable<void() &&> callback) {
  if (callback == nullptr) {
    LOG(DFATAL) << "Cannot add NULL task to LoopExecutor.";
    return;
  }
  add_after_helper_.ScheduleAddAfterAt(when, std::move(callback));
}

void LoopExecutor::CompleteAddAfter(absl::AnyInvocable<void() &&> task) {
  absl::MutexLock l(mu_);
  Add_Prelocked(std::move(task));
}

int LoopExecutor::num_pending_closures() const {
  absl::MutexLock l(mu_);
  return work_queue_.size();
}

}  // namespace concurrent
