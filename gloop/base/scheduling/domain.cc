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

#include "gloop/base/scheduling/domain.h"

#include <atomic>
#include <functional>
#include <list>

#include "absl/base/attributes.h"
#include "absl/base/call_once.h"
#include "absl/base/config.h"
#include "absl/base/const_init.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/debugging/leak_check.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/internal/create_thread_identity.h"
#include "absl/synchronization/internal/kernel_timeout.h"
#include "absl/synchronization/mutex.h"
#include "gloop/base/scheduling/downcalls.h"
#include "gloop/base/scheduling/low-level-support.h"
#include "gloop/base/scheduling/scheduler.h"
#include "gloop/base/spinlock.h"
#include "gloop/base/thread-identity.h"

// Users need to link //thread/fiber/debug:streamz in order to
// get instrumentantation, otherwise
// this def will just be a null function pointer, resulting in no
// instrumentation.
ABSL_ATTRIBUTE_WEAK void Thread_Fiber_InitStreamz() {}

namespace base {
namespace scheduling {
namespace {

using absl::synchronization_internal::KernelTimeout;

// b/35097229: The first construction of a block-scoped thread_local object with
// a dtor depends on a pthread_once initialization containing a block-scoped
// static object (for allocating a pthread_key).  When we co-operatively
// reschedule around the initialization of this (inner) static we may deadlock
// with another co-operative thread waiting on the outer pthread_once; as
// pthread_once is not co-operative.
//
// This can be avoided by guaranteeing that the pthread_key initialization above
// occurs before any co-operative rescheduling is possible.
ABSL_CONST_INIT static absl::once_flag once;

void BlockScopedThreadLocal() {
#if ABSL_HAVE_THREAD_LOCAL  // No need for workaround if thread_local missing.
  struct TypeWithDtor {
    ~TypeWithDtor() = default;
  };

  // TODO: remove the LeakCheckerDisabler eventually.  HeapChecker
  // (but not lsan) reports false positives from standard-library level
  // allocations during the construction of the first block-scoped thread_local.
  absl::LeakCheckDisabler disable_leak_check;
  thread_local TypeWithDtor block_scoped_thread_local;
  static_cast<void>(block_scoped_thread_local);
#endif
}

// All accesses to the domain list must be protected by the mutex below.
std::list<Domain*>* GetDomainList() {
  static std::list<Domain*>* domain_list = new std::list<Domain*>;

  return domain_list;
}

// All accesses to the domain list above must be protected by the mutex.
ABSL_CONST_INIT absl::Mutex domain_list_mutex(absl::kConstInit);

}  // namespace

Domain::Domain(absl::string_view name_prefix, int max_concurrency)
    : max_concurrency_(max_concurrency),
      root_scheduler_(nullptr),
      name_(name_prefix) {
  schedule_from_root_count_.store(0, std::memory_order_release);
  absl::call_once(once, BlockScopedThreadLocal);
  // Make sure we initialize our streamz at least once.
  Thread_Fiber_InitStreamz();
}

void Domain::MarkFullyConstructed() {
  absl::MutexLock lock(domain_list_mutex);
  GetDomainList()->push_back(this);
}

Domain::~Domain() {
  absl::MutexLock lock(domain_list_mutex);
  GetDomainList()->remove(this);
}

// static
void Domain::Iterate(const std::function<void(const Domain*)>& functor) {
  absl::MutexLock lock(domain_list_mutex);
  for (const auto& domain : *GetDomainList()) {
    functor(domain);
  }
}

void Domain::set_root_scheduler(Scheduler* scheduler) {
  ABSL_RAW_CHECK(!root_scheduler_, "domain already has a root scheduler");
  root_scheduler_ = scheduler;
}

void Domain::SetCurrentThreadSchedulable(Schedulable* new_current) {
  absl::base_internal::ThreadIdentity* identity;
  identity = absl::synchronization_internal::GetOrCreateCurrentThreadIdentity();
  // We must synchronize against wake_lock to ensure we do not race with the
  // delivery of a Post() against our previous schedulable.
  SpinLockHolder l(*identity->scheduler_state.association_lock());
  identity->scheduler_state.bound_schedulable.store(new_current,
                                                    std::memory_order_relaxed);
}

// Default implementation simply initiates additional concurrency in domain.
void Domain::DomainStartPotentiallyBlockingRegion(Schedulable* current) {
  ABSL_RAW_CHECK(absl::base_internal::SchedulingGuard::DisableRescheduling(),
                 "Unexpected DomainStartPBR call: no thread identity.");
  Schedulable* to_run = Downcalls::DomainObservedBlocking(current);
  if (to_run) {
    ResumeAdditionalSchedulable(to_run);
  }
}

void Domain::DomainFinishPotentiallyBlockingRegion(Schedulable* current) {
  Schedulable* to_run = Downcalls::DomainObservedWakeup(current);
  if (to_run) {
    if (to_run == current) {
      // We are already within the correct thread, resume execution.
    } else {
      SwapCurrent(current, to_run, KernelTimeout::Never());
    }
  } else {
    // Wait to be scheduled.
    BlockCurrent(current, KernelTimeout::Never());
  }
  absl::base_internal::SchedulingGuard::EnableRescheduling(true);
}

void Domain::ScheduleNextFromRoot() {
  schedule_from_root_count_.fetch_add(1, std::memory_order_release);
}

}  // namespace scheduling
}  // namespace base
