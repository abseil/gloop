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

#ifndef THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_TRACING_BASE_H_
#define THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_TRACING_BASE_H_

#include <atomic>
#include <cstdint>
#include <iosfwd>

#include "absl/base/attributes.h"
#include "absl/base/casts.h"
#include "absl/base/config.h"
#include "absl/log/check.h"

namespace perftools::tracing {

class TraceEventListener;

// `SyncId` is a dense identifier used to identify specific synchronous code
// executions. `SyncId` values are only required to be unique inside a single
// causality trace graph or dapper trace inside a specific single process.
// `SyncId` values are generated using trivial atomic ops.
using SyncId = uint32_t;

// `TraceSpanId` identifies `trace spans`. A span typically adds hierarchy to
// distributed executions. The tracing library is agnostic to exactly 'what' a
// span is and only provides the API and state to manage the state for a
// synchronous execution. The span associated with the current synchronous
// execution can be be observed through the `active_span_id()` function. In
// Dapper, the span id is the same as the `rpc_id` of the active trace context.
using TraceSpanId = uint64_t;

// `MsgId` is used to identify `messages`. Typically (but not necessarily) these
// are the `rpc_id` values of RPC requests, responses and streaming messages.
// `MsgId` values should be globally unique values.
using MsgId = uint64_t;

// `MsgSequence` can be used to identify sequences of messages that are part of
// a single logical message. Sequence values should be dense, and ideally form a
// closed monotonic series, i.e., {1, 2, ...., N}.
using MsgSequence = uint32_t;

// `EndPoint` identifies the endpoint of a client/server session.
// Currently we only identify streaming client/server endpoints, but this
// is subject to change in the future.
enum class EndPoint {
  kStreamingClient = 1,  // Client side streaming session.
  kStreamingServer = 2,  // Server side streaming session.
};

// `MsgFlags` defines possible message specific bit flags that may be set
// on message related events. We currently only define flags that apply to
// streaming message events, but this may change in the future where flags
// may be defined for other message related events such as unary RPCs.
enum class MsgFlags {
  // Default / empty flag value
  kNone = 0,
  kDefault = 0,

  // Set when the sender has closed their half of the streaming session,
  // and no more streaming message will be sent.
  kHalfClose = 1,

  // Set on streaming messages without a payload, i.e.; a pure control message.
  kControl = 2,
};

// `MsgOrigin` describes at which end of a client/server session a given
// message originated. For example: with unary RPCs, a 'Client' message is
// typically also referred to as a 'Request' and a 'Server' message as a
// 'Response'. For other protocols such as streaming messages, the notion of
// 'Client' and 'Server' originated messages avoids such 'Request/Response'
// terminology which can be rather ambiguous for these protocols.
enum class MsgOrigin : uint16_t { kClient = 1, kServer = 2 };

// Convenience logical operations on streaming flags.
MsgFlags operator&(MsgFlags lhs, MsgFlags rhs);
MsgFlags operator|(MsgFlags lhs, MsgFlags rhs);

// Returns true if `lhs` contains all bits set in `rhs`
bool HasFlags(MsgFlags lhs, MsgFlags rhs);

// `BarrierId` identifies a waitable object. `BarrierId` values are typically
// the address of the underlying synchronization object.
using BarrierId = uint64_t;

// `ControlFlowType` defines the type of a control flow event. Typically control
// flow events come in pairs, with a causality from the first event (schedule)
// to the last event (continuation).
//
// ControlFlow annotations can be used where code depends on library code or
// servers that don't provide causality information. For example, streaming
// RPCs do not have an implied causality as they can be some application
// defined `N:M` model.
//
// Control flow events have a `Schedule` --> `Continue` causality, i.e., for
// critical path computations, causality is back-tracked from a `Continue` to a
// corresponding `Schedule` event. Control flows can have a sequence number to
// facilitate multiple `Schedule` and `Continue` events.
enum class ControlFlowType : uint16_t {
  // Default value for undefined or unknown flow types.
  kUndefined,

  // Generic control flow: there is a causal edge defined from the first to the
  // second event in the set of time-ordered pairs of Generic Flow events with
  // the same cooperative id. E.g., for a set of Flow events with the same coop
  // id, in time order: A, B, C, and D. We expect the causal edges (A->B) and
  // (C->D) in the causal graph.
  // Applications should prefer to use explicit `Schedule` and `Continue` flow
  // types to avoid ambiguity from the timing of possibly overlapping events.
  kGeneric = 1,

  // Control flow schedule: there is a causal edge defined to a corresponding
  // `Continue` event with the same ControlFlowId value. Schedule and Continue
  // events should be strictly paired. Future versions may support additional
  // N:M Schedule -> Continue annotations. Having more than one Schedule or
  // Continue event with the same ControlFlowId is experimental. The current
  // default behavior for critical path computation is that the last Continue
  // event is matched to the first Schedule event.
  kSchedule = 2,

  // Control flow continue: continuation of a control flow schedule event.
  kContinue = 3,

  // Start and End mark the beginning and end of some local work performed by a
  // server on behalf of a remote client such as one or more streaming messages.
  // The Start and End events are optional, and intended to reflect an implied
  // client --> server causality. Simplified, the server's Start, End span of
  // execution reflects the execution scoped by Schedule, Continue events.
  kStart = 4,
  kEnd = 5,
};

// `ControlFlowId` identifies an explicitly annotated control flow causality.
using ControlFlowId = uint64_t;

// `ControlFlowSequence` adds optional sequence numbers to ControlFlow events.
// Control flow sequence numbers are an experimental feature and subject to
// change. Applications should leave control flow sequences zero.
using ControlFlowSequence = uint32_t;

// Predefined constants
inline constexpr SyncId kNoSyncId = {0};
inline constexpr SyncId kMainSyncId = {1};
inline constexpr BarrierId kNoBarrierId = {0};
inline constexpr MsgSequence kNoMsgSequence = static_cast<uint32_t>(-1);

// Returns a `BarrierId` value computed from the passed in address value,
// or `kNoBarrierId` if the passed in `object` is nullptr.
//
// This is a convenience helper to create a barrier id value from a unique
// address: the address of an object uniquely identifies that object.
// This function is deterministic but not canonical: the function guarantees
// that any two distinct address values inside the same process will result
// in two distinct barrier id values, but the bit pattern of the returned value
// is undefined and may change over time or per compiled binary version.
BarrierId ToBarrierId(const void* object);

// Returns true if the current thread has an active causality trace.
bool has_sync_trace();

// Returns the 'synchronous execution id' identifying the executing code
// of the current thread, or kNoSyncId if no causality trace is active.
SyncId active_sync_id();

// Returns the active span id of the current 'synchronous execution, or 0 if if
// no causality trace is active or no span is associated with the currently
// traced execution.
TraceSpanId active_trace_span_id();

// Streaming support for debugging / logging purposes.
std::ostream& operator<<(std::ostream&, MsgOrigin);
std::ostream& operator<<(std::ostream&, ControlFlowType);
std::ostream& operator<<(std::ostream&, EndPoint);
std::ostream& operator<<(std::ostream&, MsgFlags);

namespace internal {

// We co-locate our TLS variables
struct TracePerThreadData {
  SyncId active_sync_id;
  TraceEventListener* active_event_listener;
  TraceSpanId active_span_id;
};

ABSL_CONST_INIT extern std::atomic<bool> use_regions;
ABSL_CONST_INIT extern thread_local TracePerThreadData per_thread;

// Random salt for 'barrier id from pointer' evaluations.
// See <link> for motivation.
extern std::atomic<uint64_t> barrier_salt;

// Sets the 'synchronous execution id' for the current thread.
inline void set_active_sync_id(SyncId sync_id) {
  per_thread.active_sync_id = sync_id;
}

// Returns the active trace event listener for the current thread or nullptr.
inline TraceEventListener* active_event_listener() {
  return per_thread.active_event_listener;
}

// Sets the 'trace_span id' for the current thread.
inline void set_active_trace_span_id(TraceSpanId span_id) {
  per_thread.active_span_id = span_id;
}

// Sets the active trace event listener for the current thread.
inline void set_active_event_listener(TraceEventListener* listener) {
  per_thread.active_event_listener = listener;
}

// Returns true if BEGIN_REGION / END_REGION  should be used for (nested)
// region annotations. This is temporary setting allowing users to opt out
// when we flip the default to true using `--nouse_dapper_pe_regions
// TODO: b/414786755 - remove
inline bool UseRegions() { return use_regions.load(std::memory_order_relaxed); }

}  // namespace internal

inline BarrierId ToBarrierId(const void* object) {
  static_assert(sizeof(object) <= sizeof(uint64_t));
  uint64_t id = absl::bit_cast<uintptr_t>(object);
  // Assert that a non-null value never becomes zero, i.e.: id <> salt.
  DCHECK_NE(id, internal::barrier_salt.load(std::memory_order_relaxed));
  return id ? id ^ internal::barrier_salt.load(std::memory_order_relaxed) : 0;
}

inline SyncId active_sync_id() { return internal::per_thread.active_sync_id; }
inline bool has_sync_trace() {
  return internal::active_event_listener() != nullptr;
}

inline TraceSpanId active_trace_span_id() {
  return internal::per_thread.active_span_id;
}

inline MsgFlags operator&(MsgFlags lhs, MsgFlags rhs) {
  return static_cast<MsgFlags>(static_cast<int>(lhs) & static_cast<int>(rhs));
}

inline MsgFlags operator|(MsgFlags lhs, MsgFlags rhs) {
  return static_cast<MsgFlags>(static_cast<int>(lhs) | static_cast<int>(rhs));
}

inline bool HasFlags(MsgFlags lhs, MsgFlags rhs) { return (lhs & rhs) == rhs; }

}  // namespace perftools::tracing

#endif  // THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_TRACING_BASE_H_
