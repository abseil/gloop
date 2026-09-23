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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_COMPARE_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_COMPARE_H_

#include "absl/types/compare.h"

// In this file, we provide non-public absl comparator adapter utility functions
// for use within Google3.

namespace gtl {

// A three-way comparison functor compares two arguments and returns
// absl::{weak,strong}_ordering::{less,equal,equivalent,greater} depending on
// the result. Providing a three-way comparison functor allows btree to
// perform fewer comparisons so it can make sense if each comparison is
// expensive. A user can provide a three-way comparison functor by returning
// absl::{weak,strong}_ordering:
//
//  struct ThreeWayComparator {
//    absl::weak_ordering operator()(const T &a, const T &b) const {
//      return DoComparison(a, b);
//    }
//  };
//  absl::btree_set<T, ThreeWayComparator> set;

// A helper function to do a boolean comparison of two keys given a boolean
// or three-way comparator. Example:
//  auto comp = set.key_comp();
//  T a = ...; T b = ...;
//  bool less_than = gtl::do_less_than_comparison(comp, a, b);
using absl::compare_internal::do_less_than_comparison;

// A helper function to do a three-way comparison of two keys given a boolean or
// three-way comparator. Example:
//  auto comp = set.key_comp();
//  T a = ...; T b = ...;
//  absl::weak_ordering result = gtl::do_three_way_comparison(comp, a, b);
using absl::compare_internal::do_three_way_comparison;

// A helper function to do a three-way comparison of two keys given an int or
// absl::{weak,strong}_ordering comparison result. Example:
//  std::string a = ...; std::string b = ...;
//  absl::weak_ordering result = gtl::compare_result_as_ordering(a.compare(b));
using absl::compare_internal::compare_result_as_ordering;

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_COMPARE_H_
