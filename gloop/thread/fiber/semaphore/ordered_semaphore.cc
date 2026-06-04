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

// Implementation of the semaphore.

#include "gloop/thread/fiber/semaphore/ordered_semaphore.h"

#include <cstdint>

#include "absl/log/check.h"
#include "absl/log/die_if_null.h"
#include "absl/log/log.h"
#include "absl/log/vlog_is_on.h"
#include "absl/synchronization/mutex.h"
#include "gloop/thread/fiber/select.h"

namespace thread {
namespace internal {

OrderedSemaphore::Resource::Resource(const uintptr_t capacity)
    : capacity_(capacity), available_(capacity) {
  if (VLOG_IS_ON(5)) {
    mu_.EnableDebugLog("ordered_semaphore");
  }
}

OrderedSemaphore::Resource::~Resource() {
  absl::MutexLock l(mu_);
  // If any callers are still waiting that's a bug.
  CHECK(waiters_ == nullptr) << "Destroying a semaphore with active waiters!";
  available_ = 0;
}

void OrderedSemaphore::Resource::Acquire(const uintptr_t amount) {
  mu_.AssertHeld();
  CHECK_GE(available_, amount)
      << "Resource oversubscribed. Only " << available_ << " are available, "
      << amount << " was requested.";
  available_ -= amount;
}

void OrderedSemaphore::Resource::Release(const uintptr_t amount) {
  mu_.AssertHeld();
  // Sanity check against mis-use.
  CHECK_GE(capacity_, available_);
  const uintptr_t used = capacity_ - available_;
  CHECK_GE(used, amount) << ": attempted to release " << amount
                         << ", which would overflow the "
                         << "semaphore (max: " << capacity_
                         << ", currently: " << available_ << ")";
  available_ += amount;

  // Wake any cases currently waiting to acquire the semaphore.
  Wake();
}

void OrderedSemaphore::Resource::Remove(::thread::internal::CaseState* c) {
  mu_.AssertHeld();
  if (waiters_ != nullptr && c->prev != nullptr) {
    ::thread::internal::RemoveFromList(&waiters_, c);
  }
  // Other cases may have been blocked behind this one -- see if they can now
  // be awakened.
  Wake();
}

void OrderedSemaphore::Resource::Wake() {
  mu_.AssertHeld();
  if (waiters_ == nullptr) return;  // No waiters.

  ::thread::internal::CaseState* waiter = ABSL_DIE_IF_NULL(waiters_);
  bool done = false;
  while (!done) {
    const uintptr_t amount = internal::Amount(waiter);
    if (available_ < amount) {
      // This waiter can't be woken up right now; since we handle acquisitions
      // in order, there's no more work to do.
      return;
    }

    absl::MutexLock l(waiter->sel->mu);
    // We must check before RemoveFromList as that can change waiters_.
    done = waiter->next == waiters_;
    // Mutex must be held for as long as we access waiter after this point.
    if (waiter->Pick()) {
      Acquire(amount);
      ::thread::internal::RemoveFromList(&waiters_, waiter);
      if (waiters_ == nullptr) return;  // This was the last waiter.
    }
    waiter = waiter->next;
  }
}

int OrderedSemaphore::Resource::count() const {
  mu_.AssertReaderHeld();
  if (waiters_ == nullptr) return 0;
  int result = 1;
  for (const auto* w = waiters_->next; w != waiters_; w = w->next) ++result;
  return result;
}

OrderedSemaphore::OrderedSemaphore(const uintptr_t capacity)
    : resource_(capacity) {}

OrderedSemaphore::~OrderedSemaphore() {
  // All previously acquired resources must have been released.
  CHECK_EQ(capacity(), current_value())
      << "Destroying a semaphore without releasing its resources!";
}

void OrderedSemaphore::Release(const uintptr_t amount) {
  absl::MutexLock l(*resource_.mu());
  resource_.Release(amount);
}

bool OrderedSemaphore::TryAcquire(const uintptr_t amount) {
  CHECK_GE(resource_.capacity(), amount);
  absl::MutexLock l(*resource_.mu());
  // Preserve order: refuse if any caller is already queued, even if there
  // would otherwise be sufficient capacity for us.
  if (*resource_.waiters() != nullptr || resource_.available() < amount) {
    return false;
  }
  resource_.Acquire(amount);
  return true;
}

uintptr_t OrderedSemaphore::current_value() const {
  absl::ReaderMutexLock l(*resource_.mu());
  return resource_.available();
}

int OrderedSemaphore::WaiterCount() const {
  absl::ReaderMutexLock l(*resource_.mu());
  return resource_.count();
}

void OrderedSemaphore::WaitForBlockedAcquirers(const int nwaiters) const {
  absl::ReaderMutexLock l(*resource_.mu());
  auto reached = [this, nwaiters]() -> bool {
    resource_.mu()->AssertReaderHeld();
    const int cnt = resource_.count();
    VLOG(1) << "Available: " << resource_.available() << ". Waiting for "
            << nwaiters << " waiters, currently " << cnt;
    return cnt == nwaiters;
  };
  resource_.mu()->Await(absl::Condition(&reached));
}

}  // namespace internal
}  // namespace thread
