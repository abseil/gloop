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

// A thread-safe, ordered producer-consumer queue.  The queue holds elements of
// type T, which must be suitable as an element of stl containers.  Elements in
// the queue can be created, produced, and consumed asynchronously.  Creation
// and production are separate activities -- creating an element allocates a
// space for it, while producing it fills in its value.  The caller must create
// an element before producing it.
//
// The queue guarantees that:
// - Elements will be consumed in the order in which they were created,
//   regardless of how long it takes to produce them after they were created.
// - An element will be produced before it is consumed.
//
// An optional limit may be placed on the size of the queue.
//
// The typical use of this queue is to incrementally map or do computation on
// the elements of an ordered list asynchronously but assemble the results back
// into the original order.
//

#ifndef THIRD_PARTY_GLOOP_THREAD_ORDERED_PRODUCER_CONSUMER_QUEUE_H_
#define THIRD_PARTY_GLOOP_THREAD_ORDERED_PRODUCER_CONSUMER_QUEUE_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>

#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

//
// Elements of type T pass through the queue in three stages:
// - First, create the element via Create.
// - Later, produce the element via Produce.
// - Asynchronously, consume elements via Consume.
//
// All operations are thread-safe.
//
template <class T>
class OrderedProducerConsumerQueue {
 public:
  // <capacity> is the maximum number of elements that the queue can hold at
  // once.  If it's zero, capacity is unlimited.
  explicit OrderedProducerConsumerQueue(size_t capacity);

  ~OrderedProducerConsumerQueue() { CHECK(Empty()); }

  // Create an element in the queue and copy/move <e> into it.  If the queue has
  // reached its capacity limit, wait until spare capacity is available.
  // Return the element's zero-based serial number, which should be passed to
  // Produce.
  // Note that this should never be called after StopConsumers.
  // Use TryCreate or TryCreateWithTimeout if such behavior is anticipated.
  int64_t Create(const T& e);
  int64_t Create(T&& e);

  // Create an element in the queue and copy/move <e> into it.  If the queue has
  // reached its capacity limit, wait until spare capacity is available.
  // Returns true if the element was created, with its zero-based serial number
  // returned in the <index>.
  // Returns false, without modifying the queue, if StopConsumers was called.
  bool TryCreate(const T& e, int64_t* index);
  // NOTE: For the rvalue-variant, if TryCreate() returns false, the
  // rvalue-reference is not modified.
  bool TryCreate(T&& e, int64_t* index);

  // Create an element if the queue and copy/move <e> into it. If the queue has
  // reached its capacity limit, wait up to the specified timeout for spare
  // capacity to become available. A zero <timeout_ms> means return immediately.
  // Returns true if the element was created, with its zero-based serial number
  // returned in the <index>.
  // Returns false if no element was created.  If <stopped> is set true,
  // StopConsumers has been called.  Otherwise, no space was available on
  // the queue within the timeout period.
  //
  // TODO: Change timeout_ms here and in TryConsumeWithTimeout() to
  // absl::Duration.
  bool TryCreateWithTimeout(const T& e, int64_t timeout_ms, int64_t* index,
                            bool* stopped);
  // NOTE: For the rvalue-variant, if TryCreateWithTimeout() returns false, the
  // rvalue-reference is not modified.
  bool TryCreateWithTimeout(T&& e, absl::Duration timeout, int64_t* index,
                            bool* stopped);

  // Mark the element as being produced.  Produce must be called exactly once on
  // each element created by Create, after which point it is eligible to be
  // consumed if all elements created earlier have already been consumed.
  // <index> is the element's zero-based serial number, as returned by Create.
  // Produce is atomic and cannot stall.
  void Produce(int64_t index);

  // Wait for an element to be available to be consumed.  If one is ready, pop
  // it into *e and return true.  Return false if StopConsumers has been called
  // and the queue is completely empty.  Elements are always consumed in the
  // order in which they were created.
  bool Consume(T* e);

  // Get an element if it is ready to be consumed. Returns false if
  // there is no element available, OR StopConsumers has been called
  // and the queue is completely empty.
  // The stopped boolean is true if and only if the queue is completely
  // empty and StopConsumers has been called.
  bool TryConsume(T* e, bool* stopped);

  // Get an element if it is ready to be consumed within a specified timeout.
  // Returns false if no element has been made available within the timeout OR
  // StopConsumers has been called and the queue is completely empty.
  // The stopped boolean is true if and only if the queue is completely empty
  // and StopConsumers has been called.
  // The timed_out boolean is set true if no new element has been made
  // available within the timeout period.
  bool TryConsumeWithTimeout(T* e, int64_t timeout_ms, bool* stopped,
                             bool* timed_out);

  // Terminate Consume requests once the queue drains entirely.
  // Also terminate all waiting and future TryCreate requests immediately.
  // StopConsumers cannot stall.
  void StopConsumers() {
    absl::MutexLock l(busy_);
    stop_requested_ = true;
    can_consume_.SignalAll();
    can_produce_.SignalAll();
  }

  // Return true if there are no elements that have been created but not
  // consumed.
  bool Empty() const {
    absl::MutexLock l(busy_);
    return q_.empty();
  }

  // Return the number of elements that have been created but not consumed.
  size_t Size() const {
    absl::MutexLock l(busy_);
    return q_.size();
  }

  // Returns true if the queue has been stopped using
  // StopConsumers(). This allows producers to check if that they may
  // be able to add new elements to the queue. If this function
  // returns false it's not guaranteed that TryCreate() will succeed
  // as the queue may be stopped between these two calls.
  bool Stopped() const {
    absl::MutexLock l(busy_);
    return stop_requested_;
  }

 private:
  // The bool flag is true if the element has been produced.
  typedef std::pair<T, bool> Pair;

  // Internal implementation of Create.
  template <class U>
  int64_t CreateInternal(U&& e);

  // Internal implementation of TryCreate* variants.
  template <class U>
  bool TryCreateInternal(U&& e, absl::Time deadline, int64_t* index,
                         bool* stopped);

  const size_t capacity_;  // Maximum number of elements; zero means unlimited
  mutable absl::Mutex busy_;  // Lock protecting all other variables below
  int64_t first_;             // Serial number of first element in q_
  std::deque<Pair> q_;        // Created but not consumed elements, oldest first
  absl::CondVar can_consume_;  // Signals appearances of an element to consume
  absl::CondVar can_produce_;  // Signals more room to create an element
  bool stop_requested_;        // True after StopConsumers has been called.
};

// ----------------------------------------------------------------------
// Implementations of non-inline functions

template <class T>
OrderedProducerConsumerQueue<T>::OrderedProducerConsumerQueue(size_t capacity)
    : capacity_(capacity), first_(0), stop_requested_(false) {}

template <class T>
int64_t OrderedProducerConsumerQueue<T>::Create(const T& e) {
  return CreateInternal(e);
}

template <class T>
int64_t OrderedProducerConsumerQueue<T>::Create(T&& e) {
  return CreateInternal(std::move(e));
}

template <class T>
template <class U>
int64_t OrderedProducerConsumerQueue<T>::CreateInternal(U&& e) {
  absl::MutexLock l(busy_);
  bool woken = false;
  if (capacity_) {
    while (q_.size() == capacity_) {
      can_produce_.Wait(&busy_);
      woken = true;
    }
  }
  CHECK(!stop_requested_) << "Create() is unsafe after StopConsumers() has "
                             "been called because the consumers may have "
                             "exited already. "
                             "Consider using TryCreate() instead.";
  int64_t index = first_ + q_.size();
  q_.push_back(std::make_pair(std::forward<U>(e), false));

  // Signal other threads waiting on Create if we just acquired more capacity.
  if (woken && q_.size() != capacity_) can_produce_.Signal();
  return index;
}

template <class T>
bool OrderedProducerConsumerQueue<T>::TryCreate(const T& e, int64_t* index) {
  bool stopped;
  return TryCreateInternal(e, absl::InfiniteFuture(), index, &stopped);
}

template <class T>
bool OrderedProducerConsumerQueue<T>::TryCreate(T&& e, int64_t* index) {
  bool stopped;
  return TryCreateInternal(std::move(e), absl::InfiniteFuture(), index,
                           &stopped);
}

template <class T>
bool OrderedProducerConsumerQueue<T>::TryCreateWithTimeout(const T& e,
                                                           int64_t timeout_ms,
                                                           int64_t* index,
                                                           bool* stopped) {
  CHECK_GE(timeout_ms, int64_t{0});
  return TryCreateInternal(e, absl::Now() + absl::Milliseconds(timeout_ms),
                           index, stopped);
}

template <class T>
bool OrderedProducerConsumerQueue<T>::TryCreateWithTimeout(
    T&& e, absl::Duration timeout, int64_t* index, bool* stopped) {
  CHECK_GE(timeout, absl::ZeroDuration());
  return TryCreateInternal(std::move(e), absl::Now() + timeout, index, stopped);
}

// Internal implementation of TryCreate* variants.
//   <timeout> == 0 : return immediately if no space on queue
//   <timeout> > 0 : block up to <timeout> if no space on queue
//   <timeout> == infinite : block if no space on queue
// Return true if successful, else false.
// In all cases, return false with <stopped> set true if StopConsumers
// was called before or during this call.
template <class T>
template <class U>
bool OrderedProducerConsumerQueue<T>::TryCreateInternal(U&& e,
                                                        absl::Time deadline,
                                                        int64_t* index,
                                                        bool* stopped) {
  absl::MutexLock l(busy_);
  bool woken = false;

  // Check for queue shutdown.
  if (stop_requested_) {
    *stopped = true;
    return false;
  }
  *stopped = false;

  if (capacity_) {
    while (q_.size() == capacity_) {
      // Wait with timeout until StopConsumers or capacity becomes available.
      if (can_produce_.WaitWithDeadline(&busy_, deadline)) {
        // Deadline expired.
        return false;
      }
      // Re-check for queue shutdown after waking from Wait.
      if (stop_requested_) {
        *stopped = true;
        return false;
      }
      woken = true;
    }
  }
  *index = first_ + q_.size();
  q_.push_back(std::make_pair(std::forward<U>(e), false));

  // Signal other threads waiting on Create if we just acquired more capacity.
  if (woken && q_.size() != capacity_) {
    can_produce_.Signal();
  }
  return true;
}

template <class T>
void OrderedProducerConsumerQueue<T>::Produce(int64_t index) {
  absl::MutexLock l(busy_);
  index -= first_;
  CHECK_GE(index, 0);
  CHECK_LT(index, q_.size());
  size_t index_word = static_cast<size_t>(index);
  Pair& p = q_[index_word];
  CHECK(!p.second) << "Element produced twice";
  p.second = true;

  // If we just produced the first element that the consumer is waiting for,
  // wake up one consumer.  The use of Signal instead of SignalAll here is
  // intentional; if after consuming the first element the consumer notices that
  // more are ready, it will wake up other consumers.
  if (index_word == 0) can_consume_.Signal();
}

template <class T>
bool OrderedProducerConsumerQueue<T>::Consume(T* e) {
  absl::MutexLock l(busy_);
  bool woken = false;
  while (q_.empty() || !q_.front().second) {
    if (q_.empty() && stop_requested_) {
      can_consume_.Signal();  // Wake up other concurrent calls to Consume
      return false;
    }
    can_consume_.Wait(&busy_);
    woken = true;
  }

  // Signal Create to proceed if we just created some capacity.
  if (q_.size() == capacity_) can_produce_.Signal();

  *e = std::move(q_.front().first);
  q_.pop_front();
  ++first_;

  // Handle the case where more than one thread is waiting in Consume and two or
  // more entries are produced in rapid succession.
  if (woken && !q_.empty() && q_.front().second) can_consume_.Signal();
  return true;
}

template <class T>
bool OrderedProducerConsumerQueue<T>::TryConsume(T* e, bool* stopped) {
  absl::MutexLock l(busy_);
  if (q_.empty() || !q_.front().second) {
    *stopped = (q_.empty() && stop_requested_);
    return false;
  }

  // Signal Create to proceed if we just created some capacity.
  if (q_.size() == capacity_) can_produce_.Signal();

  *e = std::move(q_.front().first);
  q_.pop_front();
  ++first_;
  *stopped = false;
  return true;
}

template <class T>
bool OrderedProducerConsumerQueue<T>::TryConsumeWithTimeout(T* e,
                                                            int64_t timeout_ms,
                                                            bool* stopped,
                                                            bool* timed_out) {
  absl::MutexLock l(busy_);
  absl::Duration left_to_wait = ::absl::Milliseconds(timeout_ms);
  absl::Time deadline = ::absl::Now() + ::absl::Milliseconds(timeout_ms);
  bool woken = false;
  while (q_.empty() || !q_.front().second) {
    if (q_.empty() && stop_requested_) {
      *stopped = true;
      *timed_out = false;
      can_consume_.Signal();  // Wake up other concurrent calls to Consume
      return false;
    }
    if (left_to_wait <= absl::ZeroDuration()) {
      *timed_out = true;
      *stopped = false;
      return false;
    }
    can_consume_.WaitWithTimeout(&busy_, left_to_wait);
    left_to_wait = deadline - ::absl::Now();
    woken = true;
  }

  // Signal Create to proceed if we just created some capacity.
  if (q_.size() == capacity_) can_produce_.Signal();

  *e = std::move(q_.front().first);
  q_.pop_front();
  ++first_;

  // Handle the case where more than one thread is waiting in Consume and two
  // or more entries are produced in rapid succession.
  if (woken && !q_.empty() && q_.front().second) can_consume_.Signal();

  *timed_out = false;
  *stopped = false;
  return true;
}

#endif  // THIRD_PARTY_GLOOP_THREAD_ORDERED_PRODUCER_CONSUMER_QUEUE_H_
