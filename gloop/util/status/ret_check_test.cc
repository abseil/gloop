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

#include "gloop/util/status/ret_check.h"

#include <ostream>
#include <string>

#include "absl/base/log_severity.h"
#include "absl/flags/flag.h"
#include "absl/log/scoped_mock_log.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gloop/gloop_test.h"
#include "gloop/util/status/status_macros.h"

namespace not_util {

// Define a different namespace `util` and  `util::Status` just to make life
// harder and ensure that macros expand correctly.
namespace util {
struct Status;  // NOLINT
}  // namespace util

namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::_;
using ::testing::AllOf;
using ::testing::HasSubstr;
using ::testing::Not;

// Matcher to verify that an error message has all the parts we guarantee.
auto HasRCheckMessage(const char* expected) {
  return AllOf(HasSubstr("RET_CHECK"), HasSubstr("TRIGGERED"),
               Not(HasSubstr("IGNORED")), HasSubstr(expected));
}

// Matcher to verify that a status is an RCheck error with the given message.
testing::Matcher<const absl::Status&> IsRCheckError(const char* expected) {
  return StatusIs(absl::StatusCode::kInternal, HasRCheckMessage(expected));
}

TEST(StatusMacrosChecksTest, RCheckFailure) {
  auto func = []() -> absl::Status {
    RET_CHECK_FAIL() << "TRIGGERED";
    return absl::OkStatus();
  };

  absl::ScopedMockLog log;
  log.StartCapturingLogs();
  EXPECT_CALL(log,
              Log(absl::LogSeverity::kError, _, HasRCheckMessage(__func__)))
      .Times(1);
  EXPECT_THAT(func(), IsRCheckError(""));
}

TEST(StatusMacrosChecksTest, RQCheckFailure) {
  auto func = []() -> absl::Status {
    RET_QCHECK_FAIL() << "TRIGGERED";
    return absl::OkStatus();
  };

  absl::ScopedMockLog log;
  log.StartCapturingLogs();
  // Unlike `RET_CHECK`, `RET_QCHECK` should not log.
  EXPECT_CALL(log, Log(_, _, _)).Times(0);
  EXPECT_THAT(func(), IsRCheckError(""));
}

TEST(StatusMacrosChecksTest, Bool) {
  auto func = []() -> absl::Status {
    RET_CHECK(true) << "IGNORED";
    RET_CHECK(false) << "TRIGGERED";
    return absl::OkStatus();
  };

  absl::ScopedMockLog log;
  log.StartCapturingLogs();
  EXPECT_CALL(log,
              Log(absl::LogSeverity::kError, _, HasRCheckMessage(__func__)))
      .Times(1);
  EXPECT_THAT(func(), IsRCheckError("false"));
}

TEST(StatusMacrosChecksTest, QBool) {
  auto func = []() -> absl::Status {
    RET_QCHECK(true) << "IGNORED";
    RET_QCHECK(false) << "TRIGGERED";
    return absl::OkStatus();
  };

  absl::ScopedMockLog log;
  log.StartCapturingLogs();
  // Unlike `RET_CHECK`, `RET_QCHECK` should not log.
  EXPECT_CALL(log, Log(_, _, _)).Times(0);
  EXPECT_THAT(func(), IsRCheckError("false"));
}

TEST(StatusMacrosChecksTest, Eq) {
  auto func = []() -> absl::Status {
    RET_CHECK_EQ(2, 2) << "IGNORED";
    RET_CHECK_EQ(3, 4) << "TRIGGERED";
    return absl::OkStatus();
  };

  absl::ScopedMockLog log;
  log.StartCapturingLogs();
  EXPECT_CALL(log,
              Log(absl::LogSeverity::kError, _, HasRCheckMessage(__func__)))
      .Times(1);
  EXPECT_THAT(func(), IsRCheckError("3 == 4"));
}

TEST(StatusMacrosChecksTest, QEq) {
  auto func = []() -> absl::Status {
    RET_QCHECK_EQ(2, 2) << "IGNORED";
    RET_QCHECK_EQ(3, 4) << "TRIGGERED";
    return absl::OkStatus();
  };

  absl::ScopedMockLog log;
  log.StartCapturingLogs();
  // Unlike `RET_CHECK_EQ`, `RET_QCHECK_EQ` should not log.
  EXPECT_CALL(log, Log(_, _, _)).Times(0);
  EXPECT_THAT(func(), IsRCheckError("3 == 4"));
}

TEST(StatusMacrosChecksTest, Ne) {
  auto func = []() -> absl::Status {
    RET_CHECK_NE(3, 4) << "IGNORED";
    RET_CHECK_NE(2, 2) << "TRIGGERED";
    return absl::OkStatus();
  };

  absl::ScopedMockLog log;
  log.StartCapturingLogs();
  EXPECT_CALL(log,
              Log(absl::LogSeverity::kError, _, HasRCheckMessage(__func__)))
      .Times(1);
  EXPECT_THAT(func(), IsRCheckError("2 != 2"));
}

TEST(StatusMacrosChecksTest, QNe) {
  auto func = []() -> absl::Status {
    RET_QCHECK_NE(3, 4) << "IGNORED";
    RET_QCHECK_NE(2, 2) << "TRIGGERED";
    return absl::OkStatus();
  };

  absl::ScopedMockLog log;
  log.StartCapturingLogs();
  // Unlike `RET_CHECK_NE`, `RET_QCHECK_NE` should not log.
  EXPECT_CALL(log, Log(_, _, _)).Times(0);
  EXPECT_THAT(func(), IsRCheckError("2 != 2"));
}

TEST(StatusMacrosChecksTest, Le) {
  auto func = []() -> absl::Status {
    RET_CHECK_LE(2, 2) << "IGNORED";
    RET_CHECK_LE(4, 3) << "TRIGGERED";
    return absl::OkStatus();
  };

  absl::ScopedMockLog log;
  log.StartCapturingLogs();
  EXPECT_CALL(log,
              Log(absl::LogSeverity::kError, _, HasRCheckMessage(__func__)))
      .Times(1);
  EXPECT_THAT(func(), IsRCheckError("4 <= 3"));
}

TEST(StatusMacrosChecksTest, QLe) {
  auto func = []() -> absl::Status {
    RET_QCHECK_LE(2, 2) << "IGNORED";
    RET_QCHECK_LE(4, 3) << "TRIGGERED";
    return absl::OkStatus();
  };

  absl::ScopedMockLog log;
  log.StartCapturingLogs();
  // Unlike `RET_CHECK_LE`, `RET_QCHECK_LE` should not log.
  EXPECT_CALL(log, Log(_, _, _)).Times(0);
  EXPECT_THAT(func(), IsRCheckError("4 <= 3"));
}

TEST(StatusMacrosChecksTest, Lt) {
  auto func = []() -> absl::Status {
    RET_CHECK_LT(2, 3) << "IGNORED";
    RET_CHECK_LT(4, 4) << "TRIGGERED";
    return absl::OkStatus();
  };

  absl::ScopedMockLog log;
  log.StartCapturingLogs();
  EXPECT_CALL(log,
              Log(absl::LogSeverity::kError, _, HasRCheckMessage(__func__)))
      .Times(1);
  EXPECT_THAT(func(), IsRCheckError("4 < 4"));
}

TEST(StatusMacrosChecksTest, QLt) {
  auto func = []() -> absl::Status {
    RET_QCHECK_LT(2, 3) << "IGNORED";
    RET_QCHECK_LT(4, 4) << "TRIGGERED";
    return absl::OkStatus();
  };

  absl::ScopedMockLog log;
  log.StartCapturingLogs();
  // Unlike `RET_CHECK_LT`, `RET_QCHECK_LT` should not log.
  EXPECT_CALL(log, Log(_, _, _)).Times(0);
  EXPECT_THAT(func(), IsRCheckError("4 < 4"));
}

TEST(StatusMacrosChecksTest, Ge) {
  auto func = []() -> absl::Status {
    RET_CHECK_GE(2, 2) << "IGNORED";
    RET_CHECK_GE(3, 4) << "TRIGGERED";
    return absl::OkStatus();
  };

  absl::ScopedMockLog log;
  log.StartCapturingLogs();
  EXPECT_CALL(log,
              Log(absl::LogSeverity::kError, _, HasRCheckMessage(__func__)))
      .Times(1);
  EXPECT_THAT(func(), IsRCheckError("3 >= 4"));
}

TEST(StatusMacrosChecksTest, QGe) {
  auto func = []() -> absl::Status {
    RET_QCHECK_GE(2, 2) << "IGNORED";
    RET_QCHECK_GE(3, 4) << "TRIGGERED";
    return absl::OkStatus();
  };

  absl::ScopedMockLog log;
  log.StartCapturingLogs();
  // Unlike `RET_CHECK_GE`, `RET_QCHECK_GE` should not log.
  EXPECT_CALL(log, Log(_, _, _)).Times(0);
  EXPECT_THAT(func(), IsRCheckError("3 >= 4"));
}

TEST(StatusMacrosChecksTest, Gt) {
  auto func = []() -> absl::Status {
    RET_CHECK_GT(3, 2) << "IGNORED";
    RET_CHECK_GT(4, 4) << "TRIGGERED";
    return absl::OkStatus();
  };

  absl::ScopedMockLog log;
  log.StartCapturingLogs();
  EXPECT_CALL(log,
              Log(absl::LogSeverity::kError, _, HasRCheckMessage(__func__)))
      .Times(1);
  EXPECT_THAT(func(), IsRCheckError("4 > 4"));
}

TEST(StatusMacrosChecksTest, QGt) {
  auto func = []() -> absl::Status {
    RET_QCHECK_GT(3, 2) << "IGNORED";
    RET_QCHECK_GT(4, 4) << "TRIGGERED";
    return absl::OkStatus();
  };

  absl::ScopedMockLog log;
  log.StartCapturingLogs();
  // Unlike `RET_CHECK_GT`, `RET_QCHECK_GT` should not log.
  EXPECT_CALL(log, Log(_, _, _)).Times(0);
  EXPECT_THAT(func(), IsRCheckError("4 > 4"));
}

TEST(StatusMacrosChecksTest, Ok) {
  auto func = []() -> absl::Status {
    RET_CHECK_OK(absl::OkStatus()) << "IGNORED";
    RET_CHECK_OK(absl::Status(absl::StatusCode::kUnknown, "zomg"))
        << "TRIGGERED";
    return absl::OkStatus();
  };

  absl::ScopedMockLog log;
  log.StartCapturingLogs();
  EXPECT_CALL(log,
              Log(absl::LogSeverity::kError, _, HasRCheckMessage(__func__)))
      .Times(1);
  EXPECT_THAT(func(), IsRCheckError("zomg"));
}

TEST(StatusMacrosChecksTest, OkEvalCount) {
  int i = 0;
  auto func = [&]() -> absl::Status {
    RET_CHECK_OK(++i % 2 ? absl::InternalError("foo")
                         : absl::InternalError("bar"))
        << "TRIGGERED";
    return absl::OkStatus();
  };
  EXPECT_THAT(func(), IsRCheckError("foo"));
  EXPECT_EQ(i, 1);
}

TEST(StatusMacrosChecksTest, OkInIf) {
  auto func = []() -> absl::Status {
    if (false) RET_CHECK_OK(absl::UnknownError("not_zomg")) << "TRIGGERED";
    if (true) RET_CHECK_OK(absl::UnknownError("zomg")) << "TRIGGERED";
    return absl::OkStatus();
  };

  absl::ScopedMockLog log;
  log.StartCapturingLogs();
  EXPECT_CALL(log,
              Log(absl::LogSeverity::kError, _, HasRCheckMessage(__func__)))
      .Times(1);
  EXPECT_THAT(func(), IsRCheckError("zomg"));
}

TEST(StatusMacrosChecksTest, QOk) {
  auto func = []() -> absl::Status {
    RET_QCHECK_OK(absl::OkStatus()) << "IGNORED";
    RET_QCHECK_OK(absl::Status(absl::StatusCode::kUnknown, "zomg"))
        << "TRIGGERED";
    return absl::OkStatus();
  };

  absl::ScopedMockLog log;
  log.StartCapturingLogs();
  // Unlike `RET_CHECK_OK`, `RET_QCHECK_OK` should not log.
  EXPECT_CALL(log, Log(_, _, _)).Times(0);
  EXPECT_THAT(func(), IsRCheckError("zomg"));
}

TEST(StatusMacrosChecksTest, StatusOrOk) {
  auto func = []() -> absl::Status {
    int val = 45;
    absl::StatusOr<int*> ok = &val;
    absl::StatusOr<int*> ok_null = nullptr;
    absl::StatusOr<int*> not_ok =
        absl::Status(absl::StatusCode::kUnknown, "zomg");
    RET_CHECK_OK(ok) << "IGNORED";
    RET_CHECK_OK(ok_null) << "IGNORED";
    RET_CHECK_OK(not_ok) << "TRIGGERED";
    return absl::OkStatus();
  };

  absl::ScopedMockLog log;
  log.StartCapturingLogs();
  EXPECT_CALL(log,
              Log(absl::LogSeverity::kError, _, HasRCheckMessage(__func__)))
      .Times(1);
  EXPECT_THAT(func(), IsRCheckError("zomg"));
}

TEST(StatusMacrosChecksTest, LocalVars) {
  auto func = [](bool condition, const char* msg) -> absl::Status {
    RET_CHECK(condition) << msg;
    return absl::OkStatus();
  };

  absl::ScopedMockLog log;
  log.StartCapturingLogs();
  EXPECT_CALL(log,
              Log(absl::LogSeverity::kError, _, HasRCheckMessage(__func__)))
      .Times(1);
  EXPECT_THAT(func(true, "IGNORED"), IsOk());
  EXPECT_THAT(func(false, "TRIGGERED"), IsRCheckError("condition"));
}

TEST(StatusMacrosChecksTest, NullStr) {
  auto func = [](const char* var, const char* msg) -> absl::Status {
    RET_CHECK_NE(var, nullptr) << msg;
    return absl::OkStatus();
  };

  absl::ScopedMockLog log;
  log.StartCapturingLogs();
  EXPECT_CALL(log,
              Log(absl::LogSeverity::kError, _, HasRCheckMessage(__func__)))
      .Times(1);
  EXPECT_THAT(func("", "IGNORED"), IsOk());
  EXPECT_THAT(func(nullptr, "TRIGGERED"), IsRCheckError("var != nullptr"));
}

TEST(StatusMacrosChecksTest, MutableNullStr) {
  auto func = [](char* var, const char* msg) -> absl::Status {
    RET_CHECK_NE(var, nullptr) << msg;
    return absl::OkStatus();
  };

  absl::ScopedMockLog log;
  log.StartCapturingLogs();
  EXPECT_CALL(log,
              Log(absl::LogSeverity::kError, _, HasRCheckMessage(__func__)))
      .Times(1);
  EXPECT_THAT(func(nullptr, "TRIGGERED"), IsRCheckError("var != nullptr"));
}

TEST(StatusMacrosChecksTest, LocalVarsOp) {
  auto func = [](int x, int y, const char* msg) -> absl::Status {
    RET_CHECK_EQ(x, y) << msg;
    return absl::OkStatus();
  };

  absl::ScopedMockLog log;
  log.StartCapturingLogs();
  EXPECT_CALL(log,
              Log(absl::LogSeverity::kError, _, HasRCheckMessage(__func__)))
      .Times(1);
  EXPECT_THAT(func(1, 1, "IGNORED"), IsOk());
  EXPECT_THAT(func(2, 3, "TRIGGERED"), IsRCheckError("x == y"));
}

// A type that has no ostream support.
struct NeedsAbslStringify {
  int i;

  bool operator==(const NeedsAbslStringify& o) const { return i == o.i; }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const NeedsAbslStringify& foo) {
    absl::Format(&sink, "[%v]", foo.i);
  }
};

TEST(StatusMacrosChecksTest, PrintingSupportsAbslStringify) {
  auto func = []() -> absl::Status {
    RET_CHECK_EQ(NeedsAbslStringify{2}, NeedsAbslStringify{3}) << "TRIGGERED";
    return absl::OkStatus();
  };

  EXPECT_THAT(func(), IsRCheckError("[2] vs. [3]"));
}

// A type that has both ostream support and AbslStringify.
struct HasAbslStringify {
  int i;

  bool operator==(const HasAbslStringify& h) const { return i == h.i; }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const HasAbslStringify& h) {
    absl::Format(&sink, "Stringify-%v", h.i);
  }

  friend std::ostream& operator<<(std::ostream& os, const HasAbslStringify& h) {
    return os << "Stream-" << h.i;
  }
};

TEST(StatusMacrosChecksTest, PrintingDefaultsToStreams) {
  // Confirm the test setup -- that the type does in fact have AbslStringify
  // support.
  ASSERT_EQ("Stringify-3", absl::StrCat(HasAbslStringify{3}));

  auto func = []() -> absl::Status {
    RET_CHECK_EQ(HasAbslStringify{2}, HasAbslStringify{3}) << "TRIGGERED";
    return absl::OkStatus();
  };

  EXPECT_THAT(func(), IsRCheckError("Stream-2 vs. Stream-3"));
}

TEST(StatusMacrosChecksTest, AbortOnFailure) {
  // Make RET_CHECK* abort on failure.
  absl::SetFlag(&FLAGS_ret_check_abort_on_failure, true);

  auto func_bool = [](bool condition, const char* msg) -> absl::Status {
    RET_CHECK(condition) << msg;
    return absl::OkStatus();
  };

  EXPECT_THAT(func_bool(true, "IGNORED"), IsOk());
  GLOOP_EXPECT_DEATH(
      { func_bool(false, "TRIGGERED").IgnoreError(); },
      "RET_CHECK.*condition.*TRIGGERED");

  auto func_eq = [](int x, int y, const char* msg) -> absl::Status {
    RET_CHECK_EQ(x, y) << msg;
    return absl::OkStatus();
  };

  EXPECT_THAT(func_eq(2, 2, "IGNORED"), IsOk());
  GLOOP_EXPECT_DEATH(
      { func_eq(1, 2, "TRIGGERED").IgnoreError(); },
      "RET_CHECK.*x == y.*TRIGGERED");
}

void BM_RetQCheckOk_Ok(benchmark::State& state) {
  auto f = [](absl::Status status) -> absl::Status {
    RET_QCHECK_OK(status);
    return absl::OkStatus();
  };
  for (auto _ : state) {
    absl::Status result = f(absl::OkStatus());
    benchmark::DoNotOptimize(result);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_RetQCheckOk_Ok);

void BM_RetQCheckOk_Error(benchmark::State& state) {
  auto f = [](absl::Status status) -> absl::Status {
    RET_QCHECK_OK(status);
    return absl::OkStatus();
  };
  for (auto _ : state) {
    absl::Status result = f(absl::InternalError("TRIGGERED"));
    benchmark::DoNotOptimize(result);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_RetQCheckOk_Error);

void BM_ReturnIfError_Ok(benchmark::State& state) {
  auto f = [](absl::Status status) -> absl::Status {
    RETURN_IF_ERROR(status);
    return absl::OkStatus();
  };
  for (auto _ : state) {
    absl::Status result = f(absl::OkStatus());
    benchmark::DoNotOptimize(result);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ReturnIfError_Ok);

void BM_ReturnIfError_Error(benchmark::State& state) {
  auto f = [](absl::Status status) -> absl::Status {
    RETURN_IF_ERROR(status);
    return absl::OkStatus();
  };
  for (auto _ : state) {
    absl::Status result = f(absl::InternalError("TRIGGERED"));
    benchmark::DoNotOptimize(result);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ReturnIfError_Error);

}  // namespace
}  // namespace not_util
