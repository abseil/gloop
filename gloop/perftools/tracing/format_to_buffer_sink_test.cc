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

#include "gloop/perftools/tracing/format_to_buffer_sink.h"

#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace perftools::tracing {
namespace {

using ::testing::Eq;
using ::testing::StrEq;

TEST(FormatToBufferSink, EmptySink) {
  FormatToBufferSink sink;
  EXPECT_TRUE(absl::Format(&sink, "%s%s", "abcd", "efg"));
  EXPECT_THAT(sink.total_size(), Eq(7));
}

TEST(FormatToBufferSink, WithBuffer) {
  char buf[] = "ABCDEFGH";
  FormatToBufferSink sink(buf);
  EXPECT_TRUE(absl::Format(&sink, "%s%s", "abcd", "efg"));
  EXPECT_THAT(sink.total_size(), Eq(7));
  EXPECT_THAT(buf, StrEq("abcdefgH"));
}

TEST(FormatToBufferSink, WithPartialBuffer) {
  char buf1[] = "ABC";
  FormatToBufferSink sink1({buf1, 2});
  EXPECT_TRUE(absl::Format(&sink1, "%s%s", "abcd", "efg"));
  EXPECT_THAT(sink1.total_size(), Eq(7));
  EXPECT_THAT(buf1, StrEq("abC"));

  char buf2[] = "ABCDE";
  FormatToBufferSink sink2({buf2, 4});
  EXPECT_TRUE(absl::Format(&sink2, "%s%s%s", "abcd", "efg", "hij"));
  EXPECT_THAT(sink2.total_size(), Eq(10));
  EXPECT_THAT(buf2, StrEq("abcdE"));
}

TEST(FormatToBufferSink, EmptyStringViewWithNull) {
  char buf[] = "ABCDEFGH";
  FormatToBufferSink sink(buf);
  sink.Append("abcd");
  sink.Append({nullptr, 0});
  sink.Append("efg");
  EXPECT_THAT(sink.total_size(), Eq(7));
  EXPECT_THAT(buf, StrEq("abcdefgH"));
}

}  // namespace
}  // namespace perftools::tracing
