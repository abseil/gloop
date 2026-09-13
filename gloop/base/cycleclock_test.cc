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

// A test for CycleClock.

#include "gloop/base/cycleclock.h"

#include <chrono>  // NOLINT(build/c++11)
#include <cstdint>
#include <thread>  // NOLINT(build/c++11)

#include "absl/base/internal/unscaledcycleclock_config.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "gloop/base/config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#if !PORTABLE_BASE  // TODO: Remove when benchmark.h is portable.
#include "benchmark/benchmark.h"
#endif  // !PORTABLE_BASE

namespace {

using ::testing::DoubleNear;

// Note that the time returned by SteadyClockNanos() uses an
// arbitrary epoch, not necessarily the Unix epoch.
int64_t SteadyClockNanos() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// A matching pair of time readings, read at about the same time.  One from the
// kernel in nanoseconds, and the other from the cycle counter.
struct TimePair {
  int64_t ns;
  int64_t cycles;
};

// Return the cycle counter, placing in *time_ns the kernel
// time read at a moment close to the read of the cycle counter.
// This is accomplished by making several attempts and
// picking the one with the lowest latency.
TimePair GetTimePair() {
  TimePair best;
  int64_t best_interval_ns = 1000 * 1000 * 1000;
  for (int i = 0; i != 10; i++) {
    int64_t start_ns = SteadyClockNanos();
    int64_t cycles = CycleClock::Now();
    int64_t end_ns = SteadyClockNanos();
    if (start_ns <= end_ns && end_ns - start_ns < best_interval_ns) {
      best.ns = end_ns;
      best.cycles = cycles;
      best_interval_ns = end_ns - start_ns;
    }
  }
  return best;
}

TEST(CycleClockTest, Frequency) {
  for (int i = 0; i != 10; i++) {
    TimePair start = GetTimePair();
    // Don't use absl::SleepFor() since it depends on CycleClock which could be
    // broken.
    std::this_thread::sleep_for(std::chrono::seconds(1));
    TimePair end = GetTimePair();
    double measured_frequency = static_cast<double>(end.cycles - start.cycles) *
                                1e9 / static_cast<double>(end.ns - start.ns);
    double returned_frequency = CycleClock::Frequency();
    VLOG(1) << "measured_frequency " << measured_frequency
            << "   returned_frequency " << returned_frequency << "   ratio "
            << returned_frequency / measured_frequency;
    // Check that the measured frequency is within 1% of
    // the nominal frequency reported.
    EXPECT_THAT(returned_frequency,
                DoubleNear(measured_frequency, measured_frequency * 0.01));
  }
}

#if !PORTABLE_BASE  // TODO: Remove when benchmark.h is portable.
void BM_Now(benchmark::State& state) {
  int64_t c = 0;
  for (auto _ : state) {
    c = CycleClock::Now();
    asm volatile("" ::[r] "r"(c));
  }
  VLOG(1) << c;
}
BENCHMARK(BM_Now);

void BM_NowWithRegisterPresure(benchmark::State& state) {
  int64_t c = 0;
  for (auto _ : state) {
    intptr_t r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0;
    intptr_t r5 = 0, r6 = 0, r7 = 0, r8 = 0, r9 = 0;
    asm volatile("" : [r] "+r"(r0)::"memory");
    asm volatile("" : [r] "+r"(r1)::"memory");
    asm volatile("" : [r] "+r"(r2)::"memory");
    asm volatile("" : [r] "+r"(r3)::"memory");
    asm volatile("" : [r] "+r"(r4)::"memory");
    asm volatile("" : [r] "+r"(r5)::"memory");
    asm volatile("" : [r] "+r"(r6)::"memory");
    asm volatile("" : [r] "+r"(r7)::"memory");
    asm volatile("" : [r] "+r"(r8)::"memory");
    asm volatile("" : [r] "+r"(r9)::"memory");

    c = CycleClock::Now();
    asm volatile("" ::[r] "r"(c));

    asm volatile("" : [r] "+r"(r0)::"memory");
    asm volatile("" : [r] "+r"(r1)::"memory");
    asm volatile("" : [r] "+r"(r2)::"memory");
    asm volatile("" : [r] "+r"(r3)::"memory");
    asm volatile("" : [r] "+r"(r4)::"memory");
    asm volatile("" : [r] "+r"(r5)::"memory");
    asm volatile("" : [r] "+r"(r6)::"memory");
    asm volatile("" : [r] "+r"(r7)::"memory");
    asm volatile("" : [r] "+r"(r8)::"memory");
    asm volatile("" : [r] "+r"(r9)::"memory");
    QCHECK_EQ(r0 + r1 + r2 + r3 + r4 + r5 + r6 + r7 + r8 + r9, 0);
  }
  VLOG(1) << c;
}
BENCHMARK(BM_NowWithRegisterPresure);

#endif

}  // namespace
