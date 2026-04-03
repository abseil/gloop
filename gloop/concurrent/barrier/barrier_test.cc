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

#include "gloop/concurrent/barrier/barrier.h"

#include <functional>
#include <thread>  // NOLINT
#include <vector>

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"

namespace concurrent {
namespace {

TEST(BarrierTest, SameThread) {
  absl::Notification n;
  std::function<void()> barrier = NewBarrier(3, [&n]() { n.Notify(); });
  EXPECT_FALSE(n.HasBeenNotified());
  barrier();
  EXPECT_FALSE(n.HasBeenNotified());
  barrier();
  EXPECT_FALSE(n.HasBeenNotified());
  barrier();
  EXPECT_TRUE(n.HasBeenNotified());
}

TEST(BarrierTest, InStdThread) {
  absl::Notification n;
  std::function<void()> barrier = NewBarrier(10, [&n]() { n.Notify(); });
  std::vector<std::thread> threads;
  for (int i = 0; i < 10; ++i) threads.emplace_back(barrier);
  for (auto& thread : threads) thread.join();
  EXPECT_TRUE(n.HasBeenNotified());
}

TEST(BarrierTest, ZeroCount) {
  absl::Notification n;
  std::function<void()> barrier = NewBarrier(0, [&n]() { n.Notify(); });
  EXPECT_TRUE(n.HasBeenNotified());
}

}  // namespace
}  // namespace concurrent
