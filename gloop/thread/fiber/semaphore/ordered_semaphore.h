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

// Base class for concrete ordered semaphore implementations.
// NOTE: this is an implementation detail. Please use FifoSemaphore or
// PrioritySemaphore.
//
// OrderedSemaphore provides the basic tooling for semaphore implementation
// that allocate their resources in some strict order.

#ifndef THIRD_PARTY_GLOOP_THREAD_FIBER_SEMAPHORE_ORDERED_SEMAPHORE_H_
#define THIRD_PARTY_GLOOP_THREAD_FIBER_SEMAPHORE_ORDERED_SEMAPHORE_H_

#include <cstdint>
#include <memory>

#include "absl/base/attributes.h"
#include "absl/base/casts.h"
#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "gloop/thread/fiber/select.h"

namespace thread {
namespace internal {

// Casting between signed and unsigned integral types isn't guaranteed to work
// as expected, according to the C++ spec.  Since the intptr_t values used in
// Case objects aren't interpreted, just bit_cast<> to guarantee that we always
// get back the same value that was passed in.
inline intptr_t UintptrToIntptr(uintptr_t val) {
  return absl::bit_cast<intptr_t>(val);
}
inline uintptr_t IntptrToUintptr(intptr_t val) {
  return absl::bit_cast<uintptr_t>(val);
}

// Interpret arg1 as an uintptr_t amount.
inline uintptr_t Amount(::thread::internal::CaseState* elem) {
  return IntptrToUintptr(elem->params->arg1);
}

class OrderedSemaphore {
 public:
  class Resource {
   public:
    explicit Resource(uintptr_t capacity);
    ~Resource();

    // Acquire or release the specified amount of the resource.
    void Acquire(uintptr_t amount) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
    void Release(uintptr_t amount) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

    // Removes c from waiters, waking following cases if possible.
    void Remove(::thread::internal::CaseState* c)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

    // Accessors
    uintptr_t capacity() const { return capacity_; }
    absl::Mutex* mu() const ABSL_LOCK_RETURNED(mu_) { return &mu_; }
    uintptr_t available() const ABSL_SHARED_LOCKS_REQUIRED(mu_) {
      return available_;
    }
    ::thread::internal::CaseState** waiters()
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
      return &waiters_;
    }

    // Counts the number of elements in waiters_.
    int count() const ABSL_SHARED_LOCKS_REQUIRED(mu_);

   private:
    // Wakes cases waiting for additional resources. Waiters are awoken in
    // strict order, until there are insufficient resources available for the
    // next waiter, or until no waiters remain.
    void Wake() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

    const uintptr_t capacity_;

    mutable absl::Mutex mu_;

    // Currently available resources in the semaphore.
    uintptr_t available_ ABSL_GUARDED_BY(mu_);

    // A list of cases waiting. OrderedSemaphore uses this list in strict order
    // to satisfy requests.
    ::thread::internal::CaseState* waiters_ ABSL_GUARDED_BY(mu_) = nullptr;
  };

  // Creates a semaphore with the specified capacity.  The semaphore's value
  // starts at this capacity, and must never exceed it.
  explicit OrderedSemaphore(uintptr_t capacity);

  // This type is neither copyable nor movable.
  OrderedSemaphore(const OrderedSemaphore&) = delete;
  OrderedSemaphore& operator=(const OrderedSemaphore&) = delete;

  // All pending semaphore operations must have completed prior to its
  // destruction, and all previously acquired resources must have been
  // released (i.e. current_value() == capacity()).
  ~OrderedSemaphore();

  // Releases resources from the semaphore.  Callers waiting to acquire the
  // semaphore will be woken in order if there are sufficient resources.
  //
  // REQUIRES: amount was previously acquired from this semaphore.
  void Release(uintptr_t amount) ABSL_LOCKS_EXCLUDED(resource_.mu());

  // Attempts to acquire `amount` from the semaphore without blocking. Returns
  // true and acquires the resources only if the operation can be satisfied
  // immediately while preserving the semaphore's strict acquisition order
  // (i.e. there are no callers already waiting). Otherwise returns false
  // without modifying state and without enqueueing the caller.
  //
  // On success, the caller must eventually Release() the acquired amount.
  //
  // REQUIRES: amount <= the semaphore's capacity.
  [[nodiscard]] bool TryAcquire(uintptr_t amount)
      ABSL_LOCKS_EXCLUDED(resource_.mu());

  // Returns the semaphore's current value (between 0 and capacity, inclusive).
  // Note that this is inherently racy, since the value may change before the
  // caller can use it.
  uintptr_t current_value() const ABSL_LOCKS_EXCLUDED(resource_.mu());

  // Returns the semaphore's capacity (maximum value).
  uintptr_t capacity() const { return resource_.capacity(); }

  // Return the number of waiters on this semaphore.
  // WARNING: This does a linear walk of the waiters list while holding the
  // lock. Intended for test code only.
  int WaiterCount() const ABSL_LOCKS_EXCLUDED(resource_.mu());

  // TESTING ONLY: waits until exactly the given number of fibers are blocked
  // while waiting to Acquire() the semaphore.
  void WaitForBlockedAcquirers(int nwaiters) const
      ABSL_LOCKS_EXCLUDED(resource_.mu());

  // Creates a new selectable and injects `resource_` into it.
  template <typename T>
  std::unique_ptr<::thread::internal::Selectable> MakeSelectable() {
    return std::make_unique<T>(&resource_);
  }

 private:
  // The current state of the resource.
  Resource resource_;
};

}  // namespace internal
}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_FIBER_SEMAPHORE_ORDERED_SEMAPHORE_H_
