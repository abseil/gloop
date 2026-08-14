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
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"
#include "gloop/thread/threadlocal.h"
#include "gloop/thread/threadpool.h"
#include "gloop/util/random/acmrandom.h"
#include "gtest/gtest.h"

namespace {

using ::thread::SimpleWeightedSemaphore;
using ::thread::SimpleWeightedSemaphoreLock;

static ThreadLocal<std::unique_ptr<ACMRandom> > random_pool;

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
  ~SemaphoreTest();

  void Run();

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

  SimpleWeightedSemaphore* throttle_;
  ThreadPool* worker_pool_;

  void StartTasks(int num);
  // Called when a single task finishes.
  void WriteDone(int task_num, uint64_t cost, uint64_t msec);

  // The cost of a single task is kBaseCost +- kDeltaCost (min 1)
  static constexpr uint64_t kBaseCost = 3;
  static constexpr uint64_t kDeltaCost = 3;

  // Properties of the worker pool
  static constexpr int kWorkerQueueLength = 80000;
  static constexpr int kNumWorkerThreads = 50;
};

SemaphoreTest::SemaphoreTest(uint64_t limit, int count_tasks,
                             int count_controllers, bool test_oscillate)
    : count_tasks_(count_tasks),
      count_controllers_(
          count_controllers == -1
              ? kNumWorkerThreads - 1
              : std::min(count_controllers, kNumWorkerThreads - 1)),
      test_oscillate_(test_oscillate),
      done_(0),
      skipped_(0),
      adjusts_(0),
      total_cost_(0),
      max_cost_(0),
      started_(count_tasks),
      finished_(count_tasks),
      worker_pool_(new ThreadPool(
          kNumWorkerThreads,
          ThreadPool::Options{.thread_options = thread::Options(),
                              .queue_capacity = kWorkerQueueLength})) {
  // Don't set the throttle limit below the amount we'll actually require...
  limit_ = std::max(limit, kBaseCost + kDeltaCost);
  oscillation_range_ = (test_oscillate_ ? limit_ / 10 : 0);
  throttle_ = new SimpleWeightedSemaphore(limit_);
  LOG(INFO) << "Throttle limit " << limit_ << " Oscillation range "
            << oscillation_range_;
  CHECK_LT(count_controllers, kNumWorkerThreads)
      << ": This test doesn't support having all its threads used as "
         "controllers";
}

SemaphoreTest::~SemaphoreTest() {
  delete throttle_;
  delete worker_pool_;
}

void SemaphoreTest::Run() {
  int num_per_controller = count_tasks_ / count_controllers_;
  int surplus = count_tasks_ % count_controllers_;
  for (int i = 0; i < count_controllers_ - 1; ++i) {
    VLOG(1) << "Starting controller " << i;
    worker_pool_->Schedule(
        absl::bind_front(&SemaphoreTest::StartTasks, this, num_per_controller));
  }
  VLOG(1) << "Starting controller " << count_controllers_ - 1;
  worker_pool_->Schedule(absl::bind_front(&SemaphoreTest::StartTasks, this,
                                          num_per_controller + surplus));

  // Wait for everything to be started
  started_.Wait();

  // Wait for everything to finish. (It's not safe to delete the throttle
  // while other threads may be using it...)
  finished_.Wait();

  EXPECT_EQ(done_ + skipped_ + adjusts_, count_tasks_);
  LOG(INFO) << " Max cost: " << max_cost_
            << " Average: " << static_cast<double>(total_cost_) / done_;
}

void SemaphoreTest::StartTasks(int num) {
  if (random_pool.get() == nullptr)
    random_pool.pointer()->reset(
        new ACMRandom(ACMRandom::HostnamePidTimeSeed()));
  ACMRandom* pool = random_pool.get().get();
  for (int i = 0; i < num; ++i) {
    if (i % 1000 == 1) VLOG(1) << "Started " << i;
    if (oscillation_range_ > 0 && absl::Bernoulli(*pool, 1.0 / 500)) {
      absl::MutexLock l(done_lock_);
      // This is intentionally slightly biased towards negative delta, so
      // that we'll find ourselves in an increasingly tricky situation
      // (small max_cost) over the course of the test.
      int delta = (absl::Uniform<uint32_t>(*pool) % (2 * oscillation_range_)) -
                  oscillation_range_;
      uint64_t new_max = std::max(uint64_t{1}, throttle_->max_cost() + delta);
      VLOG(2) << "Adjusting capacity by " << delta << ": new max " << new_max;
      throttle_->set_max_cost(new_max);
      // This doesn't jump to WriteDone; we're done in this case.
      finished_.DecrementCount();
      adjusts_++;
    } else {
      uint64_t cost = kBaseCost +
                      (absl::Uniform<uint64_t>(*pool) % (2 * kDeltaCost + 1)) -
                      kDeltaCost;
      // If we're testing oscillation, always acquire 1, or we're just
      // going to be testing deadlocks as we try to acquire more than the
      // capacity of the lock.
      if (cost < 1 || oscillation_range_ > 0) cost = 1;
      // Try to acquire. If we're stopped, just skip
      VLOG(2) << "Acquiring " << cost << ": pending "
              << throttle_->pending_cost() << " max " << throttle_->max_cost();
      uint64_t msec;
      if (throttle_->Acquire(cost, &msec)) {
        worker_pool_->Schedule(
            absl::bind_front(&SemaphoreTest::WriteDone, this, i, cost, msec));
      } else {
        absl::MutexLock l(done_lock_);
        skipped_++;
        finished_.DecrementCount();  // "Finished" for free
        if (skipped_ % 100 == 0) VLOG(1) << "Skipped " << skipped_;
      }
    }
    started_.DecrementCount();
  }
}

void SemaphoreTest::WriteDone(int task_num, uint64_t cost, uint64_t msec) {
  CHECK_NE(throttle_, nullptr);
  uint64_t pending = throttle_->pending_cost();

  {
    absl::MutexLock l(done_lock_);
    done_++;
    total_cost_ += pending;
    max_cost_ = std::max(max_cost_, pending);
    EXPECT_LE(done_ + skipped_ + adjusts_, count_tasks_);
    if (done_ % 1000 == 0) VLOG(1) << done_ << " done";
    VLOG(1) << "Task " << task_num << " executed: " << done_ << " done, "
            << pending << " pending";
  }

  throttle_->Release(cost);
  VLOG(2) << "Released " << cost << ": Pending " << throttle_->pending_cost()
          << " limit " << throttle_->max_cost();

  // Every so often, do a stop and start on the throttle
  if (random_pool.get() == nullptr) {
    random_pool.pointer()->reset(
        new ACMRandom(ACMRandom::HostnamePidTimeSeed() + task_num));
  }
  if (absl::Bernoulli(*random_pool.get(), 1.0 / 1000)) {
    VLOG(2) << "Stopping";
    throttle_->Stop();
    VLOG(2) << "Restarting";
    throttle_->Start();
  }

  finished_.DecrementCount();
}

struct SemaphoreTestParams {
  uint64_t throttle_limit;
  int count_tasks;
  int count_controllers;
  bool test_oscillation;
};

class SimpleWeightedSemaphoreThrottleTest
    : public ::testing::TestWithParam<SemaphoreTestParams> {};

TEST_P(SimpleWeightedSemaphoreThrottleTest, Throttle) {
  const SemaphoreTestParams& params = GetParam();
  SemaphoreTest test(params.throttle_limit, params.count_tasks,
                     params.count_controllers, params.test_oscillation);
  test.Run();
}

INSTANTIATE_TEST_SUITE_P(
    , SimpleWeightedSemaphoreThrottleTest,
    ::testing::Values(
        // Test the throttle at a range of speeds
        SemaphoreTestParams{1, 500, 1, false},
        SemaphoreTestParams{10, 5000, 1, false},
        SemaphoreTestParams{50, 5000, 1, false},
        SemaphoreTestParams{500, 50000, 1, false},
        SemaphoreTestParams{5000, 50000, 1, false},
        // Test starting tasks from multiple threads
        SemaphoreTestParams{50, 50000, 5, false},
        SemaphoreTestParams{5000, 50000, -1, false},
        // Test oscillating capacity
        SemaphoreTestParams{50, 50000, 5, true}),
    [](const ::testing::TestParamInfo<SemaphoreTestParams>& info) {
      std::string controllers =
          info.param.count_controllers == -1
              ? "Max"
              : absl::StrCat(info.param.count_controllers);
      return absl::StrCat("Limit", info.param.throttle_limit, "_Tasks",
                          info.param.count_tasks, "_Controllers", controllers,
                          info.param.test_oscillation ? "_Oscillating" : "");
    });

// Helper thread for OverflowAcquireTest().
class OverflowAcquireTestThread : public Thread {
 public:
  explicit OverflowAcquireTestThread(SimpleWeightedSemaphore* sem) : sem_(sem) {
    CHECK_NE(sem_, nullptr);
    SetJoinable(true);
  }

 protected:
  void Run() override {
    absl::SleepFor(
        absl::Seconds(2));  // Give the Acquire() call a chance to run first.
    sem_->Release(1);
  }

 private:
  SimpleWeightedSemaphore* sem_;
};

// There used to be an integer overflow bug in
// SimpleWeightedSemaphore::Acquire() where if pending_cost_ + cost would
// overflow a uint64, we would allow the acquisition to happen, but then CHECK
// and crash the program.  Ensure that we've fixed this bug.
TEST(SimpleWeightedSemaphoreTest, OverflowAcquire) {
  LOG(INFO) << "Starting test: Overflow Acquire.";
  SimpleWeightedSemaphore sem(std::numeric_limits<uint64_t>::max());
  ASSERT_TRUE(sem.Acquire(std::numeric_limits<uint64_t>::max()));
  OverflowAcquireTestThread thread(&sem);
  thread.Start();
  // We won't be able to Acquire() until |thread| has called Release().
  EXPECT_TRUE(sem.Acquire(1));
  thread.Join();
  sem.Release(std::numeric_limits<uint64_t>::max());
  LOG(INFO) << "Finishing test: Overflow Acquire.";
}

ABSL_CONST_INIT static absl::Mutex always_acquire_mutex_(absl::kConstInit);

void TryToAcquireAlways(SimpleWeightedSemaphore* s, bool* started) {
  {
    absl::MutexLock l(always_acquire_mutex_);
    *started = true;
    EXPECT_TRUE(s->stopped());
  }
  s->AcquireAlways(1);
  EXPECT_FALSE(s->stopped());
  s->Release(1);
}

TEST(SimpleWeightedSemaphoreTest, AcquireAlways) {
  SimpleWeightedSemaphore s(10);
  s.Stop();

  bool thread_started = false;

  thread::Options options;
  options.set_joinable(true);
  ClosureThread ct(options, "AcquireAlwaysThread",
                   absl::bind_front(TryToAcquireAlways, &s, &thread_started));

  absl::MutexLock lock(always_acquire_mutex_);
  ct.Start();
  always_acquire_mutex_.Await(absl::Condition(&thread_started));

  // Now, the other thread should be blocked, start the semaphore
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

}  // namespace
