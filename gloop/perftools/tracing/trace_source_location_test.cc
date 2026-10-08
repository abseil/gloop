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

#include "gloop/perftools/tracing/trace_source_location.h"

#include <sstream>
#include <type_traits>

#include "absl/strings/str_cat.h"
#include "absl/types/source_location.h"
#include "gloop/perftools/tracing/test_only_access.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace perftools::tracing {
namespace {

using ::testing::Eq;
using ::testing::Ne;
using ::testing::StrEq;

constexpr auto access = perftools::tracing::testing::TestOnlyAccess::Create<
    TraceSourceLocation::Access>();

TEST(TraceSourceLocation, DefaultCtor) {
  TraceSourceLocation location;
  ASSERT_THAT(location.file_name(), Ne(nullptr));
  EXPECT_THAT(location.file_name(), StrEq(""));
  ASSERT_THAT(location.line(), Eq(0));
  ASSERT_THAT(location.function_name(), Ne(nullptr));
  EXPECT_THAT(location.function_name(), StrEq(""));
  ASSERT_THAT(location.column(), Eq(0));
}

TEST(TraceSourceLocation, FromAbslSourceLocation) {
  absl::SourceLocation current = absl::SourceLocation::current();
  TraceSourceLocation location = current;
  ASSERT_THAT(location.file_name(), Eq(current.file_name()));
  ASSERT_THAT(location.line(), Eq(current.line()));
  ASSERT_THAT(location.function_name(), Ne(nullptr));
  EXPECT_THAT(location.function_name(), StrEq(""));
  ASSERT_THAT(location.column(), Eq(0));
}

TEST(TraceSourceLocation, DirectAccess) {
  const char* file_name = "foo.cc";
  int line = 12345;
  TraceSourceLocation location(access, file_name, line);
  ASSERT_THAT(location.file_name(), Eq(file_name));
  ASSERT_THAT(location.line(), Eq(line));
  ASSERT_THAT(location.function_name(), Ne(nullptr));
  EXPECT_THAT(location.function_name(), StrEq(""));
  ASSERT_THAT(location.column(), Eq(0));
}

TEST(TraceSourceLocation, CopyCtor) {
  const char* file_name = "foo.cc";
  int line = 12345;
  TraceSourceLocation src(access, file_name, line);
  TraceSourceLocation location(src);
  ASSERT_THAT(location.file_name(), Eq(file_name));
  ASSERT_THAT(location.line(), Eq(line));
  ASSERT_THAT(location.function_name(), Ne(nullptr));
  EXPECT_THAT(location.function_name(), StrEq(""));
  ASSERT_THAT(location.column(), Eq(0));
}

TEST(TraceSourceLocation, Assign) {
  const char* file_name = "foo.cc";
  int line = 12345;
  TraceSourceLocation src(access, file_name, line);
  TraceSourceLocation location;
  location = src;
  ASSERT_THAT(location.file_name(), Eq(file_name));
  ASSERT_THAT(location.line(), Eq(line));
  ASSERT_THAT(location.function_name(), Ne(nullptr));
  EXPECT_THAT(location.function_name(), StrEq(""));
  ASSERT_THAT(location.column(), Eq(0));
}

TEST(TraceSourceLocation, Current) {
  const char* file_name = __FILE__;
  int line = __LINE__ + 1;
  constexpr TraceSourceLocation location = TraceSourceLocation::current();
  ASSERT_THAT(location.file_name(), Eq(file_name));
  ASSERT_THAT(location.line(), Eq(line));
  ASSERT_THAT(location.function_name(), Ne(nullptr));
  EXPECT_THAT(location.function_name(), StrEq(""));
  ASSERT_THAT(location.column(), Eq(0));
}

TEST(TraceSourceLocation, Triviality) {
  EXPECT_TRUE(std::is_trivially_copyable<TraceSourceLocation>::value);
  EXPECT_TRUE(std::is_trivially_copy_assignable<TraceSourceLocation>::value);
  EXPECT_TRUE(std::is_trivially_move_assignable<TraceSourceLocation>::value);
  EXPECT_TRUE(std::is_trivially_copy_constructible<TraceSourceLocation>::value);
  EXPECT_TRUE(std::is_trivially_move_constructible<TraceSourceLocation>::value);
  EXPECT_TRUE(std::is_trivially_destructible<TraceSourceLocation>::value);
}

TEST(TraceSourceLocation, Stringify) {
  EXPECT_THAT(absl::StrCat(TraceSourceLocation(access, "potato.cc", 123)),
              Eq("potato.cc:123"));
}

TEST(TraceSourceLocation, ToOstream) {
  std::ostringstream ss;
  ss << TraceSourceLocation(access, "potato.cc", 123);
  EXPECT_THAT(ss.str(), Eq("potato.cc:123"));
}

}  // namespace
}  // namespace perftools::tracing
