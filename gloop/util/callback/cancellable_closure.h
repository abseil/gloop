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

#ifndef THIRD_PARTY_GLOOP_UTIL_CALLBACK_CANCELLABLE_CLOSURE_H_
#define THIRD_PARTY_GLOOP_UTIL_CALLBACK_CANCELLABLE_CLOSURE_H_
// If you are submitting a Closure to an Executor and wish to cancel
// the Closure, consider using thread::AddCancellable()
// in thread/executor.h

// A CancellableClosure implements a Closure that can be cancelled with
// Cancel() or waited for with WaitUntil().  It is intended to be used with
// Executor or anything else that can take a Closure.  Unlike AbortableCallback,
// its operations can be used by multiple threads and are independent of
// SelectServer.
//
// CancellableClosure wraps another Closure that is provided to New().  When
// the CancellableClosure's Run() method is called, it will call the
// wrapped Closure's Run() method unless the Cancel() method has been called.
// A CancellableClosure is deleted only after both its Run()
// method has been called and Unref() has been called once more than Ref().
// Therefore, CancellableClosure's Run() method should be called
// exactly once to allow deletion, even though the Run() method of the wrapped
// Closure may not be called if it has been cancelled.
//
// In normal use, the client creates a CancellableClosure and submits it to
// some other abstraction (perhaps via Executor::Add()) that guarantees
// eventually to call the CancellableClosure's Run() method.  The client will
// normally retain a pointer to the CancellableClosure to cancel the call to
// the wrapped Closure's Run() method, or to wait until completion (with
// Cancel() and/or WaitUntil()).
//
// A call to WaitUntil() may, at the caller's option, run the wrapped Closure
// in the context of the calling thread if the wrapped Closure has not yet been
// started or cancelled; this avoids deadlock in situations where an Executor
// may wait for a Closure being run by that same Executor. The client may
// control object lifetime by reference counting with Ref()/Unref().
//
// A CancellableClosure is not repeatable.
//
// The wrapped Closure may be in one of four states:
//  - not-yet-called - neither the wrapped Closure nor Cancel() has been called
//  - wont-be-called - Cancel() was called before the wrapped Closure, which
//                     will therefore not be called by Run() or WaitUntil().
//  - running -------- the wrapped Closure is running.
//  - finished ------- the wrapped Closure has run to completion.

// Examples:
//   util::callback::CancellableClosure *cc =
//       util::callback::CancellableClosure::New(wrapped_cl);
//   executor->Add(cc);
//   ...
//   if (want_to_cancel) {
//     Closure *cancelled_cl;
//     cc->Cancel(&cancelled_cl); // non-blocking
//     delete cancelled_cl; // delete if cancelled; no-op on 0-pointer otherwise
//   }
//   ...
//   cc->WaitUntil(kForever, 0);    // Wait for call to finish.
//                                  // May wait even if Cancel() was called;
//                                  // the closure may have started running.
//   cc->Unref(); // allow *cc to delete itself.

#include <cstdint>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "gloop/base/callback.h"

namespace util {
namespace callback {

class CancellableClosure : public Closure {
 public:
  // This type is neither copyable nor movable.
  CancellableClosure(const CancellableClosure&) = delete;
  CancellableClosure& operator=(const CancellableClosure&) = delete;

  // Allocate a new CancellableClosure with not-yet-called wrapped Closure
  // wrapped_cl, and refcount==1.
  // All instances are allocated with New(); deallocate with Unref().
  static CancellableClosure* New(Closure* wrapped_cl);

  // Increment or decrement the reference count.  This object is deleted when
  // this->Run() has returned and Unref() has been called once more than Ref().
  void Ref();    // increment refcount
  void Unref();  // decrement refcount

  // If the wrapped Closure is not-yet-called, set its state to running and
  // call wrapped_cl->Run().  On return, set the wrapped Closure's state to
  // finished.
  virtual void Run();

  // If the wrapped Closure is not-yet-called, set its state to wont-be-called,
  // and place its pointer in *pcl.  Return a value indicating action taken:
  enum CancelResult {
    RUNNING,            // wrapped_cl->Run() is running; *pcl set to 0.
    CANCELLED,          // *wrapped_cl was cancelled by this call; *pcl set
                        // to wrapped_cl.  this->Run() must still be called for
                        // this object to be freed, but it will not call
                        // wrapped_cl->Run().
    ALREADY_CANCELLED,  // a previous call returned CANCELLED; *pcl set to 0.
    FINISHED,           // wrapped_cl->Run() ran and finished; *pcl set to 0.
  };
  CancelResult Cancel(Closure** pcl);

  // Either wait until the wrapped Closure is either finished or wont-be-called
  // and then return true, or wait until UNIX time abstimeout_ms ms and return
  // false, whichever is sooner.  abstimeout_ms==kForever waits until the
  // closure is finished or wont-be-called and then returns true.  The calling
  // thread will run the wrapped callback if it's not-yet-called and
  // kRunInCaller is set in flags.  Requires refcount>0.
  bool WaitUntil(int64_t abstimeout_ms, int flags);
  static const int64_t kForever;  // abstimeout_ms: wait forever
  static const int kRunInCaller;  // flag:  run in calling thread if wrapped
                                  // Closure not-yet-called

  // Tests for repeatability: a CancellableClosure is never repeatable.
  virtual bool IsRepeatable() const { return false; }

  // -----------------------------------------------------------------------
  // implementation details follow
 private:
  absl::Mutex mu_;       // Protects all fields below.
  int refcount_;         // Reference count; under mu_.
  int state_;            // The wrapped-Closure state; under mu_.
  Closure* wrapped_cl_;  // Wrapped Closure; under mu_.
  void RunInternal() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  // Unlocks mu_ and may delete this.
  void UnrefAndUnlock() ABSL_UNLOCK_FUNCTION(mu_);
  explicit CancellableClosure(Closure* wrapped_cl);  // clients must use New()
  virtual ~CancellableClosure();                     // clients must use Unref()
};

}  // namespace callback
}  // namespace util
#endif  // THIRD_PARTY_GLOOP_UTIL_CALLBACK_CANCELLABLE_CLOSURE_H_
