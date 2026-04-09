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

#include "gloop/thread/add_after_helper.h"

#include <cstdint>
#include <utility>

#include "absl/base/casts.h"
#include "absl/functional/any_invocable.h"
#include "absl/functional/bind_front.h"
#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/callback.h"
#include "gloop/base/walltime.h"
#include "gloop/thread/executor.h"
#include "gloop/thread/timedcall.h"
#include "gloop/util/callback/cancellable_closure.h"
#include "gloop/util/functional/from_callback.h"
#include "gloop/util/functional/to_callback.h"

namespace thread {

using util::callback::CancellableClosure;

namespace {

// Since there's only one TimedCall thread, it only makes sense to
// have one instance of this type.  The most important part of the
// implementation is AddAfter().
class TimedCallExecutor : public Executor {
 public:
  void Schedule(absl::AnyInvocable<void() &&> task) override {
    ScheduleAt(absl::Now(), std::move(task));
  }
  bool TrySchedule(absl::AnyInvocable<void() &&> task) override {
    ScheduleAt(absl::Now(), std::move(task));
    return true;
  }
  void ScheduleAfterForMigration(absl::Duration delay,
                                 absl::AnyInvocable<void() &&> task) override {
    TimedCall::RunAt(base::ToWallTime(absl::Now() + delay), std::move(task));
  }
  void ScheduleAt(absl::Time when,
                  absl::AnyInvocable<void() &&> task) override {
    TimedCall::RunAt(base::ToWallTime(when), std::move(task));
  }
  int num_pending_closures() const override { return 0; }
};

TimedCallExecutor* SingletonTimedCallExecutor() {
  static auto* const singleton = new TimedCallExecutor();
  return singleton;
}

}  // namespace

AddAfterHelper::AddAfterHelper(
    Executor* underlying_executor,
    absl::AnyInvocable<void(absl::AnyInvocable<void() &&>)> complete_add_after)
    : underlying_executor_(underlying_executor == nullptr
                               ? SingletonTimedCallExecutor()
                               : underlying_executor),
      complete_add_after_(std::move(complete_add_after)),
      next_sequence_number_(0),
      shutting_down_(false),
      add_afters_() {}

AddAfterHelper::~AddAfterHelper() {
  if (!shutting_down_) {
    ShutdownAndRunPendingImmediately();
  }
  if (!add_afters_.empty()) {
    LOG(DFATAL) << "Something got through"
                << " AddAfterHelper::ScheduleAddAfter()"
                << " after executor shut down.";
    // This will probably crash later in production, but there's a
    // chance it won't, so just leave the error message and continue.
  }
}

CancellableClosure* AddAfterHelper::AddTaskForCompletion(
    absl::AnyInvocable<void() &&> task) {
  absl::MutexLock l(mu_);
  if (shutting_down_) {
    // Don't schedule any calls from outside this object after it has
    // been shut down.  TODO: We may need a way to configure
    // what happens to a task after shutdown.  See, for example,
    // java.util.concurrent.RejectedExecutionHandler.
    return nullptr;
  }

  const int64_t sequence_num = ++next_sequence_number_;
  CancellableClosure* cancellable =
      CancellableClosure::New(::util::functional::ToCallback(
          absl::bind_front(&AddAfterHelper::CompleteAndRemoveFromMap, this,
                           sequence_num, std::move(task))));
  if (!add_afters_.insert_or_assign(sequence_num, cancellable).second) {
    LOG(DFATAL) << "Sequence number re-used. AddAfterHelper may"
                << " crash if it's deleted too soon after this.";
  }

  return cancellable;
}

void AddAfterHelper::ScheduleAddAfter(absl::Duration delay, Closure* task) {
  absl::AnyInvocable<void() &&> callback;
  if (task->IsRepeatable()) {
    // Don't take ownership if this is a permanent callback.
    callback = util::functional::FromCallback(task);
  } else {
    callback = util::functional::FromCallbackWithOwnership(task);
  }
  CancellableClosure* cancellable = AddTaskForCompletion(std::move(callback));

  if (cancellable) {
    // This AddAfter() call has to be after inserting cancellable into
    // the map because CompleteAndRemoveFromMap tries to remove the
    // entry from the map.
    underlying_executor_->ScheduleAfterForMigration(
        delay,
        util::functional::FromCallbackWithOwnership<Closure>(cancellable));
  }
}

void AddAfterHelper::ScheduleAddAfterAt(
    absl::Time when, absl::AnyInvocable<void() &&> callback) {
  CancellableClosure* cancellable = AddTaskForCompletion(std::move(callback));

  if (cancellable) {
    // This ScheduleAt() call has to be after inserting cancellable into
    // the map because CompleteAndRemoveFromMap tries to remove the
    // entry from the map.
    underlying_executor_->ScheduleAt(
        when, util::functional::FromCallbackWithOwnership(
                  absl::implicit_cast<Closure*>(cancellable)));
  }
}

void AddAfterHelper::CompleteAndRemoveFromMap(
    int64_t sequence_number, absl::AnyInvocable<void() &&> task) {
  complete_add_after_(std::move(task));

  // The order is important here. If complete_add_after_->Run() comes
  // after the entry is erased from the map, it may still be running
  // after Shutdown...() returns.

  absl::MutexLock l(mu_);
  auto entry = add_afters_.find(sequence_number);
  // entry could be end() if Shutdown...() is running concurrently
  // and trying to clean up AddAfter calls.
  if (entry != add_afters_.end()) {
    entry->second->Unref();
    add_afters_.erase(entry);
  }
}

void AddAfterHelper::ShutdownAndRunPendingImmediately() {
  // We need to cancel any scheduled AddAfter() calls and complete
  // them immediately.
  decltype(add_afters_) local_add_afters;
  {
    absl::MutexLock l(mu_);
    // Tells AddAfter() not to accept any more tasks.
    shutting_down_ = true;
    local_add_afters.swap(add_afters_);
  }
  // It's important to do this unlocked because if
  // CompleteAndRemoveFromMap() is already running,
  // CancellableClosure::WaitUntil() will block waiting for it to
  // finish, and it needs to be able to acquire the mutex to finish.
  for (auto& p : local_add_afters) {
    CancellableClosure* closure = p.second;
    // We want to run CompleteAndRemoveFromMap() as soon as possible
    // and wait for it to be finished.  kRunInCaller does that.
    closure->WaitUntil(CancellableClosure::kForever,
                       CancellableClosure::kRunInCaller);
    closure->Unref();
  }
}

bool AddAfterHelper::IsShuttingDown() const {
  absl::MutexLock l(mu_);
  return shutting_down_;
}

}  // namespace thread
