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

// Unit test for SymbolizedStackWriter class.

#include "gloop/util/symbolize/symbolized_stack_writer.h"

#include <string.h>

#include <memory>
#include <string>

#include "absl/base/log_severity.h"
#include "absl/base/macros.h"
#include "absl/log/log.h"
#include "absl/log/scoped_mock_log.h"
#include "absl/strings/string_view.h"
#include "gloop/thread/thread.h"
#include "gloop/util/symbolize/demangle.h"
#include "gloop/util/symbolize/symbolize.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::HasSubstr;

using util::SymbolizedStackWriter;
using util::SymbolMap;

struct TestCase {
  const char* input;
  const char* output;
};

// Parameterized by compression level.
class SymbolizedStackWriterTest : public testing::TestWithParam<int> {};

TEST_P(SymbolizedStackWriterTest, Write) {
  std::unique_ptr<SymbolMap> symbol_map(
      SymbolMap::GetEmpty(/*compression_level=*/GetParam()));
  symbol_map->AddSymbol("foo", 0x40, 0x40);
  symbol_map->AddSymbol("bar", 0x80, 0x60);
  symbol_map->AddSymbol("baz", 0xe0, 0x20);
  symbol_map->AddSymbol("main", 0x1234, 0x50);  // add main to appear unstripped

  TestCase test_cases[] = {
      {"", ""},
      {"foo", "foo"},
      {"abcdef", "abcdef"},  // a hexadecimal number without 0x
      {"0x00", "  0x00000000: (unknown)\n"},
      {"0x40", "  0x00000040: foo\n"},
      {"0x79", "  0x00000079: foo\n"},
      {"0x80", "  0x00000080: bar\n"},
      {"0xe0", "  0x000000e0: baz\n"},
      {"0xff", "  0x000000ff: baz\n"},
      {"0x100", "  0x00000100: (unknown)\n"},
      {"0x00  0x40\n0x80   0xe0\n\n  0x100\n",
       "  0x00000000: (unknown)\n"
       "  0x00000040: foo\n"
       "  0x00000080: bar\n"
       "  0x000000e0: baz\n"
       "  0x00000100: (unknown)\n"},
  };

  for (int i = 0; i < ABSL_ARRAYSIZE(test_cases); ++i) {
    std::string output;
    SymbolizedStackWriter writer(*symbol_map, &output);
    writer.Write(test_cases[i].input, strlen(test_cases[i].input));
    ASSERT_EQ(output, test_cases[i].output);
  }
}

TEST(SymbolizedStackWriterTest, WriteNoSymbolization) {
  std::unique_ptr<SymbolMap> symbol_map(SymbolMap::GetEmpty());
  symbol_map->AddSymbol("foo", 0x40, 0x40);
  std::string output;
  SymbolizedStackWriter writer(*symbol_map, &output);
  writer.Write("0x40", strlen("0x40"),
               ThreadStackWriterOptions{.symbolize = false});
  ASSERT_EQ(output, "0x40");
}

TEST(SymbolizedStackWriterTest, WriteDuplicated) {
  std::unique_ptr<SymbolMap> symbol_map(SymbolMap::GetEmpty());
  std::string output;
  SymbolizedStackWriter writer(*symbol_map, &output);
  static constexpr absl::string_view kInput1 =
      "--- Thread 5 (name: T5) stack: ---\n"
      "  0x40\n"
      "  0x41\n"
      "  0x42\n";
  writer.Write(kInput1.data(), kInput1.size(), {});
  static constexpr absl::string_view kInput2 =
      "--- Thread 6 (name: T6) stack: ---\n"
      "  0x40\n"
      "  0x41\n"
      "  0x42\n";
  writer.Write(kInput2.data(), kInput2.size(), {});
  EXPECT_THAT(output, HasSubstr("--- Thread 5 (name: T5) stack: ---\n"
                                "  0x40\n"
                                "  0x41\n"
                                "  0x42\n"
                                "--- Thread 6 (name: T6) stack: ---\n"
                                "  [same as previous thread]\n"));
}

// Test with demangling.
#ifndef GLOOP_UNSUPPORTED_LIBSTDCXX  // Extra characters inserted in libstdc++
TEST_P(SymbolizedStackWriterTest, Write2) {
  if (!util::DemanglingIsSupported()) {
    LOG(INFO) << "demangling isn't supported on this platform";
    return;
  }

  std::unique_ptr<SymbolMap> symbol_map(
      SymbolMap::GetEmpty(/*compression_level=*/GetParam()));
  // Symbol names are mangled with GCC 4.0's ABI.
  symbol_map->AddSymbol("foo", 0x40, 0x40);
  symbol_map->AddSymbol("_Z3barv", 0x80, 0x60);
  symbol_map->AddSymbol("_Z3bazi", 0xe0, 0x20);
  symbol_map->AddSymbol("main", 0x1234, 0x50);  // add main to appear unstripped

  TestCase test_cases[] = {
      {"0x40", "  0x00000040: foo\n"},
      {"0x80", "  0x00000080: bar()\n"},
      {"0xe0", "  0x000000e0: baz(int)\n"},
      {"0x00  0x40\n0x80   0xe0\n\n  0x100\n",
       "  0x00000000: (unknown)\n"
       "  0x00000040: foo\n"
       "  0x00000080: bar()\n"
       "  0x000000e0: baz(int)\n"
       "  0x00000100: (unknown)\n"},
  };

  for (int i = 0; i < ABSL_ARRAYSIZE(test_cases); ++i) {
    std::string output;
    SymbolizedStackWriter writer(*symbol_map, &output);
    writer.Write(test_cases[i].input, strlen(test_cases[i].input));
    ASSERT_EQ(output, test_cases[i].output);
  }
}
#endif  // GLOOP_UNSUPPORTED_LIBSTDCXX

TEST_P(SymbolizedStackWriterTest, SymbolizeCreatorThread) {
  std::unique_ptr<SymbolMap> symbol_map(
      SymbolMap::GetEmpty(/*compression_level=*/GetParam()));
  symbol_map->AddSymbol("foo", 0x40, 0x40);
  symbol_map->AddSymbol("thread_starter", 0x80, 0x20);
  symbol_map->AddSymbol("bar", 0xe0, 0x20);
  symbol_map->AddSymbol("main", 0x1234, 0x50);  // add main to appear unstripped

  TestCase not_symbolizing_creator_thread = {"0x40  creator: 0x80 0xe0\n",
                                             "  0x00000040: foo\n"
                                             "  creator: 0x80 0xe0\n"};

  TestCase symbolizing_creator_thread = {"0x40  creator: 0x80 0xe0\n",
                                         "  0x00000040: foo\n"
                                         "  creator:\n"
                                         "  0x00000080: thread_starter\n"
                                         "  0x000000e0: bar\n"};

  std::string output1;
  SymbolizedStackWriter writer1(*symbol_map, &output1);
  writer1.Write(not_symbolizing_creator_thread.input,
                strlen(not_symbolizing_creator_thread.input));
  ASSERT_EQ(output1, not_symbolizing_creator_thread.output);

  std::string output2;
  SymbolizedStackWriter writer2(*symbol_map, &output2);
  writer2.set_symbolize_creator_thread(true);
  writer2.Write(symbolizing_creator_thread.input,
                strlen(symbolizing_creator_thread.input));
  ASSERT_EQ(output2, symbolizing_creator_thread.output);
}

// Test that symbolizing a real stacktrace works. This test does
// nothing if the unittest itself is stripped. We force this unittest
// to link statically, so the having symbols is an all-or-nothing
// decision. If the test were linked dynamically, it would be possible
// that main() is stripped out of this executable, but the .so's
// haven't been stripped. It's hard to detect that case and skip the
// test gracefully, so we link statically.
TEST_P(SymbolizedStackWriterTest, ExtractStack) {
  auto symbol_map = SymbolMap::Create(/*copy_symbol_names=*/true,
                                      /*compression_level=*/GetParam());
  if (symbol_map->binary_is_stripped()) {
    GTEST_SKIP() << "Stripped binary.";
    return;
  }
  std::string output;
  SymbolizedStackWriter writer(*symbol_map, &output);
  Thread_ExtractStacks(&writer);
  // We have to be somewhere in main(), so look for that in the stack trace.
  ASSERT_NE(output.find(": main"), -1);
}

TEST_P(SymbolizedStackWriterTest, ExtractStackWithSeparateLogging) {
  auto symbol_map = SymbolMap::Create(/*copy_symbol_names=*/true,
                                      /*compression_level=*/GetParam());
  if (symbol_map->binary_is_stripped()) {
    GTEST_SKIP() << "Stripped binary.";
    return;
  }
  // For this test we're going to need to capture the logs because
  // the thread stacks will be written directly there by SymbolizedStackWriter.
  absl::ScopedMockLog log;
  EXPECT_CALL(log, Log).Times(AnyNumber());
  // We have to be somewhere in main(), so look for that in the LOG().
  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, _, HasSubstr(": main")));
  log.StartCapturingLogs();
  SymbolizedStackWriter logger(
      [](const absl::string_view s) { LOG(INFO) << s; });
  Thread_ExtractStacks(&logger);
}

INSTANTIATE_TEST_SUITE_P(SymbolizedStackWriterTestInstantiation,
                         SymbolizedStackWriterTest,
                         ::testing::Values(0, 1, 2, 3));
