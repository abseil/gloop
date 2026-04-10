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

#include "gloop/util/functional/auto_function_runner.h"

#include <functional>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "gloop/base/callback.h"
#include "gloop/util/functional/to_callback.h"
#include "gtest/gtest.h"

namespace util::functional {

namespace {

constexpr auto kInitialState = 0;
constexpr auto kChangedState = 1;
static_assert(
    kInitialState != kChangedState,
    "`kInitialState` is used for testing that the callback has not run. "
    "`kChangedState` is used to indicate that the callback has run. This "
    "static assertion makes it clear that the underlying values of these "
    "constants isn't important, so long as they are different.");

TEST(AutoFunctionRunnerTest, DefaultConstructor) {
  AutoFunctionRunner callback;
  EXPECT_FALSE(callback);
}

TEST(AutoFunctionRunnerTest, CallbackConstructor) {
  {
    AutoFunctionRunner callback(std::function<void()>{});
    EXPECT_FALSE(callback);
  }

  {
    AutoFunctionRunner callback(absl::AnyInvocable<void() &&>{});
    EXPECT_FALSE(callback);
  }

  {
    AutoFunctionRunner callback([] {});
    EXPECT_TRUE(callback);
  }
}

TEST(AutoFunctionRunnerTest, MoveConstructor) {
  {
    AutoFunctionRunner lhs_callback;
    AutoFunctionRunner rhs_callback([] {});

    lhs_callback = std::move(rhs_callback);

    EXPECT_TRUE(lhs_callback);
    EXPECT_FALSE(rhs_callback);
  }

  {
    AutoFunctionRunner lhs_callback;
    AutoFunctionRunner rhs_callback(nullptr);

    lhs_callback = std::move(rhs_callback);

    EXPECT_FALSE(lhs_callback);
    EXPECT_FALSE(rhs_callback);
  }
}

TEST(AutoFunctionRunnerTest, BasicUsage) {
  auto state = kInitialState;

  {
    EXPECT_EQ(state, kInitialState);

    AutoFunctionRunner callback([&] { state = kChangedState; });
    EXPECT_TRUE(callback);

    EXPECT_EQ(state, kInitialState);
  }

  EXPECT_EQ(state, kChangedState);
}

TEST(AutoFunctionRunnerTest, Cancel) {
  auto state = kInitialState;

  {
    EXPECT_EQ(state, kInitialState);

    AutoFunctionRunner callback([&] { state = kChangedState; });
    EXPECT_TRUE(callback);

    callback.Cancel();
    EXPECT_FALSE(callback);
    EXPECT_EQ(state, kInitialState);
  }

  EXPECT_EQ(state, kInitialState);
}

TEST(AutoFunctionRunnerTest, Invoke) {
  auto state = kInitialState;

  {
    EXPECT_EQ(state, kInitialState);

    AutoFunctionRunner callback([&] { state = kChangedState; });
    EXPECT_TRUE(callback);

    callback.Invoke();
    EXPECT_FALSE(callback);
    EXPECT_EQ(state, kChangedState);
  }

  EXPECT_EQ(state, kChangedState);
}

TEST(AutoFunctionRunnerTest, ResetFunctionWithFunction) {
  auto state_for_old_cb = kInitialState;
  auto state_for_new_cb = kInitialState;

  {
    AutoFunctionRunner callback([&] { state_for_old_cb = kChangedState; });
    std::function<void()> new_cb = [&] { state_for_new_cb = kChangedState; };
    callback.Reset(std::move(new_cb));
    EXPECT_TRUE(callback);

    EXPECT_EQ(state_for_old_cb, kInitialState);
    EXPECT_EQ(state_for_new_cb, kInitialState);
  }
  EXPECT_EQ(state_for_old_cb, kInitialState);
  EXPECT_EQ(state_for_new_cb, kChangedState);
}

TEST(AutoFunctionRunnerTest, ResetFunctionWithNullFunction) {
  auto state_for_old_cb = kInitialState;
  auto state_for_new_cb = kInitialState;

  {
    AutoFunctionRunner callback([&] { state_for_old_cb = kChangedState; });
    std::function<void()> new_cb = nullptr;
    callback.Reset(std::move(new_cb));
    EXPECT_FALSE(callback);

    EXPECT_EQ(state_for_old_cb, kInitialState);
    EXPECT_EQ(state_for_new_cb, kInitialState);
  }
  EXPECT_EQ(state_for_old_cb, kInitialState);
  EXPECT_EQ(state_for_new_cb, kInitialState);
}

TEST(AutoFunctionRunnerTest, ResetFunctionWithFunctor) {
  auto state_for_old_cb = kInitialState;
  auto state_for_new_cb = kInitialState;

  {
    AutoFunctionRunner callback([&] { state_for_old_cb = kChangedState; });
    callback.Reset([&] { state_for_new_cb = kChangedState; });
    EXPECT_TRUE(callback);

    EXPECT_EQ(state_for_old_cb, kInitialState);
    EXPECT_EQ(state_for_new_cb, kInitialState);
  }
  EXPECT_EQ(state_for_old_cb, kInitialState);
  EXPECT_EQ(state_for_new_cb, kChangedState);
}

TEST(AutoFunctionRunnerTest, Release) {
  AutoFunctionRunner callback([] {});
  EXPECT_TRUE(callback);

  Closure* ptr = ::util::functional::ToCallback(callback.ReleaseInvocable());
  EXPECT_FALSE(callback);

  ASSERT_TRUE(ptr);
  ptr->Run();
}

TEST(AutoFunctionRunnerTest, ReleaseInvocable) {
  AutoFunctionRunner callback([] {});
  EXPECT_TRUE(callback);

  absl::AnyInvocable<void() &&> f = callback.ReleaseInvocable();
  EXPECT_FALSE(callback);

  ASSERT_TRUE(f);
  std::move(f)();
}

// Additional state value for testing assignment
constexpr auto kChangedAgainState = 2;
static_assert(kChangedAgainState != kInitialState,
              "See `kInitialState != kChangedState` above.");
static_assert(kChangedAgainState != kChangedState,
              "See `kInitialState != kChangedState` above.");

TEST(AutoFunctionRunnerTest, Assign) {
  // engaged = engaged;
  {
    auto state = kInitialState;

    {
      AutoFunctionRunner lhs_callback([&] { state = kChangedState; });
      AutoFunctionRunner rhs_callback([&] { state = kChangedAgainState; });

      EXPECT_TRUE(lhs_callback);
      EXPECT_TRUE(rhs_callback);
      EXPECT_EQ(state, kInitialState);

      lhs_callback = std::move(rhs_callback);
      EXPECT_FALSE(rhs_callback);
      EXPECT_EQ(state, kChangedState);
    }

    EXPECT_EQ(state, kChangedAgainState);
  }

  // engaged = disengaged;
  {
    auto state = kInitialState;

    {
      AutoFunctionRunner lhs_callback([&] { state = kChangedState; });
      AutoFunctionRunner rhs_callback([&] { state = kChangedAgainState; });
      rhs_callback.Cancel();

      EXPECT_TRUE(lhs_callback);
      EXPECT_FALSE(rhs_callback);
      EXPECT_EQ(state, kInitialState);

      lhs_callback = std::move(rhs_callback);

      EXPECT_FALSE(rhs_callback);
      EXPECT_EQ(state, kChangedState);
    }

    EXPECT_EQ(state, kChangedState);
  }

  // disengaged = engaged;
  {
    auto state = kInitialState;

    {
      AutoFunctionRunner lhs_callback([&] { state = kChangedState; });
      AutoFunctionRunner rhs_callback([&] { state = kChangedAgainState; });
      lhs_callback.Cancel();

      EXPECT_FALSE(lhs_callback);
      EXPECT_TRUE(rhs_callback);
      EXPECT_EQ(state, kInitialState);

      lhs_callback = std::move(rhs_callback);

      EXPECT_TRUE(lhs_callback);
      EXPECT_FALSE(rhs_callback);
      EXPECT_EQ(state, kInitialState);
    }

    EXPECT_EQ(state, kChangedAgainState);
  }

  // disengaged = disengaged;
  {
    auto state = kInitialState;

    {
      AutoFunctionRunner lhs_callback([&] { state = kChangedState; });
      AutoFunctionRunner rhs_callback([&] { state = kChangedAgainState; });
      lhs_callback.Cancel();
      rhs_callback.Cancel();

      EXPECT_FALSE(lhs_callback);
      EXPECT_FALSE(rhs_callback);
      EXPECT_EQ(state, kInitialState);

      lhs_callback = std::move(rhs_callback);

      EXPECT_FALSE(lhs_callback);
      EXPECT_FALSE(rhs_callback);
      EXPECT_EQ(state, kInitialState);
    }

    EXPECT_EQ(state, kInitialState);
  }
}

}  // namespace

}  // namespace util::functional
