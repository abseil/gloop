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
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "absl/strings/charset.h"
#include "absl/strings/escaping.h"
#include "absl/strings/internal/escaping.h"
#include "absl/strings/string_view.h"

namespace strings {

namespace {

// ----------------------------------------------------------------------
// BackslashEscape, BackslashUnescape, and BackslashUnescapedFind
// ----------------------------------------------------------------------

template <typename Functor>
void BackslashEscape(absl::string_view src, Functor&& delimiter_check,
                     std::string* dest) {
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
                       std::string* dest) {
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
                     std::string* dest) {
  BackslashEscape(src, [delim](unsigned char c) { return c == delim; }, dest);
}
void BackslashEscape(absl::string_view src, const absl::CharSet& delims,
                     std::string* dest) {
  BackslashEscape(
      src, [&delims](unsigned char c) { return delims.contains(c); }, dest);
}

void BackslashUnescape(absl::string_view src, unsigned char delim,
                       std::string* dest) {
  BackslashUnescape(src, [delim](unsigned char c) { return c == delim; }, dest);
}
void BackslashUnescape(absl::string_view src, const absl::CharSet& delims,
                       std::string* dest) {
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

void LegacyBase64EscapeWithoutPadding(absl::string_view src,
                                      std::string* dest) {
  absl::strings_internal::Base64EscapeInternal(
      reinterpret_cast<const unsigned char*>(src.data()), src.size(), dest,
      /*do_padding=*/false, absl::strings_internal::kBase64Chars);
}

namespace strings_internal {

size_t EncodeUTF8Char(char* buffer, char32_t utf8_char) {
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

}  // namespace strings
