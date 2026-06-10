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

#include "gloop/thread/threadpool.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <ctime>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/chunked_queue.h"
#include "absl/flags/flag.h"
#include "absl/functional/any_invocable.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gloop/base/context.h"
#include "gloop/thread/add_after_helper.h"
#include "gloop/thread/cpu_subcontainer.h"
#include "gloop/thread/python_stack_size.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"
#include "gloop/thread/wait_state.h"
#include "gloop/thread/watchdog.h"
#include "gloop/util/functional/to_callback.h"
#include "gloop/util/intops/saturated_cast.h"

#if THREAD_HAVE_ALTERNATE_THREAD_POOL
#error Feature macros and BUILD file are out of sync.
#endif

ABSL_FLAG(
    bool, thread_pool_lazy_spawn_workers, true,
    "If false, ThreadPool will use the historical default behavior of "
    "spawning all threads when StartWorkers is called. If true, "
    "ThreadPool will use the new behavior of lazily spawning workers "
    "when there is not one actively ready to handle new work.\n\n"
    "NOTE: This flag will be REMOVED in the near future, it is only intended "
    "for quick production fixes. If you determine that you need to retain this "
    "behavior long term, set force_eager_thread_creation on ThreadPool "
    "construction.");

AbstractThreadPool::~AbstractThreadPool() {}

// Run callback->Run().  To permit a callback to be wrapped in another.
static void RunCallback(WatchdogCallback callback, WatchDog* watchdog) {
  (*callback)(watchdog);
}

void ThreadPool::RunWorker() {
  {
    absl::MutexLock lock(mutex_);
    running_threads_++;
  }
  *thread::Executor::CurrentExecutorPointerInternal() = this;
  std::unique_ptr<WatchDog> watchdog;
  if (watchdog_timeout_ > absl::ZeroDuration() &&
      watchdog_timeout_ != absl::InfiniteDuration()) {
    watchdog =
        std::make_unique<WatchDog>((std::string("ThreadPool worker ") +
                                    LiveThread_Name(Thread_GetMyLiveThread())),
                                   watchdog_timeout_);
    if (watchdog_callback_ != nullptr) {
      watchdog->SetCallback(::util::functional::ToPermanentCallback(
          absl::bind_front(&RunCallback, watchdog_callback_)));
    }
  }

  while (true) {
    // Disable the watchdog while we're idle
    if (watchdog) {
      watchdog->Disable();
    }

    std::unique_ptr<Entry> entry = DequeueWork();
    if (entry == nullptr) {
      break;
    }
    if (watchdog) {
      watchdog->Alive();
    }
    // Run scheduled callback with our copy of the scheduler's context.
    // We early deallocate `entry` so any (tail) scheduling from inside
    // the callback can directly reuse the hot memory allocation.
    base::WithContext with(std::move(entry->context));
    absl::AnyInvocable<void() &&> callback = std::move(entry->callback);
    entry = nullptr;
    std::move(callback)();
  }
}

ThreadPool::ThreadPool(int num_threads, Options options)
    : capacity_(options.queue_capacity),
      // It is a common error to call ThreadPool(workitems.size()), which
      // crashes when workitems is empty. Prevent those crashes by creating at
      // least one thread.
      max_threads_(num_threads == 0 ? 1 : num_threads),
      watchdog_timeout_(options.watchdog_timeout),
      name_prefix_(std::move(options.name_prefix)),
      thread_options_(
          options
              .thread_options
              // ThreadPool requires the threads to be joinable
              .set_joinable(true)
              // Potentially increase the stack size if we are a Python binary.
              // <link>
              .set_stack_size(thread::python::MaybeAdjustStackSize(
                  options.thread_options.stack_size(), "ThreadPool"))),
      eager_thread_creation_(
          options.force_eager_thread_creation ||
          !absl::GetFlag(FLAGS_thread_pool_lazy_spawn_workers)),
      add_after_helper_(nullptr,
                        [this](auto cb) {
                          absl::MutexLock l(mutex_);
                          DCHECK(!stopping_)
                              << "AddAfterHelper invoked closure after it has "
                                 "been shutdown, this "
                                 "is a bug in AddAfterHelper";
                          if (ABSL_PREDICT_FALSE(stopping_)) return;
                          InternalPut(std::move(cb));
                        }),
      watchdog_callback_(options.watchdog_callback),
      subcontainer_(
          thread::CpuSubContainer::Create(thread_options_, name_prefix_)) {
  LOG_IF(WARNING, num_threads == 0)
      << "Attempted to create ThreadPool (name: " << name_prefix_
      << ") with num_threads=0, "
      << "falling back to num_threads=1.";
  CHECK_GT(max_threads_, 0u);
  CHECK_GT(options.queue_capacity, 0);

  // Spawn a single thread to handle work by default, spawn more on an
  // as-needed basis.
  const size_t to_start = eager_thread_creation_ ? max_threads_ : 1;
  absl::MutexLock lock(mutex_);
  for (size_t i = 0; i < to_start; ++i) {
    SpawnThread();
  }
}

ThreadPool::~ThreadPool() {
  // Provide for safe shutdown by forcing all AddAfter() closures onto
  // queue_, even if they have not yet expired. After this call returns, no
  // more closures can be queued, even if AddAfter() is somehow called.
  add_after_helper_.ShutdownAndRunPendingImmediately();
  // Make threads finish up by setting stopping_. Ensure all threads waiting see
  // this change by signalling their condvar.
  {
    absl::MutexLock l(mutex_);
    stopping_ = true;
    for (Waiter& waiter : waiters_) {
      waiter.cv.Signal();
    }
    // Wake any producer threads blocked in Put() waiting for room in a
    // bounded queue so they can observe `stopping_` and bail out before
    // the ThreadPool (and its mutex) is destroyed.
    wait_nonfull_.SignalAll();
    // Wait until the queue is empty. This implies no new threads will be
    // spawned, and all existing threads are exiting.
    auto queue_empty = [this]() ABSL_SHARED_LOCKS_REQUIRED(mutex_) {
      return queue_.empty();
    };
    mutex_.Await(absl::Condition(&queue_empty));
  }
  // Join and delete all threads. Because the queue is empty, we know no new
  // threads will be added to threads_.
  for (Thread* worker : threads_) {
    worker->Join();
    delete worker;
  }

  delete subcontainer_;
}

void ThreadPool::SpawnThread() {
  CHECK_LE(threads_.size(), max_threads_);
  Thread* thread = threads_.emplace_back(new ClosureThread(
      thread_options_, name_prefix_, [this] { RunWorker(); }));
  thread->SetInitialCpuSubContainer(subcontainer_);
  thread->Start();
}

void ThreadPool::Schedule(absl::AnyInvocable<void() &&> callback) {
  DCHECK(callback != nullptr);
  Put(std::move(callback));
}

void ThreadPool::ScheduleAt(absl::Time when,
                            absl::AnyInvocable<void() &&> callback) {
  DCHECK(callback != nullptr);
  DCHECK(!Stopping()) << "Callback added after destructor started";
  // TimedCall::RunAt requires that the schedule time be after the unix epoch.
  add_after_helper_.ScheduleAddAfterAt(std::max(when, absl::FromUnixSeconds(1)),
                                       std::move(callback));
}

bool ThreadPool::TrySchedule(absl::AnyInvocable<void() &&> callback) {
  DCHECK(callback != nullptr);
  return TryPut(std::move(callback));
}

bool ThreadPool::ScheduleIfReadyToRun(absl::AnyInvocable<void() &&> callback) {
  DCHECK(callback != nullptr);
  return PutIfReadyToRun(std::move(callback));
}

int ThreadPool::queue_count() const {
  return queue_size_.load(std::memory_order_relaxed);
}

int ThreadPool::queue_capacity() const {
  absl::MutexLock lock(mutex_);
  return util_intops::saturated_cast<int>(capacity_);
}

std::string ThreadPool::thread_name_prefix() const { return name_prefix_; }

int ThreadPool::num_threads() const { return max_threads_; }

void ThreadPool::Put(absl_nonnull absl::AnyInvocable<void() &&>&& callback) {
  // Wait for queue to be not-full
  absl::MutexLock m(mutex_);
  DCHECK(!stopping_) << "Callback added after destructor started";
  if (ABSL_PREDICT_FALSE(stopping_)) return;
  if (IsLimitedCapacity()) {
    while (queue_.size() >= capacity_ && !stopping_) {
      wait_nonfull_.Wait(&mutex_);
    }
  }
  // Recheck stopping_ after waking from wait_nonfull_, which the destructor
  // signals during shutdown; without this the pool object can be destroyed
  // before InternalPut() runs, causing a use-after-free on mutex_.
  if (ABSL_PREDICT_FALSE(stopping_)) return;
  InternalPut(std::move(callback));
}

bool ThreadPool::TryPut(absl_nonnull absl::AnyInvocable<void() &&>&& callback) {
  absl::MutexLock m(mutex_);
  DCHECK(!stopping_) << "Callback added after destructor started";
  if (ABSL_PREDICT_FALSE(stopping_)) return false;

  // Check if the queue is full
  if (queue_.size() >= capacity_) {
    return false;
  }
  InternalPut(std::move(callback));
  return true;
}

bool ThreadPool::PutIfReadyToRun(
    absl_nonnull absl::AnyInvocable<void() &&>&& callback) {
  absl::MutexLock m(mutex_);
  DCHECK(!stopping_) << "Callback added after destructor started";
  if (ABSL_PREDICT_FALSE(stopping_)) return false;
  if (waiters_.empty()) return false;
  if (queue_.size() >= capacity_) return false;
  if (!queue_.empty() && /* O(n) */ waiters_.size() <= queue_.size()) {
    return false;
  }
  InternalPut(std::move(callback));
  return true;
}

void ThreadPool::SignalWaiter() {
  DCHECK(!queue_.empty());
  if (waiters_.empty()) {
    // If there are no waiters, try spawning a new thread to pick up work.
    if (running_threads_ == threads_.size() && threads_.size() < max_threads_) {
      SpawnThread();
    }
  } else {
    // If there are waiters we wake the last inserted waiter. Note that we can
    // signal this waiter multiple times. This is not only ok but it is crucial
    // to reduce spurious wakeups.
    waiters_.front().cv.Signal();
  }
}

void ThreadPool::InternalPut(
    absl_nonnull absl::AnyInvocable<void() &&>&& callback) {
  // Schedule the callback to be run in our queue with a copy of the current
  // caller's context which is essential for distributed execution, tracing,
  // census accounting and security in google3 applications.
  queue_.push_back(std::make_unique<Entry>(
      base::Context(base::Context::kThread), std::move(callback)));
  queue_size_.fetch_add(1, std::memory_order_relaxed);
  SignalWaiter();
}

std::unique_ptr<ThreadPool::Entry> ThreadPool::DequeueWork() {
  thread::WaitStateScope scope(
      thread::WaitStateScope::WaitState::kWaitingForWork);
  // Wait for queue to be not-empty
  absl::MutexLock m(mutex_);
  while (queue_.empty() && !stopping_) {
    Waiter self;
    waiters_.push_front(&self);
    self.cv.Wait(&mutex_);
    waiters_.erase(&self);
  }
  if (queue_.empty()) {
    DCHECK(stopping_);
    return nullptr;
  }
  absl_nonnull std::unique_ptr<Entry> entry = std::move(queue_.front());
  queue_.pop_front();
  queue_size_.fetch_sub(1, std::memory_order_relaxed);

  // Be careful: have to signal every time we remove an element,
  // or do something more complicated with broadcasts.
  if (IsLimitedCapacity()) {
    wait_nonfull_.Signal();
  }

  if (!queue_.empty()) SignalWaiter();
  return entry;
}
