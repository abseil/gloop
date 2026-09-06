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

#include "gloop/thread/fiber/priority_admission_scheduler.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>  // NOLINT(build/c++11)
#include <vector>

#include "absl/base/config.h"
#include "absl/log/check.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/scheduling/downcalls.h"
#include "gloop/thread/fiber/bundle.h"
#include "gloop/thread/fiber/fiber-options.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/fifolifo-schedulers.h"
#include "gloop/thread/fiber/init-domain.h"
#include "gloop/thread/fiber/select.h"
#include "gtest/gtest.h"

using base::scheduling::Domain;
using base::scheduling::Downcalls;

namespace thread {

// When we expect something does not get notified or joinable, the fixed
// timeout is really problematic:
// - To avoid flaky tests on loaded systems, it must be long enough, yet
// - To make sure tests finish fast, it must be short enough.
//
// In term of correctness, since the timeout is not infinitely long, both
// functions may produce false negative. So writing EXPECT_TRUE with them
// would make tests flaky, and EXPECT_FALSE would make them pass when they
// should not. The former is more a problem because it makes the tests not
// dependable, while the latter can be fixed by running the tests many
// times. As a result, we choose to avoid EXPECT_TRUE with them.
//
// TODO: find a way to directly assert the scheduling decision.
bool NotifiedOrTimedOut(const absl::Notification& n) {
  return n.WaitForNotificationWithTimeout(absl::Milliseconds(30));
}
bool FinishedOrTimedOut(thread::Fiber* fiber) {
  return thread::SelectUntil(absl::Now() + absl::Milliseconds(30),
                             {fiber->OnJoinable()}) == 0;
}

// Helpers for atomic operations to make memory ordering explicit and clean.
// Memory ordering is important for these tests to remain deterministic.
void Set(std::atomic<bool>& flag) {
  flag.store(true, std::memory_order_release);
}

bool IsSet(const std::atomic<bool>& flag) {
  return flag.load(std::memory_order_acquire);
}

// We use Spin() to pause scheduling events, and then add other fibers to
// the scheduler, so as to probe the scheduling decision with deterministic
// input.
//
// We use architecture-specific CPU pause/yield instructions to prevent CPU
// pipeline starvation on hyperthreaded or virtualized test runners while
// spinning.
//
// Note: We use atomic flags and Spin() rather than absl::Notification in many
// of these tests. Using absl::Notification yields the fiber to the scheduler,
// which breaks the deterministic scheduling state we are trying to test and
// causes flakiness. Spin() allows us to hold the execution slot and pause
// scheduling events deterministically.
void Spin(const std::atomic<bool>& exit) {
  [[maybe_unused]] uint64_t spin = 0;
  while (!IsSet(exit)) {
#if ABSL_HAVE_BUILTIN(__builtin_ia32_pause)
    __builtin_ia32_pause();
#elif ABSL_HAVE_BUILTIN(__builtin_arm_yield)
    __builtin_arm_yield();
#else
    ++spin;
#endif
  }
}

class FiberSchedulerTest : public testing::TestWithParam<bool> {
 protected:
  static Scheduler* NewChildTreeScheduler(PriorityAdmissionScheduler* root,
                                          int priority, int num_slots) {
    Scheduler* child = thread::NewChildLIFOScheduler(root, num_slots);
    root->SetChildPriority(child, priority);
    return child;
  }

  static std::unique_ptr<thread::Fiber> NewTestTree(
      PriorityAdmissionScheduler* root, int priority, int num_slots,
      std::function<void()> function) {
    Scheduler* child = NewChildTreeScheduler(root, priority, num_slots);
    std::unique_ptr<thread::Fiber> fiber =
        thread::NewTree(thread::TreeOptions().set_scheduler(child), function);
    child->Orphan();
    return fiber;
  }

  static std::unique_ptr<thread::Fiber> NewTestTree(
      PriorityAdmissionScheduler* root, int priority,
      std::function<void()> function) {
    return NewTestTree(root, priority, 1, function);
  }

  static void Yield() {
    if (Downcalls::CurrentThreadIsCooperative()) {
      Downcalls::Reschedule();
    } else {
      std::this_thread::yield();
      absl::SleepFor(absl::Milliseconds(30));
    }
  }

  PriorityAdmissionScheduler* NewRootScheduler(int num_slots,
                                               int num_priorities) {
    if (GetParam()) {
      // Use PriorityAdmissionScheduler as domain root scheduler.
      custom_domain_ = thread::CreateCustomDomain(
          {.name = "Test", .max_concurrency = num_slots});
      return new PriorityAdmissionScheduler(custom_domain_.get(),
                                            num_priorities);
    } else {
      // Use PriorityAdmissionScheduler as a child of domain root scheduler.
      return new PriorityAdmissionScheduler(
          thread::DefaultDomain()->root_scheduler(), num_slots, num_priorities);
    }
  }

  std::unique_ptr<Domain> custom_domain_;
};

TEST_P(FiberSchedulerTest, Priority) {
  PriorityAdmissionScheduler* root = NewRootScheduler(1, 2);
  absl::Notification s1, s2, s3;
  std::atomic<bool> e1(false), e2(false), e3(false);
  auto f1 = NewTestTree(root, 0, [&s1, &e1]() {
    s1.Notify();
    Spin(e1);
  });
  s1.WaitForNotification();
  auto f2 = NewTestTree(root, 1, [&s2, &e2]() {
    s2.Notify();
    Spin(e2);
  });
  auto f3 = NewTestTree(root, 0, [&s3, &e3]() {
    s3.Notify();
    Spin(e3);
  });
  auto f4 = NewTestTree(root, 1, []() {});
  Yield();
  EXPECT_EQ(3, root->num_queued());
  EXPECT_EQ(1, root->num_running());

  Set(e1);
  Yield();
  f1->Join();
  // Schedule f3 due to priority.
  EXPECT_FALSE(NotifiedOrTimedOut(s2));
  s3.WaitForNotification();
  EXPECT_FALSE(FinishedOrTimedOut(f4.get()));
  EXPECT_EQ(2, root->num_queued());
  EXPECT_EQ(1, root->num_running());

  Set(e3);
  Yield();
  f3->Join();
  // Schedule f2 due to FIFO.
  s2.WaitForNotification();
  EXPECT_FALSE(FinishedOrTimedOut(f4.get()));
  EXPECT_EQ(1, root->num_queued());
  EXPECT_EQ(1, root->num_running());

  Set(e2);
  Yield();
  f2->Join();
  f4->Join();
  Yield();
  EXPECT_EQ(0, root->num_queued());
  EXPECT_EQ(0, root->num_running());
  root->Orphan();
}

TEST_P(FiberSchedulerTest, InProgress) {
  PriorityAdmissionScheduler* root = NewRootScheduler(1, 2);
  absl::Notification n1;
  std::atomic<bool> s1(false), s2(false), s3(false);
  std::atomic<bool> e1(false), e2(false);
  auto f1 = NewTestTree(root, 1, [&n1, &s1, &e1]() {
    Set(s1);
    n1.WaitForNotification();
    Spin(e1);
  });
  Spin(s1);
  EXPECT_EQ(1, root->num_running());
  auto f2 = NewTestTree(root, 0, [&s2, &e2]() {
    Set(s2);
    Spin(e2);
  });
  auto f3 = NewTestTree(root, 0, [&s3]() { Set(s3); });
  Spin(s2);
  n1.Notify();
  EXPECT_EQ(2, root->num_running());

  // - Low priority in progress f1 runs and blocks.
  // - High priority f2 spins.
  // - Now f1 is unblocked.
  // As f2 finishes, schedule f1 over f3, because f1 is in progress,
  // despite of its low priority.
  Set(e2);
  Yield();
  f2->Join();
  EXPECT_FALSE(IsSet(s3));
  EXPECT_EQ(1, root->num_running());

  Set(e1);
  Yield();
  f1->Join();
  f3->Join();
  Yield();
  EXPECT_EQ(0, root->num_running());
  root->Orphan();
}

TEST_P(FiberSchedulerTest, InProgressChild) {
  PriorityAdmissionScheduler* root = NewRootScheduler(1, 2);
  std::atomic<bool> n11(false), n12(false);
  std::atomic<bool> e11(false), e12(false);
  auto f1 = NewTestTree(root, 1, [&n11, &e11, &n12, &e12]() {
    thread::Bundle bundle;
    bundle.Add([&n11, &e11]() {
      Set(n11);
      Spin(e11);
    });
    bundle.Add([&n12, &e12]() {
      Set(n12);
      Spin(e12);
    });
    bundle.JoinAll();
  });
  // Child scheduler is LIFO so 2nd child gets scheduled.
  EXPECT_FALSE(IsSet(n11));
  Spin(n12);
  auto f2 = NewTestTree(root, 0, []() {});
  Yield();
  EXPECT_EQ(1, root->num_queued());
  EXPECT_EQ(1, root->num_running());  // f1

  // Since f1 started, both f1's children inherit high "in progress"
  // priority, thus chosen over f2 despite its higher original priority.
  Set(e12);
  Spin(n11);
  EXPECT_FALSE(FinishedOrTimedOut(f2.get()));
  EXPECT_EQ(1, root->num_queued());
  EXPECT_EQ(1, root->num_running());  // f1

  Set(e11);
  Yield();
  f1->Join();
  f2->Join();
  Yield();
  EXPECT_EQ(0, root->num_queued());
  EXPECT_EQ(0, root->num_running());
  root->Orphan();
}

TEST_P(FiberSchedulerTest, MultipleSlots) {
  PriorityAdmissionScheduler* root = NewRootScheduler(2, 2);
  std::vector<absl::Notification> s(20), n(20);
  std::vector<std::unique_ptr<thread::Fiber>> f(20);
  for (int i = 0; i < 20; ++i) {
    absl::Notification& ss = s[i];
    const absl::Notification& nn = n[i];
    auto ff = [&ss, &nn]() {
      ss.Notify();
      nn.WaitForNotification();
    };
    if (i % 4 == 0) {
      f[i] = NewTestTree(root, 0, ff);
    } else if (i % 4 == 1) {
      f[i] = NewTestTree(root, 1, ff);
    } else if (i % 4 == 2) {
      // Priority missing.
      Scheduler* child = thread::NewChildLIFOScheduler(root, 1);
      f[i] = thread::NewTree(thread::TreeOptions().set_scheduler(child), ff);
      child->Orphan();
    } else {
      // Directly add work item.
      f[i] = thread::NewTree(thread::TreeOptions().set_scheduler(root), ff);
    }
  }
  for (int i = 0; i < 20; ++i) {
    s[i].WaitForNotification();
  }
  EXPECT_EQ(0, root->num_queued());
  EXPECT_EQ(20, root->num_running());
  for (int i = 0; i < 20; ++i) {
    n[i].Notify();
  }
  Yield();
  for (int i = 0; i < 20; ++i) {
    f[i]->Join();
  }
  Yield();
  EXPECT_EQ(0, root->num_queued());
  EXPECT_EQ(0, root->num_running());
  root->Orphan();
}

TEST_P(FiberSchedulerTest, MultipleSlotsPerChild) {
  PriorityAdmissionScheduler* root = NewRootScheduler(2, 2);
  std::atomic<bool> n0(false), n11(false), n12(false);
  std::atomic<bool> e0(false), e11(false), e12(false);
  auto f0 = NewTestTree(root, 0, [&n0, &e0]() {
    Set(n0);
    Spin(e0);
  });
  auto f1 = NewTestTree(root, 1, 2, [&n11, &e11, &n12, &e12]() {
    thread::Bundle bundle;
    bundle.Add([&n11, &e11]() {
      Set(n11);
      Spin(e11);
    });
    bundle.Add([&n12, &e12]() {
      Set(n12);
      Spin(e12);
    });
    bundle.JoinAll();
  });
  Spin(n0);
  // Child scheduler is LIFO so 2nd child gets scheduled.
  EXPECT_FALSE(IsSet(n11));
  Spin(n12);
  EXPECT_EQ(0, root->num_queued());
  auto f2 = NewTestTree(root, 0, []() {});
  Yield();
  EXPECT_EQ(1, root->num_queued());   // f2
  EXPECT_EQ(2, root->num_running());  // f0 and f1

  // Since f1 started, f1's first child inherits the high "in progress"
  // priority, thus chosen over f2 which has higher original priority.
  Set(e0);
  Yield();
  f0->Join();
  Spin(n11);
  EXPECT_FALSE(FinishedOrTimedOut(f2.get()));
  EXPECT_EQ(1, root->num_queued());   // f2
  EXPECT_EQ(1, root->num_running());  // f1

  Set(e11);
  Set(e12);
  Yield();
  f1->Join();
  f2->Join();
  Yield();
  EXPECT_EQ(0, root->num_queued());
  EXPECT_EQ(0, root->num_running());
  root->Orphan();
}

TEST_P(FiberSchedulerTest, PriorityForManagingSlots) {
  PriorityAdmissionScheduler* root = NewRootScheduler(2, 2);
  std::atomic<bool> s1(false), s2(false), s3(false), s4(false);
  std::atomic<bool> e1(false), e2(false), e3(false), e4(false);
  std::atomic<bool> f1_spin(false), f2_spin(false);
  auto f1 = NewTestTree(root, 0, [&s1, &e1, &f1_spin]() {
    Set(f1_spin);
    Set(s1);
    Spin(e1);
  });
  auto f2 = NewTestTree(root, 0, [&s2, &e2, &f2_spin]() {
    Set(f2_spin);
    Set(s2);
    Spin(e2);
  });
  Scheduler* child = NewChildTreeScheduler(root, 1, 2);
  auto f3 =
      thread::NewTree(thread::TreeOptions().set_scheduler(child), [&s3, &e3]() {
        Set(s3);
        Spin(e3);
      });
  auto f4 =
      thread::NewTree(thread::TreeOptions().set_scheduler(child), [&s4, &e4]() {
        Set(s4);
        Spin(e4);
      });
  child->Orphan();
  Spin(s1);
  Spin(s2);
  Spin(f1_spin);
  Spin(f2_spin);
  EXPECT_EQ(1, root->num_queued());   // (f3, f4) as 1
  EXPECT_EQ(2, root->num_running());  // f1 and f2

  Set(e1);
  Yield();
  f1->Join();
  // Child scheduler is LIFO so f4 is scheduled.
  EXPECT_FALSE(IsSet(s3));
  Spin(s4);
  EXPECT_EQ(2, root->num_running());  // f2 and f4

  auto f5 = NewTestTree(root, 0, []() {});
  Set(e2);
  Yield();
  f2->Join();
  // Priority is assigned to scheduler, not schedulable, thus f3 and f4 are
  // both boosted to "in progress" priority once one of them gets run. Now
  // f3 wins over f5 despite its lower original priority.
  Spin(s3);
  EXPECT_FALSE(FinishedOrTimedOut(f5.get()));
  EXPECT_EQ(1, root->num_queued());   // f5
  EXPECT_EQ(1, root->num_running());  // (f3, f4) as 1

  Set(e3);
  Set(e4);
  Yield();
  f3->Join();
  f4->Join();
  f5->Join();
  Yield();
  EXPECT_EQ(0, root->num_queued());
  EXPECT_EQ(0, root->num_running());
  root->Orphan();
}

TEST_P(FiberSchedulerTest, TailPointerWhenBoostPriority) {
  PriorityAdmissionScheduler* root = NewRootScheduler(2, 1);
  std::atomic<bool> s1(false), s2(false);
  std::atomic<bool> e1(false), e2(false);
  auto f1 = NewTestTree(root, 0, [&s1, &e1]() {
    Set(s1);
    Spin(e1);
  });
  auto f2 = NewTestTree(root, 0, [&s2, &e2]() {
    Set(s2);
    Spin(e2);
  });
  Scheduler* child = NewChildTreeScheduler(root, 0, 2);
  auto f3 =
      thread::NewTree(thread::TreeOptions().set_scheduler(child), []() {});
  auto f4 = NewTestTree(root, 0, []() {});
  auto f5 =
      thread::NewTree(thread::TreeOptions().set_scheduler(child), []() {});
  child->Orphan();
  Spin(s1);
  Spin(s2);
  EXPECT_EQ(2, root->num_queued());   // (f3, f5) and f4
  EXPECT_EQ(2, root->num_running());  // f1 and f2

  Set(e1);
  Set(e2);
  Yield();
  f1->Join();
  f2->Join();
  f3->Join();
  f4->Join();
  f5->Join();
  Yield();
  EXPECT_EQ(0, root->num_queued());
  EXPECT_EQ(0, root->num_running());
  root->Orphan();
}

TEST_P(FiberSchedulerTest, YieldingDoesNotDeadlock) {
  PriorityAdmissionScheduler* root = NewRootScheduler(1, 1);
  absl::Notification n1;
  std::atomic<bool> s1(false), s2(false);
  std::atomic<bool> y2(true);
  auto f1 = NewTestTree(root, 0, [&s1, &n1]() {
    Set(s1);
    n1.WaitForNotification();
  });
  auto f2 = NewTestTree(root, 0, [&s2, &y2]() {
    Set(s2);
    [[maybe_unused]] int yield = 0;
    while (IsSet(y2)) {
      ++yield;
      CHECK(Downcalls::CurrentThreadIsCooperative());
      Domain::CurrentDomain()->ScheduleNextFromRoot();
      Downcalls::Reschedule();
    }
  });
  Spin(s1);
  Spin(s2);
  EXPECT_EQ(2, root->num_running());

  // The intended behavior is that, as f2 yielding, it should not block f1
  // once f1 becomes runnable. The rescheduling only reaches to the child
  // (tree) scheduler which is unaware of f1, so we have to manually
  // reschedule from root.
  n1.Notify();
  f1->Join();
  Yield();
  EXPECT_EQ(1, root->num_running());

  y2.store(false, std::memory_order_release);
  f2->Join();
  Yield();
  EXPECT_EQ(0, root->num_queued());
  EXPECT_EQ(0, root->num_running());
  root->Orphan();
}

TEST_P(FiberSchedulerTest, YieldingChild) {
  PriorityAdmissionScheduler* root = NewRootScheduler(1, 1);
  std::atomic<bool> s1(false), s2(false);
  std::atomic<bool> y2(true);
  Scheduler* child = NewChildTreeScheduler(root, 0, 1);
  auto f = thread::NewTree(thread::TreeOptions().set_scheduler(child),
                           [&s1, &s2, &y2]() {
                             thread::Bundle bundle;
                             bundle.Add([&s1]() { Set(s1); });
                             bundle.Add([&s2, &y2]() {
                               Set(s2);
                               [[maybe_unused]] int yield = 0;
                               while (IsSet(y2)) {
                                 ++yield;
                                 CHECK(Downcalls::CurrentThreadIsCooperative());
                                 Downcalls::Reschedule();
                               }
                             });
                             bundle.JoinAll();
                           });
  child->Orphan();

  // LIFO within the tree so second child gets scheduled, then it yields,
  // which should get first child running.
  Spin(s1);
  Spin(s2);
  EXPECT_EQ(1, root->num_running());  // 1 child scheduler only

  y2.store(false, std::memory_order_release);
  f->Join();
  Yield();
  EXPECT_EQ(0, root->num_queued());
  EXPECT_EQ(0, root->num_running());
  root->Orphan();
}

TEST_P(FiberSchedulerTest, NoExplicitPrioritySet) {
  // When users do not call SetChildPriority or directly add kWorkItem to
  // PriorityAdmissionScheduler, things still work.
  PriorityAdmissionScheduler* root = NewRootScheduler(2, 2);
  Scheduler* child = thread::NewChildLIFOScheduler(root, 1);
  auto f1 =
      thread::NewTree(thread::TreeOptions().set_scheduler(child), []() {});
  child->Orphan();
  auto f2 = thread::NewTree(thread::TreeOptions().set_scheduler(root), []() {});
  f1->Join();
  f2->Join();
  root->Orphan();
}

TEST_P(FiberSchedulerTest, DestroyBeforeWake) {
  PriorityAdmissionScheduler* root = NewRootScheduler(2, 2);
  Scheduler* child = NewChildTreeScheduler(root, 0, 1);
  child->Orphan();
  root->Orphan();
}

INSTANTIATE_TEST_SUITE_P(AsDomainRoot, FiberSchedulerTest, testing::Bool());

}  // namespace thread
