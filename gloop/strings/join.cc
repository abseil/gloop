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

#include "gloop/strings/join.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/ascii.h"
#include "absl/strings/resize_and_overwrite.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace strings {

// Copies the input string into the destination, repeating every " character.
static size_t DoubleQuotes(absl::string_view src, char* dest_not_nul_terminated,
                           size_t dest_len) {
  size_t used = 0;
  size_t i = 0;

  while (i < src.size()) {
    const char ch = src[i++];
    const bool repeats = ch == '"';

    DCHECK_LT(used + static_cast<int>(repeats), dest_len);

    dest_not_nul_terminated[used++] = ch;

    if (repeats) {
      dest_not_nul_terminated[used++] = ch;
    }
  }

  return used;
}

void JoinCSVLineWithDelimiter(absl::Span<const std::string> cols,
                              char delimiter, std::string* output) {
  CHECK(output);
  CHECK(output->empty());
  std::vector<std::string> quoted_cols;
  quoted_cols.reserve(cols.size());

  const char escape_chars[] = {delimiter, '\0', '\"', '\r', '\n'};

  // If the string contains the delimiter, quote, or newline anywhere, or begins
  // or ends with whitespace (ie ascii_isspace() returns true), escape all
  // double-quotes and bracket the string in double quotes. string.rbegin()
  // evaluates to the last character of the string.
  for (const std::string& col : cols) {
    if ((col.find_first_of(absl::string_view(
             escape_chars, std::size(escape_chars))) != std::string::npos) ||
        (!col.empty() && (absl::ascii_isspace(*col.begin()) ||
                          absl::ascii_isspace(*col.rbegin())))) {
      std::string quoted;
      absl::StringResizeAndOverwrite(
          quoted, col.size() * 2 + 2,
          [col](char* buffer, ptrdiff_t buffer_size) {
            size_t used = 0;
            buffer[used++] = '"';
            used += DoubleQuotes(col, buffer + used, buffer_size - used);
            buffer[used++] = '"';
            return used;
          });
      quoted_cols.push_back(std::move(quoted));
    } else {
      quoted_cols.push_back(col);
    }
  }
  *output = absl::StrJoin(quoted_cols, absl::string_view(&delimiter, 1));
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
