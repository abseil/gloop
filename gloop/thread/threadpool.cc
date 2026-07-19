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
#include "absl/log/die_if_null.h"
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

ABSL_FLAG(
    bool, thread_pool_gc_workers, true,
    "If false, ThreadPool will use the historical default behavior of "
    "never cleaning up threads. If true, ThreadPool will use the new behavior "
    "of cleaning up inactive workers.\n\n"
    "NOTE: This flag will be REMOVED in the near future, it is only intended "
    "for quick production fixes. If you determine that you need to retain this "
    "behavior long term, set force_eager_thread_creation on ThreadPool "
    "construction.");

AbstractThreadPool::~AbstractThreadPool() {}

// Run callback->Run().  To permit a callback to be wrapped in another.
static void RunCallback(WatchdogCallback callback, WatchDog* watchdog) {
  (*callback)(watchdog);
}

void ThreadPool::ThreadPoolWorker::Run() {
  {
    absl::MutexLock lock(pool_->mutex_);
    ++pool_->running_threads_;
  }
  pool_->RunWorker();
  Thread* dead_thread;
  {
    absl::MutexLock lock(pool_->mutex_);
    --pool_->exiting_threads_;
    --pool_->running_threads_;
    --pool_->num_threads_;
    dead_thread = std::exchange(pool_->dead_thread_, this);
  }
  if (dead_thread) {
    dead_thread->Join();
    delete dead_thread;
  }
}

void ThreadPool::RunWorker() {
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
      // Spawn threads on an as-needed basis and clean them up when they are
      // idle for a long time.
      gc_workers_(!options.force_eager_thread_creation &&
                  absl::GetFlag(FLAGS_thread_pool_gc_workers)),
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

  // Spawn threads on an as-needed basis, unless eager thread creation is
  // requested.
  if (eager_thread_creation_) {
    absl::MutexLock lock(mutex_);
    for (size_t i = 0; i < max_threads_; ++i) {
      SpawnThread();
    }
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
    // Wait until all threads are about to exit.
    auto no_threads = [this]() ABSL_SHARED_LOCKS_REQUIRED(mutex_) {
      return num_threads_ == 0;
    };
    mutex_.Await(absl::Condition(&no_threads));
  }
  // Join the last worker thread. Because all threads are guaranteed to be
  // joined, we can safely delete subcontainer_.
  if (dead_thread_) {
    dead_thread_->Join();
  }
  delete dead_thread_;

  delete subcontainer_;
}

ThreadPool::ThreadPoolWorker::ThreadPoolWorker(ThreadPool* pool)
    : Thread(pool->thread_options_, pool->name_prefix_), pool_(pool) {
  SetInitialCpuSubContainer(pool->subcontainer_);
  Start();
}

void ThreadPool::SpawnThread() {
  CHECK_LT(num_threads_ - exiting_threads_, max_threads_);
  ++num_threads_;
  (void)new ThreadPoolWorker(this);
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
    while (queue_.size() >= capacity_) {
      wait_nonfull_.Wait(&mutex_);
    }
  }
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
    bool no_spawning_threads = running_threads_ == num_threads_;
    bool can_spawn_thread = num_threads_ - exiting_threads_ < max_threads_;
    // If there are no waiters and no actively spawning threads, try spawning a
    // new thread to pick up work.
    if (no_spawning_threads && can_spawn_thread) {
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
  // The maximum amount of time a thread can be idle before it is cleaned up.
  // Matches the equivalent value in FiberThreadPool.
  static constexpr absl::Duration kMaxIdleTime = absl::Minutes(1);
  absl::Duration wait_time =
      gc_workers_ ? kMaxIdleTime : absl::InfiniteDuration();
  while (queue_.empty() && !stopping_) {
    Waiter self;
    waiters_.push_front(&self);
    bool timed_out = self.cv.WaitWithTimeout(&mutex_, wait_time);
    waiters_.erase(&self);
    if (timed_out) {
      // The thread has been idle for too long, it should exit.
      break;
    }
  }
  if (queue_.empty()) {
    // Ensure that any racing attempt to enqueue work will spawn a new thread to
    // do so if needed.
    ++exiting_threads_;
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
