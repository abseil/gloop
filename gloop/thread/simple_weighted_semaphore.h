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

// A SimpleWeightedSemaphore is a simple mechanism for keeping a bound on the
// concurrent use of a resource, e.g. on the number of simultaneous calls to an
// asynchronous operation. In the constructor, you set a maximum amount of cost
// available, and then there are functions for requesting use of a certain
// amount of cost and relinquishing it later. This class is thread-safe; it is
// allowed (and encouraged) to use a single semaphore across multiple threads.
//
// NOTE: The implementation is effectively the same as WeightedSemaphore's. The
// difference is that WeightedSemaphore allows the exporting of statistics, and
// in a way that makes it less portable.
//
// Note that when using a semaphore as a throttle, the semaphore may
// deadlock if the asynchronous operation's callback calls Acquire. (The
// problem is that all available threads could end up calling Acquire at
// once, thus preventing any tasks that would release resources from
// running) Thus if a callback may need to start further operations which
// would use up capacity, it needs to reserve an estimate of its maximum
// capacity needed when it makes the original reservation. Partial releases
// during a callback are OK. (So long as everything is ultimately released...)
//
// Sample usage: (To throttle an asynchronous operation)
//
// static auto *write_throttle_ = new SimpleWeightedSemaphore(100);
// void WriteData(const char *data, int length) {
//   CHECK(write_throttle_->Acquire(length));
//   writer_->AsynchronousWrite(data, length,
//                              NewCallback(WriteDone, data, length));
// }
// void WriteDone(const char *data, int length, int length_written) {
//   CHECK_GE(length_written, 0);
//   CHECK_LE(length_written, length);
//   write_throttle_->Release(length_written);
//   int length_remaining = length - length_written;
//   if (length_remaining > 0) {
//     writer_->AsynchronousWrite(
//         data + length_written, length_remaining,
//         NewCallback(WriteDone, data + length_written, length_remaining));
//   }
// }
// void Flush() {
//   write_throttle_->Stop();
// }

#ifndef THIRD_PARTY_GLOOP_THREAD_SIMPLE_WEIGHTED_SEMAPHORE_H_
#define THIRD_PARTY_GLOOP_THREAD_SIMPLE_WEIGHTED_SEMAPHORE_H_

#include <cstdint>

#include "absl/base/attributes.h"
#include "absl/synchronization/mutex.h"

class WeightedSemaphore;  // Forward-declared for friending.

namespace thread {

class SimpleWeightedSemaphore {
 public:
  // Create a semaphore with a maximum cost for all concurrent operations.
  explicit SimpleWeightedSemaphore(uint64_t max_concurrent_cost);

  // The destructor brings the semaphore into a stop state before destroying
  // the object.
  ~SimpleWeightedSemaphore();

  // If stop_on_exit() is true (the default), then ~SimpleWeightedSemaphore will
  // bring the semaphore to a stop before destroying it.
  bool stop_on_exit() const { return stop_on_exit_; }
  void set_stop_on_exit(bool b) { stop_on_exit_ = b; }

  // Bring the semaphore into a stop state. Forbid all further acquires and
  // wait until all pending operations have released their resources. This
  // is typically called from a Flush() method. If two threads call Stop
  // simultaneously, both will wait until the stop has finished and then
  // return.
  // If `msec` is not null, fills it with the time in milliseconds taken
  // to stop.
  void Stop(uint64_t* msec = nullptr);

  // Bring a stopped semaphore into a start state, allowing acquires to
  // happen. A newly constructed semaphore is started by default. If the
  // semaphore is in the middle of a pending Stop(), this waits for that
  // to finish first.
  void Start();

  // Try to acquire use of so much cost; blocks until that much cost is
  // available. If acquires are no longer possible, because the semaphore
  // is in a stop state, it returns false and does not acquire the resource.
  // Callers should handle that case appropriately.
  bool Acquire(uint64_t cost) ABSL_MUST_USE_RESULT {
    return Acquire(cost, nullptr);
  }

  // Just like Acquire, but returns the number of milliseconds spent
  // blocking.
  bool Acquire(uint64_t cost, uint64_t* msec) ABSL_MUST_USE_RESULT;

  // Try to acquire use of so much cost, but don't block if it isn't
  // immediately available or if the semaphore is stopped. Returns true
  // if the resource was successfully acquired.
  bool TryAcquire(uint64_t cost);

  // Just like Acquire, except will block (rather than return false) even if
  // the semaphore is in the stopped state.  This is only useful if the caller
  // knows that the semaphore will be restarted again at some point in the
  // future.
  void AcquireAlways(uint64_t cost) { return AcquireAlways(cost, nullptr); }

  // Just like AcquireAlways, but returns the number of milliseconds spent
  // blocking.  Note: blocking time spent waiting for the semaphore to
  // start is not counted.
  void AcquireAlways(uint64_t cost, uint64_t* msec);

  // Relinquish the use of the given cost.
  void Release(uint64_t cost);

  // Returns the total cost of all currently pending operations. (Mostly for
  // debugging and testing)
  uint64_t pending_cost() const {
    absl::MutexLock l(pending_operations_lock_);
    return pending_cost_;
  }

  // Returns the total amount of cost available.
  uint64_t max_cost() const {
    absl::MutexLock l(pending_operations_lock_);
    return max_cost_;
  }

  // Change the current value of max_concurrent_cost. If this increases it,
  // that will take effect immediately; if it decreases it, current resource
  // holders can keep their resources, but further acquires are going to
  // block until the total amount of resource held is less than max_cost.
  void set_max_cost(uint64_t max_cost);

  // Tells whether the system is currently in a stopped state.
  bool stopped() const {
    absl::MutexLock l(pending_operations_lock_);
    return stopped_;
  }

 private:
  // For use *ONLY* by WeightedSemaphore:
  friend class ::WeightedSemaphore;
  absl::Mutex* pending_operations_lock() const {
    return &pending_operations_lock_;
  }
  const uint64_t* pending_cost_pointer() const { return &pending_cost_; }
  const uint64_t* max_cost_pointer() const { return &max_cost_; }

  // Returns true iff we can acquire cost, based on the current values of
  // pending_cost_ and max_cost_.
  // REQUIRES: We already hold pending_operations_lock_.
  bool CanAcquire(uint64_t cost) const;

  // This lock protects the following variables:
  mutable absl::Mutex pending_operations_lock_;
  uint64_t pending_cost_ = 0;  // The total cost in use at the moment
  uint64_t max_cost_;          // The total cost available
  // acquire_changed_ signals whenever an event happens that may change a
  // waiting acquire: either new resources have become available or the
  // semaphore has stopped.
  absl::CondVar acquire_changed_;
  // no_pending_ops_ signals whenever a stop is in progress and the total cost
  // of pending operations reaches zero.
  absl::CondVar no_pending_ops_;
  // stop_finished_ signals when a pending stop operation completes.
  absl::CondVar stop_finished_;
  // started_ signals whenever a Start() completes.
  absl::CondVar started_;
  bool stopped_ = false;      // True if the semaphore is not accepting requests
  bool stopping_ = false;     // True in the middle of a run->stop transition
  bool stop_on_exit_ = true;  // True if the d'tor should Stop().
};

// An RAII class to support acquiring and automatically releasing resources from
// an `SimpleWeightedSemaphore`. This lock requests resources via
// `AcquireAlways`.
class [[nodiscard]] SimpleWeightedSemaphoreLock {
 public:
  // Acquire and manage a `cost` of resources from `semaphore`.
  explicit SimpleWeightedSemaphoreLock(SimpleWeightedSemaphore* semaphore,
                                       uint64_t cost);

  ~SimpleWeightedSemaphoreLock();

  // This class is not copyable nor movable.
  SimpleWeightedSemaphoreLock(const SimpleWeightedSemaphoreLock&) = delete;
  SimpleWeightedSemaphoreLock(SimpleWeightedSemaphoreLock&& other) = delete;
  SimpleWeightedSemaphoreLock& operator=(const SimpleWeightedSemaphoreLock&) =
      delete;
  SimpleWeightedSemaphoreLock& operator=(SimpleWeightedSemaphoreLock&& other) =
      delete;

 private:
  SimpleWeightedSemaphore* const semaphore_;
  const uint64_t cost_;
};

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_SIMPLE_WEIGHTED_SEMAPHORE_H_
