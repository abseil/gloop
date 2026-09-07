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

#ifndef THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_NOOP_TRACE_EVENT_LISTENER_H_
#define THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_NOOP_TRACE_EVENT_LISTENER_H_

#include "gloop/perftools/tracing/trace_event_listener.h"

namespace perftools::tracing {

// Returns a global, thread safe noop trace event listener.
TraceEventListener* NoopTraceEventListener();

}  // namespace perftools::tracing

#endif  // THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_NOOP_TRACE_EVENT_LISTENER_H_
