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

#include "gloop/thread/fiber/pthread-domain.h"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <ctime>

#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/flags/flag.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/internal/kernel_timeout.h"
#include "gloop/base/scheduling/domain.h"
#include "gloop/base/scheduling/downcalls.h"
#include "gloop/base/scheduling/scheduler.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/internal/fiber-thread-pool.h"
#include "gloop/thread/os_semaphore.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"

#if !PORTABLE_BASE
#endif

using absl::synchronization_internal::KernelTimeout;
using base::scheduling::Downcalls;
using base::scheduling::Schedulable;

namespace thread {
namespace {

class PthreadDomain : public base::scheduling::Domain {
 public:
  PthreadDomain(absl::string_view name_prefix, int max_concurrency);

  // This type is neither copyable nor movable.
  PthreadDomain(const PthreadDomain&) = delete;
  PthreadDomain& operator=(const PthreadDomain&) = delete;

  // Destructor does not return until all threads associated with this domain
  // have been released.
  // REQUIRES: root_scheduler() is deletable.
  ~PthreadDomain() override;

 protected:
  // Returns true if "schedulable" is bound to a SwitchToDomainThread.
  static bool IsBoundToThread(Schedulable* schedulable);

  // Allocates, and begins running, a new thread to host "schedulable".
  // REQUIRES: !IsBoundToThread(schedulable);
  void AllocateThreadToSchedulable(Schedulable* schedulable);

  // Implementations of interfaces defined by Domain below.
  void ResumeAdditionalSchedulable(Schedulable* additional) override;
  bool BlockCurrent(Schedulable* current, KernelTimeout t) override;
  bool SwapCurrent(Schedulable* current, Schedulable* next,
                   KernelTimeout t) override;

  // REQUIRES: scheduler->domain() == this
  Schedulable* CreateExecutableSchedulable(
      base::scheduling::Scheduler* scheduler, ExecutableFn function,
      void* arg) override;

 private:
  // We use one the first managed flag to track whether a schedulable has been
  // associated with a thread yet.
  enum { kSchedulableManagedFlagBound = 17 };

  void CoordinateBlockingTimeout(Schedulable* current);

  // Some users depend on behavior where the default stack size is checked once
  // upon creation of the domain, and does not change even if
  // FLAGS_fibers_default_stack_size changes later.
  const int default_fiber_stack_size_;
  std::atomic<intptr_t> num_threads_;

  friend class PthreadDomainThread;
};

//------------------------------------------------------------------------------
// PthreadDomainThread
//------------------------------------------------------------------------------

// We use a per-thread POSIX semaphore to modulate an individual
// PthreadDomainThread's runnability.
//
// Resumes consist of sem_post()-ing against the semaphore.
// Blocking consist of a sem_waiting().
// Swaps are a { sem_post(), sem_wait() } pair.
//
// This gives us the commutative semantics required for a domain with respect to
// ordering of Resume/Block/Swap operations.
//
// When IsBoundToThread(schedulable) == true:
//   schedulable is bound to a thread.
//   schedulable->managed.work == address of semaphore above
// When IsBoundToThread(schedulable) == false:
//   schedulable is not yet bound to any thread.
//   schedulable->managed.work == address of closure to execute.
class PthreadDomainThread : public Thread {
 public:
  // A new PthreadDomainThread will always immediately begin execution of
  // 'first_schedulable'.  This allows work to be specified without
  // synchronizing on thread creation.
  //
  // REQUIRES: first_schedulable != nullptr
  PthreadDomainThread(PthreadDomain* domain, const Options& options,
                      Schedulable* first_schedulable)
      : Thread(options, absl::StrCat(domain->name(), "-PDomainT")),
        domain_(domain),
        current_schedulable_(first_schedulable),
        work_(nullptr, nullptr) {
    internal::OsSemaphoreInit(&sem_);
  }

 private:
  // PthreadDomainThreads are self-deleting.
  ~PthreadDomainThread() override { internal::OsSemaphoreDestroy(&sem_); }

  // Bind "schedulable" to this thread.  May only be called by *this.
  void BindSchedulable(Schedulable* schedulable) {
    ABSL_RAW_CHECK(schedulable->set_managed_flag(
                       PthreadDomain::kSchedulableManagedFlagBound),
                   "schedulable already bound");
    current_schedulable_ = schedulable;
    work_ = base::scheduling::DomainFunctor(
        reinterpret_cast<PthreadDomain::ExecutableFn>(
            schedulable->managed.work),
        reinterpret_cast<void*>(schedulable->managed_arg()));
    // Now that "schedulable" is bound to a thread, replace work with the
    // controlling semaphore.
    schedulable->managed.work = reinterpret_cast<void*>(&sem_);
  }

  void Run() override {
    // This is "first_schedulable"; we need to assign it to ourselves now that
    // we have an identifiable thread.
    BindSchedulable(current_schedulable_);

    do {
      ABSL_RAW_DCHECK(current_schedulable_ != nullptr,
                      "current_schedulable_ == nullptr");

      PthreadDomain::SetCurrentThreadSchedulable(current_schedulable_);
      work_();
      PthreadDomain::SetCurrentThreadSchedulable(nullptr);

      Schedulable* prev = current_schedulable_;
      current_schedulable_ = nullptr;
      Schedulable* next = Downcalls::DomainObservedBlocking(prev);
      DeleteSchedulable(prev);

      if (next) {
        if (!PthreadDomain::IsBoundToThread(next)) {
          // This is the only time we ever recycle in a pthread domain.
          BindSchedulable(next);
        } else {
          domain_->ResumeAdditionalSchedulable(next);  // Maintain concurrency.
        }
      }
    } while (current_schedulable_);
    domain_->num_threads_.fetch_sub(1, std::memory_order_seq_cst);
    delete this;  // PthreadDomainThreads are self-deleting.
  }

  PthreadDomain* const domain_;
  Schedulable* current_schedulable_;
  base::scheduling::DomainFunctor work_;
  internal::OsSemaphore sem_;
};

//------------------------------------------------------------------------------
// PthreadDomain
//------------------------------------------------------------------------------

static inline internal::OsSemaphore* SemForSchedulable(
    Schedulable* schedulable) {
  return reinterpret_cast<internal::OsSemaphore*>(schedulable->managed.work);
}

PthreadDomain::PthreadDomain(absl::string_view name_prefix, int max_concurrency)
    : base::scheduling::Domain(name_prefix, max_concurrency),
      default_fiber_stack_size_(
          absl::GetFlag(FLAGS_fibers_default_thread_stack_size)) {
  num_threads_.store(0, std::memory_order_release);
  MarkFullyConstructed();
}

PthreadDomain::~PthreadDomain() {
  // This could be made cheaper, but by specification (that the root_scheduler()
  // is deletable) all threads should be on their way out.
  while (num_threads_.load(std::memory_order_acquire) != 0) {
  }
}

inline bool PthreadDomain::IsBoundToThread(
    base::scheduling::Schedulable* schedulable) {
  return schedulable->is_flag_set(kSchedulableManagedFlagBound);
}

inline void PthreadDomain::AllocateThreadToSchedulable(
    Schedulable* schedulable) {
  ABSL_RAW_DCHECK(!IsBoundToThread(schedulable), "schedulable already bound");
  PthreadDomainThread* thread;
  thread = new PthreadDomainThread(this,
                                   internal::ThreadOptionsForSchedulable(
                                       schedulable, default_fiber_stack_size_),
                                   schedulable);
  num_threads_.fetch_add(1, std::memory_order_relaxed);
  thread->Start();
}

Schedulable* PthreadDomain::CreateExecutableSchedulable(
    base::scheduling::Scheduler* scheduler, ExecutableFn function, void* arg) {
  ABSL_RAW_CHECK(scheduler->domain() == this, "scheduler from remote domain");
  Schedulable* schedulable;
  schedulable = scheduler->NewManagedSchedulable(Schedulable::kWorkItem);
  schedulable->managed.work = reinterpret_cast<void*>(function);
  schedulable->set_managed_arg(reinterpret_cast<intptr_t>(arg));

  return schedulable;
}

void PthreadDomain::CoordinateBlockingTimeout(
    base::scheduling::Schedulable* current) {
  Schedulable* to_run = Downcalls::DomainObservedTimeout(current);
  SwapOrBlockCurrent(current, to_run, KernelTimeout::Never());
}

void PthreadDomain::ResumeAdditionalSchedulable(Schedulable* additional) {
  if (!IsBoundToThread(additional)) {
    AllocateThreadToSchedulable(additional);  // Execution begins automatically.
  } else {
    internal::OsSemaphorePost(SemForSchedulable(additional));
  }
}

bool PthreadDomain::BlockCurrent(Schedulable* current, KernelTimeout t) {
  int rc;
  do {
    if (t.has_timeout()) {
      struct timespec abs = t.MakeAbsTimespec();
      rc = internal::OsSemaphoreTimedWait(SemForSchedulable(current), &abs);
    } else {
      rc = internal::OsSemaphoreWait(SemForSchedulable(current));
    }
  } while (rc == -1 && errno == EINTR);

  if (rc == -1) {
    ABSL_RAW_CHECK(errno == ETIMEDOUT, "unexpected wait exit");
    CoordinateBlockingTimeout(current);
    return false;
  }

  return true;
}

bool PthreadDomain::SwapCurrent(Schedulable* current, Schedulable* next,
                                KernelTimeout t) {
  ABSL_RAW_DCHECK(current != nullptr, "!current");
  ABSL_RAW_DCHECK(next != nullptr, "!next");

  ResumeAdditionalSchedulable(next);
  return BlockCurrent(current, t);
}

}  // namespace

base::scheduling::Domain* NewPthreadDomain(absl::string_view name_prefix,
                                           int max_concurrency) {
  return new PthreadDomain(name_prefix, max_concurrency);
}

}  // namespace thread
