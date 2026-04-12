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

#include "gloop/perftools/tracing/with_trace_event_listener.h"

#include "gloop/base/tracecontext.h"
#include "gloop/gloop_test.h"
#include "gloop/perftools/tracing/mock_trace_event_listener.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/tracing.h"
#include "gloop/perftools/tracing/tracing_base.h"

namespace perftools::tracing {
namespace {

using ::testing::_;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::NiceMock;

TEST(WithTraceEventHandlerTest, Basics) {
  InSequence in_sequence;
  NiceMock<MockTraceEventListener> mock;
  EXPECT_CALL(mock, OnTraceBeginSync(kMainSyncId, _));
  {
    WithTraceEventListener with(&mock);
    EXPECT_THAT(internal::active_event_listener(), Eq(&mock));
    EXPECT_TRUE(TraceContext::Current()->ContainsTraceEventListener(&mock));
    EXPECT_CALL(mock, OnTraceEndSync(kMainSyncId));
    EXPECT_CALL(mock, ReleaseEventListener());
  }
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  EXPECT_FALSE(TraceContext::Current()->ContainsTraceEventListener(&mock));
}

TEST(WithTraceEventHandlerTest, NoopOnNullptr) {
  WithTraceEventListener with(nullptr);
  TraceMark("Into the void");
}

class MySynchronization {
 public:
  void Notify() { TraceSignal(this); }
  void Notify(StringRef label) { TraceSignal(this, label); }
};

TEST(ExampleFromHeaderDocumentation, EmitsOnTraceSignal) {
  NiceMock<MockTraceEventListener> mock;
  WithTraceEventListener with(&mock);

  MySynchronization sync;
  EXPECT_CALL(mock, OnTraceSignal(ToBarrierId(&sync), _));
  EXPECT_CALL(mock, OnTraceSignal(ToBarrierId(&sync), Eq("MyLabel")));
  sync.Notify();
  sync.Notify("MyLabel");
}

}  // namespace
}  // namespace perftools::tracing
