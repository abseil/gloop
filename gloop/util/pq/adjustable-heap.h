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

#include <iterator>
#include <type_traits>
#include <utility>

namespace util {
namespace pq {

namespace internal {

template <typename Iter>
using ValueType = typename std::iterator_traits<Iter>::value_type;

template <typename T>
inline constexpr bool kByValue =
    sizeof(T) <= 2 * sizeof(void*) && std::is_trivially_copyable_v<T>;

// Resolves to V (by value) for small trivially copyable elements, or Ref
// (by reference) otherwise. Holding small elements (such as pointers) by value
// keeps them in registers across writes and maintainer callbacks that could
// otherwise alias under -fno-strict-aliasing, while avoiding extra copies for
// larger or non-trivial types.
template <typename V, typename Ref = V&>
using ValueOrRef =
    std::conditional_t<kByValue<V> && std::is_convertible_v<Ref, V>, V, Ref>;

template <typename Iter, typename Distance, typename Elem, typename Maintainer>
inline void Set(Iter base, Distance offset, Elem&& elem,
                Maintainer* maintainer) {
  *(base + offset) = std::forward<Elem>(elem);
  if constexpr (kByValue<ValueType<Iter>> &&
                std::is_invocable_v<Maintainer&, Elem&, Distance>) {
    (*maintainer)(elem, offset);
  } else if constexpr (std::is_invocable_v<
                           Maintainer&, decltype(*(base + offset)), Distance>) {
    (*maintainer)(*(base + offset), offset);
  } else {
    (*maintainer)(offset);
  }
}

template <typename Iter, typename Distance, typename Compare>
inline std::pair<Distance, ValueOrRef<ValueType<Iter>>> LargerChild(
    Iter base, Distance two_i, const Compare& comp) {
  ValueOrRef<ValueType<Iter>> left = *(base + (two_i + 1));
  ValueOrRef<ValueType<Iter>> right = *(base + (two_i + 2));
  if (comp(left, right)) return {two_i + 2, right};
  return {two_i + 1, left};
}

template <bool kSkipIfUnchanged, typename Iter, typename Distance, typename T,
          typename Compare, typename Maintainer>
inline void AdjustDownwardsImpl(Iter base, Distance i, Distance len, T&& t,
                                const Compare& comp, Maintainer* maintainer) {
  using V = ValueType<Iter>;
  const Distance start = i;
  ValueOrRef<V, T&&> val = std::forward<T>(t);
  Distance two_i = 2 * i;
  if (len >= Distance(2)) {
    const Distance limit = len - Distance(2);
    while (two_i < limit) {
      auto [next_i, next_val] = LargerChild(base, two_i, comp);
      if (!comp(val, next_val)) {
        if constexpr (kSkipIfUnchanged) {
          if (i == start) return;
        }
        Set(base, i, std::forward<decltype(val)>(val), maintainer);
        return;
      }
      Set(base, i, std::move(next_val), maintainer);
      i = next_i;
      two_i = 2 * i;
    }
  }
  const Distance left_child = two_i + 1;
  if (left_child < len) {
    ValueOrRef<V> left = *(base + left_child);
    if (comp(val, left)) {
      Set(base, i, std::move(left), maintainer);
      i = left_child;
    }
  }
  if constexpr (kSkipIfUnchanged) {
    if (i == start) return;
  }
  Set(base, i, std::forward<decltype(val)>(val), maintainer);
}

}  // namespace internal

// Adjusts a heap so as to move a hole at position i closer to the root,
// until a suitable position is found for element t.  Then, copies t into that
// position.  This function is a slight generalization of std::push_heap() in
// that it takes a pointer to an IndexMaintainer functor.  This functor is
// called each time immediately after modifying a value in the underlying
// container, with the offset of the modified element as its argument (or with
// the modified element and offset if invocable with two arguments).
template <typename RandomAccessIterator, typename Distance, typename T,
          typename Compare, typename IndexMaintainer>
inline void AdjustUpwards(const RandomAccessIterator& first, Distance i, T&& t,
                          const Compare& comp, IndexMaintainer* maintainer) {
  using V = internal::ValueType<RandomAccessIterator>;
  RandomAccessIterator base = first;
  internal::ValueOrRef<V, T&&> val = std::forward<T>(t);
  while (i > Distance(0)) {
    const Distance parent = (i - 1) / 2;
    internal::ValueOrRef<V> parent_val = *(base + parent);
    if (!comp(parent_val, val)) break;
    internal::Set(base, i, std::move(parent_val), maintainer);
    i = parent;
  }
  internal::Set(base, i, std::forward<decltype(val)>(val), maintainer);
}

// Adjusts a heap so as to move a hole at position i farther away from the root,
// until a suitable position is found for element t.  Then, copies t into that
// position.  This function is a generalization of std::pop_heap() in two
// regards:  One, std::pop_heap() can only treat the first (i.e., maximum)
// element of a heap as a hole, which is an artificial restriction.  And two,
// this function takes a pointer to an IndexMaintainer functor, which is
// called each time immediately after modifying a value in the underlying
// conatiner, with the offset of the modified element as its argument (or with
// the modified element and offset if invocable with two arguments).
template <typename RandomAccessIterator, typename Distance, typename T,
          typename Compare, typename IndexMaintainer>
inline void AdjustDownwards(const RandomAccessIterator& first, Distance i,
                            const Distance& length, T&& t, const Compare& comp,
                            IndexMaintainer* maintainer) {
  internal::AdjustDownwardsImpl<false>(first, i, length, std::forward<T>(t),
                                       comp, maintainer);
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
  using V = internal::ValueType<RandomAccessIterator>;
  RandomAccessIterator base = first;
  Distance two_i = 2 * i;
  if (length >= Distance(2)) {
    const Distance limit = length - Distance(2);
    while (two_i < limit) {
      auto [next_i, next_val] = internal::LargerChild(base, two_i, comp);
      internal::Set(base, i, std::move(next_val), maintainer);
      i = next_i;
      two_i = 2 * i;
    }
  }
  const Distance left_child = two_i + 1;
  if (left_child < length) {
    internal::ValueOrRef<V> left = *(base + left_child);
    internal::Set(base, i, std::move(left), maintainer);
    return left_child;
  }
  return i;
}

}  // namespace pq
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_PQ_ADJUSTABLE_HEAP_H__
