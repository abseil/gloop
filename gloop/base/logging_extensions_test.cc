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

// Unit tests for logging_extensions.h.
//
// This testsuite consists of the following:
//
// * Tests for structured logging helpers.
// * Tests for logging stats.
// * Tests for `absl::LogSink` implementations.
// * Tests for structured logging overflow behavior.
// * Tests for exit-time hook execution.
// * Tests for integration with `base::CrashReason` API.
// * Tests for stack usage.
// * Miscellaneous tests.
//
// Setting `--minloglevel` or `ABSL_MIN_LOG_LEVEL` should not cause any tests to
// fail, however tests that use a particular severity level but are not
// responsible for validating interactions with `--minloglevel` and/or
// `ABSL_MIN_LOG_LEVEL` will be skipped when the level they use is disabled.
// The intent is that the whole testsuite be run without these options to
// validate ordinary behaviors and again with various settings of these options
// to validate their specific effects on applicable test cases.

#include "gloop/base/logging_extensions.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/log_severity.h"
#include "absl/container/flat_hash_map.h"
#include "absl/debugging/stacktrace.h"
#include "absl/debugging/symbolize.h"
#include "absl/log/check.h"
#include "absl/log/internal/test_helpers.h"
#include "absl/log/log.h"
#include "absl/log/log_entry.h"
#include "absl/log/log_sink.h"
#include "absl/log/scoped_mock_log.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "gloop/base/config.h"
#include "gloop/thread/threadpool.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#ifdef _POSIX_VERSION
#include "gloop/base/process_state.h"
#endif

#if BASE_HAVE_CRASHREASON
#include "gloop/base/crash.h"
#endif

namespace {
using ::absl::log_internal::LoggingEnabledAt;
using ::testing::AllOf;
using ::testing::AnyOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Matcher;
using ::testing::Property;
using ::testing::Test;
#if GTEST_HAS_DEATH_TEST
using ::absl::log_internal::DiedOfFatal;
#endif

class LoggingTest : public Test {
 protected:
  LoggingTest() : sink_(absl::MockLogDefault::kDisallowUnexpected) {
    sink_.StartCapturingLogs();
  }

  absl::ScopedMockLog sink_;
};

// Tests for logging stats.
// ------------------------

// A `CapturedLogStats` captures the values of the logging stats counters at
// construction time so that assertions can be made afterward about the amount
// of any increases in their values.  For example:
//
//   CapturedLogStats stats;
//   LOG(INFO) << "YOU CAN'T CUT BACK ON FUNDING! YOU WILL REGRET THIS!";
//   EXPECT_THAT(stats, NewMessages({{absl::LogSeverity::kInfo, 1}}));
class CapturedLogStats {
 public:
  CapturedLogStats()
      : messages_baseline_{
            {base_logging::LoggedMessages(absl::LogSeverity::kInfo),
             base_logging::LoggedMessages(absl::LogSeverity::kWarning),
             base_logging::LoggedMessages(absl::LogSeverity::kError)}},
        bytes_baseline_{
            {base_logging::LoggedBytes(absl::LogSeverity::kInfo),
             base_logging::LoggedBytes(absl::LogSeverity::kWarning),
             base_logging::LoggedBytes(absl::LogSeverity::kError)}} {
    // These are only ever non-zero during `~LogMessage()`.
    CHECK_EQ(base_logging::LoggedMessages(absl::LogSeverity::kFatal), 0ul);
    CHECK_EQ(base_logging::LoggedBytes(absl::LogSeverity::kFatal), 0ul);
  }
  // These methods return the number of new messages or bytes counted since
  // construction.
  size_t NewInfoMessages() const {
    return base_logging::LoggedMessages(absl::LogSeverity::kInfo) -
           messages_baseline_[static_cast<int>(absl::LogSeverity::kInfo)];
  }
  size_t NewWarningMessages() const {
    return base_logging::LoggedMessages(absl::LogSeverity::kWarning) -
           messages_baseline_[static_cast<int>(absl::LogSeverity::kWarning)];
  }
  size_t NewErrorMessages() const {
    return base_logging::LoggedMessages(absl::LogSeverity::kError) -
           messages_baseline_[static_cast<int>(absl::LogSeverity::kError)];
  }
  size_t NewInfoBytes() const {
    return base_logging::LoggedBytes(absl::LogSeverity::kInfo) -
           bytes_baseline_[static_cast<int>(absl::LogSeverity::kInfo)];
  }
  size_t NewWarningBytes() const {
    return base_logging::LoggedBytes(absl::LogSeverity::kWarning) -
           bytes_baseline_[static_cast<int>(absl::LogSeverity::kWarning)];
  }
  size_t NewErrorBytes() const {
    return base_logging::LoggedBytes(absl::LogSeverity::kError) -
           bytes_baseline_[static_cast<int>(absl::LogSeverity::kError)];
  }

 private:
  // `kFatal` counters are omitted because they are nearly impossible (actually
  // impossible in some cases) to sample in the interval between when they are
  // incremented and when the process dies.
  const std::vector<size_t> messages_baseline_;
  const std::vector<size_t> bytes_baseline_;
};

// Tests for `absl::LogSink` implementations.
// ------------------------------------------

class LogSinkImplTest : public Test {
 protected:
  void SetUp() override {
    // These tests use `INFO` severity but are not responsible for validating
    // the effects of disabling it with `--minloglevel` or `ABSL_MIN_LOG_LEVEL`.
    if (!LoggingEnabledAt(absl::LogSeverity::kInfo))
      GTEST_SKIP() << "LogSinkImplTests skipped since INFO logging is disabled";
  }
};

using CopyToStringSinkTest = LogSinkImplTest;

TEST_F(CopyToStringSinkTest, Null) {
  base_logging::CopyToStringSink sink(nullptr);
  // This should not crash:
  LOG(INFO).ToSinkOnly(&sink) << "hello world";
}

TEST_F(CopyToStringSinkTest, NonNull) {
  std::string str;
  base_logging::CopyToStringSink sink(&str);
  LOG(INFO).ToSinkOnly(&sink) << "hello world";
  if (LoggingEnabledAt(absl::LogSeverity::kError)) {
    EXPECT_THAT(str, Eq("hello world"));
  } else {
    EXPECT_THAT(str, Eq(""));
  }
}

TEST_F(CopyToStringSinkTest, Concurrency) {
  std::string str;
  base_logging::CopyToStringSink sink(&str);

  // TSAN can only detect concurrency problems when a test is multithreaded.
  int threads = 2;
  {
    ThreadPool pool(threads);

    pool.Schedule(
        [&sink]() { LOG(INFO).ToSinkOnly(&sink) << "[from one callback]"; });
    pool.Schedule([&sink]() {
      LOG(INFO).ToSinkOnly(&sink) << "[from another callback]";
    });
  }  // ThreadPool destructor blocks until all jobs are done.

  if (LoggingEnabledAt(absl::LogSeverity::kInfo)) {
    EXPECT_THAT(
        str, AnyOf(Eq("[from one callback]"), Eq("[from another callback]")));
  }
}

using AppendToVectorSinkTest = LogSinkImplTest;

TEST_F(AppendToVectorSinkTest, Null) {
  base_logging::AppendToVectorSink sink(nullptr);
  // This should not crash:
  LOG(INFO).ToSinkOnly(&sink) << "hello world";
}

TEST_F(AppendToVectorSinkTest, NonNull) {
  std::vector<std::string> vec;
  base_logging::AppendToVectorSink sink(&vec);
  LOG(INFO).ToSinkOnly(&sink) << "hello";
  LOG(ERROR).ToSinkOnly(&sink) << "world";
  if (LoggingEnabledAt(absl::LogSeverity::kError)) {
    EXPECT_THAT(vec, ElementsAre("hello", "world"));
  } else if (LoggingEnabledAt(absl::LogSeverity::kInfo)) {
    EXPECT_THAT(vec, ElementsAre("hello"));
  } else {
    EXPECT_THAT(vec, IsEmpty());
  }
}

TEST_F(AppendToVectorSinkTest, Concurrency) {
  std::vector<std::string> vec;
  base_logging::AppendToVectorSink sink(&vec);

  // TSAN can only detect concurrency problems when a test is multithreaded.
  int threads = 2;
  {
    ThreadPool pool(threads);

    pool.Schedule(
        [&sink]() { LOG(INFO).ToSinkOnly(&sink) << "[from one callback]"; });
    pool.Schedule([&sink]() {
      LOG(INFO).ToSinkOnly(&sink) << "[from another callback]";
    });
  }  // ThreadPool destructor blocks until all jobs are done.

  if (LoggingEnabledAt(absl::LogSeverity::kInfo)) {
    EXPECT_THAT(vec, testing::UnorderedElementsAre("[from one callback]",
                                                   "[from another callback]"));
  }
}

using NullSafeSinkWrapperTest = LogSinkImplTest;

TEST_F(NullSafeSinkWrapperTest, Null) {
  base_logging::NullSafeSinkWrapper sink(nullptr);
  // This should not crash:
  LOG(INFO).ToSinkOnly(&sink) << "hello world";
}

TEST_F(NullSafeSinkWrapperTest, NonNull) {
  absl::ScopedMockLog mock_sink(absl::MockLogDefault::kDisallowUnexpected);
  base_logging::NullSafeSinkWrapper sink(&mock_sink.UseAsLocalSink());
  if (LoggingEnabledAt(absl::LogSeverity::kInfo)) {
    EXPECT_CALL(mock_sink,
                Send(Property("text_message", &absl::LogEntry::text_message,
                              "hello world")));
  }
  LOG(INFO).ToSinkOnly(&sink) << "hello world";
}

TEST_F(NullSafeSinkWrapperTest, Concurrency) {
  std::string str;
  base_logging::CopyToStringSink string_sink(&str);
  base_logging::NullSafeSinkWrapper wrapper_sink(&string_sink);

  // TSAN can only detect concurrency problems when a test is multithreaded.
  int threads = 2;
  {
    ThreadPool pool(threads);

    pool.Schedule([&wrapper_sink] {
      LOG(INFO).ToSinkOnly(&wrapper_sink) << "[from one callback]";
      wrapper_sink.Flush();
    });
    pool.Schedule([&wrapper_sink]() {
      LOG(INFO).ToSinkOnly(&wrapper_sink) << "[from another callback]";
      wrapper_sink.Flush();
    });
  }  // ThreadPool destructor blocks until all jobs are done.

  if (LoggingEnabledAt(absl::LogSeverity::kInfo)) {
    EXPECT_THAT(
        str, AnyOf(Eq("[from one callback]"), Eq("[from another callback]")));
  }
}

// Tests for exit-time hook execution.
// -----------------------------------

#if GTEST_HAS_DEATH_TEST
#ifdef _POSIX_VERSION
#if PORTABLE_BASE
// TODO: BUG
#elif defined(__Fuchsia__)
// TODO: Fuchsia doesn't have RunSignalSafeOnFailure, it's in
// PORT_POSIX_SOURCES.
#else
TEST(HookDeathTest, QFatalDoesNotRunRunOnFailure) {
  EXPECT_DEATH(
      {
        RunSignalSafeOnFailure([](FailureContext) {
          const absl::string_view msg = "running RunOnFailure hook\n";
          absl::raw_log_internal::AsyncSignalSafeWriteError(msg.data(),
                                                            msg.size());
        });
        LOG(QFATAL) << "hello world";
      },
      Not(HasSubstr("running RunOnFailure hook")));
}
#endif
#endif
#endif

// Tests for integration with `base::CrashReason` API.
// ---------------------------------------------------

#if BASE_HAVE_CRASHREASON
// Validates that one of the top `expected_max_depth` frames of the trace in
// `base::GetCrashReason()` matches `expected_frame`.  Writes "PASSED" or
// "FAILED" to stderr to communicate with death test parent process.
class CrashReasonValidatorLogSink : public absl::LogSink {
 public:
  explicit CrashReasonValidatorLogSink(const void* expected_frame,
                                       int expected_max_depth)
      : expected_frame_(expected_frame),
        expected_max_depth_(expected_max_depth) {}
  void Send(const absl::LogEntry& entry) override {
    const base::CrashReason* reason = base::GetCrashReason();
    if (!reason) {
      return;
    }
    if (!reason->depth) {
      fputs(
          "FAILED: base::GetCrashReason() returned a reason with no stack "
          "frames\n",
          stderr);
      return;
    }
    std::array<char, 1024> expected_symbol;
    if (!absl::Symbolize(expected_frame_, expected_symbol.data(),
                         expected_symbol.size())) {
      fputs("FAILED: failed to symbolize expected frame\n", stderr);
      return;
    }
    absl::FPrintF(stderr,
                  "Looking for frame %p %s in the first %d frames of the "
                  "CrashReason trace...\n",
                  expected_frame_, expected_symbol.data(), expected_max_depth_);
    std::array<char, 1024> symbol;
    for (int i = 0; i < reason->depth; i++) {
      // Here we first try `pc - 1` because `pc` may be in the next function.
      // The overrun happens when the function ends with a call to a function
      // annotated `[[noreturn]]` (e.g. `CHECK`).
      // If symbolization of `pc - 1` fails, we also try `pc` in case we crashed
      // on the first instruction of a function (e.g. `__restore_rt`).
      if (!absl::Symbolize(reinterpret_cast<char*>(reason->stack[i]) - 1,
                           symbol.data(), symbol.size()) &&
          !absl::Symbolize(reason->stack[i], symbol.data(), symbol.size())) {
        absl::FPrintF(
            stderr, "FAILED: failed to symbolize frame %d of CrashReason\n", i);
        return;
      }
      absl::FPrintF(stderr, "Frame %d: %p %s\n", i, reason->stack[i],
                    symbol.data());
      if (!strncmp(symbol.data(), expected_symbol.data(),
                   std::min(symbol.size(), expected_symbol.size()))) {
        if (i <= expected_max_depth_) {
          fputs("PASSED: found it!\n", stderr);
        } else {
          absl::FPrintF(stderr,
                        "FAILED: found it, but not in the first %d frames\n",
                        expected_max_depth_);
        }
        return;
      }
    }
    fputs(
        "FAILED: base::GetCrashReason() returned a reason, but the stack trace "
        "didn't include the expected frame (see above)\n",
        stderr);
  }

 private:
  const void* expected_frame_;
  int expected_max_depth_;
};

#if GTEST_HAS_DEATH_TEST
TEST(CrashReasonDeathTest, SetByFatalAndHasCorrectTopFrame) {
  std::array<void*, 1> frames;
  ASSERT_THAT(
      absl::GetStackTrace(frames.data(), frames.size(), /* skip_count = */ 0),
      Eq(1));
  CrashReasonValidatorLogSink sink(frames[0], 10);
  EXPECT_EXIT(
      { LOG(FATAL).ToSinkAlso(&sink) << "goodbye world"; }, DiedOfFatal,
      AllOf(HasSubstr("PASSED: found it!"), Not(HasSubstr("FAILED"))));
}
#endif

// TODO: add tests to validate that `LOG(FATAL)` correctly sets
// `CrashReason` from inside `LogSink::Send()` (it probably doesn't).  This
// probably needs to be done with a `RunSignalSafeOnFailure` callback since we
// can't go more than one sink deep.
#endif  // BASE_HAVE_CRASHREASON

// Tests for stack usage.
// ----------------------

// clang-format off
#define BASE_LOGGING_LOGGING_INTERNAL_LOG_50X(suffix)                     \
  LOG(INFO) suffix; LOG(INFO) suffix; LOG(INFO) suffix; LOG(INFO) suffix; \
  LOG(INFO) suffix; LOG(INFO) suffix; LOG(INFO) suffix; LOG(INFO) suffix; \
  LOG(INFO) suffix; LOG(INFO) suffix; LOG(INFO) suffix; LOG(INFO) suffix; \
  LOG(INFO) suffix; LOG(INFO) suffix; LOG(INFO) suffix; LOG(INFO) suffix; \
  LOG(INFO) suffix; LOG(INFO) suffix; LOG(INFO) suffix; LOG(INFO) suffix; \
  LOG(INFO) suffix; LOG(INFO) suffix; LOG(INFO) suffix; LOG(INFO) suffix; \
  LOG(INFO) suffix; LOG(INFO) suffix; LOG(INFO) suffix; LOG(INFO) suffix; \
  LOG(INFO) suffix; LOG(INFO) suffix; LOG(INFO) suffix; LOG(INFO) suffix; \
  LOG(INFO) suffix; LOG(INFO) suffix; LOG(INFO) suffix; LOG(INFO) suffix; \
  LOG(INFO) suffix; LOG(INFO) suffix; LOG(INFO) suffix; LOG(INFO) suffix; \
  LOG(INFO) suffix; LOG(INFO) suffix; LOG(INFO) suffix; LOG(INFO) suffix; \
  LOG(INFO) suffix; LOG(INFO) suffix
// clang-format on

class LargeStackFrameCompileTest {
  // Non-optimized builds don't reuse stack space for different `LOG` statements
  // within a frame, so it's important that each statement use as little of the
  // frame as possible to avoid overflowing google3's various very long
  // functions.

  static void EmptyStatements() {  // NOLINT(google-readability-function-size)
    // `LOG(INFO);` 500 times:
    BASE_LOGGING_LOGGING_INTERNAL_LOG_50X();
    BASE_LOGGING_LOGGING_INTERNAL_LOG_50X();
    BASE_LOGGING_LOGGING_INTERNAL_LOG_50X();
    BASE_LOGGING_LOGGING_INTERNAL_LOG_50X();
    BASE_LOGGING_LOGGING_INTERNAL_LOG_50X();
    BASE_LOGGING_LOGGING_INTERNAL_LOG_50X();
    BASE_LOGGING_LOGGING_INTERNAL_LOG_50X();
    BASE_LOGGING_LOGGING_INTERNAL_LOG_50X();
    BASE_LOGGING_LOGGING_INTERNAL_LOG_50X();
    BASE_LOGGING_LOGGING_INTERNAL_LOG_50X();
  }

  static void OneArgStatements() {  // NOLINT(google-readability-function-size)
    // `LOG(INFO) << 1;` 500 times:
    BASE_LOGGING_LOGGING_INTERNAL_LOG_50X(<< 1);
    BASE_LOGGING_LOGGING_INTERNAL_LOG_50X(<< 1);
    BASE_LOGGING_LOGGING_INTERNAL_LOG_50X(<< 1);
    BASE_LOGGING_LOGGING_INTERNAL_LOG_50X(<< 1);
    BASE_LOGGING_LOGGING_INTERNAL_LOG_50X(<< 1);
    BASE_LOGGING_LOGGING_INTERNAL_LOG_50X(<< 1);
    BASE_LOGGING_LOGGING_INTERNAL_LOG_50X(<< 1);
    BASE_LOGGING_LOGGING_INTERNAL_LOG_50X(<< 1);
    BASE_LOGGING_LOGGING_INTERNAL_LOG_50X(<< 1);
    BASE_LOGGING_LOGGING_INTERNAL_LOG_50X(<< 1);
  }
};

// Miscellaneous tests.
// --------------------

TEST(DebugFatalConstantTest, AreTheSameIntegralValue) {
  EXPECT_THAT(base_logging::DFATAL, Eq(static_cast<int>(absl::kLogDebugFatal)));
}

}  // namespace
