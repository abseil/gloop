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

#include "gloop/concurrent/barrier/incremental_barrier.h"

#include <assert.h>

#include <cstdint>
#include <functional>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/tracing.h"
#include "gloop/util/atomic_danger/refcount.h"

namespace concurrent {

// Essentially the same as IncrementalBarrier in that it is an alternative for a
// barrier closure in which the value of N is not known ahead of time. The
// interface is less friendly though, but it's good enough for us to implement
// IncrementalBarrier with.
//
// Also, as opposed to IncrementalBarrier, InternalIncrementalBarrier is
// self-deleting.
//
// This class is thread-safe.
//
// NOTE: You may be wondering why we need another class here at
// all. Mainly, this is needed because of the interface we expose for
// IncrementalBarrier. Specifically, ~IncrementalBarrier() is run before the
// 'done_closure' is run. The key problem here is that the callbacks returned by
// IncrementalBarrier::Get() may be run at any time either before or after
// ~IncrementalBarrier(). As such, it is not possible to have the callbacks
// returned by IncrementalBarrier::Get() refer to a member function of
// IncrementalBarrier, and another class must be used. BarrierClosure sounds
// like a likely candidate, but there we suffer the problem that got us here in
// the first place: N must be specified when the BarrierClosure is constructed.
class InternalIncrementalBarrier {
 public:
  // Similar to BarrierClosure but setting N_ to 1 saves one for
  // IncrementalBarrier's destructor.
  explicit InternalIncrementalBarrier(
      absl::AnyInvocable<void() &&> done_closure,
      perftools::tracing::StringLabel label)
      : done_closure_(std::move(done_closure)), label_(std::move(label)) {}

  // Used when wrapped as a std::function.
  void operator()() {
    // This is the same as in BarrierClosure.
    perftools::tracing::TraceSignal(this, label_);
    if (N_.Dec()) {
      perftools::tracing::TraceObserved(this, label_);
      absl::AnyInvocable<void() &&> done = std::move(done_closure_);
      delete this;
      std::move(done)();
    }
  }

  std::function<void()> FunctionInc() {
    N_.Inc();
    return std::ref(*this);
  }

  absl::AnyInvocable<void() &&> InvocableInc() {
    N_.Inc();
    return [this]() { (*this)(); };
  }

 private:
  atomic_danger::RefCount<intptr_t> N_;
  absl::AnyInvocable<void() &&> done_closure_;
  perftools::tracing::StringLabel label_;
};

IncrementalBarrier::IncrementalBarrier(
    absl::AnyInvocable<void() &&> done_closure,
    perftools::tracing::StringLabel label)
    : internal_barrier_(new InternalIncrementalBarrier(std::move(done_closure),
                                                       std::move(label))) {}

IncrementalBarrier::~IncrementalBarrier() {
  // Force decrementing of InternalIncrementalBarrier's refcount.
  // This means we won't be adding any new callbacks so
  // InternalIncrementalBarrier can behave just like BarrierClosure
  // now.
  (*internal_barrier_)();
}

std::function<void()> IncrementalBarrier::FunctionInc() {
  return internal_barrier_->FunctionInc();
}

absl::AnyInvocable<void() &&> IncrementalBarrier::InvocableInc() {
  return internal_barrier_->InvocableInc();
}

}  // namespace concurrent
