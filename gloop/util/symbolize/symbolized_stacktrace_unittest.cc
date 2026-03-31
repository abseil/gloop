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

// Unit test for stack trace functions.

#include "gloop/util/symbolize/symbolized_stacktrace.h"

#include <execinfo.h>
#include <setjmp.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/debugging/internal/symbolize.h"
#include "absl/debugging/stacktrace.h"
#include "absl/flags/flag.h"
#include "absl/functional/function_ref.h"
#include "absl/log/flags.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "gloop/base/examine_stack.h"
#include "gloop/util/symbolize/symbolize.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using ::testing::HasSubstr;
using ::testing::Not;

TEST(util, GetSymbolizedStackTrace) {
  std::vector<std::string> stack;
  util::GetSymbolizedStackTrace(&stack, 100, 0);
  // At least it should contain "main";
  for (const auto& s : stack) {
    if (absl::EndsWith(absl::string_view(s), "main")) return;
  }
  EXPECT_TRUE(false) << "Did not find 'main' in stack:\n  "
                     << absl::StrJoin(stack, "\n  ");
}

TEST(util, NoDemangling) {
  std::string trace = util::GetSymbolizedStackTraceAsString(100, 0, false);
  LOG(INFO) << "@@stacktrace\n" << trace;

  // Should have a mangled version of testing::.
  EXPECT_THAT(trace, HasSubstr("_ZN7testing"));
}

ABSL_ATTRIBUTE_NOINLINE std::string StackTraceCaller(int skip_count) {
  // Helps to keep frame in the stack trace.
  VLOG(1) << "@@frame\n" << __builtin_frame_address(0);
  return util::GetSymbolizedStackTraceAsString(100, skip_count);
}

TEST(util, SkipCountWorks) {
  std::vector<std::string> stack0;
  std::vector<std::string> stack1;
  util::GetSymbolizedStackTrace(&stack0, 100, 0);
  util::GetSymbolizedStackTrace(&stack1, 100, 1);

  // clang-format off
  ASSERT_EQ(stack0.size(), stack1.size() + 1)
      << "Size mismatch:\n"
      << "stack0:\n" << absl::StrJoin(stack0, "\n")
      << "\nstack1:\n" << absl::StrJoin(stack1, "\n");
  // clang-format on

  // With skip_count==0, this method (util_SkipCountWorks_Test::TestBody)
  // should be present.
  EXPECT_THAT(stack0[0], HasSubstr("SkipCountWorks"));
  // With skip_count==1 SkipCountWorks should not appear.
  EXPECT_THAT(stack1[0], Not(HasSubstr("SkipCountWorks")));

  // stack0[j+1] should match stack1[j]
  for (int j = 0; j < stack1.size(); j++) {
    EXPECT_EQ(stack0[j + 1], stack1[j]);
  }

  VLOG(1) << "@@frame\n" << __builtin_frame_address(0);
  std::string trace_skip_0 = StackTraceCaller(0);
  std::string trace_skip_1 = StackTraceCaller(1);
  VLOG(1) << "@@trace0\n" << trace_skip_0;
  VLOG(1) << "@@trace1\n" << trace_skip_1;

  EXPECT_THAT(trace_skip_0, HasSubstr("StackTraceCaller"));
  EXPECT_THAT(trace_skip_0, HasSubstr("SkipCountWorks"));

  EXPECT_THAT(trace_skip_1, Not(HasSubstr("StackTraceCaller")));
  EXPECT_THAT(trace_skip_1, HasSubstr("SkipCountWorks"));
}

TEST(util, GetSymbolizedStackTraceAsString) {
  std::string trace = util::GetSymbolizedStackTraceAsString(100);
  LOG(INFO) << "@@stacktrace\n" << trace;
  // At least it should contain "main";
  EXPECT_NE(strstr(trace.c_str(), "main"), nullptr);
  // Should have a demangled version of testing::.
  EXPECT_THAT(trace, HasSubstr("testing::"));

  // Print stack trace using glibc's function.
  void* stack[100];
  int n = backtrace(stack, ABSL_ARRAYSIZE(stack));
  backtrace_symbols_fd(stack, n, 2);  // To stderr.
}

TEST(util, SymbolizeStackTraceAsString) {
  void* addresses[100];
  int depth = absl::GetStackTrace(addresses, 100, 1);
  std::string trace = util::SymbolizeStackTraceAsString(addresses, depth);
  LOG(INFO) << "@@stacktrace\n" << trace;
  // Should end with a newline.
  EXPECT_EQ('\n', trace.back());
  // At least it should contain "main";
  EXPECT_NE(strstr(trace.c_str(), "main"), nullptr);
}

TEST(util, SymbolizeStackTrace) {
  std::vector<std::string> stack;
  void* addresses[100];
  int depth = absl::GetStackTrace(addresses, 100, 1);
  util::SymbolizeStackTrace(addresses, depth, &stack);
  // At least it should contain "main";
  for (const auto& s : stack) {
    if (absl::EndsWith(absl::string_view(s), "main")) return;
  }
  EXPECT_TRUE(false) << "Did not find 'main' in stack:\n  "
                     << absl::StrJoin(stack, "\n  ");
}

// The two global variables are used in NoReturnFunctionTest().
static std::string* g_stacktrace;
static jmp_buf g_jmp_buf;

static void NoReturnFunc() ABSL_ATTRIBUTE_NORETURN;
static void FuncToCallNoReturnFunc() ABSL_ATTRIBUTE_NOINLINE;

static void NoReturnFunc() {
  g_stacktrace = new std::string(util::GetSymbolizedStackTraceAsString(100));
  const int kReturnValue = 123;  // Just an arbitrary non-zero number.
  // This is tricky but we return to the original caller by longjmp(),
  // as we cannot "return" if a function is attributed with "noreturn".
  longjmp(g_jmp_buf, kReturnValue);
  LOG(FATAL) << "Should not reach";
}

static void FuncToCallNoReturnFunc() { NoReturnFunc(); }

TEST(util, NoReturnFunctionTest) {
  // There was a bug which caused symbolized stack traces to
  // occasionally contain incorrect function names when a "noreturn"
  // function is called at the end of a function.
  if (setjmp(g_jmp_buf) == 0) {  // First time.
    FuncToCallNoReturnFunc();
  } else {  // Back from longjmp().
    // Should contain FuncToCallNoReturnFunc.  This didn't appear
    // before fixing the "pc - 1" bug.
    EXPECT_NE(strstr(g_stacktrace->c_str(), "FuncToCallNoReturnFunc"), nullptr);
    delete g_stacktrace;
  }
}

// The purpose of this class is to confirm that the non-mutating methods
// SymbolizeStackTraceAsString and SymbolizeStackTrace take a 'stack' arg with
// proper constness. The const methods of this class will not compile
// otherwise.
class StackCapturer {
 public:
  StackCapturer() : depth_(0) {}
  void CaptureStackTrace() {
    depth_ = absl::GetStackTrace(addresses_, kMaxDepth, 0);
  }
  std::string GetSymbolizedStackTraceAsString() const {
    return util::SymbolizeStackTraceAsString(addresses_, depth_);
  }
  void GetSymbolizedStackTrace(std::vector<std::string>* vector) const {
    util::SymbolizeStackTrace(addresses_, depth_, vector);
  }

 private:
  static constexpr int kMaxDepth = 32;
  void* addresses_[kMaxDepth];
  int depth_;
};

TEST(util, CheckSymbolizedStackTraceConstCorrectness) {
  StackCapturer capturer;
  capturer.CaptureStackTrace();
  capturer.GetSymbolizedStackTraceAsString();
  std::vector<std::string> dummy;
  capturer.GetSymbolizedStackTrace(&dummy);
}

TEST(util, CurrentStackTraceWorks) {
  // ::CurrentStackTrace decorates symbols with file/line info in debug builds.
  absl::debugging_internal::RemoveAllSymbolDecorators();
  const auto str1 = util::CurrentStackTrace();
  VLOG(1) << "util::CurrentStackTrace\n" << str1;
  const auto str2 = CurrentStackTrace();
  VLOG(1) << "::CurrentStackTrace\n" << str2;

  // The outputs are mostly the same, but template arguments are decoded in
  // the util:: version.
  std::vector<std::string> v1 = absl::StrSplit(str1, '\n');
  std::vector<std::string> v2 = absl::StrSplit(str2, '\n');

  ASSERT_GT(v1.size(), 0);
  ASSERT_GT(v2.size(), 0);

  int j = 0;
  // Both should start with "Stack trace:"
  EXPECT_EQ(v1[j], v2[j]);
  EXPECT_EQ(v1[j], "Stack trace:");
  j += 1;

  if (absl::StrContains(v2[j], "  CurrentStackTrace()")) {
    // On PPC, ::CurrentStackTrace sets skip_count=0 because it is alleged to
    // be tail-call optimized. This doesn't actually happen with current Clang.
    // Ignore that entry.
    LOG(INFO) << "Detected a bug in ::CurrentStackTrace. Ignoring line:\n"
              << v2[j];
    v2.erase(v2.begin() + j);
  }

  // clang-format off
  ASSERT_EQ(v1.size(), v2.size()) << "Stacks differ:\n"
                                  << "util::CurrentStackTace:\n" << str1
                                  << "\n::CurrentStackTrace:\n" << str2;
  // clang-format on

  // The next level corresponds to two different lines above where we actually
  // capture the two stacks, and has different PCs.
  EXPECT_THAT(v1[j], HasSubstr("CurrentStackTraceWorks"));
  EXPECT_THAT(v2[j], HasSubstr("CurrentStackTraceWorks"));
  j += 1;

  // Compare other levels, but ignore template demangling.
  for (; j < v1.size(); j++) {
    if (!absl::StrContains(v1[j], '<')) {
      EXPECT_EQ(v1[j], v2[j]);
    } else {
      VLOG(1) << "Ignoring (template)" << v1[j];
      VLOG(1) << "Ignoring (template)" << v2[j];
    }
  }
}

TEST(util, CurrentStackTraceWithUrlWorks) {
  const auto str1 = util::CurrentStackTrace(false);
  EXPECT_THAT(str1, Not(HasSubstr("Call Stack URL:")));
}

ABSL_ATTRIBUTE_NOINLINE
ABSL_ATTRIBUTE_NO_TAIL_CALL
// See <internal thread>?e=48417069
void CallFnFromDeeplyNested(int n_recur,
                            absl::FunctionRef<std::string(void)> fn) {
  if (n_recur > 0) {
    CallFnFromDeeplyNested(n_recur - 1, fn);
  } else {
    fn();
  }
}

// Note: this benchmark provides unrealistically fast times for
// ::CurrentStackTrace, because it will have 100% cache hit ratio.
// To get a realistic result, disable Symbolizer::InsertSymbolInCache.
void BM_CurrentStackTrace(benchmark::State& state) {
  for (auto _ : state) {
    CallFnFromDeeplyNested(50, ::CurrentStackTrace);
  }
}
BENCHMARK(BM_CurrentStackTrace);

void BM_UtilCurrentStackTrace(benchmark::State& state) {
  auto fn = [] { return ::util::CurrentStackTrace(false); };
  for (auto _ : state) {
    CallFnFromDeeplyNested(50, fn);
  }
}
BENCHMARK(BM_UtilCurrentStackTrace);

}  // namespace

ABSL_ATTRIBUTE_NO_TAIL_CALL int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);

  if (util::SymbolMap::GetCached().binary_is_stripped()) {
    LOG(INFO) << "We cannot run this test if stripped";
    return 0;
  }
  if (!benchmark::GetBenchmarkFilter().empty()) {
    benchmark::RunSpecifiedBenchmarks();
    exit(0);
  }
  return RUN_ALL_TESTS();
}
