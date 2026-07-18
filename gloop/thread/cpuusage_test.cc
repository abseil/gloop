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

#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/sysinfo.h"
#include "gloop/thread/threadpool.h"
#include "gtest/gtest.h"

namespace {

// A routine that chews up a fixed amount of CPU.
// The volatile should discourage the compiler from optimizing this away.
void Tick(volatile double* a, volatile double* b) { *a /= (*a + *b); }

// The number of iterations needed per second of CPU time.
// The initial value is improved by Calibrate().
double iterations_per_second = 1e7;

// Use up the specified amount of CPU
void SpinFor(double seconds) {
  const double iters = seconds * iterations_per_second;
  double a = 1000.0;
  double b = 1.0;
  for (int i = 0; i < iters; i++) {
    Tick(&a, &b);
  }
}

void Calibrate() {
  // recalibrate until we get 2 estimates in a row within 10% of one another.
  double old_iterations_per_second;
  int i = 0;
  do {
    const double start = absl::FDivDuration(base::CPUUsage(), absl::Seconds(1));
    SpinFor(1.0);
    const double finish =
        absl::FDivDuration(base::CPUUsage(), absl::Seconds(1));
    old_iterations_per_second = iterations_per_second;
    iterations_per_second /= finish - start;
    i++;
    CHECK_LT(i, 20);  // crash if too many retries
  } while (iterations_per_second < 0.9 * old_iterations_per_second ||
           old_iterations_per_second * 1.1 < iterations_per_second);
}

class CpuUsageEnvironment : public ::testing::Environment {
 public:
  ~CpuUsageEnvironment() override = default;
  void SetUp() override { Calibrate(); }
};

testing::Environment* const cpu_usage_env =
    testing::AddGlobalTestEnvironment(new CpuUsageEnvironment);

constexpr double kSpinTime = 1.0;
constexpr double kEpsilonLow = 0.3;
constexpr double kEpsilonHigh = 0.7;
constexpr int kMaxTrials = 20;  // max trials of each test
constexpr int kExpectedOK = 5;  // number of trials required within
                                // kSpinTime-kEpsilonLow to
                                // kSpinTime-kEpsilonHigh

// Return 1 if the time between "start" and "finish" is in the
// range kSpinTime - kEpsilonLow  to kSpinTime + kEpsilonHigh,
// or 0 otherwise.
int InBounds(double start, double finish) {
  bool in_bounds = kSpinTime - kEpsilonLow <= (finish - start) &&
                   (finish - start) <= kSpinTime + kEpsilonHigh;
  if (!in_bounds) {
    Calibrate();
  }
  return in_bounds ? 1 : 0;
}

// Return whether a particular test is finished.  The test is finished after
// in_bounds is at least kExpectedOK, or when the remaining trails would not be
// enough to make in_bounds reach kExpectedOK.
bool TestFinished(int i, int in_bounds) {
  return kMaxTrials - i < kExpectedOK - in_bounds || in_bounds >= kExpectedOK;
}

TEST(MyCPUUsage, MainThread) {
  int in_bounds = 0;
  for (int i = 0; !TestFinished(i, in_bounds); i++) {
    const double start = absl::FDivDuration(base::CPUUsage(), absl::Seconds(1));
    SpinFor(kSpinTime);
    const double finish =
        absl::FDivDuration(base::CPUUsage(), absl::Seconds(1));
    in_bounds += InBounds(start, finish);
  }
  EXPECT_GE(in_bounds, kExpectedOK);
}

TEST(MyCPUUsage, FinishedChildThread) {
  int in_bounds = 0;
  for (int i = 0; !TestFinished(i, in_bounds); i++) {
    const double start = absl::FDivDuration(base::CPUUsage(), absl::Seconds(1));
    {
      ThreadPool pool(1);
      pool.Schedule(absl::bind_front(SpinFor, kSpinTime));
    }
    const double finish =
        absl::FDivDuration(base::CPUUsage(), absl::Seconds(1));
    in_bounds += InBounds(start, finish);
  }
  EXPECT_GE(in_bounds, kExpectedOK);
}

TEST(MyCPUUsage, ActiveChildThread) {
  int in_bounds = 0;
  {
    ThreadPool pool(1);
    for (int i = 0; !TestFinished(i, in_bounds); i++) {
      const double start =
          absl::FDivDuration(base::CPUUsage(), absl::Seconds(1));
      absl::Notification n;
      pool.Schedule([&n] {
        SpinFor(kSpinTime);
        n.Notify();
      });
      n.WaitForNotification();
      const double finish =
          absl::FDivDuration(base::CPUUsage(), absl::Seconds(1));
      in_bounds += InBounds(start, finish);
    }
    EXPECT_GE(in_bounds, kExpectedOK);
  }
  absl::SleepFor(absl::Seconds(1));
}

TEST(MyCPUUsage, MultipleChildren) {
  ThreadPool pool(2);
  int in_bounds = 0;
  for (int i = 0; !TestFinished(i, in_bounds); i++) {
    const double start = absl::FDivDuration(base::CPUUsage(), absl::Seconds(1));
    absl::Notification n1, n2;
    pool.Schedule([&n1] {
      SpinFor(kSpinTime / 2.0);
      n1.Notify();
    });
    pool.Schedule([&n2] {
      SpinFor(kSpinTime / 2.0);
      n2.Notify();
    });
    n1.WaitForNotification();
    n2.WaitForNotification();
    const double finish =
        absl::FDivDuration(base::CPUUsage(), absl::Seconds(1));
    in_bounds += InBounds(start, finish);
  }
  EXPECT_GE(in_bounds, kExpectedOK);
}

}  // namespace
