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

// All of the functions in this file are thread-safe.

#ifndef THIRD_PARTY_GLOOP_BASE_TIME_SUPPORT_H_
#define THIRD_PARTY_GLOOP_BASE_TIME_SUPPORT_H_
#include <sys/types.h>
#include <time.h>

#include <cstdint>
#include <string>

#include "absl/time/clock.h"  // IWYU pragma: keep

// Avoid these time constants in favor of the type-safe libraries
// https://github.com/abseil/abseil-cpp/tree/master/absl/time/time.h and
// https://github.com/abseil/abseil-cpp/tree/master/absl/time/civil_time.h
const int kNumSecondsPerMinute = 60;
const int kNumSecondsPerHour = kNumSecondsPerMinute * 60;
const int kNumSecondsPerDay = kNumSecondsPerHour * 24;
const int kNumSecondsPerWeek = kNumSecondsPerDay * 7;

const int64_t kNumMillisPerSecond = 1000LL;
const int64_t kNumMillisPerMinute = 60LL * kNumMillisPerSecond;
const int64_t kNumMillisPerHour = 60LL * kNumMillisPerMinute;
const int64_t kNumMillisPerDay = 24LL * kNumMillisPerHour;

const int64_t kNumMicrosPerMilli = 1000LL;
const int64_t kNumMicrosPerSecond = kNumMillisPerSecond * 1000LL;
const int64_t kNumMicrosPerMinute = kNumMillisPerMinute * 1000LL;
const int64_t kNumMicrosPerHour = kNumMillisPerHour * 1000LL;
const int64_t kNumMicrosPerDay = kNumMillisPerDay * 1000LL;

// This is exactly like mktime() except it is guaranteed to return -1 on
// failure.  Some versions of glibc allow mktime() to return negative
// values which the standard says are undefined.  See the standard at
// http://pubs.opengroup.org/onlinepubs/009696799/basedefs/xbd_chap04.html
// under the heading "Seconds Since the Epoch".
inline time_t gmktime(struct tm* tm) {
  time_t t = mktime(tm);
  return t < 0 ? time_t(-1) : t;
}

// Like mktime(), but interprets the fields as GMT rather than local.
// That is, the inverse of gmtime().  Unlike mktime(), the input value
// is not modified (in fact it is const), but it need not be normalized.
// Like mktime() this will return -1 on failure or if the result is invalid.
//
// Almost certainly you want to use mkgmtime_timet() rather than this
// function.
int64_t mkgmtime(const struct tm* tm);

// Like mkgmtime(), but returns time_t, which can represent a wider range
// of times, depending on sizeof(long).  Will return -1 on failure or if
// the result is invalid.
time_t mkgmtime_timet(const struct tm* tm);

// A simple wrapper for strptime().  By doing this we can avoid defining
// _GNU_SOURCE in other files.  Only touches fields specified by format.
char* gstrptime(const char* s, const char* format, struct tm* tm);

// Tries various RFC formats to convert a buffer to a struct tm.
//
// WARNING WARNING WARNING -- this function only handles a few cases
// and completely ignores time zones!  If you do use it, be aware that
// it may fail on perfectly legal dates.
bool rfc_strptime(const char* buf, struct tm* tm);

// For historical reasons, several functions in this file refer to a "Julian"
// date.  However, these functions do not use the same epoch as any of the
// several other "Julian Date" systems.  The epoch of the "Googlian Date" is
// midnight, November 24, 4714 BCE, which is day 0.
//
// True Julian Dates, as used in astronomy, have their epoch at *noon*,
// November 24, 4714 BCE, which is January 1, 4713 BCE in the proleptic Julian
// calendar.  Also commonly used is the Modified Julian Date system, which
// starts at *midnight*, November 17, 1858 CE (Gregorian), or 2400000.5 days
// later than the Julian Date epoch.  This offset was chosen so contemporary
// dates would fit in an 18-bit half-word on an IBM 704.
//
// Julian Days are generally given as real numbers.  The integral number
// of days since the epoch is referred to as the Julian Day Number.  The
// functions in this file work with what might be called a Googlian Date
// Number, equal to the Modified Julian Day Number plus 2400000.

// Converts from the Gregorian civil calendar to a continuous counter of days.
// See note above about the epoch.
//
// The year must be a full year (eg. 2010) in the range -5000000..5000000.
// Zero and negative years are fine with 1 = 1 CE, 0 = 1 BCE, -1 = 2 BCE, etc.
//
// REQUIRES: month is in the range 1..12
// REQUIRES: year is in the range -5000000..5000000
int YMDToJulian(int year, int month, int day);

// Translate a Julian date back into normal YMD date.
void JulianToYMD(int JD, int* y, int* m, int* d);

// Convert a Julian date to seconds since the epoch
int64_t JulianToSeconds(int julian);

// Convert seconds since the epoch to Julian date
int SecondsToJulian(int64_t seconds);

// Convert a YMD date to seconds since the epoch.  The time is taken to be
// midnight (i.e. the beginning of the day) in GMT.  Month and day are 1-based.
time_t YMDToSeconds(int y, int m, int d);

// Convert a YMDHMS time to seconds since the epoch.  The time is taken to be
// in GMT.  Month and day are 1-based.
time_t YMDHMSToSeconds(int year, int month, int day, int hour, int minute,
                       int second);

// Convert a YMD date to seconds since the epoch.  The time is taken to be
// midnight (i.e. the beginning of the day) in GMT.  Month and day are 1-based.
// A time_t version of YMDToSeconds.
time_t YMDToTimet(int y, int m, int d);

// Convert a YMDHMS time to seconds since the epoch.  The time is taken to be
// in GMT. Month and day are 1-based.  A time_t version of YMDHMSToSeconds.
time_t YMDHMSToTimet(int year, int month, int day, int hour, int minute,
                     int second);

// Returns true if the given year is a leap year.
bool IfLeapYear(int year);

// Returns true if this day does exist (m=[1..12], d=[1..31]).
bool IfDayExists(int y, int m, int d);

// Returns the number of days in the given month, or 0 if month is invalid
// (m=[1..12]).
int GetDaysInMonth(int y, int m);

// Converts a daterange of the form <from_date>..<to_date> where each
// date is in ISO 8601 format (e.g., 2006-05-23) to its Julian counterpart.
// "julian_start" and "julian_end" are out parameters which will contain
// the Julian dates.  If the daterange is of the form "..<to_date>",
// *julian_start will be set to INT_MIN, and if daterange is of the form
// "<from_date>..", *julian_end will be set to INT_MAX.
// Returns true if the input daterange is a legitimate date range.
bool Iso8601DateRangeToJulian(const char* daterange, int* julian_start,
                              int* julian_end);

// Converts a date in ISO 8601 format (e.g., 2006-05-23) to its Julian
// counterpart.  "julian_date" is an out parameter which will contain
// the converted date in Julian.
// Returns true if the input date is a legitimate date.
bool Iso8601DateToJulian(const char* date, int* julian_date);

// Returns true if daterange is of the right form
// (i.e. <from_date>..<to_date> where each date is in ISO 8601 format
// YYYY-MM-DD)
inline bool IsValidDateRange(const char* daterange) {
  int from, to;
  return Iso8601DateRangeToJulian(daterange, &from, &to);
}

// Given a number of seconds representing a time interval, return a string
// of the form [-]d:hh:mm:ss, where d is the number of days, hh the number
// of hours, mm the number of minutes, and ss the number of seconds.  If
// num_fields is given, display at least that many fields; if omitted,
// display all of them even if they're zero.
std::string TimeIntervalString(int interval);
std::string TimeIntervalString(int interval, int num_fields);
inline std::string TimeIntervalString(double interval) {
  return TimeIntervalString(static_cast<int>(interval), 4);
}

// Same as TimeIntervalString except that the time is given in units of
// 10^-power seconds.  The returned string will have the form [-]d:hh:mm:ss.f,
// with d, hh, and mm optionally omitted if num_fields and the value are both
// small enough.  power specifies the number of fractional second digits f,
// which are always present even if they contain trailing zeroes.  power may
// range between 0 and 9.
std::string TimeIntervalStringSubsecond(int64_t interval, int num_fields,
                                        int power);

// Specialization of TimeIntervalStringSubsecond for milliseconds.
inline std::string TimeIntervalStringMS(int64_t interval_ms, int num_fields) {
  return TimeIntervalStringSubsecond(interval_ms, num_fields, 3);
}

// Specialization of TimeIntervalStringSubsecond for microseconds.
inline std::string TimeIntervalStringMicros(int64_t interval_us,
                                            int num_fields) {
  return TimeIntervalStringSubsecond(interval_us, num_fields, 6);
}

// Given a number of seconds representing a time interval, return a string
// that is a human-readable description of that interval.  For example,
// given '90', returns '1 min 30 secs'.
std::string MakeIntervalString(int interval_secs);

#endif  // THIRD_PARTY_GLOOP_BASE_TIME_SUPPORT_H_
