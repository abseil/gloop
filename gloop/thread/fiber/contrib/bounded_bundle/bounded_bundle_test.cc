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

#include "gloop/thread/fiber/contrib/bounded_bundle/bounded_bundle.h"

#include "absl/log/log.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/util/status/codes.pb.h"
#include "gtest/gtest.h"

namespace {

TEST(BoundedBundleTest, LimitsParallelism) {
  absl::Notification n;
  thread::Fiber f([&n] {
    static constexpr int kMaxLiveFibers = 10;
    thread::BoundedBundle bundle(kMaxLiveFibers);

    // Completely fill the bundle so it has no more capacity.
    for (int i = 0; i < kMaxLiveFibers; ++i) {
      bundle.Add([&n, i]() mutable {
        VLOG(1) << "Operation " << i << " starting";
        EXPECT_FALSE(n.HasBeenNotified())
            << "Bundle capacity less than expected!";
        n.WaitForNotification();
        VLOG(1) << "Operation " << i << " finished";
      });
    }

    // Try to add another operation. This should block due to lack of capacity.
    VLOG(1) << "Adding operation beyond capacity";
    bundle.Add([] {
      VLOG(1) << "Executing extra operation";
      EXPECT_TRUE(thread::Cancelled())
          << "This should not have executed before fiber cancellation, since "
             "that means we were able to add more than kMaxLiveFibers "
             "operations to the bundle at a time";
    });

    EXPECT_TRUE(thread::Cancelled()) << "The call to Add() blocks";
    bundle.JoinAll();
  });

  // Sleep for a bit to give the fiber time to run.
  absl::SleepFor(absl::Seconds(1));

  // Cancel the fiber to signal that it should pass.
  VLOG(1) << "Cancelling fiber";
  f.Cancel();

  // Allow everything in the bundle to proceed.
  VLOG(1) << "Releasing capacity-filling operations";
  n.Notify();
  f.Join();
}

}  // namespace
