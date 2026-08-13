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

// External interfaces for interacting with user-level scheduling.  These
// interfaces are intended only for very low-level clients such as the
// implementations of synchronization primitives.  Users should prefer the
// Fibers interface which exposes these semantics automatically when available.
//
// See:
//   //gloop/thread/fiber/fiber.h

#ifndef THIRD_PARTY_GLOOP_BASE_SCHEDULING_DOWNCALLS_H_
#define THIRD_PARTY_GLOOP_BASE_SCHEDULING_DOWNCALLS_H_

#include <atomic>

#include "absl/base/config.h"
#include "absl/base/internal/thread_identity.h"
#include "absl/synchronization/internal/kernel_timeout.h"
#include "absl/synchronization/internal/per_thread_sem.h"

namespace thread {
class Fiber;
}  // namespace thread

// For Java support
namespace devtools_java_launcher {
class DowncallsProxy;
}  // namespace devtools_java_launcher

namespace base {
namespace scheduling {

class Schedulable;

class Downcalls {
 public:
  // Returns true if the calling thread is being managed by a domain and is
  // currently eligible for user-level scheduling.
  //
  // Returns false otherwise; this includes the case where the current thread
  // is co-operative, but scheduling has been temporarily disabled using
  // SchedulingGuard.
  static bool CurrentThreadIsCooperative();

  // Initiate a co-operative scheduling decision, rescheduling the (currently
  // running) caller.  The calling entity remains runnable and may be scheduled
  // again at any time.
  //
  // CAUTION: No invariants are defined about reselection; the calling activity
  // is still runnable and re-scheduling may always reselect it.  e.g.:
  //   while(1) { base::scheduling::Reschedule(); }
  // may potentially run indefinitely without running anything else.
  // TODO: consider returning 'bool' (against alternate selection).
  // REQUIRES: CurrentThreadIsCooperative() == true
  static void Reschedule();

  //----------------------------------------------------------------------------
  // End of public interfaces
  //----------------------------------------------------------------------------

  // Domain specific interfaces:
  // ---------------------------
  // The interfaces below may only be called by Domain implementations to
  // synchronize non user-level scheduler events (e.g. blocking syscall)
  // affecting a schedulable's runnability.

  // Record that "blocked" has blocked externally; potentially schedule a
  // replacement.
  //
  // If an kWorkItem schedulable may be scheduled to replace "blocked"; return
  // it.  Its execution must be resumed.  Else return null.
  //
  // REQUIRES: caller must be an implementation of Domain.
  // REQUIRES: "blocked" must have been running.
  // REQUIRES: when returned != null: domain must guarantee its resumption.
  static Schedulable* DomainObservedBlocking(Schedulable* blocked);

  // Record that "woken" has received an external wake-up and is again eligible
  // for scheduling.  Typically this function is the result of
  // re-synchronize on expiration of Domain::BlockCurrent(<timeout>).
  //
  // If this wake-up leads to additional concurrency, return a kWorkItem
  // schedulable whose execution must be resumed.  Else return null.
  //
  // Returns null if the Domain is already at maximum concurrency.
  // REQUIRES: caller must be an implementation of Domain.
  // REQUIRES: Must pair with previous DomainObservedBlocking(woken).
  // REQUIRES: when return != woken: "woken" must not run until scheduled.
  static Schedulable* DomainObservedWakeup(Schedulable* woken);

  // Record that "expired" is again runnable due to the expiration of a previous
  // SwapCurrent(..., timeout) or BlockCurrent(..., timeout) operation.
  //
  // If this wake-up leads to additional concurrency, return a kWorkItem
  // schedulable whose execution must be resumed.  Else return null.
  //
  // REQUIRES: caller must be an implementation of Domain.
  // REQUIRES: Expired outstanding {Swap, Block}Current(expired, ..., t).
  static Schedulable* DomainObservedTimeout(Schedulable* expired);
  // ----------------------------------
  // End of Domain specific interfaces.

  // Scheduler specific interfaces:
  // ------------------------------
  // Runs "function" in the calling thread context as soon as it can be
  // guaranteed not to collide with a (locally executing) parallel scheduling
  // operation.
  //
  // Used by schedulers to initiate co-operative operations which must avoid
  // nesting within their own pre-existing invocation.
  //
  // When multiple functions are queued, their execution order is not defined.
  //
  // Note that like Scheduler code, "function" will not be run in a context
  // where the current thread may cooperatively reschedule.  This means that it
  // is illegal to use a cooperative primitive such as Mutex.  For more details
  // see SchedulingGuard.
  typedef void (*QueueableFunction)(void*);
  static void RunWhenSchedulingAllowed(QueueableFunction function, void* arg);
  // -------------------------------------
  // End of Scheduler specific interfaces.

 private:
  Downcalls() {}  // May not be instantiated, only encapsulates static methods.

  // Each Schedulable has an associated semaphore (runnable_count) manipulated
  // by Post() and Wait() calls.
  //
  // A Schedulable is runnable iff its runnable_count >= 0.
  //
  // CAUTION: Callers should be aware of the existence of imprecise wake-ups.
  // E.g., a call to Wait(...) may expire concurrently with its matching Post().
  // Callers may therefore never assert pre-conditions on Wait()'s return.

  // Increment "schedulables"'s runnable_count.
  // REQUIRES: May only be called by approved synchronization primitives.
  static void Post(Schedulable* schedulable);

  // Decrement the runnable_count of the schedulable bound to the calling thread
  // and wait for runnable_count >= 0.  A optional timeout may also be supplied.
  // If t.has_timeout() this call will arrange to trigger an automatic
  // increment of runnable_count at t if Wait() has not completed by that
  // time.
  //
  // Returns true if the current schedulable's runnable count:
  // (a) was previously > 0
  // (b) became >= 0 again as the result of a matching Post().
  //
  // Returns false if Wait() completed as a result of "t"'s expiry.
  //
  // Implementations may arbitrarily return false in the event that (b) and
  // "t"'s expiry occur simultaneously.
  //
  // REQUIRES: CurrentThreadIsCooperative() == true.
  // REQUIRES: May only be called by approved synchronization primitives.
  static bool Wait(absl::synchronization_internal::KernelTimeout t);

  // Internal helpers which apply the specified operations to the scheduling
  // tree as appropriate.
  static bool UserSchedule(bool runnable,
                           absl::synchronization_internal::KernelTimeout t);
  static Schedulable* HierarchicalPostAndSchedule(Schedulable* schedulable,
                                                  int inc_runnable_delta = 1);
  static Schedulable* HierarchicalStopRunning(Schedulable* current,
                                              bool runnable, int finish_depth);
  static void EnterScheduleNext(absl::base_internal::ThreadIdentity* identity);
  static Schedulable* LeaveScheduleNext(
      absl::base_internal::ThreadIdentity* identity, Schedulable* result);
  static Schedulable* ScheduleNext(Schedulable* prev, bool runnable);

  friend void ::ABSL_INTERNAL_C_SYMBOL(AbslInternalPerThreadSemPost)(
      absl::base_internal::ThreadIdentity* identity);
  friend bool ::ABSL_INTERNAL_C_SYMBOL(AbslInternalPerThreadSemWait)(
      absl::synchronization_internal::KernelTimeout t);
  friend class Domain;
  friend class DowncallsTestlets;
  friend class Scheduler;
  friend class devtools_java_launcher::DowncallsProxy;
  friend class thread::Fiber;
};

//------------------------------------------------------------------------------
// End of public interfaces.
//------------------------------------------------------------------------------

inline bool Downcalls::CurrentThreadIsCooperative() {
  absl::base_internal::ThreadIdentity* identity =
      absl::base_internal::CurrentThreadIdentityIfPresent();

  // We may only co-operatively schedule if we're not already below an entry
  // point to the scheduling code.
  // TODO: Move to low-level-support.h and make more consistent.
  return identity != nullptr &&
         identity->scheduler_state.bound_schedulable.load(
             std::memory_order_relaxed) &&
         identity->scheduler_state.scheduling_disabled_depth.load(
             std::memory_order_relaxed) == 0;
}

inline void Downcalls::Reschedule() {
  UserSchedule(true, absl::synchronization_internal::KernelTimeout::Never());
}

inline bool Downcalls::Wait(absl::synchronization_internal::KernelTimeout t) {
  return UserSchedule(false, t);
}

}  // namespace scheduling
}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_SCHEDULING_DOWNCALLS_H_
