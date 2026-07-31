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

// TODO: add fuzz testing
#include "gloop/perftools/tracing/sync_context.h"

#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "gloop/base/context.h"
#include "gloop/base/mock_tracer.h"
#include "gloop/base/tracecontext.h"
#include "gloop/perftools/tracing/mock_trace_event_listener.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/trace_source_location.h"
#include "gloop/perftools/tracing/tracing_base.h"
#include "gloop/perftools/tracing/tracing_core.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace perftools::tracing::core {

using ::testing::_;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;

class SyncContextTestPeer {
 public:
  static SyncContext::Access access() { return SyncContext::Access(); }
};

namespace {

// RawThread runs some invocable on a raw thread, i.e., not copying or
// initializing any per-thread context and other gloop specific goodies.
class RawThread final : public Thread {
 public:
  template <typename Fn>
  explicit RawThread(Fn&& fn, absl::string_view name = "RawThread")
      : Thread(thread::Options().set_joinable(true), name),
        invocable_(std::forward<Fn>(fn)) {
    Start();
  }

 private:
  void Run() final { std::move(invocable_)(); }

  absl::AnyInvocable<void() &&> invocable_;
};

SyncContext::Access access() { return SyncContextTestPeer::access(); }

// Whitebox: SyncContext will assign odd numbers for 'out of thin air' contexts
// created through copies, and even numbers for contexts intended for explicit
// scheduling through 'CreateThread'.
constexpr SyncId kFirstThreadSyncId{2};
constexpr SyncId kFirstAutoSyncId{3};
constexpr SyncId kSecondAutoSyncId{5};

class EmptyEventListener final : public TraceEventListener {
 public:
  TraceEventListener* GetEventListener(SyncId sync_id) override { return this; }
  void ReleaseEventListener() override {}
};

// Context is a helper class that wraps a `SyncContext` mimicking more closely
// the actual implementation in `TraceContext` and simplifying unit tests.
class Context {
 public:
  // Context has default value semantics (default construct, copy, move)
  Context() = default;
  Context(Context&& rhs) = default;
  Context(const Context& rhs) = default;
  Context& operator=(Context&& rhs) = default;
  Context& operator=(const Context& rhs) = default;

  // Creates a context with the provided listener automatically added.
  explicit Context(TraceEventListener* tel) { AddListener(tel); }

  // Returns a context that is a 'spawned' child of the current thread.
  static Context CreateThread(StringRef label = {}) {
    Context context;
    context.sync_ = thread_context_.sync_.CreateThread(label);
    return context;
  }

  // Returns the sync_id of the current instance.
  SyncId sync_id() const { return sync_.sync_id(); }

  // Returns the current thread context.
  static const Context& Current() { return thread_context_; }

  // Swaps `context` with the current thread context.
  static void Swap(Context& context, StringRef label = {}) {
    thread_context_.SwapTo(context, label);
  }

  // Restores the current thread context from `context`, resetting `context`.
  static void Restore(Context& context) {
    thread_context_.RestoreFrom(context);
  }

  // Called by the test fixture: checks no thread state was leaked,
  // and resets any possibly leaked thread state.
  static void Cleanup() {
    EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
    EXPECT_THAT(active_trace_span_id(), Eq(0));
    EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
    EXPECT_FALSE(thread_context_.has_listeners());

    internal::set_active_sync_id(kNoSyncId);
    internal::set_active_trace_span_id(0);
    internal::set_active_event_listener(nullptr);
    thread_context_ = Context{};
  }

  // Functions directly routing to the wrapped sync context
  void AddListener(TraceEventListener* tel) { sync_.AddListener(tel); }
  void RemoveListener(TraceEventListener* tel) { sync_.RemoveListener(tel); }
  static void AddListenerToCurrent(TraceEventListener* tel) {
    auto& tc = thread_context_;
    tc.sync_.AddListenerToCurrent(access(), tc.span_id_, tel);
  }
  static void AddListenerToCurrent(TraceEventListener* tel, StringLabel label) {
    auto& tc = thread_context_;
    tc.sync_.AddListenerToCurrent(access(), tc.span_id_, tel, label);
  }
  static void RemoveListenerFromCurrent(TraceEventListener* tel) {
    thread_context_.sync_.RemoveListenerFromCurrent(access(), tel);
  }
  bool has_listeners() const { return sync_.has_listeners(); }

  SyncContext& sync_context() { return sync_; }
  const SyncContext& sync_context() const { return sync_; }

 private:
  void SwapTo(Context& context, StringRef label = {}) {
    sync_.BeforeSwapCurrent(access(), context.sync_);
    using std::swap;
    swap(*this, context);
    sync_.AfterSwapCurrent(access(), span_id_, label);
  }

  void RestoreFrom(Context& context) {
    sync_.BeforeRestoreCurrent(access(), context.sync_);
    using std::swap;
    swap(*this, context);
    sync_.AfterRestoreCurrent(access(), span_id_);
  }

  SyncContext sync_;
  int span_id_ = 0;
  static thread_local Context thread_context_;
};

thread_local Context Context::thread_context_;  // NOLINT

// `WithContext` is the equivalent of `base::WithTraceContext`
class WithContext {
 public:
  explicit WithContext(Context&& context, StringRef label = {})
      : context_(std::move(context)) {
    Context::Swap(context_, label);
  }

  explicit WithContext(const Context& context, StringRef label = {})
      : context_(context) {
    Context::Swap(context_, label);
  }

  explicit WithContext(TraceEventListener* listener, StringRef label = {}) {
    context_.AddListener(listener);
    Context::Swap(context_, label);
  }

  ~WithContext() { Context::Restore(context_); }

 private:
  Context context_;
};

class SyncContextTest : public ::testing::Test {
 public:
  ~SyncContextTest() override { Context::Cleanup(); }
};

TEST(SyncContextValueSemantics, DefaultCtor) {
  SyncContext context;
  EXPECT_THAT(context.sync_id(), Eq(kNoSyncId));
  EXPECT_FALSE(context.has_listeners());
  EXPECT_THAT(context.listener(), Eq(nullptr));
  EXPECT_FALSE(context.same_trace(context));
}

TEST(SyncContextValueSemantics, OperationsOnDefaultInstanceAreNops) {
  MockTraceEventListener mock;
  SyncContext context, other;
  EXPECT_FALSE(context.CreateThread("Thread").has_listeners());
  context.BeforeSwapCurrent(access(), other);
  context.AfterSwapCurrent(access(), /*span_id=*/1, "Swap");
  context.BeforeRestoreCurrent(access(), other);
  context.AfterRestoreCurrent(access(), /*span_id=*/1, "Restore");
  context.AddListener(nullptr);
  EXPECT_FALSE(other.ContainsListener(nullptr));
  EXPECT_FALSE(other.ContainsListener(&mock));

  using std::swap;
  swap(context, other);
  EXPECT_FALSE(context.has_listeners());
  EXPECT_FALSE(other.has_listeners());
}

TEST(SyncContextValueSemantics, CopyConstruct) {
  SyncContext empty1;
  SyncContext empty2(empty1);
  EXPECT_FALSE(empty1.has_listeners());
  EXPECT_FALSE(empty2.has_listeners());

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
  EXPECT_FALSE(empty1.has_listeners());
  EXPECT_FALSE(empty2.has_listeners());

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
  EXPECT_FALSE(context.has_listeners());
}

TEST(SyncContextValueSemantics, MoveConstruct) {
  SyncContext empty1;
  SyncContext empty2(std::move(empty1));
  EXPECT_FALSE(empty1.has_listeners());  // NOLINT
  EXPECT_FALSE(empty2.has_listeners());

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
  EXPECT_FALSE(empty1.has_listeners());  // NOLINT
  EXPECT_FALSE(empty2.has_listeners());

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
  StrictMock<MockTraceEventListener> mock;
  SyncContext root;
  root.AddListener(&mock);
  EXPECT_TRUE(root.has_listeners());
  EXPECT_CALL(mock, ReleaseEventListener());
}

TEST_F(SyncContextTest, AddNullptrListener) {
  SyncContext root;
  root.AddListener(nullptr);
  EXPECT_FALSE(root.has_listeners());

  WithContext with(Context{});
  Context::AddListenerToCurrent(nullptr);
  EXPECT_FALSE(Context::Current().has_listeners());
}

TEST_F(SyncContextTest, ListenersAreReleaseInLifoOrder) {
  StrictMock<MockTraceEventListener> mock1, mock2;
  InSequence in_sequence;
  SyncContext root;
  root.AddListener(&mock1);
  root.AddListener(&mock2);
  EXPECT_TRUE(root.has_listeners());
  EXPECT_CALL(mock2, ReleaseEventListener());
  EXPECT_CALL(mock1, ReleaseEventListener());
}

TEST_F(SyncContextTest, ListenersOnCurrentAreEndedInLifoOrder) {
  StrictMock<MockTraceEventListener> mock1, mock2;
  InSequence sequence;
  WithContext with(Context{});

  EXPECT_CALL(mock1, OnTraceBeginSync(kMainSyncId, Eq("Main1")));
  Context::AddListenerToCurrent(&mock1, "Main1");

  EXPECT_CALL(mock2, OnTraceBeginSync(kMainSyncId, Eq("Main2")));
  Context::AddListenerToCurrent(&mock2, "Main2");

  EXPECT_CALL(mock2, OnTraceEndSync(kMainSyncId));
  EXPECT_CALL(mock1, OnTraceEndSync(kMainSyncId));
}

// Matches that `arg` contains a non empty source location.
MATCHER(IsSourceLocation, "Holds a source location") {
  return arg.IsSourceLocation();
}

TEST_F(SyncContextTest, AddListenersToCurrentDefaultsToSourceLocation) {
  NiceMock<MockTraceEventListener> mock1, mock2;
  WithContext with(Context{});

  EXPECT_CALL(mock1, OnTraceBeginSync(kMainSyncId, IsSourceLocation()));
  Context::AddListenerToCurrent(&mock1);

  EXPECT_CALL(mock2, OnTraceBeginSync(kMainSyncId, IsSourceLocation()));
  Context::AddListenerToCurrent(&mock2);
}

TEST_F(SyncContextTest, AddListenersToCurrentNoCurrent) {
  NiceMock<MockTraceEventListener> mock1, mock2;
  SyncContext root;

  // The internal code has no way to verify the state of
  // something that is 'empty' / a nullptr pimpl.
  EXPECT_CALL(mock1, OnTraceBeginSync(kMainSyncId, _));
  root.AddListenerToCurrent(access(), /*span_id=*/0, &mock1);

  EXPECT_CALL(mock2, OnTraceBeginSync(kMainSyncId, _));
  root.AddListenerToCurrent(access(), /*span_id=*/0, &mock2);

  EXPECT_CALL(mock2, ReleaseEventListener());
  EXPECT_CALL(mock1, ReleaseEventListener());

  root.BeforeRestoreCurrent(access(), {});

  // Cleanup state
  internal::set_active_sync_id(kNoSyncId);
  internal::set_active_event_listener(nullptr);
}

TEST_F(SyncContextTest, ContainsListener) {
  StrictMock<MockTraceEventListener> mock1, mock2;
  SyncContext root;
  root.AddListener(&mock1);

  // Confirm behavior for `nullptr`. SyncContext always forwards.
  EXPECT_CALL(mock1, Contains(nullptr)).WillOnce(Return(false));
  EXPECT_FALSE(root.ContainsListener(nullptr));

  // Confirm behavior for `matching` on non empty SyncContext
  EXPECT_CALL(mock1, Contains(&mock1)).WillOnce(Return(true));
  EXPECT_TRUE(root.ContainsListener(&mock1));

  // Confirm behavior for `non matching` on non empty SyncContext
  EXPECT_CALL(mock1, Contains(&mock2)).WillOnce(Return(false));
  EXPECT_FALSE(root.ContainsListener(&mock2));

  EXPECT_CALL(mock1, ReleaseEventListener());
}

TEST_F(SyncContextTest, ContainsListenerOnEmptyContext) {
  StrictMock<MockTraceEventListener> mock;

  // Calls on an empty handle should never hit crash or anything,
  // and accept nullptr values directly returning false.
  SyncContext root;
  EXPECT_FALSE(root.ContainsListener(nullptr));
  EXPECT_FALSE(root.ContainsListener(&mock));
}

TEST_F(SyncContextTest, RemoveListener) {
  InSequence in_sequence;
  StrictMock<MockTraceEventListener> mock;
  SyncContext root;
  root.AddListener(&mock);
  EXPECT_CALL(mock, Extract(&mock));
  EXPECT_CALL(mock, ReleaseEventListener());
  root.RemoveListener(&mock);
  EXPECT_FALSE(root.has_listeners());
}

TEST_F(SyncContextTest, RemoveNullptrListener) {
  Context root;
  root.RemoveListener(nullptr);
  EXPECT_FALSE(root.has_listeners());

  Context::RemoveListenerFromCurrent(nullptr);
  EXPECT_FALSE(Context::Current().has_listeners());
}

TEST_F(SyncContextTest, RemoveListeners) {
  InSequence in_sequence;
  NiceMock<MockTraceEventListener> mock1, mock2;
  SyncContext root;
  root.AddListener(&mock1);
  root.AddListener(&mock2);

  EXPECT_CALL(mock2, Extract(&mock2));
  EXPECT_CALL(mock2, ReleaseEventListener());
  root.RemoveListener(&mock2);

  EXPECT_CALL(mock1, Extract(&mock1));
  EXPECT_CALL(mock1, ReleaseEventListener());
  root.RemoveListener(&mock1);
}

TEST_F(SyncContextTest, RemoveListenersFromCurrent) {
  NiceMock<MockTraceEventListener> mock1, mock2;
  Context root;
  root.AddListener(&mock1);
  root.AddListener(&mock2);
  EXPECT_CALL(mock1, OnTraceBeginSync(kMainSyncId, _));
  EXPECT_CALL(mock2, OnTraceBeginSync(kMainSyncId, _));
  WithContext with(std::move(root));

  EXPECT_CALL(mock2, OnTraceEndSync(kMainSyncId));
  EXPECT_CALL(mock2, Extract(&mock2));
  EXPECT_CALL(mock2, ReleaseEventListener());
  Context::RemoveListenerFromCurrent(&mock2);

  EXPECT_CALL(mock1, OnTraceEndSync(kMainSyncId));
  EXPECT_CALL(mock1, Extract(&mock1));
  EXPECT_CALL(mock1, ReleaseEventListener());
  Context::RemoveListenerFromCurrent(&mock1);
}

TEST_F(SyncContextTest, CornerCaseAddDuplicateListener) {
  StrictMock<MockTraceEventListener> mock;
  InSequence in_sequence;
  SyncContext root;
  root.AddListener(&mock);
  root.AddListener(&mock);

  EXPECT_CALL(mock, Extract(&mock));
  EXPECT_CALL(mock, ReleaseEventListener());
  root.RemoveListener(&mock);
  EXPECT_TRUE(root.has_listeners());

  EXPECT_CALL(mock, Extract(&mock));
  EXPECT_CALL(mock, ReleaseEventListener());
  root.RemoveListener(&mock);
  EXPECT_FALSE(root.has_listeners());
}

TEST_F(SyncContextTest, CreateThread) {
  NiceMock<MockTraceEventListener> mock1, mock2;
  WithContext with(&mock1);

  SyncId sync_id = kFirstThreadSyncId;
  EXPECT_CALL(mock1, OnTraceSpawn(sync_id, Eq("Child")));
  EXPECT_CALL(mock1, GetEventListener(sync_id)).WillOnce(Return(&mock2));
  Context context = Context::CreateThread("Child");
  EXPECT_TRUE(context.has_listeners());
  EXPECT_THAT(context.sync_id(), Eq(sync_id));
}

TEST_F(SyncContextTest, SameTrace) {
  StrictMock<MockTraceEventListener> mock;
  EXPECT_CALL(mock, GetEventListener(_)).WillRepeatedly(Return(&mock));
  SyncContext context1;
  context1.AddListener(&mock);
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
  SyncContext other;
  other.AddListener(&mock);
  EXPECT_FALSE(context1.same_trace(other));
  EXPECT_FALSE(context1.same_trace(empty));
  EXPECT_FALSE(empty.same_trace(empty));
}

TEST_F(SyncContextTest, CommonSwapRestore) {
  StrictMock<MockTraceEventListener> mock;
  InSequence in_sequence;

  EXPECT_CALL(mock, OnTraceBeginSync(kMainSyncId, Eq("Main")));
  WithContext with(&mock, "Main");
  EXPECT_THAT(tracing::active_sync_id(), Eq(kMainSyncId));

  // Verify per thread listener is installed
  EXPECT_CALL(mock, OnTraceMark(Eq("PerThreadListener"), _));
  TraceMark("PerThreadListener", TraceSourceLocation::current());

  EXPECT_CALL(mock, OnTraceEndSync(kMainSyncId));
}

TEST_F(SyncContextTest, SwapRestoreEmpty) {
  WithContext with(Context{}, "Main");
}

TEST_F(SyncContextTest, SwapContextSuspendResumeContext) {
  StrictMock<MockTraceEventListener> mock;
  InSequence in_sequence;

  EXPECT_CALL(mock, OnTraceBeginSync(kMainSyncId, Eq("Main")));
  WithContext with(&mock, "Main");
  {
    EXPECT_CALL(mock, OnTraceSuspendSync(kMainSyncId));
    WithContext with(Context{});
    EXPECT_THAT(tracing::active_sync_id(), Eq(kNoSyncId));

    // Verify per thread listener is uninstalled
    TraceMark("NoPerThreadListener", TraceSourceLocation::current());

    EXPECT_CALL(mock, OnTraceResumeSync(kMainSyncId));
  }
  EXPECT_THAT(tracing::active_sync_id(), Eq(kMainSyncId));

  // Verify per thread listener is reinstalled
  EXPECT_CALL(mock, OnTraceMark(Eq("PerThreadListener"), _));
  TraceMark("PerThreadListener", TraceSourceLocation::current());

  EXPECT_CALL(mock, OnTraceEndSync(kMainSyncId));
}

TEST_F(SyncContextTest, SwapContextSuspendResumeWithSwap) {
  StrictMock<MockTraceEventListener> mock;
  InSequence in_sequence;

  EXPECT_CALL(mock, OnTraceBeginSync(kMainSyncId, Eq("Main")));
  WithContext with(&mock, "Main");
  {
    EXPECT_CALL(mock, OnTraceSuspendSync(kMainSyncId));
    Context empty;
    Context::Swap(empty);
    EXPECT_THAT(tracing::active_sync_id(), Eq(kNoSyncId));

    // Verify per thread listener is uninstalled
    TraceMark("NoPerThreadListener", TraceSourceLocation::current());

    EXPECT_CALL(mock, OnTraceResumeSync(kMainSyncId));
    Context::Swap(empty);
  }
  EXPECT_THAT(tracing::active_sync_id(), Eq(kMainSyncId));

  // Verify per thread listener is reinstalled
  EXPECT_CALL(mock, OnTraceMark(Eq("PerThreadListener"), _));
  TraceMark("PerThreadListener", TraceSourceLocation::current());

  EXPECT_CALL(mock, OnTraceEndSync(kMainSyncId));
}

TEST_F(SyncContextTest, CommonThreadSpawnIntoThread) {
  StrictMock<MockTraceEventListener> mock, child_mock;
  InSequence in_sequence;

  EXPECT_CALL(mock, OnTraceBeginSync(kMainSyncId, Eq("Main")));
  WithContext with(&mock, "Main");

  SyncId sync_id = kFirstThreadSyncId;
  EXPECT_CALL(mock, OnTraceSpawn(sync_id, Eq("Child")));
  EXPECT_CALL(mock, GetEventListener(sync_id)).WillOnce(Return(&child_mock));
  Context child = Context::CreateThread("Child");

  RawThread thread([&, child = std::move(child)]() mutable {
    EXPECT_CALL(child_mock, OnTraceBeginSync(kFirstThreadSyncId, Eq("Thread")));
    WithContext with(std::move(child), "Thread");
    EXPECT_CALL(child_mock, OnTraceEndSync(kFirstThreadSyncId));
  });
  thread.Join();

  EXPECT_CALL(mock, OnTraceEndSync(kMainSyncId));
}

TEST_F(SyncContextTest, CopiedContextIntoThread) {
  StrictMock<MockTraceEventListener> mock, child_mock;
  InSequence in_sequence;

  EXPECT_CALL(mock, OnTraceBeginSync(kMainSyncId, Eq("Main")));
  WithContext with(&mock, "Main");

  SyncId sync_id = kFirstAutoSyncId;
  EXPECT_CALL(mock, GetEventListener(sync_id)).WillOnce(Return(&child_mock));
  Context child = Context::Current();

  RawThread thread([&, child = std::move(child)]() mutable {
    EXPECT_CALL(child_mock, OnTraceBeginSync(sync_id, Eq("Thread")));
    WithContext with(std::move(child), "Thread");
    EXPECT_CALL(child_mock, OnTraceEndSync(sync_id));
  });
  thread.Join();

  EXPECT_CALL(mock, OnTraceEndSync(kMainSyncId));
}

TEST_F(SyncContextTest, Nested) {
  StrictMock<MockTraceEventListener> mock, nested_mock1, nested_mock2;
  InSequence in_sequence;

  EXPECT_CALL(mock, OnTraceBeginSync(kMainSyncId, Eq("Main")));
  WithContext with(&mock, "Main");

  SyncId sync_id = kFirstAutoSyncId;
  EXPECT_CALL(mock, GetEventListener(sync_id)).WillOnce(Return(&nested_mock1));
  Context nested = Context::Current();

  // `nested_mock2` is ignored as this context is nested.
  nested.AddListener(&nested_mock2);

  {
    // Swapping to `nested' should not affect the active_sync id or listener.
    // We do expect a call to EnterSync() for the nested scoped
    EXPECT_CALL(mock, OnTraceEnterSync(nested.sync_id(), Eq("Nested")));
    WithContext with(std::move(nested), "Nested");
    EXPECT_THAT(active_sync_id(), Eq(kMainSyncId));

    // Verify that mock is still in place.
    EXPECT_CALL(mock, OnTraceMark(Eq("Mark"), _));
    core::TraceMark("Mark", TraceSourceLocation::current());

    // Expect 'EnterSync' for switching back to the main scope
    EXPECT_CALL(mock, OnTraceEnterSync(kMainSyncId, _));

    EXPECT_CALL(nested_mock2, ReleaseEventListener());
    EXPECT_CALL(nested_mock1, ReleaseEventListener());
  }

  EXPECT_CALL(mock, OnTraceEndSync(kMainSyncId));
}

TEST_F(SyncContextTest, BeforeSwapRestoreDeathTest) {
  StrictMock<MockTraceEventListener> mock;
  SyncContext root;
  root.AddListener(&mock);
  EXPECT_DEBUG_DEATH(root.BeforeSwapCurrent(access(), {}), ".*");
  EXPECT_DEBUG_DEATH(root.BeforeRestoreCurrent(access(), {}), ".*");
}

TEST_F(SyncContextTest, SwapSuspendedContextWithDifferentSyncId) {
  StrictMock<MockTraceEventListener> mock1, mock2, mock3;
  Context root(&mock1);

  // Create a suspended context
  SyncId sync_id2 = kFirstAutoSyncId;
  EXPECT_CALL(mock1, GetEventListener(sync_id2)).WillOnce(Return(&mock2));
  Context suspended = root;
  EXPECT_CALL(mock2, OnTraceBeginSync(sync_id2, Eq("Thread2")));
  Context::Swap(suspended, "Thread2");
  EXPECT_CALL(mock2, OnTraceSuspendSync(sync_id2));
  Context::Swap(suspended, "Thread2");

  // Create a new active context in the same trace
  SyncId sync_id3 = kSecondAutoSyncId;
  EXPECT_CALL(mock1, GetEventListener(sync_id3)).WillOnce(Return(&mock3));
  Context context = root;
  EXPECT_CALL(mock3, OnTraceBeginSync(sync_id3, Eq("Thread3")));
  Context::Swap(context, "Thread3");

  // End context by restoring `suspended` in the thread.
  EXPECT_CALL(mock3, OnTraceEndSync(sync_id3));
  EXPECT_CALL(mock3, ReleaseEventListener());
  EXPECT_CALL(mock2, OnTraceResumeSync(sync_id2));
  Context::Restore(suspended);

  // End `suspended` by restore `context` in it (which should be empty)
  EXPECT_CALL(mock2, OnTraceEndSync(sync_id2));
  EXPECT_CALL(mock2, ReleaseEventListener());
  Context::Restore(context);
}

TEST_F(SyncContextTest, MoveSuspendedContextAcrossThreadsLikeK3) {
  StrictMock<MockTraceEventListener> mock;
  Context context(&mock);

  // Begin and Suspend
  EXPECT_CALL(mock, OnTraceBeginSync(kMainSyncId, Eq("Main")));
  Context::Swap(context, "Main");
  EXPECT_CALL(mock, OnTraceSuspendSync(kMainSyncId));
  Context::Swap(context);

  //  'restore' into a thread
  RawThread thread([&mock, context = std::move(context)]() mutable {
    EXPECT_CALL(mock, OnTraceResumeSync(kMainSyncId));
    Context::Swap(context);
    EXPECT_CALL(mock, OnTraceEndSync(kMainSyncId));
    EXPECT_CALL(mock, ReleaseEventListener());
    Context::Restore(context);
  });
  thread.Join();
}

TEST_F(SyncContextTest, SuspendNestedContext) {
  StrictMock<MockTraceEventListener> mock1, mock2;
  EXPECT_CALL(mock1, OnTraceBeginSync(kMainSyncId, Eq("Main")));
  WithContext with(&mock1, "Main");

  {
    EXPECT_CALL(mock1, GetEventListener(_)).WillOnce(Return(&mock2));
    EXPECT_CALL(mock1, OnTraceEnterSync(kFirstAutoSyncId, Eq("Child")));
    WithContext with(Context::Current(), "Child");
    EXPECT_THAT(active_sync_id(), Eq(kMainSyncId));
    EXPECT_THAT(internal::active_event_listener(), &mock1);

    {
      EXPECT_CALL(mock1, OnTraceSuspendSync(kMainSyncId));
      WithContext with({}, "Empty");
      EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
      EXPECT_THAT(internal::active_event_listener(), nullptr);
      EXPECT_CALL(mock1, OnTraceResumeSync(kMainSyncId));
    }

    EXPECT_CALL(mock1, OnTraceEnterSync(kMainSyncId, _));

    EXPECT_THAT(active_sync_id(), Eq(kMainSyncId));
    EXPECT_THAT(internal::active_event_listener(), &mock1);
  }
  EXPECT_CALL(mock1, OnTraceEndSync(kMainSyncId));
}

TEST_F(SyncContextTest, CornerCaseSwapRestoreAbandonNestedContext) {
  StrictMock<MockTraceEventListener> mock1, mock2;
  Context tc1(&mock1);
  EXPECT_CALL(mock1, GetEventListener(_)).WillOnce(Return(&mock2));
  Context tc2 = tc1;

  // Swap --> tc1 is active
  EXPECT_CALL(mock1, OnTraceBeginSync(kMainSyncId, _));
  Context::Swap(tc1);

  // Swap --> tc2 is nested
  EXPECT_CALL(mock1, OnTraceEnterSync(tc2.sync_id(), _));
  Context::Swap(tc2);

  // Swap out tc2 --> sync is suspended, mock2 is now discarded.
  EXPECT_CALL(mock2, ReleaseEventListener());
  EXPECT_CALL(mock1, OnTraceSuspendSync(kMainSyncId));
  {
    Context empty;
    Context::Swap(empty);
    EXPECT_CALL(mock1, ReleaseEventListener());
  }

  // Swap back tc1 --> was nested, now abandoned
  EXPECT_DEBUG_DEATH(Context::Swap(tc2), "");

  // Restore
  Context::Restore(tc1);
}

TEST_F(SyncContextTest, SwapAbandonNestedTraceContext) {
  StrictMock<MockTraceEventListener> mock1, mock2;
  Context tc1;
  tc1.AddListener(&mock1);
  EXPECT_CALL(mock1, GetEventListener(_)).WillOnce(Return(&mock2));
  Context tc2 = tc1;

  // Swap in tc1, then swap in tc2 nesting tc2 inside tc1:
  //   BEGIN sync_id: 1
  //   ENTER sync_id: 1, (enter sync_id: 3)
  EXPECT_CALL(mock1, OnTraceBeginSync(kMainSyncId, _));
  Context::Swap(tc1);
  EXPECT_CALL(mock1, OnTraceEnterSync(kFirstAutoSyncId, _));
  Context::Swap(tc2);

  // Restore to empty using tc1. This ends the current sync execution.
  //   END sync_id: 1
  EXPECT_CALL(mock1, OnTraceEndSync(kMainSyncId));
  Context::Restore(tc1);

  // Restore from tc2 (swapped out tc1) now discovers we abandoned
  // tc1 for synchronous tracing:
  EXPECT_DEBUG_DEATH(Context::Restore(tc2), ".*");
}

TEST_F(SyncContextTest, SwapNestedContextIntoDifferentSyncOfSameTrace) {
  // This test specifically exercises the subtle case where code scopes
  // a nested context (leading to ENTER/ENTER) and swaps it back for re-use /
  // resumption, but then swaps it back into an unrelated different sync id
  // (say, a child fiber for the same RPC) again as a nested context.
  // Our 'nested invariant' checks on 'managed_sync_id_` must make sure we
  // recognize this by removing the active sync id value from the context once
  // it is swapped out as 'nested'.
  NiceMock<MockTraceEventListener> mock1, mock2, mock3;
  WithContext with(&mock1, "Main");

  EXPECT_CALL(mock1, GetEventListener(_)).WillOnce(Return(&mock2));
  Context nested = Context::Current();

  EXPECT_CALL(mock1, GetEventListener(_)).WillOnce(Return(&mock3));
  Context child = Context::Current();

  EXPECT_CALL(mock1, OnTraceEnterSync(kFirstAutoSyncId, _));
  Context::Swap(nested);
  EXPECT_CALL(mock1, OnTraceEnterSync(kMainSyncId, _));
  Context::Swap(nested);

  RawThread thread([&, child = std::move(child)]() mutable {
    WithContext with(std::move(child), "Thread");
    EXPECT_CALL(mock3, OnTraceEnterSync(kFirstAutoSyncId, _));
    Context::Swap(nested);
    EXPECT_CALL(mock3, OnTraceEnterSync(kSecondAutoSyncId, _));
    Context::Restore(nested);
  });
  thread.Join();
}

}  // namespace
}  // namespace perftools::tracing::core
