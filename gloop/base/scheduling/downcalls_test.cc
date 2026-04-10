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

#include "gloop/base/scheduling/downcalls.h"

#include <stdlib.h>

#include <memory>

#include "absl/base/internal/raw_logging.h"
#include "absl/base/optimization.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/flags.h"
#include "absl/log/log.h"
#include "absl/synchronization/internal/create_thread_identity.h"
#include "absl/synchronization/internal/kernel_timeout.h"
#include "absl/time/time.h"
#include "gloop/base/init_google.h"
#include "gloop/base/scheduling/domain.h"
#include "gloop/base/scheduling/low-level-support.h"
#include "gloop/base/scheduling/scheduler.h"
#include "gtest/gtest.h"

namespace base {
namespace scheduling {

using absl::base_internal::SchedulingGuard;
using absl::synchronization_internal::KernelTimeout;

// Avoid requiring friendship with Domain.
static inline Schedulable* CurrentSchedulable() {
  return Schedulable::GetBoundSchedulable(
      absl::synchronization_internal::GetOrCreateCurrentThreadIdentity());
}

class DowncallsTestlets {
 public:
  static bool Wait(KernelTimeout t) { return Downcalls::Wait(t); }

  static void PostWhenSchedulingAllowed(Schedulable* schedulable) {
    Downcalls::RunWhenSchedulingAllowed(
        reinterpret_cast<Downcalls::QueueableFunction>(Downcalls::Post),
        reinterpret_cast<void*>(schedulable));
  }

  static bool DisableRescheduling() {
    return SchedulingGuard::DisableRescheduling();
  }

  static void EnableRescheduling(bool disable_result) {
    SchedulingGuard::EnableRescheduling(disable_result);
  }
};

TEST(Downcalls, ThreadDisableSchedulingWorks) {
  // Running in a non-cooperative thread.
  EXPECT_FALSE(SchedulingGuard::ReschedulingIsAllowed());
  bool disable_result = DowncallsTestlets::DisableRescheduling();
  EXPECT_FALSE(SchedulingGuard::ReschedulingIsAllowed());
  DowncallsTestlets::EnableRescheduling(disable_result);
  EXPECT_FALSE(SchedulingGuard::ReschedulingIsAllowed());
}

class SelfWakingScheduler : public Scheduler {
 public:
  explicit SelfWakingScheduler(Scheduler* parent)
      : Scheduler(parent, 1),
        slot_(NewManagingSlot()),
        schedulable_(nullptr),
        waking_self_(false) {}

  ~SelfWakingScheduler() override { DeleteManagingSlot(slot_); }

 private:
  Slot Wake(Schedulable* schedulable) override {
    if (!waking_self_) {  // Step (1)   [ See WakeSelfWorks below. ]
      ABSL_RAW_CHECK(schedulable_ == nullptr, "unexpected wakeup");
      schedulable_ = schedulable;
    } else {  // Step (3)
      ABSL_RAW_CHECK(schedulable == CurrentSchedulable(),
                     "expected self wakeup");
    }
    schedulable_ = schedulable;
    return slot_;
  }

  Schedulable* ScheduleManaged(Slot managing_slot, Schedulable* prev,
                               bool runnable) override {
    if (prev == nullptr) {  // Steps (2), (5)
      return schedulable_;
    } else {  // Step (3)
      ABSL_RAW_CHECK(!runnable, "expected blocking");
      ABSL_RAW_CHECK(prev == schedulable_, "unexpected prev");
      DowncallsTestlets::PostWhenSchedulingAllowed(prev);
      waking_self_ = true;
      return nullptr;
    }
  }

  bool StopRunning(Slot managing_slot, Schedulable* current,
                   bool runnable) override {
    LOG(FATAL) << "unimplemented";
  }

  Slot slot_;
  Schedulable* schedulable_;
  bool waking_self_;
};

// Tests that delivery of a self-wakeup, delivered by RunWhenSchedulingAllowed
// is correctly handled.
//
// The exact mechanism is:
// Step 1: Schedulable woken by creating new Fiber
// Step 2: Schedulable created above is selected to run, WaitToBeWoken() starts.
// Step 3: WaitToBeWoken() calls Wait() --> ScheduleManaged(..., false)
//         Queue a post against ourselves.
// Step 4: Queued Downcalls::Post is delivered, self wake-up received.
// Step 5: Locally, reschedule self (observed by WaitToBeWoken()).

TEST(Schedulable, FlagsWork) {
  void* backing;
  Schedulable* s;

  // Schedulables must be cache-aligned.
  PCHECK(posix_memalign(&backing, ABSL_CACHELINE_SIZE, sizeof(Schedulable)) ==
         0);
  s = new (backing) Schedulable(nullptr, Schedulable::kWorkItem);

  // All flags should be clear.
  EXPECT_FALSE(s->clear_manager_flag(2));  // Nop
  for (int i = 0; i < 31; i++) {
    EXPECT_FALSE(s->is_flag_set(i));
  }

  // Set a single flag.
  EXPECT_TRUE(s->set_manager_flag(0));
  EXPECT_FALSE(s->set_manager_flag(0));  // Double-setting should be a nop.

  EXPECT_TRUE(s->is_flag_set(0));
  for (int i = 1; i < 31; i++) {
    EXPECT_FALSE(s->is_flag_set(i));
  }

  // Set a second flag.
  EXPECT_TRUE(s->set_manager_flag(3));
  EXPECT_TRUE(s->is_flag_set(3));
  EXPECT_TRUE(s->is_flag_set(0));  // Should still be set.
  for (int i = 1; i < 31; i++) {   // All other flags should remain unset.
    if (i != 3) {
      EXPECT_FALSE(s->is_flag_set(i));
    }
  }

  // Clear our first flag.
  EXPECT_TRUE(s->clear_manager_flag(3));
  EXPECT_FALSE(s->clear_manager_flag(3));  // Should already be cleared.
  EXPECT_FALSE(s->is_flag_set(3));
  EXPECT_FALSE(s->is_flag_set(1));  // Arbitrary.
  EXPECT_TRUE(s->is_flag_set(0));   // Should still be set.

  // Free storage.
  s->~Schedulable();
  free(backing);
}

}  // namespace scheduling
}  // namespace base

int main(int argc, char** argv) {
  InitGoogle(argv[0], &argc, &argv, true);

  // Ensure that a ThreadIdentity is installed for all tests and benchmarks.
  CHECK(absl::synchronization_internal::GetOrCreateCurrentThreadIdentity() !=
        nullptr);

  return RUN_ALL_TESTS();
}
