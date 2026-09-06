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

#include "gloop/util/task/sync_task.h"

#include <array>
#include <memory>
#include <string>

#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "benchmark/benchmark.h"
#include "gloop/base/context.h"
#include "gloop/base/timer.h"
#include "gloop/thread/config.h"
#include "gloop/thread/executor.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/util/task/sleep.h"
#include "gloop/util/task/task.h"
#include "google/protobuf/arena.h"
#include "gtest/gtest.h"

namespace {

// Returns the task with the given status.  A helper for
// ReturnTaskWhenCancelled(), below.
static void ReturnTaskWithStatus(absl::Status status, util::Task* task) {
  task->Return(status);
}

// Returns the task with the given status when the task is cancelled. Has
// no effect if the task is never cancelled.
static void ReturnTaskWhenCancelled(absl::Status status, util::Task* task) {
  task->WhenCancelled(absl::bind_front(&ReturnTaskWithStatus, status, task));
}

// Returns parent_task with status if sleep_task is OK.  A helper for
// ReturnTaskAfterDelay(), below.
static void ReturnTaskUnlessCancelled(absl::Status status,
                                      util::Task* parent_task,
                                      util::Task* sleep_task) {
  if (sleep_task->status().ok()) {
    parent_task->Return(status);
  }
}

// Returns task with status after the given delay.
static void ReturnTaskAfterDelay(absl::Duration delay, absl::Status status,
                                 util::Task* task) {
  util::SleepUntil(absl::Now() + delay,
                   task->AddChild(absl::bind_front(&ReturnTaskUnlessCancelled,
                                                   status, task)));
}

class SyncTaskTest : public ::testing::TestWithParam<bool> {
 protected:
  std::unique_ptr<util::SyncTask> GetSyncTask() {
    if (use_alternate_executor_) {
      return std::make_unique<util::SyncTask>(
          thread::SingletonInlineExecutor());
    }
    return std::make_unique<util::SyncTask>();
  }

  const bool use_alternate_executor_ = GetParam();
};

TEST_P(SyncTaskTest, InitiallyNotDone) {
  auto s = GetSyncTask();
  ASSERT_TRUE(!s->IsDone());
  s->task()->Return();
  s->WaitIgnoresCancel();
}

TEST_P(SyncTaskTest, StateChanges) {
  auto s = GetSyncTask();
  s->task()->Return(absl::UnknownError(""));
  s->WaitIgnoresCancel();
  ASSERT_TRUE(s->IsDone());
  ASSERT_EQ(s->status(), absl::UnknownError(""));
}

TEST_P(SyncTaskTest, WaitIgnoresCancel) {
  auto s = GetSyncTask();
  ReturnTaskAfterDelay(absl::Milliseconds(100), absl::UnknownError(""),
                       s->task());
  s->WaitIgnoresCancel();
  ASSERT_TRUE(s->IsDone());
  ASSERT_EQ(s->status(), absl::UnknownError(""));
}

TEST_P(SyncTaskTest, Wait) {
  auto s = GetSyncTask();
  ReturnTaskAfterDelay(absl::Milliseconds(100), absl::UnknownError(""),
                       s->task());
  ASSERT_EQ(s->Wait(), absl::UnknownError(""));
  ASSERT_TRUE(s->IsDone());
}

#if THREAD_HAVE_FIBER
static void DoWait(util::SyncTask* t, absl::Status* rv) { *rv = t->Wait(); }

TEST_P(SyncTaskTest, WaitWithFiberCancellation) {
  auto s = GetSyncTask();
  ReturnTaskWhenCancelled(absl::CancelledError(), s->task());
  absl::Status status;
  thread::Fiber f(absl::bind_front(&DoWait, s.get(), &status));
  f.Cancel();
  f.Join();
  ASSERT_TRUE(s->IsDone());
  ASSERT_EQ(status, absl::CancelledError());
}
#endif  // THREAD_HAVE_FIBER

TEST_P(SyncTaskTest, WaitIgnoresCancelWithTimeout) {
  auto s = GetSyncTask();
  ReturnTaskAfterDelay(absl::Milliseconds(100), absl::UnknownError(""),
                       s->task());

  SimpleCycleTimer timer;
  timer.Start();
  ASSERT_FALSE(s->WaitIgnoresCancelWithTimeout(absl::Milliseconds(10)));
  ASSERT_LT(timer.Get(), 20);

  ASSERT_FALSE(s->IsDone());
  ASSERT_TRUE(s->WaitIgnoresCancelWithTimeout(absl::Milliseconds(1000)));
  ASSERT_TRUE(s->IsDone());
  ASSERT_EQ(s->status(), absl::UnknownError(""));
}

TEST_P(SyncTaskTest, WaitWithTimeout) {
  auto s = GetSyncTask();
  ReturnTaskWhenCancelled(absl::CancelledError(), s->task());
  ReturnTaskAfterDelay(absl::Milliseconds(100), absl::UnknownError(""),
                       s->task());

  SimpleCycleTimer timer;
  timer.Start();
  absl::Status rv;
  ASSERT_FALSE(s->WaitWithTimeout(absl::Milliseconds(10)));
  ASSERT_LT(timer.Get(), 20);

  ASSERT_FALSE(s->IsDone());
  ASSERT_TRUE(s->WaitWithTimeout(absl::Milliseconds(1000)));
  ASSERT_TRUE(s->IsDone());
  ASSERT_EQ(s->status(), absl::UnknownError(""));
}

#if THREAD_HAVE_FIBER
TEST_P(SyncTaskTest, FiberCancelledBeforeWaitWithTimeoutCalled) {
  // Calling SyncTask::WaitWithTimeout() from an already cancelled fiber
  // cancels the SyncTask.

  auto s = GetSyncTask();

  ReturnTaskWhenCancelled(absl::CancelledError(), s->task());
  bool done;
  thread::Fiber f([&] {
    // Cancel this fiber, then wait.  It should transition to done immediately,
    // by way of being cancelled.
    thread::Fiber::Current()->Cancel();
    done = s->WaitWithTimeout(absl::Hours(24));
  });
  f.Join();

  EXPECT_TRUE(done);
  EXPECT_EQ(s->status(), absl::CancelledError());
  CHECK(s->IsDone()) << "; test is broken: SyncTask is not done!";
}

TEST_P(SyncTaskTest, FiberCancelledAfterWaitWithTimeoutCalled) {
  // Calling SyncTask::WaitWithTimeout() from a fiber that is later cancelled
  // cancels the SyncTask.

  auto s = GetSyncTask();

  ReturnTaskWhenCancelled(absl::CancelledError(), s->task());
  absl::Notification about_to_wait;
  bool done;
  thread::Fiber f([&] {
    about_to_wait.Notify();
    // Wait until done.  The test code below should cancel us soon, which should
    // transition `s` to done by way of cancellation.
    done = s->WaitWithTimeout(absl::Hours(24));
  });
  // Cance `f` once it is almost certain that its call to `WaitWithTimeout`
  // has blocked.
  about_to_wait.WaitForNotification();
  absl::SleepFor(absl::Seconds(1));
  f.Cancel();
  f.Join();

  EXPECT_TRUE(done);
  EXPECT_EQ(s->status(), absl::CancelledError());
  CHECK(s->IsDone()) << "; test is broken: SyncTask is not done!";
}
#endif

TEST_P(SyncTaskTest, WaitIgnoresCancelWithTimeoutThenCancelNeedsWaitToBeDone) {
  auto s = GetSyncTask();
  ReturnTaskWhenCancelled(absl::CancelledError(), s->task());

  SimpleCycleTimer timer;
  timer.Start();
  ASSERT_FALSE(s->WaitIgnoresCancelWithTimeout(absl::Milliseconds(10)));
  ASSERT_LT(timer.Get(), 20);
  ASSERT_FALSE(s->IsDone());

  s->Cancel();
  // Task may or may not be done at this point. We still need to
  // wait until it completes.
  s->WaitIgnoresCancel();
  ASSERT_TRUE(s->IsDone());
}

TEST_P(SyncTaskTest, Reset) {
  // Set up and finish a task.
  auto s = GetSyncTask();
  s->task()->Return(absl::UnknownError(""));
  s->WaitIgnoresCancel();
  ASSERT_TRUE(s->IsDone());
  ASSERT_EQ(s->status(), absl::UnknownError(""));

  // Reset it. The executor should be preserved.
  s->Reset();

  if (use_alternate_executor_) {
    EXPECT_EQ(thread::SingletonInlineExecutor(), s->task()->executor());
  } else {
    EXPECT_EQ(thread::Executor::DefaultExecutor(), s->task()->executor());
  }

  // We should be able to reuse it.
  ASSERT_TRUE(!s->IsDone());
  EXPECT_TRUE(s->task()->inline_done_callback());
  s->task()->Return(absl::OutOfRangeError("Reset Test"));
  s->WaitIgnoresCancel();
  ASSERT_TRUE(s->IsDone());
  ASSERT_EQ(s->status(), absl::OutOfRangeError("Reset Test"));
}

TEST_P(SyncTaskTest, CancelBefore) {
  auto s = GetSyncTask();
  ReturnTaskWhenCancelled(absl::CancelledError(), s->task());

  s->Cancel();

  s->WaitIgnoresCancel();
  ASSERT_TRUE(s->IsDone());

  s->task()->Return(absl::UnknownError(""));
  ASSERT_EQ(s->status(), absl::CancelledError());
}

TEST_P(SyncTaskTest, CancelAfter) {
  auto s = GetSyncTask();
  ReturnTaskWhenCancelled(absl::CancelledError(), s->task());

  s->task()->Return(absl::UnknownError(""));
  s->Cancel();

  s->WaitIgnoresCancel();
  ASSERT_TRUE(s->IsDone());
  ASSERT_EQ(s->status(), absl::UnknownError(""));
}

INSTANTIATE_TEST_SUITE_P(ExecutorTests, SyncTaskTest, testing::Bool());

TEST(SyncTaskContextTest, InherritsCurrentContext) {
  absl::Time deadline = absl::Now() + absl::Seconds(1234);
  base::WithDeadline with_deadline(deadline);
  util::SyncTask s;
  EXPECT_EQ(s.task()->context().deadline(), deadline);

  // Clean up.
  s.task()->Return();
}

TEST(SyncTaskContextTest, UsesBackgroundContextWhenConfigured) {
  util::SyncTask s(util::SyncTask::kWithBackgroundContext);

  // Clean up.
  s.task()->Return();
}

TEST(SyncTaskUsesArena, SyncTaskUsesArena) {
  google::protobuf::Arena arena;
  EXPECT_EQ(arena.SpaceUsed(), 0);

  util::SyncTask s(thread::SingletonInlineExecutor(), &arena);
  EXPECT_EQ(arena.SpaceUsed(), 0);
  auto* child = s.task()->AddChild([](util::Task*) {});
  EXPECT_GT(arena.SpaceUsed(), 0);
  child->Return();
  s.task()->Return();
  s.WaitIgnoresCancel();
  ASSERT_TRUE(s.IsDone());
  ASSERT_TRUE(s.task()->IsDone());

  s.Reset();
  EXPECT_EQ(s.task()->arena(), &arena);
  // Reset() does not reset the arena.
  EXPECT_GT(arena.SpaceUsed(), 0);
  s.task()->Return();
  s.WaitIgnoresCancel();
}

}  // namespace

static void BM_SyncTask(benchmark::State& state) {
  for (auto _ : state) {
    util::SyncTask s;
    s.task()->Return();
    s.WaitIgnoresCancel();
  }
}
BENCHMARK(BM_SyncTask);

static void BM_SyncTask2(benchmark::State& state) {
  util::SyncTask s;
  bool first = true;
  for (auto _ : state) {
    if (first) {
      first = false;
    } else {
      s.Reset();
    }
    s.task()->Return();
    s.WaitIgnoresCancel();
  }
}
BENCHMARK(BM_SyncTask2);

static void BM_SyncTaskWithArena(benchmark::State& state) {
  std::array<char, 4096> buffer;
  google::protobuf::Arena arena(buffer.data(), buffer.size());
  util::SyncTask s(thread::SingletonInlineExecutor(), &arena);
  for (auto _ : state) {
    auto* child = s.task()->AddChild([](util::Task*) {});
    child->Return();
    s.task()->Return();
    s.WaitIgnoresCancel();
    s.Reset();
  }
  s.task()->Return();
}
BENCHMARK(BM_SyncTaskWithArena);

static void BM_SyncTaskInline(benchmark::State& state) {
  for (auto _ : state) {
    util::SyncTask s(thread::SingletonInlineExecutor());
    s.task()->Return();
    s.WaitIgnoresCancel();
  }
}
BENCHMARK(BM_SyncTaskInline);
