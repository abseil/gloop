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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_LABS_SORTED_RANGE_INTERNAL_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_LABS_SORTED_RANGE_INTERNAL_H_

#include <algorithm>
#include <iterator>
#include <type_traits>
#include <utility>

#include "absl/container/fixed_array.h"
#include "gloop/util/gtl/iterator_adaptors.h"

// IWYU pragma: private, include "util/gtl/labs/sorted_range.h"

namespace gtl::labs::internal_sorted_range {

// Copied from
// https://github.com/abseil/abseil-cpp/tree/master/absl/algorithm/container.h
// NOTE: it is important to defer to ADL lookup for building with C++ modules,
using std::begin;

// The type of the iterator given by begin(c) or std::begin(c), based on ADL.
template <typename SizedRange>
using SizedRangeIter = decltype(begin(std::declval<SizedRange&>()));

// SortedRangeImpl creates a sorted vector of pointers to the elements of a
// container, and provides iterators over the elements using gtl::iterator_ptr.
template <typename SizedRange, typename Compare>
class SortedRangeImpl {
 public:
  static_assert(
      std::is_convertible<typename std::iterator_traits<
                              SizedRangeIter<SizedRange>>::iterator_category,
                          std::forward_iterator_tag>{},
      "Expected a collection with at least a forward_iterator.");
  // Get the proper pointer type based on the container.
  //   std::vector<T> => T*
  //   const std::vector<T> => const T*
  //   absl::flat_hash_set<T> => const T* (always const)
  //   const absl::flat_hash_set<T> => const T*
  //   absl::flat_hash_map<K, V> => pair<const K, V>* (key always const)
  using value_type =
      typename std::iterator_traits<SizedRangeIter<SizedRange>>::value_type;
  using size_type = typename absl::FixedArray<value_type>::size_type;
  using difference_type = typename std::iterator_traits<
      SizedRangeIter<SizedRange>>::difference_type;
  using reference =
      typename std::iterator_traits<SizedRangeIter<SizedRange>>::reference;
  using const_reference = typename std::iterator_traits<
      SizedRangeIter<const SizedRange>>::reference;
  using pointer =
      typename std::iterator_traits<SizedRangeIter<SizedRange>>::pointer;
  using const_pointer =
      typename std::iterator_traits<SizedRangeIter<const SizedRange>>::pointer;

  SortedRangeImpl(SizedRange& container, Compare cmp, bool is_stable_sort)
      // std::size is required for raw C arrays.
      : elements_(std::size(container)) {
    {
      size_type idx = 0;
      for (auto& e : container) {
        elements_[idx++] = &e;
      }
    }
    auto comparator = [&cmp](const_pointer e1, const_pointer e2) {
      return cmp(*e1, *e2);
    };
    if (is_stable_sort) {
      std::stable_sort(elements_.begin(), elements_.end(), comparator);
    } else {
      std::sort(elements_.begin(), elements_.end(), comparator);
    }
  }

  // Iterators API.
  using const_iterator =
      gtl::iterator_ptr<typename absl::FixedArray<pointer>::const_iterator>;
  const_iterator cbegin() const { return const_iterator(elements_.cbegin()); }
  const_iterator cend() const { return const_iterator(elements_.cend()); }

  using iterator =
      gtl::iterator_ptr<typename absl::FixedArray<pointer>::iterator>;
  iterator begin() { return iterator(elements_.begin()); }
  iterator end() { return iterator(elements_.end()); }
  const_iterator begin() const { return const_iterator(elements_.cbegin()); }
  const_iterator end() const { return const_iterator(elements_.cend()); }

  using const_reverse_iterator = gtl::iterator_ptr<
      typename absl::FixedArray<pointer>::const_reverse_iterator>;
  const_reverse_iterator crbegin() const {
    return const_reverse_iterator(elements_.crbegin());
  }
  const_reverse_iterator crend() const {
    return const_reverse_iterator(elements_.crend());
  }

  using reverse_iterator =
      gtl::iterator_ptr<typename absl::FixedArray<pointer>::reverse_iterator>;
  reverse_iterator rbegin() { return reverse_iterator(elements_.rbegin()); }
  reverse_iterator rend() { return reverse_iterator(elements_.rend()); }
  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(elements_.crbegin());
  }
  const_reverse_iterator rend() const {
    return const_reverse_iterator(elements_.crend());
  }

  // Element access.
  reference at(size_type pos) { return *elements_.at(pos); }

  const_reference at(size_type pos) const { return *elements_.at(pos); }

  reference operator[](size_type pos) { return *elements_[pos]; }

  const_reference operator[](size_type pos) const { return *elements_[pos]; }

  // Capacity.
  bool empty() const noexcept { return elements_.empty(); }

  size_type size() const noexcept { return elements_.size(); }

 private:
  // Sorted pointers to the elements in the original container.
  absl::FixedArray<pointer> elements_;
};

}  // namespace gtl::labs::internal_sorted_range

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_LABS_SORTED_RANGE_INTERNAL_H_
