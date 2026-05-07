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

#include <cstddef>
#include <utility>
#include <vector>

#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/trace_source_location.h"
#include "gloop/perftools/tracing/tracing_base.h"

namespace perftools::tracing {

void TraceEventListener::OnTraceBeginSync(SyncId, StringRef) {}
void TraceEventListener::OnTraceEndSync(SyncId) {}
void TraceEventListener::OnTraceSuspendSync(SyncId) {}
void TraceEventListener::OnTraceResumeSync(SyncId) {}
void TraceEventListener::OnTraceEnterSync(SyncId, StringRef) {}
void TraceEventListener::OnTraceSpawn(SyncId, StringRef) {}
void TraceEventListener::OnTraceWait(BarrierId, StringRef) {}
void TraceEventListener::OnTraceContinue(BarrierId) {}
void TraceEventListener::OnTraceObserved(BarrierId, StringRef) {}
void TraceEventListener::OnTraceSignal(BarrierId, StringRef) {}
void TraceEventListener::OnTraceSend(StringRef, MsgOrigin, MsgId, MsgSequence) {
}
void TraceEventListener::OnTraceReceive(StringRef, MsgOrigin, MsgId,
                                        MsgSequence) {}
void TraceEventListener::OnTraceSessionStart(StringRef, MsgId, EndPoint) {}
void TraceEventListener::OnTraceSessionEnd(StringRef, MsgId, EndPoint) {}
void TraceEventListener::OnTraceStreamingSend(MsgOrigin, MsgId, MsgSequence,
                                              MsgFlags) {}
void TraceEventListener::OnTraceStreamingReceive(MsgOrigin, MsgId, MsgSequence,
                                                 MsgFlags) {}
void TraceEventListener::OnTraceMark(StringRef, TraceSourceLocation location) {}
// void TraceEventListener::OnTraceBeginRegion(StringRef label) {
//   OnTraceBeginRegion(label, TraceSourceLocation::current());
// }
void TraceEventListener::OnTraceBeginRegion(StringRef, TraceSourceLocation) {}
void TraceEventListener::OnTraceEndRegion() {}
void TraceEventListener::OnTraceControlFlow(StringRef, ControlFlowType,
                                            ControlFlowId,
                                            ControlFlowSequence) {}

std::pair<TraceEventListener*, bool> TraceEventListener::Extract(
    TraceEventListener* listener) {
  return (listener == this) ? std::make_pair(nullptr, true)
                            : std::make_pair(this, false);
}

void TraceEventListener::ExtractAll(
    std::vector<TraceEventListener*>& listeners) {
  listeners.push_back(this);
}

bool TraceEventListener::Contains(TraceEventListener* listener) const {
  return listener == this;
}

size_t TraceEventListener::Depth() const { return 1; }

}  // namespace perftools::tracing
