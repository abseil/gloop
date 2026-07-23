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

// A thread-safe priority queue.
//
// PrioritizedWaitQueue supports:
// - locking and waiting for data to appear
// - sorted inserts via the Compare template argument

#ifndef THIRD_PARTY_GLOOP_THREAD_PRIORITIZED_WAIT_QUEUE_H_
#define THIRD_PARTY_GLOOP_THREAD_PRIORITIZED_WAIT_QUEUE_H_

#include <algorithm>
#include <deque>
#include <functional>
#include <queue>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

//
// PrioritizedWaitQueue supports both locking and waiting for new elements to
// appear.  All operations are thread-safe.  The interface is identical to
// WaitQueue<> with the exception that push_front() does not exist.
//
// The underlying data structure is std::priority_queue<T, std::deque<T>,
// Compare> (which is a max-heap), so that calls to Pop and Wait will choose the
// greatest available element according to Compare, where Compare is considered
// to define a "less-than" relationship.  In other words, Compare(l, r) shall
// return true if 'r' is to be popped before 'l'.
//
template <class T, class Compare = std::less<T>>
class PrioritizedWaitQueue {
 public:
  struct container_type : std::priority_queue<T, std::deque<T>, Compare> {
    using std::priority_queue<T, std::deque<T>, Compare>::priority_queue;
    void pop_top(T* dst) {
      std::pop_heap(this->c.begin(), this->c.end(), this->comp);
      *dst = std::move(this->c.back());
      this->c.pop_back();
    }

    // std::priority_queue has a push_range method, but that uses std::ranges
    // which is forbidden in google3. Instead, expose an iterator-based
    // overload of push which allows multiple items to be inserted into the
    // queue before re-sorting the container once.
    template <typename Iterator>
    void push_many(Iterator first, Iterator last) {
      this->c.insert(this->c.end(), first, last);
      std::make_heap(this->c.begin(), this->c.end(), this->comp);
    }
  };

  using value_type = typename container_type::value_type;
  using size_type = typename container_type::size_type;

  PrioritizedWaitQueue()
      : max_queue_size_(kInfiniteQueueSize), stop_requested_(false) {}

  explicit PrioritizedWaitQueue(const Compare& compare)
      : q_(compare),
        max_queue_size_(kInfiniteQueueSize),
        stop_requested_(false) {}

  bool empty() const {
    absl::ReaderMutexLock l(busy_);
    return q_.empty();
  }
  size_type size() const {
    absl::ReaderMutexLock l(busy_);
    return q_.size();
  }

  // size_type is always unsigned, so this is effectively infinite
  static constexpr size_type kInfiniteQueueSize = static_cast<size_type>(-1);

  // Call to set a limited queue size. If this size is reached, calls to push
  // will block until elements have been popped from the queue. Call with
  // kInfiniteQueueSize (the default) to make push non-blocking. This can be
  // called while the queue is active. If it is called with a size less than the
  // current queue size, future inserts will block until the queue sizes goes
  // below the new maximum.
  void set_max_queue_size(size_type max_queue_size) {
    absl::MutexLock l(busy_);
    max_queue_size_ = max_queue_size;
    unfull_.SignalAll();
  }

  // Push x onto the priority queue. If a waiter was waiting for an element to
  // appear, wake it up.  Will block if max_queue_size_ has been reached.
  void push(const value_type& x) { push_internal(x); }
  void push(value_type&& x) { push_internal(std::move(x)); }

  // Push many values at once onto the priority queue. This is more efficient
  // than calling push in a loop because the underlying container will only need
  // to be reordered once.
  //
  // For bounded queues, the range will be inserted in chunks as capacity
  // becomes available in the queue.
  template <typename Iterator>
  void push_many(Iterator first, Iterator last) {
    absl::MutexLock l(busy_);

    while (first != last) {
      while (q_.size() >= max_queue_size_ && !stop_requested_)
        unfull_.Wait(&busy_);

      if (stop_requested_) {
        if (q_.empty()) ready_.Signal();
        q_.push_many(first, last);
        break;
      }

      const size_type available = max_queue_size_ - q_.size();
      const size_type remaining = std::distance(first, last);

      auto chunk_end = first;
      std::advance(chunk_end, std::min(available, remaining));

      if (q_.empty()) ready_.Signal();
      q_.push_many(first, chunk_end);
      first = chunk_end;
    }
  }

  // Pop the greatest element into *p.  If it was present, return
  // true; if the queue was empty, leave *p unchanged and return false.
  bool Pop(value_type* p);

  // Set *p to the greatest element.  If there was an element, return
  // true; if the queue was empty, leave *p unchanged and return false.
  // This function requires value_type to be copyable.
  bool Front(value_type* p) const;

  // Swap the container in the queue with the user provided
  // container, which should be empty. A common use case for this is to
  // send out everything in the queue in one operation.
  void SwapEmptyContainer(container_type* container);

  // Wait for an element to appear.  If an element is ready, pop it into *p and
  // return true.  If the queue is currently empty, wait for an element to
  // appear, and then pop it into *p and return true.  Return false if the queue
  // is empty and StopWaiters() was called during or prior to waiting, in which
  // case *p is unchanged.
  bool Wait(value_type* p);

  // Wait for an element to appear.  If an element is ready, pop it into *p,
  // clear *timed_out, and return true.  If the queue is currently empty, wait
  // for an element to appear and then pop it into *p, clear *timed_out, and
  // return true.  If the given timeout elapses without an element appearing,
  // set *timed_out and return true.  Return false if the queue is empty and
  // StopWaiters() was called during or prior to waiting, in which case *p is
  // unchanged.
  bool WaitWithTimeout(value_type* p, absl::Duration timeout, bool* timed_out);

  // DEPRECATED: Use the overload that accepts an absl::Duration instead.
  bool WaitWithTimeout(value_type* p, int milliseconds, bool* timed_out) {
    return WaitWithTimeout(p, absl::Milliseconds(milliseconds), timed_out);
  }

  // Terminate existing and future *blocking* Wait() requests.  Calls to Wait()
  // when the queue is non-empty are non-blocking, and so those are *not*
  // terminated.
  void StopWaiters() {
    absl::MutexLock l(busy_);
    stop_requested_ = true;
    ready_.SignalAll();
    unfull_.SignalAll();
  }

  // Copy the current contents of the queue into a separate
  // container.
  //
  // IMPORTANT: For this method to be thread-safe, item_type must be a type with
  // value semantics. Queues containing raw pointers must not be copied, because
  // the item might be popped and deleted before the copy is inspected. It is
  // safe to use shared_ptr<> items, but be aware that the items might be
  // available to two threads at once.
  void CopyTo(container_type* container) const;

 protected:
  mutable absl::Mutex busy_;
  container_type q_ ABSL_GUARDED_BY(busy_);
  // This condition variable is used to signal transitions from an empty to
  // a non-empty queue.
  absl::CondVar ready_;
  size_type max_queue_size_ ABSL_GUARDED_BY(busy_);

  // This condition is signaled whenever an element is removed from
  // the queue. Inserts block on this when (q_.size() >= max_queue_size_).
  absl::CondVar unfull_;
  // True after StopWaiters() has been called.
  bool stop_requested_ ABSL_GUARDED_BY(busy_);

 private:
  template <typename U>
  void push_internal(U&& x) ABSL_LOCKS_EXCLUDED(busy_) {
    absl::MutexLock l(busy_);
    while (q_.size() >= max_queue_size_ && !stop_requested_)
      unfull_.Wait(&busy_);
    if (q_.empty()) ready_.Signal();
    q_.push(std::forward<U>(x));
  }
};

// ----------------------------------------------------------------------------
// Implementations of non-inline functions

template <class T, class Compare>
bool PrioritizedWaitQueue<T, Compare>::Pop(value_type* p) {
  absl::MutexLock l(busy_);
  if (q_.empty()) return false;
  q_.pop_top(p);
  unfull_.Signal();
  return true;
}

template <class T, class Compare>
bool PrioritizedWaitQueue<T, Compare>::Front(value_type* p) const {
  absl::ReaderMutexLock l(busy_);
  if (q_.empty()) return false;
  *p = q_.top();
  return true;
}

template <class T, class Compare>
void PrioritizedWaitQueue<T, Compare>::SwapEmptyContainer(
    container_type* container) {
  DCHECK(container->empty());
  absl::MutexLock l(busy_);
  q_.swap(*container);
  // In Pop(), each time we send a signal, a pending push() may be unblocked.
  // When push() is finished, the queue is guaranteed to be full again; the
  // next Pop() will unblock any remaining pending push(). So, calling Signal()
  // is sufficient there.
  // This is not the case for SwapEmptyContainer, because multiple elements can
  // be removed from the queue; we need to signal all the pending push().
  unfull_.SignalAll();
}

template <class T, class Compare>
bool PrioritizedWaitQueue<T, Compare>::Wait(value_type* p) {
  absl::MutexLock l(busy_);
  bool woken = false;
  while (q_.empty()) {
    if (stop_requested_) return false;
    ready_.Wait(&busy_);
    woken = true;
  }
  q_.pop_top(p);
  // Handle the case where more than one thread is waiting for a new entry
  // and two or more entries appear in rapid succession.
  if (woken && !q_.empty()) ready_.Signal();
  unfull_.Signal();
  return true;
}

template <class T, class Compare>
bool PrioritizedWaitQueue<T, Compare>::WaitWithTimeout(value_type* p,
                                                       absl::Duration timeout,
                                                       bool* timed_out) {
  *timed_out = false;
  absl::Time deadline = absl::Now() + timeout;

  absl::MutexLock l(busy_);
  bool woken = false;
  while (q_.empty()) {
    if (stop_requested_) return false;
    if (deadline <= absl::Now()) {
      *timed_out = true;
      return true;
    }
    ready_.WaitWithDeadline(&busy_, deadline);
    woken = true;
  }
  q_.pop_top(p);
  // Handle the case where more than one thread is waiting for a new entry
  // and two or more entries appear in rapid succession.
  if (woken && !q_.empty()) ready_.Signal();
  unfull_.Signal();
  return true;
}

template <class T, class Compare>
void PrioritizedWaitQueue<T, Compare>::CopyTo(container_type* container) const {
  absl::MutexLock l(busy_);
  *container = q_;
}

#endif  // THIRD_PARTY_GLOOP_THREAD_PRIORITIZED_WAIT_QUEUE_H_
