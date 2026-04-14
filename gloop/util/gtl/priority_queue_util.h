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

// Provides utility methods for interacting with priority_queues and data
// structures with invariants managed by std::*_heap.

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_PRIORITY_QUEUE_UTIL_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_PRIORITY_QUEUE_UTIL_H_

#include <assert.h>

#include <algorithm>
#include <cstddef>
#include <queue>
#include <utility>
#include <vector>

namespace gtl {

// Removes the top element from a std::priority_queue and returns it.  Supports
// movable types.
template <typename T, typename Container, typename Comparator>
T ConsumeTop(std::priority_queue<T, Container, Comparator>* q) {
  // std::priority_queue is required to implement pop() as if it called:
  //   std::pop_heap()
  //   c.pop_back()
  // unfortunately, it does not provide access to the removed element.  If the
  // element is move only (such as a unique_ptr), there is no way to reclaim it
  // in the standard API.  std::priority_queue does, however, expose the
  // underlying container as a protected member, so we use that access to
  // extract the desired element between those two calls.
  using Q = std::priority_queue<T, Container, Comparator>;
  struct Expose : Q {
    using Q::c;
    using Q::comp;
  };
  auto& c = q->*&Expose::c;
  auto& comp = q->*&Expose::comp;
  auto r = std::move(c.front());
  std::pop_heap(c.begin(), c.end(), comp);
  c.pop_back();
  return r;
}

// Reorders the elements of the range [first, last) to restore the heap
// condition (i.e. `std::is_heap(first, last, comp)`) following a change
// in the value of `*(first + hole)`.
//
// REQUIRES: `first + hole < last`, and [first, last) would be a valid heap
// if `*(first + hole)` had a suitable value.
template <typename RandIt, typename Compare>
void push_down_heap(size_t hole, RandIt first, RandIt last, Compare comp) {
  size_t size = last - first;
  assert(hole < size);
  auto value = std::move(first[hole]);
  while (true) {
    size_t l_child = 2 * hole + 1;
    size_t r_child = l_child + 1;
    size_t max_child = l_child;
    if (r_child < size && comp(first[l_child], first[r_child])) {
      max_child = r_child;
    }
    if (max_child >= size) break;
    if (!comp(value, first[max_child])) break;
    first[hole] = std::move(first[max_child]);
    hole = max_child;
  }
  first[hole] = std::move(value);
}

// Pushes the root down the heap.
template <typename RandIt, typename Compare>
void push_root_heap(RandIt first, RandIt last, Compare comp) {
  push_down_heap(0, std::move(first), std::move(last), std::move(comp));
}

// Outputs the indexes of the elements that compare equal to the root of the
// heap.
//
// REQUIRES: eq(a, b) <=> !comp(a, b) && !comp(b, a)
// REQUIRES: first != last
template <typename RandIt, typename Eq>
void get_all_top_heap(RandIt first, RandIt last, Eq eq,
                      std::vector<size_t>* out) {
  out->clear();
  out->push_back(0);
  size_t limit = last - first;
  for (size_t i = 0; i != out->size(); ++i) {
    size_t x = (*out)[i];
    size_t left = 2 * x + 1;
    size_t right = 2 * x + 2;
    if (left >= limit) break;
    if (eq(first[0], first[left])) out->push_back(left);
    if (right >= limit) break;
    if (eq(first[0], first[right])) out->push_back(right);
  }
}

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_PRIORITY_QUEUE_UTIL_H_
