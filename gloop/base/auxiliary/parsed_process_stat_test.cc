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

#include "gloop/base/auxiliary/parsed_process_stat.h"

#include <fcntl.h>

#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "fuzztest/fuzztest.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace base {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Not;

TEST(ParsedProcessStatTest, UnderstandsRealStatLine) {
  // This input obtained by executing `cat /proc/self/stat`.
  ParsedProcessStat stat(
      "104315 (cat) R 16526 104315 16526 34818 104315 4194304 107 0 0 0 0 0 0 "
      "0 20 0 1 0 4716098 5754880 226 18446744073709551615 94372780531712 "
      "94372780551593 140724200813584 0 0 0 0 0 0 0 0 0 17 5 0 0 0 0 0 "
      "94372780567600 94372780569216 94372811526144 140724200820773 "
      "140724200820793 140724200820793 140724200841195 0");
  EXPECT_THAT(stat.GetSignedIntField(51), IsOkAndHolds(Eq(0)));
  EXPECT_THAT(stat.GetUnsignedIntField(50), IsOkAndHolds(Eq(140724200841195)));
  EXPECT_THAT(stat.GetUnsignedIntField(9), IsOkAndHolds(Eq(107)));
  EXPECT_THAT(stat.GetState(), IsOkAndHolds(Eq('R')));
  EXPECT_THAT(stat.GetComm(), IsOkAndHolds(Eq("(cat)")));
  EXPECT_THAT(stat.GetSignedIntField(0), IsOkAndHolds(Eq(104315)));
  EXPECT_THAT(stat.GetSignedIntField(9),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("not a signed integer")));
}

TEST(ParsedProcessStatTest, HandlesSpaceAndParensInComm) {
  // This is how /proc/self/stat can start for a binary called a\)\ \(b.
  ParsedProcessStat stat("123 (a) (b) R 5 6");

  EXPECT_THAT(stat.GetComm(), IsOkAndHolds(Eq("(a) (b)")));
  EXPECT_THAT(stat.GetState(), IsOkAndHolds(Eq('R')));
  EXPECT_THAT(stat.GetSignedIntField(3), IsOkAndHolds(Eq(5)));
  EXPECT_THAT(stat.GetSignedIntField(4), IsOkAndHolds(Eq(6)));
  EXPECT_THAT(stat.GetSignedIntField(0), IsOkAndHolds(Eq(123)));
}

TEST(ParsedProcessStatTest, UnderstandsEmptyComm) {
  ParsedProcessStat stat("1 () S");

  EXPECT_THAT(stat.GetSignedIntField(0), IsOkAndHolds(Eq(1)));
  EXPECT_THAT(stat.GetComm(), IsOkAndHolds(Eq("()")));
  EXPECT_THAT(stat.GetState(), IsOkAndHolds(Eq('S')));
}

TEST(ParsedProcessStatTest, AllFieldsParsed) {
  std::string line = "0 (1) Z";
  for (size_t i = 3; i < ParsedProcessStat::kNumFields; ++i) {
    absl::StrAppend(&line, " ", i);
  }
  ParsedProcessStat stat(line);

  EXPECT_THAT(stat.GetSignedIntField(0), IsOkAndHolds(Eq(0)));
  EXPECT_THAT(stat.GetComm(), IsOkAndHolds(Eq("(1)")));
  EXPECT_THAT(stat.GetState(), IsOkAndHolds(Eq('Z')));
  for (size_t i = 3; i < ParsedProcessStat::kNumFields; ++i) {
    SCOPED_TRACE(i);
    auto signed_result = stat.GetSignedIntField(i);
    auto unsigned_result = stat.GetUnsignedIntField(i);
    if (!signed_result.ok()) {
      EXPECT_THAT(signed_result, StatusIs(absl::StatusCode::kInvalidArgument));
      EXPECT_THAT(unsigned_result, IsOkAndHolds(Eq(i)));
    } else {
      EXPECT_THAT(unsigned_result,
                  StatusIs(absl::StatusCode::kInvalidArgument));
      EXPECT_THAT(signed_result, IsOkAndHolds(Eq(i)));
    }
  }
}

TEST(ParsedProcessStatTest, ReportsInvalidFieldIndex) {
  ParsedProcessStat stat("123 (ab) R 5 6");

  static constexpr auto kInvalidIndicesSample = std::to_array<int>(
      {1, 2,  // 1, 2 are fields with non-integer values.
       5,  // 5 is generally a valid index, but the concrete line is too short.
       -1, ParsedProcessStat::kNumFields + 1, 228, 11532, 123456789});
  for (int i : kInvalidIndicesSample) {
    SCOPED_TRACE(i);
    EXPECT_THAT(stat.GetSignedIntField(i),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(stat.GetUnsignedIntField(i),
                StatusIs(absl::StatusCode::kInvalidArgument));
  }
}

using ParsedProcessStatInvalidTest = testing::TestWithParam<std::string>;

TEST_P(ParsedProcessStatInvalidTest, ReportsInvalidInput) {
  ParsedProcessStat stat(GetParam());

  EXPECT_THAT(stat.GetComm(), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(stat.GetSignedIntField(0),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(stat.GetState(), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(stat.GetSignedIntField(3),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

INSTANTIATE_TEST_SUITE_P(, ParsedProcessStatInvalidTest,
                         testing::Values("7 (Missing closing paren 44565",
                                         "842 Missing opening paren) 010 7",
                                         "987654321 )Wrong order( 333 12",
                                         "987654322 )Wrong (order( 333 12",
                                         "987654323 )Wrong )order( 333 12",
                                         "123 No parens at all 4 5 6"));

struct IntFieldChoice {
  bool as_signed;
  int field_idx;
};
// Fuzzer smoke test for parsing field given by `field_ids` from `line`.
void SmokeParsedProcessStatFields(
    std::string line, const std::vector<IntFieldChoice>& field_ids) {
  std::string trimmed(absl::StripAsciiWhitespace(line));
  ParsedProcessStat stat(std::move(line));
  ParsedProcessStat parsed_trimmed(std::move(trimmed));
  for (IntFieldChoice choice : field_ids) {
    if (choice.as_signed) {
      auto signed_field = stat.GetSignedIntField(choice.field_idx);
      auto signed_field_from_trimmed =
          parsed_trimmed.GetSignedIntField(choice.field_idx);

      ASSERT_THAT(signed_field.status().code(),
                  Eq(signed_field_from_trimmed.status().code()));
      EXPECT_THAT(signed_field.status(),
                  Not(StatusIs(absl::StatusCode::kInternal)));
      if (signed_field.ok()) {
        EXPECT_THAT(*signed_field, Eq(*signed_field_from_trimmed));
      }
    } else {
      auto unsigned_field = stat.GetUnsignedIntField(choice.field_idx);
      auto unsigned_field_from_trimmed =
          parsed_trimmed.GetUnsignedIntField(choice.field_idx);

      ASSERT_THAT(unsigned_field.status().code(),
                  Eq(unsigned_field_from_trimmed.status().code()));
      EXPECT_THAT(unsigned_field.status(),
                  Not(StatusIs(absl::StatusCode::kInternal)));
      if (unsigned_field.ok()) {
        EXPECT_THAT(*unsigned_field, Eq(*unsigned_field_from_trimmed));
      }
    }
  }
}

// Fuzzer smoke test for parsing comm and state fields from `line`.
// For every `true` in `comm_fields` tries to read a comm field, for every
// `false` a state field.
void SmokeParsedProcessStat(std::string line,
                            const std::vector<bool>& comm_fields) {
  std::string trimmed(absl::StripAsciiWhitespace(line));
  ParsedProcessStat stat(std::move(line));
  ParsedProcessStat parsed_trimmed(std::move(trimmed));
  for (bool choose_comm : comm_fields) {
    if (choose_comm) {
      auto comm = stat.GetComm();
      auto comm_from_trimmed = parsed_trimmed.GetComm();

      ASSERT_THAT(comm.status().code(), Eq(comm_from_trimmed.status().code()));
      EXPECT_THAT(comm.status(), Not(StatusIs(absl::StatusCode::kInternal)));
      if (comm.ok()) {
        EXPECT_THAT(*comm, Eq(*comm_from_trimmed));
      }
    } else {
      auto state = stat.GetState();
      auto state_from_trimmed = parsed_trimmed.GetState();

      ASSERT_THAT(state.status().code(),
                  Eq(state_from_trimmed.status().code()));
      EXPECT_THAT(state.status(), Not(StatusIs(absl::StatusCode::kInternal)));
      if (state.ok()) {
        EXPECT_THAT(*state, Eq(*state_from_trimmed));
      }
    }
  }
}

FUZZ_TEST(ParseProcStatFuzzer, SmokeParsedProcessStatFields);
FUZZ_TEST(ParseProcStatFuzzer, SmokeParsedProcessStat);

}  // namespace
}  // namespace base
