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

#ifndef THIRD_PARTY_GLOOP_STRINGS_SPLIT_H_
#define THIRD_PARTY_GLOOP_STRINGS_SPLIT_H_

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/internal/raw_logging.h"
#include "absl/base/nullability.h"
#include "absl/strings/charset.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

#ifdef SWIG
%include "absl/strings/str_split.h"
#endif

namespace strings {

// ----------------------------------------------------------------------
// DEPRECATED(jgm): Use absl::StrSplit() instead, e.g.,:
//   std::pair<string_view, string_view> p =
//       absl::StrSplit("a-b-c", absl::MaxSplits('-', 1))
//   // p.first = "a"
//   // p.second = "b-c"
//
//   See the absl::StrSplit() documentation for more details.
//
// SplitOneStringToken()
//   Returns the first "delim" delimited string from "*source" and modifies
//   *source to point after the delimiter that was found. If no delimiter is
//   found, *source is set to nullptr.
//
//   If the start of *source is a delimiter, an empty string is returned.
//   If *source is nullptr, an empty string is returned.
//
//   "delim" is treated as a sequence of 1 or more character delimiters. Any one
//   of the characters present in "delim" is considered to be a single
//   delimiter; the delimiter is not "delim" as a whole. For example:
//
//     const char* s = "abc=;de";
//     string r = SplitOneStringToken(&s, ";=");
//     // r = "abc"
//     // s points to ";de"
// ----------------------------------------------------------------------
std::string SplitOneStringToken(const char** source, const char* delim);

// ----------------------------------------------------------------------
// SplitOneIntToken()
// SplitOneInt32Token()
// SplitOneUint32Token()
// SplitOneInt64Token()
// SplitOneUint64Token()
// SplitOneDoubleToken()
// SplitOneFloatToken()
//   Parse a single "delim" delimited number from "*source" into "*value".
//   Modify *source to point after the delimiter.
//   If no delimiter is present after the number, set *source to nullptr.
//
//   If the start of *source is not an number, return false.
//   If the int is followed by the null character, return true.
//   If the int is not followed by a character from delim, return false.
//   If *source is nullptr, return false.
//
//   They cannot handle decimal numbers with leading 0s, since they will be
//   treated as octal.
// ----------------------------------------------------------------------
bool SplitOneIntToken(const char** source, const char* delim, int* value);
bool SplitOneInt32Token(const char** source, const char* delim, int32_t* value);
bool SplitOneUint32Token(const char** source, const char* delim,
                         uint32_t* value);
bool SplitOneInt64Token(const char** source, const char* delim, int64_t* value);
bool SplitOneUint64Token(const char** source, const char* delim,
                         uint64_t* value);
bool SplitOneDoubleToken(const char** source, const char* delim, double* value);
bool SplitOneFloatToken(const char** source, const char* delim, float* value);

// Some aliases, so that the function names are standardized against the names
// of the reflection setters/getters in proto2. This makes it easier to use
// certain macros with reflection when creating custom text formats for protos.

inline bool SplitOneUInt32Token(const char** source, const char* delim,
                                uint32_t* value) {
  return SplitOneUint32Token(source, delim, value);
}

inline bool SplitOneUInt64Token(const char** source, const char* delim,
                                uint64_t* value) {
  return SplitOneUint64Token(source, delim, value);
}

// ----------------------------------------------------------------------
// SplitOneDecimalIntToken()
// SplitOneDecimalInt32Token()
// SplitOneDecimalUint32Token()
// SplitOneDecimalInt64Token()
// SplitOneDecimalUint64Token()
// Parse a single "delim"-delimited number from "*source" into "*value".
// Unlike SplitOneIntToken, etc., this function always interprets
// the numbers as decimal.
bool SplitOneDecimalIntToken(const char** source, const char* delim,
                             int* value);
bool SplitOneDecimalInt32Token(const char** source, const char* delim,
                               int32_t* value);
bool SplitOneDecimalUint32Token(const char** source, const char* delim,
                                uint32_t* value);
bool SplitOneDecimalInt64Token(const char** source, const char* delim,
                               int64_t* value);
bool SplitOneDecimalUint64Token(const char** source, const char* delim,
                                uint64_t* value);

// ----------------------------------------------------------------------
// SplitOneHexUint32Token()
// SplitOneHexUint64Token()
// Once more, for hexadecimal numbers (unsigned only).
bool SplitOneHexUint32Token(const char** source, const char* delim,
                            uint32_t* value);
bool SplitOneHexUint64Token(const char** source, const char* delim,
                            uint64_t* value);

// ----------------------------------------------------------------------
// DEPRECATED(jgm): Use absl::StrSplit() instead, e.g.,:
//   using absl::MaxSplits
//
//   // kv.first is the key; kv.second contains all the values
//   std::pair<string, string> kv = absl::StrSplit(text, MaxSplits(kvdelim, 1));
//   std::vector<string> values = absl::StrSplit(kv.second, vv_delim);
//
//   See the absl::StrSplit() documentation for more details.
//
// SplitStringIntoKeyValues()
// Split a line into a key string and a vector of value strings. The line has
// the following format:
//
// <key><kvsep>+<vvsep>*<value1><vvsep>+<value2><vvsep>+<value3>...<vvsep>*
//
// where key and value are strings; */+ means zero/one or more; <kvsep> is
// a delimiter character to separate key and value; and <vvsep> is a delimiter
// character to separate between values. The user can specify a bunch of
// delimiter characters using a string. For example, if the user specifies
// the separator string as "\t ", then either ' ' or '\t' or any combination
// of them wil be treated as separator. For <vvsep>, the user can specify a
// empty string to indicate there is only one value.
//
// Note: this function assumes the input string begins exactly with a
// key. Therefore, if you use whitespaces to separate key and value, you
// should not let whitespace precedes the key in the input. Otherwise, you
// will get an empty string as the key.
//
// A line with no <kvsep> will return an empty string as the key, even if
// <key> is non-empty!
//
// The syntax makes it impossible for a value to be the empty string.
// It is possible for the number of values to be zero.
//
// Returns false if the line has no <kvsep> or if the number of values is
// zero.
//
// ----------------------------------------------------------------------
bool SplitStringIntoKeyValues(absl::string_view line,
                              absl::string_view key_value_delimiters,
                              absl::string_view value_value_delimiters,
                              std::string* key,
                              std::vector<std::string>* values);

// ----------------------------------------------------------------------
// DEPRECATED(jgm): Use absl::StrSplit() instead, e.g.,:
//   using absl::MaxSplits;
//
//   vector<pair<string, string>> pairs;  // or even map<string, string>
//   for (string_view sp : absl::StrSplit(line, pair_delim)) {
//     pairs.push_back(absl::StrSplit(sp, MaxSplits(kv_delim, 1)));
//   }
//
//   See the absl::StrSplit() documentation for more details.
//
// SplitStringIntoKeyValuePairs()
// Split a line into a vector of <key, value> pairs. The line has
// the following format:
//
// <kvpsep>*<key1><kvsep>+<value1><kvpsep>+<key2><kvsep>+<value2>...<kvpsep>*
//
// Where key and value are strings; */+ means zero/one or more. <kvsep> is
// a delimiter character to separate key and value and <kvpsep> is a delimiter
// character to separate key value pairs. The user can specify a bunch of
// delimiter characters using a string.
//
// Note: this function assumes each key-value pair begins exactly with a
// key. Therefore, if you use whitespaces to separate key and value, you
// should not let whitespace precede the key in the pair. Otherwise, you
// will get an empty string as the key.
//
// A pair with no <kvsep> will return empty strings as the key and value,
// even if <key> is non-empty!
//
// Returns false for pairs with no <kvsep> specified and for pairs with
// empty strings as values.
//
// ----------------------------------------------------------------------
bool SplitStringIntoKeyValuePairs(
    absl::string_view line, absl::string_view key_value_delimiters,
    absl::string_view key_value_pair_delimiters,
    std::vector<std::pair<std::string, std::string> >* kv_pairs);

// ----------------------------------------------------------------------
// SplitRange()
//
//    Splits a string of the form "<from>-<to>".  Either or both can be
//    missing.  A raw number (<from>) is interpreted as "<from>-".  Modifies
//    parameters insofar as they're specified by the string.  RETURNS
//    true iff the input is a well-formed range.  If it RETURNS false,
//    from and to remain unchanged.  If there is any whitespace in rangestr,
//    the whitespace and everything following it is ignored.
// ----------------------------------------------------------------------
bool SplitRange(absl::string_view rangestr, int32_t* from, int32_t* to);

// LEGACY(jgm): The SplitCSV* functions are no longer recommended.
// Use util::csv::Parser defined in util/csv/parser.h instead.
//
// Example: To parse a single line:
//   #include "util/csv/parser.h"
//   std::vector<string> fields = util::csv::ParseLine(line).fields();
//
// Example: To parse an entire file:
//   #include "util/csv/parser.h"
//   for (const util::csv::Record& rec : Parser(source)) {
//     std::vector<string> fields = rec.fields();
//   }
//
// SplitCSVLineWithDelimiter()
//
//    CSV lines come in many guises.  There's the Comma Separated Values
//    variety, in which fields are separated by (surprise!) commas.  There's
//    also the tab-separated values variant, in which tabs separate the
//    fields.  This routine handles both.  For both delimiters, whitespace is
//    trimmed from either side of the field value. If the delimiter is ',', we
//    play additional games with quotes.  A field value surrounded by double
//    quotes is allowed to contain commas, which are not treated as field
//    separators.  Within a double-quoted string, a series of two double quotes
//    signals an escaped single double quote. It'll be clearer in the examples.
//    Example:
//     Google , x , "Buchheit, Paul", "string with "" quote in it"
//     -->  [Google], [x], [Buchheit, Paul], [string with " quote in it]
//
// SplitCSVLine()
//    A convenience wrapper around SplitCSVLineWithDelimiter which uses
//    ',' as the delimiter.
//
void SplitCSVLine(char* absl_nonnull line, std::vector<char*>* cols);
void SplitCSVLineWithDelimiter(char* absl_nonnull line, char delimiter,
                               std::vector<char*>* cols);
// SplitCSVLine string wrapper that internally makes a copy of string line.
void SplitCSVLineWithDelimiterForStrings(absl::string_view line, char delimiter,
                                         std::vector<std::string>* cols);

namespace strings_internal {

template <typename Container, typename InsertPolicy>
bool SplitStringAndParseToInserter(
    absl::string_view source, absl::string_view delimiters,
    bool (*parse)(const std::string& str,
                  typename Container::value_type* value),
    Container* result, InsertPolicy insert_policy) {
  ABSL_RAW_CHECK(parse != nullptr, "Parsing function must not be null.");
  ABSL_RAW_CHECK(result != nullptr, "Output container must not be null.");
  ABSL_RAW_CHECK(delimiters.data() != nullptr, "Delimiters must not be null.");
  ABSL_RAW_CHECK(!delimiters.empty(), "Delimiters must have non-zero length.");
  bool retval = true;
  for (absl::string_view piece :
       absl::StrSplit(source, absl::ByAnyChar(delimiters), absl::SkipEmpty())) {
    typename Container::value_type t;
    if (parse(std::string(piece), &t)) {
      insert_policy(result, t);
    } else {
      retval = false;
    }
  }
  return retval;
}
template <typename Container, typename InsertPolicy>
bool SplitStringAndParseToInserter(
    absl::string_view source, absl::string_view delimiters,
    bool (*parse)(absl::string_view str, typename Container::value_type* value),
    Container* result, InsertPolicy insert_policy) {
  ABSL_RAW_CHECK(parse != nullptr, "Parsing function must not be null.");
  ABSL_RAW_CHECK(result != nullptr, "Output container must not be null.");
  ABSL_RAW_CHECK(delimiters.data() != nullptr, "Delimiters must not be null.");
  ABSL_RAW_CHECK(!delimiters.empty(), "Delimiters must have non-zero length.");
  bool retval = true;
  for (absl::string_view piece :
       absl::StrSplit(source, absl::ByAnyChar(delimiters), absl::SkipEmpty())) {
    typename Container::value_type t;
    if (parse(piece, &t)) {
      insert_policy(result, t);
    } else {
      retval = false;
    }
  }
  return retval;
}

// Cannot use output iterator here (e.g. std::inserter, std::back_inserter)
// because some callers use non-standard containers that don't have iterators,
// only an insert() or push_back() method.
struct BasicInsertPolicy {
  template <typename C, typename V>
  void operator()(C* c, const V& v) const {
    c->insert(v);
  }
};

struct BackInsertPolicy {
  template <typename C, typename V>
  void operator()(C* c, const V& v) const {
    c->push_back(v);
  }
};

}  // namespace strings_internal

// ClipString()
//
// Clips a string to a max length. We try to clip on a word boundary if this is
// possible. If the string is clipped, we append an ellipsis.
//
// ***NOTE***
// ClipString counts length with strlen.  If you have non-single-byte strings
// like UTF-8, this is wrong.  If you are displaying the clipped strings to
// users in a frontend, consider using ClipStringOnWordBoundary in
// webserver/util/snippets/rewriteboldtags, which considers the width of the
// string, not just the number of bytes.
//
// TODO Move ClipString back to strutil.  The problem with this is
// that ClipStringHelper is used behind the scenes by SplitStringToLines, but
// probably shouldn't be exposed in the .h files.
void ClipString(std::string* full_str, ptrdiff_t max_len);

// SplitStringToLines()
//
// Splits a string into lines of maximum length
// 'max_len'. Append the resulting lines to 'result'. Will attempt
// to split on word boundaries.  If 'num_lines'
// is zero it splits up the whole string regardless of length. If
// 'num_lines' is positive, it returns at most num_lines lines, and
// appends a "..." to the end of the last line if the string is too
// long to fit completely into 'num_lines' lines.
void SplitStringToLines(absl::string_view str, int max_len, int num_lines,
                        std::vector<std::string>* result);

// SplitStringWithEscaping()
// SplitStringWithEscapingAllowEmpty()
// SplitStringWithEscapingToSet()
//
//   Splits the string using the specified delimiters, taking escaping into
//   account. '\' is not allowed as a delimiter.
//
//   Within the string, preserve a delimiter preceded by a backslash as a
//   literal delimiter. In addition, preserve two consecutive backslashes as
//   a single literal backslash. Do not unescape any other backslash-character
//   sequence.
//
//   Eg. 'foo\=bar=baz\\qu\ux' split on '=' becomes ('foo=bar', 'baz\qu\ux')
//
//   All versions other than "AllowEmpty" discard any empty substrings.
void SplitStringWithEscaping(absl::string_view full, unsigned char delimiter,
                             std::vector<std::string>* result);
void SplitStringWithEscaping(absl::string_view full,
                             const absl::CharSet& delimiters,
                             std::vector<std::string>* result);
void SplitStringWithEscapingAllowEmpty(absl::string_view full,
                                       unsigned char delimiter,
                                       std::vector<std::string>* result);
void SplitStringWithEscapingAllowEmpty(absl::string_view full,
                                       const absl::CharSet& delimiters,
                                       std::vector<std::string>* result);
void SplitStringWithEscapingToSet(absl::string_view full,
                                  unsigned char delimiter,
                                  std::set<std::string>* result);
void SplitStringWithEscapingToSet(absl::string_view full,
                                  const absl::CharSet& delimiters,
                                  std::set<std::string>* result);

// SplitStringAndParse()
// SplitStringAndParseToContainer()
// SplitStringAndParseToList()
//
//    Splits a string using a list of character delimiters.  For each
//    component, parse using the provided parsing function and if
//    successful, append it to 'result'. Return true if and only if
//    all components parse successfully. If there are consecutive
//    delimiters, this function skips over all of them.  This function
//    will correctly handle parsing strings that have embedded \0s.
//
// SplitStringAndParse fills into a vector.
// SplitStringAndParseToContainer fills into any container that implements
//    a single-argument insert function. (i.e. insert(const value_type& x) ).
// SplitStringAndParseToList fills into any container that implements a single-
//    argument push_back function (i.e. push_back(const value_type& x) ), plus
//    value_type& back() and pop_back().
//    NOTE: This implementation relies on parsing in-place into the "back()"
//    reference, so its performance may depend on the efficiency of back().
//
// Example Usage (verified in split_test.cc):
//  std::vector<double> values;
//  CHECK(SplitStringAndParse("1.0,2.0,3.0", ",", &safe_strtod, &values));
//  CHECK_EQ(3, values.size());
//
//  std::set<int64> values;
//  CHECK(SplitStringAndParseToContainer("3,1,1,2", ",",
//        &safe_strto64, &values));
//  CHECK_EQ(3, values.size());  // std::set<> retains only unique values
//
//  std::deque<int64> values;
//  CHECK(SplitStringAndParseToList("3,1,1,2", ",", &safe_strto64, &values));
//  CHECK_EQ(4, values.size());
template <typename T>
bool SplitStringAndParse(absl::string_view source, absl::string_view delimiter,
                         bool (*parse)(const std::string& str, T* value),
                         std::vector<T>* result);
template <typename T>
bool SplitStringAndParse(absl::string_view source, absl::string_view delimiter,
                         bool (*parse)(absl::string_view str, T* value),
                         std::vector<T>* result);
template <typename Container>
bool SplitStringAndParseToContainer(
    absl::string_view source, absl::string_view delimiter,
    bool (*parse)(const std::string& str,
                  typename Container::value_type* value),
    Container* result);
template <typename Container>
bool SplitStringAndParseToContainer(
    absl::string_view source, absl::string_view delimiter,
    bool (*parse)(absl::string_view str, typename Container::value_type* value),
    Container* result);

template <typename List>
bool SplitStringAndParseToList(absl::string_view source,
                               absl::string_view delimiter,
                               bool (*parse)(const std::string& str,
                                             typename List::value_type* value),
                               List* result);
template <typename List>
bool SplitStringAndParseToList(absl::string_view source,
                               absl::string_view delimiter,
                               bool (*parse)(absl::string_view str,
                                             typename List::value_type* value),
                               List* result);

// SplitStructuredLine()
//
//    Splits a line using the given delimiter, and places the columns into
//    'cols'. If the symbol_pair string has an odd number of characters, the
//    last character (which cannot be paired) will be assumed to be both an
//    opening and closing symbol.
//    WARNING : The input string 'line' is destroyed in the process.
//    The function returns 0 if the line was parsed correctly (i.e all the
//    opened braces had their closing braces) otherwise, it returns the position
//    of the error.
//    Example:
//     SplitStructuredLine("item1,item2,{subitem1,subitem2},item4,[5,{6,7}]",
//                         ',',
//                         "{}[]", &output)
//     --> output = { "item1", "item2", "{subitem1,subitem2}", "item4",
//                    "[5,{6,7}]" }
//    Example2: trying to split "item1,[item2,{4,5],5}" will fail and the
//              function will return the position of the problem : ]
char* SplitStructuredLine(char* line, char delimiter,
                          absl::string_view symbol_pairs,
                          std::vector<char*>* cols);

// Similar to the function with the same name above, but splits an
// absl::string_view into absl::string_view parts. Returns true if successful.
bool SplitStructuredLine(absl::string_view line, char delimiter,
                         absl::string_view symbol_pairs,
                         std::vector<absl::string_view>* cols);

// SplitStructuredLineWithEscapes()
//
//    Like SplitStructuredLine but also allows characters to be escaped.
//
//    WARNING: the escape characters will be replicated in the output
//    columns rather than being consumed, i.e. if {} were the opening and
//    closing symbols, using \{ to quote a curly brace in the middle of
//    an option would pass this unchanged.
//
//    Example:
//     SplitStructuredLineWithEscapes(
//       "\{item1\},it\\em2,{\{subitem1\},sub\\item2},item4\,item5,[5,{6,7}]",
//                     ',',
//                     "{}[]",
//                     &output)
//     --> output = { "\{item1\}", "it\\em2", "{\{subitem1\},sub\\item2}",
//                    "item4\,item5", "[5,{6,7}]" }
char* SplitStructuredLineWithEscapes(char* line, char delimiter,
                                     absl::string_view symbol_pairs,
                                     std::vector<char*>* cols);

// Similar to the function with the same name above, but splits an
// absl::string_view into absl::string_view parts. Returns true if successful.
bool SplitStructuredLineWithEscapes(absl::string_view line, char delimiter,
                                    absl::string_view symbol_pairs,
                                    std::vector<absl::string_view>* cols);

// SplitLeadingDec32Values()
// SplitLeadingDec64Values()
//
// Splits space-separated decimal int32/int64 values. Appends parsed integers to
// the end of the result vector, stopping at the first unparsable spot. Skips
// past leading and repeated whitespace (does not consume trailing whitespace),
// and returns a pointer beyond the last character parsed.
const char* SplitLeadingDec32Values(const char* str,
                                    std::vector<int32_t>* result);
const char* SplitLeadingDec64Values(const char* str,
                                    std::vector<int64_t>* result);

// SplitStringAndParse()
template <typename T>
bool SplitStringAndParse(absl::string_view source, absl::string_view delimiter,
                         bool (*parse)(const std::string& str, T* value),
                         std::vector<T>* result) {
  return SplitStringAndParseToList(source, delimiter, parse, result);
}
template <typename T>
bool SplitStringAndParse(absl::string_view source, absl::string_view delimiter,
                         bool (*parse)(absl::string_view str, T* value),
                         std::vector<T>* result) {
  return SplitStringAndParseToList(source, delimiter, parse, result);
}

// SplitStringAndParseToContainer()
template <typename Container>
bool SplitStringAndParseToContainer(
    absl::string_view source, absl::string_view delimiter,
    bool (*parse)(const std::string& str,
                  typename Container::value_type* value),
    Container* result) {
  return strings::strings_internal::SplitStringAndParseToInserter(
      source, delimiter, parse, result,
      strings::strings_internal::BasicInsertPolicy());
}
template <typename Container>
bool SplitStringAndParseToContainer(
    absl::string_view source, absl::string_view delimiter,
    bool (*parse)(absl::string_view str, typename Container::value_type* value),
    Container* result) {
  return strings::strings_internal::SplitStringAndParseToInserter(
      source, delimiter, parse, result,
      strings::strings_internal::BasicInsertPolicy());
}

// SplitStringAndParseToList()
template <typename List>
bool SplitStringAndParseToList(absl::string_view source,
                               absl::string_view delimiter,
                               bool (*parse)(const std::string& str,
                                             typename List::value_type* value),
                               List* result) {
  return strings::strings_internal::SplitStringAndParseToInserter(
      source, delimiter, parse, result,
      strings::strings_internal::BackInsertPolicy());
}
template <typename List>
bool SplitStringAndParseToList(absl::string_view source,
                               absl::string_view delimiter,
                               bool (*parse)(absl::string_view str,
                                             typename List::value_type* value),
                               List* result) {
  return strings::strings_internal::SplitStringAndParseToInserter(
      source, delimiter, parse, result,
      strings::strings_internal::BackInsertPolicy());
}

}  // namespace strings

// Old names for <link>:
using ::strings::SplitCSVLine;                         // NOLINT
using ::strings::SplitCSVLineWithDelimiterForStrings;  // NOLINT
using ::strings::SplitOneDecimalInt32Token;            // NOLINT
using ::strings::SplitOneDecimalInt64Token;            // NOLINT
using ::strings::SplitOneDecimalIntToken;              // NOLINT
using ::strings::SplitOneDecimalUint32Token;           // NOLINT
using ::strings::SplitOneDoubleToken;                  // NOLINT
using ::strings::SplitOneInt32Token;                   // NOLINT
using ::strings::SplitOneInt64Token;                   // NOLINT
using ::strings::SplitOneIntToken;                     // NOLINT
using ::strings::SplitOneStringToken;                  // NOLINT
using ::strings::SplitOneUint64Token;                  // NOLINT
using ::strings::SplitRange;                           // NOLINT
using ::strings::SplitStringIntoKeyValuePairs;         // NOLINT

// Old names for <link>:
using ::strings::ClipString;                         // NOLINT
using ::strings::SplitLeadingDec32Values;            // NOLINT
using ::strings::SplitLeadingDec64Values;            // NOLINT
using ::strings::SplitStringAndParse;                // NOLINT
using ::strings::SplitStringAndParseToContainer;     // NOLINT
using ::strings::SplitStringAndParseToList;          // NOLINT
using ::strings::SplitStringToLines;                 // NOLINT
using ::strings::SplitStringWithEscaping;            // NOLINT
using ::strings::SplitStringWithEscapingAllowEmpty;  // NOLINT
using ::strings::SplitStringWithEscapingToSet;       // NOLINT
using ::strings::SplitStructuredLine;                // NOLINT
using ::strings::SplitStructuredLineWithEscapes;     // NOLINT

#endif  // THIRD_PARTY_GLOOP_STRINGS_SPLIT_H_
