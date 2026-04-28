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

#include "gloop/base/auxiliary/synchronization_profiling_test_util.h"

#include <atomic>
#include <memory>

#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace lock_profiling_test {

TestLockInterface* TestLockInterface::New(enum TestLockType lock_type) {
  switch (lock_type) {
    case kMutexLock:
      return new MutexTestLock();
    case kSpinLock:
      return new SpinTestLock();
    default:
      return nullptr;
  }
}

void LockHolder::CauseLockContention(int number_of_threads,
                                     absl::Duration hold_time) {
  // All threads mark their attempt to acquire the lock.  There is no way to
  // truly know that a thread is blocked on the lock, so this attempts to be
  // a close approximation.  A thread can still get scheduled out or
  // interrupted after this increment and before contending on the lock,
  // but that window is small, so the chance of a false positive on the
  // tests is greatly reduced.
  attempted_lock_acquisitions_.fetch_add(1, std::memory_order_acq_rel);
  test_lock_->Lock();

  // The first thread acquiring the lock will wait for all other threads to
  // try for the lock.  This is needed to guarantee as much as possible that
  // each thread starts contending for the lock before the test officially
  // starts.  Without this, an initial thread could execute the entire lock
  // before any other thread started contending which would lower the overall
  // contention percentage and fail checks for the amount of contention that
  // should have been generated.
  while (attempted_lock_acquisitions_.load(std::memory_order_relaxed) !=
         number_of_threads) {
    // busy wait
  }

  // Actually measure the hold time as it may be longer than requested on
  // systems with lots of other activity going on.  For example, the
  // actual sleep done while holding the lock may sleep for much longer than
  // the desired time period on a very busy system.
  absl::Time hold_time_start = absl::Now();
  absl::SleepFor(hold_time);
  lock_invocation_count_++;
  absl::Duration d = absl::Now() - hold_time_start;
  hold_time_ += d;

  if (lock_invocation_count_ != number_of_threads) {
    // Sum up the accumulated hold time
    //
    // |-----H1---------|
    // |----------------|-----H2-------|
    // |-------------------------------|---H3--------|
    // |---------------------------------------------|---H4-------|
    // ...
    //
    // hold_time_accumulation = H1 + (H1 + H2) + (H1 + H2 + H3) + ...
    // A CycleCounter accumulates the time for successive Start/Stops, so
    // just add in the hold_time_ each time through except for the last
    // thread as no other thread contends with the last thread.
    hold_and_wait_time_accumulation_ += hold_time_;
  }
  hold_time_accumulation_ += d;

  test_lock_->Unlock();
}

CallstackInterface* CallstackInterface::New(enum CallstackType callstack_type) {
  switch (callstack_type) {
    case kSimpleCallstack:
      return new SimpleCallstack();
    case kContextCallstack:
      return new ContextCallstack();
    case kComplexCallstack:
      return new ComplexCallstack();
    default:
      return nullptr;
  }
}

}  // namespace lock_profiling_test
