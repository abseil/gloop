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

#ifndef THIRD_PARTY_GLOOP_STRINGS_ESCAPING_H_
#define THIRD_PARTY_GLOOP_STRINGS_ESCAPING_H_

#include <stddef.h>

#include <string>

#include "absl/strings/charset.h"
#include "absl/strings/escaping.h"  // IWYU pragma: keep
#include "absl/strings/string_view.h"

#ifdef SWIG
%include "absl/strings/escaping.h"
#endif

namespace strings {

namespace strings_internal {

// For Unicode code points 0 through 0x10FFFF, EncodeUTF8Char writes
// out the UTF-8 encoding into buffer, and returns the number of chars
// it wrote.
//
// As described in https://datatracker.ietf.org/doc/html/rfc3629#section-3, the
// encodings are:
//    00 -     7F : 0xxxxxxx
//    80 -    7FF : 110xxxxx 10xxxxxx
//   800 -   FFFF : 1110xxxx 10xxxxxx 10xxxxxx
// 10000 - 10FFFF : 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
//
// Values greater than 0x10FFFF are not supported and may or may not write
// characters into buffer, however never will more than kMaxEncodedUTF8Size
// bytes be written, regardless of the value of utf8_char.
enum { kMaxEncodedUTF8Size = 4 };
size_t EncodeUTF8Char(char* buffer, char32_t utf8_char);

}  // namespace strings_internal

// BackslashEscape(), BackslashUnescape(), BackslashUnescapedFind()
//
// Uses backslashes to selectively escape or unescape a set of delimiter
// characters.
//
// SYNOPSIS:
//
//   #include "gloop/strings/escaping.h"
//   #include "absl/strings/charset.h"
//   #include "absl/strings/string_view.h"
//
//   namespace strings {
//     void BackslashEscape(absl::string_view src,
//                          const absl::CharSet& delims,
//                          std::string* dest);
//     void BackslashUnescape(absl::string_view src,
//                            const absl::CharSet& delims,
//                            std::string* dest);
//     size_type BackslashUnescapedFind(
//         absl::string_view src,
//         const absl::CharSet& delims);
//   }
//
// PARAMS:
//
//   absl::string_view src
//      The source to be escaped or unescaped.
//
//   absl::CharSet& delims
//      A absl::CharSet of characters to be considered escaped or
//      unescaped by the operation. There are also overloaded versions of these
//      methods that accept a single character.
//
//   std::string* dest
//      Destination string Appended to by strings::BackslashEscape and
//      strings::BackslashUnescape.
//
// DESCRIPTION:
//
//  void BackslashEscape(absl::string_view src, const absl::CharSet& delims,
//                       std::string* dest);
//
//    Replace any instance of a member 'c' of the const absl::CharSet with
//    { '\\', c }.
//    For example, when exporting maps to /varz, label values need to
//    have all dots escaped.  Appends the result to dest.
//
// void BackslashUnescape(absl::string_view src,
//                        const absl::CharSet& delims,
//                        std::string* dest);
//
//    For all member characters 'c' in the specified 'delims',
//    replace any appearance of the 2-character sequence { '\\', c } in
//    the specified 'src' with just { c }.
//    Does not unescape backslashes unless '\\' is a member of 'delims'.
//    Appends the result to the specified 'dest'.
//
//  size_type BackslashUnescapedFind(
//      absl::string_view src,
//      const absl::CharSet& delims);
//
//    Return the position in src of the first unescaped instance of any of
//    the indicated characters, or absl::string_view::npos if no unescaped
//    members of 'delims' are found.
//
// NOTE:
//
//    These functions do not escape or unescape backslash '\' by default.
//
//    strings::BackslashUnescapedFind always ignores escaped backslashes
//    (this is true whether 'delims' contains backslash or not).
//
//    For all strings, strings::BackslashUnescape is the exact inverse of
//    the strings::BackslashEscape with the matching 'delims' argument.
//    That is, for any absl::string_view 'str' and any const absl::CharSet
//    'delims':
//
//        std::string RoundTrip(absl::string_view src,
//                              const absl::CharSet& delims) {
//          std::string encoded = strings::BackslashEscape(src, delims);
//          return strings::BackslashUnescape(encoded, delims);
//        }
//
//        ASSERT_EQ(src, RoundTrip(str, delims));  // always true
//
//    Note that this is true whether 'delims' contains backslash or not.
//
//    BackslashUnescapedFind can be used to find the end of a string encoded
//    with BackslashEscape, with certain restrictions: for any strings
//    'prefix' and 'suffix', any character set 'delims' that contains
//    backslash, and any delimiter 'd' from 'delims' other than backslash:
//
//        std::string encoded =
//            absl::StrCat(strings::BackslashEscape(prefix, delims), d, suffix);
//        size_t end = strings::BackslashUnescapedFind(encoded, delims);
//        std::string decoded =
//            strings::BackslashUnescape(encoded.substr(0,end), delims);
//
//        ASSERT_EQ(prefix, decoded);
//
// EXAMPLES:
//
//    Example 1:
//      Join arbitrary string fields with ':'.
//      Any ':' and '\\' occurring in any of the fields will be escaped.
//      Backslashes have to be escaped to prevent backslashes in the input from
//      changing the output.
//
//    std::vector<std::string> fields = ...;
//    constexpr absl::CharSet kDelims = absl::CharSet(":\\");
//    std::string encoded;
//    const char* sep = "";
//    for (const auto& field : fields) {
//      StrAppend(&encoded, sep);
//      strings::BackslashEscape(field, kDelims, &encoded);
//      sep = ":";
//    }
//
//    Example 2:
//      Find the field boundaries in such an encoded string.
//
//      absl::string_view encoded_sp = ...;  // from example 1
//      constexpr absl::CharSet kDelims = absl::CharSet(":\\");
//      std::vector<absl::string_view> fields;
//      while (!encoded_sp.empty()) {
//        size_type pos = strings::BackslashUnescapedFind(encoded_sp, kDelims);
//        if (pos == absl::string_view::npos) {
//          pos = encoded_sp.size();
//        }
//        fields.push_back(encoded_sp.substr(0, pos));
//        if (pos < encoded_sp.size()) {
//          ++pos;
//        }
//        encoded_sp.remove_prefix(pos);
//      }
//
//    Example 3:
//      Unescape the fields identified in Example 2.
//
//      std::vector<absl::string_view> encoded_fields = ...;  // from example 2
//      constexpr absl::CharSet kDelims = absl::CharSet(":\\");
//      std::vector<std::string> decoded_fields;
//      for (absl::string_view enc : encoded_fields) {
//         std::string f;
//         strings::BackslashUnescape(enc, kDelims, &f);
//         decoded_fields.push_back(f);
//      }
//
void BackslashEscape(absl::string_view src, unsigned char delim,
                     std::string* dest);
void BackslashEscape(absl::string_view src, const absl::CharSet& delims,
                     std::string* dest);

void BackslashUnescape(absl::string_view src, unsigned char delim,
                       std::string* dest);
void BackslashUnescape(absl::string_view src, const absl::CharSet& delims,
                       std::string* dest);

absl::string_view::size_type BackslashUnescapedFind(absl::string_view src,
                                                    unsigned char delim);
absl::string_view::size_type BackslashUnescapedFind(
    absl::string_view src, const absl::CharSet& delims);

// Convenience overloads of BackslashEscape() that return a string value.
inline std::string BackslashEscape(absl::string_view src, unsigned char delim) {
  std::string s;
  BackslashEscape(src, delim, &s);
  return s;
}
inline std::string BackslashEscape(absl::string_view src,
                                   const absl::CharSet& delims) {
  std::string s;
  BackslashEscape(src, delims, &s);
  return s;
}

// Convenience overloads of BackslashUnescape() that return a string value.
inline std::string BackslashUnescape(absl::string_view src,
                                     unsigned char delim) {
  std::string s;
  BackslashUnescape(src, delim, &s);
  return s;
}
inline std::string BackslashUnescape(absl::string_view src,
                                     const absl::CharSet& delims) {
  std::string s;
  BackslashUnescape(src, delims, &s);
  return s;
}

// ----------------------------------------------------------------------
// LegacyBase64EscapeWithoutPadding()
//    Base64-encodes `src` and writes the result to `dest`. Does not pad `dest`.
//
//    This is not a provided overload of absl::Base64Escape because it does not
//    conform to an RFC specification. This function meets the needs of the
//    handful of callers that want base64-encoding without padding. See
//    b/114449174 for more context.
// ----------------------------------------------------------------------
void LegacyBase64EscapeWithoutPadding(absl::string_view src, std::string* dest);

// ----------------------------------------------------------------------
// UnescapeCEscapeSequences()
//    Copies "source" to "dest", rewriting C-style escape sequences
//    -- '\n', '\r', '\\', '\ooo', etc -- to their ASCII
//    equivalents.  "dest" must be sufficiently large to hold all
//    the characters in the rewritten string (i.e. at least as large
//    as strlen(source) + 1 should be safe, since the replacements
//    are always shorter than the original escaped sequences).  It's
//    safe for source and dest to be the same.  RETURNS the length
//    of dest.
//
//    It allows hex sequences \xhh, or generally \xhhhhh with an
//    arbitrary number of hex digits, but all of them together must
//    specify a value of a single byte (e.g. \x0045 is equivalent
//    to \x45, and \x1234 is erroneous). If the value is too large,
//    it is truncated to 8 bits and an error is set. This is also
//    true of octal values that exceed 0xff.
//
//    It also allows escape sequences of the form \uhhhh (exactly four
//    hex digits, upper or lower case) or \Uhhhhhhhh (exactly eight
//    hex digits, upper or lower case) to specify a Unicode code
//    point. The dest array will contain the UTF8-encoded version of
//    that code-point (e.g., if source contains \u2019, then dest will
//    contain the three bytes 0xE2, 0x80, and 0x99). For the inverse
//    transformation, use UniLib::UTF8EscapeString
//    (util/utf8/public/unilib.h), not CEscapeString.
//
//    Errors are reported with LOG(ERROR).  The effect on the dest array is not
//    defined if an error occurs, but rest of the source will be processed.
//
//    *** DEPRECATED: Use absl::CUnescape() in new code ***
//    ----------------------------------------------------------------------
[[deprecated("Use absl::CUnescape()")]]
ptrdiff_t UnescapeCEscapeSequences(const char* source, char* dest);

// ----------------------------------------------------------------------
// UnescapeCEscapeString()
//    This does the same thing as UnescapeCEscapeSequences, but creates
//    a new string. The caller does not need to worry about allocating
//    a dest buffer. This should be used for non performance critical
//    tasks such as printing debug messages. It is safe for src and dest
//    to be the same.
//
//    Errors are reported with LOG(ERROR).
//
//    In the first call, the length of dest is returned. In the second call, the
//    new string is returned.
//
//    *** DEPRECATED: Use absl::CUnescape() in new code ***
// ----------------------------------------------------------------------
[[deprecated("Use absl::CUnescape()")]]
ptrdiff_t UnescapeCEscapeString(const std::string& src, std::string* dest);
[[deprecated("Use absl::CUnescape()")]]
std::string UnescapeCEscapeString(const std::string& src);

}  // namespace strings

#endif  // THIRD_PARTY_GLOOP_STRINGS_ESCAPING_H_
