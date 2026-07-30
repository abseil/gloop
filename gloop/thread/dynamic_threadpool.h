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

// DynamicThreadPool is an implementation of a ThreadPool which can be
// configured to have at least a fixed minimum number of workers and can grow
// dynamically to have atmost a fixed maximum number of workers.  The maximum
// limit for the number of threads can also be modified on the fly.
//
// NOTE:
// See thread/thread_manager.h for a more generic implementation of dynamic
// threadpools. New clients should prefer to use that module over
// dynamic_threadpool.  If there is a compelling reason to use
// dynamic_threadpool, please email m3b and sanjay so we can see if there are
// changes that should be made to thread_manager.

#ifndef THIRD_PARTY_GLOOP_THREAD_DYNAMIC_THREADPOOL_H_
#define THIRD_PARTY_GLOOP_THREAD_DYNAMIC_THREADPOOL_H_

#include <cstddef>
#include <ctime>
#include <deque>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "gloop/base/callback.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"
#include "gloop/thread/threadpool.h"

class DynamicThreadPoolWorker;

class DynamicThreadPool : public AbstractThreadPool {
  friend class DynamicThreadPoolWorker;

 public:
  // queue_capacity is guaranteed to be INT_MAX by default. The values of the
  // other fields are reasonable implementation-specific defaults.
  struct Options {
    thread::Options thread_options;

    int queue_capacity;
    int min_threads;
    int max_threads;
    int max_idle_ms;

    Options();
  };

  // Create a thread pool that contains at least "min_threads" threads, has the
  // specified producer-consumer queue capacity, and may grow up to
  // "max_threads" if necessary.
  //
  // When adding a closure:
  // 1. if there are any idle threads, one of these idle threads will run
  //    the closure
  // 2. otherwise, if the current number of threads is less than max_threads,
  //    create a new thread to run the closure
  // 3. otherwise, if there is room in the queue, add the closure to the queue
  // 4. otherwise, block until there is room in the queue.
  //
  // If max_idle_ms >= 0, then idle threads will exit if above min_threads and
  // they've been idle for longer than "max_idle_ms" milliseconds.
  DynamicThreadPool(absl::string_view thread_name_prefix,
                    const Options& options);

  // DEPRECATED: use the first constructor, which takes an Options class.
  DynamicThreadPool(int queue_capacity, int min_threads, int max_threads,
                    int max_idle_ms);

  // This type is neither copyable nor movable.
  DynamicThreadPool(const DynamicThreadPool&) = delete;
  DynamicThreadPool& operator=(const DynamicThreadPool&) = delete;

  ~DynamicThreadPool() override;

  // Implementations of inherited methods;
  void Schedule(absl::AnyInvocable<void() &&> callback) override;
  // Never blocks or fails. If we are at the thread limit and the queue is
  // full, adds the Closure to it, anyway.
  void ScheduleAt(absl::Time when,
                  absl::AnyInvocable<void() &&> callback) override;

  bool TrySchedule(absl::AnyInvocable<void() &&> callback) override;
  bool ScheduleIfReadyToRun(absl::AnyInvocable<void() &&> callback) override;
  int queue_count() const override;
  int queue_capacity() const override;

  // The following are provided for debugging only
  std::string thread_name_prefix() const override {
    return thread_name_prefix_;
  }

  int num_threads() const;
  Thread* thread(int i) const;

  // Call this to shut down the thread pool. It will not return until
  // all threads have run to completion.
  void ShutDown();

  // Increase the maximum thread limit. If there's a task in the queue,
  // start up a new thread.
  void IncrementMaxThreads();

  // Decrease the maximum thread limit if that would not cause the
  // limit to fall below the minimum thread limit. This latter case is
  // typically due to a programming error and the return value of this
  // function should be CHECKed by the caller.
  // Returns: true on success, (thread limit decremented and >= min)
  //          false on failure (limit already <= min)
  bool DecrementMaxThreads();

 private:
  typedef std::list<DynamicThreadPoolWorker*> ThreadList;
  typedef std::vector<DynamicThreadPoolWorker*> ThreadVec;

  // Internal, delegated constructor.
  //
  // TODO: the body of this function can be folded into the
  // public non-deprecated constructor, and this function removed, if the
  // deprecated constructor is ever removed.
  DynamicThreadPool(absl::string_view thread_name_prefix,
                    const thread::Options& thread_options, int queue_capacity,
                    int min_threads, int max_threads, int max_idle_ms);

  // Create a new thread- called either from the constructor, or with mutex_
  // locked
  DynamicThreadPoolWorker* AddThread() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Join and delete all threads in this vector
  void JoinThreads(ThreadVec* join_threads) ABSL_LOCKS_EXCLUDED(mutex_);

  // Internal consistency check - called with mutex_ locked
  void Check() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Returns whether we can run or queue a Closure without forcibly queueing
  // it.
  bool ReadyToRunOrQueue() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  enum Caller { kAdd, kTryAdd, kAddIfReadyToRun, kAddAfter };

  // Add a Closure (helper for all add routines).
  bool AddInternal(Closure* closure, Caller caller) ABSL_LOCKS_EXCLUDED(mutex_);

  // Helper for AddAfter.
  void AddAfterInternal(Closure* closure) ABSL_LOCKS_EXCLUDED(mutex_);

  // Reap (join all exited threads) - called with mutex_ locked
  void Reap() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Dequeue a closure for this worker
  Closure* Dequeue(DynamicThreadPoolWorker* thread) ABSL_LOCKS_EXCLUDED(mutex_);

  // Reap (join all exited threads) when the number of exited threads grows
  // above kReapFactor * min_threads
  static const int kReapFactor;

  mutable absl::Mutex mutex_;
  std::unique_ptr<std::deque<Closure*> > queue_;
  const int queue_capacity_;                   // The capacity of the queue
  std::unique_ptr<ThreadList> threads_;        // Active (running/idle) threads
  std::unique_ptr<ThreadList> idle_threads_;   // Currently idle threads
  std::unique_ptr<ThreadVec> exited_threads_;  // Threads to join
  absl::Notification ended_;                   // Have threads been joined?
  const int min_threads_;
  int max_threads_;
  const int max_idle_ms_;
  thread::Options thread_options_;
  const std::string thread_name_prefix_;
};

#endif  // THIRD_PARTY_GLOOP_THREAD_DYNAMIC_THREADPOOL_H_
