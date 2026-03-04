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

// This file contains various timers: for wall time, user time, elapsed
// time, etc.  All timers have the same interface, which is supposed to
// remind you of what a stopwatch does:
//    Start() -- start the timer up again
//    Stop() -- stop the timer (Start() continues where we left off)
//    Reset() -- set the timer back to 0
//    Restart() -- set the timer back to 0 and then start it
//    Get() -- return the current value of the timer, in seconds.
//
// We inline all the timers, because low overhead is very important.
//
// Reset returns a bool because it only makes sense to reset the StackTimer
// in a specific case and we wanted the signature to match for all classes.
//
// Note that when converting between cycle counts and milliseconds, both of
// which are represented by int64_t values, you should not assume precision
// greater than 40 bits.

#ifndef THIRD_PARTY_GLOOP_BASE_TIMER_H_
#define THIRD_PARTY_GLOOP_BASE_TIMER_H_

#include <cstdint>

#define GOOGLE3_HAS_PROCESS_DURATION 1

#if !defined(GOOGLE3_HAS_PROCESS_DURATION)
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <sys/resource.h>
#include <sys/time.h>
#endif  // GOOGLE3_HAS_PROCESS_DURATION

#include <math.h>
#include <sys/types.h>
#include <time.h>

#include "absl/base/attributes.h"
#include "absl/base/internal/cycleclock.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/port.h"  // IWYU pragma: keep

// ----------------------------------------------------------------------
// CycleTimerRoot
//    An all-static base class for CycleTimer classes to hide the
//    conversion constants and ensure that they're initialized safely.
//    Don't write on these constants, and only read them after calling Init().
class CycleTimerRoot {
 protected:
  CycleTimerRoot() { Init(); }

  // Store both constants and their inverses to avoid division:
  static double cycles_per_second_;
  static double seconds_per_cycle_;
  static double cycles_per_ms_;
  static double ms_per_cycle_;
  static double cycles_per_usec_;
  static double usec_per_cycle_;

  static void Init();

 private:
  static void ProtectedInit();
};

// ----------------------------------------------------------------------
// CycleTimerBase
//    An all-static utility class for cycle timing conversions.
//    This was formerly a (private) base class of [Simple]CycleTimer; the
//    name was kept for backward compatibility with users of these methods.
//    Thread-safe.
class CycleTimerBase : CycleTimerRoot {
 private:
  // Private constructor: no instantiation.
  CycleTimerBase();

 public:
  // Conversion routines (slightly slower than the methods on CycleTimer, but
  // handy if you don't have one):
  static int64_t SecondsToCycles(double seconds);
  static double CyclesToSeconds(int64_t cycles);

  [[deprecated(
      "Use DurationToCycles instead. See "
      "<link>.")]] static int64_t
  MsToCycles(int64_t ms);
  [[deprecated(
      "Use CyclesToDuration instead. See "
      "<link>.")]] static int64_t
  CyclesToMs(int64_t cycles);

  [[deprecated(
      "Use DurationToCycles instead. See "
      "<link>.")]] static int64_t
  UsecToCycles(int64_t usec);
  [[deprecated(
      "Use CyclesToDuration instead. See "
      "<link>.")]] static int64_t
  CyclesToUsec(int64_t cycles);

  static int64_t DurationToCycles(absl::Duration duration);
  static absl::Duration CyclesToDuration(int64_t cycles);
};

inline int64_t CycleTimerBase::SecondsToCycles(double seconds) {
  Init();
  return static_cast<int64_t>(cycles_per_second_ * seconds);
}

inline double CycleTimerBase::CyclesToSeconds(int64_t cycles) {
  Init();
  return static_cast<double>(cycles) * seconds_per_cycle_;
}

inline int64_t CycleTimerBase::MsToCycles(int64_t ms) {
  Init();
  return static_cast<int64_t>(round(ms * cycles_per_ms_));
}

inline int64_t CycleTimerBase::CyclesToMs(int64_t cycles) {
  Init();
  return static_cast<int64_t>(round(cycles * ms_per_cycle_));
}

inline int64_t CycleTimerBase::UsecToCycles(int64_t usec) {
  Init();
  return static_cast<int64_t>(round(usec * cycles_per_usec_));
}

inline int64_t CycleTimerBase::CyclesToUsec(int64_t cycles) {
  Init();
  return static_cast<int64_t>(round(cycles * usec_per_cycle_));
}

inline int64_t CycleTimerBase::DurationToCycles(absl::Duration duration) {
  return SecondsToCycles(absl::FDivDuration(duration, absl::Seconds(1)));
}

inline absl::Duration CycleTimerBase::CyclesToDuration(int64_t cycles) {
  return absl::Seconds(CyclesToSeconds(cycles));
}

// ----------------------------------------------------------------------
// CycleTimerInstance
//    Base class for CycleTimer and SimpleCycleTimer providing conversion
//    instance member functions that safely access the CycleTimerRoot
//    conversion constants.  Thread-safe.
class CycleTimerInstance : CycleTimerRoot {
 public:
  inline CycleTimerInstance() {}

  // Static versions of these routines can be found in CycleTimerBase, but
  // these are provided for backward compatibility.  These are also 1-2ns
  // faster because they don't require a conditional.
  inline int64_t SecondsToCycles(double seconds) const;
  inline double CyclesToSeconds(int64_t cycles) const;

  [[deprecated(
      "Use DurationToCycles instead. See "
      "<link>.")]] inline int64_t
  MsToCycles(int64_t ms) const;
  [[deprecated(
      "Use CyclesToDuration instead. See "
      "<link>.")]] inline int64_t
  CyclesToMs(int64_t cycles) const;

  [[deprecated(
      "Use DurationToCycles instead. See "
      "<link>.")]] inline int64_t
  UsecToCycles(int64_t usec) const;
  [[deprecated(
      "Use CyclesToDuration instead. See "
      "<link>.")]] inline int64_t
  CyclesToUsec(int64_t cycles) const;

  inline int64_t DurationToCycles(absl::Duration duration) const;
  inline absl::Duration CyclesToDuration(int64_t cycles) const;
};

inline int64_t CycleTimerInstance::SecondsToCycles(double seconds) const {
  return static_cast<int64_t>(cycles_per_second_ * seconds);
}

inline double CycleTimerInstance::CyclesToSeconds(int64_t cycles) const {
  return static_cast<double>(cycles) * seconds_per_cycle_;
}

inline int64_t CycleTimerInstance::MsToCycles(int64_t ms) const {
  return static_cast<int64_t>(round(ms * cycles_per_ms_));
}

inline int64_t CycleTimerInstance::CyclesToMs(int64_t cycles) const {
  return static_cast<int64_t>(round(cycles * ms_per_cycle_));
}

inline int64_t CycleTimerInstance::UsecToCycles(int64_t usec) const {
  return static_cast<int64_t>(round(usec * cycles_per_usec_));
}

inline int64_t CycleTimerInstance::CyclesToUsec(int64_t cycles) const {
  return static_cast<int64_t>(round(cycles * usec_per_cycle_));
}

inline int64_t CycleTimerInstance::DurationToCycles(
    absl::Duration duration) const {
  return SecondsToCycles(absl::FDivDuration(duration, absl::Seconds(1)));
}

inline absl::Duration CycleTimerInstance::CyclesToDuration(
    int64_t cycles) const {
  return absl::Seconds(CyclesToSeconds(cycles));
}

// ----------------------------------------------------------------------
// CycleTimer
//    This is an interface to the Pentium hardware cycle counter, which
//    can count time with one-cycle accuracy.
//       The measurement overhead itself is around 40 cycles (min.), so
//    it's no good for measuring very short code sequences.  Also, Linux
//    doesn't save the counters at context switches, so it's essentially
//    a WALL TIME counter.  In other words, if you get a context
//    switch during your measurement period the time will be wildly off.
//    Thread-compatible.
// ----------------------------------------------------------------------
class CycleTimer : public CycleTimerInstance {
 public:
  inline CycleTimer();

  inline void Start();
  inline void Stop();
  // This also stops a running timer:
  inline bool Reset();
  inline void Restart();

  inline double Get() const;         // get the value in seconds
  inline int64_t GetCycles() const;  // raw # of cycles
  [[deprecated("Use GetDuration instead. See <link>.")]] inline int64_t
  GetInMs() const;
  [[deprecated("Use GetDuration instead. See <link>.")]] inline int64_t
  GetInUsec() const;
  inline absl::Duration GetDuration() const;

  // Increment the cycle count by the given amount.
  inline void Increment(int64_t cycles);

  // True if the timer is currently running:
  inline bool IsRunning() const;

  // Cycles elapsed since the most recent call to Start().
  inline int64_t GetCyclesThisRun() const;

 private:
  int64_t start_;  // when we last started the stopwatch
  int64_t sum_;    // accumulated # of cycles timed
};

inline CycleTimer::CycleTimer() { Reset(); }

inline void CycleTimer::Start() {  // Just save when we started
  start_ = absl::base_internal::CycleClock::Now();
}

inline void CycleTimer::Stop() {  // Update total time, 1st time it's called
  if (start_)
    sum_ += absl::base_internal::CycleClock::Now() -
            start_;  // so two Stop()s is safe
  start_ = 0;        // protect against next Stop()
}

inline bool CycleTimer::Reset() {  // As if we had hit Stop() first
  start_ = sum_ = 0;
  return true;
}

inline void CycleTimer::Restart() {
  Reset();
  Start();
}

inline int64_t CycleTimer::GetCycles() const {  // We may be running even now!
  return sum_ + (start_ ? absl::base_internal::CycleClock::Now() - start_ : 0);
}

inline double CycleTimer::Get() const { return CyclesToSeconds(GetCycles()); }

inline int64_t CycleTimer::GetInMs() const { return CyclesToMs(GetCycles()); }

inline int64_t CycleTimer::GetInUsec() const {
  return CyclesToUsec(GetCycles());
}

inline absl::Duration CycleTimer::GetDuration() const {
  return CyclesToDuration(GetCycles());
}

inline void CycleTimer::Increment(int64_t cycles) { sum_ += cycles; }

inline bool CycleTimer::IsRunning() const { return (start_ != 0); }

inline int64_t CycleTimer::GetCyclesThisRun() const {
  return (start_ ? absl::base_internal::CycleClock::Now() - start_ : 0);
}

// ----------------------------------------------------------------------
// SimpleCycleTimer
//    This is similar to CycleTimer but doesn't support pausing.
//    SimpleCycleTimer is only half the size of CycleTimer, so it's preferable
//    if it does what you need.
//    Thread-compatible.
// ----------------------------------------------------------------------
class SimpleCycleTimer : public CycleTimerInstance {
 public:
  // The timer does not start automatically when constructed.
  inline SimpleCycleTimer() : time_(0) {}

  // An alias to make transitioning from a CycleTimer easier, but any Start is
  // really a Restart:
  inline void Start() { Restart(); }
  // Stop and restart the timer:
  inline void Restart();

  // Stop the timer and retain the time elapsed:
  inline void Stop();
  // Stop the timer and clear the time elapsed:
  inline void Reset();

  // True if the timer is currently running:
  inline bool IsRunning() const;

  // If the timer is running, the get routines return the time elapsed since
  // it started.  If it isn't running, they return the time elapsed in the
  // last run.
  inline double Get() const;         // seconds
  inline int64_t GetCycles() const;  // raw # of cycles
  [[deprecated("Use GetDuration instead. See <link>.")]] inline int64_t
  GetInMs() const;
  [[deprecated("Use GetDuration instead. See <link>.")]] inline int64_t
  GetInUsec() const;
  inline absl::Duration GetDuration() const;

  // Increment the cycle count by the given amount.
  inline void Increment(int64_t cycles);

 private:
  // time_ is a bit overloaded.
  // If the timer is reset, it will be 0.
  // If the timer is running, it will be the negative of the start cycle count.
  // If the timer is stopped, it will be the cycles elapsed in the last run.
  int64_t time_;
};

inline void SimpleCycleTimer::Restart() {
  time_ = -absl::base_internal::CycleClock::Now();
}

inline void SimpleCycleTimer::Stop() {
  if (IsRunning()) {
    // This computes the difference between now and the start time:
    time_ += absl::base_internal::CycleClock::Now();
  }
}

inline void SimpleCycleTimer::Reset() { time_ = 0; }

inline bool SimpleCycleTimer::IsRunning() const { return (time_ < 0); }

inline int64_t SimpleCycleTimer::GetCycles() const {
  // If running, return the difference between now and the start time, else
  // just return time_, which will be the elapsed time of the most recent run.
  return (IsRunning()) ? absl::base_internal::CycleClock::Now() + time_ : time_;
}

inline double SimpleCycleTimer::Get() const {
  return CyclesToSeconds(GetCycles());
}

inline int64_t SimpleCycleTimer::GetInMs() const {
  return CyclesToMs(GetCycles());
}

inline int64_t SimpleCycleTimer::GetInUsec() const {
  return CyclesToUsec(GetCycles());
}

inline absl::Duration SimpleCycleTimer::GetDuration() const {
  return CyclesToDuration(GetCycles());
}

inline void SimpleCycleTimer::Increment(int64_t cycles) {
  // The increment mustn't cause it to implicitly change state:
  CHECK_GE(cycles, 0);
  CHECK(time_ >= 0 || time_ < -cycles);
  time_ += cycles;
}

// ----------------------------------------------------------------------
// ScopedTime
//    This is an object that starts a timer when created and stops it when
//    deleted.  This is useful for timing a code block that has multiple exit
//    points.  If the timer is already running (or nullptr), it does nothing, so
//    these can be safely nested (in a recursive function, for example).
//    Works with CycleTimer, SimpleCycleTimer, and WallTimer.
// ----------------------------------------------------------------------
template <class Timer>
class ScopedTime {
 public:
  explicit ScopedTime(Timer* timer)
      : timer_((timer && !timer->IsRunning()) ? timer : nullptr) {
    if (timer_) timer_->Start();
  }

  // Do not copy/move this RAII object.
  ScopedTime(const ScopedTime&) = delete;
  ScopedTime& operator=(const ScopedTime&) = delete;

  ~ScopedTime() {
    if (timer_) timer_->Stop();
  }

 private:
  Timer* timer_;
};

// Convenient aliases reduce typing
typedef ScopedTime<CycleTimer> ScopedCycleTime;
typedef ScopedTime<SimpleCycleTimer> ScopedSimpleCycleTime;

// Catch bug where variable name is omitted, e.g. ScopedCycleTime(&timer);
// Note: if for some reason you want to heap-allocate ScopedTime (e.g. for use
// with shared_ptr), use the raw template instead of the typedefs.
#define ScopedCycleTime(x) \
  static_assert(0, "scoped_timer_decl_missing_var_name")
#define ScopedSimpleCycleTime(x) \
  static_assert(0, "scoped_timer_decl_missing_var_name")

// ----------------------------------------------------------------------
// ScopedWallTime
//    This is an object that computes the wall time duration between when the
//    object is created and when it is deleted (in seconds). This is useful for
//    timing a code block that has multiple exit points. Nested creation with
//    the same aggregate_time results in over-accounting. Different threads must
//    provide different aggregate_time.
//
//    Note the difference between ScopedWallTime and ScopedTime<WallTimer>: the
//    former takes a pointer to a bare WallTime value and adds to it, while the
//    latter takes a WallTimer object and calls Start() and Stop() on it.
//    ----------------------------------------------------------------------
class ScopedWallTime {
 public:
  // We do not own the pointer. The pointer must be valid for the duration
  // of the existence of the ScopedWallTime instance. Not thread safe for
  // aggregate_time. Adds time in seconds to *aggregate_time on destruction.
  explicit ScopedWallTime(double* aggregate_time);
  ~ScopedWallTime();

  // Do not copy/move this RAII object.
  ScopedWallTime(const ScopedWallTime&) = delete;
  ScopedWallTime& operator=(const ScopedWallTime&) = delete;

 private:
  double* aggregate_time_;

  // When the instance was created.
  double start_time_;
};

// ----------------------------------------------------------------------
// WallTimer
//    This is an interface to the system clock. The time measured is
//    wall clock time.
//    Important note: Do not assume that consecutive calls to these methods
//    always return results that are non-decreasing - a DECREASE (a few ms)
//    between calls has been observed on several occasions, even on a single
//    processor machine. Please refer to http://b/issue?id=777807
//    Thread-compatible.
// ----------------------------------------------------------------------
class WallTimer {
 public:
  inline WallTimer();

  inline void Start();
  inline void Stop();
  inline bool Reset();
  inline void Restart();
  inline bool IsRunning() const;
  inline double Get() const;  // Returns time in seconds.
  [[deprecated("Use GetDuration instead. See <link>.")]] inline int64_t
  GetInMs() const;
  inline absl::Duration GetDuration() const;

 private:
  // Returns the elapsed duration since the timer was started, or
  // ZeroDuration() if the timer is not running.
  absl::Duration DurationSinceLastStart() const;

  // The time when the timer was started, or InfinitePast() if the
  // timer is not running.
  absl::Time start_time_;

  // The total accumulated duration between all prior start..stop
  // sequences. Does not include the current measured period if the
  // timer is running.
  absl::Duration accumulated_duration_;
};

inline WallTimer::WallTimer()
    : start_time_(absl::InfinitePast()),
      accumulated_duration_(absl::ZeroDuration()) {}

inline bool WallTimer::IsRunning() const {
  return start_time_ != absl::InfinitePast();
}

inline absl::Duration WallTimer::DurationSinceLastStart() const {
  return IsRunning() ? absl::Now() - start_time_ : absl::ZeroDuration();
}

inline void WallTimer::Start() { start_time_ = absl::Now(); }

inline void WallTimer::Stop() {
  accumulated_duration_ += DurationSinceLastStart();
  start_time_ = absl::InfinitePast();
}

inline bool WallTimer::Reset() {
  start_time_ = absl::InfinitePast();
  accumulated_duration_ = absl::ZeroDuration();
  return true;
}

inline void WallTimer::Restart() {
  Reset();
  Start();
}

inline absl::Duration WallTimer::GetDuration() const {
  return accumulated_duration_ + DurationSinceLastStart();
}

inline double WallTimer::Get() const {
  return absl::ToDoubleSeconds(GetDuration());
}

inline int64_t WallTimer::GetInMs() const {
  return absl::ToInt64Milliseconds(GetDuration());
}

#if !defined(__native_client__)
// ----------------------------------------------------------------------
// RUsageTimer
//    This is an interface to the getrusage() syscall, which returns
//    resource usage statistics.
//    On current Linux systems, the reported value is believed to be accurate to
//    within ~10ms of the true value.
//    Other platforms may use equivalent APIs to get the same statistics.
//    Thread-compatible.
// ----------------------------------------------------------------------
template <int mode>
class RUsageTimer {
 public:
  enum Mode { USER, SYSTEM, BOTH };

  inline RUsageTimer()
      : duration_at_last_start_(-absl::InfiniteDuration()),
        accumulated_duration_(absl::ZeroDuration()) {}

  inline void Start() { duration_at_last_start_ = ReadProcessDuration(); }

  inline void Stop() {
    accumulated_duration_ += DurationSinceLastStart();
    duration_at_last_start_ = -absl::InfiniteDuration();
  }

  inline bool Reset() {
    duration_at_last_start_ = -absl::InfiniteDuration();
    accumulated_duration_ = absl::ZeroDuration();
    return true;
  }

  inline void Restart() {
    Reset();
    Start();
  }

  inline bool IsRunning() const {
    return duration_at_last_start_ != -absl::InfiniteDuration();
  }

  inline double Get() const { return absl::ToDoubleSeconds(GetDuration()); }

  [[deprecated("Use GetDuration instead. See <link>.")]] inline int64_t
  GetInMs() const {
    return absl::ToInt64Milliseconds(GetDuration());
  }

  inline absl::Duration GetDuration() const {
    return accumulated_duration_ + DurationSinceLastStart();
  }

 private:
  static absl::Duration ReadProcessDuration() {
#if !defined(GOOGLE3_HAS_PROCESS_DURATION)
    LOG(FATAL) << "RUsageTimer::ReadProcessDuration not implemented on this OS";
    return absl::ZeroDuration();
#elif defined(_WIN32)
    HANDLE process = GetCurrentProcess();
    // GetProcessTimes() uses 100-nanosecond time units.
    FILETIME creation_time, exit_time, kernel_time, user_time;
    GetProcessTimes(process, &creation_time, &exit_time, &kernel_time,
                    &user_time);
    ULARGE_INTEGER kernel;
    kernel.HighPart = kernel_time.dwHighDateTime;
    kernel.LowPart = kernel_time.dwLowDateTime;
    ULARGE_INTEGER user;
    user.HighPart = user_time.dwHighDateTime;
    user.LowPart = user_time.dwLowDateTime;
    if (mode == USER) {
      return absl::Nanoseconds(user.QuadPart * 100);
    }
    if (mode == SYSTEM) {
      return absl::Nanoseconds(kernel.QuadPart * 100);
    }
    if (mode == BOTH) {
      return absl::Nanoseconds(user.QuadPart * 100) +
             absl::Nanoseconds(kernel.QuadPart * 100);
    }
#else
    struct rusage r;
    getrusage(RUSAGE_SELF, &r);
    if (mode == USER) {
      return absl::DurationFromTimeval(r.ru_utime);
    }
    if (mode == SYSTEM) {
      return absl::DurationFromTimeval(r.ru_stime);
    }
    if (mode == BOTH) {
      return absl::DurationFromTimeval(r.ru_utime) +
             absl::DurationFromTimeval(r.ru_stime);
    }
#endif  // GOOGLE3_HAS_PROCESS_DURATION
    LOG(FATAL) << "Invalid mode value in class RUsageTimer: " << mode;
    return absl::ZeroDuration();
  }

  // Returns the elapsed duration since the timer was started, or
  // ZeroDuration() if the timer is not running.
  absl::Duration DurationSinceLastStart() const {
    return IsRunning() ? ReadProcessDuration() - duration_at_last_start_
                       : absl::ZeroDuration();
  }

  // The duration already accumulated by the process when the timer
  // was last started, or -InfiniteDutation() if the timer is not
  // running.
  absl::Duration duration_at_last_start_;

  // The total accumulated duration between all prior start..stop
  // sequences. Does not include the current measured period if the
  // timer is running.
  absl::Duration accumulated_duration_;
};

// UserTimer measures time spent in userspace in the current process
typedef RUsageTimer<RUsageTimer<0>::USER> UserTimer;
// SystemTimer measures time spent in the kernel in the current process
typedef RUsageTimer<RUsageTimer<0>::SYSTEM> SystemTimer;
// VirtualTimer measures total user/kernel time in the current process
typedef RUsageTimer<RUsageTimer<0>::BOTH> VirtualTimer;

// ----------------------------------------------------------------------
// UserSystemWallTimer
//    A user-system-wall timer just wraps a user timer, system timer,
//    and a wall timer.  Any call to an interface method is passed
//    through to all three timers.  A call to get fills in 3 pointers
//    to doubles instead of returning a double.
// ----------------------------------------------------------------------
class UserSystemWallTimer {
 public:
  inline UserSystemWallTimer()
      : user_timer_(), system_timer_(), wall_timer_() {}

  inline void Start() {
    user_timer_.Start();
    system_timer_.Start();
    wall_timer_.Start();
  }

  inline void Stop() {
    user_timer_.Stop();
    system_timer_.Stop();
    wall_timer_.Stop();
  }

  inline bool Reset() {
    user_timer_.Reset();
    system_timer_.Reset();
    wall_timer_.Reset();
    return true;
  }

  inline void Restart() {
    user_timer_.Restart();
    system_timer_.Restart();
    wall_timer_.Restart();
  }

  inline bool IsRunning() const { return user_timer_.IsRunning(); }

  inline void Get(double* user, double* sys, double* wall) const {
    *user = user_timer_.Get();
    *sys = system_timer_.Get();
    *wall = wall_timer_.Get();
  }

  [[deprecated("Use GetDuration instead. See <link>.")]] inline void GetInMs(
      int64_t* user, int64_t* sys, int64_t* wall) const {
    *user = user_timer_.GetInMs();
    *sys = system_timer_.GetInMs();
    *wall = wall_timer_.GetInMs();
  }

  inline void GetDuration(absl::Duration* user, absl::Duration* sys,
                          absl::Duration* wall) const {
    *user = user_timer_.GetDuration();
    *sys = system_timer_.GetDuration();
    *wall = wall_timer_.GetDuration();
  }

 private:
  UserTimer user_timer_;
  SystemTimer system_timer_;
  WallTimer wall_timer_;
};
#endif  // !defined(__native_client__)

// ----------------------------------------------------------------------
// ElapsedTimer
//    An elapsed timer is a little different from a normal timer;
//    it doesn't have the standard interface.  It's meant to be
//    enclosed in a block; when the block exits, it logs (at
//    level INFO) how much time the block took.
//       The first argument is a prefix, the second can be false to
//    cause the timer to do nothing (presumably it's a run-time expression),
//    the third is a minimum time (in seconds) below which we don't print.
// ----------------------------------------------------------------------
class ElapsedTimer {
 public:
  inline explicit ElapsedTimer(absl::string_view prefix);
  inline ElapsedTimer(absl::string_view prefix, bool active, double mintime);

  // Do not copy/move this RAII object.
  ElapsedTimer(const ElapsedTimer&) = delete;
  ElapsedTimer& operator=(const ElapsedTimer&) = delete;

  ~ElapsedTimer();

 private:
  absl::string_view prefix_;
  const double mintime_;
  SimpleCycleTimer ct_;
};

inline ElapsedTimer::ElapsedTimer(absl::string_view prefix)
    : prefix_(prefix), mintime_(0.0), ct_() {
  ct_.Start();
}

inline ElapsedTimer::ElapsedTimer(absl::string_view prefix, bool active,
                                  double mintime)
    : prefix_(prefix), mintime_(mintime), ct_() {
  if (active) ct_.Start();
}

#if !defined(__native_client__)
// ----------------------------------------------------------------------
// ElapsedUserTimer
//    Just like ElapsedTimer, except with a UserTimer instead of CycleTimer.
// ----------------------------------------------------------------------
class ElapsedUserTimer {
 public:
  inline explicit ElapsedUserTimer(absl::string_view prefix);
  inline ElapsedUserTimer(absl::string_view prefix, bool active,
                          double mintime);

  // Do not copy/move this RAII object.
  ElapsedUserTimer(const ElapsedUserTimer&) = delete;
  ElapsedUserTimer& operator=(const ElapsedUserTimer&) = delete;

  inline ~ElapsedUserTimer();

 private:
  absl::string_view prefix_;
  const double mintime_;
  UserTimer ut_;
};

inline ElapsedUserTimer::ElapsedUserTimer(absl::string_view prefix)
    : prefix_(prefix), mintime_(0.0), ut_() {
  ut_.Start();
}

inline ElapsedUserTimer::ElapsedUserTimer(absl::string_view prefix, bool active,
                                          double mintime)
    : prefix_(prefix), mintime_(mintime), ut_() {
  if (active) ut_.Start();
}

inline ElapsedUserTimer::~ElapsedUserTimer() {
  double elapsed;
  if (ut_.IsRunning() && (elapsed = ut_.Get()) > mintime_) {
    LOG(INFO) << prefix_ << ": " << (elapsed * 1000.0) << " ms (elapsed)";
  }
}
#endif  // !defined(__native_client__)

// ----------------------------------------------------------------------
// StackTimer
//    StackTimer has an interface similar to CycleTimer that may be used to
//    measure the time between the outermost (from a time point of view) calls
//    to Start and Stop. This is useful in determining the amount of time spent
//    in a portion of multi-threaded code (prevents double counting of the same
//    time due to context switches).  It is not a subclass due to thread-safety
//    issues.  Thread-safe.
// ----------------------------------------------------------------------
class StackTimer {
 public:
  inline StackTimer();

  inline void Start();
  inline void Stop();
  inline bool Reset();
  inline bool IsRunning() const;
  inline int64_t GetCycles() const;
  inline double Get() const;
  [[deprecated("Use GetDuration instead. See <link>.")]] inline int64_t
  GetInMs() const;
  inline absl::Duration GetDuration() const;
  [[deprecated(
      "Use CycleTimerBase::CyclesToDuration instead. See "
      "<link>.")]] static inline int64_t
  CyclesToUsec(int64_t cycles);

 private:
  CycleTimer timer_;
  int stack_;
  mutable absl::Mutex m_;
};

inline StackTimer::StackTimer() { stack_ = 0; }

inline void StackTimer::Start() {  // Just save when we started
  absl::MutexLock m(m_);
  if (!stack_) timer_.Start();
  stack_++;
}

inline void StackTimer::Stop() {  // Just save when we started
  absl::MutexLock m(m_);
  stack_--;
  if (!stack_) timer_.Stop();
}

// Only reset if the timer isn't busy
inline bool StackTimer::Reset() {
  absl::MutexLock m(m_);
  if (!stack_) timer_.Reset();
  return (stack_ == 0);
}

inline bool StackTimer::IsRunning() const {
  absl::MutexLock m(m_);
  return timer_.IsRunning();
}

inline int64_t StackTimer::GetCycles() const {
  absl::MutexLock m(m_);
  return timer_.GetCycles();
}

inline double StackTimer::Get() const {
  absl::MutexLock m(m_);
  return timer_.Get();
}

inline int64_t StackTimer::GetInMs() const {
  absl::MutexLock m(m_);
  return timer_.GetInMs();
}

inline absl::Duration StackTimer::GetDuration() const {
  absl::MutexLock m(m_);
  return timer_.GetDuration();
}

/*static*/ inline int64_t StackTimer::CyclesToUsec(int64_t cycles) {
  return CycleTimerBase::CyclesToUsec(cycles);
}

// ----------------------------------------------------------------------
// SecondTimer
//    Returns the number of seconds since it was created or the last
//    time Restart was called.  It has a simplified interface and
//    returns an integer accurate to within about a second.
//    Thread-compatible.
// ----------------------------------------------------------------------
class SecondTimer {
 public:
  SecondTimer() { time(&begin_); }

  void Restart() { time(&begin_); }
  int32_t Get() const { return static_cast<int32_t>(time(nullptr) - begin_); }

 private:
  time_t begin_;  // Time this timer was started
};

// ----------------------------------------------------------------------
// TimevalData
//    A utility class not intended for public consumption.
//    Thread-compatible.
// ----------------------------------------------------------------------
class ABSL_DEPRECATED("Use absl::Time or absl::Duration instead") TimevalData {
 public:
  inline TimevalData();
  inline TimevalData(const TimevalData& d);

  inline void Start(const timeval& tv);
  inline void Add(const timeval& tv);
  inline void Reset();
  inline double Get() const;
  inline int64_t GetInMs() const;
  inline absl::Duration GetDuration() const;

 private:
  static inline int64_t TimevalToUsec(const timeval& tv) {
    return static_cast<int64_t>(1000000) * tv.tv_sec + tv.tv_usec;
  }
  int64_t start_usec_;  // start time in microseconds.
  int64_t sum_usec_;    // sum of time diffs in microseconds.
};

inline TimevalData::TimevalData() : start_usec_(0), sum_usec_(0) {}

inline TimevalData::TimevalData(const TimevalData& d)
    : start_usec_(d.start_usec_), sum_usec_(d.sum_usec_) {}

inline void TimevalData::Start(const timeval& tv) {
  start_usec_ = TimevalToUsec(tv);
}

inline void TimevalData::Add(const timeval& tv) {
  sum_usec_ += TimevalToUsec(tv) - start_usec_;
}

inline void TimevalData::Reset() {
  start_usec_ = 0;
  sum_usec_ = 0;
}

inline double TimevalData::Get() const {
  return static_cast<double>(sum_usec_) * 1e-6;
}

inline int64_t TimevalData::GetInMs() const { return sum_usec_ / 1000; }

inline absl::Duration TimevalData::GetDuration() const {
  return absl::Microseconds(sum_usec_);
}

#endif  // THIRD_PARTY_GLOOP_BASE_TIMER_H_
