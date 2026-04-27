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

#include "gloop/strings/escaping.h"

#include <cstddef>
#include <iterator>
#include <string>

#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "absl/strings/charset.h"
#include "absl/strings/escaping.h"
#include "absl/strings/resize_and_overwrite.h"
#include "absl/strings/string_view.h"

namespace strings {

namespace {

// ----------------------------------------------------------------------
// BackslashEscape, BackslashUnescape, and BackslashUnescapedFind
// ----------------------------------------------------------------------

template <typename Functor>
void BackslashEscape(absl::string_view src, Functor&& delimiter_check,
                     std::string* absl_nonnull dest) {
  typedef absl::string_view::const_iterator Iter;
  Iter first = src.begin();
  Iter last = src.end();
  while (first != last) {
    // Advance to next character we need to escape, or to end of source
    Iter next = first;
    while (next != last && !delimiter_check(*next)) {
      ++next;
    }
    // Append the whole run of non-escaped chars
    dest->append(first, next);
    if (next != last) {
      // Char at *next needs to be escaped.
      char c[2] = {'\\', *next++};
      dest->append(c, c + ABSL_ARRAYSIZE(c));
    }
    first = next;
  }
}

template <typename Functor>
void BackslashUnescape(absl::string_view src, Functor&& delimiter_check,
                       std::string* absl_nonnull dest) {
  typedef absl::string_view::const_iterator Iter;
  Iter first = src.begin();
  Iter last = src.end();
  bool escaped = false;
  for (; first != last; ++first) {
    if (escaped) {
      if (delimiter_check(*first)) {
        dest->push_back(*first);
        escaped = false;
      } else {
        dest->push_back('\\');
        if (*first == '\\') {
          escaped = true;
        } else {
          escaped = false;
          dest->push_back(*first);
        }
      }
    } else {
      if (*first == '\\') {
        escaped = true;
      } else {
        dest->push_back(*first);
      }
    }
  }
  if (escaped) {
    dest->push_back('\\');  // trailing backslash
  }
}

template <typename Functor>
static inline absl::string_view::const_iterator BackslashUnescapedFindIter(
    absl::string_view::const_iterator first,
    absl::string_view::const_iterator last, Functor&& delimiter_check) {
  bool escaped = false;
  absl::string_view::const_iterator slash_pos = {};  // valid only if escaped
  for (; first != last; ++first) {
    if (escaped) {
      if (delimiter_check('\\')) {
        if (*first == '\\') {
          continue;
        }
        if ((std::distance(slash_pos, first) & 1) == 0) {
          escaped = false;
          if (delimiter_check(*first)) {
            return first;
          }
          continue;
        }
        // odd distance to 'first': an unescaped '\' is at (first-1).
        if (delimiter_check(*first)) {
          continue;
        }
        return first - 1;
      } else {
        escaped = false;
        continue;
      }
    }
    if (*first == '\\') {
      escaped = true;
      slash_pos = first;
      continue;
    }
    if (delimiter_check(*first)) {
      return first;
    }
  }
  if (escaped && delimiter_check('\\')) {
    if ((std::distance(slash_pos, first) & 1) == 1) {
      return first - 1;
    }
  }
  return first;
}

template <typename Functor>
absl::string_view::size_type BackslashUnescapedFind(absl::string_view src,
                                                    Functor&& delimiter_check) {
  absl::string_view::const_iterator pos =
      BackslashUnescapedFindIter(src.begin(), src.end(), delimiter_check);
  if (pos == src.end()) return absl::string_view::npos;
  return std::distance(src.begin(), pos);
}

}  // namespace

void BackslashEscape(absl::string_view src, unsigned char delim,
                     std::string* absl_nonnull dest) {
  BackslashEscape(src, [delim](unsigned char c) { return c == delim; }, dest);
}
void BackslashEscape(absl::string_view src, const absl::CharSet& delims,
                     std::string* absl_nonnull dest) {
  BackslashEscape(
      src, [&delims](unsigned char c) { return delims.contains(c); }, dest);
}

void BackslashUnescape(absl::string_view src, unsigned char delim,
                       std::string* absl_nonnull dest) {
  BackslashUnescape(src, [delim](unsigned char c) { return c == delim; }, dest);
}
void BackslashUnescape(absl::string_view src, const absl::CharSet& delims,
                       std::string* absl_nonnull dest) {
  BackslashUnescape(
      src, [&delims](unsigned char c) { return delims.contains(c); }, dest);
}

absl::string_view::size_type BackslashUnescapedFind(absl::string_view src,
                                                    unsigned char delim) {
  return BackslashUnescapedFind(
      src, [delim](unsigned char c) { return c == delim; });
}
absl::string_view::size_type BackslashUnescapedFind(
    absl::string_view src, const absl::CharSet& delims) {
  return BackslashUnescapedFind(
      src, [&delims](unsigned char c) { return delims.contains(c); });
}

ptrdiff_t EscapeStrForCSV(const char* absl_nonnull src, char* absl_nonnull dest,
                          ptrdiff_t dest_len) {
  ptrdiff_t used = 0;

  while (true) {
    if (*src == '\0' && used < dest_len) {
      dest[used] = '\0';
      return used;
    }

    if (used + 1 >= dest_len)  // +1 because we might require two characters
      return -1;

    if (*src == '"') dest[used++] = '"';

    dest[used++] = *src++;
  }
}

static unsigned int HexDigitToInt(char c) {
  static_assert('0' == 0x30 && 'A' == 0x41 && 'a' == 0x61,
                "Character set must be ASCII.");
  assert(absl::ascii_isxdigit(static_cast<unsigned char>(c)));
  unsigned int x = static_cast<unsigned char>(c);
  if (x > '9') {
    x += 9;
  }
  return x & 0xf;
}

static inline bool IsSurrogate(char32_t c, absl::string_view src) {
  if (c >= 0xD800 && c <= 0xDFFF) {
    LOG(ERROR) << "surrogate character (0xD800-DFFF): \\" << src;
    return true;
  }
  return false;
}

// ----------------------------------------------------------------------
//    NOTE: any changes to this function must also be reflected in the newer
//    CUnescape().
// ----------------------------------------------------------------------

#define IS_OCTAL_DIGIT(c) (((c) >= '0') && ((c) <= '7'))

ptrdiff_t UnescapeCEscapeSequences(const char* absl_nonnull source,
                                   char* absl_nonnull dest) {
  char* d = dest;

  const char* p = source;

  // Small optimization for case where source = dest and there's no escaping
  while (p == d && *p != '\0' && *p != '\\') p++, d++;

  while (*p != '\0') {
    if (*p != '\\') {
      *d++ = *p++;
    } else {
      switch (*++p) {  // skip past the '\\'
        case '\0':
          LOG(ERROR) << "String cannot end with \\: " << source;
          *d = '\0';
          return d - dest;  // we're done with p
        case 'a':
          *d++ = '\a';
          break;
        case 'b':
          *d++ = '\b';
          break;
        case 'f':
          *d++ = '\f';
          break;
        case 'n':
          *d++ = '\n';
          break;
        case 'r':
          *d++ = '\r';
          break;
        case 't':
          *d++ = '\t';
          break;
        case 'v':
          *d++ = '\v';
          break;
        case '\\':
          *d++ = '\\';
          break;
        case '?':
          *d++ = '\?';
          break;  // \?  Who knew?
        case '\'':
          *d++ = '\'';
          break;
        case '"':
          *d++ = '\"';
          break;
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7': {
          // octal digit: 1 to 3 digits
          const char* octal_start = p;
          unsigned int ch = *p - '0';
          if (IS_OCTAL_DIGIT(p[1])) ch = ch * 8 + *++p - '0';
          if (IS_OCTAL_DIGIT(p[1]))    // safe (and easy) to do this twice
            ch = ch * 8 + *++p - '0';  // now points at last digit
          if (ch > 0xFF) {
            LOG(ERROR) << "Value of \\"
                       << absl::string_view(octal_start, p + 1 - octal_start)
                       << " exceeds 8 bits";
            break;
          }
          *d++ = ch;
          break;
        }
        case 'x':
        case 'X': {
          if (!absl::ascii_isxdigit(p[1])) {
            if (p[1] == '\0') {
              LOG(ERROR) << "String cannot end with \\x";
            } else {
              LOG(ERROR) << "\\x cannot be followed by a non-hex digit: \\"
                         << *p << p[1];
            }
            break;
          }
          unsigned int ch = 0;
          const char* hex_start = p;
          while (absl::ascii_isxdigit(p[1]))  // arbitrarily many hex digits
            ch = (ch << 4) + HexDigitToInt(*++p);
          if (ch > 0xFF) {
            LOG(ERROR) << "Value of \\"
                       << absl::string_view(hex_start, p + 1 - hex_start)
                       << " exceeds 8 bits";
            break;
          }
          *d++ = ch;
          break;
        }
        case 'u': {
          // \uhhhh => convert 4 hex digits to UTF-8
          char32_t rune = 0;
          const char* hex_start = p;
          bool error_hit = false;
          for (int i = 0; i < 4; ++i) {
            if (absl::ascii_isxdigit(p[1])) {            // Look one char ahead.
              rune = (rune << 4) + HexDigitToInt(*++p);  // Advance p.
            } else {
              LOG(ERROR) << "\\u must be followed by 4 hex digits: \\"
                         << absl::string_view(hex_start, p + 1 - hex_start);
              error_hit = true;
              break;
            }
          }
          if (error_hit ||
              IsSurrogate(rune,
                          absl::string_view(hex_start, p + 1 - hex_start))) {
            break;
          }
          d += strings_internal::EncodeUTF8Char(d, rune);
          break;
        }
        case 'U': {
          // \Uhhhhhhhh => convert 8 hex digits to UTF-8
          char32_t rune = 0;
          const char* hex_start = p;
          bool error_hit = false;
          for (int i = 0; i < 8; ++i) {
            if (absl::ascii_isxdigit(p[1])) {  // Look one char ahead.
              // Don't change rune until we're sure this
              // is within the Unicode limit, but do advance p.
              char32_t newrune = (rune << 4) + HexDigitToInt(*++p);
              if (newrune > 0x10FFFF) {
                LOG(ERROR) << "Value of \\"
                           << absl::string_view(hex_start, p + 1 - hex_start)
                           << " exceeds Unicode limit (0x10FFFF)";
                error_hit = true;
                break;
              }
              rune = newrune;
            } else {
              LOG(ERROR) << "\\U must be followed by 8 hex digits: \\"
                         << absl::string_view(hex_start, p + 1 - hex_start);
              error_hit = true;
              break;
            }
          }
          if (error_hit ||
              IsSurrogate(rune,
                          absl::string_view(hex_start, p + 1 - hex_start))) {
            break;
          }
          d += strings_internal::EncodeUTF8Char(d, rune);
          break;
        }
        default:
          LOG(ERROR) << "Unknown escape sequence: \\" << *p;
      }
      p++;  // read past letter we escaped
    }
  }
  *d = '\0';
  return d - dest;
}

ptrdiff_t UnescapeCEscapeString(const std::string& src,
                                std::string* absl_nonnull dest) {
  CHECK(dest);
  dest->resize(src.size() + 1);
  ptrdiff_t len = UnescapeCEscapeSequences(src.c_str(), &(*dest)[0]);
  dest->erase(len);
  return len;
}

std::string UnescapeCEscapeString(const std::string& src) {
  std::string unescaped(src.size() + 1, '\0');

  ptrdiff_t len = UnescapeCEscapeSequences(src.c_str(), &unescaped[0]);
  unescaped.erase(len);
  return unescaped;
}

void LegacyBase64EscapeWithoutPadding(absl::string_view src,
                                      std::string* absl_nonnull dest) {
  *dest = absl::Base64Escape(src);
  // Removes at most 2 '=' padding characters.
  while (!dest->empty() && dest->back() == '=') {
    dest->pop_back();
  }
}

namespace strings_internal {

size_t EncodeUTF8Char(char* absl_nonnull buffer, char32_t utf8_char) {
  if (utf8_char <= 0x7F) {
    *buffer = static_cast<char>(utf8_char);
    return 1;
  } else if (utf8_char <= 0x7FF) {
    buffer[1] = 0x80 | (utf8_char & 0x3F);
    utf8_char >>= 6;
    buffer[0] = 0xC0 | utf8_char;
    return 2;
  } else if (utf8_char <= 0xFFFF) {
    buffer[2] = 0x80 | (utf8_char & 0x3F);
    utf8_char >>= 6;
    buffer[1] = 0x80 | (utf8_char & 0x3F);
    utf8_char >>= 6;
    buffer[0] = 0xE0 | utf8_char;
    return 3;
  } else {
    buffer[3] = 0x80 | (utf8_char & 0x3F);
    utf8_char >>= 6;
    buffer[2] = 0x80 | (utf8_char & 0x3F);
    utf8_char >>= 6;
    buffer[1] = 0x80 | (utf8_char & 0x3F);
    utf8_char >>= 6;
    buffer[0] = 0xF0 | utf8_char;
    return 4;
  }
}

}  // namespace strings_internal

// Helper to implement the very similar logic shared by
// QuotedPrintableUnescape() and QEncodingUnescape().
enum class QUnescapeMode { kRfc2045, kRfc2047 };

static std::string QUnescapeImpl(const QUnescapeMode mode,
                                 absl::string_view src) {
  std::string dest;
  absl::StringResizeAndOverwrite(
      dest, src.size(), [mode, src](char* absl_nonnull buf, size_t) {
        size_t in = 0;
        size_t out = 0;
        while (in < src.size()) {
          if (src[in] == '=') {
            if (in + 1 < src.size()) {
              // QuotedPrintableUnescape() allows soft line breaks with just
              // '\n'. Both modes allow "\r\n" line breaks.
              if (src[in + 1] == '\n' && mode == QUnescapeMode::kRfc2045) {
                ++in;
              } else if (in + 2 < src.size()) {
                if (absl::ascii_isxdigit(src[in + 1]) &&
                    absl::ascii_isxdigit(src[in + 2])) {
                  buf[out++] = HexDigitToInt(src[in + 1]) * 16 +
                               HexDigitToInt(src[in + 2]);
                  in += 2;
                } else if (src[in + 1] == '\r' && src[in + 2] == '\n') {
                  in += 2;
                }
              }
            }
            ++in;
          } else if (src[in] == '_' && mode == QUnescapeMode::kRfc2047) {
            // According to RFC 2047, '_' is converted to space.
            buf[out++] = ' ';
            ++in;
          } else {
            buf[out++] = src[in++];
          }
        }
        return out;
      });
  return dest;
}

std::string QuotedPrintableUnescape(absl::string_view src) {
  return QUnescapeImpl(QUnescapeMode::kRfc2045, src);
}

std::string QEncodingUnescape(absl::string_view src) {
  return QUnescapeImpl(QUnescapeMode::kRfc2047, src);
}

}  // namespace strings
