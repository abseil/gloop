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

// Provides generically useful Selectables for use with Select.

#ifndef THIRD_PARTY_GLOOP_THREAD_FIBER_SELECTABLES_H_
#define THIRD_PARTY_GLOOP_THREAD_FIBER_SELECTABLES_H_

#include <atomic>

#include "absl/base/config.h"  // IWYU pragma: keep
#include "absl/log/check.h"
#include "gloop/base/spinlock.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/trace_source_location.h"
#include "gloop/thread/fiber/select-internal.h"

namespace thread {

// PermanentEvent
// ---------------
// Provides a level-triggered event which may be added to a Select statement.
// PermanentEvents may only transition into a notified state.  Selecting
// against OnEvent() for an event that has already been signalled will always
// return immediately.
//
// Memory ordering: For any threads X and Y, if X calls `Notify()`, then any
// action taken by X before it calls `Notify()` is visible to thread Y after:
//  * Y selects OnEvent(), or
//  * Y receives a `true` return value from `HasBeenNotified()`
//
// Example usage:
//     void ProduceValues(thread::Channel<int>* queue,
//                        thread::PermanentEvent* quit) {
//       int i = 0;
//       while (true) {
//         switch (thread::Select({ queue->writer()->OnWrite(i),
//                                  quit->OnEvent() })) {
//           case 0: {
//             ++i;  // Prepare next write
//             break;
//           }
//           case 1: {
//             ...  // quit was signalled, stop producing
//             return;
//           }
//         }
//       }
//     }
class PermanentEvent : public internal::Selectable {
 public:
  PermanentEvent() = default;

  ~PermanentEvent() override {
    // We acquire the lock here so that PermanentEvent can synchronize
    // its own deletion.
    lock_.lock();
    DCHECK(enqueued_list_ == nullptr);
#if defined(ABSL_HAVE_THREAD_SANITIZER)
    // This is normally superfluous (and omitting it saves enough cycles that we
    // care) but as of 2026Q1 omitting the unlock confuses TSAN's lock
    // accounting in detect_deadlocks mode, at least when running with Python.
    lock_.unlock();
#endif
  }

  PermanentEvent(const PermanentEvent&) = delete;
  PermanentEvent& operator=(const PermanentEvent&) = delete;

  // Signal that the event has occurred.  Any Selectors on this event will be
  // immediately notified, future Select statements against this event will be
  // non-blocking.  May only be called once.
  void Notify(perftools::tracing::StringRef label =
                  perftools::tracing::TraceSourceLocation::current());

  // Returns true if Notify() has been called.  False otherwise.
  bool HasBeenNotified() const;

  // May be passed to Select.  Will always evaluate immediately for an event
  // that has already been notified.  Once the case has been signalled, then
  // deleting the PermanentEvent will not interfere with the caller of Notify().
  Case OnEvent() const {
    Case c = {const_cast<PermanentEvent*>(this)};
    return c;
  }

  // Implementation of Selectable interface.
  bool Handle(internal::CaseState* c, bool enqueue) override;
  void Unregister(internal::CaseState* c) override;

 private:
  friend class Fiber;

  struct CancellationEventTag final {
    explicit CancellationEventTag() = default;
  };

  struct SuppressCancellationColorCheckTag final {
    explicit SuppressCancellationColorCheckTag() = default;
  };

  // Construct an event that exists in particular to synchronize on cancellation
  // of a fiber.
  explicit PermanentEvent(CancellationEventTag) : cancellation_event_(true) {}

  // Like the public HasBeenNotified method, but without a check for
  // cancellation coloring.
  bool HasBeenNotified(SuppressCancellationColorCheckTag) const {
    return notified_.load(std::memory_order_acquire);
  }

  SpinLock lock_;  // Synchronizes notification with Selector (un)registration
  std::atomic<bool> notified_{false};

  // Does this event exist to track cancellation of a fiber? This is used for
  // the purpose of <link> checks.
  const bool cancellation_event_ = false;

  internal::CaseState* enqueued_list_ = nullptr;
};

// NonSelectableCase()
// -------------------
// Provides a 'null' case which will never evaluate as ready by Select.  This
// may be used to substitute a Selectable that is no longer of interest within a
// set, without re-labeling adjacent elements.
//
// Example:
//   int item;
//   bool ok;
//   thread::CaseArray cases = { chan1.reader()->OnRead(&item, &ok),
//                               chan2.reader()->OnRead(&item, &ok) };
//
//   while (1) {
//     int index = thread::Select(cases);
//     if (!ok) {
//       // Channel has been closed
//       cases[index] = NonSelectableCase();
//     } else {
//       ...  process item
//     }
Case NonSelectableCase();

// AlwaysSelectableCase()
// ----------------------
// Provides case which will always evaluate as ready by Select.  This may be
// used when returning a Case for an event that is already known to be ready.
//
// Example:
//   class WorkItem {
//    public:
//     // Start the work and return a case that will become ready when
//     // the work has completed.
//     thread::Case Start() {
//       if (!status_.ok()) {
//         // An error has already been detected, so the work has completed.
//         return thread::AlwaysSelectableCase();
//       } else {
//         ... start real work ...
//       }
//     }
//    private:
//     util::Status status_;
//   };
Case AlwaysSelectableCase();

namespace internal {
void CheckActiveCancellationColor();
}  // namespace internal

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_FIBER_SELECTABLES_H_
