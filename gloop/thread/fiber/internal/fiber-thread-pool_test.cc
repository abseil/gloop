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

#include "gloop/thread/fiber/internal/fiber-thread-pool.h"

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <thread>  // NOLINT(build/c++11)
#include <vector>

#include "absl/flags/flag.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/scheduling/domain.h"
#include "gloop/thread/fiber/fiber-options.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/fifolifo-schedulers.h"
#include "gloop/thread/fiber/futex-domain.h"
#include "gloop/thread/fiber/pthread-domain.h"
#include "gtest/gtest.h"

// We need these flags in order to properly test our thread reaping behavior.
// See fiber-thread-pool.cc for a full description.
ABSL_DECLARE_FLAG(int64_t, switchto_domain_idle_thread_death_interval_ms);
ABSL_DECLARE_FLAG(int64_t, fiber_thread_reaper_cooldown_ms);
ABSL_DECLARE_FLAG(int64_t, switchto_domain_idle_thread_timeout_ms);

namespace {

bool ThreadPoolNameMatches(absl::string_view given_name,
                           absl::string_view domain_name) {
  return given_name == absl::StrCat(domain_name, "-thread_pool");
}

// Yields the current thread to avoid spinning too long and blocking other work.
void YieldThread() { std::this_thread::yield(); }

TEST(FiberThreadPool, NoReapingUnderMinIdleThreads) {
  const std::string test_domain_name = "test_domain";

  // Create a custom domain + scheduler.
  base::scheduling::Domain* test_domain =
      thread::NewFutexDomain(test_domain_name, 10);
  if (!test_domain)
    test_domain = thread::NewPthreadDomain(test_domain_name, 10);

  base::scheduling::Scheduler* test_scheduler =
      thread::NewRootFIFOScheduler(test_domain);

  // 0 -> no fibers started.
  // 1 + quickly_idle Join()'ed -> active fiber started & idle fiber exited.
  // 2 -> we are done with statistic collecting.
  std::atomic<int> stage{0};
  auto always_active = thread::NewTree(
      thread::TreeOptions().set_scheduler(test_domain->root_scheduler()),
      [&stage] {
        stage.fetch_add(1, std::memory_order_seq_cst);

        // Spin until we are done with the test.
        while (stage.load(std::memory_order_seq_cst) != 2) {
          YieldThread();
        }
      });

  auto quickly_idle = thread::NewTree(
      thread::TreeOptions().set_scheduler(test_domain->root_scheduler()), [] {
        // Exit quickly and turn the backing thread idle.
      });

  // At this point the backing thread should be idle and the active fiber
  // should be spinning.
  quickly_idle->Join();
  while (stage.load(std::memory_order_seq_cst) != 1) {
    YieldThread();
  }

  absl::SleepFor(absl::Seconds(1));

  std::vector<struct thread::CommonFiberThreadPool::Stats> stats =
      thread::CommonFiberThreadPool::GetStats();
  for (auto s : stats) {
    if (ThreadPoolNameMatches(s.name, test_domain_name)) {
      // We have found the thread pool corresponding to the test domain.
      // We expect one active and one idle thread.
      EXPECT_EQ(s.num_active_threads, 1);
      EXPECT_EQ(s.num_idle_threads, 1);
    }
  }

  // Sleep for 3 "reaping periods". Nothing should happen as we have too few
  // idle threads to reap.
  absl::SleepFor(absl::Milliseconds(
      absl::GetFlag(FLAGS_switchto_domain_idle_thread_death_interval_ms) * 3));

  // Assert that no reaping happened.
  stats = thread::CommonFiberThreadPool::GetStats();
  for (auto s : stats) {
    if (ThreadPoolNameMatches(s.name, test_domain_name)) {
      // We have found the thread pool corresponding to the test domain.
      // We expect one active and one idle thread.
      EXPECT_EQ(s.num_active_threads, 1);
      EXPECT_EQ(s.num_idle_threads, 1);
    }
  }

  stage.fetch_add(1, std::memory_order_relaxed);
  always_active->Join();

  // Cleanup the test domain and scheduler.
  test_scheduler->Orphan();
  delete test_domain;
}

TEST(FiberThreadPool, Quiescence) {
  const std::string test_domain_name = "test_domain";
  // Create a custom domain + scheduler.
  base::scheduling::Domain* test_domain =
      thread::NewFutexDomain(test_domain_name, 10);
  if (!test_domain)
    test_domain = thread::NewPthreadDomain(test_domain_name, 10);
  base::scheduling::Scheduler* test_scheduler =
      thread::NewRootFIFOScheduler(test_domain);

  int first_pass_reaper_run_count;
  std::vector<struct thread::CommonFiberThreadPool::Stats> stats =
      thread::CommonFiberThreadPool::GetStats();
  for (auto s : stats) {
    if (ThreadPoolNameMatches(s.name, test_domain_name)) {
      first_pass_reaper_run_count = s.reaper_run_count;
    }
  }

  // Spawn 1 active thread, and N idle threads
  std::atomic<int> stage{0};  // 0 -> active thread not started, 1 -> active
                              // thread started, 2 -> test over.
  auto always_active = thread::NewTree(
      thread::TreeOptions().set_scheduler(test_domain->root_scheduler()),
      [&stage] {
        stage.fetch_add(1, std::memory_order_seq_cst);

        // Spin until we are done with the test.
        while (stage.load(std::memory_order_seq_cst) != 2) {
          YieldThread();
        }
      });

  absl::Notification n;
  std::vector<std::unique_ptr<thread::Fiber>> quickly_idle_fibers;
  std::atomic<int> quickly_spawned{0};
  const int fibers_to_spawn = 25;
  for (int i = 0; i < fibers_to_spawn; i++) {
    quickly_idle_fibers.push_back(thread::NewTree(
        thread::TreeOptions().set_scheduler(test_domain->root_scheduler()),
        [&n, &quickly_spawned] {
          quickly_spawned.fetch_add(1, std::memory_order_release);
          // Exit quickly and turn the backing thread idle, but don't exit
          // before all other idle threads have been created.
          n.WaitForNotification();
        }));
  }

  // Wait for all the threads to spawn before notifying them.
  while (quickly_spawned.load(std::memory_order_acquire) != fibers_to_spawn) {
    YieldThread();
  }
  n.Notify();

  // We need to wait longer than the cooldown period in the case the reaper was
  // currently cooling down.
  absl::SleepFor(absl::Milliseconds(
      absl::GetFlag(FLAGS_fiber_thread_reaper_cooldown_ms) * 3));

  int second_pass_reaper_run_count;
  stats = thread::CommonFiberThreadPool::GetStats();
  for (auto s : stats) {
    if (ThreadPoolNameMatches(s.name, test_domain_name)) {
      second_pass_reaper_run_count = s.reaper_run_count;
      EXPECT_GE(second_pass_reaper_run_count,
                first_pass_reaper_run_count);  // Reaper has run.
    }
  }

  // We should have lots of idle thread by now and the reaper should be running.
  // Spin until we can observe the reaper not running.
  unsigned int prev_reaper_run_count = second_pass_reaper_run_count;
  bool quiesced = true;
  for (int i = 0; i < 50; i++) {
    // Sleep longer than the reaping interval so that we necessarily should see
    // an increase in run count unless the reaper has stopped running.
    absl::SleepFor(absl::Milliseconds(
        absl::GetFlag(FLAGS_switchto_domain_idle_thread_death_interval_ms) *
        2));
    stats = thread::CommonFiberThreadPool::GetStats();
    for (auto s : stats) {
      if (ThreadPoolNameMatches(s.name, test_domain_name)) {
        if (s.reaper_run_count == prev_reaper_run_count) {
          quiesced = true;
          break;
        }
        prev_reaper_run_count = s.reaper_run_count;
      }
    }
  }

  EXPECT_TRUE(quiesced);

  stage.fetch_add(1, std::memory_order_acq_rel);
  always_active->Join();
  for (std::unique_ptr<thread::Fiber>& f : quickly_idle_fibers) {
    f->Join();
  }

  // Cleanup the test domain and scheduler.
  test_scheduler->Orphan();
  delete test_domain;
}

TEST(FiberThreadPool, Restarts) {
  const std::string test_domain_name = "test_domain";
  // Create a custom domain + scheduler.
  base::scheduling::Domain* test_domain =
      thread::NewFutexDomain(test_domain_name, 10);
  if (!test_domain)
    test_domain = thread::NewPthreadDomain(test_domain_name, 10);
  base::scheduling::Scheduler* test_scheduler =
      thread::NewRootFIFOScheduler(test_domain);

  int first_pass_reaper_run_count;
  std::vector<struct thread::CommonFiberThreadPool::Stats> stats =
      thread::CommonFiberThreadPool::GetStats();
  for (auto s : stats) {
    if (ThreadPoolNameMatches(s.name, test_domain_name)) {
      first_pass_reaper_run_count = s.reaper_run_count;
    }
  }

  // Spawn 1 active thread, and N idle threads
  std::atomic<int> stage{0};  // 0 -> active thread not started, 1 -> active
                              // thread started, 2 -> test over.
  auto always_active = thread::NewTree(
      thread::TreeOptions().set_scheduler(test_domain->root_scheduler()),
      [&stage] {
        stage.fetch_add(1, std::memory_order_seq_cst);

        // Spin until we are done with the test.
        while (stage.load(std::memory_order_seq_cst) != 2) {
          YieldThread();
        }
      });

  absl::Notification n;
  std::vector<std::unique_ptr<thread::Fiber>> quickly_idle_fibers;
  std::atomic<int> quickly_spawned{0};
  const int fibers_to_spawn = 25;
  for (int i = 0; i < fibers_to_spawn; i++) {
    quickly_idle_fibers.push_back(thread::NewTree(
        thread::TreeOptions().set_scheduler(test_domain->root_scheduler()),
        [&n, &quickly_spawned] {
          quickly_spawned.fetch_add(1, std::memory_order_release);
          // Exit quickly and turn the backing thread idle, but don't exit
          // before all other idle threads have been created.
          n.WaitForNotification();
        }));
  }

  // Wait for all the threads to spawn before notifying them.
  while (quickly_spawned.load(std::memory_order_acquire) != fibers_to_spawn) {
    YieldThread();
  }
  n.Notify();

  // We need to wait longer than the cooldown period in the case the reaper was
  // currently cooling down.
  absl::SleepFor(absl::Milliseconds(
      absl::GetFlag(FLAGS_fiber_thread_reaper_cooldown_ms) * 3));

  int second_pass_reaper_run_count;
  stats = thread::CommonFiberThreadPool::GetStats();
  for (auto s : stats) {
    if (ThreadPoolNameMatches(s.name, test_domain_name)) {
      second_pass_reaper_run_count = s.reaper_run_count;
      EXPECT_GE(second_pass_reaper_run_count,
                first_pass_reaper_run_count);  // Reaper has run.
    }
  }

  // We should have lots of idle thread by now and the reaper should be running.
  // Spin until we can observe the reaper not running.
  unsigned int prev_reaper_run_count = second_pass_reaper_run_count;
  bool keep_going = true;
  while (keep_going) {
    // Sleep longer than the reaping interval so that we necessarily should see
    // an increase in run count unless the reaper has stopped running.
    absl::SleepFor(absl::Milliseconds(
        absl::GetFlag(FLAGS_switchto_domain_idle_thread_death_interval_ms) *
        2));
    stats = thread::CommonFiberThreadPool::GetStats();
    for (auto s : stats) {
      if (ThreadPoolNameMatches(s.name, test_domain_name)) {
        if (s.reaper_run_count == prev_reaper_run_count) {
          keep_going = false;
          break;
        }
        prev_reaper_run_count = s.reaper_run_count;
      }
    }
  }

  // Force the reaper to restart by adding more idle threads.
  absl::Notification n2;
  quickly_spawned.exchange(0, std::memory_order_release);
  for (int i = 0; i < fibers_to_spawn; i++) {
    quickly_idle_fibers.push_back(thread::NewTree(
        thread::TreeOptions().set_scheduler(test_domain->root_scheduler()),
        [&n2, &quickly_spawned] {
          quickly_spawned.fetch_add(1, std::memory_order_acq_rel);
          // Exit quickly and turn the backing thread idle, but don't exit
          // before all other idle threads have been created.
          n2.WaitForNotification();
        }));
  }
  while (quickly_spawned.load(std::memory_order_acquire) != fibers_to_spawn) {
    YieldThread();
  }
  n2.Notify();

  // Now check that the reaper restarted by asserting that the reaper run count
  // has increased.
  bool increased = false;
  for (int i = 0; i < 50; i++) {
    stats = thread::CommonFiberThreadPool::GetStats();
    for (auto s : stats) {
      if (ThreadPoolNameMatches(s.name, test_domain_name)) {
        if (s.reaper_run_count > prev_reaper_run_count) {
          increased = true;
          break;
        }
      }
      if (increased) {
        break;
      }
    }
    absl::SleepFor(absl::Seconds(1));
  }

  EXPECT_TRUE(increased);
  stage.fetch_add(1, std::memory_order_acq_rel);
  always_active->Join();
  for (std::unique_ptr<thread::Fiber>& f : quickly_idle_fibers) {
    f->Join();
  }
  test_scheduler->Orphan();
  delete test_domain;
}

TEST(FiberThreadPool, ReapingAboveMinIdleThreads) {
  const std::string test_domain_name = "test_domain";

  // Create a custom domain + scheduler.
  base::scheduling::Domain* test_domain =
      thread::NewFutexDomain(test_domain_name, 10);
  if (!test_domain)
    test_domain = thread::NewPthreadDomain(test_domain_name, 10);

  base::scheduling::Scheduler* test_scheduler =
      thread::NewRootFIFOScheduler(test_domain);

  // 0 -> no fibers started.
  // 1 + quickly_idle_fibers Join()'ed -> active fiber started & idle fiber
  // exited. 2 -> we are done with statistic collecting.
  std::atomic<int> stage{0};
  auto always_active = thread::NewTree(
      thread::TreeOptions().set_scheduler(test_domain->root_scheduler()),
      [&stage] {
        stage.fetch_add(1, std::memory_order_seq_cst);

        // Spin until we are done with the test.
        while (stage.load(std::memory_order_seq_cst) != 2) {
          YieldThread();
        }
      });

  absl::Notification n;
  std::vector<std::unique_ptr<thread::Fiber>> quickly_idle_fibers;
  for (int i = 0; i < 10; i++) {
    quickly_idle_fibers.push_back(thread::NewTree(
        thread::TreeOptions().set_scheduler(test_domain->root_scheduler()),
        [&n] {
          // Exit quickly and turn the backing thread idle, but don't exit
          // before all other idle threads have been created.
          n.WaitForNotification();
        }));
  }
  n.Notify();

  // At this point the backing threads should be idle and the active fiber
  // should be spinning.
  for (auto const& f : quickly_idle_fibers) {
    f->Join();
  }

  while (stage.load(std::memory_order_seq_cst) != 1) {
    YieldThread();
  }

  // There is a subtle race where we will collect statistics after we join the
  // fiber, but before it is counted as idle. Sleeping for a second assuages
  // this race.
  absl::SleepFor(absl::Seconds(5));

  int first_pass_idle_threads;
  std::vector<struct thread::CommonFiberThreadPool::Stats> stats =
      thread::CommonFiberThreadPool::GetStats();
  for (auto s : stats) {
    if (ThreadPoolNameMatches(s.name, test_domain_name)) {
      // We have found the thread pool corresponding to the test domain.
      // We expect one active and a number of idle threads less than or equal to
      // the size of our vector. If it is less than, then this means we raced
      // with the reaping and it won, which is still valid behavior.
      EXPECT_EQ(s.num_active_threads, 1);
      first_pass_idle_threads = s.num_idle_threads;
      EXPECT_LE(first_pass_idle_threads, quickly_idle_fibers.size());
    }
  }

  // Sleep for 3 "reaping periods". Nothing should happen as we have too few
  // idle threads to reap.
  absl::SleepFor(absl::Milliseconds(
      absl::GetFlag(FLAGS_switchto_domain_idle_thread_death_interval_ms) * 3));

  // Assert that no reaping happened.
  stats = thread::CommonFiberThreadPool::GetStats();
  for (auto s : stats) {
    if (ThreadPoolNameMatches(s.name, test_domain_name)) {
      // We have found the thread pool corresponding to the test domain.
      // We expect one active and fewer idle threads or the same than before.
      EXPECT_EQ(s.num_active_threads, 1);
      EXPECT_LE(s.num_idle_threads, first_pass_idle_threads);
    }
  }

  stage.fetch_add(1, std::memory_order_relaxed);
  always_active->Join();

  // Cleanup the test domain and scheduler.
  test_scheduler->Orphan();
  delete test_domain;
}

TEST(FiberThreadPool, IterableStats) {
  // Each domain contains its own thread pool. To test our ability to correctly
  // get stats for multiple thread pools, we create multiple domains. To test
  // the stats are correct we manifest a scenario where n threads are active in
  // the n-1th domain, which we can identify by name.
  const int nCustomDomains = 10;
  std::vector<base::scheduling::Domain*> domains;
  std::vector<base::scheduling::Scheduler*> schedulers;
  std::vector<std::unique_ptr<thread::Fiber>> fibers;
  std::map<std::string, int>
      name_to_nfibers;  // Android and fuchsia seem to not have
                        // absl::flat_hash_map available.

  constexpr int num_fibers = ((nCustomDomains + 1) * nCustomDomains) / 2;
  std::atomic<int> started_fibers{0};

  domains.reserve(nCustomDomains);
  schedulers.reserve(nCustomDomains);
  fibers.reserve(num_fibers);

  for (int i = 0; i < nCustomDomains; i++) {
    // We validate our Stats struct by name, e.g. test_5-thread_pool should have
    // _6_ threads.
    name_to_nfibers.emplace(absl::StrCat("test_", i, "-thread_pool"), i + 1);
    base::scheduling::Domain* test_domain =
        thread::NewFutexDomain(absl::StrCat("test_", i), 10);
    if (!test_domain)
      test_domain = thread::NewPthreadDomain(absl::StrCat("test_", i), 10);
    domains.push_back(test_domain);
    schedulers.push_back(thread::NewRootFIFOScheduler(domains[i]));

    // Create i+1 threads and keep them active for 10 seconds.
    for (int j = 0; j <= i; j++) {
      fibers.push_back(thread::NewTree(
          thread::TreeOptions().set_scheduler(domains[i]->root_scheduler()),
          [&started_fibers] {
            started_fibers.fetch_add(1, std::memory_order_seq_cst);

            // Spin until we are done with our statistics collecting to keep all
            // fibers in an active state.
            // The + 1 is not a typo, we signal that we are done with statistic
            // collecting by incrementing our counter one extra time.
            while (started_fibers.load(std::memory_order_seq_cst) !=
                   num_fibers + 1) {
              YieldThread();
            }
          }));
    }
  }

  // Spin until all our fibers have started.
  while (started_fibers.load(std::memory_order_seq_cst) != num_fibers) {
    YieldThread();
  }

  std::vector<struct thread::CommonFiberThreadPool::Stats> stats =
      thread::CommonFiberThreadPool::GetStats();
  for (auto s : stats) {
    EXPECT_EQ(name_to_nfibers.at(std::string(s.name)), s.num_active_threads);
  }

  // Increment one extra time so the fibers know they can die.
  started_fibers.fetch_add(1, std::memory_order_seq_cst);

  for (auto& f : fibers) {
    f->Join();
  }

  for (int i = 0; i < nCustomDomains; i++) {
    schedulers[i]->Orphan();
    delete domains[i];
  }
}

}  // namespace
