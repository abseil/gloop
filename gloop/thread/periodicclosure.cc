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

#include "gloop/thread/periodicclosure.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock_interface.h"
#include "absl/time/time.h"
#include "absl/types/source_location.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"
#include "gloop/thread/wait_state.h"

namespace thread {
namespace {

std::string ThreadNameFromSourceLocation(absl::SourceLocation loc) {
  std::string file_name = std::string(loc.file_name());
  // Erase file extension.
  size_t dot = file_name.rfind('.');
  if (dot != std::string::npos) file_name.erase(dot);
  return thread::SanitizeThreadNamePrefix(std::move(file_name));
}

}  // namespace

PeriodicClosureOptions::PeriodicClosureOptions(absl::SourceLocation loc)
    : name_prefix_(ThreadNameFromSourceLocation(loc)),
      clock_(&absl::Clock::GetRealClock()),
      startup_delay_(absl::ZeroDuration()),
      force_exclusive_thread_(false) {}

class ThreadPeriodicClosure final : public PeriodicClosure::Impl {
 public:
  ThreadPeriodicClosure(
      absl::AnyInvocable<void()> fun, absl::Duration interval,
      const PeriodicClosureOptions& options = PeriodicClosureOptions());

  ~ThreadPeriodicClosure() override;

  void Start() override;

  void RunNow() override { ForceRunInternal(true); }

  void RunSoon() override { ForceRunInternal(false); }

  void Stop() override;

  absl::Duration Interval() const override {
    absl::MutexLock l(mutex_);
    return interval_;
  }

  void SetInterval(absl::Duration interval) override {
    absl::MutexLock l(mutex_);
    interval_ = interval;
  }

 private:
  void RunLoop(absl::Time start) ABSL_LOCKS_EXCLUDED(mutex_);

  // Multiplexing RunNow and RunSoon to share code
  void ForceRunInternal(bool blocking) ABSL_LOCKS_EXCLUDED(mutex_);

  // Conditions used to signal between ForceRun()/Stop() and RunLoop().
  bool QuitOrForceRun() const ABSL_SHARED_LOCKS_REQUIRED(mutex_);
  bool ForceRunDone(int64_t target_run) const
      ABSL_SHARED_LOCKS_REQUIRED(mutex_);

  // Protects state below.
  mutable absl::Mutex mutex_;

  // How many runs we've finished.
  int64_t finished_runs_ ABSL_GUARDED_BY(mutex_) = 0;
  // How many runs we've started.
  int64_t started_runs_ ABSL_GUARDED_BY(mutex_) = 0;
  // Account for calls to RunNow().
  int64_t forced_run_ ABSL_GUARDED_BY(mutex_) = 0;

  // Thread for running "closure_"
  Thread* thread_ ABSL_GUARDED_BY(mutex_) = nullptr;
  absl::Duration interval_ ABSL_GUARDED_BY(mutex_);  // Interval between calls
  absl::AnyInvocable<void()> callback_;              // Actual client closure
  const PeriodicClosureOptions options_;
};

ThreadPeriodicClosure::ThreadPeriodicClosure(
    absl::AnyInvocable<void()> fun, absl::Duration interval,
    const PeriodicClosureOptions& options)
    : interval_(interval), callback_(std::move(fun)), options_(options) {
  CHECK_GE(interval, absl::ZeroDuration())
      << " The value of 'interval' should be >= 0";
}

ThreadPeriodicClosure::~ThreadPeriodicClosure() {
  CHECK(thread_ == nullptr) << "must be Stop()'d before destructed";
}

void ThreadPeriodicClosure::Start() {
  absl::MutexLock lock(mutex_);

  CHECK(thread_ == nullptr) << "already running";

  // Record the starting time here instead of in RunLoop.  That way, if there
  // is a delay starting RunLoop, that does not affect the timing of the first
  // closure.  (Such a delay can often happen in tests where the test simulates
  // a large time delay immediately after calling Start.)
  auto c = absl::bind_front(&ThreadPeriodicClosure::RunLoop, this,
                            options_.clock()->TimeNow());
  thread_ = new ClosureThread(thread::Options(), options_.name_prefix(), c);

  thread_->SetJoinable(true);
  thread_->Start();
}

void ThreadPeriodicClosure::ForceRunInternal(bool blocking) {
  absl::MutexLock lock(mutex_);
  CHECK(thread_ != nullptr) << "PeriodicClosure not Start()'d";

  // A run is forced whenever forced_run_ > started_runs_
  forced_run_ = started_runs_ + 1;

  if (!blocking) return;

  // Wait for our internal thread to "signal" that its done.
  std::function<bool()> c =
      absl::bind_front(&ThreadPeriodicClosure::ForceRunDone, this, forced_run_);
  mutex_.Await(absl::Condition(&c));
}

// Used with Condition() in RunLoop() to wait for either a call to Stop(),
// which will set thread_ == nullptr, or a forced run which we haven't executed
// yet.
// L >= mutex_
bool ThreadPeriodicClosure::QuitOrForceRun() const {
  return thread_ == nullptr || forced_run_ > started_runs_;
}

// Used with Condition() to wait for a given "target_run" to complete.
// L >= mutex_
bool ThreadPeriodicClosure::ForceRunDone(int64_t target_run) const {
  CHECK(thread_ != nullptr)
      << "PeriodicClosure stopped while RunNow() was waiting";
  return finished_runs_ >= target_run;
}

void ThreadPeriodicClosure::Stop() {
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

void ThreadPeriodicClosure::RunLoop(absl::Time start) {
  absl::MutexLock lock(mutex_);

  if (options_.startup_delay() > absl::ZeroDuration()) {
    absl::Condition cond =
        absl::Condition(this, &ThreadPeriodicClosure::QuitOrForceRun);
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
        absl::Condition(this, &ThreadPeriodicClosure::QuitOrForceRun);

    WaitStateScope scope(WaitStateScope::WaitState::kWaitingForWork);
    options_.clock()->AwaitWithDeadline(&mutex_, cond, deadline);
  }
}

// Weak definition, overridden in eventmanager/eventmanager_default.cc.
ABSL_ATTRIBUTE_WEAK PeriodicClosure::Impl*
PeriodicClosure::CreateEventManagerPeriodicClosure(
    absl::AnyInvocable<void()>& fun, absl::Duration interval,
    const PeriodicClosureOptions& options) {
  return nullptr;
}

PeriodicClosure::PeriodicClosure(absl::AnyInvocable<void()> fun,
                                 absl::Duration interval,
                                 const PeriodicClosureOptions& options)
    : impl_([&]() -> Impl* {
        if (auto* impl =
                CreateEventManagerPeriodicClosure(fun, interval, options)) {
          return impl;
        }
        return new ThreadPeriodicClosure(std::move(fun), interval, options);
      }()) {}

}  // namespace thread
