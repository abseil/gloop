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

#ifndef THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_SCOPED_BRIDGE_TRACE_EVENT_LISTENER_H_
#define THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_SCOPED_BRIDGE_TRACE_EVENT_LISTENER_H_

#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/trace_event_listener.h"
#include "gloop/perftools/tracing/trace_source_location.h"

namespace perftools::tracing {

// `ScopedBridgeTraceEventListener` replaces installed trace event listeners
// with their bridging version for the code scoped by this class.
//
// See `TraceEventListener::GetBridgingEventListener()` for more information
// on bridging and how individual `TraceEventListener` instances can decide
// to participate in bridging tracing contexts.
//
// Applications should not use this class directly. This class is intended to
// be used exclusively by (Dapper) tracing APIs such as `LinkedTraceSpan`.
//
// A possible implementation could look as follows:
//
//   class LinkedTraceSpan {
//    public:
//     LinkedTraceSpan(..., source_location) {
//       TraceContext tc = CreateNewTraceContext();
//
//       const TraceContext& current = base::CurrentContext().trace_context();
//       if (current.is_traced()) {
//         if (tc.is_traced() && tc.global_id() != current.global_id()) {
//           scoped_bridge_.emplace(source_location);
//           tc.CopySyncContextFrom(current);
//         }
//       }
//       with_trace_context_.emplace(std::move(tc));
//     }
//
//    private:
//     std::optional<ScopedBridgeTraceEventListener> scoped_bridge_;
//     std::optional<WithTraceContext> with_trace_context_;
//   };
//
class ScopedBridgeTraceEventListener {
 public:
  // Installs a bridging TraceEventListener for the active trace event listener:
  // - captures the current active trace event listener.
  // - obtains and installs the (optional) bridging listener active listener.
  //
  // The 'nullness' of the actively installed trace event listener is preserved.
  // I.e.: if the active trace event listener is null, then it will remain null.
  // If the current active trace event listener is non-null, then the listener
  // installed for this scope will be non-null as well to preserve tracing
  // in-variants. Practically this means that if the bridge returned by the
  // active listener is null, then `ScopedBridgeTraceEventListener` installs
  // a `NoopTraceEventListener` for the lifetime of the scope.
  explicit ScopedBridgeTraceEventListener(
      StringRef label = TraceSourceLocation::current());

  // Restores the original trace event listener captured at construction.
  // Will log an error if the active trace event listener changed between the
  // constructor and the destructor. Applications should guarantee that no
  // trace event listeners are left installed or removed at the end of the
  // scope. Such a condition could arise when applications use unsupported
  // direct `TraceContext` manipulation functions.
  ~ScopedBridgeTraceEventListener();

  // ScopedBridgeTraceEventListener is not copyable or assignable.
  ScopedBridgeTraceEventListener(const ScopedBridgeTraceEventListener&) =
      delete;
  ScopedBridgeTraceEventListener& operator=(
      const ScopedBridgeTraceEventListener&) = delete;

  // Returns true if an active bridging trace event listener was installed.
  bool active() const { return active_; }

 private:
  TraceEventListener* const previous_;
  bool active_ = false;
  TraceEventListener* const bridged_;
};

}  // namespace perftools::tracing

#endif  // THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_SCOPED_BRIDGE_TRACE_EVENT_LISTENER_H_
