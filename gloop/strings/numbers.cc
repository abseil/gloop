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

#include "gloop/strings/numbers.h"

#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <memory>
#include <string>

#include "absl/base/internal/raw_logging.h"
#include "absl/numeric/int128.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gloop/base/fprint.h"
#include "gloop/strings/string_view_utils.h"
#include "gloop/strings/util.h"

namespace strings {

namespace {

struct ZeroTerminated {
  char buf[48];
  std::string s;

  explicit ZeroTerminated(absl::string_view sp) {
    if (sp.size() < sizeof(buf)) {
      buf[sp.size()] = 0;
      memcpy(buf, sp.data(), sp.size());
    } else {
      s.assign(sp.data(), sp.size());
    }
  }

  operator const char*() const { return !s.empty() ? &s[0] : buf; }  // NOLINT
};

// Reads a <double> in *text, which may not be whitespace-initiated.
// *len is the length, or -1 if text is '\0'-terminated, which is more
// efficient.  Sets *text to the end of the double, and val to the
// converted value, and the length of the double is subtracted from
// *len. <double> may also be a '?', in which case val will be
// unchanged. Returns true upon success.  If initial_minus is
// non-null, then *initial_minus will indicate whether the first
// symbol seen was a '-', which will be ignored. Similarly, if
// final_period is non-null, then *final_period will indicate whether
// the last symbol seen was a '.', which will be ignored. This is
// useful in case that an initial '-' or final '.' would have another
// meaning (as a separator, e.g.).
static inline bool EatADouble(const char** text, ptrdiff_t* len,
                              bool allow_question, double* val,
                              bool* initial_minus, bool* final_period) {
  const char* pos = *text;
  ptrdiff_t rem = *len;  // remaining length, or -1 if null-terminated

  if (pos == nullptr || rem == 0) return false;

  if (allow_question && (*pos == '?')) {
    *text = pos + 1;
    if (rem != -1) *len = rem - 1;
    return true;
  }

  if (initial_minus) {
    if ((*initial_minus = (*pos == '-'))) {  // Yes, we want assignment.
      if (rem == 1) return false;
      ++pos;
      if (rem != -1) --rem;
    }
  }

  // a double has to begin one of these (we don't allow 'inf' or whitespace)
  // this also serves as an optimization.
  if (!strchr("-+.0123456789", *pos)) return false;

  // strtod is evil in that the second param is a non-const char**
  char* end_nonconst;
  double retval;
  if (rem == -1) {
    retval = strtod(pos, &end_nonconst);
  } else {
    // not '\0'-terminated & no obvious terminator found. must copy.
    std::unique_ptr<char[]> buf(new char[rem + 1]);
    memcpy(buf.get(), pos, rem);
    buf[rem] = '\0';
    retval = strtod(buf.get(), &end_nonconst);
    end_nonconst = const_cast<char*>(pos) + (end_nonconst - buf.get());
  }

  if (pos == end_nonconst) return false;

  if (final_period) {
    *final_period = (end_nonconst[-1] == '.');
    if (*final_period) {
      --end_nonconst;
    }
  }

  *text = end_nonconst;
  *val = retval;
  if (rem != -1) *len = rem - (end_nonconst - pos);
  return true;
}

// If update, consume one of acceptable_chars from string *text of
// length len and return that char, or '\0' otherwise. If len is -1,
// *text is null-terminated. If update is false, don't alter *text and
// *len. If null_ok, then update must be false, and, if text has no
// more chars, then return '\1' (arbitrary nonzero).
static inline char EatAChar(const char** text, ptrdiff_t* len,
                            const char* acceptable_chars, bool update,
                            bool null_ok) {
  assert(!(update && null_ok));
  if ((*len == 0) || (**text == '\0'))
    return (null_ok ? '\1' : '\0');  // if null_ok, we're in predicate mode.

  if (strchr(acceptable_chars, **text)) {
    char result = **text;
    if (update) {
      ++(*text);
      if (*len != -1) --(*len);
    }
    return result;
  }

  return '\0';  // no match; no update
}

}  // namespace

// Parse an expression in 'text' of the form: <comparator><double> or
// <double><sep><double> See full comments in header file.
bool ParseDoubleRange(const char* text, ptrdiff_t len, const char** end,
                      double* from, double* to, bool* is_currency,
                      const DoubleRangeOptions& opts) {
  const double from_default = opts.dont_modify_unbounded ? *from : -HUGE_VAL;

  if (!opts.dont_modify_unbounded) {
    *from = -HUGE_VAL;
    *to = HUGE_VAL;
  }
  if (opts.allow_currency && (is_currency != nullptr)) *is_currency = false;

  assert(len >= -1);
  assert(opts.separators && (*opts.separators != '\0'));
  // these aren't valid separators
  assert(strlen(opts.separators) == strcspn(opts.separators, "+0123456789eE$"));
  assert(opts.num_required_bounds <= 2);

  // Handle easier cases of comparators (<, >) first
  if (opts.allow_comparators) {
    char comparator = EatAChar(&text, &len, "<>", true, false);
    if (comparator) {
      double* dest = (comparator == '>') ? from : to;
      EatAChar(&text, &len, "=", true, false);
      if (opts.allow_currency && EatAChar(&text, &len, "$", true, false))
        if (is_currency != nullptr) *is_currency = true;
      if (!EatADouble(&text, &len, opts.allow_unbounded_markers, dest, nullptr,
                      nullptr))
        return false;
      *end = text;
      return EatAChar(&text, &len, opts.acceptable_terminators, false,
                      opts.null_terminator_ok);
    }
  }

  bool seen_dollar =
      (opts.allow_currency && EatAChar(&text, &len, "$", true, false));

  // If we see a '-', two things could be happening: -<to> or
  // <from>... where <from> is negative. Treat initial minus sign as a
  // separator if '-' is a valid separator.
  // Similarly, we prepare for the possibility of seeing a '.' at the
  // end of the number, in case '.' (which really means '..') is a
  // separator.
  bool initial_minus_sign = false;
  bool final_period = false;
  bool* check_initial_minus = (strchr(opts.separators, '-') && !seen_dollar &&
                               (opts.num_required_bounds < 2))
                                  ? (&initial_minus_sign)
                                  : nullptr;
  bool* check_final_period =
      strchr(opts.separators, '.') ? (&final_period) : nullptr;
  bool double_seen = EatADouble(&text, &len, opts.allow_unbounded_markers, from,
                                check_initial_minus, check_final_period);

  // if 2 bounds required, must see a double (or '?' if allowed)
  if ((opts.num_required_bounds == 2) && !double_seen) return false;

  if (seen_dollar && !double_seen) {
    --text;
    if (len != -1) ++len;
    seen_dollar = false;
  }
  // If we're here, we've read the first double and now expect a
  // separator and another <double>.
  char separator = EatAChar(&text, &len, opts.separators, true, false);
  if (separator == '.') {
    // seen one '.' as separator; must check for another; perhaps set seplen=2
    if (EatAChar(&text, &len, ".", true, false)) {
      if (final_period) {
        // We may have three periods in a row. The first is part of the
        // first number, the others are a separator. Policy: 234...567
        // is "234." to "567", not "234" to ".567".
        EatAChar(&text, &len, ".", true, false);
      }
    } else if (!EatAChar(&text, &len, opts.separators, true, false)) {
      // just one '.' and no other separator; uneat the first '.' we saw
      --text;
      if (len != -1) ++len;
      separator = '\0';
    }
  }
  // By now, we've consumed whatever separator there may have been,
  // and separator is true iff there was one.
  if (!separator) {
    if (final_period)  // final period now considered part of first double
      EatAChar(&text, &len, ".", true, false);
    if (initial_minus_sign && double_seen) {
      *to = *from;
      *from = from_default;
    } else if (opts.require_separator ||
               (opts.num_required_bounds > 0 && !double_seen) ||
               (opts.num_required_bounds > 1)) {
      return false;
    }
  } else {
    if (initial_minus_sign && double_seen) *from = -(*from);
    // read second <double>
    bool second_dollar_seen =
        (seen_dollar || (opts.allow_currency && !double_seen)) &&
        EatAChar(&text, &len, "$", true, false);
    bool second_double_seen = EatADouble(
        &text, &len, opts.allow_unbounded_markers, to, nullptr, nullptr);
    if (opts.num_required_bounds >
        static_cast<uint32_t>(double_seen + second_double_seen))
      return false;
    if (second_dollar_seen && !second_double_seen) {
      --text;
      if (len != -1) ++len;
      second_dollar_seen = false;
    }
    seen_dollar = seen_dollar || second_dollar_seen;
  }

  if (seen_dollar && (is_currency != nullptr)) *is_currency = true;
  // We're done. But we have to check that the next char is a proper
  // terminator.
  *end = text;
  char terminator = EatAChar(&text, &len, opts.acceptable_terminators, false,
                             opts.null_terminator_ok);
  if (terminator == '.') --(*end);
  return terminator;
}

// ----------------------------------------------------------------------
// ConsumeStrayLeadingZeroes
//    Eliminates all leading zeroes (unless the string itself is composed
//    of nothing but zeroes, in which case one is kept: 0...0 becomes 0).
// --------------------------------------------------------------------

void ConsumeStrayLeadingZeroes(std::string* const str) {
  const std::string::size_type len(str->size());
  if (len > 1 && (*str)[0] == '0') {
    const char *const begin(str->c_str()), *const end(begin + len),
        *ptr(begin + 1);
    while (ptr != end && *ptr == '0') {
      ++ptr;
    }
    std::string::size_type remove(ptr - begin);
    assert(ptr > begin);
    if (remove == len) {
      --remove;  // if they are all zero, leave one...
    }
    str->erase(0, remove);
  }
}

// ----------------------------------------------------------------------
// FpToString()
// Uint128ToHexString()
//    Convert various types to their string representation, possibly padded
//    with spaces, using snprintf format specifiers.
// ----------------------------------------------------------------------

std::string FpToString(Fprint fp) {
  return absl::StrCat(absl::Hex(fp, absl::kZeroPad16));
}

char* DoubleToBuffer(double i, char* buffer) {
  return absl::numbers_internal::RoundTripDoubleToBuffer(i, buffer);
}
char* FloatToBuffer(float i, char* buffer) {
  return absl::numbers_internal::RoundTripFloatToBuffer(i, buffer);
}

std::string SimpleBtoa(bool value) {
  return value ? std::string("true") : std::string("false");
}

// Default arguments
std::string Uint128ToHexString(absl::uint128 ui128) {
  return absl::StrCat(absl::Hex(absl::Uint128High64(ui128), absl::kZeroPad16),
                      absl::Hex(absl::Uint128Low64(ui128), absl::kZeroPad16));
}

// ----------------------------------------------------------------------
// StringToFp()
// HexStringToUint128()
//    Convert various hex string representations to their types.
// ----------------------------------------------------------------------

bool StringToFp(absl::string_view hex, Fprint* fp) {
  if (hex.size() != 16) return false;
  // Verify that there are no invalid characters.
  if (hex.find_first_not_of("0123456789abcdefABCDEF", 0) !=
      absl::string_view::npos)
    return false;
  return absl::SimpleHexAtoi(hex, fp);
}

// ----------------------------------------------------------------------
// ParseLeadingInt32Value()
// ParseLeadingUInt32Value()
//    A simple parser for [u]int32 values. Returns the parsed value
//    if a valid value is found; else returns deflt
//    This cannot handle decimal numbers with leading 0s.
// --------------------------------------------------------------------

static int32_t ParseLeadingInt32Value(const char* str, int32_t deflt) {
  using std::numeric_limits;

  char* error = nullptr;
  int64_t value = strtol(str, &error, 0);  // NOLINT
  // Limit long values to int32 min/max.  Needed for lp64; no-op on 32 bits.
  if (value > std::numeric_limits<int32_t>::max()) {
    value = std::numeric_limits<int32_t>::max();
  } else if (value < std::numeric_limits<int32_t>::min()) {
    value = std::numeric_limits<int32_t>::min();
  }
  return (error == str) ? deflt : static_cast<int32_t>(value);
}

static uint32_t ParseLeadingUInt32Value(const char* str, uint32_t deflt) {
  using std::numeric_limits;

  if (std::numeric_limits<unsigned long>::max() ==  // NOLINT
      std::numeric_limits<uint32_t>::max()) {
    // When long is 32 bits, we can use strtoul.
    char* error = nullptr;
    const uint32_t value = strtoul(str, &error, 0);  // NOLINT
    return (error == str) ? deflt : value;
  } else {
    // When long is 64 bits, we must use strtoll and handle limits by hand.
    // The reason we cannot use an unsigned 64-bit strtoull is that it would
    // be impossible to differentiate "-2" (which should wrap around to the
    // value UINT_MAX-1) from a string with ULONG_MAX-1(which should be
    // pegged to UINT_MAX due to overflow).
    static_assert(sizeof(long long) == sizeof(int64_t),  // NOLINT
                  "This method assumes strtoll is 64-bit, "
                  "but sizeof long long is not sizeof int64_t");
    char* error = nullptr;
    int64_t value = strtoll(str, &error, 0);  // NOLINT
    if (value > std::numeric_limits<uint32_t>::max() ||
        value < -static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
      value = std::numeric_limits<uint32_t>::max();
    }
    // Within these limits, truncation to 32 bits handles negatives correctly.
    return (error == str) ? deflt : static_cast<uint32_t>(value);
  }
}

// ----------------------------------------------------------------------
// ParseLeadingDec32Value
// ParseLeadingUDec32Value
//    A simple parser for [u]int32 values. Returns the parsed value
//    if a valid value is found; else returns deflt
//    The string passed in is treated as *10 based*.
//    This can handle strings with leading 0s.
// --------------------------------------------------------------------

static int32_t ParseLeadingDec32Value(const char* str, int32_t deflt) {
  using std::numeric_limits;

  char* error = nullptr;
  int64_t value = strtol(str, &error, 10);  // NOLINT
  // Limit long values to int32 min/max.  Needed for 64-bit platforms to
  // guarantee identical 32-bit-looking overflow behavior; no-op on 32 bits.
  if (value > std::numeric_limits<int32_t>::max()) {
    value = std::numeric_limits<int32_t>::max();
  } else if (value < std::numeric_limits<int32_t>::min()) {
    value = std::numeric_limits<int32_t>::min();
  }
  return (error == str) ? deflt : static_cast<int32_t>(value);
}

static uint32_t ParseLeadingUDec32Value(const char* str, uint32_t deflt) {
  using std::numeric_limits;

  if (std::numeric_limits<unsigned long>::max() ==  // NOLINT
      std::numeric_limits<uint32_t>::max()) {
    // When long is 32 bits, we can use strtoul.
    char* error = nullptr;
    const uint32_t value = strtoul(str, &error, 10);  // NOLINT
    return (error == str) ? deflt : value;
  } else {
    // When long is 64 bits, we must use strtoll and handle limits by hand.
    // The reason we cannot use an unsigned 64-bit strtoull is that it would
    // be impossible to differentiate "-2" (which should wrap around to the
    // value UINT_MAX-1) from a string with ULONG_MAX-1 (which should be
    // pegged to UINT_MAX due to overflow).
    static_assert(sizeof(long long) == sizeof(int64_t),  // NOLINT
                  "This method assumes strtoll is 64-bit, "
                  "but sizeof long long is not sizeof int64_t");
    char* error = nullptr;
    int64_t value = strtoll(str, &error, 10);  // NOLINT
    if (value > std::numeric_limits<uint32_t>::max() ||
        value < -static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
      value = std::numeric_limits<uint32_t>::max();
    }
    // Within these limits, truncation to 32 bits handles negatives correctly.
    return (error == str) ? deflt : static_cast<uint32_t>(value);
  }
}

// ----------------------------------------------------------------------
// ParseLeadingUInt64Value
// ParseLeadingInt64Value
// ParseLeadingHex64Value
//    A simple parser for 64-bit values. Returns the parsed value if a
//    valid integer is found; else returns deflt
//    UInt64 and Int64 cannot handle decimal numbers with leading 0s.
// --------------------------------------------------------------------
static uint64_t ParseLeadingUInt64Value(const char* str, uint64_t deflt) {
  static_assert(sizeof(unsigned long long) == sizeof(uint64_t),  // NOLINT
                "This method assumes strtoull is 64-bit, "
                "but sizeof long long is not sizeof uint64_t");
  char* error = nullptr;
  const uint64_t value = strtoull(str, &error, 0);  // NOLINT
  return (error == str) ? deflt : value;
}

static int64_t ParseLeadingInt64Value(const char* str, int64_t deflt) {
  static_assert(sizeof(long long) == sizeof(int64_t),  // NOLINT
                "This method assumes strtoll is 64-bit, "
                "but sizeof long long is not sizeof int64_t");
  char* error = nullptr;
  const int64_t value = strtoll(str, &error, 0);  // NOLINT
  return (error == str) ? deflt : value;
}

static uint64_t ParseLeadingHex64Value(const char* str, uint64_t deflt) {
  static_assert(sizeof(unsigned long long) == sizeof(uint64_t),  // NOLINT
                "This method assumes strtoull is 64-bit, "
                "but sizeof long long is not sizeof uint64_t");
  char* error = nullptr;
  const uint64_t value = strtoull(str, &error, 16);  // NOLINT
  return (error == str) ? deflt : value;
}

// ----------------------------------------------------------------------
// ParseLeadingDec64Value
// ParseLeadingUDec64Value
//    A simple parser for [u]int64 values. Returns the parsed value
//    if a valid value is found; else returns deflt
//    The string passed in is treated as *10 based*.
//    This can handle strings with leading 0s.
// --------------------------------------------------------------------

static int64_t ParseLeadingDec64Value(const char* str, int64_t deflt) {
  static_assert(sizeof(long long) == sizeof(int64_t),  // NOLINT
                "This method assumes strtoll is 64-bit, "
                "but sizeof long long is not sizeof int64_t");
  char* error = nullptr;
  const int64_t value = strtoll(str, &error, 10);  // NOLINT
  return (error == str) ? deflt : value;
}

static uint64_t ParseLeadingUDec64Value(const char* str, uint64_t deflt) {
  static_assert(sizeof(unsigned long long) == sizeof(uint64_t),  // NOLINT
                "This method assumes strtoull is 64-bit, "
                "but sizeof long long is not sizeof uint64_t");
  char* error = nullptr;
  const uint64_t value = strtoull(str, &error, 10);  // NOLINT
  return (error == str) ? deflt : value;
}

// ----------------------------------------------------------------------
// ParseLeadingDoubleValue()
//    A simple parser for double values. Returns the parsed value
//    if a valid value is found; else returns deflt
// --------------------------------------------------------------------

double ParseLeadingDoubleValue(const char* str, double deflt) {
  char* error = nullptr;
  errno = 0;
  const double value = strtod(str, &error);
  if (errno != 0 ||    // overflow/underflow happened
      error == str) {  // no valid parse
    return deflt;
  } else {
    return value;
  }
}

// ----------------------------------------------------------------------
// ParseLeadingBoolValue()
//    A recognizer of boolean string values. Returns the parsed value
//    if a valid value is found; else returns deflt.  This skips leading
//    whitespace, is case insensitive, and recognizes these forms:
//    0/1, false/true, no/yes, n/y
// --------------------------------------------------------------------
bool ParseLeadingBoolValue(absl::string_view str, bool deflt) {
  strings::RemoveLeadingWhitespace(&str);
  // Keep alphanumeric
  const char* const start = str.data();
  const char* alpha_num_end = start;
  const char* end = alpha_num_end + str.size();
  while (alpha_num_end < end && absl::ascii_isalnum(*alpha_num_end)) {
    ++alpha_num_end;
  }
  const absl::string_view value(start, alpha_num_end - start);
  switch (value.size()) {
    case 1: {
      const char c = value[0];
      if (c == '0' || c == 'n' || c == 'N') return false;
      if (c == '1' || c == 'y' || c == 'Y') return true;
    } break;
    case 2:
      if (absl::EqualsIgnoreCase(value, "no")) return false;
      break;
    case 3:
      if (absl::EqualsIgnoreCase(value, "yes")) return true;
      break;
    case 4:
      if (absl::EqualsIgnoreCase(value, "true")) return true;
      break;
    case 5:
      if (absl::EqualsIgnoreCase(value, "false")) return false;
      break;
  }
  return deflt;
}

// ----------------------------------------------------------------------
// ParseLeadingInt32Value()
// ParseLeadingUInt32Value()
//    A simple parser for [u]int32 values. Returns the parsed value
//    if a valid value is found; else returns deflt
//    This cannot handle decimal numbers with leading 0s.
// --------------------------------------------------------------------

int32_t ParseLeadingInt32Value(absl::string_view str, int32_t deflt) {
  return ParseLeadingInt32Value(ZeroTerminated(str), deflt);
}

uint32_t ParseLeadingUInt32Value(absl::string_view str, uint32_t deflt) {
  return ParseLeadingUInt32Value(ZeroTerminated(str), deflt);
}

// ----------------------------------------------------------------------
// ParseLeadingDec32Value
// ParseLeadingUDec32Value
//    A simple parser for [u]int32 values. Returns the parsed value
//    if a valid value is found; else returns deflt
//    The string passed in is treated as *10 based*.
//    This can handle strings with leading 0s.
// --------------------------------------------------------------------

int32_t ParseLeadingDec32Value(absl::string_view str, int32_t deflt) {
  return ParseLeadingDec32Value(ZeroTerminated(str), deflt);
}

uint32_t ParseLeadingUDec32Value(absl::string_view str, uint32_t deflt) {
  return ParseLeadingUDec32Value(ZeroTerminated(str), deflt);
}

// ----------------------------------------------------------------------
// ParseLeadingUInt64Value
// ParseLeadingInt64Value
// ParseLeadingHex64Value
//    A simple parser for 64-bit values. Returns the parsed value if a
//    valid integer is found; else returns deflt
//    UInt64 and Int64 cannot handle decimal numbers with leading 0s.
// --------------------------------------------------------------------
uint64_t ParseLeadingUInt64Value(absl::string_view str, uint64_t deflt) {
  return ParseLeadingUInt64Value(ZeroTerminated(str), deflt);
}

int64_t ParseLeadingInt64Value(absl::string_view str, int64_t deflt) {
  return ParseLeadingInt64Value(ZeroTerminated(str), deflt);
}

uint64_t ParseLeadingHex64Value(absl::string_view str, uint64_t deflt) {
  return ParseLeadingHex64Value(ZeroTerminated(str), deflt);
}

// ----------------------------------------------------------------------
// ParseLeadingDec64Value
// ParseLeadingUDec64Value
//    A simple parser for [u]int64 values. Returns the parsed value
//    if a valid value is found; else returns deflt
//    The string passed in is treated as *10 based*.
//    This can handle strings with leading 0s.
// --------------------------------------------------------------------

int64_t ParseLeadingDec64Value(absl::string_view str, int64_t deflt) {
  return ParseLeadingDec64Value(ZeroTerminated(str), deflt);
}

uint64_t ParseLeadingUDec64Value(absl::string_view str, uint64_t deflt) {
  return ParseLeadingUDec64Value(ZeroTerminated(str), deflt);
}

uint64_t atoi_kmgt(const char* s) {
  static_assert(sizeof(unsigned long long) == sizeof(uint64_t),  // NOLINT
                "This method assumes strtoull is 64-bit, "
                "but sizeof long long is not sizeof uint64_t");
  char* endptr;
  uint64_t n = strtoull(s, &endptr, 10);  // NOLINT
  uint64_t scale = 1;
  char c = *endptr;
  if (c != '\0') {
    c = absl::ascii_toupper(c);
    switch (c) {
      case 'K':
        scale = uint64_t{1} << 10;
        break;
      case 'M':
        scale = uint64_t{1} << 20;
        break;
      case 'G':
        scale = uint64_t{1} << 30;
        break;
      case 'T':
        scale = uint64_t{1} << 40;
        break;
      default:
        ABSL_RAW_LOG(DFATAL,
                     "atoi_kmgt: ignoring invalid mnemonic '%c'; "
                     "should be one of 'K', 'M', 'G', and 'T'.",
                     c);
        break;
    }
  }
  if (scale > 1 && n > std::numeric_limits<uint64_t>::max() / scale) {
    ABSL_RAW_LOG(DFATAL, "atoi_kmgt: overflow detected for value '%s'", s);
    return std::numeric_limits<uint64_t>::max();
  }
  return n * scale;
}

// ----------------------------------------------------------------------
// AutoDigitStrCmp
// AutoDigitLessThan
// StrictAutoDigitLessThan
// autodigit_less
// autodigit_greater
// strict_autodigit_less
// strict_autodigit_greater
//    These are like std::less<string> and std::greater<string>, except when a
//    run of digits is encountered at corresponding points in the two
//    arguments.  Such digit strings are compared numerically instead
//    of lexicographically.  Therefore if you sort by
//    "autodigit_less", some machine names might get sorted as:
//        exaf1
//        exaf2
//        exaf10
//    When using "strict" comparison (AutoDigitStrCmp with the strict flag
//    set to true, or the strict version of the other functions),
//    strings that represent equal numbers will not be considered equal if
//    the string representations are not identical.  That is, "01" < "1" in
//    strict mode, but "01" == "1" otherwise.
// ----------------------------------------------------------------------

int AutoDigitStrCmp(absl::string_view a, absl::string_view b, bool strict) {
  size_t aindex = 0;
  size_t bindex = 0;
  while ((aindex < a.size()) && (bindex < b.size())) {
    if (absl::ascii_isdigit(a[aindex]) && absl::ascii_isdigit(b[bindex])) {
      // Compare runs of digits.  Instead of extracting numbers, we
      // just skip leading zeroes, and then get the run-lengths.  This
      // allows us to handle arbitrary precision numbers.  We remember
      // how many zeroes we found so that we can differentiate between
      // "1" and "01" in strict mode.

      // Skip leading zeroes, but remember how many we found
      size_t azeroes = aindex;
      size_t bzeroes = bindex;
      while ((aindex < a.size()) && (a[aindex] == '0')) aindex++;
      while ((bindex < b.size()) && (b[bindex] == '0')) bindex++;
      azeroes = aindex - azeroes;
      bzeroes = bindex - bzeroes;

      // Count digit lengths
      size_t astart = aindex;
      size_t bstart = bindex;
      while ((aindex < a.size()) && absl::ascii_isdigit(a[aindex])) aindex++;
      while ((bindex < b.size()) && absl::ascii_isdigit(b[bindex])) bindex++;
      if (aindex - astart < bindex - bstart) {
        // a has shorter run of digits: so smaller
        return -1;
      } else if (aindex - astart > bindex - bstart) {
        // a has longer run of digits: so larger
        return 1;
      } else {
        // Same lengths, so compare digit by digit
        for (size_t i = 0; i < aindex - astart; i++) {
          if (a[astart + i] < b[bstart + i]) {
            return -1;
          } else if (a[astart + i] > b[bstart + i]) {
            return 1;
          }
        }
        // Equal: did one have more leading zeroes?
        if (strict && azeroes != bzeroes) {
          if (azeroes > bzeroes) {
            // a has more leading zeroes: a < b
            return -1;
          } else {
            // b has more leading zeroes: a > b
            return 1;
          }
        }
        // Equal: so continue scanning
      }
    } else if (a[aindex] < b[bindex]) {
      return -1;
    } else if (a[aindex] > b[bindex]) {
      return 1;
    } else {
      aindex++;
      bindex++;
    }
  }

  if (aindex < a.size()) {
    // b is prefix of a
    return 1;
  } else if (bindex < b.size()) {
    // a is prefix of b
    return -1;
  } else {
    // a is equal to b
    return 0;
  }
}

int AutoDigitStrCmpZ(const char* a, const char* b, bool strict) {
  for (;; ++a, ++b) {
    if (absl::ascii_isdigit(*a) && absl::ascii_isdigit(*b)) {
      // Compare runs of digits.  Instead of extracting numbers, we
      // // just skip leading zeroes, and then get the run-lengths.  This
      // allows us to handle arbitrary precision numbers.  We remember
      // how many zeroes we found so that we can differentiate between
      // "1" and "01" in strict mode.
      //
      // Skip leading zeroes, but remember how many we found
      const char* azeros_start = a;
      const char* bzeros_start = b;
      while (*a == '0') ++a;
      while (*b == '0') ++b;
      const char* adigits_start = a;
      const char* bdigits_start = b;
      while (absl::ascii_isdigit(*a)) ++a;
      while (absl::ascii_isdigit(*b)) ++b;
      if (a - adigits_start != b - bdigits_start) {
        if (a - adigits_start < b - bdigits_start) {
          // a has shorter run of digits: so smaller
          return -1;  // less than
        } else {
          // a has longer run of digits: so larger
          return 1;  // greater than
        }
      }
      // same amount of non-zero digits; must compare digit by digit.
      for (ptrdiff_t i = 0; i < a - adigits_start; i++) {
        if (adigits_start[i] != bdigits_start[i]) {
          if (adigits_start[i] < bdigits_start[i]) {
            return -1;  // less than
          } else {
            return 1;  // greater than
          }
        }
      }
      if (strict) {
        // the non-zero digits were equal, so must compare number of zeros.
        if (a - azeros_start != b - bzeros_start) {
          if (a - azeros_start > b - bzeros_start) {
            // a has longer run of digits: so smaller
            return -1;  // less than
          } else {
            // a has shorter run of digits: so larger
            return 1;  // greater than
          }
        }
      }
      // we have now skipped over an identical digit sequence, and are
      // guaranteed that a and b now point at non-digits.  Thus we can fall
      // through to our non-digit code safely.
    }
    if (*a != *b) {
      if (*a < *b) return -1;  // less than
      return 1;                // greater than
    }
    if (*a == 0) return 0;  // equal to
  }
}

int HexDigitsPrefix(const char* buf, ptrdiff_t num_digits) {
  for (ptrdiff_t i = 0; i < num_digits; i++)
    if (!absl::ascii_isxdigit(buf[i]))
      return 0;  // This also detects end of string as '\0' is not xdigit.
  return 1;
}

// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// ItoaKMGT()
//    Description: converts an integer to a string
//    Truncates values to a readable unit: K, M, G or T
//    Opposite of atoi_kmgt()
//    e.g. 100 -> "100" 1500 -> "1500"  4000 -> "3K"   57185920 -> "54M"
//
//    Return value: string
// ----------------------------------------------------------------------
std::string ItoaKMGT(int64_t i) {
  char buffer[kFastToBufferSize + 2];  // 2 = sign plus "KMGT" character.
  char* out = &buffer[0];

  char suffix = '\0';
  uint64_t ui = static_cast<uint64_t>(i);
  if (i < 0) {
    *out++ = '-';
    ui = static_cast<uint64_t>(-i);
  }

  uint64_t val;
  if ((val = (ui >> 40)) > 1) {
    suffix = 'T';
  } else if ((val = (ui >> 30)) > 1) {
    suffix = 'G';
  } else if ((val = (ui >> 20)) > 1) {
    suffix = 'M';
  } else if ((val = (ui >> 10)) > 1) {
    suffix = 'K';
  } else {
    val = ui;
  }

  out = FastUInt64ToBufferLeft(val, out);
  if (suffix) *out++ = suffix;

  return std::string(&buffer[0], out);
}

// Parse the sign and optional hex or oct prefix in text.
inline bool safe_parse_sign_and_base(absl::string_view* text /*inout*/,
                                     int* base_ptr /*inout*/,
                                     bool* negative_ptr /*output*/) {
  if (text->data() == nullptr) {
    return false;
  }

  const char* start = text->data();
  const char* end = start + text->size();
  int base = *base_ptr;

  // Consume whitespace.
  while (start < end && absl::ascii_isspace(start[0])) {
    ++start;
  }
  while (start < end && absl::ascii_isspace(end[-1])) {
    --end;
  }
  if (start >= end) {
    return false;
  }

  // Consume sign.
  *negative_ptr = (start[0] == '-');
  if (*negative_ptr || start[0] == '+') {
    ++start;
    if (start >= end) {
      return false;
    }
  }

  // Consume base-dependent prefix.
  //  base 0: "0x" -> base 16, "0" -> base 8, default -> base 10
  //  base 16: "0x" -> base 16
  // Also validate the base.
  if (base == 0) {
    if (end - start >= 2 && start[0] == '0' &&
        (start[1] == 'x' || start[1] == 'X')) {
      base = 16;
      start += 2;
      if (start >= end) {
        // "0x" with no digits after is invalid.
        return false;
      }
    } else if (end - start >= 1 && start[0] == '0') {
      base = 8;
      start += 1;
    } else {
      base = 10;
    }
  } else if (base == 16) {
    if (end - start >= 2 && start[0] == '0' &&
        (start[1] == 'x' || start[1] == 'X')) {
      start += 2;
      if (start >= end) {
        // "0x" with no digits after is invalid.
        return false;
      }
    }
  } else if (base >= 2 && base <= 36) {
    // okay
  } else {
    return false;
  }
  *text = absl::string_view(start, end - start);
  *base_ptr = base;
  return true;
}

// Consume digits.
//
// The classic loop:
//
//   for each digit
//     value = value * base + digit
//   value *= sign
//
// The classic loop needs overflow checking.  It also fails on the most
// negative integer, -2147483648 in 32-bit two's complement representation.
//
// My improved loop:
//
//  if (!negative)
//    for each digit
//      value = value * base
//      value = value + digit
//  else
//    for each digit
//      value = value * base
//      value = value - digit
//
// Overflow checking becomes simple.

// Lookup tables per IntType:
// vmax/base and vmin/base are precomputed because division costs at least 8ns.
// TODO: Doing this per base instead (i.e. an array of structs, not a
// struct of arrays) would probably be better in terms of d-cache for the most
// commonly used bases.
template <typename IntType>
struct LookupTables {
  static const IntType kVmaxOverBase[];
  static const IntType kVminOverBase[];
};

// An array initializer macro for X/base where base in [0, 36].
// However, note that lookups for base in [0, 1] should never happen because
// base has been validated to be in [2, 36] by safe_parse_sign_and_base().
#define X_OVER_BASE_INITIALIZER(X)                                    \
  {                                                                   \
      0,      0,      X / 2,  X / 3,  X / 4,  X / 5,  X / 6,  X / 7,  \
      X / 8,  X / 9,  X / 10, X / 11, X / 12, X / 13, X / 14, X / 15, \
      X / 16, X / 17, X / 18, X / 19, X / 20, X / 21, X / 22, X / 23, \
      X / 24, X / 25, X / 26, X / 27, X / 28, X / 29, X / 30, X / 31, \
      X / 32, X / 33, X / 34, X / 35, X / 36,                         \
  }

template <typename IntType>
const IntType LookupTables<IntType>::kVmaxOverBase[] =
    X_OVER_BASE_INITIALIZER(std::numeric_limits<IntType>::max());

template <typename IntType>
const IntType LookupTables<IntType>::kVminOverBase[] =
    X_OVER_BASE_INITIALIZER(std::numeric_limits<IntType>::min());

#undef X_OVER_BASE_INITIALIZER

template <typename IntType>
inline bool safe_parse_positive_int(absl::string_view text, int base,
                                    IntType* value_p) {
  IntType value = 0;
  const IntType vmax = std::numeric_limits<IntType>::max();
  assert(vmax > 0);
  assert(base >= 0);
  assert(vmax >= static_cast<IntType>(base));
  const IntType vmax_over_base = LookupTables<IntType>::kVmaxOverBase[base];
  const char* start = text.data();
  const char* end = start + text.size();
  // loop over digits
  for (; start < end; ++start) {
    const unsigned char c = static_cast<unsigned char>(start[0]);
    const int8_t digit = ::strings_internal::kAsciiToInt[c];
    if (digit >= base) {
      *value_p = value;
      return false;
    }
    if (value > vmax_over_base) {
      *value_p = vmax;
      return false;
    }
    value *= base;
    if (value > vmax - digit) {
      *value_p = vmax;
      return false;
    }
    value += digit;
  }
  *value_p = value;
  return true;
}

template <typename IntType>
inline bool safe_uint_internal(absl::string_view text, IntType* value_p,
                               int base) {
  *value_p = 0;
  bool negative;
  if (!safe_parse_sign_and_base(&text, &base, &negative) || negative) {
    return false;
  }
  return safe_parse_positive_int(text, base, value_p);
}

bool safe_strtosize_t_base(absl::string_view text, size_t* value, int base) {
  return safe_uint_internal<size_t>(text, value, base);
}

// ----------------------------------------------------------------------
// u64tostr_base36()
//    Converts unsigned number to string representation in base-36.
// --------------------------------------------------------------------
size_t u64tostr_base36(uint64_t number, size_t buf_size, char* buffer) {
  ABSL_RAW_CHECK(buf_size > 0, "Buffer size must be positive.");
  ABSL_RAW_CHECK(buffer != nullptr, "Output buffer must not be nullptr.");
  static const char kAlphabet[] = "0123456789abcdefghijklmnopqrstuvwxyz";

  buffer[buf_size - 1] = '\0';
  size_t result_size = 1;

  do {
    if (buf_size == result_size) {  // Ran out of space.
      return 0;
    }
    int remainder = number % 36;
    number /= 36;
    buffer[buf_size - result_size - 1] = kAlphabet[remainder];
    result_size++;
  } while (number);

  memmove(buffer, buffer + buf_size - result_size, result_size);

  return result_size - 1;
}

// Use this instead of gmtime_r if you want to build for Windows.
// Windows doesn't have a 'gmtime_r', but it has the similar 'gmtime_s'.
// TODO: Probably belongs in time_support.{cc|h}.
static struct tm* PortableSafeGmtime(const time_t* timep, struct tm* result) {
#ifdef _WIN32
  return gmtime_s(result, timep) == 0 ? result : nullptr;
#else
  return gmtime_r(timep, result);
#endif  // _WIN32
}

char* FastTimeToBuffer(time_t t, char* buffer) {
  if (t == 0) {
    time(&t);
  }

  struct tm tm;
  if (PortableSafeGmtime(&t, &tm) == nullptr) {
    // Error message must fit in 30-char buffer.
    memcpy(buffer, "Invalid:", sizeof("Invalid:"));
    absl::numbers_internal::FastIntToBuffer(t, buffer + strlen(buffer));
    return buffer;
  }

  // strftime format: "%a, %d %b %Y %H:%M:%S GMT",
  // but strftime does locale stuff which we do not want
  // plus strftime takes > 10x the time of hard code

  const char* weekday_name = "Xxx";
  switch (tm.tm_wday) {
    case 0:
      weekday_name = "Sun";
      break;
    case 1:
      weekday_name = "Mon";
      break;
    case 2:
      weekday_name = "Tue";
      break;
    case 3:
      weekday_name = "Wed";
      break;
    case 4:
      weekday_name = "Thu";
      break;
    case 5:
      weekday_name = "Fri";
      break;
    case 6:
      weekday_name = "Sat";
      break;
    default:
      break;
  }

  const char* month_name = "Xxx";
  switch (tm.tm_mon) {
    case 0:
      month_name = "Jan";
      break;
    case 1:
      month_name = "Feb";
      break;
    case 2:
      month_name = "Mar";
      break;
    case 3:
      month_name = "Apr";
      break;
    case 4:
      month_name = "May";
      break;
    case 5:
      month_name = "Jun";
      break;
    case 6:
      month_name = "Jul";
      break;
    case 7:
      month_name = "Aug";
      break;
    case 8:
      month_name = "Sep";
      break;
    case 9:
      month_name = "Oct";
      break;
    case 10:
      month_name = "Nov";
      break;
    case 11:
      month_name = "Dec";
      break;
    default:
      break;
  }

  // Write out the buffer.

  memcpy(buffer + 0, weekday_name, 3);
  buffer[3] = ',';
  buffer[4] = ' ';

  PutTwoDigits(tm.tm_mday, buffer + 5);
  buffer[7] = ' ';

  memcpy(buffer + 8, month_name, 3);
  buffer[11] = ' ';

  int32_t year = tm.tm_year + 1900;
  if (year >= 0 && year <= 9999) {
    PutTwoDigits(year / 100, buffer + 12);
  } else {
    memcpy(buffer, "Invalid:", sizeof("Invalid:"));
    absl::numbers_internal::FastIntToBuffer(t, buffer + strlen(buffer));
    return buffer;
  }
  PutTwoDigits(year % 100, buffer + 14);
  buffer[16] = ' ';

  PutTwoDigits(tm.tm_hour, buffer + 17);
  buffer[19] = ':';

  PutTwoDigits(tm.tm_min, buffer + 20);
  buffer[22] = ':';

  PutTwoDigits(tm.tm_sec, buffer + 23);

  // includes ending NUL
  memcpy(buffer + 25, " GMT", 5);

  return buffer;
}

}  // namespace strings
