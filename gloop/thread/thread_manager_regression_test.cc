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

#include <sys/resource.h>
#include <sys/time.h>

#include <cstdio>

#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/init_google.h"
#include "gloop/thread/thread_manager.h"
#include "gtest/gtest.h"

static void TestWaitQueueThread(absl::Notification* n) {
  ::absl::SleepFor(::absl::Seconds(1));
  n->Notify();
}

TEST(ThreadManagerCPUUsage, Basic) {
  struct rusage usage_before, usage_after;
  getrusage(RUSAGE_SELF, &usage_before);

  absl::Notification n;
  thread::DefaultQueue()->Schedule([&n] { TestWaitQueueThread(&n); });
  n.WaitForNotification();

  getrusage(RUSAGE_SELF, &usage_after);

  // Convert timeval to WallTime
  double before = usage_before.ru_utime.tv_sec +
                  usage_before.ru_utime.tv_usec / 1000.0 / 1000.0;
  double after = usage_after.ru_utime.tv_sec +
                 +usage_after.ru_utime.tv_usec / 1000.0 / 1000.0;
  printf("user time: %f\n", after - before);

  // The two threads just slept for 1 second, so their CPU usage should be
  // very small.
  EXPECT_LT(after - before, 0.1);
};

int main(int argc, char* argv[]) {
  InitGoogle(argv[0], &argc, &argv, true);
  return RUN_ALL_TESTS();
}
