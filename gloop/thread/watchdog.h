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

//

#ifndef THIRD_PARTY_GLOOP_THREAD_WATCHDOG_H_
#define THIRD_PARTY_GLOOP_THREAD_WATCHDOG_H_

#include <sys/time.h>
#include <sys/types.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/thread_annotations.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gloop/base/callback.h"
#include "gloop/base/examine_stack.h"
#include "gloop/util/functional/from_callback.h"

class WatchDog;

typedef ::util::functional::CallbackFunctor<WatchDog*> WatchdogCallback;

// A WatchDog is a dead man's switch used by multi-threaded applications to
// check periodically that all threads (with a WatchDog) are alive. It normally
// kills the process if they are not.
//
// Do not use WatchDog for timing (use absl/time.h APIs instead).
//
// Expected usage is that a WatchDog is created for each thread, and each thread
// periodically calls the Alive() method to report that the calling thread is
// still alive. Each WatchDog's Alive() method must be called at least every
// "timeout" seconds. If a WatchDog stops checking in, the WatchDog
// implementation will eventually kill the application.
//
// Example:
// class MyThread : public Thread {
//   virtual void Run() {
//     WatchDog w("MyThread", 60);
//     while (true) {
//       w.Alive();
//       DoSomethingUseful();
//     }
//   }
//
// All public methods are thread-safe.
//
// Alive() should typically be called from the thread that created the WatchDog.
// Calling Alive from multiple threads is legal, but may cause confusing error
// reports if the watchdog times out.

class WatchDog final {
 public:
  static constexpr int32_t kDefaultTimeout = 5 * 60;  // seconds
  static constexpr absl::Duration DefaultTimeout() {
    return absl::Seconds(kDefaultTimeout);
  }

  explicit WatchDog();

  explicit WatchDog(const char* name, absl::Duration timeout);
  explicit WatchDog(absl::string_view name, absl::Duration timeout);
  explicit WatchDog(std::string name, absl::Duration timeout);

  ABSL_DEPRECATED("Use the constructor that takes absl::Duration")
  explicit WatchDog(const char* name, int32_t timeout_sec)
      : WatchDog(std::string(name), absl::Seconds(timeout_sec)) {}
  ABSL_DEPRECATED("Use the constructor that takes absl::Duration")
  explicit WatchDog(absl::string_view name, int32_t timeout_sec)
      : WatchDog(std::string(name), absl::Seconds(timeout_sec)) {}
  ABSL_DEPRECATED("Use the constructor that takes absl::Duration")
  explicit WatchDog(std::string name, int32_t timeout_sec)
      : WatchDog(std::move(name), absl::Seconds(timeout_sec)) {}

  WatchDog(const WatchDog&) = delete;
  WatchDog& operator=(const WatchDog&) = delete;

  ~WatchDog();

  // Report that calling thread is alive, and therefore has not expired. Must be
  // called at least once every "timeout" seconds.
  void Alive();

  // Place the watchdog in suspension until the next call to Alive(). While
  // suspended, a watchdog will never time out.
  void Disable() { disabled_.store(true, std::memory_order_release); }

  bool IsDisabled() { return disabled_.load(std::memory_order_relaxed); }

  // Set the callback to be run in place of TimedOut() when the timeout expires.
  // If callback==NULL, TimedOut() is called, which aborts. SetCallback() takes
  // ownership of *callback. The callback must be repeatable. It will not be
  // called after the WatchDog's destructor returns. Each call to SetCallback()
  // negates the effect of previous calls. The callback is expected to kill the
  // process. If it does not do so, it must return swiftly.
  //
  // Beware that *callback may be called from arbitrary contexts and thus
  // *callback should not acquire application-level locks, or run arbitrary
  // code. If you must run such code, do something like "alarm(20);" at the top
  // of the callback to ensure that the process will die.
  void SetCallback(WatchdogCallback callback);

  // Set the timeout to the new value and return the previous timeout.
  // Also marks the calling thread as alive, but does not affect whether or not
  // the watchdog is enabled.
  ABSL_DEPRECATED("Use SetTimeoutDuration() instead")
  int32_t SetTimeout(int32_t timeout_sec) {
    return SetTimeoutDuration(absl::Seconds(timeout_sec)) / absl::Seconds(1);
  }

  // Set the timeout to the new value and return the previous timeout.
  // Also marks the calling thread as alive, but does not affect whether or not
  // the watchdog is enabled.
  absl::Duration SetTimeoutDuration(absl::Duration timeout);

  // Call the TimedOut() method (or callbacks, if set) of any expired WatchDogs.
  //
  // By default (--watch_thread_liveness = true), a background thread will
  // periodically call CheckAlive() for you.
  static void CheckAlive();

  // The name of this watchdog.
  const std::string& name() const { return name_; }

  // Returns the current timeout.
  absl::Duration timeout_duration() const;

  // Current timeout in seconds.
  ABSL_DEPRECATED("Use timeout_duration() instead")
  int32_t timeout() const { return timeout_duration() / absl::Seconds(1); }

  // Writes a human-readable status of this watchdog.
  //
  // Uses a buffer-oriented API to be explicit about heap allocations, etc.,
  // because it is designed for calls from arbitrary contexts. See also
  // SetCallback(). It is recommended (though not necessary) that `buf_size` be
  // at least `kPrintStatusBufSize`.
  void PrintStatus(char* buf, int buf_size) const;
  static constexpr int kPrintStatusBufSize = 256;  // See PrintStatus().

  // Print the stack trace of the thread associated with this Watchdog to
  // a DebugWriter (see base/examine_stack.h).
  void PrintStackTraceTo(DebugWriter* writer, void* writer_arg);

  // By default, stacks go to stderr to increase the chances of useful output if
  // the process locks up.
  void PrintStackTrace() { PrintStackTraceTo(&DebugWriteToStderr, nullptr); }

  // The default routine called when "this" expires if no callback is set. This
  // routine crashes the program.
  [[noreturn]] void TimedOut();

  // GetTID() of the creating thread, or thread that first calls Alive().
  pid_t tid() const { return tid_.load(std::memory_order_relaxed); }

  // pthread_self() of the creating thread, or thread that first calls Alive().
  pthread_t pthread_id() const {
    return pthread_id_.load(std::memory_order_relaxed);
  }

  // Internal value for periodically checking WatchDogs. Users should not depend
  // on this value.
  // TODO: refactor the shared WatchDog state.
  static constexpr int kInternalCheckSeconds = 3;

 private:
  friend class WatchDogTest;
  friend class WatchDogChecksManipulation;

  struct DogCall {
    WatchDog* dog;
    int dogs_index;
    int gen;
    WatchdogCallback cb;
  };

  // Struct containing a snapshot of a WatchDog's internal state.
  struct WatchDogState {
    absl::Time last_called_alive;
    absl::Duration timeout;
  };
  // Helper function that does an immediate read of the state.
  WatchDogState ReadCurrentState() const;

  static void CheckTimeout(std::vector<DogCall>* expiry_calls);
  static void RunCallbacks(std::vector<DogCall>* expiry_calls);

  // Returns success value.
  static bool GetKernelStack(pid_t tid, std::string* stack);

  // Waits until CheckTimeout isn't in progress, then pauses checks on all
  // watchdogs. Calling Alive doesn't resume checks.
  static void PauseChecks();

  // Cancels one call to PauseChecks. If the count of active PauseChecks goes to
  // 0, it resumes checks on all watchdogs and sets last_called_alive_ to Now().
  // It doesn't change the disabled_ state. Should be called only once per every
  // active PauseChecks.
  static void ResumeChecks();

  void SetCrashReasonFromStuckThread();

  // Internal implementations of TimedOut() and PrintStatus(). When possible, we
  // can bind the exact values used to determine that this WatchDog expired.
  // The 'cached_expiration_time' parameter is the value of the (potentially
  // stale) cached clock at the time the watchdog was determined to have
  // expired. It is passed in to ensure that the expiration message is accurate
  // and reflects the conditions at the time of expiry detection.
  [[noreturn]] void TimedOutInternal(const WatchDogState& state,
                                     absl::Time cached_expiration_time);
  void PrintStatusInternal(char* buf, int buf_size, const WatchDogState& state,
                           absl::Time now_for_check) const;

  void PrintExpirationMessage(char* buf, int buf_size,
                              const WatchDogState& state,
                              absl::Time now_for_check) const;

  void RefreshAliveTimestamp();

  // Last time that Alive() was called, in unix nanoseconds.
  // May be somewhat stale as the time source is the cached clock.
  // This is int64_t instead of absl::Time so that it can be a lockfree atomic.
  std::atomic<int64_t> last_called_alive_unix_nanos_;

  // If true, Alive() has been called at least once.
  std::atomic<bool> alive_called_;

  // If true, expiry checks are disabled.
  std::atomic<bool> disabled_;

  // Generation number of watchdog, to prevent it from being confused with
  // another (constant after init).
  int generation_;

  // GetTID() of the creating thread, or thread that first calls Alive().
  std::atomic<pid_t> tid_;

  // Index of this in dogs_[].
  int dogs_index_ ABSL_GUARDED_BY(dogs_lock_);

  // GetTID() of the thread running the callback, or 0.
  pid_t callback_tid_ ABSL_GUARDED_BY(dogs_lock_);

  // pthread_self() of the creating thread, or thread that first calls Alive().
  std::atomic<pthread_t> pthread_id_;

  // How much leeway to grant before killing process, in nanoseconds.  This is
  // int64_t instead of absl::Duration so that it can be a lockfree atomic.
  std::atomic<int64_t> timeout_duration_nanos_;

  // If non-null, this callback is run instead of TimedOut().
  WatchdogCallback callback_ ABSL_GUARDED_BY(dogs_lock_);

  // User-supplied WatchDog name.
  const std::string name_;

  // The error message generated by `TimedOutInternal()` is time-sensitive.  We
  // save it here so that it doesn't have to be regenerated at a later time in
  // the signal-handler invoked by `SetCrashReasonFromStuckThread()`. We've seen
  // a case where sending a signal to the stuck thread fixed the thread, leading
  // to a confusing error message (b/193425898#comment19).
  std::string crash_reason_message_;

  // Protects global WatchDog data.
  ABSL_CONST_INIT static absl::Mutex dogs_lock_;

  // List of all WatchDogs so that they can be checked.
  ABSL_CONST_INIT static std::vector<WatchDog*>* dogs_
      ABSL_GUARDED_BY(dogs_lock_) ABSL_PT_GUARDED_BY(dogs_lock_);

  // Generation of WatchDogs.
  ABSL_CONST_INIT static int generation_count_ ABSL_GUARDED_BY(dogs_lock_);

  // Used to ensure CheckTimeout isn't in progress before pausing checks.
  ABSL_CONST_INIT static bool check_in_progress_ ABSL_GUARDED_BY(dogs_lock_);

  // If greater than 0 then timeout checks are paused on all watchdogs in the
  // process.
  ABSL_CONST_INIT static int active_pauses_ ABSL_GUARDED_BY(dogs_lock_);
};

#endif  // THIRD_PARTY_GLOOP_THREAD_WATCHDOG_H_
