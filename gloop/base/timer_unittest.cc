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

//
// This file tests timer.h
// It calls each timer in various ways.

#include "gloop/base/timer.h"

#include "gloop/util/gtl/unique_array.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include <functional>
#include <thread>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/config.h"
#include "gtest/gtest.h"

#if !PORTABLE_BASE  // TODO: Portable benchmark.h
#include "benchmark/benchmark.h"
#endif

// This function is templated so that hopefully the compiler will emit
// and error if t1 and t2 are different types, instead of doing an
// implicit conversion to float.
template <typename T>
static int CheckEqualInternal(T t1, T t2, double bound, const char* str,
                              int line_num) {
  // times are "the same":
  if (t1 <= (t2 * (1.0 + bound) + bound) &&
      t2 <= (t1 * (1.0 + bound) + bound)) {
    return 0;
  }
  LOG(WARNING) << "TEST FAILED ON LINE " << line_num << ": " << str
               << ": t1=" << t1 << " != t2=" << t2;
  return 1;
}

static int CheckEqualDurationInternal(absl::Duration t1, absl::Duration t2,
                                      double bound, const char* str,
                                      int line_num) {
  return CheckEqualInternal(absl::FDivDuration(t1, absl::Seconds(1)),
                            absl::FDivDuration(t2, absl::Seconds(1)), bound,
                            str, line_num);
}

#define CheckEqual(_t1, _t2) \
  CheckEqualInternal(_t1, _t2, 0.005, #_t1 " == " #_t2, __LINE__)
#define CheckEqualBound(_t1, _t2, _bound) \
  CheckEqualInternal(_t1, _t2, _bound, #_t1 " == " #_t2, __LINE__)
#define CheckEqualDuration(_t1, _t2) \
  CheckEqualDurationInternal(_t1, _t2, 0.005, #_t1 " == " #_t2, __LINE__)

// Run `test_function` until it fails or a self imposed deadline
// expires.  The `test_function` returns the number of failures, or
// zero for a passing test.
//
// Tests can fail due to other processes running and slowing us down.
// Thus we run `test_function` many times; if it succeeds even once
// things are ok, but if it fails consistently we're in trouble.
static void DoFlakyTimerTestLoop(const std::function<int()>& test_function) {
  const absl::Time deadline = absl::Now() + absl::Minutes(2);
  int attempt;
  for (attempt = 0; absl::Now() < deadline; ++attempt) {
    const int num_failures = test_function();
    if (num_failures == 0) {
      return;  // PASS
    }
    LOG(ERROR) << "ATTEMPT #" << attempt << " FAILED WITH " << num_failures
               << " FAILURES";
  }
  FAIL() << "All " << attempt
         << " attempts failed.  See the INFO log for more details.";
}

// Tests the timers that measure "real" time (CycleTimer, WallTimer,
// etc).  Returns the number of failures encountered during the test.
// Since timing tests are flaky, this test is made to be run in a loop
// until either zero failures occur or the test has failed some number
// of times.
static int DoRealTimerTests() {
  int num_failures = 0;

  ElapsedTimer et("2 second sleep");
  ElapsedTimer et_disabled("2 second sleep (disabled)", false, 0.0);

  CycleTimer ct;
  SimpleCycleTimer sct;
  WallTimer wt;

  EXPECT_FALSE(ct.IsRunning());
  EXPECT_FALSE(sct.IsRunning());
  EXPECT_FALSE(wt.IsRunning());

  ct.Start();
  sct.Start();
  wt.Start();

  EXPECT_TRUE(ct.IsRunning());
  EXPECT_TRUE(sct.IsRunning());
  EXPECT_TRUE(wt.IsRunning());

  absl::SleepFor(absl::Seconds(1));

  ct.Stop();
  sct.Stop();
  wt.Stop();

  EXPECT_FALSE(ct.IsRunning());
  EXPECT_FALSE(sct.IsRunning());
  EXPECT_FALSE(wt.IsRunning());

  // Check CycleTimer.
  num_failures += CheckEqual(ct.Get(), ct.GetInMs() / 1000.0);
  num_failures += CheckEqual(ct.GetInMs(), ct.GetInUsec() / 1000);
  num_failures +=
      CheckEqual(ct.GetInUsec(), absl::ToInt64Microseconds(ct.GetDuration()));
  num_failures += CheckEqual(ct.Get(), ct.CyclesToMs(ct.GetCycles()) / 1000.0);
  num_failures += CheckEqual(ct.CyclesToMs(ct.GetCycles()),
                             ct.CyclesToUsec(ct.GetCycles()) / 1000);
  num_failures += CheckEqual(
      ct.CyclesToUsec(ct.GetCycles()),
      absl::ToInt64Microseconds(ct.CyclesToDuration(ct.GetCycles())));
  LOG(INFO) << "CycleTimer: " << ct.Get() << "sec == " << ct.GetInMs() << "msec"
            << " (" << ct.GetCycles()
            << " cycles == " << ct.CyclesToUsec(ct.GetCycles())
            << " usec == " << absl::FormatDuration(ct.GetDuration()) << ")";

  // Check SimpleCycleTimer.
  num_failures += CheckEqual(sct.Get(), sct.GetInMs() / 1000.0);
  num_failures += CheckEqual(sct.GetInMs(), sct.GetInUsec() / 1000);
  num_failures +=
      CheckEqual(sct.GetInUsec(), absl::ToInt64Microseconds(sct.GetDuration()));
  num_failures += CheckEqual(
      sct.Get(), CycleTimerBase::CyclesToMs(sct.GetCycles()) / 1000.0);
  num_failures +=
      CheckEqual(CycleTimerBase::CyclesToMs(sct.GetCycles()),
                 CycleTimerBase::CyclesToUsec(sct.GetCycles()) / 1000);
  num_failures += CheckEqual(CycleTimerBase::CyclesToUsec(sct.GetCycles()),
                             CycleTimerBase::CyclesToDuration(sct.GetCycles()) /
                                 absl::Microseconds(1));
  num_failures += CheckEqual(sct.Get(), wt.Get());
  LOG(INFO) << "SimpleCycleTimer: " << sct.Get() << "sec == " << sct.GetInMs()
            << "msec"
            << " (" << sct.GetCycles() << " cycles == " << sct.GetInUsec()
            << "usec == " << absl::FormatDuration(sct.GetDuration()) << ")";

  // Check Walltimer.
  num_failures += CheckEqual(wt.Get(), wt.GetInMs() / 1000.0);
  num_failures +=
      CheckEqual(wt.GetInMs(), absl::ToInt64Milliseconds(wt.GetDuration()));
  LOG(INFO) << "WallTimer: " << wt.Get() << "sec == " << wt.GetInMs()
            << "msec == " << absl::FormatDuration(wt.GetDuration());

  // et should LOG(INFO), but et_disabled should not.
  return num_failures;
}

TEST(TimerTest, RealTimerTests) { DoFlakyTimerTestLoop(&DoRealTimerTests); }

// The usage timers (UserTimer, SystemTimer, VirtualTimer, etc) aren't
// available on all platforms.
#if !defined(__native_client__)

static void EatUserTimeForSeconds(double seconds) {
  SimpleCycleTimer elapsed;
  elapsed.Start();
  while (elapsed.Get() < seconds) {
    // Do some relatively expensive work in userspace, so that the userspace
    // work dominates any system work that may occur in SimpleCycleTimer::Get().
    volatile uint64_t dummy_work = 1;
    static constexpr uint64_t kDivisor = 4294967291;
    for (int i = 0; i < 1000; ++i) {
      dummy_work %= kDivisor;
    }
  }
}

static void EatSystemTimeForSeconds(double seconds) {
  constexpr size_t bufsize = 100 << 10;
  auto buffer = gtl::MakeUniqueArrayForOverwrite<char>(bufsize);
  memset(buffer.data(), 0, bufsize);

  SimpleCycleTimer elapsed;
  elapsed.Start();

#ifdef _WIN32
  HANDLE read_pipe, write_pipe;
  CHECK(CreatePipe(&read_pipe, &write_pipe, nullptr, bufsize));
  while (elapsed.Get() < seconds) {
    CHECK(WriteFile(write_pipe, buffer.get(), bufsize, nullptr, nullptr));
    CHECK(ReadFile(read_pipe, buffer.get(), bufsize, nullptr, nullptr));
  }
  CHECK(CloseHandle(write_pipe));
  CHECK(CloseHandle(read_pipe));
#else
  int fd;
  PCHECK((fd = open("/dev/zero", O_RDONLY)) != -1);
  while (elapsed.Get() < seconds) {
    PCHECK(read(fd, buffer.data(), bufsize) != -1);
  }
  close(fd);
#endif
}

// Tests the timers that measure "usage" time (UserTimer, SystemTimer,
// etc).  Returns the number of failures encountered during the test.
// Since timing tests are flaky, this test is made to be run in a loop
// until either zero failures occur or the test has failed some number
// of times.
static int DoUsageTimerTests() {
  int num_failures = 0;

  // Test that absl::SleepFor does not consume user or system time.
  {
    UserTimer ut;
    SystemTimer st;
    VirtualTimer vt;
    UserSystemWallTimer uswt;

    EXPECT_FALSE(ut.IsRunning());
    EXPECT_FALSE(st.IsRunning());
    EXPECT_FALSE(vt.IsRunning());
    EXPECT_FALSE(uswt.IsRunning());

    ut.Start();
    st.Start();
    vt.Start();
    uswt.Start();

    EXPECT_TRUE(ut.IsRunning());
    EXPECT_TRUE(st.IsRunning());
    EXPECT_TRUE(vt.IsRunning());
    EXPECT_TRUE(uswt.IsRunning());

    absl::SleepFor(absl::Seconds(1));

    ut.Stop();
    st.Stop();
    vt.Stop();
    uswt.Stop();

    EXPECT_FALSE(ut.IsRunning());
    EXPECT_FALSE(st.IsRunning());
    EXPECT_FALSE(vt.IsRunning());
    EXPECT_FALSE(uswt.IsRunning());

    // Check UserTimer.
    num_failures += CheckEqualBound(ut.Get(), 0.0, 0.1);
    num_failures += CheckEqual(ut.Get(), ut.GetInMs() / 1000.0);
    num_failures +=
        CheckEqual(absl::ToInt64Milliseconds(ut.GetDuration()), ut.GetInMs());
    LOG(INFO) << "UserTimer: " << ut.Get() << "sec == " << ut.GetInMs()
              << "msec "
              << "(" << absl::FormatDuration(ut.GetDuration()) << ")";

    // Check SystemTimer.
    num_failures += CheckEqualBound(st.Get(), 0.0, 0.1);
    num_failures += CheckEqual(st.Get(), st.GetInMs() / 1000.0);
    num_failures +=
        CheckEqual(absl::ToInt64Milliseconds(st.GetDuration()), st.GetInMs());
    LOG(INFO) << "SystemTimer: " << st.Get() << "sec == " << st.GetInMs()
              << "msec "
              << "(" << absl::FormatDuration(st.GetDuration()) << ")";

    // Check VirtualTimer.
    num_failures += CheckEqualBound(vt.Get(), 0.0, 0.1);
    num_failures += CheckEqual(vt.Get(), vt.GetInMs() / 1000.0);
    num_failures +=
        CheckEqual(absl::ToInt64Milliseconds(vt.GetDuration()), vt.GetInMs());
    LOG(INFO) << "VirtualTimer: " << vt.Get() << "sec == " << vt.GetInMs()
              << "msec "
              << "(" << absl::FormatDuration(vt.GetDuration()) << ")";

    // Check UserSystemWallTimer.
    double user, sys, wall;
    uswt.Get(&user, &sys, &wall);
    num_failures += CheckEqualBound(user, 0.0, 0.1);
    num_failures += CheckEqualBound(sys, 0.0, 0.1);
    num_failures += CheckEqualBound(wall, 1.0, 0.05);
    int64_t user_ms, sys_ms, wall_ms;
    uswt.GetInMs(&user_ms, &sys_ms, &wall_ms);
    num_failures += CheckEqual(user, user_ms / 1000.0);
    num_failures += CheckEqual(sys, sys_ms / 1000.0);
    num_failures += CheckEqual(wall, wall_ms / 1000.0);
    absl::Duration user_dur, sys_dur, wall_dur;
    uswt.GetDuration(&user_dur, &sys_dur, &wall_dur);
    num_failures += CheckEqual(absl::ToInt64Milliseconds(user_dur), user_ms);
    num_failures += CheckEqual(absl::ToInt64Milliseconds(sys_dur), sys_ms);
    num_failures += CheckEqual(absl::ToInt64Milliseconds(wall_dur), wall_ms);
  }

  // Test that UserTimer works.
  {
    UserTimer ut;
    SystemTimer st;
    VirtualTimer vt;
    UserSystemWallTimer uswt;

    EXPECT_FALSE(ut.IsRunning());
    EXPECT_FALSE(st.IsRunning());
    EXPECT_FALSE(vt.IsRunning());
    EXPECT_FALSE(uswt.IsRunning());

    ut.Start();
    st.Start();
    vt.Start();
    uswt.Start();

    EXPECT_TRUE(ut.IsRunning());
    EXPECT_TRUE(st.IsRunning());
    EXPECT_TRUE(vt.IsRunning());
    EXPECT_TRUE(uswt.IsRunning());

    EatUserTimeForSeconds(0.5);

    ut.Stop();
    st.Stop();
    vt.Stop();
    uswt.Stop();

    EXPECT_FALSE(ut.IsRunning());
    EXPECT_FALSE(st.IsRunning());
    EXPECT_FALSE(vt.IsRunning());
    EXPECT_FALSE(uswt.IsRunning());

    // Check UserTimer.
    num_failures += CheckEqualBound(ut.Get(), 0.5, 0.1);
    num_failures += CheckEqual(ut.Get(), ut.GetInMs() / 1000.0);
    num_failures +=
        CheckEqual(absl::ToInt64Milliseconds(ut.GetDuration()), ut.GetInMs());
    LOG(INFO) << "UserTimer: " << ut.Get() << "sec == " << ut.GetInMs()
              << "msec "
              << "(" << absl::FormatDuration(ut.GetDuration()) << ")";

    // Check SystemTimer.
    num_failures += CheckEqualBound(st.Get(), 0.0, 0.1);
    num_failures += CheckEqual(st.Get(), st.GetInMs() / 1000.0);
    num_failures +=
        CheckEqual(absl::ToInt64Milliseconds(st.GetDuration()), st.GetInMs());
    LOG(INFO) << "SystemTimer: " << st.Get() << "sec == " << st.GetInMs()
              << "msec "
              << "(" << absl::FormatDuration(st.GetDuration()) << ")";

    // Check VirtualTimer.
    num_failures += CheckEqualBound(vt.Get(), 0.5, 0.1);
    num_failures += CheckEqual(vt.Get(), vt.GetInMs() / 1000.0);
    num_failures +=
        CheckEqual(absl::ToInt64Milliseconds(vt.GetDuration()), vt.GetInMs());
    LOG(INFO) << "VirtualTimer: " << vt.Get() << "sec == " << vt.GetInMs()
              << "msec "
              << "(" << absl::FormatDuration(vt.GetDuration()) << ")";

    // Check UserSystemWallTimer.
    double user, sys, wall;
    uswt.Get(&user, &sys, &wall);
    num_failures += CheckEqualBound(user, 0.5, 0.1);
    num_failures += CheckEqualBound(sys, 0.0, 0.1);
    num_failures += CheckEqualBound(wall, 0.5, 0.05);
    int64_t user_ms, sys_ms, wall_ms;
    uswt.GetInMs(&user_ms, &sys_ms, &wall_ms);
    num_failures += CheckEqual(user, user_ms / 1000.0);
    num_failures += CheckEqual(sys, sys_ms / 1000.0);
    num_failures += CheckEqual(wall, wall_ms / 1000.0);
    absl::Duration user_dur, sys_dur, wall_dur;
    uswt.GetDuration(&user_dur, &sys_dur, &wall_dur);
    num_failures += CheckEqual(absl::ToInt64Milliseconds(user_dur), user_ms);
    num_failures += CheckEqual(absl::ToInt64Milliseconds(sys_dur), sys_ms);
    num_failures += CheckEqual(absl::ToInt64Milliseconds(wall_dur), wall_ms);
  }

  // Test that SystemTimer works.
  {
    UserTimer ut;
    SystemTimer st;
    VirtualTimer vt;
    UserSystemWallTimer uswt;

    EXPECT_FALSE(ut.IsRunning());
    EXPECT_FALSE(st.IsRunning());
    EXPECT_FALSE(vt.IsRunning());
    EXPECT_FALSE(uswt.IsRunning());

    ut.Start();
    st.Start();
    vt.Start();
    uswt.Start();

    EXPECT_TRUE(ut.IsRunning());
    EXPECT_TRUE(st.IsRunning());
    EXPECT_TRUE(vt.IsRunning());
    EXPECT_TRUE(uswt.IsRunning());

    EatSystemTimeForSeconds(0.5);

    ut.Stop();
    st.Stop();
    vt.Stop();
    uswt.Stop();

    EXPECT_FALSE(ut.IsRunning());
    EXPECT_FALSE(st.IsRunning());
    EXPECT_FALSE(vt.IsRunning());
    EXPECT_FALSE(uswt.IsRunning());

    // Check UserTimer.
    num_failures += CheckEqualBound(ut.Get(), 0.0, 0.1);
    num_failures += CheckEqual(ut.Get(), ut.GetInMs() / 1000.0);
    num_failures +=
        CheckEqual(absl::ToInt64Milliseconds(ut.GetDuration()), ut.GetInMs());
    LOG(INFO) << "UserTimer: " << ut.Get() << "sec == " << ut.GetInMs()
              << "msec "
              << "(" << absl::FormatDuration(ut.GetDuration()) << ")";

    // Check SystemTimer.
    num_failures += CheckEqualBound(st.Get(), 0.5, 0.1);
    num_failures += CheckEqual(st.Get(), st.GetInMs() / 1000.0);
    num_failures +=
        CheckEqual(absl::ToInt64Milliseconds(st.GetDuration()), st.GetInMs());
    LOG(INFO) << "SystemTimer: " << st.Get() << "sec == " << st.GetInMs()
              << "msec "
              << "(" << absl::FormatDuration(st.GetDuration()) << ")";

    // Check VirtualTimer.
    num_failures += CheckEqualBound(vt.Get(), 0.5, 0.1);
    num_failures += CheckEqual(vt.Get(), vt.GetInMs() / 1000.0);
    num_failures +=
        CheckEqual(absl::ToInt64Milliseconds(vt.GetDuration()), vt.GetInMs());
    LOG(INFO) << "VirtualTimer: " << vt.Get() << "sec == " << vt.GetInMs()
              << "msec "
              << "(" << absl::FormatDuration(vt.GetDuration()) << ")";

    // Check UserSystemWallTimer.
    double user, sys, wall;
    uswt.Get(&user, &sys, &wall);
    num_failures += CheckEqualBound(user, 0.0, 0.1);
    num_failures += CheckEqualBound(sys, 0.5, 0.1);
    num_failures += CheckEqualBound(wall, 0.5, 0.05);

    int64_t user_ms, sys_ms, wall_ms;
    uswt.GetInMs(&user_ms, &sys_ms, &wall_ms);
    num_failures += CheckEqual(user, user_ms / 1000.0);
    num_failures += CheckEqual(sys, sys_ms / 1000.0);
    num_failures += CheckEqual(wall, wall_ms / 1000.0);
    absl::Duration user_dur, sys_dur, wall_dur;
    uswt.GetDuration(&user_dur, &sys_dur, &wall_dur);
    num_failures += CheckEqual(absl::ToInt64Milliseconds(user_dur), user_ms);
    num_failures += CheckEqual(absl::ToInt64Milliseconds(sys_dur), sys_ms);
    num_failures += CheckEqual(absl::ToInt64Milliseconds(wall_dur), wall_ms);
  }

  // Test that UserTimer and SystemTimer measure time consumed by
  // background threads.
  {
    UserTimer ut;
    SystemTimer st;
    VirtualTimer vt;
    UserSystemWallTimer uswt;

    ut.Start();
    st.Start();
    vt.Start();
    uswt.Start();

    // Run these two background threads serially, rather than in parallel, to
    // try to keep wall-clock time and thread runtime in sync.
    // (Eat{User,System}TimeForSeconds measure their arguments in wall-clock
    // time.) This isn't foolproof--on a heavily loaded system,
    // EatUserTimeForSeconds might end up doing only 0.4 or 0.3 s of work during
    // 0.5 s of wall-clock time. However, running the threads serially works
    // well enough that the flaky timer test loop should prevent any failures.
    std::thread eat_user(EatUserTimeForSeconds, 0.5);
    eat_user.join();
    std::thread eat_system(EatSystemTimeForSeconds, 0.5);
    eat_system.join();

    ut.Stop();
    st.Stop();
    vt.Stop();
    uswt.Stop();

    num_failures += CheckEqualBound(ut.Get(), 0.5, 0.1);
    num_failures += CheckEqualBound(st.Get(), 0.5, 0.1);
    num_failures += CheckEqualBound(vt.Get(), 1.0, 0.1);

    double user, sys, wall;
    uswt.Get(&user, &sys, &wall);
    num_failures += CheckEqualBound(user, 0.5, 0.1);
    num_failures += CheckEqualBound(sys, 0.5, 0.1);
    EXPECT_GE(wall, 0.5);  // Anything more precise won't work with only 1 CPU.
  }

  return num_failures;
}

TEST(TimerTest, UsageTimerTests) {
#if defined(ABSL_HAVE_THREAD_SANITIZER)
  GTEST_SKIP() << "tsan distorts usage too much to be worthwhile for testing.";
#elif defined(__APPLE__)
  GTEST_SKIP() << "Darwin usage is flaky, even in the flaky timer loop.";
#endif
  DoFlakyTimerTestLoop(&DoUsageTimerTests);
}

#endif  // !defined(__native_client__)

TEST(TimerTest, CycleTimerTests) {
  // To detect unintended growth:
  EXPECT_EQ(16, sizeof(CycleTimer));
  EXPECT_EQ(8, sizeof(SimpleCycleTimer));

  CycleTimer ct;
  SimpleCycleTimer sct;

  EXPECT_TRUE(!ct.IsRunning());
  EXPECT_TRUE(!sct.IsRunning());
  ct.Start();
  sct.Start();
  EXPECT_TRUE(ct.IsRunning());
  EXPECT_TRUE(sct.IsRunning());

  absl::SleepFor(absl::Milliseconds(1000));

  double time = sct.Get();
  // Loose bounds:
  EXPECT_GT(time, 0.5);
  EXPECT_LT(time, 1.5);
  CheckEqual(sct.GetCycles(), ct.GetCycles());
  CheckEqual(sct.Get(), ct.Get());
  CheckEqual(sct.GetInMs(), ct.GetInMs());
  CheckEqual(sct.GetInUsec(), ct.GetInUsec());
  CheckEqualDuration(sct.GetDuration(), ct.GetDuration());

  ct.Stop();
  sct.Stop();
  EXPECT_TRUE(!ct.IsRunning());
  EXPECT_TRUE(!sct.IsRunning());

  EXPECT_GE(ct.Get(), time);
  EXPECT_GE(sct.Get(), time);

  ct.Start();
  sct.Start();
  EXPECT_TRUE(ct.IsRunning());
  EXPECT_TRUE(sct.IsRunning());
  absl::SleepFor(absl::Milliseconds(1000));

  // This continues to accumulate:
  EXPECT_GT(ct.Get(), 1.5);
  EXPECT_LT(ct.Get(), 2.5);
  // This started over:
  EXPECT_GT(sct.Get(), 0.5);
  EXPECT_LT(sct.Get(), 1.5);

  CycleTimer ct2;
  SimpleCycleTimer sct2;

  // Test increment while running:
  int64_t cycles = ct.GetCycles();
  ct.Increment(100000);
  CheckEqual(ct.GetCycles(), cycles + 100000);

  cycles = sct.GetCycles();
  sct.Increment(100000);
  CheckEqual(sct.GetCycles(), cycles + 100000);
}

TEST(TimerTest, ConversionTests) {
  CycleTimer ct;
  SimpleCycleTimer sct;

  for (int64_t ms = 0; ms < 1000000; ++ms) {
    ct.Reset();
    ct.Increment(ct.MsToCycles(ms));
    EXPECT_EQ(ct.GetInMs(), ms);

    int64_t cycles = ct.MsToCycles(ms);
    EXPECT_EQ(ct.CyclesToMs(cycles), ms);

    cycles = CycleTimerBase::MsToCycles(ms);
    EXPECT_EQ(CycleTimerBase::CyclesToMs(cycles), ms);

    int64_t expected_usec = ms * 1000;
    EXPECT_GE(ct.CyclesToUsec(cycles), expected_usec * 0.95);
    EXPECT_LE(ct.CyclesToUsec(cycles), expected_usec * 1.05);

    EXPECT_GE(CycleTimerBase::CyclesToUsec(cycles), expected_usec * 0.95);
    EXPECT_LE(CycleTimerBase::CyclesToUsec(cycles), expected_usec * 1.05);

    int64_t usec_to_cycles = ct.UsecToCycles(expected_usec);
    EXPECT_GE(usec_to_cycles, cycles * 0.95);
    EXPECT_LE(usec_to_cycles, cycles * 1.05);

    usec_to_cycles = CycleTimerBase::UsecToCycles(expected_usec);
    EXPECT_GE(usec_to_cycles, cycles * 0.95);
    EXPECT_LE(usec_to_cycles, cycles * 1.05);

    double expected_seconds = 0.001L * ms;
    EXPECT_GE(CycleTimerBase::CyclesToSeconds(cycles), expected_seconds * 0.95);
    EXPECT_LE(CycleTimerBase::CyclesToSeconds(cycles), expected_seconds * 1.05);

    int64_t seconds_to_cycles =
        CycleTimerBase::SecondsToCycles(expected_seconds);
    EXPECT_GE(seconds_to_cycles, cycles * 0.95);
    EXPECT_LE(seconds_to_cycles, cycles * 1.05);
  }

  for (int64_t ms = 1; ms < (1ull << 34); ms *= 2) {
    int64_t cycles = ct.MsToCycles(ms);
    EXPECT_EQ(ct.CyclesToMs(cycles), ms);

    int64_t sct_cycles = sct.MsToCycles(ms);
    EXPECT_EQ(cycles, sct_cycles);
    EXPECT_EQ(sct.CyclesToMs(sct_cycles), ms);

    int64_t ctb_cycles = CycleTimerBase::MsToCycles(ms);
    EXPECT_EQ(ctb_cycles, cycles);
    EXPECT_EQ(CycleTimerBase::CyclesToMs(ctb_cycles), ms);

    int64_t expected_usec = ms * 1000;
    EXPECT_GT(ct.CyclesToUsec(cycles), expected_usec * 0.95);
    EXPECT_LT(ct.CyclesToUsec(cycles), expected_usec * 1.05);

    EXPECT_GT(sct.CyclesToUsec(cycles), expected_usec * 0.95);
    EXPECT_LT(sct.CyclesToUsec(cycles), expected_usec * 1.05);

    EXPECT_GT(CycleTimerBase::CyclesToUsec(cycles), expected_usec * 0.95);
    EXPECT_LT(CycleTimerBase::CyclesToUsec(cycles), expected_usec * 1.05);

    int64_t usec_to_cycles = ct.UsecToCycles(expected_usec);
    EXPECT_GT(usec_to_cycles, cycles * 0.95);
    EXPECT_LT(usec_to_cycles, cycles * 1.05);

    usec_to_cycles = sct.UsecToCycles(expected_usec);
    EXPECT_GT(usec_to_cycles, cycles * 0.95);
    EXPECT_LT(usec_to_cycles, cycles * 1.05);

    usec_to_cycles = CycleTimerBase::UsecToCycles(expected_usec);
    EXPECT_GT(usec_to_cycles, cycles * 0.95);
    EXPECT_LT(usec_to_cycles, cycles * 1.05);

    double expected_seconds = 0.001L * ms;
    EXPECT_GT(CycleTimerBase::CyclesToSeconds(cycles), expected_seconds * 0.95);
    EXPECT_LT(CycleTimerBase::CyclesToSeconds(cycles), expected_seconds * 1.05);

    int64_t seconds_to_cycles =
        CycleTimerBase::SecondsToCycles(expected_seconds);
    EXPECT_GT(seconds_to_cycles, cycles * 0.95);
    EXPECT_LT(seconds_to_cycles, cycles * 1.05);
  }
}

TEST(TimerTest, CopyAndAssignTest) {
  SimpleCycleTimer a;
  a.Start();
  ASSERT_TRUE(a.IsRunning());

  // On AArch64, the "cycle timer" is actually fairly divorced from the CPU
  // clock; it can be substantially slower, running on the scale of megahertz
  // rather than gigahertz. Wait until at least one cycle has passed so that
  // copies of `a` will have nonzero cycle counts.
  while (a.GetCycles() == 0) {
    absl::SleepFor(absl::Nanoseconds(50));
  }

  SimpleCycleTimer b(a);
  EXPECT_TRUE(b.IsRunning());
  EXPECT_GT(b.GetCycles(), 0);

  b.Stop();
  EXPECT_TRUE(a.IsRunning());
  EXPECT_TRUE(!b.IsRunning());

  b = a;
  EXPECT_TRUE(b.IsRunning());
}

// gettimeofday(2) can return negative microsecond values in the timeval struct,
// probably due to the messed up way Linux handles time.
//
// This test validates that TimevalData handles this correctly.
//
TEST(TimerTest, NegativeTimevalTest) {
  struct timeval tv_start;
  struct timeval tv_stop;

  tv_start.tv_sec = 1164827942;
  tv_start.tv_usec = -2653;
  tv_stop.tv_sec = 1164827942;
  tv_stop.tv_usec = 25984;

  TimevalData test;
  test.Start(tv_start);
  test.Add(tv_stop);

  // Result should be 28637 microseconds
  EXPECT_EQ(test.GetDuration(), absl::Microseconds(28637));
  EXPECT_EQ(test.GetInMs(), 28);
  EXPECT_EQ(test.Get(), 0.028637);
}

template <class Timer>
class ScopedTimeTest : public ::testing::Test {};

#if !defined(__native_client__)
using ScopedTimeTestTypes =
    ::testing::Types<CycleTimer, SimpleCycleTimer, WallTimer, UserTimer,
                     SystemTimer, VirtualTimer>;
#else
using ScopedTimeTestTypes =
    ::testing::Types<CycleTimer, SimpleCycleTimer, WallTimer>;
#endif
TYPED_TEST_SUITE(ScopedTimeTest, ScopedTimeTestTypes);

TYPED_TEST(ScopedTimeTest, TypedTest) {
  TypeParam timer;
  EXPECT_TRUE(!timer.IsRunning());
  const double kShortInterval = 0.001;  // 1 ms

  {
    ScopedTime<TypeParam> scoped_time(&timer);
    EXPECT_TRUE(timer.IsRunning());

    // busy-wait for the short interval
    while (timer.Get() < kShortInterval) {
    }

    {
      ScopedTime<TypeParam> scoped_time1(&timer);

      // Start of nested scope shouldn't stop or reset the timer; if the timer
      // gets reset this interval should be shorter than the busy-wait above.
      EXPECT_TRUE(timer.IsRunning());
      EXPECT_GE(timer.Get(), kShortInterval);
    }

    // End of nested scope shouldn't stop or reset the timer.
    EXPECT_TRUE(timer.IsRunning());
    EXPECT_GE(timer.Get(), kShortInterval);
  }

  // End of original scope should stop but not reset the timer.
  EXPECT_TRUE(!timer.IsRunning());
  EXPECT_GE(timer.Get(), kShortInterval);
}

#if !PORTABLE_BASE  // TODO: Portable benchmark.h

static void BM_CyclesToSeconds(benchmark::State& state) {
  CycleTimer ct;
  double result = 0;
  int64_t val = 123456789;
  for (auto _ : state) {
    result += ct.CyclesToSeconds(val);
    ++val;
  }
  if (result == -1) {
    printf("Dummy use\n");
  }
}
BENCHMARK(BM_CyclesToSeconds);

static void BM_SecondsToCycles(benchmark::State& state) {
  CycleTimer ct;
  int64_t result = 0;
  double val = 12.3456;
  for (auto _ : state) {
    result += ct.SecondsToCycles(val);
    val += 0.1;
  }
  if (result == -1) {
    printf("Dummy use\n");
  }
}
BENCHMARK(BM_SecondsToCycles);

static void BM_CyclesToMs(benchmark::State& state) {
  CycleTimer ct;
  int64_t val = (1ll << state.range(0));
  int64_t result = 0;
  for (auto _ : state) {
    result += ct.CyclesToMs(val);
    val++;
  }
  if (result == -1) {
    printf("Dummy use\n");
  }
}
BENCHMARK(BM_CyclesToMs)->Arg(1)->Arg(40);

static void BM_MsToCycles(benchmark::State& state) {
  CycleTimer ct;
  int64_t val = (1ll << state.range(0));
  int64_t result = 0;
  for (auto _ : state) {
    result += ct.MsToCycles(val);
    val++;
  }
  if (result == -1) {
    printf("Dummy use\n");
  }
}
BENCHMARK(BM_MsToCycles)->Arg(1)->Arg(40);

static void BM_CyclesToUsec(benchmark::State& state) {
  CycleTimer ct;
  int64_t val = (1ll << state.range(0));
  int64_t result = 0;
  for (auto _ : state) {
    result += ct.CyclesToUsec(val);
    val++;
  }
  if (result == -1) {
    printf("Dummy use\n");
  }
}
BENCHMARK(BM_CyclesToUsec)->Arg(1)->Arg(40);

static void BM_UsecToCycles(benchmark::State& state) {
  CycleTimer ct;
  int64_t val = (1ll << state.range(0));
  int64_t result = 0;
  for (auto _ : state) {
    result += ct.UsecToCycles(val);
    val++;
  }
  if (result == -1) {
    printf("Dummy use\n");
  }
}
BENCHMARK(BM_UsecToCycles)->Arg(1)->Arg(40);

static void BM_CTBaseCyclesToUsec(benchmark::State& state) {
  int64_t val = (1ll << state.range(0));
  int64_t result = 0;
  for (auto _ : state) {
    result += CycleTimerBase::CyclesToUsec(val);
    val++;
  }
  if (result == -1) {
    printf("Dummy use\n");
  }
}
BENCHMARK(BM_CTBaseCyclesToUsec)->Arg(1)->Arg(40);

static void BM_CTBaseUsecToCycles(benchmark::State& state) {
  CycleTimer ct;
  int64_t val = (1ll << state.range(0));
  int64_t result = 0;
  for (auto _ : state) {
    result += CycleTimerBase::UsecToCycles(val);
    val++;
  }
  if (result == -1) {
    printf("Dummy use\n");
  }
}
BENCHMARK(BM_CTBaseUsecToCycles)->Arg(1)->Arg(40);

static void BM_Get(benchmark::State& state) {
  CycleTimer ct;
  ct.Start();
  ct.Increment(int64_t{1} << state.range(0));
  double result = 0;
  for (auto _ : state) {
    result += ct.Get();
  }
  if (result == -1) {
    printf("Dummy use\n");
  }
}
BENCHMARK(BM_Get)->Arg(0)->Arg(40);

static void BM_GetInMs(benchmark::State& state) {
  CycleTimer ct;
  ct.Start();
  ct.Increment(int64_t{1} << state.range(0));
  int64_t result = 0;
  for (auto _ : state) {
    result += ct.GetInMs();
  }
  if (result == -1) {
    printf("Dummy use\n");
  }
}
BENCHMARK(BM_GetInMs)->Arg(0)->Arg(40);

static void BM_GetInUsec(benchmark::State& state) {
  CycleTimer ct;
  ct.Start();
  ct.Increment(int64_t{1} << state.range(0));
  int64_t result = 0;
  for (auto _ : state) {
    result += ct.GetInUsec();
  }
  if (result == -1) {
    printf("Dummy use\n");
  }
}
BENCHMARK(BM_GetInUsec)->Arg(0)->Arg(40);

#endif  // !PORTABLE_BASE
