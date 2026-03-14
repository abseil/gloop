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

#ifndef THIRD_PARTY_GLOOP_BASE_AUXILIARY_PARSED_PROCESS_STAT_H_
#define THIRD_PARTY_GLOOP_BASE_AUXILIARY_PARSED_PROCESS_STAT_H_

// IWYU pragma: private, include "base/sysinfo.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace base {

// Represents a parsed line of `/proc/self/stat` or another file of that format.
class ParsedProcessStat {
 public:
  // The number of fields in `/proc/self/stat`.
  static constexpr size_t kNumFields = 52;

  // Creates the object wrapping `line`. Parsing happens on-demand, not during
  // construction.
  explicit ParsedProcessStat(std::string line);

  // Field accessors. Return a zero (or an empty string) if the requested fields
  // were not contained in the "stat" file.

  // Returns field with index 1 (comm).
  absl::StatusOr<absl::string_view> GetComm();
  // Returns field with index 2 (state).
  absl::StatusOr<char> GetState();
  // Returns the signed-integer field specified by its field index. Valid
  // indices are for fields which `man 5 proc` recommends to parse as signed
  // ints.
  absl::StatusOr<int64_t> GetSignedIntField(size_t field_index);
  // Returns the signed-integer field specified by its field index. Valid
  // indices are for fields which `man 5 proc` recommends to parse as unsigned
  // ints.
  absl::StatusOr<uint64_t> GetUnsignedIntField(size_t field_index);

 private:
  // Parses fields so that `fields_.size()` is at least `count`. Assumes that
  // `count` <= `kNumFields`.
  absl::Status EnsureFieldsParsed(size_t count);

  // The contents (i.e., the only line) of the "stat" file.
  const std::string stat_line_;

  // `fields_[i]` stores where the i-th field is located in `stat_line_`.
  std::vector<absl::string_view> fields_;
};
}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_AUXILIARY_PARSED_PROCESS_STAT_H_
