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

#include <memory>

#include "absl/functional/bind_front.h"
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

}  // namespace
}  // namespace testvalue
}  // namespace testing
