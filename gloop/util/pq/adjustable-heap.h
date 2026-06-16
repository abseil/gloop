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

#ifndef THIRD_PARTY_GLOOP_UTIL_PQ_ADJUSTABLE_HEAP_H__
#define THIRD_PARTY_GLOOP_UTIL_PQ_ADJUSTABLE_HEAP_H__

#include <utility>

namespace util {
namespace pq {

// Adjusts a heap so as to move a hole at position i closer to the root,
// until a suitable position is found for element t.  Then, copies t into that
// position.  This function is a slight generalization of std::push_heap() in
// that it takes a pointer to an IndexMaintainer functor.  This functor is
// called each time immediately after modifying a value in the underlying
// container, with the offset of the modified element as its argument.
template <typename RandomAccessIterator, typename Distance, typename T,
          typename Compare, typename IndexMaintainer>
inline void AdjustUpwards(const RandomAccessIterator& first, Distance i, T&& t,
                          const Compare& comp, IndexMaintainer* maintainer) {
  while (i > Distance(0)) {
    const Distance parent = (i - 1) / 2;
    if (!comp(*(first + parent), t)) break;
    *(first + i) = std::move(*(first + parent));
    (*maintainer)(i);
    i = parent;
  }
  *(first + i) = std::forward<T>(t);
  (*maintainer)(i);
}

// Adjusts a heap so as to move a hole at position i farther away from the root,
// until a suitable position is found for element t.  Then, copies t into that
// position.  This function is a generalization of std::pop_heap() in two
// regards:  One, std::pop_heap() can only treat the first (i.e., maximum)
// element of a heap as a hole, which is an artificial restriction.  And two,
// this function takes a pointer to an IndexMaintainer functor, which is
// called each time immediately after modifying a value in the underlying
// conatiner, with the offset of the modified element as its argument.
template <typename RandomAccessIterator, typename Distance, typename T,
          typename Compare, typename IndexMaintainer>
inline void AdjustDownwards(const RandomAccessIterator& first, Distance i,
                            const Distance& length, T&& t, const Compare& comp,
                            IndexMaintainer* maintainer) {
  while (true) {
    const Distance left_child = 1 + 2 * i;
    if (left_child >= length) break;
    const Distance right_child = left_child + 1;
    const Distance& next_i =
        right_child < length &&
                comp(*(first + left_child), *(first + right_child))
            ? right_child
            : left_child;
    if (!comp(t, *(first + next_i))) break;
    *(first + i) = std::move(*(first + next_i));
    (*maintainer)(i);
    i = next_i;
  }
  *(first + i) = std::forward<T>(t);
  (*maintainer)(i);
}

// Adjusts a heap so as to move a hole at position i farther away from the root.
// Returns new hole position that is guaranteed to be in the bottom layer of the
// heap (i.e. has no children).
template <typename RandomAccessIterator, typename Distance, typename Compare,
          typename IndexMaintainer>
inline Distance AdjustHoleDownwards(const RandomAccessIterator& first,
                                    Distance i, Distance length,
                                    const Compare& comp,
                                    IndexMaintainer* maintainer) {
  while (true) {
    const Distance left_child = 1 + 2 * i;
    if (left_child >= length) return i;
    const Distance right_child = left_child + 1;
    if (right_child >= length) {
      *(first + i) = std::move(*(first + left_child));
      (*maintainer)(i);
      return left_child;
    }
    const Distance next_i = comp(*(first + left_child), *(first + right_child))
                                ? right_child
                                : left_child;
    *(first + i) = std::move(*(first + next_i));
    (*maintainer)(i);
    i = next_i;
  }
}

}  // namespace pq
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_PQ_ADJUSTABLE_HEAP_H__
