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

// Unit tests for logger.h.
//
// Setting `--minloglevel` or `ABSL_MIN_LOG_LEVEL` should not cause any tests to
// fail, however tests that use a particular severity level but are not
// responsible for validating interactions with `--minloglevel` and/or
// `ABSL_MIN_LOG_LEVEL` will be skipped when the level they use is disabled.
// The intent is that the whole testsuite be run without these options to
// validate ordinary behaviors and again with various settings of these options
// to validate their specific effects on applicable test cases.

#include "gloop/base/logger.h"

#include <ctime>

#include "absl/base/log_severity.h"
#include "absl/flags/flag.h"
#include "absl/log/internal/test_actions.h"
#include "absl/log/internal/test_helpers.h"
#include "absl/log/log.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/config.h"
#include "gloop/base/log_file.h"
#include "gloop/base/log_file_flags.h"
#include "gloop/base/logging_extensions.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace base_logging {
namespace {
using ::absl::log_internal::LoggingEnabledAt;
using ::absl::log_internal::WriteToStderr;
using ::testing::AllOf;
using ::testing::EndsWith;
using ::testing::Ge;
using ::testing::HasSubstr;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Matcher;
using ::testing::StrictMock;
using ::testing::Test;
using ::testing::Truly;
#if GTEST_HAS_DEATH_TEST
using ::absl::log_internal::DiedOfFatal;
#endif

#if GLOOP_INTERNAL_PROD_LOGGING
// With more than one mocker of the same type, test failure messages are hard to
// interpret.  These types are distinct and are printed as such in stacktraces.
class MockInfoLogger : public Logger {
 public:
  MOCK_METHOD(void, Write, (bool, time_t, const char*, size_t), (override));
  MOCK_METHOD(void, Flush, (), (override));
  MOCK_METHOD(size_t, LogSize, (), (override));
};
class MockWarningLogger : public Logger {
 public:
  MOCK_METHOD(void, Write, (bool, time_t, const char*, size_t), (override));
  MOCK_METHOD(void, Flush, (), (override));
  MOCK_METHOD(size_t, LogSize, (), (override));
};
class MockErrorLogger : public Logger {
 public:
  MOCK_METHOD(void, Write, (bool, time_t, const char*, size_t), (override));
  MOCK_METHOD(void, Flush, (), (override));
  MOCK_METHOD(size_t, LogSize, (), (override));
};
class MockFatalLogger : public Logger {
 public:
  MOCK_METHOD(void, Write, (bool, time_t, const char*, size_t), (override));
  MOCK_METHOD(void, Flush, (), (override));
  MOCK_METHOD(size_t, LogSize, (), (override));
};

class LoggerTest : public Test {
 protected:
  LoggerTest()
      : original_info_logger_(GetLogger(absl::LogSeverity::kInfo)),
        original_warning_logger_(GetLogger(absl::LogSeverity::kWarning)),
        original_error_logger_(GetLogger(absl::LogSeverity::kError)),
        original_fatal_logger_(GetLogger(absl::LogSeverity::kFatal)) {
    absl::SetFlag(&FLAGS_logtostderr, false);

    info_logger_ = new StrictMock<MockInfoLogger>;
    warning_logger_ = new StrictMock<MockWarningLogger>;
    error_logger_ = new StrictMock<MockErrorLogger>;
    fatal_logger_ = new StrictMock<MockFatalLogger>;
  }
  ~LoggerTest() override {
    if (!mocks_activated_) {
      delete info_logger_;
      delete warning_logger_;
      delete error_logger_;
      delete fatal_logger_;
      return;
    }
    logging_internal::SetLogger(absl::LogSeverity::kInfo,
                                original_info_logger_);
    logging_internal::SetLogger(absl::LogSeverity::kWarning,
                                original_warning_logger_);
    logging_internal::SetLogger(absl::LogSeverity::kError,
                                original_error_logger_);
    logging_internal::SetLogger(absl::LogSeverity::kFatal,
                                original_fatal_logger_);
  }
  void ActivateMockLoggers() {
    logging_internal::SetLogger(absl::LogSeverity::kInfo, info_logger_);
    logging_internal::SetLogger(absl::LogSeverity::kWarning, warning_logger_);
    logging_internal::SetLogger(absl::LogSeverity::kError, error_logger_);
    logging_internal::SetLogger(absl::LogSeverity::kFatal, fatal_logger_);
    mocks_activated_ = true;
  }

  StrictMock<MockInfoLogger>* info_logger_;
  StrictMock<MockWarningLogger>* warning_logger_;
  StrictMock<MockErrorLogger>* error_logger_;
  StrictMock<MockFatalLogger>* fatal_logger_;
  Logger* original_info_logger_;
  Logger* original_warning_logger_;
  Logger* original_error_logger_;
  Logger* original_fatal_logger_;
  bool mocks_activated_ = false;
};
#if GTEST_HAS_DEATH_TEST
using LoggerDeathTest = LoggerTest;
#endif

// Matches a timestamp if it falls after the instantiation of this matcher and
// before its execution, as is normal when used with EXPECT_CALL.
Matcher<time_t> TimeTInMatchWindow() {
  return AllOf(Ge(absl::ToTimeT(absl::Now())), Truly([](time_t arg) {
                 return arg <= absl::ToTimeT(absl::Now());
               }));
}

TEST_F(LoggerTest, WritesInfo) {
  if (LoggingEnabledAt(absl::LogSeverity::kInfo)) {
    EXPECT_CALL(*info_logger_,
                Write(/* force_flush = */ IsFalse(), TimeTInMatchWindow(),
                      EndsWith("hello world\n"), Ge(13)));
  }
  ActivateMockLoggers();
  LOG(INFO) << "hello world";
}

TEST_F(LoggerTest, FlushesInfo) {
  EXPECT_CALL(*info_logger_, Flush());
  EXPECT_CALL(*warning_logger_, Flush());
  EXPECT_CALL(*error_logger_, Flush());
  EXPECT_CALL(*fatal_logger_, Flush());
  ActivateMockLoggers();
  FlushLogFiles(absl::LogSeverity::kInfo);
}

TEST_F(LoggerTest, FlushesWarning) {
  EXPECT_CALL(*warning_logger_, Flush());
  EXPECT_CALL(*error_logger_, Flush());
  EXPECT_CALL(*fatal_logger_, Flush());
  ActivateMockLoggers();
  FlushLogFiles(absl::LogSeverity::kWarning);
}

TEST_F(LoggerTest, FlushesError) {
  EXPECT_CALL(*error_logger_, Flush());
  EXPECT_CALL(*fatal_logger_, Flush());
  ActivateMockLoggers();
  FlushLogFiles(absl::LogSeverity::kError);
}

TEST_F(LoggerTest, FlushesFatal) {
  EXPECT_CALL(*fatal_logger_, Flush());
  ActivateMockLoggers();
  FlushLogFiles(absl::LogSeverity::kFatal);
}
TEST_F(LoggerTest, Nullptr) {
  EnableLogToFiles(false);
  FlushLogFiles(absl::LogSeverity::kInfo);
  EnableLogToFiles(true);
}
#endif  // GLOOP_INTERNAL_PROD_LOGGING

}  // namespace
}  // namespace base_logging
