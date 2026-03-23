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

#ifndef THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_MOCK_TRACE_EVENT_LISTENER_H_
#define THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_MOCK_TRACE_EVENT_LISTENER_H_

#include <utility>

#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/trace_event_listener.h"
#include "gloop/perftools/tracing/tracing_base.h"
#include "gmock/gmock.h"

namespace perftools::tracing {

// Mock class for TraceEventListener.
// The mock delegates `Extract` calls to the concrete default.
class MockTraceEventListener : public TraceEventListener {
 public:
  // Installs an ON_CALL() delegating `Extract` to the base class which
  // provides a default implementation for all implementations.
  MockTraceEventListener();

  MOCK_METHOD(TraceEventListener*, GetEventListener, (SyncId sync_id));
  MOCK_METHOD(void, ReleaseEventListener, ());
  MOCK_METHOD((std::pair<TraceEventListener*, bool>), Extract,
              (TraceEventListener*));
  MOCK_METHOD(bool, Contains, (TraceEventListener*), (const));

  MOCK_METHOD(void, OnTraceSpawn, (SyncId sync_id, StringRef));

  MOCK_METHOD(void, OnTraceBeginSync, (SyncId sync_id, StringRef label));
  MOCK_METHOD(void, OnTraceEnterSync, (SyncId sync_id, StringRef label));
  MOCK_METHOD(void, OnTraceSuspendSync, (SyncId sync_id));
  MOCK_METHOD(void, OnTraceResumeSync, (SyncId sync_id));
  MOCK_METHOD(void, OnTraceEndSync, (SyncId sync_id));

  MOCK_METHOD(void, OnTraceWait, (BarrierId, StringRef));
  MOCK_METHOD(void, OnTraceContinue, (BarrierId));
  MOCK_METHOD(void, OnTraceObserved, (BarrierId, StringRef));
  MOCK_METHOD(void, OnTraceSignal, (BarrierId, StringRef));

  MOCK_METHOD(void, OnTraceSend, (StringRef, MsgOrigin, MsgId, MsgSequence));
  MOCK_METHOD(void, OnTraceReceive, (StringRef, MsgOrigin, MsgId, MsgSequence));

  MOCK_METHOD(void, OnTraceSessionStart, (StringRef, MsgId, EndPoint));
  MOCK_METHOD(void, OnTraceSessionEnd, (StringRef, MsgId, EndPoint));

  MOCK_METHOD(void, OnTraceStreamingSend,
              (MsgOrigin, MsgId, MsgSequence, MsgFlags));
  MOCK_METHOD(void, OnTraceStreamingReceive,
              (MsgOrigin, MsgId, MsgSequence, MsgFlags));

  MOCK_METHOD(void, OnTraceMark, (StringRef));
  MOCK_METHOD(void, OnTraceBeginRegion, (StringRef));
  MOCK_METHOD(void, OnTraceEndRegion, ());

  MOCK_METHOD(void, OnTraceControlFlow,
              (StringRef, ControlFlowType, ControlFlowId, ControlFlowSequence));
};

inline MockTraceEventListener::MockTraceEventListener() {
  ON_CALL(*this, Extract(::testing::_))
      .WillByDefault([this](TraceEventListener* listener) {
        return TraceEventListener::Extract(listener);
      });

  ON_CALL(*this, Contains(::testing::_))
      .WillByDefault(::testing::Invoke(
          [this](TraceEventListener* listener) { return listener == this; }));

  EXPECT_CALL(*this, ReleaseEventListener).Times(::testing::AnyNumber());
}

}  // namespace perftools::tracing

#endif  // THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_MOCK_TRACE_EVENT_LISTENER_H_
