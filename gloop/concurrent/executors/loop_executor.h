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

#ifndef THIRD_PARTY_GLOOP_CONCURRENT_EXECUTORS_LOOP_EXECUTOR_H_
#define THIRD_PARTY_GLOOP_CONCURRENT_EXECUTORS_LOOP_EXECUTOR_H_

#include <deque>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gloop/base/callback.h"
#include "gloop/thread/add_after_helper.h"
#include "gloop/thread/executor.h"

class TimedCall;

namespace concurrent {

// Allows you to turn one thread into an Executor.  This can be useful
// when you already have a thread and need to run a single other
// continuation-based computation.  Closures added to a LoopExecutor
// are executed in FIFO order in a single thread.
//
// This class is thread-safe, except that only one call to
// TryRunOneClosure(), RunQueuedClosures(), or Loop() can be active at
// a time, to preserve the single-threaded nature of closure running.
class LoopExecutor : public thread::Executor {
 public:
  LoopExecutor();

  // You must make sure Loop(), etc., are no longer running by the
  // time the LoopExecutor is destroyed.  If there are closures queued
  // that haven't run by the time the LoopExecutor is destroyed, they
  // are deleted.
  //
  // If you have trouble ensuring that the loop has returned by the
  // time you destroy the executor, contact the OWNERS and we may revisit
  // that requirement.
  ~LoopExecutor() override;

  // 3 Closure-execution functions: At most one of
  // TryRunOneClosure(), RunOneIteration(), and Loop() may be
  // running at any particular time, but they can be called
  // concurrently with other methods.

  // Runs a single closure that has been added to this Executor.
  // Returns true if a closure was run, or false if no closure was
  // available.
  bool TryRunOneClosure();

  // Runs the closures that have already been added and stops.  If
  // those closures or another thread add others, the new closures
  // are not run by this call.
  void RunQueuedClosures();

  // Runs closures until MakeLoopExit() is called.
  void Loop();

  // Makes the loop exit immediately.  Any currently-running closure
  // will finish, but any queued closures will not run.
  void MakeLoopExit();

  // Executor interface.
  void Schedule(absl::AnyInvocable<void() &&> callback) override;
  bool TrySchedule(absl::AnyInvocable<void() &&> callback) override {
    Schedule(std::move(callback));
    return true;
  }

  // Unlike many Executors, it is safe to destroy a LoopExecutor
  // before the delayed task is run.
  void ScheduleAt(absl::Time when,
                  absl::AnyInvocable<void() &&> callback) override;
  int num_pending_closures() const override;

 private:
  class ScopedLoopRunningRegion;

  // Adds task to work_queue_.
  inline void Add_Prelocked(absl::AnyInvocable<void() &&> task)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Removes and runs the first task from work_queue_.
  // Precondition: !work_queue_.empty()
  // Releases and re-acquires mu_.
  inline void RunFrontTask() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Same as Add(), but doesn't check the task for NULL or involve a
  // virtual call.
  void CompleteAddAfter(absl::AnyInvocable<void() &&> task);

  // True when the work_queue_ is non-empty or the loop is exiting.
  // Called from mu_.Await().
  bool WorkAvailable() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  mutable absl::Mutex mu_;
  // Used to make sure only one loop is running at a time.  This
  // serves as something of a mutex around the running closures, but
  // it's actually the user's responsibility to ensure that only one
  // thread tries to run closures at a time, so this LOG(DFATAL)s if
  // there's a collision.
  bool loop_running_ ABSL_GUARDED_BY(mu_);
  // True when MakeLoopExit() has been called, and no Loop() call
  // has exited due to that call yet.
  bool loop_should_exit_ ABSL_GUARDED_BY(mu_);
  // True while a thread is blocked in Loop() waiting for a closure
  // to be added.
  bool ready_to_run_ ABSL_GUARDED_BY(mu_);
  // The list may not contain NULL closures.
  std::deque<Closure*> work_queue_ ABSL_GUARDED_BY(mu_);
  // Provides a shutdown-safe implementation of AddAfter().
  thread::AddAfterHelper add_after_helper_;
};

}  // namespace concurrent

#endif  // THIRD_PARTY_GLOOP_CONCURRENT_EXECUTORS_LOOP_EXECUTOR_H_
