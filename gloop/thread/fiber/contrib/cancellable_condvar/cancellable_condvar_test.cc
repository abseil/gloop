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

// A test for CancellableCondVar.

#include "gloop/thread/fiber/contrib/cancellable_condvar/cancellable_condvar.h"

#include <cstdint>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/functional/bind_front.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/thread/fiber/fiber.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using ::testing::AllOf;
using ::testing::Ge;
using ::testing::Le;

// This struct is used as fiber context in several of the tests below.
struct WaitDescriptor {
  const char* test_name;  // name of the test executing
  absl::Time start_time;  // used to make debug output use relative times

  // WaitDescriptor is used by a fiber that waits on a CancellableCondVar.  The
  // following fields describe how to wait, and what we expect to happen.

  absl::Time deadline;  // the deadline to be given to
                        // CancellableAwaitDeadline()

  // The bounds of when wakeup should occur.
  // They are generous to allow for scheduling variations.
  absl::Time expected_min_wakeup;
  absl::Time expected_max_wakeup;

  bool expected_result;     // Expected result from CancellableAwait*().
  bool expected_cancelled;  // Whether cancellation is expected.
  bool expected_timeout;    // Whether a timeout is expected.

  bool allow_cancellation;  // Whether to use Cancellable* wait variants,
  bool allow_deadline;      // Whether to use *WithDeadline* wait variants,

  bool use_signal_all;  // Use SignalAll() instead of Signal() for wakeups.

  bool attempt_cancellation;  // whether cancellation should be attempted

  absl::Mutex mu;  // Protects condition.
  // The condition being awaited.
  bool condition ABSL_GUARDED_BY(mu) = false;
  // The condition variable to signal.
  thread::CancellableCondVar cv;
};

// Return a human-readable string representing the value of *wd, for debugging.
std::string WaitDescriptorAsString(const WaitDescriptor* wd)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(wd->mu) {
  return absl::StrFormat(
      "%s: start_time=%s deadline=%s expected_min_wakeup=%s "
      "expected_max_wakeup=%s expected_result=%v expected_cancelled=%v "
      "expected_timeout=%v allow_cancellation=%v allow_deadline=%v "
      "use_signal_all=%v attempt_cancellation=%v condition=%v",
      wd->test_name, absl::FormatTime(wd->start_time),
      absl::FormatDuration(wd->deadline - wd->start_time),
      absl::FormatDuration(wd->expected_min_wakeup - wd->start_time),
      absl::FormatDuration(wd->expected_max_wakeup - wd->start_time),
      wd->expected_result, wd->expected_cancelled, wd->expected_timeout,
      wd->allow_cancellation, wd->allow_deadline, wd->use_signal_all,
      wd->attempt_cancellation, wd->condition);
}

// Acquire a lock in writer mode; wait until a timeout, cancellation, or
// conventional wakeup occurs; test the wakeup reason and timing against
// expectations; and release the previously acquired lock.  The lock type, and
// the expectations for the wakeup type and timing are encoded in *wd.
//
// This is the body of the fiber used in most of the tests below.
void FiberBody(WaitDescriptor* wd) ABSL_LOCKS_EXCLUDED(wd->mu) {
  bool result = false;
  absl::MutexLock lock(&wd->mu);
  VLOG(1) << "FiberBody start for " << WaitDescriptorAsString(wd);
  while (!wd->condition && !thread::Cancelled() && absl::Now() < wd->deadline) {
    if (wd->allow_cancellation && wd->allow_deadline) {
      result = wd->cv.CancellableWaitWithDeadline(&wd->mu, wd->deadline);
    } else if (wd->allow_cancellation) {
      wd->cv.CancellableWait(&wd->mu);
      result = false;  // cannot have timed out
    } else if (wd->allow_deadline) {
      result = wd->cv.WaitWithDeadline(&wd->mu, wd->deadline);
    } else {
      wd->cv.Wait(&wd->mu);
      result = false;  // cannot have timed out
    }
  }
  const absl::Time finish_time = absl::Now();
  EXPECT_EQ(result, wd->expected_result) << WaitDescriptorAsString(wd);
  EXPECT_EQ(thread::Cancelled(), wd->expected_cancelled)
      << WaitDescriptorAsString(wd);
  EXPECT_EQ(finish_time >= wd->deadline, wd->expected_timeout)
      << WaitDescriptorAsString(wd);
  EXPECT_THAT(finish_time,
              AllOf(Ge(wd->expected_min_wakeup), Le(wd->expected_max_wakeup)))
      << WaitDescriptorAsString(wd);
}

struct WakeupTestParams {
  bool allow_cancellation;
  bool allow_deadline;
  bool use_signal_all;
};

class WakeupTest : public testing::TestWithParam<WakeupTestParams> {};

// Test the case in which the thread is woken, rather than being cancelled, or
// hitting a deadline.
TEST_P(WakeupTest, WakesUp) {
  const WakeupTestParams& params = GetParam();
  WaitDescriptor wd;
  wd.test_name = "TestWakeup";
  wd.start_time = absl::Now();
  wd.deadline = wd.start_time + absl::Seconds(100);
  wd.expected_min_wakeup = wd.start_time + absl::Milliseconds(950);
  wd.expected_max_wakeup = wd.start_time + absl::Seconds(10);
  wd.expected_result = false;
  wd.expected_cancelled = false;
  wd.expected_timeout = false;
  wd.allow_cancellation = params.allow_cancellation;
  wd.allow_deadline = params.allow_deadline;
  wd.use_signal_all = params.use_signal_all;
  wd.attempt_cancellation = false;

  thread::Fiber fiber(absl::bind_front(&FiberBody, &wd));
  absl::SleepFor(absl::Seconds(1));
  {
    absl::MutexLock lock(&wd.mu);
    wd.condition = true;  // make condition true
    if (wd.use_signal_all) {
      wd.cv.SignalAll();
    } else {
      wd.cv.Signal();
    }
  }
  fiber.Join();
}

INSTANTIATE_TEST_SUITE_P(
    , WakeupTest,
    testing::Values(WakeupTestParams{.allow_cancellation = false,
                                     .allow_deadline = false,
                                     .use_signal_all = false},
                    WakeupTestParams{.allow_cancellation = false,
                                     .allow_deadline = false,
                                     .use_signal_all = true},
                    WakeupTestParams{.allow_cancellation = false,
                                     .allow_deadline = true,
                                     .use_signal_all = false},
                    WakeupTestParams{.allow_cancellation = false,
                                     .allow_deadline = true,
                                     .use_signal_all = true},
                    WakeupTestParams{.allow_cancellation = true,
                                     .allow_deadline = false,
                                     .use_signal_all = false},
                    WakeupTestParams{.allow_cancellation = true,
                                     .allow_deadline = false,
                                     .use_signal_all = true},
                    WakeupTestParams{.allow_cancellation = true,
                                     .allow_deadline = true,
                                     .use_signal_all = false},
                    WakeupTestParams{.allow_cancellation = true,
                                     .allow_deadline = true,
                                     .use_signal_all = true}),
    [](const testing::TestParamInfo<WakeupTestParams>& info) {
      return absl::StrCat(
          info.param.allow_cancellation ? "Cancellable" : "NonCancellable", "_",
          info.param.allow_deadline ? "WithDeadline" : "NoDeadline", "_",
          info.param.use_signal_all ? "SignalAll" : "Signal");
    });

class DeadlineTest
    : public testing::TestWithParam<bool /*allow_cancellation*/> {};

// Test the case in which the thread hits a deadline, rather than being
// cancelled, or being woken.
TEST_P(DeadlineTest, Expires) {
  const bool allow_cancellation = GetParam();
  WaitDescriptor wd;
  wd.test_name = "TestDeadline";
  wd.start_time = absl::Now();
  wd.deadline = wd.start_time + absl::Seconds(1);
  wd.expected_min_wakeup = wd.start_time + absl::Milliseconds(950);
  wd.expected_max_wakeup = wd.start_time + absl::Seconds(10);
  wd.expected_result = true;
  wd.expected_cancelled = false;
  wd.expected_timeout = true;
  wd.allow_cancellation = allow_cancellation;
  wd.allow_deadline = true;
  wd.use_signal_all = false;
  wd.attempt_cancellation = false;

  thread::Fiber fiber(absl::bind_front(&FiberBody, &wd));
  // allow deadline to expire
  fiber.Join();
}

INSTANTIATE_TEST_SUITE_P(, DeadlineTest, testing::Bool(),
                         [](const testing::TestParamInfo<bool>& info) {
                           return info.param ? "Cancellable" : "NonCancellable";
                         });

class CancellationTest
    : public testing::TestWithParam<bool /*allow_deadline*/> {};

// Test the case in which the thread is cancelled, rather than hitting a
// deadline, or being woken.
TEST_P(CancellationTest, Cancels) {
  const bool allow_deadline = GetParam();
  WaitDescriptor wd;
  wd.test_name = "TestCancellation";
  wd.start_time = absl::Now();
  wd.deadline = wd.start_time + absl::Seconds(100);
  wd.expected_min_wakeup = wd.start_time + absl::Milliseconds(950);
  wd.expected_max_wakeup = wd.start_time + absl::Seconds(10);
  wd.expected_result = false;
  wd.expected_cancelled = true;
  wd.expected_timeout = false;
  wd.allow_cancellation = true;
  wd.allow_deadline = allow_deadline;
  wd.use_signal_all = false;
  wd.attempt_cancellation = true;

  thread::Fiber fiber(absl::bind_front(&FiberBody, &wd));
  absl::SleepFor(absl::Seconds(1));
  fiber.Cancel();  // cancel the fiber
  fiber.Join();
}

INSTANTIATE_TEST_SUITE_P(, CancellationTest, testing::Bool(),
                         [](const testing::TestParamInfo<bool>& info) {
                           return info.param ? "WithDeadline" : "NoDeadline";
                         });

class NonCancellationTest
    : public testing::TestWithParam<bool /*allow_deadline*/> {};

// Test the case in which the thread is cancelled, but the wait is not flagged
// as being cancellable.  The thread is woken by a normal wakeup a few seconds
// later.
TEST_P(NonCancellationTest, IgnoresCancellation) {
  const bool allow_deadline = GetParam();
  WaitDescriptor wd;
  wd.test_name = "TestNonCancellation";
  wd.start_time = absl::Now();
  wd.deadline = wd.start_time + absl::Seconds(100);
  wd.expected_min_wakeup = wd.start_time + absl::Milliseconds(3500);
  wd.expected_max_wakeup = wd.start_time + absl::Seconds(10);
  wd.expected_result = false;
  // We try to cancel the fiber, even though the timings will reveal that
  // we didn't wake the fiber at cancellation time.
  wd.expected_cancelled = true;
  wd.expected_timeout = false;
  wd.allow_cancellation = false;
  wd.allow_deadline = allow_deadline;
  wd.use_signal_all = false;
  wd.attempt_cancellation = true;

  thread::Fiber fiber(absl::bind_front(&FiberBody, &wd));
  absl::SleepFor(absl::Seconds(1));
  fiber.Cancel();  // Cancel the fiber; it won't wake because the waits
                   // specified by wait_flag_non_cancellable_possibilities
                   // are non-cancellable.
  absl::SleepFor(absl::Seconds(3));
  {
    absl::MutexLock lock(&wd.mu);
    wd.condition = true;  // make condition true
    wd.cv.Signal();
  }
  fiber.Join();
}

INSTANTIATE_TEST_SUITE_P(, NonCancellationTest, testing::Bool(),
                         [](const testing::TestParamInfo<bool>& info) {
                           return info.param ? "WithDeadline" : "NoDeadline";
                         });

// A context used for TestPingPong(), containing a counter, a mutex that
// protects it, and two condition variables that are alternately signalled by
// two threads that increment to even and odd counter values respectively.
struct PingPongState {
  absl::Mutex mu;  // protects loop count.
  // successively increments up to max_loop_count
  int32_t loop_count ABSL_GUARDED_BY(mu) = 0;
  // maximum loop_count; must be even and exceed 0
  int32_t max_loop_count = 2000000;

  // Signalled when loop_count is even.
  thread::CancellableCondVar loop_count_is_even;

  // Signalled when loop_count is odd.
  thread::CancellableCondVar loop_count_is_odd;
};

// Until pps->loop_count==pps->max_loop_count-1, repeatedly wait until
// pps->loop_count is even, then increment loop_count, and signal
// pps->loop_count_is_odd.
void PingPongMakeOddThread(PingPongState* pps) ABSL_LOCKS_EXCLUDED(pps->mu) {
  absl::MutexLock lock(&pps->mu);
  while (pps->loop_count != pps->max_loop_count - 1) {
    while ((pps->loop_count & 1) == 1) {  // wait while loop_count is odd
      pps->loop_count_is_even.Wait(&pps->mu);
    }
    // loop_count is now even; make odd.
    pps->loop_count++;
    pps->loop_count_is_odd.Signal();
  }
}

// Until pps->loop_count==pps->max_loop_count, repeatedly wait until
// pps->loop_count is odd, then increment loop_count, and signal
// pps->loop_count_is_even.
void PingPongMakeEvenThread(PingPongState* pps) ABSL_LOCKS_EXCLUDED(pps->mu) {
  absl::MutexLock lock(&pps->mu);
  while (pps->loop_count != pps->max_loop_count) {
    while ((pps->loop_count & 1) == 0) {  // wait while loop_count is even
      pps->loop_count_is_odd.Wait(&pps->mu);
    }
    // loop_count is now odd; make even.
    pps->loop_count++;
    pps->loop_count_is_even.Signal();
  }
}

// Test the case of two fibers alternately waking each other up many times.
TEST(CancellableCondVarTest, PingPongAlternatesBetweenFibers) {
  PingPongState pps;
  thread::Fiber make_even(absl::bind_front(&PingPongMakeEvenThread, &pps));
  thread::Fiber make_odd(absl::bind_front(&PingPongMakeOddThread, &pps));
  make_even.Join();
  make_odd.Join();
  absl::MutexLock lock(&pps.mu);
  EXPECT_EQ(pps.loop_count, pps.max_loop_count);
}

}  // namespace
