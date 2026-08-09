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

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>

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
      dest->append(c, c + std::size(c));
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

static ptrdiff_t EscapeStrForCSV(absl::string_view src,
                                 char* dest_not_nul_terminated,
                                 ptrdiff_t dest_len) {
  if (dest_len < 0) {
    return -1;
  }

  ptrdiff_t used = 0;

  size_t i = 0;

  while (i < src.size()) {
    const char ch = src[i++];
    const bool repeats = ch == '"';

    if (used + static_cast<int>(repeats) >= dest_len) {
      return -1;
    }

    dest_not_nul_terminated[used++] = ch;

    if (repeats) {
      dest_not_nul_terminated[used++] = ch;
    }
  }

  return used;
}

std::string QuoteStrForCSV(absl::string_view src) {
  std::string result;
  constexpr const char need_quotes[] = {'"', ',', '\0', '\r', '\n'};
  const bool needs_quote =
      src.find_first_of(absl::string_view(
          need_quotes, std::size(need_quotes))) != absl::string_view::npos;
  absl::StringResizeAndOverwrite(
      result, src.size() * 2 + 2,
      [src, needs_quote](char* buf, ptrdiff_t buf_size) {
        ptrdiff_t used = 0;
        if (needs_quote) {
          buf[used++] = '"';
        }
        used += EscapeStrForCSV(src, buf + used, buf_size - used);
        if (needs_quote) {
          buf[used++] = '"';
        }
        return used;
      });
  return result;
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

ptrdiff_t UnescapeCEscapeString(absl::string_view src,
                                std::string* absl_nonnull dest) {
  std::string error;
  if (!absl::CUnescape(src, dest, &error)) {
    // This function is documented to report errors via LOG(ERROR).
    LOG(ERROR) << error;
  }
  return static_cast<ptrdiff_t>(dest->size());
}

std::string UnescapeCEscapeString(absl::string_view src) {
  std::string unescaped;
  std::string error;
  if (!absl::CUnescape(src, &unescaped, &error)) {
    // This function is documented to report errors via LOG(ERROR).
    LOG(ERROR) << error;
  }
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

namespace {
// Mapping from number of Base32 escaped characters (0 through 8) to number of
// unescaped bytes.  8 Base32 escaped characters represent 5 unescaped bytes.
// For N < 8, then number of unescaped bytes is less than 5.  Note that in
// valid input, N can only be 0, 2, 4, 5, 7, or 8 (corresponding to 0, 1, 2,
// 3, 4, or 5 unescaped bytes).
//
// We use 5 for invalid values of N to be safe, since this is used to compute
// the length of the buffer to hold unescaped data.
//
// See https://datatracker.ietf.org/doc/html/rfc4648#section-6 for details.
constexpr std::array<int, 9> kBase32NumUnescapedBytes = {0, 5, 1, 5, 2,
                                                         3, 5, 4, 5};

constexpr unsigned char kIllegalBase32CharSentinel = 99;

// Standard Base32 uses the letters A-Z followed by the numbers 2–7.
constexpr std::array<unsigned char, 256> kBase32InverseAlphabet = []() {
  std::array<unsigned char, 256> a{};
  for (int c = 0; c < 256; ++c) {
    if (c >= 'A' && c <= 'Z') {
      a[c] = c - 'A';
    } else if (c >= 'a' && c <= 'z') {
      a[c] = c - 'a';
    } else if (c >= '2' && c <= '7') {
      a[c] = 26 + c - '2';
    } else {
      a[c] = kIllegalBase32CharSentinel;
    }
  }
  return a;
}();

// Base32Hex uses the numbers 0-9 followed by the letters A–V.
constexpr std::array<unsigned char, 256> kBase32HexInverseAlphabet = []() {
  std::array<unsigned char, 256> a{};
  for (int c = 0; c < 256; ++c) {
    if (c >= '0' && c <= '9') {
      a[c] = c - '0';
    } else if (c >= 'A' && c <= 'V') {
      a[c] = 10 + c - 'A';
    } else if (c >= 'a' && c <= 'v') {
      a[c] = 10 + c - 'a';
    } else {
      a[c] = kIllegalBase32CharSentinel;
    }
  }
  return a;
}();

ptrdiff_t GeneralBase32Unescape(
    absl::string_view src, char* absl_nonnull dest, size_t szdest,
    const std::array<unsigned char, 256>& inverse_alphabet) {
  uint32_t buffer = 0;
  int bits_left = 0;
  bool padding_started = false;
  size_t non_padding_chars = 0;
  size_t out = 0;

  for (char c : src) {
    // Must be handled first since the inverse_alphabet arrays mark this
    // character as illegal.
    if (c == '=') {
      padding_started = true;
      continue;
    }

    // Non-padding character after padding started.
    if (padding_started) {
      return -1;
    }

    unsigned char value = inverse_alphabet[static_cast<unsigned char>(c)];

    if (value == kIllegalBase32CharSentinel) {
      return -1;
    }

    // `buffer` acts as a shift register. We only need to hold up to 12 bits at
    // a time, so shifting already-processed bits off is safe and expected.
    buffer = (buffer << 5) | value;
    bits_left += 5;
    ++non_padding_chars;

    // bits_left is the number of accumulated bits in the shift register.
    // We can use them once we have accumulated a full byte.
    if (bits_left >= 8) {
      bits_left -= 8;
      if (out >= szdest) {
        return -1;  // Out of space.
      }
      // Take the full byte out of the shift register by shifting off
      // any extra bits that are part of the next incomplete byte.
      dest[out++] = static_cast<char>((buffer >> bits_left) & 0xff);
    }
  }

  size_t remainder = non_padding_chars % 8;
  if (padding_started) {
    // https://datatracker.ietf.org/doc/html/rfc4648#section-6.
    // Padded input: must fill the last block exactly.
    // Valid data character lengths per 8-character block are 2, 4, 5, 7, or 8.
    // 8 can be ignored because this is a remainder.
    constexpr uint32_t kValidRemainderMaskPadded =
        (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7);
    if (((1 << remainder) & kValidRemainderMaskPadded) == 0) {
      return -1;
    }
    if (src.size() != ((non_padding_chars / 8) + 1) * 8) {
      return -1;
    }
  } else {
    // https://datatracker.ietf.org/doc/html/rfc4648#section-6.
    // Unpadded input: length must correspond to a valid number of data chars.
    constexpr uint32_t kValidRemainderMaskUnpadded =
        (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7);
    if (((1 << remainder) & kValidRemainderMaskUnpadded) == 0) {
      return -1;
    }
  }

  // Remaining accumulated bits must be zero. This enforces strict RFC 4648
  // compliance by rejecting non-zero padding bits.
  if (bits_left > 0) {
    uint32_t remaining_mask = (1 << bits_left) - 1;
    if ((buffer & remaining_mask) != 0) {
      return -1;
    }
  }

  return static_cast<ptrdiff_t>(out);
}

bool GeneralBase32Unescape(
    absl::string_view src, std::string* absl_nonnull dest,
    const std::array<unsigned char, 256>& inverse_alphabet) {
  // Determine the size of the output string.
  const ptrdiff_t dest_len =
      5 * (src.size() / 8) + kBase32NumUnescapedBytes[src.size() % 8];

  bool success = true;
  absl::StringResizeAndOverwrite(
      *dest, dest_len,
      [src, inverse_alphabet, &success](char* buf, size_t buf_size) {
        const ptrdiff_t len =
            GeneralBase32Unescape(src, buf, buf_size, inverse_alphabet);
        if (len < 0) {
          success = false;
          return size_t{0};
        }
        return static_cast<size_t>(len);
      });
  return success;
}

}  // namespace

ptrdiff_t Base32Unescape(const char* absl_nullable src, ptrdiff_t slen,
                         char* absl_nonnull dest, ptrdiff_t szdest) {
  return GeneralBase32Unescape(absl::string_view(src, slen), dest, szdest,
                               kBase32InverseAlphabet);
}

bool Base32Unescape(absl::string_view src, std::string* absl_nonnull dest) {
  return GeneralBase32Unescape(src, dest, kBase32InverseAlphabet);
}

bool Base32HexUnescape(absl::string_view src, std::string* absl_nonnull dest) {
  return GeneralBase32Unescape(src, dest, kBase32HexInverseAlphabet);
}

static void GeneralFiveBytesToEightBase32Digits(
    const unsigned char* absl_nonnull in_bytes, char* absl_nonnull out,
    const std::array<char, 32>& alphabet) {
  // It's easier to just hard code this.
  // The conversion is based on the following picture of the division of a
  // 40-bit block into 8 5-byte words:
  //
  //       5   3  2  5  1  4   4 1  5  2  3   5
  //     |:::::::|:::::::|:::::::|:::::::|:::::::
  //     +----+----+----+----+----+----+----+----
  //
  out[0] = alphabet[in_bytes[0] >> 3];
  out[1] = alphabet[(in_bytes[0] & 0x07) << 2 | in_bytes[1] >> 6];
  out[2] = alphabet[(in_bytes[1] & 0x3E) >> 1];
  out[3] = alphabet[(in_bytes[1] & 0x01) << 4 | in_bytes[2] >> 4];
  out[4] = alphabet[(in_bytes[2] & 0x0F) << 1 | in_bytes[3] >> 7];
  out[5] = alphabet[(in_bytes[3] & 0x7C) >> 2];
  out[6] = alphabet[(in_bytes[3] & 0x03) << 3 | in_bytes[4] >> 5];
  out[7] = alphabet[(in_bytes[4] & 0x1F)];
}

static ptrdiff_t GeneralBase32Escape(const unsigned char* absl_nullable src,
                                     size_t szsrc, char* absl_nonnull dest,
                                     size_t szdest,
                                     const std::array<char, 32>& alphabet) {
  static const char kPad32 = '=';

  if (szsrc == 0) return 0;

  char* cur_dest = dest;
  const unsigned char* cur_src = src;

  // Five bytes of data encodes to eight characters of cyphertext.
  // So we can pump through three-byte chunks atomically.
  while (szsrc > 4) {  // keep going until we have less than 40 bits
    if (szdest < 8) return 0;
    szdest -= 8;

    GeneralFiveBytesToEightBase32Digits(cur_src, cur_dest, alphabet);

    cur_dest += 8;
    cur_src += 5;
    szsrc -= 5;
  }

  // Now deal with the tail (<=4 bytes).
  if (szsrc > 0) {
    if (szdest < 8) return 0;
    szdest -= 8;
    unsigned char last_chunk[5];
    memcpy(last_chunk, cur_src, szsrc);

    for (size_t i = szsrc; i < 5; ++i) {
      last_chunk[i] = '\0';
    }

    GeneralFiveBytesToEightBase32Digits(last_chunk, cur_dest, alphabet);
    ptrdiff_t filled = (szsrc * 8) / 5 + 1;
    cur_dest += filled;

    // Add on the padding.
    for (int i = 0; i < (8 - filled); ++i) {
      *(cur_dest++) = kPad32;
    }
  }

  return cur_dest - dest;
}

static bool GeneralBase32Escape(absl::string_view src,
                                std::string* absl_nonnull dest,
                                const std::array<char, 32>& alphabet) {
  const ptrdiff_t max_escaped_size = CalculateBase32EscapedLen(src.length());
  absl::StringResizeAndOverwrite(
      *dest, max_escaped_size + 1, [src, alphabet](char* buf, size_t buf_size) {
        const ptrdiff_t escaped_len = GeneralBase32Escape(
            reinterpret_cast<const unsigned char*>(src.data()), src.length(),
            buf, buf_size, alphabet);
        return static_cast<size_t>(escaped_len);
      });
  // The pointer version of GeneralBase32Escape() returns 0 on error
  // when szsrc > 0.
  return !dest->empty() || src.empty();
}

static constexpr std::array<char, 32> kBase32Alphabet = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K',
    'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V',
    'W', 'X', 'Y', 'Z', '2', '3', '4', '5', '6', '7'};

ptrdiff_t Base32Escape(const unsigned char* absl_nullable src, size_t szsrc,
                       char* absl_nonnull dest, size_t szdest) {
  return GeneralBase32Escape(src, szsrc, dest, szdest, kBase32Alphabet);
}

bool Base32Escape(absl::string_view src, std::string* absl_nonnull dest) {
  return GeneralBase32Escape(src, dest, kBase32Alphabet);
}

void FiveBytesToEightBase32Digits(const unsigned char* absl_nonnull in_bytes,
                                  char* absl_nonnull out) {
  GeneralFiveBytesToEightBase32Digits(in_bytes, out, kBase32Alphabet);
}

static constexpr std::array<char, 32> Base32HexAlphabet = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A',
    'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
    'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V',
};

ptrdiff_t Base32HexEscape(const unsigned char* absl_nullable src, size_t szsrc,
                          char* absl_nonnull dest, size_t szdest) {
  return GeneralBase32Escape(src, szsrc, dest, szdest, Base32HexAlphabet);
}

bool Base32HexEscape(absl::string_view src, std::string* absl_nonnull dest) {
  return GeneralBase32Escape(src, dest, Base32HexAlphabet);
}

ptrdiff_t CalculateBase32EscapedLen(size_t input_len) {
  CHECK_LE(input_len, std::numeric_limits<size_t>::max() / 8 - 4);
  size_t intermediate_result = 8 * input_len + 4;
  size_t len = intermediate_result / 5;
  len = (len + 7) & ~7;
  return len;
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

// ----------------------------------------------------------------------
// CleanStringLineEndings()
//   Clean up a multi-line string to conform to Unix line endings.
//   Reads from src and appends to dst, so usually dst should be empty.
//
//   If there is no line ending at the end of a non-empty string, it can
//   be added automatically.
//
//   Four different types of input are correctly handled:
//
//     - Unix/Linux files: line ending is LF: pass through unchanged
//
//     - DOS/Windows files: line ending is CRLF: convert to LF
//
//     - Legacy Mac files: line ending is CR: convert to LF
//
//     - Garbled files: random line endings: convert gracefully
//                      lonely CR, lonely LF, CRLF: convert to LF
//
//   @param src The multi-line string to convert
//   @param dst The converted string is appended to this string
//   @param auto_end_last_line Automatically terminate the last line
//
//   Limitations:
//
//     This does not do the right thing for CRCRLF files created by
//     broken programs that do another Unix->DOS conversion on files
//     that are already in CRLF format.  For this, a two-pass approach
//     brute-force would be needed that
//
//       (1) determines the presence of LF (first one is ok)
//       (2) if yes, removes any CR, else convert every CR to LF
// ----------------------------------------------------------------------
void CleanStringLineEndings(absl::string_view src, std::string* dst,
                            bool auto_end_last_line) {
  if (dst->empty()) {
    dst->append(src);
    CleanStringLineEndings(dst, auto_end_last_line);
  } else {
    std::string tmp(src);
    CleanStringLineEndings(&tmp, auto_end_last_line);
    dst->append(tmp);
  }
}

void CleanStringLineEndings(std::string* str, bool auto_end_last_line) {
  ptrdiff_t output_pos = 0;
  bool r_seen = false;
  ptrdiff_t len = str->size();

  char* p = &(*str)[0];

  for (ptrdiff_t input_pos = 0; input_pos < len;) {
    if (!r_seen && input_pos + 8 < len) {
      uint64_t v;
      memcpy(&v, p + input_pos, sizeof(v));
      // Loop over groups of 8 bytes at a time until we come across
      // a word that has a byte whose value is less than or equal to
      // '\r' (i.e. could contain a \n (0x0a) or a \r (0x0d) ).
      //
      // We use a has_less macro that quickly tests a whole 64-bit
      // word to see if any of the bytes has a value < N.
      //
      // For more details, see:
      //   http://graphics.stanford.edu/~seander/bithacks.html#HasLessInWord
#define has_less(x, n) (((x) - ~0ULL / 255 * (n)) & ~(x) & ~0ULL / 255 * 128)
      if (!has_less(v, '\r' + 1)) {
#undef has_less
        // No byte in this word has a value that could be a \r or a \n
        if (output_pos != input_pos) {
          memcpy(p + output_pos, &v, sizeof(v));
        }
        input_pos += 8;
        output_pos += 8;
        continue;
      }
    }
    std::string::const_reference in = p[input_pos];
    if (in == '\r') {
      if (r_seen) p[output_pos++] = '\n';
      r_seen = true;
    } else if (in == '\n') {
      if (input_pos != output_pos)
        p[output_pos++] = '\n';
      else
        output_pos++;
      r_seen = false;
    } else {
      if (r_seen) p[output_pos++] = '\n';
      r_seen = false;
      if (input_pos != output_pos)
        p[output_pos++] = in;
      else
        output_pos++;
    }
    input_pos++;
  }
  if (r_seen ||
      (auto_end_last_line && output_pos > 0 && p[output_pos - 1] != '\n')) {
    str->resize(output_pos + 1);
    str->operator[](output_pos) = '\n';
  } else if (output_pos < len) {
    str->resize(output_pos);
  }
}

}  // namespace strings
