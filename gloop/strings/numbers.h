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

#ifndef THIRD_PARTY_GLOOP_STRINGS_NUMBERS_H_
#define THIRD_PARTY_GLOOP_STRINGS_NUMBERS_H_

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <type_traits>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/numeric/int128.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gloop/base/fprint.h"

#ifdef SWIG
%include "absl/strings/numbers.h"
#endif

namespace strings_internal {

// Represents integer values of digits.
// Uses 36 to indicate an invalid character since we support
// bases up to 36.
static constexpr int8_t kAsciiToInt[256] = {
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,  // 16 36s.
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 0,  1,  2,  3,  4,  5,
    6,  7,  8,  9,  36, 36, 36, 36, 36, 36, 36, 10, 11, 12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
    36, 36, 36, 36, 36, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 36, 36, 36, 36, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36};

}  // namespace strings_internal

namespace strings {

ABSL_DEPRECATE_AND_INLINE()
inline bool safe_strtof(absl::string_view str, float* value) {
  return absl::SimpleAtof(str, value);
}
ABSL_DEPRECATE_AND_INLINE()
inline bool safe_strtod(absl::string_view str, double* value) {
  return absl::SimpleAtod(str, value);
}
ABSL_DEPRECATE_AND_INLINE()
inline bool safe_strtob(absl::string_view str, bool* value) {
  return absl::SimpleAtob(str, value);
}

using absl::numbers_internal::  // NOLINT(readability/namespace)
    kFastToBufferSize;
using absl::numbers_internal::  // NOLINT(readability/namespace)
    kSixDigitsToBufferSize;

inline char* FastInt32ToBufferLeft(int32_t i, char* buffer) {
  return absl::numbers_internal::FastIntToBuffer(i, buffer);
}
inline char* FastUInt32ToBufferLeft(uint32_t i, char* buffer) {
  return absl::numbers_internal::FastIntToBuffer(i, buffer);
}
inline char* FastInt64ToBufferLeft(int64_t i, char* buffer) {
  return absl::numbers_internal::FastIntToBuffer(i, buffer);
}
inline char* FastUInt64ToBufferLeft(uint64_t i, char* buffer) {
  return absl::numbers_internal::FastIntToBuffer(i, buffer);
}

inline char* FastInt32ToBuffer(int32_t i, char* buffer) {
  absl::numbers_internal::FastIntToBuffer(i, buffer);
  return buffer;
}
inline char* FastUInt32ToBuffer(uint32_t i, char* buffer) {
  absl::numbers_internal::FastIntToBuffer(i, buffer);
  return buffer;
}
inline char* FastInt64ToBuffer(int64_t i, char* buffer) {
  absl::numbers_internal::FastIntToBuffer(i, buffer);
  return buffer;
}
inline char* FastUInt64ToBuffer(uint64_t i, char* buffer) {
  absl::numbers_internal::FastIntToBuffer(i, buffer);
  return buffer;
}

template <typename int_type>
char* FastIntToBufferLeft(int_type i, char* buffer) {
  return absl::numbers_internal::FastIntToBuffer(i, buffer);
}

// Converts a double or float into a string which, if passed to `strtod()` or
// `strtof()` respectively, will produce the exact same original double or
// float.
//
// Exception: for NaN values,` strtod(RoundTripDtoa(NaN))` or
// `strtof(RoundTripFtoa(NaN))` may produce any NaN value, not necessarily the
// exact same original NaN value.
//
// Note: Calls to `RoundTrip*toa()` should preferably be replaced with
// `absl::StrCat(absl::LegacyPrecision(d))`.
//
// This routine attempts to produce a short output string; however it is not
// guaranteed to be as short as possible.
ABSL_DEPRECATE_AND_INLINE()
inline std::string RoundTripDtoa(double value) {
  return absl::StrCat(absl::HighPrecision(value));
}
ABSL_DEPRECATE_AND_INLINE()
inline std::string RoundTripFtoa(float value) {
  return absl::StrCat(absl::HighPrecision(value));
}
ABSL_DEPRECATE_AND_INLINE()
inline std::string RoundTripFtoa(double value) {
  // Cast to float so we get single-precision output.
  return absl::StrCat(absl::HighPrecision(static_cast<float>(value)));
}

// Overloads of RoundTrip*toa() to prevent accidentally passing integers to the
// RoundTrip formatters. It is expensive to use these functions to convert
// integers to strings. Instead, please use the implicit formatting provided by
// `StrCat()` and `StrAppend()`.
std::string RoundTripDtoa(int value) = delete;
std::string RoundTripFtoa(int value) = delete;

ABSL_DEPRECATE_AND_INLINE()
[[nodiscard]] inline std::string SimpleFtoa(float f) {
  return absl::StrCat(absl::HighPrecision(f));
}
ABSL_DEPRECATE_AND_INLINE()
[[nodiscard]] inline std::string SimpleDtoa(double d) {
  return absl::StrCat(absl::HighPrecision(d));
}

// Converts a boolean into a string, which if passed to `safe_strtob()` will
// produce the exact same original boolean, returning `true` if the value ==
// true, and `false` otherwise.
std::string SimpleBtoa(bool value);

ABSL_DEPRECATED("Use absl::StrCat to convert numbers to strings")
char* DoubleToBuffer(double i, char* buffer);
ABSL_DEPRECATED("Use absl::StrCat to convert numbers to strings")
char* FloatToBuffer(float i, char* buffer);

using absl::numbers_internal::  // NOLINT(readability/namespace)
    safe_strto32_base;
using absl::numbers_internal::  // NOLINT(readability/namespace)
    safe_strto64_base;
using absl::numbers_internal::  // NOLINT(readability/namespace)
    safe_strtou32_base;
using absl::numbers_internal::  // NOLINT(readability/namespace)
    safe_strtou64_base;

bool safe_strtosize_t_base(absl::string_view text, size_t* value, int base);

ABSL_DEPRECATE_AND_INLINE()
inline bool safe_strto32(absl::string_view text, int32_t* value) {
  return absl::SimpleAtoi(text, value);
}

ABSL_DEPRECATE_AND_INLINE()
inline bool safe_strto64(absl::string_view text, int64_t* value) {
  return absl::SimpleAtoi(text, value);
}

ABSL_DEPRECATE_AND_INLINE()
inline bool safe_strtou32(absl::string_view text, uint32_t* value) {
  return absl::SimpleAtoi(text, value);
}

ABSL_DEPRECATE_AND_INLINE()
inline bool safe_strtou64(absl::string_view text, uint64_t* value) {
  return absl::SimpleAtoi(text, value);
}

// Converts a fingerprint to 16 hex digits.
std::string FpToString(Fprint fp);

// Converts a string of 16 hex digits to a fingerprint, returning `true` on
// successful conversion, or `false` on invalid input.
bool StringToFp(absl::string_view hex, Fprint* fp);

// Converts between uint128 and 32-digit hex string. Note: no leading 0x or
// 0X prefix is generated.
std::string Uint128ToHexString(absl::uint128 ui128);

// Converts a hex string to an uint128 value, returning the value on successful
// conversion, or `nullopt` on invalid input. If the string contains a character
// which is not a valid hexadecimal digit, this is deemed an invalid input,
// including "x", so a leading 0x or 0X prefix is not allowed.
constexpr std::optional<absl::uint128> HexStringToUint128(
    absl::string_view hex) {
  constexpr int kDigitsPerPart = 16;
  constexpr int kBitsPerDigit = 4;
  constexpr int kBase = 16;
  if (hex.empty() || hex.size() > kDigitsPerPart * 2) {
    return std::nullopt;
  }
  uint64_t top = 0, bottom = 0;
  while (hex.size() > kDigitsPerPart) {
    const unsigned char c = static_cast<unsigned char>(hex[0]);
    const int8_t digit = ::strings_internal::kAsciiToInt[c];
    if (digit >= kBase) {
      return std::nullopt;
    }
    top = (top << kBitsPerDigit) | digit;
    hex = hex.substr(1);
  }
  while (!hex.empty()) {
    const unsigned char c = static_cast<unsigned char>(hex[0]);
    const int8_t digit = ::strings_internal::kAsciiToInt[c];
    if (digit >= kBase) {
      return std::nullopt;
    }
    bottom = (bottom << kBitsPerDigit) | digit;
    hex = hex.substr(1);
  }
  return absl::MakeUint128(top, bottom);
}
constexpr bool HexStringToUint128(absl::string_view hex, absl::uint128* value) {
  const auto result = HexStringToUint128(hex);
  if (result.has_value()) {
    *value = *result;
    return true;
  }
  *value = absl::MakeUint128(0, 0);
  return false;
}

// A simple parser for int32 values. Returns the parsed value
// if a valid integer is found; else returns deflt. It does not
// check if str is entirely consumed.
// This cannot handle decimal numbers with leading 0s, since they will be
// treated as octal.  If you know it's decimal, use ParseLeadingDec32Value.
int32_t ParseLeadingInt32Value(absl::string_view str, int32_t deflt);

// A simple parser for uint32 values. Returns the parsed value
// if a valid integer is found; else returns deflt. It does not
// check if str is entirely consumed.
// This cannot handle decimal numbers with leading 0s, since they will be
// treated as octal.  If you know it's decimal, use ParseLeadingUDec32Value.
uint32_t ParseLeadingUInt32Value(absl::string_view str, uint32_t deflt);

// A simple parser for decimal int32 values. Returns the parsed value
// if a valid integer is found; else returns deflt. It does not
// check if str is entirely consumed.
// The string passed in is treated as *10 based*.
// This can handle strings with leading 0s.
// See also: ParseLeadingDec64Value
int32_t ParseLeadingDec32Value(absl::string_view str, int32_t deflt);

// A simple parser for decimal uint32 values. Returns the parsed value
// if a valid integer is found; else returns deflt. It does not
// check if str is entirely consumed.
// The string passed in is treated as *10 based*.
// This can handle strings with leading 0s.
// See also: ParseLeadingUDec64Value
uint32_t ParseLeadingUDec32Value(absl::string_view str, uint32_t deflt);

// A simple parser for long long values.
// Returns the parsed value if a
// valid integer is found; else returns deflt
uint64_t ParseLeadingUInt64Value(absl::string_view str, uint64_t deflt);
int64_t ParseLeadingInt64Value(absl::string_view str, int64_t deflt);
uint64_t ParseLeadingHex64Value(absl::string_view str, uint64_t deflt);
int64_t ParseLeadingDec64Value(absl::string_view str, int64_t deflt);
uint64_t ParseLeadingUDec64Value(absl::string_view str, uint64_t deflt);

// A simple parser for double values. Returns the parsed value
// if a valid double is found; else returns deflt. It does not
// check if str is entirely consumed.
double ParseLeadingDoubleValue(const char* str, double deflt);
inline double ParseLeadingDoubleValue(const std::string& str, double deflt) {
  return ParseLeadingDoubleValue(str.c_str(), deflt);
}

// A recognizer of boolean string values. Returns the parsed value
// if a valid value is found; else returns deflt.  This skips leading
// whitespace, is case insensitive, and recognizes these forms:
// 0/1, false/true, no/yes, n/y
bool ParseLeadingBoolValue(absl::string_view str, bool deflt);

// Converts the number argument to a string representation in base-36, returning
// the number of bytes written, not including terminating NUL. Conversion fails
// if the buffer is too small to hold the string and terminating NUL. A
// return value of 0 indicates an error.
size_t u64tostr_base36(uint64_t number, size_t buf_size, char* buffer);

// Parses a string representation of a 64-bit unsigned integer, optionally
// followed by a size suffix ('K', 'M', 'G', 'T', case-insensitive).  These are
// binary sizes, based on powers of 1024 (equivalent to KiB, MiB, GiB, TiB).
//
// Returns the parsed and scaled value, or std::nullopt if the string is empty,
// contains invalid characters, has trailing characters after the suffix, or if
// the value overflows.
//
// New users should prefer //gloop/util/units/bytes.h instead, which handles
// both binary and decimal units and differentiates more clearly between the
// two.
std::optional<uint64_t> AtoiKMGT(absl::string_view s);

// Converts the given string representation into a 64-bit unsigned integer
// value similar to `atoi(s)`, except `s` may refer to metric size suffixes for
// kilo, mega, giga, and tera. (E.g. "16k", "32M", "2G", "4t").
//
// Deprecated. Use AtoiKMGT or //gloop/util/units/bytes.h instead.
ABSL_DEPRECATED("Use AtoiKMGT instead")
uint64_t atoi_kmgt(const char* s);
ABSL_DEPRECATED("Use AtoiKMGT instead")
inline uint64_t atoi_kmgt(const std::string& s) { return atoi_kmgt(s.c_str()); }

// Converts an integer to a string. Truncates values to K, G, M or T as
// appropriate. Opposite of atoi_kmgt() E.g. 3000 -> 2K   57185920 -> 54M
std::string ItoaKMGT(int64_t i);

// `FastTimeToBuffer()`, intended for speed, puts the output into RFC822 format.
//
// NOTE: In 64-bit land, `sizeof(time_t)` is 8, so it is possible to pass to
// `FastTimeToBuffer()` a time whose year cannot be represented in 4 digits. In
// this case, the output buffer will contain the string "Invalid:<value>"
//
// WARNING: `FastTimeToBuffer(0, ...)` returns the current time, not 1970.
// WARNING: This "0" behavior is deprecated.  Please pass `time(nullptr)`
//          if you want a string from the current time.
//
// The buffer size should be at least `kFastToBufferSize` bytes.

ABSL_DEPRECATED("Use FastFormatRFC1123GMT() from util/time/time.h")
char* FastTimeToBuffer(time_t t, char* buffer);

// Returns 1 if `buf` is prefixed by `num_digits` of hex digits; returns 0
// otherwise. The function checks for '\0' for string termination.
int HexDigitsPrefix(const char* buf, ptrdiff_t num_digits);

// Eliminates all leading zeroes (unless the string itself is composed
// of nothing but zeroes, in which case one is kept: 0...0 becomes 0).
void ConsumeStrayLeadingZeroes(std::string* str);

// Converts an integer to a string.  Commas are inserted if the result would
// have more than three consecutive digits, where every comma is followed
// by exactly 3 digits.
template <typename IntType>
inline std::string SimpleItoaWithCommas(IntType ii) {
  static_assert(std::is_integral_v<IntType>);
  std::string s1 = absl::StrCat(ii);
  absl::string_view sp1(s1);
  std::string output;
  // Copy leading non-digit characters unconditionally.
  // This picks up the leading sign.
  while (!sp1.empty() && !absl::ascii_isdigit(sp1[0])) {
    output.push_back(sp1[0]);
    sp1.remove_prefix(1);
  }
  // Copy rest of input characters.
  for (absl::string_view::size_type i = 0; i < sp1.size(); ++i) {
    if (i > 0 && (sp1.size() - i) % 3 == 0) {
      output.push_back(',');
    }
    output.push_back(sp1[i]);
  }
  return output;
}

// Parses an expression in 'text' of the form: <double><sep><double> where
// <double> may be a double-precision number and <sep> is a single char or "..",
// and must be one of the chars in parameter 'separators', which may contain '-'
// or '.' (which means "..") or any chars not allowed in a double. If
// allow_unbounded_markers, <double> may also be a '?' to indicate unboundedness
// (if on the left of <sep>, means unbounded below; if on the right, means
// unbounded above). Depending on num_required_bounds, which may be 0, 1, or 2,
// <double> may also be the empty string, indicating unboundedness. If
// require_separator is false, then a single <double> is acceptable and is
// parsed as a range bounded from below. We also check that the character
// following the range must be in acceptable_terminators. If null_terminator_ok,
// then it is also OK if the range ends in \0 or after len chars. If
// allow_currency is true, the first <double> may be optionally preceded by a
// '$', in which case *is_currency will be true, and the second <double> may
// similarly be preceded by a '$'. In these cases, the '$' will be ignored
// (otherwise it's an error). If allow_comparators is true, the expression in
// 'text' may also be of the form <comparator><double>, where <comparator> is
// '<' or '>' or '<=' or '>='. separators and require_separator are ignored in
// this format, but all other parameters function as for the first format.
// Returns true if the expression is parsed successfully; false otherwise. If
// successful, output params are: 'end', which points to the char just beyond
// the expression; 'from' and 'to' are set to the values of the <double>s, and
// are -inf and inf (or unchanged, depending on dont_modify_unbounded) if
// unbounded. Output params are undefined if false is returned. len is the input
// length, or -1 if text is '\0'-terminated, which is more efficient.
struct DoubleRangeOptions {
  const char* separators;
  const char* acceptable_terminators;
  uint32_t num_required_bounds;
  bool require_separator;
  bool null_terminator_ok;
  bool allow_unbounded_markers;
  bool dont_modify_unbounded;
  bool allow_currency;
  bool allow_comparators;
};

bool ParseDoubleRange(const char* text, ptrdiff_t len, const char** end,
                      double* from, double* to, bool* is_currency,
                      const DoubleRangeOptions& opts);

// -----------------------------------------------------------------------------
// Natural Sort Order Utilities
// -----------------------------------------------------------------------------
//
// A Natural Sort Order sorts strings containing multi-digit characters ordered
// as if those digits were considered as one character, in numerical order.
// For example, "image9" is considered before "image10".  (This goes by the
// name `natsort()` in Go and PHP.)
//
// For more information, see https://en.wikipedia.org/wiki/Natural_sort_order.

// These are like std::less<string> and std::greater<string>, except when a
// run of digits is encountered at corresponding points in the two
// arguments.  Such digit strings are compared numerically instead
// of lexicographically.  Therefore if you sort by
// "autodigit_less", some machine names might get sorted as:
//    exaf1
//    exaf2
//    exaf10
// When using "strict" comparison (AutoDigitStrCmp with the strict flag
// set to true, or the strict version of the other functions),
// strings that represent equal numbers will not be considered equal if
// the string representations are not identical.  That is, "01" < "1" in
// strict mode, but "01" == "1" otherwise.

int AutoDigitStrCmp(absl::string_view a, absl::string_view b, bool strict);
inline bool AutoDigitLessThan(absl::string_view a, absl::string_view b) {
  return AutoDigitStrCmp(a, b, false) < 0;
}
inline bool StrictAutoDigitLessThan(absl::string_view a, absl::string_view b) {
  return AutoDigitStrCmp(a, b, true) < 0;
}

// For speed, when you know you have zero-terminated strings.
int AutoDigitStrCmpZ(const char* a, const char* b, bool strict);

struct autodigit_less {
  using is_transparent = void;
  bool operator()(absl::string_view a, absl::string_view b) const {
    return AutoDigitStrCmp(a, b, /*strict=*/false) < 0;
  }
};

struct autodigit_greater {
  using is_transparent = void;
  bool operator()(absl::string_view a, absl::string_view b) const {
    return AutoDigitStrCmp(a, b, /*strict=*/false) > 0;
  }
};

struct strict_autodigit_less {
  using is_transparent = void;
  bool operator()(absl::string_view a, absl::string_view b) const {
    return AutoDigitStrCmp(a, b, /*strict=*/true) < 0;
  }
};

struct strict_autodigit_greater {
  using is_transparent = void;
  bool operator()(absl::string_view a, absl::string_view b) const {
    return AutoDigitStrCmp(a, b, /*strict=*/true) > 0;
  }
};

}  // namespace strings

#endif  // THIRD_PARTY_GLOOP_STRINGS_NUMBERS_H_
