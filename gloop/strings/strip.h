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

#ifndef THIRD_PARTY_GLOOP_STRINGS_STRIP_H_
#define THIRD_PARTY_GLOOP_STRINGS_STRIP_H_

#include <stddef.h>

#include <string>

#include "absl/base/macros.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"

namespace strings {

// Replaces runs of one or more ASCII 'dup_char' with a single occurrence,
// and returns the number of characters that were removed.
//
// Example:
//       StripDupCharacters("a//b/c//d", '/', 0) => "a/b/c/d"
ptrdiff_t StripDupCharacters(std::string* s, char dup_char,
                             ptrdiff_t start_pos);

// Removes the trailing '\n' or '\r\n' from 's', if one exists. Returns true if
// a newline was found and removed.
bool StripTrailingNewline(std::string* s);

// Strips everything enclosed in pairs of curly braces ('{' and '}') and the
// curly braces themselves. Doesn't touch open braces without a closing brace.
// Does not handle nesting.
void StripCurlyBraces(std::string* s);

// Performs the same operation as StripCurlyBraces, but allows the caller to
// specify different left and right bracket ASCII characters,
// such as '(' and ')'.
void StripBrackets(char left, char right, std::string* s);

// Strips everything between a right angle bracket ('<') and left angle bracket
// ('>') including the brackets themselves, e.g.
// "the quick <b>brown</b> fox" --> "the quick brown fox".
//
// This does not understand HTML nor does it know anything about HTML tags or
// comments. This is simply a text processing function that removes text between
// pairs of angle brackets. Note that in the example above the word "brown" is
// not removed because it is not between pairs of angle brackets.
//
// This is NOT safe for security and this will NOT prevent against XSS.
//
// For a more full-featured HTML parser, see //webutil/pageutil/pageutil.h.
void StripMarkupTags(std::string* s);
std::string OutputWithMarkupTagsStripped(const std::string& s);

// Removes any occurrences of the *bytes* in 'remove' from the:
//
//   - start of the string "Left"
//   - end of the string "Right"
//   - both ends of the string
//
// *Warning*: The Trim... functions operate on *bytes* in the remove string.
// When the remove string contains multi-byte (non-ASCII) characters,
// then some strings will turn into garbage which will break downstream code.
// Use icu::UnicodeSet and its spanUTF8()/spanBackUTF8().
//
// Returns the number of chars removed.
ptrdiff_t TrimStringLeft(std::string* s, absl::string_view remove);
ptrdiff_t TrimStringRight(std::string* s, absl::string_view remove);
inline ptrdiff_t TrimString(std::string* s, absl::string_view remove) {
  return TrimStringRight(s, remove) + TrimStringLeft(s, remove);
}
ptrdiff_t TrimStringLeft(absl::string_view* s, absl::string_view remove);
ptrdiff_t TrimStringRight(absl::string_view* s, absl::string_view remove);
inline ptrdiff_t TrimString(absl::string_view* s, absl::string_view remove) {
  return TrimStringRight(s, remove) + TrimStringLeft(s, remove);
}

// Removes leading and trailing runs, and collapses middle runs of a set of
// *bytes* into a single *byte* (the first one specified in 'remove').
// E.g.: TrimRunsInString(&s, " :,()") removes leading and trailing delimiter
// chars and collapses and converts internal runs of delimiters to single ' '
// characters, so, for example, "  a:(b):c  " -> "a b c".
//
// *Warning*: This function operates on *bytes* in the remove string.
// When the remove string contains multi-byte (non-ASCII) characters,
// then some strings will turn into garbage which will break downstream code.
// Use icu::UnicodeSet and its spanUTF8()/spanBackUTF8().
void TrimRunsInString(std::string* s, absl::string_view remove);

// Removes all internal '\0' characters from the string.
void RemoveNullsInString(std::string* s);

// Removes all occurrences of the given ASCII character from the given string.
// Returns the new length.
ptrdiff_t strrm(char* str, char c);
ptrdiff_t memrm(char* str, ptrdiff_t strlen, char c);

// Removes all occurrences of any *byte* from 'chars' from the given string.
// Returns the new length.
//
// *Warning*: This function operates on *bytes* in the remove string.
// When the remove string contains multi-byte (non-ASCII) characters,
// then some strings will turn into garbage which will break downstream code.
// Use icu::UnicodeSet and its spanUTF8()/spanBackUTF8().
ptrdiff_t strrmm(char* str, const char* chars);
ptrdiff_t strrmm(std::string* str, const std::string& chars);

// Returns a copy of the input string 'str' with the given 'prefix' removed. If
// the prefix doesn't match, returns a copy of the original string.
//
// The "Try" version stores the stripped string in the 'result' out-param.
// It returns true iff the prefix was found and removed. It is safe for 'result'
// to point back to the input string.
//
// See also absl::ConsumePrefix().
ABSL_DEPRECATE_AND_INLINE()
inline std::string StripPrefixString(absl::string_view str,
                                     absl::string_view prefix) {
  return std::string(absl::StripPrefix(str, prefix));
}
inline bool TryStripPrefixString(absl::string_view str,
                                 absl::string_view prefix,
                                 std::string* result) {
  bool res = absl::ConsumePrefix(&str, prefix);
  result->assign(str.begin(), str.end());
  return res;
}

// Returns a copy of the input string 'str' with the given 'suffix' removed. If
// the suffix doesn't match, returns a copy of the original string.
//
// The "Try" version stores the stripped string in the 'result' out-param and
// returns true iff the suffix was found and removed. It is safe for 'result' to
// point back to the input string.
//
// See also absl::ConsumeSuffix().
ABSL_DEPRECATE_AND_INLINE()
inline std::string StripSuffixString(absl::string_view str,
                                     absl::string_view suffix) {
  return std::string(absl::StripSuffix(str, suffix));
}
inline bool TryStripSuffixString(absl::string_view str,
                                 absl::string_view suffix,
                                 std::string* result) {
  bool res = absl::ConsumeSuffix(&str, suffix);
  result->assign(str.begin(), str.end());
  return res;
}

// Replaces any of the *bytes* in `remove` with the *byte* `replace_with`.
//
// *Warning*: This function operates on *bytes* in the remove string.
// When the remove string contains multi-byte (non-ASCII) characters,
// then some strings will turn into garbage which will break downstream code.
// Use icu::UnicodeSet and its spanUTF8()/spanBackUTF8().
void ReplaceCharacters(char* str, size_t len, absl::string_view remove,
                       char replace_with);
void ReplaceCharacters(std::string* s, absl::string_view remove,
                       char replace_with);

// Replaces the character `remove` with the character `replace_with`.
inline void ReplaceCharacter(char* str, size_t len, char remove,
                             char replace_with) {
  if (str == nullptr) return;
  for (size_t i = 0; i < len; ++i) {
    if (str[i] == remove) str[i] = replace_with;
  }
}

// Returns a pointer to the first character in `str` that is not
// ASCII whitespace. Never returns nullptr. `str` must be NUL-terminated.
[[deprecated("Use absl::StripLeadingAsciiWhitespace()")]]
inline const char* SkipLeadingWhitespace(const char* str) {
  while (absl::ascii_isspace(*str)) ++str;
  return str;
}

[[deprecated("Use absl::StripLeadingAsciiWhitespace()")]]
inline char* SkipLeadingWhitespace(char* str) {
  while (absl::ascii_isspace(*str)) ++str;
  return str;
}

}  // namespace strings

// Old names for <link>:

ABSL_DEPRECATE_AND_INLINE()
inline std::string StripPrefixString(absl::string_view str,
                                     absl::string_view prefix) {
  return std::string(absl::StripPrefix(str, prefix));
}

ABSL_DEPRECATE_AND_INLINE()
inline std::string StripSuffixString(absl::string_view str,
                                     absl::string_view suffix) {
  return std::string(absl::StripSuffix(str, suffix));
}

ABSL_DEPRECATE_AND_INLINE()
inline bool TryStripPrefixString(absl::string_view str,
                                 absl::string_view prefix,
                                 std::string* result) {
  return ::strings::TryStripPrefixString(str, prefix, result);
}

ABSL_DEPRECATE_AND_INLINE()
inline bool TryStripSuffixString(absl::string_view str,
                                 absl::string_view suffix,
                                 std::string* result) {
  return ::strings::TryStripSuffixString(str, suffix, result);
}

#endif  // THIRD_PARTY_GLOOP_STRINGS_STRIP_H_
