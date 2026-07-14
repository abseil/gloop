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

// Closure that supports Wait(), helpful for turning asynchronous calls into
// synchronous ones.  Typical usage:
//
//   BlockingClosure done;
//   foo.DoAsyncCall(..., &done);
//   done.Wait();
//   [process results]
//
// You can also wrap an existing closure, in which case Wait() will block until
// AFTER the inner closure has returned.  BlockingClosure is repeatable if and
// only if the inner closure is repeatable OR not provided.  BlockingClosure
// does NOT take ownership of the given pointer.
//
//   int n = 0;
//   BlockingClosure done(NewCallback(&IncrementN, &n));
//   foo.DoAsyncCall(..., &done);
//   done.Wait();
//   CHECK_EQ(n, 1);
//
// You can also reuse closures using Reset(); however, when doing so you MUST
// provide a repeatable callback.  BlockingClosure is thread safe, but does not
// provide any synchronization guarantees for the inner callback, so a closure
// wrapped in BlockingClosure has the same thread-safety properties as a regular
// Closure*.
//
// This is derived heavily from Yonatan Zunger's BlockingClosure implementation
// in teragoogle/indexserver/unittest-utils-inl.h.
//
// CAVEATS
//
// This will not work if Run() and Wait() are called in the same thread, because
// Wait() blocks completely.  So for example, if you're expecting Run() to be
// called in the same SelectServer loop from which Wait() is called, the result
// will be DEADLOCK.
//
// It's important to understand exactly what will happen when Run() is called on
// the given Closure.  For example, suppose you've wrapped a Closure (c) inside
// an AbortableClosure.  In this case when Wait() returns, c->Run() may not have
// been run yet and may indeed still be running.  As such, take great caution
// when using Closures created by anything other than NewCallback and
// NewPermanentCallback.
//
// NOTE: In many cases where only signalling behavior is required,
// absl::Notification or absl::BlockingCounter is clearer and more efficient.
// See <link>++-concurrency#other for details.

#ifndef THIRD_PARTY_GLOOP_UTIL_CALLBACK_BLOCKING_CALLBACK_H_
#define THIRD_PARTY_GLOOP_UTIL_CALLBACK_BLOCKING_CALLBACK_H_

#include <cstdint>

#include "absl/base/casts.h"
#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gloop/base/callback.h"

class BlockingClosure : public Closure {
 public:
  // Create a blocking closure around an existing closure.  Run() will be
  // invoked on the closure prior to returning from Wait().  Passing NULL
  // results in a no-op when this closure is invoked, although it's easier to
  // use the no-argument constructor below.  We DO NOT take ownership of this
  // pointer.  So if it's possible that 'closure' will never be called or you're
  // using a permanent closure, you should arrange for the closure to be
  // deleted.
  explicit BlockingClosure(Closure* closure);

  // Create a no-op blocking closure.  This is identical to
  // BlockingClosure(NewPermanentCallback(&DoNothing)) except you
  // don't need to delete the permanent callback.
  BlockingClosure();

  // This type is neither copyable nor movable.
  BlockingClosure(const BlockingClosure&) = delete;
  BlockingClosure& operator=(const BlockingClosure&) = delete;

  ~BlockingClosure();

  // Run the closure and signal anyone waiting.
  void Run() override ABSL_LOCKS_EXCLUDED(&done_lock_);

  // Run the closure and signal anyone waiting.
  void operator()() ABSL_LOCKS_EXCLUDED(&done_lock_) { Run(); }

  // Wait for the callback to be run.  Note that these calls will return
  // immediately after this closure has been invoked.
  void Wait() const ABSL_LOCKS_EXCLUDED(&done_lock_);

  // Waits until the deadline -- as specified in milliseconds or via Duration --
  // for the callback to be run.  Returns false iff the callback is not run
  // within the given amount of time.  Similar to Wait(), this will return true
  // immediately after the closure has been invoked.
  bool WaitWithTimeout(int64_t deadline_ms) const
      ABSL_LOCKS_EXCLUDED(&done_lock_);
  bool WaitWithTimeout(absl::Duration deadline) const
      ABSL_LOCKS_EXCLUDED(&done_lock_);

  // Wait for the callback to be run N times. Similar to Wait(), this will
  // return immediately after this closure has been invoked.
  void WaitForNum(int N) const ABSL_LOCKS_EXCLUDED(&done_lock_);

  // Wait for one of the following two conditions to be true:
  //   1) the callback is run N times
  //   2) the deadline expires
  // Returns true iff this Closure has been invoked N or more times before the
  // deadline expires.  Similar to Wait(), this will return immediately after
  // this closure has been invoked.
  bool WaitForNumCalled(int N, int deadline_ms) const
      ABSL_LOCKS_EXCLUDED(&done_lock_);
  bool WaitForNumCalled(int N, absl::Duration deadline) const
      ABSL_LOCKS_EXCLUDED(&done_lock_);

  // Reset the state of this Closure so that Wait() will block until the next
  // time the closure is invoked.  Usually if already invoked, Wait() will
  // return immediately.  You must not use Reset() if you provided a
  // non-repeatable closure when constructing this object.  Reset() is always
  // legal if the no-argument constructor is used.  It is possible for Reset()
  // to take effect after Run() has been invoked but before all waiters have
  // been notified, so be very careful about calling Reset() when you may have
  // multiple waiters.
  void Reset() ABSL_LOCKS_EXCLUDED(&done_lock_);

  // Return true if this closure is repeatable.  As discussed above, this is
  // true unless a non-repeatable closure has been provided.  See
  // base/callback.h for more information.
  bool IsRepeatable() const override;

  // When using a BlockingClosure* in a NewCallback() which expects a Closure*
  // use this method to explicitly up-cast to avoid a type mis-match.
  Closure* AsClosure() { return absl::implicit_cast<Closure*>(this); }

  // Returns how many times Run has been called.
  int num_called() const ABSL_LOCKS_EXCLUDED(&done_lock_);

 private:
  // Used by WaitForNumCalled() to determine when the closure has been invoked
  // for the Nth time.
  bool NumCalledCondition(int expected_count) const
      ABSL_SHARED_LOCKS_REQUIRED(&done_lock_);

  Closure* closure_;  // The user provided Closure (may be NULL)

  mutable absl::Mutex done_lock_;  // Protect the state below
  // How many times Run() has been called
  int num_calls_ ABSL_GUARDED_BY(done_lock_) = 0;
  // Has Run() been called at least once?
  bool done_ ABSL_GUARDED_BY(done_lock_) = false;

  const bool repeatable_;  // Is closure above repeatable?
  // Note, do not reorder variables without considering changes in layout and
  // padding of this class.
};

#endif  // THIRD_PARTY_GLOOP_UTIL_CALLBACK_BLOCKING_CALLBACK_H_
