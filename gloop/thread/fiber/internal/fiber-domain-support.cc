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

#include "gloop/thread/fiber/internal/fiber-domain-support.h"

#include <signal.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "absl/base/internal/raw_logging.h"
#include "absl/flags/flag.h"
#include "absl/numeric/bits.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gloop/base/scheduling/domain.h"
#include "gloop/base/scheduling/low-level-support.h"
#include "gloop/base/scheduling/scheduler.h"
#include "gloop/base/static_threadlocal.h"
#include "gloop/thread/fiber/fiber-internal.h"
#include "gloop/thread/fiber/internal/fiber-thread-options.h"
#include "gloop/thread/fiber/internal/fiber-thread-pool.h"
#include "gloop/thread/thread_options.h"

using ::base::scheduling::Schedulable;

namespace thread {
namespace internal {

// Lives in thread::internal namespace in thread.cc
extern const size_t kDefaultRequiredStackSize;

extern int InternalRequestedStackSizeToStackSizeClass(
    size_t requested_stack_size, int flag_requested_stack_size) {
  if (requested_stack_size == 0) {
    // Default to default thread.cc stack size if flag is set to 0 (legacy
    // behavior)
    if (flag_requested_stack_size == 0) {
      requested_stack_size = kDefaultRequiredStackSize;
    } else {
      requested_stack_size = flag_requested_stack_size;
    }
  }

  constexpr size_t minStackSize = 1 << thread::internal::kMinStackSizeLog2;
  constexpr size_t maxStackSize = 1 << thread::internal::kMaxStackSizeLog2;

  // Make sure we don't request a stack size that results in negative indices.
  const size_t ceil_requested_stack_size =
      std::max<size_t>(minStackSize, requested_stack_size);
  const size_t clipped_and_rounded_requested_stack_size =
      std::min<size_t>(maxStackSize, ceil_requested_stack_size);

  // log2
  return thread::internal::StackSizeToStackSizeClass(
      clipped_and_rounded_requested_stack_size);
}

// We accept stack sizes in the range [1 << kMinStackSizeLog2, 1 <<
// kMaxStackSizeLog2]. We can define an equivalence class on this set such
// that two stack sizes are in the same set if they share the same power of
// two greater than or equal to the stack size itself. Each of these classes
// is assigned a number between 0 and kMaxStackSizeLog2 - kMinStackSizeLog2,
// which we call the stack size class.
// IOW: we want to compute the value of this expression:
// ceil(log_2(stack_size)) - kMinStackSizeLog2.
// = floor(log_2(stack_size-1)) + 1 - kMinStackSizeLog2
// = 64 - leading_zeros(stack_size-1) - kMinStackSizeLog2
int StackSizeToStackSizeClass(size_t stack_size) {
  // NOTE: At one point in time, there was an implementation here that tried
  // to cache this computation in a global, assuming that all threads would
  // have the same stack size. That might be a slight win in micro-benchmarks,
  // but not a win in practice (see <link>)
  return (64 - kMinStackSizeLog2) -
         absl::countl_zero(static_cast<uint64_t>(stack_size - 1));
}

size_t StackSizeClassToStackSize(int stack_class) {
  return 1 << (stack_class + kMinStackSizeLog2);
}

}  // namespace internal
}  // namespace thread

namespace {
int StackSizeClass(base::scheduling::Schedulable* schedulable,
                   int default_stack_size) {
  // DomainTest doesn't attach fibers to their schedulable's so allow the test
  // to do this by returning a default value.
  if (!base::scheduling::IsFiberAttached(schedulable))
    return thread::internal::InternalRequestedStackSizeToStackSizeClass(
        0, default_stack_size);

  size_t requested_stack_size =
      reinterpret_cast<thread::Fiber*>(schedulable->managed_arg())
          ->options()
          .GetStackSize();
  return thread::internal::InternalRequestedStackSizeToStackSizeClass(
      requested_stack_size, default_stack_size);
}
}  // namespace

namespace thread {
namespace internal {
thread::Options ThreadOptionsForSchedulable(
    base::scheduling::Schedulable* schedulable, int default_stack_size) {
  auto opts = thread::Options();
  opts.set_stack_size(thread::internal::StackSizeClassToStackSize(
      StackSizeClass(schedulable, default_stack_size)));
  return opts;
}
}  // namespace internal

CommonFiberDomain::CommonFiberDomain(absl::string_view name_prefix,
                                     int max_concurrency)
    : base::scheduling::Domain(name_prefix, max_concurrency),
      thread_pool_(absl::StrCat(name_prefix, "-thread_pool")),
      default_fiber_stack_size_(
          absl::GetFlag(FLAGS_fibers_default_thread_stack_size)) {
  references_held_.store(1, std::memory_order_release);
}

void CommonFiberDomainThread::Exit() { this->domain_->RawResume(this); }

void CommonFiberDomain::WaitForThreads() {
  // TODO: fix this when we have InternalRef.
  thread_pool_.WaitForThreads();
  // Wait for all the other threads to exit while we are still the right type.
  while (references_held_.load(std::memory_order_acquire) != 1) {
    // spin.
  }
}

CommonFiberDomain::~CommonFiberDomain() { TmpInternalUnref(); }

Schedulable* CommonFiberDomain::CreateExecutableSchedulable(
    base::scheduling::Scheduler* scheduler, ExecutableFn function, void* arg) {
  ABSL_RAW_CHECK(scheduler->domain() == this, "scheduler from remote domain");
  Schedulable* schedulable;
  schedulable = scheduler->NewManagedSchedulable(Schedulable::kWorkItem);
  schedulable->managed.work = reinterpret_cast<void*>(function);
  schedulable->set_managed_arg(reinterpret_cast<intptr_t>(arg));

  return schedulable;
}

STATIC_THREAD_LOCAL_POD(CommonFiberDomainThread*, eligible_local);

CommonFiberDomainThread** CommonFiberDomainThread::EligibleLocalPtr() {
  return eligible_local.pointer();
}

CommonFiberDomainThread* CommonFiberDomain::PickThreadForSchedulable(
    Schedulable* schedulable) {
  // Determine if we should schedule this schedulable should run on a big stack
  // thread. Remember that 0 signifies we will just go with the default stack
  // size set by flag.
  int stack_size_class = StackSizeClass(schedulable, default_fiber_stack_size_);
  // We maintain a per-CommonFiberDomain free-list of previously used
  // threads. This allows us to allocate new execution quickly, without
  // incurring thread creation overheads.
  //
  // When execution of a kWorkItem schedulable finishes, the owning thread will
  // synchronize against thread_list_.num_idle, joining the list if insufficient
  // idle threads exist.
  //
  // A waiting-to-be-recycled CommonFiberDomain thread is always in a
  // domain-blocked state with an assigned DestinationTarget.  This means that
  // when binding a new schedulable to it, we may simply assign it as that
  // schedulable's thread and continue the invoking SwapCurrent() or
  // ResumeAdditionalSchedulable() call directly.
  CommonFiberDomainThread* reused_thread = nullptr;

  // Check for local rebind.
  if (eligible_local.get() != nullptr) {
    reused_thread = eligible_local.get();
    // We can only use this thread if the stack sizes match.
    if (stack_size_class == reused_thread->StackSizeClass()) {
      eligible_local.get() = nullptr;
      reused_thread->SetNextSchedulable(schedulable);
      return nullptr;  // Local rebind requires no resume.
    }
  }

  // Try to grab a thread from the free-list.
  reused_thread = static_cast<CommonFiberDomainThread*>(
      thread_pool_.TryGetIdleThread(stack_size_class));

  if (reused_thread) {
    // We recycled a previously used thread.
    reused_thread->SetNextSchedulable(schedulable);
    return reused_thread;
  }
  StartNewThread(schedulable);
  return nullptr;  // new threads automatically start
}

void CommonFiberDomainThread::Run() {
  domain_->thread_pool_.AddNewActiveThread(this);
  domain_->ReportThreadAssignment(tid(), next_schedulable_);

  WorkLoop();

  delete this;  // CommonFiberDomainThreads are self-deleting.
}

CommonFiberDomainThread::CommonFiberDomainThread(CommonFiberDomain* domain,
                                                 absl::string_view name,
                                                 Schedulable* first)
    : CommonFiberThread(internal::ThreadOptionsForSchedulable(
                            first, domain->default_fiber_stack_size_),
                        name),
      domain_(domain),
      reclaim_active_{
          options().stack_size() > ::thread::internal::kDefaultFiberStackSize,
      },
      next_schedulable_(first) {
  domain_->TmpInternalRef();
}

CommonFiberDomainThread::~CommonFiberDomainThread() {
  domain_->thread_pool_.RemoveActiveThread(this);
  domain_->TmpInternalUnref();
}

}  // namespace thread
