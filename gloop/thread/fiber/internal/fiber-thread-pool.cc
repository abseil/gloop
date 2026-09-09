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

#include "gloop/thread/fiber/internal/fiber-thread-pool.h"

#include <algorithm>
#include <cstdint>
#include <list>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/flags/flag.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/spinlock.h"
#include "gloop/thread/fiber/internal/fiber-thread-options.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"
#include "gloop/thread/wait_state.h"

static constexpr int32_t kDefaultMinIdleThreads = 5;

ABSL_RETIRED_FLAG(bool, switchto_domain_ignore_min_idle_threads, true,
                  "Retired.");
ABSL_RETIRED_FLAG(int32_t, switchto_domain_min_idle_threads,
                  kDefaultMinIdleThreads, "Retired.");

ABSL_FLAG(int64_t, switchto_domain_idle_thread_timeout_ms, 60000,
          "Time an idle thread should be left cached waiting for "
          "newly spawned work.");

ABSL_FLAG(int64_t, switchto_domain_idle_thread_death_interval_ms,
          thread::internal::kMobile ? 5 * 1000 : 100,
          "Time our babysitter thread sleeps between killing aged-out "
          "threads as a rate limiter.");

ABSL_FLAG(int64_t, fiber_thread_reaper_cooldown_ms,
          thread::internal::kMobile ? 30 * 1000 : 5 * 1000,
          "If our thread reaper notices that it has not reclaimed any threads "
          "in a while, it will wait this long before destroying itself. We "
          "wait longer on mobile as a shorter cooldown period will result in "
          "more frequent wakeups. Additionally, mobile apps spawn fewer "
          "fibers in general, meaning lower frequency of thread state "
          "changes, meaning we need to wait longer to observe a "
          "steady-state.");

namespace thread {
namespace {

ABSL_CONST_INIT absl::Mutex thread_pool_list_mu_(absl::kConstInit);
std::list<CommonFiberThreadPool*>* GetThreadPoolList() {
  static std::list<CommonFiberThreadPool*>* list =
      new std::list<CommonFiberThreadPool*>();
  return list;
}

// RegisterThreadPool and DeregisterThreadPool are needed to add the thread pool
// to the global list of thread pools in the binary. This list is used to export
// statistics about thread pools.
void RegisterThreadPool(CommonFiberThreadPool* in) {
  absl::MutexLock l(thread_pool_list_mu_);
  GetThreadPoolList()->push_back(in);
}

void DeregisterThreadPool(CommonFiberThreadPool* in) {
  absl::MutexLock l(thread_pool_list_mu_);
  GetThreadPoolList()->remove(in);
}
}  // namespace

CommonFiberThread::CommonFiberThread(thread::Options opts,
                                     absl::string_view name)
    : Thread(opts, name) {}

CommonFiberThreadPool::CommonFiberThreadPool(absl::string_view name)
    : name_(name) {
  RegisterThreadPool(this);
}

void CommonFiberThreadPool::WaitForThreads() {
  {
    ::SpinLockHolder l(lock_);
    // Tell any threads entering into WaitForNextWorkItem not to bother.
    shutdown_ = true;
  }

  // We must release our freelist of threads.
  CommonFiberThread* idle_thread;
  for (int i = 0;
       i < internal::kMaxStackSizeLog2 - internal::kMinStackSizeLog2 + 1; i++) {
    while ((idle_thread = TryGetIdleThread(i))) {
      idle_thread->Exit();
    }
  }
}

void CommonFiberThreadPool::MarkActive(ThreadList& thread_list,
                                       CommonFiberThread& thr) {
  num_idle_threads_inc_locked(-1, thread_list);
  ABSL_RAW_CHECK(thread_list.num_idle >= 0, "corrupt thread_list");

  thread_list.idle_threads.erase(&thr);
  num_active_threads_inc_locked(1, thread_list);
  thread_list.active_threads.push_back(&thr);
}

void CommonFiberThreadPool::MarkIdle(ThreadList& thread_list,
                                     CommonFiberThread& thr) {
  num_active_threads_inc_locked(-1, thread_list);
  ABSL_RAW_CHECK(thread_list.num_active >= 0, "corrupt thread_list");

  thread_list.active_threads.erase(&thr);
  num_idle_threads_inc_locked(1, thread_list);
  thread_list.idle_threads.push_front(&thr);
}

bool CommonFiberThreadPool::TryAddIdleThread(CommonFiberThread* thr) {
  int stack_size_class = thr->StackSizeClass();
  {
    ::SpinLockHolder l(lock_);
    if (shutdown_) return false;
    MarkIdle(threads_[stack_size_class], *thr);

    absl::Duration timeout = absl::Milliseconds(
        absl::GetFlag(FLAGS_switchto_domain_idle_thread_timeout_ms));
    thr->SetExpiry(absl::Now() + timeout);

    // Release thread not needed or already running.
    if (num_idle_threads_ < kDefaultMinIdleThreads ||
        periodic_release_thread_running_) {
      return true;
    }
    periodic_release_thread_running_ = true;
  }
  // Spawn the reaper thread while not holding the lock.
  StartDetachedThread(absl::StrCat(name_, "-fiber_thread_reaper"),
                      [this] { PeriodicReleaseThreadBody(); });
  return true;
}

CommonFiberThread* CommonFiberThreadPool::TryGetIdleThread(
    int stack_size_class) {
  ::SpinLockHolder l(lock_);
  auto& thread_list_ = threads_[stack_size_class];
  if (thread_list_.idle_threads.empty()) return nullptr;
  CommonFiberThread* thr = &thread_list_.idle_threads.front();
  MarkActive(thread_list_, *thr);
  return thr;
}

void CommonFiberThreadPool::AddNewActiveThread(CommonFiberThread* thr) {
  int stack_size_class = thr->StackSizeClass();
  ::SpinLockHolder l(lock_);
  auto& thread_list_ = threads_[stack_size_class];
  num_active_threads_inc_locked(1, thread_list_);
  thread_list_.active_threads.push_front(thr);
}

void CommonFiberThreadPool::RemoveActiveThread(CommonFiberThread* thr) {
  int stack_size_class = thr->StackSizeClass();
  ::SpinLockHolder l(lock_);
  auto& thread_list_ = threads_[stack_size_class];
  num_active_threads_inc_locked(-1, thread_list_);
  ABSL_RAW_CHECK(thread_list_.num_active >= 0, "corrupt thread_list_");
  thread_list_.active_threads.erase(thr);
}

// We periodically expire idle threads in the cache if they go unused.  It turns
// out to be faster to coordinate this externally, rather than including a sleep
// on the original timeout.
absl::Duration CommonFiberThreadPool::PeriodicReleaseIdleThreads() {
  ++reaper_run_count_;
  static constexpr absl::Duration kIdleReleasePeriod = absl::Seconds(1);
  // By default we sleep for a reasonable time (e.g. seconds) between
  // attempts, this both assures a reasonable period to establish a watermark
  // on and minimizes overhead.
  absl::Duration delay_next = kIdleReleasePeriod;
  bool reclaim_happened = false;

  // Reclaim threads with exponential decay if they've been idle a long
  // time and there is a buffer, reclaiming RAM from the thread stacks.
  // We do this for both default and big stack threads IF big stack
  // threads are enabled. We pull this into a closure so that we don't
  // repeat the code for both stack lists.
  for (int stack_size_class = 0;
       stack_size_class <
       internal::kMaxStackSizeLog2 - internal::kMinStackSizeLog2 + 1;
       stack_size_class++) {
    // Reclaim threads with exponential decay if they've been idle a long
    // time and there is a buffer, reclaiming RAM from the thread stacks.
    // We do this for both default and big stack threads IF big stack
    // threads are enabled. We pull this into a closure so that we don't
    // repeat the code for both stack lists.
    auto& thread_list_ = threads_[stack_size_class];
    static const float kReclaimRatio = 0.001;
    int num_threads_target =
        std::max(static_cast<int>(thread_list_.num_idle * (1 - kReclaimRatio)),
                 kDefaultMinIdleThreads);
    while (thread_list_.num_idle > num_threads_target) {
      // The reaper will quit once it has observed that there are fewer
      // idle threads than `kDefaultMinIdleThreads`, twice in a
      // row.
      //
      // num_threads_target >= kDefaultMinIdleThreads, which
      // implies that if the conditional in the above while loop is true,
      // that we are seeing more idle threads than the minimum. Since this
      // is the case, we set reclaim_happened to true so that we will run
      // the reaper again & not enter cooldown mode. If the condition is
      // not true, reclaim_happened will be false, and the reaper will
      // enter cooldown mode, where if it once again sees
      // !reclaim_happened it will despawn and we will enter the steady
      // state.
      //
      // This means that the steady state will be num(idle_threads) <=
      // kDefaultMinIdleThreads and the reaper not running.
      reclaim_happened = true;
      // Technically, we don't sort the freelist by age.  But we
      // serialize additions to it and compute expiries underneath the
      // lock.  So the back thread should always be oldest up to clock
      // skew between processors-- small enough that I don't worry
      // about missing a too-old thread.
      auto* candidate = &thread_list_.idle_threads.back();
      if (absl::Now() >= candidate->expiry()) {
        MarkActive(thread_list_, *candidate);
        candidate->Exit();

        // Once we've started freeing idle threads, we schedule more
        // aggressively in case there are many to be freed. But not so
        // frequently that we're likely to cause large latency spikes.
        delay_next = absl::Milliseconds(
            absl::GetFlag(FLAGS_switchto_domain_idle_thread_death_interval_ms));
      } else {
        break;
      }
    }
  }

  if (reclaim_happened) {
    return delay_next;
  } else {
    return absl::InfiniteDuration();
  }
}

void CommonFiberThreadPool::PeriodicReleaseThreadBody() {
  // Run our reaper as long as the requested delay is less than or equal
  // to what the normal delay is -- implying that reclaim actually
  // happened.
  absl::Duration next_delay = absl::Milliseconds(
      absl::GetFlag(FLAGS_switchto_domain_idle_thread_death_interval_ms));
  absl::Duration cooldown_delay =
      absl::Milliseconds(absl::GetFlag(FLAGS_fiber_thread_reaper_cooldown_ms));

  bool cooling_down = false;
  while (true) {
    {
      ::SpinLockHolder l(lock_);
      if (shutdown_) break;
      next_delay = PeriodicReleaseIdleThreads();
      if (next_delay == absl::InfiniteDuration()) {
        if (cooling_down) break;
        cooling_down = true;
        next_delay = cooldown_delay;
      }
    }

    WaitStateScope scope(WaitStateScope::WaitState::kWaitingForWork);
    // We wait for the specified timeout OR for a signal to kill the reaping
    // thread because we are exiting.
    reaper_shutdown_.WaitForNotificationWithTimeout(next_delay);
  }

  ::SpinLockHolder l(lock_);
  ABSL_RAW_CHECK(periodic_release_thread_running_,
                 "wrong running_ flag value on stop");
  periodic_release_thread_running_ = false;
  if (shutdown_) {
    // Ack that we received the shutdown signal.
    last_reaper_finished_.Notify();
  }
}

CommonFiberThreadPool::~CommonFiberThreadPool() {
  DeregisterThreadPool(this);
  WaitForThreads();

  // After WaitForThreads() returns, the idle list is empty and since we are in
  // the destructor of the thread pool, the destructor of the owning Domain has
  // already finished. This implies that no one can spawn more fibers, which
  // means the reaper cannot be spawned at this point.
  reaper_shutdown_.Notify();

  // Determine if there is an active reaper. If there is, we wait for it to
  // finish.
  ::SpinLockHolder l(lock_);
  if (periodic_release_thread_running_) {
    lock_.unlock();
    // If the reaper is still running, it will notify last_reaper_finished_ when
    // it exits if shutdown_ is true.
    last_reaper_finished_.WaitForNotification();
    lock_.lock();  // Re-acquire lock to wait for the reaper to release it.
    ABSL_RAW_CHECK(!periodic_release_thread_running_, "reaper still running");
  }
}

struct CommonFiberThreadPool::Stats CommonFiberThreadPool::Stats() const {
  struct CommonFiberThreadPool::Stats s;
  s.num_active_threads = 0;
  s.num_idle_threads = 0;
  s.reaper_run_count = 0;

  {
    ::SpinLockHolder l(lock_);
    s.num_active_threads += num_active_threads_;
    s.num_idle_threads += num_idle_threads_;
    s.reaper_run_count += reaper_run_count_;
  }

  s.id = reinterpret_cast<uintptr_t>(this);
  s.name = name_;
  return s;
}

std::vector<struct CommonFiberThreadPool::Stats>
CommonFiberThreadPool::GetStats() {
  absl::MutexLock l(thread_pool_list_mu_);

  std::vector<struct CommonFiberThreadPool::Stats> res;
  for (auto it = GetThreadPoolList()->begin(); it != GetThreadPoolList()->end();
       ++it) {
    res.push_back((*it)->Stats());
  }
  return res;
}

}  // namespace thread
