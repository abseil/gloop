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

#include "gloop/thread/timedcall.h"

#include <atomic>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/walltime.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/wait_state.h"
#include "gloop/util/gtl/intrusive_heap.h"

struct TimedCallCompare {
  bool operator()(const TimedCall* lhs, const TimedCall* rhs) const {
    return lhs->deadline_ < rhs->deadline_;
  }
};

struct TimedCallLinkAccess {
  static gtl::IntrusiveHeapLink Get(const TimedCall* t) { return t->heap_; }
  static void Set(TimedCall* t, gtl::IntrusiveHeapLink link) {
    t->heap_ = link;
  }
};

using TimedCallHeap =
    gtl::IntrusiveHeap<TimedCall, TimedCallCompare, TimedCallLinkAccess>;

// Protects q, timer_thread, timer_thread_id, got_thread_id, deadline_ptr.
ABSL_CONST_INIT static absl::Mutex mu(absl::kConstInit);

// Signalled when an earlier first element of *q is inserted.
ABSL_CONST_INIT static absl::CondVar* inserted = nullptr;

static TimedCallHeap* q ABSL_GUARDED_BY(mu);  // timeout queue

int TimedCall::NumScheduled() {
  absl::ReaderMutexLock l(mu);
  return q->size();
}

TimedCall* Top() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu) {
  return q->empty() ? nullptr : q->top();
}

// Signalled when timer_thread makes a state change useful to a client.
ABSL_CONST_INIT static absl::CondVar* client = nullptr;

static Thread* timer_thread
    ABSL_GUARDED_BY(mu);  // the thread that runs callbacks
static pthread_t timer_thread_id
    ABSL_GUARDED_BY(mu);  // thread id of timer_thread
static bool got_thread_id
    ABSL_GUARDED_BY(mu);  // true iff timer_thread_id valid
static WallTime* deadline_ptr ABSL_GUARDED_BY(mu);  // pointer to deadline in
                                                    // running TimedCall or null

const WallTime TimedCall::Running = 0.0;   // must be zero
const WallTime TimedCall::Expired = -1.0;  // must be -ve
const WallTime TimedCall::Stop = -2.0;     // must be -ve

// This thread loops forever looking at the timeout queue, and waiting for
// changes.  If a timeout goes off, is calls the callback.
void TimedCall::Thread() {
  absl::MutexLock l(mu);
  timer_thread_id = pthread_self();
  got_thread_id = true;
  client->SignalAll();
  for (;;) {
    absl::Time now = absl::Now();
    TimedCall* first = Top();
    while (first != nullptr &&
           base::FromWallTime(first->deadline_) <= now) {  // process timeouts
      // "first" will not be valid after we've erased it, but deadline_ptr will
      // be valid or 0 because the destructor zeroes deadline_ptr
      // if it points to the deleted TimedCall, and the code below
      // changes *deadline_ptr only if it was Running, which is set only
      // by this code.
      deadline_ptr = &first->deadline_;
      *deadline_ptr = Running;
      absl::AnyInvocable<void() &&> f = std::move(first->f_);
      q->Remove(first);  // stop timer
      mu.unlock();
      if (f != nullptr) {
        std::move(f)();
        f = {};  // Destroy the invocable outside the lock.
      }
      mu.lock();
      if (deadline_ptr != nullptr && *deadline_ptr == Running) {
        // client didn't override or delete TimedCall
        *deadline_ptr = Expired;
        // ~TimedCall must not see !active_ before any write to *deadline_ptr.
        first->active_.store(false, std::memory_order_release);
      }
      deadline_ptr = nullptr;
      // Set()'s invariants only require callers to hold no resources on which
      // the *associated* callback may depend.  We must be careful to release
      // any threads waiting on "first.Set()" before starting another callback.
      client->SignalAll();
      first = Top();
    }
    const absl::Time deadline = first != nullptr
                                    ? base::FromWallTime(first->deadline_)
                                    : absl::Now() + absl::Seconds(10);

    thread::WaitStateScope scope(
        thread::WaitStateScope::WaitState::kWaitingForWork);
    inserted->WaitWithDeadline(&mu, deadline);
  }
}

// One time initialization to start timer_thread and allocate timeout queue.
// Called from first Set().
void TimedCall::InitTimedCall() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu) {
  if (q == nullptr) {  // initialize q and start thread
    q = new TimedCallHeap;
    client = new absl::CondVar;
    inserted = new absl::CondVar;
    timer_thread = new ClosureThread(&TimedCall::Thread);
    timer_thread->SetStackSize(64 * 1024);
    timer_thread->SetNamePrefix("timedcall");
    timer_thread->Start();
  }
}

// Common code needed by constructors.
void TimedCall::Initialize() {
  this->deadline_ = Stop;
  this->f_ = nullptr;
  this->active_.store(false, std::memory_order_relaxed);
}

// Constructors
TimedCall::TimedCall() { this->Initialize(); }

TimedCall::TimedCall(WallTime deadline, absl::AnyInvocable<void() &&> f) {
  this->Initialize();
  this->Set(deadline, std::move(f));
}

// Removes this TimedCall from timer queue
// If (flags & kNoWait) == 0 and we're not in the TimedCall thread, wait for
// the call to complete if it's now running.
// L=mu;  may release and reacquire mu.
void TimedCall::Remove(int flags) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu) {
  // if in timer queue or call running now...
  if (this->deadline_ >= 0) {
    while (!got_thread_id) {  // wait until thread_id is available
      client->Wait(&mu);
    }
    // If we're in the TimedCall thread or no waiting is allowed, don't wait.
    if (!pthread_equal(pthread_self(), timer_thread_id) &&
        (flags & TimedCall::kNoWait) == 0) {
      // If not in TimedCall thread and waiting is allowed, wait until this
      // call is not running.
      while (deadline_ptr == &this->deadline_) {
        client->Wait(&mu);
      }
      // call has now either run to completion or will not run
    }
    if (this->deadline_ > 0) {  // if still in timer queue
      q->Remove(this);          // remove from queue
      this->deadline_ = Stop;
      // mu_ coordinates the actual writes to active.  Any destruction of *this,
      // must be coordinated with any existing future calls to Set(), and will
      // see their value of active_.
      this->active_.store(false, std::memory_order_release);
    }
  }
}

// Destructor
TimedCall::~TimedCall() {
  // active_ is advisory here.  We are guaranteed to see active_ == true if this
  // object is in the heap or running, we may see active_ == true or false
  // otherwise.
  // SUBTLE: We need an Acquire here to guarantee that writes from a subsequent
  // reallocation cannot be ordered before an observation of !active_.
  if (this->active_.load(std::memory_order_acquire)) {
    absl::MutexLock l(mu);
    this->Remove(0 /*flags*/);  // it is always safe to remove
    DCHECK_EQ(this->heap_.get(), gtl::IntrusiveHeapLink::kNotMember);
    if (deadline_ptr == &this->deadline_) {  // delete from running callback
      deadline_ptr = nullptr;  // discard pointer to deallocated storage
    }
  }
}

void TimedCall::Set(WallTime deadline, absl::AnyInvocable<void() &&> f,
                    int flags) {
  absl::MutexLock l(mu);
  InitTimedCall();
  this->Remove(flags);
  if (deadline > 0 || this->deadline_ > 0) {  // timer on or will be
    this->deadline_ = deadline;               // change timer
  }  // else leave timer in its current "off" state.
  this->f_ = std::move(f);
  if (this->deadline_ > 0) {  // add to timer queue
    // If the caller is synchronizing external destruction of *this with the
    // completion of this call, then they are already responsible for ensuring
    // that this write is visible.
    this->active_.store(true, std::memory_order_relaxed);
    q->Push(this);
    if (this->heap_.get() == 0) {
      inserted->Signal();
    }
  }
}

WallTime TimedCall::deadline() const {
  absl::MutexLock l(mu);
  return this->deadline_;
}

absl::AnyInvocable<void() &&> TimedCall::f() {
  absl::MutexLock l(mu);
  return std::move(this->f_);
}

// This structure keeps the context of the call initiated by RunAt
struct RunAtHelper {
  TimedCall tc;
  absl::AnyInvocable<void() &&> f;  // the user-provided function
};

// Weak to allow overriding TimedCall usage with eventmanager::Default().
ABSL_ATTRIBUTE_WEAK void TimedCall::RunAt(WallTime deadline,
                                          absl::AnyInvocable<void() &&> f) {
  CHECK_GT(deadline, 0);
  RunAtHelper* helper = new RunAtHelper();
  helper->f = std::move(f);
  helper->tc.Set(deadline, [helper] {
    std::move(helper->f)();
    delete helper;
  });
}
