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

#ifndef THIRD_PARTY_GLOOP_BASE_INTERNAL_MUNGE_OUTPUT_H_
#define THIRD_PARTY_GLOOP_BASE_INTERNAL_MUNGE_OUTPUT_H_

#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace base_logging {
namespace logging_testing {

// Munges a log line.  Will "normalize" all items in the line that can vary from
// test to test, e.g. timestamp and thread ID fields.  Returns `absl::nullopt`
// if the line is to be dropped altogether, e.g. `FlagSaver` lines which only
// appear on some platforms.
std::optional<std::string> MungeLine(std::string line);

// Reads `filename` into a string.
absl::StatusOr<std::string> ReadFile(absl::string_view filename);

// Munges every line in `file` and returns the concatenated result.  If `output`
// is non-empty, the result is also written into the named file (e.g. in
// ${TEST_UNDECLARED_OUTPUTS_DIR} for the benefit of Sponge users).
absl::StatusOr<std::string> MungeFile(absl::string_view file,
                                      absl::string_view output = "");

}  // namespace logging_testing
}  // namespace base_logging

#endif  // THIRD_PARTY_GLOOP_BASE_INTERNAL_MUNGE_OUTPUT_H_
