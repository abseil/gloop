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

// A test for CancellableClosure

#include "gloop/util/callback/cancellable_closure.h"

#include <array>
#include <climits>
#include <cstdint>
#include <string>
#include <thread>  // NOLINT(build/c++11)
#include <tuple>
#include <vector>

#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/callback.h"
#include "gloop/thread/threadpool.h"
#include "gloop/util/functional/from_callback.h"
#include "gloop/util/functional/to_callback.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using ::testing::IsNull;
using ::testing::NotNull;

TEST(CancellableClosureTest, RunBeforeUnref) {
  int x = 0;
  util::callback::CancellableClosure* cc =
      util::callback::CancellableClosure::New(
          ::util::functional::ToCallback([&x] { x = 1; }));
  // Use Run() before Unref()
  cc->Run();
  cc->Unref();
  EXPECT_EQ(x, 1);
}

TEST(CancellableClosureTest, UnrefBeforeRun) {
  int x = 0;
  util::callback::CancellableClosure* cc =
      util::callback::CancellableClosure::New(
          ::util::functional::ToCallback([&x] { x = 1; }));
  // Use Unref() before Run()
  cc->Unref();  // should be able to call Run after the last Unref().
  cc->Run();
  EXPECT_EQ(x, 1);
}

TEST(CancellableClosureTest, RefUnref) {
  util::callback::CancellableClosure* cc;
  int x;
  static const int kReferences = 100;

  // Ensure that it takes one more Unref() than we've done Ref()s
  // to keep the heap checker happy.
  x = 0;
  cc = util::callback::CancellableClosure::New(
      ::util::functional::ToCallback([&x] { x = 1; }));
  cc->Run();
  EXPECT_EQ(x, 1);
  for (int i = 0; i != kReferences; i++) {
    cc->Ref();
  }
  for (int i = 0; i != kReferences + 1; i++) {
    cc->Unref();
  }
}

struct WaitUntilExpectations {
  bool expected_success;
  int expected_x;
  int expected_min_duration_ms;
};

WaitUntilExpectations ComputeWaitUntilExpectations(int run_delay_ms,
                                                   int run_time_ms,
                                                   int wait_delay_ms,
                                                   int flags) {
  if ((flags & util::callback::CancellableClosure::kRunInCaller) != 0) {
    // Expect closure to have run, and delay determined by run time
    // because WaitUntil() will run the closure.
    return {true, 1, run_time_ms};
  }
  if (wait_delay_ms == INT_MAX) {
    return {true, 1, run_delay_ms + run_time_ms};
  }
  if (run_delay_ms < wait_delay_ms) {
    // Expect closure to have run, and delay to be determined by
    // Executor delay plus run time.
    return {true, 1, run_delay_ms + run_time_ms};
  }
  // Expect closure not to have run, and delay determined by wait
  // argument.
  return {false, 0, wait_delay_ms};
}

class CancellableClosureWaitUntilTest
    : public ::testing::TestWithParam<std::tuple<int, int, int, int>> {};

// Check a single use of WaitUntil() with a given delay, wait time, and flags.
// Run the closures on *exec, and notify *done when finished.
TEST_P(CancellableClosureWaitUntilTest, Run) {
  auto [run_delay_ms, run_time_ms, wait_delay_ms, flags] = GetParam();

  ThreadPool exec(40);

  int x = 0;
  util::callback::CancellableClosure* cc =
      util::callback::CancellableClosure::New(
          ::util::functional::ToCallback([&x, run_time_ms] {
            if (run_time_ms > 0) {
              absl::SleepFor(absl::Milliseconds(run_time_ms));
            }
            x = 1;
          }));

  absl::Time now = exec.clock()->TimeNow();
  exec.ScheduleAt(now + absl::Milliseconds(run_delay_ms),
                  ::util::functional::FromCallback(cc));

  int64_t before_ms = absl::ToUnixMillis(now);
  int64_t timeout_ms =
      (wait_delay_ms == INT_MAX ? util::callback::CancellableClosure::kForever
                                : before_ms + wait_delay_ms);

  bool result = cc->WaitUntil(timeout_ms, flags);
  int64_t interval_ms = absl::ToUnixMillis(exec.clock()->TimeNow()) - before_ms;

  auto expected = ComputeWaitUntilExpectations(run_delay_ms, run_time_ms,
                                               wait_delay_ms, flags);

  EXPECT_EQ(result, expected.expected_success);
  EXPECT_EQ(x, expected.expected_x);

  // GTest CI workers and sanitizers can suffer from scheduler latency. We
  // apply a -100ms lower bound tolerance for clock granularity, and a
  // generous +5000ms upper bound to prevent false-positive flakiness.
  EXPECT_GE(interval_ms, expected.expected_min_duration_ms - 100);
  EXPECT_LT(interval_ms, expected.expected_min_duration_ms + 5000);

  EXPECT_TRUE(cc->WaitUntil(util::callback::CancellableClosure::kForever, 0));
  EXPECT_EQ(x, 1);

  cc->Unref();
}

// Test many combinations of delays and parameters for WaitUntil.
INSTANTIATE_TEST_SUITE_P(
    CancellableClosureWaitUntilTestSuite, CancellableClosureWaitUntilTest,
    testing::Combine(
        testing::Values(100, 500, 900), testing::Values(0, 100),
        testing::Values(350, 750, INT_MAX),
        testing::Values(0, util::callback::CancellableClosure::kRunInCaller)));

MATCHER_P2(MatchesCancelledClosure, expect_nonnull, x_ptr,
           std::string(negation ? "does not match" : "matches") +
               " cancelled closure expectation") {
  Closure* cancelled_cl = arg;
  if (expect_nonnull) {
    if (cancelled_cl == nullptr) {
      *result_listener << "which is null";
      return false;
    }
    if (*x_ptr != 0) {
      *result_listener << "which is non-null, but x is already " << *x_ptr
                       << " (expected 0 before running)";
      return false;
    }
    cancelled_cl->Run();
    if (*x_ptr != 1) {
      *result_listener << "which is non-null, but running it did not change "
                          "x to 1 (it is "
                       << *x_ptr << ")";
      return false;
    }
    return true;
  } else {
    if (cancelled_cl != nullptr) {
      *result_listener << "which is non-null";
      return false;
    }
    return true;
  }
}

struct CancelExpectations {
  util::callback::CancellableClosure::CancelResult expected_result1;
  util::callback::CancellableClosure::CancelResult expected_result2;
  bool expect_cancelled_nonnull;
  int expected_x_before_cancelled_run;
};

CancelExpectations ComputeCancelExpectations(int run_delay_ms, int run_time_ms,
                                             int wait_delay_ms, int flags) {
  if (wait_delay_ms < run_delay_ms) {
    // Expect to have cancelled on the first try.
    return {util::callback::CancellableClosure::CANCELLED,
            util::callback::CancellableClosure::ALREADY_CANCELLED, true, 0};
  }
  if (wait_delay_ms < run_delay_ms + run_time_ms) {
    // Expect to see the closure while it's running on the first try,
    // and finished on the second try.
    return {util::callback::CancellableClosure::RUNNING,
            util::callback::CancellableClosure::FINISHED, false, 1};
  }
  // Expect to see the closure finished.
  return {util::callback::CancellableClosure::FINISHED,
          util::callback::CancellableClosure::FINISHED, false, 1};
}

class CancellableClosureCancelTest
    : public ::testing::TestWithParam<std::tuple<int, int, int, int>> {};

// Check Cancel() of a closure delayed for run_delay_ms that runs for
// run_time_ms, cancelling after wait_delay_ms, and trying again after
// WaitUntil() with flags.
// Run the closures on *exec, and notify *done when finished.
TEST_P(CancellableClosureCancelTest, Run) {
  auto [run_delay_ms, run_time_ms, wait_delay_ms, flags] = GetParam();

  ThreadPool exec(40);

  int x = 0;
  util::callback::CancellableClosure* cc =
      util::callback::CancellableClosure::New(
          ::util::functional::ToCallback([&x, run_time_ms] {
            if (run_time_ms > 0) {
              absl::SleepFor(absl::Milliseconds(run_time_ms));
            }
            x = 1;
          }));

  absl::Time start_time = exec.clock()->TimeNow();
  exec.ScheduleAt(start_time + absl::Milliseconds(run_delay_ms),
                  ::util::functional::FromCallback(cc));

  absl::SleepFor(absl::Milliseconds(wait_delay_ms));

  Closure* cancelled_cl = nullptr;
  util::callback::CancellableClosure::CancelResult result =
      cc->Cancel(&cancelled_cl);

  // Measure actual sleep duration to avoid flakiness from scheduler latency
  // or sanitizer overheads causing sleep overshoots.
  int64_t actual_wait_ms =
      absl::ToInt64Milliseconds(exec.clock()->TimeNow() - start_time);

  // then wait until the closure has either run or been cancelled
  EXPECT_TRUE(
      cc->WaitUntil(util::callback::CancellableClosure::kForever, flags));

  // then try cancelling again.
  Closure* cancelled_cl2 = nullptr;
  util::callback::CancellableClosure::CancelResult result2 =
      cc->Cancel(&cancelled_cl2);

  // Now we compute the result we should expect.
  auto expected = ComputeCancelExpectations(run_delay_ms, run_time_ms,
                                            actual_wait_ms, flags);

  EXPECT_EQ(result, expected.expected_result1);
  EXPECT_EQ(result2, expected.expected_result2);
  EXPECT_EQ(x, expected.expected_x_before_cancelled_run);

  EXPECT_THAT(cancelled_cl,
              MatchesCancelledClosure(expected.expect_cancelled_nonnull, &x));
  EXPECT_THAT(cancelled_cl2, IsNull());

  cc->Unref();
}

// Test many combinations of delays and parameters for Cancel.
INSTANTIATE_TEST_SUITE_P(
    CancellableClosureCancelTestSuite, CancellableClosureCancelTest,
    testing::Combine(
        testing::Values(0, 300), testing::Values(0, 300),
        testing::Values(150, 450, 750),
        testing::Values(0, util::callback::CancellableClosure::kRunInCaller)));

TEST(CancellableClosureTest, CancelBeforeRunning) {
  int run_count = 0;
  util::callback::CancellableClosure* cc =
      util::callback::CancellableClosure::New(
          ::util::functional::ToCallback([&run_count] { run_count++; }));

  Closure* cancelled_cl = nullptr;
  util::callback::CancellableClosure::CancelResult result =
      cc->Cancel(&cancelled_cl);

  EXPECT_EQ(result, util::callback::CancellableClosure::CANCELLED);
  ASSERT_THAT(cancelled_cl, NotNull());

  // According to CancellableClosure's API contract, when Cancel() returns
  // CANCELLED, the wrapped Closure is returned in cancelled_cl. The caller
  // is responsible for executing/deleting the cancelled closure exactly once,
  // and cc->Run() must still be called to allow the object to be freed.
  cancelled_cl->Run();
  EXPECT_EQ(run_count, 1);

  // cc->Run() must still be called, but since it is cancelled, it should
  // NOT run the wrapped callback again.
  cc->Run();
  EXPECT_EQ(run_count, 1);

  cc->Unref();
}

TEST(CancellableClosureTest, CancelWhileRunning) {
  absl::Notification cb_started;
  absl::Notification cb_resume;

  util::callback::CancellableClosure* cc =
      util::callback::CancellableClosure::New(
          ::util::functional::ToCallback([&cb_started, &cb_resume] {
            cb_started.Notify();
            cb_resume.WaitForNotification();
          }));

  std::thread run_thread([cc] { cc->Run(); });

  // Wait until the callback has actually started executing.
  cb_started.WaitForNotification();

  Closure* cancelled_cl = nullptr;
  util::callback::CancellableClosure::CancelResult result =
      cc->Cancel(&cancelled_cl);

  // Since the closure is currently running, Cancel() should return RUNNING.
  EXPECT_EQ(result, util::callback::CancellableClosure::RUNNING);
  EXPECT_THAT(cancelled_cl, IsNull());

  // Resume the callback and join the thread.
  cb_resume.Notify();
  run_thread.join();

  cc->Unref();
}

TEST(CancellableClosureTest, WaitThenCancel) {
  bool run_flag = false;
  util::callback::CancellableClosure* cc =
      util::callback::CancellableClosure::New(
          ::util::functional::ToCallback([&run_flag] { run_flag = true; }));

  absl::Notification waiter_started;
  absl::Notification waiter_done;
  std::thread waiter_thread([cc, &waiter_started, &waiter_done] {
    waiter_started.Notify();
    bool success =
        cc->WaitUntil(util::callback::CancellableClosure::kForever, 0);
    EXPECT_TRUE(success);
    waiter_done.Notify();
  });

  waiter_started.WaitForNotification();

  std::this_thread::yield();
  absl::SleepFor(absl::Milliseconds(10));

  Closure* cancelled_cl = nullptr;
  EXPECT_EQ(cc->Cancel(&cancelled_cl),
            util::callback::CancellableClosure::CANCELLED);
  ASSERT_THAT(cancelled_cl, NotNull());

  waiter_done.WaitForNotification();
  waiter_thread.join();

  EXPECT_FALSE(run_flag);

  cc->Run();

  cancelled_cl->Run();
  EXPECT_TRUE(run_flag);

  cc->Unref();
}

TEST(CancellableClosureTest, ConcurrentMultipleWaiters) {
  bool run_flag = false;
  util::callback::CancellableClosure* cc =
      util::callback::CancellableClosure::New(
          ::util::functional::ToCallback([&run_flag] { run_flag = true; }));

  static constexpr int kNumWaiters = 10;
  std::vector<std::thread> waiter_threads;
  std::array<absl::Notification, kNumWaiters> started_notifications;
  std::array<absl::Notification, kNumWaiters> done_notifications;

  for (int i = 0; i < kNumWaiters; ++i) {
    absl::Notification* started = &started_notifications[i];
    absl::Notification* done = &done_notifications[i];
    waiter_threads.emplace_back([cc, started, done] {
      started->Notify();
      bool success =
          cc->WaitUntil(util::callback::CancellableClosure::kForever, 0);
      EXPECT_TRUE(success);
      done->Notify();
    });
  }

  for (int i = 0; i < kNumWaiters; ++i) {
    started_notifications[i].WaitForNotification();
  }

  std::this_thread::yield();
  absl::SleepFor(absl::Milliseconds(10));

  cc->Run();
  EXPECT_TRUE(run_flag);

  for (int i = 0; i < kNumWaiters; ++i) {
    done_notifications[i].WaitForNotification();
  }

  for (auto& t : waiter_threads) {
    t.join();
  }

  cc->Unref();
}

}  // namespace
