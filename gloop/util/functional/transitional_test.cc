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

#include "gloop/util/functional/transitional.h"

#include <functional>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/synchronization/notification.h"
#include "gloop/base/context.h"
#include "gloop/base/tracecontext.h"
#include "gtest/gtest.h"

namespace util {
namespace functional {
namespace transitional {

namespace {

constexpr int kExistingRpcId = 3;
constexpr int kNewRpcId = 4;

// Creates a Context which differs from the current one by the given rpc_id.
// We can use to disambiguate various Contexts in our tests.
base::Context CreateContext(int rpc_id) {
  TraceContext new_trace(base::CurrentContext().trace_context());
  new_trace.set_rpc_id(rpc_id);
  return base::ContextBuilder(base::CurrentContext())
      .set_trace_context(new_trace)
      .BuildValue();
}

TEST(TransitionalWithContextTest, CaptureContext) {
  // Set our initial rpc id to a known value
  const base::WithContext with_existing_context(CreateContext(kExistingRpcId));

  absl::Notification n;
  std::function<void()> func = [&n] {
    EXPECT_EQ(base::CurrentContext().trace_context().rpc_id(), kNewRpcId);
    n.Notify();
  };
  std::function<void()> wrapped;
  {
    // Use a different rpc id during our call to WithCurrentContext
    const base::WithContext with_new_context(CreateContext(kNewRpcId));
    wrapped = MaybeWithCurrentContext(true, func);
  }

  wrapped();
  ASSERT_TRUE(n.HasBeenNotified());
}

TEST(TransitionalWithContextTest, DontCaptureContext) {
  // Set our initial rpc id to a known value
  const base::WithContext with_existing_context(CreateContext(kExistingRpcId));

  absl::Notification n;
  std::function<void()> func = [&n] {
    EXPECT_EQ(base::CurrentContext().trace_context().rpc_id(), kExistingRpcId);
    n.Notify();
  };
  std::function<void()> wrapped;
  {
    // Use a different rpc id during our call to WithCurrentContext
    const base::WithContext with_new_context(CreateContext(kNewRpcId));
    wrapped = MaybeWithCurrentContext(false, func);
  }

  wrapped();
  ASSERT_TRUE(n.HasBeenNotified());
}

TEST(TransitionalWithContextTest, MoveOnlyCaptureContext) {
  // Set our initial rpc id to a known value
  const base::WithContext with_existing_context(CreateContext(kExistingRpcId));

  absl::Notification n;
  absl::AnyInvocable<void() &&> func = [&n] {
    EXPECT_EQ(base::CurrentContext().trace_context().rpc_id(), kNewRpcId);
    n.Notify();
  };
  absl::AnyInvocable<void() &&> wrapped;
  {
    // Use a different rpc id during our call to WithCurrentContext
    const base::WithContext with_new_context(CreateContext(kNewRpcId));
    wrapped = MaybeWithCurrentContext(true, std::move(func));
  }

  std::move(wrapped)();
  ASSERT_TRUE(n.HasBeenNotified());
}

TEST(TransitionalWithContextTest, MoveOnlyDontCaptureContext) {
  // Set our initial rpc id to a known value
  const base::WithContext with_existing_context(CreateContext(kExistingRpcId));

  absl::Notification n;
  absl::AnyInvocable<void() &&> func = [&n] {
    EXPECT_EQ(base::CurrentContext().trace_context().rpc_id(), kExistingRpcId);
    n.Notify();
  };
  absl::AnyInvocable<void() &&> wrapped;
  {
    // Use a different rpc id during our call to WithCurrentContext
    const base::WithContext with_new_context(CreateContext(kNewRpcId));
    wrapped = MaybeWithCurrentContext(false, std::move(func));
  }

  std::move(wrapped)();
  ASSERT_TRUE(n.HasBeenNotified());
}

}  // namespace

}  // namespace transitional
}  // namespace functional
}  // namespace util
