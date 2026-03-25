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

#include "gloop/thread/os_semaphore.h"

#include <cerrno>
#include <ctime>
#include <thread>  // NOLINT

#include "absl/functional/bind_front.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gtest/gtest.h"

namespace thread {
namespace internal {
namespace {

constexpr absl::Duration kTestTimeout = absl::Seconds(2);

void PostWait(OsSemaphore* poster, OsSemaphore* waiter) {
  EXPECT_EQ(OsSemaphorePost(poster), 0);
  EXPECT_EQ(OsSemaphoreWait(waiter), 0);
}

TEST(SemaphoreTest, Basic) {
  OsSemaphore s1, s2;
  ASSERT_EQ(OsSemaphoreInit(&s1), 0);
  ASSERT_EQ(OsSemaphoreInit(&s2), 0);

  std::thread t(absl::bind_front(PostWait, &s1, &s2));

  EXPECT_EQ(OsSemaphoreWait(&s1), 0);
  EXPECT_EQ(OsSemaphorePost(&s2), 0);

  t.join();

  ASSERT_EQ(OsSemaphoreDestroy(&s1), 0);
  ASSERT_EQ(OsSemaphoreDestroy(&s2), 0);
}

TEST(SemaphoreTest, TimedWait) {
  OsSemaphore s1, s2;
  ASSERT_EQ(OsSemaphoreInit(&s1), 0);
  ASSERT_EQ(OsSemaphoreInit(&s2), 0);

  absl::Time start = absl::Now();
  timespec deadline = absl::ToTimespec(start + kTestTimeout);
  EXPECT_EQ(OsSemaphoreTimedWait(&s1, &deadline), -1);
  EXPECT_EQ(errno, ETIMEDOUT);
  EXPECT_GE(absl::Now() - start, kTestTimeout);

  std::thread t(absl::bind_front(PostWait, &s1, &s2));

  deadline = absl::ToTimespec(absl::Now() + kTestTimeout);
  EXPECT_EQ(OsSemaphoreTimedWait(&s1, &deadline), 0);
  EXPECT_EQ(OsSemaphorePost(&s2), 0);

  // Already expired.
  EXPECT_EQ(OsSemaphoreTimedWait(&s1, &deadline), -1);
  EXPECT_EQ(errno, ETIMEDOUT);

  t.join();

  ASSERT_EQ(OsSemaphoreDestroy(&s1), 0);
  ASSERT_EQ(OsSemaphoreDestroy(&s2), 0);
}

TEST(SemaphoreTest, TimedWaitRelative) {
  OsSemaphore s1, s2;
  ASSERT_EQ(OsSemaphoreInit(&s1), 0);
  ASSERT_EQ(OsSemaphoreInit(&s2), 0);

  absl::Time start = absl::Now();
  EXPECT_EQ(OsSemaphoreTimedWaitRelative(&s1, kTestTimeout), -1);
  EXPECT_EQ(errno, ETIMEDOUT);
  EXPECT_GE(absl::Now() - start, kTestTimeout);

  std::thread t(absl::bind_front(PostWait, &s1, &s2));

  EXPECT_EQ(OsSemaphoreTimedWaitRelative(&s1, kTestTimeout), 0);
  EXPECT_EQ(OsSemaphorePost(&s2), 0);

  EXPECT_EQ(OsSemaphoreTimedWaitRelative(&s1, absl::Seconds(-1)), -1);
  EXPECT_EQ(errno, ETIMEDOUT);

  t.join();

  ASSERT_EQ(OsSemaphoreDestroy(&s1), 0);
  ASSERT_EQ(OsSemaphoreDestroy(&s2), 0);
}

}  // namespace
}  // namespace internal
}  // namespace thread
