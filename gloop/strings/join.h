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

#ifndef THIRD_PARTY_GLOOP_STRINGS_JOIN_H_
#define THIRD_PARTY_GLOOP_STRINGS_JOIN_H_

#include <string>
#include <type_traits>
#include <vector>

#include "absl/base/macros.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

#ifdef SWIG
%include "absl/strings/str_join.h"
#endif

namespace strings {

// ----------------------------------------------------------------------
// MOE:begin_strip
// LEGACY(jgm): Utilities provided in util/csv/writer.h are now preferred for
// writing CSV data in google3.
//
// MOE:end_strip
// Example for CSV formatting a single record (a sequence container of string,
// char*, or string_view values) using the util::csv::WriteRecordToString helper
// function:
//   std::vector<string> record = ...;
//   string line = util::csv::WriteRecordToString(record);
//
// NOTE: When writing many records, use the util::csv::Writer class directly.
//
// JoinCSVLineWithDelimiter()
//    This function is the inverse of SplitCSVLineWithDelimiter() in that the
//    string returned by JoinCSVLineWithDelimiter() can be passed to
//    SplitCSVLineWithDelimiter() to get the original string vector back.
//    Quotes and escapes the elements of original_cols according to CSV quoting
//    rules, and the joins the escaped quoted strings with commas using
//    JoinStrings().  Note that JoinCSVLineWithDelimiter() will not necessarily
//    return the same string originally passed in to
//    SplitCSVLineWithDelimiter(), since SplitCSVLineWithDelimiter() can handle
//    gratuitous spacing and quoting. 'output' must point to an empty string.
//
//    Example:
//     [Google], [x], [Buchheit, Paul], [string with " quote in it], [ space ]
//     --->  [Google,x,"Buchheit, Paul","string with "" quote in it"," space "]
//
// JoinCSVLine()
//    A convenience wrapper around JoinCSVLineWithDelimiter which uses
//    ',' as the delimiter.
// ----------------------------------------------------------------------
void JoinCSVLine(const std::vector<std::string>& original_cols,
                 std::string* output);
std::string JoinCSVLine(const std::vector<std::string>& original_cols);
void JoinCSVLineWithDelimiter(absl::Span<const std::string> original_cols,
                              char delimiter, std::string* output);

namespace strings_internal {
// Legacy Formatter. Converts alpha-numeric types to strings, just
// like AlphaNumFormatterImpl, but floating-point values have enough
// digits to round-trip.  Produces same results as AlphaNumFormatterImpl
// did prior to July 2017.
struct LegacyFormatterImpl {
  template <typename T>
  void operator()(std::string* out, const T& t) const {
    if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
      absl::StrAppend(out, absl::HighPrecision(t));
    } else {
      absl::StrAppend(out, t);
    }
  }
};
}  // namespace strings_internal

// Legacy Formatter. Converts alpha-numeric types to strings, just
// like absl::AlphaNumFormatter, but floating-point values have enough
// digits to round-trip.  Produces same results as absl::AlphaNumFormatter
// did prior to July 2017.
// This is a transitional API; see <link>
// Unlike absl::AlphaNumFormatter, vector<bool> is not supported.
// TODO - b/492863849: Consider renaming this HighPrecisionFormatter.
inline ::strings::strings_internal::LegacyFormatterImpl LegacyFormatter() {
  return ::strings::strings_internal::LegacyFormatterImpl();
}

}  // namespace strings

#endif  // THIRD_PARTY_GLOOP_STRINGS_JOIN_H_
