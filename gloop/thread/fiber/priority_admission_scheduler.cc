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

#include "gloop/thread/fiber/priority_admission_scheduler.h"

#include <atomic>
#include <cstdint>

#include "absl/base/nullability.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/log/vlog_is_on.h"
#include "gloop/base/raw_logging.h"
#include "gloop/base/scheduling/domain.h"
#include "gloop/base/scheduling/scheduler.h"

using base::scheduling::Domain;
using base::scheduling::Schedulable;
using base::scheduling::Scheduler;
using base::scheduling::Slot;

namespace thread {

namespace internal {

Schedulable* absl_nonnull LinkedSchedulableList::Pop() {
  ABSL_RAW_DCHECK(!empty(), "pop empty list");
  Schedulable* result = head_;
  head_ = static_cast<Schedulable*>(head_->manager_ptr1);
  if (head_ == nullptr) tail_ = nullptr;
  return result;
}

void LinkedSchedulableList::PushHead(Schedulable* absl_nonnull schedulable) {
  if (head_ == nullptr) {
    schedulable->manager_ptr1 = nullptr;
    head_ = tail_ = schedulable;
  } else {
    schedulable->manager_ptr1 = head_;
    head_ = schedulable;
  }
}

void LinkedSchedulableList::PushTail(Schedulable* absl_nonnull schedulable) {
  if (head_ == nullptr) {
    schedulable->manager_ptr1 = nullptr;
    head_ = tail_ = schedulable;
  } else {
    schedulable->manager_ptr1 = nullptr;
    tail_->manager_ptr1 = schedulable;
    tail_ = schedulable;
  }
}

}  // namespace internal

PriorityAdmissionScheduler::PriorityAdmissionScheduler(
    Domain* absl_nonnull domain, int num_priorities)
    : Scheduler(domain),
      num_priorities_(num_priorities),
      per_priority_(num_priorities),
      idle_slots_(num_slots()) {
  for (int i = 0; i < num_slots(); ++i) {
    idle_slots_[i] = NewManagingSlot();
  }
}

PriorityAdmissionScheduler::PriorityAdmissionScheduler(
    Scheduler* absl_nonnull parent, int slots, int num_priorities)
    : Scheduler(parent, slots),
      num_priorities_(num_priorities),
      per_priority_(num_priorities),
      idle_slots_(num_slots()) {
  for (int i = 0; i < num_slots(); ++i) {
    idle_slots_[i] = NewManagingSlot();
  }
}

PriorityAdmissionScheduler::~PriorityAdmissionScheduler() {
  for (Slot& slot : idle_slots_) {
    DeleteManagingSlot(slot);
  }
}

static constexpr int kFlagIndexForAdmission = 0;

// ---------------- helpers on parent_arg1 encoding ----------------
//
// The type of content stored in parent_arg1 is determined by the last two
// bits:
//
// 0th bit - 1 implies the higher bits are a pointer, 0 implies priority in
//           higher bits
// 1st bit - if pointer, encodes if unadmitted or admitted
//
//   0 (00): priority encoded in higher bits
//   1 (01): unadmitted, encoded pointer to the tail managing slot in the queue
//   2 (10): reserved
//   3 (11): admitted, encoded pointer to the head of all the managing slots
//
// The following helper functions encode/decode parent_arg1 based on the rules
// above.
//
// When unadmitted, the managing slots are siblings in the per priority
// queue, linked with manager_ptr1 (through LinkedSchedulableList). After
// admitted, they are linked with manager_num1.

static bool HoldsPriority(const Scheduler* child) {
  const intptr_t p = child->parent_arg1();
  if ((p & 1) == 0) {
    ABSL_RAW_DCHECK((p & 3) != 2, "invalid, bit value 10 not used");
    return true;
  } else {
    return false;
  }
}

static bool IsPointerAdmitted(const Scheduler* child) {
  const intptr_t p = child->parent_arg1();
  ABSL_RAW_DCHECK((p & 1) == 1, "parent_arg1 not a pointer");
  return p & 2;
}

static Schedulable* GetPointer(const Scheduler* child) {
  const intptr_t p = child->parent_arg1();
  ABSL_RAW_DCHECK((p & 1) == 1, "parent_arg1 not a pointer");
  return reinterpret_cast<Schedulable*>(p & ~static_cast<intptr_t>(3));
}

static void SetChildSchedulerPointer(Scheduler* child, bool admitted,
                                     Schedulable* sched) {
  intptr_t ptr = reinterpret_cast<intptr_t>(sched);
  child->set_parent_arg1(ptr | (admitted ? 3 : 1));

  // Checks that these helpers are correctly implemented.
  ABSL_RAW_DCHECK(admitted == IsPointerAdmitted(child), "bad pointer");
  ABSL_RAW_DCHECK(sched == GetPointer(child), "bad pointer");
}

static int GetPriority(const Scheduler* child) {
  const intptr_t p = child->parent_arg1();
  ABSL_RAW_DCHECK((p & 3) == 0, "parent_arg1 not a priority");
  return p >> 2;
}

static void SetChildSchedulerPriority(Scheduler* child, int priority) {
  child->set_parent_arg1(priority << 2);
  ABSL_RAW_DCHECK(GetPriority(child) == priority, "priority corrupt");
}

// ---------------- end helpers on parent_arg1 encoding ----------------

void PriorityAdmissionScheduler::SetChildPriority(Scheduler* absl_nonnull child,
                                                  int priority) {
  CHECK(priority >= 0 && priority < num_priorities_)
      << priority << " out of [0," << num_priorities_ << ")";
  CHECK_EQ(this, child->parent()) << "not a child scheduler";
  SetChildSchedulerPriority(child, priority);
}

void PriorityAdmissionScheduler::DeleteManagedSchedulable(
    Schedulable* schedulable) {
  struct Combinable {
    struct Args {
      PriorityAdmissionScheduler* scheduler;
      Schedulable* schedulable;
    };

    static intptr_t DeleteChildSlot(Args* args) {
      args->scheduler->DeleteChildSlotLocked(args->schedulable);
      return 0;
    }
  };

  if (schedulable->is_flag_set(kFlagIndexForAdmission)) {
    if (schedulable->type == Schedulable::kChildSlot) {
      Combinable::Args args{this, schedulable};
      lock_.ExecuteLocked(Combinable::DeleteChildSlot, &args);
    } else {
      ABSL_RAW_DCHECK(schedulable->type == Schedulable::kWorkItem,
                      "invalid type");
      num_running_.fetch_sub(1, std::memory_order_relaxed);
    }
  }

  // Below is essentially Scheduler::DeleteManagedSchedulable.
  delete schedulable;
  Unref();
}

Slot PriorityAdmissionScheduler::Wake(Schedulable* absl_nonnull schedulable) {
  struct Combinable {
    struct Args {
      PriorityAdmissionScheduler* scheduler;
      Schedulable* schedulable;
    };

    static intptr_t Wake(Args* args) {
      return args->scheduler->WakeLocked(args->schedulable).AsWord();
    }
  };

  Combinable::Args args{this, schedulable};
  Slot result = Slot::FromWord(lock_.ExecuteLocked(Combinable::Wake, &args));
  if (VLOG_IS_ON(3)) {
    ABSL_RAW_LOG(INFO, "Root::Wake %p -> %ld", schedulable, result.AsWord());
  }
  return result;
}

Slot PriorityAdmissionScheduler::WakeLocked(
    Schedulable* absl_nonnull schedulable) {
  Enqueue(schedulable);
  ++queued_or_running_;
  if (idle_slots_.empty()) {
    return Slot::NullSlot();
  } else {
    ++active_slots_;
    Slot result = idle_slots_.back();
    idle_slots_.pop_back();
    return result;
  }
}

Schedulable* absl_nullable PriorityAdmissionScheduler::ScheduleManaged(
    Slot managing_slot, Schedulable* prev, bool runnable) {
  struct Combinable {
    struct Args {
      PriorityAdmissionScheduler* scheduler;
      Slot managing_slot;
      Schedulable* prev;
      bool runnable;
    };

    static Schedulable* ScheduleManaged(Args* args) {
      return args->scheduler->ScheduleManagedLocked(args->managing_slot,
                                                    args->prev, args->runnable);
    }
  };

  Combinable::Args args{this, managing_slot, prev, runnable};
  Schedulable* result = lock_.ExecuteLocked(Combinable::ScheduleManaged, &args);
  if (VLOG_IS_ON(3)) {
    ABSL_RAW_LOG(INFO, "Root::Sched (%ld) %p -> %p", managing_slot.AsWord(),
                 prev, result);
  }
  return result;
}

Schedulable* absl_nullable PriorityAdmissionScheduler::ScheduleManagedLocked(
    Slot managing_slot, Schedulable* prev, bool runnable) {
  Schedulable* result;
  if (prev == nullptr) {
    result = Dequeue();
    ABSL_RAW_CHECK(result != nullptr, "schedule empty queue without prev");
  } else if (runnable) {
    result = Dequeue();
    if (result == nullptr) {
      result = prev;
    } else {
      ABSL_RAW_DCHECK(prev->is_flag_set(kFlagIndexForAdmission),
                      "prev unadmitted");
      Enqueue(prev);
    }
  } else {
    --queued_or_running_;
    if (queued_or_running_ < active_slots_) {
      --active_slots_;
      idle_slots_.push_back(managing_slot);
      result = nullptr;
    } else {
      result = Dequeue();
    }
  }
  return result;
}

bool PriorityAdmissionScheduler::StopRunning(Slot managing_slot,
                                             Schedulable* current,
                                             bool runnable) {
  struct Combinable {
    struct Args {
      PriorityAdmissionScheduler* scheduler;
      Slot managing_slot;
      Schedulable* current;
      bool runnable;
    };

    static intptr_t StopRunning(Args* args) {
      return args->scheduler->StopRunningLocked(args->managing_slot,
                                                args->current, args->runnable);
    }
  };
  if (VLOG_IS_ON(3)) {
    ABSL_RAW_LOG(INFO, "Root::Stop %p", current);
  }
  Combinable::Args args{this, managing_slot, current, runnable};
  return lock_.ExecuteLocked(Combinable::StopRunning, &args);
}

bool PriorityAdmissionScheduler::StopRunningLocked(Slot managing_slot,
                                                   Schedulable* current,
                                                   bool runnable) {
  if (runnable) {
    ABSL_RAW_DCHECK(current->is_flag_set(kFlagIndexForAdmission),
                    "prev unadmitted");
    Enqueue(current);
    return true;
  } else {
    --queued_or_running_;
    if (queued_or_running_ < active_slots_) {
      --active_slots_;
      idle_slots_.push_back(managing_slot);
      return false;
    } else {
      return true;
    }
  }
}

void PriorityAdmissionScheduler::Enqueue(Schedulable* schedulable) {
  if (schedulable->type == Schedulable::kChildSlot) {
    Scheduler* child = schedulable->managed.scheduler;
    if (HoldsPriority(child)) {
      // Now parent_arg1 holds the priority, and this is the first
      // schedulable managing the child scheduler enqueued.
      const int priority = GetPriority(child);
      ABSL_RAW_DCHECK(priority < num_priorities_, "priority out of range");
      per_priority_[priority].PushTail(schedulable);
      num_queued_.fetch_add(1, std::memory_order_relaxed);
      // Change parent_arg1 to point to the tail of the schedulables
      // managing this child scheduler.
      SetChildSchedulerPointer(child, false, schedulable);
    } else {
      // Now parent_arg1 is a pointer.
      Schedulable* ptr = GetPointer(child);
      if (IsPointerAdmitted(child)) {
        if (schedulable->set_manager_flag(kFlagIndexForAdmission)) {
          // Not admitted before, add to the (head of the) list.
          schedulable->manager_num1 = reinterpret_cast<uint64_t>(ptr);
          SetChildSchedulerPointer(child, true, schedulable);
        }  // else, schedulable already in the list
        in_progress_.PushTail(schedulable);
      } else {
        // Insert the schedulable after parent_arg1, and point parent_arg1
        // to it (the new tail).
        schedulable->manager_ptr1 = ptr->manager_ptr1;
        ptr->manager_ptr1 = schedulable;
        SetChildSchedulerPointer(child, false, schedulable);
        // If parent_arg1 is the tail of any per priority queue, update the
        // queue's tail pointer too.
        if (schedulable->manager_ptr1 == nullptr) {
          for (internal::LinkedSchedulableList& q : per_priority_) {
            if (ptr == q.tail()) q.set_tail(schedulable);
          }
        }
      }
    }
  } else {
    ABSL_RAW_DCHECK(schedulable->type == Schedulable::kWorkItem,
                    "unknown type");
    if (schedulable->is_flag_set(kFlagIndexForAdmission)) {
      in_progress_.PushTail(schedulable);
    } else {
      per_priority_[0].PushTail(schedulable);
      num_queued_.fetch_add(1, std::memory_order_relaxed);
    }
  }
}

Schedulable* absl_nullable PriorityAdmissionScheduler::Dequeue() {
  if (!in_progress_.empty()) {
    Schedulable* result = in_progress_.Pop();
    // Enforces some invariants before returning, in debug mode.
    if (result->type == Schedulable::kChildSlot) {
      Scheduler* child = result->managed.scheduler;
      ABSL_RAW_DCHECK(IsPointerAdmitted(child),
                      "unadmitted child in admitted queue");
    } else {
      ABSL_RAW_DCHECK(result->type == Schedulable::kWorkItem, "unknown type");
    }
    ABSL_RAW_DCHECK(result->is_flag_set(kFlagIndexForAdmission),
                    "unadmitted schedulable in admitted queue");
    return result;
  }
  for (internal::LinkedSchedulableList& q : per_priority_) {
    if (!q.empty()) {
      Schedulable* result = q.Pop();
      if (result->type == Schedulable::kChildSlot) {
        // Mark the child scheduler admitted, and construct the list.
        Scheduler* child = result->managed.scheduler;
        ABSL_RAW_DCHECK(child->parent() == this, "schedule non managing");
        SetChildSchedulerPointer(child, true, result);

        // There may be multiple slots (managed by this) managing the same
        // child scheduler. When any slot managing the child scheduler gets
        // admitted, all others should be admitted too. Enqueue above
        // ensures all the siblings are next to each other in the queue.
        //
        // The block of code below moves all these siblings from the
        // per-priority queue into the in-progress queue, while linking them
        // together using manager_num1, as admitted ones are linked by
        // manager_num1 (see "parent_arg1" comment above).
        //
        // Typically additional parallelism within a scheduler is internal
        // (e.g. one managed entity wakes another). We do not typically
        // expect to find siblings here.
        //
        // Check FiberSchedulerTest PriorityForManagingSlots for example.
        Schedulable* last = result;
        Schedulable* curr = q.head();
        while (curr != nullptr && curr->type == Schedulable::kChildSlot &&
               curr->managed.scheduler == child) {
          ABSL_RAW_CHECK(curr == q.Pop(), "invariant");
          ABSL_RAW_CHECK(curr->set_manager_flag(kFlagIndexForAdmission),
                         "admitted schedulable in queue");
          last->manager_num1 = reinterpret_cast<uint64_t>(curr);
          in_progress_.PushTail(curr);
          last = curr;
          curr = q.head();
        }
        last->manager_num1 = reinterpret_cast<uint64_t>(nullptr);
      } else {
        ABSL_RAW_DCHECK(result->type == Schedulable::kWorkItem, "unknown type");
      }
      ABSL_RAW_CHECK(result->set_manager_flag(kFlagIndexForAdmission),
                     "admitted schedulable in per priority queue");
      num_queued_.fetch_sub(1, std::memory_order_relaxed);
      num_running_.fetch_add(1, std::memory_order_relaxed);
      return result;
    }
  }
  return nullptr;
}

void PriorityAdmissionScheduler::DeleteChildSlotLocked(
    Schedulable* schedulable) {
  Scheduler* child = schedulable->managed.scheduler;
  ABSL_RAW_DCHECK(IsPointerAdmitted(child), "child not admitted");
  Schedulable* head = GetPointer(child);
  if (schedulable == head) {
    Schedulable* next = reinterpret_cast<Schedulable*>(head->manager_num1);
    if (next == nullptr) {
      // Last schedulable deleted. Unadmit child scheduler in case it
      // constructs new slots later.
      SetChildSchedulerPriority(child, 0);
      num_running_.fetch_sub(1, std::memory_order_relaxed);
    } else {
      SetChildSchedulerPointer(child, true, next);
    }
  } else {
    // Linear search over the linked list maintained in manager_num1 for the
    // schedulable to be deleted, and remove it from the list
    Schedulable* curr = reinterpret_cast<Schedulable*>(head->manager_num1);
    while (curr != nullptr) {
      if (schedulable == curr) {
        head->manager_num1 = curr->manager_num1;
        break;
      }
      head = curr;
      curr = reinterpret_cast<Schedulable*>(curr->manager_num1);
    }
    ABSL_RAW_DCHECK(curr != nullptr, "scheduler leak");
  }
}

}  // namespace thread
