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

//
// A bunch of fibers repeatedly hash an array of ints protected by a
// spinlock.  If the spinlock is working properly, all elements of the
// array should be identically at the end of the test.
//
// This test uses non-portable google3 APIs such as Fibers and benchmarks.
// See //absl/base:spinlock_test_common for tests using the
// portable APIs.

#include "gloop/base/spinlock.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/base_config.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/internal/create_thread_identity.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/commandlineflags.h"
#include "gloop/base/init_google.h"
#include "gloop/base/scheduling/domain.h"
#include "gloop/base/scheduling/low-level-support.h"
#include "gloop/base/scheduling/scheduler.h"
#include "gloop/base/scheduling/scheduling_mode.h"
#include "gloop/base/sysinfo.h"
#include "gloop/thread/fiber/fiber-options.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/fifolifo-schedulers.h"
#include "gloop/thread/fiber/init-domain.h"
#include "gloop/util/hash/legacy_hash.h"
#include "gtest/gtest.h"

DEFINE_int32(numthreads, 10, "Number of threads");
DEFINE_int32(iters, 10, "Number of iterations per thread");
ABSL_DECLARE_FLAG(absl::Duration, experimental_spinlock_wait_max_delay);

namespace {

static const int kArrayLength = 10;
static uint32_t values[kArrayLength];

ABSL_CONST_INIT static SpinLock static_cooperative_spinlock;
ABSL_CONST_INIT static SpinLock static_noncooperative_spinlock(
    base::scheduling::SCHEDULE_KERNEL_ONLY);

static void TestFunction(SpinLock* spinlock) {
  int tid = GetTID();
  for (int i = 0; i < absl::GetFlag(FLAGS_iters); i++) {
    SpinLockHolder h(*spinlock);
    for (int j = 0; j < kArrayLength; j++) {
      const int index = (j + tid) % kArrayLength;
      values[index] = HashTo32(values[index] + tid);
      absl::SleepFor(absl::Milliseconds(1));
    }
  }
}

static void FiberTest(SpinLock* spinlock) {
  // Use default domain & scheduler.
  base::scheduling::Scheduler* parent =
      thread::DefaultDomain()->root_scheduler();
  base::scheduling::Scheduler* scheduler = thread::NewChildLIFOScheduler(
      parent, absl::GetFlag(FLAGS_numthreads) / 2);
  std::vector<std::unique_ptr<thread::Fiber> > fibers(
      absl::GetFlag(FLAGS_numthreads));

  for (int i = 0; i < absl::GetFlag(FLAGS_numthreads); ++i) {
    fibers[i] = thread::NewTree(thread::TreeOptions().set_scheduler(scheduler),
                                absl::bind_front(TestFunction, spinlock));
  }

  for (int i = 0; i < absl::GetFlag(FLAGS_numthreads); ++i)
    fibers[i]->Join();  // Wait

  spinlock->lock();
  for (int i = 1; i < kArrayLength; i++) {
    EXPECT_EQ(values[0], values[i]);
  }
  spinlock->unlock();
  scheduler->Orphan();
}

#if !PORTABLE_BASE
TEST(SpinLock, StackCooperativeAllowsScheduling) {
  struct Helper {
    static void TestInFiber() {
      SpinLock spinlock;
      spinlock.lock();
      EXPECT_TRUE(base::scheduling::SchedulingGuard::ReschedulingIsAllowed());
      spinlock.unlock();
    }
  };

  thread::Fiber f(Helper::TestInFiber);
  f.Join();
}

TEST(SpinLock, StaticCooperativeAllowsScheduling) {
  struct Helper {
    static void TestInFiber() {
      static_cooperative_spinlock.lock();
      EXPECT_TRUE(base::scheduling::SchedulingGuard::ReschedulingIsAllowed());
      static_cooperative_spinlock.unlock();
    }
  };

  thread::Fiber f(Helper::TestInFiber);
  f.Join();
}
#endif

TEST(SpinLockWithFibers, StackSpinLock) {
  SpinLock spinlock;
  FiberTest(&spinlock);
}

TEST(SpinLockWithFibers, StackCooperativeSpinLock) {
  SpinLock spinlock;
  FiberTest(&spinlock);
}

TEST(SpinLockWithFibers, StackCooperativeSpinLockDeepSleep) {
  SpinLock spinlock;
  FiberTest(&spinlock);
}

TEST(SpinLockWithFibers, StackNonCooperativeSpinLockDeepSleep) {
  SpinLock spinlock(base::scheduling::SCHEDULE_KERNEL_ONLY);
  FiberTest(&spinlock);
}

TEST(SpinLockWithFibers, StackNonCooperativeSpinLock) {
  SpinLock spinlock(base::scheduling::SCHEDULE_KERNEL_ONLY);
  FiberTest(&spinlock);
}

TEST(SpinLockWithFibers, StaticCooperativeSpinLock) {
  FiberTest(&static_cooperative_spinlock);
}

TEST(SpinLockWithFibers, StaticNonCooperativeSpinLock) {
  FiberTest(&static_noncooperative_spinlock);
}

TEST(SpinLockWithFibers, DoesNotDeadlock) {
  struct Helper {
    static void NotifyThenLock(absl::Notification* locked, SpinLock* spinlock,
                               absl::BlockingCounter* b) {
      locked->WaitForNotification();  // Wait for LockThenWait() to hold "s".
      b->DecrementCount();
      SpinLockHolder l(*spinlock);
    }

    static void LockThenWait(absl::Notification* locked, SpinLock* spinlock,
                             absl::BlockingCounter* b) {
      SpinLockHolder l(*spinlock);
      locked->Notify();
      b->Wait();
    }

    static void DeadlockTest(SpinLock* spinlock, int num_spinners) {
      absl::Notification locked;
      absl::BlockingCounter counter(num_spinners);
      std::vector<std::unique_ptr<thread::Fiber> > fibers(1 + num_spinners);

      base::scheduling::Scheduler* scheduler = thread::NewChildLIFOScheduler(
          thread::DefaultDomain()->root_scheduler(), 1);
      fibers[0] = thread::NewTree(
          thread::TreeOptions().set_scheduler(scheduler),
          absl::bind_front(Helper::LockThenWait, &locked, spinlock, &counter));
      for (int i = 0; i < num_spinners; ++i) {
        fibers[i + 1] =
            thread::NewTree(thread::TreeOptions().set_scheduler(scheduler),
                            absl::bind_front(Helper::NotifyThenLock, &locked,
                                             spinlock, &counter));
      }

      for (std::unique_ptr<thread::Fiber>& fiber : fibers) fiber->Join();
      scheduler->Orphan();
    }
  };

  SpinLock stack_cooperative_spinlock;
  const int kNumSpinners = std::min(NumCPUs(), 64);
  Helper::DeadlockTest(&stack_cooperative_spinlock, kNumSpinners);
  Helper::DeadlockTest(&static_cooperative_spinlock, kNumSpinners);
}

static_assert(!std::is_copy_constructible<SpinLockHolder>(),
              "RAII types like SpinLockHolder should not be copyable.");

}  // namespace

int main(int argc, char** argv) {
  InitGoogle(argv[0], &argc, &argv, true);
  // Ensure that a ThreadIdentity is installed for all tests.
  CHECK(base::subtle::GetOrCreateCurrentThreadIdentity() != nullptr);
  return RUN_ALL_TESTS();
}
