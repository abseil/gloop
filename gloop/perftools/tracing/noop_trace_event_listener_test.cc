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

#include "gloop/perftools/tracing/noop_trace_event_listener.h"

#include "gloop/perftools/tracing/trace_event_listener.h"
#include "gloop/perftools/tracing/tracing_base.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::Ne;

namespace perftools::tracing {
namespace {

TEST(NoopTraceEventListener, GetAndRelease) {
  TraceEventListener* listener = NoopTraceEventListener();
  ASSERT_THAT(listener, Ne(nullptr));
  listener->ReleaseEventListener();
}

TEST(NoopTraceEventListener, GetEventListener) {
  TraceEventListener* parent = NoopTraceEventListener();
  ASSERT_THAT(parent, Ne(nullptr));
  TraceEventListener* listener = parent->GetEventListener(SyncId(1));
  ASSERT_THAT(listener, Ne(nullptr));
  listener->ReleaseEventListener();
}

}  // namespace
}  // namespace perftools::tracing
