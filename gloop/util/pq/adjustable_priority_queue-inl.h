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

#ifndef THIRD_PARTY_GLOOP_UTIL_PQ_ADJUSTABLE_PRIORITY_QUEUE_INL_H_
#define THIRD_PARTY_GLOOP_UTIL_PQ_ADJUSTABLE_PRIORITY_QUEUE_INL_H_

#include <functional>
#include <list>
#include <vector>

#include "absl/log/check.h"
#include "gloop/util/pq/adjustable-heap.h"
#include "gloop/util/pq/adjustable_priority_queue.h"

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
  return elems_[0];
}

template <class T, class C, class I>
inline const T* AdjustablePriorityQueue<T, C, I>::Top() const {
  return elems_[0];
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
    return queue_->imanip_.SetHeapIndex(queue_->elems_[offset], offset);
  }

 private:
  AdjustablePriorityQueue<T, C, I>* queue_;
};

template <class T, class C, class I>
void AdjustablePriorityQueue<T, C, I>::Add(T* val) {
  AQPSetHeapIndex<T, C, I> imanip(this);
  // Extend the size of the vector by one.  We could just use
  // vector<T>::resize(), but maybe T is not default-constructible.
  elems_.push_back(val);
  const ssize_t sminus1 = Size() - 1;
  util::pq::AdjustUpwards(elems_.begin(), sminus1, val,
                          LowerPriorityThan<T, C>(&c_), &imanip);
}

template <class T, class C, class I>
void AdjustablePriorityQueue<T, C, I>::NoteChangedPriority(T* val) {
  AQPSetHeapIndex<T, C, I> imanip(this);
  LowerPriorityThan<T, C> lower_priority(&c_);
  const ssize_t i = imanip_.GetHeapIndex(*val);
  const ssize_t parent = (i - 1) / 2;
  if (i > 0 && lower_priority(elems_[parent], val)) {
    elems_[i] = elems_[parent];
    imanip_.SetHeapIndex(elems_[i], i);
    util::pq::AdjustUpwards(elems_.begin(), parent, val, lower_priority,
                            &imanip);
  } else {
    const ssize_t size = Size();
    util::pq::AdjustDownwards(elems_.begin(), i, size, val, lower_priority,
                              &imanip);
  }
}

template <class T, class C, class I>
void AdjustablePriorityQueue<T, C, I>::Remove(T* val) {
  const ssize_t end = elems_.size() - 1;
  const ssize_t i = imanip_.GetHeapIndex(*val);
  DCHECK_EQ(elems_[i], val);
  if (i == end) {
    elems_.pop_back();
    return;
  }
  elems_[i] = elems_[end];
  imanip_.SetHeapIndex(elems_[i], i);
  elems_.pop_back();
  NoteChangedPriority(elems_[i]);
}

template <class T, class C, class I>
void AdjustablePriorityQueue<T, C, I>::Pop() {
  const size_t size = elems_.size();
  DCHECK_GT(size, 0);
  AQPSetHeapIndex<T, C, I> imanip(this);
  LowerPriorityThan<T, C> lower_priority(&c_);
  T* v = elems_[size - 1];  // Load early as this also serves as Prefetch.
  size_t i = util::pq::AdjustHoleDownwards(elems_.begin(), size_t{0}, size,
                                           lower_priority, &imanip);
  elems_.pop_back();
  if (i + 1 == size) return;
  util::pq::AdjustUpwards(elems_.begin(), i, v, lower_priority, &imanip);
}

template <class T, class C, class I>
bool AdjustablePriorityQueue<T, C, I>::Contains(const T* val) const {
  ssize_t i = imanip_.GetHeapIndex(*val);
  return (i >= 0 && i < elems_.size() && elems_[i] == val);
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

#endif  // THIRD_PARTY_GLOOP_UTIL_PQ_ADJUSTABLE_PRIORITY_QUEUE_INL_H_
