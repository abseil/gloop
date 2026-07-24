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

#include "gloop/thread/fiber/fiber.h"

#include <pthread.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <random>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/const_init.h"
#include "absl/base/internal/thread_identity.h"
#include "absl/base/thread_annotations.h"
#include "absl/debugging/leak_check.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "benchmark/benchmark.h"
#include "gloop/base/callback.h"
#include "gloop/base/cancellation_coloring.h"
#include "gloop/base/context.h"
#include "gloop/base/scheduling/domain.h"
#include "gloop/base/scheduling/downcalls.h"
#include "gloop/base/scheduling/scheduler.h"
#include "gloop/base/thread-identity.h"
#include "gloop/base/tracecontext.h"
#include "gloop/thread/executor.h"
#include "gloop/thread/fiber/bundle.h"
#include "gloop/thread/fiber/channel.h"
#include "gloop/thread/fiber/fiber-internal.h"
#include "gloop/thread/fiber/fiber-options.h"
#include "gloop/thread/fiber/fifolifo-schedulers.h"
#include "gloop/thread/fiber/init-domain.h"
#include "gloop/thread/fiber/internal/fiber-thread-options.h"
#include "gloop/thread/fiber/internal/fiber-thread-pool.h"
#include "gloop/thread/fiber/select.h"
#include "gloop/thread/fiber/selectables.h"
#include "gloop/thread/fiber/sleep.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_manager.h"
#include "gloop/util/functional/callable_once.h"
#include "gloop/util/functional/from_callback.h"
#include "gloop/util/functional/to_callback.h"
#include "gtest/gtest.h"

ABSL_FLAG(absl::Duration, wait_timeout_stress_test_duration, absl::Seconds(3),
          "Wait timeout stress test duration.");

#if !defined(__ANDROID__) && !defined(__APPLE__) && !defined(__Fuchsia__) && \
    !defined(ABSL_HAVE_ADDRESS_SANITIZER)
// TODO: jack this up as we improve support.
constexpr int kDefaultFiberTestCount = 5 * 1000;
#else
// Tests on these OS run with limited resources.
constexpr int kDefaultFiberTestCount = 500;
#endif
ABSL_FLAG(int32_t, fiber_test_count, kDefaultFiberTestCount,
          "Spawn this many fibers in the scaling test");

ABSL_FLAG(bool, fiber_test_wait_for_input, false,
          "Stall once fibers are spawned and working in the scaling test; "
          "useful for debugging");

namespace thread {

// Strong implementation for testing leaving a breadcrumb in thread_status.
void AdjustDefaultTreeContext(base::Context& context, const void*) {
  context.set_thread_status("Adjusted");
}

extern bool StackShouldIncludeGuardSize();

namespace internal {
extern int InternalRequestedStackSizeToStackSizeClass(
    size_t requested_stack_size, int flag_requested_stack_size);
}

static void Error(int e, int* s) { *s = e; }

static void Sleep(absl::Duration d, int e, int* s) {
  absl::SleepFor(d);
  *s = e;
}

static void WaitForCancel() {
  Select({OnCancel()});
  EXPECT_TRUE(thread::Cancelled());
}

static void Notify(absl::Notification* notification) { notification->Notify(); }

static void DoNothing() {}

static void VectorSum(std::unique_ptr<std::vector<int>> ints, int* sum) {
  *sum = 0;
  for (int i : *ints) {
    *sum += i;
  }
}

class FiberTest : public ::testing::Test {};

TEST_F(FiberTest, Run) {
  int e;
  Fiber fiber(std::bind(Error, 100, &e));
  fiber.Join();
  EXPECT_EQ(100, e);
}

TEST_F(FiberTest, RunWithOptions) {
  int e;
  Fiber fiber(FiberOptions(), std::bind(Error, 100, &e));
  fiber.Join();
  EXPECT_EQ(100, e);
}

TEST_F(FiberTest, RunLegacy) {
  int e;
  Fiber fiber([&e] { Error(100, &e); });
  fiber.Join();
  EXPECT_EQ(100, e);
}

TEST_F(FiberTest, RepeatedJoin) {
  int e;
  Fiber fiber(std::bind(Error, 100, &e));
  fiber.Join();
  EXPECT_EQ(100, e);
  fiber.Join();
}

TEST_F(FiberTest, RunMulti) {
  int a, b;
  Fiber fiber1(std::bind(Error, 101, &a));
  Fiber fiber2(std::bind(Error, 102, &b));
  fiber2.Join();
  EXPECT_EQ(102, b);
  fiber1.Join();
  EXPECT_EQ(101, a);
}

TEST_F(FiberTest, BasicStorage) {
  int a = 0;
  int b = 0;
  std::pair<Fiber, Fiber> store{std::bind(Error, 101, &a),
                                std::bind(Error, 102, &b)};
  store.second.Join();
  EXPECT_EQ(102, b);
  store.first.Join();
  EXPECT_EQ(101, a);
}

TEST_F(FiberTest, BindFront) {
  int a = 0;
  Fiber fiber(absl::bind_front(Error, 100, &a));
  fiber.Join();
  EXPECT_EQ(a, 100);
}

TEST_F(FiberTest, BindFront_RvalueReferenceArgs) {
  std::vector<int> xs = {1, 2, 3};
  int sum = 0;
  Fiber fiber(absl::bind_front(
      [&sum](std::vector<int>&& reference_arg) {
        VectorSum(std::make_unique<std::vector<int>>(std::move(reference_arg)),
                  &sum);
      },
      std::move(xs)));
  fiber.Join();
  EXPECT_EQ(sum, 1 + 2 + 3);
}

TEST_F(FiberTest, CallAtMostOnce) {
  auto move_only =
      std::make_unique<std::vector<int>>(std::vector<int>{1, 2, 3});
  int sum = 0;
  Fiber fiber(::util::functional::CallAtMostOnce(
      absl::bind_front(VectorSum, std::move(move_only), &sum)));
  fiber.Join();
  EXPECT_EQ(sum, 1 + 2 + 3);
}

TEST_F(FiberTest, MoveOnlyInvocable) {
  auto move_only =
      std::make_unique<std::vector<int>>(std::vector<int>{1, 2, 3});
  int sum = 0;
  Fiber fiber([m = std::move(move_only), &sum]() mutable {
    VectorSum(std::move(m), &sum);
  });
  fiber.Join();
  EXPECT_EQ(sum, 1 + 2 + 3);
}

TEST_F(FiberTest, Nested) {
  struct State {
    static void Doit(int* e) {
      Fiber child(std::bind(Error, 200, e));
      child.Join();
    }
  };
  int e;
  Fiber fiber(std::bind(&State::Doit, &e));
  fiber.Join();
  EXPECT_EQ(200, e);
}

TEST_F(FiberTest, NestedMultiple) {
  struct State {
    static void Doit() {
      static const int N = 100;
      Fiber* children[N];
      int result[N];
      for (int i = 0; i < N; i++) {
        children[i] = new Fiber(
            std::bind(Sleep, absl::Seconds(0.1), 300 + i, &result[i]));
      }
      for (int i = 0; i < N; i++) {
        children[i]->Join();
        EXPECT_EQ(300 + i, result[i]);
        delete children[i];
      }
    }
  };
  Fiber fiber(&State::Doit);
  fiber.Join();
}

// Join() should mark the end of parent<->child interactions.  We should not
// wait, or depend on, the destructor.
TEST_F(FiberTest, JoinWaitsForChildJoinNotDestructor) {
  static const absl::Duration kDelay = absl::Milliseconds(10);
  absl::Time start = absl::Now();
  Fiber* child;
  Fiber fiber([&child] {
    child = new Fiber(std::bind(absl::SleepFor, kDelay));
    child->Join();
  });
  fiber.Join();

  const absl::Time finish = absl::Now();
  EXPECT_GT(finish - start, kDelay);
  delete child;
}

static void RunThenJoin(std::function<void()> run_in_fiber,
                        absl::BlockingCounter* counter) {
  Fiber f(run_in_fiber);
  f.Join();
  counter->DecrementCount();
}

TEST_F(FiberTest, CreateFromDefaultManagedQueue) {
  // There was previously a bug where creating fibers from threads owned by
  // DefaultQueue() sometimes caused corruption of the per-thread 'current
  // fiber' pointer.
  absl::BlockingCounter counter(1000);
  for (int i = 0; i < 1000; ++i) {
    DefaultQueue()->Schedule([&counter] { RunThenJoin(DoNothing, &counter); });
  }

  counter.Wait();
}

TEST_F(FiberTest, BasicCancellation) {
  Fiber f(WaitForCancel);
  f.Cancel();
  f.Join();
}

TEST_F(FiberTest, BasicTreeCancellation) {
  struct Helper {
    static void CancellableTree(int depth) {
      if (depth > 0) {
        Fiber l(std::bind(CancellableTree, depth - 1));
        Fiber r(std::bind(CancellableTree, depth - 1));
        l.Join();
        r.Join();
      }

      Select({OnCancel()});
    }
  };

  Fiber f(std::bind(Helper::CancellableTree, 3));
  f.Cancel();
  f.Join();
}

TEST_F(FiberTest, AttachToCancelledTree) {
  struct Helper {
    static void CancelledChild() { EXPECT_TRUE(thread::Cancelled()); }

    static void Parent() {
      // Cancel ourselves
      Fiber::Current()->Cancel();

      // Spawn a child
      Fiber child(CancelledChild);
      EXPECT_TRUE(child.Cancelled());
      child.Join();
    }
  };

  Fiber test(Helper::Parent);
  test.Join();
}

TEST_F(FiberTest, CancelOnlySubtree) {
  struct Helper {
    static void CancelledChild() { EXPECT_TRUE(thread::Cancelled()); }

    static void Child() { Select({OnCancel()}); }

    static void Parent() {
      Fiber child(Child);
      child.Cancel();
      EXPECT_TRUE(child.Cancelled());
      EXPECT_FALSE(thread::Cancelled());
      child.Join();
    }
  };

  Fiber test(Helper::Parent);
  test.Join();
}

TEST_F(FiberTest, CancelLargeTree) {
  const int kNumChildren = 4;
  const int kMaxDepth = 5;  // kNumChildren ** kMaxDepth => 1024 fibers
  static PermanentEvent ready;
  ABSL_CONST_INIT static absl::Mutex ready_mutex(absl::kConstInit);

  struct Helper {
    static void Body(int depth) {
      if (depth < kMaxDepth) {
        Fiber* children[kNumChildren];
        int i;

        for (i = 0; i < kNumChildren; i++) {
          children[i] = new Fiber(std::bind(Helper::Body, depth + 1));
        }

        // Ensure that SleepFor() correctly reschedules.  While we don't
        // (currently) verify this explicitly, it significantly expands the test
        // time in a very observable fashion when incorrect.
        absl::SleepFor(absl::Milliseconds(50));

        for (i = 0; i < kNumChildren; i++) {
          children[i]->Join();
          delete children[i];
        }
      } else {
        // We have a tree of at depth kMaxDepth, trigger a cancellation from the
        // root.  This will lead to a reasonably interesting mix of cancelling
        // already existing fibers and new fibers being spawned into cancelled
        // sub-trees.
        absl::MutexLock l(ready_mutex);
        if (!ready.HasBeenNotified()) {
          ready.Notify();
        }
      }

      Select({OnCancel()});
    }
  };

  Fiber root(std::bind(Helper::Body, 0));
  Select({ready.OnEvent()});
  root.Cancel();
  root.Join();
}

TEST_F(FiberTest,
       CancelParentFiberChildFiberAlreadyCancelledAndDoneButNotJoined) {
  ASSERT_FALSE(Cancelled());

  PermanentEvent parent_fiber_cancelled;
  PermanentEvent child_fiber_cancelled;
  Fiber parent([&parent_fiber_cancelled, &child_fiber_cancelled]() {
    Fiber child([]() { Select({OnCancel()}); });

    child.Cancel();
    child_fiber_cancelled.Notify();

    // Make sure to Join() 'child' *after* the parent fiber's Cancel() call is
    // complete.
    Select({parent_fiber_cancelled.OnEvent()});
    child.Join();
  });

  // Make sure to Cancel() 'parent' fiber *after* the child fiber's Cancel()
  // call is complete.
  Select({child_fiber_cancelled.OnEvent()});
  parent.Cancel();
  parent_fiber_cancelled.Notify();
  parent.Join();

  // Assert that the parent fiber is indeed cancelled:
  EXPECT_TRUE(parent.Cancelled());
}

TEST_F(FiberTest, CancelParentFiberChildFiberAlreadyCancelledButRunning) {
  ASSERT_FALSE(Cancelled());

  PermanentEvent parent_fiber_cancelled;
  PermanentEvent child_fiber_cancelled;
  Fiber parent([&parent_fiber_cancelled, &child_fiber_cancelled]() {
    Fiber child([&parent_fiber_cancelled]() {
      Select({OnCancel()});

      // Make sure to keep 'child' running until the parent fiber's Cancel()
      // call is complete.
      Select({parent_fiber_cancelled.OnEvent()});
    });

    child.Cancel();
    child_fiber_cancelled.Notify();
    child.Join();
  });

  // Make sure to Cancel() 'parent' fiber *after* the child fiber's Cancel()
  // call is complete.
  Select({child_fiber_cancelled.OnEvent()});
  parent.Cancel();
  parent_fiber_cancelled.Notify();
  parent.Join();

  // Assert that the parent fiber is indeed cancelled:
  EXPECT_TRUE(parent.Cancelled());
}

TEST_F(FiberTest, ConcurrentCancelWithOverlappingSubtreesSimple) {
  ASSERT_FALSE(Cancelled());

  PermanentEvent nested_fiber_created;
  PermanentEvent nested_fiber_cancelled;
  Fiber* nested = nullptr;
  Fiber root([&nested, &nested_fiber_created, &nested_fiber_cancelled]() {
    Fiber f([&nested_fiber_created]() {
      nested_fiber_created.Notify();
      Select({OnCancel()});
    });
    nested = &f;
    f.Join();
    // We are planning to call f.Cancel() (using 'nested->Cancel()') below. So,
    // we should wait for that call to be done before deallocating 'f'. The
    // order between f.Join() and f.Cancel() should not matter and thus f.Join()
    // is called before this.
    Select({nested_fiber_cancelled.OnEvent()});
  });

  Select({nested_fiber_created.OnEvent()});
  CHECK(nested != nullptr);

  Bundle bundle;
  bundle.Add([&root]() { root.Cancel(); });
  bundle.Add([nested, &nested_fiber_cancelled]() {
    nested->Cancel();
    nested_fiber_cancelled.Notify();
  });
  bundle.JoinAll();

  root.Join();
}

TEST_F(FiberTest, WaitTimeout) {
  absl::Mutex mutex;
  bool condition = false;
  absl::CondVar cond_var;
  const absl::Duration duration = absl::Milliseconds(10);

  Fiber fiber1([&] {
    absl::MutexLock lock(mutex);
    if (!cond_var.WaitWithTimeout(&mutex, duration)) {
      EXPECT_TRUE(condition);
    }
  });

  Fiber fiber2([&] {
    absl::SleepFor(duration);
    absl::MutexLock lock(mutex);
    condition = true;
    cond_var.Signal();
  });

  fiber1.Join();
  fiber2.Join();
}

TEST_F(FiberTest, WaitTimeoutStressTest) {
  absl::Mutex mutex;
  bool condition = false;
  absl::CondVar cond_var;
  const absl::Duration duration = absl::Microseconds(200);
  const absl::Time timeout =
      absl::Now() + absl::GetFlag(FLAGS_wait_timeout_stress_test_duration);

  Fiber fiber1([&] {
    while (absl::Now() < timeout) {
      absl::MutexLock lock(mutex);
      if (!cond_var.WaitWithTimeout(&mutex, duration)) {
        EXPECT_TRUE(condition);
        condition = false;
      }
    }
  });

  Fiber fiber2([&] {
    while (absl::Now() < timeout) {
      absl::SleepFor(duration);
      absl::MutexLock lock(mutex);
      condition = true;
      cond_var.Signal();
    }
  });

  fiber1.Join();
  fiber2.Join();
}

TEST_F(FiberTest, ConcurrentCancelWithOverlappingSubtrees) {
  ASSERT_FALSE(Cancelled());

  std::vector<PermanentEvent> ready(5);
  Fiber* root_f1 = nullptr;
  Fiber* root_f1_f2 = nullptr;
  PermanentEvent root_f1_cancelled;
  PermanentEvent root_f1_f2_cancelled;
  Fiber root([&ready, &root_f1, &root_f1_f2, &root_f1_cancelled,
              &root_f1_f2_cancelled]() {
    Fiber f1([&ready, &root_f1_f2, &root_f1_f2_cancelled]() {
      Fiber f2([&ready]() {
        Fiber([&ready]() {
          Fiber([&ready]() {
            ready[0].Notify();
            Select({OnCancel()});
          }).Join();
        }).Join();
      });
      root_f1_f2 = &f2;

      Fiber f3([&ready]() {
        ready[1].Notify();
        Select({OnCancel()});
      });

      f2.Join();
      f3.Join();
      // 'f2' should live beyond 'root_f1_f2->Cancel()' call.
      Select({root_f1_f2_cancelled.OnEvent()});
    });
    root_f1 = &f1;

    Fiber f4([&ready]() {
      Fiber f5([&ready]() {
        ready[2].Notify();
        Select({OnCancel()});
      });
      Fiber f6([&ready]() {
        ready[3].Notify();
        Select({OnCancel()});
      });
      f5.Join();
      f6.Join();
    });

    f1.Join();
    f4.Join();
    // 'f1' should live beyond 'root_f1->Cancel()' call.
    Select({root_f1_cancelled.OnEvent()});
  });

  Fiber disjoint_root([&ready]() {
    ready[4].Notify();
    Select({OnCancel()});
  });

  for (const PermanentEvent& e : ready) {
    Select({e.OnEvent()});
  }

  CHECK(root_f1 != nullptr);
  CHECK(root_f1_f2 != nullptr);

  Bundle bundle;
  bundle.Add([&root]() { root.Cancel(); });
  bundle.Add([root_f1, &root_f1_cancelled]() {
    root_f1->Cancel();
    root_f1_cancelled.Notify();
  });
  bundle.Add([root_f1_f2, &root_f1_f2_cancelled]() {
    root_f1_f2->Cancel();
    root_f1_f2_cancelled.Notify();
  });
  bundle.Add([&disjoint_root]() { disjoint_root.Cancel(); });
  bundle.JoinAll();

  root.Join();
  disjoint_root.Join();
}

TEST_F(FiberTest, RootFiberWorks) {
  {
    absl::Notification root_ran;
    std::unique_ptr<Fiber> r =
        NewTree(TreeOptions(), std::bind(Notify, &root_ran));
    root_ran.WaitForNotification();
    r->Join();  // Root fibers still require joining.
  }

  // As above, except specify FiberOptions and use a lambda.
  {
    absl::Notification root_ran;
    std::unique_ptr<Fiber> r =
        NewTree(TreeOptions(), [&root_ran] { root_ran.Notify(); });
    root_ran.WaitForNotification();
    r->Join();  // Root fibers still require joining.
  }
}

TEST_F(FiberTest, RootFiberTreeOptionsInvokesWeakHook) {
  TreeOptions options;
  std::unique_ptr<Fiber> r = NewTree(options, [] {
    // 'thread_status' should be set by our strong hook in this test.
    ASSERT_NE(base::CurrentContext().thread_status(), nullptr);
    ASSERT_STREQ(base::CurrentContext().thread_status(), "Adjusted");
  });
  r->Join();
}

TEST_F(FiberTest, RootFiberTreeOptionsByRValueInvokesWeakHook) {
  TreeOptions options;
  std::unique_ptr<Fiber> r = NewTree(options, [] {
    // 'thread_status' should be set by our strong hook in this test.
    ASSERT_NE(base::CurrentContext().thread_status(), nullptr);
    ASSERT_STREQ(base::CurrentContext().thread_status(), "Adjusted");
  });
  r->Join();
}

TEST_F(FiberTest, DetachedRootFiberWorks) {
  absl::Notification root_ran;
  Detach(TreeOptions(), std::bind(Notify, &root_ran));
  root_ran.WaitForNotification();
  // Heap-checker will validate the fiber we created above is freed.
}

static void CheckDeadlineAndWaitForCancel(absl::Time deadline) {
  ASSERT_EQ(deadline, base::CurrentContext().deadline());
  WaitForCancel();  // Deadline will cancel us.
}

static void CheckDeadlineWaitForCancelAndNotify(absl::Time deadline,
                                                absl::Notification* n) {
  CheckDeadlineAndWaitForCancel(deadline);
  n->Notify();
}

// While the above checks that the deadline property is inherited, these tests
// ensure it's enforced.
TEST_F(FiberTest, FibersRespondToDeadlines) {
  absl::Time deadline = absl::Now() + absl::Milliseconds(1);
  base::WithDeadline wd(deadline);

  // Check all methods for creating fibers.
  std::vector<std::unique_ptr<Fiber>> fibers;

  fibers.emplace_back(
      new Fiber(std::bind(CheckDeadlineAndWaitForCancel, deadline)));

  fibers.emplace_back(new Fiber(
      FiberOptions(), std::bind(CheckDeadlineAndWaitForCancel, deadline)));

  for (const auto& f : fibers) {
    f->Join();
    EXPECT_TRUE(f->Cancelled());
  }
}

TEST_F(FiberTest, FibersRespondToInfinitePastDeadlines) {
  // While unexpected in production code, it's possible tests will pass a
  // deadline of absl::InfinitePast().  Ensure it's handled correctly.
  base::WithDeadline wd(absl::InfinitePast());
  Fiber f(WaitForCancel);
  f.Join();
}

TEST_F(FiberTest, RootFibersRespondToDeadlines) {
  absl::Time deadline = absl::Now() + absl::Milliseconds(1);
  base::Context c(base::ContextBuilder(base::BackgroundContext())
                      .set_deadline(deadline)
                      .BuildValue());

  // Check all methods for creating root fibers.
  std::vector<std::unique_ptr<Fiber>> fibers;
  fibers.push_back(NewTree(TreeOptions().set_context(c),
                           std::bind(CheckDeadlineAndWaitForCancel, deadline)));

  fibers.push_back(NewTree(TreeOptions().set_context(c),
                           std::bind(CheckDeadlineAndWaitForCancel, deadline)));

  for (const auto& f : fibers) {
    f->Join();
    EXPECT_TRUE(f->Cancelled());
  }
}

TEST_F(FiberTest, TemporaryCallbackFibersRespondToDeadlines) {
  absl::Time deadline = absl::Now() + absl::Milliseconds(1);
  base::WithDeadline wd(deadline);

  // Check all methods for creating fibers.
  std::vector<std::unique_ptr<Fiber>> fibers;

  fibers.emplace_back(
      new Fiber(std::bind(CheckDeadlineAndWaitForCancel, deadline)));

  fibers.emplace_back(new Fiber(
      FiberOptions(), std::bind(CheckDeadlineAndWaitForCancel, deadline)));

  fibers.emplace_back(
      new Fiber([deadline] { CheckDeadlineAndWaitForCancel(deadline); }));

  fibers.emplace_back(
      NewTree(TreeOptions().set_context(base::CurrentContext()),
              absl::bind_front(CheckDeadlineAndWaitForCancel, deadline)));

  absl::Notification detached_done;
  thread::Detach(thread::TreeOptions().set_context(base::CurrentContext()),
                 absl::bind_front(CheckDeadlineWaitForCancelAndNotify, deadline,
                                  &detached_done));

  detached_done.WaitForNotification();
  for (const auto& f : fibers) {
    f->Join();
    EXPECT_TRUE(f->Cancelled());
  }
}

TEST_F(FiberTest, PermanentCallbackFibersRespondToDeadlines) {
  absl::Time deadline = absl::Now() + absl::Milliseconds(1);

  std::function<void()> callback =
      absl::bind_front(CheckDeadlineAndWaitForCancel, deadline);

  absl::Notification detached_done;
  std::unique_ptr<Closure> detached_callback(
      ::util::functional::ToPermanentCallback(absl::bind_front(
          CheckDeadlineWaitForCancelAndNotify, deadline, &detached_done)));

  base::WithDeadline wd(deadline);

  // Check all methods for creating fibers.
  std::vector<std::unique_ptr<Fiber>> fibers;

  fibers.emplace_back(new Fiber(callback));
  fibers.emplace_back(
      NewTree(TreeOptions().set_context(base::CurrentContext()), callback));

  thread::Detach(thread::TreeOptions().set_context(base::CurrentContext()),
                 util::functional::FromCallback(detached_callback.get()));

  detached_done.WaitForNotification();
  for (const auto& f : fibers) {
    f->Join();
    EXPECT_TRUE(f->Cancelled());
  }
}

// Ensure that the active Context's deadline is used as opposed; it should
// overwrite any deadline captured into NewCallback's copy of Context.
TEST_F(FiberTest, TemporaryCallbackInheritsEnvironmentalDeadline) {
  absl::Time deadline = absl::Now() + absl::Milliseconds(1);
  absl::Time earlier_deadline = deadline - absl::Microseconds(500);
  absl::Time later_deadline = deadline + absl::Microseconds(500);

  std::function<void()> earlier[2];
  std::function<void()> later[2];

  // We create callbacks that asserting the deadline that will later become
  // active when we create our test fibers.
  {
    base::WithDeadline wd(earlier_deadline);
    earlier[0] = absl::bind_front(CheckDeadlineAndWaitForCancel, deadline);
    earlier[1] = absl::bind_front(CheckDeadlineAndWaitForCancel, deadline);
  }

  {
    base::WithDeadline wd(later_deadline);
    later[0] = absl::bind_front(CheckDeadlineAndWaitForCancel, deadline);
    later[1] = absl::bind_front(CheckDeadlineAndWaitForCancel, deadline);
  }

  // Both root and child fibers should ignore the deadlines set in the Contexts
  // captured above.
  base::WithDeadline wd(deadline);
  std::unique_ptr<Fiber> r_early =
      NewTree(TreeOptions().set_context(base::CurrentContext()), earlier[0]);
  std::unique_ptr<Fiber> r_late =
      NewTree(TreeOptions().set_context(base::CurrentContext()), later[0]);

  Fiber c_early(earlier[1]);
  Fiber c_late(later[1]);

  r_early->Join();
  r_late->Join();
  c_early.Join();
  c_late.Join();
}

// Verify that a child fiber with a shorter deadline than its parent's can get
// cancelled without its parent getting cancelled.
TEST_F(FiberTest, ChildFiberDeadlineSoonerThanParent) {
  base::WithDeadline wd_parent(absl::Now() + absl::Hours(1));

  absl::Notification child_cancelled;
  Fiber parent([&]() {
    base::WithDeadline wd_child(absl::Now() + absl::Milliseconds(100));
    Fiber child([&]() {
      WaitForCancel();
      child_cancelled.Notify();
    });
    child.Join();
  });

  child_cancelled.WaitForNotification();
  EXPECT_FALSE(parent.Cancelled());  // parent should not be cancelled yet
  parent.Join();
}

// Verify that a child fiber with a longer deadline than its parent's will
// anyway get cancelled when its parent does.
TEST_F(FiberTest, ChildFiberDeadlineLaterThanParent) {
  base::WithDeadline wd_parent(absl::Now() + absl::Milliseconds(100));

  absl::Notification child_cancelled;
  Fiber parent([&]() {
    base::WithDeadline wd_child(absl::Now() + absl::Hours(10));
    Fiber child([&]() {
      WaitForCancel();
      child_cancelled.Notify();
    });
    child.Join();
  });

  child_cancelled.WaitForNotification();
  parent.Join();
  EXPECT_TRUE(parent.Cancelled());
}

// Verify that a child fiber with an equal deadline to its parent's will get
// cancelled when its parent does. (This is similar to the above, but for the
// equality edge case.)
TEST_F(FiberTest, ChildFiberDeadlineEqualToParent) {
  absl::Time parent_deadline = absl::Now() + absl::Milliseconds(100);
  base::WithDeadline wd_parent(parent_deadline);

  absl::Notification child_cancelled;
  Fiber parent([&]() {
    base::WithDeadline wd_child(parent_deadline);
    Fiber child([&]() {
      WaitForCancel();
      child_cancelled.Notify();
    });
    child.Join();
  });

  child_cancelled.WaitForNotification();
  parent.Join();
  EXPECT_TRUE(parent.Cancelled());
}

TEST_F(FiberTest, DynamicFiberInClosureThreadInheritsDeadline) {
  absl::Notification child_cancelled;

  ClosureThread t([&] {
    base::WithDeadline wd_parent(absl::Now() + absl::Hours(10));
    thread::Fiber::Current();  // Installs a dynamic fiber.

    base::WithDeadline wd_child(absl::Now() + absl::Milliseconds(100));

    // Create a Bundle which implicitly creates a dynamic child fiber.
    thread::Bundle bundle;

    CancellableSleepFor(absl::Milliseconds(500));  // Allow deadline to expire.
    EXPECT_TRUE(bundle.Cancelled());

    bundle.JoinAll();
  });
  t.SetJoinable(true);
  t.Start();
  t.Join();
}

// Tests above should not have affected environmental contexts.
TEST_F(FiberTest, EnvironmentalDeadlinesUnpermuted) {
  EXPECT_EQ(absl::InfiniteFuture(), base::CurrentContext().deadline());
  EXPECT_EQ(absl::InfiniteFuture(), base::BackgroundContext().deadline());
}

// This returns the scheduler we are currently running under (if cooperative)
// using an open-coded introspection.  Do not copy this code, this is NOT a
// supported API.
base::scheduling::Scheduler* GetCurrentScheduler() {
  absl::base_internal::ThreadIdentity* identity;
  identity =
      absl::base_internal::CurrentThreadIdentityIfPresent();  // must exist
  return ::base::scheduling::Schedulable::GetBoundSchedulable(identity)
      ->manager;
}

TEST_F(FiberTest, NewFiberTreeSetParallelism) {
  const int max_parallelism =
      std::min(4, thread::DefaultDomain()->max_concurrency());
  for (int parallelism = 1; parallelism <= max_parallelism; parallelism++) {
    auto f = NewTree(
        std::move(TreeOptions().set_max_cpu_slots(parallelism)), [parallelism] {
          ASSERT_EQ(parallelism, GetCurrentScheduler()->num_slots());
        });
    f->Join();
  }
}

TEST_F(FiberTest, NewFiberTreeSetMaxCpuSlots) {
  const int num_cpu_slots = thread::DefaultDomain()->max_concurrency() + 1;
  auto f = NewTree(TreeOptions().set_max_cpu_slots(num_cpu_slots), [] {
    ASSERT_EQ(thread::DefaultDomain()->max_concurrency(),
              GetCurrentScheduler()->num_slots());
  });
  f->Join();
}

TEST_F(FiberTest, NewFiberTreeSetScheduler) {
  struct Helper {
    Helper() : mutex(), count() {}

    void Increment() {
      absl::MutexLock l(mutex);
      ++count;
    }

    void CheckSchedulerBody(base::scheduling::Scheduler* scheduler) {
      ASSERT_TRUE(GetCurrentScheduler() == scheduler);
      Increment();
    }

    void RootBody(base::scheduling::Scheduler* scheduler) {
      CheckSchedulerBody(scheduler);  // Check scheduler on ourselves/child.
      Fiber c(std::bind(&Helper::CheckSchedulerBody, this, scheduler));
      c.Join();
    }

    absl::Mutex mutex;
    int count;
  };

  Helper helper;
  base::scheduling::Scheduler* scheduler =
      NewChildFIFOScheduler(DefaultDomain()->root_scheduler(), 1);
  std::vector<std::unique_ptr<Fiber>> fibers;
  static constexpr size_t kNumFibers = 5;
  for (size_t i = 0; i < kNumFibers; ++i) {
    fibers.push_back(std::unique_ptr<Fiber>(
        NewTree(TreeOptions().set_scheduler(scheduler),
                std::bind(&Helper::RootBody, &helper, scheduler))));
  }
  scheduler->Orphan();
  for (auto& fiber : fibers) {
    fiber->Join();
  }

  EXPECT_EQ(2 * kNumFibers, helper.count);
}

TEST_F(FiberTest, RootFiberIsIndependent) {
  struct Helper {
    static void EmancipatingParent(std::unique_ptr<Fiber>* root) {
      *root = NewTree(TreeOptions(), [] { Select({OnCancel()}); });
      Select({OnCancel()});
    }
  };
  std::unique_ptr<Fiber> detached_root;
  Fiber fiber(std::bind(&Helper::EmancipatingParent, &detached_root));

  fiber.Cancel();
  fiber.Join();

  // "fiber"'s cancellation should not have affected "detached".  We test this
  // by making "detached" dependent on its own cancellation to complete.
  EXPECT_FALSE(detached_root->Cancelled());
  EXPECT_EQ(-1, TrySelect({detached_root->OnJoinable()}));

  detached_root->Cancel();
  detached_root->Join();
}

TEST_F(FiberTest, ImplicitFiberIgnoresPastContextDeadline) {
  absl::Time deadline = absl::InfinitePast();
  base::WithDeadline wd(deadline);
  // Allow time for cancellation to be delivered, if it erroneously exists.
  EXPECT_EQ(
      -1, SelectUntil(deadline + absl::Milliseconds(5), {thread::OnCancel()}));
  EXPECT_LT(deadline, absl::Now());
  EXPECT_FALSE(thread::Cancelled());
}

TEST_F(FiberTest, ImplicitFiberIgnoresFutureContextDeadline) {
  absl::Time deadline = absl::Now() + absl::Milliseconds(1);
  base::WithDeadline wd(deadline);
  EXPECT_EQ(
      -1, SelectUntil(deadline + absl::Milliseconds(4), {thread::OnCancel()}));
  EXPECT_LT(deadline, absl::Now());
  EXPECT_FALSE(thread::Cancelled());
}

TEST_F(FiberTest, CurrentExists) {
  Fiber* current = Fiber::Current();
  ASSERT_TRUE(current != nullptr);
}

TEST_F(FiberTest, ImplicitFiberParentsChild) {
  absl::Notification child_started;
  absl::Notification parent_set;
  thread::Fiber* parent = nullptr;
  Fiber child([&] {
    child_started.Notify();
    parent_set.WaitForNotification();
    ASSERT_EQ(Fiber::Current()->parent(), parent);
  });  // Our implicit should parent
  // ::Current should return the same context that child's constructor saw.
  child_started.WaitForNotification();
  parent = Fiber::Current();
  parent_set.Notify();
  child.Join();
}

// Spawn a bunch of fibers and do a bit of work in each to ensure
// proper scaling properties.
TEST_F(FiberTest, MaxOut) {
  const int num_fibers = absl::GetFlag(FLAGS_fiber_test_count);
  struct Helper {
    // Read a token from prev and write to next, until closed/cancelled.
    static void Work(absl::Notification* started, Reader<int>* prev,
                     Writer<int>* next, int num) {
      started->Notify();
      int val;
      bool ok;
      while (0 != Select({thread::OnCancel(), prev->OnRead(&val, &ok)})) {
        VLOG(1) << "in " << num;
        if (!ok) {
          break;
        }

        next->WriteUnlessCancelled(val);
      }

      next->Close();
      VLOG(1) << "quitting " << num;
    }
  };

  thread::Bundle children;
  std::vector<std::unique_ptr<Channel<int>>> channels;
  std::unique_ptr<Channel<int>> parent(new Channel<int>(0));
  Reader<int>* prev = parent->reader();
  for (int i = 0; i < num_fibers; ++i) {
    absl::Notification started;
    Channel<int>* chan = new Channel<int>(0);
    children.Add(std::bind(Helper::Work, &started, prev, chan->writer(), i));
    channels.emplace_back(chan);
    started.WaitForNotification();
    prev = chan->reader();
    LOG_EVERY_N(INFO, 1000) << "Done with " << i;
  }

  // we have a loop if we write into <parent> and read from <prev>
  absl::Notification started;
  Fiber loop(std::bind(Helper::Work, &started, prev, parent->writer(), -1));
  started.WaitForNotification();
  // prime the pump
  int val = 0;
  parent->writer()->Write(val);
  if (absl::GetFlag(FLAGS_fiber_test_wait_for_input)) {
    LOG(INFO) << "Press enter to stop loop";
    char buf[20];
    (void)fgets(buf, sizeof(buf), stdin);
  }
  LOG(INFO) << "Stopping everyone.";
  loop.Cancel();
  loop.Join();
  // catch the token to let everyone exit
  prev->Read(&val);
  children.JoinAll();
}

TEST_F(FiberTest, Sleep) {
  // no clock
  Fiber([]() {
    const absl::Duration d = absl::Milliseconds(15);
    absl::Time now = absl::Now();
    EXPECT_TRUE(CancellableSleepFor(d));
    absl::Time after = absl::Now();
    EXPECT_LE(d, after - now);
    Fiber* me = Fiber::Current();
    const absl::Duration forever = absl::Hours(10000);
    thread::DefaultQueue()->ScheduleAt(absl::Now() + d,
                                       [me]() { me->Cancel(); });
    EXPECT_FALSE(CancellableSleepFor(forever));
  }).Join();
  // clock
  Fiber([]() {
    auto clock = &absl::Clock::GetRealClock();
    const absl::Duration d = absl::Milliseconds(15);
    absl::Time now = absl::Now();
    EXPECT_TRUE(thread::CancellableSleepFor(clock, d));
    absl::Time after = absl::Now();
    EXPECT_LE(d, after - now);
    Fiber* me = Fiber::Current();
    const absl::Duration forever = absl::Hours(10000);
    thread::DefaultQueue()->ScheduleAt(absl::Now() + d,
                                       [me]() { me->Cancel(); });
    EXPECT_FALSE(thread::CancellableSleepFor(clock, forever));
  }).Join();
}

struct ObservesDeletion {
  // Notifies "n" when *this is freed.
  explicit ObservesDeletion(absl::Notification* n) : n_(n) {}
  ~ObservesDeletion() { n_->Notify(); }

  absl::Notification* n_;
};

static void ObservesDeletionHelper(std::shared_ptr<ObservesDeletion> arg) {}

TEST_F(FiberTest, NewCallbackFiberFreesArgumentsBeforeJoin) {
  absl::Notification n;
  Fiber c(absl::bind_front(ObservesDeletionHelper,
                           std::make_shared<ObservesDeletion>(&n)));
  // "n" should be notified prior to "c" being joined.
  n.WaitForNotification();
  c.Join();
}

TEST_F(FiberTest, FunctionFiberFreesArgumentsBeforeJoin) {
  absl::Notification n;
  Fiber c(std::bind(ObservesDeletionHelper,
                    std::make_shared<ObservesDeletion>(&n)));
  // "n" should be notified prior to "c" being joined.
  n.WaitForNotification();
  c.Join();
}

#if ABSL_HAVE_THREAD_LOCAL
// See b/35097229 and comments within Scheduler() [base/scheduling/scheduler.cc]
struct TypeWithDtor {
  ~TypeWithDtor() = default;
};

void BlockScopedThreadLocalWithDtor() {
  thread_local TypeWithDtor obj;
  benchmark::DoNotOptimize(obj);
}

TEST_F(FiberTest, ThreadLocalDoesNotDeadlock) {
  auto root = NewTree(std::move(TreeOptions().set_max_cpu_slots(1)), [] {
    thread::Bundle b;
    for (int i = 0; i < 2; ++i) {
      b.Add([&] { BlockScopedThreadLocalWithDtor(); });
    }
    b.JoinAll();
  });
  root->Join();
}
#endif  // ABSL_HAVE_THREAD_LOCAL

// Class that is in DistinctFiberScope white-list.
class DistinctFiberScopeTest : public DistinctFiberScope {
 public:
  DistinctFiberScopeTest() : DistinctFiberScope(FiberOptions()) {}
  explicit DistinctFiberScopeTest(const FiberOptions& options)
      : DistinctFiberScope(options) {}
};

TEST_F(FiberTest, DistinctFiberRun) {
  Fiber* outer = Fiber::Current();
  {
    DistinctFiberScopeTest scope(FiberOptions().SetInternedName("fname"));
    // Inner fiber must be distinct from outer fiber.
    CHECK_NE(Fiber::Current(), outer);
    CHECK_EQ(Fiber::Current()->options().name(), "fname");
  }
  // Outer fiber must be restored.
  CHECK_EQ(Fiber::Current(), outer);
}

TEST_F(FiberTest, DistinctFiberCancel) {
  Fiber* inner = nullptr;
  absl::Notification inner_initialized;

  // Run regular outer fiber.
  Fiber regular([&] {
    // Run a fiber inline and wait for it to be cancelled.
    {
      DistinctFiberScopeTest scope;
      inner = Fiber::Current();
      inner_initialized.Notify();
      CHECK_EQ(thread::Select({OnCancel()}), 0);
      CHECK(Cancelled());
    }

    // Inner cancellations must not affect outer fiber.
    CHECK(!Cancelled());
  });

  CHECK(inner_initialized.WaitForNotificationWithTimeout(absl::Seconds(5)));
  inner->Cancel();

  regular.Join();
}

TEST_F(FiberTest, DistinctFiberScopeInheritsScheduler) {
  // Bounce to a child fiber to get on the default domain.
  Fiber{[] {
    auto custom_domain = thread::CreateCustomDomain({.name = "custom"});
    base::scheduling::Scheduler* custom_scheduler =
        thread::NewRootFIFOScheduler(custom_domain.get());

    ASSERT_NE(custom_domain.get(), thread::DefaultDomain());

    Fiber{Fiber::RootFiber{}, TreeOptions().set_scheduler(custom_scheduler),
          [&] {
            ASSERT_EQ(base::scheduling::Domain::CurrentDomain(),
                      custom_domain.get());

            DistinctFiberScopeTest scope;

            Fiber child([&] {
              EXPECT_EQ(base::scheduling::Domain::CurrentDomain(),
                        custom_domain.get());
            });
            child.Join();
          }}
        .Join();

    custom_scheduler->Orphan();
    ASSERT_EQ(base::scheduling::Domain::CurrentDomain(),
              thread::DefaultDomain());
  }}.Join();
}

TEST_F(FiberTest, IsFiber) {
  std::atomic<bool> first_call_ok{false};
  std::atomic<bool> second_call_ok{false};

  ClosureThread t([&first_call_ok, &second_call_ok] {
    first_call_ok = !thread::Fiber::IsFiber();

    thread::Fiber::Current();  // Installs a dynamic fiber.
    second_call_ok = thread::Fiber::IsFiber();
  });

  t.SetJoinable(true);
  t.Start();
  t.Join();

  EXPECT_TRUE(first_call_ok);
  EXPECT_TRUE(second_call_ok);
}

TEST_F(FiberTest, DomainCacheDefaultFiberStackSizeFlag) {
  // Create a fiber, record its stack size. Set the stack size flag. This should
  // not do anything since the fibers come from same domain.
  auto prev_default_stack_size =
      absl::GetFlag(FLAGS_fibers_default_thread_stack_size);
  int recorded_stack_size;
  auto f = thread::NewTree(TreeOptions(), [&recorded_stack_size] {
    size_t stack_size;

#if !defined(__APPLE__)
    pthread_attr_t p_attr;
    pthread_getattr_np(pthread_self(), &p_attr);
    pthread_attr_getstacksize(&p_attr, &stack_size);
    pthread_attr_destroy(&p_attr);
#else
    stack_size = pthread_get_stacksize_np(pthread_self());
#endif
    recorded_stack_size = stack_size;
  });
  f->Join();

  absl::SetFlag(&FLAGS_fibers_default_thread_stack_size,
                prev_default_stack_size * 4);
  auto j = thread::NewTree(TreeOptions(), [recorded_stack_size] {
    size_t stack_size;

#if !defined(__APPLE__)
    pthread_attr_t p_attr;
    pthread_getattr_np(pthread_self(), &p_attr);
    pthread_attr_getstacksize(&p_attr, &stack_size);
    pthread_attr_destroy(&p_attr);
#else
    stack_size = pthread_get_stacksize_np(pthread_self());
#endif
    // Despite the flag changing, we are still in the same domain, so these
    // should be equal.
    ASSERT_EQ(recorded_stack_size, stack_size);
  });
  j->Join();
}

TEST_F(FiberTest, TestStackClassesInRange) {
  const int kMinStackSizeClass = 0;
  const int kMaxStackSizeClass =
      internal::kMaxStackSizeLog2 - internal::kMinStackSizeLog2;

  // It takes to long to test all of the ~ 2^30 * 2^32 possible values so just
  // test the interesting combos.
  const size_t requested_stack_size_partitions[] = {
      0,  // User doesn't request a stack size
      (1 << internal::kMinStackSizeLog2) -
          100,  // User requests a stack size lower than the lower bound
      1 << internal::kMinStackSizeLog2,  // User requests a stack
                                         // size equal to lower
                                         // bound
      1 << internal::kMaxStackSizeLog2,  // User requests a stack
                                         // size equal to upper
                                         // bound
      (1 << internal::kMaxStackSizeLog2) +
          100};  // User requests a stack size above upper bound. This should
                 // check fail in FiberOptions.

  const int flag_requested_stack_size_partitions[] = {
      0,  // Flag wants default behavior
      (1 << internal::kMinStackSizeLog2) -
          100,  // Flag requests a stack size lower than the lower bound
      1 << internal::kMinStackSizeLog2,  // Flag requests a stack
                                         // size equal to lower
                                         // bound
      1 << internal::kMaxStackSizeLog2,  // Flag requests a stack
                                         // size equal to upper
                                         // bound
      (1 << internal::kMaxStackSizeLog2) +
          100};  // Flag requests a stack size above upper bound.

  for (size_t requested_stack_size : requested_stack_size_partitions) {
    for (int flag_requested_stack_size : flag_requested_stack_size_partitions) {
      int stack_size_class =
          internal::InternalRequestedStackSizeToStackSizeClass(
              requested_stack_size, flag_requested_stack_size);
      EXPECT_GE(stack_size_class, kMinStackSizeClass);
      EXPECT_LE(stack_size_class, kMaxStackSizeClass);
    }
  }
}

TEST_F(FiberTest, TestStackSizes) {
  const size_t kMinSz = 1ULL << internal::kMinStackSizeLog2;
  const size_t kMaxSz = 1ULL << internal::kMaxStackSizeLog2;

  // Test random sizes.
  std::random_device rd;
  std::mt19937 generator(rd());
  std::uniform_int_distribution<size_t> distribution(kMinSz, kMaxSz);

  for (int idx = 0; idx < 100; ++idx) {
    const size_t requested_sz = distribution(generator);

    const int stack_class = internal::StackSizeToStackSizeClass(requested_sz);
    const size_t received_sz = internal::StackSizeClassToStackSize(stack_class);

    EXPECT_LE(requested_sz, received_sz);
    EXPECT_LT(received_sz, requested_sz * 2);
  }

  // Test specific edge cases.
  for (int idx = internal::kMinStackSizeLog2;
       idx <= internal::kMaxStackSizeLog2; ++idx) {
    size_t requested_sz;
    size_t received_sz;
    int stack_class;

    // 2^N
    requested_sz = 1ULL << idx;
    stack_class = internal::StackSizeToStackSizeClass(requested_sz);
    received_sz = internal::StackSizeClassToStackSize(stack_class);
    EXPECT_EQ(requested_sz, received_sz);

    // 2^N + 1
    if (idx < internal::kMaxStackSizeLog2) {
      requested_sz = (1ULL << idx) + 1;
      stack_class = internal::StackSizeToStackSizeClass(requested_sz);
      received_sz = internal::StackSizeClassToStackSize(stack_class);
      EXPECT_EQ(1ULL << (idx + 1), received_sz);
    }

    // 2^N - 1
    if (idx > internal::kMinStackSizeLog2) {
      requested_sz = (1ULL << idx) - 1;
      stack_class = internal::StackSizeToStackSizeClass(requested_sz);
      received_sz = internal::StackSizeClassToStackSize(stack_class);
      EXPECT_EQ(1ULL << idx, received_sz);
    }
  }
}

TEST_F(FiberTest, TestMultipleStackSizes) {
  // Test that we can use every stack size class between the min and max. The
  // test environment can get a bit cranky if we allocate the big threads last,
  // so we loop through the sizes backwards.
  for (int j = internal::kMaxStackSizeLog2 - internal::kMinStackSizeLog2;
       j >= 0; j--) {
    int size = internal::StackSizeClassToStackSize(j);
    auto f = thread::NewTree(
        TreeOptions().set_fiber_options(FiberOptions().SetStackSize(size)),
        [size] {
          size_t stack_size;

#if !defined(__APPLE__)
          // This includes the stack guard so we account for
          // that.
          size_t guard_size;
          pthread_attr_t p_attr;
          pthread_getattr_np(pthread_self(), &p_attr);
          pthread_attr_getstacksize(&p_attr, &stack_size);
          pthread_attr_getguardsize(&p_attr, &guard_size);
          pthread_attr_destroy(&p_attr);
          if (StackShouldIncludeGuardSize()) stack_size -= guard_size;
#else
          stack_size = pthread_get_stacksize_np(pthread_self());
#endif
          // There is a lot of stuff
          // that goes on in Thread::Start() that modifies the
          // value we request for the stack size. So check that we got at
          // least what we requested.
          EXPECT_GE(stack_size, size);
        });
    f->Join();
  }
}

#ifndef NDEBUG
// Code run in a newly-created fiber should have the kFibers cancellation color,
// no matter how the fiber is created.
TEST_F(FiberTest, CancellationColoring) {
  // Child fiber
  thread::Fiber([] {
    EXPECT_EQ(base::internal::CancellationColor::kFibers,
              base::internal::GetActiveCancellationColor());
  }).Join();

  // Bundle
  {
    thread::Bundle b;
    b.Add([] {
      EXPECT_EQ(base::internal::CancellationColor::kFibers,
                base::internal::GetActiveCancellationColor());
    });
    b.JoinAll();
  }

  // New tree
  thread::NewTree(thread::TreeOptions(), [] {
    EXPECT_EQ(base::internal::CancellationColor::kFibers,
              base::internal::GetActiveCancellationColor());
  })->Join();

  // Detached tree
  {
    absl::Notification done;
    thread::Detach(thread::TreeOptions(), [&] {
      EXPECT_EQ(base::internal::CancellationColor::kFibers,
                base::internal::GetActiveCancellationColor());

      done.Notify();
    });

    done.WaitForNotification();
  }
}
#endif

TEST(Concurrency, EmptyFiber) {
  thread::NewTree(std::move(thread::TreeOptions().set_max_cpu_slots(4)), []() {
    constexpr int kNumTrials = 1000;
    for (int i = 0; i < kNumTrials; ++i) {
      thread::Fiber fiber([] {});
      fiber.Join();
    }
  })->Join();
}

TEST(FiberScopedTest, WithInlineExecutor) {
  // Case 1: cancellation leaks out.
  {
    DistinctFiberScopeTest scope;
    EXPECT_FALSE(Fiber::Current()->Cancelled());
    SingletonInlineExecutor()->Schedule([]() { Fiber::Current()->Cancel(); });
    EXPECT_TRUE(Fiber::Current()->Cancelled());
  }

  // Case 2: cancellation is contained.
  {
    DistinctFiberScopeTest scope(FiberOptions().SetInternedName("fname"));
    EXPECT_FALSE(Fiber::Current()->Cancelled());
    EXPECT_EQ(Fiber::Current()->options().name(), "fname");
    SingletonInlineExecutor()->Schedule(
        FiberScoped([]() { Fiber::Current()->Cancel(); }));
    EXPECT_FALSE(Fiber::Current()->Cancelled());
  }
}

namespace {
void SpawnAsyncFiber(std::function<void()> fn,
                     const char* fiber_name = nullptr) {
  if (fiber_name == nullptr) {
    StartDetachedThread("", ChildFiberScoped(std::move(fn)));
  } else {
    StartDetachedThread(
        "", ChildFiberScoped(FiberOptions().SetInternedName(fiber_name),
                             std::move(fn)));
  }
}
}  // namespace

TEST(ChildFiberScopedTest, Basic) {
  std::atomic<bool> fiber_ok{false};

  // With a parent fiber, name inherited.
  fiber_ok.store(false);
  Fiber(FiberOptions().SetInternedName("scope"), [&] {
    SpawnAsyncFiber([&fiber_ok]() {
      fiber_ok.store(Fiber::Current()->options().name() == "scope");
    });
  }).Join();
  // Note that now the parent fiber synchronizes with the async fiber,
  // so there is no need to spin/wait.
  EXPECT_TRUE(fiber_ok.load());

  // With a parent fiber, name overwritten.
  fiber_ok.store(false);
  Fiber(FiberOptions().SetInternedName("scope"), [&] {
    SpawnAsyncFiber(
        [&fiber_ok]() {
          fiber_ok.store(Fiber::Current()->options().name() == "test_fiber");
        },
        "test_fiber");
  }).Join();
  // Note that now the parent fiber synchronizes with the async fiber,
  // so there is no need to spin/wait.
  EXPECT_TRUE(fiber_ok.load());
}

TEST(ChildFiberScopedTest, MultipleNested) {
  std::atomic<int> counter{0};

  TreeOptions opts;
  opts.set_fiber_options(FiberOptions().SetInternedName("testing_tree"));
  std::unique_ptr<Fiber> root = NewTree(opts, [&counter]() {
    SpawnAsyncFiber([&counter]() {
      SpawnAsyncFiber([&counter]() {
        EXPECT_EQ(Fiber::Current()->options().name(), "testing_tree");
        ++counter;
      });
      SpawnAsyncFiber([&counter]() {
        EXPECT_EQ(Fiber::Current()->options().name(), "testing_tree");
        ++counter;
      });
      SpawnAsyncFiber([&counter]() {
        EXPECT_EQ(Fiber::Current()->options().name(), "testing_tree");
        ++counter;
      });
    });
    SpawnAsyncFiber([&counter]() {
      EXPECT_EQ(Fiber::Current()->options().name(), "testing_tree");
      ++counter;
    });
  });
  root->Join();
  EXPECT_EQ(counter.load(), 4);
  absl::SleepFor(absl::Milliseconds(200));
}

TEST(PthreadExit, Works) {
#if defined(ABSL_HAVE_THREAD_SANITIZER) || defined(ABSL_HAVE_MEMORY_SANITIZER)
  // Sanitizers complain about some stack/memory dumping code in thread.cc.
  // Also heap checker is not compatible with some non-prod systems.
  return;
#endif
  std::vector<std::unique_ptr<ClosureThread>> threads;

  threads.emplace_back(
      std::make_unique<ClosureThread>([]() { pthread_exit(nullptr); }));

  threads.emplace_back(std::make_unique<ClosureThread>([]() {
#if defined(__Fuchsia__)
    // Fuchsia does not support pthread_cleanup_push or thread exit handlers,
    // and thus will have an ASAN error due to not clearing a stack fiber
    // pointer.
    return;
#endif
    absl::LeakCheckDisabler d;
    DistinctFiberScopeTest scope;
    pthread_exit(nullptr);
  }));

  threads.emplace_back(std::make_unique<ClosureThread>([]() {
    absl::LeakCheckDisabler d;
    CHECK(Fiber::Current() != nullptr);
    pthread_exit(nullptr);
  }));

  threads.emplace_back(std::make_unique<ClosureThread>([]() {
#if defined(__Fuchsia__)
    // Fuchsia does not support pthread_cleanup_push or thread exit handlers,
    // and thus will have an ASAN error due to not clearing a stack fiber
    // pointer.
    return;
#endif
    absl::LeakCheckDisabler d;
    CHECK(Fiber::Current() != nullptr);
    Fiber fiber([]() { pthread_exit(nullptr); });
    absl::SleepFor(absl::Milliseconds(100));
    pthread_exit(nullptr);
  }));

  for (auto& thread : threads) {
    thread->SetJoinable(true);
    thread->Start();
  }

  for (auto& thread : threads) {
    thread->Join();
  }
}

TEST(CancelAfterDeadlineTest, WorksForParent) {
  auto fiber =
      NewTree(TreeOptions().set_context(
                  base::ContextBuilder(base::ThreadContext())
                      .set_deadline(absl::Now() + absl::Milliseconds(1))
                      .BuildValue()),
              [=] {
                absl::SleepFor(absl::Milliseconds(100));
                EXPECT_TRUE(thread::Cancelled());
              });
  fiber->Join();
}

TEST(CancelAfterDeadlineTest, WorksForChild) {
  auto test_child_with_deadline = [](absl::Duration child_deadline) {
    absl::Notification fiber_done;
    const auto root_deadline = absl::Seconds(10);
    const auto now = absl::Now();
    Detach(TreeOptions().set_context(base::ContextBuilder(base::ThreadContext())
                                         .set_deadline(now + root_deadline)
                                         .BuildValue()),
           [&]() {
             base::WithDeadline wd(now + child_deadline);
             Fiber child([=] {
               absl::SleepFor(absl::Milliseconds(100));
               EXPECT_EQ(thread::Cancelled(),
                         child_deadline < absl::Milliseconds(100));
             });
             child.Join();
             fiber_done.Notify();
           });
    fiber_done.WaitForNotification();
  };

  // Parent deadline is always absl::Seconds(10).
  // Internal cancellation, child is cancelled for both shorter and longer
  // deadline.
  test_child_with_deadline(absl::Milliseconds(1));
  test_child_with_deadline(absl::Seconds(20));
}

TEST(NewTreeTest, DetermineSlotsFromParent) {
  base::scheduling::Scheduler* scheduler = thread::NewChildLIFOScheduler(
      thread::DefaultDomain()->root_scheduler(), 2);
  thread::TreeOptions opts;
  opts.set_scheduler(scheduler);
  auto tree1 = thread::NewTree(opts, [&]() {
    auto tree2 = thread::NewTree(
        thread::TreeOptions().set_max_cpu_slots(3).set_parent_scheduler(
            scheduler),
        []() {});
    tree2->Join();
  });
  scheduler->Orphan();
  tree1->Join();
}

TEST(OneShotAlarm, SchedulesAfterDeadline) {
  absl::Notification done;
  internal::OneShotAlarm alarm(absl::Now() + absl::Milliseconds(10),
                               [&] { done.Notify(); });
  done.WaitForNotification();
}

TEST(OneShotAlarm, CancelBeforeRunning) {
  {
    internal::OneShotAlarm alarm(absl::Now() + absl::Hours(1),
                                 [&] { CHECK(false) << "Should not run"; });
  }
  absl::SleepFor(absl::Milliseconds(10));
}

TEST(OneShotAlarm, CancelInfiniteFuture) {
  {
    internal::OneShotAlarm alarm(absl::InfiniteFuture(),
                                 [&] { CHECK(false) << "Should not run"; });
  }
  absl::SleepFor(absl::Milliseconds(10));
}

// Measures the entire cost to run a nop fiber, including: creation,
// scheduling, execution, and cleanup.
static void BM_FiberFromThread(benchmark::State& state) {
  for (auto _ : state) {
    Fiber f(DoNothing);
    f.Join();
  }
}
BENCHMARK(BM_FiberFromThread);

// As above, however, should be more efficient as fiber-fiber scheduling is
// entirely cooperative.
static void BM_FiberFromFiber(benchmark::State& state) {
  struct Helper {
    static void Benchmark(benchmark::State* state) {
      for (auto _ : *state) {
        Fiber f(DoNothing);
        f.Join();
      }
    }
  };

  Fiber b(std::bind(Helper::Benchmark, &state));
  b.Join();
}
BENCHMARK(BM_FiberFromFiber);

// Using legacy Closure constructor.
static void BM_FiberClosureFromFiber(benchmark::State& state) {
  struct Helper {
    static void Benchmark(benchmark::State* state) {
      for (auto _ : *state) {
        Fiber f([] {});
        f.Join();
      }
    }
  };

  Fiber b(std::bind(Helper::Benchmark, &state));
  b.Join();
}
BENCHMARK(BM_FiberClosureFromFiber);

static void BM_DistinctFiberScope(benchmark::State& state) {
  for (auto _ : state) {
    DistinctFiberScopeTest scope;
    // do nothing
  }
}
BENCHMARK(BM_DistinctFiberScope);

static void BM_ChildFiberScopedOrNot(bool fiber_scoped,
                                     benchmark::State& state) {
  std::atomic<int64_t> counter{0};
  DistinctFiberScopeTest scope;

  auto inner_fn = [&counter]() { ++counter; };

  for (auto _ : state) {
    auto fn = fiber_scoped ? ChildFiberScoped(inner_fn) : FiberScoped(inner_fn);
    ClosureThread thread(std::move(fn));
    thread.SetJoinable(true);
    thread.Start();
    thread.Join();
  }
}
static void BM_ChildFiberScoped(benchmark::State& state) {
  return BM_ChildFiberScopedOrNot(true, state);
}
static void BM_FiberScoped(benchmark::State& state) {
  return BM_ChildFiberScopedOrNot(false, state);
}
BENCHMARK(BM_ChildFiberScoped);
BENCHMARK(BM_FiberScoped);

void BM_ThreadCancelled(benchmark::State& state) {
  for (auto _ : state) {
    CHECK(!thread::Cancelled());
  }
}
BENCHMARK(BM_ThreadCancelled);

void BM_FiberCancel(benchmark::State& state) {
  const int height = state.range(0);
  const int branch = state.range(1);

  class FiberTreeFactory {
   public:
    FiberTreeFactory(const int height, const int branch)
        : height_(height),
          branch_(branch),
          leaf_fibers_([height, branch]() {
            int leaf_fibers = 1;
            for (int i = 0; i < height; ++i) {
              leaf_fibers *= branch;
            }
            return leaf_fibers;
          }()),
          leaf_generated_(0) {}

    // Can be called at most once.
    void Body() { return BodyImpl(height_); }

    Case OnAllLeavesGenerated() const {
      return all_leaves_generated_.OnEvent();
    }

   private:
    void BodyImpl(int levels_remaining) {
      // Base case: leaf fiber.
      if (levels_remaining == 0) {
        {
          absl::MutexLock l(mu_);
          ++leaf_generated_;
          if (leaf_generated_ == leaf_fibers_) {
            all_leaves_generated_.Notify();
          }
        }
        Select({OnCancel()});
        return;
      }

      // Recursive case: nested fibers.
      Bundle bundle;
      for (int i = 0; i < branch_; ++i) {
        bundle.Add(
            [this, levels_remaining]() { BodyImpl(levels_remaining - 1); });
      }
      bundle.JoinAll();
    }

    const int height_;
    const int branch_;
    const int leaf_fibers_;
    absl::Mutex mu_;
    int leaf_generated_ ABSL_GUARDED_BY(mu_);
    PermanentEvent all_leaves_generated_;
  };

  while (state.KeepRunning()) {
    state.PauseTiming();
    FiberTreeFactory fiber_tree_factory(height, branch);
    Fiber fiber([&fiber_tree_factory]() { fiber_tree_factory.Body(); });
    Select({fiber_tree_factory.OnAllLeavesGenerated()});
    state.ResumeTiming();

    fiber.Cancel();

    state.PauseTiming();  // fiber.Join() should not influence BM time.
    fiber.Join();
    state.ResumeTiming();  // Otherwise the first PauseTiming() would fail!
  }
}
BENCHMARK(BM_FiberCancel)
    ->Args({/*height=*/0, /*branch=*/0} /* 1 fiber */)
    ->Args({/*height=*/1, /*branch=*/10} /* 1 + 10 fibers */)
    ->Args({/*height=*/1, /*branch=*/100} /* 1 + 100 fibers */)
    ->Args({/*height=*/1, /*branch=*/1000} /* 1 + 1000 fibers */)
    ->Args({/*height=*/5, /*branch=*/2} /* 1 + 2 + 4 + 8 + 16 + 32 fibers */)
    ->Args({/*height=*/8, /*branch=*/2} /* 1 + 2 + 4 + ... + 256 fibers */)
    ->Args({/*height=*/10, /*branch=*/2} /* 1 + 2 + 4 + ... + 1024 fibers */)
    ->Args({/*height=*/4, /*branch=*/5} /* 1 + 5 + 25 + 125 + 625 fibers */)
    ->Args({/*height=*/3, /*branch=*/10} /* 1 + 10 + 100 + 1000 fibers */)
    ->Args({/*height=*/100, /*branch=*/1} /* 101 fibers */)
    ->Args({/*height=*/1000, /*branch=*/1} /* 1001 fibers */)
    ->Args({/*height=*/10000, /*branch=*/1} /* 10001 fibers */);

void BM_FiberMutexJoin(benchmark::State& state) {
  // Measure how long it takes for a large number of fibers blocked on a mutex
  // to wake up and clean up (=finish/join).
  const int num_fibers = state.range(0);
  for (auto _ : state) {
    state.PauseTiming();

    absl::BlockingCounter fibers_to_start{num_fibers};
    int done_fibers{0};  // Guarded by the mutex below.
    absl::Mutex mutex;
    mutex.lock();

    auto tree = NewTree(std::move(TreeOptions().set_max_cpu_slots(4)), [&] {
      Bundle bundle;
      for (int idx = 0; idx < num_fibers; ++idx) {
        bundle.Add([&]() {
          fibers_to_start.DecrementCount();
          absl::MutexLock lock(mutex);
          ++done_fibers;
        });
      }
      bundle.JoinAll();
    });
    fibers_to_start.Wait();

    state.ResumeTiming();
    mutex.unlock();
    tree->Join();
    CHECK_EQ(done_fibers, num_fibers);
  }
}
BENCHMARK(BM_FiberMutexJoin)->Range(1024, 4096 * 4);

}  // namespace thread
