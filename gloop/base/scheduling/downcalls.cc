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

#include "gloop/base/scheduling/downcalls.h"

#include <atomic>
#include <cstdint>
#include <limits>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/optimization.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/internal/create_thread_identity.h"
#include "absl/synchronization/internal/kernel_timeout.h"
#include "gloop/base/scheduling/domain.h"
#include "gloop/base/scheduling/low-level-support.h"
#include "gloop/base/scheduling/scheduler.h"
#include "gloop/base/static_threadlocal.h"
#include "gloop/base/thread-identity.h"

namespace thread {
extern void InternalSetCurrentFiberName(absl::string_view fiber_name);
extern absl::string_view InternalGetCurrentFiberName();

ABSL_ATTRIBUTE_WEAK
void InternalSetCurrentFiberName(absl::string_view) {}
ABSL_ATTRIBUTE_WEAK absl::string_view InternalGetCurrentFiberName() {
  return "";
}
}  // namespace thread

#ifdef ABSL_HAVE_THREAD_SANITIZER
extern "C" {
void __tsan_acquire(void* addr);
void __tsan_release(void* addr);
}
#endif

namespace base {
namespace scheduling {

using absl::synchronization_internal::KernelTimeout;

// Returns true if this increment resulted in schedulable being woken.
// Returns false otherwise.
//
// Acts as a Acquire barrier on "schedulable" when returning true.
//
// Barrier protects "schedulable->managing_slot".
static inline bool IncRunnable(Schedulable* schedulable, int delta = 1) {
  int old =
      schedulable->runnable_count.fetch_add(delta, std::memory_order_seq_cst);
  return (old < 0) && (old + delta >= 0);
}

// Returns true if schedulable is still runnable after runnable_count has been
// decremented.
// Returns false otherwise, Release barrier against "schedulable" in this case.
//
// Acts as a Release barrier on "schedulable" when returning false.
//
// CAUTION: In the DecRunnable(schedulable) == false case you have typically
// lost the right to dereference the passed schedulable as wake-ups may allow
// another thread to take ownership.
static inline bool DecRunnable(Schedulable* schedulable) {
  return schedulable->runnable_count.fetch_sub(1, std::memory_order_seq_cst) -
             1 >=
         0;
}

static inline bool IsRunnable(Schedulable* schedulable) {
  return schedulable->runnable_count.load(std::memory_order_relaxed) >= 0;
}

// REQUIRES: "schedulable" must not be a root schedulable.
static inline Domain* DomainOf(Schedulable* schedulable) {
  return schedulable->manager->domain();
}

// Stop running "current", which must be bound to the calling thread.  When
// runnable == true, "current" will be re-queued against its managing scheduler
// for future selection; otherwise it will block.  This process repeats
// hierarchically until we find a runnable 'schedulable' such that
//   schedulable->manager->depth() <= "finish_depth".
// (we do NOT call StopRunning on this schedulable).
//
// Returns the first runnable schedulable satisfying our condition
// above.  If we get to the root slot and find that it is no longer
// runnable, then we return nullptr.
//
// As a concrete example, suppose you pass 0 for 'finish_depth'.  Then
// we try to call StopRunning on all the schedulers except the root
// scheduler.  Then we return (possibility #1) a schedulable managed
// by the root scheduler (i.e., not a root slot, but something whose
// managing_slot is a root slot).
//
// But, there are two other possibilities.  If #1 was not runnable,
// then we might return (possibility #2) a root slot.  And if that
// root slot was not runnable, then we might return (possibility #3)
// nullptr.
//
// This seems complicated, but basically the invariant that we're
// providing is that if we return a non-null Schedulable, it is
// runnable (at a depth <= finish_depth).
Schedulable* Downcalls::HierarchicalStopRunning(Schedulable* current,
                                                bool runnable,
                                                int finish_depth) {
  ABSL_RAW_DCHECK(current->managing_slot != Slot::NullSlot(),
                  "passed unscheduled current");
  ABSL_RAW_DCHECK(current->manager->depth() >= finish_depth,
                  "current < finish_depth");

  Schedulable* prev = current;
  Schedulable* managing_slot = current->managing_slot.schedulable_;
  current->managing_slot = Slot::NullSlot();
  if (!runnable) {
    // We'll operate on the higher level managing_slot schedulables in the loop
    // below but first check for ::Post() races versus current.  We may not
    // modify current beyond this.
    runnable = DecRunnable(current);
  }

  Scheduler* scheduler = prev->manager;
  Schedulable* next_managing_slot;
  do {
    ABSL_RAW_DCHECK(managing_slot, "unscheduled prev");

    // It is possible that descheduling prev will also result in managing_slot
    // becoming idle.  In this case we have lost the "right" to modify
    // managing_slot as it may have already been advertised to a concurrent
    // operation such as a wake-up.
    //
    // To handle this we speculatively assume this operation will block and
    // update managing_slot's runnability appropriately.  In the event that it
    // remains runnable, we will have retained exclusive rights to it and may
    // safely undo this.
    //
    // Note: We specifically cannot safely optimize against runnable == true as
    // StopRunning(..., true) may still return false.
    next_managing_slot = managing_slot->managing_slot.schedulable_;
    managing_slot->managing_slot = Slot::NullSlot();
    DecRunnable(managing_slot);  // Assume managing_slot blocks.

    // StopRunning() has release semantics on passed schedulables.
    runnable = scheduler->StopRunning(Slot::FromSchedulable(managing_slot),
                                      prev, runnable);
    if (runnable) {
      // managing_slot was not potentially rescheduled and we have still have
      // exclusive rights to it, we must undo our speculative blocking above.
      IncRunnable(managing_slot);
    }

    prev = managing_slot;
    managing_slot = next_managing_slot;
    scheduler = prev->manager;
    // We must continue until we are either:
    //  (a) At the root.
    //  (b) Still runnable and managed by a scheduler of suitable depth.
  } while (managing_slot && (!runnable || scheduler->depth() > finish_depth));

  if (runnable) {
    // We must restore managing_slot since we potentially erased it above.
    prev->managing_slot = Slot::FromSchedulable(managing_slot);
    return prev;
  } else {
    return nullptr;
  }
}

struct QueuedFunction {
  typedef std::vector<QueuedFunction> ListType;

  void (*function)(void*);  // Must match Scheduler::QueuableFunction
  void* arg;
};

STATIC_THREAD_LOCAL(QueuedFunction::ListType, rwsa_list);

// State bits describing "schedule_next_state" (stored on ThreadIdentity).
//
// For a more detailed description of their associated logic see
// EnterScheduleNext() and LeaveScheduleNext().
//
// kScheduleNextRunning:
//   ScheduleNext() is already running within this thread.  It is not safe to
//   schedule, operations must be queued via RunWhenSchedulingAllowed().
//
// kScheduleNextRunningQueuedFunctions:
//   Executing functions previously queued via RunWhenSchedulingAllowed().
//   REQUIRES: kScheduleNextRunning set.
//
// kScheduleNextHaveQueuedFunctions:
//   Functions have been queued by RunWhenSchedulingAllowed().  They will be
//   executed (in order) as soon as it is safe to schedule.
//   REQUIRES: kScheduleNextRunning set.
//
// kScheduleNextPickedSelf:
//   A queued scheduling operation reselected the current thread.  This is only
//   used in the case that a queued function performs the reselection.
//
enum {
  kScheduleNextRunning = 1,
  kScheduleNextRunningQueuedFunctions = 2,
  kScheduleNextHaveQueuedFunctions = 4,
  kScheduleNextPickedSelf = 8
};

// We store our state on ThreadIdentity (as opposed to an independent TLS word)
// to both minimize our cache footprint and have this state be externally
// available for debugging.
static inline uint32_t* ScheduleNextState(
    absl::base_internal::ThreadIdentity* identity) {
  return &identity->scheduler_state.schedule_next_state;
}

void Downcalls::RunWhenSchedulingAllowed(QueueableFunction function,
                                         void* arg) {
  uint32_t* state = ScheduleNextState(
      absl::synchronization_internal::GetOrCreateCurrentThreadIdentity());
  // We may run "function" directly iff ScheduleNext() is not running.
  // EnterScheduleNext() guarantees that kScheduleNextRunning is set on entry.
  if (*state & kScheduleNextRunning) {
    *state |= kScheduleNextHaveQueuedFunctions;
    QueuedFunction to_queue;
    to_queue.function = function;
    to_queue.arg = arg;
    // Will be run (by LeaveScheduleNext()) when scheduling is again allowed.
    // List is per-thread so we do not require locking.
    rwsa_list.get().push_back(to_queue);
  } else {
    // Even though we are running function directly, it should still be run in a
    // non-cooperative context.
    bool disable_result =
        absl::base_internal::SchedulingGuard::DisableRescheduling();
    (*function)(arg);
    absl::base_internal::SchedulingGuard::EnableRescheduling(disable_result);
  }
}

// Called on every entry to ScheduleNext().
//
// These functions (entry and pairing exit) may be nested e.g.:
//   ScheduleNext() [1]
//     (EnterScheduleNext ...)
//     > (Scheduler code):RunWhenSchedulingAllowed( ... )  E.g. Downcalls::Post
//     (LeaveScheduleNext, run queued functions
//        > Downcalls::Post(...)
//          (EnterScheduleNext ...)   [Singly nested.]
//            > ScheduleNext() [2]
//
// It is guaranteed that the nesting depth above will never exceed 1.  In the
// case that [2] issues a subsequent RunWhenSchedulingAllowed(), it will be
// queued for execution by [1]'s original LeaveScheduleNext().
inline void Downcalls::EnterScheduleNext(
    absl::base_internal::ThreadIdentity* identity) {
  *ScheduleNextState(identity) |= kScheduleNextRunning;
}

// Pairs with EnterScheduleNext(), called at every return from ScheduleNext().
//
// Guarantees that if "prev" [as in ScheduleNext(..., prev, ...)] was
// re-scheduled by any local invocation of ScheduleNext() (original, or queued)
// that it is returned.
inline Schedulable* Downcalls::LeaveScheduleNext(
    absl::base_internal::ThreadIdentity* identity, Schedulable* result) {
  uint32_t* state = ScheduleNextState(identity);

  // This is the common case.  We only need to do further work if a Scheduler
  // queued work [RunWhenSchedulingAllowed()] during ScheduleNext()'s execution.
  if (ABSL_PREDICT_TRUE(*state == kScheduleNextRunning)) {
    *state = 0;
    return result;
  }

  // If we're already nested then we can check our result and return
  // immediately.  If we've enqueued any further functions they will be picked
  // up by the outer LeaveScheduleNext().
  if (*state & kScheduleNextRunningQueuedFunctions) {
    // [1]: It's possible that during the execution of queued functions it is
    // possible that we became eligible for selection again and scheduled
    // ourselves.  When this occurs we always specify that we return ourselves
    // as scheduled.  This is translated at [2].
    if (result != nullptr &&
        result == Schedulable::GetBoundSchedulable(identity)) {
      *state |= kScheduleNextPickedSelf;
      result = nullptr;
    }

    return result;
  }

  // Execution below only occurs within outermost LeaveScheduleNext().
  if (*state & kScheduleNextHaveQueuedFunctions) {
    // Prevent reentrancy: queued "function" likely to depend on ScheduleNext().
    *state |= kScheduleNextRunningQueuedFunctions;

    QueuedFunction::ListType* queued = rwsa_list.pointer();
    while (!queued->empty()) {
      QueuedFunction to_run = queued->back();
      // We remove queued before executing it so that we may potentially re-use
      // its storage should it queue another function in turn.
      queued->pop_back();
      (*to_run.function)(to_run.arg);
    }

    // [2]: We explicitly handle the case that our schedulable was re-selected
    // during the execution of a queued function.  We do this by substituting
    // ourselves for the original result (if any) and resuming it.
    //
    // This bit can only be set after we ran queued functions.
    if (*state & kScheduleNextPickedSelf) {
      if (result != nullptr) {
        DomainOf(result)->ResumeAdditionalSchedulable(result);
      }
      result = Domain::CurrentThreadSchedulable();
    }

    // We wait until running all queued functions to clear these bits, both as
    // they protect against reentrancy and as a queued function can re-set them.
    *state &= ~(kScheduleNextHaveQueuedFunctions |
                kScheduleNextRunningQueuedFunctions | kScheduleNextPickedSelf);
  }

  *state &= ~kScheduleNextRunning;
  ABSL_RAW_DCHECK(*state == 0, "expected schedule_next_state == 0");
  return result;
}

// Downcalls::ScheduleNext()
// -------------------------
// Given a currently running schedulable "prev", optionally requeue it on its
// managing scheduler (when "runnable" == true) and select the next schedulable
// that should run in its slot.  Scheduling is always hierarchical; we will
// attempt to re-schedule parent schedulers when a child has no runnable
// entities.
//
// Returns a kWorkItem schedulable that should be resumed in the place of
// prev; nullptr if none are currently available.  In this case the root entity
// previously responsible for "prev"'s scheduling is made available for wakeups.
//
// SCHEDULING DECISION:
// The scheduling decision may be summarized as:
//
// 1. Synchronize with domain to determine "starting_depth" for the current
//    scheduling decision.  We guarantee that a scheduler at, or above, this
//    level will have the opportunity to express their selection in the returned
//    Schedulable.
// 2. <Stop running the previous (but still currently running) schedulable>
//    (a) Deschedule it; requeuing on its managing scheduler if still runnable.
//    (b) Repeat, against the (a)'s managing_slot (which was responsible for
//        scheduling (a)) until we are left with a schedulable no deeper
//        than starting_depth.
// 3. <Actual scheduling begins>
//    (a) Starting from the Scheduler S managed by the "current" schedulable
//        (initialized above) attempt to schedule a schedulable managed by S.
//        E.g.:  Scheduler* S = current->managed.scheduler;
//               next = S->ScheduleManaged(current, ...);
//       (i) If "next" == nullptr
//           Reschedule "current" as non-runnable and continue up the tree
//           (i.e., current->managing_slot).  If "current" is already at
//           root, we are finished and must go idle.
//      (ii) If next != nullptr (a managed schedulable is available):
//           If next->type == kWorkItem: we are finished, return it.
//           Otherwise: it manages a child Scheduler slot, continue at (a).
//

// Returns whether the scheduler managed by schedulable has depth() > depth.
static inline bool DeeperThan(Schedulable* schedulable, int depth) {
  ABSL_RAW_DCHECK(schedulable->type == Schedulable::kChildSlot,
                  "non-scheduler entity");
  // NOTE: Since "depth" is already in register we trade an extra branch
  // in the unlikely case to avoid a dependent load on scheduler->depth().
  // This is unlikely because high-frequency rescheduling events (e.g. Mutex)
  // will always try to reschedule locally.
  if (ABSL_PREDICT_TRUE(depth == std::numeric_limits<int32_t>::max())) {
    return false;
  }
  return schedulable->managed.scheduler->depth() > depth;
}

Schedulable* Downcalls::ScheduleNext(Schedulable* prev, bool runnable) {
  ABSL_RAW_DCHECK(prev != nullptr, "no prev");
  absl::base_internal::ThreadIdentity* identity;
  identity = absl::synchronization_internal::GetOrCreateCurrentThreadIdentity();
  EnterScheduleNext(identity);

#ifdef ABSL_HAVE_THREAD_SANITIZER
  __tsan_acquire(&prev->managing_slot);  // Pairs with __tsan_release below.
#endif
  Schedulable* managing_slot;
  // Find the first Scheduler to ScheduleManaged(...) against.
  if (prev->type == Schedulable::kChildSlot) {
    managing_slot = prev;
    prev = nullptr;
    ABSL_RAW_DCHECK(!runnable,
                    "re-scheduling scheduler entity with runnable==true");
  } else {
    managing_slot = prev->managing_slot.schedulable_;
  }

  // Synchronize with Domain::ScheduleNextFromRoot().  We can't use DomainOf()
  // here as "managing_slot" may not have a managing scheduler (e.g. slot).
  int starting_depth =
      managing_slot->managed.scheduler->domain()->NextSchedulingStartingDepth();

  // If prev was deeper than starting_depth then we must call
  // HierarchicalStopRunning to "walk" back up the scheduling tree.  This is
  // only applicable at the start of our scheduling decision.
  if (DeeperThan(managing_slot, starting_depth)) {
    prev = HierarchicalStopRunning(prev ? prev : managing_slot, runnable,
                                   starting_depth);
    if (prev == nullptr) {
      // Stopping prev resulted in our slot becoming idle.
      return LeaveScheduleNext(identity, nullptr);
    }

    if (prev->managing_slot == Slot::NullSlot()) {
      // We reached the root when stopping 'prev'.
      managing_slot = prev;
      prev = nullptr;
      runnable = false;
    } else {
      managing_slot = prev->managing_slot.schedulable_;
      runnable = true;
    }
  } else if (prev != nullptr) {
    // We are about to {de,re}schedule prev; however we lose the right to
    // touch it beyond DecRunnable() so first clear its managing_slot.  We only
    // attempt to maintain this on kWorkItem schedulables (harder for slots).
    prev->managing_slot = Slot::NullSlot();
  }

  ABSL_RAW_DCHECK(managing_slot, "missing managing_slot");
  ABSL_RAW_DCHECK(managing_slot->type == Schedulable::kChildSlot,
                  "non-scheduler entity");
  Schedulable* next;
  Scheduler* scheduler = managing_slot->managed.scheduler;
  do {
    // We must cache the next ancestor slot before attempting to schedule since
    // if ScheduleManaged() returns nullptr, then that scheduler may advertise
    // "managing_slot" to a parallel wake-up and ScheduleNext().  Potentially
    // overwriting its ->managing_slot before we attempt to traverse up.
    Schedulable* next_managing_slot = managing_slot->managing_slot.schedulable_;
    if (prev != nullptr && !runnable) {
      // Synchronize runnable versus potential racing wake-ups.
      runnable = DecRunnable(prev);
    }
    next = scheduler->ScheduleManaged(Slot::FromSchedulable(managing_slot),
                                      prev, runnable);
    if (next == nullptr) {
      if (managing_slot->manager == nullptr) {
        // We've reached the root of the scheduling hierarchy.
        if (DecRunnable(managing_slot)) {
          // managing_slot is still runnable; we've raced with a wake-up.  Since
          // we're still runnable the remote wake-up must have been queued and
          // we should retry scheduling using the scheduler it manages.
          runnable = false;
          prev = nullptr;
          continue;
        }
        // Otherwise: our root schedulable is no longer runnable => go idle.
        break;
      }

      // When nothing is runnable we must traverse up.
      scheduler = managing_slot->manager;
      prev = managing_slot;  // managing_slot known to exist above.
      managing_slot = next_managing_slot;
      // Note that prev now manages "scheduler" above, which just reported
      // having no work available.  We must now reschedule prev using its
      // managing_slot (which we cached), on its managing scheduler.
      runnable = false;
    } else {
      // We were able to schedule something from our current Scheduler.
      next->managing_slot = Slot::FromSchedulable(managing_slot);
#ifdef ABSL_HAVE_THREAD_SANITIZER
      __tsan_release(&next->managing_slot);  // Pairs with __tsan_acquire above.
#endif
      if (next->type == Schedulable::kWorkItem) {
        break;  // Execution!  We're finished.
      } else {
        // We've scheduled an entity managing the slot of a child scheduler.  We
        // extend them our right to run and continue the scheduling decision
        // using the managed scheduler.
        managing_slot = next;
        prev = nullptr;
        runnable = false;
        scheduler = managing_slot->managed.scheduler;
      }
    }
  } while (true);

  return LeaveScheduleNext(identity, next);
}

// This is a wrapper which combines re-scheduling the schedulable bound to the
// calling thread (see ScheduleNext()) and enacting the resultant decision in
// the affected domains.
//
// When "runnable" == false, an absolute expiration "abstime" may be
// (optionally) specified.  If the rescheduled entity has not resumed execution
// by this time, it's domain will synchronize to ensure that it is automatically
// woken.
//
// Returns true when "runnable" == true.
// Equivalent return semantics to Downcalls::Wait() when "runnable" == false.
//
// REQUIRES: Downcalls::CurrentIsCooperative() == true
bool Downcalls::UserSchedule(bool runnable, KernelTimeout t) {
#if 0
  // Temporarily disabled until SpinLock and GoogleOnceInit block cooperatively
  // by default.
  ABSL_RAW_CHECK(absl::base_internal::SchedulingGuard::ReschedulingIsAllowed(),
            "Attempt to reschedule within ScheduleGuard region.");
#endif
  absl::base_internal::SchedulingGuard::ScopedDisable disable_rescheduling;

  Schedulable* prev = Domain::CurrentThreadSchedulable();
  // NOTE: If TSAN complains about the memory reference in the check below, it's
  // likely not a TSAN issue and indicates a test timeout. See b/275634003 for
  // details.
  ABSL_RAW_CHECK(prev->managing_slot != Slot::NullSlot(),
                 "Attempt to reschedule without a slot (inside PBR?).");
  DomainOf(prev)->EnteringScheduler(prev);
  Schedulable* next = ScheduleNext(prev, runnable);

  bool timeout = false;

  timeout = !DomainOf(prev)->SwapOrBlockCurrent(prev, next, t);

  ABSL_RAW_DCHECK(IsRunnable(prev), "current not runnable");
#if !ABSL_HAVE_THREAD_SANITIZER
  ABSL_RAW_DCHECK(prev->managing_slot != Slot::NullSlot(),
                  "current unscheduled");
#endif
  return !timeout;
}

// Increment "schedulable"'s runnable_count.  If this causes "schedulable" to
// become runnable, queue it against its managing scheduler.  E.g.:
//   schedulable->manager->Wake(schedulable);
//
// If this increases the concurrency available within a scheduler, a Schedulable
// managing the newly woken slot will be returned.  We repeat this process
// against the returned Schedulable, and its Scheduler, until no slots are
// returned, or we reach a root schedulable.
//
// If we succeed in reaching a root-schedulable then this wake-up has led to
// additional concurrency within the domain.  Schedule it, returning a kWorkItem
// schedulable that should run in the woken slot.  Otherwise, returns nullptr.
//
// REQUIRES: Returned kWorkItem schedulables must resume execution.
Schedulable* Downcalls::HierarchicalPostAndSchedule(Schedulable* schedulable,
                                                    int inc_runnable_delta) {
  absl::base_internal::ThreadIdentity* identity;
  identity = absl::synchronization_internal::GetOrCreateCurrentThreadIdentity();
  // We need a wakeup to be "atomic" from the perspective of other scheduling
  // operations.  For example, any RWSA calls issued by Scheduler::Wake() may
  // not execute until we're finished.
  EnterScheduleNext(identity);
  do {
    if (!IncRunnable(schedulable, inc_runnable_delta)) {
      // Raced with schedulable actually blocking.
      schedulable = nullptr;
      break;
    }
    inc_runnable_delta = 1;
    if (!schedulable->manager) {
      // Root schedulable.  Our caller must initiate additional concurrency.
      break;
    }
    schedulable = schedulable->manager->Wake(schedulable).schedulable_;
  } while (schedulable);

  if (schedulable != nullptr) {
    // "schedulable" represents available concurrency within the encapsulating
    // domain.  Attempt to schedule it.
    // Implicitly includes LeaveScheduleNext().
    return ScheduleNext(schedulable, false);
  } else {
    return LeaveScheduleNext(identity, nullptr);
  }
}

void Downcalls::Post(Schedulable* schedulable) {
  absl::base_internal::SchedulingGuard::ScopedDisable disable_rescheduling;

  Schedulable* additional = HierarchicalPostAndSchedule(schedulable);
  if (additional) {
    // Tell the encapsulating domain about the new concurrency.
    DomainOf(additional)->ResumeAdditionalSchedulable(additional);
  }
}

// Guarantees a final DecRunnable will leave runnable_count at ~ kint32min + 1
// (any outstanding imprecise wake-up can result in a larger, but still blocked
// value). Targets kint32min + 1 so that we can subtract this value without
// having gcc whine about overflow when we make -kint32min.
static const int kSynchronizeBlockRunnableCount =
    std::numeric_limits<int32_t>::min() + 2;
// This is kSynchronizeBlockRunnableCount adjusted for imprecise wake-ups.
static const int kSynchronizeBlockMinCount =
    std::numeric_limits<int32_t>::min() / 2;

Schedulable* Downcalls::DomainObservedBlocking(Schedulable* blocked) {
  absl::base_internal::SchedulingGuard::ScopedDisable disable_rescheduling;

  // "blocked" is blocking externally, we should not attempt to schedule it
  // again until its domain notifies us that it's again runnable. We must
  // guarantee that only DomainObservedWakeup() can resume our execution, but we
  // must preserve any current Post()s until then.
  int value = blocked->runnable_count.fetch_add(kSynchronizeBlockRunnableCount);
  ABSL_RAW_DCHECK(!(value < kSynchronizeBlockMinCount), "already DOB");

  return ScheduleNext(blocked, false);
}

Schedulable* Downcalls::DomainObservedWakeup(Schedulable* woken) {
  absl::base_internal::SchedulingGuard::ScopedDisable disable_rescheduling;

  ABSL_RAW_CHECK(woken->runnable_count.load(std::memory_order_relaxed) <
                     kSynchronizeBlockMinCount,
                 "unmatched DomainObservedWakeup()");
  ABSL_RAW_DCHECK(woken->manager, "DomainObservedWakeup on !kWorkItem");
  // We paired with DomainObservedBlocking().  We must now guarantee that we
  // become runnable at user-level also.  It's safe to race with an imprecise
  // wake-up at this point, since we only care that someone wakes "woken".
  return HierarchicalPostAndSchedule(woken,
                                     -kSynchronizeBlockRunnableCount + 1);
}

Schedulable* Downcalls::DomainObservedTimeout(Schedulable* expired) {
  // This disable should typically be redundant.
  absl::base_internal::SchedulingGuard::ScopedDisable disable_rescheduling;

  return HierarchicalPostAndSchedule(expired);
}

static const bool __asan_ready = true;

// The following three callback definitions allow for (typically glibc) code
// outside of google3 to schedule cooperatively when locking.
//
// See Domain::{Start, Finish}PotentiallyBlockingRegion() for more details.
extern "C" {
ABSL_ATTRIBUTE_NO_SANITIZE_ADDRESS
void __google_cxa_guard_acquire_begin(void) {
  if (!__asan_ready) return;
  absl::base_internal::ThreadIdentity* identity =
      absl::base_internal::CurrentThreadIdentityIfPresent();
  if (identity != nullptr) {
    ++identity->static_initialization_depth;
  }
  Domain::StartPotentiallyBlockingRegion();
}

ABSL_ATTRIBUTE_NO_SANITIZE_ADDRESS
void __google_cxa_guard_acquire_end(void) {
  if (!__asan_ready) return;
  Domain::FinishPotentiallyBlockingRegion();
}

ABSL_ATTRIBUTE_NO_SANITIZE_ADDRESS
void __google_cxa_guard_release_end(void) {
  if (!__asan_ready) return;
  absl::base_internal::ThreadIdentity* identity =
      absl::base_internal::CurrentThreadIdentityIfPresent();
  if (identity != nullptr) {
    --identity->static_initialization_depth;
  }
}

}  // extern "C"
}  // namespace scheduling
}  // namespace base

// The following two callbacks export:
//   absl::base_internal::SchedulingGuard::DisableRescheduling()
//   absl::base_internal::SchedulingGuard::EnableRescheduling()
// For code outside of google3 to disable/enable cooperative scheduling.
//
// See absl::base_internal::SchedulingGuard for more details.
extern "C" bool __google_disable_rescheduling(void) {
  return absl::base_internal::SchedulingGuard::DisableRescheduling();
}
extern "C" void __google_enable_rescheduling(bool disable_result) {
  absl::base_internal::SchedulingGuard::EnableRescheduling(disable_result);
}
