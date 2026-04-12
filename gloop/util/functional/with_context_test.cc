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

#include "gloop/util/functional/with_context.h"

#include <functional>
#include <memory>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "gloop/base/context.h"
#include "gloop/base/tracecontext.h"
#include "gloop/gloop_test.h"
#include "gloop/perftools/tracing/mock_trace_event_listener.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/with_trace_event_listener.h"

namespace util {
namespace functional {

namespace {

using ::perftools::tracing::MockTraceEventListener;
using ::perftools::tracing::StringRef;
using ::perftools::tracing::WithTraceEventListener;
using ::testing::_;
using ::testing::Eq;
using ::testing::NiceMock;
using ::testing::StrEq;
using ::testing::WithArgs;

constexpr int kExistingRpcId = 3;
constexpr int kNewRpcId = 4;
constexpr int kMagicReturnValue = 5;

// Creates a Context which differs from the current one by the given rpc_id.
// We can use to disambiguate various Contexts in our tests.
base::Context CreateContext(int rpc_id) {
  TraceContext new_trace(base::CurrentContext().trace_context());
  new_trace.set_rpc_id(rpc_id);
  return base::ContextBuilder(base::CurrentContext())
      .set_trace_context(new_trace)
      .BuildValue();
}

TEST(WithContextTest, WrapFunctionVoidNoArgs) {
  // Set our initial rpc id to a known value
  const base::WithContext with_existing_context(CreateContext(kExistingRpcId));

  bool called = false;
  auto func = [&called] {
    EXPECT_EQ(base::CurrentContext().trace_context().rpc_id(), kNewRpcId);
    called = true;
  };
  std::function<void()> wrapped;
  {
    // Use a different rpc id during our call to WithCurrentContext
    const base::WithContext with_new_context(CreateContext(kNewRpcId));
    wrapped = WithCurrentContext(func);
  }

  wrapped();
  ASSERT_TRUE(called);
}

TEST(WithContextTest, WrapFunctionVoidWithArgs) {
  // Set our initial rpc id to a known value
  const base::WithContext with_existing_context(CreateContext(kExistingRpcId));

  std::function<void(bool&)> func = [](bool& called) {
    EXPECT_EQ(base::CurrentContext().trace_context().rpc_id(), kNewRpcId);
    called = true;
  };
  std::function<void(bool&)> wrapped;
  {
    // Use a different rpc id during our call to WithCurrentContext
    const base::WithContext with_new_context(CreateContext(kNewRpcId));
    wrapped = WithCurrentContext(func);
  }

  bool called = false;
  wrapped(called);
  ASSERT_TRUE(called);
}

TEST(WithContextTest, WrapFunctionNonVoidNoArgs) {
  // Set our initial rpc id to a known value
  const base::WithContext with_existing_context(CreateContext(kExistingRpcId));

  bool called = false;
  std::function<int()> func = [&called]() -> int {
    EXPECT_EQ(base::CurrentContext().trace_context().rpc_id(), kNewRpcId);
    called = true;
    return kMagicReturnValue;
  };
  std::function<int()> wrapped;
  {
    // Use a different rpc id during our call to WithCurrentContext
    const base::WithContext with_new_context(CreateContext(kNewRpcId));
    wrapped = WithCurrentContext(func);
  }

  ASSERT_EQ(kMagicReturnValue, wrapped());
  ASSERT_TRUE(called);
}

TEST(WithContextTest, WrapMovableAnyInvocableVoidNoArgs) {
  // Set our initial rpc id to a known value
  const base::WithContext with_existing_context(CreateContext(kExistingRpcId));

  bool called = false;
  absl::AnyInvocable<void() &&> func = [&called] {
    EXPECT_EQ(base::CurrentContext().trace_context().rpc_id(), kNewRpcId);
    called = true;
  };
  absl::AnyInvocable<void() &&> wrapped;
  {
    // Use a different rpc id during our call to WithCurrentContext
    const base::WithContext with_new_context(CreateContext(kNewRpcId));
    wrapped = WithCurrentContext(std::move(func));
  }

  std::move(wrapped)();
  ASSERT_TRUE(called);
}

TEST(WithContextTest, WrapMovableAnyInvocableVoidWithArgs) {
  // Set our initial rpc id to a known value
  const base::WithContext with_existing_context(CreateContext(kExistingRpcId));

  absl::AnyInvocable<void(bool&) &&> func = [](bool& called) {
    EXPECT_EQ(base::CurrentContext().trace_context().rpc_id(), kNewRpcId);
    called = true;
  };
  absl::AnyInvocable<void(bool&) &&> wrapped;
  {
    // Use a different rpc id during our call to WithCurrentContext
    const base::WithContext with_new_context(CreateContext(kNewRpcId));
    wrapped = WithCurrentContext(std::move(func));
  }

  bool called = false;
  std::move(wrapped)(called);
  ASSERT_TRUE(called);
}

TEST(WithContextTest, WrapMovableAnyInvocableNonVoidNoArgs) {
  // Set our initial rpc id to a known value
  const base::WithContext with_existing_context(CreateContext(kExistingRpcId));

  bool called = false;
  absl::AnyInvocable<int() &&> func = [&called]() -> int {
    EXPECT_EQ(base::CurrentContext().trace_context().rpc_id(), kNewRpcId);
    called = true;
    return kMagicReturnValue;
  };
  absl::AnyInvocable<int() &&> wrapped;
  {
    // Use a different rpc id during our call to WithCurrentContext
    const base::WithContext with_new_context(CreateContext(kNewRpcId));
    wrapped = WithCurrentContext(std::move(func));
  }

  EXPECT_EQ(kMagicReturnValue, std::move(wrapped)());
  ASSERT_TRUE(called);
}

TEST(WithContextTest, WrapMovableAnyInvocableNonVoidWithArgs) {
  // Set our initial rpc id to a known value
  const base::WithContext with_existing_context(CreateContext(kExistingRpcId));

  absl::AnyInvocable<int(bool&) &&> func = [](bool& called) -> int {
    EXPECT_EQ(base::CurrentContext().trace_context().rpc_id(), kNewRpcId);
    called = true;
    return kMagicReturnValue;
  };
  absl::AnyInvocable<int(bool&) &&> wrapped;
  {
    // Use a different rpc id during our call to WithCurrentContext
    const base::WithContext with_new_context(CreateContext(kNewRpcId));
    wrapped = WithCurrentContext(std::move(func));
  }

  bool called = false;
  EXPECT_EQ(kMagicReturnValue, std::move(wrapped)(called));
  ASSERT_TRUE(called);
}

TEST(WithContextTest, WrapCopyableAnyInvocableVoidNoArgs) {
  // Set our initial rpc id to a known value
  const base::WithContext with_existing_context(CreateContext(kExistingRpcId));

  bool called = false;
  absl::AnyInvocable<void()> func = [&called] {
    EXPECT_EQ(base::CurrentContext().trace_context().rpc_id(), kNewRpcId);
    called = true;
  };
  absl::AnyInvocable<void()> wrapped;
  {
    // Use a different rpc id during our call to WithCurrentContext
    const base::WithContext with_new_context(CreateContext(kNewRpcId));
    wrapped = WithCurrentContext(std::move(func));
  }

  std::move(wrapped)();
  ASSERT_TRUE(called);
}

TEST(WithContextTest, WrapCopyableAnyInvocableVoidWithArgs) {
  // Set our initial rpc id to a known value
  const base::WithContext with_existing_context(CreateContext(kExistingRpcId));

  absl::AnyInvocable<void(bool&)> func = [](bool& called) {
    EXPECT_EQ(base::CurrentContext().trace_context().rpc_id(), kNewRpcId);
    called = true;
  };
  absl::AnyInvocable<void(bool&)> wrapped;
  {
    // Use a different rpc id during our call to WithCurrentContext
    const base::WithContext with_new_context(CreateContext(kNewRpcId));
    wrapped = WithCurrentContext(std::move(func));
  }

  bool called = false;
  std::move(wrapped)(called);
  ASSERT_TRUE(called);
}

TEST(WithContextTest, WrapCopyableAnyInvocableNonVoidNoArgs) {
  // Set our initial rpc id to a known value
  const base::WithContext with_existing_context(CreateContext(kExistingRpcId));

  bool called = false;
  absl::AnyInvocable<int()> func = [&called]() -> int {
    EXPECT_EQ(base::CurrentContext().trace_context().rpc_id(), kNewRpcId);
    called = true;
    return kMagicReturnValue;
  };
  absl::AnyInvocable<int()> wrapped;
  {
    // Use a different rpc id during our call to WithCurrentContext
    const base::WithContext with_new_context(CreateContext(kNewRpcId));
    wrapped = WithCurrentContext(std::move(func));
  }

  EXPECT_EQ(kMagicReturnValue, std::move(wrapped)());
  ASSERT_TRUE(called);
}

TEST(WithContextTest, WrapCopyableAnyInvocableNonVoidWithArgs) {
  // Set our initial rpc id to a known value
  const base::WithContext with_existing_context(CreateContext(kExistingRpcId));

  absl::AnyInvocable<int(bool&)> func = [](bool& called) -> int {
    EXPECT_EQ(base::CurrentContext().trace_context().rpc_id(), kNewRpcId);
    called = true;
    return kMagicReturnValue;
  };
  absl::AnyInvocable<int(bool&)> wrapped;
  {
    // Use a different rpc id during our call to WithCurrentContext
    const base::WithContext with_new_context(CreateContext(kNewRpcId));
    wrapped = WithCurrentContext(std::move(func));
  }

  bool called = false;
  EXPECT_EQ(kMagicReturnValue, std::move(wrapped)(called));
  ASSERT_TRUE(called);
}

TEST(WithContextTest, WrapMoveOnlyArgs) {
  // Set our initial rpc id to a known value
  const base::WithContext with_existing_context(CreateContext(kExistingRpcId));

  absl::AnyInvocable<int(std::unique_ptr<int>, std::unique_ptr<int>)> sum =
      [](std::unique_ptr<int> x, std::unique_ptr<int> y) -> int {
    EXPECT_EQ(base::CurrentContext().trace_context().rpc_id(), kNewRpcId);
    return *x + *y;
  };
  absl::AnyInvocable<int(std::unique_ptr<int>, std::unique_ptr<int>)>
      wrapped_sum;
  {
    // Use a different rpc id during our call to WithCurrentContext
    const base::WithContext with_new_context(CreateContext(kNewRpcId));
    wrapped_sum = WithCurrentContext(std::move(sum));
  }

  EXPECT_EQ(9, std::move(wrapped_sum)(std::make_unique<int>(4),
                                      std::make_unique<int>(5)));
}

// Tests for various const- and reference-qualified operators
TEST(WithContextTest, OperatorConstRef) {
  // Set our initial rpc id to a known value
  const base::WithContext with_existing_context(CreateContext(kExistingRpcId));

  struct Sum {
    int operator()(int x, int y) const& {
      EXPECT_EQ(base::CurrentContext().trace_context().rpc_id(), kNewRpcId);
      return x + y;
    }
  };
  Sum sum;
  absl::AnyInvocable<int(int, int)> wrapped_sum;
  {
    // Use a different rpc id during our call to WithCurrentContext
    const base::WithContext with_new_context(CreateContext(kNewRpcId));
    wrapped_sum = WithCurrentContext(std::move(sum));
  }

  EXPECT_EQ(9, wrapped_sum(4, 5));
}

TEST(WithContextTest, OperatorRef) {
  // Set our initial rpc id to a known value
  const base::WithContext with_existing_context(CreateContext(kExistingRpcId));

  struct Sum {
    int operator()(int x, int y) & {
      EXPECT_EQ(base::CurrentContext().trace_context().rpc_id(), kNewRpcId);
      return x + y;
    }
  };
  Sum sum;
  absl::AnyInvocable<int(int, int)> wrapped_sum;
  {
    // Use a different rpc id during our call to WithCurrentContext
    const base::WithContext with_new_context(CreateContext(kNewRpcId));
    wrapped_sum = WithCurrentContext(std::move(sum));
  }

  EXPECT_EQ(9, wrapped_sum(4, 5));
}

TEST(WithContextTest, OperatorRefRef) {
  // Set our initial rpc id to a known value
  const base::WithContext with_existing_context(CreateContext(kExistingRpcId));

  struct Sum {
    int operator()(int x, int y) && {
      EXPECT_EQ(base::CurrentContext().trace_context().rpc_id(), kNewRpcId);
      return x + y;
    }
  };
  Sum sum;
  absl::AnyInvocable<int(int, int) &&> wrapped_sum;
  {
    // Use a different rpc id during our call to WithCurrentContext
    const base::WithContext with_new_context(CreateContext(kNewRpcId));
    wrapped_sum = WithCurrentContext(std::move(sum));
  }

  EXPECT_EQ(9, std::move(wrapped_sum)(4, 5));
}

TEST(WithContextTest, ExplicitContext) {
  // Set our initial rpc id to a known value
  const base::WithContext with_existing_context(CreateContext(kExistingRpcId));

  bool called = false;
  auto func = [&called] {
    EXPECT_EQ(base::CurrentContext().trace_context().rpc_id(), kNewRpcId);
    called = true;
  };
  std::function<void()> wrapped;
  // Use a different rpc id during our call to WithCurrentContext

  wrapped = WithContext(std::move(func), CreateContext(kNewRpcId));

  wrapped();
  ASSERT_TRUE(called);
}

TEST(WithContextTest, ConstOperatorConstRef) {
  // Set our initial rpc id to a known value
  const base::WithContext with_existing_context(CreateContext(kExistingRpcId));

  struct Sum {
    int operator()(int x, int y) const& {
      EXPECT_EQ(base::CurrentContext().trace_context().rpc_id(), kNewRpcId);
      return x + y;
    }
  };
  Sum sum;
  // Use a different rpc id
  const absl::AnyInvocable<int(int, int) const> wrapped_sum =
      WithContext(std::move(sum), CreateContext(kNewRpcId));

  EXPECT_EQ(9, wrapped_sum(4, 5));
}

TEST(WithContextTest, ConstOperatorConstRefRef) {
  // Set our initial rpc id to a known value
  const base::WithContext with_existing_context(CreateContext(kExistingRpcId));

  struct Sum {
    int operator()(int x, int y) const&& {
      EXPECT_EQ(base::CurrentContext().trace_context().rpc_id(), kNewRpcId);
      return x + y;
    }
  };
  Sum sum;
  // Use a different rpc id
  const absl::AnyInvocable<int(int, int) const&&> wrapped_sum =
      WithContext(std::move(sum), CreateContext(kNewRpcId));

  EXPECT_EQ(9, std::move(wrapped_sum)(4, 5));
}

}  // namespace

}  // namespace functional
}  // namespace util
