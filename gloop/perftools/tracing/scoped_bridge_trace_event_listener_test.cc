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

#include "absl/base/log_severity.h"
#include "absl/log/scoped_mock_log.h"
#include "absl/strings/string_view.h"
#include "gloop/perftools/tracing/mock_trace_event_listener.h"
#include "gloop/perftools/tracing/trace_event_listener.h"
#include "gloop/perftools/tracing/tracing_base.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::absl::LogSeverity;
using ::absl::ScopedMockLog;
using ::testing::_;
using ::testing::Eq;
using ::testing::Ne;
using ::testing::Return;
using ::testing::StrictMock;

namespace perftools::tracing {
namespace {

MATCHER_P(EqSourceLocation, line, "") {
  return arg.file_name() == absl::string_view(__FILE__) && arg.line() == line;
}

class ScopedBridgeTraceEventListenerTest : public ::testing::Test {
 public:
  ScopedBridgeTraceEventListenerTest() {
    EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
    internal::set_active_event_listener(nullptr);
  }

  ~ScopedBridgeTraceEventListenerTest() {
    internal::set_active_event_listener(nullptr);
  }
};

TEST_F(ScopedBridgeTraceEventListenerTest, DoesNothingWithoutCurrentListener) {
  ScopedBridgeTraceEventListener scoped("Label");
  EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
}

TEST_F(ScopedBridgeTraceEventListenerTest, CapturesLabel) {
  StrictMock<MockTraceEventListener> mock;
  internal::set_active_event_listener(&mock);

  EXPECT_CALL(mock, GetBridgingEventListener(Eq("Label")));
  ScopedBridgeTraceEventListener scoped("Label");
}

TEST_F(ScopedBridgeTraceEventListenerTest, CapturesCurrentSourceLocation) {
  StrictMock<MockTraceEventListener> mock;
  internal::set_active_event_listener(&mock);

  EXPECT_CALL(mock, GetBridgingEventListener(EqSourceLocation(__LINE__ + 1)));
  ScopedBridgeTraceEventListener scoped;
}

TEST_F(ScopedBridgeTraceEventListenerTest, WithNonBridgingCurrentListener) {
  StrictMock<MockTraceEventListener> mock;
  internal::set_active_event_listener(&mock);
  {
    EXPECT_CALL(mock, GetBridgingEventListener(_));
    ScopedBridgeTraceEventListener scoped;
    EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
  }
  EXPECT_THAT(internal::active_event_listener(), Eq(&mock));
}

TEST_F(ScopedBridgeTraceEventListenerTest, WithBridgingCurrentListener) {
  StrictMock<MockTraceEventListener> mock1, mock2;
  internal::set_active_event_listener(&mock1);
  {
    EXPECT_CALL(mock1, GetBridgingEventListener(_)).WillOnce(Return(&mock2));
    ScopedBridgeTraceEventListener scoped;
    EXPECT_THAT(internal::active_event_listener(), Eq(&mock2));
    EXPECT_CALL(mock2, ReleaseEventListener());
  }
  EXPECT_THAT(internal::active_event_listener(), Eq(&mock1));
}

TEST_F(ScopedBridgeTraceEventListenerTest, WithSelfBridgingCurrentListener) {
  StrictMock<MockTraceEventListener> mock;
  internal::set_active_event_listener(&mock);
  {
    EXPECT_CALL(mock, GetBridgingEventListener(_)).WillOnce(Return(&mock));
    ScopedBridgeTraceEventListener scoped;
    EXPECT_THAT(internal::active_event_listener(), Eq(&mock));
    EXPECT_CALL(mock, ReleaseEventListener());
  }
  EXPECT_THAT(internal::active_event_listener(), Eq(&mock));
}

TEST_F(ScopedBridgeTraceEventListenerTest, LogsErrorIfListenerChanged) {
  StrictMock<MockTraceEventListener> mock1, mock2;
  internal::set_active_event_listener(&mock1);
  ScopedMockLog log;
  log.StartCapturingLogs();
  {
    EXPECT_CALL(mock1, GetBridgingEventListener(_));
    ScopedBridgeTraceEventListener scoped;
    EXPECT_THAT(internal::active_event_listener(), Eq(nullptr));
    internal::set_active_event_listener(&mock2);
    EXPECT_CALL(log, Log(LogSeverity::kError, _, "Active listener changed"));
  }
  // Restored listener should now multiplex mock1 and mock2
  ASSERT_THAT(internal::active_event_listener(), Ne(nullptr));
  EXPECT_CALL(mock1, ReleaseEventListener());
  EXPECT_CALL(mock2, ReleaseEventListener());
  internal::active_event_listener()->ReleaseEventListener();
}

auto ReturnExtracted(TraceEventListener* listener, bool success) {
  return Return(std::pair<TraceEventListener*, bool>(listener, success));
}

TEST_F(ScopedBridgeTraceEventListenerTest, RobustAgainstAddedListener2) {
  StrictMock<MockTraceEventListener> mock1, mock2, mock3, mock4;
  internal::set_active_event_listener(&mock1);
  {
    EXPECT_CALL(mock1, GetBridgingEventListener(_)).WillOnce(Return(&mock2));
    ScopedBridgeTraceEventListener scoped;
    internal::set_active_event_listener(&mock3);
    EXPECT_CALL(mock3, Extract(&mock2)).WillOnce(ReturnExtracted(&mock4, true));
    EXPECT_CALL(mock2, ReleaseEventListener());
  }
  // Restored listener should now multiplex mock1 and mock4
  ASSERT_THAT(internal::active_event_listener(), Ne(nullptr));
  EXPECT_CALL(mock1, ReleaseEventListener());
  EXPECT_CALL(mock4, ReleaseEventListener());
  internal::active_event_listener()->ReleaseEventListener();
}

TEST_F(ScopedBridgeTraceEventListenerTest, RobustAgainstStolenBridge) {
  StrictMock<MockTraceEventListener> mock1, mock2;
  internal::set_active_event_listener(&mock1);
  {
    EXPECT_CALL(mock1, GetBridgingEventListener(_)).WillOnce(Return(&mock2));
    ScopedBridgeTraceEventListener scoped;
    internal::set_active_event_listener(nullptr);
  }
  EXPECT_THAT(internal::active_event_listener(), Eq(&mock1));
}

TEST_F(ScopedBridgeTraceEventListenerTest, RobustAgainstReplacedBridge) {
  StrictMock<MockTraceEventListener> mock1, mock2, mock3;
  internal::set_active_event_listener(&mock1);
  {
    EXPECT_CALL(mock1, GetBridgingEventListener(_)).WillOnce(Return(&mock2));
    ScopedBridgeTraceEventListener scoped;
    internal::set_active_event_listener(&mock3);
    EXPECT_CALL(mock3, Extract(&mock2))
        .WillOnce(ReturnExtracted(&mock3, false));
  }
  // Restored listener should now multiplex mock1 and mock3
  ASSERT_THAT(internal::active_event_listener(), Ne(nullptr));
  EXPECT_CALL(mock1, ReleaseEventListener());
  EXPECT_CALL(mock3, ReleaseEventListener());
  internal::active_event_listener()->ReleaseEventListener();
}
}  // namespace
}  // namespace perftools::tracing
