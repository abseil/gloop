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

#include "gloop/perftools/tracing/trace_event_listener.h"

#include <utility>
#include <vector>

#include "gloop/gloop_test.h"
#include "gloop/perftools/tracing/mock_trace_event_listener.h"
#include "gloop/perftools/tracing/tracing_base.h"

namespace perftools::tracing {
namespace {

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::StrictMock;

// Returns a <Listener, success> pair value as returned by
// the Extract() function.
auto EqPair(TraceEventListener* listener, bool success) {
  return Eq(std::make_pair(listener, success));
}

// Minimum default TraceEventListener implementation.
class DefaultTraceEventListener : public TraceEventListener {
 public:
  TraceEventListener* GetEventListener(SyncId) override { return this; }
  void ReleaseEventListener() override {}
};

TEST(TraceEventListener, Contains) {
  DefaultTraceEventListener listener1, listener2;
  EXPECT_TRUE(listener1.Contains(&listener1));
  EXPECT_FALSE(listener1.Contains(nullptr));
  EXPECT_FALSE(listener1.Contains(&listener2));
}

TEST(TraceEventListener, Extract) {
  DefaultTraceEventListener listener1, listener2;
  EXPECT_THAT(listener1.Extract(&listener1), EqPair(nullptr, true));
  EXPECT_THAT(listener1.Extract(nullptr), EqPair(&listener1, false));
  EXPECT_THAT(listener1.Extract(&listener2), EqPair(&listener1, false));
}

TEST(TraceEventListener, ExtractAll) {
  DefaultTraceEventListener listener;
  std::vector<TraceEventListener*> listeners;
  listener.ExtractAll(listeners);
  EXPECT_THAT(listeners, ElementsAre(&listener));
}

TEST(TraceEventListener, Depth) {
  DefaultTraceEventListener listener;
  EXPECT_THAT(listener.Depth(), Eq(1));
}

TEST(TraceEventListener, CascadingRelease) {
  InSequence in_sequence;
  StrictMock<MockTraceEventListener> mock1;
  StrictMock<MockTraceEventListener> mock2;

  // mock2 doesn't match mock1
  EXPECT_CALL(mock1, Extract(&mock2));
  EXPECT_THAT(mock1.CascadingRelease(&mock2), EqPair(&mock1, false));

  // mock1 matches mock1
  EXPECT_CALL(mock1, Extract(&mock1));
  EXPECT_CALL(mock1, ReleaseEventListener());
  EXPECT_THAT(mock1.CascadingRelease(&mock1), EqPair(nullptr, true));
}

TEST(TraceEventListener, InvokeAllEvents) {
  class MiniListener : public TraceEventListener {
   public:
    TraceEventListener* GetEventListener(SyncId) override { return this; }
    void ReleaseEventListener() override {}
  };
  MiniListener listener;
  listener.OnTraceSpawn(SyncId{632}, "Hello world");
  listener.OnTraceBeginSync(SyncId{412}, "Begin");
  listener.OnTraceEndSync(SyncId{721});
  listener.OnTraceSuspendSync(SyncId{721});
  listener.OnTraceResumeSync(SyncId{721});
  listener.OnTraceEnterSync(SyncId{722}, "Enter");
  listener.OnTraceWait(BarrierId{313131}, "Wait wait");
  listener.OnTraceContinue(BarrierId{3412442});
  listener.OnTraceObserved(BarrierId{3412442}, "Peekaboo");
  listener.OnTraceSignal(BarrierId{7648223}, "Ping");

  listener.OnTraceSend("Send it!", MsgOrigin::kClient, MsgId{3332},
                       MsgSequence{1});
  listener.OnTraceReceive("Send it!", MsgOrigin::kServer, MsgId{3332},
                          MsgSequence{1});

  listener.OnTraceSessionStart("Start Client", MsgId{842},
                               EndPoint::kStreamingClient);
  listener.OnTraceStreamingSend(MsgOrigin::kClient, MsgId{842}, MsgSequence{0},
                                MsgFlags::kDefault);
  listener.OnTraceStreamingReceive(MsgOrigin::kServer, MsgId{842},
                                   MsgSequence{0}, MsgFlags::kDefault);
  listener.OnTraceSessionEnd("Finish Client", MsgId{842},
                             EndPoint::kStreamingClient);

  listener.OnTraceMark("Not Marc");
  listener.OnTraceBeginRegion("Here, not there");
  listener.OnTraceEndRegion();
  listener.OnTraceControlFlow("Flow", ControlFlowType::kSchedule,
                              ControlFlowId{3421}, ControlFlowSequence(1));
}

TEST(TraceEventListenerPtr, ConfirmValidUniquePtrImplementation) {
  StrictMock<MockTraceEventListener> mock;
  TraceEventListenerPtr ptr;
  EXPECT_THAT(ptr, Eq(nullptr));
  EXPECT_THAT(ptr.get(), Eq(nullptr));
  ptr = TraceEventListenerPtr(&mock);
  EXPECT_THAT(ptr.get(), Eq(&mock));
  EXPECT_CALL(mock, ReleaseEventListener());
}

}  // namespace
}  // namespace perftools::tracing
