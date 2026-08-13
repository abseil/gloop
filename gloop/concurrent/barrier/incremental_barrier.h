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

#ifndef CONCURRENT_BARRIER_INCREMENTAL_BARRIER_H__
#define CONCURRENT_BARRIER_INCREMENTAL_BARRIER_H__

#include <functional>

#include "absl/base/attributes.h"
#include "absl/functional/any_invocable.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/trace_source_location.h"

namespace concurrent {

class InternalIncrementalBarrier;

// IncrementalBarrier is a convenience class to be used in place of a barrier
// (defined in util/functional/barrier.h). It is particularly helpful when you
// don't know the correct value of N when constructing the barrier. For example,
// consider the following usage of barrier:
//
//   void MakeCalls() {
//     int calls_to_make = 0;
//     for (int i = 0; i < objects_.size(); ++i) {
//       if (CrazyComplicatedPredicate(objects_[i]))
//         ++calls_to_make;
//     }
//
//     // +1 in case Lookup finishes quickly and done() results in "this" being
//     // deleted, invalidating objects_.
//     auto barrier = util::functional::NewBarrier(calls_to_make + 1, done);
//     for (int i = 0; i < objects_.size(); ++i) {
//       if (CrazyComplicatedPredicate(objects_[i])) {
//         Lookup(objects_[i], barrier);
//       }
//     }
//     barrier();
//   }
//
// This code is suboptimal for a number of reasons, such as code duplication,
// inefficiency (if CrazyComplicatedPredicate() is expensive), and readability.
// IncrementalBarrier addresses these problems by allowing the client to never
// explicitly have to indicate the value of N, either before the barrier is
// constructed or after. Here's what the function would look like using
// IncrementalBarrier instead:
//
//   void MakeCalls() {
//     IncrementalBarrier barrier(done);
//     for (int i = 0; i < objects_.size(); ++i) {
//       if (CrazyComplicatedPredicate(objects_[i])) {
//         Lookup(objects_[i], barrier.InvocableInc());
//       }
//     }
//   }
//
//  The passed in callback will never be called before the IncrementalBarrier is
//  destructed, so if you need to block on it, it must be nested in its own
//  block:
//    void MakeCallsSync() {
//      absl::Notification wait_for_all;
//      {
//        IncrementalBarrier([&wait_for_all] { wait_for_all.Notify(); });
//        ...
//        ...
//      }
//      wait_for_all.WaitForNotification();
//    }
//
// This class is thread-safe.
class IncrementalBarrier {
 public:
  // Creates an IncrementalBarrier with the provided 'done' closure.
  // Applications can optionally provide a label for the barrier which is
  // used in traced executions such as a Dapper traced query. The default
  // label value is the caller's source location.
  // For example:
  //   IncrementalBarrier done([this], { OnDone(); }, "RequestsMatched");
  explicit IncrementalBarrier(
      absl::AnyInvocable<void() &&> done_closure,
      perftools::tracing::StringLabel label =
          perftools::tracing::TraceSourceLocation::current());

  // This type is neither copyable nor movable.
  IncrementalBarrier(const IncrementalBarrier&) = delete;
  IncrementalBarrier& operator=(const IncrementalBarrier&) = delete;

  // Signals that InvocableInc() will never again be called, and thus the
  // correct value of N for this barrier closure is the number of calls that
  // were made to InvocableInc() while this IncrementalBarrier object existed.
  // Once each callback returned by InvocableInc() below has been run,
  // 'done_closure' will be run.
  //
  // Put another way, 'done_closure' will not be run until the
  // IncrementalBarrier object is destructed.
  //
  // If InvocableInc() was never called, 'done_closure' will be immediately run.
  // If all callbacks returned by InvocableInc() have already been run,
  // 'done_closure' will be immediately run. Otherwise, 'done_closure' will be
  // run once all such callbacks have been run.
  //
  // Note that the d-tor does not wait for all the callbacks to be called.
  // Thus the done_closure can be called long after the d-tor has returned and
  // the IncrementalBarrier instance has been destructed.
  ~IncrementalBarrier();

  // Increments the internal counter and returns a function object which
  // decrements that counter when invoked. Furthermore, each object returned by
  // InvocableInc() should eventually be run, or else 'done_closure' will never
  // be run.
  absl::AnyInvocable<void() &&> InvocableInc();

  // Similar to InvocableInc(), but returns a std::function instead. This
  // function must only be invoked once.  (i.e., it should not be copied and
  // invoked multiple times.)
  ABSL_DEPRECATED("Use InvocableInc instead.")
  std::function<void()> FunctionInc();

 private:
  InternalIncrementalBarrier* internal_barrier_;  // self-deleting
};

}  // namespace concurrent

#endif  // CONCURRENT_BARRIER_INCREMENTAL_BARRIER_H__
