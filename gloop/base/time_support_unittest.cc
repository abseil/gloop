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

#include "gloop/base/time_support.h"

#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#if !defined(GUNIT_NO_GOOGLE3)
#include "benchmark/benchmark.h"
#endif

namespace {

// ISSUES/TODO
// * There is very little (if any) sanity checking in the time_support
//   functions.  For example the year in IfLeapYear() can be negative.
//   These functions shouldn't have gotten past code review and are
//   begging to be fixed.
// * The naming of the time_support functions (IMHO) is very poor.
// * About half the unit test functions are empty (ran out of time).
// * We should simplify the Julian tests to make them easier to read
//   if we get a chance.

// Helper for test SleepForSignal.
volatile sig_atomic_t received_signo = 0;
void AlarmHandler(int signo) { received_signo = signo; }

TEST(TimeSupport, SleepFor) {
  static constexpr absl::Duration kTolerance = absl::Milliseconds(300);
  absl::Duration sleep_time = absl::Milliseconds(2500);
  absl::Time start = absl::Now();
  absl::SleepFor(sleep_time);
  absl::Time end = absl::Now();
  EXPECT_GE(end - start, sleep_time - kTolerance);
  EXPECT_LE(end - start, sleep_time + kTolerance);
}

TEST(TimeSupport, SleepForSignal) {
#if defined(__Fuchsia__)
  GTEST_SKIP() << "signal() is not implemented on Fuchsia";
#endif
  static constexpr absl::Duration kTolerance = absl::Milliseconds(300);
  received_signo = 0;
  sig_t old_alarm = signal(SIGALRM, AlarmHandler);
  alarm(2);
  absl::Duration sleep_time = absl::Milliseconds(3500);
  absl::Time start = absl::Now();
  absl::SleepFor(sleep_time);
  absl::Time end = absl::Now();
  signal(SIGALRM, old_alarm);
  EXPECT_EQ(received_signo, SIGALRM);
  EXPECT_GE(end - start, sleep_time - kTolerance);
  EXPECT_LE(end - start, sleep_time + kTolerance);
}

// Helper for test rfc_strptime.
void CheckRoundTripsWithFormat(time_t when, const char* time_format) {
  SCOPED_TRACE(absl::StrCat("time_format=", time_format, " when=", when));
  struct tm when_tm;
  gmtime_r(&when, &when_tm);
  char buf[1024];
  ASSERT_NE(strftime(buf, sizeof(buf), time_format, &when_tm), 0);

  struct tm rfc_parsed_tm = {0};
  EXPECT_TRUE(rfc_strptime(buf, &rfc_parsed_tm));
  const time_t expected_time = mktime(&when_tm);
  EXPECT_EQ(mktime(&rfc_parsed_tm), expected_time);

  // gstrptime() is purely a wrapper for strptime() so it is not very
  // interesting to test alone.  But might as well exercise gstrptime()
  // while we're here.
  struct tm g_parsed_tm = {0};
  EXPECT_NE(gstrptime(buf, time_format, &g_parsed_tm), nullptr);
  EXPECT_EQ(mktime(&g_parsed_tm), expected_time);
}

// Testing rfc_strptime (and gstrptime).
TEST(TimeSupport, rfc_strptime) {
  unsigned int seed = testing::UnitTest::GetInstance()->random_seed();
  for (int i = 0; i < 10; ++i) {
    SCOPED_TRACE(absl::StrCat("i=", i));
    CheckRoundTripsWithFormat(time(nullptr), "%a, %e %b %Y %H:%M:%S GMT");
    CheckRoundTripsWithFormat(rand_r(&seed), "%a, %e %b %Y %H:%M:%S GMT");
    CheckRoundTripsWithFormat(time(nullptr), "%a, %e-%b-%y %H:%M:%S GMT");
    CheckRoundTripsWithFormat(rand_r(&seed), "%a, %e-%b-%y %H:%M:%S GMT");
    CheckRoundTripsWithFormat(time(nullptr), "%a %b %e %H:%M:%S %Y");
    CheckRoundTripsWithFormat(rand_r(&seed), "%a %b %e %H:%M:%S %Y");
    // I'm not testing the erroneous format "%a, %b %e %H:%M:%S %Y"
  }
}

// Testing mkgmtime.
TEST(TimeSupport, mkgmtime) {
  srandom(testing::UnitTest::GetInstance()->random_seed());
  struct tm parts;

  // Test to make sure mkgmtime() and gmtime() really are inverse.
  for (int i = 0; i != 100; ++i) {
    const time_t orig = random();
    SCOPED_TRACE(absl::StrCat("i=", i, " orig=", orig));
    gmtime_r(&orig, &parts);
    EXPECT_EQ(mkgmtime(&parts), orig);

    // Try denormalized values for each time component that is used.
    // Avoid the first and last two years of the range because the denorm
    // values might yield times outside the time_t range.
    if (orig < 2 * 365 * 24 * 60 * 60 ||
        orig > std::numeric_limits<int32_t>::max() - 2 * 365 * 24 * 60 * 60)
      continue;

    parts.tm_sec += 100;
    EXPECT_EQ(mkgmtime(&parts), orig + 100);
    parts.tm_sec -= 200;
    EXPECT_EQ(mkgmtime(&parts), orig - 100);
    parts.tm_sec += 100;

    parts.tm_min += 100;
    EXPECT_EQ(mkgmtime(&parts), orig + 100 * 60);
    parts.tm_min -= 200;
    EXPECT_EQ(mkgmtime(&parts), orig - 100 * 60);
    parts.tm_min += 100;

    parts.tm_hour += 100;
    EXPECT_EQ(mkgmtime(&parts), orig + 100 * 60 * 60);
    parts.tm_hour -= 200;
    EXPECT_EQ(mkgmtime(&parts), orig - 100 * 60 * 60);
    parts.tm_hour += 100;

    parts.tm_mday += 100;
    EXPECT_EQ(mkgmtime(&parts), orig + 100 * 60 * 60 * 24);
    parts.tm_mday -= 200;
    EXPECT_EQ(mkgmtime(&parts), orig - 100 * 60 * 60 * 24);
    parts.tm_mday += 100;

    parts.tm_mon += 24;
    parts.tm_year -= 2;
    EXPECT_EQ(mkgmtime(&parts), orig);
    parts.tm_mon -= 48;
    parts.tm_year += 4;
    EXPECT_EQ(mkgmtime(&parts), orig);
  }

  // Try a few time values outside the time_t range.
  const time_t small = 86400 + 7200;  // Jan  2 1970 02:00:00
  const time_t large = std::numeric_limits<int32_t>::max() - 86400 -
                       7200;  // Jan 18 2038 01:14:07

  gmtime_r(&small, &parts);
  parts.tm_year -= 1;
  EXPECT_EQ(mkgmtime(&parts), -1);
  parts.tm_year += 1;

  parts.tm_hour -= 30;
  EXPECT_EQ(mkgmtime(&parts), -1);
  parts.tm_hour += 30;

  gmtime_r(&large, &parts);
  parts.tm_year += 1;
  EXPECT_EQ(mkgmtime(&parts), -1);
  parts.tm_year -= 1;

  parts.tm_hour += 30;
  EXPECT_EQ(mkgmtime(&parts), -1);
  parts.tm_hour -= 30;
}

// Testing mkgmtime_timet.
TEST(TimeSupport, mkgmtime_timet) {
  srandom(testing::UnitTest::GetInstance()->random_seed());
  struct tm parts;

  // Test to make sure mkgmtime_timet() and gmtime() really are inverse.
  for (int i = 0; i != 100; ++i) {
    const time_t orig = random();
    SCOPED_TRACE(absl::StrCat("i=", i, " orig=", orig));
    gmtime_r(&orig, &parts);
    EXPECT_EQ(mkgmtime_timet(&parts), orig);

    // Try denormalized values for each time component that is used.
    // Avoid the first and last two years of the range because the denorm
    // values might yield times outside the time_t range.
    if (orig < 2 * 365 * 24 * 60 * 60 ||
        orig > std::numeric_limits<int32_t>::max() - 2 * 365 * 24 * 60 * 60)
      continue;

    parts.tm_sec += 100;
    EXPECT_EQ(mkgmtime_timet(&parts), orig + 100);
    parts.tm_sec -= 200;
    EXPECT_EQ(mkgmtime_timet(&parts), orig - 100);
    parts.tm_sec += 100;

    parts.tm_min += 100;
    EXPECT_EQ(mkgmtime_timet(&parts), orig + 100 * 60);
    parts.tm_min -= 200;
    EXPECT_EQ(mkgmtime_timet(&parts), orig - 100 * 60);
    parts.tm_min += 100;

    parts.tm_hour += 100;
    EXPECT_EQ(mkgmtime_timet(&parts), orig + 100 * 60 * 60);
    parts.tm_hour -= 200;
    EXPECT_EQ(mkgmtime_timet(&parts), orig - 100 * 60 * 60);
    parts.tm_hour += 100;

    parts.tm_mday += 100;
    EXPECT_EQ(mkgmtime_timet(&parts), orig + 100 * 60 * 60 * 24);
    parts.tm_mday -= 200;
    EXPECT_EQ(mkgmtime_timet(&parts), orig - 100 * 60 * 60 * 24);
    parts.tm_mday += 100;

    parts.tm_mon += 24;
    parts.tm_year -= 2;
    EXPECT_EQ(mkgmtime_timet(&parts), orig);
    parts.tm_mon -= 48;
    parts.tm_year += 4;
    EXPECT_EQ(mkgmtime_timet(&parts), orig);
  }

  // Try a few time values outside the 32-bit time_t range.
  const time_t small = 86400 + 7200;  // Jan  2 1970 02:00:00
  const time_t large = std::numeric_limits<int32_t>::max() - 86400 -
                       7200;  // Jan 18 2038 01:14:07

  gmtime_r(&small, &parts);
  parts.tm_year -= 1;
  EXPECT_EQ(mkgmtime_timet(&parts), -1);
  parts.tm_year += 1;

  parts.tm_hour -= 30;
  EXPECT_EQ(mkgmtime_timet(&parts), -1);
  parts.tm_hour += 30;

  gmtime_r(&large, &parts);
  parts.tm_year += 1;
  if (sizeof(time_t) == 4) {
    EXPECT_EQ(mkgmtime_timet(&parts), -1);
  } else {
    EXPECT_EQ(mkgmtime_timet(&parts), large + 86400 * uint64_t{365});
  }
  parts.tm_year -= 1;
  parts.tm_hour += 30;
  if (sizeof(time_t) == 4) {
    EXPECT_EQ(mkgmtime_timet(&parts), -1);
  } else {
    EXPECT_EQ(mkgmtime_timet(&parts), large + 30 * uint64_t{3600});
  }
  parts.tm_hour -= 30;

  if (sizeof(time_t) == 8) {
    parts.tm_year = 1;  // 1901
    EXPECT_EQ(mkgmtime_timet(&parts), -1);
  }
}

struct SimpleDate {
  int y;
  int m;
  int d;
};

bool operator==(const SimpleDate& d1, const SimpleDate& d2) {
  return d1.y == d2.y && d1.m == d2.m && d1.d == d2.d;
}

std::ostream& operator<<(std::ostream& os, const SimpleDate& d) {
  char prev = os.fill('0');
  os << std::setw(4) << d.y << '-'  // YYYY
     << std::setw(2) << d.m << '-'  // MM
     << std::setw(2) << d.d;        // DD
  os.fill(prev);
  return os;
}

// Test YMDToJulian() and JulianToYMD().
TEST(TimeSupport, YMDToJulian) {
  SimpleDate d = {-4713, 11, 24};
  int expected_jd = 0;
  while (d.y < 3000) {  // let's test until the year 3000
    int jd = YMDToJulian(d.y, d.m, d.d);
    EXPECT_EQ(jd, expected_jd);

    SimpleDate d2 = {0, 0, 0};
    JulianToYMD(jd, &d2.y, &d2.m, &d2.d);
    EXPECT_EQ(d2, d);

    ++d.d;
    if (d.d > GetDaysInMonth(d.y, d.m)) {
      d.d -= GetDaysInMonth(d.y, d.m);
      ++d.m;
      if (d.m > 12) {
        d.m = 1;
        ++d.y;
      }
    }
    expected_jd = jd + 1;
  }
}

TEST(TimeSupport, GetDaysInMonth) {
  EXPECT_EQ(GetDaysInMonth(1900, -1), 0);
  EXPECT_EQ(GetDaysInMonth(1900, 0), 0);
  EXPECT_EQ(GetDaysInMonth(1900, 1), 31);
  EXPECT_EQ(GetDaysInMonth(1900, 2), 28);  // feb in non-LY
  EXPECT_EQ(GetDaysInMonth(1904, 2), 29);  // feb in LY
  EXPECT_EQ(GetDaysInMonth(2000, 2), 29);  // feb in LY
  EXPECT_EQ(GetDaysInMonth(1900, 3), 31);
  EXPECT_EQ(GetDaysInMonth(1900, 4), 30);
  EXPECT_EQ(GetDaysInMonth(1900, 5), 31);
  EXPECT_EQ(GetDaysInMonth(1900, 6), 30);
  EXPECT_EQ(GetDaysInMonth(1900, 7), 31);
  EXPECT_EQ(GetDaysInMonth(1900, 8), 31);
  EXPECT_EQ(GetDaysInMonth(1900, 9), 30);
  EXPECT_EQ(GetDaysInMonth(1900, 10), 31);
  EXPECT_EQ(GetDaysInMonth(1900, 11), 30);
  EXPECT_EQ(GetDaysInMonth(1900, 12), 31);
  EXPECT_EQ(GetDaysInMonth(1900, 13), 0);
}

TEST(TimeSupport, TestIfDayExists) {
  // illegal m/d
  EXPECT_FALSE(IfDayExists(1904, 13, 29));
  EXPECT_FALSE(IfDayExists(1904, 0, 29));
  EXPECT_FALSE(IfDayExists(1904, 2, -12));

  // check expected values
  EXPECT_FALSE(IfDayExists(1900, 2, 29));
  EXPECT_TRUE(IfDayExists(1900, 2, 28));
  EXPECT_TRUE(IfDayExists(1904, 2, 29));
  EXPECT_TRUE(IfDayExists(2000, 2, 29));
}

MATCHER_P2(ParsesAsJulianDateRange, expected_start, expected_end, "") {
  int j1 = 0;
  int j2 = 0;
  if (!Iso8601DateRangeToJulian(arg, &j1, &j2)) {
    *result_listener << "failed to parse ISO 8601 date range";
    return false;
  }
  if (j1 != expected_start || j2 != expected_end) {
    *result_listener << "parsed Julian range [" << j1 << ", " << j2
                     << "], expected [" << expected_start << ", "
                     << expected_end << "]";
    return false;
  }
  return true;
}

TEST(TimeSupport, Iso8601DateRangeToJulian) {
  EXPECT_THAT("1929-01-15..2007-01-17",
              ParsesAsJulianDateRange(2425627, 2454118));
  EXPECT_THAT("2007-01-17..2007-01-17",
              ParsesAsJulianDateRange(2454118, 2454118));
  EXPECT_THAT("2007-01-17..3501-08-15",
              ParsesAsJulianDateRange(2454118, 3000000));
  EXPECT_THAT("2007-01-17..", ParsesAsJulianDateRange(2454118, INT_MAX));
  EXPECT_THAT("..2007-01-17", ParsesAsJulianDateRange(INT_MIN, 2454118));

  // invalid dateranges
  int j1, j2;  // julian dates
  EXPECT_FALSE(Iso8601DateRangeToJulian("1929-01-15-2007-01-17", &j1, &j2));
  EXPECT_FALSE(Iso8601DateRangeToJulian("1929-01-15.2007-01-17", &j1, &j2));
  EXPECT_FALSE(Iso8601DateRangeToJulian("1929-01-15...2007-01-17", &j1, &j2));
  EXPECT_FALSE(Iso8601DateRangeToJulian("2007-01-17..2007-01-16", &j1, &j2));
  EXPECT_FALSE(
      Iso8601DateRangeToJulian("999999999-01-15..2007-01-17", &j1, &j2));
  EXPECT_FALSE(
      Iso8601DateRangeToJulian("999999999-01-15..999999999-01-16", &j1, &j2));
  EXPECT_FALSE(
      Iso8601DateRangeToJulian("1929-01-15..999999999-01-17", &j1, &j2));
  EXPECT_FALSE(
      Iso8601DateRangeToJulian("-999999999-01-15..2007-01-17", &j1, &j2));
  EXPECT_FALSE(
      Iso8601DateRangeToJulian("-999999999-01-15..-999999999-01-14", &j1, &j2));
}

MATCHER_P(ParsesAsJulianDate, expected_jd, "") {
  int julian_date = 0;
  if (!Iso8601DateToJulian(arg, &julian_date)) {
    *result_listener << "failed to parse ISO 8601 date";
    return false;
  }
  if (julian_date != expected_jd) {
    *result_listener << "parsed Julian date " << julian_date << ", expected "
                     << expected_jd;
    return false;
  }
  return true;
}

TEST(TimeSupport, Iso8601DateToJulian) {
  EXPECT_THAT("1929-01-15", ParsesAsJulianDate(2425627));
  EXPECT_THAT("2007-01-17", ParsesAsJulianDate(2454118));
  EXPECT_THAT("3501-08-15", ParsesAsJulianDate(3000000));

  // invalid dates
  int julian_date;
  EXPECT_FALSE(Iso8601DateToJulian("2007-01-32", &julian_date));
  EXPECT_FALSE(Iso8601DateToJulian("2007-13-01", &julian_date));
  EXPECT_FALSE(Iso8601DateToJulian("2007-01-17zqx", &julian_date));
  EXPECT_FALSE(Iso8601DateToJulian("999999999-01-15", &julian_date));
  EXPECT_FALSE(Iso8601DateToJulian("-999999999-01-15", &julian_date));
}

TEST(TimeSupport, JulianToSeconds) {
  // Julian 2454040 is Nov 1, 2006, incidentally.
  EXPECT_EQ(JulianToSeconds(2454040), 1162252800);
  EXPECT_EQ(SecondsToJulian(JulianToSeconds(2454040)), 2454040);
}

TEST(TimeSupport, YMDToSeconds) {
  EXPECT_EQ(YMDToSeconds(2004, 9, 17), 1095379200);
  EXPECT_EQ(YMDToSeconds(1970, 1, 1), 0);
  EXPECT_EQ(YMDToSeconds(1999, 1, 1), 915148800);
}

TEST(TimeSupport, YMDHMSToSeconds) {
  constexpr int hour = 4;
  constexpr int minute = 23;
  constexpr int second = 17;
  constexpr int offset = 3600 * hour + 60 * minute + second;
  EXPECT_EQ(YMDHMSToSeconds(2004, 9, 17, hour, minute, second),
            1095379200 + offset);
  EXPECT_EQ(YMDHMSToSeconds(1970, 1, 1, 0, 0, 0), 0);
  EXPECT_EQ(YMDHMSToSeconds(1999, 1, 1, hour, minute, second),
            915148800 + offset);
}

TEST(TimeSupport, YMDToTimet) {
  EXPECT_EQ(YMDToTimet(2004, 9, 17), 1095379200);
  EXPECT_EQ(YMDToTimet(1970, 1, 1), 0);
  EXPECT_EQ(YMDToTimet(1999, 1, 1), 915148800);
}

TEST(TimeSupport, YMDHMSToTimet) {
  constexpr int hour = 4;
  constexpr int minute = 23;
  constexpr int second = 17;
  constexpr int offset = 3600 * hour + 60 * minute + second;
  EXPECT_EQ(YMDHMSToTimet(2004, 9, 17, hour, minute, second),
            1095379200 + offset);
  EXPECT_EQ(YMDHMSToTimet(1970, 1, 1, 0, 0, 0), 0);
  EXPECT_EQ(YMDHMSToTimet(1999, 1, 1, hour, minute, second),
            915148800 + offset);

  EXPECT_EQ(YMDHMSToTimet(1969, 12, 31, 23, 59, 59), -1);

  if (sizeof(time_t) == 8) {
    EXPECT_EQ(YMDHMSToTimet(2039, 1, 1, hour, minute, second),
              uint64_t{2177452800} + offset);
    EXPECT_EQ(YMDHMSToTimet(5000, 1, 1, hour, minute, second),
              uint64_t{95617584000} + offset);
    // 1901 is a year that for 32-bit time_t's will wrap to be a positive
    // number in the mkgmtime_timet.  For 64-bit time_t's we're just verifying
    // that we've done the math correctly.
    EXPECT_EQ(YMDHMSToTimet(1901, 1, 1, hour, minute, second), -1);
  }
}

TEST(TimeSupport, TimeIntervalString) {
  EXPECT_EQ(TimeIntervalString(0), "0:00:00:00");
  EXPECT_EQ(TimeIntervalString(0, 4), "0:00:00:00");
  EXPECT_EQ(TimeIntervalString(0, 3), "0:00:00");
  EXPECT_EQ(TimeIntervalString(0, 2), "0:00");
  EXPECT_EQ(TimeIntervalString(0, 1), "0");
  EXPECT_EQ(TimeIntervalString(0, 0), "0");
  EXPECT_EQ(TimeIntervalString(-1), "-0:00:00:01");
  EXPECT_EQ(TimeIntervalString(-1, 4), "-0:00:00:01");
  EXPECT_EQ(TimeIntervalString(-1, 3), "-0:00:01");
  EXPECT_EQ(TimeIntervalString(-1, 2), "-0:01");
  EXPECT_EQ(TimeIntervalString(-1, 1), "-1");
  EXPECT_EQ(TimeIntervalString(-1, 0), "-1");
  EXPECT_EQ(TimeIntervalString(17), "0:00:00:17");
  EXPECT_EQ(TimeIntervalString(17, 4), "0:00:00:17");
  EXPECT_EQ(TimeIntervalString(17, 3), "0:00:17");
  EXPECT_EQ(TimeIntervalString(17, 2), "0:17");
  EXPECT_EQ(TimeIntervalString(17, 1), "17");
  EXPECT_EQ(TimeIntervalString(17, 0), "17");
  EXPECT_EQ(TimeIntervalString(-60, 0), "-1:00");
  EXPECT_EQ(TimeIntervalString(7201), "0:02:00:01");
  EXPECT_EQ(TimeIntervalString(7201, 4), "0:02:00:01");
  EXPECT_EQ(TimeIntervalString(7201, 3), "2:00:01");
  EXPECT_EQ(TimeIntervalString(7201, 2), "2:00:01");
  EXPECT_EQ(TimeIntervalString(7201, 0), "2:00:01");
  EXPECT_EQ(TimeIntervalString(-7201), "-0:02:00:01");
  EXPECT_EQ(TimeIntervalString(-7201, 4), "-0:02:00:01");
  EXPECT_EQ(TimeIntervalString(-7201, 3), "-2:00:01");
  EXPECT_EQ(TimeIntervalString(-7201, 2), "-2:00:01");
  EXPECT_EQ(TimeIntervalString(-7201, 0), "-2:00:01");
  EXPECT_EQ(TimeIntervalString(1000000), "11:13:46:40");
  EXPECT_EQ(TimeIntervalString(1000000, 5), "11:13:46:40");
  EXPECT_EQ(TimeIntervalString(1000000, 3), "11:13:46:40");
  EXPECT_EQ(TimeIntervalString(-1000000, 3), "-11:13:46:40");
  EXPECT_EQ(TimeIntervalString(2147483647, 0), "24855:03:14:07");
  EXPECT_EQ(TimeIntervalString(static_cast<int>(2147483648U), 0),
            "-24855:03:14:08");

  EXPECT_EQ(TimeIntervalStringMS(0, 4), "0:00:00:00.000");
  EXPECT_EQ(TimeIntervalStringMS(0, 3), "0:00:00.000");
  EXPECT_EQ(TimeIntervalStringMS(0, 2), "0:00.000");
  EXPECT_EQ(TimeIntervalStringMS(0, 1), "0.000");
  EXPECT_EQ(TimeIntervalStringMS(0, 0), "0.000");
  EXPECT_EQ(TimeIntervalStringMS(3200, 0), "3.200");
  EXPECT_EQ(TimeIntervalStringMS(-1, 4), "-0:00:00:00.001");
  EXPECT_EQ(TimeIntervalStringMS(-1, 3), "-0:00:00.001");
  EXPECT_EQ(TimeIntervalStringMS(-1, 2), "-0:00.001");
  EXPECT_EQ(TimeIntervalStringMS(-1, 1), "-0.001");
  EXPECT_EQ(TimeIntervalStringMS(-1, 0), "-0.001");
  EXPECT_EQ(TimeIntervalStringMS(17320, 4), "0:00:00:17.320");
  EXPECT_EQ(TimeIntervalStringMS(17320, 3), "0:00:17.320");
  EXPECT_EQ(TimeIntervalStringMS(17320, 2), "0:17.320");
  EXPECT_EQ(TimeIntervalStringMS(17320, 1), "17.320");
  EXPECT_EQ(TimeIntervalStringMS(17320, 0), "17.320");
  EXPECT_EQ(TimeIntervalStringMS(-120005, 0), "-2:00.005");
  EXPECT_EQ(TimeIntervalStringMS(7201925, 4), "0:02:00:01.925");
  EXPECT_EQ(TimeIntervalStringMS(7201925, 3), "2:00:01.925");
  EXPECT_EQ(TimeIntervalStringMS(7201922, 2), "2:00:01.922");
  EXPECT_EQ(TimeIntervalStringMS(7201925, 0), "2:00:01.925");
  EXPECT_EQ(TimeIntervalStringMS(1000000002, 5), "11:13:46:40.002");
  EXPECT_EQ(TimeIntervalStringMS(1000000999, 3), "11:13:46:40.999");
  EXPECT_EQ(TimeIntervalStringMS(-1000001321, 3), "-11:13:46:41.321");
  EXPECT_EQ(TimeIntervalStringMS(int64_t{12345678912345678}, 0),
            "142889802:05:25:45.678");

  EXPECT_EQ(TimeIntervalStringMicros(0, 4), "0:00:00:00.000000");
  EXPECT_EQ(TimeIntervalStringMicros(0, 3), "0:00:00.000000");
  EXPECT_EQ(TimeIntervalStringMicros(0, 2), "0:00.000000");
  EXPECT_EQ(TimeIntervalStringMicros(0, 1), "0.000000");
  EXPECT_EQ(TimeIntervalStringMicros(0, 0), "0.000000");
  EXPECT_EQ(TimeIntervalStringMicros(3200000, 0), "3.200000");
  EXPECT_EQ(TimeIntervalStringMicros(-1, 4), "-0:00:00:00.000001");
  EXPECT_EQ(TimeIntervalStringMicros(-1, 3), "-0:00:00.000001");
  EXPECT_EQ(TimeIntervalStringMicros(-1, 2), "-0:00.000001");
  EXPECT_EQ(TimeIntervalStringMicros(-1, 1), "-0.000001");
  EXPECT_EQ(TimeIntervalStringMicros(-1, 0), "-0.000001");
  EXPECT_EQ(TimeIntervalStringMicros(17264320, 4), "0:00:00:17.264320");
  EXPECT_EQ(TimeIntervalStringMicros(17264321, 3), "0:00:17.264321");
  EXPECT_EQ(TimeIntervalStringMicros(17264320, 2), "0:17.264320");
  EXPECT_EQ(TimeIntervalStringMicros(17264320, 1), "17.264320");
  EXPECT_EQ(TimeIntervalStringMicros(7264320, 0), "7.264320");
  EXPECT_EQ(TimeIntervalStringMicros(-120100005, 0), "-2:00.100005");
  EXPECT_EQ(TimeIntervalStringMicros(int64_t{7201999925}, 4),
            "0:02:00:01.999925");
  EXPECT_EQ(TimeIntervalStringMicros(int64_t{7201999925}, 3), "2:00:01.999925");
  EXPECT_EQ(TimeIntervalStringMicros(int64_t{7201999925}, 2), "2:00:01.999925");
  EXPECT_EQ(TimeIntervalStringMicros(int64_t{7201999925}, 0), "2:00:01.999925");
  EXPECT_EQ(TimeIntervalStringMicros(int64_t{1000000000002}, 5),
            "11:13:46:40.000002");
  EXPECT_EQ(TimeIntervalStringMicros(int64_t{1000000999999}, 3),
            "11:13:46:40.999999");
  EXPECT_EQ(TimeIntervalStringMicros(int64_t{-1000001654321}, 3),
            "-11:13:46:41.654321");
  EXPECT_EQ(TimeIntervalStringMicros(int64_t{1234567891234567890}, 0),
            "14288980:05:20:34.567890");

  EXPECT_EQ(TimeIntervalStringSubsecond(0, 4, 0), "0:00:00:00");
  EXPECT_EQ(TimeIntervalStringSubsecond(0, 3, 0), "0:00:00");
  EXPECT_EQ(TimeIntervalStringSubsecond(0, 2, 0), "0:00");
  EXPECT_EQ(TimeIntervalStringSubsecond(0, 1, 0), "0");
  EXPECT_EQ(TimeIntervalStringSubsecond(0, 0, 0), "0");
  EXPECT_EQ(TimeIntervalStringSubsecond(-1, 4, 0), "-0:00:00:01");
  EXPECT_EQ(TimeIntervalStringSubsecond(-1, 3, 0), "-0:00:01");
  EXPECT_EQ(TimeIntervalStringSubsecond(-1, 2, 0), "-0:01");
  EXPECT_EQ(TimeIntervalStringSubsecond(-1, 1, 0), "-1");
  EXPECT_EQ(TimeIntervalStringSubsecond(-1, 0, 0), "-1");
  EXPECT_EQ(TimeIntervalStringSubsecond(0, 4, 1), "0:00:00:00.0");
  EXPECT_EQ(TimeIntervalStringSubsecond(0, 3, 2), "0:00:00.00");
  EXPECT_EQ(TimeIntervalStringSubsecond(2000000000, 2, 9), "0:02.000000000");
  EXPECT_EQ(TimeIntervalStringSubsecond(0, 1, 1), "0.0");
  EXPECT_EQ(TimeIntervalStringSubsecond(0, 0, 1), "0.0");
  EXPECT_EQ(TimeIntervalStringSubsecond(-1, 4, 1), "-0:00:00:00.1");
  EXPECT_EQ(TimeIntervalStringSubsecond(-11, 3, 9), "-0:00:00.000000011");
  EXPECT_EQ(TimeIntervalStringSubsecond(-10, 2, 1), "-0:01.0");
  EXPECT_EQ(TimeIntervalStringSubsecond(-5, 1, 1), "-0.5");
  EXPECT_EQ(TimeIntervalStringSubsecond(-100, 0, 7), "-0.0000100");
  EXPECT_EQ(TimeIntervalStringSubsecond(1230000100, 0, 7), "2:03.0000100");
  EXPECT_EQ(TimeIntervalStringSubsecond(int64_t{1234567891234567890}, 0, 0),
            "14288980222622:07:31:30");
  EXPECT_EQ(TimeIntervalStringSubsecond(int64_t{1234567891234567890}, 0, 1),
            "1428898022262:05:33:09.0");
  EXPECT_EQ(TimeIntervalStringSubsecond(int64_t{-1234567891234567890}, 2, 9),
            "-14288:23:31:31.234567890");
  EXPECT_EQ(
      TimeIntervalStringSubsecond(std::numeric_limits<int64_t>::min(), 0, 0),
      "-106751991167300:15:30:08");
}

TEST(TimeSupport, MakeIntervalString) {
  EXPECT_EQ(MakeIntervalString(1), "1 sec");
  EXPECT_EQ(MakeIntervalString(59), "59 secs");

  EXPECT_EQ(MakeIntervalString(60), "1 min 0 secs");
  EXPECT_EQ(MakeIntervalString(61), "1 min 1 sec");
  EXPECT_EQ(MakeIntervalString((2 * 60) + 59), "2 mins 59 secs");
  EXPECT_EQ(MakeIntervalString((59 * 60) + 7), "59 mins 7 secs");

  EXPECT_EQ(MakeIntervalString((1 * 3600) + 2 * 60), "1 hour 2 mins");
  EXPECT_EQ(MakeIntervalString((2 * 3600) + 1 * 60), "2 hours 1 min");

  EXPECT_EQ(MakeIntervalString(86400 + (12 * 3600)), "1 day 12 hours");
  EXPECT_EQ(MakeIntervalString((2 * 86400) + (1 * 3600)), "2 days 1 hour");
}

TEST(TimeSupport, GetCurrentTimeNanosMonotonic) {
  const int64_t start = absl::GetCurrentTimeNanos();
  int64_t now = start;
  int64_t last = start;
  int errors = 0;

  while (now - start < 15LL * 1000 * 1000 * 1000) {
    now = absl::GetCurrentTimeNanos();
    if (now < last) {
      LOG(ERROR) << "Regression noted"
                 << ", now = " << now << ", last = " << last
                 << ", delta = " << (last - now);
      ++errors;
    }
    last = now;
  }
  if (errors != 0) {
    LOG(ERROR) << "Ignoring regressions until GetCurrentTimeNanos() is fixed";
    errors = 0;
  }
  EXPECT_EQ(errors, 0);
}

// TODO: Benchmarks are currently not portable.
#if !defined(GUNIT_NO_GOOGLE3)
namespace benchmarks {

// Run on lpab5 (32 X 2600 MHz CPUs); 2015/05/21-11:00:38
// CPU: Intel Sandybridge with HyperThreading (16 cores)
// Benchmark                                  Time(ns)    CPU(ns) Iterations
// -------------------------------------------------------------------------
// BM_GetCurrentTimeNanos                            9          9   75633184
// BM_YMDHMSToSeconds                               17         17   42248543

void BM_GetCurrentTimeNanos(benchmark::State& state) {
  int64_t c = 1;
  for (auto _ : state) {
    c = absl::GetCurrentTimeNanos();
  }
  CHECK_GE(c, 0);
}
BENCHMARK(BM_GetCurrentTimeNanos);

void BM_YMDHMSToSeconds(benchmark::State& state) {
  time_t t = 0;
  int i = 0;
  for (auto _ : state) {
    t = YMDHMSToSeconds(2015, 5, 19, 21, 20, i);
    ++i;
  }
  CHECK_GE(t, 0);
}
BENCHMARK(BM_YMDHMSToSeconds);

}  // namespace benchmarks
#endif

}  // namespace
