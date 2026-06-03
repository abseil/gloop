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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE  // for strptime(3)
#endif

#include "gloop/base/time_support.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include <cstdint>
#include <limits>
#include <string>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"

// Convert a "struct tm" interpreted as *GMT* into a long.  For legacy
// reasons, results that overflow an int are treated as errors yet the
// return value is a int64_t.
//
// Like gmktime() this method returns -1 on failure.  Negative results
// are considered undefined by the standard so these cases are considered
// failures and thus return -1.
int64_t mkgmtime(const struct tm* tm) {
  int64_t result = absl::ToUnixSeconds(absl::FromTM(*tm, absl::UTCTimeZone()));
  if (result < 0 || result > std::numeric_limits<int>::max()) {
    return -1;
  }
  return result;
}

// A time_t version of mkgmtime().  If the result doesn't fit into a time_t,
// or the result is before the unix epoch, the result is -1.
time_t mkgmtime_timet(const struct tm* tm) {
  int64_t result = absl::ToUnixSeconds(absl::FromTM(*tm, absl::UTCTimeZone()));
  if (result < 0 || result > std::numeric_limits<time_t>::max()) {
    return -1;
  }
  return static_cast<time_t>(result);
}

// This is just a wrapper for strptime.  By providing this we can avoid
// defining _GNU_SOURCE in other files.
char* gstrptime(const char* s, const char* format, struct tm* tm) {
  return strptime(s, format, tm);
}

// TODO: This code completely ignores time zones and is in general
// a really bad date parser.  There are like 3 or 4 RFC-format date parsers
// floating around in the Google code base!  So, one day, we should write
// one good one instead of more bad ones, and have everyone use that.
//
// We consider the following date formats:
//    Sun, 06 Nov 1994 08:49:37 GMT    ; RFC 822, updated by RFC 1123
//    Sunday, 06-Nov-94 08:49:37 GMT   ; RFC 850, obsoleted by RFC 1036
//    Sun Nov  6 08:49:37 1994         ; ANSI C's asctime() format
//
bool rfc_strptime(const char* buf, struct tm* tm) {
  memset(tm, 0, sizeof(*tm));
  return strptime(buf, "%a, %e %b %Y %H:%M:%S", tm) ||  // RFC 822/1123
         strptime(buf, "%a, %e-%b-%y %H:%M:%S", tm) ||  // RFC 850/1036
         strptime(buf, "%a %b %e %H:%M:%S %Y", tm) ||   // asctime()
         strptime(buf, "%a, %b %e %H:%M:%S %Y", tm);  // legacy buggy asctime()
  // The last format is an erroneous variation on asctime() format.
  // I've added the correct format, but I'm leaving the incorrect one
  // in place in case legacy code expects it to work (even though its
  // doubtful).
}

namespace {

int64_t DivideRoundUp(int64_t a, int64_t b) {
  DCHECK_GT(b, 0);
  int ret = a / b;
  if (ret * b < a) ++ret;  // add one if the result was rounded down
  return ret;
}

}  // namespace

// Returns the number of days since Nov 24, 4714 BCE on the proleptic Gregorian
// calendar.
int YMDToJulian(int year, int month, int day) {
  if (month < 1 || month > 12) {
    LOG(DFATAL) << "month out of range: " << month;
    return 0;
  }

  // Month-to-day offset for non-leap-years.
  static const int month_day[12] = {0,   31,  59,  90,  120, 151,
                                    181, 212, 243, 273, 304, 334};

  // Leap year rules: every 4, except 100, except 400.
  int febs_since_0 = month > 2 ? year + 1 : year;
  int since_0 = 365 * year + month_day[month - 1] + (day - 1) +
                DivideRoundUp(febs_since_0, 4) -
                DivideRoundUp(febs_since_0, 100) +
                DivideRoundUp(febs_since_0, 400);

  // Convert from 0-epoch (0001-01-01 BC) to Google Epoch (4714-11-24 BC).
  // Since the "BC" system does not have a year zero, 1 BC == year zero.
  return since_0 + 1721060;
}

// Translate a Julian date back into normal YMD date.  Algorithm is from:
// Fliegel, H. F., and Van Flandern, T. C., "A Machine Algorithm for
//   Processing Calendar Dates," Communications of the Association of
//   Computing Machines, vol. 11 (1968), p. 657
void JulianToYMD(int JD, int* y, int* m, int* d) {
  int L = JD + 68569;
  int N = 4 * L / 146097;
  L = L - (146097 * N + 3) / 4;
  int I = 4000 * (L + 1) / 1461001;
  L = L - (1461 * I / 4) + 31;
  int J = 80 * L / 2447;
  int K = L - 2447 * J / 80;
  L = J / 11;
  J = J + 2 - 12 * L;
  I = 100 * (N - 49) + I + L;
  *y = I;
  *m = J;
  *d = K;
}

namespace {
// Julian date of Jan 1, 1970: the start of the epoch.
const int kEpochBegin = 2440588;
}  // namespace

int64_t JulianToSeconds(int julian) { return 86400LL * (julian - kEpochBegin); }

int SecondsToJulian(int64_t seconds) {
  return static_cast<int>(seconds / 86400LL) + kEpochBegin;
}

time_t YMDToSeconds(int year, int month, int day) {
  return YMDHMSToSeconds(year, month, day, 0, 0, 0);
}

time_t YMDHMSToSeconds(int year, int month, int day, int hour, int minute,
                       int second) {
  int64_t result = absl::ToUnixSeconds(
      absl::FromCivil(absl::CivilSecond(year, month, day, hour, minute, second),
                      absl::UTCTimeZone()));
  if (result < 0 || result > std::numeric_limits<int>::max()) {
    return -1;
  }
  return static_cast<time_t>(result);
}

time_t YMDToTimet(int year, int month, int day) {
  return YMDHMSToTimet(year, month, day, 0, 0, 0);
}

time_t YMDHMSToTimet(int year, int month, int day, int hour, int minute,
                     int second) {
  int64_t result = absl::ToUnixSeconds(
      absl::FromCivil(absl::CivilSecond(year, month, day, hour, minute, second),
                      absl::UTCTimeZone()));
  if (result < 0 || result > std::numeric_limits<time_t>::max()) {
    return -1;
  }
  return static_cast<time_t>(result);
}

bool IfLeapYear(int year) {
  return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

// Return true if this day actually exists.
bool IfDayExists(int y, int m, int d) {
  return d >= 1 && d <= GetDaysInMonth(y, m);
}

// Returns the number of days in the given month, or 0 if month is invalid.
int GetDaysInMonth(int y, int m) {
  static const int days_in_month[12] = {31, 28, 31, 30, 31, 30,
                                        31, 31, 30, 31, 30, 31};
  if (m < 1 || m > 12) return 0;
  if (m == 2 && IfLeapYear(y)) return 29;
  return days_in_month[m - 1];
}

// Converts a daterange of the form <from_date>..<to_date> where each
// date is in ISO 8601 format (e.g., 2006-05-23) to its Julian counterpart.
// "julian_start" and "julian_end" are out parameters which will contain
// the Julian dates.  If the daterange is of the form "..<to_date>",
// *julian_start will be set to INT_MIN, and if daterange is of the form
// "<from_date>..", *julian_end will be set to INT_MAX.
// Returns true if the input daterange is a legitimate date range.
bool Iso8601DateRangeToJulian(const char* daterange, int* julian_start,
                              int* julian_end) {
  int y, m, d;
  char c;

  const char* tmp = strstr(daterange, "..");
  if (tmp == nullptr) return false;
  const char* end_date = tmp + 2;

  if (tmp == daterange) {  // we have an open-ended start date
    *julian_start = INT_MIN;
  } else {
    if (sscanf(daterange, "%d-%d-%d", &y, &m, &d) != 3) {
      return false;
    }
    if (y < -5000000 || y > 5000000) {
      return false;
    }
    *julian_start = YMDToJulian(y, m, d);
    if (!IfDayExists(y, m, d)) {
      return false;
    }
  }
  if (*end_date == '\0') {  // we have an open-ended end date
    *julian_end = INT_MAX;
  } else {
    if (sscanf(end_date, "%d-%d-%d%c", &y, &m, &d, &c) != 3) {
      return false;
    }
    if (y < -5000000 || y > 5000000) {
      return false;
    }
    *julian_end = YMDToJulian(y, m, d);
    if (!IfDayExists(y, m, d)) {
      return false;
    }
  }

  if (*julian_start > *julian_end) {
    return false;
  }
  return true;
}

// Converts a date in ISO 8601 format (e.g., 2006-05-23) to its Julian
// counterpart.  "julian_date" is an out parameter which will contain
// the converted date in Julian.
// Returns true if the input date is a legitimate date.
bool Iso8601DateToJulian(const char* date, int* julian_date) {
  int y, m, d;
  char c;
  if (sscanf(date, "%d-%d-%d%c", &y, &m, &d, &c) != 3) {
    return false;
  }
  if (y < -5000000 || y > 5000000) {
    return false;
  }
  if (!IfDayExists(y, m, d)) {
    return false;
  }
  *julian_date = YMDToJulian(y, m, d);
  return true;
}

// Given a number of seconds representing a time interval, return a string
// of the form [-]d:hh:mm:ss, where d is the number of days, hh the number of
// hours, mm the number of minutes, and ss the number of seconds.
std::string TimeIntervalString(int interval) {
  unsigned i = interval;
  const char* sign = "";
  if (interval < 0) {
    i = -(interval + 1);
    ++i;
    sign = "-";
  }

  int s = i % 60;
  i /= 60;
  int m = i % 60;
  i /= 60;
  int h = i % 24;
  int d = i / 24;

  return absl::StrFormat("%s%d:%02d:%02d:%02d", sign, d, h, m, s);
}

// Given a number of seconds representing a time interval, return a string
// of the form [-]d:hh:mm:ss, where d is the number of days, hh the number of
// hours, mm the number of minutes, and ss the number of seconds.  Display at
// least num_fields fields, omitting most significant ones if they're zero.
std::string TimeIntervalString(int interval, int num_fields) {
  unsigned i = interval;
  const char* sign = "";
  if (interval < 0) {
    i = -(interval + 1);
    ++i;
    sign = "-";
  }

  int s = i % 60;
  i /= 60;
  int m = i % 60;
  i /= 60;
  int h = i % 24;
  int d = i / 24;

  if (d || num_fields >= 4) {
    return absl::StrFormat("%s%d:%02d:%02d:%02d", sign, d, h, m, s);
  } else if (h || num_fields >= 3) {
    return absl::StrFormat("%s%d:%02d:%02d", sign, h, m, s);
  } else if (m || num_fields >= 2) {
    return absl::StrFormat("%s%d:%02d", sign, m, s);
  } else {
    return absl::StrFormat("%s%d", sign, s);
  }
}

// Same as TimeIntervalString except that the time is given in units of
// 10^-power seconds.  The returned string will have the form [-]d:hh:mm:ss.f,
// with d, hh, and mm optionally omitted if num_fields and the value are both
// small enough.  power specifies the number of fractional second digits f,
// which are always present even if they contain trailing zeroes.  power may
// range between 0 and 9.
std::string TimeIntervalStringSubsecond(int64_t interval, int num_fields,
                                        int power) {
  CHECK(power >= 0 && power <= 9);
  static const unsigned kPowersOf10[] = {
      1,      10,      100,      1000,      10000,
      100000, 1000000, 10000000, 100000000, 1000000000};
  unsigned power10 = kPowersOf10[power];

  uint64_t i = interval;
  std::string result;
  if (interval < 0) {
    i = -(interval + 1);
    ++i;
    result = "-";
  }

  unsigned fraction = i % power10;
  i /= power10;
  unsigned s = i % 60;
  i /= 60;
  unsigned m = i % 60;
  i /= 60;
  unsigned h = i % 24;
  uint64_t d = i / 24;

  if (d || num_fields >= 4) {
    absl::StrAppendFormat(&result, "%u:%02u:%02u:%02u", d, h, m, s);
  } else if (h || num_fields >= 3) {
    absl::StrAppendFormat(&result, "%u:%02u:%02u", h, m, s);
  } else if (m || num_fields >= 2) {
    absl::StrAppendFormat(&result, "%u:%02u", m, s);
  } else {
    absl::StrAppendFormat(&result, "%u", s);
  }
  if (power) {
    absl::StrAppendFormat(&result, ".%0*u", power, fraction);
  }
  return result;
}

#define PLURAL(i) (i == 1 ? "" : "s")

std::string MakeIntervalString(int interval) {
  if (interval < 60) {
    return absl::StrFormat("%d sec%s", interval, PLURAL(interval));
  }

  int r = interval % 60;
  interval = interval / 60;
  if (interval < 60) {
    return absl::StrFormat("%d min%s %d sec%s", interval, PLURAL(interval), r,
                           PLURAL(r));
  }

  r = interval % 60;
  interval = interval / 60;
  if (interval < 24) {
    return absl::StrFormat("%d hour%s %d min%s", interval, PLURAL(interval), r,
                           PLURAL(r));
  }

  r = interval % 24;
  interval = interval / 24;
  return absl::StrFormat("%d day%s %d hour%s", interval, PLURAL(interval), r,
                         PLURAL(r));
}

#undef PLURAL
