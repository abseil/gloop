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

#include "gloop/util/csv/writer.h"

#include <functional>
#include <string>

#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace util {
namespace csv {

std::function<void(absl::string_view, strings::ByteSink*)>
StandardFieldFormatter(char field_delimiter) {
  std::string quotable_chars = std::string(1, field_delimiter) + "\"\n\r";
  return [quotable_chars](absl::string_view field, strings::ByteSink* sink) {
    // if this field contains any of the quotable chars, surround
    // it with quotes
    bool quote_field =
        field.find_first_of(quotable_chars) != absl::string_view::npos;
    if (quote_field) {
      sink->Append("\"");
    }

    // Split the field by quote char, write each piece, and escape the
    // quote with another quote.
    absl::string_view qq;
    for (absl::string_view sp : absl::StrSplit(field, '"')) {
      sink->Append(qq);
      sink->Append(sp);
      qq = absl::string_view("\"\"", 2);
    }

    // closing quote
    if (quote_field) {
      sink->Append("\"");
    }
  };
}

}  // namespace csv
}  // namespace util
