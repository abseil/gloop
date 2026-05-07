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

// This file defines the 'public' tracing API for causality tracing.
//
#ifndef THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_TRACING_H_
#define THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_TRACING_H_

#include "absl/log/check.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/trace_source_location.h"
#include "gloop/perftools/tracing/tracing_base.h"
#include "gloop/perftools/tracing/tracing_core.h"

namespace perftools::tracing {

// `TraceRegion` scopes a region of execution, emitting 'TraceBeginRegion`
// and `TraceEndRegion` events if a trace event listener is active for the
// current thread.
class TraceRegion {
 public:
  explicit TraceRegion(StringRef label, TraceSourceLocation location =
                                            TraceSourceLocation::current()) {
    core::TraceBeginRegion(label, location);
  }
  ~TraceRegion() { core::TraceEndRegion(); }
};

// Emits the `OnTraceMark()` event if the current thread is traced.
void TraceMark(StringRef label,
               TraceSourceLocation location = TraceSourceLocation::current());

// `TraceScopedWait` scopes a possible blocking execution, emitting 'TraceWait`
// and `TraceContinue` events if a trace event listener is active for the
// current thread. Applications should prefer to use standard library objects
// and functions that have trace support by default. `TraceScopedWait` should
// only be needed for annotating non standard or lower level synchronizations.
class TraceScopedWait {
 public:
  // Creates a scoped wait with an empty label and kNoBarrierId.
  TraceScopedWait();

  // Creates a scoped wait with the provided label and the provided barrier id.
  explicit TraceScopedWait(BarrierId barrier_id, StringRef label = {});
  explicit TraceScopedWait(const void* object, StringRef label = {});

  // Sets the barrier id to be used for the 'continue' event emitted at the end
  // of the scope. This function is typically used for multi-object waits.
  void SetBarrierId(BarrierId barrier_id);
  void SetBarrierId(const void* object);

  ~TraceScopedWait();

 private:
  BarrierId barrier_id_;
};

// Signals the provided barrier. `barrier_id` typically matches the 'Continue'
// `barrier_id` value of a corresponding `TraceScopedWait` instance.
void TraceSignal(BarrierId barrier_id,
                 StringRef label = TraceSourceLocation::current());
void TraceSignal(const void* object,
                 StringRef label = TraceSourceLocation::current());

// Records that the provided barrier was observed as being signaled, i.e., a non
// blocking wait for a signal observing a relevant state change. For example,
// a `canceled` state of a running fiber.
void TraceObserved(BarrierId barrier_id,
                   StringRef label = TraceSourceLocation::current());
void TraceObserved(const void* object,
                   StringRef label = TraceSourceLocation::current());

// Emits the `OnTraceSend()` event if the current thread is traced.
// `sequence` contains the sequence number for the message if it is part of
// a streaming request or response, or `kNoMsgSequence` if this message is
// a unary RPC request or response for which we record no sequence number.
void TraceSend(StringRef label, MsgOrigin origin, MsgId id,
               MsgSequence sequence = kNoMsgSequence);

// Emits the `OnTraceReceive()` event if the current thread is traced.
// `sequence` contains the sequence number for the message if it is part of
// a streaming request or response, or `kNoMsgSequence` if this message is
// a unary RPC request or response for which we record no sequence number.
void TraceReceive(StringRef label, MsgOrigin origin, MsgId id,
                  MsgSequence sequence = kNoMsgSequence);

// Emits the `OnTraceSessionStart()` event if the current thread is traced.
// This event is emitted for session based protocols such as streaming RPCs.
// 'label' contains the name of the session. 'id' contains the (RPC) id
// identifying the streaming message session. 'end_point` defines the type of
// session and location (client / server) of the session end point.
// Currently the only (streaming) session protocols supported by the tracing
// API are streaming RPC sessions. Unary RPCs do not have explicit sessions
// or Start/End session events.
void TraceSessionStart(StringRef label, MsgId id, EndPoint end_point);

// Emits the `OnTraceSessionEnd()` event if the current thread is traced.
// See `TraceSessionStart()` for documentation.
void TraceSessionEnd(StringRef label, MsgId id, EndPoint end_point);

// Emits the `OnTraceStreamingSend()` event if the current thread is traced.
// 'id' contains the RPC id identifying the streaming message session and
// `sequence` contains the sequence number for the message.
void TraceStreamingSend(MsgOrigin origin, MsgId id, MsgSequence sequence,
                        MsgFlags flags);

// Emits the `OnTraceStreamingReceive()` event if the current thread is traced.
// 'id' contains the RPC id identifying the streaming message session and
// `sequence` contains the sequence number for the message.
void TraceStreamingReceive(MsgOrigin origin, MsgId id, MsgSequence sequence,
                           MsgFlags flags);

// `ScopedDisableTraceEventListeners` disables any trace events to be emitted.
// This class is intended to be used by tracers and tracer implementations.
// For example, a Tracer or TraceEventListener implementation executing non
// trivial logic that can result itself in trace events such as higher level
// synchronization and RPC calls should disable trace events being emitted.
class ScopedDisableTraceEvents {
 public:
  ScopedDisableTraceEvents() noexcept;
  ~ScopedDisableTraceEvents() noexcept;

  // Not copy-able, movable or assignable.
  ScopedDisableTraceEvents(const ScopedDisableTraceEvents&) = delete;
  ScopedDisableTraceEvents& operator=(const ScopedDisableTraceEvents&) = delete;

 private:
  // Returns a singleton, infinite lifetime, no-op TraceEventListener instance.
  static TraceEventListener* NoopListener();

  TraceEventListener* const listener_;
};

// `TraceScopedSuspend` scopes an execution potentially yielding the current CPU
// to another runnable thread such as a call to `Downcalls::Reschedule()` or
// `sched_yield()`, emitting 'TraceSuspendSync()` and `TraceResumeSync()` events
// on the start and end of the scope, disabling any subsequent tracing events
// from being emitted while the scope is active.
//
// This class is intended to be used predominantly by library functions and
// custom scheduler / scheduling logic, and should in general not be used by
// standard server code.
//
// The class should explicitly NOT be used for annotating any potentially
// blocking operations such as `absl::Notification` or blocking IO operations,
// and only be used for yielding operations where no work is executed on behalf
// of the current thread.
class TraceScopedSuspend {
 public:
  TraceScopedSuspend() noexcept;
  ~TraceScopedSuspend() noexcept;

  // Not copy-able, movable or assignable.
  TraceScopedSuspend(const TraceScopedSuspend&) = delete;
  TraceScopedSuspend& operator=(const TraceScopedSuspend&) = delete;

 private:
  const SyncId sync_id_;
  TraceEventListener* const listener_;
};

// Emits the `OnTraceControlFlow()` event if the current thread is traced.
// `label` is the (human readable) label for this event, `id` identifies the
// control flow event. `sequence` can be used to identify multiple events that
// are part of the same control flow. For example, `id` could be set to the RPC
// id of an underlying request, with `sequence` identifying any subsequent
// series of messages being send and received on behalf of the RPC.
// See `ControlFlowType` for more information on control flow event types.
inline void TraceControlFlow(StringRef label, ControlFlowType type,
                             ControlFlowId id,
                             ControlFlowSequence sequence = 0) {
  core::TraceControlFlow(label, type, id, sequence);
}

// --------------------
// Implementation
// --------------------
inline void TraceMark(StringRef label, TraceSourceLocation location) {
  core::TraceMark(label, location);
}

inline TraceScopedWait::~TraceScopedWait() {
  if (auto* listener = internal::active_event_listener()) {
    listener->OnTraceContinue(barrier_id_);
  }
}

inline TraceScopedWait::TraceScopedWait(BarrierId barrier_id, StringRef label) {
  if (auto* listener = internal::active_event_listener()) {
    listener->OnTraceWait(barrier_id_ = barrier_id, label);
  }
}

inline TraceScopedWait::TraceScopedWait(const void* object, StringRef label) {
  if (auto* listener = internal::active_event_listener()) {
    listener->OnTraceWait(barrier_id_ = ToBarrierId(object), label);
  }
}

inline TraceScopedWait::TraceScopedWait()
    : TraceScopedWait(kNoBarrierId, StringRef{}) {}

inline void TraceScopedWait::SetBarrierId(BarrierId barrier_id) {
  barrier_id_ = barrier_id;
}

inline void TraceScopedWait::SetBarrierId(const void* object) {
  barrier_id_ = ToBarrierId(object);
}

inline void TraceSignal(BarrierId barrier_id, StringRef label) {
  core::TraceSignal(barrier_id, label);
}

inline void TraceSignal(const void* object, StringRef label) {
  core::TraceSignal(object, label);
}

inline void TraceObserved(BarrierId barrier_id, StringRef label) {
  core::TraceObserved(barrier_id, label);
}
inline void TraceObserved(const void* object, StringRef label) {
  core::TraceObserved(object, label);
}

inline void TraceSend(StringRef label, MsgOrigin origin, MsgId id,
                      MsgSequence sequence) {
  core::TraceSend(label, origin, id, sequence);
}

inline void TraceReceive(StringRef label, MsgOrigin origin, MsgId id,
                         MsgSequence sequence) {
  core::TraceReceive(label, origin, id, sequence);
}

inline void TraceSessionStart(StringRef label, MsgId id, EndPoint end_point) {
  core::TraceSessionStart(label, id, end_point);
}

inline void TraceSessionEnd(StringRef label, MsgId id, EndPoint end_point) {
  core::TraceSessionEnd(label, id, end_point);
}

inline void TraceStreamingSend(MsgOrigin origin, MsgId id, MsgSequence sequence,
                               MsgFlags flags) {
  core::TraceStreamingSend(origin, id, sequence, flags);
}

inline void TraceStreamingReceive(MsgOrigin origin, MsgId id,
                                  MsgSequence sequence, MsgFlags flags) {
  core::TraceStreamingReceive(origin, id, sequence, flags);
}

inline ScopedDisableTraceEvents::ScopedDisableTraceEvents() noexcept
    : listener_(internal::active_event_listener()) {
  if (listener_ != nullptr) {
    // Important: we can't simply set the listener to 'nullptr' as the
    // per-thread logic in SyncContext holds the invariant that an active sync
    // context transfers ownership of its listener to the running thread, and
    // the current thread's listener is never zero with an active context.
    internal::set_active_event_listener(NoopListener());
  }
}

inline ScopedDisableTraceEvents::~ScopedDisableTraceEvents() noexcept {
  if (listener_ != nullptr) {
    // Assert ownership did not change.
    DCHECK_EQ(internal::active_event_listener(), NoopListener());
    internal::set_active_event_listener(listener_);
  }
}

inline TraceScopedSuspend::TraceScopedSuspend() noexcept
    : sync_id_(active_sync_id()), listener_(internal::active_event_listener()) {
  if (listener_ != nullptr) {
    listener_->OnTraceSuspendSync(sync_id_);
    internal::set_active_sync_id(kNoSyncId);
    internal::set_active_event_listener(nullptr);
  }
}

inline TraceScopedSuspend::~TraceScopedSuspend() noexcept {
  if (listener_ != nullptr) {
    internal::set_active_sync_id(sync_id_);
    internal::set_active_event_listener(listener_);
    listener_->OnTraceResumeSync(sync_id_);
  }
}

}  // namespace perftools::tracing

#endif  // THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_TRACING_H_
