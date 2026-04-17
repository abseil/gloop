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

// Reference FIFO (First-In-First-Out) and LIFO (Last-In-First-Out) Schedulers.
//
// By 'FIFO' we mean that managed Schedulables will always be scheduled (i.e.
// returned by ScheduleManaged(...)) in the order that they were previously
// queued by either:
//   Wake(schedulable), or
//   ScheduleManaged(..., schedulable, true)
//
// FIFOSchedulers guarantee no starvation (provided rescheduling occurs) and are
// suitable for use as a domain's root scheduler.
//
// Conversely, LIFO schedulers will always schedule in reverse-wakeup order.
// e.g. The most recently woken or re-scheduled Schedulable.
//
// As a result LIFO Schedulers do not make guarantees regarding starvation and
// should only be used, for example, as the scheduler for a single Fiber-tree.
// In this example we are only trying to complete sub-work as quickly as
// possible, since the parent depends on all children completing, there is no
// notion of fairness.
//
// Implements the Scheduler interface, see:
//   //gloop/base/scheduling/scheduler.h

#ifndef THIRD_PARTY_GLOOP_THREAD_FIBER_FIFOLIFO_SCHEDULERS_H_
#define THIRD_PARTY_GLOOP_THREAD_FIBER_FIFOLIFO_SCHEDULERS_H_

#include "gloop/base/scheduling/scheduler.h"

namespace base {
namespace scheduling {
class Domain;
}  // namespace scheduling
}  // namespace base

namespace thread {

// Allocate a new FIFOScheduler as the root scheduler for "domain".
// REQUIRES: domain->root_scheduler() == nullptr.
base::scheduling::Scheduler* NewRootFIFOScheduler(
    base::scheduling::Domain* domain);

// Allocate a new FIFOScheduler, with "num_slots" slots, as a child of "parent".
base::scheduling::Scheduler* NewChildFIFOScheduler(
    base::scheduling::Scheduler* parent, int num_slots);

// Allocate a new LIFOScheduler, with "num_slots" slots, as a child of "parent".
base::scheduling::Scheduler* NewChildLIFOScheduler(
    base::scheduling::Scheduler* parent, int num_slots);

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_FIBER_FIFOLIFO_SCHEDULERS_H_
