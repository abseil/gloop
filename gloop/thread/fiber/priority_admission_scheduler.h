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

#ifndef THIRD_PARTY_GLOOP_THREAD_FIBER_PRIORITY_ADMISSION_SCHEDULER_H_
#define THIRD_PARTY_GLOOP_THREAD_FIBER_PRIORITY_ADMISSION_SCHEDULER_H_

// Scheduler hierarchy with the following objectives:
//   1. Maximize CPU utilization.
//   2. Minimize running time (excluding queue time) for each closure.
//   3. Minimize overall latency (queue time plus running time) for high
//      priority closures.

#include <atomic>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/container/fixed_array.h"
#include "gloop/base/scheduling/domain.h"
#include "gloop/base/scheduling/low-level-support.h"
#include "gloop/base/scheduling/scheduler.h"
#include "gloop/thread/fiber/scheduler-types.h"

namespace thread {

namespace internal {

// Maintain schedulables in a singly linked list. Assume full ownership of
// base::scheduling::Schedulable::manager_ptr1 field. Thread compatible.
class LinkedSchedulableList {
 public:
  LinkedSchedulableList() = default;

  // This type is neither copyable nor movable.
  LinkedSchedulableList(const LinkedSchedulableList&) = delete;
  LinkedSchedulableList& operator=(const LinkedSchedulableList&) = delete;

  base::scheduling::Schedulable* Pop();
  void PushHead(base::scheduling::Schedulable* schedulable);
  void PushTail(base::scheduling::Schedulable* schedulable);

  bool empty() const { return head_ == nullptr; }
  base::scheduling::Schedulable* head() const { return head_; }
  base::scheduling::Schedulable* tail() const { return tail_; }
  void set_tail(base::scheduling::Schedulable* tail) { tail_ = tail; }

 private:
  base::scheduling::Schedulable* absl_nullable head_ = nullptr;
  base::scheduling::Schedulable* absl_nullable tail_ = nullptr;
};

}  // namespace internal

// Implements a scheduler with priority-based admission control. Child
// schedulers (typically associated with a fiber tree) may be assigned a
// priority; whenever this scheduler has idle capacity and there is work
// waiting to be admitted, the highest priority children will be admitted
// until this scheduler no longer has idle capacity. Once admitted,
// scheduling always occurs in FIFO order.
//
// Admission within the same priority level is FIFO.
class PriorityAdmissionScheduler : public base::scheduling::Scheduler {
 public:
  PriorityAdmissionScheduler(base::scheduling::Domain* domain,
                             int num_priorities);
  PriorityAdmissionScheduler(Scheduler* parent, int slots, int num_priorities);

  // This type is neither copyable nor movable.
  PriorityAdmissionScheduler(const PriorityAdmissionScheduler&) = delete;
  PriorityAdmissionScheduler& operator=(const PriorityAdmissionScheduler&) =
      delete;

 protected:
  ~PriorityAdmissionScheduler() override;

 public:
  // Sets the priority of a newly created child scheduler. If a child
  // scheduler is created without calling this, all schedulables managed by
  // it are considered to have priority 0 (highest).
  // REQUIRES: child must be a new scheduler with no managed schedulables.
  // REQUIRES: child->parent() == this.
  // REQUIRES: priority >= 0 && priority < num_priorities.
  void SetChildPriority(Scheduler* child, int priority);

  // Both num_queued and num_running estimate the number of child schedulables
  // that are in different stages. Child schedulables are only counted once,
  // regardless of how many slots they have.
  int num_queued() const { return num_queued_.load(std::memory_order_relaxed); }
  int num_running() const {
    return num_running_.load(std::memory_order_relaxed);
  }

 private:
  void DeleteManagedSchedulable(
      base::scheduling::Schedulable* schedulable) override;

  base::scheduling::Slot Wake(
      base::scheduling::Schedulable* schedulable) override;
  base::scheduling::Slot WakeLocked(base::scheduling::Schedulable* schedulable);

  base::scheduling::Schedulable* ScheduleManaged(
      base::scheduling::Slot managing, base::scheduling::Schedulable* prev,
      bool runnable) override;
  base::scheduling::Schedulable* ScheduleManagedLocked(
      base::scheduling::Slot managing, base::scheduling::Schedulable* prev,
      bool runnable);

  bool StopRunning(base::scheduling::Slot managing,
                   base::scheduling::Schedulable* current,
                   bool runnable) override;
  bool StopRunningLocked(base::scheduling::Slot managing,
                         base::scheduling::Schedulable* current, bool runnable);

  void Enqueue(base::scheduling::Schedulable* schedulable);

  base::scheduling::Schedulable* Dequeue();

  void DeleteChildSlotLocked(base::scheduling::Schedulable* schedulable);

  const int num_priorities_;
  std::atomic<int> num_queued_{0};
  std::atomic<int> num_running_{0};
  thread::internal::CombinerLock lock_;
  internal::LinkedSchedulableList in_progress_;
  absl::FixedArray<internal::LinkedSchedulableList> per_priority_;
  int queued_or_running_ = 0;
  int active_slots_ = 0;
  std::vector<base::scheduling::Slot> idle_slots_;
};

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_FIBER_PRIORITY_ADMISSION_SCHEDULER_H_
