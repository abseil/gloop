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

#include "gloop/strings/human_readable.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <system_error>  // NOLINT(build/c++11)

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/log/check.h"
#include "absl/numeric/int128.h"
#include "absl/strings/ascii.h"
#include "absl/strings/charconv.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "gloop/strings/si_prefix.h"

namespace strings {

namespace {

template <typename T>
const char* GetNegStr(T* value) {
  if (*value < 0) {
    *value = -(*value);
    return "-";
  } else {
    return "";
  }
}

}  // namespace

bool HumanReadableNumBytes::LessThan(absl::string_view a, absl::string_view b) {
  int64_t a_bytes, b_bytes;
  if (!HumanReadableNumBytes::ToInt64(a, &a_bytes)) a_bytes = 0;
  if (!HumanReadableNumBytes::ToInt64(b, &b_bytes)) b_bytes = 0;
  return (a_bytes < b_bytes);
}

bool HumanReadableNumBytes::ToInt64(absl::string_view str, int64_t* num_bytes) {
  if (str.empty()) {
    // TODO: this is a bug IMO, but we keep this logic to be bug to
    // bug compatible.
    //
    *num_bytes = 0;
    return true;
  }

  int64_t scale = 1;
  switch (str.back()) {
    // NB: an int64 can only go up to <8 EB.
    case 'E':
      scale <<= 10;
      ABSL_FALLTHROUGH_INTENDED;
    case 'P':
      scale <<= 10;
      ABSL_FALLTHROUGH_INTENDED;
    case 'T':
      scale <<= 10;
      ABSL_FALLTHROUGH_INTENDED;
    case 'G':
      scale <<= 10;
      ABSL_FALLTHROUGH_INTENDED;
    case 'M':
      scale <<= 10;
      ABSL_FALLTHROUGH_INTENDED;
    case 'K':
    case 'k':
      scale <<= 10;
      ABSL_FALLTHROUGH_INTENDED;
    case 'B':
      str.remove_suffix(1);
      break;
    default:
      break;
  }
  double d = 0;
  if (!absl::SimpleAtod(str, &d)) return false;

  // NaN can't be represented as an integer.
  if (std::isnan(d)) {
    return false;
  }

  d *= scale;
  if (d > 0) {
    // Note that numeric_limits<int64>::max() will not round trip correctly when
    // converted to double and back, as it will be rounded up and the conversion
    // back to int64 will overflow.  However, numeric_limits<int64>::min() is a
    // power of 2 and thus exactly representable, and does not have this issue.
    if (d >= static_cast<double>(std::numeric_limits<int64_t>::max()))
      return false;
    *num_bytes = static_cast<int64_t>(d + 0.5);
  } else {
    if (d < static_cast<double>(std::numeric_limits<int64_t>::min()))
      return false;
    *num_bytes = static_cast<int64_t>(d - 0.5);
  }
  return true;
}

bool HumanReadableNumBytes::ToDouble(absl::string_view str, double* num_bytes) {
  if (str.empty()) return false;

  double scale = 1;
  switch (str.back()) {
    case 'Y':
      scale *= 1024;  // That's a yotta bytes!
      ABSL_FALLTHROUGH_INTENDED;
    case 'Z':
      scale *= 1024;
      ABSL_FALLTHROUGH_INTENDED;
    case 'E':
      scale *= 1024;
      ABSL_FALLTHROUGH_INTENDED;
    case 'P':
      scale *= 1024;
      ABSL_FALLTHROUGH_INTENDED;
    case 'T':
      scale *= 1024;
      ABSL_FALLTHROUGH_INTENDED;
    case 'G':
      scale *= 1024;
      ABSL_FALLTHROUGH_INTENDED;
    case 'M':
      scale *= 1024;
      ABSL_FALLTHROUGH_INTENDED;
    case 'K':
    case 'k':
      scale *= 1024;
      ABSL_FALLTHROUGH_INTENDED;
    case 'B':
      str.remove_suffix(1);
      break;
    default:
      break;  // to here.
  }

  // If this didn't consume the entire string, fail.
  if (!absl::SimpleAtod(str, num_bytes)) return false;

  *num_bytes *= scale;
  return true;
}

std::string HumanReadableNumBytes::DoubleToString(double num_bytes) {
  const char* neg_str = GetNegStr(&num_bytes);
  static const char units[] = "BKMGTPEZY";
  double scaled = num_bytes;
  int i = 0;
  for (; i < strlen(units) && scaled >= 1024.0; ++i) {
    scaled /= 1024.0;
  }
  if (i == strlen(units)) {
    return absl::StrFormat("%s%g", neg_str, num_bytes);
  } else {
    return absl::StrFormat("%s%.2f%c", neg_str, scaled, units[i]);
  }
}

std::string HumanReadableNumBytes::Int128ToString(absl::int128 num_bytes) {
  if (num_bytes == std::numeric_limits<absl::int128>::min()) {
    // Special case for number whose absolute value is out of range.
    return "-140737488355328Y";
  }
  const char* neg_str = GetNegStr(&num_bytes);

  // Special case for bytes.
  if (num_bytes < 1024) {
    // No fractions for bytes.
    return absl::StrFormat("%s%dB", neg_str, num_bytes);
  }

  static const char units[] = "KMGTPEZY";
  const char* unit = units;
  double scaled(num_bytes);
  scaled /= 1024.0;
  while (scaled >= 1024.0 && unit[1] != 0) {
    scaled /= 1024.0;
    ++unit;
  }

  return *unit == 'K' ? absl::StrFormat("%s%.1f%c", neg_str, scaled, *unit)
                      : absl::StrFormat("%s%.2f%c", neg_str, scaled, *unit);
}

std::string HumanReadableNumBytes::ToString(int64_t num_bytes) {
  if (num_bytes == std::numeric_limits<int64_t>::min()) {
    // Special case for number whose absolute value is out of range.
    return "-8E";
  }

  const char* neg_str = GetNegStr(&num_bytes);

  // Special case for bytes.
  if (num_bytes < int64_t{1024}) {
    // No fractions for bytes.
    return absl::StrFormat("%s%dB", neg_str, num_bytes);
  }

  static const char units[] = "KMGTPE";  // int64 only goes up to E.
  const char* unit = units;
  while (num_bytes >= int64_t{1024} * int64_t{1024}) {
    num_bytes /= int64_t{1024};
    ++unit;
    CHECK(unit < units + ABSL_ARRAYSIZE(units));
  }

  return absl::StrFormat("%s%.*f%c", neg_str, (*unit == 'K') ? 1 : 2,
                         num_bytes / 1024.0, *unit);
}

std::string HumanReadableNumBytes::ToStringWithoutRounding(int64_t num_bytes) {
  if (num_bytes == std::numeric_limits<int64_t>::min()) {
    // Special case for number whose absolute value is out of range.
    return "-8E";
  }

  absl::string_view neg_str;
  if (num_bytes < 0) {
    num_bytes = -num_bytes;
    neg_str = "-";
  }

  static const char units[] = "BKMGTPE";  // int64 only goes up to E.

  int64_t num_units = num_bytes;
  int unit_type = 0;
  for (; unit_type < ABSL_ARRAYSIZE(units); unit_type++) {
    if (num_units % 1024 != 0) {
      // Not divisible by the next unit.
      break;
    }

    int64_t next_units = num_units >> 10;
    if (next_units == 0) {
      // Less than the next unit.
      break;
    }

    num_units = next_units;
  }
  return absl::StrCat(neg_str, num_units,
                      absl::string_view(units + unit_type, 1));
}

std::string HumanReadableInt::ToString(int64_t value) {
  std::string s;
  uint64_t v = value;
  if (value < 0) {
    v = 0 - v;  // MSVC 2013 errors on unary negation of unsigned.
    s += '-';
  }
  if (v < int64_t{1000}) {
    absl::StrAppendFormat(&s, "%d", v);
  } else if (v >= int64_t{1000000000000000}) {
    // Number bigger than 1E15; use that notation.
    absl::StrAppendFormat(&s, "%0.3G", static_cast<double>(v));
  } else {
    static const char units[] = "kMBT";
    const char* unit = units;
    while (v >= int64_t{1000000}) {
      v /= int64_t{1000};
      ++unit;
      CHECK(unit < units + ABSL_ARRAYSIZE(units));
    }
    absl::StrAppendFormat(&s, "%.2f%c", v / 1000.0, *unit);
  }
  return s;
}

std::string HumanReadableInt::Int128ToString(absl::int128 value) {
  if (value >= int64_t{1000000000000000} ||
      value <= int64_t{-1000000000000000}) {
    // Number bigger than 1E15; use that notation.
    return absl::StrFormat("%0.3G", static_cast<double>(value));
  }
  return ToString(static_cast<int64_t>(value));
}

std::string HumanReadableNum::ToString(int64_t value) {
  return HumanReadableInt::ToString(value);
}

std::string HumanReadableNum::DoubleToString(double value) {
  std::string s;
  if (value < 0) {
    s += '-';
    value = -value;
  }
  if (value < 1.0) {
    absl::StrAppendFormat(&s, "%.3f", value);
  } else if (value < 10) {
    absl::StrAppendFormat(&s, "%.2f", value);
  } else if (value < 1e2) {
    absl::StrAppendFormat(&s, "%.1f", value);
  } else if (value < 1e3) {
    absl::StrAppendFormat(&s, "%.0f", value);
  } else if (value >= 1e15) {
    // Number bigger than 1E15; use that notation.
    absl::StrAppendFormat(&s, "%0.3G", value);
  } else {
    static const char units[] = "kMBT";
    const char* unit = units;
    while (value >= 1e6) {
      value /= 1e3;
      ++unit;
      CHECK(unit < units + ABSL_ARRAYSIZE(units));
    }
    absl::StrAppendFormat(&s, "%.2f%c", value / 1000.0, *unit);
  }
  return s;
}

// See <link> for source of metric prefixes.
bool HumanReadableNum::ToDouble(absl::string_view str, double* value) {
  if (str.empty()) return false;

  double scale = 1;
  switch (str.back()) {
    case 'K':
    case 'k':
      scale = 1e3;
      str.remove_suffix(1);
      break;
    case 'M':
      scale = 1e6;
      str.remove_suffix(1);
      break;
    case 'B':
    case 'G':
      scale = 1e9;
      str.remove_suffix(1);
      break;
    case 'T':
      scale = 1e12;
      str.remove_suffix(1);
      break;
    default:
      break;
  }

  // If this didn't consume the entire string, fail.
  if (!absl::SimpleAtod(str, value)) return false;

  *value *= scale;
  return true;
}

// See <link> for source of metric prefixes.
bool HumanReadableInt::ToInt64(absl::string_view str, int64_t* value) {
  if (str.empty()) return false;

  double scale = 1;
  switch (str.back()) {
    case 'k':
      scale = 1e3;
      str.remove_suffix(1);
      break;
    case 'M':
      scale = 1e6;
      str.remove_suffix(1);
      break;
    case 'B':
    case 'G':
      scale = 1e9;
      str.remove_suffix(1);
      break;
    case 'T':
      scale = 1e12;
      str.remove_suffix(1);
      break;
    default:
      break;
  }

  double d;
  // If this didn't consume the entire string, fail.
  if (!absl::SimpleAtod(str, &d)) return false;

  // NaN can't be represented as an integer.
  if (std::isnan(d)) {
    return false;
  }

  d *= scale;

  // Note that numeric_limits<int64>::max() will not round trip correctly when
  // converted to double and back, as it will be rounded up and the conversion
  // back to int64 will overflow.  However, numeric_limits<int64>::min() is a
  // power of 2 and thus exactly representable, and does not have this issue.
  if (d >= static_cast<double>(std::numeric_limits<int64_t>::max()) ||
      d < static_cast<double>(std::numeric_limits<int64_t>::min()))
    return false;

  *value = static_cast<int64_t>(d < 0 ? d - 0.5 : d + 0.5);
  return true;
}

// Abbreviations used here are acceptable English abbreviations
// without the ending period (".") for brevity, except for uncommon
// abbreviations, in which case the entire word is spelled out. ("mo"
// and "mos" are not good abbreviations for "months" -- with or
// without the period). If needed, one can add a
// HumanReadableTime::ToStringShort() for shorter abbreviations or one
// for always spelling out the unit, HumanReadableTime::ToStringLong().
std::string HumanReadableElapsedTime::ToShortString(double seconds) {
  std::string human_readable;

  if (seconds < 0) {
    human_readable = "-";
    seconds = -seconds;
  }

  // Start with us and keep going up to years.
  // The comparisons must account for rounding to prevent the format breaking
  // the tested condition and returning, e.g., "1e+03 us" instead of "1 ms".
  const double microseconds = seconds * 1.0e6;
  if (microseconds < 999.5) {
    absl::StrAppendFormat(&human_readable, "%0.3g us", microseconds);
    return human_readable;
  }
  double milliseconds = seconds * 1e3;
  if (milliseconds >= .995 && milliseconds < 1) {
    // Round half to even in StringAppendF would convert this to 0.999 ms.
    milliseconds = 1.0;
  }
  if (milliseconds < 999.5) {
    absl::StrAppendFormat(&human_readable, "%0.3g ms", milliseconds);
    return human_readable;
  }

  constexpr double kSecondsInMinute = 60.0;
  if (seconds < kSecondsInMinute) {
    absl::StrAppendFormat(&human_readable, "%0.3g s", seconds);
    return human_readable;
  }
  seconds /= kSecondsInMinute;

  constexpr double kMinutesInHour = 60.0;
  if (seconds < kMinutesInHour) {
    absl::StrAppendFormat(&human_readable, "%0.3g min", seconds);
    return human_readable;
  }
  seconds /= kMinutesInHour;

  constexpr double kHoursInDay = 24.0;
  if (seconds < kHoursInDay) {
    absl::StrAppendFormat(&human_readable, "%0.3g h", seconds);
    return human_readable;
  }
  seconds /= kHoursInDay;

  constexpr double kDaysInMonth = 30.436875;
  if (seconds < kDaysInMonth) {
    absl::StrAppendFormat(&human_readable, "%0.3g days", seconds);
    return human_readable;
  }
  constexpr double kDaysInYear = 365.2425;
  if (seconds < kDaysInYear) {
    absl::StrAppendFormat(&human_readable, "%0.3g months",
                          seconds / kDaysInMonth);
    return human_readable;
  }
  seconds /= kDaysInYear;
  absl::StrAppendFormat(&human_readable, "%0.3g years", seconds);
  return human_readable;
}

bool HumanReadableElapsedTime::ToDouble(absl::string_view str, double* value) {
  struct TimeUnits {
    const char* unit;  // unit name
    double seconds;    // number of seconds in that unit (minutes => 60)
  };

  // These must be sorted in decreasing length.  In particulary, a
  // string must exist before and of its substrings or the substring
  // will match;
  static const TimeUnits kUnits[] = {
      // Long forms
      {"microsecond", 0.000001},
      {"millisecond", 0.001},
      {"second", 1.0},
      {"minute", 60.0},
      {"hour", 3600.0},
      {"day", 86400.0},
      {"week", 7 * 86400.0},
      {"month", 30 * 86400.0},
      {"year", 365 * 86400.0},

      // Abbreviated forms
      {"microsec", 0.000001},
      {"millisec", 0.001},
      {"sec", 1.0},
      {"min", 60.0},
      {"hr", 3600.0},
      {"dy", 86400.0},
      {"wk", 7 * 86400.0},
      {"mon", 30 * 86400.0},
      {"yr", 365 * 86400.0},

      // micro -> u
      {"usecond", 0.000001},
      {"usec", 0.000001},
      // milli -> m
      {"msecond", 0.001},
      {"msec", 0.001},

      // Ultra-short form
      {"us", 0.000001},
      {"ms", 0.001},
      {"s", 1.0},
      {"m", 60.0},
      {"h", 3600.0},
      {"d", 86400.0},
      {"w", 7 * 86400.0},
      {"M", 30 * 86400.0},  // upper-case M to disambiguate with minute
      {"y", 365 * 86400.0}};

  str = absl::StripLeadingAsciiWhitespace(str);
  if (str.empty()) {
    return false;
  }

  int sign = 1;
  if (absl::ConsumePrefix(&str, "-")) {
    sign = -1;
    str = absl::StripLeadingAsciiWhitespace(str);
  } else if (absl::ConsumePrefix(&str, "+")) {
    str = absl::StripLeadingAsciiWhitespace(str);
  }
  if (str.empty()) {
    // Empty string and strings with just a sign are illegal.
    return false;
  }

  double work_value = 0;
  do {
    // Leading signs on individual values are not allowed.
    if (str.front() == '-' || str.front() == '+') {
      return false;
    }
    double factor = 0;
    auto res = absl::from_chars(str.data(), str.data() + str.size(), factor);
    if (res.ec != std::errc() || res.ptr == str.data()) {
      // Illegally formatted value, no values consumed by from_chars.
      return false;
    }
    str.remove_prefix(res.ptr - str.data());
    str = absl::StripLeadingAsciiWhitespace(str);
    bool found_unit = false;
    for (int i = 0; !found_unit && i < ABSL_ARRAYSIZE(kUnits); ++i) {
      absl::string_view unit_sv(kUnits[i].unit);
      if (absl::ConsumePrefix(&str, unit_sv)) {
        work_value += factor * kUnits[i].seconds;
        // Allowing pluralization of any unit (except empty string)
        if (!unit_sv.empty()) {
          absl::ConsumePrefix(&str, "s");
        }
        found_unit = true;
      }
    }
    if (!found_unit) {
      return false;
    }
    str = absl::StripLeadingAsciiWhitespace(str);
  } while (!str.empty());

  *value = sign * work_value;
  return true;
}

namespace {

std::string PositiveTimeString(absl::Duration duration) {
  CHECK_GE(duration, absl::ZeroDuration());

  // TODO: Use CivilTime for more accurate time conversions.
  static const absl::Duration kOneDay = absl::Hours(24);
  // The Gregorian calendar has 97 leap days every 400 years.
  static const absl::Duration kOneYear = (365 + 97.0 / 400.0) * kOneDay;
  static const absl::Duration kOneMonth = kOneYear / 12.0;

  if (duration > kOneYear) {
    return absl::StrFormat("%.1f yrs", absl::FDivDuration(duration, kOneYear));
  } else if (duration > kOneMonth) {
    const int64_t months = absl::IDivDuration(duration, kOneMonth, &duration);
    const int64_t days = absl::IDivDuration(duration, kOneDay, &duration);
    return absl::StrFormat("%d %s and %d %s", months,
                           (months == 1 ? "month" : "months"), days,
                           (days == 1 ? "day" : "days"));
  } else if (duration > kOneDay) {
    const int64_t days = absl::IDivDuration(duration, kOneDay, &duration);
    const int64_t hours =
        absl::IDivDuration(duration, absl::Hours(1), &duration);
    return absl::StrFormat("%d %s %d %s", days, (days == 1 ? "day" : "days"),
                           hours, (hours == 1 ? "hour" : "hours"));
  } else if (duration > absl::Hours(1)) {
    const int64_t hours =
        absl::IDivDuration(duration, absl::Hours(1), &duration);
    const int64_t minutes =
        absl::IDivDuration(duration, absl::Minutes(1), &duration);
    return absl::StrFormat("%d %s %d min", hours,
                           (hours == 1 ? "hour" : "hours"), minutes);
  } else if (duration > absl::Minutes(1)) {
    const int64_t minutes =
        absl::IDivDuration(duration, absl::Minutes(1), &duration);
    const int64_t seconds =
        absl::IDivDuration(duration, absl::Seconds(1), &duration);
    return absl::StrFormat("%d min %d sec", minutes, seconds);
  } else if (duration >= absl::Seconds(1)) {
    const int64_t seconds =
        absl::IDivDuration(duration, absl::Seconds(1), &duration);
    return absl::StrFormat("%d sec", seconds);
  } else {
    return absl::StrCat(strings::si_prefix::ToDecimalStringFullySpecified(
                            absl::ToDoubleSeconds(duration), 1.0, 1),
                        "sec");
  }
}

std::string ThenString(absl::Time now, absl::Time then,
                       absl::TimeZone timezone) {
  absl::CivilDay now_date = absl::ToCivilDay(now, timezone);
  absl::CivilDay then_date = absl::ToCivilDay(then, timezone);
  if (then_date == now_date) {
    return absl::FormatTime("today at %H:%M", then, timezone);
  }
  if (then_date > now_date) {
    if (then_date == now_date + 1) {
      return absl::FormatTime("tomorrow at %H:%M", then, timezone);
    }
  } else {
    if (then_date == now_date - 1) {
      return absl::FormatTime("yesterday at %H:%M", then, timezone);
    }
  }
  if (then_date.year() == now_date.year()) {
    return absl::FormatTime("%B %d at %H:%M", then, timezone);
  }
  return absl::FormatTime("%B %d, %Y at %H:%M", then, timezone);
}

}  // namespace

std::string DeltaTimeString(absl::Duration duration) {
  return (duration < absl::ZeroDuration()
              ? absl::StrCat("-", PositiveTimeString(-duration))
              : PositiveTimeString(duration));
}

std::string ElapsedTimeString(absl::Duration duration) {
  return (duration < absl::ZeroDuration()
              ? PositiveTimeString(-duration) + " ago"
              : "in " + PositiveTimeString(duration));
}

std::string NowAndThen(absl::Time now, absl::Time then,
                       absl::TimeZone timezone) {
  return absl::StrCat(ThenString(now, then, timezone), " (",
                      ElapsedTimeString(then - now), ")");
}

}  // namespace strings
