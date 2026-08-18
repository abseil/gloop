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

// A fiber-friendly FIFO semaphore implementation.
//
// Semaphores provide synchronized allocation of limited resources.  A semaphore
// is initialized with a non-zero "capacity", an integer that indicates
// maximum resource availability.  A caller can acquire part or all of this
// capacity, which decreases the semaphore's value by the acquired amount.
// If insufficient resources are available (i.e. the value is < the requested
// amount), callers wait in FIFO order until more resources become available.
// Once a caller is finished using its acquired resource, it releases it back
// to the semaphore.
//
// This is generally useful for limiting resource consumption: capping overall
// memory or disk usage for a pool of fibers, limiting the number of concurrent
// RPCs, etc.
//
//    // Initialized in the parent/root:
//    const uintptr_t max_kb_used = 8 * (2 << 20);
//    FifoSemaphore sem(max_kb_used);
//
//    // Shared routine run by numerous fibers concurrently:
//    void WaitAndDoExpensiveWork(const SomeInput& input) {
//      // Wait until there's enough free space.
//      uintptr_t kb_needed = input.EstimateScratchSpaceRequired();
//      if (Select({thread::OnCancel(), sem.OnAcquire(kb_needed)}) == 0) {
//        // Cancelled before we could acquire the needed space.
//        return;
//      }
//
//      // Successfully reserved space.
//      DoProcessing(...);
//
//      // Done: release the space so other fibers can do work.
//      sem.Release(kb_needed);
//    }
//
// Some things to keep in mind when using semaphores:
//
// 1. Acquisition is first-in, first-out: if the semaphore's current value is
//    2, and caller A tries to acquire 3, then caller B tries to acquire 1,
//    both A and B will block (even though there are sufficient resources for
//    B).  This guarantees fairness and avoids starvation, but it may not be
//    suitable for some uses.
//
// 2. It is an error to Acquire() or Release() an amount greater than the
//    semaphore's capacity.
//
// 3. Typically, callers should Release() the same amount they Acquire().
//    Failure to release previously acquired resources is usually a "leak",
//    and may deadlock other callers.  Release of unacquired resources is
//    usually an error, and may overflow the semaphore's capacity.  In all
//    cases, total resources which were acquired from the semaphore must
//    eventually be released to it.
//
// 4. As with mutexes, use caution when performing multiple acquisitions, or
//    other blocking operations while holding acquired resources.  It's better
//    to acquire all resources up front then release them piecemeal, rather
//    than the other way around.
//

#ifndef THIRD_PARTY_GLOOP_THREAD_FIBER_SEMAPHORE_FIFO_SEMAPHORE_H_
#define THIRD_PARTY_GLOOP_THREAD_FIBER_SEMAPHORE_FIFO_SEMAPHORE_H_

#include <cstdint>
#include <memory>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "absl/types/source_location.h"
#include "gloop/thread/fiber/select.h"
#include "gloop/thread/fiber/semaphore/ordered_semaphore.h"

namespace thread {

class ABSL_LOCKABLE FifoSemaphore {
 public:
  // Creates a semaphore with the specified capacity.  The semaphore's value
  // starts at this capacity, and must never exceed it.
  explicit FifoSemaphore(uintptr_t capacity);

  // This type is neither copyable nor movable.
  FifoSemaphore(const FifoSemaphore&) = delete;
  FifoSemaphore& operator=(const FifoSemaphore&) = delete;

  // Returns a Case (to pass to Select()) that will finish by acquiring the
  // requested resources from the semaphore.  Selectable only when the
  // semaphore's value is >= the requested amount.
  //
  // REQUIRES: amount <= the semaphore's capacity.
  Case OnAcquire(uintptr_t amount);

  // Convenience helper that will block to acquire the requested amount.
  void Acquire(uintptr_t amount);

  // Attempts to acquire `amount` from the semaphore without blocking.
  //
  // Returns true and acquires the resources only if the semaphore has at
  // least `amount` available AND no callers are already queued ahead. The
  // FIFO-fairness check is what prevents TryAcquire() from starving fibers
  // that have already blocked via Acquire() or Select() on OnAcquire().
  //
  // Returns false otherwise; the caller is not enqueued.
  //
  // On success, the caller must eventually Release() the acquired amount.
  //
  // REQUIRES: amount <= the semaphore's capacity.
  [[nodiscard]] bool TryAcquire(uintptr_t amount);

  // Releases resources from the semaphore.  Callers waiting to acquire the
  // semaphore will be woken in FIFO order if there are sufficient resources.
  //
  // REQUIRES: amount was previously acquired from this semaphore.
  void Release(uintptr_t amount);

  // Waits for all the pending semaphore operations to finish and for all the
  // resources to be released.
  //
  // Note: This method synchronizes with already-pending users of the semaphore;
  // it does not prevent future users from Acquire()ing more resources, and it
  // is inherently racy if used with concurrent callers.
  //
  // If there are no concurrent/future calls to acquire the resources it is
  // guaranteed that, once this method returns, all of the acquired resources
  // have been released (i.e. current_value() == capacity()).
  void WaitUntilAllResourcesReleased();

  // Returns the semaphore's current available value (between 0 and capacity,
  // inclusive). Note that this is inherently racy, since the value may change
  // before the caller can use it.
  uintptr_t current_value() const { return sem_.current_value(); }

  // Returns the semaphore's capacity (maximum value).
  uintptr_t capacity() const { return sem_.capacity(); }

 private:
  internal::OrderedSemaphore sem_;
  const std::unique_ptr<internal::Selectable> acquirer_;

  friend class FifoSemaphoreTest;
};

// RAII wrapper that takes in a Semaphore and an amount to acquire. The
// constructor does not take ownership of the semaphore. At end of construction,
// this object has acquired `amount` from the lock, which it releases upon
// destruction. The semaphore must outlive this object.
// FifoSemaphoreLock is moveable.
// NOTE: while `amount` is allowed to be 0, it is discouraged to use
//       FifoSemaphoreLock as a no-op.
class FifoSemaphoreLock {
 public:
  // Factory for FifoSemaphoreLock acquiring `amount` with the given deadline.
  // Also respects fiber cancellation.
  // Returns:
  //   - kCancelled if the thread is cancelled while waiting for the resource.
  //   - kDeadlineExceeded if the deadline passes.
  //   - kInvalidArgument if any of the passed arguments are invalid.
  static absl::StatusOr<FifoSemaphoreLock> MakeFifoSemaphoreLockWithDeadline(
      FifoSemaphore* semaphore, uintptr_t amount, absl::Time deadline,
      absl::SourceLocation loc = absl::SourceLocation::current());

  FifoSemaphoreLock(FifoSemaphore& semaphore
                        ABSL_INTERNAL_ATTRIBUTE_CAPTURED_BY_THIS,
                    uintptr_t amount);
  ABSL_DEPRECATE_AND_INLINE()
  FifoSemaphoreLock(FifoSemaphore* semaphore, uintptr_t amount)
      : FifoSemaphoreLock(*semaphore, amount) {}
  ~FifoSemaphoreLock();

  FifoSemaphoreLock() = default;
  FifoSemaphoreLock(const FifoSemaphoreLock& other) = delete;
  FifoSemaphoreLock& operator=(const FifoSemaphoreLock& other) = delete;
  FifoSemaphoreLock(FifoSemaphoreLock&& other);
  FifoSemaphoreLock& operator=(FifoSemaphoreLock&& other);

 private:
  // Internal constructor used in the factory.
  enum PreAcquiredT { kPreAcquired };
  FifoSemaphoreLock(FifoSemaphore* semaphore, uintptr_t amount,
                    PreAcquiredT pre_acquired);

  uintptr_t amount_ = 0;
  FifoSemaphore* semaphore_ = nullptr;
};

// As above, but uses the full amount of the semaphore. Allows you to use
// FifoSemaphore as a Mutex replacement for fair FIFO behavior.
class ABSL_SCOPED_LOCKABLE FifoSemaphoreMutexLock {
 public:
  explicit FifoSemaphoreMutexLock(
      FifoSemaphore& semaphore ABSL_INTERNAL_ATTRIBUTE_CAPTURED_BY_THIS)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(semaphore);

  ABSL_DEPRECATE_AND_INLINE()
  explicit FifoSemaphoreMutexLock(FifoSemaphore* semaphore)
      ABSL_EXCLUSIVE_LOCK_FUNCTION(semaphore)
      : FifoSemaphoreMutexLock(*semaphore) {}
  ~FifoSemaphoreMutexLock() ABSL_UNLOCK_FUNCTION();

  FifoSemaphoreMutexLock(const FifoSemaphoreMutexLock& other) = delete;
  FifoSemaphoreMutexLock& operator=(const FifoSemaphoreMutexLock& other) =
      delete;

 private:
  FifoSemaphore& semaphore_;
};

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_FIBER_SEMAPHORE_FIFO_SEMAPHORE_H_
