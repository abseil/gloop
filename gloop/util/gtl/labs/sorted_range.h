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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_LABS_SORTED_RANGE_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_LABS_SORTED_RANGE_H_

#include <functional>

#include "gloop/util/gtl/comparator.h"
#include "gloop/util/gtl/labs/sorted_range_internal.h"

namespace gtl::labs {

// SortedRange returns a sorted view of the elements of a sized range. It does
// not copy or own any of the data from the underlying sized range, and is only
// valid while that sized range remains in scope and unmodified.
//
// It is useful for iterating in order over STL containers, including vectors,
// maps, sets, multisets, std::array, raw c arrays and such. By default it sorts
// elements in ascending order. The intended use case is to provide a
// deterministic view of containers with nondeterministic iteration order, but
// the output order will be deterministic only if the elements of the container
// are unique with respect to the `Compare` function.
//
// For example, if you had a flat_hash_map<string, int>, you could iterate over
// it in sorted order (ie, deterministic) via:
//
//   for (const auto &[key, value] : gtl::labs::SortedRange(mymap)) { ... }
template <typename SizedRange, typename Compare = std::less<>>
auto SortedRange(SizedRange& sized_range, Compare cmp = Compare()) noexcept {
  return internal_sorted_range::SortedRangeImpl<SizedRange, Compare>(
      sized_range, cmp, /*is_stable_sort=*/false);
}

// Same as SortedRange but preserves the input order of equivalent values.
template <typename SizedRange, typename Compare = std::less<>>
auto StableSortedRange(SizedRange& sized_range,
                       Compare cmp = Compare()) noexcept {
  return internal_sorted_range::SortedRangeImpl<SizedRange, Compare>(
      sized_range, cmp,
      /*is_stable_sort=*/true);
}

// Sorts comparing only keys of associative containers. Note that this results
// in a partial order for std::multimap and std::unordered_multimap.
template <typename AssociativeRange, typename Compare = std::less<>>
auto KeySortedRange(AssociativeRange& associative_range,
                    Compare cmp = Compare()) noexcept {
  return internal_sorted_range::SortedRangeImpl<
      AssociativeRange, gtl::OrderBy<gtl::First, Compare>>(
      associative_range, gtl::OrderBy<gtl::First, Compare>(gtl::First(), cmp),
      /*is_stable_sort=*/false);
}

}  // namespace gtl::labs

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_LABS_SORTED_RANGE_H_
