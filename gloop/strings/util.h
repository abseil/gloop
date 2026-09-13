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

// Useful string functions and so forth.  This is a grab-bag file.
//
// You might also want to look at memutil.h, which holds mem*()
// equivalents of a lot of the str*() functions in string.h,
// eg memstr, mempbrk, etc.
//
// These functions work fine for UTF-8 strings as long as you can
// consider them to be just byte strings.  For example, due to the
// design of UTF-8 you do not need to worry about accidental matches,
// as long as all your inputs are valid UTF-8 (use \uHHHH, not \xHH or \oOOO).
//
// Caveats:
// * all the lengths in these routines refer to byte counts,
//   not character counts.
// * case-insensitivity in these routines assumes that all the letters
//   in question are in the range A-Z or a-z.
//
// If you need Unicode specific processing (for example being aware of
// Unicode character boundaries, or knowledge of Unicode casing rules,
// or various forms of equivalence and normalization), take a look at
// files in i18n/utf8.

#ifndef THIRD_PARTY_GLOOP_STRINGS_UTIL_H_
#define THIRD_PARTY_GLOOP_STRINGS_UTIL_H_

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/strings/string_view.h"
#include "gloop/strings/string_view_utils.h"

// Newer functions.

namespace strings {

// Finds the next end-of-line sequence.
// An end-of-line sequence is one of:
//   \n    common on unix, including mac os x
//   \r    common on macos 9 and before
//   \r\n  common on windows
//
// Returns an absl::string_view that contains the end-of-line sequence (a
// pointer into the input, 1 or 2 characters long).
//
// If the input does not contain an end-of-line sequence, returns an empty
// absl::string_view located at the end of the input:
//    absl::string_view(sp.data() + sp.length(), 0).
absl::string_view FindEol(absl::string_view sp);

}  // namespace strings

// Older functions.

// Duplicates a non-null, non-empty char* string. Returns a pointer to the new
// string, or nullptr if the input is null or empty.
inline char* strdup_nonempty(const char* src) {
  if (src && src[0]) {
#ifdef _MSC_VER
    return _strdup(src);
#else
    return strdup(src);
#endif
  }
  return nullptr;
}

// Finds the first occurrence of a character in at most a given number of bytes
// of a char* string. Returns a pointer to the first occurrence, or nullptr if
// no occurrence found in the first sz bytes.
// Never searches past the first null character in the string; therefore, only
// suitable for NUL-terminated strings.
// WARNING: Removes const-ness of string argument!
inline char* strnchr(const char* buf, char c, size_t sz) {
  const char* end = buf + sz;
  while (buf != end && *buf) {
    if (*buf == c) return const_cast<char*>(buf);
    ++buf;
  }
  return nullptr;
}

// Finds the first occurrence of the null-terminated needle in at most the first
// haystack_len bytes of haystack. Returns nullptr if needle is not found.
// Returns haystack if needle is empty.
// WARNING: Removes const-ness of string argument!
char* strnstr(const char* haystack, const char* needle, size_t haystack_len);

// Matches a prefix (which must be a char* literal!) against the beginning of
// str. Returns a pointer past the prefix, or nullptr if the prefix wasn't
// matched. (Like the standard strcasecmp(), but for efficiency doesn't call
// strlen() on prefix, and returns a pointer rather than an int.)
//
// For a similar function that works on absl::string_view, see
// absl::ConsumePrefix().
//
// The ""'s catch people who don't pass in a literal for "prefix"
#ifndef strprefix
#define strprefix(str, prefix)                         \
  (strncmp(str, prefix, sizeof("" prefix "") - 1) == 0 \
       ? str + sizeof(prefix) - 1                      \
       : nullptr)
#endif

// Same as strprefix() (immediately above), but matches a case-insensitive
// prefix.
#ifndef strcaseprefix
#ifdef _MSC_VER
#define strcaseprefix(str, prefix)                       \
  (_strnicmp(str, prefix, sizeof("" prefix "") - 1) == 0 \
       ? str + sizeof(prefix) - 1                        \
       : nullptr)
#else  // _MSC_VER
#define strcaseprefix(str, prefix)                         \
  (strncasecmp(str, prefix, sizeof("" prefix "") - 1) == 0 \
       ? str + sizeof(prefix) - 1                          \
       : nullptr)
#endif  // _MSC_VER
#endif  // strcaseprefix

// Matches a prefix (up to the first needle_size bytes of needle) in the first
// haystack_size byte of haystack. Returns a pointer past the prefix, or nullptr
// if the prefix wasn't matched. (Unlike strprefix(), prefix doesn't need to be
// a char* literal. Like the standard strncmp(), but also takes a haystack_size,
// and returns a pointer rather than an int.)
//
// Always returns either nullptr or haystack + needle_size.
const char* strnprefix(const char* haystack, ptrdiff_t haystack_size,
                       const char* needle, ptrdiff_t needle_size);

// Matches a case-insensitive prefix (up to the first needle_size bytes of
// needle) in the first haystack_size byte of haystack. Returns a pointer past
// the prefix, or nullptr if the prefix wasn't matched.
//
// Always returns either nullptr or haystack + needle_size.
const char* strncaseprefix(const char* haystack, ptrdiff_t haystack_size,
                           const char* needle, ptrdiff_t needle_size);

// Matches a prefix; returns a pointer past the prefix, or nullptr if not found.
// (Like strprefix() and strcaseprefix() but not restricted to searching for
// char* literals). Templated so searching a const char* returns a const char*,
// and searching a non-const char* returns a non-const char*.
template <typename CharStar>
inline CharStar var_strprefix(CharStar str, const char* prefix) {
  const ptrdiff_t len = strlen(prefix);
  return strncmp(str, prefix, len) == 0 ? str + len : nullptr;
}

// Same as var_strprefix() (immediately above), but matches a case-insensitive
// prefix.
template <typename CharStar>
inline CharStar var_strcaseprefix(CharStar str, const char* prefix) {
  const ptrdiff_t len = strlen(prefix);
#ifdef _MSC_VER
  return _strnicmp(str, prefix, len) == 0 ? str + len : nullptr;
#else
  return strncasecmp(str, prefix, len) == 0 ? str + len : nullptr;
#endif
}

// Returns input, or "(null)" if nullptr. (Useful for logging.)
inline const char* GetPrintableString(const char* const in) {
  return nullptr == in ? "(null)" : in;
}

// Returns where suffix begins in str, or nullptr if str doesn't end with
// suffix.
inline char* strsuffix(char* str, const char* suffix) {
  const size_t lenstr = strlen(str);
  const size_t lensuffix = strlen(suffix);
  char* strbeginningoftheend = str + lenstr - lensuffix;

  if (lenstr >= lensuffix && 0 == strcmp(strbeginningoftheend, suffix)) {
    return (strbeginningoftheend);
  } else {
    return (nullptr);
  }
}
inline const char* strsuffix(const char* str, const char* suffix) {
  return const_cast<const char*>(strsuffix(const_cast<char*>(str), suffix));
}

// Same as strsuffix() (immediately above), but matches a case-insensitive
// suffix.
char* strcasesuffix(char* str, const char* suffix);
inline const char* strcasesuffix(const char* str, const char* suffix) {
  return const_cast<const char*>(strcasesuffix(const_cast<char*>(str), suffix));
}

const char* strnsuffix(const char* haystack, ptrdiff_t haystack_size,
                       const char* needle, ptrdiff_t needle_size);
const char* strncasesuffix(const char* haystack, ptrdiff_t haystack_size,
                           const char* needle, ptrdiff_t needle_size);

// Returns the number of times a character occurs in a string for a null
// terminated string.
inline ptrdiff_t strcount(const char* buf, char c) {
  if (buf == nullptr) return 0;
  ptrdiff_t num = 0;
  for (const char* bp = buf; *bp != '\0'; bp++) {
    if (*bp == c) num++;
  }
  return num;
}
// Returns the number of times a character occurs in a string for a string
// defined by a pointer to the first character and a pointer just past the last
// character.
inline ptrdiff_t strcount(const char* buf_begin, const char* buf_end, char c) {
  if (buf_begin == nullptr) return 0;
  if (buf_end <= buf_begin) return 0;
  ptrdiff_t num = 0;
  for (const char* bp = buf_begin; bp != buf_end; bp++) {
    if (*bp == c) num++;
  }
  return num;
}
// Returns the number of times a character occurs in a string for a string
// defined by a pointer to the first char and a length:
inline ptrdiff_t strcount(const char* buf, size_t len, char c) {
  return strcount(buf, buf + len, c);
}
// Returns the number of times a character occurs in a string_view:
inline ptrdiff_t strcount(const absl::string_view buf, char c) {
  return strcount(buf.data(), buf.size(), c);
}

// Returns a pointer to the nth occurrence of a character in a null-terminated
// string.
// WARNING: Removes const-ness of string argument!
char* strchrnth(const char* str, char c, int n);

// Returns a pointer to the nth occurrence of a character in a null-terminated
// string, or the last occurrence if occurs fewer than n times.
// WARNING: Removes const-ness of string argument!
char* AdjustedLastPos(const char* str, char separator, int n);

// STL-compatible function objects for char* string keys:

// Compares two char* strings for equality. (Works with null, which compares
// equal only to another null). Useful in hash tables:
//    hash_map<const char*, Value, hash<const char*>, streq> ht;
struct streq {
  bool operator()(const char* s1, const char* s2) const {
    return ((s1 == nullptr && s2 == nullptr) ||
            (s1 && s2 && *s1 == *s2 && strcmp(s1, s2) == 0));
  }
};

// Compares two char* strings. (Works with nullptr, which compares greater than
// any non-null). Useful in maps:
//    std::map<const char*, Value, strlt> m;
struct strlt {
  bool operator()(const char* s1, const char* s2) const {
    return (s1 != s2) &&
           (s2 == nullptr || (s1 != nullptr && strcmp(s1, s2) < 0));
  }
};

// Returns whether str has only Ascii characters (as defined by
// absl::ascii_isascii() in strings/ascii.h).
bool IsAscii(absl::string_view str);
ABSL_DEPRECATE_AND_INLINE()
inline bool IsAscii(const char* str, ptrdiff_t len) {
  return IsAscii(absl::string_view(str, len));
}

namespace strings {

// Returns whether str has only printable characters (as defined by
// absl::ascii_isprint() in strings/ascii.h).
bool IsPrint(absl::string_view str);

}  // namespace strings

// Returns the smallest lexicographically larger string of equal or smaller
// length. Returns an empty string if there is no such successor (if the input
// is empty or consists entirely of 0xff bytes).
// Useful for calculating the smallest lexicographically larger string
// that will not be prefixed by the input string.
//
// Examples:
// "a" -> "b", "aaa" -> "aab", "aa\xff" -> "ab", "\xff" -> "", "" -> ""
void PrefixSuccessor(std::string* prefix);
inline std::string PrefixSuccessor(absl::string_view prefix) {
  std::string limit(prefix);
  PrefixSuccessor(&limit);
  return limit;
}

// Returns the immediate lexicographically-following string.
//
// WARNING: Returns the input string with a '\0' appended; if you call c_str()
// on the result, it will compare equal to s.
std::string ImmediateSuccessor(absl::string_view s);

// Fills in *separator with a short string less than limit but greater than or
// equal to start. If limit is greater than start, *separator is the common
// prefix of start and limit, followed by the successor to the next character in
// start. Examples:
// FindShortestSeparator("foobar", "foxhunt", &sep) => sep == "fop"
// FindShortestSeparator("abracadabra", "bacradabra", &sep) => sep == "b"
// If limit is less than or equal to start, fills in *separator with start.
void FindShortestSeparator(absl::string_view start, absl::string_view limit,
                           std::string* separator);

// Copies at most n-1 bytes from src to dest, and returns dest. If n >=1, null
// terminates dest; otherwise, returns dest unchanged. Unlike strncpy(), only
// puts one null character at the end of dest.
inline char* safestrncpy(char* dest, const char* src, size_t n) {
  if (n < 1) return dest;

  // Avoid using non-ANSI memccpy(), which is also deprecated in MSVC
  for (size_t i = 0; i < n; ++i) {
    if ((dest[i] = src[i]) == '\0') return dest;
  }

  dest[n - 1] = '\0';
  return dest;
}

// Case-insensitive strstr(); use system strcasestr() instead.
// WARNING: Removes const-ness of string argument!
char* gstrcasestr(const char* haystack, const char* needle);

// Finds (case insensitively) the first occurrence of (NUL-terminated) needle
// in at most the first len bytes of haystack. Returns a pointer into haystack,
// or nullptr if needle wasn't found.
const char* gstrncasestr(const char* haystack, const char* needle, size_t len);

// Finds (case insensitively), in str (which is a list of tokens separated by
// non_alpha), a token prefix and a token suffix. Returns a pointer into str of
// the position of prefix, or nullptr if not found.
// WARNING: Removes const-ness of string argument!
char* gstrncasestr_split(const char* str, const char* prefix, char non_alpha,
                         const char* suffix, size_t n);

// Finds (case insensitively) needle in haystack, paying attention only to
// alphanumerics in either string. Returns a pointer into haystack, or nullptr
// if not found.
// Example: strcasestr_alnum("This is a longer test string", "IS-A-LONGER")
// returns a pointer to "is a longer".
// WARNING: Removes const-ness of string argument!
char* strcasestr_alnum(const char* haystack, const char* needle);

// Returns the number times substring appears in text. Overlapping substrings
// are all counted, so CountSubstring("aaa", "aa") == 2.
// Worst-case performance is O(text.length() * substring.length()), but only
// when there are many overlapping matches.
int CountSubstring(absl::string_view text, absl::string_view substring);

// Returns a pointer to the start of needle in haystack.  The haystack is
// interpreted as tokens separated by one or more of delim; to be found,
// needle's occurrence must start and end on whole token boundaries.
// If the needle is not found, or if either of the parameters is a null
// pointer, the call returns a null pointer.
// An empty string needle is found at the beginning of any non-null haystack,
// including an empty string haystack.
//
// NOTE: Consider instead using absl::StrSplit()
//       (absl/strings/str_split.h) and std::find().
const char* strstr_delimited(const char* haystack, const char* needle,
                             char delim);

// Gets the next token from string *stringp, where tokens are strings separated
// by characters from delim.
char* gstrsep(char** stringp, const char* delim);

// Returns a duplicate of the_string, with memory allocated by new[].
char* strdup_with_new(const char* the_string);

// Returns a duplicate of up to the first max_length bytes of the_string, with
// memory allocated by new[].
char* strndup_with_new(const char* the_string, size_t max_length);

// Finds, in the_string, the first "word" (consecutive !absl::ascii_isspace()
// characters). Returns pointer to the beginning of the word, and sets *end_ptr
// to the character after the word (which may be space or '\0'); returns nullptr
// (and *end_ptr is undefined) if no next word found.
// end_ptr must not be null.
//
// Both these functions are DEPRECATED(mec).
// Call strings::ScanForFirstWord below.
const char* ScanForFirstWord(const char* the_string, const char** end_ptr);

namespace strings {

// A version with an absl::string_view-based interface.  Returns the first
// "word" (consecutive !absl::ascii_isspace() characters, as above) in the
// input, or an empty absl::string_view otherwise.  When non-empty, the return
// value will alias the array underlying the input.
absl::string_view ScanForFirstWord(absl::string_view input);

// Returns true if s contains at least one ASCII whitespace character.
bool ContainsWhitespace(absl::string_view s);

}  // namespace strings

// For the following functions, an "identifier" is a letter or underscore,
// followed by letters, underscores, or digits.

// Returns true if `str` starts with a valid identifier and advances `str` past
// the identifier.  Otherwise, returns false and leaves `str` unmodified.
bool AdvanceIdentifier(absl::string_view* str);

// Returns a pointer past the end of the "identifier" (see above) beginning at
// str, or nullptr if str doesn't start with an identifier.
ABSL_DEPRECATED("Use AdvanceIdentifier(absl::string_view*) instead")
const char* AdvanceIdentifier(const char* str);

// Returns whether str is an "identifier" (see above).
bool IsIdentifier(absl::string_view str);

// Inserts separator after every interval characters in *s (but never appends to
// the end of the original *s).
void UniformInsertString(std::string* s, ptrdiff_t interval,
                         const char* separator);

// Finds the nth occurrence of c in s; returns the index in s of that
// occurrence, or std::string::npos if fewer than n occurrences.
ptrdiff_t FindNth(absl::string_view s, char c, int n);

// Finds the nth-to-last occurrence of c in s; returns the index in s of that
// occurrence, or -1 if fewer than n occurrences.
ptrdiff_t ReverseFindNth(absl::string_view s, char c, int n);

// Returns true if s is empty or contains only ASCII whitespace characters.
bool OnlyWhitespace(absl::string_view s);

// Formats a string in the same fashion as snprintf(), but returns either the
// number of characters written, or zero if not enough space was available.
// (snprintf() returns the number of characters that would have been written if
// enough space had been available.)
//
// A drop-in replacement for the safe_snprintf() macro.
int SafeSnprintf(char* str, size_t size, const char* format, ...)
    ABSL_PRINTF_ATTRIBUTE(3, 4);

// Reads a line (terminated by delim) from file into *str. Reads delim from
// file, but doesn't copy it into *str. Returns true if read a delim-terminated
// line, or false on end-of-file or error.
bool GetlineFromStdioFile(FILE* file, std::string* str, char delim);

// Writes a two-character representation of 'i' to 'buf'. 'i' must be in the
// range 0 <= i < 100, and buf must have space for two characters. Example:
//   char buf[2];
//   PutTwoDigits(42, buf);
//   // buf[0] == '4'
//   // buf[1] == '2'
inline void PutTwoDigits(size_t i, char* buf) {
  static const char two_ASCII_digits[100][2] = {
      {'0', '0'}, {'0', '1'}, {'0', '2'}, {'0', '3'}, {'0', '4'}, {'0', '5'},
      {'0', '6'}, {'0', '7'}, {'0', '8'}, {'0', '9'}, {'1', '0'}, {'1', '1'},
      {'1', '2'}, {'1', '3'}, {'1', '4'}, {'1', '5'}, {'1', '6'}, {'1', '7'},
      {'1', '8'}, {'1', '9'}, {'2', '0'}, {'2', '1'}, {'2', '2'}, {'2', '3'},
      {'2', '4'}, {'2', '5'}, {'2', '6'}, {'2', '7'}, {'2', '8'}, {'2', '9'},
      {'3', '0'}, {'3', '1'}, {'3', '2'}, {'3', '3'}, {'3', '4'}, {'3', '5'},
      {'3', '6'}, {'3', '7'}, {'3', '8'}, {'3', '9'}, {'4', '0'}, {'4', '1'},
      {'4', '2'}, {'4', '3'}, {'4', '4'}, {'4', '5'}, {'4', '6'}, {'4', '7'},
      {'4', '8'}, {'4', '9'}, {'5', '0'}, {'5', '1'}, {'5', '2'}, {'5', '3'},
      {'5', '4'}, {'5', '5'}, {'5', '6'}, {'5', '7'}, {'5', '8'}, {'5', '9'},
      {'6', '0'}, {'6', '1'}, {'6', '2'}, {'6', '3'}, {'6', '4'}, {'6', '5'},
      {'6', '6'}, {'6', '7'}, {'6', '8'}, {'6', '9'}, {'7', '0'}, {'7', '1'},
      {'7', '2'}, {'7', '3'}, {'7', '4'}, {'7', '5'}, {'7', '6'}, {'7', '7'},
      {'7', '8'}, {'7', '9'}, {'8', '0'}, {'8', '1'}, {'8', '2'}, {'8', '3'},
      {'8', '4'}, {'8', '5'}, {'8', '6'}, {'8', '7'}, {'8', '8'}, {'8', '9'},
      {'9', '0'}, {'9', '1'}, {'9', '2'}, {'9', '3'}, {'9', '4'}, {'9', '5'},
      {'9', '6'}, {'9', '7'}, {'9', '8'}, {'9', '9'}};
  assert(i < 100);
  memcpy(buf, two_ASCII_digits[i], 2);
}

#endif  // THIRD_PARTY_GLOOP_STRINGS_UTIL_H_
