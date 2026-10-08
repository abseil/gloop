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

#include <utility>

#include "absl/base/call_once.h"
#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock_interface.h"
#include "absl/time/time.h"

namespace util {

namespace monotonic_clock_internal {

// This state, which contains the "guts" of MonotonicClockImpl, is separate
// from the class instance so that it can be shared to implement a
// SynchronizedMonotonicClock.  (The per-instance state of MonotonicClock is
// just for frills like the correction metrics and callback.)  It lives in this
// private namespace so that test code can use it without exposing it to the
// world.
struct State {
  // The clock whose time is being corrected.
  absl::Clock* raw_clock;
  absl::Mutex lock;
  // The largest time ever returned by Now().
  absl::Time max_time ABSL_GUARDED_BY(lock);
  explicit State(absl::Clock* clock)
      : raw_clock(clock), max_time(absl::UnixEpoch()) {}
};

}  // namespace monotonic_clock_internal

using monotonic_clock_internal::State;

class MonotonicClockImpl : public MonotonicClock {
 public:
  // By default, MonotonicClockImpl owns the state_.  ReleaseState(), below,
  // can be used to prevent the MCI destructor from deleting a shared state_.
  explicit MonotonicClockImpl(State* state)
      : state_(state),
        state_owned_(true),
        last_raw_time_(absl::UnixEpoch()),
        correction_count_(0),
        max_correction_(absl::ZeroDuration()),
        correction_callback_(ReportCorrection) {}

  // This type is neither copyable nor movable.
  MonotonicClockImpl(const MonotonicClockImpl&) = delete;
  MonotonicClockImpl& operator=(const MonotonicClockImpl&) = delete;

  ~MonotonicClockImpl() override {
    if (state_owned_) delete state_;
  }

  // Absolve this object of responsibility for state_.
  void ReleaseState() {
    CHECK(state_owned_);
    state_owned_ = false;
  }

  //
  // The Clock interface (see absl/time/clock_interface.h).
  //

  // The logic in TimeNow() is based on GFS_NowMS().
  absl::Time TimeNow() override {
    // These variables save some state from the critical section below.
    absl::Time raw_time;
    absl::Time local_max_time;
    absl::Time local_last_raw_time;

    // As there are several early exits from this function, use MutexLock.
    {
      absl::MutexLock m(state_->lock);

      // Check consistency of internal data with state_.
      CHECK_LE(last_raw_time_, state_->max_time)
          << "non-monotonic behavior: last_raw_time_=" << last_raw_time_
          << ", max_time=" << state_->max_time;

      raw_time = state_->raw_clock->TimeNow();

      // Normal case: time is advancing.  Update state and return the raw time.
      if (raw_time >= state_->max_time) {
        last_raw_time_ = raw_time;
        state_->max_time = raw_time;
        return raw_time;
      }

      // Exceptional case: Raw time is within a window of a previous backward
      // jump.  We do not run any callbacks or update metrics here since we
      // already did that when the backward jump was detected.
      if (raw_time >= last_raw_time_) {
        last_raw_time_ = raw_time;
        return state_->max_time;
      }

      // Exceptional case: Raw time jumped backward.  Remainder of function
      // handles this case.
      //
      // First, update correction metrics.
      ++correction_count_;
      absl::Duration delta = state_->max_time - raw_time;
      CHECK_LT(absl::ZeroDuration(), delta);
      if (delta > max_correction_) {
        max_correction_ = delta;
      }

      // Copy state into local vars before updating last_raw_time_ and leaving
      // the critical section.
      local_max_time = state_->max_time;
      local_last_raw_time = last_raw_time_;
      last_raw_time_ = raw_time;
    }  // MutexLock

    // Run the correction_callback_ with local copies of the state vars.
    {
      absl::MutexLock c(callback_lock_);
      if (correction_callback_) {
        correction_callback_(raw_time, local_last_raw_time, local_max_time);
      }
    }

    // Return the saved maximum time.
    return local_max_time;
  }

  // The strategy of Sleep and SleepUntil is K.I.S.S.: set an alarm on the
  // raw_clock for the desired wakeup_time, and then snooze the alarm if we wake
  // up too soon.  This guarantees that the caller won't wake up too soon (which
  // would require us to advance monotonic time simply by the act of waking up),
  // however the caller may sleep for much longer (in monotonic time) if
  // monotonic time jumps far into the future.  Whether or not this happens
  // depends on the behavior of the raw clock.
  void Sleep(absl::Duration d) override {
    absl::Time wakeup_time = TimeNow() + d;
    SleepUntil(wakeup_time);
  }

  void SleepUntil(absl::Time wakeup_time) override {
    while (TimeNow() < wakeup_time) {
      state_->raw_clock->SleepUntil(wakeup_time);
    }
  }

  bool AwaitWithDeadline(absl::Mutex* mu, const absl::Condition& cond,
                         absl::Time wakeup_time) override {
    while (!cond.Eval() && (TimeNow() < wakeup_time)) {
      state_->raw_clock->AwaitWithDeadline(mu, cond, wakeup_time);
    }
    return cond.Eval();
  }

  //
  // End of Clock interface.
  //

  //
  // The MonotonicClock interface.
  //

  // Change the correction callback.  Deletes the current correction_callback,
  // if any.
  void set_correction_callback(
      absl::AnyInvocable<void(absl::Time, absl::Time, absl::Time)>
          correction_callback) override {
    absl::MutexLock c(callback_lock_);
    correction_callback_ = std::move(correction_callback);
  }

  // Revert to the default callback.  Deletes the current correction_callback,
  // if any.
  void set_default_correction_callback() override {
    set_correction_callback(ReportCorrection);
  }

  // Get metrics about time corrections.
  void GetCorrectionMetrics(int* correction_count,
                            double* max_correction) override {
    absl::MutexLock l(state_->lock);
    if (correction_count != nullptr) *correction_count = correction_count_;
    if (max_correction != nullptr)
      *max_correction = absl::FDivDuration(max_correction_, absl::Seconds(1));
  }

  // Reset values returned by GetCorrectionMetrics().
  void ResetCorrectionMetrics() override {
    absl::MutexLock l(state_->lock);
    correction_count_ = 0;
    max_correction_ = absl::ZeroDuration();
  }

  //
  // End of MonotonicClock interface.
  //

 private:
  // The default correction callback simply logs a message whenever raw
  // time goes backward.
  static void ReportCorrection(absl::Time raw_time, absl::Time last_raw_time,
                               absl::Time monotonic_time) {
    LOG(WARNING) << "Time jumped backward: " << last_raw_time - raw_time
                 << " (raw) " << monotonic_time - raw_time << " (monotonic)";
  }

  // The guts of the monotonic clock.  Caution: this may point to a static
  // object.
  State* state_;
  // If true, this object owns state_ and is responsible for deallocating it.
  bool state_owned_;

  // last_raw_time_ remembers the last value obtained from raw_clock_.
  // It prevents spurious calls to ReportCorrection when time moves
  // forward by a smaller amount than a prior backward jump.
  absl::Time last_raw_time_ ABSL_GUARDED_BY(state_->lock);

  // Variables that keep track of time corrections made by this instance of
  // MonotonicClock.  (All such metrics are instance-local for reasons
  // described earlier.)
  int correction_count_ ABSL_GUARDED_BY(state_->lock);
  absl::Duration max_correction_ ABSL_GUARDED_BY(state_->lock);

  // A lock to prevent deletion of the correction_callback while it's being
  // used.  By design, callback_lock_ and state_->lock will never be held at
  // the same time; however it may happen if the user supplies a correction
  // callback that calls back into the class.  See the comments for
  // set_correction_callback in the header file.
  absl::Mutex callback_lock_;
  // Called by Now() when raw time goes backward.
  absl::AnyInvocable<void(absl::Time, absl::Time, absl::Time)>
      correction_callback_ ABSL_GUARDED_BY(callback_lock_);
};

// Factory methods.
MonotonicClock* MonotonicClock::CreateMonotonicClock(absl::Clock* clock) {
  State* state = new State(clock);
  // MonotonicClockImpl takes ownership of state.
  return new MonotonicClockImpl(state);
}

namespace monotonic_clock_internal {

static State* sync_state = nullptr;
static void InitSyncState() {
  sync_state = new State(&absl::Clock::GetRealClock());
}

}  // namespace monotonic_clock_internal

// The reason that SynchronizedMonotonicClock is not implemented as a singleton
// is so that different code bases can handle clock corrections their own way.
MonotonicClock* MonotonicClock::CreateSynchronizedMonotonicClock() {
  static absl::once_flag once;
  absl::call_once(/*out*/ once, monotonic_clock_internal::InitSyncState);
  MonotonicClockImpl* clock =
      new MonotonicClockImpl(monotonic_clock_internal::sync_state);
  // Release ownership of sync_state.
  clock->ReleaseState();
  return clock;
}

namespace monotonic_clock_internal {  // Test code.

void SynchronizedMonotonicClockReset() {
  if (sync_state == nullptr) return;
  LOG(INFO) << "Resetting SynchronizedMonotonicClock";
  absl::MutexLock m(sync_state->lock);
  sync_state->max_time = absl::UnixEpoch();
}

State* CreateMonotonicClockState(absl::Clock* raw_clock) {
  return new State(raw_clock);
}

void DeleteMonotonicClockState(State* state) { delete state; }

MonotonicClock* CreateMonotonicClock(State* state) {
  MonotonicClockImpl* clock = new MonotonicClockImpl(state);
  // Release ownership of sync_state.
  clock->ReleaseState();
  return clock;
}

}  // namespace monotonic_clock_internal
}  // namespace util
