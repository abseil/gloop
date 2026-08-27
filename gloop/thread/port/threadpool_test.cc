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

#include "gloop/thread/threadpool.h"

#include <cstdint>
#include <functional>
#include <memory>

#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/synchronization/barrier.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/timer.h"
#include "gtest/gtest.h"

namespace {
class SimpleGuardedInteger {
 public:
  explicit SimpleGuardedInteger(int initial_value) : value_(initial_value) {}
  SimpleGuardedInteger(const SimpleGuardedInteger&) = delete;
  SimpleGuardedInteger& operator=(const SimpleGuardedInteger&) = delete;

  void Decrement() {
    absl::MutexLock l(mutex_);
    CHECK_GE(value_, 1);
    --value_;
    changed_.SignalAll();
  }

  void Increment() {
    absl::MutexLock l(mutex_);
    ++value_;
    changed_.SignalAll();
  }

  int Value() {
    absl::MutexLock l(mutex_);
    return value_;
  }

  void WaitForZero() {
    absl::MutexLock l(mutex_);
    while (value_ != 0) {
      changed_.Wait(&mutex_);
    }
  }

 private:
  absl::Mutex mutex_;
  absl::CondVar changed_;
  int value_ ABSL_GUARDED_BY(mutex_);
};

bool ScheduleIfReadyToRunByDeadline(absl::Time deadline, ThreadPool* pool,
                                    std::function<void()> callback) {
  while (!pool->ScheduleIfReadyToRun(callback)) {
    if (absl::Now() > deadline) {
      return false;
    }
    // TODO sleep for ~1ms here once absl::SleepFor is available.
  }
  return true;
}

// Loops for |milliseconds| of wall-clock time.
static void LoopForMs(int64_t milliseconds) {
  WallTimer timer;
  timer.Start();
  while (timer.GetInMs() < milliseconds) {
  }
}

// A function that increments the given integer.
void IncrementIntegerJob(SimpleGuardedInteger* value) {
  LoopForMs(100);
  value->Increment();
}

TEST(ThreadPoolTest, ThreadedIntegerIncrement) {
  std::unique_ptr<ThreadPool> thread_pool(
      new ThreadPool(100, {"test_thread_pool"}));
  EXPECT_EQ(thread_pool->num_threads(), 100);
  SimpleGuardedInteger count(0);
  for (int i = 0; i < 1000; ++i) {
    if (i % 2 == 0) {
      thread_pool->Schedule([&count] { IncrementIntegerJob(&count); });
    } else {
      thread_pool->Schedule([&count]() { IncrementIntegerJob(&count); });
    }
  }
  thread_pool.reset(nullptr);
  EXPECT_EQ(count.Value(), 1000);
}

TEST(ThreadPoolTest, DestroyWithoutStart) {
  std::unique_ptr<ThreadPool> thread_pool(new ThreadPool(100));
  thread_pool.reset(nullptr);
}

TEST(ThreadPoolTest, ScheduleIfReadyToRunTest) {
  constexpr int kNumThreads = 4;
  std::unique_ptr<ThreadPool> thread_pool(new ThreadPool(kNumThreads));
  // Force spawning all threads.
  absl::Barrier barrier(kNumThreads + 1);
  for (int i = 0; i < kNumThreads; ++i) {
    thread_pool->Schedule([&barrier] { barrier.Block(); });
  }
  barrier.Block();

  // Allow an empirically determined grace period before we consider false
  // from ScheduleIfReadyToRun a test failure.  The StartWorkers() call is
  // not required to make all threads immediately available.
  const absl::Duration kGracePeriod = absl::Milliseconds(200);
  SCOPED_TRACE(testing::Message()
               << "Using a " << kGracePeriod << " grace period.");

  // These counters are used as barriers to synchronize the return of the
  // 'work' closures below.  With them we can deterministically fill the
  // pool with kNumThreads running closures.
  SimpleGuardedInteger work_stop(1);
  SimpleGuardedInteger work_running(kNumThreads);
  {
    absl::Time deadline = absl::Now() + kGracePeriod;
    auto work = [&work_stop, &work_running] {
      work_running.Decrement();
      work_running.WaitForZero();
      work_stop.WaitForZero();
    };
    for (int k = 0; k < kNumThreads; ++k) {
      if (!ScheduleIfReadyToRunByDeadline(deadline, thread_pool.get(), work)) {
        ADD_FAILURE() << "Could not schedule within deadline.";
        // The work wasn't scheduled.  Decrement the barrier to avoid a
        // test timeout.
        work_running.Decrement();
      }
    }
    work_running.WaitForZero();
  }

  // The all threads in the pool are now running.
  EXPECT_FALSE(thread_pool->ScheduleIfReadyToRun([] {}));

  // Unblock the work closures.
  work_stop.Decrement();

  // Workers are unblocked so scheduling should be possible "soon".
  EXPECT_TRUE(ScheduleIfReadyToRunByDeadline(absl::Now() + kGracePeriod,
                                             thread_pool.get(), [] {}));

  // Delete the thread pool explicitly to ensure that all closures that might
  // reference stack variables are done.
  thread_pool = nullptr;
}

// If num_threads is 0, ThreadPool should not CHECK-fail or crash.
TEST(ThreadPoolTest, NumThreadsZero) { ThreadPool pool(0); }

// If num_threads is 1, the closures are run in FIFO order.
TEST(ThreadPoolTest, OneThreadRunsClosuresFIFO) {
  int count = 0;  // Declare first so that it outlives the thread pool.
  ThreadPool pool(1);
  EXPECT_EQ(pool.num_threads(), 1);
  for (int i = 0; i < 1000; ++i) {
    pool.Schedule([&count, i]() {
      EXPECT_EQ(count, i);
      count++;
    });
  }
}

}  // namespace
