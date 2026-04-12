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

#include "gloop/util/gtl/flat_set.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/inlined_vector.h"
#include "absl/flags/flag.h"
#include "absl/hash/hash_testing.h"
#include "absl/log/log.h"
#include "absl/meta/internal/constexpr_testing.h"
#include "absl/meta/type_traits.h"
#include "absl/random/random.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "gloop/gloop_test.h"
#include "gloop/util/gtl/comparator.h"
#include "gloop/util/gtl/stl_util.h"
#include "gloop/util/gtl/switch.h"

ABSL_FLAG(uint64_t, benchmark_random_seed, absl::ToUnixMillis(absl::Now()),
          "Random seed.");

namespace gtl {
namespace {

using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::IsEmpty;
using ::testing::Pair;

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
int Get(const OnlyLT& x) { return x.i; }
int Get(const std::unique_ptr<int>& x) { return *x; }
int Get(const std::shared_ptr<int>& x) { return *x; }
template <typename T>
int Get(T t) {
  return t;
}

// Allows comparing int to std::unique_ptr<int>.
struct TransparentCmp {
  template <typename T, typename U>
  bool operator()(const T& t, const U& u) const {
    return Get(t) < Get(u);
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

std::vector<std::unique_ptr<int>> UniquePtrs(absl::Span<const int> v) {
  std::vector<std::unique_ptr<int>> res;
  for (int i : v) {
    res.push_back(std::make_unique<int>(i));
  }
  return res;
}

// Test expectation methods.
template <typename It>
void ExpectElements(It begin, It end, const std::vector<int>& expected) {
  std::vector<int> actual;
  for (; begin != end; ++begin) {
    actual.push_back(Get(*begin));
  }

  EXPECT_THAT(actual, ElementsAreArray(expected));
}

template <typename Set>
void ExpectElements(const Set& set, const std::vector<int>& expected) {
  ExpectElements(set.begin(), set.end(), expected);
}

TEST(FlatSetTest, DefaultIsValid) {
  flat_set<OnlyLT> s;

  EXPECT_TRUE(s.empty());
  EXPECT_EQ(0, s.size());
  EXPECT_EQ(std::vector<int>().max_size(), s.max_size());
  EXPECT_EQ(s.end(), s.find(OnlyLT()));
  EXPECT_EQ(s.end(), s.lower_bound(OnlyLT()));
  EXPECT_EQ(s.end(), s.upper_bound(OnlyLT()));
}

TEST(FlatSetTest, EmplaceDoesNotConfuseConstructors) {
  flat_set<std::vector<int>> s;
  s.emplace(1, 10);
  s.emplace_hint(s.begin(), 1, 8);
  EXPECT_THAT(s, ElementsAre(ElementsAre(8), ElementsAre(10)));
}

TEST(FlatSetTest, MovableNoExcept) {
  EXPECT_TRUE(std::is_nothrow_move_constructible<flat_set<int>>::value);
  EXPECT_TRUE(std::is_nothrow_move_assignable<flat_set<int>>::value);
}

TEST(FlatSetTest, RangeAndListConstruction) {
  std::vector<OnlyLT> v{OnlyLT(4), OnlyLT(9), OnlyLT(1), OnlyLT(17)};
  std::vector<int> expected = {1, 4, 9, 17};

  ExpectElements(flat_set<OnlyLT>(v.begin(), v.end()), expected);
  ExpectElements(flat_set<OnlyLT>(v.rbegin(), v.rend()), expected);
  ExpectElements(
      flat_set<OnlyLT>({OnlyLT(4), OnlyLT(9), OnlyLT(1), OnlyLT(17)}),
      expected);
}

TEST(FlatSetTest, ContainerMoveConstruction) {
  std::vector<int> v = {4, 9, 1, 4};
  std::vector<int> expected = {1, 4, 9};

  ExpectElements(flat_set<std::unique_ptr<int>, TransparentCmp>(UniquePtrs(v)),
                 expected);
}

TEST(FlatSetTest, MoveRep) {
  flat_set<int> s = {4, 9, 1, 4};
  std::vector<int> rep = std::move(s).ExtractRep();
  EXPECT_THAT(s, IsEmpty());  // NOLINT(bugprone-use-after-move)
  EXPECT_THAT(rep, ElementsAre(1, 4, 9));
}

TEST(FlatSetTest, ExplicitComparator) {
  flat_set<OnlyLT, ReverseCmp> s = {OnlyLT(4), OnlyLT(1), OnlyLT(17)};
  ExpectElements(s, {17, 4, 1});
  s.insert(OnlyLT(5));
  ExpectElements(s, {17, 5, 4, 1});
}

TEST(FlatSetTest, Iterators) {
  flat_set<OnlyLT> s = {OnlyLT(4), OnlyLT(9), OnlyLT(1), OnlyLT(17)};
  ExpectElements(s.begin(), s.end(), {1, 4, 9, 17});
  ExpectElements(s.cbegin(), s.cend(), {1, 4, 9, 17});
  ExpectElements(s.rbegin(), s.rend(), {17, 9, 4, 1});
  ExpectElements(s.crbegin(), s.crend(), {17, 9, 4, 1});

  const flat_set<OnlyLT>& c = s;
  ExpectElements(c.begin(), c.end(), {1, 4, 9, 17});
  ExpectElements(c.rbegin(), c.rend(), {17, 9, 4, 1});
}

TEST(FlatSetTest, InsertHandlesDuplicatesCorrectly) {
  flat_set<OnlyLT> s;
  auto p = s.insert(OnlyLT(10));
  EXPECT_THAT(p, Pair(s.begin(), true));
  p = s.insert(OnlyLT(10));
  EXPECT_THAT(p, Pair(s.begin(), false));
  p = s.insert(OnlyLT(5));
  EXPECT_THAT(p, Pair(s.begin(), true));
  p = s.insert(OnlyLT(5));
  EXPECT_THAT(p, Pair(s.begin(), false));
}

TEST(FlatSetTest, InsertMaintainsSortedOrder) {
  flat_set<OnlyLT> s;
  OnlyLT lt5{5};

  // Test both lvalue & rvalue insert.
  s.insert(OnlyLT(10));
  s.insert(lt5);
  EXPECT_EQ(2, s.size());
  EXPECT_EQ(2, s.end() - s.begin());
  EXPECT_TRUE(std::is_sorted(s.begin(), s.end()));

  OnlyLT lt100{100};
  s.insert(lt100);
  s.insert(OnlyLT(1));
  EXPECT_EQ(4, s.size());
  EXPECT_TRUE(std::is_sorted(s.begin(), s.end()));
}

TEST(FlatSetTest, InsertInitializerList) {
  flat_set<OnlyLT> s;
  s.insert({OnlyLT(7), OnlyLT(3)});
  ExpectElements(s, {3, 7});
  s.insert({OnlyLT(4), OnlyLT(2), OnlyLT(6)});
  ExpectElements(s, {2, 3, 4, 6, 7});
}

TEST(FlatSetTest, InsertWithHintWorks) {
  flat_set<OnlyLT> s;
  // Empty, insert with begin hint:
  s.insert(s.begin(), OnlyLT(1));
  EXPECT_EQ(1, s.size());
  s.clear();
  // Empty, insert with end hint:
  OnlyLT lt1{1};
  s.insert(s.end(), lt1);
  EXPECT_EQ(1, s.size());
  // Insert with correct hint:
  s.insert(s.end(), OnlyLT(2));
  EXPECT_EQ(2, s.size());
  // Insert value already present, ensure no duplicate added:
  auto it = s.insert(s.end(), OnlyLT(2));
  EXPECT_EQ(2, s.size());
  // Verify that the iterator returned is accurate.
  EXPECT_TRUE(s.find(OnlyLT(2)) == it);
  s.erase(OnlyLT(2));
  EXPECT_EQ(1, s.size());
  // Insertion in the middle of two other elements.
  it = s.insert(OnlyLT(3)).first;
  s.insert(it, OnlyLT(2));
  EXPECT_EQ(3, s.size());
  EXPECT_TRUE(std::is_sorted(s.begin(), s.end()));
}

TEST(FlatSetTest, InsertWithBadHint) {
  flat_set<OnlyLT> s = {OnlyLT(1), OnlyLT(9)};
  // Bad hint (too small), should still insert:
  auto it = s.insert(s.begin(), OnlyLT(2));
  EXPECT_EQ(it, s.begin() + 1);
  ExpectElements(s, {1, 2, 9});
  // Bad hint, too large this time:
  it = s.insert(s.begin() + 2, OnlyLT(0));
  EXPECT_EQ(it, s.begin());
  ExpectElements(s, {0, 1, 2, 9});
}

TEST(FlatSetTest, IteratorInsertWorks) {
  flat_set<OnlyLT> s;
  std::vector<OnlyLT> v = {OnlyLT(1), OnlyLT(3), OnlyLT(2), OnlyLT(2)};
  s.insert(v.begin(), v.end());
  ExpectElements(s, {1, 2, 3});
}

TEST(FlatSetTest, InsertRangeWorks) {
  flat_set<OnlyLT> s;
  s.insert(OnlyLT(1));
  s.insert(OnlyLT(6));
  std::vector<OnlyLT> to_insert;
  // Duplicates...
  for (int i = 0; i < 6; ++i) {
    to_insert.push_back(OnlyLT(i));
    to_insert.push_back(OnlyLT(i));
  }
  // And unsorted!
  absl::BitGen bitgen;
  std::shuffle(to_insert.begin(), to_insert.end(), bitgen);
  s.insert(to_insert.begin(), to_insert.end());
  EXPECT_EQ(7, s.size());
  for (int i = 0; i <= 6; ++i) {
    EXPECT_EQ(1, s.count(OnlyLT(i))) << "Missing " << i;
  }
}

#ifndef _MSC_VER
// MSVC is very confused by this templated code and does not compile.
TEST(FlatSetTest, InsertRangeUsesFirstValue) {
  // Range constructor chooses first copy of equivalent elements.
  flat_set<std::pair<int, int>, OrderByFirst> s = {
      {1, 1}, {1, 0}, {2, 2}, {2, 1}, {1, 7}, {1, 1}, {2, 9}};
  std::vector<std::pair<int, int>> expected = {{1, 1}, {2, 2}};
  EXPECT_THAT(s, ElementsAreArray(expected));
  // When inserting a range, the same property holds among new elements.
  s.insert({{0, 0}, {4, 4}, {0, 9}, {4, 4}});
  expected = {{0, 0}, {1, 1}, {2, 2}, {4, 4}};
  EXPECT_THAT(s, ElementsAreArray(expected));
  // Elements already in a set take precedence over the new ones inserted.
  s.insert({{0, -1}, {0, 99}, {1, 9}, {3, 3}, {1, -7}, {4, 5}, {3, 1}, {3, 7}});
  expected = {{0, 0}, {1, 1}, {2, 2}, {3, 3}, {4, 4}};
  EXPECT_THAT(s, ElementsAreArray(expected));
}
#endif

TEST(FlatSetTest, Emplace) {
  flat_set<OnlyLT> s;
  auto result = s.emplace(1);
  EXPECT_EQ(result.first, s.begin());
  EXPECT_TRUE(result.second);
  result = s.emplace(9);
  EXPECT_EQ(result.first, s.begin() + 1);
  EXPECT_TRUE(result.second);
  ExpectElements(s, {1, 9});

  result = s.emplace(9);
  EXPECT_EQ(result.first, s.begin() + 1);
  EXPECT_FALSE(result.second);
  ExpectElements(s, {1, 9});

  // Basic test of emplace_hint; the more extensive test for insert with hint
  // (which emplace_hint calls) exists above.
  auto it = s.emplace_hint(s.end(), 10);
  EXPECT_EQ(it, s.begin() + 2);
  ExpectElements(s, {1, 9, 10});
  // Bad hint, still inserts.
  it = s.emplace_hint(s.end(), 0);
  EXPECT_EQ(it, s.begin());
  ExpectElements(s, {0, 1, 9, 10});
  // Correct hint in the middle:
  it = s.emplace_hint(s.begin() + 2, 7);
  EXPECT_EQ(it, s.begin() + 2);
  ExpectElements(s, {0, 1, 7, 9, 10});
  // Element exists.
  it = s.emplace_hint(s.begin() + 3, 7);
  EXPECT_EQ(it, s.begin() + 2);
  ExpectElements(s, {0, 1, 7, 9, 10});
}

TEST(FlatSetTest, EraseWorks) {
  flat_set<OnlyLT> s = {OnlyLT(4), OnlyLT(9), OnlyLT(1), OnlyLT(17)};
  EXPECT_EQ(0, s.erase(OnlyLT(0)));
  EXPECT_EQ(1, s.erase(OnlyLT(4)));
  ExpectElements(s, {1, 9, 17});

  auto it = s.erase(s.begin() + 1);
  EXPECT_EQ(it, s.begin() + 1);
  ExpectElements(s, {1, 17});
}

TEST(FlatSetTest, RangeEraseWorks) {
  flat_set<OnlyLT> s = {OnlyLT(4), OnlyLT(9), OnlyLT(1), OnlyLT(17)};
  auto it = s.erase(s.begin() + 1, s.begin() + 3);
  EXPECT_EQ(it, s.begin() + 1);
  ExpectElements(s, {1, 17});
  // Empty range.
  it = s.erase(s.begin() + 1, s.begin() + 1);
  EXPECT_EQ(it, s.begin() + 1);
  ExpectElements(s, {1, 17});
}

TEST(FlatSetTest, RemoveIfWorks) {
  flat_set<OnlyLT> s = {OnlyLT(4), OnlyLT(9), OnlyLT(2), OnlyLT(17)};
  size_t n_removed = s.remove_if([](OnlyLT x) { return x.i % 2 == 0; });
  EXPECT_EQ(n_removed, 2);
  ExpectElements(s, {9, 17});
}

TEST(FlatSetTest, FindWorks) {
  OnlyLT good = OnlyLT(10);
  OnlyLT bad = OnlyLT(13);
  flat_set<OnlyLT> s = {good};
  const auto& c = s;
  EXPECT_EQ(s.begin(), s.find(good));
  EXPECT_EQ(s.end(), s.find(bad));
  EXPECT_EQ(c.begin(), c.find(good));
  EXPECT_EQ(c.cend(), c.find(bad));
}

void TestConstness(const OnlyLT&) {}
[[maybe_unused]] void TestConstness(OnlyLT&) {
  FAIL() << "Expected const reference";
}

TEST(FlatSetTest, ValueTypeIsConst) {
  flat_set<OnlyLT> s = {OnlyLT(1), OnlyLT(3), OnlyLT(5)};
  TestConstness(*s.begin());
  TestConstness(*s.rbegin());
  TestConstness(*s.find(OnlyLT(3)));
  TestConstness(*s.lower_bound(OnlyLT(4)));
}

// Helper method to cover const / non-const set.
template <typename Set>
void TestBinarySearches() {
  Set s = {OnlyLT(1), OnlyLT(3), OnlyLT(5)};
  EXPECT_EQ(s.lower_bound(OnlyLT(3)), s.begin() + 1);
  EXPECT_EQ(s.upper_bound(OnlyLT(3)), s.begin() + 2);
  EXPECT_EQ(s.lower_bound(OnlyLT(4)), s.begin() + 2);
  EXPECT_EQ(s.upper_bound(OnlyLT(4)), s.begin() + 2);

  EXPECT_THAT(s.equal_range(OnlyLT(3)), Pair(s.begin() + 1, s.begin() + 2));
  EXPECT_THAT(s.equal_range(OnlyLT(4)), Pair(s.begin() + 2, s.begin() + 2));
}

TEST(FlatSetTest, BinarySearchesWork) {
  TestBinarySearches<flat_set<OnlyLT>>();
  TestBinarySearches<const flat_set<OnlyLT>>();
}

TEST(FlatSetTest, CopyAndAssignmentWork) {
  flat_set<OnlyLT> s;
  s.insert(OnlyLT(1));
  ExpectElements(s, {1});
  flat_set<OnlyLT> s2(s);
  ExpectElements(s2, {1});
  flat_set<OnlyLT> s3;
  s3 = s;
  EXPECT_EQ(1, s3.size());

  // Assignment from initializer list.
  s = {OnlyLT(7)};
  ExpectElements(s, {7});
}

TEST(FlatSetTest, CountWorks) {
  flat_set<OnlyLT> s;
  OnlyLT v(1);
  EXPECT_EQ(0, s.count(v));
  s.insert(v);
  EXPECT_EQ(1, s.count(v));
  s.insert(v);
  EXPECT_EQ(1, s.count(v));
}

TEST(FlatSetTest, ContainsWorks) {
  flat_set<OnlyLT> s;
  OnlyLT v(1);
  EXPECT_FALSE(s.contains(v));
  s.insert(v);
  EXPECT_TRUE(s.contains(v));
  s.insert(v);
  EXPECT_TRUE(s.contains(v));
}

TEST(FlatSetTest, InstantiatesWithInlinedVector) {
  flat_set<OnlyLT, std::less<OnlyLT>, absl::InlinedVector<OnlyLT, 7>> m;
  m.insert(OnlyLT(1));
  EXPECT_EQ(1, m.size());
}

TEST(FlatSetTest, OperatorsWorkWithInlinedVector) {
  flat_set<int, std::less<>, absl::InlinedVector<int, 7>> s, s2;

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

TEST(FlatSetTest, RelationalOperatorsWork) {
  OnlyLT v1(1);
  OnlyLT v2(2);

  flat_set<OnlyLT> s1, s2;
  EXPECT_FALSE(s1 < s2);
  EXPECT_FALSE(s1 > s2);
  EXPECT_TRUE(s1 <= s2);
  EXPECT_TRUE(s1 >= s2);

  s2.insert(v1);
  EXPECT_TRUE(s1 < s2);
  EXPECT_FALSE(s1 > s2);
  EXPECT_TRUE(s1 <= s2);
  EXPECT_FALSE(s1 >= s2);

  s1.insert(v2);
  EXPECT_FALSE(s1 < s2);
  EXPECT_TRUE(s1 > s2);
  EXPECT_FALSE(s1 <= s2);
  EXPECT_TRUE(s1 >= s2);

  s1.insert(v1);
  s2.insert(v2);
  EXPECT_FALSE(s1 < s2);
  EXPECT_FALSE(s1 > s2);
  EXPECT_TRUE(s1 <= s2);
  EXPECT_TRUE(s1 >= s2);
}

TEST(FlatSetTest, ComparisonsWork) {
  flat_set<int> s1, s2;
  EXPECT_FALSE(s1 != s2);
  EXPECT_TRUE(s1 == s2);

  s2.insert(1);
  EXPECT_TRUE(s1 != s2);
  EXPECT_FALSE(s1 == s2);

  s1.insert(2);
  EXPECT_TRUE(s1 != s2);
  EXPECT_FALSE(s1 == s2);

  s1.insert(1);
  s2.insert(2);
  EXPECT_FALSE(s1 != s2);
  EXPECT_TRUE(s1 == s2);
}

TEST(FlatSetTest, SwapWorks) {
  flat_set<OnlyLT> s1 = {OnlyLT(1)};
  flat_set<OnlyLT> s2 = {OnlyLT(2)};
  s1.swap(s2);
  ExpectElements(s1, {2});
  ExpectElements(s2, {1});
  using std::swap;
  swap(s1, s2);
  ExpectElements(s1, {1});
  ExpectElements(s2, {2});
}

TEST(FlatSetTest, VectorExtensions) {
  flat_set<OnlyLT> s;
  EXPECT_EQ(0, s.capacity());
  for (int i : {1, 2, 3, 4, 5}) {
    s.insert(OnlyLT(i));
  }
  EXPECT_GE(s.capacity(), 5);
  s.reserve(1000);
  EXPECT_GE(s.capacity(), 1000);

  // shrink_to_fit is non-binding, but - given that one motivation for flat_set
  // is memory optimization - we would really like it to work. If we have a
  // standard library which does not honour shrink_to_fit, we should reimplement
  // it ourselves in flat_set.
  s.shrink_to_fit();
  EXPECT_EQ(s.capacity(), 5);
}

TEST(FlatSetTest, MakeConstSpan) {
  const flat_set<int> ints{1, 2, 3};
  absl::Span<const int> int_span = absl::MakeSpan(ints);
  ExpectElements(int_span, {1, 2, 3});
}

// Tests for transparent comparator (a.k.a. heterogeneous lookup).

// Helper method to cover const & non-const overloads (depending on the template
// argument).
template <typename Set>
void TestHeterogeneousLookup() {
  Set s = {OnlyLT(3), OnlyLT(1), OnlyLT(5)};
  EXPECT_EQ(s.begin() + 1, s.find(3));
  EXPECT_EQ(s.begin() + 1, s.find(3.14));
  EXPECT_EQ(s.end(), s.find(4));

  EXPECT_EQ(1, s.count(3));
  EXPECT_EQ(0, s.count(4));
  EXPECT_TRUE(s.contains(3));
  EXPECT_FALSE(s.contains(4));

  EXPECT_EQ(s.lower_bound(3), s.begin() + 1);
  EXPECT_EQ(s.upper_bound(3), s.begin() + 2);
  EXPECT_EQ(s.lower_bound(4), s.begin() + 2);
  EXPECT_EQ(s.upper_bound(4), s.begin() + 2);

  EXPECT_THAT(s.equal_range(3), Pair(s.begin() + 1, s.begin() + 2));
  EXPECT_THAT(s.equal_range(4), Pair(s.begin() + 2, s.begin() + 2));

  if constexpr (!std::is_const_v<Set>) {
    EXPECT_EQ(s.erase(2), 0);
    EXPECT_EQ(s.erase(1), 1);
  }
}

TEST(FlatSetTest, HeterogeneousLookup) {
  TestHeterogeneousLookup<flat_set<OnlyLT, TransparentCmp>>();
  TestHeterogeneousLookup<const flat_set<OnlyLT, TransparentCmp>>();
}

TEST(FlatSetTest, NoHeterogeneousLookupWithoutTypedef) {
  flat_set<std::string, NonTransparent> s = {"hello", "world"};
  EXPECT_EQ(s.end(), s.find("blah"));
  EXPECT_EQ(s.begin(), s.lower_bound("hello"));
  EXPECT_EQ(1, s.count("world"));
  EXPECT_TRUE(s.contains("world"));
}

// Tests for noncopyable elements.

TEST(FlatSetTest, NoncopyableCreation) {
  // Using transparent comparator just for testing convenience.
  flat_set<std::unique_ptr<int>, TransparentCmp> s;
  EXPECT_TRUE(s.empty());
}

TEST(FlatSetTest, NoncopyableFromRange) {
  // Creation from iterators.
  std::vector<std::unique_ptr<int>> v = UniquePtrs({42, 7, 7});
  flat_set<std::unique_ptr<int>, TransparentCmp> s(
      std::make_move_iterator(v.begin()), std::make_move_iterator(v.end()));
  ExpectElements(s, {7, 42});

  // insert range
  v = UniquePtrs({10, 10, 100});
  s.insert(std::make_move_iterator(v.begin()),
           std::make_move_iterator(v.end()));
  ExpectElements(s, {7, 10, 42, 100});
}

TEST(FlatSetTest, NonCopyableMovesAndAssignments) {
  flat_set<std::unique_ptr<int>, TransparentCmp> s1;
  s1.insert(std::make_unique<int>(1));
  s1.insert(std::make_unique<int>(0));

  flat_set<std::unique_ptr<int>, TransparentCmp> s2 = std::move(s1);
  ExpectElements(s1, {});  // NOLINT misc-use-after-move
  ExpectElements(s2, {0, 1});
  s1 = std::move(s2);
  ExpectElements(s1, {0, 1});
  ExpectElements(s2, {});  // NOLINT misc-use-after-move
  // Swaps.
  s1.swap(s2);
  ExpectElements(s1, {});
  ExpectElements(s2, {0, 1});
  using std::swap;
  swap(s1, s2);
  ExpectElements(s1, {0, 1});
  ExpectElements(s2, {});
}

TEST(FlatSetTest, NoncopyableInsertAndEmplace) {
  flat_set<std::unique_ptr<int>, TransparentCmp> s;
  s.insert(std::make_unique<int>(0));
  s.insert(std::make_unique<int>(5));
  s.insert(std::make_unique<int>(5));
  s.emplace(new int(7));
  s.emplace(new int(3));
  // We do not leak memory, even if emplace fails (note that this is not
  // generally guaranteed by std::set/map).
  s.emplace(new int(3));
  ExpectElements(s, {0, 3, 5, 7});

  // Insert with hint:
  s.insert(s.begin() + 1, std::make_unique<int>(2));
  s.insert(s.begin() + 2, std::make_unique<int>(2));  // already exists
  s.insert(s.begin(), std::make_unique<int>(8));      // incorrect hint
  ExpectElements(s, {0, 2, 3, 5, 7, 8});
  // emplace_hint
  s.emplace_hint(s.begin() + 1, std::make_unique<int>(1));
  s.emplace_hint(s.begin() + 2, std::make_unique<int>(1));  // already exists
  s.emplace_hint(s.begin(), std::make_unique<int>(9));      // incorrect hint
  ExpectElements(s, {0, 1, 2, 3, 5, 7, 8, 9});
}

// Tests for stateful comparator.

TEST(FlatSetTest, StatefulComparator) {
  // Lambda has no default constructor, so this test guarantees that we do not
  // accidentally try to default-construct a comparator.
  int calls = 0;
  auto cmp = [&calls](int x, int y) {
    ++calls;
    return x < y;
  };
  using Cmp = decltype(cmp);

  // Create flat_sets using all constructors, with the same stateful comparator.
  flat_set<int, Cmp> s1(cmp);
  flat_set<int, Cmp> s2(s1.begin(), s1.begin(), cmp);
  flat_set<int, Cmp> s3({}, cmp);
  flat_set<int, Cmp> s4 = s1;
  for (auto* s : {&s1, &s2, &s3, &s4}) {
    s->insert(0);
    EXPECT_EQ(calls, 0);
    s->insert(1);
    EXPECT_GT(calls, 0);
    calls = 0;
    EXPECT_EQ(1, s->erase(0));
    EXPECT_GT(calls, 0);
    calls = 0;
  }
}

// Test that the moved-from set is in valid state, even if moved-from comparator
// is not (as is the case for std::function).
TEST(FlatSetTest, MoveCopyAssignStatefulComparator) {
  using Cmp = std::function<bool(int, int)>;
  flat_set<int, Cmp> s1(std::greater<int>{});
  flat_set<int, Cmp> s2(std::greater<int>{});
  flat_set<int, Cmp> s3(s1);
  flat_set<int, Cmp> s4(std::move(s1));
  flat_set<int, Cmp> s5, s6;
  s5 = s2;
  s6 = std::move(s2);
  // All the sets use the 'greater' comparator (though in case of moved-from
  // sets, this is an implementation detail; we only guarantee a valid state).
  for (auto* s :
       {&s1, &s2, &s3, &s4, &s5, &s6}) {  // NOLINT misc-use-after-move
    s->insert({2, 3});
    ExpectElements(*s, {3, 2});
  }
}

// Tests for gtl::sorted_unique_container_t constructor.
TEST(FlatSetTest, SortedUniqueContainerConstructor) {
  // Using unique_ptrs guarantees we do not incur additional copies.
  flat_set<std::unique_ptr<int>, TransparentCmp> s1(sorted_unique_container,
                                                    UniquePtrs({1, 2, 3}));
  flat_set<std::unique_ptr<int>, TransparentCmp> s2(
      sorted_unique_container, TransparentCmp(), UniquePtrs({1, 2, 3}));
  ExpectElements(s1, {1, 2, 3});
  ExpectElements(s2, {1, 2, 3});

  // Try a more complex vector constructor.
  std::vector<int> v = {3, 2, 1};
  flat_set<int, std::greater<int>> s3(sorted_unique_container, v.begin(),
                                      v.end(), std::allocator<int>());
  flat_set<int, std::greater<int>> s4(sorted_unique_container,
                                      std::greater<int>(), v.begin(), v.end(),
                                      std::allocator<int>());
  ExpectElements(s3, {3, 2, 1});
  ExpectElements(s4, {3, 2, 1});
}

#if GTEST_HAS_DEATH_TEST
TEST(FlatSetTest, SortedUniqueContainerDeathTest) {
  std::vector<int> ordered = {1, 2, 3};
  std::vector<int> reversed = {3, 2, 1};
  std::vector<int> repeated = {7, 7};

  flat_set<int> s1(sorted_unique_container, ordered);
  for (const auto& v : {reversed, repeated}) {
    GLOOP_ASSERT_DEBUG_DEATH(flat_set<int>(sorted_unique_container, v),
                             "check_invariants");
  }

  // Non-default comparator.
  flat_set<int, std::greater<int>> s2(sorted_unique_container, reversed);
  flat_set<int, std::greater<int>> s3(sorted_unique_container,
                                      std::greater<int>(), reversed);
  for (const auto& v : {ordered, repeated}) {
    GLOOP_ASSERT_DEBUG_DEATH(
        (flat_set<int, std::greater<int>>(sorted_unique_container, v)),
        "check_invariants");
    GLOOP_ASSERT_DEBUG_DEATH(
        (flat_set<int, std::greater<int>>(sorted_unique_container,
                                          std::greater<int>(), v)),
        "check_invariants");
  }
}
#endif

TEST(FlatSetTest, SortedUniqueStdFunctionComparator) {
  flat_set<std::unique_ptr<int>,
           std::function<bool(const std::unique_ptr<int>&,
                              const std::unique_ptr<int>&)>>
      s(sorted_unique_container, TransparentCmp(), UniquePtrs({1, 2, 3}));
  ExpectElements(s, {1, 2, 3});
}

TEST(FlatSetTest, SortedUniqueContainerSpan) {
  int array[] = {1, 2, 3};
  flat_set<int, std::less<int>, absl::Span<int>> s(sorted_unique_container,
                                                   array, 3);
  ExpectElements(s, {1, 2, 3});
  EXPECT_EQ(&*s.begin(), array);
}

// Basic test for stateful allocators.
TEST(FlatSetTest, StatefulAllocator) {
  int64_t bytes = 0;
  flat_set<int, std::less<int>, std::vector<int, STLCountingAllocator<int>>> s(
      sorted_unique_container, STLCountingAllocator<int>(&bytes));
  EXPECT_EQ(bytes, 0);
  s.insert(7);
  EXPECT_GT(bytes, 0);
  bytes = 0;
  auto s2 = s;
  EXPECT_GT(bytes, 0);
}

TEST(FlatSetTest, StdArrayRep) {
  // shared_ptr allows easily testing move behavior of flat_set due to its
  // well-defined move semantics.
  std::array<std::shared_ptr<int>, 2> array = {
      {std::make_shared<int>(1), std::make_shared<int>(2)}};
  flat_set<int, TransparentCmp, std::array<std::shared_ptr<int>, 2>> s(
      sorted_unique_container, std::move(array));

  ExpectElements(s, {1, 2});

  // For std::array, we copy the Rep instead of moving it. See
  // internal_flat::Impl for rationale.
  auto s1(std::move(s));
  ExpectElements(s, {1, 2});  // NOLINT misc-use-after-move
  ExpectElements(s1, {1, 2});
  s1 = std::move(s);
  ExpectElements(s, {1, 2});  // NOLINT misc-use-after-move
  ExpectElements(s1, {1, 2});
  EXPECT_EQ(*s.begin(), *s1.begin());
}

#if GTEST_HAS_DEATH_TEST
TEST(FlatSetTest, StdArrayRepTest) {
  // Do not allow default construction where it doesn't make sense.
  GLOOP_ASSERT_DEBUG_DEATH(
      (flat_set<int, std::less<int>, std::array<int, 7>>()),
      "check_invariants");
}
#endif

TEST(FlatSetTest, Hash) {
  using S = gtl::flat_set<int>;
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      {S{},                                                   //
       S{0}, S{1}, S{2},                                      //
       S{0, 1}, S{1, 0}, S{0, 2}, S{2, 0}, S{1, 2}, S{2, 1},  //
       S{0, 1, 2}}));
}

TEST(FlatSetsetTest, EraseIteratorTransparentCmp) {
  flat_set<int, std::less<>> s = {1, 2, 3};
  s.erase(s.begin());
  EXPECT_THAT(s, ElementsAre(2, 3));
}

TEST(FlatSetTest, CopiesInputComparator) {
  struct Cmp {
    int state;
    bool operator==(const Cmp& other) const { return state == other.state; }
    bool operator!=(const Cmp& other) const { return state != other.state; }
    bool operator()(int left, int right) const { return left < right; }
  };
  Cmp cmp1 = {1};
  Cmp cmp2 = {2};
  ASSERT_NE(cmp1, cmp2);

  flat_set<int, Cmp> s1(cmp1);
  flat_set<int, Cmp> s2(cmp2);
  EXPECT_EQ(s1.key_comp(), cmp1);
  EXPECT_EQ(s2.key_comp(), cmp2);
  EXPECT_EQ(s1.value_comp(), cmp1);
  EXPECT_EQ(s2.value_comp(), cmp2);
}

constexpr auto DoMoveConstruction(
    gtl::flat_set<int, std::less<int>, absl::Span<const int>> object) {
  return object;
}

TEST(FlatSetTest, ConstexprConstructor) {
  // Empty
  constexpr gtl::flat_set<int, std::less<int>, std::array<int, 0>> kSetEmpty;
  EXPECT_THAT(kSetEmpty, ElementsAre());

  static constexpr std::array<int, 4> kArray{{1, 4, 17, 222}};

  const auto tester = [](const auto& set) {
    ASSERT_TRUE(set.find(4) != set.end());
    EXPECT_THAT(*set.find(4), 4);
    EXPECT_THAT(set, ElementsAre(1, 4, 17, 222));
  };

  // Using std::array:
  constexpr gtl::flat_set<int, std::less<int>, std::array<int, 4>> kSetArray(
      gtl::sorted_unique_container, kArray);
  tester(kSetArray);

  // Using absl::Span:
  constexpr gtl::flat_set<int, std::less<int>, absl::Span<const int>> kSetSpan(
      gtl::sorted_unique_container, kArray);
  tester(kSetSpan);

  // Copy:
  constexpr gtl::flat_set<int, std::less<int>, absl::Span<const int>>
      kSetSpanCopy(kSetSpan);
  tester(kSetSpanCopy);

  // Move:
  constexpr auto kSetSpanMove = DoMoveConstruction(kSetSpan);
  tester(kSetSpanMove);
}

struct OddsFirst {
  constexpr bool operator()(int a, int b) const {
    return std::pair(a % 2 == 0, a) < std::pair(b % 2 == 0, b);
  }
};

TEST(FlatSetTest, ConstexprFactoryExplicit) {
  constexpr auto kSetEmpty = gtl::fixed_flat_set_of<int>({});
  EXPECT_THAT(kSetEmpty, ElementsAre());

  constexpr auto kSetOne = gtl::fixed_flat_set_of<int>({1});
  EXPECT_THAT(kSetOne, ElementsAre(1));

  constexpr auto kSetMany = gtl::fixed_flat_set_of<int>({3, 4, 1, 5});
  ASSERT_TRUE(kSetMany.find(4) != kSetMany.end());
  EXPECT_THAT(*kSetMany.find(4), 4);
  EXPECT_THAT(kSetMany, ElementsAre(1, 3, 4, 5));

  constexpr auto kSetCustomLess =
      gtl::fixed_flat_set_of<int>({1, 2, 3}, OddsFirst{});
  EXPECT_THAT(kSetCustomLess, ElementsAre(1, 3, 2));
}

TEST(FlatSetTest, ConstexprFactoryImplicit) {
  // Can't do empty implicitly.
  constexpr auto kSetOne = gtl::fixed_flat_set_of({1});
  EXPECT_THAT(kSetOne, ElementsAre(1));

  constexpr auto kSetMany = gtl::fixed_flat_set_of({3, 4, 1, 5});
  ASSERT_TRUE(kSetMany.find(4) != kSetMany.end());
  EXPECT_THAT(*kSetMany.find(4), 4);
  EXPECT_THAT(kSetMany, ElementsAre(1, 3, 4, 5));

  constexpr auto kSetCustomLess =
      gtl::fixed_flat_set_of({1, 2, 3}, OddsFirst{});
  EXPECT_THAT(kSetCustomLess, ElementsAre(1, 3, 2));
}

TEST(FlatSetTest, ConstexprFactoryArrayExplicit) {
  constexpr std::array<int, 0> kEmpty = {};
  constexpr auto kSetEmpty = gtl::fixed_flat_set_of<int>(kEmpty);
  EXPECT_THAT(kSetEmpty, ElementsAre());

  constexpr std::array<int, 1> kOne = {1};
  constexpr auto kSetOne = gtl::fixed_flat_set_of<int>(kOne);
  EXPECT_THAT(kSetOne, ElementsAre(1));

  constexpr std::array<int, 4> kMany = {3, 4, 1, 5};
  constexpr auto kSetMany = gtl::fixed_flat_set_of<int>(kMany);
  ASSERT_TRUE(kSetMany.find(4) != kSetMany.end());
  EXPECT_THAT(*kSetMany.find(4), 4);
  EXPECT_THAT(kSetMany, ElementsAre(1, 3, 4, 5));

  constexpr std::array<int, 3> kCustomLess = {1, 2, 3};
  constexpr auto kSetCustomLess =
      gtl::fixed_flat_set_of<int>(kCustomLess, OddsFirst{});
  EXPECT_THAT(kSetCustomLess, ElementsAre(1, 3, 2));
}

TEST(FlatSetTest, ConstexprFactoryArrayImplicit) {
  constexpr std::array<int, 0> kEmpty = {};
  constexpr auto kSetEmpty = gtl::fixed_flat_set_of(kEmpty);
  EXPECT_THAT(kSetEmpty, ElementsAre());

  constexpr std::array<int, 1> kOne = {1};
  constexpr auto kSetOne = gtl::fixed_flat_set_of(kOne);
  EXPECT_THAT(kSetOne, ElementsAre(1));

  constexpr std::array<int, 4> kMany = {3, 4, 1, 5};
  constexpr auto kSetMany = gtl::fixed_flat_set_of(kMany);
  ASSERT_TRUE(kSetMany.find(4) != kSetMany.end());
  EXPECT_THAT(*kSetMany.find(4), 4);
  EXPECT_THAT(kSetMany, ElementsAre(1, 3, 4, 5));

  constexpr std::array<int, 3> kCustomLess = {1, 2, 3};
  constexpr auto kSetCustomLess =
      gtl::fixed_flat_set_of(kCustomLess, OddsFirst{});
  EXPECT_THAT(kSetCustomLess, ElementsAre(1, 3, 2));
}

template <int N>
struct Data {
  unsigned values[N]{};
};
template <int N>
constexpr Data<N> MakeSortedData() {
  Data<N> data;
  for (int i = 0; i < N; ++i) {
    data.values[i] = i;
  }
  return data;
}
template <int N>
constexpr Data<N> MakeUnsortedData() {
  Data<N> data;
  // A couple of primes.
  // This will give unique values that are unsorted mod 2^32.
  constexpr unsigned kMul = 1979;
  constexpr unsigned kMod = (unsigned{1} << 31) - 1;
  unsigned v = 1;
  for (int i = 0; i < N; ++i) {
    data.values[i] = v;
    v = v * kMul % kMod;
  }
  return data;
}

TEST(FlatSetTest, ConstexprLimitsSorted) {
  constexpr size_t size = 25000;
  constexpr auto data = MakeSortedData<size>();
  constexpr auto set = gtl::fixed_flat_set_of(data.values);
  EXPECT_EQ(set.size(), size);
}

TEST(FlatSetTest, ConstexprLimitsUnsorted) {
  constexpr size_t size = 5000;
  constexpr auto data = MakeUnsortedData<size>();
  constexpr auto set = gtl::fixed_flat_set_of(data.values);
  EXPECT_EQ(set.size(), size);
}

TEST(FlatSetTest, NonConstexprOnDuplicates) {
  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return gtl::fixed_flat_set_of<int>({0, 1, 2}); }));
  EXPECT_FALSE(absl::meta_internal::HasConstexprEvaluation(
      [] { return gtl::fixed_flat_set_of<int>({0, 1, 1}); }));
  EXPECT_FALSE(absl::meta_internal::HasConstexprEvaluation([] {
    std::array<int, 3> kArr = {0, 1, 1};
    return gtl::fixed_flat_set_of<int>(kArr);
  }));
}

TEST(FlatSetTest, ConstexprKeyComp) {
  static constexpr auto kSet = gtl::fixed_flat_set_of<int>({1});

  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return kSet.key_comp(); }));
}

TEST(FlatSetTest, ConstexprValueComp) {
  static constexpr auto kSet = gtl::fixed_flat_set_of<int>({1});

  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return kSet.value_comp(); }));
}

TEST(FlatSetTest, ConstexprMemberFunctionsCapacity) {
  static constexpr auto kSet = gtl::fixed_flat_set_of<int>({1});

  EXPECT_TRUE(
      absl::meta_internal::HasConstexprEvaluation([] { return kSet.empty(); }));

  EXPECT_TRUE(
      absl::meta_internal::HasConstexprEvaluation([] { return kSet.size(); }));

  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return kSet.max_size(); }));
}

TEST(FlatSetTest, ConstexprMemberFunctionsIterators) {
  static constexpr auto kSet = gtl::fixed_flat_set_of<int>({1});

  EXPECT_TRUE(
      absl::meta_internal::HasConstexprEvaluation([] { return kSet.begin(); }));

  EXPECT_TRUE(
      absl::meta_internal::HasConstexprEvaluation([] { return kSet.end(); }));

  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return kSet.cbegin(); }));

  EXPECT_TRUE(
      absl::meta_internal::HasConstexprEvaluation([] { return kSet.cend(); }));

  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return kSet.rbegin(); }));

  EXPECT_TRUE(
      absl::meta_internal::HasConstexprEvaluation([] { return kSet.rend(); }));

  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return kSet.crbegin(); }));

  EXPECT_TRUE(
      absl::meta_internal::HasConstexprEvaluation([] { return kSet.crend(); }));
}

TEST(FlatSetTest, ConstexprPairAsKey) {
  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation([] {
    return gtl::fixed_flat_set_of<std::pair<int, int>>(
        {{0, 0}, {1, 1}, {2, 2}});
  }));
  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation([] {
    return gtl::fixed_flat_set_of<std::pair<int, int>>(
        {{2, 2}, {1, 1}, {0, 0}});
  }));
}

TEST(FlatSetTest, ConstexprTupleAsKey) {
  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation([] {
    return gtl::fixed_flat_set_of<std::tuple<int, int>>(
        {{0, 0}, {1, 1}, {2, 2}});
  }));
  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation([] {
    return gtl::fixed_flat_set_of<std::tuple<int, int>>(
        {{2, 2}, {1, 1}, {0, 0}});
  }));
}

// constexpr flat_set operations require constexpr STL algorithms.
#ifdef __cpp_lib_constexpr_algorithms
using Set = gtl::flat_set<int>;

TEST(FlatSetTest, ConstexprFind) {
  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return Set().find(1); }));
}

TEST(FlatSetTest, ConstexprCount) {
  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return Set().count(1); }));
}

TEST(FlatSetTest, ConstexprContains) {
  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return Set().contains(1); }));
}

TEST(FlatSetTest, ConstexprLowerBound) {
  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return Set().lower_bound(1); }));
}

TEST(FlatSetTest, ConstexprUpperBound) {
  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return Set().upper_bound(1); }));
}

TEST(FlatSetTest, ConstexprEqualRange) {
  EXPECT_TRUE(absl::meta_internal::HasConstexprEvaluation(
      [] { return Set().equal_range(1); }));
}
#endif  // __cpp_lib_constexpr_algorithms

TEST(FlatSetTest, NoexceptMove) {
  EXPECT_TRUE(std::is_nothrow_move_constructible_v<gtl::flat_set<int>>);
  EXPECT_TRUE(std::is_nothrow_move_assignable_v<gtl::flat_set<int>>);

  struct LessWithThrowingCopy : std::less<int> {
    LessWithThrowingCopy(const LessWithThrowingCopy&) {}
    LessWithThrowingCopy& operator=(const LessWithThrowingCopy&) {
      return *this;
    }
  };
  EXPECT_TRUE((std::is_nothrow_move_constructible_v<
               gtl::flat_set<int, LessWithThrowingCopy>>));
  EXPECT_TRUE((std::is_nothrow_move_assignable_v<
               gtl::flat_set<int, LessWithThrowingCopy>>));

  using NoClearNoExceptCopy = std::array<int, 3>;
  EXPECT_TRUE((std::is_nothrow_move_constructible_v<
               gtl::flat_set<int, std::less<>, NoClearNoExceptCopy>>));
  EXPECT_TRUE((std::is_nothrow_move_assignable_v<
               gtl::flat_set<int, std::less<>, NoClearNoExceptCopy>>));

  struct NoClearThrowingCopy : std::array<int, 3> {
    NoClearThrowingCopy(const NoClearThrowingCopy&) {}
    NoClearThrowingCopy& operator=(const NoClearThrowingCopy&) { return *this; }
  };
  EXPECT_TRUE((std::is_nothrow_move_constructible_v<
               gtl::flat_set<int, std::less<>, NoClearThrowingCopy>>));
  EXPECT_TRUE((std::is_nothrow_move_assignable_v<
               gtl::flat_set<int, std::less<>, NoClearThrowingCopy>>));
}

template <class, class = void>
struct IsTransparent : std::false_type {};
template <class T>
struct IsTransparent<T, absl::void_t<typename T::is_transparent>>
    : std::true_type {};

template <typename Cmp>
inline constexpr bool kIsTransparent = IsTransparent<Cmp>::value;

TEST(FlatSetTest, StringCmpTransparentByDefault) {
  EXPECT_TRUE(kIsTransparent<flat_set<std::string>::key_compare>);
  EXPECT_TRUE(kIsTransparent<flat_set<absl::string_view>::key_compare>);
  EXPECT_TRUE(kIsTransparent<flat_set<absl::Cord>::key_compare>);
}

//
// Benchmarks
//

template <class Set>
Set Build(absl::Span<const typename Set::value_type> vs) {
  Set s;
  for (const auto& v : vs) {
    s.insert(v);
  }
  return s;
}

template <typename Set>
Set MakeSetOfSize(int size) {
  Set set;
  // Insert only odds. This allows us to easily find with hits and misses.
  while (set.size() < size) set.insert(set.size() * 2 + 1);
  return set;
}

template <typename Set>
std::vector<int> MakeKeys(const Set& set, bool hits, bool misses) {
  std::vector<int> v;
  // odds are hits, evens are misses
  if (hits) {
    for (int i : set) v.push_back(i);
  }
  if (misses) {
    for (int i : set) v.push_back(i - 1);
  }

  VLOG(1) << "Random seed: '" << absl::GetFlag(FLAGS_benchmark_random_seed)
          << "'";
  std::mt19937_64 rng(absl::GetFlag(FLAGS_benchmark_random_seed));
  std::shuffle(v.begin(), v.end(), rng);

  return v;
}

template <class Set>
void BM_Find(::benchmark::State& state) {
  const int size = state.range(0);
  const auto set = MakeSetOfSize<Set>(size);
  const auto keys = MakeKeys(set, true, true);

  while (state.KeepRunningBatch(keys.size())) {
    for (int key : keys) {
      benchmark::DoNotOptimize(set);
      benchmark::DoNotOptimize(set.find(key));
    }
  }
}

void ConfigureFind(benchmark::Benchmark* b) { b->Range(1, 1 << 16); }

BENCHMARK(BM_Find<flat_set<uint64_t>>)->Apply(ConfigureFind);
BENCHMARK(BM_Find<std::set<uint64_t>>)->Apply(ConfigureFind);
BENCHMARK(BM_Find<absl::btree_set<uint64_t>>)->Apply(ConfigureFind);
BENCHMARK(BM_Find<absl::flat_hash_set<uint64_t>>)->Apply(ConfigureFind);
BENCHMARK(BM_Find<std::unordered_set<uint64_t>>)->Apply(ConfigureFind);

template <class Set>
void BM_Contains(::benchmark::State& state) {
  const int size = state.range(0);
  const auto set = MakeSetOfSize<Set>(size);
  const auto keys = MakeKeys(set, true, true);

  while (state.KeepRunningBatch(keys.size())) {
    for (int key : keys) {
      benchmark::DoNotOptimize(set);
      benchmark::DoNotOptimize(set.contains(key));
    }
  }
}

BENCHMARK(BM_Contains<flat_set<uint64_t>>)->Apply(ConfigureFind);
BENCHMARK(BM_Contains<absl::btree_set<uint64_t>>)->Apply(ConfigureFind);
BENCHMARK(BM_Contains<absl::flat_hash_set<uint64_t>>)->Apply(ConfigureFind);

template <class Set>
void BM_InsertAndFind(::benchmark::State& state) {
  int size = state.range(0);
  int lookups_per_insert = state.range(1);

  VLOG(1) << "Random seed: '" << absl::GetFlag(FLAGS_benchmark_random_seed)
          << "'";
  std::mt19937_64 rng(absl::GetFlag(FLAGS_benchmark_random_seed));

  std::vector<uint64_t> pool;
  pool.reserve(size);
  for (int i = 0; i < size; ++i) {
    pool.push_back(absl::Uniform<uint64_t>(rng));
  }

  for (auto _ : state) {
    Set s(Build<Set>(pool));
    for (int lookup = 0; lookup < lookups_per_insert; ++lookup) {
      for (auto value : pool) {
        benchmark::DoNotOptimize(s);
        benchmark::DoNotOptimize(s.find(value));
      }
    }
  }
}

// Benchmarks are run for the following combination or arguments:
// {1, 8, 64, 4096} x {1, 8}
BENCHMARK(BM_InsertAndFind<flat_set<uint64_t>>)->RangePair(1, 1 << 12, 1, 8);
BENCHMARK(BM_InsertAndFind<std::set<uint64_t>>)->RangePair(1, 1 << 12, 1, 8);
BENCHMARK(BM_InsertAndFind<absl::btree_set<uint64_t>>)
    ->RangePair(1, 1 << 12, 1, 8);
BENCHMARK(BM_InsertAndFind<absl::flat_hash_set<uint64_t>>)
    ->RangePair(1, 1 << 12, 1, 8);
BENCHMARK(BM_InsertAndFind<std::unordered_set<uint64_t>>)
    ->RangePair(1, 1 << 12, 1, 8);

// Make a set with `n` ints.
// We use the first `n` odd numbers to make it easy to have uniform hit/miss
// lookups.
template <int n>
constexpr auto MakeConstexprTestSet() {
  int values[n]{};
  for (int i = 0; i < n; ++i) {
    values[i] = 2 * i + 1;
  }
  return gtl::fixed_flat_set_of<int>(values);
}

template <typename Op>
void RunOpOnConstexprSet(::benchmark::State& state, Op op) {
  gtl::switch_index<1, 17>(
      [&](auto idx) {
        constexpr auto set = MakeConstexprTestSet<decltype(idx)::value>();

        const int max = 2 + set.end()[-1];
        while (state.KeepRunningBatch(max)) {
          for (int i = 0; i < max; ++i) {
            benchmark::DoNotOptimize(i);
            benchmark::DoNotOptimize(op(set, i));
          }
        }
      },
      state.range(0));
}

void BM_FindConstexpr(::benchmark::State& state) {
  RunOpOnConstexprSet(state,
                      [](const auto& set, int i) { return set.find(i); });
}
BENCHMARK(BM_FindConstexpr)->Arg(1)->Arg(2)->Arg(3)->Arg(4)->Arg(8)->Arg(16);

void BM_ContainsConstexpr(::benchmark::State& state) {
  RunOpOnConstexprSet(state,
                      [](const auto& set, int i) { return set.contains(i); });
}
BENCHMARK(BM_ContainsConstexpr)
    ->Arg(1)
    ->Arg(2)
    ->Arg(3)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16);

// A set with values that are harder for the compiler to optimize numerically.
// Arbitrarily chose the list of the first perfect numbers.
constexpr auto kLargeValueSet =
    gtl::fixed_flat_set_of<uint64_t>({6, 28, 496, 8128, 33550336, 8589869056,
                                      137438691328, 2305843008139952128});
void BM_FindConstexprLarge(::benchmark::State& state) {
  while (state.KeepRunningBatch(kLargeValueSet.size() * 2)) {
    for (auto i : kLargeValueSet) {
      // One hit, one miss
      benchmark::DoNotOptimize(i);
      benchmark::DoNotOptimize(kLargeValueSet.find(i));
      benchmark::DoNotOptimize(i);
      benchmark::DoNotOptimize(kLargeValueSet.find(i + 1));
    }
  }
}
BENCHMARK(BM_FindConstexprLarge);

void BM_ContainsConstexprLarge(::benchmark::State& state) {
  while (state.KeepRunningBatch(kLargeValueSet.size() * 2)) {
    for (auto i : kLargeValueSet) {
      // One hit, one miss
      benchmark::DoNotOptimize(i);
      benchmark::DoNotOptimize(kLargeValueSet.contains(i));
      benchmark::DoNotOptimize(i);
      benchmark::DoNotOptimize(kLargeValueSet.contains(i + 1));
    }
  }
}
BENCHMARK(BM_ContainsConstexprLarge);

// TODO: Need a benchmark showing that insert with hint is actually
// faster than the regular one.

}  // namespace
}  // namespace gtl

// Codegen thunks to allow for dumping the code with gdb or alike.
auto FindConstexpr1Codegen(int i) {
  static constexpr auto set = gtl::MakeConstexprTestSet<1>();
  return set.find(i);
}
auto FindConstexpr3Codegen(int i) {
  static constexpr auto set = gtl::MakeConstexprTestSet<3>();
  return set.find(i);
}
auto FindConstexpr7Codegen(int i) {
  static constexpr auto set = gtl::MakeConstexprTestSet<7>();
  return set.find(i);
}
auto ContainsConstexpr1Codegen(int i) {
  static constexpr auto set = gtl::MakeConstexprTestSet<1>();
  return set.contains(i);
}
auto ContainsConstexpr3Codegen(int i) {
  static constexpr auto set = gtl::MakeConstexprTestSet<3>();
  return set.contains(i);
}
auto ContainsConstexpr7Codegen(int i) {
  static constexpr auto set = gtl::MakeConstexprTestSet<7>();
  return set.contains(i);
}
auto FindConstexprLargeCodegen(uint64_t i) {
  return gtl::kLargeValueSet.find(i);
}
auto ContainsConstexprLargeCodegen(uint64_t i) {
  return gtl::kLargeValueSet.contains(i);
}
auto FindDynamicCodegen(const gtl::flat_set<int>& set, int i) {
  return set.find(i);
}
auto ContainsDynamicCodegen(const gtl::flat_set<int>& set, int i) {
  return set.contains(i);
}
// Make sure they are odr-used to force the compiler to generate them.
int odr = (benchmark::DoNotOptimize(std::make_tuple(
               FindConstexpr1Codegen, FindConstexpr3Codegen,
               FindConstexpr7Codegen, ContainsConstexpr1Codegen,
               ContainsConstexpr3Codegen, ContainsConstexpr7Codegen,
               FindConstexprLargeCodegen, ContainsConstexprLargeCodegen,
               FindDynamicCodegen, ContainsDynamicCodegen)),
           1);
