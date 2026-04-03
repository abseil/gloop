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

// WaitQueueThread provides a simple way to run a thread-safe queue with a
// single consumer thread.
//
// Example:
//
//   void Consume(const T &t) { ... }
//
//   // Create a consumer.
//   WaitQueueThread<T> consumer(thread::Options(), "consumer",
//                               NewPermanentCallback(&Consume));
//   ...
//   consumer.push(t);

#ifndef THIRD_PARTY_GLOOP_THREAD_WAIT_QUEUE_THREAD_H_
#define THIRD_PARTY_GLOOP_THREAD_WAIT_QUEUE_THREAD_H_

#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"
#include "gloop/thread/wait_queue.h"

// WaitQueueThread contains a WaitQueue or PrioritizedWaitQueue and a single
// consumer thread.  The thread is owned and deleted by the WaitQueueThread
// instance.
// The type of Queue determines the order in which items are consumed: if a
// WaitQueue is used then items are consumed in FIFO order.
template <typename T, typename Queue = WaitQueue<T> >
class WaitQueueThread {
 public:
  typedef typename Queue::value_type value_type;
  typedef typename Queue::size_type size_type;

  // Starts a consumer thread that processes enqueued items using "callback".
  WaitQueueThread(const thread::Options& thread_options,
                  absl::string_view thread_name,
                  absl::AnyInvocable<void(T)> callback);

  // Drains the queue and destroys the consumer thread.
  // Note: push() (and push_front()) must not be called once the destructor is
  //       called.
  ~WaitQueueThread();

  // Enqueues an item to be consumed by "callback" onto the back of the queue.
  // push() is threadsafe and can be called from any thread as long as the
  // destructor has not been called yet.
  void push(const value_type& item) { queue_.push(item); }
  void push(value_type&& item) { queue_.push(std::move(item)); }

  // Enqueues an item to be consumed by "callback" onto the front of the queue.
  // Queue must support a push_front() method.
  // Like push(), push_front() is threadsafe and can be called from any thread
  // as long as the destructor has not been called yet.
  void push_front(const value_type& item) { queue_.push_front(item); }
  void push_front(value_type&& item) { queue_.push_front(std::move(item)); }

  // Sets the max queue size for the underlying Queue.
  void set_max_queue_size(size_type max_queue_size) {
    queue_.set_max_queue_size(max_queue_size);
  }

  // Size of underlying queue.
  size_type queue_size() const { return queue_.size(); }

  // Atomically copy the current contents of the queue into a separate
  // container.
  //
  // IMPORTANT: For this method to be safe, item_type must be a type with value
  // semantics. Queues containing raw pointers must not be copied, because the
  // item might be popped and deleted before the copy is inspected. It is safe
  // to use shared_ptr<> items, but be aware that the items might be available
  // to two threads at once.
  void CopyTo(typename Queue::container_type* container) const;

  // Direct access to the queue implementation. You should not push anything
  // on the queue after calling the destructor. This function is meant to
  // allow a producer thread to push data to the WaitQueueThread callback
  // using just a queue as the API, decoupling the WaitQueueThread from
  // the producer implementation.
  Queue* queue() { return &queue_; }

 private:
  Queue queue_;
  ClosureThread thread_;  // The consumer thread.
};

// ----------------------------------------------------------------------------
// Implementations of non-inline functions

template <typename T, typename Queue>
WaitQueueThread<T, Queue>::WaitQueueThread(
    const thread::Options& thread_options, absl::string_view thread_name,
    absl::AnyInvocable<void(T)> callback)
    : thread_(thread_options, thread_name,
              [this, callback = std::move(callback)]() mutable {
                typename Queue::value_type item;
                while (queue_.Wait(&item)) {
                  callback(std::move(item));
                }
              }) {
  thread_.SetJoinable(true);
  thread_.Start();
}

template <typename T, typename Queue>
WaitQueueThread<T, Queue>::~WaitQueueThread() {
  queue_.StopWaiters();
  thread_.Join();
}

template <typename T, typename Queue>
void WaitQueueThread<T, Queue>::CopyTo(
    typename Queue::container_type* container) const {
  queue_.CopyTo(container);
}

#endif  // THIRD_PARTY_GLOOP_THREAD_WAIT_QUEUE_THREAD_H_
