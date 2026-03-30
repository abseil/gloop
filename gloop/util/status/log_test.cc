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

#include "gloop/util/status/log.h"

#include "absl/log/scoped_mock_log.h"
#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using ::absl::ScopedMockLog;
using ::testing::_;
using ::testing::HasSubstr;

TEST(ErrorsTest, ElseBlockerWorks) {
  // NOTE: This test just makes sure that the code compiles.
  // LOG_IF_ERROR expands to an if statement itself, the else blocker makes sure
  // the compiler doesn't complain.
  // See //gloop/util/task/status_macros.h;l=274;rcl=599520712.
  if (true) LOG_IF_ERROR(INFO, absl::OkStatus());
}

absl::Status NoError() { return absl::OkStatus(); }

TEST(ErrorsTest, DoesNotLogOnSuccess) {
  ScopedMockLog log;
  EXPECT_CALL(log, Log).Times(0);
  log.StartCapturingLogs();
  LOG_IF_ERROR(INFO, NoError());
}

absl::Status FooError() { return absl::InternalError("Foo"); }

TEST(ErrorsTest, LogOnFailure) {
  ScopedMockLog log;
  EXPECT_CALL(log, Log(_, _, HasSubstr("Foo")));
  log.StartCapturingLogs();
  LOG_IF_ERROR(INFO, FooError());
}

TEST(ErrorsTest, LogExtraMessage) {
  ScopedMockLog log;
  EXPECT_CALL(log, Log(_, _, HasSubstr("Bar")));
  log.StartCapturingLogs();
  LOG_IF_ERROR(INFO, FooError()) << "at Bar.";
}

TEST(ErrorsTest, PrependMessage) {
  ScopedMockLog log;
  EXPECT_CALL(log, Log(_, _, HasSubstr("INTERNAL: Fun: Foo")));
  log.StartCapturingLogs();
  LOG_IF_ERROR(INFO, FooError()).SetPrepend() << "Fun: ";
}

}  // namespace
