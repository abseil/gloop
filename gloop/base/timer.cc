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

// Most of the timer code is in timer.h, but some stuff needs the
// static storage of a .cc file.

#include "gloop/base/timer.h"

#include "absl/base/call_once.h"
#include "absl/base/internal/cycleclock.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/time/clock.h"
#include "gloop/base/walltime.h"

// Pre-initialized values:
double CycleTimerRoot::cycles_per_second_ = 0.0;
double CycleTimerRoot::seconds_per_cycle_ = 0.0;
double CycleTimerRoot::cycles_per_ms_ = 0;
double CycleTimerRoot::ms_per_cycle_ = 0.0;
double CycleTimerRoot::cycles_per_usec_ = 0;
double CycleTimerRoot::usec_per_cycle_ = 0.0;

static absl::once_flag g_cycle_timer_once;

void CycleTimerRoot::Init() {
  absl::base_internal::LowLevelCallOnce(&g_cycle_timer_once,
                                        &CycleTimerRoot::ProtectedInit);
}

void CycleTimerRoot::ProtectedInit() {
  double cps = absl::base_internal::CycleClock::Frequency();
  CHECK_GT(cps, 0.0);
  cycles_per_second_ = cps;
  seconds_per_cycle_ = 1.L / cps;
  cycles_per_ms_ = cps / 1e3;
  ms_per_cycle_ = 1e3 / cps;
  cycles_per_usec_ = cps / 1e6;
  usec_per_cycle_ = 1e6 / cps;
}

ScopedWallTime::ScopedWallTime(double* aggregate_time)
    : aggregate_time_(aggregate_time),
      start_time_(base::ToWallTime(absl::Now())) {}

ScopedWallTime::~ScopedWallTime() {
  *aggregate_time_ += base::ToWallTime(absl::Now()) - start_time_;
}

ElapsedTimer::~ElapsedTimer() {
  if (ct_.IsRunning()) {
    double elapsed = ct_.Get();
    if (elapsed > mintime_) {
      VLOG(1) << prefix_ << ": " << (elapsed * 1000.0) << " ms (elapsed)";
    }
  }
}
