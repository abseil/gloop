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

// TODO: add fuzz testing
#include "gloop/perftools/tracing/sync_context.h"

#include <optional>
#include <utility>

#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/perftools/tracing/mock_trace_event_listener.h"
#include "gloop/perftools/tracing/noop_trace_event_listener.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/test_only_access.h"
#include "gloop/perftools/tracing/trace_event_listener.h"
#include "gloop/perftools/tracing/trace_source_location.h"
#include "gloop/perftools/tracing/tracing_base.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace perftools::tracing::core {

using ::perftools::tracing::testing::TestOnlyAccess;
using ::testing::_;
using ::testing::AllOf;
using ::testing::AnyNumber;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::InSequence;
using ::testing::Le;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;
using State = SyncContext::State;

namespace {

SyncContext::Access access() {
  return TestOnlyAccess::Create<SyncContext::Access>();
}

// Whitebox: SyncContext will assign odd numbers for 'out of thin air' contexts
// created through copies, and even numbers for contexts intended for explicit
// scheduling through 'CreateThread'.
constexpr SyncId kFirstThreadSyncId{2};
constexpr SyncId kFirstAutoSyncId{3};

// Test fixture for SyncContext providing a default context and mock listener,
// and automatically resetting TLS state and releasing `mock_` on teardown.
class SyncContextTest : public ::testing::Test {
 public:
  ~SyncContextTest() override {
    EXPECT_CALL(mock_, ReleaseEventListener()).Times(AnyNumber());
    context_.Reset(TestOnlyAccess());
    SyncContext::ResetThreadState(TestOnlyAccess());
  }

 protected:
  static constexpr SyncId kSyncId{4};
  static constexpr SyncId kActiveSyncId{5};

  // Creates a SyncContext configured with the given test state and IDs.
  static SyncContext CreateSync(TraceEventListener* listener,
                                State state = State::kDefault,
                                SyncId active_sync_id = kNoSyncId,
                                SyncId sync_id = kSyncId,
                                Ancestor ancestor = {}) {
    return SyncContext::Create(TestOnlyAccess(), listener, state, sync_id,
                               active_sync_id, ancestor);
  }

  // Initializes `context_` with `mock_` in `state`. Defaults `active_sync_id`
  // to `kNoSyncId` for `State::kDefault` and `kActiveSyncId` for other states.
  void SetupContext(State state = State::kDefault,
                    std::optional<SyncId> active_sync_id = std::nullopt) {
    SyncId id = active_sync_id.value_or(
        state == State::kDefault ? kNoSyncId : kActiveSyncId);
    context_ = CreateSync(&mock_, state, id, kSyncId);
  }

  // Initializes `context_` as an active context (optionally holding a swapped
  // `nested_listener`) and sets `mock_` and `kActiveSyncId` in TLS.
  void SetupActiveContext(TraceEventListener* nested_listener = nullptr) {
    context_ =
        CreateSync(nested_listener, State::kActive, kActiveSyncId, kSyncId);
    SyncContext::SetActiveContext(TestOnlyAccess(), &context_);
    internal::set_active_sync_id(kActiveSyncId);
    internal::set_active_event_listener(&mock_);
  }

  StrictMock<MockTraceEventListener> mock_;
  SyncContext context_;
};

TEST(SyncContextValueSemantics, DefaultCtor) {
  SyncContext context;
  EXPECT_THAT(context.sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(context.active_sync_id(), Eq(kNoSyncId));
  EXPECT_FALSE(context.ancestor().has_value());
  EXPECT_FALSE(context);
  EXPECT_TRUE(context.empty());
  EXPECT_THAT(context.state(), Eq(SyncContext::State::kEmpty));
  EXPECT_THAT(context.listener(), Eq(nullptr));
  EXPECT_FALSE(context.same_trace(context));
}

TEST(SyncContextValueSemantics, OperationsOnDefaultInstanceAreNops) {
  MockTraceEventListener mock;
  SyncContext context, other;
  EXPECT_TRUE(context.CreateThread("Thread").empty());
  EXPECT_TRUE(context.CreateNoop().empty());
  context.BeforeSwapCurrent(access(), other);
  context.AfterSwapCurrent(access(), "Swap");
  context.BeforeRestoreCurrent(access(), other);
  context.AfterRestoreCurrent(access(), "Restore");
  context.AddListener(nullptr);
  EXPECT_FALSE(other.ContainsListener(nullptr));
  EXPECT_FALSE(other.ContainsListener(&mock));

  using std::swap;
  swap(context, other);
  EXPECT_TRUE(context.empty());
  EXPECT_TRUE(other.empty());
}

TEST(SyncContextValueSemantics, CopyConstruct) {
  SyncContext empty1;
  SyncContext empty2(empty1);
  EXPECT_TRUE(empty1.empty());
  EXPECT_TRUE(empty2.empty());

  StrictMock<MockTraceEventListener> mock1, mock2;
  SyncContext src;
  src.AddListener(&mock1);

  SyncId sync_id = kFirstAutoSyncId;
  EXPECT_CALL(mock1, GetEventListener(sync_id)).WillOnce(Return(&mock2));
  SyncContext context(src);
  EXPECT_THAT(context.sync_id(), Eq(sync_id));
  EXPECT_THAT(context.listener(), Eq(&mock2));
}

TEST(SyncContextValueSemantics, CopyAssign) {
  SyncContext empty1;
  SyncContext empty2;
  empty2 = empty1;
  EXPECT_TRUE(empty1.empty());
  EXPECT_TRUE(empty2.empty());

  StrictMock<MockTraceEventListener> mock1, mock2;
  SyncContext src;
  src.AddListener(&mock1);

  SyncContext context;
  SyncId sync_id = kFirstAutoSyncId;
  EXPECT_CALL(mock1, GetEventListener(sync_id)).WillOnce(Return(&mock2));
  context = src;
  EXPECT_THAT(context.sync_id(), Eq(sync_id));
  EXPECT_THAT(context.listener(), Eq(&mock2));

  SyncContext empty;
  EXPECT_CALL(mock2, ReleaseEventListener());
  context = empty;
  EXPECT_TRUE(context.empty());
}

TEST(SyncContextValueSemantics, MoveConstruct) {
  SyncContext empty1;
  SyncContext empty2(std::move(empty1));
  EXPECT_TRUE(empty1.empty());  // NOLINT
  EXPECT_TRUE(empty2.empty());

  StrictMock<MockTraceEventListener> mock;
  SyncContext src;
  src.AddListener(&mock);
  SyncContext context(std::move(src));
  EXPECT_THAT(context.sync_id(), Eq(kMainSyncId));
  EXPECT_THAT(context.listener(), Eq(&mock));
  EXPECT_THAT(src.sync_id(), Eq(kNoSyncId));  // NOLINT
  EXPECT_THAT(src.listener(), Eq(nullptr));   // NOLINT
}

TEST(SyncContextValueSemantics, MoveAssign) {
  SyncContext empty1;
  SyncContext empty2;
  empty2 = std::move(empty1);
  EXPECT_TRUE(empty1.empty());  // NOLINT
  EXPECT_TRUE(empty2.empty());

  StrictMock<MockTraceEventListener> mock;
  SyncContext src;
  src.AddListener(&mock);
  SyncContext context;
  context = std::move(src);
  EXPECT_THAT(context.sync_id(), Eq(kMainSyncId));
  EXPECT_THAT(context.listener(), Eq(&mock));
  EXPECT_THAT(src.sync_id(), Eq(kNoSyncId));  // NOLINT
  EXPECT_THAT(src.listener(), Eq(nullptr));   // NOLINT

  SyncContext empty;
  EXPECT_CALL(mock, ReleaseEventListener());
  context = std::move(empty);
  EXPECT_THAT(empty.sync_id(), Eq(kNoSyncId));  // NOLINT
  EXPECT_THAT(empty.listener(), Eq(nullptr));   // NOLINT
  EXPECT_THAT(context.sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(context.listener(), Eq(nullptr));
}

TEST(SyncContextValueSemantics, SelfCopyAssign) {
  StrictMock<MockTraceEventListener> mock;
  SyncContext context;
  context.AddListener(&mock);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wself-assign-overloaded"
  context = context;
#pragma GCC diagnostic pop

  EXPECT_THAT(context.sync_id(), Eq(kMainSyncId));
  EXPECT_THAT(context.listener(), Eq(&mock));
  EXPECT_CALL(mock, ReleaseEventListener());
}

TEST(SyncContextValueSemantics, SelfMoveAssign) {
  StrictMock<MockTraceEventListener> mock;
  SyncContext context;
  context.AddListener(&mock);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wself-move"
  context = std::move(context);
#pragma GCC diagnostic pop

  EXPECT_THAT(context.sync_id(), Eq(kMainSyncId));
  EXPECT_THAT(context.listener(), Eq(&mock));
  EXPECT_CALL(mock, ReleaseEventListener());
}

TEST(SyncContextValueSemantics, Swap) {
  StrictMock<MockTraceEventListener> mock1, mock2;
  EXPECT_CALL(mock1, GetEventListener(_)).WillOnce(Return(&mock2));
  SyncContext lhs;
  lhs.AddListener(&mock1);
  SyncContext rhs = lhs;

  SyncId id1 = lhs.sync_id();
  SyncId id2 = rhs.sync_id();

  using std::swap;
  swap(lhs, rhs);

  EXPECT_THAT(lhs.sync_id(), Eq(id2));
  EXPECT_THAT(rhs.sync_id(), Eq(id1));
  EXPECT_THAT(lhs.listener(), Eq(&mock2));
  EXPECT_THAT(rhs.listener(), Eq(&mock1));

  SyncContext empty;
  swap(lhs, empty);
  EXPECT_THAT(lhs.sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(lhs.listener(), Eq(nullptr));
  EXPECT_THAT(empty.sync_id(), Eq(id2));
  EXPECT_THAT(empty.listener(), Eq(&mock2));
}

TEST_F(SyncContextTest, AddListener) {
  context_.AddListener(&mock_);
  EXPECT_TRUE(context_);
  EXPECT_FALSE(context_.empty());
  EXPECT_THAT(context_.sync_id(), Eq(kMainSyncId));
  EXPECT_THAT(context_.active_sync_id(), Eq(kNoSyncId));
  EXPECT_FALSE(context_.ancestor().has_value());
  EXPECT_THAT(context_.state(), Eq(State::kDefault));
  EXPECT_CALL(mock_, ReleaseEventListener());
  context_ = {};
}

TEST_F(SyncContextTest, AddNullptrListener) {
  context_.AddListener(nullptr);
  EXPECT_TRUE(context_.empty());

  context_.AddListenerToCurrent(access(), nullptr);
  EXPECT_TRUE(context_.empty());
}

TEST_F(SyncContextTest, ListenersAreReleaseInLifoOrder) {
  StrictMock<MockTraceEventListener> mock1, mock2;
  InSequence in_sequence;
  context_.AddListener(&mock1);
  context_.AddListener(&mock2);
  EXPECT_FALSE(context_.empty());
  EXPECT_CALL(mock2, ReleaseEventListener());
  EXPECT_CALL(mock1, ReleaseEventListener());
  context_ = {};
}

TEST_F(SyncContextTest, DestroyingEmptyContextIsNoOp) {
  SetupActiveContext();
  {
    SyncContext empty;
  }
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(&context_));
}

TEST_F(SyncContextTest, AddListenerToCurrentOnEmptyContext) {
  EXPECT_CALL(mock_, OnTraceBeginSync(kMainSyncId, Eq("Main")));
  context_.AddListenerToCurrent(access(), &mock_, "Main");

  EXPECT_THAT(context_.state(), Eq(State::kActive));
  EXPECT_THAT(context_.sync_id(), Eq(kMainSyncId));
  EXPECT_THAT(context_.active_sync_id(), Eq(kMainSyncId));
  EXPECT_THAT(context_.listener(), Eq(nullptr));

  EXPECT_THAT(active_sync_id(), Eq(kMainSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(&mock_));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(&context_));
}

TEST_F(SyncContextTest, ListenersOnCurrentAreEndedInLifoOrder) {
  StrictMock<MockTraceEventListener> mock1, mock2;
  InSequence sequence;

  EXPECT_CALL(mock1, OnTraceBeginSync(kMainSyncId, Eq("Main1")));
  context_.AddListenerToCurrent(access(), &mock1, "Main1");

  EXPECT_CALL(mock2, OnTraceBeginSync(kMainSyncId, Eq("Main2")));
  context_.AddListenerToCurrent(access(), &mock2, "Main2");

  EXPECT_CALL(mock2, OnTraceEndSync(kMainSyncId));
  EXPECT_CALL(mock1, OnTraceEndSync(kMainSyncId));
  EXPECT_CALL(mock2, ReleaseEventListener());
  EXPECT_CALL(mock1, ReleaseEventListener());
  context_.EndContext(access());
}

// Matches that `arg` contains a non empty source location.
MATCHER(IsSourceLocation, "Holds a source location") {
  return arg.IsSourceLocation();
}

TEST_F(SyncContextTest, AddListenersToCurrentDefaultsToSourceLocation) {
  NiceMock<MockTraceEventListener> mock2;
  SetupActiveContext();

  EXPECT_CALL(mock2, OnTraceBeginSync(kActiveSyncId, IsSourceLocation()));
  context_.AddListenerToCurrent(access(), &mock2);

  EXPECT_CALL(mock2, OnTraceEndSync(kActiveSyncId));
  EXPECT_CALL(mock_, OnTraceEndSync(kActiveSyncId));
  context_.EndContext(access());
}

TEST_F(SyncContextTest, ContainsListener) {
  StrictMock<MockTraceEventListener> mock2;
  SetupContext();

  // Confirm behavior for `nullptr`. SyncContext always forwards.
  EXPECT_CALL(mock_, Contains(nullptr)).WillOnce(Return(false));
  EXPECT_FALSE(context_.ContainsListener(nullptr));

  // Confirm behavior for `matching` on non empty SyncContext
  EXPECT_CALL(mock_, Contains(&mock_)).WillOnce(Return(true));
  EXPECT_TRUE(context_.ContainsListener(&mock_));

  // Confirm behavior for `non matching` on non empty SyncContext
  EXPECT_CALL(mock_, Contains(&mock2)).WillOnce(Return(false));
  EXPECT_FALSE(context_.ContainsListener(&mock2));
}

TEST_F(SyncContextTest, ContainsListenerOnActiveContext) {
  StrictMock<MockTraceEventListener> mock2;
  SetupActiveContext();

  EXPECT_CALL(mock_, Contains(&mock_)).WillOnce(Return(true));
  EXPECT_TRUE(context_.ContainsListener(&mock_));

  EXPECT_CALL(mock_, Contains(&mock2)).WillOnce(Return(false));
  EXPECT_FALSE(context_.ContainsListener(&mock2));
}

TEST_F(SyncContextTest, ContainsListenerOnEmptyContext) {
  // Calls on an empty handle should never hit crash or anything,
  // and accept nullptr values directly returning false.
  EXPECT_FALSE(context_.ContainsListener(nullptr));
  EXPECT_FALSE(context_.ContainsListener(&mock_));
}

TEST_F(SyncContextTest, RemoveListener) {
  InSequence in_sequence;
  SetupContext();
  EXPECT_CALL(mock_, Extract(&mock_));
  EXPECT_CALL(mock_, ReleaseEventListener());
  context_.RemoveListener(&mock_);
  EXPECT_TRUE(context_.empty());
}

TEST_F(SyncContextTest, RemoveNullptrListener) {
  context_.RemoveListener(nullptr);
  EXPECT_TRUE(context_.empty());

  context_.RemoveListenerFromCurrent(access(), nullptr);
  EXPECT_TRUE(context_.empty());
}

TEST_F(SyncContextTest, RemoveListeners) {
  InSequence in_sequence;
  NiceMock<MockTraceEventListener> mock1, mock2;
  context_.AddListener(&mock1);
  context_.AddListener(&mock2);

  EXPECT_CALL(mock2, Extract(&mock2));
  EXPECT_CALL(mock2, ReleaseEventListener());
  context_.RemoveListener(&mock2);

  EXPECT_CALL(mock1, Extract(&mock1));
  EXPECT_CALL(mock1, ReleaseEventListener());
  context_.RemoveListener(&mock1);
  EXPECT_TRUE(context_.empty());
}

TEST_F(SyncContextTest, RemoveListenersFromCurrent) {
  NiceMock<MockTraceEventListener> mock2;
  SetupActiveContext();
  EXPECT_CALL(mock2, OnTraceBeginSync(kActiveSyncId, _));
  context_.AddListenerToCurrent(access(), &mock2);
  TraceEventListener* active_listener = internal::active_event_listener();

  // EndSync() is the last sync event invoked.
  EXPECT_CALL(mock2, OnTraceEndSync(kActiveSyncId)).WillOnce([&](auto) {
    EXPECT_THAT(internal::active_event_listener(), Eq(active_listener));
  });
  // Extract() is the final interaction where active listener is in place.
  EXPECT_CALL(mock2, Extract(&mock2)).WillOnce([&](auto...) {
    EXPECT_THAT(internal::active_event_listener(), Eq(active_listener));
    return std::pair<TraceEventListener*, bool>(nullptr, true);
  });
  // Release() is guaranteed to be invoked after the active listener is reset.
  EXPECT_CALL(mock2, ReleaseEventListener()).WillOnce([&](auto...) {
    EXPECT_THAT(internal::active_event_listener(), Eq(&mock_));
  });
  context_.RemoveListenerFromCurrent(access(), &mock2);

  // EndSync() is the last sync event invoked.
  EXPECT_CALL(mock_, OnTraceEndSync(kActiveSyncId));
  // Extract() is the final interaction where active listener is in place.
  EXPECT_CALL(mock_, Extract(&mock_)).WillOnce([&](auto...) {
    EXPECT_THAT(internal::active_event_listener(), Eq(&mock_));
    return std::pair<TraceEventListener*, bool>(nullptr, true);
  });
  // Release() is guaranteed to be invoked after the active listener is reset.
  EXPECT_CALL(mock_, ReleaseEventListener()).WillOnce([&](auto...) {
    EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  });
  context_.RemoveListenerFromCurrent(access(), &mock_);
  EXPECT_TRUE(context_.empty());
  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, CornerCaseAddDuplicateListener) {
  InSequence in_sequence;
  context_.AddListener(&mock_);
  context_.AddListener(&mock_);

  EXPECT_CALL(mock_, Extract(&mock_));
  EXPECT_CALL(mock_, ReleaseEventListener());
  context_.RemoveListener(&mock_);
  EXPECT_FALSE(context_.empty());

  EXPECT_CALL(mock_, Extract(&mock_));
  EXPECT_CALL(mock_, ReleaseEventListener());
  context_.RemoveListener(&mock_);
  EXPECT_TRUE(context_.empty());
}

TEST_F(SyncContextTest, CreateThread) {
  StrictMock<MockTraceEventListener> child_mock;
  SetupActiveContext();

  SyncId sync_id = kFirstThreadSyncId;
  EXPECT_CALL(mock_, OnTraceSpawn(sync_id, Eq("Child")));
  EXPECT_CALL(mock_, GetEventListener(sync_id)).WillOnce(Return(&child_mock));
  EXPECT_CALL(child_mock, ReleaseEventListener());
  const absl::Time before = absl::Now();
  SyncContext child = context_.CreateThread("Child");
  const absl::Time after = absl::Now();
  EXPECT_FALSE(child.empty());
  EXPECT_THAT(child.state(), Eq(State::kDefault));
  EXPECT_THAT(child.sync_id(), Eq(sync_id));
  EXPECT_THAT(child.active_sync_id(), Eq(kNoSyncId));
  EXPECT_TRUE(child.ancestor().has_value());
  EXPECT_THAT(child.ancestor().sync_id, Eq(kActiveSyncId));
  EXPECT_THAT(child.ancestor().timestamp, AllOf(Ge(before), Le(after)));
  EXPECT_THAT(child.listener(), Eq(&child_mock));
  EXPECT_TRUE(child.same_trace(context_));
}

TEST_F(SyncContextTest, CreateThreadFailsWithoutActiveListener) {
  context_ = CreateSync(nullptr, State::kActive, kActiveSyncId);
  EXPECT_TRUE(context_.CreateThread("Child").empty());
}

TEST_F(SyncContextTest, CreateThreadFailsWithInvalidState) {
  StrictMock<MockTraceEventListener> active_mock;
  internal::set_active_sync_id(kActiveSyncId);
  internal::set_active_event_listener(&active_mock);

  for (State state : {
           State::kDefault,
           State::kNested,
           State::kSuspended,
           State::kWait,
           State::kZombie,
       }) {
    SCOPED_TRACE(state);
    SetupContext(state);
    EXPECT_TRUE(context_.CreateThread("Child").empty());
  }

  internal::set_active_event_listener(nullptr);
}

TEST_F(SyncContextTest, CreateNoop) {
  SetupActiveContext();

  SyncContext noop = context_.CreateNoop();
  EXPECT_TRUE(noop.same_trace(context_));
  EXPECT_TRUE(noop.ContainsListener(NoopTraceEventListener()));
  EXPECT_THAT(noop.sync_id(), Eq(kFirstAutoSyncId));
  EXPECT_FALSE(noop.ancestor().has_value());
}

TEST_F(SyncContextTest, CopyActiveContextRecordsAncestor) {
  StrictMock<MockTraceEventListener> child_mock;
  SetupActiveContext();

  EXPECT_CALL(mock_, GetEventListener(kFirstAutoSyncId))
      .WillOnce(Return(&child_mock));
  EXPECT_CALL(child_mock, ReleaseEventListener());
  const absl::Time before = absl::Now();
  SyncContext copy = context_;
  const absl::Time after = absl::Now();
  EXPECT_TRUE(copy.ancestor().has_value());
  EXPECT_THAT(copy.ancestor().sync_id, Eq(kActiveSyncId));
  EXPECT_THAT(copy.ancestor().timestamp, AllOf(Ge(before), Le(after)));
}

TEST_F(SyncContextTest, CopyInactiveContextInheritsAncestor) {
  StrictMock<MockTraceEventListener> child_mock;
  const Ancestor initial_ancestor{.sync_id = SyncId{42},
                                  .timestamp = absl::FromUnixSeconds(12345)};
  context_ =
      CreateSync(&mock_, State::kDefault, kNoSyncId, kSyncId, initial_ancestor);

  EXPECT_CALL(mock_, GetEventListener(kFirstAutoSyncId))
      .WillOnce(Return(&child_mock));
  EXPECT_CALL(child_mock, ReleaseEventListener());
  SyncContext copy = context_;
  EXPECT_THAT(copy.ancestor(), Eq(initial_ancestor));
}

TEST_F(SyncContextTest, CopyWithUnrelatedActiveTraceInheritsAncestor) {
  StrictMock<MockTraceEventListener> other_mock, child_mock;
  SetupActiveContext();

  const Ancestor initial_ancestor{.sync_id = SyncId{42},
                                  .timestamp = absl::FromUnixSeconds(12345)};
  SyncContext unrelated = CreateSync(&other_mock, State::kDefault, kNoSyncId,
                                     SyncId{10}, initial_ancestor);

  EXPECT_CALL(other_mock, GetEventListener(kFirstAutoSyncId))
      .WillOnce(Return(&child_mock));
  EXPECT_CALL(child_mock, ReleaseEventListener());
  EXPECT_CALL(other_mock, ReleaseEventListener());
  SyncContext copy = unrelated;
  EXPECT_THAT(copy.ancestor(), Eq(initial_ancestor));
}

TEST_F(SyncContextTest,
       CopyInactiveContextWithSameTraceActiveRecordsActiveSyncId) {
  StrictMock<MockTraceEventListener> inactive_mock, child_mock;
  SetupActiveContext();

  // Create an inactive copy belonging to the same trace while `context_` is not
  // active in TLS so `inactive` starts with an empty ancestor.
  SyncContext::SetActiveContext(TestOnlyAccess(), nullptr);
  EXPECT_CALL(mock_, GetEventListener(kFirstAutoSyncId))
      .WillOnce(Return(&inactive_mock));
  SyncContext inactive = context_;
  ASSERT_FALSE(inactive.ancestor().has_value());

  // Now restore `context_` as the active context in TLS and copy `inactive`.
  SyncContext::SetActiveContext(TestOnlyAccess(), &context_);
  EXPECT_CALL(inactive_mock, GetEventListener(_)).WillOnce(Return(&child_mock));
  EXPECT_CALL(child_mock, ReleaseEventListener());
  EXPECT_CALL(inactive_mock, ReleaseEventListener());
  const absl::Time before = absl::Now();
  SyncContext copy = inactive;
  const absl::Time after = absl::Now();
  EXPECT_TRUE(copy.ancestor().has_value());
  EXPECT_THAT(copy.ancestor().sync_id, Eq(kActiveSyncId));
  EXPECT_THAT(copy.ancestor().timestamp, AllOf(Ge(before), Le(after)));
}

TEST_F(SyncContextTest,
       CopyInactiveContextWithSameTraceActiveReplacesAncestor) {
  StrictMock<MockTraceEventListener> active_mock, child_mock;
  const Ancestor initial_ancestor{.sync_id = SyncId{42},
                                  .timestamp = absl::FromUnixSeconds(12345)};
  context_ =
      CreateSync(&mock_, State::kDefault, kNoSyncId, kSyncId, initial_ancestor);

  EXPECT_CALL(mock_, GetEventListener(kFirstAutoSyncId))
      .WillOnce(Return(&active_mock));
  SyncContext active_copy = context_;

  EXPECT_CALL(active_mock, OnTraceBeginSync(kFirstAutoSyncId, Eq("Begin")));
  ASSERT_TRUE(active_copy.ResumeContext(access(), kNoBarrierId, "Begin"));

  EXPECT_CALL(mock_, GetEventListener(_)).WillOnce(Return(&child_mock));
  EXPECT_CALL(child_mock, ReleaseEventListener());
  const absl::Time before = absl::Now();
  SyncContext copy = context_;
  const absl::Time after = absl::Now();

  EXPECT_TRUE(copy.ancestor().has_value());
  EXPECT_THAT(copy.ancestor().sync_id, Eq(kFirstAutoSyncId));
  EXPECT_THAT(copy.ancestor().timestamp, AllOf(Ge(before), Le(after)));

  EXPECT_CALL(active_mock, OnTraceEndSync(kFirstAutoSyncId));
  EXPECT_CALL(active_mock, ReleaseEventListener());
  EXPECT_TRUE(active_copy.EndContext(access()));
}

TEST_F(SyncContextTest, SameTrace) {
  EXPECT_CALL(mock_, GetEventListener(_)).WillRepeatedly(Return(&mock_));
  SyncContext context1 = CreateSync(&mock_);
  SyncContext context2 = context1;
  SyncContext context3 = context2;
  SyncContext context4 = context3;
  SyncContext context5 = context4;
  EXPECT_TRUE(context1.same_trace(context1));
  EXPECT_TRUE(context1.same_trace(context2));
  EXPECT_TRUE(context1.same_trace(context3));
  EXPECT_TRUE(context1.same_trace(context4));
  EXPECT_TRUE(context1.same_trace(context5));

  SyncContext empty;
  SyncContext other = CreateSync(&mock_);
  EXPECT_FALSE(context1.same_trace(other));
  EXPECT_FALSE(context1.same_trace(empty));
  EXPECT_FALSE(empty.same_trace(empty));
}

TEST_F(SyncContextTest, IsolateFromSideEffectsInReleaseOnRemoveFromCurrent) {
  SetupActiveContext();
  EXPECT_CALL(mock_, OnTraceEndSync(kActiveSyncId));
  EXPECT_CALL(mock_, Extract(&mock_));
  EXPECT_CALL(mock_, ReleaseEventListener()).WillOnce([&]() {
    EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  });
  context_.RemoveListenerFromCurrent(access(), &mock_);
  EXPECT_TRUE(context_.empty());
  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, IsolateFromSideEffectsInReleaseOnDelete) {
  SetupContext();
  EXPECT_CALL(mock_, ReleaseEventListener()).WillOnce([&]() {
    EXPECT_TRUE(context_.empty());
  });
  context_ = {};
}

// Listeners must be able to observe the active context (e.g. to resolve the
// current ancestor) from inside the events that start or resume an execution.
TEST_F(SyncContextTest, ActiveContextIsSetBeforeBeginResumeContinueAndEnter) {
  const auto expect_active = [&](SyncId sync_id) {
    EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(&context_));
    EXPECT_THAT(active_sync_id(), Eq(sync_id));
  };

  // OnTraceBeginSync via AfterSwapCurrent / ResumeContext.
  for (int i = 0; i < 2; ++i) {
    SetupContext();
    EXPECT_CALL(mock_, OnTraceBeginSync(kSyncId, _)).WillOnce([&](auto...) {
      expect_active(kSyncId);
    });
    if (i == 0) {
      context_.AfterSwapCurrent(access(), "Begin");
    } else {
      EXPECT_TRUE(context_.ResumeContext(access(), kNoBarrierId, "Begin"));
    }
    SyncContext::ResetThreadState(TestOnlyAccess());
    context_.Reset(TestOnlyAccess());
  }

  // OnTraceResumeSync via AfterSwapCurrent / ResumeContext.
  for (int i = 0; i < 2; ++i) {
    SetupContext(State::kSuspended);
    EXPECT_CALL(mock_, OnTraceResumeSync(kActiveSyncId)).WillOnce([&](auto...) {
      expect_active(kActiveSyncId);
    });
    if (i == 0) {
      context_.AfterSwapCurrent(access(), "Resume");
    } else {
      EXPECT_TRUE(context_.ResumeContext(access(), kNoBarrierId, "Resume"));
    }
    SyncContext::ResetThreadState(TestOnlyAccess());
    context_.Reset(TestOnlyAccess());
  }

  // OnTraceContinue via ResumeContext.
  SetupContext(State::kWait);
  EXPECT_CALL(mock_, OnTraceContinue(BarrierId(1234))).WillOnce([&](auto...) {
    expect_active(kActiveSyncId);
  });
  EXPECT_TRUE(context_.ResumeContext(access(), BarrierId(1234), "Continue"));
  SyncContext::ResetThreadState(TestOnlyAccess());
  context_.Reset(TestOnlyAccess());

  // OnTraceEnterSync via AfterSwapCurrent on a nested context.
  StrictMock<MockTraceEventListener> nested;
  SetupActiveContext();
  SyncContext inner = CreateSync(&nested, State::kDefault, SyncId(99), kSyncId);
  EXPECT_CALL(mock_, OnTraceEnterSync(kSyncId, _)).WillOnce([&](auto...) {
    EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(&inner));
    EXPECT_THAT(active_sync_id(), Eq(kActiveSyncId));
  });
  inner.AfterSwapCurrent(access(), "Nested");
  inner.Reset(TestOnlyAccess());
}

// `SetActiveContext(nullptr)` pairs with `set_active_sync_id(kNoSyncId)`: both
// are cleared before any (nested) listener is released as part of ending or
// suspending the active execution.
TEST_F(SyncContextTest, ActiveContextIsClearedBeforeNestedListenerIsReleased) {
  StrictMock<MockTraceEventListener> nested;
  EXPECT_CALL(nested, ReleaseEventListener()).Times(3).WillRepeatedly([&]() {
    EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
    EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  });
  EXPECT_CALL(mock_, OnTraceEndSync(kActiveSyncId)).Times(2);
  EXPECT_CALL(mock_, OnTraceSuspendSync(kActiveSyncId));
  EXPECT_CALL(mock_, ReleaseEventListener()).Times(2);

  // EndContext.
  SetupActiveContext(&nested);
  EXPECT_TRUE(context_.EndContext(access()));
  EXPECT_TRUE(context_.empty());

  // BeforeRestoreCurrent to an empty (unrelated) context.
  SetupActiveContext(&nested);
  context_.BeforeRestoreCurrent(access(), {});
  EXPECT_THAT(context_.state(), Eq(State::kZombie));

  // BeforeSwapCurrent to an empty (unrelated) context.
  SetupActiveContext(&nested);
  context_.BeforeSwapCurrent(access(), {});
  EXPECT_THAT(context_.state(), Eq(State::kSuspended));
}

TEST_F(SyncContextTest, BeforeSwapEmptyContext) {
  context_.BeforeSwapCurrent(access(), {});
  EXPECT_TRUE(context_.empty());

  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, BeforeSwapZombieContext) {
  StrictMock<MockTraceEventListener> other_mock;
  EXPECT_CALL(other_mock, ReleaseEventListener());
  SyncContext other = CreateSync(&other_mock);
  SetupContext(State::kZombie);

  context_.BeforeSwapCurrent(access(), {});
  EXPECT_THAT(context_.state(), Eq(State::kZombie));
  EXPECT_THAT(context_.active_sync_id(), Eq(kActiveSyncId));
  EXPECT_THAT(context_.listener(), Eq(&mock_));
  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));

  context_.BeforeSwapCurrent(access(), other);
  EXPECT_THAT(context_.state(), Eq(State::kZombie));
  EXPECT_THAT(context_.active_sync_id(), Eq(kActiveSyncId));
  EXPECT_THAT(context_.listener(), Eq(&mock_));
  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, BeforeSwapActiveContext) {
  SetupActiveContext();

  EXPECT_CALL(mock_, OnTraceSuspendSync(kActiveSyncId));
  context_.BeforeSwapCurrent(access(), {});

  EXPECT_THAT(context_.state(), Eq(State::kSuspended));
  EXPECT_THAT(context_.active_sync_id(), Eq(kActiveSyncId));
  EXPECT_THAT(context_.listener(), Eq(&mock_));

  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, BeforeSwapActiveContextToSameTrace) {
  SetupActiveContext();
  EXPECT_CALL(mock_, GetEventListener(_)).WillOnce(Return(&mock_));
  SyncContext to = context_;

  context_.BeforeSwapCurrent(access(), to);

  EXPECT_THAT(context_.state(), Eq(State::kNested));
  EXPECT_THAT(context_.active_sync_id(), Eq(kActiveSyncId));
  EXPECT_THAT(context_.listener(), Eq(nullptr));

  EXPECT_THAT(active_sync_id(), Eq(kActiveSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(&mock_));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, BeforeSwapActiveContextToSuspendedSameTrace) {
  SyncContext to = CreateSync(&mock_, State::kSuspended, kActiveSyncId);
  EXPECT_CALL(mock_, GetEventListener(_)).WillOnce(Return(&mock_));
  context_ = to;
  EXPECT_CALL(mock_, OnTraceBeginSync(kFirstAutoSyncId, _));
  context_.AfterSwapCurrent(access(), "Begin");

  EXPECT_CALL(mock_, OnTraceSuspendSync(kFirstAutoSyncId));
  context_.BeforeSwapCurrent(access(), to);

  EXPECT_THAT(context_.state(), Eq(State::kSuspended));
  EXPECT_THAT(context_.active_sync_id(), Eq(kFirstAutoSyncId));
  EXPECT_THAT(context_.listener(), Eq(&mock_));

  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, BeforeSwapReleasesNestedListener) {
  StrictMock<MockTraceEventListener> nested;
  SetupActiveContext(&nested);

  EXPECT_CALL(mock_, OnTraceSuspendSync(kActiveSyncId));
  EXPECT_CALL(nested, ReleaseEventListener());
  context_.BeforeSwapCurrent(access(), {});

  EXPECT_THAT(context_.state(), Eq(State::kSuspended));
  EXPECT_THAT(context_.active_sync_id(), Eq(kActiveSyncId));
  EXPECT_THAT(context_.listener(), Eq(&mock_));

  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, BeforeSwapFailsWithoutActiveListener) {
  context_ = CreateSync(nullptr, State::kActive, kActiveSyncId);
  SyncContext::SetActiveContext(TestOnlyAccess(), &context_);
  context_.BeforeSwapCurrent(access(), {});

  EXPECT_THAT(context_.state(), Eq(State::kActive));
  EXPECT_THAT(context_.active_sync_id(), Eq(kActiveSyncId));
  EXPECT_THAT(context_.listener(), Eq(nullptr));

  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, BeforeSwapFailsWithInvalidState) {
  StrictMock<MockTraceEventListener> active_mock;
  internal::set_active_sync_id(kActiveSyncId);
  internal::set_active_event_listener(&active_mock);

  for (State state : {
           State::kDefault,
           State::kNested,
           State::kSuspended,
           State::kWait,
       }) {
    SCOPED_TRACE(state);
    SetupContext(state, kActiveSyncId);
    context_.BeforeSwapCurrent(access(), {});

    EXPECT_THAT(context_.state(), Eq(state));
    EXPECT_THAT(context_.active_sync_id(), Eq(kActiveSyncId));
    EXPECT_THAT(context_.listener(), Eq(&mock_));

    EXPECT_THAT(active_sync_id(), Eq(kActiveSyncId));
    EXPECT_THAT(internal::active_event_listener(), Eq(&active_mock));
  }

  internal::set_active_event_listener(nullptr);
}

TEST_F(SyncContextTest, BeforeRestoreEmptyContext) {
  context_.BeforeRestoreCurrent(access(), {});
  EXPECT_TRUE(context_.empty());

  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, BeforeRestoreZombieContext) {
  StrictMock<MockTraceEventListener> other_mock;
  EXPECT_CALL(other_mock, ReleaseEventListener());
  SyncContext other = CreateSync(&other_mock);
  SetupContext(State::kZombie);

  context_.BeforeRestoreCurrent(access(), {});
  EXPECT_THAT(context_.state(), Eq(State::kZombie));
  EXPECT_THAT(context_.active_sync_id(), Eq(kActiveSyncId));
  EXPECT_THAT(context_.listener(), Eq(&mock_));
  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));

  context_.BeforeRestoreCurrent(access(), other);
  EXPECT_THAT(context_.state(), Eq(State::kZombie));
  EXPECT_THAT(context_.active_sync_id(), Eq(kActiveSyncId));
  EXPECT_THAT(context_.listener(), Eq(&mock_));
  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, BeforeRestoreActiveContext) {
  SetupActiveContext();

  EXPECT_CALL(mock_, OnTraceEndSync(kActiveSyncId));
  context_.BeforeRestoreCurrent(access(), {});

  EXPECT_THAT(context_.state(), Eq(State::kZombie));
  EXPECT_THAT(context_.active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(context_.listener(), Eq(&mock_));

  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, BeforeRestoreActiveContextToSameTrace) {
  SetupActiveContext();
  EXPECT_CALL(mock_, GetEventListener(_)).WillOnce(Return(&mock_));
  SyncContext to = context_;

  context_.BeforeRestoreCurrent(access(), to);

  EXPECT_THAT(context_.state(), Eq(State::kZombie));
  EXPECT_THAT(context_.active_sync_id(), Eq(kActiveSyncId));
  EXPECT_THAT(context_.listener(), Eq(nullptr));

  EXPECT_THAT(active_sync_id(), Eq(kActiveSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(&mock_));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, BeforeRestoreActiveContextToSuspendedSameTrace) {
  SyncContext to = CreateSync(&mock_, State::kSuspended, kActiveSyncId);
  EXPECT_CALL(mock_, GetEventListener(_)).WillOnce(Return(&mock_));
  context_ = to;
  EXPECT_CALL(mock_, OnTraceBeginSync(kFirstAutoSyncId, _));
  context_.AfterSwapCurrent(access(), "Begin");

  EXPECT_CALL(mock_, OnTraceEndSync(kFirstAutoSyncId));
  context_.BeforeRestoreCurrent(access(), to);

  EXPECT_THAT(context_.state(), Eq(State::kZombie));
  EXPECT_THAT(context_.active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(context_.listener(), Eq(&mock_));

  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, BeforeRestoreReleasesNestedListener) {
  StrictMock<MockTraceEventListener> nested;
  SetupActiveContext(&nested);

  EXPECT_CALL(mock_, OnTraceEndSync(kActiveSyncId));
  EXPECT_CALL(nested, ReleaseEventListener());
  context_.BeforeRestoreCurrent(access(), {});

  EXPECT_THAT(context_.state(), Eq(State::kZombie));
  EXPECT_THAT(context_.active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(context_.listener(), Eq(&mock_));

  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, BeforeRestoreFailsWithoutActiveListener) {
  context_ = CreateSync(nullptr, State::kActive, kActiveSyncId);
  SyncContext::SetActiveContext(TestOnlyAccess(), &context_);
  context_.BeforeRestoreCurrent(access(), {});

  EXPECT_THAT(context_.state(), Eq(State::kActive));
  EXPECT_THAT(context_.active_sync_id(), Eq(kActiveSyncId));
  EXPECT_THAT(context_.listener(), Eq(nullptr));

  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, BeforeRestoreFailsWithInvalidState) {
  StrictMock<MockTraceEventListener> active_mock;
  internal::set_active_sync_id(kActiveSyncId);
  internal::set_active_event_listener(&active_mock);

  for (State state : {
           State::kDefault,
           State::kNested,
           State::kSuspended,
           State::kWait,
       }) {
    SCOPED_TRACE(state);
    SetupContext(state, kActiveSyncId);
    context_.BeforeRestoreCurrent(access(), {});

    EXPECT_THAT(context_.state(), Eq(state));
    EXPECT_THAT(context_.active_sync_id(), Eq(kActiveSyncId));
    EXPECT_THAT(context_.listener(), Eq(&mock_));

    EXPECT_THAT(active_sync_id(), Eq(kActiveSyncId));
    EXPECT_THAT(internal::active_event_listener(), Eq(&active_mock));
    EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
  }

  internal::set_active_event_listener(nullptr);
}

TEST_F(SyncContextTest, AfterSwapEmptyContext) {
  context_.AfterSwapCurrent(access(), "Empty");
  EXPECT_TRUE(context_.empty());

  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, AfterSwapDefaultContext) {
  SetupContext();

  EXPECT_CALL(mock_, OnTraceBeginSync(Eq(kSyncId), Eq("Begin")));
  context_.AfterSwapCurrent(access(), "Begin");

  EXPECT_THAT(context_.state(), Eq(State::kActive));
  EXPECT_THAT(context_.active_sync_id(), Eq(kSyncId));
  EXPECT_THAT(context_.listener(), Eq(nullptr));

  EXPECT_THAT(active_sync_id(), Eq(kSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(&mock_));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(&context_));
}

TEST_F(SyncContextTest, AfterSwapDefaultContextSetsActiveAncestorForBeginSync) {
  const Ancestor ancestor{.sync_id = SyncId{42},
                          .timestamp = absl::FromUnixSeconds(12345)};
  context_ = CreateSync(&mock_, State::kDefault, kNoSyncId, kSyncId, ancestor);

  EXPECT_CALL(mock_, OnTraceBeginSync(Eq(kSyncId), Eq("Begin")))
      .WillOnce([&](SyncId, StringRef) {
        ASSERT_THAT(SyncContext::ActiveContext(access()), Eq(&context_));
        EXPECT_THAT(SyncContext::ActiveContext(access())->ancestor(),
                    Eq(ancestor));
      });
  context_.AfterSwapCurrent(access(), "Begin");
}

TEST_F(SyncContextTest, AfterSwapSuspendedContext) {
  SetupContext(State::kSuspended);

  EXPECT_CALL(mock_, OnTraceResumeSync(kActiveSyncId));
  context_.AfterSwapCurrent(access(), "Resume");

  EXPECT_THAT(context_.state(), Eq(State::kActive));
  EXPECT_THAT(context_.active_sync_id(), Eq(kActiveSyncId));
  EXPECT_THAT(context_.listener(), Eq(nullptr));

  EXPECT_THAT(active_sync_id(), Eq(kActiveSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(&mock_));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(&context_));
}

TEST_F(SyncContextTest, AfterSwapNestedContext) {
  StrictMock<MockTraceEventListener> nested;
  for (State state : {State::kDefault, State::kNested}) {
    SetupActiveContext();
    SyncContext inner = CreateSync(&nested, state, SyncId(99), kSyncId);

    EXPECT_CALL(mock_, OnTraceEnterSync(kSyncId, Eq("Nested")));
    inner.AfterSwapCurrent(access(), "Nested");

    EXPECT_THAT(inner.state(), Eq(State::kActive));
    EXPECT_THAT(inner.active_sync_id(), Eq(kActiveSyncId));
    EXPECT_THAT(inner.listener(), Eq(&nested));

    EXPECT_THAT(active_sync_id(), Eq(kActiveSyncId));
    EXPECT_THAT(internal::active_event_listener(), Eq(&mock_));
    EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(&inner));
    inner.Reset(TestOnlyAccess());
    context_.Reset(TestOnlyAccess());
  }
}

TEST_F(SyncContextTest, AfterSwapFailsOnAbandonedTracer) {
  context_ = CreateSync(nullptr);
  context_.AfterSwapCurrent(access(), "Abandoned");
  EXPECT_TRUE(context_.empty());

  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, AfterSwapFailsWithInvalidState) {
  SetupContext(State::kNested);

  EXPECT_CALL(mock_, ReleaseEventListener());
  context_.AfterSwapCurrent(access(), "NestedWithoutActiveTLS");
  EXPECT_TRUE(context_.empty());

  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, AfterRestoreEmptyContext) {
  context_.AfterRestoreCurrent(access(), "Empty");
  EXPECT_TRUE(context_.empty());

  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, AfterRestoreZombieContext) {
  SetupContext(State::kZombie);

  EXPECT_CALL(mock_, ReleaseEventListener());
  context_.AfterRestoreCurrent(access(), "Zombie");
  EXPECT_TRUE(context_.empty());

  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, AfterRestoreDefaultContext) {
  SetupContext();

  EXPECT_CALL(mock_, OnTraceBeginSync(Eq(kSyncId), Eq("Begin")));
  context_.AfterRestoreCurrent(access(), "Begin");

  EXPECT_THAT(context_.state(), Eq(State::kActive));
  EXPECT_THAT(context_.active_sync_id(), Eq(kSyncId));
  EXPECT_THAT(context_.listener(), Eq(nullptr));

  EXPECT_THAT(active_sync_id(), Eq(kSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(&mock_));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(&context_));
}

TEST_F(SyncContextTest, AfterRestoreSuspendedContext) {
  SetupContext(State::kSuspended);

  EXPECT_CALL(mock_, OnTraceResumeSync(kActiveSyncId));
  context_.AfterRestoreCurrent(access(), "Resume");

  EXPECT_THAT(context_.state(), Eq(State::kActive));
  EXPECT_THAT(context_.active_sync_id(), Eq(kActiveSyncId));
  EXPECT_THAT(context_.listener(), Eq(nullptr));

  EXPECT_THAT(active_sync_id(), Eq(kActiveSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(&mock_));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(&context_));
}

TEST_F(SyncContextTest, AfterRestoreNestedContext) {
  StrictMock<MockTraceEventListener> nested;
  for (State state : {State::kDefault, State::kNested}) {
    SetupActiveContext();
    SyncContext inner = CreateSync(&nested, state, SyncId(99), kSyncId);

    EXPECT_CALL(mock_, OnTraceEnterSync(kSyncId, Eq("Nested")));
    inner.AfterRestoreCurrent(access(), "Nested");

    EXPECT_THAT(inner.state(), Eq(State::kActive));
    EXPECT_THAT(inner.active_sync_id(), Eq(kActiveSyncId));
    EXPECT_THAT(inner.listener(), Eq(&nested));

    EXPECT_THAT(active_sync_id(), Eq(kActiveSyncId));
    EXPECT_THAT(internal::active_event_listener(), Eq(&mock_));
    EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(&inner));
    inner.Reset(TestOnlyAccess());
    context_.Reset(TestOnlyAccess());
  }
}

TEST_F(SyncContextTest, AfterRestoreFailsOnAbandonedTracer) {
  context_ = CreateSync(nullptr);
  context_.AfterRestoreCurrent(access(), "Abandoned");
  EXPECT_TRUE(context_.empty());

  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, AfterRestoreFailsWithInvalidState) {
  SetupContext(State::kNested);

  EXPECT_CALL(mock_, ReleaseEventListener());
  context_.AfterRestoreCurrent(access(), "NestedWithoutActiveTLS");
  EXPECT_TRUE(context_.empty());

  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, ResumeEmptyContext) {
  EXPECT_FALSE(context_.ResumeContext(access(), kNoBarrierId, "Empty"));
  EXPECT_TRUE(context_.empty());

  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, ResumeDefaultContext) {
  SetupContext();

  EXPECT_CALL(mock_, OnTraceBeginSync(Eq(kSyncId), Eq("Begin")));
  EXPECT_TRUE(context_.ResumeContext(access(), kNoBarrierId, "Begin"));

  EXPECT_THAT(context_.state(), Eq(State::kActive));
  EXPECT_THAT(context_.active_sync_id(), Eq(kSyncId));
  EXPECT_THAT(context_.listener(), Eq(nullptr));

  EXPECT_THAT(active_sync_id(), Eq(kSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(&mock_));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(&context_));
}

TEST_F(SyncContextTest, ResumeDefaultContextSetsActiveAncestorForBeginSync) {
  const Ancestor ancestor{.sync_id = SyncId{42},
                          .timestamp = absl::FromUnixSeconds(12345)};
  context_ = CreateSync(&mock_, State::kDefault, kNoSyncId, kSyncId, ancestor);

  EXPECT_CALL(mock_, OnTraceBeginSync(Eq(kSyncId), Eq("Begin")))
      .WillOnce([&](SyncId, StringRef) {
        ASSERT_THAT(SyncContext::ActiveContext(access()), Eq(&context_));
        EXPECT_THAT(SyncContext::ActiveContext(access())->ancestor(),
                    Eq(ancestor));
      });
  EXPECT_TRUE(context_.ResumeContext(access(), kNoBarrierId, "Begin"));
}

TEST_F(SyncContextTest, ResumeDefaultContextWithBarrier) {
  InSequence in_sequence;
  SetupContext();

  EXPECT_CALL(mock_, OnTraceBeginSync(Eq(kSyncId), Eq("Begin")));
  EXPECT_CALL(mock_, OnTraceObserved(Eq(BarrierId(1234)), Eq("Begin")));
  EXPECT_TRUE(context_.ResumeContext(access(), BarrierId(1234), "Begin"));

  EXPECT_THAT(context_.state(), Eq(State::kActive));
  EXPECT_THAT(context_.active_sync_id(), Eq(kSyncId));
  EXPECT_THAT(context_.listener(), Eq(nullptr));

  EXPECT_THAT(active_sync_id(), Eq(kSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(&mock_));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(&context_));
}

TEST_F(SyncContextTest, ResumeSuspendedContext) {
  SetupContext(State::kSuspended);

  EXPECT_CALL(mock_, OnTraceResumeSync(kActiveSyncId));
  EXPECT_TRUE(context_.ResumeContext(access(), BarrierId(1234), "Resume"));

  EXPECT_THAT(context_.state(), Eq(State::kActive));
  EXPECT_THAT(context_.active_sync_id(), Eq(kActiveSyncId));
  EXPECT_THAT(context_.listener(), Eq(nullptr));

  EXPECT_THAT(active_sync_id(), Eq(kActiveSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(&mock_));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(&context_));
}

TEST_F(SyncContextTest, ResumeWaitSuspendedContext) {
  SetupContext(State::kWait);

  EXPECT_CALL(mock_, OnTraceContinue(Eq(BarrierId(1234))));
  EXPECT_TRUE(context_.ResumeContext(access(), BarrierId(1234), "Resume"));

  EXPECT_THAT(context_.state(), Eq(State::kActive));
  EXPECT_THAT(context_.active_sync_id(), Eq(kActiveSyncId));
  EXPECT_THAT(context_.listener(), Eq(nullptr));

  EXPECT_THAT(active_sync_id(), Eq(kActiveSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(&mock_));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(&context_));
}

TEST_F(SyncContextTest, ResumeFailsOnAbandonedTracer) {
  context_ = CreateSync(nullptr);

  EXPECT_FALSE(context_.ResumeContext(access(), kNoBarrierId, "Begin"));
  EXPECT_TRUE(context_.empty());
}

TEST_F(SyncContextTest, ResumeFailsWithActiveListener) {
  StrictMock<MockTraceEventListener> other_mock;
  SetupActiveContext();

  SyncContext suspended = CreateSync(&other_mock);
  EXPECT_CALL(other_mock, ReleaseEventListener());
  EXPECT_FALSE(suspended.ResumeContext(access(), kNoBarrierId, "Begin"));
  EXPECT_TRUE(suspended.empty());

  EXPECT_THAT(active_sync_id(), Eq(kActiveSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(&mock_));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(&context_));
}

TEST_F(SyncContextTest, ResumeFailsWithInvalidState) {
  for (State state : {
           State::kNested,
           State::kActive,
           State::kZombie,
       }) {
    SCOPED_TRACE(state);
    SetupContext(state);

    EXPECT_CALL(mock_, ReleaseEventListener());
    ASSERT_FALSE(context_.ResumeContext(access(), kNoBarrierId, "Begin"));
    EXPECT_TRUE(context_.empty());

    EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
    EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
    EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
  }
}

TEST_F(SyncContextTest, SuspendEmptyContext) {
  EXPECT_FALSE(context_.SuspendContext(access(), {}, "Empty"));
  EXPECT_TRUE(context_.empty());

  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, SuspendActiveContextAsWaiting) {
  SetupActiveContext();
  EXPECT_CALL(mock_, GetEventListener(_)).WillOnce(Return(&mock_));
  SyncContext to = context_;

  EXPECT_CALL(mock_, OnTraceWait(Eq(kNoBarrierId), Eq("Wait")));
  EXPECT_TRUE(context_.SuspendContext(access(), to, "Wait"));

  EXPECT_THAT(context_.state(), Eq(State::kWait));
  EXPECT_THAT(context_.active_sync_id(), Eq(kActiveSyncId));
  EXPECT_THAT(context_.listener(), Eq(&mock_));

  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, SuspendActiveContextAsSuspended) {
  SyncContext background;
  SetupActiveContext();

  EXPECT_CALL(mock_, OnTraceSuspendSync(kActiveSyncId));
  EXPECT_TRUE(context_.SuspendContext(access(), background, "Unused"));

  EXPECT_THAT(context_.state(), Eq(State::kSuspended));
  EXPECT_THAT(context_.active_sync_id(), Eq(kActiveSyncId));
  EXPECT_THAT(context_.listener(), Eq(&mock_));

  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, SuspendContextReleasesNestedListener) {
  // A nested context typically still owns a listener as it was swapped
  // in while retaining the outermost listener.
  StrictMock<MockTraceEventListener> nested;
  SetupActiveContext(&nested);

  EXPECT_CALL(mock_, OnTraceSuspendSync(_));
  EXPECT_CALL(nested, ReleaseEventListener());
  EXPECT_TRUE(context_.SuspendContext(access(), {}, ""));

  EXPECT_THAT(context_.state(), Eq(State::kSuspended));
  EXPECT_THAT(context_.active_sync_id(), Eq(kActiveSyncId));
  EXPECT_THAT(context_.listener(), Eq(&mock_));

  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, SuspendFailsWithoutActiveListener) {
  context_ = CreateSync(nullptr, State::kActive, kActiveSyncId);
  SyncContext::SetActiveContext(TestOnlyAccess(), &context_);

  EXPECT_FALSE(context_.SuspendContext(access(), {}, ""));
  EXPECT_TRUE(context_.empty());
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, SuspendFailsWithInvalidState) {
  StrictMock<MockTraceEventListener> active_mock;
  internal::set_active_sync_id(kActiveSyncId);
  internal::set_active_event_listener(&active_mock);

  for (State state : {
           State::kDefault,
           State::kNested,
           State::kSuspended,
           State::kWait,
           State::kZombie,
       }) {
    SCOPED_TRACE(state);
    SetupContext(state);

    EXPECT_CALL(mock_, ReleaseEventListener());
    EXPECT_FALSE(context_.SuspendContext(access(), {}, ""));
    EXPECT_TRUE(context_.empty());

    EXPECT_THAT(active_sync_id(), Eq(kActiveSyncId));
    EXPECT_THAT(internal::active_event_listener(), Eq(&active_mock));
    EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
  }

  internal::set_active_event_listener(nullptr);
}

TEST_F(SyncContextTest, EndEmptyContext) {
  EXPECT_FALSE(context_.EndContext(access()));
  EXPECT_TRUE(context_.empty());

  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, EndContext) {
  SetupActiveContext();

  EXPECT_CALL(mock_, OnTraceEndSync(kActiveSyncId));
  EXPECT_CALL(mock_, ReleaseEventListener());
  EXPECT_TRUE(context_.EndContext(access()));
  EXPECT_TRUE(context_.empty());

  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, EndContextReleasesNestedListener) {
  StrictMock<MockTraceEventListener> nested;
  SetupActiveContext(&nested);

  EXPECT_CALL(mock_, OnTraceEndSync(kActiveSyncId));
  EXPECT_CALL(mock_, ReleaseEventListener());
  EXPECT_CALL(nested, ReleaseEventListener());
  EXPECT_TRUE(context_.EndContext(access()));
  EXPECT_TRUE(context_.empty());

  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, EndFailsWithoutActiveListener) {
  context_ = CreateSync(nullptr, State::kActive, kActiveSyncId);
  SyncContext::SetActiveContext(TestOnlyAccess(), &context_);

  EXPECT_FALSE(context_.EndContext(access()));
  EXPECT_TRUE(context_.empty());
  EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
}

TEST_F(SyncContextTest, EndFailsWithInvalidState) {
  StrictMock<MockTraceEventListener> active_mock;
  internal::set_active_sync_id(kActiveSyncId);
  internal::set_active_event_listener(&active_mock);

  for (State state : {
           State::kDefault,
           State::kNested,
           State::kSuspended,
           State::kWait,
           State::kZombie,
       }) {
    SCOPED_TRACE(state);
    SetupContext(state);

    EXPECT_CALL(mock_, ReleaseEventListener());
    EXPECT_FALSE(context_.EndContext(access()));
    EXPECT_TRUE(context_.empty());

    EXPECT_THAT(active_sync_id(), Eq(kActiveSyncId));
    EXPECT_THAT(internal::active_event_listener(), Eq(&active_mock));
    EXPECT_THAT(SyncContext::ActiveContext(access()), Eq(nullptr));
  }

  internal::set_active_event_listener(nullptr);
}

}  // namespace
}  // namespace perftools::tracing::core
