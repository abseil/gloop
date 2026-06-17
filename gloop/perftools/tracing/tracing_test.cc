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

#include "gloop/perftools/tracing/tracing.h"

#include "absl/strings/string_view.h"
#include "gloop/perftools/tracing/mock_trace_event_listener.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/test_only_access.h"
#include "gloop/perftools/tracing/trace_source_location.h"
#include "gloop/perftools/tracing/tracing_base.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace perftools::tracing {
namespace {

using ::perftools::tracing::testing::TestOnlyAccess;
using ::testing::_;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::Ne;
using ::testing::StrictMock;

TraceSourceLocation SourceLocation(const char* file, int line) {
  return TraceSourceLocation(
      TestOnlyAccess::Create<TraceSourceLocation::Access>(), file, line);
}

MATCHER_P2(EqSourceLocation, file, line, "Matches source location") {
  return arg.file_name() == absl::string_view(file) && arg.line() == line;
}

MATCHER(IsSourceLocation, "Matches " __FILE__) {
  if (!arg.IsSourceLocation()) {
    *result_listener << "Expected source location "
                     << __FILE__ ", found non source location " << arg;
    return false;
  }
  TraceSourceLocation source_location = arg.source_location();
  if (source_location.file_name() != absl::string_view(__FILE__)) {
    *result_listener << "Expected source location " << __FILE__ << ", found "
                     << source_location.file_name();
    return false;
  }
  return true;
}

// Helper class to quickly scope a trace event listener for testing
class WithListener {
 public:
  explicit WithListener(TraceEventListener* listener) {
    internal::set_active_sync_id(kMainSyncId);
    internal::set_active_event_listener(listener);
  }

  ~WithListener() {
    internal::set_active_sync_id(kNoSyncId);
    internal::set_active_event_listener(nullptr);
  }
};

TEST(TracingApiTest, Inactive) {
  TraceRegion region("Hello world");
  TraceMark("Not Marc");
  TraceScopedWait wait1(kNoBarrierId, "Wait wait");
  TraceScopedWait wait2(&wait1, "Wait wait");
  wait1.SetBarrierId(kNoBarrierId);
  wait1.SetBarrierId(BarrierId{1245});
  wait1.SetBarrierId(&wait1);
  TraceSignal(kNoBarrierId);
  TraceSignal(BarrierId{1245});
  TraceSignal(BarrierId{1245}, "12345");
  TraceSignal(&wait1);
  TraceObserved(kNoBarrierId);
  TraceObserved(BarrierId{1245});
  TraceObserved(&wait1);
  TraceSend("Send it", MsgOrigin::kClient, MsgId{1});
  TraceReceive("Praise", MsgOrigin::kServer, MsgId{1});
  TraceSessionStart("Start", MsgId{842}, EndPoint::kStreamingClient);
  TraceSessionEnd("End", MsgId{842}, EndPoint::kStreamingServer);
  TraceStreamingSend(MsgOrigin::kClient, MsgId{842}, MsgSequence{0},
                     MsgFlags::kDefault);
  TraceStreamingSend(MsgOrigin::kServer, MsgId{842}, MsgSequence{17},
                     MsgFlags::kHalfClose);
  TraceStreamingReceive(MsgOrigin::kClient, MsgId{842}, MsgSequence{0},
                        MsgFlags::kDefault);
  TraceStreamingReceive(MsgOrigin::kServer, MsgId{842}, MsgSequence{13},
                        MsgFlags::kHalfClose);
  TraceScopedSuspend suspend;
}

TEST(TracingApiTest, Region) {
  InSequence in_sequence;
  StrictMock<MockTraceEventListener> mock;
  WithListener with(&mock);

  EXPECT_CALL(mock,
              OnTraceBeginRegion(Eq("Hello"), EqSourceLocation("foo.h", 12)));
  TraceRegion region1("Hello", SourceLocation("foo.h", 12));
  EXPECT_CALL(mock, OnTraceBeginRegion(
                        Eq("World"), EqSourceLocation(__FILE__, __LINE__ + 1)));
  TraceRegion region2("World");
  EXPECT_CALL(mock, OnTraceEndRegion());
  EXPECT_CALL(mock, OnTraceEndRegion());
}

TEST(TracingApiTest, Mark) {
  StrictMock<MockTraceEventListener> mock;
  WithListener with(&mock);

  EXPECT_CALL(mock, OnTraceMark(Eq("Not Marc"), EqSourceLocation("foo.h", 12)));
  TraceMark("Not Marc", SourceLocation("foo.h", 12));
  EXPECT_CALL(mock, OnTraceMark(Eq("Also not Marc"),
                                EqSourceLocation(__FILE__, __LINE__ + 1)));
  TraceMark("Also not Marc");
}

TEST(TracingApiTest, DefaultScopedWait) {
  InSequence in_sequence;
  StrictMock<MockTraceEventListener> mock;
  WithListener with(&mock);

  EXPECT_CALL(mock, OnTraceWait(kNoBarrierId, Eq(StringRef{})));
  TraceScopedWait wait;
  EXPECT_CALL(mock, OnTraceContinue(kNoBarrierId));
}

TEST(TracingApiTest, ScopedWaitWithBarrierId) {
  InSequence in_sequence;
  StrictMock<MockTraceEventListener> mock;
  WithListener with(&mock);

  EXPECT_CALL(mock, OnTraceWait(BarrierId{1234}, Eq("Wait wait")));
  TraceScopedWait wait(BarrierId(1234), "Wait wait");
  EXPECT_CALL(mock, OnTraceContinue(BarrierId{1234}));
}

TEST(TracingApiTest, ScopedWaitSetBarrierId) {
  InSequence in_sequence;
  StrictMock<MockTraceEventListener> mock;
  WithListener with(&mock);

  EXPECT_CALL(mock, OnTraceWait(BarrierId{1234}, Eq("Wait wait")));
  TraceScopedWait wait(BarrierId(1234), "Wait wait");
  wait.SetBarrierId(BarrierId{3456});
  EXPECT_CALL(mock, OnTraceContinue(BarrierId{3456}));
}

TEST(TracingApiTest, ScopedWaitWithObject) {
  InSequence in_sequence;
  StrictMock<MockTraceEventListener> mock;
  WithListener with(&mock);

  int id1 = 0;
  EXPECT_CALL(mock, OnTraceWait(ToBarrierId(&id1), Eq("Wait wait")));
  TraceScopedWait wait(&id1, "Wait wait");
  int id2 = 0;
  wait.SetBarrierId(&id2);
  EXPECT_CALL(mock, OnTraceContinue(ToBarrierId(&id2)));
}

TEST(TracingApiTest, Signal) {
  StrictMock<MockTraceEventListener> mock;
  WithListener with(&mock);

  EXPECT_CALL(mock, OnTraceSignal(BarrierId{1234}, IsSourceLocation()));
  TraceSignal(BarrierId(1234));
  EXPECT_CALL(mock, OnTraceSignal(BarrierId{1234}, Eq("Ping")));
  TraceSignal(BarrierId(1234), "Ping");
}

TEST(TracingApiTest, SignalWithObject) {
  StrictMock<MockTraceEventListener> mock;
  WithListener with(&mock);

  int id = 0;
  EXPECT_CALL(mock, OnTraceSignal(ToBarrierId(&id), IsSourceLocation()));
  TraceSignal(&id);
  EXPECT_CALL(mock, OnTraceSignal(ToBarrierId(&id), Eq("Ping")));
  TraceSignal(&id, "Ping");
}

TEST(TracingApiTest, Observed) {
  StrictMock<MockTraceEventListener> mock;
  WithListener with(&mock);

  EXPECT_CALL(mock, OnTraceObserved(BarrierId{1234}, IsSourceLocation()));
  TraceObserved(BarrierId(1234));
  EXPECT_CALL(mock, OnTraceObserved(BarrierId{1234}, Eq("Peekaboo")));
  TraceObserved(BarrierId(1234), "Peekaboo");
}

TEST(TracingApiTest, ObservedWithObject) {
  StrictMock<MockTraceEventListener> mock;
  WithListener with(&mock);

  int id = 0;
  EXPECT_CALL(mock, OnTraceObserved(ToBarrierId(&id), IsSourceLocation()));
  TraceObserved(&id);
  EXPECT_CALL(mock, OnTraceObserved(ToBarrierId(&id), Eq("Peekaboo")));
  TraceObserved(&id, "Peekaboo");
}

TEST(TracingApiTest, ScopedDisableTraceEventsWithoutActiveListenerDoesNothing) {
  {
    ScopedDisableTraceEvents scoped;
    EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
    EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  }
  EXPECT_THAT(active_sync_id(), Eq(kNoSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
}

TEST(TracingApiTest, ScopedDisableTraceEventsWithActiveListenerSwapsToNoop) {
  StrictMock<MockTraceEventListener> mock;
  WithListener with(&mock);

  EXPECT_CALL(mock, OnTraceMark(Eq("Mark1"), _));
  tracing::TraceMark("Mark1");
  {
    ScopedDisableTraceEvents scoped;
    TraceEventListener* noop = internal::active_event_listener();
    EXPECT_THAT(active_sync_id(), Eq(kMainSyncId));
    EXPECT_THAT(noop, Ne(nullptr));
    EXPECT_THAT(noop, Ne(&mock));
    EXPECT_THAT(noop->GetEventListener(kMainSyncId), Eq(noop));
    noop->ReleaseEventListener();
    tracing::TraceMark("Mark2");
  }
  EXPECT_THAT(active_sync_id(), Eq(kMainSyncId));
  EXPECT_THAT(internal::active_event_listener(), Eq(&mock));
  EXPECT_CALL(mock, OnTraceMark(Eq("Mark3"), _));
  tracing::TraceMark("Mark3");
}

TEST(TracingApiTest, TraceScopedSuspend) {
  StrictMock<MockTraceEventListener> mock;
  WithListener with(&mock);

  EXPECT_CALL(mock, OnTraceMark(Eq("Mark1"), _));
  tracing::TraceMark("Mark1");
  {
    EXPECT_CALL(mock, OnTraceSuspendSync(Eq(kMainSyncId)));
    TraceScopedSuspend suspend;
    tracing::TraceMark("IgnoredMark");
    {
      TraceScopedSuspend this_should_be_a_nop;
      tracing::TraceMark("IgnoredMark");
    }
    tracing::TraceMark("IgnoredMark");
    EXPECT_CALL(mock, OnTraceResumeSync(Eq(kMainSyncId)));
  }
  EXPECT_CALL(mock, OnTraceMark(Eq("Mark2"), _));
  tracing::TraceMark("Mark2");
}

TEST(TracingApiTest, TraceSendReceiveDefaultSequence) {
  StrictMock<MockTraceEventListener> mock;
  WithListener with(&mock);
  EXPECT_CALL(mock, OnTraceSend(Eq("Send it"), MsgOrigin::kClient, 12345));
  TraceSend("Send it", MsgOrigin::kClient, 12345);
  EXPECT_CALL(mock, OnTraceReceive(Eq("Got it"), MsgOrigin::kClient, 12345));
  TraceReceive("Got it", MsgOrigin::kClient, 12345);
}

TEST(TracingApiTest, TraceStartEndSession) {
  StrictMock<MockTraceEventListener> mock;
  WithListener with(&mock);
  EXPECT_CALL(mock, OnTraceSessionStart(Eq("Client"), 12345,
                                        EndPoint::kStreamingClient));
  EXPECT_CALL(mock, OnTraceSessionStart(Eq("Server"), 54321,
                                        EndPoint::kStreamingServer));
  TraceSessionStart("Client", 12345, EndPoint::kStreamingClient);
  TraceSessionStart("Server", 54321, EndPoint::kStreamingServer);
}

TEST(TracingApiTest, TraceStreamingSendReceive) {
  StrictMock<MockTraceEventListener> mock;
  WithListener with(&mock);
  EXPECT_CALL(mock, OnTraceStreamingSend(MsgOrigin::kClient, 12345,
                                         MsgSequence{1}, MsgFlags::kHalfClose));
  EXPECT_CALL(mock,
              OnTraceStreamingReceive(MsgOrigin::kClient, 12345, MsgSequence{2},
                                      MsgFlags::kControl));
  TraceStreamingSend(MsgOrigin::kClient, 12345, MsgSequence{1},
                     MsgFlags::kHalfClose);
  TraceStreamingReceive(MsgOrigin::kClient, 12345, MsgSequence{2},
                        MsgFlags::kControl);
}

}  // namespace
}  // namespace perftools::tracing
