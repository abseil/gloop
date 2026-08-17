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

#include "gloop/thread/periodicclosure.h"

#include <cstdint>
#include <functional>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock_interface.h"
#include "absl/time/time.h"
#include "absl/types/source_location.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/wait_state.h"

namespace thread {

PeriodicClosureOptions::PeriodicClosureOptions()
    : clock_(&absl::Clock::GetRealClock()),
      startup_delay_(absl::ZeroDuration()) {}

PeriodicClosure::PeriodicClosure(absl::AnyInvocable<void()> fun,
                                 absl::Duration interval,
                                 const PeriodicClosureOptions& options)
    : interval_(interval), callback_(std::move(fun)), options_(options) {
  CHECK_GE(interval, absl::ZeroDuration())
      << " The value of 'interval' should be >= 0";
}

PeriodicClosure::~PeriodicClosure() {
  CHECK(thread_ == nullptr) << "must be Stop()'d before destructed";
}

void PeriodicClosure::Start(absl::SourceLocation loc) {
  absl::MutexLock lock(mutex_);

  CHECK(thread_ == nullptr) << "already running";

  // Record the starting time here instead of in RunLoop.  That way, if there
  // is a delay starting RunLoop, that does not affect the timing of the first
  // closure.  (Such a delay can often happen in tests where the test simulates
  // a large time delay immediately after calling Start.)
  auto c = absl::bind_front(&PeriodicClosure::RunLoop, this,
                            options_.clock()->TimeNow());
  thread_ =
      new ClosureThread(options_.thread_options(), options_.name_prefix(), c);

  thread_->SetJoinable(true);
  thread_->Start(loc);
}

void PeriodicClosure::ForceRunInternal(bool blocking) {
  absl::MutexLock lock(mutex_);
  CHECK(thread_ != nullptr) << "PeriodicClosure not Start()'d";

  // A run is forced whenever forced_run_ > started_runs_
  forced_run_ = started_runs_ + 1;

  if (!blocking) return;

  // Wait for our internal thread to "signal" that its done.
  std::function<bool()> c =
      absl::bind_front(&PeriodicClosure::ForceRunDone, this, forced_run_);
  mutex_.Await(absl::Condition(&c));
}

// Used with Condition() in RunLoop() to wait for either a call to Stop(),
// which will set thread_ == nullptr, or a forced run which we haven't executed
// yet.
// L >= mutex_
bool PeriodicClosure::QuitOrForceRun() const {
  return thread_ == nullptr || forced_run_ > started_runs_;
}

// Used with Condition() to wait for a given "target_run" to complete.
// L >= mutex_
bool PeriodicClosure::ForceRunDone(int64_t target_run) const {
  CHECK(thread_ != nullptr)
      << "PeriodicClosure stopped while RunNow() was waiting";
  return finished_runs_ >= target_run;
}

void PeriodicClosure::Stop() {
  Thread* internal_thread = nullptr;

  {
    absl::MutexLock lock(mutex_);
    CHECK(thread_ != nullptr) << "not running";

    // signal QuitOrForceRun() that we are stopping
    internal_thread = thread_;
    thread_ = nullptr;
  }

  // wait for the closure to complete and clean up
  internal_thread->Join();
  delete internal_thread;
}

void PeriodicClosure::RunLoop(absl::Time start) {
  absl::MutexLock lock(mutex_);

  if (options_.startup_delay() > absl::ZeroDuration()) {
    absl::Condition cond =
        absl::Condition(this, &PeriodicClosure::QuitOrForceRun);
    const absl::Time deadline = start + options_.startup_delay();

    WaitStateScope scope(WaitStateScope::WaitState::kWaitingForWork);
    options_.clock()->AwaitWithDeadline(&mutex_, cond, deadline);
  }

  while (thread_ != nullptr) {
    started_runs_++;  // make note that we are starting a run now

    // don't hold the lock while we are running the closure
    mutex_.unlock();

    DVLOG(3) << "Running closure";
    absl::Time begin = options_.clock()->TimeNow();
    callback_();

    // The deadline is relative to when the last closure started.
    mutex_.lock();
    absl::Time deadline = begin + interval_;

    // Record the details of this run.
    finished_runs_++;

    // We want to sleep until 'deadline' but to allow Stop() to
    // instantly quit and RunNow() to trigger a run.
    absl::Condition cond =
        absl::Condition(this, &PeriodicClosure::QuitOrForceRun);

    WaitStateScope scope(WaitStateScope::WaitState::kWaitingForWork);
    options_.clock()->AwaitWithDeadline(&mutex_, cond, deadline);
  }
}

}  // namespace thread
