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

#include "gloop/thread/simple_weighted_semaphore.h"

#include <cstdint>

#include "absl/log/check.h"
#include "absl/log/die_if_null.h"
#include "absl/synchronization/mutex.h"
#include "gloop/base/timer.h"

namespace thread {

SimpleWeightedSemaphore::SimpleWeightedSemaphore(uint64_t max_concurrent_cost)
    : max_cost_(max_concurrent_cost) {}

SimpleWeightedSemaphore::~SimpleWeightedSemaphore() {
  // In retrospect, it would have been a better idea to not call Stop() in the
  // destructor at all, and have people do so explicitly if they wanted to. That
  // way we wouldn't need this stop_on_exit nonsense. Ah, hindsight. --zunger
  if (stop_on_exit_) Stop();
}

// The caller already holds pending_operations_lock_.
bool SimpleWeightedSemaphore::CanAcquire(uint64_t cost) const {
  pending_operations_lock_.AssertHeld();

  // We check for overflow here.  The maximum possible max_cost_ is kuint64max,
  // so if pending_cost_ + cost overflows, we know we can't acquire.
  uint64_t new_cost = pending_cost_ + cost;
  return (new_cost <= max_cost_ && new_cost >= pending_cost_);
}

void SimpleWeightedSemaphore::Stop(uint64_t* msec) {
  CycleTimer ct;
  ct.Start();
  {
    absl::MutexLock l(pending_operations_lock_);
    // Mark that we're in the middle of a stop. If another thread calls
    // Stop now, it'll just continue to mark that a stop is in progress,
    // so this is not a problem; but as long as this is true, no call to
    // Start will finish.
    stopping_ = true;

    // Forbid new acquires
    stopped_ = true;
    // Abort pending acquires
    acquire_changed_.SignalAll();
    // Wait for all pending operations to finish
    while (pending_cost_ > 0) no_pending_ops_.Wait(&pending_operations_lock_);

    // If anyone was waiting for the stop to finish, let them know. Again,
    // if two Stops are running in parallel, both of them will set stopping_
    // to false in rapid succession, which is not a problem.
    stopping_ = false;
    stop_finished_.SignalAll();
  }
  if (msec != nullptr) {
    *msec = ct.GetInMs();
  }
}

void SimpleWeightedSemaphore::Start() {
  absl::MutexLock l(pending_operations_lock_);
  while (stopping_) stop_finished_.Wait(&pending_operations_lock_);
  stopped_ = false;
  started_.SignalAll();
}

void SimpleWeightedSemaphore::set_max_cost(uint64_t max_cost) {
  absl::MutexLock l(pending_operations_lock_);
  max_cost_ = max_cost;
  acquire_changed_.SignalAll();
}

bool SimpleWeightedSemaphore::Acquire(uint64_t cost, uint64_t* msec) {
  CycleTimer ct;
  bool success = true;
  {
    absl::MutexLock l(pending_operations_lock_);
    if (stopped_) return false;

    if (cost == 0) {
      if (msec != nullptr) *msec = 0;
      return true;
    }

    // Wait for enough resources to become available
    ct.Start();
    while (!CanAcquire(cost) && !stopped_)
      acquire_changed_.Wait(&pending_operations_lock_);

    // We may have been stopped during the wait. Only actually acquire the
    // resources if we weren't.
    if (stopped_) {
      success = false;
    } else {
      pending_cost_ += cost;
      // Should be guaranteed by CanAcquire().
      CHECK_GE(pending_cost_, cost);
    }
  }
  if (msec != nullptr) {
    *msec = ct.GetInMs();
  }
  return success;
}

void SimpleWeightedSemaphore::AcquireAlways(uint64_t cost, uint64_t* msec) {
  CycleTimer ct;
  {
    absl::MutexLock l(pending_operations_lock_);

    do {
      // Wait for us to be running
      while (stopped_) started_.Wait(&pending_operations_lock_);

      if (cost == 0) {
        if (msec != nullptr) *msec = 0;
        return;
      }

      // Wait for enough resources to become available
      ct.Restart();
      while (!CanAcquire(cost) && !stopped_)
        acquire_changed_.Wait(&pending_operations_lock_);

      // We may have been stopped during the wait. Only actually acquire the
      // resources if we weren't.
    } while (stopped_);

    pending_cost_ += cost;

    // Should be guaranteed by CanAcquire().
    CHECK_GE(pending_cost_, cost);
  }

  if (msec != nullptr) {
    *msec = ct.GetInMs();
  }
}

bool SimpleWeightedSemaphore::TryAcquire(uint64_t cost) {
  absl::MutexLock l(pending_operations_lock_);
  // Just try once; no looping or waiting for anything.
  if (!stopped_ && CanAcquire(cost)) {
    pending_cost_ += cost;
    CHECK_GE(pending_cost_, cost);
    return true;
  }
  return false;
}

void SimpleWeightedSemaphore::Release(uint64_t cost) {
  if (cost == 0) return;
  absl::MutexLock l(pending_operations_lock_);
  // If someone tries to release more cost than the total pending, something
  // is seriously wrong
  CHECK_LE(cost, pending_cost_);
  pending_cost_ -= cost;
  acquire_changed_.SignalAll();
  if (pending_cost_ == 0) no_pending_ops_.SignalAll();
}

SimpleWeightedSemaphoreLock::SimpleWeightedSemaphoreLock(
    SimpleWeightedSemaphore* semaphore, uint64_t cost)
    : semaphore_(ABSL_DIE_IF_NULL(semaphore)), cost_(cost) {
  semaphore_->AcquireAlways(cost_);
}

SimpleWeightedSemaphoreLock::~SimpleWeightedSemaphoreLock() {
  semaphore_->Release(cost_);
}

}  // namespace thread
