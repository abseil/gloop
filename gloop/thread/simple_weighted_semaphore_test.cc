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

#include "gloop/thread/simple_weighted_semaphore.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <string>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"
#include "gloop/thread/threadpool.h"
#include "gloop/util/random/shared_bit_gen.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using ::absl_testing::IsOk;
using ::thread::SimpleWeightedSemaphore;
using ::thread::SimpleWeightedSemaphoreLock;

class SemaphoreTest {
 public:
  // Start a semaphore test. We will create a throttle (semaphore) with the
  // given limit, and then have count_controllers separate threads start up
  // count_tasks tasks. We let all the tasks run, keeping track of average
  // and peak load on the throttle, and make sure nothing untoward ever
  // happens. If count_controllers is -1, we use the maximum possible.
  // If test_oscillate is true, the capacity of the throttle will be varied
  // over the run, and (to avoid deadlocks) all acquires will be of 1 unit.
  SemaphoreTest(uint64_t limit, int count_tasks, int count_controllers,
                bool test_oscillate);
  ~SemaphoreTest() = default;

  absl::Status Run();

 private:
  uint64_t limit_;         // The throttle's limit
  const int count_tasks_;  // The number of limit-consuming tasks to launch
  const int count_controllers_;  // The number of threads starting tasks
  const bool test_oscillate_;

  // Statistics
  absl::Mutex done_lock_;       // Protects the following uint64's:
  uint64_t done_;               // Number of tasks already finished
  uint64_t skipped_;            // Number skipped because of throttle stoppage
  uint64_t adjusts_;            // Number of throttle adjusts
  uint64_t total_cost_;         // For computing the avg cost in use at any time
  uint64_t max_cost_;           // The peak cost in use by the throttle
  uint64_t oscillation_range_;  // Changes in max_cost_
  absl::BlockingCounter started_;   // Count how many have started up
  absl::BlockingCounter finished_;  // Count how many have finished

  std::unique_ptr<SimpleWeightedSemaphore> throttle_;
  std::unique_ptr<ThreadPool> worker_pool_;

  void StartTasks(int num, int controller_id);
  // Called when a single task finishes.
  void WriteDone(int task_num, uint64_t cost, uint64_t msec);

  // The cost of a single task is kBaseCost +- kDeltaCost (min 1)
  static constexpr uint64_t kBaseCost = 3;
  static constexpr uint64_t kDeltaCost = 3;

  // Properties of the worker pool
  static constexpr int kWorkerQueueLength = 80000;
  static constexpr int kNumWorkerThreads = 80;
};

SemaphoreTest::SemaphoreTest(uint64_t limit, int count_tasks,
                             int count_controllers, bool test_oscillate)
    // Don't set the throttle limit below the amount we'll actually require.
    : limit_(std::max(limit, kBaseCost + kDeltaCost)),
      count_tasks_(count_tasks),
      count_controllers_(
          count_controllers == -1
              ? kNumWorkerThreads - 10
              : std::min(count_controllers, kNumWorkerThreads - 10)),
      test_oscillate_(test_oscillate),
      done_(0),
      skipped_(0),
      adjusts_(0),
      total_cost_(0),
      max_cost_(0),
      oscillation_range_(test_oscillate_ ? limit_ / 10 : 0),
      started_(count_tasks),
      finished_(count_tasks),
      throttle_(std::make_unique<SimpleWeightedSemaphore>(limit_)),
      worker_pool_(std::make_unique<ThreadPool>(
          kNumWorkerThreads,
          ThreadPool::Options{.thread_options = thread::Options(),
                              .queue_capacity = kWorkerQueueLength})) {
  VLOG(1) << "Throttle limit " << limit_ << " Oscillation range "
          << oscillation_range_;
  CHECK_LT(count_controllers, kNumWorkerThreads)
      << ": This test doesn't support having all its threads used as "
         "controllers";
}

absl::Status SemaphoreTest::Run() {
  int num_per_controller = count_tasks_ / count_controllers_;
  int surplus = count_tasks_ % count_controllers_;
  for (int i = 0; i < count_controllers_ - 1; ++i) {
    VLOG(1) << "Starting controller " << i;
    worker_pool_->Schedule(
        [this, num_per_controller, i]() { StartTasks(num_per_controller, i); });
  }
  VLOG(1) << "Starting controller " << count_controllers_ - 1;
  worker_pool_->Schedule([this, num_per_controller, surplus]() {
    StartTasks(num_per_controller + surplus, count_controllers_ - 1);
  });

  // Wait for everything to be started
  started_.Wait();

  // Wait for everything to finish. (It's not safe to delete the throttle
  // while other threads may be using it...)
  finished_.Wait();

  VLOG(1) << " Max cost: " << max_cost_
          << " Average: " << static_cast<double>(total_cost_) / done_;

  uint64_t actual_total = done_ + skipped_ + adjusts_;
  if (actual_total != static_cast<uint64_t>(count_tasks_)) {
    return absl::InternalError(
        absl::StrCat("Simulation failed: done (", done_, ") + skipped (",
                     skipped_, ") + adjusts (", adjusts_, ") = ", actual_total,
                     " != expected task count (", count_tasks_, ")"));
  }

  return absl::OkStatus();
}

void SemaphoreTest::StartTasks(int num, int controller_id) {
  std::mt19937_64 bitgen(controller_id);
  for (int i = 0; i < num; ++i) {
    if (i % 1000 == 1) VLOG(1) << "Started " << i;
    if (oscillation_range_ > 0 && absl::Bernoulli(bitgen, 1.0 / 500)) {
      absl::MutexLock l(done_lock_);
      // This is intentionally slightly biased towards negative delta, so
      // that we'll find ourselves in an increasingly tricky situation
      // (small max_cost) over the course of the test.
      int64_t signed_oscillation_range =
          static_cast<int64_t>(oscillation_range_);
      int64_t delta = absl::Uniform<int64_t>(bitgen, -signed_oscillation_range,
                                             signed_oscillation_range);
      int64_t current_max = static_cast<int64_t>(throttle_->max_cost());
      uint64_t new_max =
          static_cast<uint64_t>(std::max(int64_t{1}, current_max + delta));
      VLOG(2) << "Adjusting capacity by " << delta << ": new max " << new_max;
      throttle_->set_max_cost(new_max);
      // This doesn't jump to WriteDone; we're done in this case.
      adjusts_++;
      finished_.DecrementCount();
    } else {
      int64_t signed_k_delta_cost = static_cast<int64_t>(kDeltaCost);
      int64_t cost_delta = absl::Uniform<int64_t>(bitgen, -signed_k_delta_cost,
                                                  signed_k_delta_cost + 1);
      int64_t calculated_cost = static_cast<int64_t>(kBaseCost) + cost_delta;
      // If we're testing oscillation, always acquire 1, or we're just
      // going to be testing deadlocks as we try to acquire more than the
      // capacity of the lock.
      uint64_t cost = (calculated_cost < 1 || oscillation_range_ > 0)
                          ? 1
                          : static_cast<uint64_t>(calculated_cost);
      // Try to acquire. If we're stopped, just skip
      VLOG(2) << "Acquiring " << cost << ": pending "
              << throttle_->pending_cost() << " max " << throttle_->max_cost();
      uint64_t msec;
      if (throttle_->Acquire(cost, &msec)) {
        worker_pool_->Schedule(
            [this, i, cost, msec]() { WriteDone(i, cost, msec); });
      } else {
        {
          absl::MutexLock l(done_lock_);
          skipped_++;
          finished_.DecrementCount();  // "Finished" for free
          if (skipped_ % 100 == 0) VLOG(1) << "Skipped " << skipped_;
        }
        // Avoid livelocks by forcing a sleep.
        absl::SleepFor(absl::Milliseconds(1));
      }
    }
    started_.DecrementCount();
  }
}

void SemaphoreTest::WriteDone(int task_num, uint64_t cost, uint64_t msec) {
  CHECK(throttle_ != nullptr);
  uint64_t pending = throttle_->pending_cost();

  {
    absl::MutexLock l(done_lock_);
    done_++;
    total_cost_ += pending;
    max_cost_ = std::max(max_cost_, pending);
    EXPECT_LE(done_ + skipped_ + adjusts_, static_cast<uint64_t>(count_tasks_));
    if (done_ % 1000 == 0) VLOG(1) << done_ << " done";
    VLOG(1) << "Task " << task_num << " executed: " << done_ << " done, "
            << pending << " pending";
  }

  throttle_->Release(cost);
  VLOG(2) << "Released " << cost << ": Pending " << throttle_->pending_cost()
          << " limit " << throttle_->max_cost();

  // Every so often, do a stop and start on the throttle
  util_random::SharedBitGen bitgen;
  if (absl::Bernoulli(bitgen, 1.0 / 1000)) {
    VLOG(2) << "Stopping";
    throttle_->Stop();
    VLOG(2) << "Restarting";
    throttle_->Start();
  }

  finished_.DecrementCount();
}

struct SemaphoreTestParams {
  uint64_t limit;
  int count_tasks;
  int count_controllers;
  bool test_oscillate;
};

std::string GetSemaphoreTestName(
    const ::testing::TestParamInfo<SemaphoreTestParams>& info) {
  std::string name = info.param.test_oscillate ? "Oscillating" : "";
  absl::StrAppend(&name, "Limit", info.param.limit);
  absl::StrAppend(&name, "Tasks", info.param.count_tasks);
  if (info.param.count_controllers == -1) {
    absl::StrAppend(&name, "ControllersMax");
  } else {
    absl::StrAppend(&name, "Controllers", info.param.count_controllers);
  }
  return name;
}

class SimpleWeightedSemaphoreParameterizedTest
    : public ::testing::TestWithParam<SemaphoreTestParams> {};

TEST_P(SimpleWeightedSemaphoreParameterizedTest, RunSimulation) {
  const SemaphoreTestParams& params = GetParam();
  SemaphoreTest test(params.limit, params.count_tasks, params.count_controllers,
                     params.test_oscillate);
  EXPECT_THAT(test.Run(), IsOk());
}

INSTANTIATE_TEST_SUITE_P(
    SimpleWeightedSemaphoreTestInstantiation,
    SimpleWeightedSemaphoreParameterizedTest,
    ::testing::Values(
        // Test the throttle at a range of speeds
        SemaphoreTestParams{/*limit=*/1, /*count_tasks=*/500,
                            /*count_controllers=*/1,
                            /*test_oscillate=*/false},
        SemaphoreTestParams{/*limit=*/10, /*count_tasks=*/5000,
                            /*count_controllers=*/1,
                            /*test_oscillate=*/false},
        SemaphoreTestParams{/*limit=*/50, /*count_tasks=*/5000,
                            /*count_controllers=*/1,
                            /*test_oscillate=*/false},
        SemaphoreTestParams{/*limit=*/500, /*count_tasks=*/50000,
                            /*count_controllers=*/1,
                            /*test_oscillate=*/false},
        SemaphoreTestParams{/*limit=*/5000, /*count_tasks=*/50000,
                            /*count_controllers=*/1,
                            /*test_oscillate=*/false},
        // Test starting tasks from multiple threads
        SemaphoreTestParams{/*limit=*/50, /*count_tasks=*/50000,
                            /*count_controllers=*/5,
                            /*test_oscillate=*/false},
        SemaphoreTestParams{/*limit=*/5000, /*count_tasks=*/50000,
                            /*count_controllers=*/-1,
                            /*test_oscillate=*/false},
        // Test oscillating capacity
        SemaphoreTestParams{/*limit=*/50, /*count_tasks=*/50000,
                            /*count_controllers=*/5,
                            /*test_oscillate=*/true}),
    GetSemaphoreTestName);

// There used to be an integer overflow bug in
// SimpleWeightedSemaphore::Acquire() where if pending_cost_ + cost would
// overflow a uint64, we would allow the acquisition to happen, but then CHECK
// and crash the program. Ensure that we've fixed this bug.
TEST(SimpleWeightedSemaphoreTest, OverflowAcquire) {
  SimpleWeightedSemaphore sem(std::numeric_limits<uint64_t>::max());
  sem.set_stop_on_exit(false);
  ASSERT_TRUE(sem.Acquire(std::numeric_limits<uint64_t>::max()));

  // Fast non-blocking check under overflow
  EXPECT_FALSE(sem.TryAcquire(1));

  absl::Notification main_thread_ready;

  thread::Options options;
  options.set_joinable(true);
  ClosureThread ct(options, "OverflowAcquireThread", [&]() {
    main_thread_ready.WaitForNotification();
    // Give the main thread a robust moment to enter Acquire and block.
    // 200ms safely absorbs TSAN/ASAN execution overheads and CFS
    // scheduling latency.
    absl::SleepFor(absl::Milliseconds(200));
    sem.Release(1);
  });

  ct.Start();

  main_thread_ready.Notify();
  // Blocks until background thread calls Release(1).
  // This verifies the blocking condvar wait and signal wakeup under overflow.
  EXPECT_TRUE(sem.Acquire(1));
  ct.Join();

  sem.Release(std::numeric_limits<uint64_t>::max());
}

TEST(SimpleWeightedSemaphoreTest, AcquireAlways) {
  SimpleWeightedSemaphore s(10);
  s.Stop();

  absl::Notification thread_started;

  thread::Options options;
  options.set_joinable(true);
  ClosureThread ct(options, "AcquireAlwaysThread", [&]() {
    EXPECT_TRUE(s.stopped());
    thread_started.Notify();
    s.AcquireAlways(1);
    EXPECT_FALSE(s.stopped());
    s.Release(1);
  });

  ct.Start();
  thread_started.WaitForNotification();
  absl::SleepFor(absl::Milliseconds(100));

  // Now, the other thread is about to block or is blocked.
  // Start the semaphore.
  s.Start();
  ct.Join();
}

TEST(SimpleWeightedSemaphoreTest, SimpleWeightedSemaphoreLock) {
  SimpleWeightedSemaphore s(1);
  {
    SimpleWeightedSemaphoreLock lock(&s, 1);
    EXPECT_FALSE(s.TryAcquire(1));
  }
  s.AcquireAlways(1);
  s.Release(1);
}

TEST(SimpleWeightedSemaphoreTest, StopAndAcquireAlwaysWithMsec) {
  SimpleWeightedSemaphore sem(10);
  sem.Stop();

  uint64_t stop_msec = std::numeric_limits<uint64_t>::max();
  sem.Stop(&stop_msec);
  EXPECT_NE(stop_msec, std::numeric_limits<uint64_t>::max());

  sem.Start();
  uint64_t acquire_msec = std::numeric_limits<uint64_t>::max();
  sem.AcquireAlways(1, &acquire_msec);
  EXPECT_NE(acquire_msec, std::numeric_limits<uint64_t>::max());
  sem.Release(1);
}

TEST(SimpleWeightedSemaphoreTest, CostZero) {
  SimpleWeightedSemaphore sem(10);

  // Test Acquire with 0 cost
  uint64_t msec = 12345;
  EXPECT_TRUE(sem.Acquire(0, &msec));
  EXPECT_EQ(msec, 0);
  EXPECT_EQ(sem.pending_cost(), 0);

  // Test AcquireAlways with 0 cost
  msec = 12345;
  sem.AcquireAlways(0, &msec);
  EXPECT_EQ(msec, 0);
  EXPECT_EQ(sem.pending_cost(), 0);

  // Test Release with 0 cost
  sem.Release(0);
  EXPECT_EQ(sem.pending_cost(), 0);
}

TEST(SimpleWeightedSemaphoreTest, StartWhileStopping) {
  SimpleWeightedSemaphore sem(10);
  ASSERT_TRUE(sem.Acquire(5));

  absl::Notification stop_thread_ready;
  absl::Notification start_thread_ready;

  thread::Options options;
  options.set_joinable(true);

  // Thread 1 calls Stop(). It will block because pending_cost is 5 > 0.
  ClosureThread stop_thread(options, "StopThread", [&]() {
    stop_thread_ready.Notify();
    sem.Stop();
  });

  // Thread 2 calls Start(). It will block because Stop() is in progress
  // (stopping_ is true).
  ClosureThread start_thread(options, "StartThread", [&]() {
    start_thread_ready.Notify();
    sem.Start();
  });

  stop_thread.Start();
  stop_thread_ready.WaitForNotification();
  // Wait a bit to ensure Stop() has actually run and is blocking.
  absl::SleepFor(absl::Milliseconds(100));

  start_thread.Start();
  start_thread_ready.WaitForNotification();
  // Wait a bit to ensure Start() has run and is blocked on stop_finished_.
  absl::SleepFor(absl::Milliseconds(100));

  // Release the pending cost. This will unblock Stop(), which will then
  // set stopping_ to false and signal stop_finished_, unblocking Start().
  sem.Release(5);

  stop_thread.Join();
  start_thread.Join();

  EXPECT_FALSE(sem.stopped());
}

TEST(SimpleWeightedSemaphoreTest, AcquireAlwaysBlockedByCapacity) {
  SimpleWeightedSemaphore sem(5);
  ASSERT_TRUE(sem.Acquire(5));

  absl::Notification acquire_thread_ready;

  thread::Options options;
  options.set_joinable(true);

  // Thread calls AcquireAlways(2). It will block because pending_cost is 5
  // and max_cost is 5.
  ClosureThread acquire_thread(options, "AcquireThread", [&]() {
    acquire_thread_ready.Notify();
    sem.AcquireAlways(2);
    sem.Release(2);
  });

  acquire_thread.Start();
  acquire_thread_ready.WaitForNotification();
  // Wait to ensure AcquireAlways is blocked on capacity.
  absl::SleepFor(absl::Milliseconds(100));

  // Stop the semaphore. This will wake up AcquireAlways from the capacity
  // wait loop, but it will loop back and block on started_ because stopped_
  // is true. Note: Stop() will block until pending operations (the original
  // Acquire(5)) are released.
  absl::Notification stop_thread_ready;
  ClosureThread stop_thread(options, "StopThread", [&]() {
    stop_thread_ready.Notify();
    sem.Stop();
  });

  stop_thread.Start();
  stop_thread_ready.WaitForNotification();
  absl::SleepFor(absl::Milliseconds(100));

  // Release the original 5 units. This allows Stop() to finish.
  sem.Release(5);
  stop_thread.Join();

  // Now the semaphore is stopped, and the acquire_thread is blocked on
  // started_. Restart the semaphore. This should allow the acquire_thread to
  // wake up, acquire the 2 units, and finish.
  sem.Start();
  acquire_thread.Join();

  EXPECT_EQ(sem.pending_cost(), 0);
}

TEST(SimpleWeightedSemaphoreTest, TryAcquire) {
  SimpleWeightedSemaphore sem(5);
  sem.Start();

  EXPECT_TRUE(sem.TryAcquire(3));
  EXPECT_FALSE(sem.TryAcquire(3));
  EXPECT_TRUE(sem.TryAcquire(2));

  sem.Release(5);
  sem.Stop();
  EXPECT_FALSE(sem.TryAcquire(1));
}

}  // namespace
