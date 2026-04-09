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

// A thread pool consists of a set of threads that sit around waiting
// for closures to appear on a queue.  When that happens, one of them
// pulls the closure off the queue and runs it.
//
// Sample usage:
//
// {
//   ThreadPool pool(num_workers, {.name_prefix="testpool"});
//   for (int i = 0; i < N; ++i) {
//     pool.Schedule([i]() { DoWork(i); });
//   }
// }
//

#ifndef THIRD_PARTY_GLOOP_THREAD_THREADPOOL_H_
#define THIRD_PARTY_GLOOP_THREAD_THREADPOOL_H_

#include "absl/base/attributes.h"
#include "gloop/thread/config.h"  // IWYU pragma: keep
#include "gtest/gtest_prod.h"

// When building some platforms, this file is effectively replaced by its analog
// in port/.
#if THREAD_HAVE_ALTERNATE_THREAD_POOL

#include "gloop/thread/port/threadpool.h"

#else

#include <climits>
#include <cstddef>
#include <ctime>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/chunked_queue.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "gloop/base/callback.h"
#include "gloop/thread/add_after_helper.h"
#include "gloop/thread/executor.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"
#include "gloop/thread/watchdog.h"
#include "gloop/util/gtl/intrusive_list.h"

// Abstract base class for all thread pool implementations
class AbstractThreadPool : public thread::Executor {
 public:
  ~AbstractThreadPool() override;

  ///////////////////////////////////////////////////////////////////////////

  // This method is deprecated and does nothing. ThreadPool implementations are
  // started on construction.
  ABSL_DEPRECATE_AND_INLINE()
  void StartWorkers() {}

  // Like TryAdd, except if the callback is not able to run immediately because
  // the number of waiting threads is not greater than the number of outstanding
  // jobs, this routine does not enqueue the callback and instead returns false.
#ifndef SWIG
  virtual bool ScheduleIfReadyToRun(absl::AnyInvocable<void() &&> callback) = 0;
#endif

  // Return an estimate of the number of queued callbacks awaiting execution
  // Same as queue_count(), below
  int num_pending_closures() const override { return queue_count(); }

  // Accessors for the thread pool's work queue

  // Number of elements in queue.  The returned count may not be valid for very
  // long since other threads may be concurrently adding/removing elements
  // to/from the queue.  So use the return value as just a hint about the size
  // of the queue.
  virtual int queue_count() const = 0;

  // Maximum number of elements in the queue
  virtual int queue_capacity() const = 0;

  virtual std::string thread_name_prefix() const = 0;
};

// This is the basic concrete implementation of an AbstractThreadPool.
// See also: <link>Main/GdhThreadpools
class ThreadPool : public AbstractThreadPool {
 public:
  struct Options {
    // The name prefix to use for threads in this pool.
    std::string name_prefix;
    // The thread options to use for threads in this pool.
    thread::Options thread_options =
        thread::Options().set_stack_size(kDefaultStackBytes);
    // The capacity of the queue of unscheduled closures. Once this fills up,
    // calls to Schedule() will block and calls to TrySchedule() will return
    // false.
    //
    // REQUIRES: queue_capacity > 0
    int queue_capacity = INT_MAX;
    // The timeout of the watchdog for each thread.
    absl::Duration watchdog_timeout = absl::InfiniteDuration();
    // The callback to call when the watchdog expires, if set.
    WatchdogCallback absl_nullable watchdog_callback = nullptr;
    // When set, always spawn all workers on construction instead of lazily
    // creating when they are needed.
    bool force_eager_thread_creation = false;
  };

  // Create a thread pool that provides a concurrency of "num_threads" threads.
  // I.e., if "num_threads" items are added, they are all guaranteed to run
  // concurrently without excessive delay. If num_threads is 1, the closures are
  // run in FIFO order.
  //
  // REQUIRES: num_threads > 0
  ThreadPool(int num_threads, Options options);

  // REQUIRES: num_threads > 0
  explicit ThreadPool(int num_threads) : ThreadPool(num_threads, Options{}) {}

  ABSL_DEPRECATE_AND_INLINE()
  ThreadPool(absl::string_view name_prefix, int num_threads)
      : ThreadPool(num_threads,
                   Options{.name_prefix = std::string(name_prefix)}) {}

  ABSL_DEPRECATE_AND_INLINE()
  ThreadPool(thread::Options thread_options, absl::string_view name_prefix,
             int num_threads)
      : ThreadPool(num_threads,
                   Options{.name_prefix = std::string(name_prefix),
                           .thread_options = std::move(thread_options)}) {}

#if defined(__ANDROID__) || defined(__APPLE__)
  // Historically, in spite of the 64k constant, threads in the portable thread
  // pool were given os-default stack sizes, and now some code relies on the
  // larger stack size. This matches the default in Thread if you pass 0.
  enum { kDefaultStackBytes = 1952 * 1024 };
#else
  enum { kDefaultStackBytes = 64 * 1024 };
#endif
#ifndef SWIG
#endif

  // Waits for closures (if any) to complete.
  ~ThreadPool() override;

  // Implementations of inherited methods;
#ifndef SWIG
  void Schedule(absl::AnyInvocable<void() &&> callback) override;
  void ScheduleAt(absl::Time when,
                  absl::AnyInvocable<void() &&> callback) override;
  bool TrySchedule(absl::AnyInvocable<void() &&> callback) override;
  bool ScheduleIfReadyToRun(absl::AnyInvocable<void() &&> callback) override;
#endif
  int queue_count() const override;
  int queue_capacity() const override;
  std::string thread_name_prefix() const override;

  int num_threads() const;

#ifndef SWIG
  // This type is neither copyable nor movable.
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;
#endif

 private:
  // Waiter for a single thread. Uses intrusive_list to allow for simple and
  // efficient mid-list removal and to avoid allocations.
  struct Waiter : public gtl::intrusive_link<Waiter> {
    absl::CondVar cv;  // signalled when there is work to do
  };

  // Spawn a single new worker thread.
  //
  // REQUIRES: threads_.size() < max_threads_
  void SpawnThread() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  void RunWorker();

  // Adds "elem" to the queue.  Causes the current thread
  // to wait for consumers if the queue is full.
  void Put(absl_nonnull std::unique_ptr<Closure> closure)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // If the queue is not full, adds "elem" to the queue and returns true.
  // If the queue is full, returns false and has no side-effects.
  bool TryPut(absl_nonnull std::unique_ptr<Closure> closure)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // If there is a waiter, adds "elem" to the queue and returns true.
  // If there are no waiters, returns false and has no side-effects.
  bool PutIfReadyToRun(absl_nonnull std::unique_ptr<Closure> closure)
      ABSL_LOCKS_EXCLUDED(mutex_);

  void InternalPut(absl_nonnull std::unique_ptr<Closure> closure)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Signals a waiter if there is one, or spawns a thread to try to add a new
  // waiter.
  //
  // REQUIRES: !queue_.empty()
  void SignalWaiter() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Removes the oldest element from the queue and returns it.
  // Causes the current thread to wait for producers if the queue is empty.
  //
  // Returns nullptr if the thread pool is shutting down.
  absl_nullable std::unique_ptr<Closure> DequeueWork()
      ABSL_LOCKS_EXCLUDED(mutex_);

  bool IsLimitedCapacity() {
    return capacity_ < std::numeric_limits<int>::max();
  }

  bool Stopping() const ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock l(mutex_);
    return stopping_;
  }

  mutable absl::Mutex mutex_;  // The protecting lock
  absl::CondVar wait_nonfull_
      ABSL_GUARDED_BY(mutex_);  // To wait until non-full
  const size_t capacity_;       // Capacity of "queue"
  gtl::intrusive_list<Waiter> waiters_
      ABSL_GUARDED_BY(mutex_);  // LIFO of threads waiting
  const size_t max_threads_;    // How many threads can I have

  const absl::Duration watchdog_timeout_;  // Watchdog timeout period
  const std::string name_prefix_;          // Worker thread name prefix
  const thread::Options thread_options_;   // Standard thread options

  absl::chunked_queue<absl_nonnull std::unique_ptr<Closure> >
      queue_;                         // Queue of elements
  const bool eager_thread_creation_;  // Whether to eagerly spawn threads

  bool stopping_ ABSL_GUARDED_BY(mutex_) = false;  // Set in destructor
  thread::AddAfterHelper add_after_helper_;        // Provides safe AddAfter()
  WatchdogCallback absl_nullable const watchdog_callback_;  // Watchdog callback
  // How many threads have entered RunWorker
  size_t running_threads_ ABSL_GUARDED_BY(mutex_) = 0;
  thread::CpuSubContainer* const subcontainer_;  // Thread scheduling container
  std::vector<Thread*> threads_ ABSL_GUARDED_BY(mutex_);  // List of threads

  FRIEND_TEST(ThreadPoolTest, OptionsConstructor);
};

#endif  // !THREAD_HAVE_ALTERNATE_THREAD_POOL

#endif  // THIRD_PARTY_GLOOP_THREAD_THREADPOOL_H_
