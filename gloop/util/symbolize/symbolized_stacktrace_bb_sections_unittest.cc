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

#include <string>
#include <vector>

#include "absl/strings/str_split.h"
#include "absl/types/span.h"
#include "gloop/util/symbolize/foobar.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {
using ::testing::ContainsRegex;

// This test verifies that stack trace works with functions built with
// with -fbasic-block-sections=all. Specifically, it calls Foo and expects that
// symbols from Foo and Bar appear alternatively in the stack trace as it would
// be the case without basic-block-sections.
TEST(util, CurrentStackTraceWithBbSections) {
  const int kDepth = 10;
  // Call Foo and get the stack trace.
  std::vector<std::string> stacktrace =
      std::vector<std::string>(absl::StrSplit(Foo(kDepth), '\n'));
  // Extract the lines associated with Foo and Bar (skip the first line as it is
  // always equal to "Stack trace:").
  auto foo_bar_lines = absl::MakeConstSpan(stacktrace).subspan(1, kDepth);

  // Check that the symbols have a basic block suffix: use a general regular
  // expression to make sure the symbol name is followed by a non-all-whitespace
  // suffix.
  EXPECT_THAT(foo_bar_lines[0], ContainsRegex(R"(Bar\s*\S+)"));
  EXPECT_THAT(foo_bar_lines[1], ContainsRegex(R"(Foo\s*\S+)"));
  EXPECT_THAT(foo_bar_lines[2], ContainsRegex(R"(Bar\s*\S+)"));

  // Check that the rest of the stack trace alternates between two symbols in
  // Foo and Bar. We start from 3, and not 2 because foo_bar_lines[0] is Bar's
  // callsite for util::CurrentStackTrace and it is placed in a BB symbol
  // different from foo_bar_lines[2], the callsite for Foo.
  for (int i = 3; i < foo_bar_lines.size(); ++i)
    EXPECT_EQ(foo_bar_lines[i], foo_bar_lines[i - 2]);
}

}  // namespace
