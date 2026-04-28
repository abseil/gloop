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

// This test uses non-portable google3 APIs such as Fiber and benchmarks.
// See once_portable_test for tests using portable APIs.

#include <pthread.h>

#include <memory>
#include <vector>

#include "absl/base/call_once.h"
#include "absl/synchronization/blocking_counter.h"
#include "gloop/base/scheduling/domain.h"
#include "gloop/base/scheduling/low-level-support.h"
#include "gloop/base/scheduling/scheduler.h"
#include "gloop/thread/fiber/fiber-options.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/fifolifo-schedulers.h"
#include "gloop/thread/fiber/init-domain.h"
#include "gtest/gtest.h"

// ------------------------------------------------------------------
// Tests use of GoogleOnceInit in a cooperative context.

// First validate that context reports appropriately.
TEST(Once, CanDisableRescheduling) {
  static absl::once_flag google_init_enabled;
  static absl::once_flag absl_init_disabled;
  static absl::once_flag absl_init_enabled;

  struct Helper {
    static void CheckEnabled() {
      EXPECT_TRUE(
          absl::base_internal::SchedulingGuard::ReschedulingIsAllowed());
    }

    static void CheckDisabled() {
      EXPECT_FALSE(
          absl::base_internal::SchedulingGuard::ReschedulingIsAllowed());
    }

    static void TestFiber() {
      absl::call_once(google_init_enabled, Helper::CheckEnabled);
      absl::call_once(absl_init_enabled, Helper::CheckEnabled);

      absl::base_internal::LowLevelCallOnce(&absl_init_disabled,
                                            Helper::CheckDisabled);
    }
  };

  // We must make sure the test is run in a cooperative context (non-cooperative
  // threads are never eligible for rescheduling).
  thread::Fiber fiber(Helper::TestFiber);
  fiber.Join();
}

// Then validate that in the cooperative case we are deadlock free.
TEST(Once, IsCooperative) {
  static int kNumFibers = 20;
  static absl::once_flag init;
  static absl::BlockingCounter num_started(kNumFibers);

  struct Helper {
    static void WaitForBlockingCounter() { num_started.Wait(); }

    static void ContendedOnce() {
      num_started.DecrementCount();
      // This test would always hang with a non-cooperative GoogleOnceInit().
      absl::call_once(init, WaitForBlockingCounter);
    }
  };

  // This tests that contending waiters are allowed to cooperatively reschedule
  // when in GoogleOnceInit.  This ensures that rescheduling within a
  // GoogleOnceInit (e.g. due to a contended mutex) cannot lead to a deadlock.
  //
  // To do this we create a scheduler with an allowed concurrency of 1, many
  // fibers attached to that scheduler then attempt a GoogleOnceInit() which is
  // only allowed to complete after all fibers have started.
  base::scheduling::Scheduler* parent =
      thread::DefaultDomain()->root_scheduler();
  base::scheduling::Scheduler* scheduler =
      thread::NewChildLIFOScheduler(parent, 1);
  std::vector<std::unique_ptr<thread::Fiber> > fibers(kNumFibers);

  for (std::unique_ptr<thread::Fiber>& fiber : fibers) {
    fiber = thread::NewTree(thread::TreeOptions().set_scheduler(scheduler),
                            Helper::ContendedOnce);
  }

  for (std::unique_ptr<thread::Fiber>& fiber : fibers) {
    fiber->Join();
  }
  scheduler->Orphan();
}
