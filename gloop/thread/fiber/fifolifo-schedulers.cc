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

#include "gloop/thread/fiber/fifolifo-schedulers.h"

#include <cstdint>

#include "absl/base/internal/raw_logging.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/inlined_vector.h"
#include "gloop/base/scheduling/scheduler.h"
#include "gloop/base/scheduling/scheduling_mode.h"
#include "gloop/base/spinlock.h"
#include "gloop/thread/fiber/scheduler-types.h"

using base::scheduling::Schedulable;
using base::scheduling::Slot;

namespace thread {

enum Ordering {
  FIFO,
  LIFO,
};

// Schedulables provide inline storage that the scheduler they are managed by
// may use to store internal state.  Here we use manager_ptr1 to maintain a list
// of queued schedulables.
static inline Schedulable** Next(Schedulable* schedulable) {
  return reinterpret_cast<Schedulable**>(&schedulable->manager_ptr1);
}

class OrderedScheduler : public base::scheduling::Scheduler {
 public:
  OrderedScheduler(Ordering queue_order, base::scheduling::Domain* domain);
  OrderedScheduler(Ordering queue_order, Scheduler* parent, int max_slots);

  // This type is neither copyable nor movable.
  OrderedScheduler(const OrderedScheduler&) = delete;
  OrderedScheduler& operator=(const OrderedScheduler&) = delete;

 protected:
  // REQUIRES: May only be called by Release().
  ~OrderedScheduler() override;

  // Implementations of interfaces defined by Scheduler.
  // See:
  // https://github.com/abseil/gloop/tree/main/gloop/base/scheduling/scheduler.h
  Slot Wake(Schedulable* to_wake) override;

  Schedulable* ScheduleManaged(Slot slot, Schedulable* prev,
                               bool runnable) override;

  bool StopRunning(Slot slot, Schedulable* current, bool runnable) override;

 private:
  void Init();

  // We allow "order" to be specified so that Enqueue() may be reused for the
  // yield case (which always enqueues in FIFO order).
  void Enqueue(Schedulable* schedulable, Ordering order)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
    if (head_ == nullptr) {
      *Next(schedulable) = nullptr;
      head_ = tail_ = schedulable;
    } else {
      if (order == FIFO) {
        // FIFO: Always enqueue after tail_.
        *Next(tail_) = schedulable;
        *Next(schedulable) = nullptr;
        tail_ = schedulable;
      } else {
        // LIFO: Always enqueue as head_.
        *Next(schedulable) = head_;
        head_ = schedulable;
      }
    }
  }

  // Ordering is handled in Enqueue(), we always dequeue from head_.
  Schedulable* Dequeue() ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
    // Dequeue should always succeed as we guarantee the invariant:
    //   queued_or_running >= active_slots_
    ABSL_RAW_DCHECK(head_ != nullptr, "unexpected empty queue");
    Schedulable* result = head_;
    head_ = *Next(head_);
    if (head_ == nullptr) {
      tail_ = nullptr;
    }
    return result;
  }

  ::SpinLock lock_{absl::base_internal::SCHEDULE_KERNEL_ONLY};
  // The number of schedulables managed by *this which  are either currently
  // running or queued.
  int queued_or_running_ ABSL_GUARDED_BY(lock_);

  // Our run-queue is a singly linked, we may enqueue at the head or tail
  // depending on the chosen ordering.
  Schedulable* head_ ABSL_GUARDED_BY(lock_);
  absl::InlinedVector<Slot, 1> idle_slots_ ABSL_GUARDED_BY(lock_);

  // All fields below are protected by combiner_lock_.
  // The following invariants are always true:
  //   idle_slots_.size() + active_slots_ == Scheduler::num_slots()
  //   queued_or_running_ >= active_slots_

  // The number of scheduled slots managing *this.
  int active_slots_ ABSL_GUARDED_BY(lock_);
  const Ordering queue_order_;
  Schedulable* tail_ ABSL_GUARDED_BY(lock_);
};

OrderedScheduler::OrderedScheduler(Ordering queue_order,
                                   base::scheduling::Domain* domain)
    : Scheduler(domain), idle_slots_(num_slots()), queue_order_(queue_order) {
  Init();
}

OrderedScheduler::OrderedScheduler(Ordering queue_order, Scheduler* parent,
                                   int max_slots)
    : Scheduler(parent, max_slots),
      idle_slots_(num_slots()),
      queue_order_(queue_order) {
  Init();
}

OrderedScheduler::~OrderedScheduler() {
  // Since we guarantee that all active slots have work reserved, it's
  // sufficient to check that there is no work running to guarantee we satisfy
  // required ~Scheduler() preconditions.
  ABSL_RAW_DCHECK(active_slots_ == 0, "scheduler not idle");
  ABSL_RAW_DCHECK(queued_or_running_ == 0, "scheduler not idle");
  ABSL_RAW_DCHECK(static_cast<int64_t>(idle_slots_.size()) == num_slots(),
                  "missing slots");
  for (auto& slot : idle_slots_) {
    DeleteManagingSlot(slot);
  }
}

void OrderedScheduler::Init() {
  ::SpinLockHolder h(lock_);
  for (int i = 0; i < num_slots(); i++) {
    idle_slots_[i] = NewManagingSlot();
  }
  head_ = tail_ = nullptr;
  active_slots_ = queued_or_running_ = 0;
}

Slot OrderedScheduler::Wake(Schedulable* to_wake) {
  ::SpinLockHolder h(lock_);
  Enqueue(to_wake, queue_order_);
  queued_or_running_++;

  // Since we've maintained active_slots <= queued_or_running_ in
  // ScheduleManaged(), we may always wake up a new slot provided we're
  // under capacity.
  Slot result;
  if (active_slots_ < num_slots()) {
    active_slots_++;
    result = idle_slots_.back();
    idle_slots_.pop_back();
  } else {
    // Already at capacity.
    result = Slot::NullSlot();
  }

  return result;
}

Schedulable* OrderedScheduler::ScheduleManaged(Slot slot, Schedulable* prev,
                                               bool runnable) {
  ::SpinLockHolder h(lock_);
  Schedulable* result;
  if (prev == nullptr) {
    // We always guarantee work for scheduled slots.
    result = Dequeue();
  } else {
    if (runnable) {
      if (head_ == nullptr) {
        // Only "prev" is eligible for scheduling.
        result = prev;
      } else {
        result = Dequeue();
        // We force yielding tasks to the end of the runqueue for both the
        // LIFO and FIFO cases.  This prevents always yielding fibers from
        // creating deadlocks in the LIFO case.
        Enqueue(prev, FIFO);
      }
    } else {  // prev != nullptr && runnable == false
      queued_or_running_--;
      if (queued_or_running_ >= active_slots_) {
        // We still have work for the current slot.
        result = Dequeue();
      } else {
        // Slot must go idle.
        idle_slots_.push_back(slot);
        active_slots_--;
        result = nullptr;
      }
    }
  }

  return result;
}

bool OrderedScheduler::StopRunning(Slot slot, Schedulable* current,
                                   bool runnable) {
  ::SpinLockHolder h(lock_);
  bool result;

  if (runnable) {
    // Always send yielding tasks to the end of the queue. See
    // ScheduleManaged.
    Enqueue(current, FIFO);
    result = true;
  } else {
    // "current" is no longer runnable, we must check to see whether there
    // is enough runnable concurrency below this scheduler to keep "slot"
    // active.
    queued_or_running_--;
    if (queued_or_running_ < active_slots_) {
      idle_slots_.push_back(slot);
      active_slots_--;
      result = false;
    } else {
      result = true;
    }
  }

  return result;
}

base::scheduling::Scheduler* NewRootFIFOScheduler(
    base::scheduling::Domain* domain) {
  return new OrderedScheduler(FIFO, domain);
}

base::scheduling::Scheduler* NewChildFIFOScheduler(
    base::scheduling::Scheduler* parent, int num_slots) {
  return new OrderedScheduler(FIFO, parent, num_slots);
}

base::scheduling::Scheduler* NewChildLIFOScheduler(
    base::scheduling::Scheduler* parent, int num_slots) {
  return new OrderedScheduler(LIFO, parent, num_slots);
}

}  // namespace thread
