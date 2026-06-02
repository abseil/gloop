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

#include "gloop/base/logging.h"  // IWYU pragma: keep

#include <sys/stat.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ios>
#include <limits>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/sysinfo.h"
#include "absl/base/log_severity.h"
#include "absl/base/macros.h"
#include "absl/cleanup/cleanup.h"
#include "absl/flags/flag.h"
#include "absl/flags/reflection.h"
#include "absl/log/check.h"
#include "absl/log/flags.h"
#include "absl/log/globals.h"
#include "absl/log/internal/flags.h"
#include "absl/log/log.h"
#include "absl/log/scoped_mock_log.h"
#include "absl/log/vlog_is_on.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/source_location.h"
#include "gloop/base/config.h"
#include "gloop/base/init_google.h"
#include "gloop/base/internal/gcapturedstream.h"
#include "gloop/base/internal/logging_directories.h"
#include "gloop/base/internal/munge_output.h"
#include "gloop/base/log_file.h"
#include "gloop/base/log_file_flags.h"
#include "gloop/base/logging_extensions.h"
#include "gloop/base/port.h"
#include "gloop/thread/config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#if defined(_POSIX_VERSION) && !defined(__ANDROID__)
#include <glob.h>
#endif

#ifndef _WIN32
#include <unistd.h>
#endif

#ifdef THREAD_HAVE_FIBER
#include "gloop/thread/fiber/bundle.h"
#include "gloop/thread/fiber/fiber.h"
#else
#include <thread>  // NOLINT(build/c++11) for portability
#endif

namespace base_logging {
namespace {
using ::absl::ScopedMockLog;
using ::absl_testing::IsOk;
using ::testing::_;
using ::testing::InSequence;
using ::testing::MatchesRegex;
using ::testing::StartsWith;

class GoldenStderrTest : public testing::Test {
 public:
  static std::string OutputFilepath(absl::string_view name) {
    std::string output_dir = testing::TempDir();
    const char* const undeclared_outputs_dir =
        getenv("TEST_UNDECLARED_OUTPUTS_DIR");
    if (undeclared_outputs_dir) output_dir = undeclared_outputs_dir;
    absl::StrAppend(&output_dir, absl::string_view(&PATH_SEPARATOR, 1), name);
    return output_dir;
  }

 protected:
  static std::string GoldenStderrFilename(absl::string_view name) {
    constexpr absl::string_view path_sep(&PATH_SEPARATOR, 1);
    return absl::StrCat("gloop", path_sep, "base", path_sep, "testdata",
                        path_sep, "logging_unittest", path_sep, name);
  }

  std::string TestName() const {
    return ::testing::UnitTest::GetInstance()->current_test_info()->name();
  }

  std::string TestNameWithSuffix(absl::string_view suffix) const {
    std::string test_name = TestName();
    absl::StrAppend(&test_name, suffix);
    return test_name;
  }
};

TEST_F(GoldenStderrTest, BeforeInitGoogle) {
#if !GLOOP_INTERNAL_PROD_LOGGING
  GTEST_SKIP() << "This testcase is not supported on this platform";
#endif  // !GLOOP_INTERNAL_PROD_LOGGING
  absl::StatusOr<std::string> golden_stderr = logging_testing::ReadFile(
      GoldenStderrFilename(TestNameWithSuffix(".txt")));
  ASSERT_THAT(golden_stderr, IsOk())
      << "Could not read golden file for test " << TestName();
  *golden_stderr = absl::StrReplaceAll(
      *golden_stderr,
      {{"Logging before InitGoogle() is",
        "All log messages before absl::InitializeLog() is called are"}});
  const absl::StatusOr<std::string> munged_captured_stderr =
      base_logging::logging_testing::MungeFile(
          OutputFilepath(TestNameWithSuffix(".txt")),
          OutputFilepath(TestNameWithSuffix(".munged.txt")));
  EXPECT_THAT(munged_captured_stderr, IsOk())
      << "Could not munge captured stderr for test " << TestName();
  // `EXPECT_EQ` produces much more readable Sponge output for diffing
  // multi-line strings than `EXPECT_THAT`.
  EXPECT_EQ(*golden_stderr, *munged_captured_stderr)
      << "Munged output differs from golden for test " << TestName();
}

// This test fixture diffs the data logged by each testcase against a golden
// file in the `testdata` directory.  Output is stripped of time, tid, and other
// nondeterministic data prior to diffing.
class AutomaticGoldenStderrTest : public GoldenStderrTest {
 protected:
  AutomaticGoldenStderrTest()
      : golden_stderr_filepath_(
            GoldenStderrFilename(TestNameWithSuffix(".txt"))),
        captured_stderr_filepath_(OutputFilepath(TestNameWithSuffix(".txt"))),
        stderr_stream_(STDERR_FILENO, captured_stderr_filepath_) {}
  ~AutomaticGoldenStderrTest() override {
    stderr_stream_.Stop();
    if (IsSkipped()) return;
    const absl::StatusOr<std::string> golden_stderr =
        logging_testing::ReadFile(golden_stderr_filepath_);
    EXPECT_THAT(golden_stderr, IsOk())
        << "Could not read golden file for test " << TestName();
    const absl::StatusOr<std::string> munged_captured_stderr =
        logging_testing::MungeFile(
            captured_stderr_filepath_,
            OutputFilepath(TestNameWithSuffix(".munged.txt")));
    EXPECT_THAT(munged_captured_stderr, IsOk())
        << "Could not munge captured stderr for test " << TestName();
    // This is written a little funny:
    // * we can't `ASSERT` that `IsOK` above inside a destructor, so we test
    //   again here.
    // * `EXPECT_EQ` produces much more readable Sponge output for diffing
    //   multi-line strings than `EXPECT_THAT`.
    if (golden_stderr.ok() && munged_captured_stderr.ok())
      EXPECT_EQ(*golden_stderr, *munged_captured_stderr)
          << "Munged output differs from golden for test " << TestName();
  };

 private:
  const std::string golden_stderr_filepath_;
  const std::string captured_stderr_filepath_;
  logging_testing::GCapturedStream stderr_stream_;
};

void TestLogging() {
  const std::string foo_space("foo ");
  LOG(INFO) << foo_space << "bar " << 10 << ' ' << 3.4;
  for (int i = 0; i < 10; ++i) {
    int old_errno = errno;
    errno = i;
    PLOG_EVERY_N(ERROR, 2) << "Plog every 2, iteration " << COUNTER;
    errno = old_errno;

    if (true) LOG_EVERY_N(ERROR, 3) << "Log every 3, iteration " << COUNTER;

    if (false) {
    } else  // NOLINT(readability/braces)
      LOG_EVERY_N(ERROR, 4) << "Log every 4, iteration " << COUNTER;

    LOG_IF_EVERY_N(WARNING, true, 5) << "Log if every 5, iteration " << COUNTER;
    LOG_IF_EVERY_N(WARNING, false, 3)
        << "Log if every 3, iteration " << COUNTER;
    LOG_IF_EVERY_N(INFO, true, 1) << "Log if every 1, iteration " << COUNTER;
    LOG_IF_EVERY_N(ERROR, (i < 3), 2)
        << "Log if less than 3 every 2, iteration " << COUNTER;

    LOG_IF_FIRST_N(INFO, i < 5, 2)
        << "Log twice if less than 5, iteration " << COUNTER;

    LOG_FIRST_N(INFO, 3) << "Log first three times, iteration " << COUNTER;
  }
  LOG_IF(WARNING, true) << "log_if this";
  LOG_IF(WARNING, false) << "don't log_if this";

  char s[] = "array";
  LOG(INFO) << s;
  const char const_s[] = "const array";
  LOG(INFO) << const_s;
  int j = 1000;
  const std::string foo("foo");
  LOG(ERROR) << foo << ' ' << j << ' ' << j << " " << std::hex << j;
}

TEST_F(AutomaticGoldenStderrTest, Logging) { TestLogging(); }

// Test that various LOG macros do not declare new local variables.  If they
// did, they'd break the compilation of code that uses a log statement in
// the middle of code with a goto into it.
[[maybe_unused]] void TestsDoNotDeclareVariables() {
  switch (0) {
    case 1:
      PLOG_EVERY_N(INFO, 1);
      LOG_EVERY_N(INFO, 2);
      LOG_EVERY_N_SEC(INFO, 3);
      LOG_IF(INFO, true);
      LOG_IF_EVERY_N(INFO, true, 4);
      LOG_IF_EVERY_N_SEC(INFO, true, 5);
      LOG_IF_FIRST_N(INFO, true, 6);
      LOG_FIRST_N(INFO, 7);
      break;
    default:
      break;
  }

  goto label;
  {
    PLOG_EVERY_N(INFO, 1);
    LOG_EVERY_N(INFO, 2);
    LOG_EVERY_N_SEC(INFO, 3);
    LOG_IF(INFO, true);
    LOG_IF_EVERY_N(INFO, true, 4);
    LOG_IF_EVERY_N_SEC(INFO, true, 5);
    LOG_IF_FIRST_N(INFO, true, 6);
    LOG_FIRST_N(INFO, 7);
  label:
    {
    }
  }
}

TEST_F(AutomaticGoldenStderrTest, RawLogging) {
#if !GOOGLE_BASE_HAS_INITGOOGLE
  //
  // NOTE: Failure to call this is the source of the whacky discrepancy between
  // prod and non-prod builds with regards to truncation of long RAW log
  // messages. The timezone is uninitialized, so the raw_io Google3LogPrefixHook
  // function crams the UNIX timestamp into the seconds field of the prefix
  // string... which makes it longer than usual, which causes differing
  // truncation lengths. Ugh.
  //
  // Putting this here since this is the only test which currently relies on
  // this in the portable unit test, and there is no obviously better home for
  // it.
  //
  base_raw_log::raw_log_internal::InitRawLog();
#endif
  std::string* foo = new std::string("foo ");

  absl::FlagSaver saver;

  // Check that RAW logging does not use mallocs.

  ABSL_RAW_LOG(INFO, "%s%s%d%c%f", foo->c_str(), "bar ", 10, ' ', 3.4);
  char s[] = "array";
  ABSL_RAW_LOG(WARNING, "%s", s);
  const char const_s[] = "const array";
  ABSL_RAW_LOG(INFO, "%s", const_s);
  void* p = reinterpret_cast<void*>(0x12345678);
  ABSL_RAW_LOG(INFO, "ptr %p", p);
  p = nullptr;
  ABSL_RAW_LOG(INFO, "ptr %p", p);
  int j = 1000;
  ABSL_RAW_LOG(ERROR, "%s%d%c%010d%s%1x", foo->c_str(), j, ' ', j, " ", j);

  ABSL_RAW_LOG(INFO, "%s%c%s", "null->", 0, "<-here");
#ifdef NDEBUG
  ABSL_RAW_LOG(INFO, "foo %d", j);  // so that have same stderr to compare
#else
  ABSL_RAW_DLOG(INFO, "foo %d", j);  // test ABSL_RAW_DLOG in debug mode
#endif

  absl::SetFlag(&FLAGS_v, 0);
  ABSL_RAW_LOG(INFO, "log");

#ifdef NDEBUG
  ABSL_RAW_DCHECK(1 == 2,
                  " ABSL_RAW_DCHECK's shouldn't be compiled in normal mode");
#endif

  ABSL_RAW_CHECK(1 == 1, "should be ok");
  ABSL_RAW_DCHECK(true, "should be ok");

  ABSL_RAW_LOG(LEVEL(true ? INFO : WARNING), "variable-severity raw");

  delete foo;
}

TEST_F(AutomaticGoldenStderrTest, RawLoggingTruncation) {
  // The Abseil log library uses least 7 characters to render the thread id in
  // the log prefix of messages written to stderr However, if more characters
  // are needed to render the thread id, then the log prefix becomes longer,
  // which eats into the maximum length of the message written (this can happen
  // on some non-linux platforms which do not use pid_t to identify threads):
  //
  //   I0102 03:04:05.678900     451 foo.cc:1234] Huge string: aaaaa...
  //   I0102 03:04:05.678900 -1075421 foo.cc:1234] Huge string: aaaa...
  //
  // This will cause golden file mismatches for this test, so therefore we skip
  // it when this happens.
  absl::log_internal::Tid tid = absl::base_internal::GetCachedTID();
  if (absl::StrCat(tid).size() > 7) {
    GTEST_SKIP() << "The current TID (" << tid
                 << ") does not fit into 7 characters and will cause false "
                 << "failures in this test.";
  }

  // test how long messages are chopped:
  std::string huge_str(50000, 'a');
  ABSL_RAW_LOG(WARNING, "Huge string: %s", huge_str.c_str());
}

// This test conflates levels for both raw and ordinary logging. If we want to
// support some of these features and not others, this test should be split up.
TEST_F(AutomaticGoldenStderrTest, RawLoggingWithLevels) {
  absl::SetFlag(&FLAGS_v, 2);
  ABSL_RAW_LOG(INFO, "log");
  ABSL_RAW_LOG(INFO, "log INFO disabled");
  ABSL_RAW_LOG(WARNING, "log WARNING on");
  absl::SetFlag(&FLAGS_v, 0);
  ABSL_RAW_LOG(INFO, "log INFO disabled");
  ABSL_RAW_LOG(ERROR, "log ERROR on");
}

#if GTEST_HAS_DEATH_TEST
TEST(RawLogging, Dfatal) {
#ifndef NDEBUG
  EXPECT_DEATH(ABSL_RAW_LOG(DFATAL, "a_complicated_message"),
               "a_complicated_message");
#else
  ABSL_RAW_LOG(DFATAL, "this_should_not_die");
#endif
  EXPECT_DEATH(ABSL_RAW_LOG(FATAL, "a_death_message"), "a_death_message");
}
#endif

// This test now almost passes without InitGoogle. The failure is caused by the
// !FlagsParsed() check in Google3LogPrefixHook which will always be true when
// InitGoogle is not called, so raw log messages are not filtered correctly.
#if GOOGLE_BASE_HAS_INITGOOGLE
void TestLogWithLevels(int v, int severity) {
  fprintf(stderr, "Test: v=%d stderrthreshold=%d\n", v, severity);
  fflush(stderr);

#if _WIN32
  // C99 requires stderr to not be fully-buffered by default (7.19.3.7), but
  // MS CRT buffers it anyway. We have a similar hack in logging.cc but only
  // for Warning+ severity.
  auto flusher = absl::MakeCleanup([]() { fflush(stderr); });
#endif
  absl::FlagSaver saver;

  absl::SetFlag(&FLAGS_v, v);
  absl::SetFlag(&FLAGS_stderrthreshold, severity);
  ABSL_RAW_LOG(INFO, "log info");
  ABSL_RAW_LOG(WARNING, "log warning");
  ABSL_RAW_LOG(ERROR, "log error");

  VLOG(-1) << "vlog -1";
  VLOG(0) << "vlog 0";
  VLOG(1) << "vlog 1";
  LOG(INFO) << "log info";
  LOG(WARNING) << "log warning";
  LOG(ERROR) << "log error";

  LOG_IF(INFO, true) << "log_if info";
  LOG_IF(INFO, false) << "don't log_if info";
  LOG_IF(WARNING, true) << "log_if warning";
  LOG_IF(WARNING, false) << "don't log_if warning";
  LOG_IF(ERROR, true) << "log_if error";
  LOG_IF(ERROR, false) << "don't log_if error";

  int c;
  c = 1;
  LOG_IF(INFO, c -= 2) << "log_if info expr";
  EXPECT_EQ(c, -1);
  c = 1;
  LOG_IF(ERROR, c -= 2) << "log_if error expr";
  EXPECT_EQ(c, -1);
  c = 2;
  LOG_IF(ERROR, c -= 2) << "don't log_if error expr";
  EXPECT_EQ(c, 0);

  c = 3;
  LOG_IF_EVERY_N(INFO, c -= 4, 1) << "log_if info every 1 expr";
  EXPECT_EQ(c, -1);
  c = 3;
  LOG_IF_EVERY_N(ERROR, c -= 4, 1) << "log_if error every 1 expr";
  EXPECT_EQ(c, -1);
  c = 4;
  LOG_IF_EVERY_N(ERROR, c -= 4, 3) << "don't log_if info every 3 expr";
  EXPECT_EQ(c, 0);
  c = 4;
  LOG_IF_EVERY_N(ERROR, c -= 4, 3) << "don't log_if error every 3 expr";
  EXPECT_EQ(c, 0);
}

TEST_F(AutomaticGoldenStderrTest, LoggingLevels) {
  TestLogWithLevels(0, INFO);
  TestLogWithLevels(1, INFO);
  TestLogWithLevels(-1, INFO);
  TestLogWithLevels(0, WARNING);
  TestLogWithLevels(1, WARNING);
  TestLogWithLevels(0, ERROR);
  TestLogWithLevels(1, ERROR);
  TestLogWithLevels(0, FATAL);
  TestLogWithLevels(1, FATAL);
}
#endif  // GOOGLE_BASE_HAS_INITGOOGLE

// These tests verify that VLogIsOn responds properly to flag changes
// at runtime.
class VLogIsOnTest : public testing::Test {
 public:
  VLogIsOnTest() {
    // Gunit handles restoring flags that are set in a test, but doesn't
    // automatically clean up state changes due to SetVLOGLevel. So, let's
    // manually make sure every test starts and ends in a clean state.
    absl::SetFlag(&FLAGS_vmodule, "");
    absl::SetFlag(&FLAGS_v, 0);
  }
  ~VLogIsOnTest() override {
    // See note in `SetUp()`.
    absl::SetFlag(&FLAGS_vmodule, "");
  }
};

TEST_F(VLogIsOnTest, DefaultLevelChange) {
  absl::SetFlag(&FLAGS_v, 0);
  bool before[] = {true, false, false, false, false};
  for (size_t i = 0; i < ABSL_ARRAYSIZE(before); ++i) {
    ASSERT_EQ(before[i], VLOG_IS_ON(i)) << i;
  }

  absl::SetFlag(&FLAGS_v, 2);
  bool after[] = {true, true, true, false, false};
  for (size_t i = 0; i < ABSL_ARRAYSIZE(after); ++i) {
    EXPECT_EQ(after[i], VLOG_IS_ON(i)) << i;
  }
}

// In this test, N threads simultaneously log, while one thread calls
// SetVLOGLevel() with a monotonically increasing logging level.  On
// each iteration through the loop, the loggers verify that their
// observed logging level never decreases.  (This should happen only
// if VLOG_IS_ON() has a race condition.)
// We use std::thread for portability reasons.
void AcquireAndLog();
void IncrementLevel();

TEST_F(VLogIsOnTest, ThreadStressTest) {
  absl::SetFlag(&FLAGS_v, 0);
  constexpr int kNumLoggers = 50;

#ifdef THREAD_HAVE_FIBER
  thread::Bundle threads;
  threads.Add(IncrementLevel);
  for (int i = 0; i < kNumLoggers; ++i) {
    threads.Add(AcquireAndLog);
  }
  threads.JoinAll();
#else
  std::vector<std::thread> threads;
  threads.emplace_back(IncrementLevel);
  for (int i = 0; i < kNumLoggers; ++i) {
    threads.emplace_back(AcquireAndLog);
  }
  for (auto& thread : threads) {
    thread.join();
  }
#endif

  EXPECT_TRUE(VLOG_IS_ON(std::numeric_limits<int16_t>::max() - 1));
}

ABSL_CONST_INIT static std::atomic<int32_t> global_level{0};

void AcquireAndLog() {
  while (true) {
    int my_level = global_level.load(std::memory_order_acquire);
    ASSERT_TRUE(VLOG_IS_ON(my_level)) << my_level;
    if (my_level == std::numeric_limits<int16_t>::max() - 1) {
      break;
    }
  }
}

void IncrementLevel() {
  for (int i = 0; i < std::numeric_limits<int16_t>::max(); ++i) {
    absl::SetVLogLevel("logging*", i);
    global_level.store(i, std::memory_order_release);
  }
}

void SetVLogLevelFoo() { absl::SetVLogLevel("foo", 1); }
void SetVLogLevelBar() { absl::SetVLogLevel("bar", 1); }

TEST_F(VLogIsOnTest, SetVLogLevelIsAtomic) {
  // We abuse the log_internal namespace a bit so that we can check two
  // different files in one test.
  EXPECT_EQ(absl::log_internal::VLogLevel("foo.cc"), 0);
  EXPECT_EQ(absl::log_internal::VLogLevel("bar.cc"), 0);

#ifdef THREAD_HAVE_FIBER
  thread::Fiber set_foo(SetVLogLevelFoo);
  thread::Fiber set_bar(SetVLogLevelBar);
  auto join_foo = [&set_foo]() { set_foo.Join(); };
  auto join_bar = [&set_bar]() { set_bar.Join(); };
#else
  std::thread set_foo(SetVLogLevelFoo);
  std::thread set_bar(SetVLogLevelBar);
  auto join_foo = [&set_foo]() { set_foo.join(); };
  auto join_bar = [&set_bar]() { set_bar.join(); };
#endif

  join_foo();
  EXPECT_EQ(absl::log_internal::VLogLevel("foo.cc"), 1);

  join_bar();
  EXPECT_EQ(absl::log_internal::VLogLevel("foo.cc"), 1);
  EXPECT_EQ(absl::log_internal::VLogLevel("bar.cc"), 1);
}

TEST_F(VLogIsOnTest, TestVmoduleRevertedToEmpty) {
  absl::SetFlag(&FLAGS_vmodule, "logging_unittest=1");
  EXPECT_TRUE(VLOG_IS_ON(1));
  absl::SetFlag(&FLAGS_vmodule, "");
  EXPECT_FALSE(VLOG_IS_ON(1));
  absl::SetFlag(&FLAGS_vmodule, "logging_unittest=1");
  EXPECT_TRUE(VLOG_IS_ON(1));
}

// Vmodule has the idiosyncracy that if multiple globs match the same module,
// the first glob wins. This test is to verify that vmodule clears all of the
// globs *before* assigning any new ones to keep vmodule stateless.
TEST_F(VLogIsOnTest, TestVmoduleClearedBeforeReassignment) {
  absl::SetFlag(&FLAGS_vmodule, "logging_unittest=1");
  EXPECT_TRUE(VLOG_IS_ON(1));
  absl::SetFlag(&FLAGS_vmodule, "base*=0");
  EXPECT_FALSE(VLOG_IS_ON(1));
  absl::SetFlag(&FLAGS_vmodule, "l*=1");
  EXPECT_TRUE(VLOG_IS_ON(1));
  absl::SetFlag(&FLAGS_vmodule, "b?se*=0");
  EXPECT_FALSE(VLOG_IS_ON(1));
  absl::SetFlag(&FLAGS_vmodule, "logging_unittest=1");
  EXPECT_TRUE(VLOG_IS_ON(1));
}

TEST_F(VLogIsOnTest, TestVmoduleExplicitlyClearedToZero) {
  absl::SetFlag(&FLAGS_vmodule, "logging_unittest=1");
  EXPECT_TRUE(VLOG_IS_ON(1));
  absl::SetFlag(&FLAGS_vmodule, "logging_unittest=0");
  EXPECT_FALSE(VLOG_IS_ON(1));
}

TEST_F(VLogIsOnTest, SetVLogLevel) {
  absl::SetFlag(&FLAGS_v, 0);
  // Arbitrarily test VLOG(0) -> VLOG(4)
  bool before[] = {true, false, false, false, false};
  for (size_t i = 0; i < ABSL_ARRAYSIZE(before); ++i) {
    ASSERT_EQ(before[i], VLOG_IS_ON(i)) << i;
  }

  bool after[] = {false, false, false, false, false};

  // Change through five different logging levels at the same logging
  // site.
  for (size_t j = 0; j < ABSL_ARRAYSIZE(after); ++j) {
    absl::SetVLogLevel("logging*", j);
    after[j] = true;
    for (size_t i = 0; i < ABSL_ARRAYSIZE(after); ++i) {
      EXPECT_EQ(after[i], VLOG_IS_ON(i)) << j << " " << i;
    }
  }
}

TEST_F(VLogIsOnTest, VModuleFlagChange) {
  absl::SetFlag(&FLAGS_v, 0);
  bool before[] = {true, false, false, false, false};
  for (size_t i = 0; i < ABSL_ARRAYSIZE(before); ++i) {
    ASSERT_EQ(before[i], VLOG_IS_ON(i)) << i;
  }

  bool after[] = {false, false, false, false, false};
  // Change through five different logging levels at the same logging
  // site.
  for (size_t j = 0; j < ABSL_ARRAYSIZE(after); ++j) {
    char pattern[] = "log*=?";
    pattern[ABSL_ARRAYSIZE(pattern) - 2] = '0' + j;
    absl::SetFlag(&FLAGS_vmodule, pattern);
    after[j] = true;
    for (size_t i = 0; i < ABSL_ARRAYSIZE(after); ++i) {
      EXPECT_EQ(after[i], VLOG_IS_ON(i)) << j << " " << i;
    }
  }
}

TEST_F(VLogIsOnTest, VModuleOrdering) {
  // The first glob to match will set the verbosity, regardless of repeats.
  absl::SetFlag(&FLAGS_vmodule,
                "?ogging_unittest=1,logging_?nittest=2,logging_unittest=3");
  EXPECT_TRUE(VLOG_IS_ON(1));
  EXPECT_FALSE(VLOG_IS_ON(2));
  absl::SetFlag(&FLAGS_vmodule,
                "?ogging_unittest=1,logging_?nittest=2,logging_unittest="
                "3,?ogging_unittest=4");
  EXPECT_TRUE(VLOG_IS_ON(1));
  EXPECT_FALSE(VLOG_IS_ON(2));
}

TEST_F(VLogIsOnTest, VLogSiteModuleFlagChange) {
  bool before[] = {true, false, false, false, false};
  for (size_t i = 0; i < ABSL_ARRAYSIZE(before); ++i) {
    static absl::log_internal::VLogSite site(__FILE__);
    ASSERT_EQ(before[i], site.IsEnabled(i)) << i;
  }

  bool after[] = {false, false, false, false, false};
  // Change through five different logging levels at the same logging
  // site.
  for (size_t j = 0; j < ABSL_ARRAYSIZE(after); ++j) {
    char pattern[] = "log*=?";
    pattern[ABSL_ARRAYSIZE(pattern) - 2] = '0' + j;
    absl::SetFlag(&FLAGS_vmodule, pattern);
    after[j] = true;
    for (size_t i = 0; i < ABSL_ARRAYSIZE(after); ++i) {
      static absl::log_internal::VLogSite site(__FILE__);
      EXPECT_EQ(after[i], site.IsEnabled(i)) << "j=" << j << " i=" << i;
    }
  }
}

TEST_F(VLogIsOnTest, EpochWrapAround) {
  // The global epoch wraps after 1<<16 calls to SetVLOGLevel().
  // Verify that site epochs are correctly invalidated in this case.
  for (int i = 0; i < 1 << 17; ++i) {
    absl::SetVLogLevel("logging*", i % 2);
    EXPECT_EQ(i % 2, VLOG_IS_ON(1)) << i;
  }
}

#if GTEST_HAS_DEATH_TEST
TEST(DeathRawCHECK, logging) {
  ASSERT_DEATH(ABSL_RAW_CHECK(false, "failure 1"),
               "RAW: Check false failed: failure 1");
  ASSERT_DEBUG_DEATH(ABSL_RAW_DCHECK(1 == 2, "failure 2"),
                     "RAW: Check 1 == 2 failed: failure 2");
}
#endif

#if defined(_POSIX_VERSION) && !defined(__ANDROID__)
// Get list of file names that match pattern
std::vector<std::string> GetFiles(const std::string& pattern) {
  glob_t g;
  const int r = glob(pattern.c_str(), 0, nullptr, &g);
  CHECK(r == 0 || r == GLOB_NOMATCH) << ": error matching " << pattern;
  auto g_cleanup = absl::MakeCleanup([&] { globfree(&g); });
  std::vector<std::string> files;
  for (int i = 0; i < g.gl_pathc; i++) {
    files.push_back(std::string(g.gl_pathv[i]));
  }
  return files;
}

// Delete files patching pattern
void DeleteFiles(const std::string& pattern) {
  std::vector<std::string> files = GetFiles(pattern);
  for (const std::string& file : files) {
    PCHECK(unlink(file.c_str()) == 0) << ": " << file;
  }
}

void CheckFile(const std::string& name, const std::string& expected_string) {
  std::vector<std::string> files = GetFiles(absl::StrCat(name, "*"));
  CHECK_EQ(files.size(), 1) << ": failed to find exactly one " << name << "*";

  FILE* file = fopen(files[0].c_str(), "r");
  CHECK(file != nullptr) << ": could not open " << files[0];
  char buf[1000];
  while (fgets(buf, sizeof(buf), file) != nullptr) {
    if (strstr(buf, expected_string.c_str()) != nullptr) {
      fclose(file);
      return;
    }
  }
  fclose(file);
  LOG(FATAL) << "Did not find " << expected_string << " in " << files[0];
}
#endif  // defined(_POSIX_VERSION) && !defined(__ANDROID__)

#if GLOOP_INTERNAL_PROD_LOGGING
void TestBasename() {
  fputs("==== Test setting log file basename\n", stderr);
  const std::string dest =
      absl::StrCat(testing::TempDir(), "/logging_test_basename");
  DeleteFiles(absl::StrCat(dest, "*"));

  SetLogDestination(INFO, dest.c_str());
  LOG(INFO) << "message to new base";
  FlushLogFiles(INFO);
  CheckFile(dest, "message to new base");

  DeleteFiles(absl::StrCat(dest, "*"));
}

void TestSymlink() {
  fputs("==== Test setting log file symlink\n", stderr);
  const std::string dest =
      absl::StrCat(testing::TempDir(), "/logging_test_symlink");
  const std::string sym = absl::StrCat(testing::TempDir(), "/symlinkbase");
  DeleteFiles(absl::StrCat(dest, "*"));
  DeleteFiles(absl::StrCat(sym, "*"));

  SetLogSymlink(INFO, "symlinkbase");
  SetLogDestination(INFO, dest.c_str());
  LOG(INFO) << "message to new symlink";
  FlushLogFiles(INFO);
  CheckFile(sym, "message to new symlink");
  EXPECT_EQ(GetSymlinkPath(absl::LogSeverity::kInfo), sym);

  DeleteFiles(absl::StrCat(dest, "*"));
  DeleteFiles(absl::StrCat(sym, "*"));
}

void TestExtension() {
  fputs("==== Test setting log file extension\n", stderr);
  const std::string dest =
      absl::StrCat(testing::TempDir(), "/logging_test_extension");
  DeleteFiles(absl::StrCat(dest, "*"));

  SetLogDestination(INFO, dest.c_str());
  SetLogFilenameExtension("specialextension");
  LOG(INFO) << "message to new extension";
  FlushLogFiles(INFO);
  CheckFile(dest, "message to new extension");

  // Check that file name ends with extension
  std::vector<std::string> filenames = GetFiles(absl::StrCat(dest, "*"));
  CHECK_EQ(filenames.size(), 1);
  CHECK(strstr(filenames[0].c_str(), "specialextension") != nullptr);

  DeleteFiles(absl::StrCat(dest, "*"));

  // Clean up the extension.
  SetLogFilenameExtension("");
}

void TestPermissions() {
  fputs("==== Test setting log file permissions\n", stderr);
  const std::string dest =
      absl::StrCat(testing::TempDir(), "/logging_test_permissions");
  DeleteFiles(absl::StrCat(dest, "*"));

  // Check that a file with permissions of 0 ends up with 06?? (don't care about
  // the umask effects).
  SetLogDestination(INFO, dest.c_str());
  SetLogFilePermissions(0);
  LOG(INFO) << "message to new extension";
  FlushLogFiles(INFO);
  CheckFile(dest, "message to new extension");
  std::vector<std::string> filenames = GetFiles(absl::StrCat(dest, "*"));
  CHECK_EQ(filenames.size(), 1);
  struct stat stat_buf;
  PCHECK(stat(filenames[0].c_str(), &stat_buf) != -1);
  CHECK_EQ(stat_buf.st_mode & 0700, 0600);

  DeleteFiles(absl::StrCat(dest, "*"));

  // Check that a file with permissions of 0644 ends up with 0644.
  SetLogDestination(INFO, dest.c_str());
  SetLogFilePermissions(0644);
  LOG(INFO) << "message to new extension";
  FlushLogFiles(INFO);
  CheckFile(dest, "message to new extension");
  filenames = GetFiles(absl::StrCat(dest, "*"));
  CHECK_EQ(filenames.size(), 1);
  PCHECK(stat(filenames[0].c_str(), &stat_buf) != -1);
  CHECK_EQ(stat_buf.st_mode & 0777, 0644);

  DeleteFiles(absl::StrCat(dest, "*"));
}

void TestGetLogPath() {
  fputs("==== Test getting log path\n", stderr);
  std::string dest =
      absl::StrCat(testing::TempDir(), "/logging_test_get_log_path");
  DeleteFiles(absl::StrCat(dest, "*"));

  // Move the log destination; as a result, the log path should be cleared.
  SetLogDestination(INFO, dest.c_str());
  CHECK(GetLogPath(INFO).empty()) << "Path: " << GetLogPath(INFO);

  // Create a log file.
  LOG(INFO) << "message to info";
  FlushLogFiles(INFO);

  // The path should now indicate the output file.
  std::vector<std::string> filenames = GetFiles(absl::StrCat(dest, "*"));
  CHECK_EQ(filenames.size(), 1);
  CHECK_EQ(filenames[0].c_str(), GetLogPath(INFO));

  DeleteFiles(absl::StrCat(dest, "*"));

  // Try to write to an inaccessible location in order to break log output.
  absl::StrAppend(&dest, "/nonexistent");
  SetLogDestination(INFO, dest.c_str());
  LOG(INFO) << "message to info";
  FlushLogFiles(INFO);
  CHECK(GetLogPath(INFO).empty()) << "Path: " << GetLogPath(INFO);
}
#endif  // GLOOP_INTERNAL_PROD_LOGGING

void TestGWQStatusMessage() {
#if !PORTABLE_BASE
  char* old_status_dir = getenv("GOOGLE_STATUS_DIR");
  std::string status_file;

  if (old_status_dir == nullptr) {
    PCHECK(setenv("GOOGLE_STATUS_DIR", testing::TempDir().c_str(), 1) != -1);
    status_file = absl::StrCat(testing::TempDir(), "/STATUS");
  } else {
    status_file = absl::StrCat(old_status_dir, "/STATUS");
  }

  if (old_status_dir == nullptr) {
    PCHECK(unsetenv("GOOGLE_STATUS_DIR") != -1);
  }
#endif
}

TEST(DVLog, Basic) {
  ScopedMockLog log;

#if NDEBUG
  // We are expecting that nothing is logged.
  EXPECT_CALL(log, Log(_, _, _)).Times(0);
#else
  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, __FILE__, "debug log"));
#endif

  log.StartCapturingLogs();
  absl::SetFlag(&FLAGS_v, 1);
  DVLOG(1) << "debug log";
}

TEST(DVLog, V0) {
  ScopedMockLog log;

  // We are expecting that nothing is logged.
  EXPECT_CALL(log, Log(_, _, _)).Times(0);

  log.StartCapturingLogs();
  absl::SetFlag(&FLAGS_v, 0);
  DVLOG(1) << "debug log";
}

TEST(LogEveryNSeconds, LogsAtCorrectTimes) {
#ifdef __MACH__
  // TODO: investigate and fix
  GTEST_SKIP() << "This is known to be broken on Darwin";
#endif
  const auto base_num_infos =
      base_logging::LoggedMessages(absl::LogSeverity::kInfo);
  int log_evaluated_count = 0;
  const absl::Time start_time = absl::Now();
  while (log_evaluated_count < 3) {
    // Log every 40 ms until we see 3 logs.
    LOG_EVERY_N_SEC(INFO, 0.04)
        << "Got " << COUNTER << " total calls "
        << "and " << ++log_evaluated_count << " log calls";
  }
  // It must take at least 80 ms to see the 3 logs (logs at 0, 40 and 80 ms).
  EXPECT_GE(absl::ToDoubleSeconds(absl::Now() - start_time),
            0.04 * (log_evaluated_count - 1));
  EXPECT_EQ(base_num_infos + 3,
            base_logging::LoggedMessages(absl::LogSeverity::kInfo));
}

TEST(LogIfEveryNSeconds, LogsAtCorrectTimesAndCountsCorrectly) {
#ifdef __MACH__
  // TODO: investigate and fix
  GTEST_SKIP() << "This is known to be broken on Darwin";
#endif
  ScopedMockLog log;
  EXPECT_CALL(log,
              Log(absl::LogSeverity::kInfo, __FILE__, "cond: true, log: 1"))
      .Times(1);
  EXPECT_CALL(log,
              Log(absl::LogSeverity::kInfo, __FILE__, "cond: true, log: 2"))
      .Times(1);
  EXPECT_CALL(log,
              Log(absl::LogSeverity::kInfo, __FILE__, "cond: true, log: 3"))
      .Times(1);

  log.StartCapturingLogs();
  const auto base_num_infos =
      base_logging::LoggedMessages(absl::LogSeverity::kInfo);
  int log_evaluated_count = 0;
  const absl::Time start_time = absl::Now();
  bool toggle = true;
  while (log_evaluated_count < 3) {
    // Log every 40 ms until we see 3 logs.
    LOG_IF_EVERY_N_SEC(INFO, toggle, 0.04)
        << "cond: " << toggle << ", log: " << ++log_evaluated_count;
    toggle = !toggle;
  }
  // It must take at least 80 ms to see the 3 logs (logs at 0, 40 and 80 ms).
  EXPECT_GE(absl::ToDoubleSeconds(absl::Now() - start_time),
            0.04 * (log_evaluated_count - 1));
  EXPECT_LE(base_num_infos + 3,
            base_logging::LoggedMessages(absl::LogSeverity::kInfo));
}

TEST(LogEveryN, CompilesWithFloat) {
  const auto base_num_infos =
      base_logging::LoggedMessages(absl::LogSeverity::kInfo);
  for (int i = 0; i < 10; ++i) {
    LOG_EVERY_N(INFO, 5.0) << "This should compile and work";
  }
  EXPECT_EQ(base_num_infos + 2,
            base_logging::LoggedMessages(absl::LogSeverity::kInfo));
}

TEST(LogEveryN, DoesntCrashWith0) {
  for (int i = 0; i < 10; ++i) {
    static int zero = 0;
    LOG_EVERY_N(INFO, zero) << "This should compile and shouldn't crash";
  }
}

TEST(LogEveryN, VariousN) {
  ScopedMockLog log;
  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, __FILE__, "Never printed"))
      .Times(0);
  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, __FILE__, "Printed 10 times"))
      .Times(10);
  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, __FILE__, "Printed 5 times"))
      .Times(5);
  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, __FILE__, "Printed 4 times"))
      .Times(4);
  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, __FILE__,
                       "Printed only the first time"))
      .Times(1);
  log.StartCapturingLogs();
  for (int i = 0; i < 10; ++i) {
    // If the compiler determines that the zero is a fixed constant,
    // it fails with a divide-by-zero error.
    static int zero = 0;
    LOG_EVERY_N(INFO, zero) << "Never printed";
    LOG_EVERY_N(INFO, 1) << "Printed 10 times";
    LOG_EVERY_N(INFO, 2) << "Printed 5 times";
    LOG_EVERY_N(INFO, 3) << "Printed 4 times";
    LOG_EVERY_N(INFO, 100) << "Printed only the first time";
  }
}

TEST(LogIfEveryN, RespectsCondition) {
  ScopedMockLog log;
  // This is the exact log output that we expect.  This ensures that:
  //  (1) The log messages are output at the correct intervals.
  //  (2) The output stream is only evaluated when messages are logged.
  //  (3) COUNTER is incremented the number of times (condition) is true.
  {
    InSequence s;
    EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, __FILE__,
                         " i:0  COUNTER:1  outputs:1"));
    EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, __FILE__,
                         " i:25  COUNTER:6  outputs:2"));
    EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, __FILE__,
                         " i:50  COUNTER:11  outputs:3"));
    EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, __FILE__,
                         " i:75  COUNTER:16  outputs:4"));
  }
  {
    InSequence s;
    EXPECT_CALL(log, Log(absl::LogSeverity::kWarning, __FILE__,
                         " i:2  COUNTER:1  outputs:1"));
    EXPECT_CALL(log, Log(absl::LogSeverity::kWarning, __FILE__,
                         " i:30  COUNTER:8  outputs:2"));
    EXPECT_CALL(log, Log(absl::LogSeverity::kWarning, __FILE__,
                         " i:58  COUNTER:15  outputs:3"));
    EXPECT_CALL(log, Log(absl::LogSeverity::kWarning, __FILE__,
                         " i:86  COUNTER:22  outputs:4"));
  }
  {
    InSequence s;
    EXPECT_CALL(log, Log(absl::LogSeverity::kError, __FILE__,
                         " i:2  COUNTER:1  outputs:1"));
    EXPECT_CALL(log, Log(absl::LogSeverity::kError, __FILE__,
                         " i:22  COUNTER:2  outputs:2"));
    EXPECT_CALL(log, Log(absl::LogSeverity::kError, __FILE__,
                         " i:42  COUNTER:3  outputs:3"));
    EXPECT_CALL(log, Log(absl::LogSeverity::kError, __FILE__,
                         " i:62  COUNTER:4  outputs:4"));
    EXPECT_CALL(log, Log(absl::LogSeverity::kError, __FILE__,
                         " i:82  COUNTER:5  outputs:5"));
  }
  log.StartCapturingLogs();
  int expected_info_num_logs = 0;
  int expected_warning_num_logs = 0;
  int expected_error_num_logs = 0;
  for (int i = 0; i < 100; ++i) {
    // condition triggers 100/5 = 20 times
    // expected_info_num_logs should be incremented 20/5 = 4 times.
    LOG_IF_EVERY_N(INFO, (i % 5) == 0, 5e0)
        << " i:" << i << "  COUNTER:" << COUNTER
        << "  outputs:" << ++expected_info_num_logs;
    EXPECT_EQ(1 + i / 25, expected_info_num_logs) << "For i = " << i;

    // condition triggers 100/4 = 25 times
    // expected_warning_num_logs should be incremented 25/7 + 1 = 4 times.
    LOG_IF_EVERY_N(WARNING, (i % 4) == 2, 7)
        << " i:" << i << "  COUNTER:" << COUNTER
        << "  outputs:" << ++expected_warning_num_logs;
    EXPECT_EQ((i + 26) / 28, expected_warning_num_logs) << "For i = " << i;

    // condition triggers 100/20 = 5 times
    // expected_error_num_logs should be incremented 5 times.
    LOG_IF_EVERY_N(ERROR, (i % 20) == 2, 1.0)
        << " i:" << i << "  COUNTER:" << COUNTER
        << "  outputs:" << ++expected_error_num_logs;
    EXPECT_EQ((i + 18) / 20, expected_error_num_logs) << "For i = " << i;
  }
}

#if GLOOP_INTERNAL_PROD_LOGGING
class LoggingDirectoriesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const char* test_tmpdir = getenv("TEST_TMPDIR");
    // If the test is run as a plain binary, it won't have this
    // environment variable.
    if (test_tmpdir == nullptr) {
      has_test_tmpdir_ = false;
    } else {
      has_test_tmpdir_ = true;
      orig_test_tmpdir_ = test_tmpdir;
    }
    base_logging::logging_internal::ClearLoggingDirectories();
  }

  void TearDown() override {
    if (has_test_tmpdir_) {
      PCHECK(setenv("TEST_TMPDIR", orig_test_tmpdir_.c_str(), 1) != -1);
    }
    base_logging::logging_internal::ClearLoggingDirectories();
  }

  bool has_test_tmpdir_;
  std::string orig_test_tmpdir_;
};

TEST_F(LoggingDirectoriesTest, LogDir) {
  absl::SetFlag(&FLAGS_log_dir, "/dummy/dir");
  const std::vector<std::string> dirs = GetLoggingDirectories();
  ASSERT_EQ(1, dirs.size());
  EXPECT_EQ("/dummy/dir", dirs[0]);
}

TEST_F(LoggingDirectoriesTest, NoLogDir) {
  absl::SetFlag(&FLAGS_log_dir, "");
  const std::vector<std::string> dirs = GetLoggingDirectories();
  for (const std::string& dir : dirs) {
    struct stat buf;
    PCHECK(stat(dir.c_str(), &buf) != -1);
    EXPECT_TRUE(S_ISDIR(buf.st_mode));
  }
}

// Set TEST_TMPDIR to the empty string to ensure that the
// code handles it correctly.
TEST_F(LoggingDirectoriesTest, EmptyEnvVar) {
  absl::SetFlag(&FLAGS_log_dir, "");
  // We use putenv() because setenv() clears TEST_TMPDIR from the
  // environment when the value is the empty string.
  char str[] = "TEST_TMPDIR=";
  PCHECK(putenv(str) != -1);
  const std::vector<std::string> dirs = GetLoggingDirectories();
  for (const std::string& dir : dirs) {
    struct stat buf;
    PCHECK(stat(dir.c_str(), &buf) != -1);
    EXPECT_TRUE(S_ISDIR(buf.st_mode));
  }
  // The call to putenv() would leave a dangling pointer when this
  // function exits.  Remove the environment variable to prevent that.
  PCHECK(unsetenv("TEST_TMPDIR") != -1);
}
#endif  // GLOOP_INTERNAL_PROD_LOGGING

#if GTEST_HAS_DEATH_TEST && \
    GTEST_GOOGLE3_MODE_  // EXPECT_DFATAL only exists in the internal gunit.
TEST(PerrorLogMessageTest, DfatalPlog) {
  EXPECT_DFATAL(
      {
        errno = 5;
        PLOG(DFATAL) << "message";
      },
      "message: .* \\[5\\]");
}
#endif

#if GTEST_HAS_DEATH_TEST
TEST(PerrorLogMessageTest, FatalPlog) {
  EXPECT_DEATH(
      {
        errno = 5;
        PLOG(FATAL) << "message";
      },
      "message: .* \\[5\\]");
}
#endif

TEST(LogIfTest, FalseNotFatal) { LOG_IF(FATAL, false); }

TEST(PerrorLogMessageTest, VariableSeverity) {
  ScopedMockLog log;
  EXPECT_CALL(log, Log(absl::LogSeverity::kWarning, _,
                       MatchesRegex("warning: .* \\[1\\]")));
  log.StartCapturingLogs();
  absl::LogSeverity W = absl::LogSeverity::kWarning;
  errno = 1;
  PLOG(LEVEL(W)) << "warning";
}

TEST(CheckOpValueTest, Volatile) {
  volatile int a = 1;
  volatile int b = 2;
  volatile void* c = nullptr;
  volatile void* d = &a;
  volatile const int* e = nullptr;
  volatile const int* f = &a;
  volatile const int g = 1;
  volatile const int h = 2;
  volatile const int* volatile i = nullptr;
  volatile const int* volatile j = &g;
  ScopedMockLog log;
  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, __FILE__, StartsWith("12")));
  log.StartCapturingLogs();
  LOG(INFO) << a << b << c << d << e << f << g << h << i << j;

  volatile bool b1 = true;
  bool b2 = false;
  CHECK_NE(b1, b2) << "volatile bool";
  CHECK_EQ(a, g);
  CHECK_NE(a, h);
  CHECK_EQ(i, e);
  CHECK_EQ(e, c);
  CHECK_EQ(d, &a);
}

// LOG_IF and related functions guarantee not to evaluate their outputs
// when the condition is not true.  These tests validate this so callers may
// rely on this behavior freely.

// Causes test failure for invocations that should not occur.
const char* Abort(
    const absl::SourceLocation location = absl::SourceLocation::current()) {
  LOG(FATAL).AtLocation(location) << "Should never be called";
  return "Should never be called";
}

// Verifies that conditional logging does not evaluate the message expression
// when no message is logged.
TEST(ConditionalLog, ShortCircuit) {
  // Set values to ensure no pollution of the globals from previous test cases.
  absl::SetFlag(&FLAGS_v, 0);
  absl::SetVLogLevel("base/logging_unittest", 0);
  absl::SetVLogLevel("logging*", 0);

  // Compute values at runtime to avoid false positives due to any current or
  // future special handling for constant expressions.
  bool true_val = VLOG_IS_ON(0);
  bool false_val = VLOG_IS_ON(1);
  int zero = false_val ? 1 : 0;
  int one = true_val ? 1 : 0;
  std::string zero_str = false_val ? "1" : "0";
  std::string one_str = true_val ? "1" : "0";

  // Sorted alphabetically by macro name.
  CHECK(true_val) << Abort();
  CHECK_EQ(zero, zero) << Abort();
  PCHECK(zero != -1) << Abort();
  CHECK_GE(one, zero) << Abort();
  CHECK_GT(one, zero) << Abort();
  CHECK_LE(zero, one) << Abort();
  CHECK_LT(zero, one) << Abort();
  CHECK_NE(zero, one) << Abort();
  CHECK_STREQ(zero_str.c_str(), zero_str.c_str()) << Abort();
  CHECK_STRNE(zero_str.c_str(), one_str.c_str()) << Abort();
  CHECK_STRCASEEQ(zero_str.c_str(), zero_str.c_str()) << Abort();
  CHECK_STRCASENE(zero_str.c_str(), one_str.c_str()) << Abort();

  DCHECK(true_val) << Abort();
  DCHECK_EQ(zero, zero) << Abort();
  DCHECK_GE(one, zero) << Abort();
  DCHECK_GT(one, zero) << Abort();
  DCHECK_LE(zero, one) << Abort();
  DCHECK_LT(zero, one) << Abort();
  DCHECK_NE(zero, one) << Abort();
  DCHECK_STREQ(zero_str.c_str(), zero_str.c_str()) << Abort();
  DCHECK_STRNE(zero_str.c_str(), one_str.c_str()) << Abort();
  DCHECK_STRCASEEQ(zero_str.c_str(), zero_str.c_str()) << Abort();
  DCHECK_STRCASENE(zero_str.c_str(), one_str.c_str()) << Abort();

  for (int i = 0; i < 10; ++i) {
    DLOG_EVERY_N(INFO, 3) << COUNTER << (i % 3 == 0 ? "okay" : Abort());
  }
  DLOG_IF(INFO, false_val) << Abort();
  DLOG_IF_EVERY_N(INFO, false_val, 1) << COUNTER << Abort();
  for (int i = 0; i < 10; ++i) {
    DLOG_IF_EVERY_N(INFO, true_val, 3) << (i % 3 == 0 ? "okay" : Abort());
  }

  DVLOG(1) << Abort();

  for (int i = 0; i < 10; ++i) {
    LOG_EVERY_N(INFO, 3) << (i % 3 == 0 ? "okay" : Abort());
  }
  for (int i = 0; i < 10; ++i) {
    LOG_EVERY_N_SEC(INFO, 100) << (i == 0 ? "okay" : Abort());
  }
  for (int i = 1; i <= 10; ++i) {
    LOG_EVERY_POW_2(INFO) << (i == 1 || i == 2 || i == 4 || i == 8 ? "okay"
                                                                   : Abort());
  }
  for (int i = 0; i < 10; ++i) {
    LOG_FIRST_N(INFO, 4) << (i < 4 ? "okay" : Abort());
  }
  LOG_IF(INFO, false_val) << Abort();
  LOG_IF_EVERY_N(INFO, false_val, 1) << Abort();
  for (int i = 0; i < 10; ++i) {
    LOG_IF_EVERY_N(INFO, true_val, 3) << (i % 3 == 0 ? "okay" : Abort());
  }
  LOG_IF_EVERY_N_SEC(INFO, false_val, 1) << Abort();
  for (int i = 0; i < 10; ++i) {
    LOG_IF_EVERY_N_SEC(INFO, true_val, 100) << (i == 0 ? "okay" : Abort());
  }
  LOG_IF_FIRST_N(INFO, false_val, 1) << Abort();
  for (int i = 0; i < 10; ++i) {
    LOG_IF_FIRST_N(INFO, true_val, 4) << (i < 4 ? "okay" : Abort());
  }
  // Note: Parens are necessary in the following.  See discussion
  // around the definition of BASE_LOGGING_CONDITION.
  LOG_IF(INFO, false_val) << (false_val ? "not" : "okay") << Abort();

  PCHECK(true_val) << Abort();
  for (int i = 0; i < 10; ++i) {
    PLOG_EVERY_N(INFO, 3) << (i % 3 == 0 ? "okay" : Abort());
  }
  for (int i = 0; i < 10; ++i) {
    PLOG_EVERY_N_SEC(INFO, 100) << (i == 0 ? "okay" : Abort());
  }
  PLOG_IF(INFO, false_val) << Abort();

  QCHECK(true_val) << Abort();
  QCHECK_EQ(zero, zero) << Abort();
  QCHECK_NE(zero, one) << Abort();
  QCHECK_LT(zero, one) << Abort();
  QCHECK_LE(zero, one) << Abort();
  QCHECK_GT(one, zero) << Abort();
  QCHECK_GE(one, zero) << Abort();
  QCHECK_STREQ(zero_str.c_str(), zero_str.c_str()) << Abort();
  QCHECK_STRNE(zero_str.c_str(), one_str.c_str()) << Abort();
  QCHECK_STRCASEEQ(zero_str.c_str(), zero_str.c_str()) << Abort();
  QCHECK_STRCASENE(zero_str.c_str(), one_str.c_str()) << Abort();

  VLOG(1) << Abort();
  for (int i = 0; i < 10; ++i) {
    VLOG_EVERY_N(0, 3) << (i % 3 == 0 ? "okay" : Abort());
  }
  VLOG_EVERY_N(1, 1) << Abort();
}

TEST(GetExistingTempDirectories, ReturnsSomething) {
  std::vector<std::string> dirs;
  GetExistingTempDirectories(&dirs);
  EXPECT_FALSE(dirs.empty());
}
}  // namespace
}  // namespace base_logging

int main(int argc, char** argv) {
#if !GTEST_GOOGLE3_MODE_
  testing::InitGoogleMock(&argc, argv);
#endif
#if GOOGLE_BASE_HAS_INITGOOGLE
  // Test some basics before InitGoogle:
  {
    base_logging::logging_testing::GCapturedStream before_init_google_stream(
        STDERR_FILENO,
        base_logging::GoldenStderrTest::OutputFilepath("BeforeInitGoogle.txt"));
    base_logging::TestLogWithLevels(absl::GetFlag(FLAGS_v),
                                    absl::GetFlag(FLAGS_stderrthreshold));
  }

  absl::SetFlag(&FLAGS_logtostderr, true);
  // Synchronous test of SetLogDestination fails if log lines are buffered by an
  // asynchronous thread.
  absl::SetFlag(&FLAGS_threaded_logging, false);

#ifdef GTEST_HAS_ABSL
  const char* usage = nullptr;
#else   // !GTEST_HAS_ABSL
  const char* usage = argv[0];
#endif  // GTEST_HAS_ABSL
  InitGoogle(usage, &argc, &argv, true);

#else   // !GOOGLE_BASE_HAS_INITGOOGLE
  testing::InitGoogleTest(&argc, argv);
#endif  // GOOGLE_BASE_HAS_INITGOOGLE

  QCHECK_EQ(RUN_ALL_TESTS(), 0);

#if GLOOP_INTERNAL_PROD_LOGGING
  absl::SetFlag(&FLAGS_logtostderr, false);
  base_logging::InitializeLogFileSinks();
  base_logging::TestBasename();
  base_logging::TestSymlink();
  base_logging::TestExtension();
  base_logging::TestPermissions();
  base_logging::TestGetLogPath();
  base_logging::TestGWQStatusMessage();
#endif  // GLOOP_INTERNAL_PROD_LOGGING

  fputs("PASS\n", stdout);
  return 0;
}
