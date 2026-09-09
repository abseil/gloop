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

#ifndef THIRD_PARTY_GLOOP_THREAD_FIBER_CONTRIB_CANCELLABLE_CONDVAR_CANCELLABLE_CONDVAR_H_
#define THIRD_PARTY_GLOOP_THREAD_FIBER_CONTRIB_CANCELLABLE_CONDVAR_CANCELLABLE_CONDVAR_H_

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gloop/base/spinlock.h"
#include "gloop/thread/fiber/select.h"
#include "gloop/thread/fiber/selectables.h"
#include "gloop/util/gtl/intrusive_list.h"

namespace thread {

// A CancellableCondVar is a Mesa-style condition variable.  It is similar to
// an absl::CondVar (q.v.) except that each wait site optionally can be allowed
// to return on fiber cancellation.  The client should use thread::Cancelled()
// to determine whether the fiber was cancelled.
//
// Example:
// Assume boolean expression "some_condition" is protected by mu, and that
// CancellableCondVar cv is signalled when "some_condition" becomes true.  A
// fiber wishing to wait for "some_condition", or a deadline, or fiber
// cancellation would do:
//
//   mu.Lock();
//   ...
//   bool expired = false;
//   while (!some_condition && !expired && !thread::Cancelled()) {
//     expired = cv.CancellableWaitWithDeadline(&mu, deadline);
//   }
//   if (expired) { /* deadline expired */ }
//   else if (thread::Cancelled()) { /* fiber has been cancelled */ }
//   else { /* some_condition is true */ }
//   ...
//   mu.Unlock();
//
// A thread making "some_condition" true would do:
//  mu.Lock();
//  ...somehow make some_condition true...
//  cv.SignalAll();
//  mu.Unlock();
//
// If the Wait*() call permits cancellation (has Cancellable in its name), it
// is important for the while loop to check thread::Cancelled() (and exit), or
// the loop will spin indefinitely.
class CancellableCondVar {
 public:
  CancellableCondVar();
  ~CancellableCondVar() = default;

  // Atomically release *mu and block on *this until woken by one of:
  //   - a call to this->Signal() or this->SignalAll(),
  //   - cancellation of the calling fiber,
  //   - deadline expiry, or
  //   - a spurious wakeup (rare, but allowed for faster scheduling choices).
  // Then reacquire *mu and return whether the deadline expired.
  //
  // Requires and ensures that the caller holds *mu in writer mode.
  // See typical usage above.
  bool CancellableWaitWithDeadline(absl::Mutex* mu, absl::Time deadline)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*mu);

  // Equivalent to CancellableWaitWithDeadline(mu, absl::InfiniteFuture()).
  void CancellableWait(absl::Mutex* mu) ABSL_EXCLUSIVE_LOCKS_REQUIRED(*mu);

  // Equivalent to CancellableWaitWithDeadline(mu, deadline), except
  // that this call ignores cancellations.
  bool WaitWithDeadline(absl::Mutex* mu, absl::Time deadline)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(*mu);

  // Equivalent to WaitWithDeadline(mu, absl::InfiniteFuture()).
  void Wait(absl::Mutex* mu) ABSL_EXCLUSIVE_LOCKS_REQUIRED(*mu);

  // Wake at least one waiter, if there is one.
  void Signal();

  // Wake all waiters.
  void SignalAll();

 private:
  SpinLock spinlock_;
  struct Waiter : public gtl::intrusive_link<Waiter> {
    thread::PermanentEvent event;
  };
  gtl::intrusive_list<Waiter> waiters_ ABSL_GUARDED_BY(spinlock_);
  bool InternalWait(absl::Mutex* mu, absl::Time deadline,
                    thread::Case canceller) ABSL_EXCLUSIVE_LOCKS_REQUIRED(*mu);
  void InternalSignal(int max_to_wake);
  CancellableCondVar(const CancellableCondVar&) = delete;
  CancellableCondVar& operator=(const CancellableCondVar&) = delete;
};

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_FIBER_CONTRIB_CANCELLABLE_CONDVAR_CANCELLABLE_CONDVAR_H_
