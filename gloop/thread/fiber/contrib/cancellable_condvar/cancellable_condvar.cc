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

// Implementation for CancellableCondVar; a cancellable condition variable.

#include "gloop/thread/fiber/contrib/cancellable_condvar/cancellable_condvar.h"

#include <climits>

#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/select.h"
#include "gloop/thread/fiber/selectables.h"

namespace thread {

// CancellableCondVar implementation details.

// A CancellableCondVar is implemented as a list of Waiter structs protected by
// a spinlock (see .h file).  Each Waiter struct is allocated in the stack
// frame of a thread calling one of the Wait calls, and contains a
// PermanentEvent that is notified when the thread is awoken.  To ensure that
// the stack frame containing the waiter has not been deallocated before the
// PermanentEvent::Notify() completes, the spinlock is held when Notify() is
// called, and the waiter always reacquires the spinlock after the wait.  This
// may reduce performance under heavy load, but simplifies the implementation.

// ---- The condition variable's private methods.

// Enqueue a local Waiter on this->waiters_; unlock *mu; wait until deadline,
// cancellation, or until the Waiter's event is notified; reacquire *mu;, and
// return whether the deadline expired.
bool CancellableCondVar::InternalWait(absl::Mutex* mu, absl::Time deadline,
                                      thread::Case canceller) {
  Waiter waiter;
  mu->AssertHeld();
  this->spinlock_.lock();  // waiter must be enqueued before *mu is unlocked
  this->waiters_.push_back(&waiter);
  this->spinlock_.unlock();
  mu->unlock();
  int selected =  // -1=>timeout; 0=>wakeup; 1=>cancelled
      thread::SelectUntil(deadline, {waiter.event.OnEvent(), canceller});
  // As a side-effect, this locked section forces signallers to complete their
  // Notify() calls (also under spinlock_) before "waiter" is deallocated.
  this->spinlock_.lock();
  if (this->waiters_.is_linked(&waiter)) {  // happens for timeout/cancellation.
    this->waiters_.erase(&waiter);
  }
  this->spinlock_.unlock();
  mu->lock();
  return selected == -1;  // Whether SelectUntil() timed out.
}

// Signal *this up to max_to_wake times.   This implements Signal() if
// max_to_wake is 1, and SignalAll() if max_to_wake is INT_MAX.
void CancellableCondVar::InternalSignal(int max_to_wake) {
  this->spinlock_.lock();
  for (int i = 0; i != max_to_wake && !this->waiters_.empty(); i++) {
    Waiter* to_wake = &this->waiters_.front();
    this->waiters_.erase(to_wake);
    // The Notify() calls must precede the release of spinlock_ so the Waiter
    // in the waiting thread's stack frame persists while Notify() completes.
    to_wake->event.Notify();
  }
  this->spinlock_.unlock();
}

// ---- The condition variable's public methods.

CancellableCondVar::CancellableCondVar() = default;

bool CancellableCondVar::CancellableWaitWithDeadline(absl::Mutex* mu,
                                                     absl::Time deadline) {
  return this->InternalWait(mu, deadline, thread::OnCancel());
}

void CancellableCondVar::CancellableWait(absl::Mutex* mu) {
  this->InternalWait(mu, absl::InfiniteFuture(), thread::OnCancel());
}

bool CancellableCondVar::WaitWithDeadline(absl::Mutex* mu,
                                          absl::Time deadline) {
  return this->InternalWait(mu, deadline, thread::NonSelectableCase());
}

void CancellableCondVar::Wait(absl::Mutex* mu) {
  this->InternalWait(mu, absl::InfiniteFuture(), thread::NonSelectableCase());
}

void CancellableCondVar::Signal() { this->InternalSignal(1); }

void CancellableCondVar::SignalAll() { this->InternalSignal(INT_MAX); }

}  // namespace thread
