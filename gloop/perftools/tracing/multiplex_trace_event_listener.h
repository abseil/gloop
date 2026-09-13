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

#ifndef THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_MULTIPLEX_TRACE_EVENT_LISTENER_H_
#define THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_MULTIPLEX_TRACE_EVENT_LISTENER_H_

#include "gloop/perftools/tracing/trace_event_listener.h"
#include "gloop/perftools/tracing/tracing_base.h"

namespace perftools::tracing {

// Multiplexes `first` and `second` into a single TraceEventListener.
//
// Returns a multiplexing instance that delivers events to both `first` and
// `second`, and manages the lifecycle of `first` and `second`. Notably, for
// both `first` and `second`, there is no observable difference between them
// being called directly, or through the multiplexing instance.
//
// If either `first` or `second` is null then it directly returns the remaining
// non-null parameter, or nullptr if both `first` or `second` are null.
//
// Multiplexing multiple event listeners into a single `TraceEventListener`
// instance simplifies tracing event dispatching logic: context implementations
// only need to manage a single listener instance, and dispatch events to
// that single instance, being agnostic to that listener itself being a
// container of listeners or an individual listener instance.
//
// For example, `AddTracer()` and `RemoveTracer()` implementations could be
// written as per the below example code:
//
//   void AddTracer(TraceEventListener* tracer) {
//     listener_ = MultiplexTraceEventListener(listener_, tracer);
//   }
//
//   void RemoveTracer(TraceEventListener* tracer) {
//     if (listener_ == nullptr) return;
//     auto [listener, success] = listener_->CascadingRelease(tracer);
//     if (success) listener_ = listener;
//   }
//
// Multiplexed instances assume that such Add/Remove logic as per the above
// example are done in LIFO order, in which case removing a tracer has O(1)
// complexity. Or in more general terms: multiplexing N event listeners will
// have O(N) complexity and space requirements in conforming use cases.
//
// Multiplexed instances are highly resilient against non-conforming tracers
// such as instances returning nullptr from `GetEventListener()`, in which
// case the multiplexing instance will fold to the remaining side (or nullptr)
// on calls to `GetEventListener()` on the multiplexed instance.
TraceEventListener* MultiplexTraceEventListener(TraceEventListener* first,
                                                TraceEventListener* second);

namespace internal {

// Internal implementation of `MultiplexTraceEventListener`.
// Requires both `first` and `second` to be non-null.
TraceEventListener* MultiplexTraceEventListener(TraceEventListener* first,
                                                TraceEventListener* second);

}  // namespace internal

inline TraceEventListener* MultiplexTraceEventListener(
    TraceEventListener* first, TraceEventListener* second) {
  if (first == nullptr) {
    return second;
  } else if (second == nullptr) {
    return first;
  } else {
    return internal::MultiplexTraceEventListener(first, second);
  }
}

}  // namespace perftools::tracing

#endif  // THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_MULTIPLEX_TRACE_EVENT_LISTENER_H_
