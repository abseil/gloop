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

#include "gloop/thread/fiber/sleep.h"

#include "absl/synchronization/notification.h"
#include "absl/time/simulated_clock.h"
#include "absl/time/time.h"
#include "gloop/thread/fiber/fiber.h"
#include "gtest/gtest.h"

namespace {

enum SleepType {
  kTime = 0,
  kDuration = 1,
};

struct TestParams {
  SleepType sleep_type = kTime;
  bool cancel_sleep = false;
};

class CancellableSleepTest : public testing::TestWithParam<TestParams> {
 protected:
  SleepType sleep_type() const { return GetParam().sleep_type; }

  bool cancel_sleep() const { return GetParam().cancel_sleep; }
};

TEST_P(CancellableSleepTest, Sleeps) {
  absl::Notification sleep_done;
  absl::SimulatedClock clock;
  clock.SetTime(absl::FromUnixSeconds(100));

  thread::Fiber sleeping_fiber([&] {
    bool sleep_completed;
    if (sleep_type() == kDuration) {
      sleep_completed = thread::CancellableSleepFor(&clock, absl::Seconds(23));
    } else {
      sleep_completed =
          thread::CancellableSleepUntil(&clock, absl::FromUnixSeconds(123));
    }
    EXPECT_EQ(sleep_completed, !cancel_sleep());
    sleep_done.Notify();
  });

  absl::SleepFor(absl::Milliseconds(100));
  EXPECT_FALSE(
      sleep_done.WaitForNotificationWithTimeout(absl::Milliseconds(100)));
  if (cancel_sleep()) {
    sleeping_fiber.Cancel();
  } else {
    clock.AdvanceTime(absl::Seconds(23));
  }
  ASSERT_TRUE(sleep_done.WaitForNotificationWithTimeout(absl::Seconds(10)));
  sleeping_fiber.Join();
}

INSTANTIATE_TEST_SUITE_P(
    DurationBasedTests, CancellableSleepTest,
    testing::Values(TestParams{.sleep_type = kDuration, .cancel_sleep = false},
                    TestParams{.sleep_type = kDuration, .cancel_sleep = true}));

INSTANTIATE_TEST_SUITE_P(
    TimeBasedTests, CancellableSleepTest,
    testing::Values(TestParams{.sleep_type = kTime, .cancel_sleep = false},
                    TestParams{.sleep_type = kTime, .cancel_sleep = true}));

}  // namespace
