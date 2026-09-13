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

// Implementation of CancellableClosure.

#include "gloop/util/callback/cancellable_closure.h"

#include <cstdint>

#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/callback.h"

namespace util {
namespace callback {

// flags for WaitUntil().
const int CancellableClosure::kRunInCaller = 0x1;

// The maximum int64 is used to mean "forever" in waits.
const int64_t CancellableClosure::kForever = (~static_cast<uint64_t>(0)) >> 1;

// Values of state_
enum {
  STATE_NOT_YET_CALLED,  // this->Run() has not been called.
  STATE_WONT_BE_CALLED,  // this->Run() has not been called, and this->Cancel()
                         // has.  The wrapped closure won't be called by
                         // this->Run().
  STATE_RUNNING,         // this->Run() is running.
  STATE_FINISHED         // this->Run() has returned.
};

// All uses of the constructor are via New() to ensure that all instances are
// heap-allocated and reference-counted.
CancellableClosure::CancellableClosure(Closure* wrapped_cl)
    : refcount_(2),  // internal extra refcount for invocation of Run()
      state_(STATE_NOT_YET_CALLED),
      wrapped_cl_(wrapped_cl) {}

CancellableClosure::~CancellableClosure() {}

/*static*/ CancellableClosure* CancellableClosure::New(Closure* wrapped_cl) {
  return new CancellableClosure(wrapped_cl);
}

// L < this->mu_
void CancellableClosure::Ref() {
  this->mu_.lock();
  this->refcount_++;
  this->mu_.unlock();
}

// L < this->mu_
void CancellableClosure::Unref() {
  this->mu_.lock();
  UnrefAndUnlock();
}

// L >= this->mu_
void CancellableClosure::UnrefAndUnlock() {
  mu_.AssertHeld();
  this->refcount_--;
  CHECK_GE(this->refcount_, 0);
  bool del = (this->refcount_ == 0);
  this->mu_.unlock();
  if (del) {
    delete this;
  }
}

// Return whether the state is finish or wont-be-called.
// Used with Condition.
static inline bool IsComplete(int* pstate) {
  return *pstate == STATE_FINISHED || *pstate == STATE_WONT_BE_CALLED;
}

// Internal version of Run(), called from both Run() and from WaitUntil().
// With this->mu_ held, if the state is not-yet-called, set the state to
// running and call wrapped_closure->Run().  In any case, set the state to
// finished.  Releases and reacquires this->mu_ in order to run the wrapped
// closure.  Does not return until the wrapped closure either has completed, or
// never will be run due to cancellation. L >= this->mu_
void CancellableClosure::RunInternal() {
  if (this->state_ == STATE_NOT_YET_CALLED) {
    Closure* wrapped_cl = this->wrapped_cl_;
    this->wrapped_cl_ = nullptr;  // zero for error checking
    this->state_ = STATE_RUNNING;
    this->mu_.unlock();
    wrapped_cl->Run();
    this->mu_.lock();
    this->state_ = STATE_FINISHED;
  } else {
    this->mu_.Await(absl::Condition(&IsComplete, &this->state_));
  }
}

// L < this->mu_
void CancellableClosure::Run() {
  this->mu_.lock();
  this->RunInternal();
  UnrefAndUnlock();
}

// L < this->mu_
bool CancellableClosure::WaitUntil(int64_t abstimeout_ms, int flags) {
  bool result = true;
  this->mu_.lock();
  CHECK_GT(this->refcount_, 0)
      << "Call to CancellableClosure::WaitUntil with reference count==0";
  if (this->state_ == STATE_NOT_YET_CALLED &&
      (flags & CancellableClosure::kRunInCaller) != 0) {
    this->RunInternal();
  } else if (abstimeout_ms == CancellableClosure::kForever) {
    this->mu_.Await(absl::Condition(&IsComplete, &this->state_));
  } else {
    result = this->mu_.AwaitWithTimeout(
        absl::Condition(&IsComplete, &this->state_),
        (absl::FromUnixMillis(abstimeout_ms) - absl::Now()));
  }
  this->mu_.unlock();
  return result;
}

// L < this->mu_
CancellableClosure::CancelResult CancellableClosure::Cancel(Closure** pcl) {
  CancellableClosure::CancelResult result;
  this->mu_.lock();
  if (this->state_ == STATE_NOT_YET_CALLED) {
    this->state_ = STATE_WONT_BE_CALLED;
    *pcl = this->wrapped_cl_;
    this->wrapped_cl_ = nullptr;  // zero for error checking
    result = CancellableClosure::CANCELLED;
  } else if (this->state_ == STATE_RUNNING) {
    result = CancellableClosure::RUNNING;
    *pcl = nullptr;
  } else if (this->state_ == STATE_WONT_BE_CALLED) {
    result = CancellableClosure::ALREADY_CANCELLED;
    *pcl = nullptr;
  } else {
    result = CancellableClosure::FINISHED;
    *pcl = nullptr;
  }
  this->mu_.unlock();
  return result;
}

}  // namespace callback
}  // namespace util
