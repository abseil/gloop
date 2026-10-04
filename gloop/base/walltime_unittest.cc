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

#include "gloop/base/walltime.h"

#include <cmath>
#include <cstdint>
#ifdef __linux__
#include <syscall.h>  // for __NR_gettimeofday
#endif
#include <sys/time.h>
#include <time.h>

#include <limits>

#include "absl/base/internal/cycleclock.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {
using absl::InfiniteFuture;
using absl::InfinitePast;
using absl::base_internal::CycleClock;  // NOLINT
using base::FromWallTime;
using base::ToWallTime;
using testing::DoubleEq;
using testing::ExplainMatchResult;

// Test that WallTime_CPS() returns a cycles-per-second value,
// WallTime_SPC() returns the inverse.
TEST(Walltime, CyclesPerSecond) {
  int64_t start_cycles = CycleClock::Now();
  WallTime start_walltime = absl::GetCurrentTimeNanos() / 1e9;
  absl::SleepFor(absl::Milliseconds(100));
  int64_t end_cycles = CycleClock::Now();
  WallTime end_walltime = absl::GetCurrentTimeNanos() / 1e9;

  int64_t elapsed_cycles = end_cycles - start_cycles;
  WallTime elapsed_walltime = end_walltime - start_walltime;
  double cycles_per_second = elapsed_cycles / elapsed_walltime;
  double seconds_per_cycle = elapsed_walltime / elapsed_cycles;
  double epsilon = 0.02;

  EXPECT_LT(cycles_per_second, WallTime_CPS() * (1.0 + epsilon));
  EXPECT_GT(cycles_per_second, WallTime_CPS() * (1.0 - epsilon));

  EXPECT_LT(seconds_per_cycle, WallTime_SPC() * (1.0 + epsilon));
  EXPECT_GT(seconds_per_cycle, WallTime_SPC() * (1.0 - epsilon));
}

TEST(Walltime, WallTimeConverter) {
  WallTimeConverter wtcc = WallTimeConverter::Now();
  absl::SleepFor(absl::Milliseconds(100));

  int64_t hundred_millis_later_cycletime = CycleClock::Now();
  WallTime hundred_millis_later_walltime = absl::GetCurrentTimeNanos() / 1e9;

  // Check that we are no more than 3ms away from the expected time
  // mrtest --runs_per_test=2000 had one failure at 1ms.
  double delta = (wtcc.CycleToWallTime(hundred_millis_later_cycletime) -
                  hundred_millis_later_walltime);

  EXPECT_GT(delta, -0.003);
  EXPECT_LT(delta, 0.003);
}

MATCHER(WallTimeRoundTrips, "") {
  return ExplainMatchResult(DoubleEq(arg), ToWallTime(FromWallTime(arg)),
                            result_listener);
}

TEST(BaseTime, RoundtripConversion) {
  // FromWallTime() and ToWallTime()
  WallTime now_wt = absl::GetCurrentTimeNanos() / 1e9;
  EXPECT_THAT(-1.5, WallTimeRoundTrips());
  EXPECT_THAT(-1.0, WallTimeRoundTrips());
  EXPECT_THAT(-0.5, WallTimeRoundTrips());
  EXPECT_THAT(0.0, WallTimeRoundTrips());
  EXPECT_THAT(0.5, WallTimeRoundTrips());
  EXPECT_THAT(1.0, WallTimeRoundTrips());
  EXPECT_THAT(1.5, WallTimeRoundTrips());
  EXPECT_THAT(now_wt, WallTimeRoundTrips());
}

TEST(Time, WallTimeLimits) {
  EXPECT_EQ(FromWallTime(-std::numeric_limits<WallTime>::max()),
            InfinitePast());
  EXPECT_EQ(FromWallTime(-std::numeric_limits<WallTime>::infinity()),
            InfinitePast());

  EXPECT_EQ(FromWallTime(std::numeric_limits<WallTime>::max()),
            InfiniteFuture());
  EXPECT_EQ(FromWallTime(std::numeric_limits<WallTime>::infinity()),
            InfiniteFuture());

  EXPECT_EQ(ToWallTime(InfiniteFuture()),
            std::numeric_limits<WallTime>::infinity());
  EXPECT_EQ(ToWallTime(InfinitePast()),
            -std::numeric_limits<WallTime>::infinity());
}

TEST(Time, WallTimeResolution) {
  // The resolution of a (64-bit) WallTime is currently just over 238ns.
  // This will double when "seconds since the Unix epoch" consumes another
  // bit early in 2038, and the test will fail.  Hopefully WallTime is
  // long gone by then.  This is mostly just for documentation purposes.
  WallTime t0 = absl::GetCurrentTimeNanos() / 1e9;
  WallTime t1 = std::nextafter(t0, std::numeric_limits<WallTime>::max());
  EXPECT_NEAR(238e-9, t1 - t0, 0.5e-9);
}

static void BM_gettimeofday(benchmark::State& state) {
  WallTime w = 1;
  for (auto _ : state) {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    w = tv.tv_sec + tv.tv_usec / 1000000.0;
  }
  benchmark::DoNotOptimize(w);
}
BENCHMARK(BM_gettimeofday);

#ifdef __linux__
static void BM_syscall_gettimeofday(benchmark::State& state) {
  WallTime w = 1;
  for (auto _ : state) {
    struct timeval tv;
    syscall(__NR_gettimeofday, &tv, nullptr);
    w = tv.tv_sec + tv.tv_usec / 1000000.0;
  }
  benchmark::DoNotOptimize(w);
}
BENCHMARK(BM_syscall_gettimeofday);
#endif

static void BM_CycleClock(benchmark::State& state) {
  int64_t c = 1;
  for (auto _ : state) {
    c = CycleClock::Now();
  }
  benchmark::DoNotOptimize(c);
}
BENCHMARK(BM_CycleClock);

static void BM_Time(benchmark::State& state) {
  time_t t;
  for (auto _ : state) {
    t = time(nullptr);
  }
  benchmark::DoNotOptimize(t);
}
BENCHMARK(BM_Time);

//
// To/FromWallTime
//

void BM_FromWallTime(benchmark::State& state) {
  absl::Time t = absl::UnixEpoch();
  WallTime now = absl::GetCurrentTimeNanos() / 1e9;
  for (auto _ : state) {
    t = base::FromWallTime(now);
  }
  benchmark::DoNotOptimize(t);
}
BENCHMARK(BM_FromWallTime);

void BM_ToWallTime(benchmark::State& state) {
  WallTime t = 0.0;
  absl::Time now = absl::Now();
  for (auto _ : state) {
    t = base::ToWallTime(now);
  }
  benchmark::DoNotOptimize(t);
}
BENCHMARK(BM_ToWallTime);

void BM_ToWallTime_Infinite(benchmark::State& state) {
  WallTime t = 0.0;
  absl::Time inf = absl::InfiniteFuture();
  for (auto _ : state) {
    t = base::ToWallTime(inf);
  }
  benchmark::DoNotOptimize(t);
}
BENCHMARK(BM_ToWallTime_Infinite);
}  // namespace
