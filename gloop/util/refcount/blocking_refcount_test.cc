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

#include "gloop/util/refcount/blocking_refcount.h"

#include <cstdint>
#include <memory>
#include <thread>  // NOLINT (for std::hardware_concurrency())
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/casts.h"
#include "absl/base/const_init.h"
#include "absl/base/thread_annotations.h"
#include "absl/functional/bind_front.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/atomic_stats_counter.h"
#include "gloop/base/walltime.h"
#include "gloop/gloop_test.h"
#include "gloop/thread/threadpool.h"

namespace util {
namespace {

using base::StatsCounter;

// Test that BlockingRefcount can be constexpr-initialized.
ABSL_CONST_INIT BlockingRefcount kStaticRefcount(absl::kConstInit);

class BlockingRefcountTest : public testing::Test {
 protected:
  BlockingRefcountTest() : pool_(20) {}

  ThreadPool pool_;
  BlockingRefcount counter_;
};

void BasicWorker(BlockingRefcount* counter) { counter->Dec(); }

TEST_F(BlockingRefcountTest, LocalCounterBasicUsageTest) {
  // Tests that the recommended usage works, including deletion.
  // We don't use the counter in BlockingRefcountTest.
  // Run this at least 100 times (--nocache_test_results --runs_per_test=50).
  const int num_iterations = 50;
  for (int i = 0; i < num_iterations; ++i) {
    BlockingRefcount* counter = new BlockingRefcount;
    const int num_tasks = 20;
    for (int t = 0; t < num_tasks; ++t) {
      counter->Inc();
      pool_.Schedule([counter] { BasicWorker(counter); });
    }
    counter->WaitForZero();
    delete counter;
  }
}

TEST_F(BlockingRefcountTest, GlobalCounterBasicUsageTest) {
  // Tests the statically-initialized refcount.
  // Tests that the recommended usage works, including deletion.
  // We don't use the counter in BlockingRefcountTest.
  // Run this at least 100 times (--nocache_test_results --runs_per_test=50).
  const int num_iterations = 50;
  for (int i = 0; i < num_iterations; ++i) {
    BlockingRefcount* counter = &kStaticRefcount;
    const int num_tasks = 20;
    for (int t = 0; t < num_tasks; ++t) {
      counter->Inc();
      pool_.Schedule([counter] { BasicWorker(counter); });
    }
    counter->WaitForZero();
  }
}

struct TaskControls {
  explicit TaskControls(int num_tasks) : active_(num_tasks), done_(num_tasks) {}

  // These control the activity's progress
  absl::Notification finish_;

  // These mark milestones of the tasks
  absl::BlockingCounter active_;
  absl::BlockingCounter done_;

  // Needed because BlockingCounter doesn't provide its current count.
  StatsCounter active_count_;
};

void Activity(BlockingRefcount* counter, TaskControls* controls) {
  controls->active_count_.Add(1);
  controls->active_.DecrementCount();
  controls->finish_.WaitForNotification();
  counter->DecN(1);
  controls->active_count_.Add(-1);
  controls->done_.DecrementCount();
}

void Master(const BlockingRefcount* counter, absl::Notification* started,
            absl::Notification* done) {
  started->Notify();
  counter->WaitForZero();
  done->Notify();
}

TEST_F(BlockingRefcountTest, BlockingRefcountCounts) {
  EXPECT_EQ(0, counter_.count());

  const int num_tasks = 5;
  TaskControls activity_controls(num_tasks);
  for (int i = 0; i < num_tasks; ++i) {
    // Normally, users would increment the reference count before
    // queuing a task.
    counter_.Inc();
    pool_.Schedule(absl::bind_front(Activity, &counter_, &activity_controls));
  }
  activity_controls.active_.Wait();

  // Now all of the tasks are outstanding in the counter.
  EXPECT_EQ(num_tasks, counter_.count());
  EXPECT_EQ(num_tasks, activity_controls.active_count_.value());

  // Spawn the Master to block on the tasks.
  absl::Notification master_started, master_done;
  pool_.Schedule(absl::bind_front(
      Master, absl::implicit_cast<const BlockingRefcount*>(&counter_),
      &master_started, &master_done));
  master_started.WaitForNotification();

  // Master should be blocked on WaitForZero now.
  EXPECT_FALSE(
      master_done.WaitForNotificationWithTimeout(absl::Milliseconds(2) /*ms*/));

  // A second Master should be able to wait as well.
  absl::Notification master2_started, master2_done;
  pool_.Schedule(absl::bind_front(
      Master, absl::implicit_cast<const BlockingRefcount*>(&counter_),
      &master2_started, &master2_done));
  master2_started.WaitForNotification();
  EXPECT_FALSE(master2_done.WaitForNotificationWithTimeout(
      absl::Milliseconds(2) /*ms*/));

  // Some tasks finishing should not be sufficient to let the Masters
  // proceed.
  counter_.DecN(num_tasks - 1);
  EXPECT_FALSE(
      master_done.WaitForNotificationWithTimeout(absl::Milliseconds(2) /*ms*/));
  EXPECT_FALSE(master2_done.HasBeenNotified());
  counter_.IncN(num_tasks - 1);

  // Allowing the outstanding tasks to complete should enable the
  // Masters to proceed.
  activity_controls.finish_.Notify();
  activity_controls.done_.Wait();
  master_done.WaitForNotification();
  master2_done.WaitForNotification();
  EXPECT_EQ(0, counter_.count());
}

void ActivityWithReference(BlockingRefcountReference ref,
                           absl::BlockingCounter* started,
                           absl::Notification* finish) {
  started->DecrementCount();
  finish->WaitForNotification();
}

TEST_F(BlockingRefcountTest, BlockingRefcountReferenceWorks) {
  EXPECT_EQ(0, counter_.count());

  const int num_tasks = 5;
  absl::Notification tasks_finish;
  {
    absl::BlockingCounter started(num_tasks);
    // Create initial reference
    BlockingRefcountReference ref(&counter_);
    EXPECT_EQ(1, counter_.count());
    for (int i = 0; i < num_tasks; ++i) {
      // Normally, users would increment the reference count before
      // queuing a task.  Copying the reference does that.
      pool_.Schedule([ref, &started, &tasks_finish] {
        ActivityWithReference(ref, &started, &tasks_finish);
      });
    }
    started.Wait();
  }
  absl::SleepFor(absl::Milliseconds(2));

  // Now all of the tasks are outstanding in the counter.  Creating a
  // closure creates 2 copies of its arguments.
  EXPECT_EQ(2 * num_tasks, counter_.count());

  // Spawn the Master to block on the tasks.
  absl::Notification master_started, master_done;
  pool_.Schedule(absl::bind_front(
      Master, absl::implicit_cast<const BlockingRefcount*>(&counter_),
      &master_started, &master_done));
  master_started.WaitForNotification();

  // Master should be blocked on WaitForZero now.
  EXPECT_FALSE(master_done.HasBeenNotified());

  // Allowing the outstanding tasks to complete should enable the
  // Master to proceed.
  tasks_finish.Notify();
  master_done.WaitForNotification();
  EXPECT_EQ(0, counter_.count());
}

TEST_F(BlockingRefcountTest, BlockingRefcountReferenceMoveConstructible) {
  EXPECT_EQ(0, counter_.count());

  {
    BlockingRefcountReference ref1(&counter_);
    EXPECT_EQ(1, counter_.count());

    BlockingRefcountReference ref2(std::move(ref1));
    EXPECT_EQ(1, counter_.count());
  }

  EXPECT_EQ(0, counter_.count());
}

TEST_F(BlockingRefcountTest, BlockingRefcountReferenceMoveAssignable) {
  EXPECT_EQ(0, counter_.count());

  {
    BlockingRefcountReference ref1(&counter_);
    EXPECT_EQ(1, counter_.count());

    BlockingRefcountReference ref2(&counter_);
    EXPECT_EQ(2, counter_.count());

    ref2 = std::move(ref1);
    EXPECT_EQ(1, counter_.count());
  }

  EXPECT_EQ(0, counter_.count());
}

TEST_F(BlockingRefcountTest, TestBRReferenceAssignment) {
  BlockingRefcount counter, counter2;
  BlockingRefcountReference r1(&counter);
  BlockingRefcountReference r2(&counter2);
  EXPECT_EQ(1, counter.count());
  EXPECT_EQ(1, counter2.count());
  r1 = r2;
  EXPECT_EQ(0, counter.count());
  EXPECT_EQ(2, counter2.count());
}

TEST_F(BlockingRefcountTest, TestSwap) {
  BlockingRefcount counter, counter2;
  counter.IncN(5);
  counter2.IncN(10);
  std::unique_ptr<BlockingRefcountReference> r1 =
      std::make_unique<BlockingRefcountReference>(&counter);
  std::unique_ptr<BlockingRefcountReference> r2 =
      std::make_unique<BlockingRefcountReference>(&counter2);
  EXPECT_EQ(6, counter.count());
  EXPECT_EQ(11, counter2.count());
  r1->swap(*r2);
  // Underlying counters didn't change, just the reference objects.
  EXPECT_EQ(6, counter.count());
  EXPECT_EQ(11, counter2.count());
  r1.reset();
  EXPECT_EQ(6, counter.count());
  EXPECT_EQ(10, counter2.count());
  r2.reset();
  EXPECT_EQ(5, counter.count());
  EXPECT_EQ(10, counter2.count());
}

void SimpleActivity(BlockingRefcount* counter, absl::BlockingCounter* done,
                    int sleep_ms, int trials) {
  for (int i = 0; i < trials; ++i) {
    BlockingRefcountReference r(counter);
    absl::SleepFor(absl::Milliseconds(sleep_ms));
  }
  done->DecrementCount();
}

void SimpleMaster(const BlockingRefcount* counter, absl::BlockingCounter* done,
                  int sleep_ms, int trials) {
  for (int i = 0; i < trials; ++i) {
    absl::SleepFor(absl::Milliseconds(sleep_ms));
    counter->WaitForZero();
  }
  done->DecrementCount();
}

// Goal of this test is to not crash, not deadlock, and end in the expected
// state.
TEST_F(BlockingRefcountTest, StressTest) {
  const int num_tasks = 100;
  absl::BlockingCounter activity_done(num_tasks * 3 / 4);
  absl::BlockingCounter master_done(num_tasks / 4);
  for (int i = 0; i < num_tasks; ++i) {
    if ((i % 4) != 0) {
      pool_.Schedule(absl::bind_front(SimpleActivity, &counter_, &activity_done,
                                      // Create a little variance in behavior.
                                      i % 4, 5 + (i % 2)));
    } else {
      pool_.Schedule(absl::bind_front(
          SimpleMaster, absl::implicit_cast<const BlockingRefcount*>(&counter_),
          &master_done,
          // Create a little variance in behavior.
          4 - (i % 3), 4 + ((i / 2) % 3)));
    }
  }
  activity_done.Wait();
  counter_.WaitForZero();
  EXPECT_EQ(0, counter_.count());
  // Make sure subsequent calls to WaitForZero do not block forever.
  counter_.WaitForZero();
  master_done.Wait();
}

// This test exercises the case when the count reaches zero ephemerally while
// there are active waiters. The goal of the test is not to deadlock.
TEST_F(BlockingRefcountTest, EphemeralZeroes) {
  const int num_tasks = 100;
  const int iters = 100000;
  BlockingRefcount stop_barrier;
  stop_barrier.IncN(num_tasks);
  for (int i = 0; i < num_tasks; ++i) {
    pool_.Schedule([this, &stop_barrier] {
      for (int i = 0; i < iters; ++i) {
        counter_.Inc();
        counter_.Dec();
        // Yield
        absl::SleepFor(absl::ZeroDuration());
      }
      stop_barrier.Dec();
    });
  }

  absl::Duration test_duration = absl::Seconds(5);
  absl::Time start_time = absl::Now();

  while (test_duration > absl::Now() - start_time) {
    counter_.WaitForZeroWithTimeout(absl::Seconds(1));
  }

  stop_barrier.WaitForZero();
  EXPECT_TRUE(counter_.WaitForZeroWithTimeout(absl::Microseconds(1)));
}

// This tests that WaitForZero can be re-entered after a previous call failed
// with a timeout.
TEST_F(BlockingRefcountTest, WaitReEntrancy) {
  counter_.Inc();
  EXPECT_FALSE(counter_.WaitForZeroWithTimeout(absl::Microseconds(1)));
  EXPECT_FALSE(counter_.WaitForZeroWithTimeout(absl::Microseconds(1)));
  counter_.Dec();
  EXPECT_TRUE(counter_.WaitForZeroWithTimeout(absl::Microseconds(1)));
}

void MasterWithTimeout(const BlockingRefcount* counter,
                       absl::Notification* started, absl::Notification* done) {
  started->Notify();
  // Use an absurdly large timeout to ensure we will wake up when the counter
  // is decremented, and not by a timeout.  With a timeout of 10, this thread
  // sometimes succeeds in waiting for 10ms before the caller can wait for 2ms.
  // For me, I saw 1 failure of this form in 10,000 trials at a timeout of 10.
  EXPECT_TRUE(counter->WaitForZeroWithTimeout(absl::Milliseconds(10000)));
  done->Notify();
}

TEST_F(BlockingRefcountTest, WaitWithTimeoutWorksWhenNoTimeout) {
  counter_.Inc();
  absl::Notification started, done;
  pool_.Schedule(
      absl::bind_front(MasterWithTimeout,
                       absl::implicit_cast<const BlockingRefcount*>(&counter_),
                       &started, &done));
  started.WaitForNotification();
  EXPECT_FALSE(
      done.WaitForNotificationWithTimeout(absl::Milliseconds(2) /*ms*/));
  counter_.Dec();
  done.WaitForNotification();
}

TEST_F(BlockingRefcountTest, WaitWithTimeoutWorksWhenTimeout) {
  counter_.Inc();
  WallTime start_time = base::ToWallTime(absl::Now());
  EXPECT_FALSE(counter_.WaitForZeroWithTimeout(absl::Milliseconds(10)));
  // Only 9ms to handle clock jitter.
  EXPECT_LT(start_time + 0.009, base::ToWallTime(absl::Now()));
}

TEST_F(BlockingRefcountTest, WaitWithTimeoutInDurationWorksWhenTimeout) {
  counter_.Inc();
  const absl::Duration timeout = absl::Milliseconds(10);
  const absl::Time start = absl::Now();
  EXPECT_FALSE(counter_.WaitForZeroWithTimeout(timeout));
  const absl::Duration jitter = absl::Milliseconds(1);
  EXPECT_LT(timeout - jitter, absl::Now() - start);
}

TEST_F(BlockingRefcountTest, WaitWithDeadlineWorksWhenTimeout) {
  counter_.Inc();
  const absl::Duration timeout = absl::Milliseconds(10);
  const absl::Time start = absl::Now();
  EXPECT_FALSE(counter_.WaitForZeroWithDeadline(start + timeout));
  const absl::Duration jitter = absl::Milliseconds(1);
  EXPECT_LT(timeout - jitter, absl::Now() - start);
}

// Define a simple blocking reference counter using mutexes to provide a base
// implementation against which to compare other implementations.
//
// For benchmarking we need minimal functionality.
//
// TODO: Unittest this base implementation.

namespace simple {

class BlockingRefcount {
 public:
  BlockingRefcount() : count_(0) {}
  ~BlockingRefcount() = default;

  void Inc() {
    absl::MutexLock ml(mu_);
    count_ += 1;
  }

  void Dec() {
    absl::MutexLock ml(mu_);
    count_ -= 1;
  }

  void WaitForZero() const {
    absl::MutexLock ml(mu_);
    mu_.Await(absl::Condition(this, &BlockingRefcount::is_zero));
  }

 private:
  bool is_zero() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    return count_ == 0;
  }

  mutable absl::Mutex mu_;
  int64_t count_ ABSL_GUARDED_BY(mu_);
};

}  // namespace simple

// Measure the raw performance of the underlying primitives.

// Measure increment time.
template <class Q>
void BM_Inc(benchmark::State& state) {
  Q counter;
  for (auto s : state) {
    counter.Inc();
  }
}
BENCHMARK_TEMPLATE(BM_Inc, BlockingRefcount);
BENCHMARK_TEMPLATE(BM_Inc, simple::BlockingRefcount);

// Measure decrement time, when not decrementing to zero.
template <class Q>
void BM_DecToNonZero(benchmark::State& state) {
  Q counter;
  for (int i = 0; i < state.max_iterations; i++) {
    counter.Inc();
  }
  // An extra inc so we don't measure time to dec to zero.
  counter.Inc();
  for (auto s : state) {
    counter.Dec();
  }
}
BENCHMARK_TEMPLATE(BM_DecToNonZero, BlockingRefcount);
BENCHMARK_TEMPLATE(BM_DecToNonZero, simple::BlockingRefcount);

// Benchmark a simple loop of Inc and Dec to zero.
// Subtract off the Inc time to get DecToZero time.
template <class Q>
void BM_IncDecToZero(benchmark::State& state) {
  Q counter;
  for (auto s : state) {
    counter.Inc();
    counter.Dec();
  }
}
BENCHMARK_TEMPLATE(BM_IncDecToZero, BlockingRefcount);
BENCHMARK_TEMPLATE(BM_IncDecToZero, simple::BlockingRefcount);

// Benchmark a simple loop of Wait.
template <class Q>
void BM_Wait(benchmark::State& state) {
  Q counter;
  for (auto s : state) {
    counter.WaitForZero();
  }
}
BENCHMARK_TEMPLATE(BM_Wait, BlockingRefcount);
BENCHMARK_TEMPLATE(BM_Wait, simple::BlockingRefcount);

template <class Q>
void BM_Contention(benchmark::State& state) {
  const int threads = std::thread::hardware_concurrency();

  Q refcount;
  BlockingRefcount start_barrier;
  start_barrier.IncN(threads);
  BlockingRefcount stop_barrier;
  stop_barrier.IncN(threads);

  const auto& worker = [&] {
    start_barrier.Dec();
    start_barrier.WaitForZero();

    for (int n = 0; n < state.max_iterations; ++n) {
      refcount.Inc();
      refcount.Dec();
    }

    stop_barrier.Dec();
  };

  refcount.Inc();

  // Run a batch of `max_iterations` Inc()/Dec() calls in parallel on enough
  // threads to saturate the hardware.
  while (state.KeepRunningBatch(state.max_iterations)) {
    ThreadPool pool(threads);

    for (int i = 0; i < threads; ++i) {
      pool.Schedule(worker);
    }
  }

  stop_barrier.WaitForZero();

  refcount.Dec();
  refcount.WaitForZero();
}
BENCHMARK_TEMPLATE(BM_Contention, BlockingRefcount);
BENCHMARK_TEMPLATE(BM_Contention, simple::BlockingRefcount);

}  // namespace
}  // namespace util
