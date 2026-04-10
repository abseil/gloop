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

// This defines the threadpool backing Fibers. It is used in both the
// cooperative and non-cooperative case. The thread pool maintains a list of
// idle threads and a list of active threads. The idle list is used to recycle
// threads when new work is available. Helper functions are exposed to make
// managing threads easier. Unlike most thread pools, this one does not provide
// the ability to post work, rather it is the responibility of the user to have
// a method to assign work to threads -- the thread pool merely manages lists of
// threads per stack size and prunes them.

#ifndef THIRD_PARTY_GLOOP_THREAD_FIBER_INTERNAL_FIBER_THREAD_POOL_H_
#define THIRD_PARTY_GLOOP_THREAD_FIBER_INTERNAL_FIBER_THREAD_POOL_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "gloop/base/scheduling/scheduler.h"
#include "gloop/base/scheduling/scheduling_mode.h"
#include "gloop/base/spinlock.h"
#include "gloop/thread/fiber/channel.h"
#include "gloop/thread/fiber/internal/fiber-thread-options.h"
#include "gloop/thread/fiber/scheduler-types.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"
#include "gloop/util/gtl/intrusive_list.h"

namespace thread {
namespace internal {

// There are various defaults that are set based on if we are compiling for a
// mobile target or not, to avoid repeating the same #ifdef guards, define a
// constant bool.
#if defined(__APPLE__) || defined(__ANDROID__)
static const bool kMobile = true;
#else
static const bool kMobile = false;
#endif

// Round up to nearest power of 2, drop bottom 12 bits, take log2
int StackSizeToStackSizeClass(size_t stack_size);
size_t StackSizeClassToStackSize(int stack_class);

// Create the thread::Options to use when a fiber domain is creating a new
// thread, honoring any stack size configured by the user either in
// FiberOptions or via the fibers_default_thread_stack_size command line flag.
thread::Options ThreadOptionsForSchedulable(
    base::scheduling::Schedulable* schedulable, int default_stack_size);
}  // namespace internal

class CommonFiberThread;
class CommonFiberDomain;

// CommonFiberThreadPool maintains a list of idle threads and active threads per
// stack size bucket.
class CommonFiberThreadPool {
 public:
  explicit CommonFiberThreadPool(absl::string_view name);

  ~CommonFiberThreadPool();

  struct Stats {
    // We want a unique key within each Stat struct as they are returned as a
    // list. We can use the address of the thread pool as a unique ID.
    uintptr_t id;
    absl::string_view name;
    int num_active_threads;
    int num_idle_threads;
    uint64_t reaper_run_count;
  };

  static std::vector<Stats> GetStats();

  // Called when a new thread is introduced.  In the cooperative case, must be
  // called after destination_target_ has been assigned, before any cooperative
  // execution begins.
  void AddNewActiveThread(CommonFiberThread* thr);

  // Tries to fetch a thread from the idle list, if one is returned it has
  // already been moved to the active list.
  CommonFiberThread* TryGetIdleThread(int stack_size_class);

  // When true is returned, "thr" has moved from active to idle list.
  // Otherwise, the thread is still active and must terminate.
  // REQUIRES: "thr" must be on the active list.
  bool TryAddIdleThread(CommonFiberThread* thr);

  // Called immediately before a thread is released, a thread may only be freed
  // from the active list.
  // REQUIRES: "thr" must be on the active list.
  void RemoveActiveThread(CommonFiberThread* thr);

 private:
  // We partition threads managed by this pool (used by domains to back
  // cooperative entities such as Fibers) into two lists: active and idle.  The
  // idle list consists of those threads not currently executing work (in the
  // cooperative case -- WaitForNextWorkItem()), the active list contains all
  // other outstanding threads.

  // Blocks until all threads on the idle list are moved to the active list and
  // have Die() called on them.
  void WaitForThreads();

  // Reaps idle threads if we have too many. Returns how long we should wait
  // before reaping again. Returns an infinite duration if we think we can turn
  // off the reaper until we have a sufficient number of idle threads. Up to the
  // coordinator to coordinate recurring calls and cooldowns.
  absl::Duration PeriodicReleaseIdleThreads()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  void PeriodicReleaseThreadBody();

  static const int kMinThreadsForRelease = 10;

  struct ThreadList {
    int num_active = 0;
    int num_idle = 0;

    // We maintain two thread lists, idle and active. Threads in the idle list
    // mean they are ready to consume more work.
    gtl::intrusive_list<CommonFiberThread> idle_threads;
    gtl::intrusive_list<CommonFiberThread> active_threads;
  };

  // Add a thread to the active list and remove it from the idle list.
  // REQUIRES: "thr" be in the idle list.
  void MarkActive(ThreadList& thread_list, CommonFiberThread& thr)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  // Add a thread to the idle list and remove it from the active list.
  // REQUIRES: "thr" be in the active list.
  void MarkIdle(ThreadList& thread_list, CommonFiberThread& thr)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  // There are only a few possible stack classes, so we use a direct map instead
  // of another resizable data structure. lock_ must be held when accessing
  // threads_ or its underlying lists.
  std::array<ThreadList,
             internal::kMaxStackSizeLog2 - internal::kMinStackSizeLog2 + 1>
      threads_ ABSL_GUARDED_BY(lock_);

  // Protects all outstanding threads, both active and idle.
  mutable ::SpinLock lock_{absl::base_internal::SCHEDULE_KERNEL_ONLY};

  int num_active_threads_ ABSL_GUARDED_BY(lock_){0};
  int num_idle_threads_ ABSL_GUARDED_BY(lock_){0};

  // We are interested in testing the behavior of our reaper. Specifically, we
  // want to keep track of how many times it is executed, which corresponds to
  // how many wakeups happen. To do so, we can export a run count. I don't worry
  // about this overflowing because even executing once every 10 milliseconds,
  // it will take 58494241 years to reach 2^64.
  uint64_t reaper_run_count_ ABSL_GUARDED_BY(lock_){0};

  // Two variables need to be modified: the global active thread counter & the
  // stack class active thread counter.
  inline void num_active_threads_inc_locked(int val, ThreadList& thread_list)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
    thread_list.num_active += val;
    num_active_threads_ += val;
  }

  // Two variables need to be modified: the global idlethread counter & the
  // stack class idle thread counter.
  inline void num_idle_threads_inc_locked(int val, ThreadList& thread_list)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) {
    thread_list.num_idle += val;
    num_idle_threads_ += val;
  }

  bool shutdown_ ABSL_GUARDED_BY(lock_){false};
  bool periodic_release_thread_running_ ABSL_GUARDED_BY(lock_){false};
  const std::string name_;

  // CommonFiberDomainThread is self-managing and "owns itself" in the sense
  // that it manages its relationship with the pool. Friendship is not
  // inherited, so we need to explicitly list every subclass.
  friend class CommonFiberDomainThread;
  friend class CommonFiberDomain;

  // Returns statistics for this thread pool.
  struct Stats Stats() const;

  // Signal from the destructor for the reaper to shutdown.
  absl::Notification reaper_shutdown_;
  // Signal from the reaper to indicate that it has finished.
  absl::Notification last_reaper_finished_;
};

class CommonFiberThread : public Thread,
                          public gtl::intrusive_link<CommonFiberThread> {
 public:
  CommonFiberThread(thread::Options opts, absl::string_view name);

 private:
  int StackSizeClass() const {
    return internal::StackSizeToStackSizeClass(options().stack_size());
  }

  // Returns the time at which this thread is too elderly
  // and should be allowed to die.  Only valid if on the freelist.
  // Must hold the pool's lock to read.
  // REQUIRES: Externally synchronized, may only be called by
  // CommonFiberThreadPool.
  absl::Time expiry() const { return expiry_; }

  // REQUIRES: Externally synchronized, may only be called by
  // CommonFiberThreadPool.
  void SetExpiry(absl::Time expiry) { expiry_ = expiry; }

  virtual void Exit() = 0;

  absl::Time expiry_;

  friend class CommonFiberThreadPool;
  friend class CommonFiberDomain;
};

}  // namespace thread
#endif  // THIRD_PARTY_GLOOP_THREAD_FIBER_INTERNAL_FIBER_THREAD_POOL_H_
