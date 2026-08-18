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

// PeriodicClosure will periodically call the given closure with a specified
// period in a background thread.  After Start() returns, the thread is
// guaranteed to have started and after Stop() returns, the thread is
// guaranteed to be stopped. Start()/Stop() may be called more than once; each
// pair of calls will result in a new thread being created and subsequently
// destroyed.
//
// PeriodicClosure runs the closure as soon as any previous run both is
// complete and was started more than "interval" earlier.  Thus, runs are
// both serialized, and normally have a period of "interval" if no run
// exceeds the time. The first invocation of the closure happens immediately
// upon running Start(), unless a startup delay has been configured.
//
// Note that, if the closure takes longer than two intervals to finish, then
// PeriodicClosure will "skip" at least one call to the closure.  For
// instance, if the period is 50ms and the closure starts runs at time 0 for
// 150ms, then the closure will immediately start executing again at time 150,
// but there will be no closure runs corresponding to times 50 or 100.  This
// is especially important to remember when using a simulated clock: advancing
// simulated time atomically over N intervals will not cause the closure to be
// called N times.
//
// This object is thread-safe.
//
// Example:
//
//   class Foo {
//    public:
//     Foo() : periodic_closure_([this]() { Bar(); },
//                               absl::Seconds(1)) {
//       periodic_closure_.Start();
//     }
//
//     ~Foo() {
//       periodic_closure_.Stop();
//     }
//
//    private:
//     void Bar() { ... }
//
//     PeriodicClosure periodic_closure_;
//   };

#ifndef THIRD_PARTY_GLOOP_THREAD_PERIODICCLOSURE_H_
#define THIRD_PARTY_GLOOP_THREAD_PERIODICCLOSURE_H_

#include <memory>
#include <string>

#include "absl/base/nullability.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock_interface.h"
#include "absl/time/time.h"
#include "absl/types/source_location.h"

namespace eventmanager {
class PeriodicClosure;
}  // namespace eventmanager

class Thread;
namespace thread {

// Provides the ability to customize several aspects of the PeriodicClosure.
// Passed to constructor of PeriodicClosure.
class PeriodicClosureOptions {
 public:
  PeriodicClosureOptions(
      absl::SourceLocation loc = absl::SourceLocation::current());

  // Specifies the thread name prefix (see the description in class Thread).
  const std::string& name_prefix() const { return name_prefix_; }
  PeriodicClosureOptions& set_name_prefix(absl::string_view name_prefix) {
    name_prefix_ = std::string(name_prefix);
    return *this;
  }

  // The clock allows injection of a simulated clock, for testing.  This
  // PeriodicClosure does not assume ownership of clock, but clock must remain
  // valid for as long as this PeriodicClosure exists.
  absl::Clock* clock() const { return clock_; }
  PeriodicClosureOptions& set_clock(absl::Clock* clock) {
    clock_ = clock;
    return *this;
  }

  // Specifies the length of sleep before the first invocation of the closure.
  // By default, the delay is ZeroDuration; that is, the closure is invoked
  // immediately after running Start(). A startup delay can be be used for
  // adding a random jitter to avoid synchronous behavior across multiple
  // periodic closures.
  absl::Duration startup_delay() const { return startup_delay_; }
  PeriodicClosureOptions& set_startup_delay(absl::Duration startup_delay) {
    startup_delay_ = startup_delay;
    return *this;
  }

  // If true, forces this PeriodicClosure to use a dedicated thread. NOTE: doing
  // so can have substantial memory cost to google if used in a widely used
  // library. See <link>.
  //
  // This option should *only* be set as a temporary measure. Blocking code
  // using PeriodicClosure should use fibers or <link> instead.
  //
  // TODO: Remove this option.
  bool force_exclusive_thread() const { return force_exclusive_thread_; }
  PeriodicClosureOptions& set_force_exclusive_thread(
      bool force_exclusive_thread) {
    force_exclusive_thread_ = force_exclusive_thread;
    return *this;
  }

 private:
  std::string name_prefix_;
  absl::Clock* clock_;
  absl::Duration startup_delay_;
  bool force_exclusive_thread_;
};

class PeriodicClosure {
 public:
  PeriodicClosure(
      absl::AnyInvocable<void()> fun, absl::Duration interval,
      const PeriodicClosureOptions& options = PeriodicClosureOptions());

  // This type is neither copyable nor movable.
  PeriodicClosure(const PeriodicClosure&) = delete;
  PeriodicClosure& operator=(const PeriodicClosure&) = delete;

  // Start the background thread which will be calling the closure.
  void Start() { impl_->Start(); }

  // (Blocking.) Wait until the closure is not running, then trigger an
  // immediate new run of the closure in the background thread.  Return once
  // the new run has completed.  The background thread will resume its normal
  // processing after this forced run is completed.
  //
  // Calling RunNow() multiple times from different threads will cause all
  // callers to block until a single run has both started and finished.
  //
  // Must not be called after or concurrently with Stop().
  void RunNow() { impl_->RunNow(); }

  // Non-blocking version of RunNow(): ensures, that a new run of the closure
  // will happen soon, whenever the background thread is scheduled, without
  // waiting out the interval.  If there is a current run already ongoing, a
  // new one will be started as soon as the current one completes.
  //
  // Calling RunSoon() multiple times from different threads while the
  // background thread does not get a chance to start the requested run will
  // force only one single out-of-schedule run.
  void RunSoon() { impl_->RunSoon(); }

  // (Blocking.) Prevents the PeriodicClosure from starting any new runs of
  // the closure, and blocks until any current run of the closure has
  // completed.  The PeriodicClosure must be Start()'d before it is Stop()'d and
  // Stop()'d before it is destroyed.
  void Stop() { impl_->Stop(); }

  // Get the interval to wait between runs.
  absl::Duration Interval() const { return impl_->Interval(); }

  // Change the interval to wait between runs.
  //
  // Note that this can race with an ongoing run of the closure, and so:
  //   * May not apply immediately
  //   * Will not change how long an existing waiting closure is stopped
  void SetInterval(absl::Duration interval) { impl_->SetInterval(interval); }

 private:
  class Impl {
   public:
    virtual ~Impl() = default;

    virtual void Start() = 0;

    virtual void RunNow() = 0;

    virtual void RunSoon() = 0;

    virtual void Stop() = 0;

    virtual absl::Duration Interval() const = 0;

    virtual void SetInterval(absl::Duration interval) = 0;
  };

  static Impl* CreateEventManagerPeriodicClosure(
      absl::AnyInvocable<void()>& fun, absl::Duration interval,
      const PeriodicClosureOptions& options);

  friend class ThreadPeriodicClosure;
  friend class eventmanager::PeriodicClosure;

  const absl_nonnull std::unique_ptr<Impl> impl_;
};

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_PERIODICCLOSURE_H_
