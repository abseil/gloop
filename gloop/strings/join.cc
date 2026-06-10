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

#include "gloop/strings/join.h"

#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_join.h"
#include "absl/types/span.h"
#include "gloop/strings/escaping.h"

namespace strings {

void JoinCSVLineWithDelimiter(absl::Span<const std::string> cols,
                              char delimiter, std::string* output) {
  CHECK(output);
  CHECK(output->empty());
  std::vector<std::string> quoted_cols;

  const std::string delimiter_str(1, delimiter);
  const std::string escape_chars = delimiter_str + "\"";

  // If the string contains the delimiter or " anywhere, or begins or ends with
  // whitespace (ie ascii_isspace() returns true), escape all double-quotes and
  // bracket the string in double quotes. string.rbegin() evaluates to the last
  // character of the string.
  for (const std::string& col : cols) {
    std::string quoted = strings::QuoteStrForCSV(col);
    if (((col.find_first_of(escape_chars) != std::string::npos) ||
         (!col.empty() && (absl::ascii_isspace(*col.begin()) ||
                           absl::ascii_isspace(*col.rbegin())))) &&
        quoted[0] != '\"') {
      quoted.insert(0, "\"");
      quoted += '\"';
    }
    quoted_cols.push_back(std::move(quoted));
  }
  *output = absl::StrJoin(quoted_cols, delimiter_str);
}

void JoinCSVLine(const std::vector<std::string>& cols, std::string* output) {
  JoinCSVLineWithDelimiter(cols, ',', output);
}

std::string JoinCSVLine(const std::vector<std::string>& cols) {
  std::string output;
  JoinCSVLine(cols, &output);
  return output;
}

}  // namespace strings
