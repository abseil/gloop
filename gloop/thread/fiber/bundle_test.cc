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

#include "gloop/thread/fiber/bundle.h"

#include <atomic>
#include <functional>
#include <memory>

#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "benchmark/benchmark.h"
#include "gloop/base/context.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/select.h"
#include "gloop/thread/thread.h"
#include "gtest/gtest.h"

namespace thread {
namespace {

static void DoNothing() {}

static void SetInt(int e, int* s) { *s = e; }

static void WaitForCancel() {
  Select({OnCancel()});
  EXPECT_TRUE(thread::Cancelled());
}

// Asserts that "deadline" matches what is set in the current context.
// Waits for cancellation when set.
static void TestDeadline(absl::Time deadline) {
  ASSERT_EQ(deadline, base::CurrentContext().deadline());
  if (deadline != absl::InfiniteFuture()) {
    // If a deadline was specified, ensure it's enacted.
    WaitForCancel();
  }
}

TEST(BundleTest, Add) {
  Bundle b;
  int e1, e2;
  b.Add(DoNothing);
  b.Add([&e1] { e1 = 100; });
  b.Add([&e2] { e2 = 200; });
  b.JoinAll();
  EXPECT_EQ(100, e1);
  EXPECT_EQ(200, e2);
}

TEST(BundleTest, AddAfterJoinAllCrash) {
  Bundle b;
  b.Add(DoNothing);
  b.JoinAll();
  ASSERT_DEATH_IF_SUPPORTED(b.Add(DoNothing), "== RUNNING");
}

TEST(BundleTest, JoinEmpty) {
  Bundle b;
  b.JoinAll();
}

TEST(BundleTest, AddDuringJoinAllCrash) {
  Bundle b;
  absl::Notification n;

  // First, create a child which we will wait for.
  b.Add([&n] { n.WaitForNotification(); });

  Fiber test([&b, &n] {
    // Allow time for our parent to initiate its Join.
    absl::SleepFor(absl::Milliseconds(300));
    ASSERT_DEATH_IF_SUPPORTED(
        while (1) {
          b.Add(DoNothing);
          absl::SleepFor(absl::Milliseconds(100));
        },
        "== RUNNING");
    n.Notify();
  });

  b.JoinAll();
  test.Join();
}

TEST(BundleTest, NestedBundle) {
  Bundle b;
  int e1 = 0, e2 = 0;
  b.Add([&e1, &e2] {
    Bundle c;
    c.Add([&e1] { e1 = 100; });
    c.Add([&e2] { e2 = 200; });
    c.JoinAll();
  });
  b.JoinAll();
  EXPECT_EQ(100, e1);
  EXPECT_EQ(200, e2);
}

TEST(BundleTest, LegacyAdd) {
  Bundle b;
  int e1, e2;
  b.Add(DoNothing);
  b.Add([&e1] { SetInt(100, &e1); });
  b.Add([&e2] { SetInt(200, &e2); });
  b.JoinAll();
  EXPECT_EQ(100, e1);
  EXPECT_EQ(200, e2);
}

TEST(BundleTest, RepeatedJoin) {
  Bundle b;
  int e1, e2;
  b.Add([&e1] { e1 = 100; });
  b.Add([&e2] { e2 = 200; });
  b.JoinAll();
  EXPECT_EQ(100, e1);
  EXPECT_EQ(200, e2);
  b.JoinAll();
}

TEST(BundleTest, BasicCancellation) {
  Bundle b;
  b.Add(WaitForCancel);
  b.Add(WaitForCancel);
  b.Add(WaitForCancel);
  EXPECT_EQ(thread::SelectUntil(absl::Now() + absl::Seconds(1), {b.OnCancel()}),
            -1);
  b.CancelAll();
  EXPECT_TRUE(b.Cancelled());
  EXPECT_EQ(thread::Select({b.OnCancel()}), 0);
  b.JoinAll();
  EXPECT_TRUE(b.Cancelled());
}

TEST(BundleTest, AttachToCancelledBundle) {
  Bundle b;
  b.CancelAll();
  b.Add(WaitForCancel);  // Should be created in a cancelled state.
  b.JoinAll();
}

// Add should copy the current context's deadline.
TEST(BundleTest, AddRespectsContextDeadline) {
  Bundle b;
  const absl::Time deadline = absl::Now() + absl::Milliseconds(20);

  {
    base::WithDeadline wd(deadline);
    b.Add(std::bind(&TestDeadline, deadline));
    b.Add([deadline] { TestDeadline(deadline); });
  }

  b.JoinAll();  // Deadline should cancel above.
  EXPECT_LE(deadline, absl::Now());
}

// Bundles are represented by a place-holder fiber which should inherit and
// respond to the current Context's deadline at time of construction.
TEST(BundleTest, AddRespectsConstructorDeadline) {
  const absl::Time deadline = absl::Now() + absl::Milliseconds(20);
  std::unique_ptr<Bundle> b;

  {
    base::WithDeadline wd(deadline);
    b = std::make_unique<Bundle>();
  }

  b->Add(WaitForCancel);
  b->Add(WaitForCancel);

  b->JoinAll();  // Deadline should cancel above.
  EXPECT_LE(deadline, absl::Now());
}

TEST(BundleTest, ParentCancels) {
  struct Helper {
    static void ParentWork() {
      Bundle b;
      b.Add(WaitForCancel);
      b.Add(WaitForCancel);
      b.JoinAll();
    }
  };

  Fiber f(Helper::ParentWork);
  f.Cancel();
  f.Join();
}

TEST(BundleTest, OnJoinable) {
  // An empty bundle should be immediately joinable.
  Bundle empty;
  EXPECT_EQ(0, Select({empty.OnJoinable()}));
  EXPECT_EQ(0, Select({empty.OnJoinable()}));  // May invoke more than once.
  empty.JoinAll();

  // A bundle with running threads should not be joinable.
  Bundle test_bundle;
  absl::Notification n;
  test_bundle.Add(DoNothing);
  test_bundle.Add([&n] { n.WaitForNotification(); });
  test_bundle.Add([&n] { n.WaitForNotification(); });
  EXPECT_EQ(-1, TrySelect({test_bundle.OnJoinable()}));
  // Also test the negative case for multiple invocation.
  EXPECT_EQ(-1, TrySelect({test_bundle.OnJoinable()}));

  // However, it should become joinable once all running fibers complete.
  n.Notify();
  EXPECT_EQ(0, Select({test_bundle.OnJoinable()}));
  test_bundle.JoinAll();

  // Finally, should still be able to select against OnJoinable.
  EXPECT_EQ(0, Select({test_bundle.OnJoinable()}));
}

TEST(BundleTest, BundleProxy) {
  std::atomic<int> counter{0};
  constexpr int async_children = 10;

  Fiber fiber([&counter]() {
    Bundle bundle;
    BundleProxy proxy(&bundle);

    // Spawn several detached threads that add (async) fibers to the bundle
    // after a delay.
    auto async_work = [&proxy, &counter]() {
      absl::SleepFor(absl::Milliseconds(100));
      proxy.Add([] { absl::SleepFor(absl::Milliseconds(100)); });
      const bool last = async_children == ++counter;
      if (last) {
        proxy.Finished();
      }
    };

    for (int i = 0; i < async_children; ++i) {
      StartDetachedThread("", async_work);
    }
    bundle.JoinAll();
    ++counter;
  });
  fiber.Join();
  EXPECT_EQ(counter.load(), async_children + 1);
}

TEST(BundleTest, BundleProxyCancel) {
  // Similar to above, but with cancellation.
  std::atomic<int> counter{0};
  constexpr int async_children = 10;

  Fiber fiber([&counter]() {
    Bundle bundle;
    BundleProxy proxy(&bundle);

    auto async_work = [&proxy, &counter]() {
      absl::SleepFor(absl::Milliseconds(100));
      proxy.Add([] {
        while (!Fiber::Current()->Cancelled()) {
        }
      });
      const bool last = async_children == ++counter;
      if (last) {
        proxy.Finished();
      }
    };

    for (int i = 0; i < async_children; ++i) {
      StartDetachedThread("", async_work);
    }

    bundle.JoinAll();
    ++counter;
  });
  fiber.Cancel();
  fiber.Join();
  EXPECT_EQ(counter.load(), async_children + 1);
}

static void BM_BundleRunNop(benchmark::State& state) {
  const int num_fibers = state.range(0);
  while (state.KeepRunningBatch(num_fibers)) {
    Bundle bundle;
    for (int i = 0; i < state.range(0); i++) {
      bundle.Add(DoNothing);
    }
    bundle.JoinAll();
  }
}
BENCHMARK(BM_BundleRunNop)->Range(1, 100 << 10);

static void BM_EmptyBundle(benchmark::State& state) {
  for (auto _ : state) {
    Bundle bundle;
    bundle.JoinAll();
  }
}
BENCHMARK(BM_EmptyBundle);

}  // namespace
}  // namespace thread
