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

#include "gloop/strings/split.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/internal/raw_logging.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "absl/strings/charset.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "gloop/base/strtoint.h"
#include "gloop/strings/util.h"

namespace strings {

//
// ==================== LEGACY SPLIT FUNCTIONS ====================
//

namespace {

// Appends the results of a call to absl::StrSplit() to the specified vector.
// This function is used with the new absl::StrSplit() API to implement the
// append semantics of the legacy Split*() functions.
//
// The "Splitter" template parameter is intended to be a
// ::strings::internal::Splitter<>, which is the return value of a call to
// absl::StrSplit(). Sample usage:
//
//   std::vector<string> v;
//   ... add stuff to "v" ...
//   AppendTo(&v, absl::StrSplit("a,b,c", ","));
//
template <typename Splitter>
inline void AppendTo(std::vector<std::string>* v, Splitter splitter) {
  if (v->empty()) {
    // Fast path for common case.
    *v = splitter;  // Calls implicit conversion operator.
    return;
  }
  // Optimization: First split to a std::vector<absl::string_view> so we can
  // size the output container appropriately.

  // Calls implicit conversion operator.
  std::vector<absl::string_view> vsp = splitter;
  size_t container_size = v->size();
  v->resize(container_size + vsp.size());
  for (absl::string_view piece : vsp) {
    (*v)[container_size++].assign(piece.data(), piece.size());
  }
}

}  // anonymous namespace

// ----------------------------------------------------------------------
// SplitOneStringToken()
//   Mainly a stringified wrapper around strpbrk()
// ----------------------------------------------------------------------
std::string SplitOneStringToken(const char** source, const char* delim) {
  assert(source);
  assert(delim);
  if (!*source) {
    return std::string();
  }
  const char* begin = *source;
  // Optimize the common case where delim is a single character.
  if (delim[0] != '\0' && delim[1] == '\0') {
    *source = strchr(*source, delim[0]);
  } else {
    *source = strpbrk(*source, delim);
  }
  if (*source) {
    return std::string(begin, (*source)++);
  } else {
    return std::string(begin);
  }
}

// ----------------------------------------------------------------------
// SplitOneIntToken()
// SplitOneInt32Token()
// SplitOneUint32Token()
// SplitOneInt64Token()
// SplitOneUint64Token()
// SplitOneDoubleToken()
// SplitOneFloatToken()
// SplitOneDecimalIntToken()
// SplitOneDecimalInt32Token()
// SplitOneDecimalUint32Token()
// SplitOneDecimalInt64Token()
// SplitOneDecimalUint64Token()
// SplitOneHexUint32Token()
// SplitOneHexUint64Token()
//   Mainly a stringified wrapper around strtol/strtoul/strtod
// ----------------------------------------------------------------------
// Curried functions for the macro below
static inline long strto32_0(const char* source, char** end) {
  return strto32(source, end, 0);
}
static inline unsigned long strtou32_0(const char* source, char** end) {
  return strtou32(source, end, 0);
}
static inline int64_t strto64_0(const char* source, char** end) {
  return strto64(source, end, 0);
}
static inline uint64_t strtou64_0(const char* source, char** end) {
  return strtou64(source, end, 0);
}
static inline long strto32_10(const char* source, char** end) {
  return strto32(source, end, 10);
}
static inline unsigned long strtou32_10(const char* source, char** end) {
  return strtou32(source, end, 10);
}
static inline int64_t strto64_10(const char* source, char** end) {
  return strto64(source, end, 10);
}
static inline uint64_t strtou64_10(const char* source, char** end) {
  return strtou64(source, end, 10);
}
static inline uint32_t strtou32_16(const char* source, char** end) {
  return strtou32(source, end, 16);
}
static inline uint64_t strtou64_16(const char* source, char** end) {
  return strtou64(source, end, 16);
}

#define DEFINE_SPLIT_ONE_NUMBER_TOKEN(name, type, function)          \
  bool SplitOne##name##Token(const char** source, const char* delim, \
                             type* value) {                          \
    assert(source);                                                  \
    assert(delim);                                                   \
    assert(value);                                                   \
    if (!*source) return false;                                      \
    /* Parse int */                                                  \
    char* end;                                                       \
    *value = static_cast<type>(function(*source, &end));             \
    if (end == *source)                                              \
      return false; /* number not present at start of string */      \
    if (end[0] && !strchr(delim, end[0]))                            \
      return false; /* Garbage characters after int */               \
    /* Advance past token */                                         \
    if (*end != '\0')                                                \
      *source = const_cast<const char*>(end + 1);                    \
    else                                                             \
      *source = nullptr;                                             \
    return true;                                                     \
  }

DEFINE_SPLIT_ONE_NUMBER_TOKEN(Int, int, strto32_0)
DEFINE_SPLIT_ONE_NUMBER_TOKEN(Int32, int32_t, strto32_0)
DEFINE_SPLIT_ONE_NUMBER_TOKEN(Uint32, uint32_t, strtou32_0)
DEFINE_SPLIT_ONE_NUMBER_TOKEN(Int64, int64_t, strto64_0)
DEFINE_SPLIT_ONE_NUMBER_TOKEN(Uint64, uint64_t, strtou64_0)
DEFINE_SPLIT_ONE_NUMBER_TOKEN(Double, double, strtod)
DEFINE_SPLIT_ONE_NUMBER_TOKEN(Float, float, strtof)
DEFINE_SPLIT_ONE_NUMBER_TOKEN(DecimalInt, int, strto32_10)
DEFINE_SPLIT_ONE_NUMBER_TOKEN(DecimalInt32, int32_t, strto32_10)
DEFINE_SPLIT_ONE_NUMBER_TOKEN(DecimalUint32, uint32_t, strtou32_10)
DEFINE_SPLIT_ONE_NUMBER_TOKEN(DecimalInt64, int64_t, strto64_10)
DEFINE_SPLIT_ONE_NUMBER_TOKEN(DecimalUint64, uint64_t, strtou64_10)
DEFINE_SPLIT_ONE_NUMBER_TOKEN(HexUint32, uint32_t, strtou32_16)
DEFINE_SPLIT_ONE_NUMBER_TOKEN(HexUint64, uint64_t, strtou64_16)

// ----------------------------------------------------------------------
// SplitRange()
//    Splits a string of the form "<from>-<to>".  Either or both can be
//    missing.  A raw number (<to>) is interpreted as "<to>-".  Modifies
//    parameters insofar as they're specified by the string.  RETURNS
//    true iff the input is a well-formed range.  If it RETURNS false,
//    from and to remain unchanged.  The range in rangestr should be
//    terminated either by "\0" or by whitespace.
// ----------------------------------------------------------------------

bool SplitRange(absl::string_view rangestr, int32_t* from, int32_t* to) {
  // Terminate the string early if it has any whitespace
  size_t whitespace_pos = 0;
  for (char c : rangestr) {
    if (absl::ascii_isspace(c)) {
      rangestr = rangestr.substr(0, whitespace_pos);
      break;
    }
    ++whitespace_pos;
  }

  absl::string_view from_str = rangestr;
  absl::string_view to_str;  // empty by default
  auto dash_pos = rangestr.find('-');
  if (dash_pos != rangestr.npos) {  // string has a dash.
    from_str = rangestr.substr(0, dash_pos);
    to_str = rangestr.substr(dash_pos + 1);
  }
  int32_t from_int = 0;
  if (!from_str.empty() && !absl::SimpleAtoi(from_str, &from_int)) {
    return false;  // not a valid integer
  }
  int32_t to_int = 0;
  if (!to_str.empty() && !absl::SimpleAtoi(to_str, &to_int)) {
    return false;  // not a valid integer
  }
  if (!from_str.empty()) *from = from_int;
  if (!to_str.empty()) *to = to_int;
  return true;
}

// ----------------------------------------------------------------------
// SplitStringIntoKeyValues()
// ----------------------------------------------------------------------
bool SplitStringIntoKeyValues(absl::string_view line,
                              absl::string_view key_value_delimiters,
                              absl::string_view value_value_delimiters,
                              std::string* key,
                              std::vector<std::string>* values) {
  key->clear();
  values->clear();

  // find the key string
  size_t end_key_pos = line.find_first_of(key_value_delimiters);
  if (end_key_pos == std::string::npos) {
    VLOG(1) << "cannot parse key from line: " << line;
    return false;  // no key
  }
  key->assign(line, 0, end_key_pos);

  // find the values string
  std::string remains(line, end_key_pos, line.size() - end_key_pos);
  size_t begin_values_pos = remains.find_first_not_of(key_value_delimiters);
  if (begin_values_pos == std::string::npos) {
    VLOG(1) << "cannot parse value from line: " << line;
    return false;  // no value
  }
  std::string values_string(remains, begin_values_pos,
                            remains.size() - begin_values_pos);

  // construct the values vector
  if (value_value_delimiters.empty()) {  // one value
    values->push_back(values_string);
  } else {  // multiple values
    AppendTo(values, absl::StrSplit(values_string,
                                    absl::ByAnyChar(value_value_delimiters),
                                    absl::SkipEmpty()));

    if (values->empty()) {
      VLOG(1) << "cannot parse value from line: " << line;
      return false;  // no value
    }
  }
  return true;
}

bool SplitStringIntoKeyValuePairs(
    absl::string_view line, absl::string_view key_value_delimiters,
    absl::string_view key_value_pair_delimiters,
    std::vector<std::pair<std::string, std::string> >* kv_pairs) {
  kv_pairs->clear();

  std::vector<std::string> pairs;
  if (key_value_pair_delimiters.empty()) {
    pairs.emplace_back(line);
  } else {
    AppendTo(&pairs,
             absl::StrSplit(line, absl::ByAnyChar(key_value_pair_delimiters),
                            absl::SkipEmpty()));
  }

  bool success = true;
  for (const std::string& pair : pairs) {
    std::string key;
    std::vector<std::string> value;
    if (!SplitStringIntoKeyValues(pair, key_value_delimiters, "", &key,
                                  &value)) {
      // Don't return here, to allow for keys without associated
      // values; just record that our split failed.
      success = false;
    }
    // we expect atmost one value because we passed in an empty vsep to
    // SplitStringIntoKeyValues
    DCHECK_LE(value.size(), 1u);
    kv_pairs->push_back(std::make_pair(key, value.empty() ? "" : value[0]));
  }
  return success;
}

void SplitCSVLineWithDelimiter(char* line, char delimiter,
                               std::vector<char*>* cols) {
  char* end_of_line = line + strlen(line);
  char* end;
  char* start;

  for (; line < end_of_line; line++) {
    // Skip leading whitespace, unless said whitespace is the delimiter.
    while (absl::ascii_isspace(*line) && *line != delimiter) ++line;

    if (*line == '"' && delimiter == ',') {  // Quoted value...
      start = ++line;
      end = start;
      for (; *line; line++) {
        if (*line == '"') {
          line++;
          if (*line != '"')  // [""] is an escaped ["]
            break;           // but just ["] is end of value
        }
        *end++ = *line;
      }
      // All characters after the closing quote and before the comma
      // are ignored.
      line = strchr(line, delimiter);
      if (!line) line = end_of_line;
    } else {
      start = line;
      line = strchr(line, delimiter);
      if (!line) line = end_of_line;
      // Skip all trailing whitespace, unless said whitespace is the delimiter.
      for (end = line; end > start; --end) {
        if (!absl::ascii_isspace(end[-1]) || end[-1] == delimiter) break;
      }
    }
    const bool need_another_column =
        (*line == delimiter) && (line == end_of_line - 1);
    *end = '\0';
    cols->push_back(start);
    // If line was something like [paul,] (comma is the last character
    // and is not proceeded by whitespace or quote) then we are about
    // to eliminate the last column (which is empty). This would be
    // incorrect.
    if (need_another_column) cols->push_back(end);

    assert(*line == '\0' || *line == delimiter);
  }
}

void SplitCSVLine(char* line, std::vector<char*>* cols) {
  SplitCSVLineWithDelimiter(line, ',', cols);
}

void SplitCSVLineWithDelimiterForStrings(absl::string_view line, char delimiter,
                                         std::vector<std::string>* cols) {
  // Unfortunately, the interface requires char*, which requires copying the
  // string.
  char* cline = strndup_with_new(line.data(), line.size());
  std::vector<char*> v;
  SplitCSVLineWithDelimiter(cline, delimiter, &v);
  for (char* str : v) {
    cols->push_back(str);
  }
  delete[] cline;
}

// Constants for ClipString()
static const int kMaxOverCut = 12;
// The ellipsis to add to strings that are too long
static const char kCutStr[] = "...";
static const int kCutStrSize = sizeof(kCutStr) - 1;

// ----------------------------------------------------------------------
// Return the place to clip the string at, or -1
// if the string doesn't need to be clipped.
// ----------------------------------------------------------------------
static ptrdiff_t ClipStringHelper(absl::string_view str, ptrdiff_t max_len,
                                  bool use_ellipsis) {
  if (str.length() <= static_cast<size_t>(max_len)) return -1;

  ptrdiff_t max_substr_len = max_len;

  if (use_ellipsis && max_len > kCutStrSize) {
    max_substr_len -= kCutStrSize;
  }

  const char* cut_by =
      (max_substr_len < kMaxOverCut ? str.data()
                                    : str.data() + max_len - kMaxOverCut);
  const char* cut_at = str.data() + max_substr_len;
  while (!absl::ascii_isspace(*cut_at) && cut_at > cut_by) cut_at--;

  if (cut_at == cut_by) {
    // No space was found
    return max_substr_len;
  } else {
    return cut_at - str.data();
  }
}

// ----------------------------------------------------------------------
// ClipString
//    Clip a string to a max length. We try to clip on a word boundary
//    if this is possible. If the string is clipped, we append an
//    ellipsis.
// ----------------------------------------------------------------------
void ClipString(std::string* full_str, ptrdiff_t max_len) {
  ptrdiff_t cut_at = ClipStringHelper(*full_str, max_len, true);
  if (cut_at != -1) {
    full_str->erase(cut_at);
    if (max_len > kCutStrSize) {
      full_str->append(kCutStr);
    }
  }
}

// ----------------------------------------------------------------------
// SplitStringWithEscaping()
// SplitStringWithEscapingAllowEmpty()
// SplitStringWithEscapingToSet()
//   Split the string using the specified delimiters, taking escaping into
//   account. '\' is not allowed as a delimiter.
// ----------------------------------------------------------------------
template <bool allow_empty, typename IN_ITER, typename Functor,
          typename OUT_ITER>
static inline void SplitStringWithEscapingToIterator(IN_ITER first,
                                                     IN_ITER last,
                                                     Functor&& delimiter_check,
                                                     OUT_ITER result) {
  ABSL_RAW_CHECK(!delimiter_check('\\'), "\\ is not allowed as a delimiter.");
  std::string part;

  for (; first != last; ++first) {
    char current_char = *first;
    if (delimiter_check(current_char)) {
      // Push substrings when we encounter delimiters.
      if (allow_empty || !part.empty()) {
        *result++ = part;
        part.clear();
      }
      continue;
    }
    if (current_char != '\\') {
      // Just a normal character.
      part.push_back(current_char);
      continue;
    }
    // We have read a backslash: look ahead if possible.
    if (++first == last) {
      // Trailing backslash. Nothing to peek at, just push it and stop.
      part.push_back('\\');
      break;
    }
    current_char = *first;
    // The next delimiter or backslash is literal.
    if (current_char != '\\' && !delimiter_check(current_char)) {
      // Don't honor unknown escape sequences: emit \f for \f.
      part.push_back('\\');
    }
    part.push_back(current_char);
  }
  // Push the trailing part.
  if (allow_empty || !part.empty()) {
    *result++ = part;
  }
}

template <bool allow_empty, typename IN_CONTAINER, typename Functor,
          typename OUT_CONTAINER>
static inline void SplitStringWithEscapingToContainer(const IN_CONTAINER& src,
                                                      Functor&& delimiter_check,
                                                      OUT_CONTAINER* result) {
  SplitStringWithEscapingToIterator<allow_empty>(
      src.begin(), src.end(), delimiter_check,
      std::inserter(*result, result->end()));
}

void SplitStringWithEscaping(absl::string_view full, unsigned char delimiter,
                             std::vector<std::string>* result) {
  SplitStringWithEscapingToContainer<false>(
      full, [delimiter](unsigned char c) { return c == delimiter; }, result);
}
void SplitStringWithEscaping(absl::string_view full,
                             const absl::CharSet& delimiters,
                             std::vector<std::string>* result) {
  SplitStringWithEscapingToContainer<false>(
      full, [&delimiters](unsigned char c) { return delimiters.contains(c); },
      result);
}

void SplitStringWithEscapingAllowEmpty(absl::string_view full,
                                       unsigned char delimiter,
                                       std::vector<std::string>* result) {
  SplitStringWithEscapingToContainer<true>(
      full, [delimiter](unsigned char c) { return c == delimiter; }, result);
}
void SplitStringWithEscapingAllowEmpty(absl::string_view full,
                                       const absl::CharSet& delimiters,
                                       std::vector<std::string>* result) {
  SplitStringWithEscapingToContainer<true>(
      full, [&delimiters](unsigned char c) { return delimiters.contains(c); },
      result);
}

void SplitStringWithEscapingToSet(absl::string_view full,
                                  unsigned char delimiter,
                                  std::set<std::string>* result) {
  SplitStringWithEscapingToContainer<false>(
      full, [delimiter](unsigned char c) { return c == delimiter; }, result);
}
void SplitStringWithEscapingToSet(absl::string_view full,
                                  const absl::CharSet& delimiters,
                                  std::set<std::string>* result) {
  SplitStringWithEscapingToContainer<false>(
      full, [&delimiters](unsigned char c) { return delimiters.contains(c); },
      result);
}

void SplitStringToLines(absl::string_view str, int max_len, int num_lines,
                        std::vector<std::string>* result) {
  if (max_len <= 0) {
    return;
  }
  for (int i = 0; (i < num_lines || num_lines <= 0); i++) {
    ptrdiff_t cut_at = ClipStringHelper(str, max_len, (i == num_lines - 1));
    if (cut_at == -1) {
      result->emplace_back(str);
      return;
    }
    result->emplace_back(str.substr(0, cut_at));
    if (i == num_lines - 1 && max_len > kCutStrSize) {
      result->at(i).append(kCutStr);
    }
    str = str.substr(cut_at);
  }
}

namespace {

// Helper class used by SplitStructuredLineInternal.
class ClosingSymbolLookup {
 public:
  explicit ClosingSymbolLookup(absl::string_view symbol_pairs)
      : closing_(), valid_closing_() {
    // Initialize the opening/closing arrays.
    for (auto symbol = symbol_pairs.begin(); symbol != symbol_pairs.end();
         ++symbol) {
      unsigned char opening = *symbol;
      ++symbol;
      // If the string ends before the closing character has been found,
      // use the opening character as the closing character.
      unsigned char closing = symbol != symbol_pairs.end() ? *symbol : opening;
      closing_[opening] = closing;
      valid_closing_[closing] = true;
      if (symbol == symbol_pairs.end()) break;
    }
  }
  ClosingSymbolLookup(const ClosingSymbolLookup&) = delete;
  ClosingSymbolLookup& operator=(const ClosingSymbolLookup&) = delete;

  // Returns the closing character corresponding to an opening one,
  // or 0 if the argument is not an opening character.
  char GetClosingChar(char opening) const {
    return closing_[static_cast<unsigned char>(opening)];
  }

  // Returns true if the argument is a closing character.
  bool IsClosing(char c) const {
    return valid_closing_[static_cast<unsigned char>(c)];
  }

 private:
  // Maps an opening character to its closing. If the entry contains 0,
  // the character is not in the opening set.
  char closing_[256];
  // Valid closing characters.
  bool valid_closing_[256];
};

char* SplitStructuredLineInternal(char* line, char delimiter,
                                  absl::string_view symbol_pairs,
                                  std::vector<char*>* cols, bool with_escapes) {
  ClosingSymbolLookup lookup(symbol_pairs);

  // Stack of symbols expected to close the current opened expressions.
  std::vector<char> expected_to_close;
  bool in_escape = false;

  ABSL_RAW_CHECK(cols != nullptr, "");
  cols->push_back(line);
  char* current;
  for (current = line; *current; ++current) {
    char c = *current;
    if (in_escape) {
      in_escape = false;
    } else if (with_escapes && c == '\\') {
      // We are escaping the next character. Note the escape still appears
      // in the output.
      in_escape = true;
    } else if (expected_to_close.empty() && c == delimiter) {
      // We don't have any open expression, this is a valid separator.
      *current = 0;
      cols->push_back(current + 1);
    } else if (!expected_to_close.empty() && c == expected_to_close.back()) {
      // Can we close the currently open expression?
      expected_to_close.pop_back();
    } else if (lookup.GetClosingChar(c)) {
      // If this is an opening symbol, we open a new expression and push
      // the expected closing symbol on the stack.
      expected_to_close.push_back(lookup.GetClosingChar(c));
    } else if (lookup.IsClosing(c)) {
      // Error: mismatched closing symbol.
      return current;
    }
  }
  if (!expected_to_close.empty()) {
    return current;  // Missing closing symbol(s)
  }
  return nullptr;  // Success
}

bool SplitStructuredLineInternal(absl::string_view line, char delimiter,
                                 absl::string_view symbol_pairs,
                                 std::vector<absl::string_view>* cols,
                                 bool with_escapes) {
  ClosingSymbolLookup lookup(symbol_pairs);

  // Stack of symbols expected to close the current opened expressions.
  std::vector<char> expected_to_close;
  bool in_escape = false;

  ABSL_RAW_CHECK(cols != nullptr, "");
  cols->push_back(line);
  for (size_t i = 0; i < line.size(); ++i) {
    char c = line[i];
    if (in_escape) {
      in_escape = false;
    } else if (with_escapes && c == '\\') {
      // We are escaping the next character. Note the escape still appears
      // in the output.
      in_escape = true;
    } else if (expected_to_close.empty() && c == delimiter) {
      // We don't have any open expression, this is a valid separator.
      cols->back().remove_suffix(line.size() - i);
      cols->push_back(line.substr(i + 1));
    } else if (!expected_to_close.empty() && c == expected_to_close.back()) {
      // Can we close the currently open expression?
      expected_to_close.pop_back();
    } else if (lookup.GetClosingChar(c)) {
      // If this is an opening symbol, we open a new expression and push
      // the expected closing symbol on the stack.
      expected_to_close.push_back(lookup.GetClosingChar(c));
    } else if (lookup.IsClosing(c)) {
      // Error: mismatched closing symbol.
      return false;
    }
  }
  if (!expected_to_close.empty()) {
    return false;  // Missing closing symbol(s)
  }
  return true;  // Success
}

}  // anonymous namespace

char* SplitStructuredLine(char* line, char delimiter,
                          absl::string_view symbol_pairs,
                          std::vector<char*>* cols) {
  return SplitStructuredLineInternal(line, delimiter, symbol_pairs, cols,
                                     false);
}

bool SplitStructuredLine(absl::string_view line, char delimiter,
                         absl::string_view symbol_pairs,
                         std::vector<absl::string_view>* cols) {
  return SplitStructuredLineInternal(line, delimiter, symbol_pairs, cols,
                                     false);
}

char* SplitStructuredLineWithEscapes(char* line, char delimiter,
                                     absl::string_view symbol_pairs,
                                     std::vector<char*>* cols) {
  return SplitStructuredLineInternal(line, delimiter, symbol_pairs, cols, true);
}

bool SplitStructuredLineWithEscapes(absl::string_view line, char delimiter,
                                    absl::string_view symbol_pairs,
                                    std::vector<absl::string_view>* cols) {
  return SplitStructuredLineInternal(line, delimiter, symbol_pairs, cols, true);
}

// ----------------------------------------------------------------------
// SplitLeadingDec32Values()
// SplitLeadingDec64Values()
//    A simple parser for space-separated decimal int32/int64 values.
//    Appends parsed integers to the end of the result vector, stopping
//    at the first unparsable spot.  Skips past leading and repeated
//    whitespace (does not consume trailing whitespace), and returns
//    a pointer beyond the last character parsed.
// --------------------------------------------------------------------
const char* SplitLeadingDec32Values(const char* str,
                                    std::vector<int32_t>* result) {
  for (;;) {
    char* end = nullptr;
    int64_t value = strtol(str, &end, 10);  // NOLINT
    if (end == str) break;
    // Limit long values to int32 min/max.  Needed for lp64.
    if (value > std::numeric_limits<int32_t>::max()) {
      value = std::numeric_limits<int32_t>::max();
    } else if (value < std::numeric_limits<int32_t>::min()) {
      value = std::numeric_limits<int32_t>::min();
    }
    result->push_back(static_cast<int32_t>(value));
    str = end;
    if (!absl::ascii_isspace(*end)) break;
  }
  return str;
}

const char* SplitLeadingDec64Values(const char* str,
                                    std::vector<int64_t>* result) {
  for (;;) {
    char* end = nullptr;
    const int64_t value = strtoll(str, &end, 10);
    if (end == str) break;
    result->push_back(value);
    str = end;
    if (!absl::ascii_isspace(*end)) break;
  }
  return str;
}

}  // namespace strings
