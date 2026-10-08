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

// AbstractSimpleSelectable provides an abstract base class that implements
// most of the details of the thread::internal::Selectable interface, making it
// much easier to define new thread::Case implementations, at least for simple
// cases that fit AbstractSimpleSelectable's semantics.  Subclasses need only:
//
//   (1) Override the WhenSelected method to determine what should happen when
//       the case is selected, and
//   (2) Provide a public method (e.g. OnFoobar), with a return type of
//       thread::Case, that delegates appropriately to
//       AbstractSimpleSelectable's protected OnEnabled method.
//
// AbstractSimpleSelectable provides a protected SetEnabled method, allowing
// subclasses to toggle whether or not they are currently selectable as their
// state changes.  Subclasses also have direct access to the mutex that must be
// held in order to call SetEnabled; this mutex can also be used to protect the
// subclasses's state, and is guaranteed to be held when WhenSelected is
// called.
//
// Note that AbstractSimpleSelectable's "enabled" semantics don't cover all
// possible Selectables one might want to implement.  For example, it can't be
// used to implement a synchronous channel, where a read Case and a write Case
// should proceed only when both are being selected on simultaneously by
// different Select calls.
//
// Example: Simple broadcast object implemented via AbstractSimpleSelectable:
//
//   // A Source is either empty, or holds a value of type T.  The OnValue
//   // can be used to wait until a value is available, then extract it, leaving
//   // the Source empty again.
//   template <typename T>
//   class Source : public AbstractSimpleSelectable<T, void> {
//    public:
//     // Constructs an empty Source.
//     Source() {}
//     // Returns true if the Source currently holds a value.
//     bool HasValue() const {
//       ReaderMutexLock lock(&this->mutex_);
//       return this->is_enabled();
//     }
//     // Stores a value into the Source, discarding the previous value if any.
//     void SetValue(const T& value) {
//       MutexLock lock(&this->mutex_);
//       value_ = value;
//       this->SetEnabled(true);
//     }
//     // When the Source is not empty, stores the Source's value into sink,
//     // emptying the Source in the process.
//     thread::Case OnValue(T* sink) {
//       return this->OnEnabled(sink, nullptr);
//     }
//    protected:
//     bool WhenSelected(T* sink, void* ignored) override
//        EXCLUSIVE_LOCKS_REQUIRED((AbstractSimpleSelectable<T,void>::mutex_)) {
//       *sink = value_;
//       return false;
//     }
//    private:
//     T value_ GUARDED_BY((AbstractSimpleSelectable<T, void>::mutex_));
//     DISALLOW_COPY_AND_ASSIGN(Source);
//   };
//
// See abstract_simple_selectable_test.cc for other examples.

#ifndef THIRD_PARTY_GLOOP_THREAD_FIBER_ABSTRACT_SIMPLE_SELECTABLE_H_
#define THIRD_PARTY_GLOOP_THREAD_FIBER_ABSTRACT_SIMPLE_SELECTABLE_H_

#include <cstdint>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "gloop/thread/fiber/select-internal.h"

namespace thread {

// Provides a select case that can be enabled or disabled; it is immediately
// selectable when enabled and never selectable when disabled.  Subclasses must
// override WhenSelected this to specify what happens when the case is
// selected.
template <typename T1, typename T2>
class AbstractSimpleSelectable : public thread::internal::Selectable {
 public:
  // Constructs an AbstractSimpleSelectable that is initially disabled
  // (i.e. not selectable).  It can be later enabled by calling
  // SetEnabled(true).
  AbstractSimpleSelectable() : enabled_(false) {}

  // This type is neither copyable nor movable.
  AbstractSimpleSelectable(const AbstractSimpleSelectable&) = delete;
  AbstractSimpleSelectable& operator=(const AbstractSimpleSelectable&) = delete;

  // Use the mutex to synchronize destruction, in case the effects of the
  // WhenSelected method allow another thread to delete the
  // AbstractSimpleSelectable while SetEnabled is still running.
  ~AbstractSimpleSelectable() override { absl::MutexLock lock(mutex_); }

 protected:
  // Returns true if the AbstractSimpleSelectable is currently enabled.
  bool is_enabled() const ABSL_SHARED_LOCKS_REQUIRED(mutex_) {
    return enabled_;
  }

  // Returns true if the result of OnEnabled() is currently in use by any
  // calls to Select() or related functions.
  bool is_selecting() const ABSL_SHARED_LOCKS_REQUIRED(mutex_) {
    return enqueued_list_ != nullptr;
  }

  // Implementation of Selectable interface.  Do not call these directly.  A
  // subclass may override these as long as they call the "Locked" functions
  // below; additional work may be done under `mutex_`.
  bool Handle(thread::internal::CaseState* state, bool enqueue) override {
    absl::MutexLock lock(mutex_);
    return HandleLocked(state, enqueue);
  }
  void Unregister(thread::internal::CaseState* state) override {
    absl::MutexLock lock(mutex_);
    UnregisterLocked(state);
  }

  // Does the work of the Selectable::Handle(), possibly causing is_selecting()
  // to become true if it was not already.  This must be called by any override
  // of Handle(), and its result must be returned.
  bool HandleLocked(thread::internal::CaseState* state, bool enqueue)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  // Does the work of the Selectable::Unregister(), possibly causing
  // is_selecting() to become false.  This must be called by any override of
  // Unregister().
  void UnregisterLocked(thread::internal::CaseState* state)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Sets whether the AbstractSimpleSelectable is currently enabled.  Enabling
  // a AbstractSimpleSelectable may cause WhenSelected to be called if any
  // Select calls are currently waiting.
  void SetEnabled(bool enable) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Returns a Case that is selectable when enabled() is true.
  thread::Case OnEnabled(T1* arg1, T2* arg2);

  // Called to perform side effects when the Case is selected.  Should return
  // true if the AbstractSimpleSelectable should remain enabled, or false if it
  // should become disabled.  Do not call SetEnabled directly from
  // WhenSelected.
  virtual bool WhenSelected(T1* arg1, T2* arg2)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) = 0;

  // Mutex exposed for use by subclasses to protect their own state.  In
  // particular, subclasses can safely lock mutex, mutate their state, and then
  // call SetEnabled before unlocking the mutex.
  mutable absl::Mutex mutex_;

 private:
  thread::internal::CaseState* enqueued_list_ ABSL_GUARDED_BY(mutex_) = nullptr;
  bool enabled_ ABSL_GUARDED_BY(mutex_);
};

template <typename T1, typename T2>
bool AbstractSimpleSelectable<T1, T2>::HandleLocked(
    thread::internal::CaseState* state, bool enqueue) {
  if (enabled_) {
    absl::MutexLock selector_lock(state->sel->mu);
    if (state->Pick()) {
      enabled_ = WhenSelected(reinterpret_cast<T1*>(state->params->arg1),
                              reinterpret_cast<T2*>(state->params->arg2));
    }
    return true;
  }
  if (enqueue) {
    thread::internal::PushBack(&enqueued_list_, state);
  }
  return false;
}

template <typename T1, typename T2>
void AbstractSimpleSelectable<T1, T2>::UnregisterLocked(
    thread::internal::CaseState* state) {
  thread::internal::RemoveFromList(&enqueued_list_, state);
}

template <typename T1, typename T2>
void AbstractSimpleSelectable<T1, T2>::SetEnabled(bool enable) {
  enabled_ = enable;
  for (thread::internal::CaseState *state = enqueued_list_, *next;
       enabled_ && state != nullptr; state = next) {
    next = (state->next != enqueued_list_ ? state->next : nullptr);
    absl::MutexLock selector_lock(state->sel->mu);
    if (state->Pick()) {
      enabled_ = WhenSelected(reinterpret_cast<T1*>(state->params->arg1),
                              reinterpret_cast<T2*>(state->params->arg2));
      thread::internal::RemoveFromList(&enqueued_list_, state);
    }
  }
}

template <typename T1, typename T2>
thread::Case AbstractSimpleSelectable<T1, T2>::OnEnabled(T1* arg1, T2* arg2) {
  return thread::Case{
      this,
      reinterpret_cast<intptr_t>(arg1),
      reinterpret_cast<intptr_t>(arg2),
  };
}

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_FIBER_ABSTRACT_SIMPLE_SELECTABLE_H_
