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

#include "gloop/thread/fiber/selectables.h"

#include <atomic>

#include "absl/base/no_destructor.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"
#include "gloop/base/cancellation_coloring.h"
#include "gloop/base/spinlock.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/tracing.h"
#include "gloop/thread/fiber/select.h"

namespace thread {

// PermanentEvent
bool PermanentEvent::Handle(internal::CaseState* c, bool enqueue) {
  if (cancellation_event_) {
    internal::CheckActiveCancellationColor();
  }

  SpinLockHolder l1(lock_);

  if (notified_.load(std::memory_order_relaxed)) {  // Synchronized by lock_
    absl::MutexLock l2(c->sel->mu);
    // Consider that in the presence of a race with another Selectable,
    // c->Pick() may return false in this case.  This is safe as we are not
    // required to maintain an active list after notification has been
    // delivered.
    return c->Pick();
  } else if (enqueue) {
    internal::PushBack(&enqueued_list_, c);
  }

  return false;
}

void PermanentEvent::Unregister(internal::CaseState* c) {
  SpinLockHolder l1(lock_);
  if (!notified_.load(std::memory_order_relaxed)) {
    // We only maintain lists of active cases up until notification.
    internal::RemoveFromList(&enqueued_list_, c);
  }
}

void PermanentEvent::Notify(perftools::tracing::StringRef label) {
  // If traced, record the causality of the event being notified (signaled).
  perftools::tracing::TraceSignal(this, label);

  SpinLockHolder l(lock_);

  DCHECK(!notified_.load(std::memory_order_relaxed))
      << "Notify() method called more than once for "
      << "PermanentEvent object " << static_cast<void*>(this);
  notified_.store(true, std::memory_order_release);

  // The transition to a notified state is a permanent one, so we tear down any
  // enqueued cases.  We must be careful to synchronize against this in the
  // future in both the Handle(..., true) and Unregister cases.
  while (enqueued_list_) {
    absl::MutexLock l2(enqueued_list_->sel->mu);
    enqueued_list_->Pick();
    // Continued storage of enqueued_list_ after Pick() is guaranteed by sel->mu
    internal::RemoveFromList(&enqueued_list_, enqueued_list_);
  }
}

bool PermanentEvent::HasBeenNotified() const {
  if (cancellation_event_) {
    internal::CheckActiveCancellationColor();
  }

  return HasBeenNotified(SuppressCancellationColorCheckTag{});
}

// NonSelectable: an ironic implementation of a Selectable.
class NonSelectable : public internal::Selectable {
 public:
  NonSelectable() = default;
  ~NonSelectable() override = default;

  // Implementation of Selectable interface.
  bool Handle(internal::CaseState* c, bool enqueue) override { return false; }
  void Unregister(internal::CaseState* c) override {}
};

Case NonSelectableCase() {
  // TODO: Select could be specialized against NonSelectable?
  static absl::NoDestructor<NonSelectable> non_selectable;
  return {non_selectable.get()};
}

// AlwaysSelectable: a trivial implementation of a Selectable.
class AlwaysSelectable : public internal::Selectable {
 public:
  AlwaysSelectable() = default;
  ~AlwaysSelectable() override = default;

  // Implementation of Selectable interface.
  bool Handle(internal::CaseState* c, bool enqueue) override {
    absl::MutexLock lock(c->sel->mu);
    // This selectable is always ready, so ask the selector to pick it.
    // Note: it may not be picked in case another selectable has already
    // been picked.
    return c->Pick();
  }
  void Unregister(internal::CaseState* c) override {}
};

Case AlwaysSelectableCase() {
  static absl::NoDestructor<AlwaysSelectable> always_selectable;
  return {always_selectable.get()};
}

void internal::CheckActiveCancellationColor() {
#ifndef NDEBUG
  // It doesn't make sense to query or select on cancellation of a fiber from a
  // function that respects some other ecosystem's cancellation system.
  //
  // Note that we must allow both kFibers and kUnknown here because fiber
  // cancellation APIs can't rely on a color being annotated
  // (http://shortn/_RaEFhLTkg3).
  switch (const auto active_color =
              base::internal::GetActiveCancellationColor()) {
    case base::internal::CancellationColor::kFibers:
    case base::internal::CancellationColor::kUnknown:
      break;

    default:
      LOG(FATAL) << "Must not select on fiber cancellation from functions of "
                    "other colors; see <link>. "
                    "Current active color on this thread: "
                 << active_color;
  }
#endif
}

}  // namespace thread
