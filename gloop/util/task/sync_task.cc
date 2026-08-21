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

#include "gloop/util/task/sync_task.h"

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/context.h"
#include "gloop/thread/config.h"
#include "gloop/thread/executor.h"
#include "gloop/util/task/task.h"
#include "google/protobuf/arena.h"

#if THREAD_HAVE_FIBER
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/select.h"
#endif

namespace util {

SyncTask::SyncTask(thread::Executor* absl_nonnull executor,
                   google::protobuf::Arena* absl_nullable arena) {
  state_.emplace(executor, arena);
}

SyncTask::SyncTask(WithBackgroundContextType, thread::Executor* executor,
                   google::protobuf::Arena* arena) {
  state_.emplace(executor, arena);
  task()->set_context(base::BackgroundContext());
}

bool SyncTask::IsDone() const {
  return state_.has_value() && state_->done.HasBeenNotified();
}

void SyncTask::Reset() {
  // Save a reference to the executor and arena before we destroy the task
  // below.
  thread::Executor* const executor = task()->executor();
  google::protobuf::Arena* const arena = task()->arena();

  // Re-initialize in the same manner as in the constructor.
  state_.emplace(executor, arena);
}

void SyncTask::Cancel() { task()->Cancel(); }

#if THREAD_HAVE_FIBER

void SyncTask::WaitIgnoresCancel() const { thread::Select({OnDone()}); }

bool SyncTask::WaitIgnoresCancelWithTimeout(absl::Duration duration) const {
  return thread::SelectUntil(absl::Now() + duration, {OnDone()}) == 0;
}

absl::Status SyncTask::Wait() const {
  if (thread::Select({thread::OnCancel(), OnDone()}) == 0) {
    task()->Cancel();  // Forward cancellation, must still wait.
    thread::Select({OnDone()});
  }
  return task()->status();
}

bool SyncTask::WaitWithTimeout(absl::Duration duration) const {
  absl::Time deadline(absl::Now() + duration);
  int result = thread::SelectUntil(deadline, {thread::OnCancel(), OnDone()});
  if (result == 0) {
    task()->Cancel();  // Forward cancellation, must still wait.
    result = thread::SelectUntil(deadline, {OnDone()});
  }

  return result != -1;
}

#else  // !THREAD_HAVE_FIBER

// There is no cancel API if there are no fibers, so it is ignored by omission.
void SyncTask::WaitIgnoresCancel() const {
  if (state_.has_value()) {
    state_->done.WaitForNotification();
  }
}

bool SyncTask::WaitIgnoresCancelWithTimeout(absl::Duration duration) const {
  return WaitWithTimeout(duration);
}

absl::Status SyncTask::Wait() const {
  if (state_.has_value()) {
    state_->done.WaitForNotification();
    return task()->status();
  }
  return absl::InternalError("SyncTask state uninitialized");
}

bool SyncTask::WaitWithTimeout(absl::Duration duration) const {
  return state_.has_value() &&
         state_->done.WaitForNotificationWithTimeout(duration);
}

#endif  // !THREAD_HAVE_FIBER

}  // namespace util
