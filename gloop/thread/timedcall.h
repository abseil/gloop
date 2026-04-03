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

#ifndef THIRD_PARTY_GLOOP_THREAD_TIMEDCALL_H_
#define THIRD_PARTY_GLOOP_THREAD_TIMEDCALL_H_

#include <atomic>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "gloop/base/walltime.h"
#include "gloop/util/gtl/intrusive_heap.h"

namespace timed_call_internal {

// Hook to allow overriding TimedCall usage with eventmanager::Default().
void TimedCallRunAt(WallTime deadline, absl::AnyInvocable<void() &&> f);

}  // namespace timed_call_internal

// TimedCall runs a procedure at a given time within one of its internal
// threads, without depending on selectservers or subclassing.
//
// ******* WARNING ******
// It is very DANGEROUS if TimedCall is used to execute blocking functions,
// because TimedCall has only one thread and many important primitives depend on
// it. The possible consequences of blocking TimedCall thread includes:
//  1: SleepUntil will never wake up.
//  2: Fibers with deadline will not expire on time.
// *******
//
// ******* NOTES ******
//  1: TimedCall executes given function in a common internal
//     thread.  Hence they should not block or take excessive time.
//
//  2: thread::Executor interface now has delayed execution functionality
//           Executor::AddAfter(int delay_ms, Closure *cl);
//     Most callers will find the higher level Executor::AddAfter() interface
//     more convenient and less error-prone to use than TimedCall.
// *******
//
// Example:
//
//    // Arrange to call void f() in "delay" in seconds
//    ...
//    WallTime when = base::ToWallTime(absl::Now()) + absl::Seconds(delay);
//    TimedCall tc(when, f);
//    ...timer is running...
//
//    // optionally  stop timer
//    tc.Set(TimedCall::Stop, nullptr);
//
//    // optionally tell whether f() ran, or you stopped the timer first
//    if (tc.deadline() == TimedCall::Stop) {
//       // you stopped the timer
//    } else {
//       // f() ran
//    }
//
// Example of a periodic callback:
//
// struct Context {
//    TimedCall tc;
//    WallTime next_run;        // time of next run
//    WallTime period;          // interval between runs
//    std::function<void()> f;  // function to run
//    ...
// };
// void Periodic(Context *c) {
//    ...
//    c->tc.Set(c->next_run += c->period, c->f);
// }
//
//    Context *c = new Context;
//    c->next_run = base::ToWallTime(absl::Now());
//    c->period = 1;            // interval between runs
//    c->tc.Set(c->next_run += c->period, absl::bind_front(&Periodic, c));

// This class is thread safe.
class TimedCall {
 public:
  TimedCall();  // sets deadline_ and f_ to 0.  See Set().

  ~TimedCall();  // f will not be called after destructor returns.
                 // If destructor is not called from a callback from this
                 // interface, then the last call of f has completed
                 // when the destructor returns.

  // constructor equivalent to calling Set()
  TimedCall(WallTime deadline, absl::AnyInvocable<void() &&> f);

  // This type is neither copyable nor movable.
  TimedCall(const TimedCall&) = delete;
  TimedCall& operator=(const TimedCall&) = delete;

  // Set():
  //
  // Atomically:
  //  set f_ = f
  //  set deadline_ = deadline iff the timer is running or being turned on
  //  Return nullptr
  //
  // Timer operation: if deadline_ > 0, at absolute UNIX time deadline_,
  // or as soon as possible thereafter, the following sequence occurs:
  //   (1) deadline_ is set to TimedCall::Running
  //   (2) f() is called (an empty f is treated as a no-op procedure)
  //   (3) deadline_ is set to TimedCall::Expired
  //
  // f() is called in a thread internal to the implementation.
  // Therefore f() should not block or take excessive time.
  // Furthermore, to avoid deadlock, at least one of the following
  // conditions must be true:
  //   (1) (flags & kNoWait) != 0
  //   (2) Caller must not hold any mutex that may be acquired by
  //       the f passed to any of the calls made to this->Set(),
  //       including this call.
  // If this restriction is problematic, use f() to queue another call
  // on a client thread.
  //
  // Calling Set() again before the callback has been called stops the timer
  // for the previously scheduled call if it is not already running.  That
  // call may or may not have run, depending on timing.  However, it is
  // guaranteed that when Set() returns, one of:
  // - this->deadline() == Running
  //   The call is still running.  This state is returned only if
  //   the Set() was run from the previously scheduled call, or
  //   (flags & kNoWait) != 0.  If these conditions are not true
  //   and the scheduled call is in progress, then the call to Set
  //   will block until the scheduled call has completed.
  // - this->deadline() == Expired
  //   The previously scheduled call has run to completion.
  // - this->deadline() == Stop
  //   The previously scheduled call will not be run.
  //
  // If you need to stop a TimedCall, call x->Set(TimedCall::Stop, 0, 0)
  // To tell whether the previously scheduled call ran, check x->deadline()
  // after stopping the timer with x->Set(TimedCall::Stop, 0, 0):
  //            if (x->deadline() != TimedCall::Stop) { call was executed }
  // If you need a repeating timer, call Set() in f().
  void Set(WallTime deadline, absl::AnyInvocable<void() &&> f, int flags);

  // Flags:
  enum {
    kNoWait = 0x1
  };  // Set() should not wait for an outstanding
      // call to complete.

  void Set(WallTime deadline, absl::AnyInvocable<void() &&> f) {
    this->Set(deadline, std::move(f), 0);
  }

  // Special deadline values
  static const WallTime Stop;  // stop the timer (may be Set by user)

  // The values below may not be Set() by clients,
  // though they may be observed with deadline().
  static const WallTime Expired;  // timer expired, call complete
  static const WallTime Running;  // call running

  // accessors
  WallTime deadline() const;
  // This will be empty if the callback has already been run.
  absl::AnyInvocable<void() &&> f();

  // Convenience method that runs the provided function at specified
  // deadline.  There is no way to cancel the call, deadline should be
  // > 0. Same limitations on locks held as for the Set function
  static void RunAt(WallTime deadline, absl::AnyInvocable<void() &&> f);

  // Return the number of active scheduled calls.
  // Note: the result may be out of date even before this routine returns if an
  // active TimedCall finishes concurrently. This call is mostly useful for
  // stats, monitoring, inspection, etc.
  static int NumScheduled();

 private:
  // TODO: convert this all to absl::Time
  WallTime deadline_;  // if > 0, absolute time when f_ is to be run;
                       // if <= 0, timer not running;
                       // set to TimedCall::Running when call is running.
                       // set to TimedCall::Expired when call has run.
  absl::AnyInvocable<void() &&> f_;  // run when now > deadline_, if deadline_>0

  gtl::IntrusiveHeapLink heap_;
  std::atomic<bool>
      active_;  // an advisory hint used by the destructor to determine
                // whether global synchronization (e.g. removal) is needed

  static void Thread();         // thread that handles deadlines
  static void InitTimedCall();  // one-time internal initialization
  void Initialize();            // common code for constructors
  void Remove(int flags);       // remove from timer queue;

  friend void ::timed_call_internal::TimedCallRunAt(
      WallTime deadline, absl::AnyInvocable<void() &&> f);
  friend struct TimedCallCompare;
  friend struct TimedCallLinkAccess;
};

#endif
