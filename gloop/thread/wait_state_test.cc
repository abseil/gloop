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

#include "gloop/thread/wait_state.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/no_destructor.h"
#include "absl/cleanup/cleanup.h"
#include "absl/flags/flag.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/strings/cord.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/barrier.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/thread-identity.h"
#include "gloop/base/walltime.h"
#include "gloop/thread/thread-internal.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"
#include "gloop/thread/threadpool.h"
#include "gloop/thread/timedcall.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace thread {

using ::testing::HasSubstr;
using ::testing::Not;

TEST(WaitStateScopeTest, ScopeRestoresPriorState) {
  // NOLINTNEXTLINE(abseil-no-internal-dependencies)
  absl::base_internal::ThreadIdentity* ti =
      absl::base_internal::CurrentThreadIdentityIfPresent();
  ASSERT_NE(ti, nullptr);

  // The default state is kActive.
  EXPECT_EQ(ti->wait_state.load(), WaitStateScope::WaitState::kActive);
  {
    // Create the first scope and expect to see its effect.
    WaitStateScope scope1(WaitStateScope::WaitState::kWaitingForWork);
    EXPECT_EQ(ti->wait_state.load(),
              WaitStateScope::WaitState::kWaitingForWork);
    {
      // Create another scope.
      WaitStateScope scope1(WaitStateScope::WaitState::kActive);
      EXPECT_EQ(ti->wait_state.load(), WaitStateScope::WaitState::kActive);
    }
    // After popping the second scope, we should have our first scope back.
    EXPECT_EQ(ti->wait_state.load(),
              WaitStateScope::WaitState::kWaitingForWork);
  }

  // The default state should be restored.
  EXPECT_EQ(ti->wait_state.load(), WaitStateScope::WaitState::kActive);
}

TEST(WaitStateScopeTest, ConditionalScope) {
  // NOLINTNEXTLINE(abseil-no-internal-dependencies)
  absl::base_internal::ThreadIdentity* ti =
      absl::base_internal::CurrentThreadIdentityIfPresent();
  ASSERT_NE(ti, nullptr);

  // The default state is kActive.
  EXPECT_EQ(ti->wait_state.load(), WaitStateScope::WaitState::kActive);
  {
    // Create a scope object which is conditionally disabled. We should not see
    // a change in state.
    WaitStateScope scope1(WaitStateScope::WaitState::kWaitingForWork,
                          /*enabled=*/false);
    EXPECT_EQ(ti->wait_state.load(), WaitStateScope::WaitState::kActive);

    // Create another scope object which is conditionally enabled.
    WaitStateScope scope2(WaitStateScope::WaitState::kWaitingForWork,
                          /*enabled=*/true);
    EXPECT_EQ(ti->wait_state.load(),
              WaitStateScope::WaitState::kWaitingForWork);

    // Create another disabled scope object.
    WaitStateScope scope3(WaitStateScope::WaitState::kActive,
                          /*enabled=*/false);
    EXPECT_EQ(ti->wait_state.load(),
              WaitStateScope::WaitState::kWaitingForWork);
  }

  // Pop every object. The default state should be restored.
  EXPECT_EQ(ti->wait_state.load(), WaitStateScope::WaitState::kActive);
}

}  // namespace thread
