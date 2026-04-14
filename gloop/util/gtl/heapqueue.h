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

// HeapQueue is a wrapper template around the STL's heap_*() functions
// - Internally, it use a vector<T> to store elements in a heap order
// - Unlike STL priority_queue, it provides an iterator mechanism to more
//   efficiently remove multiple elements than repeated calls to pop()
// - HeapQueue needs T::operator<() at instantiation time

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_HEAPQUEUE_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_HEAPQUEUE_H_

#include <stddef.h>

#include <algorithm>
#include <functional>
#include <vector>

#include "absl/log/check.h"
#include "gloop/util/gtl/priority_queue_util.h"

namespace gtl {

// This class is used to safely remove bulks of items from the heap.
// Since we support unsafe (i.e. non-heap maintaining) removal,
// it makes sure that if anything was removed, the heap is rebuild afterwards.
// During the lifetime of a HeapQueueBulkRemover, the only safe operation
// is direct iteration over the element storage, all other operations (top, pop,
// push, move operations) have undefined behavior.
template <typename T>
class HeapQueueBulkRemover {
 public:
  explicit HeapQueueBulkRemover(T* heap_queue)
      : heap_queue_(heap_queue), changed_(false) {}

  // This type is neither copyable nor movable.
  HeapQueueBulkRemover(const HeapQueueBulkRemover&) = delete;
  HeapQueueBulkRemover& operator=(const HeapQueueBulkRemover&) = delete;

  ~HeapQueueBulkRemover() {
    if (changed_) {
      heap_queue_->rebuild();
    }
  }

  // remove() is a wrapper around T::unsafe_remove(), which commonly is
  // HeapQueue::unsafe_remove(); refer to the comments there.
  //
  // After this method has been called, pos contains a new element from the
  // underlying storage, so possibly needs to be revisited.
  void remove(typename T::iterator pos) {
    changed_ = true;
    heap_queue_->unsafe_remove(pos);
  }

  // remove_if() removes all elements that match a predicate.
  template <typename Predicate>
  void remove_if(Predicate predicate) {
    for (auto it = heap_queue_->begin(); it != heap_queue_->end();) {
      if (predicate(*it)) {
        remove(it);
      } else {
        ++it;
      }
    }
  }

 private:
  T* const heap_queue_;
  bool changed_;
};

template <class T, class Comparator = std::less<T>,
          class Vector = std::vector<T> >
class HeapQueue {
 public:
  typedef typename Vector::iterator iterator;
  typedef typename Vector::const_iterator const_iterator;
  typedef T value_type;
  typedef T& reference;
  typedef const T& const_reference;

  HeapQueue();
  HeapQueue(const HeapQueue&) = default;
  HeapQueue(HeapQueue&&) = default;
  HeapQueue& operator=(const HeapQueue&) = default;
  HeapQueue& operator=(HeapQueue&&) = default;

  // Primary mechanism used for storing and retrieving data on the heap
  bool empty() const;
  int size() const;
  const_reference top() const;
  void pop();
  void clear();

  // Removes the element at the front of the heap and returns it.
  value_type consume_top();

  // Attempts to add the element to the heap.
  //
  // If no max size was configured or the heap's size is less than the
  // configured max size, adds the item and returns true.
  //
  // Otherwise, if the item compares less than top(), pops the top item, adds
  // this item, and return true.
  //
  // Otherwise does nothing and returns false.
  bool push(const value_type& item) { return push_internal(item); }
  bool push(value_type&& item) { return push_internal(std::move(item)); }

  // Sets a maximum size for the heap; see notes on push(). Note that the heap's
  // size may be larger than this if it was already larger when set_max_size was
  // called or if assign() is called with a range larger than this.
  void set_max_size(int size) { max_size_ = size; }

  // Replace all elements in the HeapQueue with the ones in
  // new_elements.  After the operation, new_elements will contain the
  // old HeapQueue elements, and the heap property of the elements in
  // the HeapQueue will be restored
  void replace_elements(Vector* new_elements);

  // Replaces the contents of the heap by the elements in [begin, end)
  // and rebuilds the heap.
  template <class InputIter>
  void assign(InputIter begin, InputIter end);

  // Remove an element from a HeapQueue.
  void erase(iterator pos);

  // Rebuild a heap based on the given comparator.
  void rebuild();

  // Copies all elements e in the range [begin, end) that satisfy
  // !Comparator(e, cutoff) to the output iterator out.
  //
  // Example:
  // heap: 10 7 3 4 1 2 3 2
  // copy_until(begin(), end(), 7): 10 7
  // copy_until(begin(), end(), 3): 10 7 3 4 3

  // The output is not ordered, but if there is any output at all, the
  // root of the heap is guaranteed to be the first.
  template <typename OutputIterator>
  void copy_until(const_iterator begin, const_iterator end,
                  const value_type& cutoff, OutputIterator out) const;

  // Iterators:
  //  If you change the value of items through these access iterators
  //  *BE SURE* to call rebuild() to ensure the integrity of the heap
  //  is maintained.
  iterator begin();
  iterator end();

  const_iterator begin() const;
  const_iterator end() const;

 protected:
  // Removes the item at pos by replacing it with the last element of the
  // vector, then removing the last element.
  //
  // Preconditions
  // - heap is not empty
  //
  // Postconditions
  // - the heap is not in a consistent state after this change, and rebuild
  //   must be called to restore it.
  // - the heap is one element shorter
  // - the item in pos has been replaced with the previous last element
  // - pos might point past the end, if the last element was replaced.
  void unsafe_remove(iterator pos);

  Vector heap_;

 private:
  // Implementation shared by both public push() methods.
  template <class Arg>
  bool push_internal(Arg&& item);

  friend class HeapQueueBulkRemover<HeapQueue<T, Comparator, Vector> >;

  int max_size_;
};

template <class T, class Comparator, class Vector>
HeapQueue<T, Comparator, Vector>::HeapQueue() : max_size_(-1) {}

template <class T, class Comparator, class Vector>
void HeapQueue<T, Comparator, Vector>::replace_elements(Vector* new_elements) {
  heap_.swap(*new_elements);
  rebuild();
}

// empty()
//  return whether heap is empty or not
template <class T, class Comparator, class Vector>
bool HeapQueue<T, Comparator, Vector>::empty() const {
  return heap_.empty();
}

// size()
//  return the number of items in the heap
template <class T, class Comparator, class Vector>
int HeapQueue<T, Comparator, Vector>::size() const {
  return heap_.size();
}

// top()
//  return the highest value element in the heap (1st in vector)
template <class T, class Comparator, class Vector>
typename HeapQueue<T, Comparator, Vector>::const_reference
HeapQueue<T, Comparator, Vector>::top() const {
  DCHECK(!heap_.empty());
  return heap_.front();
}

// push_internal()
//  Pushes item into the heap using push_heap(), which expects new item at the
//  end of the vector, thus we need to add the item to the end of the vector
//  and call push_heap().  If the heap is size-limited (max_size_ is set) then
//  only items that are less than top() are pushed and the top item is pop()d
//  off the heap.
template <class T, class Comparator, class Vector>
template <class Arg>
bool HeapQueue<T, Comparator, Vector>::push_internal(Arg&& item) {
  if (max_size_ > 0 && size() >= max_size_) {
    if (!Comparator()(item, top())) {
      return false;
    }
    pop();
  }
  heap_.push_back(std::forward<Arg>(item));
  std::push_heap(heap_.begin(), heap_.end(), Comparator());
  return true;
}

// pop()
//  pop_heap move the top element to the end of the vector
//  so we need to call vector::pop_back() after pop_heap()
template <class T, class Comparator, class Vector>
void HeapQueue<T, Comparator, Vector>::pop() {
  DCHECK(!heap_.empty());
  std::pop_heap(heap_.begin(), heap_.end(), Comparator());
  heap_.pop_back();
}

// consume_top()
//  Like pop(), but returns the removed element.
template <class T, class Comparator, class Vector>
auto HeapQueue<T, Comparator, Vector>::consume_top() -> value_type {
  DCHECK(!heap_.empty());
  std::pop_heap(heap_.begin(), heap_.end(), Comparator());
  value_type to_return = std::move(heap_.back());
  heap_.pop_back();
  return to_return;
}

// Iterators:  begin(), end()
//  Just return the vector's begin() and end()
template <class T, class Comparator, class Vector>
typename HeapQueue<T, Comparator, Vector>::iterator
HeapQueue<T, Comparator, Vector>::begin() {
  return heap_.begin();
}

template <class T, class Comparator, class Vector>
typename HeapQueue<T, Comparator, Vector>::iterator
HeapQueue<T, Comparator, Vector>::end() {
  return heap_.end();
}

template <class T, class Comparator, class Vector>
typename HeapQueue<T, Comparator, Vector>::const_iterator
HeapQueue<T, Comparator, Vector>::begin() const {
  return heap_.begin();
}

template <class T, class Comparator, class Vector>
typename HeapQueue<T, Comparator, Vector>::const_iterator
HeapQueue<T, Comparator, Vector>::end() const {
  return heap_.end();
}

// clear()
// - If you do your own garbage collection of the items of type T
//   then you might need to call clear() to remove its heap entries
// - Reset the HeapQueue if you messed up values of its entries
template <class T, class Comparator, class Vector>
void HeapQueue<T, Comparator, Vector>::clear() {
  heap_.erase(heap_.begin(), heap_.end());
}

// Rebuild a heap. Useful if the contents of a heap
// change, with respect to the Comparator function.
// - Calls make_heap, so may be computationally
//   expensive.
template <class T, class Comparator, class Vector>
void HeapQueue<T, Comparator, Vector>::rebuild() {
  std::make_heap(heap_.begin(), heap_.end(), Comparator());
}

// Remove a random element from a HeapQueue.
template <class T, class Comparator, class Vector>
void HeapQueue<T, Comparator, Vector>::erase(iterator pos) {
  DCHECK(!heap_.empty());
  if (pos == heap_.end() - 1) {
    heap_.pop_back();
    return;
  }

  *pos = std::move(heap_.back());
  heap_.pop_back();
  push_down_heap(pos - heap_.begin(), heap_.begin(), heap_.end(), Comparator{});
}

template <class T, class Comparator, class Vector>
template <typename OutputIterator>
void HeapQueue<T, Comparator, Vector>::copy_until(const_iterator begin,
                                                  const_iterator end,
                                                  const value_type& cutoff,
                                                  OutputIterator out) const {
  if (begin < end && !Comparator()(*begin, cutoff)) {
    *out = *begin;
    ++out;
    const size_t pos = begin - this->begin();
    const size_t pos_left = pos * 2 + 1;
    const size_t pos_right = pos * 2 + 2;
    if (pos_left < size()) {
      copy_until(this->begin() + pos_left, end, cutoff, out);
    }
    if (pos_right < size()) {
      copy_until(this->begin() + pos_right, end, cutoff, out);
    }
  }
}

template <class T, class Comparator, class Vector>
void HeapQueue<T, Comparator, Vector>::unsafe_remove(iterator pos) {
  // HeapQueue::erase is based on vector::erase (or any other underlying
  // container it uses, but we use vector) which modifies all elements
  // after pos by moving them one element to the right. We don't need this
  // here since the heap needs to be rebuild anyway, so we can take advantage
  // of O(1) removal here, at the expense of more difficult iteration.
  DCHECK(!this->heap_.empty());
  *pos = std::move(this->heap_.back());
  this->heap_.pop_back();
}

template <class T, class Comparator, class Vector>
template <class InputIter>
void HeapQueue<T, Comparator, Vector>::assign(InputIter begin, InputIter end) {
  this->heap_.assign(begin, end);
  this->rebuild();
}

}  // namespace gtl

// Old names for <link>:
using ::gtl::HeapQueue;             // NOLINT
using ::gtl::HeapQueueBulkRemover;  // NOLINT

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_HEAPQUEUE_H_
