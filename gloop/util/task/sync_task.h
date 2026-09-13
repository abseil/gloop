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

// A SyncTask object supports synchronously calling code
// that requires a Task.  The object can be reused by
// calling Reset() between uses.
//
// Example: to call the asynchronous function FooBar
//
//   SyncTask sync;
//   FooBar(a, b, c, sync.task());
//   return sync.Wait();
//
// Example: periodically signal that we are still waiting for a task to be done.
//
//   SyncTask sync;
//   FooBar(a, b, c, sync.task());
//   while (!sync.WaitWithTimeout(10)) {
//     LOG(INFO) << "Task not done yet...";
//   }
//   return sync.status();
//
// Example: wait some time for FooBar then cancel the task.
//
//   SyncTask sync;
//   FooBar(a, b, c, sync.task());
//   sync.WaitWithTimeout(absl::Seconds(10));
//   sync.Cancel();
//   return sync.Wait(); // wait for the task to be in done state.
//
#ifndef UTIL_TASK_SYNC_TASK_H__
#define UTIL_TASK_SYNC_TASK_H__

#include <optional>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/time/time.h"
#include "gloop/thread/config.h"
#include "gloop/thread/executor.h"
#include "gloop/thread/fiber/select.h"
#include "gloop/util/task/task.h"
#include "google/protobuf/arena.h"

#if THREAD_HAVE_FIBER
#include "gloop/thread/fiber/selectables.h"
#else
#include "absl/synchronization/notification.h"
#endif  // ! THREAD_HAVE_FIBER

namespace util {

class SyncTask {
 public:
  // A SyncTask can be in two state:
  //      ACTIVE       -- this is the initial state
  //      DONE         -- the asynchronous task has completed
  // Create a new SyncTask in the ACTIVE state.
  // If the `executor` argument is provided, use that executor as the task's
  // executor. `executor` must not be nullptr. If the `arena` argument is
  // provided, it is passed to the constructor of the util::Task.
  explicit SyncTask(thread::Executor* absl_nonnull executor =
                        thread::Executor::DefaultExecutor(),
                    google::protobuf::Arena* absl_nullable arena = nullptr);

#ifndef SWIG
  // Typed argument for using background context. See below.
  struct WithBackgroundContextType {};
  static inline constexpr WithBackgroundContextType kWithBackgroundContext = {};

  // Overloaded constructor to create a SyncTask with a background context.
  // The default constructor captures a copy of the current thread's context
  // through its internal util::Task object, but an application may want to use
  // the default background context for long running SyncTask() instances, to
  // prevent long lived traces.
  // For example:
  //   `SyncTask sync_task(SyncTask::kWithBackgroundContext, ...)`
  explicit SyncTask(
      WithBackgroundContextType,
      thread::Executor* executor = thread::Executor::DefaultExecutor(),
      google::protobuf::Arena* arena = nullptr);

  // This type is neither copyable nor movable.
  SyncTask(const SyncTask&) = delete;
  SyncTask& operator=(const SyncTask&) = delete;
#endif

  // REQUIRES: in the DONE state.
  ~SyncTask() {}  // not = default; due to SWIG

  // Return the internal task object.  This can be
  // passed to the asynchronous call, which is responsible
  // for calling Task::Return at some point in the future.
  Task* task() const { return &state_->task; }

  // Return the status object for this sync task.
  // REQUIRES: IsDone() is true
  absl::Status status() const { return task()->status(); }

  // Returns true iff the task is in the DONE state.
  bool IsDone() const;

#if THREAD_HAVE_FIBER
  // When using this, use status() above once OnDone() is selected to get the
  // final task status.
  thread::Case OnDone() const { return state_->done.OnEvent(); }
#endif

  // Block until IsDone() is true.  Supports fiber cancellation, Cancel()
  // will be invoked automatically if this is called from a fiber that becomes
  // cancelled.
  //
  // Returns the status() of the task.
  absl::Status Wait() const;

  // Block until IsDone() is true or the given `duration` has elapsed.
  //
  // Returns true only if IsDone() becomes true (i.e. the underlying task
  // transitions to DONE).  Otherwise, returns false due to timeout; in
  // this case IsDone() may or may not be true and it is not safe to
  // destroy this SyncTask.
  //
  // Supports fiber cancellation, Cancel() will be invoked automatically if
  // this is called from a fiber that becomes cancelled.  Does not return
  // immediately due to this alone, but the underlying task may honor
  // cancellation and become DONE.
  //
  // When coordinating simultaneous SyncTask objects it may be preferable to
  // use OnDone() explicitly.  Example:
  //
  //   int result;
  //   absl::Time deadline(absl::Now() + duration);
  //   thread::CaseArray cases{thread::OnCancel(),
  //                           sync1.OnDone(),
  //                           sync2.OnDone()};
  //   int pending = cases.size() - 1;
  //   while (pending &&
  //          (result = thread::SelectUntil(deadline, cases)) != -1) {
  //     if (result == 0) {
  //       sync1.Cancel();
  //       sync2.Cancel();
  //     } else {
  //       --pending;
  //     }
  //     cases[result] = thread::NonSelectableCase();
  //   }
  //   if (result == -1) {
  //     ...  // timed out, handling non-done SyncTasks.
  //   } else {
  //     ...
  //   }
  //
  bool WaitWithTimeout(absl::Duration duration) const;

  // Wait for task to become done.  Ignores fiber cancellation.
  //
  // In general, unless you have a special need to ignore fiber cancellation,
  // you should prefer Wait() so that fiber cancellation will get propagated
  // to the task automatically.
  void WaitIgnoresCancel() const;

  // Wait for task to become done, or |duration| to pass.  Ignores fiber
  // cancellation.  Returns true if task becomes done.
  //
  // In general, unless you have a special need to ignore fiber cancellation,
  // you should prefer WaitWithTimeout() so that fiber cancellation will get
  // propagated to the task automatically.
  bool WaitIgnoresCancelWithTimeout(absl::Duration duration) const;

  // Resets the object so it can be used again.
  // REQUIRES: in the DONE state.
  void Reset();

  // If the task is still active, request the task to cancel
  // itself and return.  A no-op if IsDone() is true;
  // After Cancel returns, the task may have not finished
  // and therefore it is not safe to destroy this SyncTask.
  void Cancel();

 private:
#if THREAD_HAVE_FIBER
  typedef thread::PermanentEvent NotificationType;
#else
  typedef absl::Notification NotificationType;
#endif

#ifndef SWIG  // SWIG does not support nested structs
  struct State {
    explicit State(thread::Executor* executor, google::protobuf::Arena* arena)
        : task([&done = done](Task*) { done.Notify(); }, executor, arena) {
      task.set_inline_done_callback(true);
    }

    NotificationType done;
    mutable Task task;
  };
#endif
  // state_ is always populated, but is optional to allow reset.
  std::optional<State> state_;
};

}  // namespace util

#endif  // UTIL_TASK_SYNC_TASK_H__
