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

#include "gloop/util/status/errno_mapping.h"

#include <errno.h>
#include <stddef.h>

#include <string>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/types/source_location.h"
#include "gloop/util/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace util {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::HasSubstr;
using ::testing::MatchesRegex;

TEST(ErrnoMappingTest, ErrnoToCanonicalCode) {
  EXPECT_EQ(ErrnoToCanonicalCode(0), error::OK);

  // Spot-check a few errno values.
  EXPECT_EQ(ErrnoToCanonicalCode(EINVAL), error::INVALID_ARGUMENT);
  EXPECT_EQ(ErrnoToCanonicalCode(ENOENT), error::NOT_FOUND);

  // Apparently errno 41 is known not to be defined.
  EXPECT_EQ(ErrnoToCanonicalCode(41), error::UNKNOWN);
}

TEST(ErrnoMappingTest, ErrnoToCanonicalStatus) {
  EXPECT_THAT(ErrnoToCanonicalStatus(0, ""), IsOk());

  // Spot-check a few errno values.
  EXPECT_THAT(ErrnoToCanonicalStatus(EINVAL, "test0"),
              StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("test0")));
  EXPECT_THAT(ErrnoToCanonicalStatus(ENOENT, "test1"),
              StatusIs(absl::StatusCode::kNotFound, HasSubstr("test1")));

  // Apparently errno 41 is known not to be defined.
  EXPECT_THAT(ErrnoToCanonicalStatus(41, "test2"),
              StatusIs(absl::StatusCode::kUnknown, HasSubstr("test2")));
}

TEST(ErrnoMappingTest, ErrnoToCanonicalStatusBuilderImplicitLocation) {
  absl::Status status;
  absl::StatusBuilder builder(absl::OkStatus());
  absl::SourceLocation loc;

  status = ErrnoToCanonicalStatusBuilder(0, "");
  EXPECT_THAT(status, IsOk());

  // Spot-check a few errno values.
  loc = ::absl::SourceLocation::current();
  builder = ErrnoToCanonicalStatusBuilder(EINVAL, "test0");
  status = builder << "message0";
  EXPECT_EQ(builder.source_location().file_name(), loc.file_name());
  EXPECT_EQ(builder.source_location().line(), loc.line() + 1);
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kInvalidArgument,
                               MatchesRegex("test0.*message0")));

  loc = ::absl::SourceLocation::current();
  builder = ErrnoToCanonicalStatusBuilder(ENOENT, "test1");
  status = builder << "message1";
  EXPECT_EQ(builder.source_location().file_name(), loc.file_name());
  EXPECT_EQ(builder.source_location().line(), loc.line() + 1);
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kNotFound,
                               MatchesRegex("test1.*message1")));

  // Apparently errno 41 is known not to be defined.
  loc = ::absl::SourceLocation::current();
  builder = ErrnoToCanonicalStatusBuilder(41, "test2");
  status = builder << "message2";
  EXPECT_EQ(builder.source_location().file_name(), loc.file_name());
  EXPECT_EQ(builder.source_location().line(), loc.line() + 1);
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kUnknown,
                               MatchesRegex("test2.*message2")));
}

TEST(ErrnoMappingTest, ErrnoToCanonicalStatusBuilderExplicitLocation) {
#line 12345
  absl::SourceLocation loc = ::absl::SourceLocation::current();
  ASSERT_EQ(loc.line(), 12345);

  absl::StatusBuilder builder =
      ErrnoToCanonicalStatusBuilder(EINVAL, "test0", loc);
  absl::Status status = builder << "message0";
  EXPECT_EQ(builder.source_location().file_name(), loc.file_name());
  EXPECT_EQ(builder.source_location().line(), 12345);
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kInvalidArgument,
                               MatchesRegex("test0.*message0")));
}

}  // namespace
}  // namespace util
