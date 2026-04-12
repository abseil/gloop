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

#include "gloop/util/gtl/iterator_range.h"

#include <map>
#include <type_traits>
#include <utility>
#include <vector>

#include "gloop/gloop_test.h"

namespace {

using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::Pair;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;

TEST(IteratorRange, WholeVector) {
  std::vector<int> v = {2, 3, 5, 7, 11, 13};
  gtl::iterator_range<std::vector<int>::iterator> range(v.begin(), v.end());
  EXPECT_THAT(range, ElementsAreArray(v));
  EXPECT_THAT(range, SizeIs(v.size()));
  EXPECT_THAT(range, Not(IsEmpty()));
}

TEST(IteratorRange, VectorMakeRange) {
  std::vector<int> v = {2, 3, 5, 7, 11, 13};
  EXPECT_THAT(gtl::make_range(v.begin(), v.end()), ElementsAreArray(v));
}

TEST(IteratorRange, PartArray) {
  int v[] = {2, 3, 5, 7, 11, 13};
  gtl::iterator_range<int*> range(&v[1], &v[4]);  // 3, 5, 7
  EXPECT_THAT(range, ElementsAre(3, 5, 7));
  EXPECT_THAT(range, SizeIs(3));
  EXPECT_THAT(range, Not(IsEmpty()));
}

TEST(IteratorRange, ArrayMakeRange) {
  int v[] = {2, 3, 5, 7, 11, 13};
  EXPECT_THAT(gtl::make_range(&v[1], &v[4]), ElementsAre(3, 5, 7));
}

TEST(IteratorRange, PairMakeRange) {
  int v[] = {2, 3, 5, 7, 11, 13};
  EXPECT_THAT(gtl::make_range(std::make_pair(&v[1], &v[4])),
              ElementsAre(3, 5, 7));
}

TEST(IteratorRange, MultimapMakeRange) {
  std::multimap<int, int> m = {{2, 3}, {2, 5}, {3, 5}, {3, 7}, {5, 11}};
  EXPECT_THAT(gtl::make_range(m.equal_range(3)),
              UnorderedElementsAre(Pair(3, 5), Pair(3, 7)));
}

TEST(IteratorRange, IteratorMemberTypeIsSameAsOnePassedIn) {
  std::vector<int> v = {0, 2, 3, 4};
  static_assert(::std::is_same<typename decltype(gtl::make_range(
                                   v.begin(), v.end()))::iterator,
                               std::vector<int>::iterator>::value,
                "iterator_range::iterator type is not the same as the type of "
                "the underlying iterator");
}

TEST(IteratorRange, VectorRangeHasSize) {
  std::vector<int> v = {0, 2, 3, 4};
  EXPECT_EQ(gtl::make_range(v.begin(), v.end()).size(), v.size());
}

}  // namespace
