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

#include "gloop/thread/shutdown_gate.h"

#include <memory>

#include "absl/synchronization/barrier.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/thread/thread.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace thread {
namespace {

using ::testing::_;

TEST(ShutdownGate, NoParticipants) {
  ShutdownGate shutdown_gate;
  EXPECT_FALSE(shutdown_gate.IsClosed());
  shutdown_gate.CloseAndWait();
  EXPECT_TRUE(shutdown_gate.IsClosed());
#ifndef NDEBUG
  EXPECT_DEATH_IF_SUPPORTED(shutdown_gate.Enter(), _);
  EXPECT_DEATH_IF_SUPPORTED(shutdown_gate.Leave(), _);
#endif
}

TEST(ShutdownGate, OneParticipant) {
  ShutdownGate shutdown_gate;
  shutdown_gate.Enter();
  shutdown_gate.Leave();
  EXPECT_FALSE(shutdown_gate.IsClosed());
  ASSERT_TRUE(shutdown_gate.TryEnter());
  shutdown_gate.Leave();
  EXPECT_FALSE(shutdown_gate.IsClosed());
  shutdown_gate.CloseAndWait();
  EXPECT_TRUE(shutdown_gate.IsClosed());
}

TEST(ShutdownGate, ManyParticipants) {
  ShutdownGate shutdown_gate;
  absl::Barrier barrier(4);
  const auto& thread_closure = [&]() -> void {
    shutdown_gate.Enter();
    barrier.Block();
    while (!shutdown_gate.IsClosed()) {
      absl::SleepFor(absl::Milliseconds(1));
    }
    shutdown_gate.Leave();
  };
  auto thread1 = std::make_unique<ClosureThread>(thread_closure);
  auto thread2 = std::make_unique<ClosureThread>(thread_closure);
  auto thread3 = std::make_unique<ClosureThread>(thread_closure);
  thread1->Start();
  thread2->Start();
  thread3->Start();
  barrier.Block();
  shutdown_gate.CloseAndWait();
}

}  // namespace
}  // namespace thread
