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

// For google3 production Linux, provide Fiber aware overrides of the
// weak symbols AbslInternalPerThreadSemPost() and
// AbslInternalPerThreadSemWait() from
// //absl/synchronization/internal/per_thread_sem.cc

#include <stdint.h>

#include <atomic>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"  // NOLINT(build/include)
#include "absl/base/internal/thread_identity.h"
#include "absl/synchronization/internal/create_thread_identity.h"
#include "absl/synchronization/internal/kernel_timeout.h"
#include "absl/synchronization/internal/per_thread_sem.h"
#include "absl/synchronization/internal/waiter.h"
#include "gloop/base/scheduling/downcalls.h"
#include "gloop/base/scheduling/scheduler.h"
#include "gloop/base/spinlock.h"
#include "tcmalloc/malloc_extension.h"

namespace base {
namespace scheduling_internal {
namespace {

using ::absl::synchronization_internal::Waiter;

void Init(absl::base_internal::ThreadIdentity* identity) {
  new (identity->waiter_state.data) Waiter();
}

void Post(absl::base_internal::ThreadIdentity* identity) {
  Waiter::GetWaiter(identity)->Post();
}

bool Wait(absl::base_internal::ThreadIdentity* identity,
          absl::synchronization_internal::KernelTimeout t) {
  return Waiter::GetWaiter(identity)->Wait(t);
}

void Poke(absl::base_internal::ThreadIdentity* identity) {
  Waiter::GetWaiter(identity)->Poke();
}

}  // namespace
}  // namespace scheduling_internal
}  // namespace base

extern "C" {

// TODO [[maybe_unused]] is to suppress scythe.
[[maybe_unused]] void ABSL_INTERNAL_C_SYMBOL(AbslInternalPerThreadSemInit)(
    absl::base_internal::ThreadIdentity* identity) {
  base::scheduling_internal::Init(identity);
}

// TODO [[maybe_unused]] is to suppress scythe.
[[maybe_unused]] void ABSL_INTERNAL_C_SYMBOL(AbslInternalPerThreadSemPoke)(
    absl::base_internal::ThreadIdentity* identity) {
  base::scheduling_internal::Poke(identity);
}

// TODO [[maybe_unused]] is to suppress scythe.
[[maybe_unused]] void ABSL_INTERNAL_C_SYMBOL(AbslInternalPerThreadSemPost)(
    absl::base_internal::ThreadIdentity* identity) {
  // We use careful double-checked locking to avoid requiring the association
  // lock for non-cooperative threads.
  if (base::scheduling::Schedulable::GetBoundSchedulable(identity) != nullptr) {
    SpinLockHolder l(*identity->scheduler_state.association_lock());
    // Holding the association lock guarantees a consistent read of the thread's
    // bound schedulable (nullptr for non-cooperative threads).  This
    // synchronization is required in the presence of imprecise wake-ups as
    // only the ThreadIdentity object is guaranteed persistent lifetime.
    base::scheduling::Schedulable* schedulable =
        base::scheduling::Schedulable::GetBoundSchedulable(identity);
    if (schedulable != nullptr) {
      base::scheduling::Downcalls::Post(schedulable);
      return;
    }
  }
  base::scheduling_internal::Post(identity);
}

// TODO [[maybe_unused]] is to suppress scythe.
[[maybe_unused]] bool ABSL_INTERNAL_C_SYMBOL(AbslInternalPerThreadSemWait)(
    absl::synchronization_internal::KernelTimeout t) {
  bool timeout = false;
  absl::base_internal::ThreadIdentity* identity =
      absl::synchronization_internal::GetOrCreateCurrentThreadIdentity();

  // Ensure wait_start != 0.
  int ticker = identity->ticker.load(std::memory_order_relaxed);
  identity->wait_start.store(ticker ? ticker : 1, std::memory_order_relaxed);
  identity->is_idle.store(false, std::memory_order_relaxed);

  if (identity->blocked_count_ptr != nullptr) {
    // Increment count of threads blocked in a given thread pool.
    identity->blocked_count_ptr->fetch_add(1, std::memory_order_relaxed);
  }

  if (base::scheduling::Schedulable::GetBoundSchedulable(identity) != nullptr) {
    // We do not require the association lock when blocking as no other thread
    // has the right to modify this field.
    timeout = !base::scheduling::Downcalls::Wait(t);
  } else {
    timeout = !base::scheduling_internal::Wait(identity, t);
  }

  if (identity->blocked_count_ptr != nullptr) {
    identity->blocked_count_ptr->fetch_sub(1, std::memory_order_relaxed);
  }

  if (identity->is_idle.load(std::memory_order_relaxed)) {
    // We became idle during the wait; become non-idle again so that
    // performance of deallocations done from now on does not suffer.
    tcmalloc::MallocExtension::MarkThreadBusy();
  }
  identity->is_idle.store(false, std::memory_order_relaxed);
  identity->wait_start.store(0, std::memory_order_relaxed);
  return !timeout;
}

}  // extern "C"
