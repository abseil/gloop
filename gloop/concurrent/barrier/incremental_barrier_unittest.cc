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

#include "gloop/concurrent/barrier/incremental_barrier.h"

#include <functional>
#include <memory>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "benchmark/benchmark.h"
#include "gtest/gtest.h"

namespace concurrent {

namespace {

class IncrementalBarrierTest : public testing::Test {
 protected:
  void SetUp() override { done_called_ = false; }

  void TearDown() override {}

 public:
  void Done() { done_called_ = true; }

 protected:
  bool done_called_;
};

TEST_F(IncrementalBarrierTest, NoCallbacks) {
  EXPECT_FALSE(done_called_);
  {
    IncrementalBarrier barrier([this] { Done(); });
    EXPECT_FALSE(done_called_);
  }
  EXPECT_TRUE(done_called_);
}

TEST_F(IncrementalBarrierTest, NoCallbacksFunctionArg) {
  EXPECT_FALSE(done_called_);
  {
    IncrementalBarrier barrier([this]() { Done(); });
    EXPECT_FALSE(done_called_);
  }
  EXPECT_TRUE(done_called_);
}

TEST_F(IncrementalBarrierTest, OneCallbackRunImmediately) {
  EXPECT_FALSE(done_called_);
  {
    IncrementalBarrier barrier([this] { Done(); });
    barrier.InvocableInc()();
    EXPECT_FALSE(done_called_);
  }
  EXPECT_TRUE(done_called_);
}

TEST_F(IncrementalBarrierTest, OneCallbackRunLater) {
  EXPECT_FALSE(done_called_);
  absl::AnyInvocable<void() &&> callback;
  {
    IncrementalBarrier barrier([this] { Done(); });
    callback = barrier.InvocableInc();
    EXPECT_FALSE(done_called_);
  }
  EXPECT_FALSE(done_called_);
  std::move(callback)();
  EXPECT_TRUE(done_called_);
}

TEST_F(IncrementalBarrierTest, OneNow_OneLater) {
  EXPECT_FALSE(done_called_);
  absl::AnyInvocable<void() &&> callback;
  {
    IncrementalBarrier barrier([this] { Done(); });
    callback = barrier.InvocableInc();
    barrier.InvocableInc()();
    EXPECT_FALSE(done_called_);
  }
  EXPECT_FALSE(done_called_);
  std::move(callback)();
  EXPECT_TRUE(done_called_);
}

TEST_F(IncrementalBarrierTest, FourCallbacksRunImmediately) {
  EXPECT_FALSE(done_called_);
  {
    IncrementalBarrier barrier([this] { Done(); });
    barrier.InvocableInc()();
    barrier.InvocableInc()();
    barrier.InvocableInc()();
    barrier.InvocableInc()();
    EXPECT_FALSE(done_called_);
  }
  EXPECT_TRUE(done_called_);
}

TEST_F(IncrementalBarrierTest, FourCallbacksRunLater) {
  absl::AnyInvocable<void() &&> callback1;
  absl::AnyInvocable<void() &&> callback2;
  absl::AnyInvocable<void() &&> callback3;
  absl::AnyInvocable<void() &&> callback4;
  EXPECT_FALSE(done_called_);
  {
    IncrementalBarrier barrier([this] { Done(); });
    callback1 = barrier.InvocableInc();
    callback2 = barrier.InvocableInc();
    callback3 = barrier.InvocableInc();
    callback4 = barrier.InvocableInc();
    EXPECT_FALSE(done_called_);
  }
  EXPECT_FALSE(done_called_);
  std::move(callback1)();
  EXPECT_FALSE(done_called_);
  std::move(callback2)();
  EXPECT_FALSE(done_called_);
  std::move(callback3)();
  EXPECT_FALSE(done_called_);
  std::move(callback4)();
  EXPECT_TRUE(done_called_);
}

TEST_F(IncrementalBarrierTest, TwoNow_TwoLater) {
  absl::AnyInvocable<void() &&> callback1;
  absl::AnyInvocable<void() &&> callback2;
  EXPECT_FALSE(done_called_);
  {
    IncrementalBarrier barrier([this] { Done(); });
    callback1 = barrier.InvocableInc();
    barrier.InvocableInc()();
    callback2 = barrier.InvocableInc();
    barrier.InvocableInc()();
    EXPECT_FALSE(done_called_);
  }
  EXPECT_FALSE(done_called_);
  std::move(callback1)();
  EXPECT_FALSE(done_called_);
  std::move(callback2)();
  EXPECT_TRUE(done_called_);
}

TEST_F(IncrementalBarrierTest, MixedNowAndLater) {
  absl::AnyInvocable<void() &&> callback2;
  std::function<void()> function;
  absl::AnyInvocable<void() &&> invocable;
  EXPECT_FALSE(done_called_);
  {
    IncrementalBarrier barrier([this]() { Done(); });
    barrier.InvocableInc()();
    barrier.FunctionInc()();
    callback2 = barrier.InvocableInc();
    function = barrier.FunctionInc();
    invocable = barrier.InvocableInc();
    EXPECT_FALSE(done_called_);
  }
  EXPECT_FALSE(done_called_);
  std::move(callback2)();
  EXPECT_FALSE(done_called_);
  function();
  EXPECT_FALSE(done_called_);
  std::move(invocable)();
  EXPECT_TRUE(done_called_);
}

TEST_F(IncrementalBarrierTest, MoveOnlyLambda) {
  EXPECT_FALSE(done_called_);
  {
    auto owned_value = std::make_unique<int>(0);
    IncrementalBarrier barrier(
        [this, owned_value = std::move(owned_value)] { Done(); });
    EXPECT_FALSE(done_called_);
  }
  EXPECT_TRUE(done_called_);
}

void BM_FunctionInc(benchmark::State& state) {
  IncrementalBarrier barrier([] {});
  for (auto _ : state) {
    barrier.FunctionInc()();
  }
}
BENCHMARK(BM_FunctionInc);

void BM_InvocableInc(benchmark::State& state) {
  IncrementalBarrier barrier([] {});
  for (auto _ : state) {
    barrier.InvocableInc()();
  }
}
BENCHMARK(BM_InvocableInc);

}  // namespace

}  // namespace concurrent
