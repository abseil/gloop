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

#include "gloop/base/scheduling/scheduler.h"

#include <stdint.h>

#include <atomic>

#include "absl/base/internal/raw_logging.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/die_if_null.h"
#include "absl/log/log.h"
#include "gloop/base/examine_stack.h"
#include "gloop/base/scheduling/domain.h"
#include "gloop/base/scheduling/downcalls.h"

namespace base {
namespace scheduling {
namespace {
int SafeSlots(const Scheduler* scheduler, int slots) {
  if (scheduler->num_slots() < slots) {
    LOG(DFATAL) << "Scheduler num_slots must be <= parent. num_slots: " << slots
                << " parent num_slots: " << scheduler->num_slots() << " "
                << base::CurrentStackTrace();
    slots = scheduler->num_slots();
  }
  return slots;
}
}  // namespace

//------------------------------------------------------------------------------
// Schedulable implementation.
//------------------------------------------------------------------------------
Schedulable::Schedulable(Scheduler* manager, Type type)
    : type(type), manager(manager), managing_slot(Slot::NullSlot()) {
  // Schedulables are always created in a blocked state.
  runnable_count.store(-1, std::memory_order_release);
  flags_.store(0, std::memory_order_relaxed);
#if BASE_SCHEDULABLE_IS_CACHELINE_ALIGNED
  // Schedulables must be cache-aligned.
  DCHECK_EQ(0, reinterpret_cast<intptr_t>(this) & (ABSL_CACHELINE_SIZE - 1));
#endif
}

//------------------------------------------------------------------------------
// Scheduler implementation.
//------------------------------------------------------------------------------
Scheduler::Scheduler(Domain* absl_nonnull domain)
    : domain_(*ABSL_DIE_IF_NULL(domain)),
      parent_(nullptr),
      depth_(0),
      num_slots_(domain->max_concurrency()),
      parent_arg1_(0) {
  orphaned_.store(false, std::memory_order_relaxed);
  domain->set_root_scheduler(this);
}

Scheduler::Scheduler(Scheduler* parent, int max_slots)
    : domain_(parent->domain_),
      parent_(parent),
      depth_(parent->depth() + 1),
      num_slots_(SafeSlots(parent, max_slots)),
      parent_arg1_(0) {
  parent->Ref();  // Parent's lifetime is to span that of *this.
  orphaned_.store(false, std::memory_order_relaxed);
}

Scheduler::~Scheduler() {
  ABSL_RAW_DCHECK(orphaned_.load(std::memory_order_relaxed),
                  "unmatched Unref()");
  if (parent_) {
    parent_->Unref();  // May not dereference parent_ below this line.
  }
}

void Scheduler::Orphan() {
  ABSL_RAW_DCHECK(!orphaned_.load(std::memory_order_relaxed), "double orphan");
  orphaned_.store(true, std::memory_order_relaxed);
  NotifyOrphaned();
  Unref();  // Pairs with the reference taken in the base constructor.
}

// Returns a root level slot schedulable managing "scheduler".
static Schedulable* NewRootSlotSchedulable(Scheduler* scheduler) {
  // Root schedulables manage the root level scheduler within the domain and so
  // have no queueing_scheduler.  The set of root schedulables may be thought as
  // equivalent to the number of scheduling slots for a domain.
  //
  // They provide us a starting point for all top-level scheduling decisions as
  // well as allowing us to advertise additional parallelism about wake-ups.
  Schedulable* result = new Schedulable(nullptr, Schedulable::kChildSlot);
#if BASE_SCHEDULABLE_IS_CACHELINE_ALIGNED
  ABSL_RAW_DCHECK(
      reinterpret_cast<uintptr_t>(result) % ABSL_CACHELINE_SIZE == 0,
      "Misaligned Schedulable");
#endif
  result->managed.scheduler = scheduler;
  return result;
}

Slot Scheduler::NewManagingSlot() {
  Schedulable* result;
  if (parent_ == nullptr) {
    result = NewRootSlotSchedulable(this);
  } else {
    result = parent_->NewManagedSchedulable(Schedulable::kChildSlot);
    result->managed.scheduler = this;
  }

  return Slot::FromSchedulable(result);
}

void Scheduler::DeleteManagingSlot(Slot slot) {
  ABSL_RAW_DCHECK(slot.managed_scheduler() == this,
                  "deleting non-managing slot");
  DeleteSchedulable(slot.schedulable_);
}

void Scheduler::Release() {
  struct Helper {
    // Scheduler* passed in arg.
    static void DeleteScheduler(void* arg) {
      Scheduler* scheduler = reinterpret_cast<Scheduler*>(arg);
      delete scheduler;
    }
  };

  // We must be careful not to delete *this while potentially involved in a
  // scheduling decision as we cannot safely synchronize one of our
  // managing_slots being 'returned' (which may have triggered the final
  // Unref()) versus subsequently un-scheduling that slot against our parent.
  Downcalls::RunWhenSchedulingAllowed(Helper::DeleteScheduler, this);
}

Schedulable* Scheduler::NewManagedSchedulable(Schedulable::Type type) {
  Ref();
  Schedulable* result = new Schedulable(this, type);
#if BASE_SCHEDULABLE_IS_CACHELINE_ALIGNED
  ABSL_RAW_DCHECK(
      reinterpret_cast<uintptr_t>(result) % ABSL_CACHELINE_SIZE == 0,
      "Misaligned Schedulable");
#endif
  return result;
}

void Scheduler::DeleteManagedSchedulable(Schedulable* schedulable) {
  delete schedulable;
  Unref();
}

void DeleteSchedulable(Schedulable* schedulable) {
  Scheduler* scheduler = schedulable->manager;
  if (scheduler) {
    scheduler->DeleteManagedSchedulable(schedulable);
  } else {
    delete schedulable;
  }
}

void Scheduler::ScheduleAdditionalSlot(Slot slot) {
  struct Helper {
    // Wrapper allowing us to explicitly cast the argument instead of the
    // function pointer
    static void PostSlot(void* slot_schedulable) {
      Downcalls::Post(static_cast<Schedulable*>(slot_schedulable));
    }
  };
  ABSL_RAW_CHECK(slot.managed_scheduler() == this,
                 "slot does not manage *this");
  Downcalls::RunWhenSchedulingAllowed(Helper::PostSlot, slot.schedulable_);
}

}  // namespace scheduling
}  // namespace base
