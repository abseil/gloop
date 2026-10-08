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

#include "gloop/util/task/sleep.h"

#include <functional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/util/task/sync_task.h"
#include "gloop/util/task/task.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace util {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::AllOf;
using ::testing::Ge;
using ::testing::Le;

constexpr absl::Duration kEpsilon = absl::Milliseconds(500);

TEST(SleepUntil, Future) {
  const absl::Time t = absl::Now() + kEpsilon;
  SyncTask s;
  SleepUntil(t, s.task());
  s.WaitIgnoresCancel();
  EXPECT_THAT(absl::Now(), AllOf(Ge(t), Le(t + kEpsilon)));
  EXPECT_THAT(s.status(), IsOk());
}

struct PastDeadlineTestCase {
  std::string name;
  std::function<absl::Time()> deadline_fn;
};

class SleepUntilPastTest
    : public ::testing::TestWithParam<PastDeadlineTestCase> {};

TEST_P(SleepUntilPastTest, CompletesImmediately) {
  const absl::Time now = absl::Now();
  SyncTask s;
  SleepUntil(GetParam().deadline_fn(), s.task());
  s.WaitIgnoresCancel();
  EXPECT_LE(absl::Now(), now + kEpsilon);
  EXPECT_THAT(s.status(), IsOk());
}

INSTANTIATE_TEST_SUITE_P(
    , SleepUntilPastTest,
    ::testing::Values(
        PastDeadlineTestCase{"OneSecondInPast",
                             [] { return absl::Now() - absl::Seconds(1); }},
        PastDeadlineTestCase{"InfinitePast",
                             [] { return absl::InfinitePast(); }},
        PastDeadlineTestCase{"UnixEpoch", [] { return absl::UnixEpoch(); }}),
    [](const ::testing::TestParamInfo<PastDeadlineTestCase>& info) {
      return info.param.name;
    });

TEST(SleepUntil, CancelLongSleep) {
  const absl::Time now = absl::Now();
  SyncTask s;
  SleepUntil(now + absl::Seconds(1000000), s.task());
  s.task()->Cancel();
  s.WaitIgnoresCancel();
  EXPECT_LE(absl::Now(), now + kEpsilon);
  EXPECT_THAT(s.status(), StatusIs(absl::StatusCode::kCancelled));
}

TEST(SleepUntil, CancelDuringCallback) {
  absl::Notification n;
  absl::Status s;
  Task t([&n, &s](Task* task) {
    // We cancel the task, though it is too late for the cancellation to
    // have any effect.
    task->Cancel();
    s = task->status();
    n.Notify();
  });
  const absl::Time now = absl::Now();
  SleepUntil(now - absl::Seconds(1), &t);
  n.WaitForNotification();
  EXPECT_LE(absl::Now(), now + kEpsilon);
  EXPECT_THAT(s, IsOk());
}

}  // namespace
}  // namespace util
