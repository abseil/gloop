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

#ifndef THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_WITH_TRACE_EVENT_LISTENER_H_
#define THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_WITH_TRACE_EVENT_LISTENER_H_

#include "gloop/base/context_access.h"
#include "gloop/base/tracecontext.h"
#include "gloop/perftools/tracing/trace_event_listener.h"
#include "gloop/perftools/tracing/tracing_base.h"

namespace perftools::tracing {

// `WithTraceEventListener` is a scoping class that installs a provided
// `TraceEventListener` instance into the current thread's `TraceContext`.

// `WithTraceEventListener` shall be scoped locally. Heap allocating or
// embedding a `WithTraceEventListener` into a container or as a member
// variable of another class may lead to undefined behavior. The only (rare)
// exception to this is embedding a `WithTraceEventListener` as a member
// into another scoping class with the same strong scoping invariants.
//
// Applications should rarely need to use `WithTraceEventListener` in their
// code and prefer to rely on existing tracing logic such as Dapper PE.
//
// `WithTraceEventListener` is aimed to be used predominantly in core library
// code and unit tests to verify tracing annotations in core library functions.
//
// Example usage:
//
//   TEST(MySynchronization, EmitsOnTraceSignal) {
//     NiceMock<MockTraceEventListener> mock;
//     WithTraceEventListener with(&mock);
//
//     MySynchronization sync;
//     EXPECT_CALL(mock, OnTraceSignal(ToBarrierId(&sync), _));
//     sync.Notify();
//   }
//
class WithTraceEventListener {
 public:
  // Creates a new instance, installing `listener` into the current thread.
  // Takes ownership of the provided listener.
  // This constructor is a no-op if `listener` is null.
  explicit WithTraceEventListener(TraceEventListener* listener);

  // WithTraceEventListener is not copyable or assignable.
  WithTraceEventListener() = delete;
  WithTraceEventListener(const WithTraceEventListener&) = delete;
  WithTraceEventListener& operator=(const WithTraceEventListener&) = delete;

  // Uninstalls the `listener` instance installed in the constructor.
  // This destructor is a no-op if the provided `listener` is null.
  ~WithTraceEventListener();

 private:
  TraceEventListener* const listener_;
};

inline WithTraceEventListener::WithTraceEventListener(
    TraceEventListener* listener)
    : listener_(listener) {
#if BASE_HAVE_TRACECONTEXT
  if (listener_) {
    TraceContext::AddTraceEventListener(base::ContextAccess(), listener_);
  }
#endif
}

inline WithTraceEventListener::~WithTraceEventListener() {
#if BASE_HAVE_TRACECONTEXT
  if (listener_) {
    TraceContext::RemoveTraceEventListener(base::ContextAccess(), listener_);
  }
#endif
}

}  // namespace perftools::tracing

#endif  // THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_WITH_TRACE_EVENT_LISTENER_H_
