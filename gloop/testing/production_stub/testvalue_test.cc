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

#include "gloop/testing/production_stub/testvalue.h"

#include <atomic>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/bind_front.h"
#include "absl/hash/hash.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/thread/threadpool.h"
#include "gtest/gtest.h"

namespace testing {
namespace testvalue {
namespace {

class TestValue : public testing::Test {
 public:
  TestValue() { Reset(); }

  static void SetUpTestSuite() { Enable(); }
};

TEST_F(TestValue, NoAdjuster) {
  int x = 100;
  Adjust("foo", &x);
  EXPECT_EQ(100, x);
}

TEST_F(TestValue, ValueAdjuster) {
  int x = 100;
  Force("value_adjuster", 200);
  Adjust("value_adjuster", &x);
  EXPECT_EQ(200, x);

  Force("value_adjuster", 300);
  Adjust("value_adjuster", &x);
  EXPECT_EQ(300, x);
}

TEST_F(TestValue, Labels) {
  int x = 100;
  Force("another_label", 200);
  Adjust("this_label", &x);
  EXPECT_EQ(100, x);
}

static void SetTo(int value, int* var) { *var = value; }

TEST_F(TestValue, Callback) {
  int x = 100;
  SetCallback<int>("callback_adjuster", absl::bind_front(SetTo, 500));
  Adjust("callback_adjuster", &x);
  EXPECT_EQ(500, x);
  SetCallback<int>("callback_adjuster", absl::bind_front(SetTo, 600));
  Adjust("callback_adjuster", &x);
  EXPECT_EQ(600, x);
}

TEST_F(TestValue, CallbackLambda) {
  int x = 100;
  SetCallback<int>("callback_adjuster", [](int* arg) { *arg = 500; });
  Adjust("callback_adjuster", &x);
  EXPECT_EQ(500, x);
  SetCallback<int>("callback_adjuster", [&x](int* arg) { x = 600; });
  Adjust("callback_adjuster", &x);
  EXPECT_EQ(600, x);
}

TEST_F(TestValue, Clear) {
  int x = 100;
  Force("clear_test", 200);
  Adjust("clear_test", &x);
  EXPECT_EQ(200, x);
  Clear("clear_test");
  x = 300;
  Adjust("clear_test", &x);
  EXPECT_EQ(300, x);
}

TEST_F(TestValue, ForceClearClear) {
  // This used to crash.
  Force("clearclear_test", 200);
  Clear("clearclear_test");
  Clear("clearclear_test");
}

TEST_F(TestValue, Reset) {
  int x = 100;
  Force("reset_test", 200);
  Reset();
  Adjust("reset_test", &x);
  EXPECT_EQ(100, x);
}

static void Delay(absl::Notification* started) {
  started->Notify();
  absl::SleepFor(absl::Seconds(2));
}

TEST_F(TestValue, ClearWaitsForActiveCalls) {
  ThreadPool pool(1, ThreadPool::Options{.name_prefix = "Test"});
  SetCallback<absl::Notification>("delay", Delay);

  absl::Notification started;

  pool.Schedule(absl::bind_front(Adjust<absl::Notification>,
                                 absl::string_view("delay"), &started));
  started.WaitForNotification();

  absl::Time start = absl::Now();
  Clear("delay");
  absl::Time end = absl::Now();
  EXPECT_LE(1.5, absl::ToDoubleSeconds(end - start));
}

TEST_F(TestValue, ForceIsAtomic) {
  // Use testvalue::Force to override a value, and then call Force again to
  // override to a second value. At no point between Force(override_1) and
  // Force(override_2) should calling code observe the original value.
  auto pool = std::make_unique<ThreadPool>(
      3, ThreadPool::Options{.name_prefix = "Test"});
  absl::Notification done;
  int original = 0;
  int override_1 = 1;
  int override_2 = 2;

  auto get_value = [&] {
    int* ret = &original;
    Adjust("override", &ret);
    return ret;
  };

  auto async_thread = [&] {
    while (!done.HasBeenNotified()) {
      int* ret = get_value();
      EXPECT_NE(*ret, 0);
    }
  };

  Force("override", &override_1);

  pool->Schedule(async_thread);
  pool->Schedule(async_thread);
  pool->Schedule(async_thread);

  absl::SleepFor(absl::Milliseconds(100));

  // Override our value again - there should be no period where the original
  // value is returned.
  Force("override", &override_2);
  absl::SleepFor(absl::Milliseconds(100));

  done.Notify();
  pool.reset();
  Clear("override");
}

TEST_F(TestValue, ScopedSetCallback) {
  int x = 0;
  Adjust("foo", &x);
  EXPECT_EQ(x, 0);

  {
    ScopedSetCallback<int> test_cb("foo", [](int* x) { *x = 100; });
    Adjust("foo", &x);
    EXPECT_EQ(x, 100);
  }

  // testvalue should be cleared once `test_cb` has gone out of scope.
  x = 200;
  Adjust("foo", &x);
  EXPECT_EQ(x, 200);
}

TEST_F(TestValue, ScopedForce) {
  static constexpr absl::string_view kLabel = "foo";

  int x = 0;
  Adjust(kLabel, &x);
  EXPECT_EQ(x, 0);

  {
    ScopedForce scope(kLabel, 1234);
    Adjust(kLabel, &x);
    EXPECT_EQ(x, 1234);
  }

  // testvalue should be cleared once `cleanup` has gone out of scope.
  x = 9876;
  Adjust(kLabel, &x);
  EXPECT_EQ(x, 9876);
}

TEST_F(TestValue, ManyLabels) {
  // Register enough labels that many of them share slots in the internal label
  // filter, and check that adjusters are still matched exactly by label.
  constexpr int kNumLabels = 5000;
  std::vector<std::string> labels;
  for (int i = 0; i < kNumLabels; ++i) {
    labels.push_back(absl::StrCat("many_labels_", i));
  }
  for (int i = 0; i < kNumLabels; i += 2) {
    Force(labels[i], i);
  }
  for (int i = 0; i < kNumLabels; ++i) {
    int x = -1;
    Adjust(labels[i], &x);
    EXPECT_EQ(x, i % 2 == 0 ? i : -1) << labels[i];
  }

  // Clear every fourth label, and replace the rest of the even labels.
  for (int i = 0; i < kNumLabels; i += 4) {
    Clear(labels[i]);
    Force(labels[i + 2], -i);
  }
  for (int i = 0; i < kNumLabels; ++i) {
    int x = -1;
    Adjust(labels[i], &x);
    EXPECT_EQ(x, i % 4 == 2 ? -(i - 2) : -1) << labels[i];
  }

  Reset();
  for (int i = 0; i < kNumLabels; ++i) {
    int x = -1;
    Adjust(labels[i], &x);
    EXPECT_EQ(x, -1) << labels[i];
  }
}

TEST_F(TestValue, LabelFilterSaturation) {
  // Register > 255 labels that collide in the same 128-entry filter slot to
  // exercise saturating Add() and Remove() at UINT8_MAX.
  constexpr int kNumColliding = 260;
  std::vector<std::string> colliding;
  for (int i = 0; static_cast<int>(colliding.size()) < kNumColliding; ++i) {
    std::string candidate = absl::StrCat("saturate_", i);
    if (absl::HashOf(absl::string_view(candidate)) % 128 == 0) {
      colliding.push_back(std::move(candidate));
    }
  }
  for (int i = 0; i < kNumColliding - 1; ++i) {
    Force(colliding[i], i);
  }
  for (int i = 0; i < kNumColliding - 1; ++i) {
    int x = -1;
    Adjust(colliding[i], &x);
    EXPECT_EQ(x, i);
  }
  int unregistered = -1;
  Adjust(colliding.back(), &unregistered);
  EXPECT_EQ(unregistered, -1);

  for (int i = 0; i < kNumColliding - 1; ++i) {
    Clear(colliding[i]);
  }
  for (int i = 0; i < kNumColliding; ++i) {
    int x = -1;
    Adjust(colliding[i], &x);
    EXPECT_EQ(x, -1);
  }

  // Re-registering and clearing a label in a saturated slot still works.
  Force(colliding[0], 42);
  int x = -1;
  Adjust(colliding[0], &x);
  EXPECT_EQ(x, 42);
  Clear(colliding[0]);
  Adjust(colliding[0], &x);
  EXPECT_EQ(x, 42);
}

TEST_F(TestValue, ConcurrentAdjustAndSetCallback) {
  // Adjust() concurrently with SetCallback() (including overwriting an active
  // callback) and Clear() on the same label. Run under ASAN/TSAN, this checks
  // that entries are not deleted while in use, and that both SetCallback() and
  // Clear() wait for in-flight callbacks of the removed entry.
  constexpr int kNumThreads = 8;
  constexpr int kNumIterations = 500;
  constexpr absl::string_view kLabel = "concurrent";
  std::atomic<bool> done = false;
  {
    ThreadPool pool(kNumThreads, ThreadPool::Options{.name_prefix = "Test"});
    for (int i = 0; i < kNumThreads; ++i) {
      pool.Schedule([&] {
        while (!done.load(std::memory_order_relaxed)) {
          int x = 0;
          Adjust(kLabel, &x);
          int y = 0;
          Adjust("concurrent_unregistered", &y);
          EXPECT_EQ(y, 0);
        }
      });
    }
    for (int i = 0; i < kNumIterations; ++i) {
      std::unique_ptr<std::atomic<int>> calls1 =
          std::make_unique<std::atomic<int>>(0);
      SetCallback<int>(kLabel, [calls = calls1.get()](int*) {
        calls->fetch_add(1, std::memory_order_relaxed);
      });
      while (calls1->load(std::memory_order_relaxed) == 0) {
        absl::SleepFor(absl::Microseconds(10));
      }

      // Overwrite the active callback without an intervening Clear();
      // SetCallback() must wait for in-flight `calls1` callbacks before
      // returning, so destroying `calls1` immediately after is safe.
      std::unique_ptr<std::atomic<int>> calls2 =
          std::make_unique<std::atomic<int>>(0);
      SetCallback<int>(kLabel, [calls = calls2.get()](int*) {
        calls->fetch_add(1, std::memory_order_relaxed);
      });
      calls1.reset();

      while (calls2->load(std::memory_order_relaxed) == 0) {
        absl::SleepFor(absl::Microseconds(10));
      }
      Clear(kLabel);
      // Clear() waited for all callbacks, so destroying `calls2` is safe.
      calls2.reset();
    }
    done.store(true, std::memory_order_relaxed);
  }
}

}  // namespace
}  // namespace testvalue
}  // namespace testing
