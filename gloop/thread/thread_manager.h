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

#ifndef THIRD_PARTY_GLOOP_THREAD_THREAD_MANAGER_H_
#define THIRD_PARTY_GLOOP_THREAD_THREAD_MANAGER_H_
// ThreadManager maintains a collection of threads; work may be given to
// the threads by different queues (which are Executors).   The queues may
// be configured to limit the threads that will be applied to the queue's
// work, the length of the queue, and the time the closures on the queue
// may run for.
//
// ThreadManager creates threads as needed to meet demand; there is no hard
// upper bound except for bound set on particular queues.  It delays thread
// creation more when many threads have been created.  It deletes threads only
// when they have been left idle for several seconds.
// Closures presented to the threads will be run as soon as possible.  Closures
// may run in any order; there is no attempt to maintain FIFO ordering.

// The default stack size for ThreadManager threads is 64k.  You are strongly
// encouraged to make your stacks fit within that limit.  ThreadManager is
// intended to be a default choice for clients that wish not to have to tune
// threadpools.  If you wish to tune your threadpools, use another
// implementation.

// For production monitoring of ThreadManager, see:
//   //gloop/thread/thread_manager_streamz.cc

// EXPECTED USE:
//  Most programs can use just one ThreadManager, the default one returned
//  by thread::DefaultManager().
//  A program that uses multiple kernel priorities might use
//  a ThreadManager for each priority.
//
//  For closures that will run for bounded time and arrive at bounded rate,
//  simply Schedule() the closures on a default ManagedQueue, which you might
//  obtain using:
//    thread::ManagedQueue *q = thread::DefaultQueue();
//  For example, if a closure performs an RPC that will either run in 10ms or
//  time out in 30s, simply Schedule() it on *q.
//
//  For closures that may take unbounded time, such as those that will hang
//  forever if they attempt to contact a service that is down (such as GFS
//  calls), you should add them via a queue with bounded limits.
//  For example:
//    thread::ManagedQueueOptions q_options;
//    q_options.thread_limit = 8;
//    q_options.queue_limit = 100;
//    thread::ManagedQueue *gfs_q = thread_manager->NewQueue("GFS", q_options);
//  You might also use a bounded-limit queue when an outside entity (perhaps
//  malicious) may generate an unbounded number of closures of a particular
//  type.  For example, you might use a bounded queue for each active user, or
//  for each active IP subnet.
//
// To avoid deadlock, it is important never to queue closure that may block
// waiting (perhaps transitively) for another closure on the same queue if the
// number of threads that may service the queue is bounded.

// Classes in this file (all in thread namespace):
// class ThreadManager
//    Manages a collection of threads that do work presented on
//    ManagedQueue executors.
// struct ManagerOptions
//    Creation options for ThreadManager.
// class ManagedQueue
//    An executor with limits on threads, queue length and work runtime.  Feeds
//    a ThreadManager.
// struct ManagedQueueOptions
//    Creation options for ManagedQueue.

#include <climits>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "gloop/thread/executor.h"
#include "gloop/thread/thread_options.h"
#include "gloop/thread/watchdog.h"
#include "gtest/gtest_prod.h"

namespace ipc::pubsub2::server {
class ThreadManagerOptions;
}  // namespace ipc::pubsub2::server

namespace thread {
class ThreadManagerPolicy;   // see thread_manager_policy.h
class ManagedQueue;          // forward declaration
struct ManagedQueueOptions;  // forward declaration
class ThreadManager;         // forward declaration

ThreadManager* DefaultManager();  // Return a pointer to the default
                                  // ThreadManager. This pointer may not be
                                  // deleted.

ManagedQueue* DefaultQueue();  // Return a pointer to the default queue on
                               // the default ThreadManager. This pointer
                               // may not be deleted.

// options to the ThreadManager constructor
struct ManagerOptions {
  ManagerOptions();                // constructor generates default values
  thread::Options thread_options;  // thread creation options
                                   // see //thread/options.h
  // Expert clients may set n_pools to control the number of internal pools
  // to use.  It will be rounded up to a power of two.  Each pool has its own
  // locking, so having more pools reduces lock contention; the number of
  // pools has no affect on specified queueing semantics.  Most users
  // should leave this at its default setting of 0, which means "pick a
  // reasonable value based on the number of CPUs available".
  ABSL_DEPRECATED(
      "n_pools is almost never set in google3. Remaining callers are being "
      "removed to remove this option.")
  int n_pools;
  // Expert clients may set "policy" to control thread-creation policy; see
  // thread_manager_policy.h.  Most users should use the default: 0.
  // The ThreadManager destructor will "delete policy".
  ThreadManagerPolicy* policy;

  WatchdogCallback get_watchdog_callback() const { return watchdog_callback; }

 private:
  // If specified, the callback to use when WatchDog detects a thread that is
  // not making progress, as determined by `time_limit_s` (see
  // ManagedQueueOptions below). The callback must satisfy the requirements
  // specified by WatchDog::SetCallback, and the ThreadManager takes ownership
  // of this callback.
  WatchdogCallback watchdog_callback;

  // TODO: Decide whether to make custom watchdog callbacks part of
  // the public API after initial data have been gathered. The watchdog callback
  // is currently a visibility-restricted API.
  friend class ipc::pubsub2::server::ThreadManagerOptions;

  FRIEND_TEST(ThreadManagerWatchdogTest, UsesCustomWatchDogCallback);
};

struct ManagedQueueStats {
  std::string queue_name;

  // Note each of the metrics below are captured at very nearby points in
  // time, but not instantaneously, thus there may be small inconsistencies
  // across metrics.
  int queue_running;
  int num_pending_closures;

  // potentially others, like add_after_count, queue_waiters
};

class ThreadManager {
 public:
  ThreadManager(absl::string_view thread_name_prefix,
                const ManagerOptions& options);

  // This type is neither copyable nor movable.
  ThreadManager(const ThreadManager&) = delete;
  ThreadManager& operator=(const ThreadManager&) = delete;

  ~ThreadManager();  // The destructor blocks until all outstanding Queues
                     // have been deleted and the work associated with them
                     // has completed.

  // Return a pointer to a named work queue serviced by this ThreadManager with
  // limits given by queue_options.  Repeated calls to NewQueue() with the same
  // name will provide distinct queues.  Queues should be discarded with
  // "delete" when no longer needed; delete will return immediately, but the
  // underlying data structures will be discarded when all pending work is
  // complete.
  ManagedQueue* NewQueue(absl::string_view name,
                         const ManagedQueueOptions& queue_options);

  // Set the num_cpus argument to DefaultThreadManagerPolicy() (see
  // thread_manager_policy.h) used to create any ThreadManager whose policy is
  // left defaulted, including the default ThreadManager.  Should be called
  // before calls to DefaultManager(), DefaultQueue(), and the ThreadManager
  // constructor.  If not called, NumCPUs() is used (from base/sysinfo.h).
  static void SetDefaultNumCPUs(int (*num_cpus)());

  // Return stats for all ManagedQueues, for monitoring (eg. streamz).
  static std::vector<ManagedQueueStats> QueueStats();

  // Implementation details follow.
  struct Rep;

 private:
  Rep* rep_;
};

// -----------------------------------------------------------------------
// A ManagedQueue is an Executor feeding a pool of threads managed by
// ThreadManager.

struct ManagedQueueOptions {   // options to NewQueue()
  int thread_limit = INT_MAX;  // Max concurrent threads to run closures from.
  int queue_limit = INT_MAX;   // Max closures to be on queue before Schedule()
                               // blocks.
  int time_limit_s = INT_MAX;  // Max interval in seconds for a closure to run
                               // if a closure runs longer, the entire process
                               // aborts.
};

// ManagedQueue is an executor that separates shutdown logic from destruction.
// It uses backing threads from ThreadManager for execution.
class ManagedQueue : public thread::Executor {
 public:
  // does not wait until work is complete; see WaitUntilComplete() below.
  ~ManagedQueue() override;

  virtual std::string name() const = 0;
  virtual ManagedQueueOptions queue_options() const = 0;

  // Wait until all work currently associated with this queue (including work
  // passed to ScheduleAt()) is complete.  If new work is added during the call,
  // it is unspecified whether the call waits for the new work to complete.
  virtual void WaitUntilComplete() = 0;

  // Return stats for the ManagedQueue. Stats may be slightly inconsistent and
  // should just be used for status pages and monitoring.
  virtual ManagedQueueStats Stats() const = 0;

  // for testing
  virtual ManagedQueue* current_executor_for_testing() const = 0;
};

}  // namespace thread
#endif  // THIRD_PARTY_GLOOP_THREAD_THREAD_MANAGER_H_
