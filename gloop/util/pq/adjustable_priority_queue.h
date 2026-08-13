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

// Adjustable priority queue.  Unlike STL priority_queue,
// implements arbitrary element removal and priority-reassignment.
// Disadvantage: is invasive in that the elements in the queue
// must implement SetHeapIndex and GetHeapIndex methods in addition
// to a < operator.  < means "is lower priority than".
// See adjustable_priority_queue_unittest.cc for an example.
// (Alternatively, you can provide separate comparator and
// heap-index-manipulation classes as template parameters.)
//
// Does no memory management of its own on individual elements...
// only manipulates pointers to them.  (No deep copying.)
// It is the user's responsibility to ensure that the data
// remains allocated for as long as it's in the priority queue,
// up to and including any final Remove() call.  (It's OK to
// delete all the data first and then call Clear(), though, as
// long as you don't do any other priority queue operations
// after the deletions.)

#ifndef THIRD_PARTY_GLOOP_UTIL_PQ_ADJUSTABLE_PRIORITY_QUEUE_H__
#define THIRD_PARTY_GLOOP_UTIL_PQ_ADJUSTABLE_PRIORITY_QUEUE_H__

#include <stddef.h>
#include <sys/types.h>

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

template <class T>
class APQDefaultHeapIndexManip {
 public:
  // t->SetHeapIndex(h) might implicitly narrow to int32 if T::SetHeapIndex
  // accepts int32.
  void SetHeapIndex(T* t, ssize_t h) const { t->SetHeapIndex(h); }
  ssize_t GetHeapIndex(const T& t) const { return t.GetHeapIndex(); }
};

template <class T, class Comp, class HeapIndexManip>
class AQPSetHeapIndex;

template <typename T, typename Comp = std::less<T>,
          typename HeapIndexManip = APQDefaultHeapIndexManip<T> >
class AdjustablePriorityQueue {
  friend class AQPSetHeapIndex<T, Comp, HeapIndexManip>;

 public:
  // Creates a new priority queue using the given comparator 'c', the heap-index
  // updater 'm', and the unordered list of initial elements in the range
  // [first, last).
  //
  // If specified, 'c' and 'm' references are not required to be alive for the
  // lifetime of this object.
  // If 'c' isn't specified, a default-constructed instance of Comp will be
  // used.
  // If 'm' isn't specified, a default-constructed instance of HeapIndexManip
  // will be used.
  // If 'first' and 'last' aren't specified, the priority queue will be
  // initially empty.
  AdjustablePriorityQueue() : AdjustablePriorityQueue(Comp()) {}
  explicit AdjustablePriorityQueue(const Comp& c,
                                   const HeapIndexManip& m = HeapIndexManip())
      : c_(c), imanip_(m) {}
  template <class Iter>
  explicit AdjustablePriorityQueue(Iter first, Iter last,
                                   const Comp& c = Comp(),
                                   const HeapIndexManip& m = HeapIndexManip());
  AdjustablePriorityQueue(const AdjustablePriorityQueue&) = delete;
  AdjustablePriorityQueue& operator=(const AdjustablePriorityQueue&) = delete;
  AdjustablePriorityQueue(AdjustablePriorityQueue&&) = default;
  AdjustablePriorityQueue& operator=(AdjustablePriorityQueue&&) = default;

  void Add(T* val);

  void Remove(T* val);

  bool Contains(const T* val) const;

  void NoteChangedPriority(T* val);
  // If val ever changes its priority, you need to call this function
  // to notify the pq so it can move it in the heap accordingly.

  T* Top();
  const T* Top() const;

  void AllTop(std::vector<T*>* topvec);
  // If there are ties for the top, this returns all of them.

  void Pop();

  ssize_t Size() const { return elems_.size(); }

  // Returns the number of elements for which storage has been allocated.
  ssize_t Capacity() const { return elems_.capacity(); }

  // Allocates storage for a given number of elements.
  void SetCapacity(size_t c) { elems_.reserve(c); }

  bool IsEmpty() const { return elems_.empty(); }

  void Clear();

  // CHECKs that the heap is actually a heap (each "parent" of >=
  // priority than its child).
  void CheckValid();

  // This is for debugging, e.g. the caller can use it to
  // examine the heap for rationality w.r.t. other parts of the
  // program.
  const std::vector<T*>* Raw() const { return &elems_; }

 private:
  [[no_unique_address]] Comp c_;
  [[no_unique_address]] HeapIndexManip imanip_;
  std::vector<T*> elems_;
};

template <class T, class Comp, class HeapIndexManip>
template <class Iter>
AdjustablePriorityQueue<T, Comp, HeapIndexManip>::AdjustablePriorityQueue(
    Iter first, Iter last, const Comp& c, const HeapIndexManip& m)
    : c_(c), imanip_(m), elems_(first, last) {
  if (elems_.empty()) return;
  std::make_heap(elems_.begin(), elems_.end(),
                 [this](T* a, T* b) { return c_(*a, *b); });
  for (size_t i = 0; i < elems_.size(); i++) {
    imanip_.SetHeapIndex(elems_[i], i);
  }
}

#endif  // THIRD_PARTY_GLOOP_UTIL_PQ_ADJUSTABLE_PRIORITY_QUEUE_H__
