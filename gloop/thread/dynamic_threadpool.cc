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

#include "gloop/thread/dynamic_threadpool.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <deque>
#include <list>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/callback.h"
#include "gloop/base/sysinfo.h"
#include "gloop/thread/executor.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"
#include "gloop/thread/wait_state.h"
#include "gloop/util/functional/to_callback.h"

// Worker thread
class DynamicThreadPoolWorker : public Thread {
  friend class DynamicThreadPool;

 public:
  explicit DynamicThreadPoolWorker(const thread::Options& thread_options,
                                   absl::string_view thread_name_prefix,
                                   DynamicThreadPool* parent)
      : Thread(thread_options, thread_name_prefix),
        parent_(parent),
        submitted_(nullptr),
        exit_(false),
        idle_pos_(parent->idle_threads_->end()) {
    threads_pos_ = parent_->threads_->insert(parent_->threads_->end(), this);
  }

  ~DynamicThreadPoolWorker() override {
    if (idle_pos_ != parent_->idle_threads_->end()) {
      parent_->idle_threads_->erase(idle_pos_);
    }
    if (threads_pos_ != parent_->threads_->end()) {
      parent_->threads_->erase(threads_pos_);
    }
  }

  void Run() override {
    Closure* callback;
    *thread::Executor::CurrentExecutorPointerInternal() = parent_;
    while ((callback = parent_->Dequeue(this)) != nullptr) {
      callback->Run();
    }
  }

 private:
  // shortcut
  typedef DynamicThreadPool::ThreadList ThreadList;

  bool idle() const { return (idle_pos_ != parent_->idle_threads_->end()); }

  // Mark this thread as idle
  void MakeIdle() {
    // Insert at the beginning so that recently-used threads get reused (for
    // better cache locality)
    if (!idle()) {
      idle_pos_ =
          parent_->idle_threads_->insert(parent_->idle_threads_->begin(), this);
    }
    CHECK(idle());
  }

  // Mark this thread as not idle
  void MakeNotIdle() {
    if (idle()) {
      parent_->idle_threads_->erase(idle_pos_);
      idle_pos_ = parent_->idle_threads_->end();
    }
    CHECK(!idle());
  }

  // Unlink this thread from the parent's list
  void Unlink() {
    MakeNotIdle();
    CHECK(threads_pos_ != parent_->threads_->end());
    parent_->threads_->erase(threads_pos_);
    threads_pos_ = parent_->threads_->end();

    parent_->exited_threads_->push_back(this);
  }

  // Submit a closure to this (idle) thread
  void Submit(Closure* closure) {
    // We only submit directly to idle threads- so this thread should have no
    // work submitted to it already
    CHECK(!submitted_);
    CHECK(closure);
    submitted_ = closure;
    idle_cond_.Signal();
  }

  // Make this thread exit
  void MakeExit() {
    exit_ = true;
    idle_cond_.Signal();
  }

 private:
  DynamicThreadPool* parent_;
  absl::CondVar idle_cond_;
  Closure* submitted_;
  bool exit_;
  ThreadList::iterator threads_pos_;  // position in parent_->threads_
  ThreadList::iterator idle_pos_;     // position in parent_->idle_threads_
};

DynamicThreadPool::Options::Options()
    : queue_capacity(INT_MAX),
      min_threads(1),
      max_threads(std::min(NumCPUs(), 32)),
      max_idle_ms(20 * 1000) {}

const int DynamicThreadPool::kReapFactor = 3;

DynamicThreadPool::DynamicThreadPool(absl::string_view thread_name_prefix,
                                     const Options& options)
    : DynamicThreadPool(thread_name_prefix, options.thread_options,
                        options.queue_capacity, options.min_threads,
                        options.max_threads, options.max_idle_ms) {}

DynamicThreadPool::DynamicThreadPool(int queue_capacity, int min_threads,
                                     int max_threads, int max_idle_ms)
    : DynamicThreadPool("DynamicThreadPoolWorker", thread::Options(),
                        queue_capacity, min_threads, max_threads, max_idle_ms) {
}

DynamicThreadPool::~DynamicThreadPool() { ShutDown(); }

void DynamicThreadPool::ShutDown() {
  if (ended_.HasBeenNotified()) {
    // delete all threads
    while (!threads_->empty())
      delete threads_->front();  // this removes itself from the list
    return;
  }

  mutex_.lock();
  Check();
  // Wait for all threads: the ones that have already exited...
  ThreadVec join_threads(*exited_threads_);
  exited_threads_->clear();

  // ... and everyone else.
  for (ThreadList::iterator it = threads_->begin(); it != threads_->end();
       ++it) {
    (*it)->MakeExit();
    join_threads.push_back(*it);
  }

  // We don't want to hold the mutex while joining
  mutex_.unlock();
  JoinThreads(&join_threads);
  ended_.Notify();
}

DynamicThreadPool::DynamicThreadPool(absl::string_view thread_name_prefix,
                                     const thread::Options& thread_options,
                                     int queue_capacity, int min_threads,
                                     int max_threads, int max_idle_ms)
    : queue_(new std::deque<Closure*>()),
      queue_capacity_(queue_capacity),
      threads_(new ThreadList()),
      idle_threads_(new ThreadList()),
      exited_threads_(new ThreadVec()),
      min_threads_(min_threads),
      max_threads_(max_threads),
      max_idle_ms_(max_idle_ms),
      thread_options_(thread_options),
      thread_name_prefix_(thread_name_prefix) {
  CHECK_GE(queue_capacity, 0);
  CHECK_GT(min_threads, 0);
  CHECK_GE(max_threads, min_threads);

  thread_options_.set_joinable(true);

  // Create the threads- they're initially idle
  for (int i = 0; i < min_threads; i++) AddThread()->MakeIdle();

  for (ThreadList::iterator it = threads_->begin(); it != threads_->end();
       ++it) {
    (*it)->Start();
  }
}

// Create a new thread
DynamicThreadPoolWorker* DynamicThreadPool::AddThread() {
  DynamicThreadPoolWorker* thread =
      new DynamicThreadPoolWorker(thread_options_, thread_name_prefix_, this);
  VLOG(2) << "AddThread: " << thread;

  // Reap all exited threads
  if (exited_threads_->size() >= min_threads_ * kReapFactor) Reap();
  return thread;
}

// Reap all exited threads
void DynamicThreadPool::Reap() {
  ThreadVec join_threads(*exited_threads_);
  exited_threads_->clear();
  int num_threads = 0;

  if (!join_threads.empty()) {
    num_threads = join_threads.size();
    // We don't want to hold the mutex while joining
    mutex_.unlock();
    JoinThreads(&join_threads);
    mutex_.lock();
  }
  VLOG(2) << "Reap: reaped " << num_threads << " threads";
}

void DynamicThreadPool::ScheduleAt(absl::Time when,
                                   absl::AnyInvocable<void() &&> callback) {
  Closure* closure = util::functional::ToCallback(std::move(callback));
  absl::Time now = absl::Now();
  if (when <= now) {
    AddInternal(closure, kAddAfter);
  } else {
    Delay(
        std::max(absl::ZeroDuration(), when - now),
        absl::bind_front(&DynamicThreadPool::AddAfterInternal, this, closure));
  }
}

void DynamicThreadPool::AddAfterInternal(Closure* closure) {
  AddInternal(closure, kAddAfter);
}

void DynamicThreadPool::Schedule(absl::AnyInvocable<void() &&> callback) {
  AddInternal(util::functional::ToCallback(std::move(callback)), kAdd);
}

bool DynamicThreadPool::TrySchedule(absl::AnyInvocable<void() &&> callback) {
  Closure* closure = util::functional::ToCallback(std::move(callback));
  bool added = AddInternal(closure, kTryAdd);
  if (!added) {
    delete closure;
  }
  return added;
}

bool DynamicThreadPool::ScheduleIfReadyToRun(
    absl::AnyInvocable<void() &&> callback) {
  Closure* closure = util::functional::ToCallback(std::move(callback));
  bool added = AddInternal(closure, kAddIfReadyToRun);
  if (!added) {
    delete closure;
  }
  return added;
}

int DynamicThreadPool::queue_count() const {
  absl::MutexLock lock(mutex_);
  return queue_->size();
}

int DynamicThreadPool::queue_capacity() const { return queue_capacity_; }

int DynamicThreadPool::num_threads() const {
  absl::ReaderMutexLock lock(mutex_);
  return threads_->size();
}

Thread* DynamicThreadPool::thread(int i) const {
  absl::MutexLock lock(mutex_);
  ThreadList::const_iterator it;
  // Count forward to the i'th element
  for (it = threads_->begin(); it != threads_->end() && i > 0; ++it, --i);
  return (it == threads_->end()) ? nullptr : *it;
}

// Join and delete all threads in the vector
void DynamicThreadPool::JoinThreads(ThreadVec* join_threads) {
  for (int i = 0; i < join_threads->size(); i++) {
    VLOG(2) << "Join " << (*join_threads)[i];
    (*join_threads)[i]->Join();
    delete (*join_threads)[i];
  }
  join_threads->clear();
}

void DynamicThreadPool::Check() {
  // We enqueue directly to idle threads, and (if there are no idle threads) we
  // place requests in the queue.
  CHECK(idle_threads_->empty() || queue_->empty());
}

bool DynamicThreadPool::ReadyToRunOrQueue() const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
  return (!idle_threads_->empty() || threads_->size() < max_threads_ ||
          queue_->size() < queue_capacity_);
}

bool DynamicThreadPool::AddInternal(Closure* closure, Caller caller) {
  CHECK(closure);
  VLOG(2) << "AddInternal(" << caller << ")";
  absl::MutexLock lock(mutex_);
  Check();

  while (true) {
    // If there's an idle thread, add the Closure to it.
    if (!idle_threads_->empty()) {
      DynamicThreadPoolWorker* thread = idle_threads_->front();
      thread->MakeNotIdle();
      VLOG(2) << "AddInternal: running using idle thread " << thread;
      thread->Submit(closure);
      return true;
    }
    // Otherwise, if we are below the thread limit, spawn a new thread and add
    // the Closure to it.
    if (threads_->size() < max_threads_) {
      CHECK(queue_->empty());
      DynamicThreadPoolWorker* thread = AddThread();
      VLOG(2) << "AddInternal: running using new thread " << thread;
      thread->Submit(closure);
      thread->Start();
      return true;
    }
    // Otherwise, if we were called by AddIfReadyToRun(), return false, as
    // there are no threads available to run the Closure.
    if (caller == kAddIfReadyToRun) {
      VLOG(2) << "AddInternal: not ready to run";
      return false;
    }
    // Otherwise, if the queue isn't full, or if we were called by AddAfter(),
    // which is not allowed to block, add the Closure to the queue.
    if (queue_->size() < queue_capacity_ || caller == kAddAfter) {
      VLOG(2) << "AddInternal: queueing";
      queue_->push_back(closure);
      return true;
    }
    // Otherwise, if we were called by TryAdd(), return false, as we can
    // neither run nor queue the Closure.
    if (caller == kTryAdd) {
      VLOG(2) << "AddInternal: not ready to run or queue";
      return false;
    }
    // Otherwise, wait for an idle thread, the ability to spawn a new thread,
    // or the ability to queue the Closure.
    VLOG(2) << "AddInternal: blocking";
    mutex_.Await(absl::Condition(this, &DynamicThreadPool::ReadyToRunOrQueue));
  }
}

Closure* DynamicThreadPool::Dequeue(DynamicThreadPoolWorker* thread) {
  absl::MutexLock lock(mutex_);

  int64_t idle_wait = max_idle_ms_;
  while (true) {
    // Check for a request sent to me directly
    if (thread->submitted_) {
      CHECK(!thread->idle());
      VLOG(2) << "Dequeue: submitted " << thread;
      Closure* closure = thread->submitted_;
      thread->submitted_ = nullptr;
      return closure;
    }

    // Check for requests in the queue
    if (!queue_->empty()) {
      CHECK(!thread->idle());
      VLOG(2) << "Dequeue: dequeue " << thread;
      Closure* closure = queue_->front();
      queue_->pop_front();
      return closure;
    }

    // Check for exit signals
    if (thread->exit_) {
      VLOG(2) << "Dequeue: exit " << thread;
      break;
    }

    if (!thread->idle()) {
      VLOG(2) << "Dequeue: idle " << thread;
      thread->MakeIdle();
    }

    thread::WaitStateScope scope(
        thread::WaitStateScope::WaitState::kWaitingForWork);
    // Wait forever (no timeout) if:
    // - no timeout is specified (max_idle_ms_ < 0), or
    // - we're at min_threads_ already
    if (max_idle_ms_ < 0 || threads_->size() == min_threads_) {
      thread->idle_cond_.Wait(&mutex_);
    } else if (idle_wait > 0) {
      int64_t wait_start = absl::ToUnixMillis(absl::Now());
      thread->idle_cond_.WaitWithTimeout(&mutex_,
                                         absl::Milliseconds(idle_wait));
      int64_t time_waited = absl::ToUnixMillis(absl::Now()) - wait_start;
      // protect against time going backwards
      if (time_waited > 0) idle_wait -= time_waited;
    } else {
      // Timed out
      VLOG(2) << "Dequeue: timeout " << thread;
      break;
    }
  }

  thread->Unlink();
  return nullptr;
}

void DynamicThreadPool::IncrementMaxThreads() {
  absl::MutexLock lock(mutex_);
  ++max_threads_;
  // If the queue is non-empty, and we are now allowed,
  // start up a new thread.
  // NOTE: In the typical case, we will be allowed, but if Decrement &
  // Increment calls come in fast and furious, we may not be.
  if (!queue_->empty() && (threads_->size() < max_threads_)) {
    Closure* closure = queue_->front();
    // The only time a NULL closure should be added to the queue is
    // from the ThreadPool destructor. In that case, nobody has any
    // business calling IncrementMaxThreads().
    if (closure == nullptr) {
      DLOG(FATAL)
          << "IncrementMaxThreads(): NULL closure at the front of the queue.";
      return;
    }
    queue_->pop_front();  // pop the closure from queue so that some other
                          // thread will not pick it before AddThread()/Reap()
                          // return, as they release and reacquire mutex_.
    DynamicThreadPoolWorker* thread = AddThread();
    VLOG(2) << "IncrementMaxThreads: grow " << thread;

    // Reason for submitting: If we do a lot of IncrementMaxThreads operations
    // in quick succession, we could end up creating more threads than we need.
    // If the queue has exactly one closure in it, calling IncrementMaxThreads
    // 100 times before any thread has a chance to start running and pick up a
    // closure from the queue creates 100 new threads, when one would be enough.
    // So we pop the closure ahead from the queue and submit here.
    //
    // However this can add latency to pending closure, if Reap() gets kicked
    // as part of AddThread(). Pending closure will be delayed, even if other
    // threads execute. If this is going to be problem, one solution is to use
    // submitted_queue_  instead of single per thread submitted_ variable.
    thread->Submit(closure);
    thread->Start();
  }
}

bool DynamicThreadPool::DecrementMaxThreads() {
  absl::MutexLock lock(mutex_);
  // In the planned usage, the number of times DecrementMaxThreads()
  // is called will always be less than the number of times
  // IncrementMaxThreads() is called. Still, take precautions.
  if (max_threads_ > min_threads_) {
    --max_threads_;
    return true;
  } else {
    LOG(ERROR) << "DecrementMaxThreads called with max_threads_="
               << max_threads_ << " and min_threads_=" << min_threads_;
    return false;
  }
}
