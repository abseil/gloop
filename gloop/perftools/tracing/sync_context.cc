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

#include "gloop/perftools/tracing/sync_context.h"

#include <atomic>
#include <iostream>
#include <memory>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "gloop/perftools/tracing/multiplex_trace_event_listener.h"
#include "gloop/perftools/tracing/noop_trace_event_listener.h"
#include "gloop/perftools/tracing/perftools_verify.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/trace_event_listener.h"
#include "gloop/perftools/tracing/tracing_base.h"

namespace perftools::tracing::core {

namespace {

SyncId tls_active_sync_id() { return active_sync_id(); }

}  // namespace

std::ostream& operator<<(std::ostream& s, SyncContext::State state) {
  switch (state) {
    case SyncContext::State::kDefault:
      return s << "Default";
    case SyncContext::State::kNested:
      return s << "Nested";
    case SyncContext::State::kActive:
      return s << "Active";
    case SyncContext::State::kSuspended:
      return s << "Suspended";
    case SyncContext::State::kZombie:
      return s << "Zombie";
  }
  return s << "???";
}

// ------------------------ Shared --------------------------------

class SyncContext::Impl::Shared {
 public:
  SyncId NewSyncId() {
    return sync_id_.fetch_add(2, std::memory_order_relaxed) + 2;
  }

  SyncId NewThreadSyncId() {
    return sync_id_.fetch_add(2, std::memory_order_relaxed) + 1;
  }

 private:
  std::atomic<SyncId> sync_id_ = {kMainSyncId};
};

// ------------------------ Context --------------------------------

SyncContext::Impl::~Impl() {
  if (listener_) {
    listener_->ReleaseEventListener();
  }
}

SyncContext::Impl::Impl(SharedPtr shared, TraceEventListener* listener,
                        SyncId sync_id) noexcept
    : sync_id_(sync_id), listener_(listener), shared_(std::move(shared)) {
  DCHECK_NE(sync_id, kNoSyncId);
}

TraceEventListener* SyncContext::Impl::ActiveListener() const {
  TraceEventListener* listener = internal::active_event_listener();
  DLOG_IF(DFATAL, listener == nullptr) << "Active listener is null";
  DLOG_IF(DFATAL, state_ != State::kActive) << "State != Active";
  return (state_ == State::kActive) ? listener : nullptr;
}

TraceEventListener* SyncContext::Impl::ThisListener() const {
  if (state_ == State::kActive) {
    TraceEventListener* listener = internal::active_event_listener();
    DLOG_IF(DFATAL, listener == nullptr) << "Active listener is null";
    return listener;
  } else {
    DLOG_IF(DFATAL, listener_ == nullptr) << "Listener_ is null";
    return listener_;
  }
}

SyncContext::Impl* SyncContext::Impl::New(TraceEventListener* listener) {
  DCHECK_NE(listener, nullptr);
  auto shared = std::make_shared<Shared>();
  return new Impl(std::move(shared), listener, kMainSyncId);
}

SyncContext::Impl* SyncContext::Impl::CreateForTesting(
    TraceEventListener* listener, SyncId sync_id, SyncId active_sync_id) {
  auto shared = std::make_shared<Shared>();
  auto* context = new Impl(std::move(shared), listener, sync_id);
  context->active_sync_id_ = active_sync_id;
  return context;
}

SyncContext::Impl* SyncContext::Impl::CreateThread(StringRef label) const {
  TraceEventListener* active_listener = ActiveListener();
  if (active_listener == nullptr) return nullptr;
  SyncId sync_id = shared_->NewThreadSyncId();
  active_listener->OnTraceSpawn(sync_id, label);
  TraceEventListener* listener = active_listener->GetEventListener(sync_id);
  return listener ? new Impl(shared_, listener, sync_id) : nullptr;
}

SyncContext::Impl* SyncContext::Impl::Copy() const {
  TraceEventListener* this_listener = ThisListener();
  if (this_listener == nullptr) return nullptr;
  SyncId sync_id = shared_->NewSyncId();
  TraceEventListener* new_listener = this_listener->GetEventListener(sync_id);
  return new_listener ? new Impl(shared_, new_listener, sync_id) : nullptr;
}

SyncContext::Impl* SyncContext::Impl::CreateNoop() const {
  return new Impl(shared_, NoopTraceEventListener(), shared_->NewSyncId());
}

void SyncContext::Impl::AddListenerToCurrent(TraceEventListener* listener,
                                             StringRef label) {
  ABSL_ASSUME(listener != nullptr);

  if (TraceEventListener* current = ActiveListener()) {
    current = MultiplexTraceEventListener(current, listener);
    internal::set_active_event_listener(current);
    listener->OnTraceBeginSync(active_sync_id_, label);
  } else {
    listener->ReleaseEventListener();
  }
}

void SyncContext::Impl::AddListener(TraceEventListener* listener) {
  if (state_ != State::kDefault) {
    DLOG(DFATAL) << "Invalid state " << state_ << " in AddListener";
    listener->ReleaseEventListener();
    return;
  }
  ABSL_ASSUME(listener != nullptr);
  ABSL_ASSUME(listener_ != nullptr);
  listener_ = MultiplexTraceEventListener(listener_, listener);
}

bool SyncContext::Impl::RemoveListenerFromCurrent(
    TraceEventListener* listener) {
  DCHECK_NE(listener, nullptr);
  TraceEventListener* active_listener = ActiveListener();
  if (active_listener == nullptr) return true;

  auto [new_listener, success] = active_listener->Extract(listener);
  if (!PERFTOOLS_VERIFY(success)) return true;

  // Signal end of sync session. This is the last event delivered
  // where the listener is reachable through the active listener.
  listener->OnTraceEndSync(active_sync_id_);

  // Set the new listener minus the extracted one and release the listener.
  // We guarantee the removed listener can no longer be reached / invoked
  // from a call the `ReleaseEventListener()`.
  internal::set_active_event_listener(new_listener);
  listener->ReleaseEventListener();

  // Check if we have remaining listeners. If so, then this instance
  // is still live and should not be deleted by the caller.
  if (new_listener != nullptr) return true;

  // All listeners are removed, return `false` to the caller to indicate
  // that this instance is no longer live and must be deleted.
  internal::set_active_sync_id(kNoSyncId);
  return false;
}

bool SyncContext::Impl::RemoveListener(TraceEventListener* listener) {
  DCHECK_NE(listener, nullptr);
  if (listener_ == nullptr || state_ != State::kDefault) {
    DLOG(DFATAL) << "Invalid state " << state_ << " in RemoveListener";
    return true;
  }

  auto [new_listener, success] = listener_->CascadingRelease(listener);
  if (!success) {
    DLOG(DFATAL) << "Failed to remove listener";
    return true;
  }

  // Set the new listener and check if we have remaining listeners.
  listener_ = new_listener;
  if (new_listener != nullptr) return true;

  // All listeners are removed, return `false` to the caller to indicate
  // that this instance is empty and must be deleted.
  return false;
}

bool SyncContext::Impl::ContainsListener(TraceEventListener* listener) const {
  TraceEventListener* this_listener = ThisListener();
  return this_listener != nullptr && this_listener->Contains(listener);
}

template <SyncContext::Impl::SwapOrRestore swap_or_restore>
void SyncContext::Impl::BeforeSwap(const Impl* to) {
  // Make sure we are not restoring from a 'to be deleted' zombie state.
  // This is rare, but currently can happen because we have `tracer` code
  // executing 'inside' a `RestoreCurrentContext` call. I.e., the current
  // context is halfway being 'swapped out', and as `tracer` finalization
  // uses a `WithTraceContext empty`, we get swap/restore calls on the active
  // context while it's halfway being swapped and eventually being destroyed.
  // Note that the `to` state MUST be valid as there are no such side effects
  // or zombie moments for the application provided `to` instance. The catch
  // here is that 'to' may be the counterpart of the empty Swap/Restore call
  // of the above scenario, so we check on that in the 'after swap'.
  if (state_ == State::kZombie) return;

  // Invariant: the active TLS context is active if not empty.
  TraceEventListener* current = ActiveListener();
  if (current == nullptr) return;

  // If the `to` instance is part of the same execution graph, we will retain
  // the `active_sync_id` as the nested context is nested and thus not affect
  // the maximum synchronous execution scope. The exception here is if `to`
  // itself is a suspended execution. This should be rare, but we can and
  // should allow for it, especially in the context of co-routines.
  if (same_trace(to) && to->state_ != State::kSuspended) {
    state_ = (swap_or_restore == kSwap) ? State::kNested : State::kZombie;
    return;
  }

  if (swap_or_restore == kSwap) {
    // Suspend the current execution.
    current->OnTraceSuspendSync(active_sync_id_);
    internal::set_active_sync_id(kNoSyncId);
    state_ = State::kSuspended;
  } else {
    // End the current execution. The current context is now in a zombie state.
    current->OnTraceEndSync(active_sync_id_);
    internal::set_active_sync_id(active_sync_id_ = kNoSyncId);
    state_ = State::kZombie;
  }

  // Swap active listener back to this instance. The current instance may be
  // nested and still own an ignored nested listener instance.
  if (listener_ != nullptr) listener_->ReleaseEventListener();
  listener_ = current;
  internal::set_active_event_listener(nullptr);
}

template <SyncContext::Impl::SwapOrRestore swap_or_restore>
bool SyncContext::Impl::AfterSwap(StringRef label) {
  // Make sure we are not restoring to a zombie state
  if (state_ == State::kZombie) {
    // This is only allowed on Restore as part of a zombie Swap/Restore.
    // See zombie comments on BeforeSwap() for more details.
    PERFTOOLS_VERIFY_EQ(swap_or_restore, kRestore);
    return false;
  }

  if (TraceEventListener* current = internal::active_event_listener()) {
    // BeforeSwap() did not clear the existing context, meaning it is a nested
    // context. Verify that this holds, i.e., the right decision was made.
    DCHECK_NE(state_, State::kSuspended);
    state_ = State::kActive;
    active_sync_id_ = tls_active_sync_id();
    current->OnTraceEnterSync(sync_id_, label);
    return true;
  }

  // This should be rare, but can happen if people play fast and loose with
  // contexts. For example, nest two contexts for the same trace but restore
  // them in reversed (invalid) order.
  if (listener_ == nullptr || state_ == State::kNested) {
    LOG_EVERY_N_SEC(DFATAL, 60) << "Attempt to Swap an abandoned tracer";
    return false;
  }

  if (state_ == State::kDefault) {
    active_sync_id_ = sync_id_;
    internal::set_active_sync_id(active_sync_id_);
    listener_->OnTraceBeginSync(active_sync_id_, label);
  } else {
    internal::set_active_sync_id(active_sync_id_);
    listener_->OnTraceResumeSync(active_sync_id_);
  }
  internal::set_active_event_listener(listener_);
  listener_ = nullptr;
  state_ = State::kActive;
  return true;
}

void SyncContext::Impl::BeforeSwapCurrent(const Impl* to) {
  BeforeSwap<kSwap>(to);
}

void SyncContext::Impl::BeforeRestoreCurrent(const Impl* to) {
  BeforeSwap<kRestore>(to);
}

bool SyncContext::Impl::AfterSwapCurrent(StringRef label) {
  return AfterSwap<kSwap>(label);
}

bool SyncContext::Impl::AfterRestoreCurrent(StringRef label) {
  return AfterSwap<kRestore>(label);
}

// ------------------------ SyncContext --------------------------------

void SyncContext::AddListener(TraceEventListener* listener) {
  if (listener != nullptr) {
    if (impl_) {
      impl_->AddListener(listener);
    } else {
      impl_ = Impl::New(listener);
    }
  }
}

void SyncContext::AddListenerToCurrent(Access, TraceSpanId,
                                       TraceEventListener* listener,
                                       StringRef label) {
  if (listener != nullptr) {
    if (impl_) {
      impl_->AddListenerToCurrent(listener, label);
    } else {
      impl_ = Impl::New(listener);
      impl_->AfterSwapCurrent(label);
    }
  }
}

void SyncContext::RemoveListener(TraceEventListener* listener) {
  if (listener == nullptr) return;
  if (impl_ != nullptr) {
    if (!impl_->RemoveListener(listener)) {
      DeleteImpl();
    }
  } else {
    DLOG(DFATAL) << "RemoveListener() on an empty instance";
  }
}

void SyncContext::RemoveListenerFromCurrent(Access,
                                            TraceEventListener* listener) {
  if (listener == nullptr) return;
  if (Impl* impl = impl_; impl != nullptr) {
    // Avoid side effects.
    impl_ = nullptr;
    if (impl->RemoveListenerFromCurrent(listener)) {
      impl_ = impl;
    } else {
      delete impl;
    }
  } else {
    DLOG(DFATAL) << "RemoveListener() on an empty instance";
  }
}

}  // namespace perftools::tracing::core
