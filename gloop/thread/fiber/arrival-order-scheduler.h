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

// Reference "oldest first" scheduler.
//
// ArrivalOrderSchedulers always schedule their "oldest" (by creation order)
// runnable schedulable.  This is unlike a FIFO scheduler which is ordered by
// last-wakeup.
//
// ArrivalOrderSchedulers do not make any guarantees regarding starvation or
// fairness.  They are best suited for independent work in which it is desirable
// to minimize the latency tail, e.g. the scheduling of root-fibers
// corresponding to RPCs.
#ifndef THIRD_PARTY_GLOOP_THREAD_FIBER_ARRIVAL_ORDER_SCHEDULER_H_
#define THIRD_PARTY_GLOOP_THREAD_FIBER_ARRIVAL_ORDER_SCHEDULER_H_

#include "gloop/base/scheduling/scheduler.h"

namespace base {
namespace scheduling {
class Domain;
}  // namespace scheduling
}  // namespace base

namespace thread {

class ArrivalOrderSchedulerOptions {
 public:
  // Initializes all options to default values.
  ArrivalOrderSchedulerOptions();
  ~ArrivalOrderSchedulerOptions() = default;

  // ArrivalOrderSchedulers support the notion of an admission limit.  This
  // limit is a hard restriction, guaranteeing that only the oldest (by order of
  // first-wakeup) "admission_limit" schedulables are eligible for scheduling at
  // any time.  Schedulables not matching this precondition (i.e. newer) will
  // NOT be allowed to execute until an older Schedulable is deleted.
  //
  // Admission limits may be usefully used with fibers to bound how many fiber
  // trees may be concurrently running.  This can prevent for example, thousands
  // of blocking requests leading to an out of memory condition.
  //
  // CAUTION: Admission limits may only be used when it can be guaranteed that
  // no synchronization dependencies exist on work that is being held back;
  // deadlocks are otherwise possible.
  //
  // Example:
  //   Scheduler* s = NewChildArrivalOrderScheduler(domain->root_scheduler(),
  //       num_slots, ArrivalOrderSchedulerOptions().set_admission_limit(2));
  //
  //   // "parent_scheduler()" allows us to control where the scheduler
  //   // allocated by each tree attaches to.  This means that each tree
  //   // will have their own scheduler (encapsulating the root fiber and any
  //   // children), with "s" as a common parent.
  //   TreeOptions tree_options;
  //   tree_options.set_parent_scheduler(s);
  //
  //   std::unique_ptr<Fiber> root_a = NewTree(tree_options, ...);
  //   std::unique_ptr<Fiber> root_b = NewTree(tree_options, ...);
  //   std::unique_ptr<Fiber> root_c = NewTree(tree_options, ...);
  //
  //   // By calling Orphan() we ensure "s" will be automatically freed once all
  //   // fiber trees above complete.  In particular, the child scheduler
  //   // allocated by each call to NewTree() will take a Ref() against "s",
  //   // these references will be released as each tree completes execution.
  //   s->Orphan();
  //
  // Since we have an admission limit of 2; only fibers from root_a and root_b
  // will be allowed to run.  The execution of root_c will be held back, even if
  // there is concurrency available, until root_a OR root_b finish.
  //
  // NOTE: It's particularly important in the example above that {root_a,
  // root_b, root_c} were root fibers (with their own trees and sub-schedulers).
  // Were this not the case, any child they created would be subject to
  // admission control and an immediate potential deadlock.
  //
  // REQUIRES: admission_limit > 0
  ArrivalOrderSchedulerOptions& set_admission_limit(int admission_limit);
  // Returns a positive integer or -1 when no limit exists.
  int admission_limit() const { return admission_limit_; }

 private:
  int admission_limit_;
};

// Allocate a new ArrivalOrderScheduler as the root scheduler for "domain" with
// default options.
// REQUIRES: domain->root_scheduler() == nullptr.
base::scheduling::Scheduler* NewRootArrivalOrderScheduler(
    base::scheduling::Domain* domain,
    const ArrivalOrderSchedulerOptions& options);

// Allocate a new ArrivalOrderScheduler, with "num_slots" slots, as a child of
// "parent" with default options.
base::scheduling::Scheduler* NewChildArrivalOrderScheduler(
    base::scheduling::Scheduler* parent, int num_slots,
    const ArrivalOrderSchedulerOptions& options);

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_FIBER_ARRIVAL_ORDER_SCHEDULER_H_
