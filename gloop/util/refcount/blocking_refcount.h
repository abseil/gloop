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

// BlockingRefcount provides a mechanism to count activities being
// performed by multiple threads and to wait for those outstanding
// activities to complete.  It differs from BlockingCounter in that
// the number of activities which are waited upon is not fixed, and
// in that BlockingRefcount is reusable.  Also, we don't currently
// provide a reliable way to determine that you hold the last
// reference, since we expect a thread to wait for all references to
// be released.
//
// For example:
//
// void Parent(Executor* executor) {
//   BlockingRefcount blocking_refcount;
//   while (...) {
//     blocking_refcount.Inc();
//     executor->Add(NewCallback(Child, &blocking_refcount, ...));
//   }
//   blocking_refcount.WaitForZero();
// }
// void Child(BlockingRefcount* blocking_refcount, ...) {
//   // Do work.
//   ...
//   blocking_refcount->Dec();
// }
//
// Normal reference-counting rules apply.  If you hold a reference, you
// may safely create additional references.
//
// For convenience, we also provide a copyable RAII reference-holding class,
// BlockingRefcountReference:
//
// void Parent(Executor* executor) {
//   BlockingRefcount blocking_refcount;
//   {
//     BlockingRefcountReference ref(&blocking_refcount);
//     while (...) {
//       executor->Add(NewCallback(Child, ref, ...));
//     }
//   }
//   blocking_refcount.WaitForZero();
// }
// void Child(BlockingRefcountReference ref, ...) {
//   // Do work.
//   ...
// }

#ifndef THIRD_PARTY_GLOOP_UTIL_REFCOUNT_BLOCKING_REFCOUNT_H_
#define THIRD_PARTY_GLOOP_UTIL_REFCOUNT_BLOCKING_REFCOUNT_H_

#include <atomic>
#include <cstdint>
#include <memory>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/macros.h"
#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

namespace util {

// Provides a reference-counting mechanism that enables a thread
// to wait for the count to reduce to 0.
class BlockingRefcount {
 public:
  // Create a BlockingRefcount with an initial reference count of 0.
  // If you need to start with some number of references, call IncN
  // immediately afterwards.
  BlockingRefcount() = default;
  // Creates a BlockingRefcount with static storage duration. A global variable
  // constructed this way avoids lifetime issues that can occur on program
  // startup and shutdown.
  // (See
  // http://https://github.com/abseil/abseil-cpp/tree/master/absl/base/const_init.h;rcl=394119395)
  //
  // For refcounts allocated on the heap and stack, prefer to use the
  // default constructor, which can interact more fully with the thread
  // sanitizer.
  //
  // Recommended usage:
  //   namespace foo {
  //   ABSL_CONST_INIT util::BlockingRefcount kRefcount(absl::kConstInit);
  //   }  // namespace foo
  explicit constexpr BlockingRefcount(absl::ConstInitType t) : mu_(t) {}

  // Non-copyable, non-moveable.
  BlockingRefcount(const BlockingRefcount&) = delete;
  BlockingRefcount& operator=(const BlockingRefcount&) = delete;

  // Destroys the object *without* waiting for the reference count to
  // decrease to 0.
  //
  // It is not safe to destroy this object while there are racing
  // decrements, to ensure there are no racing decrements you must either
  // call `WaitForZero` or have some other external knowledge.
  // In particular, `count() == 0` is not sufficient to guarantee that there are
  // no racing decrements.
  ~BlockingRefcount() = default;

  // Increase the reference count by 1.
  // Precondition: either the counter is already nonzero, or there are no
  // pending WaitForZero* calls
  void Inc() { IncN(1); }
  // Increase the reference count by number.
  // Precondition: either the counter is already nonzero, or there are no
  // pending WaitForZero* calls
  void IncN(int64_t n) {
    DCHECK_GE(n, 0);
    count_.fetch_add(n, std::memory_order_relaxed);
  }
  // Decrease the reference count by 1.  It is an error to decrease the
  // reference count below 0.
  void Dec() { DecN(1); }
  // Decrease the reference count by number.  It is an error to decrease
  // the reference count below 0.
  void DecN(int64_t n);

  // Wait for the reference count to decrement to zero.  When used to
  // maintain a conventional reference count and a single waiter, it
  // is safe for the waiter to delete this object once WaitForZero
  // returns.  It is safe for multiple threads to wait simultaneously.
  // Use caution when allowing multiple threads to increment and
  // decrement the count simultaneously, with the count potentially
  // going to zero temporarily.  The order of racing decrements and
  // increments will determine whether the count actually reaches zero
  // or not, the timing of the WaitForZero call will determine whether
  // the waiter observes the ephemeral zero, and, even if waiters
  // awaken, the count may no longer be zero by the time this method
  // returns.
  void WaitForZero() const;
  // Like WaitForZero, but with a deadline.
  // Returns true if count reached zero, false if this call timed out.
  bool WaitForZeroWithDeadline(absl::Time deadline) const;
  // Equivalent to WaitForZeroWithDeadline(absl::Now() + timeout)
  bool WaitForZeroWithTimeout(absl::Duration timeout) const;

  // Like WaitForZeroWithTimeout, but with the timeout specified
  // in milliseconds.
  ABSL_DEPRECATE_AND_INLINE()
  bool WaitForZeroWithTimeout(int ms) const {
    return WaitForZeroWithTimeout(absl::Milliseconds(ms));
  }

  // Returns the value of the reference count.  Provides a snapshot of
  // the current state, and may be out of date by the time this method
  // returns.  In general, do not use this to determine whether you
  // hold the last reference, since another thread may increment the
  // count.
  int64_t count() const {
    return count_.load(std::memory_order_acquire) & ~kWaiting;
  }

 private:
  // Flag set by a waiter to indicate that it's waiting.
  static constexpr uint64_t kWaiting = 1ULL << 63;

  bool WaitCondition() const {
    return (count_.load(std::memory_order_relaxed) & kWaiting) == 0;
  }

  mutable absl::Mutex mu_;  // Mutex for waiting/signaling.
  mutable std::atomic<uint64_t> count_{
      0};  // The reference count. High bit is
           // used to indicate an active waiter.
};

namespace internal::blocking_refcount {

// Custom std::unique_ptr deleter that decrements the BlockingRefcount instead
// of deleting it.
struct DecrementingDeleter {
  void operator()(BlockingRefcount* counter) { counter->Dec(); }
};

}  // namespace internal::blocking_refcount

// Copyable RAII class for BlockingRefcount to maintain and copy references.
class ABSL_ATTRIBUTE_TRIVIAL_ABI BlockingRefcountReference {
 public:
  // Constructor.  Increments underlying counter.
  explicit BlockingRefcountReference(BlockingRefcount* counter)
      : counter_(counter) {
    if (counter_) {
      counter_->Inc();
    }
  }
  ~BlockingRefcountReference() = default;

  BlockingRefcountReference(BlockingRefcountReference&&) = default;
  BlockingRefcountReference(const BlockingRefcountReference& ref)
      : BlockingRefcountReference(ref.counter_.get()) {}

  BlockingRefcountReference& operator=(BlockingRefcountReference&&) = default;
  BlockingRefcountReference& operator=(const BlockingRefcountReference& ref) {
    return ((*this) = BlockingRefcountReference(ref));
  }

  // Swap the underlying counters (not their values).
  void swap(BlockingRefcountReference& other) noexcept {
    using std::swap;  // <link>
    swap(this->counter_, other.counter_);
  }

 private:
  std::unique_ptr<BlockingRefcount,
                  internal::blocking_refcount::DecrementingDeleter>
      counter_;
};

inline void BlockingRefcount::DecN(int64_t n) {
  DCHECK_GT(n, 0);
  auto previous = count_.fetch_sub(n, std::memory_order_acq_rel);
  // Lock the mutex only if decrementing the count results in zero and there's
  // a waiter waiting.
  if (previous == kWaiting + n) {
    absl::MutexLock l(mu_);
    // We have observed the count reaching zero while there's an active
    // waiter. Re-set the flag and notify the waiters by unlocking the mutex.
    // Since kWaiting is accessed only under lock, relaxed order suffices.
    count_.fetch_and(~kWaiting, std::memory_order_relaxed);
  }
}

}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_REFCOUNT_BLOCKING_REFCOUNT_H_
