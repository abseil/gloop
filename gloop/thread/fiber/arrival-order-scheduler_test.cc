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

#include "gloop/thread/fiber/arrival-order-scheduler.h"

#include <functional>
#include <memory>
#include <queue>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/flags.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/init_google.h"
#include "gloop/base/scheduling/domain.h"
#include "gloop/base/scheduling/downcalls.h"
#include "gloop/base/scheduling/scheduler.h"
#include "gloop/thread/fiber/fiber-options.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/fifolifo-schedulers.h"
#include "gloop/thread/fiber/init-domain.h"
#include "gloop/thread/fiber/select.h"
#include "gloop/util/random/shared_bit_gen.h"
#include "gtest/gtest.h"

namespace thread {

// Validates that the currently running fiber is always the "oldest".  We do
// this by maintaining a heap of runnable fibers, ordered by creation.  The
// running fiber always asserts that no older fibers are eligible (in the heap).
static void TestOldestFiberRuns(base::scheduling::Scheduler* scheduler) {
  static std::priority_queue<int> lowest;

  struct Helper {
    static void Worker(int index) {
      util_random::SharedBitGen rng;
      for (int i = 0; i < 500; i++) {
        // We don't need any locking since num_slots() == 1
        lowest.push(index);
        ASSERT_EQ(lowest.top(), index);  // Smallest should always be running.
        base::scheduling::Downcalls::Reschedule();
        lowest.pop();  // "index" not eligible while sleeping.
        double duration = absl::Uniform(rng, 0.1, 1.0);
        ::absl::SleepFor(::absl::Milliseconds(duration));
      }
    }

    static void Parent() {
      static const int kNumChildren = 100;
      std::vector<std::unique_ptr<Fiber>> children(kNumChildren);

      // priority_queue::top() returns largest, so we invert passed "index".
      for (int i = kNumChildren - 1; i >= 0; i--) {
        children[i] = std::make_unique<Fiber>([i] { Worker(i); });
      }
      for (auto& child : children) {
        child->Join();
      }
    }
  };

  auto test = NewTree(TreeOptions().set_scheduler(scheduler), Helper::Parent);
  test->Join();
}

TEST(ArrivalOrderSchedulerTest, OldestFiberRuns) {
  base::scheduling::Scheduler* scheduler = NewChildArrivalOrderScheduler(
      DefaultDomain()->root_scheduler(), 1, ArrivalOrderSchedulerOptions());
  TestOldestFiberRuns(scheduler);
  scheduler->Orphan();
}

TEST(ArrivalOrderSchedulerTest, OldestFiberRunsWithAdmissionLimit) {
  base::scheduling::Scheduler* scheduler = NewChildArrivalOrderScheduler(
      DefaultDomain()->root_scheduler(), 1,
      ArrivalOrderSchedulerOptions().set_admission_limit(50));
  TestOldestFiberRuns(scheduler);
  scheduler->Orphan();
}

// A basic test for admission control.  We create a scheduler with extremely
// limited admissions.  Specifically, 2 schedulables, enough only for a root
// fiber and a single child.
//
// We create a large set of children beneath the root fiber.
// All children start as runnable, each child blocks to guarantee that there is
// concurrency and opportunity for others; although none should ever be
// admitted.  To validate this we ensure that the execution of the preceding
// child is complete (e.g. it is Joinable).
TEST(ArrivalOrderSchedulerTest, AdmissionLimitWorks) {
  struct Helper {
    // REQUIRES: "runs_after" is joinable.
    static void OrderedChild(Fiber* runs_after) {
      if (runs_after) {
        ASSERT_EQ(0, TrySelect({runs_after->OnJoinable()}));
      }
      // Provide a scheduling opportunity.
      ::absl::SleepFor(::absl::Milliseconds(1));
    }

    static void Parent(base::scheduling::Scheduler* tree_scheduler) {
      static const int kNumChildren = 100;
      std::vector<std::unique_ptr<Fiber>> children(kNumChildren);

      children[0] = std::make_unique<Fiber>([] { OrderedChild(nullptr); });
      for (int i = 1; i < kNumChildren; i++) {
        Fiber* prev = children[i - 1].get();
        // There are two cases to try:
        //  (a) A fiber directly attached to tree-scheduler.
        //  (b) A fiber attached to a scheduler attached to tree-scheduler.
        // In the latter the admission of new schedulables is indirect as it is
        // the deletion of the child scheduler's slots which will admit new
        // concurrency.
        //
        // We alternate between the two options.
        if (i % 2 == 0) {
          children[i] = std::make_unique<Fiber>([prev] { OrderedChild(prev); });
        } else {
          base::scheduling::Scheduler* nested_scheduler =
              NewChildArrivalOrderScheduler(tree_scheduler, 1,
                                            ArrivalOrderSchedulerOptions());
          children[i] = NewTree(TreeOptions().set_scheduler(nested_scheduler),
                                std::bind(OrderedChild, prev));
          nested_scheduler->Orphan();
        }
      }

      for (auto& child : children) {
        child->Join();
      }
    }
  };

  // We set an admission limit of 2.  At any given time at most Parent() and
  // one of its children should be allowed to run.
  base::scheduling::Scheduler* limited_scheduler =
      NewChildArrivalOrderScheduler(
          DefaultDomain()->root_scheduler(), 1,
          ArrivalOrderSchedulerOptions().set_admission_limit(2));
  auto test = NewTree(TreeOptions().set_scheduler(limited_scheduler),
                      std::bind(Helper::Parent, limited_scheduler));
  limited_scheduler->Orphan();  // Will free as soon as test completes.
  test->Join();
}

TEST(ArrivalOrderSchedulerTest, GroupAdmissionLimitWorks) {
  // Concurrency of 2, with only a single generation active at one time.
  base::scheduling::Scheduler* limited_scheduler =
      NewChildArrivalOrderScheduler(
          DefaultDomain()->root_scheduler(), 2,
          ArrivalOrderSchedulerOptions().set_admission_limit(1));

  struct Helper {
    static void Parent() {
      int run = 0;
      Fiber child([&run] { Helper::Child(&run); });
      // Unless our child is treated with the same generation as us this will
      // deadlock and result in a test timeout.
      child.Join();
      EXPECT_EQ(run, 1);
    }

    static void Child(int* run) { *run = 1; }
  };

  std::vector<std::unique_ptr<Fiber>> roots(100);
  for (auto& r : roots) {
    base::scheduling::Scheduler* s =
        NewChildFIFOScheduler(limited_scheduler, 2);
    r = NewTree(TreeOptions().set_scheduler(s), Helper::Parent);
    s->Orphan();
  }

  for (auto& r : roots) r->Join();
  limited_scheduler->Orphan();
}

}  // namespace thread

int main(int argc, char** argv) {
  absl::SetFlag(&FLAGS_logtostderr, true);
  InitGoogle(argv[0], &argc, &argv, true);

  return RUN_ALL_TESTS();
}
