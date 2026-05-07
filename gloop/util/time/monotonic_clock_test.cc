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

#include "gloop/util/time/monotonic_clock.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/base/log_severity.h"
#include "absl/base/thread_annotations.h"
#include "absl/flags/flag.h"
#include "absl/functional/bind_front.h"
#include "absl/log/log.h"
#include "absl/log/scoped_mock_log.h"
#include "absl/random/random.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/clock_interface.h"
#include "absl/time/simulated_clock.h"
#include "absl/time/time.h"
#include "gloop/base/commandlineflags.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"
#include "gloop/util/random/acmrandom.h"
#include "gloop/util/time/monotonic_clock-internal.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

// absl::Now() recomputes clock drift approx. every 2 seconds, so run real
// clock tests for at least that long.
static constexpr double kDefaultRealTestSecs = 2.5;

ABSL_FLAG(double, real_test_secs, kDefaultRealTestSecs,
          "Number of seconds to run real-time clock tests");

namespace util {
namespace monotonic_clock_internal {
struct State;
}  // namespace monotonic_clock_internal
}  // namespace util

using testing::_;
using testing::EndsWith;
using testing::HasSubstr;
using testing::InSequence;

namespace util {

using monotonic_clock_internal::CreateMonotonicClock;
using monotonic_clock_internal::CreateMonotonicClockState;
using monotonic_clock_internal::DeleteMonotonicClockState;
using monotonic_clock_internal::State;
using monotonic_clock_internal::SynchronizedMonotonicClockReset;

// Run each non-deterministic test with a different seed unless
// --test_random_seed is present on the commandline.  Print the seed for
// reproducibility.
static int32_t GetRandomSeed(const std::string& test_name) {
  int32_t seed;
  if (!base::WasPresentOnCommandLine("test_random_seed")) {
    seed = GTEST_FLAG_GET(random_seed);
  } else {
    seed = ACMRandom::HostnamePidTimeSeed();
  }
  LOG(INFO) << test_name << " random seed: " << seed;
  return seed;
}

class MonotonicClockTest : public testing::Test {
 protected:
  MonotonicClockTest() = default;
  ~MonotonicClockTest() override = default;

  void SetUp() override { SynchronizedMonotonicClockReset(); }

  void VerifyCorrectionMetrics(MonotonicClock* clock,
                               int num_corrections_expect,
                               double max_correction_expect) {
    int clock_num_corrections;
    double clock_max_correction;
    clock->GetCorrectionMetrics(&clock_num_corrections, &clock_max_correction);
    ASSERT_EQ(num_corrections_expect, clock_num_corrections);
    ASSERT_DOUBLE_EQ(max_correction_expect, clock_max_correction);
  }

  // This test produces no time corrections.
  void TestSimulatedForwardTime(absl::SimulatedClock* sim_clock,
                                MonotonicClock* mono_clock) {
    absl::Time base_time = sim_clock->TimeNow();
    ASSERT_EQ(base_time, mono_clock->TimeNow());
    sim_clock->AdvanceTime(absl::Seconds(10));
    ASSERT_EQ(base_time + absl::Seconds(10), sim_clock->TimeNow());
    ASSERT_EQ(base_time + absl::Seconds(10), mono_clock->TimeNow());
    sim_clock->AdvanceTime(absl::Seconds(10));
    ASSERT_EQ(base_time + absl::Seconds(20), sim_clock->TimeNow());
    ASSERT_EQ(base_time + absl::Seconds(20), mono_clock->TimeNow());
    sim_clock->AdvanceTime(absl::Seconds(5));
    ASSERT_EQ(base_time + absl::Seconds(25), sim_clock->TimeNow());
    ASSERT_EQ(base_time + absl::Seconds(25), mono_clock->TimeNow());
    VerifyCorrectionMetrics(mono_clock, 0, 0.0);
  }

  // This test produces three correction callbacks: one with arguments
  // (50, 100, 100), one with (80, 90, 100), and one with (60, 105, 105).
  void TestSimulatedBackwardTime(absl::SimulatedClock* sim_clock,
                                 MonotonicClock* mono_clock) {
    absl::Time base_time = sim_clock->TimeNow();
    sim_clock->AdvanceTime(absl::Seconds(100));
    ASSERT_EQ(base_time + absl::Seconds(100), sim_clock->TimeNow());
    ASSERT_EQ(base_time + absl::Seconds(100), mono_clock->TimeNow());
    VerifyCorrectionMetrics(mono_clock, 0, 0.0);
    // Time moves backward -- expect a correction.
    sim_clock->AdvanceTime(absl::Seconds(-50));
    ASSERT_EQ(base_time + absl::Seconds(50), sim_clock->TimeNow());
    ASSERT_EQ(base_time + absl::Seconds(100),  // correction
              mono_clock->TimeNow());
    VerifyCorrectionMetrics(mono_clock, 1, 50.0);
    // Time moves forward, but not enough to exceed the last value returned by
    // TimeNow(). No correction in this case.
    sim_clock->AdvanceTime(absl::Seconds(20));
    ASSERT_EQ(base_time + absl::Seconds(70), sim_clock->TimeNow());
    ASSERT_EQ(base_time + absl::Seconds(100), mono_clock->TimeNow());
    VerifyCorrectionMetrics(mono_clock, 1, 50.0);
    sim_clock->AdvanceTime(absl::Seconds(20));
    ASSERT_EQ(base_time + absl::Seconds(90), sim_clock->TimeNow());
    ASSERT_EQ(base_time + absl::Seconds(100), mono_clock->TimeNow());
    VerifyCorrectionMetrics(mono_clock, 1, 50.0);
    // Time moves backwards again -- expect a correction.
    sim_clock->AdvanceTime(absl::Seconds(-10));
    ASSERT_EQ(base_time + absl::Seconds(80), sim_clock->TimeNow());
    ASSERT_EQ(base_time + absl::Seconds(100),  // correction
              mono_clock->TimeNow());
    VerifyCorrectionMetrics(mono_clock, 2, 50.0);
    // Time moves forward enough to advance monotonic time.
    sim_clock->AdvanceTime(absl::Seconds(25));
    ASSERT_EQ(base_time + absl::Seconds(105), sim_clock->TimeNow());
    ASSERT_EQ(base_time + absl::Seconds(105), mono_clock->TimeNow());
    VerifyCorrectionMetrics(mono_clock, 2, 50.0);
    // Time moves backward again.
    sim_clock->AdvanceTime(absl::Seconds(-45));
    ASSERT_EQ(base_time + absl::Seconds(60), sim_clock->TimeNow());
    ASSERT_EQ(base_time + absl::Seconds(105),  // correction
              mono_clock->TimeNow());
    VerifyCorrectionMetrics(mono_clock, 3, 50.0);

    // Reset metrics and re-verify.
    mono_clock->ResetCorrectionMetrics();
    VerifyCorrectionMetrics(mono_clock, 0, 0.0);
  }

  // Test that the Sleep/SleepUntil calls do not return until monotonic time
  // passes the requested wakeup time.
  void TestRandomSleep(MonotonicClock* mono_clock) {
    ACMRandom random(GetRandomSeed("RandomSleep"));
    const int kNumSamples = 5;

    // Sleep.
    for (int i = 0; i < kNumSamples; i++) {
      absl::Duration sleep_time = absl::Seconds(
          absl::Uniform<float>(absl::IntervalOpen, random, 0, 1) / 5.0);
      absl::Time before = mono_clock->TimeNow();
      absl::Time wakeup_time = before + sleep_time;
      mono_clock->Sleep(sleep_time);
      absl::Time after = mono_clock->TimeNow();
      ASSERT_LE(wakeup_time, after);
    }

    // SleepUntil.
    for (int i = 0; i < kNumSamples; i++) {
      absl::Duration sleep_time = absl::Seconds(
          absl::Uniform<float>(absl::IntervalOpen, random, 0, 1) / 5.0);
      absl::Time before = mono_clock->TimeNow();
      absl::Time wakeup_time = before + sleep_time;
      mono_clock->SleepUntil(wakeup_time);
      absl::Time after = mono_clock->TimeNow();
      ASSERT_LE(wakeup_time, after);
    }
  }
};

// Use a custom callback to verify the parameters passed to the callback.
class MockCallback {
 public:
  MOCK_METHOD(void, CorrectionCallback,
              (absl::Time raw_time, absl::Time last_raw_time,
               absl::Time last_monotonic_time));
};

// Time moves forward only -- there should be no time corrections.
TEST_F(MonotonicClockTest, SimulatedForwardTime) {
  MockCallback cb;
  absl::SimulatedClock sim_clock;
  MonotonicClock* mono_clock = MonotonicClock::CreateMonotonicClock(&sim_clock);
  mono_clock->set_correction_callback(
      absl::bind_front(&MockCallback::CorrectionCallback, &cb));
  // It is an error to receive a correction callback in this test.
  EXPECT_CALL(cb, CorrectionCallback(_, _, _)).Times(0);
  TestSimulatedForwardTime(&sim_clock, mono_clock);
  delete mono_clock;
}

// Time moves forward and backward.  The default correction callback should
// print 3 log messages (see TestSimulatedBackwardTime).
TEST_F(MonotonicClockTest, SimulatedBackwardTime) {
  absl::ScopedMockLog log;
  absl::SimulatedClock sim_clock;
  MonotonicClock* mono_clock = MonotonicClock::CreateMonotonicClock(&sim_clock);
  EXPECT_CALL(log,
              Log(absl::LogSeverity::kWarning, EndsWith("/monotonic_clock.cc"),
                  HasSubstr("Time jumped backward")))
      .Times(3);
  log.StartCapturingLogs();
  TestSimulatedBackwardTime(&sim_clock, mono_clock);
  delete mono_clock;
}

// Time moves forward and backward.  The correction callback should be called 3
// times with the indicated parameters (see TestSimulatedBackwardTime).
TEST_F(MonotonicClockTest, CustomCallback) {
  absl::SimulatedClock sim_clock;
  MonotonicClock* mono_clock = MonotonicClock::CreateMonotonicClock(&sim_clock);
  MockCallback cb;
  mono_clock->set_correction_callback(
      absl::bind_front(&MockCallback::CorrectionCallback, &cb));
  InSequence dummy;
  EXPECT_CALL(cb, CorrectionCallback(absl::FromUnixSeconds(50),
                                     absl::FromUnixSeconds(100),
                                     absl::FromUnixSeconds(100)))
      .Times(1);
  EXPECT_CALL(cb, CorrectionCallback(absl::FromUnixSeconds(80),
                                     absl::FromUnixSeconds(90),
                                     absl::FromUnixSeconds(100)))
      .Times(1);
  EXPECT_CALL(cb, CorrectionCallback(absl::FromUnixSeconds(60),
                                     absl::FromUnixSeconds(105),
                                     absl::FromUnixSeconds(105)))
      .Times(1);
  TestSimulatedBackwardTime(&sim_clock, mono_clock);
  // Ensure that resetting the correction callback still works.
  absl::Time mono_time = mono_clock->TimeNow();
  mono_clock->set_default_correction_callback();
  sim_clock.AdvanceTime(absl::Seconds(-1));
  absl::ScopedMockLog log;
  EXPECT_CALL(log,
              Log(absl::LogSeverity::kWarning, EndsWith("/monotonic_clock.cc"),
                  HasSubstr("Time jumped backward")))
      .Times(1);
  log.StartCapturingLogs();
  ASSERT_EQ(mono_time, mono_clock->TimeNow());
  delete mono_clock;
}

// Test that it's okay to set correction callback to NULL.
TEST_F(MonotonicClockTest, NullCorrectionCallback) {
  absl::SimulatedClock sim_clock;
  MonotonicClock* mono_clock = MonotonicClock::CreateMonotonicClock(&sim_clock);
  mono_clock->set_correction_callback(nullptr);
  TestSimulatedBackwardTime(&sim_clock, mono_clock);
  // Ensure that changing the correction callback back to non-NULL still works.
  absl::Time sim_time = sim_clock.TimeNow();
  absl::Time mono_time = mono_clock->TimeNow();
  MockCallback cb;
  mono_clock->set_correction_callback(
      absl::bind_front(&MockCallback::CorrectionCallback, &cb));
  sim_clock.AdvanceTime(absl::Seconds(-1));
  EXPECT_CALL(
      cb, CorrectionCallback(sim_time - absl::Seconds(1), sim_time, mono_time))
      .Times(1);
  ASSERT_EQ(mono_time, mono_clock->TimeNow());
  delete mono_clock;
}

// This correction callback calls the GetCorrectionMetrics function.
class GetCMCallback {
 public:
  explicit GetCMCallback(MonotonicClock* mono_clock)
      : clock(mono_clock), ncalls(0) {}
  void CorrectionCallback(absl::Time raw_time, absl::Time last_raw_time,
                          absl::Time last_monotonic_time) {
    ++ncalls;
    int correction_count;
    double max_correction;
    clock->GetCorrectionMetrics(&correction_count, &max_correction);
    ASSERT_EQ(ncalls, correction_count);
  }
  MonotonicClock* clock;
  int ncalls;
};

TEST_F(MonotonicClockTest, GetCMCallback) {
  absl::SimulatedClock sim_clock;
  MonotonicClock* mono_clock = MonotonicClock::CreateMonotonicClock(&sim_clock);
  GetCMCallback cb(mono_clock);
  mono_clock->set_correction_callback(
      absl::bind_front(&GetCMCallback::CorrectionCallback, &cb));
  TestSimulatedBackwardTime(&sim_clock, mono_clock);
  ASSERT_EQ(3, cb.ncalls);
  delete mono_clock;
}

// This correction callback calls the ResetCorrectionMetrics function.
class ResetCMCallback {
 public:
  explicit ResetCMCallback(MonotonicClock* mono_clock)
      : clock(mono_clock), ncalls(0) {}
  void CorrectionCallback(absl::Time raw_time, absl::Time last_raw_time,
                          absl::Time last_monotonic_time) {
    ++ncalls;
    clock->ResetCorrectionMetrics();
  }
  MonotonicClock* clock;
  int ncalls;
};

TEST_F(MonotonicClockTest, ResetCMCallback) {
  absl::SimulatedClock sim_clock;
  MonotonicClock* mono_clock = MonotonicClock::CreateMonotonicClock(&sim_clock);
  ResetCMCallback cb(mono_clock);
  mono_clock->set_correction_callback(
      absl::bind_front(&ResetCMCallback::CorrectionCallback, &cb));
  // We can't use TestSimulatedBackwardTime because it expects the correction
  // count to increase.
  absl::Time base_time = sim_clock.TimeNow();
  sim_clock.AdvanceTime(absl::Seconds(100));
  ASSERT_EQ(base_time + absl::Seconds(100), mono_clock->TimeNow());
  sim_clock.AdvanceTime(absl::Seconds(-10));
  ASSERT_EQ(base_time + absl::Seconds(100), mono_clock->TimeNow());
  ASSERT_EQ(1, cb.ncalls);
  VerifyCorrectionMetrics(mono_clock, 0, 0.0);
  sim_clock.AdvanceTime(absl::Seconds(-10));
  ASSERT_EQ(base_time + absl::Seconds(100), mono_clock->TimeNow());
  ASSERT_EQ(2, cb.ncalls);
  VerifyCorrectionMetrics(mono_clock, 0, 0.0);
  delete mono_clock;
}

// Take a random walk through time.
TEST_F(MonotonicClockTest, SimulatedRandomWalk) {
  absl::SimulatedClock sim_clock;
  MonotonicClock* mono_clock = MonotonicClock::CreateMonotonicClock(&sim_clock);
  MockCallback cb;
  mono_clock->set_correction_callback(
      absl::bind_front(&MockCallback::CorrectionCallback, &cb));
  sim_clock.AdvanceTime(absl::Now() - sim_clock.TimeNow());
  ASSERT_EQ(sim_clock.TimeNow(), mono_clock->TimeNow());

  // Generate kNumSamples random clock adjustments.
  const int kNumSamples = 5;
  ACMRandom random(GetRandomSeed("RandomWalk"));
  // Keep track of maximum time on clock and corrections.
  absl::Time max_time = sim_clock.TimeNow();
  int num_corrections = 0;
  absl::Duration max_correction = absl::ZeroDuration();
  for (int i = 0; i < kNumSamples; i++) {
    absl::Duration jump =
        absl::Seconds(absl::Uniform<float>(absl::IntervalOpen, random, 0, 1) -
                      0.5);  // (-0.5s, 0.5s)
    absl::Time sim_last_time = sim_clock.TimeNow();
    sim_clock.AdvanceTime(jump);
    absl::Time sim_time = sim_clock.TimeNow();
    if (jump < absl::ZeroDuration()) {
      ASSERT_LT(sim_time, max_time);
      absl::Duration correction = max_time - sim_time;
      if (correction > max_correction) {
        max_correction = correction;
      }
      ++num_corrections;
      EXPECT_CALL(cb, CorrectionCallback(sim_time, sim_last_time, max_time))
          .Times(1);
    }
    if (sim_clock.TimeNow() > max_time) {
      max_time = sim_clock.TimeNow();
    }
    ASSERT_EQ(max_time, mono_clock->TimeNow());
  }
  VerifyCorrectionMetrics(mono_clock, num_corrections,
                          absl::FDivDuration(max_correction, absl::Seconds(1)));
  delete mono_clock;
}

TEST_F(MonotonicClockTest, RealTime) {
  MonotonicClock* mono_clock =
      MonotonicClock::CreateMonotonicClock(&absl::Clock::GetRealClock());
  // Call mono_clock->Now() continuously for FLAGS_real_test_secs seconds.
  absl::Time start = absl::Now();
  absl::Time time = start;
  int64_t num_calls = 0;
  do {
    absl::Time last_time = time;
    time = mono_clock->TimeNow();
    ASSERT_LE(last_time, time);
    ++num_calls;
  } while (time - start < absl::Seconds(absl::GetFlag(FLAGS_real_test_secs)));
  // Just out of curiousity -- did real clock go backwards?
  int clock_num_corrections;
  mono_clock->GetCorrectionMetrics(&clock_num_corrections, nullptr);
  LOG(INFO) << clock_num_corrections << " corrections in " << num_calls
            << " calls to mono_clock->Now()";
  delete mono_clock;
}

// Test the Sleep interface using a MonotonicClock.
TEST_F(MonotonicClockTest, RandomSleep) {
  MonotonicClock* mono_clock =
      MonotonicClock::CreateMonotonicClock(&absl::Clock::GetRealClock());
  TestRandomSleep(mono_clock);
  delete mono_clock;
}

// Test the Sleep interface using a SynchronizedMonotonicClock.
TEST_F(MonotonicClockTest, RandomSleepSynced) {
  MonotonicClock* mono_clock =
      MonotonicClock::CreateSynchronizedMonotonicClock();
  TestRandomSleep(mono_clock);
  delete mono_clock;
}

// Test that SleepUntil has no effect if monotonic time has passed the
// requested wakeup time.
TEST_F(MonotonicClockTest, SimulatedInsomnia) {
  absl::SimulatedClock sim_clock;
  MonotonicClock* mono_clock = MonotonicClock::CreateMonotonicClock(&sim_clock);
  sim_clock.AdvanceTime(absl::Now() - sim_clock.TimeNow());
  ASSERT_EQ(sim_clock.TimeNow(), mono_clock->TimeNow());

  sim_clock.AdvanceTime(absl::Seconds(-3.14159));
  // Even though sim_clock will never advance, this call will not sleep
  // because monotonic_time has already advanced beyond the wakeup time.
  mono_clock->SleepUntil(sim_clock.TimeNow() + absl::Seconds(1));
  // Note that the same test can't be performed with Sleep because the argument
  // to sleep is an offset from monotonic time, not raw time.
  delete mono_clock;
}

// Two monotonic clocks, clock1 and clock2, each synced to the same
// raw clock.  Advance simulated time, read one clock, regress simulated
// time, and read the other clock.  The values should be the same.
TEST_F(MonotonicClockTest, SyncedPair) {
  absl::SimulatedClock sim_clock;
  State* state = CreateMonotonicClockState(&sim_clock);
  MonotonicClock* clock1 = CreateMonotonicClock(state);
  MonotonicClock* clock2 = CreateMonotonicClock(state);
  sim_clock.AdvanceTime(absl::Seconds(1000));
  ASSERT_EQ(sim_clock.TimeNow(), clock1->TimeNow());
  ASSERT_EQ(sim_clock.TimeNow(), clock2->TimeNow());

  absl::Time time1, time2;
  sim_clock.AdvanceTime(absl::Seconds(2));
  time1 = clock1->TimeNow();
  ASSERT_EQ(sim_clock.TimeNow(), time1);
  sim_clock.AdvanceTime(absl::Seconds(-5));
  time2 = clock2->TimeNow();
  ASSERT_EQ(time1, time2);
  VerifyCorrectionMetrics(clock1, 0, 0.0);
  VerifyCorrectionMetrics(clock2, 1, 5.0);

  clock1->ResetCorrectionMetrics();
  clock2->ResetCorrectionMetrics();
  VerifyCorrectionMetrics(clock1, 0, 0.0);
  VerifyCorrectionMetrics(clock2, 0, 0.0);

  // In this example, time on clock1 goes forward by a greater amount than
  // time goes backward on clock2.  Although clock2 still reports the global
  // monotonic time, it does not report a correction because it never
  // observed a raw clock reading that went backward.
  sim_clock.AdvanceTime(absl::Seconds(10));
  time1 = clock1->TimeNow();
  ASSERT_EQ(sim_clock.TimeNow(), time1);
  sim_clock.AdvanceTime(absl::Seconds(-1));
  time2 = clock2->TimeNow();
  ASSERT_EQ(time1, time2);
  VerifyCorrectionMetrics(clock1, 0, 0.0);
  VerifyCorrectionMetrics(clock2, 0, 0.0);

  delete clock1;
  delete clock2;
  DeleteMonotonicClockState(state);
}

// Test that a globally-synchronized MonotonicClock is unaffected by clock
// behavior of a vanilla MonotonicClock.
TEST_F(MonotonicClockTest, UnsyncedPair) {
  absl::SimulatedClock sim_clock;
  MonotonicClock* sync_clock =
      MonotonicClock::CreateSynchronizedMonotonicClock();
  MonotonicClock* mono_clock = MonotonicClock::CreateMonotonicClock(&sim_clock);
  absl::Time before = sync_clock->TimeNow();
  sim_clock.AdvanceTime(before - sim_clock.TimeNow());
  ASSERT_EQ(before, mono_clock->TimeNow());
  sim_clock.AdvanceTime(absl::Seconds(60));
  ASSERT_LT(sync_clock->TimeNow(), mono_clock->TimeNow());
  delete sync_clock;
  delete mono_clock;
}

// The factory method CreateSynchronizedMonotonicClock should return a
// MonotonicClock based on real time.  Since time waits for no unit test,
// we can't test equality of the time read from the factory-produced clock
// and the time read from a real clock.  But we can verifying that, as long
// as the real clock moves forward, the time read from the factory-produced
// clock is bounded by consecutive readings of the real clock.
TEST_F(MonotonicClockTest, CreateSynchronizedMonotonicClock) {
  absl::Clock* real_clock = &absl::Clock::GetRealClock();
  MonotonicClock* mono_clock =
      MonotonicClock::CreateSynchronizedMonotonicClock();
  const int kNumSamples = 100;
  for (int i = 0; i < kNumSamples; ++i) {
    absl::Time before = real_clock->TimeNow();
    absl::Time now = mono_clock->TimeNow();
    absl::Time after = real_clock->TimeNow();
    if (after < before) {
      // Real clock moved backward -- test is invalid.
      continue;
    }
    ASSERT_LE(before, now);
    ASSERT_LE(now, after);
  }
  delete mono_clock;
}

// Start up a number of threads to beat on the interface to verify that
// (a) nothing crashes and (b) nothing deadlocks.
class ClockFrenzy {
 public:
  ClockFrenzy()
      : real_clock_(&absl::Clock::GetRealClock()),
        random_(new ACMRandom(GetRandomSeed("ClockFrenzy"))) {}

  ~ClockFrenzy() { delete random_; }

  void AddSimulatedClock(absl::SimulatedClock* clock) {
    sim_clocks_.push_back(clock);
  }

  void AddMonotonicClock(MonotonicClock* clock) {
    mono_clocks_.push_back(clock);
  }

  void CorrectionCallback(absl::Time raw_time, absl::Time last_raw_time,
                          absl::Time last_monotonic_time) {
    // Occasionally, sleep for up to a millisecond.
    if (UniformRandom(3)) return;
    // Note that this call shouldn't deadlock because we aren't calling back
    // into MonotonicClock.
    real_clock_->Sleep(absl::Milliseconds(RndFloatRandom()));
  }

  void Feed() {
    while (feed_.load(std::memory_order_relaxed)) {
      // 40% of the time, advance a simulated clock.
      // 50% of the time, read a monotonic clock.
      // 10% of the time, change the closure on a monotonic clock.
      const int32_t u = UniformRandom(100);
      if (u < 40) {
        // Pick a simulated clock and advance it.
        const int nclocks = sim_clocks_.size();
        if (nclocks == 0) continue;
        absl::SimulatedClock* sim_clock = sim_clocks_[UniformRandom(nclocks)];
        // Bias the clock towards forward movement.
        sim_clock->AdvanceTime(absl::Seconds(RndFloatRandom() - 0.2));
      } else if (u < 90) {
        // Pick a monotonic clock and read it.
        const int nclocks = mono_clocks_.size();
        if (nclocks == 0) continue;
        MonotonicClock* mono_clock = mono_clocks_[UniformRandom(nclocks)];
        mono_clock->TimeNow();
      } else {
        // Pick a monotonic clock and change its correction callback.
        const int nclocks = mono_clocks_.size();
        if (nclocks == 0) continue;
        MonotonicClock* mono_clock = mono_clocks_[UniformRandom(nclocks)];
        // Use the default callback only rarely because it is so verbose!
        const int32_t u2 = UniformRandom(100);
        if (u2 < 49) {
          mono_clock->set_correction_callback(nullptr);
        } else if (u2 < 98) {
          mono_clock->set_correction_callback(
              absl::bind_front(&ClockFrenzy::CorrectionCallback, this));
        } else {  // 2%
          mono_clock->set_default_correction_callback();
        }
      }
    }
  }

  // Start Feed-ing threads.
  void Start(int nthreads) {
    feed_.store(true, std::memory_order_relaxed);
    thread::Options options;
    options.set_joinable(true);
    for (int i = 0; i < nthreads; ++i) {
      auto closure = [this] { Feed(); };
      threads_.push_back(new ClosureThread(options, "FeedingFrenzy", closure));
      threads_[i]->Start();
    }
  }

  void Stop() { feed_.store(false, std::memory_order_relaxed); }

  // Wait for all threads to finish.
  void Wait() {
    while (!threads_.empty()) {
      threads_.back()->Join();
      delete threads_.back();
      threads_.pop_back();
    }
  }

 private:
  absl::Clock* real_clock_;
  std::vector<absl::SimulatedClock*> sim_clocks_;
  std::vector<MonotonicClock*> mono_clocks_;
  std::vector<Thread*> threads_;

  // Provide a lock to avoid race conditions in non-threadsafe ACMRandom.
  mutable absl::Mutex lock_;
  ACMRandom* random_ ABSL_GUARDED_BY(lock_);

  // An atomic bool that serves as the stopping notification.
  std::atomic<bool> feed_;

  // Thread-safe random number generation functions for use by other class
  // member functions.
  int32_t UniformRandom(int32_t n) {
    absl::MutexLock l(lock_);
    return absl::Uniform<int32_t>(*random_, 0, n);
  }

  float RndFloatRandom() {
    absl::MutexLock l(lock_);
    return absl::Uniform<float>(absl::IntervalOpen, *random_, 0, 1);
  }
};

TEST_F(MonotonicClockTest, SimulatedFrenzy) {
  ClockFrenzy f;
  absl::SimulatedClock s1, s2;
  f.AddSimulatedClock(&s1);
  f.AddSimulatedClock(&s2);
  MonotonicClock* m11 = MonotonicClock::CreateMonotonicClock(&s1);
  State* state = CreateMonotonicClockState(&s1);
  MonotonicClock* m12 = CreateMonotonicClock(state);
  MonotonicClock* m13 = CreateMonotonicClock(state);
  MonotonicClock* m21 = MonotonicClock::CreateMonotonicClock(&s2);
  MonotonicClock* m22 = MonotonicClock::CreateMonotonicClock(&s2);
  f.AddMonotonicClock(m11);
  f.AddMonotonicClock(m12);
  f.AddMonotonicClock(m13);
  f.AddMonotonicClock(m21);
  f.AddMonotonicClock(m22);
  f.Start(10);
  absl::Clock::GetRealClock().Sleep(absl::Seconds(1));
  f.Stop();
  f.Wait();
  delete m11;
  delete m12;
  delete m13;
  delete m21;
  delete m22;
  DeleteMonotonicClockState(state);
}

// Just for completeness, a frenzy with only real-time
// SynchronizedMonotonicClock instances.
TEST_F(MonotonicClockTest, RealFrenzy) {
  ClockFrenzy f;
  MonotonicClock* m1 = MonotonicClock::CreateSynchronizedMonotonicClock();
  MonotonicClock* m2 = MonotonicClock::CreateSynchronizedMonotonicClock();
  MonotonicClock* m3 = MonotonicClock::CreateSynchronizedMonotonicClock();
  f.AddMonotonicClock(m1);
  f.AddMonotonicClock(m2);
  f.AddMonotonicClock(m3);
  f.Start(10);
  absl::Clock::GetRealClock().Sleep(
      absl::Seconds(absl::GetFlag(FLAGS_real_test_secs)));
  f.Stop();
  f.Wait();
  // Just out of curiousity -- did real clock go backwards?
  int clock_num_corrections;
  m1->GetCorrectionMetrics(&clock_num_corrections, nullptr);
  LOG_IF(INFO, clock_num_corrections > 0)
      << clock_num_corrections << " corrections";
  m2->GetCorrectionMetrics(&clock_num_corrections, nullptr);
  LOG_IF(INFO, clock_num_corrections > 0)
      << clock_num_corrections << " corrections";
  m3->GetCorrectionMetrics(&clock_num_corrections, nullptr);
  LOG_IF(INFO, clock_num_corrections > 0)
      << clock_num_corrections << " corrections";
  delete m1;
  delete m2;
  delete m3;
}

}  // namespace util
