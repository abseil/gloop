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

#include "absl/functional/bind_front.h"
#include "absl/status/status.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/util/status/status.h"
#include "gloop/util/task/sync_task.h"
#include "gloop/util/task/task.h"
#include "gtest/gtest.h"

constexpr absl::Duration kEpsilon = absl::Milliseconds(500);

TEST(SleepUntil, Future) {
  const absl::Time t = absl::Now() + kEpsilon;
  util::SyncTask s;
  util::SleepUntil(t, s.task());
  s.WaitIgnoresCancel();
  ASSERT_GE(absl::Now(), t);
  ASSERT_LE(absl::Now(), t + kEpsilon);
  ASSERT_TRUE(s.status().ok());
}

TEST(SleepUntil, Past) {
  const absl::Time now = absl::Now();
  util::SyncTask s;
  util::SleepUntil(now - absl::Seconds(1), s.task());
  s.WaitIgnoresCancel();
  ASSERT_LE(absl::Now(), now + kEpsilon);
  ASSERT_TRUE(s.status().ok());
}

TEST(SleepUntil, InfinitePast) {
  const absl::Time now = absl::Now();
  util::SyncTask s;
  util::SleepUntil(absl::InfinitePast(), s.task());
  s.WaitIgnoresCancel();
  ASSERT_LE(absl::Now(), now + kEpsilon);
  ASSERT_TRUE(s.status().ok());
}

TEST(SleepUntil, CancelLongSleep) {
  const absl::Time now = absl::Now();
  util::SyncTask s;
  util::SleepUntil(now + absl::Seconds(1000000), s.task());
  s.task()->Cancel();
  s.WaitIgnoresCancel();
  ASSERT_LE(absl::Now(), now + kEpsilon);
  ASSERT_TRUE(::util::HasErrorCode(s.status(), absl::StatusCode::kCancelled));
}

static void CancelTask(absl::Notification* n, absl::Status* s,
                       util::Task* task) {
  // We cancel the task, though it is too late for the cancellation to
  // have any effect.
  task->Cancel();
  *s = task->status();
  n->Notify();
}

TEST(SleepUntil, CancelDuringCallback) {
  absl::Notification n;
  absl::Status s;
  util::Task t(absl::bind_front(CancelTask, &n, &s));
  const absl::Time now = absl::Now();
  util::SleepUntil(now - absl::Seconds(1), &t);
  n.WaitForNotification();
  ASSERT_LE(absl::Now(), now + kEpsilon);
  ASSERT_TRUE(s.ok());
}
