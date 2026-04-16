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

#include "gloop/concurrent/executors/loop_executor.h"

#include <memory>

#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/atomic_sequence_num.h"
#include "gloop/base/callback.h"
#include "gloop/thread/executor.h"
#include "gloop/thread/threadpool.h"
#include "gloop/util/functional/from_callback.h"
#include "gloop/util/functional/to_callback.h"
#include "gtest/gtest.h"

namespace {

using base::SequenceNumber;
using concurrent::LoopExecutor;

void ExpectNotCalled() { ADD_FAILURE() << "Shouldn't be called."; }

TEST(LoopExecutorTest, LoopExitsImmediately) {
  LoopExecutor looper;
  looper.MakeLoopExit();
  looper.Loop();
}

void LoopAndSignal(absl::Notification* started, LoopExecutor* looper,
                   absl::Notification* done) {
  started->Notify();
  looper->Loop();
  done->Notify();
}
TEST(LoopExecutorTest, LoopExitsAfterLooping) {
  LoopExecutor looper;
  ThreadPool pool(1);

  absl::Notification started, done;
  pool.Schedule(
      [&started, &looper, &done] { LoopAndSignal(&started, &looper, &done); });
  started.WaitForNotification();
  // Give the LoopExecutor time to exit early from Loop(), if it's
  // going to.
  EXPECT_FALSE(
      done.WaitForNotificationWithTimeout(absl::Milliseconds(10) /*ms*/));
  looper.MakeLoopExit();
  done.WaitForNotification();
}

TEST(LoopExecutorTest, TryRunOneWithNoClosures) {
  LoopExecutor looper;
  EXPECT_FALSE(looper.TryRunOneClosure());
}

void FlipToTrue(bool* b) {
  EXPECT_FALSE(*b);
  *b = true;
}

TEST(LoopExecutorTest, TryRunOneWithTwoClosures) {
  LoopExecutor looper;
  bool first_called = false;
  bool second_called = false;
  looper.Schedule([&first_called] { FlipToTrue(&first_called); });
  looper.Schedule([&second_called] { FlipToTrue(&second_called); });
  EXPECT_FALSE(first_called);
  EXPECT_FALSE(second_called);
  EXPECT_TRUE(looper.TryRunOneClosure());
  EXPECT_TRUE(first_called);
  EXPECT_FALSE(second_called);
  EXPECT_TRUE(looper.TryRunOneClosure());
  EXPECT_TRUE(first_called);
  EXPECT_TRUE(second_called);
  EXPECT_FALSE(looper.TryRunOneClosure());
}

void SetAndEnqueue(LoopExecutor* looper, bool* first, bool* second) {
  *first = true;
  looper->Schedule([second] { FlipToTrue(second); });
}

TEST(LoopExecutorTest, RunQueued) {
  LoopExecutor looper;
  bool first_called = false;
  bool second_called = false;
  bool third_called = false;
  looper.Schedule([&looper, &first_called, &third_called] {
    SetAndEnqueue(&looper, &first_called, &third_called);
  });
  looper.Schedule([&second_called] { FlipToTrue(&second_called); });
  EXPECT_FALSE(first_called);
  EXPECT_FALSE(second_called);
  EXPECT_FALSE(third_called);
  looper.RunQueuedClosures();
  EXPECT_TRUE(first_called);
  EXPECT_TRUE(second_called);
  EXPECT_FALSE(third_called);
  looper.RunQueuedClosures();
  EXPECT_TRUE(first_called);
  EXPECT_TRUE(second_called);
  EXPECT_TRUE(third_called);
}

TEST(LoopExecutorTest, DeletesUnrunClosures) {
  LoopExecutor looper;
  looper.Schedule(&ExpectNotCalled);
  // If the destructor doesn't destroy the closure, it'll leak, and
  // the heapchecker should complain.
}

TEST(LoopExecutorTest, IgnoresUnrunPermanentClosures) {
  std::unique_ptr<Closure> permanent_cb(
      ::util::functional::ToPermanentCallback(&ExpectNotCalled));
  LoopExecutor looper;
  looper.Schedule(util::functional::FromCallback(permanent_cb.get()));
  // If the destructor of looper destroys the closure there will be a double
  // delete when the scoped_ptr goes out of scope and the debug allocator will
  // complain.
}

TEST(LoopExecutorTest, DestructorHandlesPendingAddAfters) {
  for (int i = 0; i < 20; ++i) {
    LoopExecutor looper;
    // One that probably won't have been added by the time the
    // destructor runs.
    looper.ScheduleAt(absl::Now() + absl::Milliseconds(10), [] {});
    // And a few that might be being added during the destructor.
    looper.ScheduleAt(absl::Now() + absl::Milliseconds(5), [] {});
    looper.ScheduleAt(absl::Now() + absl::Milliseconds(5), [] {});
    looper.ScheduleAt(absl::Now() + absl::Milliseconds(5), [] {});
    looper.ScheduleAt(absl::Now() + absl::Milliseconds(5), [] {});
    looper.ScheduleAt(absl::Now() + absl::Milliseconds(5), [] {});
    absl::SleepFor(absl::Milliseconds(5));
  }
  absl::SleepFor(absl::Milliseconds(100));
}

TEST(LoopExecutorTest, AddAfterDelays) {
  LoopExecutor looper;
  absl::Time start_time = absl::Now();
  looper.ScheduleAt(absl::Now() + absl::Milliseconds(50),
                    [&looper] { looper.MakeLoopExit(); });
  looper.Loop();
  // Only check for a 40ms delay to protect against time jumping
  // backwards.  On the assumption that TimedCall is correct, we only
  // need to check that we block at all.
  EXPECT_LT(start_time + absl::Milliseconds(40), absl::Now());
}

TEST(LoopExecutorTest, NumPendingClosures) {
  LoopExecutor looper;
  EXPECT_EQ(0, looper.num_pending_closures());
  looper.Schedule([] {});
  EXPECT_EQ(1, looper.num_pending_closures());
  looper.Schedule([] {});
  looper.Schedule([] {});
  looper.Schedule([] {});
  EXPECT_EQ(4, looper.num_pending_closures());
  EXPECT_TRUE(looper.TryRunOneClosure());
  EXPECT_EQ(3, looper.num_pending_closures());
  looper.RunQueuedClosures();
  EXPECT_EQ(0, looper.num_pending_closures());
}

TEST(LoopExecutorTest, TryAdd) {
  LoopExecutor looper;
  // TryAdd always succeeds.
  EXPECT_TRUE(looper.TrySchedule([&looper] { looper.MakeLoopExit(); }));
  looper.Loop();
}

void AssignFromSequence(int* i, SequenceNumber* sequence) {
  *i = sequence->GetNext();
}

TEST(LoopExecutorTest, FIFO) {
  SequenceNumber seq;
  int first, second, third;
  absl::Notification start, done;
  LoopExecutor looper;
  ThreadPool pool(1);

  pool.Schedule([&looper] { looper.Loop(); });
  looper.Schedule([&start] { start.Notify(); });
  start.WaitForNotification();
  looper.Schedule([&first, &seq] { AssignFromSequence(&first, &seq); });
  looper.Schedule([&second, &seq] { AssignFromSequence(&second, &seq); });
  looper.Schedule([&third, &seq] { AssignFromSequence(&third, &seq); });
  looper.Schedule([&done] { done.Notify(); });
  done.WaitForNotification();
  EXPECT_EQ(0, first);
  EXPECT_EQ(1, second);
  EXPECT_EQ(2, third);
  looper.MakeLoopExit();
}

void ExpectCurrentExecutorEq(LoopExecutor* expected_executor) {
  EXPECT_EQ(expected_executor, thread::Executor::CurrentExecutor());
}

TEST(LoopExecutorTest, SetsCurrentExecutor) {
  // Check that even when CurrentExecutor is already set (by the
  // ThreadPool), LoopExecutor still sets it.
  LoopExecutor looper;
  ThreadPool pool(1);

  looper.Schedule([&looper] { ExpectCurrentExecutorEq(&looper); });
  pool.Schedule([&looper] { looper.RunQueuedClosures(); });
}

}  // namespace
