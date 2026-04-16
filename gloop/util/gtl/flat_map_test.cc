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

#include "gloop/util/gtl/flat_map.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/container/inlined_vector.h"
#include "absl/container/internal/test_instance_tracker.h"
#include "absl/hash/hash_testing.h"
#include "absl/log/check.h"
#include "absl/meta/internal/constexpr_testing.h"
#include "absl/meta/type_traits.h"
#include "absl/random/random.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/source_location.h"
#include "absl/types/span.h"
#include "benchmark/benchmark.h"
#include "gloop/util/gtl/stl_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace gtl {
namespace {

using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::Pair;
using ::testing::StrEq;

// This ensures that we don't depend on operators other than <.
struct OnlyLT {
  int i;
  OnlyLT() = default;
  explicit OnlyLT(int new_i) : i(new_i) {}
  bool operator<(const OnlyLT& other) const { return i < other.i; }
};

struct ReverseCmp {
  bool operator()(OnlyLT x, OnlyLT y) const { return x.i > y.i; }
};

// Get an integer from a few supported types. Used as a test helper, as well as
// for a transparent comparator.
int ToInt(const OnlyLT& x) { return x.i; }
int ToInt(const std::unique_ptr<int>& x) { return *x; }
int ToInt(const std::shared_ptr<int>& x) { return *x; }
template <typename T>
int ToInt(T t) {
  return t;
}

// Allows comparing int to unique_ptr<int>.
struct TransparentCmp {
  template <typename T, typename U>
  bool operator()(const T& t, const U& u) const {
    return ToInt(t) < ToInt(u);
  }

  using is_transparent = void;
};

struct NonTransparent {
  template <typename T, typename U>
  bool operator()(const T& t, const U& u) const {
    // Treating all comparators as transparent can cause inefficiencies (see
    // N3657 C++ proposal). Test that for comparators without 'is_transparent'
    // typedef (like this one), we do not attempt heterogeneous lookup.
    EXPECT_TRUE((std::is_same<T, U>()));
    return t < u;
  }
};

std::vector<std::pair<const int, std::unique_ptr<int>>> UniquePtrs(
    absl::Span<const int> v) {
  std::vector<std::pair<const int, std::unique_ptr<int>>> res;
  for (int i : v) {
    res.emplace_back(i, std::make_unique<int>(i));
  }
  return res;
}

std::vector<std::pair<const int, std::unique_ptr<int>>> UniquePtrs(
    absl::Span<const std::pair<int, int>> v) {
  std::vector<std::pair<const int, std::unique_ptr<int>>> res;
  for (const auto& [key, value] : v) {
    res.emplace_back(key, std::make_unique<int>(value));
  }
  return res;
}

// Test expectation methods.
template <typename It>
void ExpectElements(
    It begin, It end, const std::vector<std::pair<int, int>>& expected,
    absl::SourceLocation loc = absl::SourceLocation::current()) {
  std::vector<std::pair<int, int>> actual;
  for (; begin != end; ++begin) {
    actual.push_back({ToInt(begin->first), ToInt(begin->second)});
  }

  EXPECT_THAT(actual, ElementsAreArray(expected))
      << absl::StrFormat("At %s:%d", loc.file_name(), loc.line());
}

template <typename Map>
void ExpectElements(
    const Map& map, const std::vector<std::pair<int, int>>& expected,
    absl::SourceLocation loc = absl::SourceLocation::current()) {
  ExpectElements(map.begin(), map.end(), expected, loc);
}

template <typename It>
void ExpectStringElements(
    It begin, It end, const std::vector<std::pair<int, std::string>>& expected,
    absl::SourceLocation loc = absl::SourceLocation::current()) {
  std::vector<std::pair<int, std::string>> actual;
  for (; begin != end; ++begin) {
    actual.push_back({ToInt(begin->first), begin->second});
  }

  EXPECT_THAT(actual, ElementsAreArray(expected))
      << absl::StrFormat("At %s:%d", loc.file_name(), loc.line());
}

template <typename Map>
void ExpectStringElements(
    const Map& map, const std::vector<std::pair<int, std::string>>& expected,
    absl::SourceLocation loc = absl::SourceLocation::current()) {
  ExpectStringElements(map.begin(), map.end(), expected, loc);
}

TEST(FlatMapTest, DefaultIsValid) {
  flat_map<OnlyLT, int> m;
  EXPECT_TRUE(m.empty());
  EXPECT_EQ(0, m.size());
  EXPECT_EQ(m.end(), m.find(OnlyLT()));
  EXPECT_EQ(m.end(), m.lower_bound(OnlyLT()));
  EXPECT_EQ(m.end(), m.upper_bound(OnlyLT()));
}

TEST(FlatMapTest, MovableNoExcept) {
  EXPECT_TRUE((std::is_nothrow_move_constructible<flat_map<int, int>>::value));
  EXPECT_TRUE((std::is_nothrow_move_assignable<flat_map<int, int>>::value));

  struct LessWithThrowingCopy : std::less<int> {
    LessWithThrowingCopy(const LessWithThrowingCopy&) {}
  };
  EXPECT_TRUE((std::is_nothrow_move_constructible<
               flat_map<int, int, LessWithThrowingCopy>>::value));

  using NoClearNoExceptCopy = std::array<std::pair<int, int>, 3>;
  EXPECT_TRUE((std::is_nothrow_move_constructible<
               flat_map<int, int, std::less<>, NoClearNoExceptCopy>>::value));

  struct NoClearThrowingCopy : std::array<std::pair<int, int>, 3> {
    NoClearThrowingCopy(const NoClearThrowingCopy&) {}
  };
  EXPECT_TRUE((std::is_nothrow_move_constructible<
               flat_map<int, int, std::less<>, NoClearThrowingCopy>>::value));
}

TEST(FlatMapTest, RangeAndListConstruction) {
  std::vector<std::pair<OnlyLT, int>> v{
      {OnlyLT(4), 4}, {OnlyLT(9), 9}, {OnlyLT(1), 1}};
  std::vector<std::pair<int, int>> expected = {{1, 1}, {4, 4}, {9, 9}};

  ExpectElements(flat_map<OnlyLT, int>(v.begin(), v.end()), expected);
  ExpectElements(flat_map<OnlyLT, int>(v.rbegin(), v.rend()), expected);
  ExpectElements(
      flat_map<OnlyLT, int>({{OnlyLT(4), 4}, {OnlyLT(9), 9}, {OnlyLT(1), 1}}),
      expected);
}

TEST(FlatMapTest, ContainerMoveConstruction) {
  std::vector<std::pair<int, int>> v = {{4, 4}, {9, 9}, {4, 1}, {1, 1}};
  std::vector<std::pair<int, int>> expected = {{1, 1}, {4, 4}, {9, 9}};

  ExpectElements(
      flat_map<int, std::unique_ptr<int>, TransparentCmp>(UniquePtrs(v)),
      expected);
}

TEST(FlatMapTest, MoveRep) {
  flat_map<int, int> s = {{4, 4}, {9, 9}, {1, 1}, {4, 4}};
  std::vector<std::pair<const int, int>> rep = std::move(s).ExtractRep();
  EXPECT_THAT(s, IsEmpty());  // NOLINT(bugprone-use-after-move)
  EXPECT_THAT(rep, ElementsAre(Pair(1, 1), Pair(4, 4), Pair(9, 9)));
}

TEST(FlatMapTest, ExplicitComparator) {
  flat_map<OnlyLT, int, ReverseCmp> m = {
      {OnlyLT(4), 4}, {OnlyLT(9), 9}, {OnlyLT(1), 1}};
  ExpectElements(m, {{9, 9}, {4, 4}, {1, 1}});
  m.insert({OnlyLT(5), 5});
  ExpectElements(m, {{9, 9}, {5, 5}, {4, 4}, {1, 1}});
}

TEST(FlatMapTest, Iterators) {
  flat_map<OnlyLT, int> m = {{OnlyLT(4), 4}, {OnlyLT(9), 9}, {OnlyLT(1), 1}};
  ExpectElements(m.begin(), m.end(), {{1, 1}, {4, 4}, {9, 9}});
  ExpectElements(m.cbegin(), m.cend(), {{1, 1}, {4, 4}, {9, 9}});
  ExpectElements(m.rbegin(), m.rend(), {{9, 9}, {4, 4}, {1, 1}});
  ExpectElements(m.crbegin(), m.crend(), {{9, 9}, {4, 4}, {1, 1}});

  const flat_map<OnlyLT, int>& c = m;
  ExpectElements(c.begin(), c.end(), {{1, 1}, {4, 4}, {9, 9}});
  ExpectElements(c.rbegin(), c.rend(), {{9, 9}, {4, 4}, {1, 1}});
}

TEST(FlatMapTest, InsertHandlesDuplicatesCorrectly) {
  flat_map<OnlyLT, int> m;
  auto p = m.insert({OnlyLT(10), 10});
  EXPECT_THAT(p, Pair(m.begin(), true));
  p = m.insert({OnlyLT(10), 9});
  EXPECT_THAT(p, Pair(m.begin(), false));
  p = m.insert({OnlyLT(5), 10});
  EXPECT_THAT(p, Pair(m.begin(), true));
  p = m.insert({OnlyLT(5), 1});
  EXPECT_THAT(p, Pair(m.begin(), false));
  ExpectElements(m, {{5, 10}, {10, 10}});
}

TEST(FlatMapTest, InsertMaintainsSortedOrder) {
  flat_map<OnlyLT, int> m;
  std::pair<OnlyLT, int> lt5 = {OnlyLT(5), 7};

  // Test both lvalue & rvalue insert.
  m.insert({OnlyLT(10), 10});
  m.insert(lt5);
  EXPECT_EQ(2, m.size());
  EXPECT_EQ(2, m.end() - m.begin());
  EXPECT_TRUE(std::is_sorted(m.begin(), m.end()));

  OnlyLT lt100{100};
  m.insert({lt100, 100});
  m.insert({OnlyLT(1), 1});
  EXPECT_EQ(4, m.size());
  EXPECT_TRUE(std::is_sorted(m.begin(), m.end()));
}

TEST(FlatMapTest, InsertInitializerList) {
  flat_map<OnlyLT, int> m;
  m.insert({{OnlyLT(7), 3}, {OnlyLT(3), 7}});
  ExpectElements(m, {{3, 7}, {7, 3}});
  m.insert({{OnlyLT(4), 4}, {OnlyLT(2), 2}, {OnlyLT(6), 6}});
  ExpectElements(m, {{2, 2}, {3, 7}, {4, 4}, {6, 6}, {7, 3}});
}

TEST(FlatMapTest, InsertWithHintWorks) {
  flat_map<OnlyLT, int> m;
  // Empty, insert with begin hint:
  m.insert(m.begin(), {OnlyLT(1), 1});
  EXPECT_EQ(1, m.size());
  m.clear();
  // Empty, insert with end hint:
  std::pair<OnlyLT, int> lt1 = {OnlyLT(1), 1};
  m.insert(m.end(), lt1);
  EXPECT_EQ(1, m.size());
  // Insert with correct hint:
  m.insert(m.end(), {OnlyLT(2), 2});
  EXPECT_EQ(2, m.size());
  // Insert value already present, ensure no duplicate added:
  auto it = m.insert(m.end(), {OnlyLT(2), 2});
  EXPECT_EQ(2, m.size());
  // Verify that the iterator returned is accurate.
  EXPECT_TRUE(m.find(OnlyLT(2)) == it);
  m.erase(OnlyLT(2));
  EXPECT_EQ(1, m.size());
  // Insertion in the middle of two other elements.
  it = m.insert({OnlyLT(3), 3}).first;
  m.insert(it, {OnlyLT(2), 2});
  EXPECT_EQ(3, m.size());
  EXPECT_TRUE(std::is_sorted(m.begin(), m.end()));
}

TEST(FlatMapTest, InsertWithBadHint) {
  flat_map<OnlyLT, int> m = {{OnlyLT(1), 1}, {OnlyLT(9), 9}};
  // Bad hint (too small), should still insert:
  auto it = m.insert(m.begin(), {OnlyLT(2), 2});
  EXPECT_EQ(it, m.begin() + 1);
  ExpectElements(m, {{1, 1}, {2, 2}, {9, 9}});
  // Bad hint, too large this time:
  it = m.insert(m.begin() + 2, {OnlyLT(0), 0});
  EXPECT_EQ(it, m.begin());
  ExpectElements(m, {{0, 0}, {1, 1}, {2, 2}, {9, 9}});
}

TEST(FlatMapTest, IteratorInsertWorks) {
  flat_map<OnlyLT, int> m;
  std::vector<std::pair<OnlyLT, int>> v = {
      {OnlyLT(1), 1}, {OnlyLT(3), 3}, {OnlyLT(2), 1}, {OnlyLT(2), 0}};
  m.insert(v.begin(), v.end());
  ExpectElements(m, {{1, 1}, {2, 1}, {3, 3}});
}

TEST(FlatMapTest, InsertRangeWorks) {
  flat_map<OnlyLT, int> m;
  m.insert({OnlyLT(1), 1});
  m.insert({OnlyLT(6), 6});
  std::vector<std::pair<OnlyLT, int>> to_insert;
  // Duplicates...
  for (int i = 0; i < 6; ++i) {
    to_insert.push_back({OnlyLT(i), i});
    to_insert.push_back({OnlyLT(i), -i});
  }
  // And unsorted!
  absl::BitGen bitgen;
  std::shuffle(to_insert.begin(), to_insert.end(), bitgen);
  m.insert(to_insert.begin(), to_insert.end());
  EXPECT_EQ(7, m.size());
  for (int i = 0; i <= 6; ++i) {
    EXPECT_EQ(1, m.count(OnlyLT(i))) << "Missing " << i;
  }
}

TEST(FlatMapTest, InsertRangeUsesFirstValue) {
  // Range constructor chooses first copy of equivalent elements.
  flat_map<int, int> m = {{1, 1}, {1, 0}, {2, 2}, {2, 1},
                          {1, 7}, {1, 1}, {2, 9}};
  std::vector<std::pair<int, int>> expected = {{1, 1}, {2, 2}};
  EXPECT_THAT(m, ElementsAreArray(expected));
  // When inserting a range, the same property holds among new elements.
  m.insert({{0, 0}, {4, 4}, {0, 9}, {4, 4}});
  expected = {{0, 0}, {1, 1}, {2, 2}, {4, 4}};
  EXPECT_THAT(m, ElementsAreArray(expected));
  // Elements already in a map take precedence over the new ones inserted.
  m.insert({{0, -1}, {0, 99}, {1, 9}, {3, 3}, {1, -7}, {4, 5}, {3, 1}, {3, 7}});
  expected = {{0, 0}, {1, 1}, {2, 2}, {3, 3}, {4, 4}};
  EXPECT_THAT(m, ElementsAreArray(expected));
}

TEST(FlatMapTest, InsertOrAssignHandlesDuplicatesCorrectly) {
  flat_map<OnlyLT, int> m;
  auto p = m.insert_or_assign(OnlyLT(10), 10);
  EXPECT_THAT(p, Pair(m.begin(), true));
  p = m.insert_or_assign(OnlyLT(10), 9);
  EXPECT_THAT(p, Pair(m.begin(), false));
  p = m.insert_or_assign(OnlyLT(5), 10);
  EXPECT_THAT(p, Pair(m.begin(), true));
  p = m.insert_or_assign(OnlyLT(5), 1);
  EXPECT_THAT(p, Pair(m.begin(), false));
  ExpectElements(m, {{5, 1}, {10, 9}});
}

TEST(FlatMapTest, InsertOrAssignWithTypeConversion) {
  flat_map<int, std::string> m;
  auto p = m.insert_or_assign(1, "hello, world");
  EXPECT_THAT(p, Pair(m.begin(), true));
  EXPECT_EQ(m[1], std::string("hello, world"));
}

TEST(FlatMapTest, InsertOrAssignMaintainsSortedOrder) {
  flat_map<OnlyLT, int> m;
  std::pair<OnlyLT, int> lt5 = {OnlyLT(5), 7};

  // Test both lvalue & rvalue insert.
  m.insert_or_assign(OnlyLT(10), 10);
  m.insert_or_assign(lt5.first, lt5.second);
  EXPECT_EQ(2, m.size());
  EXPECT_EQ(2, m.end() - m.begin());
  EXPECT_TRUE(std::is_sorted(m.begin(), m.end()));

  OnlyLT lt100{100};
  m.insert_or_assign(lt100, 100);
  m.insert_or_assign(OnlyLT(1), 1);
  EXPECT_EQ(4, m.size());
  EXPECT_TRUE(std::is_sorted(m.begin(), m.end()));
}

TEST(FlatMapTest, TryEmplaceBasicTest) {
  flat_map<OnlyLT, std::string> m;

  // Should construct a string from the literal.
  m.try_emplace(OnlyLT(1), "one");
  EXPECT_EQ(1, m.size());

  // Try other string constructors and const lvalue key.
  const OnlyLT key(42);
  m.try_emplace(key, 3, 'a');
  m.try_emplace(OnlyLT(2), std::string("two"));

  EXPECT_TRUE(std::is_sorted(m.begin(), m.end()));
  ExpectStringElements(m, {{1, "one"}, {2, "two"}, {42, "aaa"}});
}

TEST(FlatMapTest, TryEmplaceWithHintWorks) {
  // Use a counting comparator here to verify that hint is used.
  int calls = 0;
  auto cmp = [&calls](OnlyLT x, OnlyLT y) {
    ++calls;
    return x < y;
  };
  using Cmp = decltype(cmp);

  flat_map<OnlyLT, int, Cmp> m(cmp);
  for (int i = 0; i < 128; i++) {
    m.emplace(OnlyLT(i), i);
  }

  // Check for the comparator
  calls = 0;
  m.emplace(OnlyLT(127), 127);
  EXPECT_GT(calls, 5);

  // Try with begin hint:
  calls = 0;
  auto it = m.try_emplace(m.begin(), OnlyLT(-1), -1);
  EXPECT_EQ(129, m.size());
  EXPECT_EQ(it, m.begin());
  EXPECT_LE(calls, 2);

  // Try with end hint:
  calls = 0;
  std::pair<OnlyLT, int> lt1 = {OnlyLT(1024), 1024};
  it = m.try_emplace(m.end(), lt1.first, lt1.second);
  EXPECT_EQ(130, m.size());
  EXPECT_EQ(it, m.end() - 1);
  EXPECT_LE(calls, 2);

  // Try value already present, bad hint; ensure no duplicate added:
  calls = 0;
  it = m.try_emplace(m.end(), OnlyLT(16), 17);
  EXPECT_EQ(130, m.size());
  EXPECT_GT(calls, 5);
  EXPECT_EQ(it, m.find(OnlyLT(16)));

  // Try value already present, hint points directly to it:
  calls = 0;
  it = m.try_emplace(it, OnlyLT(16), 17);
  EXPECT_EQ(130, m.size());
  EXPECT_LE(calls, 2);
  EXPECT_EQ(it, m.find(OnlyLT(16)));

  // Try value already present, hint points right above it:
  calls = 0;
  it = m.try_emplace(it + 1, OnlyLT(16), 17);
  EXPECT_EQ(130, m.size());
  EXPECT_LE(calls, 3);
  EXPECT_EQ(it, m.find(OnlyLT(16)));

  m.erase(OnlyLT(2));
  EXPECT_EQ(129, m.size());
  auto hint = m.find(OnlyLT(3));
  // Try emplace in the middle of two other elements.
  calls = 0;
  m.try_emplace(hint, OnlyLT(2), 2);
  EXPECT_EQ(130, m.size());
  EXPECT_LE(calls, 2);

  EXPECT_TRUE(std::is_sorted(m.begin(), m.end()));
}

TEST(FlatMapTest, TryEmplaceWithBadHint) {
  flat_map<OnlyLT, int> m = {{OnlyLT(1), 1}, {OnlyLT(9), 9}};

  // Bad hint (too small), should still emplace:
  auto it = m.try_emplace(m.begin(), OnlyLT(2), 2);
  EXPECT_EQ(it, m.begin() + 1);
  ExpectElements(m, {{1, 1}, {2, 2}, {9, 9}});

  // Bad hint, too large this time:
  it = m.try_emplace(m.begin() + 2, OnlyLT(0), 0);
  EXPECT_EQ(it, m.begin());
  ExpectElements(m, {{0, 0}, {1, 1}, {2, 2}, {9, 9}});
}

TEST(FlatMapTest, TryEmplaceMaintainsSortedOrder) {
  flat_map<OnlyLT, std::string> m;
  std::pair<OnlyLT, std::string> lt5 = {OnlyLT(5), "five"};

  // Test both lvalue & rvalue emplace.
  m.try_emplace(OnlyLT(10), "ten");
  m.try_emplace(lt5.first, lt5.second);
  EXPECT_EQ(2, m.size());
  EXPECT_EQ(2, m.end() - m.begin());
  EXPECT_TRUE(std::is_sorted(m.begin(), m.end()));

  OnlyLT lt100{100};
  m.try_emplace(lt100, "hundred");
  m.try_emplace(OnlyLT(1), "one");
  EXPECT_EQ(4, m.size());
  EXPECT_TRUE(std::is_sorted(m.begin(), m.end()));
}

TEST(FlatMapTest, TryEmplaceWithHintAndNoValueArgsWorks) {
  flat_map<int, int> m;
  m.try_emplace(m.end(), 1);
  EXPECT_EQ(0, m[1]);
}

TEST(FlatMapTest, TryEmplaceWithHintAndMultipleValueArgsWorks) {
  flat_map<int, std::string> m;
  m.try_emplace(m.end(), 1, 10, 'a');
  EXPECT_EQ(std::string(10, 'a'), m[1]);
}

TEST(FlatMapTest, InsertOrAssignWithHintWorks) {
  flat_map<OnlyLT, int> m;
  // Empty, insert with begin hint:
  m.insert_or_assign(m.begin(), OnlyLT(1), 1);
  EXPECT_EQ(1, m.size());
  m.clear();
  // Empty, insert with end hint:
  std::pair<OnlyLT, int> lt1 = {OnlyLT(1), 1};
  m.insert_or_assign(m.end(), lt1.first, lt1.second);
  EXPECT_EQ(1, m.size());
  // Insert with correct hint:
  m.insert_or_assign(m.end(), OnlyLT(2), 2);
  EXPECT_EQ(2, m.size());
  // Insert value already present, ensure no duplicate added:
  auto it = m.insert_or_assign(m.end(), OnlyLT(2), 2);
  EXPECT_EQ(2, m.size());
  // Verify that the iterator returned is accurate.
  EXPECT_TRUE(m.find(OnlyLT(2)) == it);
  m.erase(OnlyLT(2));
  EXPECT_EQ(1, m.size());
  // Insertion in the middle of two other elements.
  it = m.insert_or_assign(OnlyLT(3), 3).first;
  m.insert_or_assign(it, OnlyLT(2), 2);
  EXPECT_EQ(3, m.size());
  EXPECT_TRUE(std::is_sorted(m.begin(), m.end()));
}

TEST(FlatMapTest, InsertOrAssignWithBadHint) {
  flat_map<OnlyLT, int> m = {{OnlyLT(1), 1}, {OnlyLT(9), 9}};
  // Bad hint (too small), should still insert:
  auto it = m.insert_or_assign(m.begin(), OnlyLT(2), 2);
  EXPECT_EQ(it, m.begin() + 1);
  ExpectElements(m, {{1, 1}, {2, 2}, {9, 9}});
  // Bad hint, too large this time:
  it = m.insert_or_assign(m.begin() + 2, OnlyLT(0), 0);
  EXPECT_EQ(it, m.begin());
  ExpectElements(m, {{0, 0}, {1, 1}, {2, 2}, {9, 9}});
}

TEST(FlatMapTest, Emplace) {
  flat_map<OnlyLT, int> m;
  auto result = m.emplace(OnlyLT(1), 1);
  EXPECT_EQ(result.first, m.begin());
  EXPECT_TRUE(result.second);
  result = m.emplace(OnlyLT(9), 9);
  EXPECT_EQ(result.first, m.begin() + 1);
  EXPECT_TRUE(result.second);
  ExpectElements(m, {{1, 1}, {9, 9}});

  result = m.emplace(OnlyLT(9), 7);
  EXPECT_EQ(result.first, m.begin() + 1);
  EXPECT_FALSE(result.second);
  ExpectElements(m, {{1, 1}, {9, 9}});

  // Basic test of emplace_hint; the more extensive test for insert with hint
  // (which emplace_hint calls) exists above.
  auto it = m.emplace_hint(m.end(), OnlyLT(10), 10);
  EXPECT_EQ(it, m.begin() + 2);
  ExpectElements(m, {{1, 1}, {9, 9}, {10, 10}});
  // Bad hint, still inserts.
  it = m.emplace_hint(m.end(), OnlyLT(0), 0);
  EXPECT_EQ(it, m.begin());
  ExpectElements(m, {{0, 0}, {1, 1}, {9, 9}, {10, 10}});
  // Correct hint in the middle:
  it = m.emplace_hint(m.begin() + 2, OnlyLT(7), 7);
  EXPECT_EQ(it, m.begin() + 2);
  ExpectElements(m, {{0, 0}, {1, 1}, {7, 7}, {9, 9}, {10, 10}});
  // Element exists.
  it = m.emplace_hint(m.begin() + 3, OnlyLT(7), 7);
  EXPECT_EQ(it, m.begin() + 2);
  ExpectElements(m, {{0, 0}, {1, 1}, {7, 7}, {9, 9}, {10, 10}});
}

TEST(FlatMapTest, EraseWorks) {
  flat_map<OnlyLT, int> m = {
      {OnlyLT(4), 4}, {OnlyLT(9), 9}, {OnlyLT(1), 1}, {OnlyLT(17), 17}};
  EXPECT_EQ(0, m.erase(OnlyLT(0)));
  EXPECT_EQ(1, m.erase(OnlyLT(4)));
  ExpectElements(m, {{1, 1}, {9, 9}, {17, 17}});

  auto it = m.erase(m.begin() + 1);
  EXPECT_EQ(it, m.begin() + 1);
  ExpectElements(m, {{1, 1}, {17, 17}});
}

TEST(FlatMapTest, RangeEraseWorks) {
  flat_map<OnlyLT, int> m = {
      {OnlyLT(4), 4}, {OnlyLT(9), 9}, {OnlyLT(1), 1}, {OnlyLT(17), 17}};
  auto it = m.erase(m.begin() + 1, m.begin() + 3);
  EXPECT_EQ(it, m.begin() + 1);
  ExpectElements(m, {{1, 1}, {17, 17}});
  // Empty range.
  it = m.erase(m.begin() + 1, m.begin() + 1);
  EXPECT_EQ(it, m.begin() + 1);
  ExpectElements(m, {{1, 1}, {17, 17}});
}

TEST(FlatMapTest, RangeEraseWorksNonTrivialValue) {
  flat_map<int, std::string> m = {{4, "4"}, {9, "9"}, {1, "1"}, {17, "17"}};
  auto it = m.erase(m.begin() + 1, m.begin() + 3);
  EXPECT_EQ(it, m.begin() + 1);
  EXPECT_THAT(m, ElementsAre(Pair(1, "1"), Pair(17, "17")));
  // Empty range.
  it = m.erase(m.begin() + 1, m.begin() + 1);
  EXPECT_EQ(it, m.begin() + 1);
  EXPECT_THAT(m, ElementsAre(Pair(1, "1"), Pair(17, "17")));
}

TEST(FlatMapTest, RemoveIfWorks) {
  flat_map<OnlyLT, int> s = {
      {OnlyLT(4), 4}, {OnlyLT(9), 9}, {OnlyLT(2), 2}, {OnlyLT(17), 17}};
  size_t n_removed =
      s.remove_if([](std::pair<OnlyLT, int> x) { return x.second % 2 == 0; });
  EXPECT_EQ(n_removed, 2);
  ExpectElements(s, {{9, 9}, {17, 17}});
}

TEST(FlatMapTest, RemoveIfIsLinear) {
  using ::absl::test_internal::CopyableMovableInstance;
  using ::absl::test_internal::InstanceTracker;

  using FlatMap = gtl::flat_map<int, CopyableMovableInstance>;

  struct TestResult {
    int num_assignments;
    int num_compares;
  };

  struct Predicate {
    bool operator()(const typename FlatMap::value_type& v) {
      ++*invocations;
      return v.second.value() % 2 != 0;
    }
    int* invocations;
  };

  auto test = [](size_t sz) {
    InstanceTracker tracker;
    FlatMap::container_type rep;
    std::generate_n(std::back_inserter(rep), sz, [i = 0]() mutable {
      return ++i, std::make_pair(i, CopyableMovableInstance(i));
    });
    FlatMap m(sorted_unique_container, std::move(rep));
    TestResult result = {};
    Predicate p = {.invocations = &result.num_compares};
    tracker.ResetCopiesMovesSwaps();
    m.remove_if(p);
    result.num_assignments = tracker.copies() + tracker.moves();
    return result;
  };

  auto result100 = test(100);
  auto result1000 = test(1000);
  auto result10000 = test(10000);

  // Predicate is called for every element.
  EXPECT_EQ(result100.num_compares, 100);
  EXPECT_EQ(result1000.num_compares, 1000);
  EXPECT_EQ(result10000.num_compares, 10000);

  // Complexity is expected to grow linearly.
  //
  // In this specific case the number of assignments is precisely N/2, but
  // in a general case it can be any number up to N-1.
  EXPECT_DOUBLE_EQ(static_cast<double>(result1000.num_assignments) /
                       result100.num_assignments,
                   static_cast<double>(result10000.num_assignments) /
                       result1000.num_assignments);
}

TEST(FlatMapTest, FindWorks) {
  OnlyLT good = OnlyLT(10);
  OnlyLT bad = OnlyLT(13);
  flat_map<OnlyLT, int> m = {{good, 42}};
  const auto& c = m;
  EXPECT_EQ(m.begin(), m.find(good));
  EXPECT_EQ(m.end(), m.find(bad));
  EXPECT_EQ(c.cbegin(), c.find(good));
  EXPECT_EQ(c.cend(), c.find(bad));
}

TEST(FlatMapTest, IndexOperatorWorks) {
  flat_map<OnlyLT, int> m;
  OnlyLT k1 = OnlyLT(1);
  OnlyLT k10 = OnlyLT(10);
  EXPECT_EQ(0, m[k10]);
  EXPECT_EQ(1, m.size());
  EXPECT_EQ(1, ++m[k10]);
  EXPECT_EQ(1, m.size());

  EXPECT_EQ(0, m[k1]);
  EXPECT_EQ(1, ++m[k1]);
  EXPECT_EQ(2, ++m[k1]);
  EXPECT_EQ(2, m.size());

  ExpectElements(m, {{1, 2}, {10, 1}});
}

// Helper method to cover const / non-const map.
template <typename Map>
void TestAt() {
  Map m = {{OnlyLT(1), 1}};
  EXPECT_EQ(1, m.at(OnlyLT(1)));
#ifdef ABSL_HAVE_EXCEPTIONS
  try {
    m.at(OnlyLT(2));
    FAIL() << "Exception not thrown";
  } catch (const std::out_of_range& e) {
    EXPECT_STREQ(e.what(), "flat_map::at");
  }
#else
  EXPECT_DEATH(m.at(OnlyLT(2)), "flat_map::at");
#endif
}

TEST(FlatMapTest, AtWorks) {
  TestAt<flat_map<OnlyLT, int>>();
  TestAt<const flat_map<OnlyLT, int>>();
}

// Helper method to cover const / non-const map.
template <typename Map>
void TestBinarySearches() {
  Map m = {{OnlyLT(1), 1}, {OnlyLT(3), 3}, {OnlyLT(5), 5}};
  EXPECT_EQ(m.lower_bound(OnlyLT(3)), m.begin() + 1);
  EXPECT_EQ(m.upper_bound(OnlyLT(3)), m.begin() + 2);
  EXPECT_EQ(m.lower_bound(OnlyLT(4)), m.begin() + 2);
  EXPECT_EQ(m.upper_bound(OnlyLT(4)), m.begin() + 2);

  EXPECT_THAT(m.equal_range(OnlyLT(3)), Pair(m.begin() + 1, m.begin() + 2));
  EXPECT_THAT(m.equal_range(OnlyLT(4)), Pair(m.begin() + 2, m.begin() + 2));
}

TEST(FlatMapTest, BinarySearchesWork) {
  TestBinarySearches<flat_map<OnlyLT, int>>();
  TestBinarySearches<const flat_map<OnlyLT, int>>();
}

TEST(FlatMapTest, CopyAndAssignmentWork) {
  flat_map<OnlyLT, int> m;
  m.insert({OnlyLT(1), 1});
  ExpectElements(m, {{1, 1}});
  flat_map<OnlyLT, int> s2(m);
  ExpectElements(s2, {{1, 1}});
  flat_map<OnlyLT, int> s3;
  s3 = m;
  EXPECT_EQ(1, s3.size());

  // Assignment from initializer list.
  m = {{OnlyLT(7), 7}};
  ExpectElements(m, {{7, 7}});
}

TEST(FlatMapTest, CountWorks) {
  flat_map<OnlyLT, int> m;
  OnlyLT v(1);
  EXPECT_EQ(0, m.count(v));
  m.insert({v, 1});
  EXPECT_EQ(1, m.count(v));
  m.insert({v, 9});
  EXPECT_EQ(1, m.count(v));
}

TEST(FlatMapTest, ContainsWorks) {
  flat_map<OnlyLT, int> m;
  OnlyLT v(1);
  EXPECT_FALSE(m.contains(v));
  m.insert({v, 1});
  EXPECT_TRUE(m.contains(v));
  m.insert({v, 9});
  EXPECT_TRUE(m.contains(v));
}

TEST(FlatMapTest, InstantiatesWithInlinedVector) {
  flat_map<OnlyLT, int, std::less<OnlyLT>,
           absl::InlinedVector<std::pair<OnlyLT, int>, 7>>
      m;
  m.insert({OnlyLT(1), 1});
  EXPECT_EQ(1, m.size());
}

TEST(FlatMapTest, OperatorsWorkWithInlinedVector) {
  flat_map<int, int, std::less<>, absl::InlinedVector<std::pair<int, int>, 7>>
      s, s2;

  EXPECT_TRUE(s == s2);
  EXPECT_FALSE(s != s2);
  EXPECT_FALSE(s < s2);
  EXPECT_FALSE(s > s2);
  EXPECT_TRUE(s <= s2);
  EXPECT_TRUE(s >= s2);

  EXPECT_TRUE(s2 == s);
  EXPECT_FALSE(s2 != s);
  EXPECT_FALSE(s2 < s);
  EXPECT_FALSE(s2 > s);
  EXPECT_TRUE(s2 <= s);
  EXPECT_TRUE(s2 >= s);
}

TEST(FlatMapTest, RelationalOperatorsWork) {
  OnlyLT v1(1);
  OnlyLT v2(2);

  flat_map<OnlyLT, int> s1, s2;
  EXPECT_FALSE(s1 < s2);
  EXPECT_FALSE(s1 > s2);
  EXPECT_TRUE(s1 <= s2);
  EXPECT_TRUE(s1 >= s2);

  s2.insert({v1, -1});
  EXPECT_TRUE(s1 < s2);
  EXPECT_FALSE(s1 > s2);
  EXPECT_TRUE(s1 <= s2);
  EXPECT_FALSE(s1 >= s2);

  s1.insert({v2, 42});
  EXPECT_FALSE(s1 < s2);
  EXPECT_TRUE(s1 > s2);
  EXPECT_FALSE(s1 <= s2);
  EXPECT_TRUE(s1 >= s2);

  s1.insert({v1, -1});
  s2.insert({v2, 42});
  EXPECT_FALSE(s1 < s2);
  EXPECT_FALSE(s1 > s2);
  EXPECT_TRUE(s1 <= s2);
  EXPECT_TRUE(s1 >= s2);
}

TEST(FlatMapTest, ComparisonsWork) {
  flat_map<int, int> s1, s2;
  EXPECT_FALSE(s1 != s2);
  EXPECT_TRUE(s1 == s2);

  s2.insert({1, 1});
  EXPECT_TRUE(s1 != s2);
  EXPECT_FALSE(s1 == s2);

  s1.insert({2, 2});
  EXPECT_TRUE(s1 != s2);
  EXPECT_FALSE(s1 == s2);

  s1.insert({1, 1});
  s2.insert({2, 2});
  EXPECT_FALSE(s1 != s2);
  EXPECT_TRUE(s1 == s2);
}

TEST(FlatMapTest, SwapWorks) {
  flat_map<OnlyLT, int> s1 = {{OnlyLT(1), 1}};
  flat_map<OnlyLT, int> s2 = {{OnlyLT(2), 2}};
  s1.swap(s2);
  ExpectElements(s1, {{2, 2}});
  ExpectElements(s2, {{1, 1}});
  using std::swap;
  swap(s1, s2);
  ExpectElements(s1, {{1, 1}});
  ExpectElements(s2, {{2, 2}});
}

TEST(FlatMapTest, VectorExtensions) {
  flat_map<OnlyLT, int> m;
  EXPECT_EQ(0, m.capacity());
  for (int i : {1, 2, 3, 4, 5}) {
    m.insert({OnlyLT(i), i});
  }
  EXPECT_GE(m.capacity(), 5);
  m.reserve(1000);
  EXPECT_GE(m.capacity(), 1000);

  // shrink_to_fit is non-binding, but - given that one motivation for flat_map
  // is memory optimization - we would really like it to work. If we have a
  // standard library which does not honour shrink_to_fit, we should reimplement
  // it ourselves in flat_map.
  m.shrink_to_fit();
  EXPECT_EQ(m.capacity(), 5);
}

// Tests for transparent comparator (a.k.a. heterogeneous lookup).

// Helper method to cover const & non-const overloads (depending on the template
// argument).
template <typename Map>
void TestHeterogeneousLookup() {
  Map m = {{OnlyLT(3), 3}, {OnlyLT(1), 1}, {OnlyLT(5), 5}};
  EXPECT_EQ(m.begin() + 1, m.find(3));
  EXPECT_EQ(m.begin() + 1, m.find(3.14));
  EXPECT_EQ(m.end(), m.find(4));

  EXPECT_EQ(1, m.count(3));
  EXPECT_EQ(0, m.count(4));
  EXPECT_TRUE(m.contains(3));
  EXPECT_FALSE(m.contains(4));

  EXPECT_EQ(m.lower_bound(3), m.begin() + 1);
  EXPECT_EQ(m.upper_bound(3), m.begin() + 2);
  EXPECT_EQ(m.lower_bound(4), m.begin() + 2);
  EXPECT_EQ(m.upper_bound(4), m.begin() + 2);

  EXPECT_THAT(m.equal_range(3), Pair(m.begin() + 1, m.begin() + 2));
  EXPECT_THAT(m.equal_range(4), Pair(m.begin() + 2, m.begin() + 2));

  if constexpr (!std::is_const_v<Map>) {
    int three = 3;
    auto hint = m.find(three);

    m[three] += 1;
    m[std::move(three)] += 1;
    EXPECT_EQ(m.at(3), 5);

    three = 3;
    auto [it6, inserted6] = m.try_emplace(three, 6);
    EXPECT_FALSE(inserted6);
    auto [it7, inserted7] = m.try_emplace(std::move(three), 7);
    EXPECT_FALSE(inserted7);

    three = 3;
    m.insert_or_assign(three, 8);
    EXPECT_EQ(m.at(three), 8);
    m.insert_or_assign(three, 9);
    EXPECT_EQ(m.at(std::move(three)), 9);

    three = 3;
    auto it10 = m.try_emplace(hint, three, 10);
    EXPECT_EQ(it10->second, 9);
    auto it11 = m.try_emplace(hint, std::move(three), 11);
    EXPECT_EQ(it11->second, 9);

    three = 3;
    m.insert_or_assign(hint, three, 12);
    EXPECT_EQ(m.at(3), 12);
    m.insert_or_assign(hint, std::move(three), 13);
    EXPECT_EQ(m.at(3), 13);
  }
}

TEST(FlatMapTest, HeterogeneousLookup) {
  TestHeterogeneousLookup<flat_map<OnlyLT, int, TransparentCmp>>();
  TestHeterogeneousLookup<const flat_map<OnlyLT, int, TransparentCmp>>();
}

TEST(FlatMapTest, InsertOrAssignForwardsCorrectly) {
  using ::absl::test_internal::CopyableMovableInstance;
  using ::absl::test_internal::InstanceTracker;
  InstanceTracker tracker;
  gtl::flat_map<int, CopyableMovableInstance> s;

  s.try_emplace(0, CopyableMovableInstance(7));
  // Moves temporary into into map
  EXPECT_EQ(tracker.moves(), 1);
  EXPECT_EQ(tracker.copies(), 0);

  CopyableMovableInstance v(13);

  s.insert_or_assign(0, v);
  // Copies v into map; no moves
  EXPECT_EQ(tracker.moves(), 1);
  EXPECT_EQ(tracker.copies(), 1);

  s.insert_or_assign(0, std::move(v));
  // Moves v into map
  EXPECT_EQ(tracker.moves(), 2);
  EXPECT_EQ(tracker.copies(), 1);
}

TEST(FlatMapTest, NoHeterogeneousLookupWithoutTypedef) {
  flat_map<std::string, int, NonTransparent> m = {{"hello", 3}, {"world", 4}};
  EXPECT_EQ(m.end(), m.find("blah"));
  EXPECT_EQ(m.begin(), m.lower_bound("hello"));
  EXPECT_EQ(1, m.count("world"));
  EXPECT_TRUE(m.contains("world"));
}

// Tests for noncopyable elements.

TEST(FlatMapTest, NoncopyableCreation) {
  // Using transparent comparator just for testing convenience.
  flat_map<int, std::unique_ptr<int>, TransparentCmp> m;
  EXPECT_TRUE(m.empty());
}

TEST(FlatMapTest, NoncopyableFromRange) {
  // Creation from iterators.
  auto v = UniquePtrs({42, 7, 7});
  flat_map<int, std::unique_ptr<int>, TransparentCmp> m(
      std::make_move_iterator(v.begin()), std::make_move_iterator(v.end()));
  ExpectElements(m, {{7, 7}, {42, 42}});

  // insert range
  v = UniquePtrs({10, 10, 100});
  m.insert(std::make_move_iterator(v.begin()),
           std::make_move_iterator(v.end()));
  ExpectElements(m, {{7, 7}, {10, 10}, {42, 42}, {100, 100}});
}

TEST(FlatMapTest, NonCopyableMovesAndAssignments) {
  flat_map<int, std::unique_ptr<int>, TransparentCmp> s1;
  s1.insert({1, std::make_unique<int>(2)});
  s1.insert({3, std::make_unique<int>(0)});

  flat_map<int, std::unique_ptr<int>, TransparentCmp> s2 = std::move(s1);
  ExpectElements(s1, {});  // NOLINT misc-use-after-move
  ExpectElements(s2, {{1, 2}, {3, 0}});
  s1 = std::move(s2);
  ExpectElements(s1, {{1, 2}, {3, 0}});
  ExpectElements(s2, {});  // NOLINT misc-use-after-move
  // Swaps.
  s1.swap(s2);
  ExpectElements(s1, {});
  ExpectElements(s2, {{1, 2}, {3, 0}});
  using std::swap;
  swap(s1, s2);
  ExpectElements(s1, {{1, 2}, {3, 0}});
  ExpectElements(s2, {});
}

TEST(FlatMapTest, NoncopyableInsertAndEmplace) {
  flat_map<int, std::unique_ptr<int>, TransparentCmp> m;
  m.insert({0, std::make_unique<int>(0)});
  m.insert({5, std::make_unique<int>(5)});
  m.insert({5, std::make_unique<int>(7)});
  m.emplace(7, std::make_unique<int>(7));
  m.emplace(3, std::make_unique<int>(3));
  // We do not leak memory, even if emplace fails (note that this is not
  // generally guaranteed by std::set/map).
  m.emplace(std::piecewise_construct, std::make_tuple(3),
            std::make_tuple(new int(42)));
  ExpectElements(m, {{0, 0}, {3, 3}, {5, 5}, {7, 7}});

  // Insert with hint:
  m.insert(m.begin() + 1, {2, std::make_unique<int>(2)});
  // already exists
  m.insert(m.begin() + 2, {2, std::make_unique<int>(2)});
  // incorrect hint
  m.insert(m.begin(), {8, std::make_unique<int>(8)});
  ExpectElements(m, {{0, 0}, {2, 2}, {3, 3}, {5, 5}, {7, 7}, {8, 8}});
  // emplace_hint
  m.emplace_hint(m.begin() + 1, 1, std::make_unique<int>(1));
  // already exists
  m.emplace_hint(m.begin() + 2, 1, std::make_unique<int>(-1));
  // incorrect hint
  m.emplace_hint(m.begin(), 9, std::make_unique<int>(9));
  ExpectElements(
      m, {{0, 0}, {1, 1}, {2, 2}, {3, 3}, {5, 5}, {7, 7}, {8, 8}, {9, 9}});
}

TEST(FlatMapTest, NoncopyableInsertOrAssign) {
  flat_map<int, std::unique_ptr<int>, TransparentCmp> m;
  m.insert_or_assign(0, std::make_unique<int>(0));
  ExpectElements(m, {{0, 0}});
  m.insert_or_assign(0, std::make_unique<int>(1));
  ExpectElements(m, {{0, 1}});
  m.insert_or_assign(m.begin(), 0, std::make_unique<int>(2));
  ExpectElements(m, {{0, 2}});
}

TEST(FlatMapTest, NoncopyableTryEmplace) {
  flat_map<int, std::unique_ptr<int>, TransparentCmp> m;
  auto result1 = m.try_emplace(0, std::make_unique<int>(0));
  ExpectElements(m, {{0, 0}});
  EXPECT_THAT(result1, Pair(m.begin(), true));

  auto new_value = std::make_unique<int>(1);
  auto result2 = m.try_emplace(0, std::move(new_value));
  ExpectElements(m, {{0, 0}});
  EXPECT_THAT(result2, Pair(m.begin(), false));

  // The value must not be moved-from if try_emplace didn't emplace.
  EXPECT_THAT(new_value, Not(Eq(nullptr)));

  auto result3 = m.try_emplace(m.end(), 1, std::make_unique<int>(0));
  ExpectElements(m, {{0, 0}, {1, 0}});
  EXPECT_EQ(result3, m.begin() + 1);
}

// Tests for stateful comparator.

TEST(FlatMapTest, StatefulComparator) {
  // Lambda has no default constructor, so this test guarantees that we do not
  // accidentally try to default-construct a comparator.
  int calls = 0;
  auto cmp = [&calls](int x, int y) {
    ++calls;
    return x < y;
  };
  using Cmp = decltype(cmp);

  // Create flat_maps using all constructors, with the same stateful comparator.
  flat_map<int, int, Cmp> m1(cmp);
  flat_map<int, int, Cmp> m2(m1.begin(), m1.begin(), cmp);
  flat_map<int, int, Cmp> m3({}, cmp);
  flat_map<int, int, Cmp> m4 = m1;
  for (auto* m : {&m1, &m2, &m3, &m4}) {  // NOLINT misc-use-after-move
    m->emplace(0, 1);
    EXPECT_EQ(calls, 0);
    m->emplace(1, 2);
    EXPECT_GT(calls, 0);
    calls = 0;
    EXPECT_EQ(1, m->erase(0));
    EXPECT_GT(calls, 0);
    calls = 0;
  }
}

// Test that the moved-from map is in valid state, even if moved-from comparator
// is not (as is the case for std::function).
TEST(FlatMapTest, MoveCopyAssignStatefulComparator) {
  using Cmp = std::function<bool(int, int)>;
  flat_map<int, int, Cmp> m1(std::greater<int>{});
  flat_map<int, int, Cmp> m2(std::greater<int>{});
  flat_map<int, int, Cmp> m3(m1);
  flat_map<int, int, Cmp> m4(std::move(m1));
  flat_map<int, int, Cmp> m5, m6;
  m5 = m2;
  m6 = std::move(m2);
  // All the maps use the 'greater' comparator (though in case of moved-from
  // maps, this is an implementation detail; we only guarantee a valid state).
  for (auto* m :
       {&m1, &m2, &m3, &m4, &m5, &m6}) {  // NOLINT misc-use-after-move
    m->insert({{2, 2}, {3, 3}});
    ExpectElements(*m, {{3, 3}, {2, 2}});
    EXPECT_TRUE(m->key_comp()(3, 2));
    EXPECT_TRUE(m->value_comp()(std::make_pair(3, 2), std::make_pair(2, 3)));
  }
}

// Tests for gtl::sorted_unique_container_t constructor.
TEST(FlatMapTest, SortedUniqueContainerConstructor) {
  // Using unique_ptrs guarantees we do not incur additional copies.
  flat_map<int, std::unique_ptr<int>, TransparentCmp> m1(
      sorted_unique_container, UniquePtrs({1, 2, 3}));
  flat_map<int, std::unique_ptr<int>, TransparentCmp> m2(
      sorted_unique_container, TransparentCmp(), UniquePtrs({1, 2, 3}));
  ExpectElements(m1, {{1, 1}, {2, 2}, {3, 3}});
  ExpectElements(m2, {{1, 1}, {2, 2}, {3, 3}});

  // Try a more complex vector constructor.
  std::vector<std::pair<int, int>> v = {{3, 2}, {1, 0}};
  flat_map<int, int, std::greater<int>> m3(sorted_unique_container, v.begin(),
                                           v.end(), std::allocator<int>());
  flat_map<int, int, std::greater<int>> m4(sorted_unique_container,
                                           std::greater<int>(), v.begin(),
                                           v.end(), std::allocator<int>());
  ExpectElements(m3, {{3, 2}, {1, 0}});
  ExpectElements(m4, {{3, 2}, {1, 0}});
}

#if GTEST_HAS_DEATH_TEST
TEST(FlatMapTest, SortedUniqueContainerDeathTest) {
  std::vector<std::pair<const int, int>> ordered = {{1, 1}, {2, 2}};
  std::vector<std::pair<const int, int>> reversed = {{3, 3}, {2, 2}};
  std::vector<std::pair<const int, int>> repeated = {{7, 7}, {7, 0}};

  flat_map<int, int> m1(sorted_unique_container, ordered);
  for (const auto& v : {reversed, repeated}) {
    ASSERT_DEBUG_DEATH((flat_map<int, int>(sorted_unique_container, v)),
                       "check_invariants");
  }

  // Non-default comparator.
  flat_map<int, int, std::greater<int>> m2(sorted_unique_container, reversed);
  flat_map<int, int, std::greater<int>> m3(sorted_unique_container,
                                           std::greater<int>(), reversed);
  for (const auto& v : {ordered, repeated}) {
    ASSERT_DEBUG_DEATH(
        (flat_map<int, int, std::greater<int>>(sorted_unique_container, v)),
        "check_invariants");
    ASSERT_DEBUG_DEATH((flat_map<int, int, std::greater<int>>(
                           sorted_unique_container, std::greater<int>(), v)),
                       "check_invariants");
  }
}
#endif

TEST(FlatMapTest, SortedUniqueStdFunctionComparator) {
  flat_map<int, std::unique_ptr<int>,
           std::function<bool(const int&, const int&)>>
      m(sorted_unique_container, TransparentCmp(), UniquePtrs({1, 2}));
  ExpectElements(m, {{1, 1}, {2, 2}});
}

TEST(FlatMapTest, SortedUniqueContainerSpan) {
  std::pair<int, int> array[] = {{1, 1}, {2, 2}};
  flat_map<int, int, std::less<int>, absl::Span<std::pair<int, int>>> m(
      sorted_unique_container, array, 2);
  ExpectElements(m, {{1, 1}, {2, 2}});
  EXPECT_EQ(&*m.begin(), array);
}

// Basic test for stateful allocators.
TEST(FlatMapTest, StatefulAllocator) {
  int64_t bytes = 0;
  using Ints = std::pair<int, int>;
  flat_map<int, int, std::less<int>,
           std::vector<Ints, STLCountingAllocator<Ints>>>
      s(sorted_unique_container, STLCountingAllocator<Ints>(&bytes));
  EXPECT_EQ(bytes, 0);
  s[7] = 7;
  EXPECT_GT(bytes, 0);
  bytes = 0;
  auto s2 = s;
  EXPECT_GT(bytes, 0);
}

TEST(FlatMapTest, StdArrayRep) {
  // shared_ptr allows easily testing move behavior of flat_map due to its
  // well-defined move semantics.
  using array_type =
      std::array<std::pair<std::shared_ptr<int>, std::shared_ptr<int>>, 2>;
  array_type array = {{{std::make_shared<int>(1), std::make_shared<int>(1)},
                       {std::make_shared<int>(2), std::make_shared<int>(2)}}};
  flat_map<int, int, TransparentCmp, array_type> m(sorted_unique_container,
                                                   std::move(array));

  ExpectElements(m, {{1, 1}, {2, 2}});

  // For std::array, we copy the Rep instead of moving it. See
  // internal_flat::Impl for rationale.
  auto m1(std::move(m));
  ExpectElements(m, {{1, 1}, {2, 2}});  // NOLINT misc-use-after-move
  ExpectElements(m1, {{1, 1}, {2, 2}});
  m1 = std::move(m);
  ExpectElements(m, {{1, 1}, {2, 2}});  // NOLINT misc-use-after-move
  ExpectElements(m1, {{1, 1}, {2, 2}});
  EXPECT_EQ(*m.begin(), *m1.begin());
}

TEST(FlatMapTest, ConstKeyRep) {
  using Rep = std::vector<std::pair<const std::string, int>>;

  flat_map<std::string, int, std::less<>, Rep> m;

  m.insert({"a", 1});
  m.emplace("b", 2);
  m.try_emplace("c", 3);

  EXPECT_THAT(m, ElementsAre(Pair("a", 1), Pair("b", 2), Pair("c", 3)));

  m.erase("b");

  EXPECT_THAT(m, ElementsAre(Pair("a", 1), Pair("c", 3)));

  m.insert(m.end(), {"b", 2});
  m.emplace_hint(m.end(), "d", 4);

  EXPECT_THAT(
      m, ElementsAre(Pair("a", 1), Pair("b", 2), Pair("c", 3), Pair("d", 4)));

  m.insert_or_assign("e", 5);
  m.insert_or_assign(m.begin(), "a", 10);
  EXPECT_THAT(m, ElementsAre(Pair("a", 10), Pair("b", 2), Pair("c", 3),
                             Pair("d", 4), Pair("e", 5)));
}

TEST(FlatMapTest, ViewValuesRep) {
  // Using loooong strings to make sure SSO does not hide potential lifetime
  // bugs in fastbuild.
  std::string key(64, 'k');
  std::string value(64, 'v');
  std::vector<std::pair<std::string, std::string>> v = {{key, value}};
  // Map holds views to objects stored in a container with a different
  // value_type.
  flat_map<absl::string_view, absl::string_view> map(v.begin(), v.end());
  EXPECT_THAT(map, ElementsAre(Pair(StrEq(key), StrEq(value))));
}

TEST(FlatMapTest, ConstKeyRepRangeMethods) {
  using Rep = std::vector<std::pair<const std::string, int>>;

  flat_map<std::string, int, std::less<>, Rep> m = {
      {"a", 1}, {"b", 2}, {"c", 3}};

  EXPECT_THAT(m, ElementsAre(Pair("a", 1), Pair("b", 2), Pair("c", 3)));

  m.insert({{"e", 5}, {"d", 4}, {"c", 3}});

  EXPECT_THAT(m, ElementsAre(Pair("a", 1), Pair("b", 2), Pair("c", 3),
                             Pair("d", 4), Pair("e", 5)));
}

TEST(FlatMapTest, ConstKeyRepContainerCtor) {
  using Rep = std::vector<std::pair<const std::string, int>>;
  using FlatMap = flat_map<std::string, int, std::less<>, Rep>;

  Rep r = {{"a", 1}, {"b", 2}, {"c", 3}};

  {
    FlatMap m(r);
    EXPECT_THAT(m, ElementsAre(Pair("a", 1), Pair("b", 2), Pair("c", 3)));
  }

  {
    FlatMap m(gtl::sorted_unique_container, r);
    EXPECT_THAT(m, ElementsAre(Pair("a", 1), Pair("b", 2), Pair("c", 3)));
  }

  {
    FlatMap m(r.begin(), r.end());
    EXPECT_THAT(m, ElementsAre(Pair("a", 1), Pair("b", 2), Pair("c", 3)));
  }
}

#if GTEST_HAS_DEATH_TEST
TEST(FlatMapTest, StdArrayRepTest) {
  // Do not allow default construction where it doesn't make sense.
  ASSERT_DEBUG_DEATH((flat_map<int, int, std::less<int>,
                               std::array<std::pair<int, int>, 7>>()),
                     "check_invariants");
}
#endif

TEST(FlatMapTest, Hash) {
  using M = gtl::flat_map<int, int>;
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      {M{},                                                    //
       M{{0, 0}}, M{{1, 0}}, M{{2, 0}}, M{{0, 1}}, M{{1, 1}},  //
       M{{0, 0}, {1, 0}}, M{{0, 1}, {1, 0}}, M{{2, 0}, {1, 1}, {0, 2}}}));
}

TEST(FlatMapTest, CopiesInputComparator) {
  struct Cmp {
    int state;
    bool operator==(const Cmp& other) const { return state == other.state; }
    bool operator!=(const Cmp& other) const { return state != other.state; }
    bool operator()(int left, int right) const { return left < right; }
  };
  Cmp cmp1 = {1};
  Cmp cmp2 = {2};
  ASSERT_NE(cmp1, cmp2);

  flat_map<int, int, Cmp> m1(cmp1);
  flat_map<int, int, Cmp> m2(cmp2);
  EXPECT_EQ(m1.key_comp(), cmp1);
  EXPECT_EQ(m2.key_comp(), cmp2);
  EXPECT_EQ(m1.value_comp(), cmp1);
  EXPECT_EQ(m2.value_comp(), cmp2);
}

constexpr auto DoMoveConstruction(
    gtl::flat_map<int, int, std::less<int>,
                  absl::Span<const std::pair<int, int>>>
        object) {
  return object;
}

TEST(FlatMapTest, ConstexprConstructor) {
  // Empty
  constexpr gtl::flat_map<int, int, std::less<int>,
                          std::array<std::pair<int, int>, 0>>
      kMapEmpty;
  EXPECT_THAT(kMapEmpty, ElementsAre());

  static constexpr std::array<std::pair<int, int>, 4> kArray{
      {{1, -1}, {4, -4}, {17, -17}, {222, -222}}};

  const auto tester = [](const auto& map) {
    ASSERT_TRUE(map.find(4) != map.end());
    EXPECT_THAT(*map.find(4), Pair(4, -4));
    EXPECT_THAT(map, ElementsAre(Pair(1, -1), Pair(4, -4), Pair(17, -17),
                                 Pair(222, -222)));
  };

  // Using std::array:
  constexpr gtl::flat_map<int, int, std::less<int>,
                          std::array<std::pair<int, int>, 4>>
      kMapArray(gtl::sorted_unique_container, kArray);
  tester(kMapArray);

  // Using absl::Span:
  constexpr gtl::flat_map<int, int, std::less<int>,
                          absl::Span<const std::pair<int, int>>>
      kMapSpan(gtl::sorted_unique_container, kArray);
  tester(kMapSpan);

  // Copy:
  constexpr gtl::flat_map<int, int, std::less<int>,
                          absl::Span<const std::pair<int, int>>>
      kMapSpanCopy(kMapSpan);
  tester(kMapSpanCopy);

  // Move:
  constexpr auto kMapSpanMove = DoMoveConstruction(kMapSpan);
  tester(kMapSpanMove);
}

struct OddsFirst {
  constexpr bool operator()(int a, int b) const {
    return std::pair(a % 2 == 0, a) < std::pair(b % 2 == 0, b);
  }
};

TEST(FlatMapTest, ConstexprFactoryExplicit) {
  constexpr auto kMapEmpty = gtl::fixed_flat_map_of<int, int>({});
  EXPECT_THAT(kMapEmpty, ElementsAre());

  constexpr auto kMapOne = gtl::fixed_flat_map_of<int, int>({{1, -1}});
  EXPECT_THAT(kMapOne, ElementsAre(Pair(1, -1)));

  constexpr auto kMapMany = gtl::fixed_flat_map_of<absl::string_view, int>(
      {{"foo", 1}, {"bar", 2}, {"baz", 3}});
  ASSERT_TRUE(kMapMany.find("foo") != kMapMany.end());
  EXPECT_THAT(*kMapMany.find("foo"), Pair("foo", 1));
  EXPECT_THAT(kMapMany,
              ElementsAre(Pair("bar", 2), Pair("baz", 3), Pair("foo", 1)));

  constexpr auto kMapCustomLess = gtl::fixed_flat_map_of<int, int>(
      {{1, -1}, {2, -2}, {3, -3}}, OddsFirst{});
  EXPECT_THAT(kMapCustomLess,
              ElementsAre(Pair(1, -1), Pair(3, -3), Pair(2, -2)));
}

TEST(FlatMapTest, ConstexprFactoryImplicit) {
  // Can't do empty implicitly.
  constexpr auto kMapOne = gtl::fixed_flat_map_of({std::pair(1, -1)});
  EXPECT_THAT(kMapOne, ElementsAre(Pair(1, -1)));

  constexpr auto kMapMany =
      gtl::fixed_flat_map_of({std::pair(1, -1), std::pair(3, -3)});
  ASSERT_TRUE(kMapMany.find(3) != kMapMany.end());
  EXPECT_THAT(*kMapMany.find(3), Pair(3, -3));
  EXPECT_THAT(kMapMany, ElementsAre(Pair(1, -1), Pair(3, -3)));

  constexpr auto kMapCustomLess = gtl::fixed_flat_map_of(
      {std::pair(1, -1), std::pair(2, -2), std::pair(3, -3)}, OddsFirst{});
  EXPECT_THAT(kMapCustomLess,
              ElementsAre(Pair(1, -1), Pair(3, -3), Pair(2, -2)));
}

TEST(FlatMapTest, ConstexprFactoryArrayExplicit) {
  constexpr std::array<std::pair<int, int>, 0> kEmpty = {};
  constexpr auto kMapEmpty = gtl::fixed_flat_map_of<int, int>(kEmpty);
  EXPECT_THAT(kMapEmpty, ElementsAre());

  constexpr std::array<std::pair<int, int>, 1> kOne = {{{1, -1}}};
  constexpr auto kMapOne = gtl::fixed_flat_map_of<int, int>(kOne);
  EXPECT_THAT(kMapOne, ElementsAre(Pair(1, -1)));

  constexpr std::array<std::pair<absl::string_view, int>, 3> kMany = {
      {{"foo", 1}, {"bar", 2}, {"baz", 3}}};
  constexpr auto kMapMany =
      gtl::fixed_flat_map_of<absl::string_view, int>(kMany);
  ASSERT_TRUE(kMapMany.find("foo") != kMapMany.end());
  EXPECT_THAT(*kMapMany.find("foo"), Pair("foo", 1));
  EXPECT_THAT(kMapMany,
              ElementsAre(Pair("bar", 2), Pair("baz", 3), Pair("foo", 1)));

  constexpr std::array<std::pair<int, int>, 3> kCustomLess = {
      {{1, -1}, {2, -2}, {3, -3}}};
  constexpr auto kMapCustomLess =
      gtl::fixed_flat_map_of<int, int>(kCustomLess, OddsFirst{});
  EXPECT_THAT(kMapCustomLess,
              ElementsAre(Pair(1, -1), Pair(3, -3), Pair(2, -2)));
}

TEST(FlatMapTest, ConstexprFactoryArrayImplicit) {
  constexpr std::array<std::pair<int, int>, 0> kEmpty = {};
  constexpr auto kMapEmpty = gtl::fixed_flat_map_of(kEmpty);
  EXPECT_THAT(kMapEmpty, ElementsAre());

  constexpr std::array<std::pair<int, int>, 1> kOne = {{{1, -1}}};
  constexpr auto kMapOne = gtl::fixed_flat_map_of(kOne);
  EXPECT_THAT(kMapOne, ElementsAre(Pair(1, -1)));

  constexpr std::array<std::pair<absl::string_view, int>, 3> kMany = {
      {{"foo", 1}, {"bar", 2}, {"baz", 3}}};
  constexpr auto kMapMany = gtl::fixed_flat_map_of(kMany);
  ASSERT_TRUE(kMapMany.find("foo") != kMapMany.end());
  EXPECT_THAT(*kMapMany.find("foo"), Pair("foo", 1));
  EXPECT_THAT(kMapMany,
              ElementsAre(Pair("bar", 2), Pair("baz", 3), Pair("foo", 1)));

  constexpr std::array<std::pair<int, int>, 3> kCustomLess = {
      {{1, -1}, {2, -2}, {3, -3}}};
  constexpr auto kMapCustomLess =
      gtl::fixed_flat_map_of(kCustomLess, OddsFirst{});
  EXPECT_THAT(kMapCustomLess,
              ElementsAre(Pair(1, -1), Pair(3, -3), Pair(2, -2)));
}

TEST(FlatMapTest, NonConstexprOnDuplicates) {
  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation([] {
    return gtl::fixed_flat_map_of<int, int>({{0, 0}, {1, 1}, {2, 2}});
  }));
  EXPECT_FALSE(absl::meta_internal::HasConstexprEvaluation([] {
    return gtl::fixed_flat_map_of<int, int>({{0, 0}, {1, 1}, {0, 2}});
  }));
  EXPECT_FALSE(absl::meta_internal::HasConstexprEvaluation([] {
    std::array<std::pair<int, int>, 3> kArr = {{{0, 0}, {1, 1}, {0, 2}}};
    return gtl::fixed_flat_map_of<int, int>(kArr);
  }));
}

TEST(FlatMapTest, ConstexprKeyComp) {
  static constexpr auto kMap = gtl::fixed_flat_map_of({std::pair(1, -1)});

  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return kMap.key_comp(); }));
}

TEST(FlatMapTest, ConstexprMemberFunctionsCapacity) {
  static constexpr auto kMap = gtl::fixed_flat_map_of<int, int>({{1, -1}});

  EXPECT_TRUE(
      absl::meta_internal::HasConstexprEvaluation([] { return kMap.empty(); }));

  EXPECT_TRUE(
      absl::meta_internal::HasConstexprEvaluation([] { return kMap.size(); }));

  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return kMap.max_size(); }));
}

TEST(FlatMapTest, ConstexprMemberFunctionsIterators) {
  static constexpr auto kMap = gtl::fixed_flat_map_of<int, int>({{1, -1}});

  EXPECT_TRUE(
      absl::meta_internal::HasConstexprEvaluation([] { return kMap.begin(); }));

  EXPECT_TRUE(
      absl::meta_internal::HasConstexprEvaluation([] { return kMap.end(); }));

  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return kMap.cbegin(); }));

  EXPECT_TRUE(
      absl::meta_internal::HasConstexprEvaluation([] { return kMap.cend(); }));

  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return kMap.rbegin(); }));

  EXPECT_TRUE(
      absl::meta_internal::HasConstexprEvaluation([] { return kMap.rend(); }));

  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return kMap.crbegin(); }));

  EXPECT_TRUE(
      absl::meta_internal::HasConstexprEvaluation([] { return kMap.crend(); }));
}

TEST(FlatMapTest, ConstexprPairAsKey) {
  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation([] {
    return gtl::fixed_flat_map_of<std::pair<int, int>, int>(
        {{{0, 0}, 0}, {{1, 1}, 1}, {{2, 2}, 2}});
  }));
  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation([] {
    return gtl::fixed_flat_map_of<std::pair<int, int>, int>(
        {{{2, 2}, 0}, {{1, 1}, 1}, {{0, 0}, 2}});
  }));
}

TEST(FlatMapTest, ConstexprTupleAsKey) {
  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation([] {
    return gtl::fixed_flat_map_of<std::tuple<int, int>, int>(
        {{{0, 0}, 0}, {{1, 1}, 1}, {{2, 2}, 2}});
  }));
  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation([] {
    return gtl::fixed_flat_map_of<std::tuple<int, int>, int>(
        {{{2, 2}, 0}, {{1, 1}, 1}, {{0, 0}, 2}});
  }));
}

// constexpr flat_map operations require constexpr STL algorithms.
#ifdef __cpp_lib_constexpr_algorithms
using Map = gtl::flat_map<int, int>;
using ConstMap = const gtl::flat_map<int, int>;

TEST(FlatMapTest, ConstexprAt) {
  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return fixed_flat_map_of<int, int>({{1, 1}}).at(1); }));

  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation([] {
    const auto kMap = fixed_flat_map_of<int, int>({{1, 1}});
    return kMap.at(1);
  }));
}

TEST(FlatMapTest, ConstexprFind) {
  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return Map().find(1); }));
  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return ConstMap().find(1); }));
}

TEST(FlatMapTest, ConstexprCount) {
  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return Map().count(1); }));
}

TEST(FlatMapTest, ConstexprContains) {
  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return Map().contains(1); }));
}

TEST(FlatMapTest, ConstexprLowerBound) {
  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return Map().lower_bound(1); }));
  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return ConstMap().lower_bound(1); }));
}

TEST(FlatMapTest, ConstexprUpperBound) {
  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return Map().upper_bound(1); }));
  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return ConstMap().upper_bound(1); }));
}

TEST(FlatMapTest, ConstexprEqualRange) {
  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return Map().equal_range(1); }));
  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return ConstMap().equal_range(1); }));
}
#endif  // __cpp_lib_constexpr_algorithms

template <class, class = void>
struct IsTransparent : std::false_type {};
template <class T>
struct IsTransparent<T, std::void_t<typename T::is_transparent>>
    : std::true_type {};

template <typename Cmp>
inline constexpr bool kIsTransparent = IsTransparent<Cmp>::value;

TEST(FlatMapTest, StringCmpTransparentByDefault) {
  EXPECT_TRUE((kIsTransparent<flat_map<std::string, int>::key_compare>));
  EXPECT_TRUE((kIsTransparent<flat_map<absl::string_view, int>::key_compare>));
  EXPECT_TRUE((kIsTransparent<flat_map<absl::Cord, int>::key_compare>));
}

struct TrackMovedFrom {
  TrackMovedFrom() = default;
  TrackMovedFrom(TrackMovedFrom&& other) {
    CHECK(!other.moved_from);
    other.moved_from = true;
  }
  bool moved_from = false;
};
// Tests that we don't move from an already-moved-from mapped_type.
TEST(FlatMapTest, TrackMovedFrom) {
  gtl::flat_map<int, TrackMovedFrom> map;
  for (int i = 0; i < 100; ++i) map.emplace(100 - i, TrackMovedFrom());
}

// TODO: Implement benchmarks.

void BM_Construct(benchmark::State& state) {
  int size = state.range(0);
  std::vector<std::pair<int, int>> input;
  for (int i = 0; i < size; ++i) input.push_back({i, i});
  for (auto s : state) {
    flat_map<int, int> map(input.begin(), input.end());
    benchmark::DoNotOptimize(map);
  }
}
BENCHMARK(BM_Construct)->Range(0, 2000);

}  // namespace
}  // namespace gtl
