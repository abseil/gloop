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

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace base {

namespace {

struct ParensPositions {
  absl::string_view::size_type open;
  absl::string_view::size_type close;
};

// Checks that `proc_stat` has the form of A+"("+Comm+")"+B,
// such that A and B contain neither "(" nor ")", and returns the positions of
// the parens enclosing Comm.
absl::StatusOr<ParensPositions> LocateComm(absl::string_view proc_stat) {
  ParensPositions comm_range;
  comm_range.open = proc_stat.find_first_of('(');
  if (comm_range.open == absl::string_view::npos) {
    return absl::InvalidArgumentError(
        absl::StrCat("No '(' in the given /proc/*/stat contents: ", proc_stat));
  }
  comm_range.close = proc_stat.find_last_of(')');
  if (comm_range.close == absl::string_view::npos) {
    return absl::InvalidArgumentError(
        absl::StrCat("No ')' in the given /proc/*/stat contents: ", proc_stat));
  }
  if (comm_range.close <= comm_range.open) {
    return absl::InvalidArgumentError(absl::StrCat(
        "')' at position ", comm_range.close, " precedes '(' at position ",
        comm_range.open, " in the given /proc/*/stat contents: ", proc_stat));
  }
  return comm_range;
}

// All fields which `man 5 proc` recommends to parse as signed ints.
constexpr std::array<int, 17> kSignedBits = {0,  3,  4,  5,  6,  7,  15, 16, 17,
                                             18, 19, 20, 23, 37, 38, 43, 51};
constexpr uint64_t GetSignedFields() {
  uint64_t result = 0;
  for (const int j : kSignedBits) {
    result |= (uint64_t{1} << j);
  }
  return result;
}
// A bitmask with the indices of all fields to be parsed as signed ints.
constexpr uint64_t kSignedIntegerFields = GetSignedFields();

}  // namespace

ParsedProcessStat::ParsedProcessStat(std::string line)
    : stat_line_(std::move(line)) {}

absl::StatusOr<absl::string_view> ParsedProcessStat::GetComm() {
  absl::Status parse_status = EnsureFieldsParsed(2);
  if (!ABSL_PREDICT_TRUE(parse_status.ok())) {
    return parse_status;
  }
  return fields_[1];
}

absl::StatusOr<char> ParsedProcessStat::GetState() {
  absl::Status parse_status = EnsureFieldsParsed(3);
  if (!ABSL_PREDICT_TRUE(parse_status.ok())) {
    return parse_status;
  }
  const absl::string_view state = fields_[2];
  if (state.size() != 1) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Status field is '", state, "', expected a single character."));
  }
  return state.front();
}

absl::StatusOr<int64_t> ParsedProcessStat::GetSignedIntField(
    size_t field_index) {
  if (field_index >= kNumFields ||
      ((uint64_t{1} << field_index) & kSignedIntegerFields) == 0) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Field with index ", field_index, " is not a signed integer."));
  }
  absl::Status parse_status = EnsureFieldsParsed(field_index + 1);
  if (!parse_status.ok()) {
    return parse_status;
  }
  int64_t result;
  if (!absl::SimpleAtoi(fields_[field_index], &result)) {
    return absl::FailedPreconditionError(
        absl::StrCat("Could not parse field ", field_index, " (",
                     fields_[field_index], ") as a number."));
  }
  return result;
}

absl::StatusOr<uint64_t> ParsedProcessStat::GetUnsignedIntField(
    size_t field_index) {
  if (field_index >= kNumFields || field_index == 1 || field_index == 2 ||
      ((uint64_t{1} << field_index) & kSignedIntegerFields) != 0) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Field with index ", field_index, " is not an unsigned integer."));
  }
  absl::Status parse_status = EnsureFieldsParsed(field_index + 1);
  if (!parse_status.ok()) {
    return parse_status;
  }
  uint64_t result;
  if (!absl::SimpleAtoi(fields_[field_index], &result)) {
    return absl::FailedPreconditionError(
        absl::StrCat("Could not parse field ", field_index, " (",
                     fields_[field_index], ") as a number."));
  }
  return result;
}

absl::Status ParsedProcessStat::EnsureFieldsParsed(size_t count) {
  // The index of the last field in `fields_`.
  size_t current_size = fields_.size();
  const bool needs_field_0 = current_size == 0;
  const bool needs_field_1 = count > 1 && current_size <= 1;

  if (needs_field_0 || needs_field_1) {
    absl::StatusOr<ParensPositions> comm_range = LocateComm(stat_line_);
    if (!ABSL_PREDICT_TRUE(comm_range.ok())) {
      return comm_range.status();
    }
    if (needs_field_0) {
      // Locate field 0.
      fields_.push_back(absl::StripAsciiWhitespace(
          absl::string_view(stat_line_.data(), comm_range->open)));
      ++current_size;
    }
    if (needs_field_1) {
      // Locate field 1.
      fields_.push_back(
          absl::string_view(stat_line_.data() + comm_range->open,
                            comm_range->close - comm_range->open + 1));
      ++current_size;
    }
  }
  while (current_size < count) {
    const absl::string_view last = fields_.back();
    size_t offset = last.size() + (last.data() - stat_line_.data());
    while (offset < stat_line_.size() &&
           absl::ascii_isspace(stat_line_[offset])) {
      ++offset;
    }
    // `offset` now points to the start of the field with `current_index`.
    size_t current = offset;
    while (current < stat_line_.size() &&
           !absl::ascii_isspace(stat_line_[current])) {
      ++current;
    }
    // `current` now points right past the end of the field with
    // `current_index`.
    if (current == offset) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Stat line '", stat_line_, "' has fewer than ", current_size,
          " fields, but field at index ", count - 1, " was requested."));
    }
    fields_.push_back(
        absl::string_view(stat_line_.data() + offset, current - offset));
    ++current_size;
  }
  return absl::OkStatus();
}

}  // namespace base
