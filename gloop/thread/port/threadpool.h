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

#ifndef THIRD_PARTY_GLOOP_THREAD_PORT_THREADPOOL_H_
#define THIRD_PARTY_GLOOP_THREAD_PORT_THREADPOOL_H_

#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/base/thread_annotations.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "gloop/base/callback.h"

// An implementation of google3 ThreadPool using POSIX threads (pthreads).
// The implementation is *not* feature-parity. It only provides a subset of
// the functionality of the google3 implementation.
//
// - The pool allocates a fixed number of worker threads on instantiation.
// - The worker threads will pick up work jobs as they arrive.
// - If all workers are busy, work jobs are queued for later execution.
//
// The thread pool is shut down when the pool is destroyed.
//
// Example usage of the thread pool:
//   {
//     ThreadPool pool(4);
//     for (int i = 0; i < 100; ++i) {  // Dispatch 100 jobs.
//       pool.Add(NewCallback(MyFunction, my_data));
//     }
//   } // ThreadPool gets destroyed only when all jobs are done.
class ThreadPool {
 public:
  struct Options {
    // The name prefix to use for threads in this pool.
    std::string name_prefix;
  };

  // Creates the thread pool with the specified number of worker threads.
  // If num_threads is 1, the closures are run in FIFO order.
  explicit ThreadPool(int num_threads) : ThreadPool(num_threads, Options{}) {}

  ABSL_DEPRECATE_AND_INLINE()
  ThreadPool(absl::string_view name_prefix, int num_threads)
      : ThreadPool(num_threads, Options{std::string(name_prefix)}) {}

  ThreadPool(int num_threads, Options options);

  // The destructor will shut down the thread pool and all jobs are executed.
  // Note that after shutdown, the thread pool does not accept further jobs.
  ~ThreadPool();

  ABSL_DEPRECATED("Not needed, the constructor now starts workers.")
  void StartWorkers() {}

  // Adds the specified "closure" to the queue for processing. If worker threads
  // are available, "closure" will run immediately. Otherwise "closure" is
  // queued for later execution.
  void Schedule(std::function<void()> closure);

  // Like Add and Schedule except if the closure is not able to run immediately
  // because the number of waiting threads is not greater than the
  // number of outstanding jobs, this routine does not enqueue the
  // closure and instead returns false.
  // NOTE: The threads created by StartWorkers() may not be immediately
  // available; subsequent calls to these functions may still return false for
  // some (very short) period of time.
  bool ScheduleIfReadyToRun(std::function<void()> closure);

  // Set the name prefix used for worker threads. Cannot be called after
  // StartWorkers.
  ABSL_DEPRECATED("Use the constructor instead.")
  void SetThreadNamePrefix(absl::string_view name_prefix);

  // Return an estimate of the number of queued callbacks awaiting execution.
  int queue_count();

  // AddAt() and ScheduleAt() are missing.
  // Intended primarily for tests, and even then as a last resort.
#ifdef ABSL_THREAD_POOL_SCHEDULE_AT_MISSING
#error ABSL_THREAD_POOL_SCHEDULE_AT_MISSING cannot be directly set
#else
#define ABSL_THREAD_POOL_SCHEDULE_AT_MISSING 1
#endif

// SetFIFOScheduling() is missing.
// Intended primarily for tests, and even then as a last resort.
#ifdef ABSL_THREAD_POOL_SET_FIFO_SCHEDULING_MISSING
#error ABSL_THREAD_POOL_SET_FIFO_SCHEDULING_MISSING cannot be directly set
#else
#define ABSL_THREAD_POOL_SET_FIFO_SCHEDULING_MISSING 1
#endif

  // Provided for debugging and testing only.
  int num_threads() const;

 private:
  class WorkerThread;
  void WorkerFunction();

  // Shuts down the thread pool, i.e. worker threads finish their work and
  // pick up new jobs until the queue is empty. This call will block until
  // the shutdown is complete.
  //
  // Note: If a worker encounters an empty queue after this call, it will exit.
  // Other workers might still be running, and if the queue fills up again, the
  // thread pool will continue to operate with a decreased number of workers.
  // It is up to the caller to prevent adding new jobs.
  void Shutdown();

  absl::CondVar condition_;
  absl::Mutex queue_mutex_;

  std::deque<std::function<void()>> queue_ ABSL_GUARDED_BY(queue_mutex_);
  // Number of workers that are currently processing a job.
  int num_executing_threads_ ABSL_GUARDED_BY(queue_mutex_) = 0;
  std::vector<WorkerThread*> pool_;

  bool exit_threads_ ABSL_GUARDED_BY(queue_mutex_) = false;
  int num_threads_ = 0;
  std::string name_prefix_;
};

#endif  // THIRD_PARTY_GLOOP_THREAD_PORT_THREADPOOL_H_
