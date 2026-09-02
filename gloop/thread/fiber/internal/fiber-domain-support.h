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

// A partial implementation of Domain implementing an idle list of threads and
// periodic freeing of them.
//
// This file should only ever be included by other cc files if they are
// implementing a Domain.

#ifndef THIRD_PARTY_GLOOP_THREAD_FIBER_INTERNAL_FIBER_DOMAIN_SUPPORT_H_
#define THIRD_PARTY_GLOOP_THREAD_FIBER_INTERNAL_FIBER_DOMAIN_SUPPORT_H_

#include <atomic>
#include <cstdint>

#include "absl/base/casts.h"
#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/optimization.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/internal/kernel_timeout.h"
#include "gloop/base/scheduling/domain.h"
#include "gloop/base/scheduling/domain_thread_assignment_callback_accessor.h"
#include "gloop/base/scheduling/scheduler.h"
#include "gloop/thread/fiber/internal/fiber-thread-pool.h"
#include "gloop/thread/wait_state.h"

#ifdef ABSL_HAVE_THREAD_SANITIZER
extern "C" {
void __tsan_acquire(void* addr);
void __tsan_release(void* addr);
}
#endif

namespace thread {

class CommonFiberDomainThread;
class CommonFiberDomain : public base::scheduling::Domain {
 public:
  CommonFiberDomain(absl::string_view name_prefix, int max_concurrency);

  // KickIdleThreads() must have first been called
  ~CommonFiberDomain() override;

  // Must be called by the subclass destructor, while RawResume is still valid.
  // Kicks any idle threads and waits for all threads to exit.
  void WaitForThreads();

  // Start a thread for a Schedulable, if necessary getting one from the idle
  // list or creating it and then binding the Schedulable. Returns the thread if
  // it needs to be Resumed, otherwise nullptr.
  CommonFiberDomainThread* RunSchedulableOnThread(
      base::scheduling::Schedulable* schedulable) {
    if (ABSL_PREDICT_TRUE(IsBoundToThread(schedulable))) {
      return DomainThreadFromSchedulable(schedulable);
    }
    return PickThreadForSchedulable(schedulable);
  }

  // Returns true if "schedulable" has had BindSchedulable called on it - ie if
  // it has begun running.
  // REQUIRES: schedulable is associated with this domain
  static bool IsBoundToThread(base::scheduling::Schedulable* schedulable) {
    return schedulable->is_flag_set(kSchedulableManagedFlagBound);
  }

  // Returns the thread that "schedulable" is running on.
  // REQUIRES: IsBoundToThread(schedulable)
  CommonFiberDomainThread* DomainThreadFromSchedulable(
      base::scheduling::Schedulable* schedulable) {
    ABSL_RAW_DCHECK(IsBoundToThread(schedulable), "schedulable must be bound");
    return reinterpret_cast<CommonFiberDomainThread*>(
        schedulable->managed.work);
  }

  // Interface for subclasses to implement (in addition to Domain):

  // Start a new thread that will immediately bind and start running
  // "schedulable"
  virtual void StartNewThread(base::scheduling::Schedulable* schedulable) = 0;

  typedef absl::synchronization_internal::KernelTimeout KernelTimeout;
  // These correspond to SwitchTo::Wait/Resume/Switch or Futex::Wait/Wake/Swap
  // or Domain::BlockCurrent/RAS/SwapCurrent. In particular, a RawResume/Swap
  // must be able to properly resume a BlockCurrented thread.
  virtual bool RawBlock(CommonFiberDomainThread* curr, KernelTimeout t) = 0;
  virtual void RawResume(CommonFiberDomainThread* additional) = 0;
  virtual bool RawSwap(CommonFiberDomainThread* prev,
                       CommonFiberDomainThread* next, KernelTimeout t) = 0;

  // Returned kWorkItem schedulable will always be uniquely associated with a
  // single thread.
  // NOTE: On "closure"'s completion the underlying backing thread may be
  // re-used by a future CreateExecutableSchedulable() call.
  // REQUIRES: scheduler->domain() == this
  base::scheduling::Schedulable* CreateExecutableSchedulable(
      base::scheduling::Scheduler* scheduler, ExecutableFn function,
      void* arg) override;

  // Sets a callback on the domain that will be invoked (for domains that
  // support it) whenever a schedulable is assigned to a thread.
  void SetThreadAssignmentCallback(
      base::scheduling::ThreadAssignmentCallback callback) override {
    thread_assignment_callback_ = callback;
  }

 private:
  // We use one the first managed flag to track whether a schedulable has been
  // associated with a thread yet.
  enum { kSchedulableManagedFlagBound = 17 };

  // Get a thread for "schedulable", preferring the current one (if we are in
  // WaitForNextWorkItem) or an idle one if possible. Otherwise create a one.
  // Returns the new thread if it needs RawResume or equivalent, otherwise it
  // will start without help.
  // REQUIRES: schedulable is not associated with any thread (no previous call
  // to PickThreadForSchedulable, much less IsBoundToThread == true)
  CommonFiberDomainThread* PickThreadForSchedulable(
      base::scheduling::Schedulable* schedulable);

  void TmpInternalRef() {
    references_held_.fetch_add(1, std::memory_order_relaxed);
  }

  void TmpInternalUnref() {
    // TODO: once Domain Orphaning is done, delete
    // "this" when refcount hits zero.
    references_held_.fetch_sub(1, std::memory_order_acq_rel);
  }

  void ReportThreadAssignment(pthread_t tid,
                              base::scheduling::Schedulable* schedulable) {
    if (thread_assignment_callback_ != nullptr) {
      thread_assignment_callback_(tid, schedulable);
    }
  }

  CommonFiberThreadPool thread_pool_;
  std::atomic<int32_t> references_held_;
  // Some users depend on behavior where the default stack size is checked once
  // upon creation of the domain, and does not change even if
  // FLAGS_fibers_default_stack_size changes for that domain.
  const int default_fiber_stack_size_;
  friend CommonFiberDomainThread;

  base::scheduling::ThreadAssignmentCallback thread_assignment_callback_{
      nullptr};
};

class CommonFiberDomainThread : public CommonFiberThread {
 public:
  // the thread::Options passed in must contain a stack size that is a power of
  // two. This is a constraint imposed by fiber domains maintaining an array of
  // threadlists for stack sizes quantized to powers of 2.
  CommonFiberDomainThread(CommonFiberDomain* domain, absl::string_view name,
                          base::scheduling::Schedulable* first);

  // CommonFiberDomainThreads are self-deleting.
  ~CommonFiberDomainThread() override;

  // The main loop for this thread, called by Run with scheduling disabled, it
  // should look basically like:
  //
  // do {
  //   prev = RunOneSchedulable();
  //   next = Downcalls::DomainObservedBlocking(prev);
  // } while (WaitForNextWorkItem(prev, next));
  virtual void WorkLoop() = 0;

  base::scheduling::Schedulable* RunOneSchedulable() {
    base::scheduling::Schedulable* current = next_schedulable_;
    next_schedulable_ = nullptr;
    base::scheduling::DomainFunctor work = BindSchedulable(current);
    CommonFiberDomain::SetCurrentThreadSchedulable(current);
    work();
    CommonFiberDomain::SetCurrentThreadSchedulable(nullptr);
    return current;
  }

  // "prev" just finished running and Downcalls::DomainObservedBlocking(prev)
  // picked "next" to run. Gets "next" running and adds this thread to the idle
  // list if necessary, and waits for a new Schedulable to run. Returns true
  // when one is found or false if this thread should exit.
  template <typename SubclassDomain>
  bool WaitForNextWorkItem(base::scheduling::Schedulable* prev,
                           base::scheduling::Schedulable* next) {
    // Unfortunately, g++ isn't smart enough to infer this even if we make a
    // virtual method domain() that returns the subclass Domain type in the
    // Thread subclass. Having the right domain type _does_ allow it to inline
    // several of these calls, which saves 4% on stubby4 rpc benchmarks.

    SubclassDomain* domain = absl::down_cast<SubclassDomain*>(domain_);
    // We scheduled an unbound entity, always resume it directly.
    if (next != nullptr && !domain->IsBoundToThread(next)) {
      DeleteSchedulable(prev);
      SetNextSchedulable(next);
      return true;
    }

    // DomainObservedBlocking did not return a candidate we could host.  It's
    // possible that deletion will admit a new schedulable (in the presence of
    // admission control); we explicitly support binding to such a target.
    // TODO: move eligible_local logic into CommonFiberDomainThread once
    // we have ThreadForSchedulable instead of TargetFromSchedulable.
    *EligibleLocalPtr() = this;
    DeleteSchedulable(prev);
    if (*EligibleLocalPtr() == nullptr) {
      // Local rebind occurred.
      if (next) {
        // Still need to get next running.
        domain->ResumeAdditionalSchedulable(next);
      }
      return true;
    } else {
      *EligibleLocalPtr() = nullptr;
    }

    if (!domain->thread_pool_.TryAddIdleThread(this)) {
      // We're dying.  If there's new work, get it started.
      if (next) {
        // Like the Switch below, only we don't want to be kept around.
        domain->ResumeAdditionalSchedulable(next);
      }
      return false;
    }

    if (next != nullptr) {
      // We're now on the free-list.  A remote SetNextSchedulable() may now
      // execute, followed by a matching swap or resume to this thread.  This
      // control transfer will pair with our blocking below to form a
      // Release/Acquire pair versus "next_schedulable_".
      domain->RawSwap(this, domain->DomainThreadFromSchedulable(next),
                      absl::synchronization_internal::KernelTimeout::Never());
    } else {
      // Nothing to do, so just sleep.
      WaitStateScope scope(WaitStateScope::WaitState::kWaitingForWork);
      domain->RawBlock(this, {});
    }
    return next_schedulable_ != nullptr;
  }

  CommonFiberDomain* const domain_;

  inline void TSAN_Acquire() const {
#ifdef ABSL_HAVE_THREAD_SANITIZER
    __tsan_acquire(const_cast<CommonFiberDomainThread*>(this));
#endif
  }
  inline void TSAN_Release() const {
#ifdef ABSL_HAVE_THREAD_SANITIZER
    __tsan_release(const_cast<CommonFiberDomainThread*>(this));
#endif
  }

 protected:
  const bool reclaim_active_;

 private:
  // TLSed pointer to the current thread iff we are eligible to receive a
  // Schedulable and haven't yet blocked
  CommonFiberDomainThread** EligibleLocalPtr();

  // Bind the schedulable that is about to be run to this thread so that
  // IsBoundToThread and DomainThreadFromSchedulable will return true and this
  // respectively. Returns the Schedulable's work Closure.
  base::scheduling::DomainFunctor BindSchedulable(
      base::scheduling::Schedulable* next) {
    // Copy DomainFunctor before we overwrite work.
    base::scheduling::DomainFunctor result(
        reinterpret_cast<CommonFiberDomain::ExecutableFn>(next->managed.work),
        reinterpret_cast<void*>(next->managed_arg()));

    next->managed.work = this;
    ABSL_RAW_CHECK(
        next->set_managed_flag(CommonFiberDomain::kSchedulableManagedFlagBound),
        "schedulable already bound");
    return result;
  }

  // Runs the thread and then deletes it. Subclasses override WorkLoop
  void Run() final;

  void Exit() override;

  // Set "schedulable" as the next item for this thread to run.
  // REQUIRES: if not called by *this, this thread must be blocked.
  // REQUIRES: "schedulable" may not be bound to any thread.
  void SetNextSchedulable(base::scheduling::Schedulable* schedulable) {
    ABSL_RAW_CHECK(!domain_->IsBoundToThread(schedulable), "already bound");
    ABSL_RAW_CHECK(next_schedulable_ == nullptr,
                   "next_schedulable_ != nullptr");
    domain_->ReportThreadAssignment(tid(), schedulable);
    next_schedulable_ = schedulable;
    // SetNextSchedulable is always followed by resuming this thread's
    // execution. This forms an explicit Release/Acquire edge.
  }

  base::scheduling::Schedulable* next_schedulable_;

  friend CommonFiberDomain;
};

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_FIBER_INTERNAL_FIBER_DOMAIN_SUPPORT_H_
