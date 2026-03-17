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

#include "gloop/base/examine_stack.h"

#include <cstddef>
#include <string>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "re2/re2.h"

namespace {

using ::testing::HasSubstr;
using ::testing::IsEmpty;

// The parameter is used for recursion depth.
using ExamineStackTest = testing::TestWithParam<int>;

ABSL_ATTRIBUTE_NOINLINE void RecursiveFunction(const int depth,
                                               std::string& output,
                                               std::string& sig_output) {
  if (depth > 0) {
    VLOG(1) << &depth;  // Make it harder to eliminate tail recursion.
    RecursiveFunction(depth - 1, output, sig_output);
  } else {
    // Recursion has bottomed out, capture the current stack trace.
    auto writer = +[](const char* p, void* arg) {
      reinterpret_cast<std::string*>(arg)->append(p);
    };
    DumpStackTrace(0, writer, &output);
    DumpPCAndStackTraceForSignalHandler(/*uc=*/nullptr, writer, &sig_output);
  }
}

TEST_P(ExamineStackTest, DumpStackRespectsDumpStackTraceLimit) {
  const int limit = GetParam();
  absl::SetFlag(&FLAGS_dump_stack_frames_limit, limit);

  std::string output, sig_output;
  // Ask for a few more frames than --dump_stack_frames_limit.
  const int recursion_depth = limit + 20;
  RecursiveFunction(recursion_depth, output, sig_output);

  const auto matcher = [](const absl::string_view line) {
    return absl::StrContains(line, "RecursiveFunction()");
  };
  const int count_in_output =
      absl::c_count_if(absl::StrSplit(output, '\n'), matcher);
  EXPECT_EQ(limit, count_in_output) << output;

  const int count_in_sig_output =
      absl::c_count_if(absl::StrSplit(sig_output, '\n'), matcher);
  // The last line is replaced with "... and NN more frames",
  // so we get one fewer "RecursiveFunction()" lines.
  EXPECT_EQ(limit - 1, count_in_sig_output) << output;
}

TEST(DumpStackTrace, ReturnsEmptyWhenFrameLimitZero) {
  absl::SetFlag(&FLAGS_dump_stack_frames_limit, 0);

  // Generate a few frames, but do not dump them due to limit 0.
  std::string output, unused;
  RecursiveFunction(/*depth=*/20, output, /*sig_output=*/unused);

  EXPECT_THAT(output, IsEmpty());
}

INSTANTIATE_TEST_SUITE_P(DumpStack, ExamineStackTest,
                         // Recursion depths:
                         testing::Values(32, 64, 128, 1000, 8000, 20000));

// Multi-line matching of DEATH_TEST is awkward, so use a custom matcher.
//
// This matcher verifies that we get expected output after
// the "--- CPU registers: ---" line:
//  - lines should not be longer than 80 characters
//  - they should contain expected registers.
MATCHER(HasRegisterDump, "") {
  const std::vector<absl::string_view> regs = {
#if defined(__x86_64__)
      "r8",  "r14", "rbp", "rsp", "rsi",
      "rip", "cr2", "trp"
#elif defined(__aarch64__)
      "x0", "x30", "sp", "pc", "pstate"
#elif defined(__riscv__)
      "pc",
      "ra",
      "sp",
      "gp",
      "tp",
      "t6",
      "s1",
      "a7",
      "s"
      "11"
#elif defined(__powerpc__)
      "r0",   "r31",   "nip", "msr",
      "trap", "result"
#endif
  };
  bool saw_cpu_registers = false;
  size_t regcount = 0;
  for (const absl::string_view line : absl::StrSplit(arg, "\n")) {
    if (!saw_cpu_registers) {
      saw_cpu_registers = absl::StrContains(line, "--- CPU registers: ---");
      continue;
    }
    if (line[0] == 'W') {
      // Reached the end of CPU register dump.
      break;
    }

    // We are after the "CPU registers:" and before "Stack contents:" lines.
    if (line.size() > 80) {
      // Line wrapping didn't work.
      *result_listener << "line too long: \"" << line << "\"";
      return false;
    }
    for (const absl::string_view reg : regs) {
      const std::string regexp = absl::StrCat(reg, "=[[:xdigit:]]+");
      if (RE2::PartialMatch(line, regexp)) regcount += 1;
    }
  }
  // Did we see all the registers we expected to see?
  *result_listener << "Found " << regcount << " expected " << regs.size();
  return regcount == regs.size();
}

}  // namespace
