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

#ifndef THIRD_PARTY_GLOOP_UTIL_TIME_MONOTONIC_CLOCK_H_
#define THIRD_PARTY_GLOOP_UTIL_TIME_MONOTONIC_CLOCK_H_

#include "absl/functional/any_invocable.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock_interface.h"
#include "absl/time/time.h"

// Throughout this code, the double type is used for differences of WallTimes.

namespace util {

// MonotonicClock is an interface for a Clock that never goes backward.
// Successive returned values from Now() are guaranteed to be monotonically
// non-decreasing, although they may not be monotonic with respect to values
// returned from other instances of MonotonicClock.
//
// You can wrap any Clock object in a MonotonicClock using the
// CreateMonotonicClock() factory method, including Clock::RealClock().
// However, if you want a monotonic version of real time, it is strongly
// recommended that you use the CreateSynchronizedMonotonicClock() factory
// method, which wraps Clock::RealClock() and guarantees that values returned
// from Now() are monotonic ACROSS instances of the class that are created by
// CreateSynchronizedMonotonicClock().
//
// All methods support concurrent access.
class MonotonicClock : public absl::Clock {
 public:
  ~MonotonicClock() override = default;

  // The Clock interface (see absl/time/clock_interface.h).
  //
  // Return a monotonically non-decreasing time.
  absl::Time TimeNow() override = 0;
  // Sleep and SleepUntil guarantee only that the caller will sleep for at
  // least as long as specified in monotonic time.  The caller may sleep for
  // much longer (in monotonic time) if monotonic time jumps far into the
  // future.  Whether or not this happens depends on the behavior of the raw
  // clock.
  void Sleep(absl::Duration d) override = 0;
  void SleepUntil(absl::Time wakeup_time) override = 0;
  bool AwaitWithDeadline(absl::Mutex* mu, const absl::Condition& cond,
                         absl::Time wakeup_time) override = 0;
  //
  // End of Clock interface.

  // Specify a callback function to be called whenever the wrapped raw clock
  // goes backward.  The arguments supplied to the callback are the current raw
  // time, the last observed raw time, and the current monotonic time.
  //
  // correction_callback can be nullptr if you want no action be taken when the
  // raw clock goes backward.
  //
  // The callback runs in the same thread as the call to Now() and is subject
  // to the following contraints:
  //   1. The callback is permitted to call GetCorrectionMetrics() or
  //      ResetCorrectionMetrics().  (The metrics returned by
  //      GetCorrectionMetrics() will include the current correction.)
  //   2. Deadlock MAY result if the callback calls Now(), Sleep() or
  //      SleepUntil().
  //   3. Deadlock is ASSURED if the callback calls set_correction_callback()
  //      or set_default_correction_callback().
  virtual void set_correction_callback(
      absl::AnyInvocable<void(absl::Time, absl::Time, absl::Time)>
          correction_callback) = 0;
  // The default correction callback simply logs a message whenever raw
  // time goes backward.  You can revert to this behavior by calling
  // set_default_correction_callback().  Deletes the current
  // correction_callback, if any.
  virtual void set_default_correction_callback() = 0;

  // Get metrics about time corrections.
  virtual void GetCorrectionMetrics(int* correction_count,
                                    double* max_correction) = 0;
  // Reset values returned by GetCorrectionMetrics().
  virtual void ResetCorrectionMetrics() = 0;

  // Factory methods.
  //
  // Create a MonotonicClock based on the given raw_clock.  This clock will
  // return monotonically non-decreasing values from Now(), but may not behave
  // monotonically with respect to other instances created by this function,
  // even if they are based on the same raw_clock.  Caller owns raw_clock.
  static MonotonicClock* CreateMonotonicClock(absl::Clock* raw_clock);

  // Create an instance of MonotonicClock that is based on Clock::RealClock().
  // All such instance are synced with each other such that return values from
  // Now() are monotonic across instances.  This allows independently developed
  // code bases to have private instances of the synchronized MonotonicClock
  // and know that they will never see time anomalies when calling from one
  // code base to another.  Each instance can have its own correction callback.
  // Unlike Clock::RealClock(), caller owns this object and should delete it
  // when no longer needed.
  static MonotonicClock* CreateSynchronizedMonotonicClock();
};

}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TIME_MONOTONIC_CLOCK_H_
