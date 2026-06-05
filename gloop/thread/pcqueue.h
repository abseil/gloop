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

#ifndef THIRD_PARTY_GLOOP_THREAD_PCQUEUE_H_
#define THIRD_PARTY_GLOOP_THREAD_PCQUEUE_H_

#include <atomic>
#include <cstdint>
#include <deque>
#include <limits>

#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

template <typename T>
class ThreadWorkerQueue {
 public:
  virtual ~ThreadWorkerQueue() {}

  virtual void Put(T elem) = 0;
  virtual void ForcePut(T elem) = 0;
  virtual bool TryPut(T elem) = 0;
  virtual bool PutIfReadyToRun(T elem) = 0;
  virtual T Get() = 0;
  virtual int64_t count() const = 0;
  virtual int64_t capacity() const = 0;
};

namespace thread {

// A (thread-safe) producer-consumer FIFO queue.
// See <link>++-concurrency#message_passing
//
// Prefer to use thread::Channel in new code, as it separates readers
// from writers.
template <typename T>
class ProducerConsumerQueue : public ThreadWorkerQueue<T> {
 public:
  constexpr static int64_t kUnbounded = std::numeric_limits<int64_t>::max();
  // Creates a producer-consumer queue that has room for "capacity"
  // entries.  kUnbounded is a special case, producing a (slightly more
  // efficient) queue that is never considered to be full.
  // REQUIRES: capacity > 0
  explicit ProducerConsumerQueue(int64_t capacity);

  // This type is neither copyable nor movable.
  ProducerConsumerQueue(const ProducerConsumerQueue&) = delete;
  ProducerConsumerQueue& operator=(const ProducerConsumerQueue&) = delete;

  // Releases resources for queue.  The queue must be empty and
  // must not have any waiters.
  ~ProducerConsumerQueue() override;

  // Adds "elem" to the queue.  Causes the current thread
  // to wait for consumers if the queue is full.
  void Put(T elem) override;

  // Adds "elem" to the queue even if the Queue is full so use carefully.
  // This operation never blocks.
  void ForcePut(T elem) override;

  // If the queue is not full, adds "elem" to the queue and returns true.
  // If the queue is full, returns false and has no side-effects.
  bool TryPut(T elem) override;

  // If the queue has more callers to Get than it has elements
  // in the queue, a new object can be used immediately.
  // In that case, this method returns true and adds the
  // element.  Otherwise, nothing is added and this method
  // returns false.  Note that it is not sufficient to check
  // the length of the queue as the callers blocking in Get
  // may not have woken up for new objects on the queue yet.
  bool PutIfReadyToRun(T elem) override;

  // Removes the oldest element from the queue and returns it.
  // Causes the current thread to wait for producers if the queue is empty.
  T Get() override;

  // Waits up to "ms" milliseconds for an element.  If an element is found,
  // removes it from the queue, stores it in *result and returns true.
  // Else (when timeout expires) returns false.
  //
  // Note: Casting a pointer of another type to void ** results in a violation
  // of C++'s "strict aliasing" rules. Instead, pass a pointer to a void * to
  // this function and then use static_cast to convert back to the actual type.
  bool GetWithTimeout(T* result, int64_t ms);

  // If the queue is not empty, removes the oldest element from the queue,
  // stores it in *result and returns true. If the queue is empty, returns false
  // and has no side-effects.
  //
  // Note: Casting a pointer of another type to void ** results in a violation
  // of C++'s "strict aliasing" rules. Instead, pass a pointer to a void * to
  // this function and then use static_cast to convert back to the actual type.
  bool TryGet(T* result);

  // Number of elements in queue.  The returned count may not be valid
  // for very long since other threads may be concurrently
  // adding/removing elements to/from the queue.  So use the return
  // value as just a hint about the size of the queue.
  int64_t count() const override ABSL_NO_THREAD_SAFETY_ANALYSIS {
    return count_.load(std::memory_order_relaxed);
  }

  // Maximum number of elements in the work queue.
  int64_t capacity() const override { return capacity_; }

 private:
  void InternalPut(T elem) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  typedef std::deque<T> Queue;

  // To reduce cache misses, we use a doubly-linked list of Waiter structs and
  // queue them in LIFO order rather than the FIFO order used by a single
  // condition variable.
  struct Waiter {      // all fields under mutex_
    absl::CondVar cv;  // signalled when there is work to do
    Waiter* next;      // double-linking for waiters_
    Waiter* prev;
  };

  void RemoveWaiter(Waiter* waiter) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  Waiter* TopWaiter() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void PushWaiter(Waiter* waiter) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  absl::Mutex mutex_;                        // The protecting lock
  const int64_t capacity_;                   // Capacity of "queue"
  int num_waiters_ ABSL_GUARDED_BY(mutex_);  // the number of waiters
  Waiter waiters_
      ABSL_GUARDED_BY(mutex_);  // doubly-linked LIFO of threads waiting
  absl::CondVar wait_nonfull_
      ABSL_GUARDED_BY(mutex_);  // To wait until non-full
  Queue queue_;                 // Queue of elements
  std::atomic<int64_t> count_
      ABSL_GUARDED_BY(mutex_);  // Number of elements in queue

  T RemoveElement() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Returns true iff the queue was constructed to have a limited capacity
  bool IsLimitedCapacity() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    return capacity_ != kUnbounded;
  }
};

template <typename T>
typename ProducerConsumerQueue<T>::Waiter*
ProducerConsumerQueue<T>::TopWaiter() {
  return waiters_.next;
}

template <typename T>
void ProducerConsumerQueue<T>::RemoveWaiter(Waiter* waiter) {
  DCHECK_NE(waiter, &waiters_);
  waiter->next->prev = waiter->prev;
  waiter->prev->next = waiter->next;
  --num_waiters_;
}

template <typename T>
void ProducerConsumerQueue<T>::PushWaiter(Waiter* waiter) {
  waiter->prev = &waiters_;
  waiter->next = waiters_.next;
  waiter->next->prev = waiter;
  waiter->prev->next = waiter;
  ++num_waiters_;
}

template <typename T>
void ProducerConsumerQueue<T>::InternalPut(T elem) {
  queue_.push_back(std::move(elem));
  count_.store(count_.load(std::memory_order_relaxed) + 1,
               std::memory_order_relaxed);
  // TopWaiter() could be the dummy node, in which case we will not wake up
  // anyone. If there are waiters we wake the last inserted waiter. Note that
  // we can signal this waiter multiple times. This is not only ok but it is
  // crucial to reduce spurious wakeups.
  TopWaiter()->cv.Signal();
}

// Internal routine: called while lock is held and the front element of the
// queue should be removed.  Removes the element, signals any Put waiters if
// necessary, wakes up more waiters if necessary, and returns the element.
template <typename T>
T ProducerConsumerQueue<T>::RemoveElement() {
  T result = std::move(queue_.front());
  queue_.pop_front();
  count_.store(count_.load(std::memory_order_relaxed) - 1,
               std::memory_order_relaxed);

  // Be careful: have to signal every time we remove an element,
  // or do something more complicated with broadcasts.
  if (IsLimitedCapacity()) {
    wait_nonfull_.Signal();
  }

  if (count_.load(std::memory_order_relaxed) > 0) {
    TopWaiter()->cv.Signal();
  }

  return result;
}

template <typename T>
ProducerConsumerQueue<T>::ProducerConsumerQueue(int64_t capacity)
    : capacity_(capacity) {
  CHECK_GT(capacity, 0);
  waiters_.next = &waiters_;
  waiters_.prev = &waiters_;
  num_waiters_ = 0;
  count_.store(0, std::memory_order_relaxed);
}

template <typename T>
ProducerConsumerQueue<T>::~ProducerConsumerQueue() {}

template <typename T>
void ProducerConsumerQueue<T>::Put(T elem) {
  // Wait for queue to be not-full
  absl::MutexLock m(mutex_);

  if (IsLimitedCapacity()) {
    while (count_.load(std::memory_order_relaxed) >= capacity_) {
      wait_nonfull_.Wait(&mutex_);
    }
  }
  InternalPut(std::move(elem));
}

template <typename T>
void ProducerConsumerQueue<T>::ForcePut(T elem) {
  // Ignore queue full and add rightaway
  absl::MutexLock m(mutex_);
  InternalPut(std::move(elem));
}

template <typename T>
bool ProducerConsumerQueue<T>::TryPut(T elem) {
  absl::MutexLock m(mutex_);

  // Check if the queue is full
  if (count_.load(std::memory_order_relaxed) >= capacity_) {
    return false;
  } else {
    InternalPut(std::move(elem));
    return true;
  }
}

template <typename T>
bool ProducerConsumerQueue<T>::PutIfReadyToRun(T elem) {
  absl::MutexLock m(mutex_);

  if (num_waiters_ <= count_.load(std::memory_order_relaxed) ||
      count_.load(std::memory_order_relaxed) >= capacity_)
    return false;

  InternalPut(std::move(elem));

  return true;
}

template <typename T>
T ProducerConsumerQueue<T>::Get() {
  // Wait for queue to be not-empty
  absl::MutexLock m(mutex_);
  if (count_.load(std::memory_order_relaxed) == 0) {
    Waiter self;
    PushWaiter(&self);
    do {
      self.cv.Wait(&mutex_);
    } while (count_.load(std::memory_order_relaxed) == 0);
    RemoveWaiter(&self);
  }
  DCHECK_GT(count_.load(std::memory_order_relaxed), 0);
  return RemoveElement();
}

template <typename T>
bool ProducerConsumerQueue<T>::GetWithTimeout(T* result, int64_t ms) {
  absl::Time deadline = absl::Now() + absl::Milliseconds(ms);
  absl::MutexLock m(mutex_);

  if (count_.load(std::memory_order_relaxed) == 0) {
    Waiter self;
    PushWaiter(&self);
    do {
      if (self.cv.WaitWithDeadline(&mutex_, deadline)) {
        // Wait timed out.
        if (count_.load(std::memory_order_relaxed) > 0) {
          // WaitWithDeadline can return true even if the condition variable
          // was signalled, if the deadline was also reached. So we need to
          // check if there is an element in the queue before returning false.
          break;
        }
        RemoveWaiter(&self);
        return false;
      }
    } while (count_.load(std::memory_order_relaxed) == 0);
    RemoveWaiter(&self);
  }

  DCHECK_GT(count_.load(std::memory_order_relaxed), 0);
  *result = RemoveElement();
  return true;
}

template <typename T>
bool ProducerConsumerQueue<T>::TryGet(T* result) {
  absl::MutexLock m(mutex_);

  if (count_.load(std::memory_order_relaxed) > 0) {
    *result = RemoveElement();
    return true;
  }
  return false;
}

}  // namespace thread

using ProducerConsumerQueue = thread::ProducerConsumerQueue<void*>;

#endif  // THIRD_PARTY_GLOOP_THREAD_PCQUEUE_H_
