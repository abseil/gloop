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

#include "gloop/util/gtl/labs/sorted_range.h"

#include <stdint.h>

#include <array>
#include <iterator>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"
#include "gloop/util/gtl/comparator.h"
#include "gloop/util/gtl/iterator_adaptors.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace gtl::labs {
namespace {

using ::testing::ElementsAre;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

using PartiallySorted = std::pair<int, char>;

TEST(SortedRangeTest, RawArrays) {
  {
    // Zero-size are not allowed in c++
    // int arr[] = {};
    // auto sorted_range = SortedRange(arr);
    // EXPECT_TRUE(sorted_range.empty());
    // EXPECT_EQ(0, sorted_range.size());
  }

  {
    int arr[] = {4, 3, 2, -1};
    int sorted_arr[std::size(arr)];
    int i = 0;
    auto sorted_range = SortedRange(arr);
    EXPECT_FALSE(sorted_range.empty());
    EXPECT_EQ(std::size(arr), sorted_range.size());
    for (const int c : sorted_range) {
      EXPECT_EQ(c, sorted_range[i]);
      // Tests operator[] const.
      EXPECT_EQ(c, std::as_const(sorted_range)[i]);
      EXPECT_EQ(c, sorted_range.at(i));
      // Tests at() const.
      EXPECT_EQ(c, std::as_const(sorted_range).at(i));
      sorted_arr[i++] = c;
    }
    EXPECT_THAT(sorted_arr, ElementsAre(-1, 2, 3, 4));
  }
  {
    int arr[] = {4, 3, 2, -1};
    int sorted_arr[std::size(arr)];
    int i = 0;
    auto sorted_range = StableSortedRange(arr);
    EXPECT_FALSE(sorted_range.empty());
    EXPECT_EQ(std::size(arr), sorted_range.size());
    for (const int c : sorted_range) {
      EXPECT_EQ(c, sorted_range[i]);
      // Tests operator[] const.
      EXPECT_EQ(c, std::as_const(sorted_range)[i]);
      EXPECT_EQ(c, sorted_range.at(i));
      // Tests at() const.
      EXPECT_EQ(c, std::as_const(sorted_range).at(i));
      sorted_arr[i++] = c;
    }
    EXPECT_THAT(sorted_arr, ElementsAre(-1, 2, 3, 4));
  }
  {
    char arr[] = {'z', 'y', 'x', 'c', 'b', 'a'};
    char sorted_arr[std::size(arr)];
    int i = 0;
    auto sorted_range = SortedRange(arr);
    EXPECT_FALSE(sorted_range.empty());
    EXPECT_EQ(std::size(arr), sorted_range.size());
    for (char c : sorted_range) {
      EXPECT_EQ(c, sorted_range[i]);
      // Tests operator[] const.
      EXPECT_EQ(c, std::as_const(sorted_range)[i]);
      EXPECT_EQ(c, sorted_range.at(i));
      // Tests at() const.
      EXPECT_EQ(c, std::as_const(sorted_range).at(i));
      sorted_arr[i++] = c;
    }
    EXPECT_THAT(sorted_arr, ElementsAre('a', 'b', 'c', 'x', 'y', 'z'));
  }
  {
    PartiallySorted arr[] = {{1, 'z'}, {5, 'z'}, {5, 'a'}, {1, 'b'}};
    PartiallySorted sorted_arr[std::size(arr)];
    int i = 0;
    auto sorted_range = StableSortedRange(arr, gtl::OrderByFirst());
    EXPECT_FALSE(sorted_range.empty());
    EXPECT_EQ(std::size(arr), sorted_range.size());
    for (const PartiallySorted& c : sorted_range) {
      EXPECT_EQ(c, sorted_range[i]);
      // Tests operator[] const.
      EXPECT_EQ(c, std::as_const(sorted_range)[i]);
      EXPECT_EQ(c, sorted_range.at(i));
      // Tests at() const.
      EXPECT_EQ(c, std::as_const(sorted_range).at(i));
      sorted_arr[i++] = c;
    }
    EXPECT_THAT(sorted_arr, ElementsAre(Pair(1, 'z'), Pair(1, 'b'),
                                        Pair(5, 'z'), Pair(5, 'a')));
  }
}

TEST(SortedRangeTest, Arrays) {
  {
    std::array<int, 5> unsorted_array = {4, 1, 50, 2, 10};
    std::array<int, std::size(unsorted_array)> sorted_array = {1, 2, 4, 10, 50};

    std::array<int, std::size(unsorted_array)> sorted_elems;
    int i = 0;
    auto sorted_range = SortedRange(unsorted_array);
    EXPECT_FALSE(sorted_range.empty());
    EXPECT_EQ(std::size(unsorted_array), sorted_range.size());
    for (int e : sorted_range) {
      EXPECT_EQ(e, sorted_range[i]);
      // Tests operator[] const.
      EXPECT_EQ(e, std::as_const(sorted_range)[i]);
      EXPECT_EQ(e, sorted_range.at(i));
      // Tests at() const.
      EXPECT_EQ(e, std::as_const(sorted_range).at(i));
      sorted_elems[i++] = e;
    }
    EXPECT_EQ(sorted_array, sorted_elems);
  }
  {
    std::array<int, 5> unsorted_array = {4, 1, 50, 2, 10};
    std::array<int, std::size(unsorted_array)> sorted_array = {1, 2, 4, 10, 50};

    std::array<int, std::size(unsorted_array)> sorted_elems;
    int i = 0;
    for (int e : StableSortedRange(unsorted_array)) {
      sorted_elems[i++] = e;
    }
    EXPECT_EQ(sorted_array, sorted_elems);
  }
  {
    std::array<PartiallySorted, 4> unsorted_array(
        {PartiallySorted{1, 'z'}, {5, 'z'}, {5, 'a'}, {1, 'b'}});
    std::array<PartiallySorted, std::size(unsorted_array)> sorted_array = {
        PartiallySorted{1, 'z'}, {1, 'b'}, {5, 'z'}, {5, 'a'}};

    std::array<PartiallySorted, std::size(unsorted_array)> sorted_elems;
    int i = 0;
    for (const PartiallySorted& e :
         StableSortedRange(unsorted_array, gtl::OrderByFirst())) {
      sorted_elems[i++] = e;
    }
    EXPECT_EQ(sorted_array, sorted_elems);
  }
}

TEST(SortedRangeTest, Vector) {
  {
    const std::vector<int> unsorted_elems = {4, 1, 50, 2, 10};
    std::vector<int> sorted_elems = {1, 2, 4, 10, 50};

    std::vector<int> elems;
    for (int e : SortedRange(unsorted_elems)) {
      elems.push_back(e);
    }
    EXPECT_EQ(sorted_elems, elems);
  }
  {
    const std::vector<int> unsorted_elems = {4, 1, 50, 2, 10};
    std::vector<int> sorted_elems = {1, 2, 4, 10, 50};

    std::vector<int> elems;
    for (int e : StableSortedRange(unsorted_elems)) {
      elems.push_back(e);
    }
    EXPECT_EQ(sorted_elems, elems);
  }
  {
    const std::vector<PartiallySorted> unsorted_elems = {
        {1, 'z'}, {5, 'z'}, {5, 'a'}, {1, 'b'}};
    std::vector<PartiallySorted> sorted_elems = {
        {1, 'z'}, {1, 'b'}, {5, 'z'}, {5, 'a'}};

    std::vector<PartiallySorted> elems;
    for (const PartiallySorted& e :
         StableSortedRange(unsorted_elems, gtl::OrderByFirst())) {
      elems.push_back(e);
    }
    EXPECT_EQ(sorted_elems, elems);
  }
}

TEST(SortedRangeTest, Set) {
  absl::flat_hash_set<int> set = {1, 2, 4, 10, 50};
  std::vector<int> sorted_elems = {1, 2, 4, 10, 50};
  {
    std::vector<int> elems;
    for (const int e : SortedRange(set)) {
      elems.push_back(e);
    }
    EXPECT_EQ(sorted_elems, elems);
  }

  {
    std::vector<int> elems;
    const auto sorted_range = SortedRange(set);
    for (const int e : sorted_range) {
      elems.push_back(e);
    }
    EXPECT_EQ(sorted_elems, elems);
  }

  {
    // Check building a range over a constant set.
    const absl::flat_hash_set<int>& cset = set;
    std::vector<int> elems;
    for (const int e : SortedRange(cset)) {
      elems.push_back(e);
    }
    EXPECT_EQ(sorted_elems, elems);
  }

  {
    auto sorted_set = SortedRange(set);
    int i = 0;
    for (auto it = sorted_set.cbegin(); it != sorted_set.cend(); ++it) {
      EXPECT_EQ(sorted_elems[i++], *it);
    }
  }

  {
    auto sorted_set = SortedRange(set);
    int i = sorted_elems.size();
    for (auto it = sorted_set.rbegin(); it != sorted_set.rend(); ++it) {
      EXPECT_EQ(sorted_elems[--i], *it);
    }
  }

  {
    auto sorted_set = SortedRange(set);
    int i = sorted_elems.size();
    for (auto it = sorted_set.crbegin(); it != sorted_set.crend(); ++it) {
      EXPECT_EQ(sorted_elems[--i], *it);
    }
  }
}

TEST(SortedRangeTest, MultiSet) {
  {
    std::unordered_multiset<std::string> multiset = {"a",   "abc", "a",
                                                     "def", "abc", "def"};
    std::vector<std::string> elems;
    for (const std::string& e : SortedRange(multiset)) {
      elems.emplace_back(e);
    }
    EXPECT_THAT(elems, ElementsAre("a", "a", "abc", "abc", "def", "def"));
  }
}

TEST(SortedRangeTest, Map) {
  constexpr int kNumEntries = 20;
  absl::flat_hash_map<int, int> map;
  std::vector<int> sorted_elems;
  for (int i = 0; i < kNumEntries; ++i) {
    map[i] = i;
    sorted_elems.push_back(i);
  }

  {
    std::vector<int> elems;
    for (const auto& e : SortedRange(map)) {
      elems.push_back(e.first);
    }
    EXPECT_EQ(sorted_elems, elems);
  }

  {
    auto sorted_map = SortedRange(map);
    int i = 0;
    for (auto it = sorted_map.cbegin(); it != sorted_map.cend(); ++it) {
      EXPECT_EQ(sorted_elems[i++], it->first);
    }
  }

  {
    auto sorted_map = SortedRange(map);
    int i = sorted_elems.size();
    for (auto it = sorted_map.rbegin(); it != sorted_map.rend(); ++it) {
      EXPECT_EQ(sorted_elems[--i], it->first);
    }
  }

  {
    auto sorted_map = SortedRange(map);
    int i = sorted_elems.size();
    for (auto it = sorted_map.crbegin(); it != sorted_map.crend(); ++it) {
      EXPECT_EQ(sorted_elems[--i], it->first);
    }
  }

  {
    // Check building a range over a constant map.
    const absl::flat_hash_map<int, int>& cmap = map;
    std::vector<int> elems;
    for (auto& e : SortedRange(cmap)) {
      elems.push_back(e.first);
    }
    EXPECT_EQ(sorted_elems, elems);
  }
}

TEST(SortedRangeTest, MapWithIncomparableValue) {
  constexpr int kNumEntries = 20;
  // Incomparable has no operator<
  struct Incomparable {
    int value;
  };

  std::vector<int> sorted_keys;
  absl::flat_hash_map<int, Incomparable> flat_map;
  std::vector<int> sorted_flat_map;
  absl::node_hash_map<int, Incomparable> node_map;
  std::vector<int> sorted_node_map;
  std::unordered_map<int, Incomparable> unordered_map;
  std::vector<int> sorted_unordered_map;

  for (int i = 0; i < kNumEntries; ++i) {
    flat_map[i] = {i};
    node_map[i] = {i};
    unordered_map[i] = {i};
    sorted_keys.push_back(i);
  }

  for (const auto& [key, unused] : KeySortedRange(flat_map)) {
    sorted_flat_map.push_back(key);
  }
  for (const auto& [key, unused] : KeySortedRange(node_map)) {
    sorted_node_map.push_back(key);
  }
  for (const auto& [key, unused] : KeySortedRange(unordered_map)) {
    sorted_unordered_map.push_back(key);
  }
  EXPECT_EQ(sorted_keys, sorted_flat_map);
  EXPECT_EQ(sorted_keys, sorted_node_map);
  EXPECT_EQ(sorted_keys, sorted_unordered_map);
}

TEST(SortedRangeTest, KeySortedRangeIsIdempotent) {
  constexpr int kNumEntries = 20;
  // Incomparable has no operator<
  struct Incomparable {
    int value;
  };

  std::vector<int> sorted_keys;
  absl::flat_hash_map<int, Incomparable> flat_map;

  for (int i = 0; i < kNumEntries; ++i) {
    flat_map[i] = {i};
    sorted_keys.push_back(i);
  }

  auto sorted_range = KeySortedRange(flat_map);

  std::vector<int> doubly_sorted_keys;
  for (const auto& [key, unused] : KeySortedRange(sorted_range)) {
    doubly_sorted_keys.push_back(key);
  }
  EXPECT_EQ(sorted_keys, doubly_sorted_keys);
}

TEST(SortedRangeTest, MapWithExplicitCompare) {
  constexpr int kNumEntries = 10;
  const std::string numbers[kNumEntries] = {"one",  "two", "three", "four",
                                            "five", "six", "seven", "eight",
                                            "nine", "ten"};

  absl::flat_hash_map<int, std::string> map;
  for (int i = 0; i < kNumEntries; ++i) {
    map[i] = numbers[i];
  }

  // Iterate on the order of the second field (alphabetically)
  std::vector<std::string> elems;
  const auto range = SortedRange(map, gtl::OrderBySecond());
  for (const auto& e : range) {
    elems.push_back(e.second);
  }
  EXPECT_THAT(elems, ElementsAre("eight", "five", "four", "nine", "one",
                                 "seven", "six", "ten", "three", "two"));
}

TEST(SortedRangeTest, MultiMap) {
  std::unordered_multimap<int, std::string> numbers = {
      {1, "one"},   {1, "un"},    {2, "two"},  {2, "deux"},
      {3, "three"}, {3, "trois"}, {4, "four"}, {4, "quatre"}};

  // Update the input in sorted order.
  std::vector<std::pair<int, std::string>> res;
  for (auto& e : SortedRange(numbers)) {
    res.emplace_back(e);
  }

  // Check that we correctly updated the input.
  EXPECT_THAT(res, testing::ElementsAre(
                       testing::Pair(1, "one"), testing::Pair(1, "un"),
                       testing::Pair(2, "deux"), testing::Pair(2, "two"),
                       testing::Pair(3, "three"), testing::Pair(3, "trois"),
                       testing::Pair(4, "four"), testing::Pair(4, "quatre")));
}

TEST(SortedRangeTest, MutableVector) {
  std::vector<std::pair<int, std::string>> input = {
      {10, "ten"},
      {4, "four"},
      {8, "eight"},
  };

  // Update the input in sorted order.
  int row_number = 1;
  for (auto& e : SortedRange(input)) {
    e.second = absl::StrCat(row_number++, ":", e.second);
  }

  // Check that we correctly updated the input.
  EXPECT_THAT(input, ElementsAre(Pair(10, "3:ten"), Pair(4, "1:four"),
                                 Pair(8, "2:eight")));
}

TEST(SortedRangeTest, MutableMap) {
  absl::flat_hash_map<int, std::string> input;
  input[10] = "ten";
  input[4] = "four";
  input[8] = "eight";

  // Update the input in sorted order.
  int row_number = 1;
  for (auto& e : SortedRange(input)) {
    e.second = absl::StrCat(row_number++, ":", e.second);
  }

  // Check that we correctly updated the input.
  EXPECT_THAT(input, UnorderedElementsAre(Pair(4, "1:four"), Pair(8, "2:eight"),
                                          Pair(10, "3:ten")));
}

TEST(SortedRangeTest, IteratorReturnTypes) {
  {
    int arr[] = {4};
    auto sorted_range = StableSortedRange(arr);
    EXPECT_TRUE((std::is_same_v<decltype(*sorted_range.begin()),
                                decltype(*std::begin(arr))>));
    // Tests begin() const;
    EXPECT_TRUE((std::is_same_v<decltype(*std::as_const(sorted_range).begin()),
                                decltype(*std::begin(std::as_const(arr)))>));
    EXPECT_TRUE((std::is_same_v<decltype(*sorted_range.cbegin()),
                                decltype(*std::cbegin(arr))>));
    EXPECT_TRUE((std::is_same_v<decltype(*sorted_range.end()),
                                decltype(*std::end(arr))>));
    // Tests end() const;
    EXPECT_TRUE((std::is_same_v<decltype(*std::as_const(sorted_range).end()),
                                decltype(*std::end(std::as_const(arr)))>));
    EXPECT_TRUE((std::is_same_v<decltype(*sorted_range.cend()),
                                decltype(*std::cend(arr))>));
    EXPECT_TRUE((std::is_same_v<decltype(*sorted_range.rbegin()),
                                decltype(*std::rbegin(arr))>));
    // Tests rbegin() const;
    EXPECT_TRUE((std::is_same_v<decltype(*std::as_const(sorted_range).rbegin()),
                                decltype(*std::rbegin(std::as_const(arr)))>));
    EXPECT_TRUE((std::is_same_v<decltype(*sorted_range.crbegin()),
                                decltype(*std::crbegin(arr))>));
    EXPECT_TRUE((std::is_same_v<decltype(*sorted_range.rend()),
                                decltype(*std::rend(arr))>));
    // Tests rend() const;
    EXPECT_TRUE((std::is_same_v<decltype(*std::as_const(sorted_range).rend()),
                                decltype(*std::rend(std::as_const(arr)))>));
    EXPECT_TRUE((std::is_same_v<decltype(*sorted_range.crend()),
                                decltype(*std::crend(arr))>));
  }
  {
    absl::flat_hash_set<int> set = {1};
    const auto sorted_range = SortedRange(set);
    EXPECT_TRUE((std::is_same_v<decltype(*sorted_range.begin()),
                                decltype(*set.begin())>));
    EXPECT_TRUE((std::is_same_v<decltype(*sorted_range.cbegin()),
                                decltype(*set.cbegin())>));
    // reverse iteration is not defined for absl::flat_hash_set.
  }
  {
    absl::flat_hash_map<int, int> map;
    auto sorted_map = SortedRange(map);
    EXPECT_TRUE((
        std::is_same_v<decltype(*sorted_map.begin()), decltype(*map.begin())>));
    EXPECT_TRUE((std::is_same_v<decltype(*sorted_map.cbegin()),
                                decltype(*map.cbegin())>));
    // reverse iteration is not defined for absl::flat_hash_map.
  }
}

}  // namespace
}  // namespace gtl::labs
