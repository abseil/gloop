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

#include "gloop/perftools/tracing/scoped_bridge_trace_event_listener.h"

#include <utility>

#include "absl/log/log.h"
#include "gloop/perftools/tracing/multiplex_trace_event_listener.h"
#include "gloop/perftools/tracing/noop_trace_event_listener.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/trace_event_listener.h"
#include "gloop/perftools/tracing/tracing_base.h"

namespace perftools::tracing {

namespace {

// Extracts `listener` from `active`.
//
// Returns `{<new_active>, true}` on success, `{active, false}` if
// `listener` is not equal to, or embedded inside `active`.
std::pair<TraceEventListener*, bool> Extract(TraceEventListener* active,
                                             TraceEventListener* listener) {
  if (listener == nullptr) return {active, false};
  if (listener == active) return {nullptr, true};
  if (active == nullptr) return {nullptr, false};
  return active->Extract(listener);
}

// Returns the bridging listener for `listener`. If `listener` is null, then
// this functions returns null, else the function will return a non-null
// listener which is either a bridging listener or a no-op listener.
// `active' is set to true if a bridging listener is installed.
TraceEventListener* Bridge(TraceEventListener* listener, StringRef label,
                           bool& active) {
  if (listener == nullptr) return nullptr;
  listener = listener->GetBridgingEventListener(label);
  if (listener == nullptr) return NoopTraceEventListener();
  active = true;
  return listener;
}

}  // namespace

ScopedBridgeTraceEventListener::ScopedBridgeTraceEventListener(StringRef label)
    : previous_(internal::active_event_listener()),
      bridged_(Bridge(previous_, label, active_)) {
  internal::set_active_event_listener(bridged_);
}

ScopedBridgeTraceEventListener::~ScopedBridgeTraceEventListener() {
  // We expect the listener state to not be changed inside the scope.
  TraceEventListener* active = internal::active_event_listener();
  LOG_IF(ERROR, active != bridged_) << "Active listener changed";

  // Extract the bridge, this should succeed, but it is robust against badness.
  auto [remaining, extracted] = Extract(active, bridged_);
  if (extracted) bridged_->ReleaseEventListener();

  // Restore the replaced listener, again, being robust against badness.
  active = MultiplexTraceEventListener(previous_, remaining);
  internal::set_active_event_listener(active);
}

}  // namespace perftools::tracing
