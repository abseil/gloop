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

#include "gloop/util/task/sleep.h"

#include <stddef.h>

#include <algorithm>

#include "absl/functional/bind_front.h"
#include "absl/status/status.h"
#include "absl/time/time.h"
#include "gloop/base/walltime.h"
#include "gloop/thread/executor.h"
#include "gloop/thread/thread_manager.h"
#include "gloop/thread/timedcall.h"
#include "gloop/util/task/task.h"

namespace util {

class SleepUntilImpl {
 public:
  static void FinishSleep(void* t) {
    Task* task = reinterpret_cast<Task*>(t);
    if (thread::InlineExecutorInternal::IsInlineExecutor(task->executor()) ||
        task->inline_done_callback()) {
      // If Return() would run inline, we send it to the default ThreadManager,
      // because the current thread is the TimedCall thread, which should not
      // execute anything long.
      task->AddHold();
      thread::DefaultQueue()->Schedule([task] {
        task->Return();
        task->RemoveHold();
      });
    } else {
      task->Return();
    }
  }
};

static void CancelSleep(Task* task, TimedCall* tc) {
  tc->Set(TimedCall::Stop, nullptr);  // Cancel timer immediately

  // Note that Stop will not return until the FinishSleep
  // call is cancelled or complete.
  // As such it is safe to call Return unconditionally
  task->Return(absl::CancelledError());
}

void SleepUntil(absl::Time deadline, Task* task) {
  TimedCall* tc = new TimedCall;

  TaskHold h(task);  // don't complete before we set the cancellation cb
  task->DeleteWhenDone(tc);
  // Require the deadline be at least 1 second past the Unix epoch.
  // TimedCall will not call the function if deadline <= 0.
  deadline = std::max(absl::FromUnixSeconds(1), deadline);
  tc->Set(base::ToWallTime(deadline),
          absl::bind_front(&SleepUntilImpl::FinishSleep, task));
  task->WhenCancelled(absl::bind_front(CancelSleep, task, tc));
}

}  // namespace util
