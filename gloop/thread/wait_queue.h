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

// A thread-safe version of the STL queue, implementing a fifo or lifo queue.
//
// WaitQueue supports locking and waiting for data to appear.
//
//
// There are many design patterns that can be used. A basic one is:
//
// - Create a WaitQueue q of elements of type T and start producer and/or
//   consumer threads. The main thread may also serve as a producer.
//
// - Each producer calls q.push and/or q.push_front whenever it has data
//   available.  q.push gives you FIFO queue semantics, while q.push_front gives
//   you stack semantics.
//
// - Each consumer sits in a loop such as:
//
//   T elt;
//   while (q.Wait(&elt)) {
//     ... consume elt ...
//   }
//   // Optionally notify that work is done
//
// - After the last element has been produced and pushed, call q.StopWaiters.
//   This will make each consumer exit its loop after all elements currently in
//   the queue have been consumed.  Do not push more elements after calling
//   q.StopWaiters because there may be no one left to consume them.
//
// - Depending on the application, it may be important to block the main thread
//   until all elements have been consumed.  Don't query the size of the queue
//   for that, because that won't tell you if any of the consumers are still
//   working on the last element. Instead, either join the consumer threads or
//   have them use a BlockingCounter or Notification to notify the main thread
//   once they reach the point labeled "optionally notify that work is done"
//   above.
//
// - If you have only one consumer thread, the simplest way to do periodic
//   checkpointing is to occasionally push dummy "checkpoint now" elements onto
//   the queue.  The consumer, upon retrieving a "checkpoint now" element does
//   whatever it needs to do to flush data and write the checkpoint and then
//   uses a Notification to tell the main thread that it wrote the checkpoint.
//   The consumer then goes back to consuming more elements.  Upon receiving the
//   notification, the main thread can be sure that all elements pushed prior to
//   the "checkpoint now" element have been safely consumed (assuming that
//   you're using FIFO queue semantics and haven't rearranged elements via a
//   PrioritizedWaitQueue).

#ifndef THIRD_PARTY_GLOOP_THREAD_WAIT_QUEUE_H_
#define THIRD_PARTY_GLOOP_THREAD_WAIT_QUEUE_H_

#include <deque>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

//
// WaitQueue supports both locking and waiting for new elements to appear.
// All operations are atomic.
//
template <class T>
class WaitQueue {
 public:
  typedef std::deque<T> container_type;

  typedef typename container_type::value_type value_type;
  typedef typename container_type::size_type size_type;

  WaitQueue() : max_queue_size_(kInfiniteQueueSize), stop_requested_(false) {}

  bool empty() const {
    absl::ReaderMutexLock l(busy_);
    return q_.empty();
  }
  size_type size() const {
    absl::ReaderMutexLock l(busy_);
    return q_.size();
  }

  // Call to set a limited queue size. If this size is reached, calls
  // to push and push_front will block until elements have been popped
  // from the queue. Call with kInfiniteQueueSize (the default) to
  // make push and push_front non-blocking. This can be called while
  // the queue is active. If it is called with a size less than the
  // current queue size, future inserts will block until the queue
  // sizes goes below the new maximum.

  // size_type is always unsigned, so this is effectively infinite
  static constexpr size_type kInfiniteQueueSize = static_cast<size_type>(-1);
  void set_max_queue_size(size_type max_queue_size) {
    absl::MutexLock l(busy_);
    max_queue_size_ = max_queue_size;
  }

  size_type max_queue_size() const {
    absl::MutexLock l(busy_);
    return max_queue_size_;
  }

  // Push x onto the back of the queue.  The last item pushed this way
  // will be the last one to be popped.  If a waiter was waiting for
  // an element to appear, wake it up.  Will block if max_queue_size_
  // has been reached.
  void push(const value_type& x) { push_internal(x); }
  void push(value_type&& x) { push_internal(std::move(x)); }

  // Same as push() but returns false if max_queue_size_ is reached instead of
  // blocking.  Returns true if x is pushed onto the queue.
  bool push_nowait(const value_type& x) { return push_nowait_internal(x); }
  bool push_nowait(value_type&& x) {
    return push_nowait_internal(std::move(x));
  }

  // Push x onto the front of the queue.  The last item pushed this way
  // will be the first one to be popped.  If a waiter was waiting for an
  // element to appear, wake it up.  Will block if max_queue_size_
  // has been reached.
  void push_front(const value_type& x) { push_front_internal(x); }
  void push_front(value_type&& x) { push_front_internal(std::move(x)); }

  // Same as push_front() but returns false if max_queue_size_ is reached
  // instead of blocking.  Returns true if x is pushed onto the queue.
  bool push_front_nowait(const value_type& x) {
    return push_front_nowait_internal(x);
  }
  bool push_front_nowait(value_type&& x) {
    return push_front_nowait_internal(std::move(x));
  }

  // Atomically pop the front element into *p.  If it was present, return
  // true; if the queue was empty, leave *p unchanged and return false.
  bool Pop(value_type* p);

  // Discard the front element if present. Returns true if an element was
  // discarded, false if nothing was discarded since the queue was empty.
  bool Drop();

  // Atomically set *p to the front element.  If there was a front element,
  // return true; if the queue was empty, leave *p unchanged and return false.
  bool Front(value_type* p) const;

  // Atomically swap the container in the queue with the user provided
  // container, which should be empty. A common use case for this is to
  // send out everything in the queue in one operation.
  void SwapEmptyContainer(container_type* container);

  // Atomically pop all elements from the queue, in order (oldest first). If the
  // queue was empty, returns an empty container.
  container_type PopAll();

  // Wait for a front element to appear.  If an element is ready, pop it into *p
  // and return true.  If the queue is currently empty, wait for an element to
  // appear, and then pop it into *p and return true.  Return false if the queue
  // is empty and StopWaiters() was called during or prior to waiting, in which
  // case *p is unchanged.
  bool Wait(value_type* p);

  // Wait for a front element to appear, but with a deadline:
  //
  // * If an element is ready, pop it into *p, set *timed_out to false, and
  //   return true.
  //
  // * Otherwise (if the queue is currently empty), wait for an element to
  //   appear until the given deadline, or until StopWaiters has been
  //   called:
  //   - If an element appears, pop it into *p, set *timed_out to false, and
  //     return true.
  //   - If StopWaiters has been called and no element has arrived, set
  //     *timed_out to false and return false; in this case *p is unchanged.
  //   - Otherwise (if the deadline has passed without either of the above two
  //     events happening), set *timed_out to true and return true; in this
  //     case *p is also unchanged.
  //
  // In summary: the return value indicates whether waiting was interrupted due
  // to a call of StopWaiters, and *timed_out indicates whether waiting was
  // interrupted due to exceeding the requested deadline. A value is only
  // written to *p if *timeout is false and the return value is true.
  bool WaitWithDeadline(value_type* p, absl::Time deadline, bool* timed_out);

  // Wait for a front element to appear, but with a timeout.
  // Equivalent to WaitWithDeadline(..., absl::Now() + timeout, ...);
  bool WaitWithTimeout(value_type* p, absl::Duration timeout, bool* timed_out);

  // DEPRECATED: Use the overload that accepts an absl::Duration instead.
  bool WaitWithTimeout(value_type* p, int milliseconds, bool* timed_out) {
    return WaitWithTimeout(p, absl::Milliseconds(milliseconds), timed_out);
  }

  // Terminate existing and future *blocking* Wait() requests.  Calls to Wait()
  // when the queue is non-empty are non-blocking, and so those are *not*
  // terminated.
  //
  // WARNING: Calling this method while there are blocked pushers (or if a
  // pusher attempts to push after this) will cause the process to CHECK-fail
  // and crash. Ensure all pushers have finished pushing and joined before
  // calling StopWaiters() for graceful shutdown.
  void StopWaiters() {
    absl::MutexLock l(busy_);
    stop_requested_ = true;
  }

  // Atomically copy the current contents of the queue into a separate
  // container.
  //
  // IMPORTANT: For this method to be safe, item_type must be a type with value
  // semantics. Queues containing raw pointers must not be copied, because the
  // item might be popped and deleted before the copy is inspected. It is safe
  // to use shared_ptr<> items, but be aware that the items might be available
  // to two threads at once.
  void CopyTo(container_type* container) const;

 protected:
  mutable absl::Mutex busy_;
  container_type q_ ABSL_GUARDED_BY(busy_);
  size_type max_queue_size_ ABSL_GUARDED_BY(busy_);
  // True after StopWaiters() has been called.
  bool stop_requested_ ABSL_GUARDED_BY(busy_);

 private:
  // Wake-up conditions.
  bool ReadyToPopOrStop() const ABSL_SHARED_LOCKS_REQUIRED(busy_) {
    return !q_.empty() || stop_requested_;
  }
  bool ReadyToPush() const ABSL_SHARED_LOCKS_REQUIRED(busy_) {
    return q_.size() < max_queue_size_ || stop_requested_;
  }

  template <typename U>
  void push_internal(U&& x) ABSL_LOCKS_EXCLUDED(busy_) {
    absl::MutexLock l(busy_, absl::Condition(this, &WaitQueue::ReadyToPush));
    CHECK(!stop_requested_) << "push called or was blocked after StopWaiters()";
    q_.push_back(std::forward<U>(x));
  }

  template <typename U>
  bool push_nowait_internal(U&& x) ABSL_LOCKS_EXCLUDED(busy_) {
    absl::MutexLock l(busy_);
    CHECK(!stop_requested_) << "push_nowait called after StopWaiters()";
    if (ReadyToPush()) {
      q_.push_back(std::forward<U>(x));
      return true;
    } else {
      return false;
    }
  }

  template <typename U>
  void push_front_internal(U&& x) ABSL_LOCKS_EXCLUDED(busy_) {
    absl::MutexLock l(busy_, absl::Condition(this, &WaitQueue::ReadyToPush));
    CHECK(!stop_requested_)
        << "push_front called or was blocked after StopWaiters()";
    q_.push_front(std::forward<U>(x));
  }

  template <typename U>
  bool push_front_nowait_internal(U&& x) ABSL_LOCKS_EXCLUDED(busy_) {
    absl::MutexLock l(busy_);
    CHECK(!stop_requested_) << "push_front_nowait called after StopWaiters()";
    if (ReadyToPush()) {
      q_.push_front(std::forward<U>(x));
      return true;
    } else {
      return false;
    }
  }
};

// ----------------------------------------------------------------------------
// Implementations of non-inline functions

template <class T>
bool WaitQueue<T>::Pop(value_type* p) {
  absl::MutexLock l(busy_);
  if (q_.empty()) return false;
  *p = std::move(q_.front());
  q_.pop_front();
  return true;
}

template <class T>
bool WaitQueue<T>::Drop() {
  absl::MutexLock l(busy_);
  if (q_.empty()) return false;
  q_.pop_front();
  return true;
}

template <class T>
bool WaitQueue<T>::Front(value_type* p) const {
  absl::ReaderMutexLock l(busy_);
  if (q_.empty()) return false;
  *p = q_.front();
  return true;
}

template <class T>
void WaitQueue<T>::SwapEmptyContainer(container_type* container) {
  DCHECK(container->empty());
  absl::MutexLock l(busy_);
  q_.swap(*container);
}

template <class T>
typename WaitQueue<T>::container_type WaitQueue<T>::PopAll() {
  container_type to_return;
  SwapEmptyContainer(&to_return);
  return to_return;
}

template <class T>
bool WaitQueue<T>::Wait(value_type* p) {
  absl::MutexLock l(busy_, absl::Condition(this, &WaitQueue::ReadyToPopOrStop));

  // At this point, either the queue is non-empty _or_ StopWaiters has been
  // called. It is critical that we check the queue size first, since the API
  // contract says that we will pop an element from the queue if possible
  // _before_ we consider stopping. In other words, stopping is only relevant if
  // the queue is empty.
  //
  // The same consideration applies in WaitWithDeadline and WaitWithTimeout
  // below.

  if (!q_.empty()) {
    *p = std::move(q_.front());
    q_.pop_front();
    return true;
  } else {
    return false;
  }
}

template <class T>
bool WaitQueue<T>::WaitWithDeadline(value_type* p, absl::Time deadline,
                                    bool* timed_out) {
  if (busy_.LockWhenWithDeadline(
          absl::Condition(this, &WaitQueue::ReadyToPopOrStop), deadline)) {
    // See similar comment in Wait above.
    if (!q_.empty()) {
      *p = std::move(q_.front());
      q_.pop_front();
      busy_.unlock();
      *timed_out = false;
      return true;
    } else {
      busy_.unlock();
      *timed_out = false;
      return false;
    }
  } else {
    busy_.unlock();
    *timed_out = true;
    return true;
  }
}

template <class T>
bool WaitQueue<T>::WaitWithTimeout(value_type* p, absl::Duration timeout,
                                   bool* timed_out) {
  return WaitWithDeadline(p, absl::Now() + timeout, timed_out);
}

template <class T>
void WaitQueue<T>::CopyTo(container_type* container) const {
  absl::MutexLock l(busy_);
  *container = q_;
}

#endif  // THIRD_PARTY_GLOOP_THREAD_WAIT_QUEUE_H_
