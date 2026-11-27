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

#include <algorithm>
#include <cstddef>
#include <functional>
#include <list>
#include <vector>

#include "absl/log/check.h"
#include "gloop/util/pq/adjustable-heap.h"

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
  // Attempts to sift `val` upwards from index `i`, peeling the first parent
  // comparison/swap so callers on the common no-sift path avoid entering
  // AdjustUpwards. Returns true if `val` moved (with its final position and
  // heap index updated), or false if it stayed at `i` (leaving index `i`
  // untouched for the caller to either record or sift downwards).
  bool TryAdjustUpwards(size_t i, T* val);

  [[no_unique_address]] Comp c_;
  [[no_unique_address]] HeapIndexManip imanip_;
  std::vector<T*> elems_;
};

template <class T, class Comp, class HeapIndexManip>
template <class Iter>
AdjustablePriorityQueue<T, Comp, HeapIndexManip>::AdjustablePriorityQueue(
    Iter first, Iter last, const Comp& c, const HeapIndexManip& m)
    : c_(c), imanip_(m), elems_(first, last) {
  const size_t size = elems_.size();
  if (size == 0) return;
  T** const data = elems_.data();
  if (size > 1) {
    // Heapify directly with AdjustDownwards instead of std::make_heap:
    // AdjustDownwards operates on raw pointers with a single bounds check per
    // level and holds T* in registers, avoiding std::make_heap's extra
    // per-level checks and reloads. Heap indices are written once in the
    // hoisted loop below rather than on every intermediate sift swap.
    struct NoOpMaintainer {
      void operator()(T*, size_t) const {}
    } noop;
    auto lower_priority = [this](T* a, T* b) { return c_(*a, *b); };
    for (size_t i = size / 2; i-- > 0;) {
      util::pq::AdjustDownwards(data, i, size, data[i], lower_priority, &noop);
    }
  }
  for (size_t i = 0; i < size; ++i) {
    imanip_.SetHeapIndex(data[i], i);
  }
}

template <typename T, typename Comparator>
class LowerPriorityThan {
 public:
  explicit LowerPriorityThan(Comparator* compare) : compare_(compare) {}
  bool operator()(T* a, T* b) const { return (*compare_)(*a, *b); }

 private:
  Comparator* compare_;
};

template <class T, class C, class I>
inline T* AdjustablePriorityQueue<T, C, I>::Top() {
  DCHECK(!elems_.empty());
  return elems_.data()[0];
}

template <class T, class C, class I>
inline const T* AdjustablePriorityQueue<T, C, I>::Top() const {
  DCHECK(!elems_.empty());
  return elems_.data()[0];
}

template <class T, class C, class I>
void AdjustablePriorityQueue<T, C, I>::AllTop(std::vector<T*>* topvec) {
  topvec->clear();
  if (Size() == 0) return;
  std::list<size_t> need_to_check_children;
  need_to_check_children.push_back(0);
  // Implements breadth-first search down tree, stopping whenever
  // there's an element < top
  while (!need_to_check_children.empty()) {
    size_t ind = need_to_check_children.front();
    need_to_check_children.pop_front();
    topvec->push_back(elems_[ind]);
    size_t leftchild = 1 + 2 * ind;
    if (leftchild < Size()) {
      if (!LowerPriorityThan<T, C>(&c_)(elems_[leftchild], elems_[ind])) {
        need_to_check_children.push_back(leftchild);
      }
      size_t rightchild = leftchild + 1;
      if (rightchild < Size() &&
          !LowerPriorityThan<T, C>(&c_)(elems_[rightchild], elems_[ind])) {
        need_to_check_children.push_back(rightchild);
      }
    }
  }
}

template <class T, class C, class I>
class AQPSetHeapIndex {
 public:
  explicit AQPSetHeapIndex(AdjustablePriorityQueue<T, C, I>* queue)
      : queue_(queue) {}
  void operator()(size_t offset) const {
    return queue_->imanip_.SetHeapIndex(queue_->elems_.data()[offset], offset);
  }
  void operator()(T* val, size_t offset) const {
    return queue_->imanip_.SetHeapIndex(val, offset);
  }

 private:
  AdjustablePriorityQueue<T, C, I>* queue_;
};

template <class T, class C, class I>
inline bool AdjustablePriorityQueue<T, C, I>::TryAdjustUpwards(size_t i,
                                                               T* val) {
  if (i == 0) return false;
  T** const data = elems_.data();
  const size_t parent = (i - 1) / 2;
  T* const parent_val = data[parent];
  LowerPriorityThan<T, C> lower_priority(&c_);
  if (!lower_priority(parent_val, val)) return false;
  data[i] = parent_val;
  imanip_.SetHeapIndex(parent_val, i);
  AQPSetHeapIndex<T, C, I> imanip(this);
  util::pq::AdjustUpwards(data, parent, val, lower_priority, &imanip);
  return true;
}

template <class T, class C, class I>
void AdjustablePriorityQueue<T, C, I>::Add(T* val) {
  const size_t i = elems_.size();
  // Extend the size of the vector by one.  We could just use
  // vector<T>::resize(), but maybe T is not default-constructible.
  elems_.push_back(val);
  if (!TryAdjustUpwards(i, val)) {
    imanip_.SetHeapIndex(val, i);
  }
}

template <class T, class C, class I>
void AdjustablePriorityQueue<T, C, I>::NoteChangedPriority(T* val) {
  const size_t i = static_cast<size_t>(imanip_.GetHeapIndex(*val));
  T** const data = elems_.data();
  const size_t size = elems_.size();
  DCHECK_LT(i, size);
  DCHECK_EQ(data[i], val);
  if (TryAdjustUpwards(i, val)) return;
  AQPSetHeapIndex<T, C, I> imanip(this);
  util::pq::internal::AdjustDownwardsImpl<true>(
      data, i, size, val, LowerPriorityThan<T, C>(&c_), &imanip);
}

template <class T, class C, class I>
void AdjustablePriorityQueue<T, C, I>::Remove(T* val) {
  const size_t size = elems_.size();
  DCHECK_GT(size, size_t{0});
  const size_t i = static_cast<size_t>(imanip_.GetHeapIndex(*val));
  T** const data = elems_.data();
  DCHECK_LT(i, size);
  DCHECK_EQ(data[i], val);
  if (size <= 1) {
    elems_.clear();
    return;
  }
  const size_t end = size - 1;
  if (i == end) {
    elems_.pop_back();
    return;
  }
  if (i == 0) {
    Pop();
    return;
  }
  T* const last = data[end];
  elems_.pop_back();
  if (TryAdjustUpwards(i, last)) return;
  AQPSetHeapIndex<T, C, I> imanip(this);
  util::pq::AdjustDownwards(data, i, end, last, LowerPriorityThan<T, C>(&c_),
                            &imanip);
}

template <class T, class C, class I>
void AdjustablePriorityQueue<T, C, I>::Pop() {
  const size_t size = elems_.size();
  DCHECK_GT(size, size_t{0});
  if (size <= 1) {
    elems_.clear();
    return;
  }
  T** const data = elems_.data();
  const size_t new_size = size - 1;
  T* const v = data[new_size];
  elems_.pop_back();
  AQPSetHeapIndex<T, C, I> imanip(this);
  LowerPriorityThan<T, C> lower_priority(&c_);
  const size_t i = util::pq::AdjustHoleDownwards(data, size_t{0}, new_size,
                                                 lower_priority, &imanip);
  util::pq::AdjustUpwards(data, i, v, lower_priority, &imanip);
}

template <class T, class C, class I>
bool AdjustablePriorityQueue<T, C, I>::Contains(const T* val) const {
  const size_t i = static_cast<size_t>(imanip_.GetHeapIndex(*val));
  return i < elems_.size() && elems_.data()[i] == val;
}

template <class T, class C, class I>
void AdjustablePriorityQueue<T, C, I>::Clear() {
  elems_.clear();
}

template <class T, class C, class I>
void AdjustablePriorityQueue<T, C, I>::CheckValid() {
  for (size_t i = 0; i < elems_.size(); ++i) {
    size_t left_child = 1 + 2 * i;
    if (left_child < elems_.size()) {
      CHECK(!(LowerPriorityThan<T, C>(&c_))(elems_[i], elems_[left_child]));
    }
    size_t right_child = left_child + 1;
    if (right_child < elems_.size()) {
      CHECK(!(LowerPriorityThan<T, C>(&c_))(elems_[i], elems_[right_child]));
    }
  }
}

#endif  // THIRD_PARTY_GLOOP_UTIL_PQ_ADJUSTABLE_PRIORITY_QUEUE_H__
