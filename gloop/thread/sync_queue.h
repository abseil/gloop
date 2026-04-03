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
// SyncQueue supports locking only.

#ifndef THIRD_PARTY_GLOOP_THREAD_SYNC_QUEUE_H_
#define THIRD_PARTY_GLOOP_THREAD_SYNC_QUEUE_H_

#include <deque>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/synchronization/mutex.h"

//
// SyncQueue supports locking only.  All operations are atomic.
// The Pop operation differs from stl in that it atomically tests
// whether the queue is empty, and, if not, it stores a copy of the element
// into the provided pointer before removing it from the front of the queue.
//
template <class T>
class SyncQueue {
 public:
  typedef std::deque<T> container_type;

  typedef typename container_type::value_type value_type;
  typedef typename container_type::size_type size_type;

  SyncQueue() = default;

  bool empty() const {
    absl::ReaderMutexLock l(busy_);
    return q_.empty();
  }
  size_type size() const {
    absl::ReaderMutexLock l(busy_);
    return q_.size();
  }

  // Push x onto the back of the queue.  The last item pushed this way
  // will be the last one to be popped.
  void push(const value_type& x) {
    absl::MutexLock l(busy_);
    q_.push_back(x);
  }

  void push(value_type&& x) {
    absl::MutexLock l(busy_);
    q_.push_back(std::move(x));
  }

  // Push x onto the front of the queue.  The last item pushed this way
  // will be the first one to be popped.
  void push_front(const value_type& x) {
    absl::MutexLock l(busy_);
    q_.push_front(x);
  }

  void push_front(value_type&& x) {
    absl::MutexLock l(busy_);
    q_.push_front(std::move(x));
  }

  // Atomically pop the front element into *p.  If it was present, return
  // true; if the queue was empty, leave *p unchanged and return false.
  bool Pop(value_type* p);

  // Atomically set *p to the front element.  If there was a front element,
  // return true; if the queue was empty, leave *p unchanged and return false.
  bool Front(value_type* p) const;

  // Atomically swap the container in the queue with the user provided
  // container, which should be empty. A common use case for this is to
  // send out everything in the queue in one operation.
  void SwapEmptyContainer(container_type* container);

 protected:
  mutable absl::Mutex busy_;
  container_type q_ ABSL_GUARDED_BY(busy_);
};

// ----------------------------------------------------------------------------
// Implementations of non-inline functions

template <class T>
bool SyncQueue<T>::Pop(value_type* p) {
  absl::MutexLock l(busy_);
  if (q_.empty()) return false;
  *p = std::move(q_.front());
  q_.pop_front();
  return true;
}

template <class T>
bool SyncQueue<T>::Front(value_type* p) const {
  absl::ReaderMutexLock l(busy_);
  if (q_.empty()) return false;
  *p = q_.front();
  return true;
}

template <class T>
void SyncQueue<T>::SwapEmptyContainer(container_type* container) {
  DCHECK(container->empty());
  absl::MutexLock l(busy_);
  q_.swap(*container);
}

#endif  // THIRD_PARTY_GLOOP_THREAD_SYNC_QUEUE_H_
