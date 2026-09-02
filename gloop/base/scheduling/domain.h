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

// A Domain manages a set of N slots onto which the execution of a set of
// concurrent activities may be multiplexed.  Each one of these N slots can be
// running any of the activities associated with the domain and corresponds to a
// host OS thread.  While the thread associated with a slot may be substituted;
// only as many threads as there are slots will be runnable at any time.  The
// operating system continues to arbitrate scheduling of these threads,
// including cross-Domain scheduling.
//
// The choice of which activity is running within a slot is managed by
// user-level Scheduler objects. In particular, we associate a tree of
// Schedulers with each domain.  The scheduling decisions are implemented by
// interactions between the Schedulers in this tree.  See scheduler.h for more
// detail.
//
// The activities associated with a domain may or may not be OS-level threads
// themselves.  We expect that in google3 programs, they will be either Threads
// (see thread/thread.h) or Fibers (see thread/fiber/fiber.h).
//
// The Domain class itself only provides an abstract interface; standard
// implementations may be found under in //thread/fiber/.
//
// We expect there to be a small number of domains in a process.  A
// typical process might have the following domains:
//
//    Domain high_priority_domain("high_priority", NumCPUs());
//    Domain regular_priority_domain("regular_priority", NumCPUs());
//    Domain background_work_domain("background", 1);
//
// Here we allow up to NumCPUs() high and regular priority slots to be active
// (in their respective domains) at any given time.  Each domain's internal
// scheduler hierarchy will be responsible for the mapping of work to these
// slots and ensuring that the appropriate number are runnable.  While the
// background_work_domain (in which we might schedule execution such as the
// tcmalloc release thread) will only ever have a single runnable slot.
// Programs may take advantage of kernel level scheduling interfaces (e.g.
// nice(2)) to ensure that the threads in the high priority domain are scheduled
// before threads in other domains.

#ifndef THIRD_PARTY_GLOOP_BASE_SCHEDULING_DOMAIN_H_
#define THIRD_PARTY_GLOOP_BASE_SCHEDULING_DOMAIN_H_

#include <errno.h>
#include <stdint.h>

#include <atomic>
#include <functional>
#include <limits>
#include <string>

#include "absl/base/optimization.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/internal/kernel_timeout.h"
#include "gloop/base/scheduling/domain_thread_assignment_callback_accessor.h"
#include "gloop/base/scheduling/low-level-support.h"
#include "gloop/base/scheduling/scheduler.h"
#include "gloop/base/thread-identity.h"
#include "gloop/thread/fiber/per-domain-counters.h"

namespace thread {
class Fiber;
}  // namespace thread

namespace base {
namespace scheduling {

class DomainFunctor;

class Domain {
 public:
  // Construct a new domain with no root Scheduler assigned and no
  // child schedulables.
  //
  // At most "max_concurrency" host OS threads will ever be concurrently active
  // within this domain.
  //
  // "name_prefix" will be used to describe this domain for monitoring and
  // debugging.  We recommend (but do not require) that each Domain in a process
  // be given a unique "name_prefix".
  //
  // REQUIRES: A root Scheduler must be subsequently specified (by constructing
  // a new Scheduler attached to this domain) before this domain can be used.
  Domain(absl::string_view name_prefix, int max_concurrency);

  // REQUIRES: root_scheduler()'s destructor pre-conditions must be met.
  // REQUIRES: Owner (of *this) must also free root_scheduler().
  virtual ~Domain();

  // Execution that might block on external operation (e.g., system calls)
  // should wrap such execution using StartPotentiallyBlockingRegion() and
  // FinishPotentiallyBlockingRegion(), e.g.:
  //
  //   Domain::StartPotentiallyBlockingRegion();
  //   int ret = epoll( ... ) < potentially wait to read from socket >
  //   Domain::FinishPotentiallyBlockingRegion();
  //   if (ret == -1) {
  //     PLOG(ERROR) << "epoll failed";
  //   }
  //
  // This is a no-op for implementations of Domain which are able to maintain
  // concurrency without these co-operative calls.
  //
  // May be safely called from any thread.  May be nested.
  // errno is preserved over the call to FinishPotentiallyBlockingRegion().
  //
  // REQUIRES: Only standard library functions, async safe google3 functions,
  // and syscalls may be used within the specified potentially blocking region.
  static void StartPotentiallyBlockingRegion();
  static void FinishPotentiallyBlockingRegion();

  // Creates an execution entity that will run "function(arg)", whose scheduling
  // will be managed by "scheduler".  The returned entity is initially blocked
  // (not runnable) and will not be eligible for execution until it is marked
  // for execution via Downcalls::Post().
  //
  // REQUIRES: scheduler->domain() == this
  typedef void (*ExecutableFn)(void*);
  virtual Schedulable* CreateExecutableSchedulable(Scheduler* scheduler,
                                                   ExecutableFn function,
                                                   void* arg) = 0;

  // Guarantee that the next scheduling decision for this domain will start from
  // the root scheduler.  Typically used to express user-level priorities within
  // Scheduler::Wake().
  void ScheduleNextFromRoot();

  // Returns the domain of the calling thread; nullptr for threads not belonging
  // to any domain.
  static Domain* CurrentDomain();

  Scheduler* root_scheduler() const { return root_scheduler_; }
  const std::string& name() const { return name_; }
  int max_concurrency() const { return max_concurrency_; }

  // Iterate over existing domains, invoking functor.
  // NOTE: Domain creation and destruction is blocked during this iteration so
  // execution time should be bound.
  static void Iterate(const std::function<void(const Domain*)>& functor);

  // Used for reading from the underlying atomics for a given fiber type.
  const thread::internal::PerDomainCounters& Counters(int type) const {
    return counters_[type];
  }

 protected:
  static bool DisableRescheduling() {
    return absl::base_internal::SchedulingGuard::DisableRescheduling();
  }

  static void EnableRescheduling(bool disable_result) {
    absl::base_internal::SchedulingGuard::EnableRescheduling(disable_result);
  }

  // Set "new_current" to be the schedulable returned for the current thread by
  // CurrentThreadSchedulable().  Overwrites any previous value.
  static void SetCurrentThreadSchedulable(Schedulable* new_current);

  // These interfaces allow the management of our (up-to) N runnable threads.
  // Schedulers belonging to this domain will invoke these methods (by passing
  // through the Downcalls interface which has permission to access these
  // private methods).
  //
  // REQUIRES:
  // - All operations must be commutative (reversible in order).
  // - Scheduler implementations must always respect max_concurrency()

  // Start, or resume previously paused, execution of "to_resume" in
  // an additional thread context within the domain.  Increases the
  // number of runnable host OS threads from this domain by 1.
  //
  // REQUIRES: to_resume->type == Schedulable::kWorkItem
  // REQUIRES: num(running schedulables) < domain->max_concurrency()
  virtual void ResumeAdditionalSchedulable(Schedulable* to_resume) = 0;

  // Synchronously pause execution of "current" until either:
  // (a) Execution is resumed (ResumeAdditionalSchedulable(), SwapCurrent()).
  // (b) "t" expires
  // Decreases the number of runnable host OS threads from this domain by 1.
  //
  // Returns true when execution is resumed [(a)]; otherwise false.
  // Implementations may arbitrarily return true or false in the in the event
  // that (a) and (b) occur simultaneously.
  //
  // REQUIRES: CurrentThreadSchedulable() == current
  virtual bool BlockCurrent(
      Schedulable* current,
      absl::synchronization_internal::KernelTimeout t) = 0;
  // Atomically {
  //   ResumeAdditionalSchedulable(next);
  //   return BlockCurrent(current, t);
  // }
  //
  // A swap operation will never modify the number of runnable host OS threads
  // owned by this domain, although it may perform substitutions.  E.g.:
  // "current" and "next" might be hosted by different threads.
  //
  // REQUIRES: CurrentThreadSchedulable() == current
  // REQUIRES: next->type == Schedulable::kWorkItem
  // REQUIRES: current != next
  virtual bool SwapCurrent(Schedulable* current, Schedulable* next,
                           absl::synchronization_internal::KernelTimeout t) = 0;

  // This Domain's implementation of StartPotentiallyBlockingRegion() and
  // FinishPotentiallyBlockingRegion() above.
  virtual void DomainStartPotentiallyBlockingRegion(Schedulable* current);
  virtual void DomainFinishPotentiallyBlockingRegion(Schedulable* current);

  virtual void EnteringScheduler(Schedulable* curr) {}

  // Used in DomainTest only.
  virtual void DomainTestDisableRescheduling() {}

  // SwapCurrent(), only treating next == nullptr as a BlockCurrent() and
  // shortcircuiting properly on current == next
  //
  // REQUIRES: CurrentThreadSchedulable() == current
  // REQUIRES: next->type == Schedulable::kWorkItem || next == nullptr
  bool SwapOrBlockCurrent(Schedulable* current, Schedulable* next,
                          absl::synchronization_internal::KernelTimeout t) {
    if (next) {
      if (current != next) {
        return SwapCurrent(current, next, t);
      } else {
        return true;
      }
    } else {
      return BlockCurrent(current, t);
    }
  }
  // A global list of all domains in the binary is maintained in
  // domain.cc. But inserting _this_ into the list in Domain::Domain()
  // is not safe, as a concurrent access to the list will see a not
  // fully constructed domain object. So all derived classes must call
  // the method below at the end of their constructor.
  void MarkFullyConstructed();

  // Used for modifying the underlying atomics.
  thread::internal::PerDomainCounters& MutableCounters(int type) {
    return counters_[type];
  }

 private:
  friend class DomainThreadAssignmentCallbackAccessor;

  // Sets a callback on the domain that will be invoked (for domains that
  // support it) whenever a schedulable is assigned to a thread.  Calling this
  // more than once on a domain has undefined behavior.
  virtual void SetThreadAssignmentCallback(ThreadAssignmentCallback callback) {}

  // Returns the depth at which the next scheduling decision for this domain
  // should begin, std::numeric_limits<int32_t>::max() when local scheduling is
  // preferred.
  int NextSchedulingStartingDepth();

  // Returns the schedulable currently bound to the calling thread.
  static Schedulable* CurrentThreadSchedulable();

  // Called by Scheduler::Scheduler(Domain*) constructor.  Does not take
  // ownership of "scheduler"'s storage.
  void set_root_scheduler(Scheduler* scheduler);

  std::atomic<int32_t> schedule_from_root_count_;
  const int max_concurrency_;
  // counters_ is only meant to be used in the case of a fiber's domain, as
  // PerDomainCounters is used to collect fiber specific stats.
  thread::internal::PerDomainCounters
      counters_[thread::internal::kNumFiberTypes];
  Scheduler* root_scheduler_;

  const std::string name_;

  friend class ConditionalPotentiallyBlockingRegion;
  friend class DomainTest;
  friend class DomainTestlets;
  friend class Downcalls;
  friend class Scheduler;
  friend class thread::Fiber;

  Domain(const Domain&) = delete;
  Domain& operator=(const Domain&) = delete;
};

// A simple functor encapsulating a function pointer and single argument, used
// to represent work that may be executed within a domain.
class DomainFunctor {
 public:
  DomainFunctor(Domain::ExecutableFn function, void* arg)
      : function_(function), arg_(arg) {}

  // Invokes "function(arg)", provided at construction.
  void operator()() {
    function_(arg_);  // Not safe to reference *this after execution.
  }

 private:
  Domain::ExecutableFn function_;
  void* arg_;
};

// Wraps a pair of Domain::StartPotentiallyBlockingRegion(),
// Domain::FinishPotentiallyBlockingRegion() calls.
class PotentiallyBlockingRegion {
 public:
  PotentiallyBlockingRegion();
  ~PotentiallyBlockingRegion();

 private:
  PotentiallyBlockingRegion(const PotentiallyBlockingRegion&) = delete;
  PotentiallyBlockingRegion& operator=(const PotentiallyBlockingRegion&) =
      delete;
};

// Wraps a pair of Domain::StartPotentiallyBlockingRegion(),
// Domain::FinishPotentiallyBlockingRegion() calls, iff the
// constructor parameter is true.
class ConditionalPotentiallyBlockingRegion {
 public:
  explicit ConditionalPotentiallyBlockingRegion(bool condition);
  ConditionalPotentiallyBlockingRegion(bool condition,
                                       bool disable_rescheduling);
  ~ConditionalPotentiallyBlockingRegion();

 private:
  const bool condition_;
  bool disable_result_ = false;

  ConditionalPotentiallyBlockingRegion(
      const ConditionalPotentiallyBlockingRegion&) = delete;
  ConditionalPotentiallyBlockingRegion& operator=(
      const ConditionalPotentiallyBlockingRegion&) = delete;
};

//------------------------------------------------------------------------------
// End of public interfaces
//------------------------------------------------------------------------------

inline Schedulable* Domain::CurrentThreadSchedulable() {
  absl::base_internal::ThreadIdentity* identity =
      absl::base_internal::CurrentThreadIdentityIfPresent();
  if (identity != nullptr) {
    return Schedulable::GetBoundSchedulable(identity);
  }
  return nullptr;
}

inline Domain* Domain::CurrentDomain() {
  Schedulable* current = CurrentThreadSchedulable();
  return current ? current->manager->domain() : nullptr;
}

inline void Domain::StartPotentiallyBlockingRegion() {
  // TODO: We should evaluate kicking transmogrified threads here.  The
  // trade-off being be that we might frequently call this in regions we want to
  // transmogrify.
  //
  // To be cooperative we must have both an identity and a bound schedulable.
  absl::base_internal::ThreadIdentity* identity =
      absl::base_internal::CurrentThreadIdentityIfPresent();
  if (identity == nullptr) {
    return;
  }

  Schedulable* current = Schedulable::GetBoundSchedulable(identity);
  if (current == nullptr) {
    return;
  }

  // Handle nested blocking regions; only increment depth.
  if (identity->scheduler_state.potentially_blocking_depth++) {
    return;
  }

  if (identity->scheduler_state.scheduling_disabled_depth.load(
          std::memory_order_seq_cst) > 0) {
    // Do an extra nested DisableRescheduling() so that FinishPBR() below can
    // determine whether to call DomainFinishPBR().
    absl::base_internal::SchedulingGuard::DisableRescheduling();
    return;
  }

  current->manager->domain()->DomainStartPotentiallyBlockingRegion(current);
}

inline void Domain::FinishPotentiallyBlockingRegion() {
  // To be cooperative we must have both an identity and a bound schedulable.
  absl::base_internal::ThreadIdentity* identity =
      absl::base_internal::CurrentThreadIdentityIfPresent();
  if (identity == nullptr) {
    return;
  }

  Schedulable* current = Schedulable::GetBoundSchedulable(identity);
  if (current == nullptr) {
    return;
  }

  // Handle nested blocking regions; only decrement depth.
  if (--identity->scheduler_state.potentially_blocking_depth) {
    return;
  }

  if (identity->scheduler_state.scheduling_disabled_depth.load(
          std::memory_order_seq_cst) > 1) {
    // If sched disable is nested, it means StartPBR() was called
    // with disabled sched, so we skip DomainFinishPBR().
    absl::base_internal::SchedulingGuard::EnableRescheduling(true);
    return;
  }

  // Pairs with outermost StartPotentiallyBlockingRegion().
  int saved_errno = errno;
  current->manager->domain()->DomainFinishPotentiallyBlockingRegion(current);
  errno = saved_errno;
}

inline int Domain::NextSchedulingStartingDepth() {
  int32_t count;
  do {
    count = schedule_from_root_count_.load(std::memory_order_relaxed);
    if (ABSL_PREDICT_TRUE(count == 0)) {
      return std::numeric_limits<int32_t>::max();  // Schedule locally.
    }
  } while (!schedule_from_root_count_.compare_exchange_weak(
      count, count - 1, std::memory_order_acquire, std::memory_order_relaxed));
  return 0;  // Schedule from root.
}

inline PotentiallyBlockingRegion::PotentiallyBlockingRegion() {
  Domain::StartPotentiallyBlockingRegion();
}

inline PotentiallyBlockingRegion::~PotentiallyBlockingRegion() {
  Domain::FinishPotentiallyBlockingRegion();
}

inline ConditionalPotentiallyBlockingRegion::
    ConditionalPotentiallyBlockingRegion(bool condition,
                                         bool disable_rescheduling)
    : condition_(condition) {
  if (condition_) {
    Domain::StartPotentiallyBlockingRegion();
  } else if (disable_rescheduling) {
    disable_result_ = Domain::DisableRescheduling();
  }
}

inline ConditionalPotentiallyBlockingRegion::
    ConditionalPotentiallyBlockingRegion(bool condition)
    : ConditionalPotentiallyBlockingRegion(condition, false) {}

inline ConditionalPotentiallyBlockingRegion::
    ~ConditionalPotentiallyBlockingRegion() {
  if (condition_) {
    Domain::FinishPotentiallyBlockingRegion();
  } else if (disable_result_) {
    Domain::EnableRescheduling(true);
  }
}

// Catch the case where the user forgets the variable name.
#define ConditionalPotentiallyBlockingRegion(x) \
  static_assert(                                \
      false, "conditional_potentially_blocking_region_decl_missing_var_name")

}  // namespace scheduling
}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_SCHEDULING_DOMAIN_H_
