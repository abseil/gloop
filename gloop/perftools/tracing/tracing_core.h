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

// This file defines the 'core' tracing API for Dapper causality tracing.
//
// All functions declare in this header are intended to be called only by Dapper
// implementations and select core library APIs such as scheduling logic, RPCs,
// and higher level synchronization logic such as `absl::Notification`.
//
// Applications should use the public Dapper PE APIs for purposes such as
// scoping code regions and adding application specific annotations.
//
#ifndef THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_TRACING_CORE_H_
#define THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_TRACING_CORE_H_

#include "absl/log/check.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/trace_event_listener.h"
#include "gloop/perftools/tracing/tracing_base.h"

namespace perftools::tracing::core {

// Emits the `OnTraceSpawn()` event if the current thread is traced.
// Requires: sync_id != kNoSyncId
void TraceSpawn(SyncId sync_id, StringRef label);

// Emits the `OnTraceWait()` event if the current thread is traced.
void TraceWait(BarrierId barrier_id, StringRef label);
void TraceWait(const void* barrier, StringRef label);

// Emits the `OnTraceContinue()` event if the current thread is traced.
void TraceContinue(BarrierId barrier_id);
void TraceContinue(const void* barrier);

// Emits the `OnTraceObserved()` event if the current thread is traced.
void TraceObserved(BarrierId barrier);
void TraceObserved(const void* barrier);

// Emits the `OnTraceSignal()` event if the current thread is traced.
void TraceSignal(BarrierId barrier_id, StringRef label);
void TraceSignal(const void* barrier, StringRef label);

// Emits the `OnTraceSend()` event if the current thread is traced.
// `sequence` contains the sequence number for the message if it is part of
// a streaming request or response, or `kNoMsgSequence` if this message is
// a unary RPC request or response for which we record no sequence number.
void TraceSend(StringRef label, MsgOrigin origin, MsgId id);

// Emits the `OnTraceReceive()` event if the current thread is traced.
// `sequence` contains the sequence number for the message if it is part of
// a streaming request or response, or `kNoMsgSequence` if this message is
// a unary RPC request or response for which we record no sequence number.
void TraceReceive(StringRef label, MsgOrigin origin, MsgId id);

// Emits the `OnTraceSessionStart()` event if the current thread is traced.
// See documentation in the public API in 'tracing.h' for more details.
void TraceSessionStart(StringRef label, MsgId id, EndPoint end_point);

// Emits the `OnTraceSessionEnd()` event if the current thread is traced.
// See documentation in the public API in 'tracing.h' for more details.
void TraceSessionEnd(StringRef label, MsgId id, EndPoint end_point);

// Emits the `OnTraceStreamingSend()` event if the current thread is traced.
// See documentation in the public API in 'tracing.h' for more details.
void TraceStreamingSend(MsgOrigin origin, MsgId id, MsgSequence sequence,
                        MsgFlags flags);

// Emits the `OnTraceStreamingReceive()` event if the current thread is traced.
// See documentation in the public API in 'tracing.h' for more details.
void TraceStreamingReceive(MsgOrigin origin, MsgId id, MsgSequence sequence,
                           MsgFlags flags);

// Emits the `OnTraceMark()` event if the current thread is traced.
void TraceMark(StringRef label, TraceSourceLocation location);

// Emits the `OnTraceBeginRegion()` event if the current thread is traced.
void TraceBeginRegion(StringRef label, TraceSourceLocation location);

// Emits the `OnTraceEndRegion()` event if the current thread is traced.
void TraceEndRegion();

// Emits the `OnTraceControlFlow()` event if the current thread is traced.
// See documentation in the public API in 'tracing.h' for more details.
void TraceControlFlow(StringRef label, ControlFlowType type, ControlFlowId id,
                      ControlFlowSequence sequence);

inline void TraceSpawn(SyncId sync_id, StringRef label) {
  DCHECK_NE(sync_id, kNoSyncId);
  if (auto* listener = internal::active_event_listener()) {
    listener->OnTraceSpawn(sync_id, label);
  }
}

inline void TraceWait(BarrierId barrier_id, StringRef label) {
  if (auto* listener = internal::active_event_listener()) {
    listener->OnTraceWait(barrier_id, label);
  }
}
inline void TraceWait(const void* barrier, StringRef label) {
  if (auto* listener = internal::active_event_listener()) {
    listener->OnTraceWait(ToBarrierId(barrier), label);
  }
}

inline void TraceContinue(BarrierId barrier_id) {
  if (auto* listener = internal::active_event_listener()) {
    listener->OnTraceContinue(barrier_id);
  }
}
inline void TraceContinue(const void* barrier) {
  if (auto* listener = internal::active_event_listener()) {
    listener->OnTraceContinue(ToBarrierId(barrier));
  }
}

inline void TraceObserved(BarrierId barrier_id, StringRef label) {
  if (auto* listener = internal::active_event_listener()) {
    listener->OnTraceObserved(barrier_id, label);
  }
}
inline void TraceObserved(const void* barrier, StringRef label) {
  if (auto* listener = internal::active_event_listener()) {
    listener->OnTraceObserved(ToBarrierId(barrier), label);
  }
}

inline void TraceSignal(BarrierId barrier_id, StringRef label) {
  if (auto* listener = internal::active_event_listener()) {
    listener->OnTraceSignal(barrier_id, label);
  }
}
inline void TraceSignal(const void* barrier, StringRef label) {
  if (auto* listener = internal::active_event_listener()) {
    listener->OnTraceSignal(ToBarrierId(barrier), label);
  }
}

inline void TraceSend(StringRef label, MsgOrigin origin, MsgId id) {
  if (auto* listener = internal::active_event_listener()) {
    listener->OnTraceSend(label, origin, id);
  }
}

inline void TraceReceive(StringRef label, MsgOrigin origin, MsgId id) {
  if (auto* listener = internal::active_event_listener()) {
    listener->OnTraceReceive(label, origin, id);
  }
}

inline void TraceSessionStart(StringRef label, MsgId id, EndPoint end_point) {
  if (auto* listener = internal::active_event_listener()) {
    listener->OnTraceSessionStart(label, id, end_point);
  }
}

inline void TraceSessionEnd(StringRef label, MsgId id, EndPoint end_point) {
  if (auto* listener = internal::active_event_listener()) {
    listener->OnTraceSessionEnd(label, id, end_point);
  }
}

inline void TraceStreamingSend(MsgOrigin origin, MsgId id, MsgSequence sequence,
                               MsgFlags flags) {
  if (auto* listener = internal::active_event_listener()) {
    listener->OnTraceStreamingSend(origin, id, sequence, flags);
  }
}

inline void TraceStreamingReceive(MsgOrigin origin, MsgId id,
                                  MsgSequence sequence, MsgFlags flags) {
  if (auto* listener = internal::active_event_listener()) {
    listener->OnTraceStreamingReceive(origin, id, sequence, flags);
  }
}

inline void TraceMark(StringRef label, TraceSourceLocation location) {
  if (auto* listener = internal::active_event_listener()) {
    listener->OnTraceMark(label, location);
  }
}

inline void TraceBeginRegion(StringRef label, TraceSourceLocation location) {
  if (auto* listener = internal::active_event_listener()) {
    listener->OnTraceBeginRegion(label, location);
  }
}

inline void TraceEndRegion() {
  if (auto* listener = internal::active_event_listener()) {
    listener->OnTraceEndRegion();
  }
}

inline void TraceControlFlow(StringRef label, ControlFlowType type,
                             ControlFlowId id, ControlFlowSequence sequence) {
  if (auto* listener = internal::active_event_listener()) {
    listener->OnTraceControlFlow(label, type, id, sequence);
  }
}

}  // namespace perftools::tracing::core

#endif  // THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_TRACING_CORE_H_
