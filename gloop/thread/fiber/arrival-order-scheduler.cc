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

#include "gloop/thread/fiber/arrival-order-scheduler.h"

#include <cstdint>
#include <deque>
#include <queue>
#include <vector>

#include "absl/base/internal/raw_logging.h"
#include "absl/container/inlined_vector.h"
#include "gloop/base/scheduling/domain.h"
#include "gloop/base/scheduling/scheduler.h"
#include "gloop/thread/fiber/scheduler-types.h"

using base::scheduling::Domain;
using base::scheduling::Schedulable;
using base::scheduling::Scheduler;
using base::scheduling::Slot;

namespace thread {

//------------------------------------------------------------------------------
// ArrivalOrderSchedulerOptions
//------------------------------------------------------------------------------

ArrivalOrderSchedulerOptions::ArrivalOrderSchedulerOptions()
    : admission_limit_(-1) {}

ArrivalOrderSchedulerOptions& ArrivalOrderSchedulerOptions::set_admission_limit(
    int admission_limit) {
  admission_limit_ = admission_limit;
  return *this;
}

namespace {

//------------------------------------------------------------------------------
// ArrivalOrderScheduler
//------------------------------------------------------------------------------

static inline uint64_t* Generation(Schedulable* schedulable) {
  // We maintain ordering using a 64-bit generation counter, set at
  // first-wakeup.
  return &schedulable->manager_num1;
}

// We must treat all slots of a child Scheduler equivalently.
// To do this we link all slots from the same scheduler together using
// "manager_ptr1" as a link.
//
// scheduler->parent_arg1() is always points to the head of this list.
static inline Schedulable** NextSchedulerSlot(Schedulable* schedulable) {
  return reinterpret_cast<Schedulable**>(&schedulable->manager_ptr1);
}

static inline Schedulable* SchedulerSlotList(Scheduler* child_scheduler) {
  return reinterpret_cast<Schedulable*>(child_scheduler->parent_arg1());
}

static void AddToSchedulerSlotList(Scheduler* child_scheduler,
                                   Schedulable* child_slot) {
  Schedulable* head = SchedulerSlotList(child_scheduler);
  *NextSchedulerSlot(child_slot) = head;
  child_scheduler->set_parent_arg1(reinterpret_cast<intptr_t>(child_slot));
}

static void RemoveFromSchedulerSlotList(Scheduler* child_scheduler,
                                        Schedulable* child_slot) {
  Schedulable* curr = SchedulerSlotList(child_scheduler);

  if (curr == child_slot) {  // Handle being the list head.
    child_scheduler->set_parent_arg1(
        reinterpret_cast<intptr_t>(*NextSchedulerSlot(child_slot)));
  } else {
    while (*NextSchedulerSlot(curr) != child_slot) {  // Otherwise find in list.
      curr = *NextSchedulerSlot(curr);
    }
    *NextSchedulerSlot(curr) = *NextSchedulerSlot(child_slot);
  }
}

struct OrderSchedulables {
  // A strict weak ordering by arrival (first wakeup) order.  We must invert the
  // actual comparison since generations are numbered incrementally (from zero).
  bool operator()(Schedulable* a, Schedulable* b) const {
    return *Generation(a) > *Generation(b);
  }
};

class ArrivalOrderScheduler : public base::scheduling::Scheduler {
 public:
  // When admission limit is specified, it is a hard bound on how many
  // concurrent managed schedulables will be allowed to run.  No limit is
  // indicated by "-1".
  // REQUIRES: admission_limit == -1 || admission_limit > 0
  ArrivalOrderScheduler(Domain* domain,
                        const ArrivalOrderSchedulerOptions& options);
  ArrivalOrderScheduler(Scheduler* parent, int max_slots,
                        const ArrivalOrderSchedulerOptions& options);

  // This type is neither copyable nor movable.
  ArrivalOrderScheduler(const ArrivalOrderScheduler&) = delete;
  ArrivalOrderScheduler& operator=(const ArrivalOrderScheduler&) = delete;

 private:
  // Implementations of interfaces defined by Scheduler.
  // See:
  // https://github.com/abseil/gloop/tree/main/gloop/base/scheduling/scheduler.h
  Slot Wake(Schedulable* to_wake) override;
  Schedulable* ScheduleManaged(Slot slot, Schedulable* prev,
                               bool runnable) override;
  bool StopRunning(Slot slot, Schedulable* current, bool runnable) override;

  // Acquires a reference.  Generation of returned schedulable is initialized to
  // zero.
  Schedulable* NewManagedSchedulable(Schedulable::Type type) override;

  // REQUIRES: May only be called by Release().
  ~ArrivalOrderScheduler() override;
  void Init(const ArrivalOrderSchedulerOptions& options);

  // Releases a reference.  Will admit a schedulable from the held_ queue if
  // present.
  void DeleteManagedSchedulable(Schedulable* schedulable) override;

  internal::CombinerLock combiner_lock_;
  // All fields below are protected by combiner_lock_.
  // The following invariants are always true:
  //   idle_slots_.size() + active_slots_ == Scheduler::num_slots()
  //   queued_or_running_ >= active_slots_
  int active_slots_, queued_or_running_;
  const bool has_admission_limit_;
  int remaining_admission_limit_;
  int64_t generation_;
  std::priority_queue<Schedulable*, std::vector<Schedulable*>,
                      OrderSchedulables>
      queue_;
  std::deque<Schedulable*> held_;
  std::vector<Slot> idle_slots_;
};

ArrivalOrderScheduler::ArrivalOrderScheduler(
    base::scheduling::Domain* domain,
    const ArrivalOrderSchedulerOptions& options)
    : Scheduler(domain),
      has_admission_limit_(options.admission_limit() != -1),
      idle_slots_(num_slots()) {
  Init(options);
}

ArrivalOrderScheduler::ArrivalOrderScheduler(
    Scheduler* parent, int max_slots,
    const ArrivalOrderSchedulerOptions& options)
    : Scheduler(parent, max_slots),
      has_admission_limit_(options.admission_limit() != -1),
      idle_slots_(num_slots()) {
  Init(options);
}

void ArrivalOrderScheduler::Init(const ArrivalOrderSchedulerOptions& options) {
  for (int i = 0; i < num_slots(); i++) {
    idle_slots_[i] = NewManagingSlot();
  }
  remaining_admission_limit_ = options.admission_limit();
  generation_ = 1;  // Must start at 1 since we use 0 to represent un-admitted.
  active_slots_ = queued_or_running_ = 0;
}

ArrivalOrderScheduler::~ArrivalOrderScheduler() {
  ABSL_RAW_CHECK(active_slots_ == 0, "scheduler not idle");
  ABSL_RAW_CHECK(held_.empty(), "remaining held elements");
  for (int i = 0; i < num_slots(); i++) {
    DeleteManagingSlot(idle_slots_[i]);
  }
}

Schedulable* ArrivalOrderScheduler::NewManagedSchedulable(
    Schedulable::Type type) {
  Schedulable* result = Scheduler::NewManagedSchedulable(type);
  *Generation(result) = 0;
  *NextSchedulerSlot(result) = nullptr;  // Will add on wake-up under lock.
  return result;
}

Slot ArrivalOrderScheduler::Wake(Schedulable* to_wake) {
  struct Combinable {
    struct Args {
      ArrivalOrderScheduler* scheduler;
      Schedulable* to_wake;
    };

    // Helper for Wake(), may not be called externally.
    static bool TryAdmitSchedulable(ArrivalOrderScheduler* scheduler,
                                    Schedulable* to_admit) {
      if (to_admit->type == Schedulable::kChildSlot) {
        Scheduler* child = to_admit->managed.scheduler;
        Schedulable* head = SchedulerSlotList(child);
        AddToSchedulerSlotList(child, to_admit);
        if (head != nullptr) {
          if (*Generation(head) != 0) {
            *Generation(to_admit) = *Generation(head);
            return true;
          } else {
            // We were not admitted and the slot schedulable representing
            // "child" is already present in the held queue.
            return false;
          }
        }
      }

      // We perform admission control on first wake-up.  If "to_admit" is a
      // kChildSlot schedulable then it is the first seen for that scheduler.
      if (scheduler->remaining_admission_limit_ == 0) {
        // No capacity remaining, enter a FIFO held queue until
        // DeleteManagedSchedulable() can admit us.
        scheduler->held_.push_back(to_admit);
        return false;
      } else {
        // Remaining capacity was either positive or -1 (no limit).  In the
        // positive case an admission limit has been set and we must account
        // for "to_wake"'s admission.
        if (scheduler->remaining_admission_limit_ > 0) {
          scheduler->remaining_admission_limit_--;
        }
        *Generation(to_admit) = scheduler->generation_++;
        return true;  // Admitted, generation assigned.
      }
    }

    static intptr_t Wake(void* void_args) {
      auto* args = static_cast<Args*>(void_args);
      ArrivalOrderScheduler* scheduler = args->scheduler;
      Schedulable* to_wake = args->to_wake;
      Slot result = Slot::NullSlot();

      if (*Generation(to_wake) == 0 &&
          !TryAdmitSchedulable(scheduler, to_wake)) {
        return result.AsWord();  // Not admitted, null slot.
      }

      scheduler->queue_.push(to_wake);
      scheduler->queued_or_running_++;
      if (scheduler->active_slots_ < scheduler->num_slots()) {
        scheduler->active_slots_++;  // Wake additional slot.
        result = scheduler->idle_slots_.back();
        scheduler->idle_slots_.pop_back();
      }

      return result.AsWord();
    }
  };

  Combinable::Args args;
  args.scheduler = this;
  args.to_wake = to_wake;
  return Slot::FromWord(combiner_lock_.ExecuteLocked(Combinable::Wake, &args));
}

Schedulable* ArrivalOrderScheduler::ScheduleManaged(Slot slot,
                                                    Schedulable* prev,
                                                    bool runnable) {
  struct Combinable {
    struct Args {
      ArrivalOrderScheduler* scheduler;
      Slot slot;
      Schedulable* prev;
      bool runnable;
    };

    static intptr_t ScheduleManaged(void* void_args) {
      auto* args = static_cast<Args*>(void_args);
      ArrivalOrderScheduler* scheduler = args->scheduler;
      Schedulable* result = nullptr;

      bool still_runnable = false;
      if (args->runnable) {
        // TODO: Consider aging the generation of a voluntarily yielding
        // schedulable.  This would eliminate yielding as source of deadlock.
        // This could alternatively consist of aging the generation each time a
        // TimeNotification is delivered.
        scheduler->queue_.push(args->prev);
        still_runnable = true;
      } else {
        if (!args->prev) {
          still_runnable = true;  // Scheduling woken slot, work is guaranteed.
        } else {
          scheduler->queued_or_running_--;
          if (scheduler->queued_or_running_ >= scheduler->active_slots_) {
            // Reserved additional work.
            still_runnable = true;
          } else {
            // This slot going idle.
            scheduler->active_slots_--;
            scheduler->idle_slots_.push_back(args->slot);
          }
        }
      }

      if (still_runnable) {
        result = scheduler->queue_.top();
        scheduler->queue_.pop();
      }
      return reinterpret_cast<intptr_t>(result);
    }
  };

  Combinable::Args args;
  args.scheduler = this;
  args.slot = slot;
  args.prev = prev;
  args.runnable = runnable;
  return reinterpret_cast<Schedulable*>(
      combiner_lock_.ExecuteLocked(Combinable::ScheduleManaged, &args));
}

void ArrivalOrderScheduler::DeleteManagedSchedulable(Schedulable* schedulable) {
  struct Combinable {
    struct Args {
      ArrivalOrderScheduler* scheduler;
      Schedulable* to_delete;
      absl::InlinedVector<Slot, 4> admitted_slots;
    };

    // Returns a bool indicating whether new slots were admitted.
    static intptr_t TryAdmitSchedulable(void* void_args) {
      auto* args = static_cast<Args*>(void_args);
      ArrivalOrderScheduler* scheduler = args->scheduler;
      Schedulable* to_delete = args->to_delete;

      ABSL_RAW_DCHECK(*Generation(to_delete) != 0,
                      "unadmitted schedulable deleted");
      if (to_delete->type == Schedulable::kChildSlot) {
        Scheduler* child_scheduler = to_delete->managed.scheduler;
        RemoveFromSchedulerSlotList(child_scheduler, to_delete);
        if (SchedulerSlotList(child_scheduler) != nullptr) {
          // Outstanding slots for this generation still exist.
          return false;
        }
      }

      if (scheduler->held_.empty()) {
        scheduler->remaining_admission_limit_++;
        return false;  // Nothing to admit.
      }

      Schedulable* admitted = scheduler->held_.front();
      scheduler->held_.pop_front();
      int64_t generation = scheduler->generation_++;

      if (admitted->type == Schedulable::kChildSlot) {
        // When holding back a scheduler we only enqueue one of its slots on the
        // held queue.  The rest are stored in a list, ensure we have the head.
        admitted = SchedulerSlotList(admitted->managed.scheduler);
      }

      bool woke_slots = false;
      do {
        *Generation(admitted) = generation;
        scheduler->queue_.push(admitted);

        // Potentially wake new slot.
        scheduler->queued_or_running_++;
        if (scheduler->active_slots_ < scheduler->num_slots() &&
            scheduler->active_slots_ < scheduler->queued_or_running_) {
          woke_slots = true;
          scheduler->active_slots_++;
          Slot woken_slot = scheduler->idle_slots_.back();
          scheduler->idle_slots_.pop_back();
          args->admitted_slots.push_back(woken_slot);
        }

        // Note: Always nullptr for kWorkItem schedulables.
        admitted = *NextSchedulerSlot(admitted);
      } while (admitted != nullptr);

      return woke_slots;
    }
  };

  // If an admission limit is set then this is where we must potentially admit a
  // new schedulable -- if it exists.
  if (has_admission_limit_ && *Generation(schedulable) != 0) {
    Combinable::Args args;

    args.scheduler = this;
    args.to_delete = schedulable;
    if (combiner_lock_.ExecuteLocked(Combinable::TryAdmitSchedulable, &args)) {
      // We return these explicitly to avoid potential re-entrancy with ::Wake()
      // which is also co-ordinated using combiner_lock_.
      for (auto slot : args.admitted_slots) {
        ScheduleAdditionalSlot(slot);
      }
    }
  }

  delete schedulable;
  Unref();
}

bool ArrivalOrderScheduler::StopRunning(Slot slot, Schedulable* current,
                                        bool runnable) {
  struct Combinable {
    struct Args {
      ArrivalOrderScheduler* scheduler;
      Slot slot;
      Schedulable* current;
      bool runnable;
    };

    static intptr_t StopRunning(void* void_args) {
      auto* args = static_cast<Args*>(void_args);
      ArrivalOrderScheduler* scheduler = args->scheduler;
      bool result;

      if (args->runnable) {
        scheduler->queue_.push(args->current);
        result = true;
      } else {
        // "current" is no longer runnable, we must check to see whether there
        // is enough runnable concurrency below this scheduler to keep "slot"
        // active.
        scheduler->queued_or_running_--;
        if (scheduler->queued_or_running_ < scheduler->active_slots_) {
          scheduler->idle_slots_.push_back(args->slot);
          scheduler->active_slots_--;
          result = false;
        } else {
          result = true;
        }
      }

      return result;
    }
  };

  Combinable::Args args;
  args.scheduler = this;
  args.slot = slot;
  args.current = current;
  args.runnable = runnable;
  return combiner_lock_.ExecuteLocked(Combinable::StopRunning, &args);
}

}  // namespace

Scheduler* NewRootArrivalOrderScheduler(
    Domain* domain, const ArrivalOrderSchedulerOptions& options) {
  return new ArrivalOrderScheduler(domain, options);
}

Scheduler* NewChildArrivalOrderScheduler(
    Scheduler* parent, int num_slots,
    const ArrivalOrderSchedulerOptions& options) {
  return new ArrivalOrderScheduler(parent, num_slots, options);
}

}  // namespace thread
