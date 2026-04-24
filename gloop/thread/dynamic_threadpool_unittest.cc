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

#include "gloop/thread/dynamic_threadpool.h"

#include <stdio.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>

#include "absl/base/log_severity.h"
#include "absl/functional/bind_front.h"
#include "absl/log/globals.h"
#include "absl/log/log.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/atomic_stats_counter.h"
#include "gloop/thread/executor.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/threadpool.h"
#include "gtest/gtest.h"

namespace {

class DynamicThreadPoolTestSuite : public ::testing::Test {
 public:
  static void SetUpTestSuite() {
    absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  }
};

class DynamicThreadPoolTest {
 public:
  DynamicThreadPoolTest() : calls_(0) {}

  void DelayedCall(absl::Duration delay, DynamicThreadPool* my_pool) {
    ASSERT_EQ(thread::Executor::CurrentExecutor(),
              static_cast<AbstractThreadPool*>(my_pool));
    // test CurrentExecutor()
    ::absl::SleepFor(delay);
    absl::MutexLock lock(mutex_);
    ++calls_;
  }

  int calls() const { return calls_; }

  std::function<void()> NewDelayedCall(absl::Duration delay,
                                       DynamicThreadPool* dynamic_pool) {
    return absl::bind_front(&DynamicThreadPoolTest::DelayedCall, this, delay,
                            dynamic_pool);
  }

 private:
  absl::Mutex mutex_;
  int calls_;
};

TEST_F(DynamicThreadPoolTestSuite, NotStarted) {
  DynamicThreadPool tp(1, 5, 5, 0);
}

TEST_F(DynamicThreadPoolTestSuite, WaitForAllThreads) {
  DynamicThreadPoolTest test;

  {
    DynamicThreadPool tp(1, 5, 5, 0);
    for (int i = 0; i < 5; i++) {
      ASSERT_TRUE(tp.ScheduleIfReadyToRun(absl::bind_front(
          &DynamicThreadPoolTest::DelayedCall, &test, absl::Seconds(1), &tp)));
    }
  }

  // The destructor should have waited for all threads to terminate
  ASSERT_EQ(test.calls(), 5);
}

// Callback to AddAfter.
static void ValidateAddAfter(int64_t run_time, base::StatsCounter* ctr,
                             absl::Notification* notification) {
  // Callback must've been executed in expected thread.
  const LiveThread* th = Thread_GetMyLiveThread();
  ASSERT_NE(th, nullptr);
  const char* thread_prefix = "DynamicThreadPoolTest";
  ASSERT_EQ(strncmp(LiveThread_Name(th), thread_prefix, strlen(thread_prefix)),
            0)
      << "Callback executed in an unknown thread, expected thread prefix "
      << thread_prefix << ", found " << LiveThread_Name(th);

  int64_t now = absl::ToUnixMillis(absl::Now());
  ASSERT_GE(now, run_time) << "Callback invoked too early";
  ctr->Add(1);
  notification->Notify();
}

TEST_F(DynamicThreadPoolTestSuite, AddAfter) {
  DynamicThreadPoolTest test;
  DynamicThreadPool::Options options;
  options.queue_capacity = 1;
  options.min_threads = 1;
  options.max_threads = 1;
  options.max_idle_ms = 2000;
  DynamicThreadPool tp("DynamicThreadPoolTest", options);

  int ms = 123;
  base::StatsCounter ctr;
  absl::Notification n1;
  absl::Notification n2;

  tp.ScheduleAt(
      absl::Now() + absl::Milliseconds(ms),
      absl::bind_front(&ValidateAddAfter,
                       absl::ToUnixMillis(absl::Now() + absl::Milliseconds(ms)),
                       &ctr, &n1));

  // Make thread busy, queue full and test EnqueueForce.
  ASSERT_TRUE(tp.TrySchedule(test.NewDelayedCall(absl::Milliseconds(30), &tp)));
  ASSERT_TRUE(tp.TrySchedule(test.NewDelayedCall(absl::Milliseconds(30), &tp)));
  tp.ScheduleAt(
      absl::Now() + absl::ZeroDuration(),
      absl::bind_front(&ValidateAddAfter,
                       absl::ToUnixMillis(absl::Now() + absl::Milliseconds(30)),
                       &ctr, &n2));

  // Expect callbacks to be executed no more than 100ms past given deadline.
  absl::Time deadline = absl::Now() + absl::Milliseconds(ms + 100);
  ASSERT_TRUE(n1.WaitForNotificationWithDeadline(deadline))
      << "Callback 1 didn't execute in time";
  ASSERT_TRUE(n2.WaitForNotificationWithDeadline(deadline))
      << "Callback 2 didn't execute in time";

  ASSERT_EQ(ctr.value(), 2);
}

TEST_F(DynamicThreadPoolTestSuite, Queuing) {
  DynamicThreadPoolTest test;

  ASSERT_EQ(thread::Executor::CurrentExecutor(), nullptr);

  {
    DynamicThreadPool tp(5, 5, 10, 2000);

    // Use up our threads
    for (int i = 0; i < 10; i++) {
      ASSERT_TRUE(
          tp.ScheduleIfReadyToRun(test.NewDelayedCall(absl::Seconds(3), &tp)));
    }
    ASSERT_EQ(tp.num_threads(), 10);

    // We shouldn't be able to add more
    std::function<void()> cb = test.NewDelayedCall(absl::Seconds(3), &tp);
    ASSERT_TRUE(!tp.ScheduleIfReadyToRun(cb));

    // but we should be able to add them using TrySchedule, since that can queue
    for (int i = 0; i < 5; i++) {
      ASSERT_TRUE(tp.TrySchedule(test.NewDelayedCall(absl::Seconds(3), &tp)));
    }
    ASSERT_EQ(tp.num_threads(), 10);

    // now TrySchedule must fail
    cb = test.NewDelayedCall(absl::Seconds(3), &tp);
    ASSERT_TRUE(!tp.TrySchedule(cb));

    // but we can add (blocking)
    tp.Schedule(test.NewDelayedCall(absl::Seconds(3), &tp));

    // Check that the number of threads goes down to min_threads after they're
    // all idle for a while
    ::absl::SleepFor(::absl::Milliseconds(6000));
    ASSERT_EQ(tp.num_threads(), 5);
  }

  ASSERT_EQ(test.calls(), 16);
}

TEST_F(DynamicThreadPoolTestSuite, IncrementMaxThreads) {
  DynamicThreadPoolTest test;

  ASSERT_EQ(thread::Executor::CurrentExecutor(), nullptr);

  {
    // Queue size = 1, min_threads = max_threads = 5.
    DynamicThreadPool tp(1, 5, 5, 2000);

    // Use up our threads
    for (int i = 0; i < 5; i++) {
      ASSERT_TRUE(
          tp.ScheduleIfReadyToRun(test.NewDelayedCall(absl::Seconds(3), &tp)));
    }
    ASSERT_EQ(5, tp.num_threads());
    ASSERT_EQ(0, tp.queue_count());

    // Another task should be queued.
    ASSERT_TRUE(tp.TrySchedule(test.NewDelayedCall(absl::Seconds(3), &tp)));
    ASSERT_EQ(1, tp.queue_count());

    // Increment max.
    tp.IncrementMaxThreads();
    ASSERT_EQ(tp.num_threads(), 6);

    // This should allow the task to run. Wait a moment before
    // checking.
    ::absl::SleepFor(::absl::Milliseconds(100));
    ASSERT_EQ(0, tp.queue_count());

    // Increment max, this time with nothing in the queue, which won't
    // actually increment the max.
    tp.IncrementMaxThreads();
    ASSERT_EQ(tp.num_threads(), 6);

    // Add another task -- should run.
    ASSERT_TRUE(tp.TrySchedule(test.NewDelayedCall(absl::Seconds(3), &tp)));
    ASSERT_EQ(0, tp.queue_count());
    ASSERT_EQ(tp.num_threads(), 7);

    // Decrement, then increment. This causes an Add to queue.
    ASSERT_TRUE(tp.DecrementMaxThreads());
    ASSERT_EQ(tp.num_threads(), 7);
    tp.IncrementMaxThreads();
    ASSERT_EQ(tp.num_threads(), 7);
    ASSERT_TRUE(tp.TrySchedule(test.NewDelayedCall(absl::Seconds(3), &tp)));
    ASSERT_EQ(1, tp.queue_count());

    // Check that the number of threads goes down to min_threads after they're
    // all idle for a while
    ::absl::SleepFor(::absl::Milliseconds(6000));
    ASSERT_EQ(tp.num_threads(), 5);

    // Test that calling ShutDown() before destroying the class
    // doesn't lead to problems.
    tp.ShutDown();
  }

  ASSERT_EQ(test.calls(), 8);
}

void IncrementMaxThreads(int ms, DynamicThreadPool* p) {
  if (ms) ::absl::SleepFor(::absl::Milliseconds(ms));
  p->IncrementMaxThreads();
}

void Notify(int ms, absl::Notification* n) {
  if (ms) ::absl::SleepFor(::absl::Milliseconds(ms));
  n->Notify();
}

TEST_F(DynamicThreadPoolTestSuite, ThreadSafety) {
  // A test to make sure that the interface is reasonably thread safe, by
  // doing many operations inside our own ThreadPool, and therefore, making
  // concurrent calls into DynamicThreadPool.

  DynamicThreadPoolTest test;

  ASSERT_EQ(thread::Executor::CurrentExecutor(), nullptr);

  {
    ThreadPool test_pool(5);

    // A big pool, so that we can add lots of stuff to it.
    DynamicThreadPool tp(200, 25, 50, 2000);

    // Call IncrementMaxThreads multiple times.
    for (int i = 0; i < 50; i++) {
      if (i < 5) {
        // Call IncrementMaxThreads "soon" (no sleep)
        test_pool.Schedule([&tp] { IncrementMaxThreads(0, &tp); });
      } else {
        // Call IncrementMaxThreads "in a little bit"
        test_pool.Schedule(
            absl::bind_front(&IncrementMaxThreads, rand() % 1000, &tp));
      }
    }

    // At this point, we have queued up a bunch of calls to IncrementMaxThreads,
    // which will still drain out of the queue as we move on to the next part of
    // our test.

    // Fill up the queue with a bunch of sleepers
    for (int i = 0; i < 100; i++) {
      // We jump through hoops here to make sure that tp.Add() is thread
      // safe by pushing a bunch of those calls into our own threadpool.
      tp.Schedule(absl::bind_front(&DynamicThreadPoolTest::DelayedCall, &test,
                                   absl::Milliseconds(rand() % 1000), &tp));

      LOG(INFO) << "i=" << i << " Queue=" << tp.queue_count()
                << " threads=" << tp.num_threads();
    }

    absl::Notification n2;
    test_pool.Schedule([&n2] { Notify(0, &n2); });
    n2.WaitForNotification();

    ASSERT_GE(tp.num_threads(), 25);  // its unclear how many threads there
                                      // will really be, since they are
                                      // started dynamically, but there surely
                                      // will be at least 25 (the default).

    // Wait for the pool to settle down and the remaining threads to exit
    // and be collected.
    absl::Notification n3;
    test_pool.Schedule([&n3] { Notify(3000, &n3); });
    n3.WaitForNotification();

    ASSERT_EQ(tp.num_threads(), 25);

    // Test that calling ShutDown() before destroying the class
    // doesn't lead to problems.
    tp.ShutDown();
  }
}

TEST_F(DynamicThreadPoolTestSuite, DecrementMaxThreadsFailure) {
  DynamicThreadPoolTest test;

  ASSERT_EQ(thread::Executor::CurrentExecutor(), nullptr);

  {
    // Queue size = 1, min_threads = max_threads = 5.
    DynamicThreadPool tp(1, 5, 5, 2000);

    ASSERT_TRUE(!tp.DecrementMaxThreads());
    // Since the decrement failed we should be able to increment then
    // decrement.
    tp.IncrementMaxThreads();
    ASSERT_TRUE(tp.DecrementMaxThreads());
  }
}

void SlowCallback(absl::BlockingCounter* counter) {
  ::absl::SleepFor(::absl::Milliseconds(100));
  counter->DecrementCount();
}

TEST_F(DynamicThreadPoolTestSuite, AddAndAddAfterRace) {
  // The CHECK(queue_->empty()) in AddInternal() used to fail since AddAfter()
  // did not use AddInternal() and did not spawn a new thread if necessary.
  // So, AddAfter() could have added something to queue_ even if
  // thread_->size() < max_threads_.
  DynamicThreadPool tp(5, 1, 5, 1000);

  absl::BlockingCounter counter(3);

  tp.Schedule([&] { SlowCallback(&counter); });
  tp.ScheduleAt(absl::Now() + absl::Milliseconds(50),
                [&counter] { SlowCallback(&counter); });
  ::absl::SleepFor(::absl::Milliseconds(50));
  tp.Schedule([&] { SlowCallback(&counter); });

  // Make sure all the scheduled threads have run before we start destroying the
  // threadpool. Otherwise we can get race conditions where the AddAfter task is
  // run after the pool has been destroyed.
  counter.Wait();
}

}  // unnamed namespace
