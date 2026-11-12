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

#include "gloop/thread/fiber/contrib/fiber_future/fiber_future.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/log/check.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/scheduling/scheduler.h"
#include "gloop/thread/fiber/bundle.h"
#include "gloop/thread/fiber/fiber-options.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/select.h"
#include "gtest/gtest.h"

namespace {

using ::thread::contrib::FiberFuture;
using ::thread::contrib::FiberFutureInNewTree;

TEST(FiberFutureTest, DoesRunChildFiber) {
  FiberFuture<int> future([]() -> int { return 1; });
  EXPECT_EQ(1, future.Get());
  ASSERT_TRUE(future.IsFinished());
}

TEST(FiberFutureTest, DoesRunChildFiberWithOptions) {
  std::string name = "TestName";
  thread::FiberOptions options;
  options.SetInternedName(name);
  FiberFuture<bool> future(options, [&name]() -> bool {
    return name == thread::Fiber::Current()->options().name();
  });
  EXPECT_TRUE(future.Get());
  ASSERT_TRUE(future.IsFinished());
}

struct UnrefScheduler {
  void operator()(base::scheduling::Scheduler* scheduler) {
    scheduler->Unref();
  }
};

TEST(FiberFutureTest, DoesRunNewFiberTree) {
  thread::FiberOptions options;
  options.SetInternedName("AFancyName");

  std::unique_ptr<FiberFuture<int>> future = FiberFuture<int>::NewTree(
      thread::TreeOptions().set_fiber_options(options), ([]() -> int {
        // To make sure that we are indeed running in a new tree, we check we
        // don't have a parent fiber.
        CHECK_EQ(thread::Fiber::Current()->parent(), nullptr);
        // To make sure the tree options were passed, we check our name.
        CHECK_EQ(thread::Fiber::Current()->options().name(), "AFancyName");
        return 1;
      }));
  ASSERT_EQ(1, future->Get());
  ASSERT_TRUE(future->IsFinished());
}

TEST(FiberFutureTest, DoesRunNewFiberTreeWithInferedType) {
  thread::FiberOptions options;
  options.SetInternedName("AFancyName");

  auto future = FiberFutureInNewTree(
      thread::TreeOptions().set_fiber_options(options), ([]() {
        // To make sure that we are indeed running in a new tree, we check we
        // don't have a parent fiber.
        CHECK_EQ(thread::Fiber::Current()->parent(), nullptr);
        // To make sure the tree options were passed, we check our name.
        CHECK_EQ(thread::Fiber::Current()->options().name(), "AFancyName");
        return 1;
      }));
  static_assert(
      std::is_same_v<decltype(future), std::unique_ptr<FiberFuture<int>>>);
  ASSERT_EQ(1, future->Get());
  ASSERT_TRUE(future->IsFinished());
}

TEST(FiberFutureTest, TestOnFinished) {
  FiberFuture<int> future([]() -> int { return 1; });
  ASSERT_EQ(0, thread::SelectUntil(absl::Now() + absl::Seconds(1),
                                   {future.OnFinished()}));
  ASSERT_TRUE(future.IsFinished());
  ASSERT_EQ(1, future.Get());
}

TEST(FiberFutureTest, Get) {
  FiberFuture<std::string> future([]() -> std::string { return "ABCDEFG"; });
  const FiberFuture<std::string>& const_ref = future;
  // Can call Get() on a const reference.
  EXPECT_EQ("ABCDEFG", const_ref.Get());
  // Can also call Get() on a non-const object, and mutate the value.
  EXPECT_EQ("ABCDEFG", future.Get());
  future.Get() = "XYZ";
  EXPECT_EQ("XYZ", const_ref.Get());
}

TEST(FiberFutureTest, GetUntilCompletes) {
  FiberFuture<int> future([]() -> int {
    return thread::SelectUntil(absl::Now() + absl::Milliseconds(100),
                               {thread::OnCancel()});
  });
  ::std::optional<int> result = future.GetUntil(absl::Now() + absl::Seconds(1));
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(*result, -1);
}

TEST(FiberFutureTest, GetUntilTimesOut) {
  FiberFuture<int> future(
      []() -> int { return thread::Select({thread::OnCancel()}); });
  ::std::optional<int> result =
      future.GetUntil(absl::Now() + absl::Milliseconds(100));
  EXPECT_FALSE(result.has_value());
}

TEST(FiberFutureTest, Consume) {
  FiberFuture<std::string> future([]() -> std::string { return "ABCDEFG"; });
  EXPECT_EQ("ABCDEFG", future.Consume());
}

TEST(FiberFutureTest, ConsumeRvalue) {
  FiberFuture<std::unique_ptr<int>> future(
      [] { return std::make_unique<int>(42); });
  auto ptr = std::move(future).Consume();
  ASSERT_NE(ptr, nullptr);
  EXPECT_EQ(*ptr, 42);
}

TEST(FiberFutureTest, GetRvalue) {
  FiberFuture<std::unique_ptr<int>> future(
      [] { return std::make_unique<int>(42); });
  auto ptr = std::move(future).Get();
  ASSERT_NE(ptr, nullptr);
  EXPECT_EQ(*ptr, 42);
}

TEST(FiberFutureTest, MoveLvalueGet) {
  FiberFuture<std::unique_ptr<int>> future(
      [] { return std::make_unique<int>(42); });
  auto ptr = std::move(future.Get());
  ASSERT_NE(ptr, nullptr);
  EXPECT_EQ(*ptr, 42);
}

TEST(FiberFutureTest, ConsumeNonCopyableType) {
  FiberFuture<std::unique_ptr<std::string>> future(
      []() { return std::make_unique<std::string>("ABCDEFG"); });
  std::unique_ptr<std::string> result = future.Consume();
  ASSERT_TRUE(result) << "Got empty result";
  EXPECT_EQ("ABCDEFG", *result);
  EXPECT_FALSE(future.Cancelled());
}

TEST(FiberFutureTest, GetRvalueNonCopyableType) {
  FiberFuture<std::unique_ptr<std::string>> future(
      []() { return std::make_unique<std::string>("ABCDEFG"); });
  std::unique_ptr<std::string> result = std::move(future).Get();
  ASSERT_TRUE(result) << "Got empty result";
  EXPECT_EQ("ABCDEFG", *result);
}

TEST(FiberFutureTest, GetLvalueNonCopyableType) {
  FiberFuture<std::unique_ptr<std::string>> future(
      []() { return std::make_unique<std::string>("ABCDEFG"); });
  std::unique_ptr<std::string> result = std::move(future.Get());
  ASSERT_TRUE(result) << "Got empty result";
  EXPECT_EQ("ABCDEFG", *result);
  EXPECT_FALSE(future.Cancelled());
}

TEST(FiberFutureTest, ConsumeUntilCompletes) {
  FiberFuture<int> future([]() -> int {
    return thread::SelectUntil(absl::Now() + absl::Milliseconds(100),
                               {thread::OnCancel()});
  });
  ::std::optional<int> result =
      future.ConsumeUntil(absl::Now() + absl::Seconds(1));
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(*result, -1);
}

TEST(FiberFutureTest, ConsumeUntilTimesOut) {
  FiberFuture<int> future(
      []() -> int { return thread::Select({thread::OnCancel()}); });
  ::std::optional<int> result =
      future.ConsumeUntil(absl::Now() + absl::Milliseconds(100));
  EXPECT_FALSE(result.has_value());
}

TEST(FiberFutureTest, DoesCancel) {
  FiberFuture<int> future([]() -> int {
    return thread::SelectUntil(absl::Now() + absl::Seconds(1),
                               {thread::OnCancel()});
  });
  future.Cancel();
  ASSERT_EQ(0, future.Get());
  EXPECT_TRUE(future.Cancelled());
}

TEST(FiberFutureTest, CancelAfterCompletion) {
  FiberFuture<int> future([]() { return 10; });
  ASSERT_EQ(10, future.Get());
  future.Cancel();
  EXPECT_TRUE(future.Cancelled());
}

TEST(FiberFutureTest, NonParentFibersCanConsumeValue) {
  FiberFuture<int> future([]() -> int { return 123; });
  thread::Bundle b;
  for (int i = 0; i < 10; i++) {
    b.Add([&future]() { EXPECT_EQ(123, future.Get()); });
  }
  b.JoinAll();
}

TEST(FibureFutureTest, ConsumeRvalue) {
  static constexpr int kFoobar = 42;
  FiberFuture<std::unique_ptr<int>> future(
      [] { return std::make_unique<int>(kFoobar); });
  ASSERT_NE(future.Get(), nullptr);
  EXPECT_EQ(*future.Get(), kFoobar);
  std::unique_ptr<int> consumed_ptr_1 = std::move(future).Consume();
  // This will give a clang-tidy warning.
  // std::unique_ptr<int> consumed_ptr_2 = std::move(future).Consume();
  ASSERT_NE(consumed_ptr_1, nullptr);
  EXPECT_EQ(*consumed_ptr_1, kFoobar);
}

TEST(FibureFutureTest, GetRvalues) {
  static constexpr int kFoobar = 42;
  FiberFuture<std::unique_ptr<int>> future(
      [] { return std::make_unique<int>(kFoobar); });
  ASSERT_NE(future.Get(), nullptr);
  EXPECT_EQ(*future.Get(), kFoobar);
  std::unique_ptr<int> consumed_ptr_1 = std::move(future).Get();
  // This will give a clang-tidy warning.
  // std::unique_ptr<int> consumed_ptr_2 = std::move(future).Get();
  ASSERT_NE(consumed_ptr_1, nullptr);
  EXPECT_EQ(*consumed_ptr_1, kFoobar);
}

TEST(FibureFutureTest, GetLvalues) {
  static constexpr int kFoobar = 42;
  FiberFuture<std::unique_ptr<int>> future(
      [] { return std::make_unique<int>(kFoobar); });
  ASSERT_NE(future.Get(), nullptr);
  EXPECT_EQ(*future.Get(), kFoobar);
  std::unique_ptr<int> consumed_ptr_1 = std::move(future.Get());
  std::unique_ptr<int> consumed_ptr_2 = std::move(future.Get());
  ASSERT_NE(consumed_ptr_1, nullptr);
  EXPECT_EQ(*consumed_ptr_1, kFoobar);
  ASSERT_EQ(consumed_ptr_2, nullptr);
}

TEST(FibureFutureTest, MovableCapture) {
  static constexpr int kFoobar = 42;
  auto foobar = std::make_unique<int>(kFoobar);
  FiberFuture<int> future([foobar = std::move(foobar)] { return *foobar; });
  EXPECT_EQ(future.Get(), kFoobar);
}

TEST(FibureFutureTest, MutableLambda) {
  static constexpr int kFoobar = 42;
  auto foobar = std::make_unique<int>(kFoobar);
  FiberFuture<std::unique_ptr<int>> future(
      [foobar = std::move(foobar)]() mutable { return std::move(foobar); });
  EXPECT_EQ(foobar, nullptr);
  ASSERT_NE(future.Get(), nullptr);
  EXPECT_EQ(*future.Get(), kFoobar);
}

TEST(FiberFutureTest, StdFunctionFiberFuture) {
  static constexpr int kFoobar = 42;
  std::function<int(void)> func = [] { return kFoobar; };
  FiberFuture<int> future(func);
  EXPECT_EQ(future.Get(), kFoobar);
}

struct NotDefaultConstructible {
  explicit NotDefaultConstructible(int value) : value(value) {}
  int value;
};

TEST(FiberFutureTest, NotDefaultConstructible) {
  static constexpr int kFoobar = 42;
  std::function<NotDefaultConstructible(void)> func = [] {
    return NotDefaultConstructible(kFoobar);
  };
  FiberFuture<NotDefaultConstructible> future(func);
  EXPECT_EQ(future.Get().value, kFoobar);
}

TEST(FiberFutureTest, ImmediateFiberFuture) {
  static constexpr int kFoobar = 42;
  FiberFuture<int> future(std::in_place, kFoobar);
  EXPECT_TRUE(future.IsFinished());
  // These operations should be a no-op for immediate futures.
  thread::Select({future.OnFinished()});
  future.Cancel();
  EXPECT_FALSE(future.Cancelled());
  EXPECT_EQ(future.Get(), kFoobar);
}

class CustomFiber : public thread::Fiber {
 public:
  template <class F>
  explicit CustomFiber(F&& function) : Fiber(std::forward<F>(function)) {}
  template <class F>
  CustomFiber(const thread::FiberOptions& options, F&& function)
      : Fiber(options, std::forward<F>(function)) {}
};

TEST(FiberFutureTest, StdFunctionCustomFiberFuture) {
  static constexpr int kFoobar = 42;
  std::function<int(void)> func = [] { return kFoobar; };
  FiberFuture<int, CustomFiber> future(func);
  EXPECT_EQ(future.Get(), kFoobar);
}

TEST(FiberFutureTest, Ctad) {
  // Technically all this could be in a function that does nothing. Putting it
  // in a TEST removes risk of dead code deletion removing them.
  {
    FiberFuture fiber([] { return 1; });
    static_assert(std::is_same_v<decltype(fiber), FiberFuture<int>>);
  }
  {
    FiberFuture fiber(thread::FiberOptions(), [] { return 1; });
    static_assert(std::is_same_v<decltype(fiber), FiberFuture<int>>);
  }
  {
    FiberFuture fiber(std::in_place, 1);
    static_assert(std::is_same_v<decltype(fiber), FiberFuture<int>>);
  }

  struct MoveCallable {
    std::unique_ptr<int> operator()() && { return std::make_unique<int>(1); }
  };

  {
    FiberFuture fiber(MoveCallable{});
    static_assert(
        std::is_same_v<decltype(fiber), FiberFuture<std::unique_ptr<int>>>);
  }
  {
    FiberFuture fiber(thread::FiberOptions(), MoveCallable{});
    static_assert(
        std::is_same_v<decltype(fiber), FiberFuture<std::unique_ptr<int>>>);
  }

  {
    FiberFuture fiber(std::in_place, std::make_unique<int>(1));
    static_assert(
        std::is_same_v<decltype(fiber), FiberFuture<std::unique_ptr<int>>>);
  }
}

}  // namespace
