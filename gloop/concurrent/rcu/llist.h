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

// A simple lock free, async signal safe linked list utility that's well-typed.
// Similar to gtl::intrusive_list, but dramatically reduced API to make lock
// freedom easy.
#ifndef THIRD_PARTY_GLOOP_CONCURRENT_RCU_LLIST_H_
#define THIRD_PARTY_GLOOP_CONCURRENT_RCU_LLIST_H_

#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <iterator>

#include "gloop/util/atomic_danger/atomic_danger.h"

namespace base {

template <typename T>
class LList;

template <typename T>
class LListEntry {
 protected:
  constexpr LListEntry() : next_(nullptr) {}

 private:
  T* next_;
  friend class LList<T>;
};

template <typename T>
class LList {
  class iterator_impl;

 public:
  constexpr LList() : head_(0) {}

  LList(const LList& other)
      : head_(other.head_.load(std::memory_order_relaxed)) {}

  bool empty() const { return head_.load(std::memory_order_relaxed) == 0; }
  // It is safe to iterate a list which is being added to (with Push),
  // but unspecified whether or not any such Push()ed elements will
  // appear in the iteration.

  // Racing any use of iterators with PopAll() is undefined.
  typedef iterator_impl iterator;

  iterator begin() { return iterator(head_.load(std::memory_order_acquire)); }
  iterator end() { return iterator(0); }

  // Insert <obj> at the head of the list; release semantics
  // against PopAll or iteration.
  // Returns true iff the list was empty.
  bool Push(T* obj) {
    LListEntry<T>* entry = obj;
    const intptr_t o = reinterpret_cast<intptr_t>(obj);
    while (true) {
      // this seems like it could be NoBarrier, but is actually needed
      // for transitivity in iteration or similar.
      const intptr_t h = head_.load(std::memory_order_acquire);
      T* const h_obj = reinterpret_cast<T*>(h);
      entry->next_ = h_obj;
      if (h == atomic_danger::CompareAndSwap(&head_, h, o,
                                             std::memory_order_release)) {
        return h == 0;
      }
    }
  }

  // Remove all elements from the list, returning them in their own list.
  // Acquire semantics against the Push()es that inserted each element.
  LList PopAll() {
    const intptr_t h = head_.exchange(0, std::memory_order_acquire);
    LList ret;
    ret.head_ = h;
    return ret;
  }

 private:
  class iterator_impl : public std::iterator<std::forward_iterator_tag, T> {
   public:
    typedef std::iterator<std::forward_iterator_tag, T> base;

    iterator_impl() : element_(NULL) {}
    explicit iterator_impl(intptr_t x) : element_(reinterpret_cast<T*>(x)) {}
    iterator_impl(const iterator_impl& x) : element_(x.element_) {}

    bool operator==(const iterator_impl& x) const {
      return element_ == x.element_;
    }

    bool operator!=(const iterator_impl& x) const {
      return element_ != x.element_;
    }

    typename base::reference operator*() const { return *operator->(); }
    typename base::pointer operator->() const { return element_; }
    iterator_impl& operator++() {
      LListEntry<T>* entry = element_;
      element_ = entry->next_;
      return *this;
    }
    iterator_impl operator++(int /*unused*/) {
      iterator_impl tmp = *this;
      ++*this;
      return tmp;
    }

   private:
    T* element_;
  };

  std::atomic<intptr_t> head_;
};

}  // namespace base

#endif  // THIRD_PARTY_GLOOP_CONCURRENT_RCU_LLIST_H_
