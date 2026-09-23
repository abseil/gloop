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

#include "gloop/util/gtl/small_map.h"

#include <stddef.h>

#ifdef __GLIBCXX__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-W#warnings"
#include <ext/hash_map>
#pragma clang diagnostic pop
#endif
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/container/internal/test_instance_tracker.h"
#include "absl/hash/hash.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gloop/util/gtl/manual_constructor.h"
#include "gloop/util/gtl/map_view.h"
#include "gloop/util/math/round.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace gtl {
namespace {
using ::absl::StrCat;
using ::absl::test_internal::InstanceTracker;      // NOLINT(build/namespaces)
using ::absl::test_internal::MovableOnlyInstance;  // NOLINT(build/namespaces)
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

TEST(SmallMap, General) {
  small_map<std::unordered_map<int, int>> m;

  EXPECT_TRUE(m.empty());

  m[0] = 5;

  EXPECT_FALSE(m.empty());
  EXPECT_EQ(m.size(), 1);

  m[9] = 2;

  EXPECT_FALSE(m.empty());
  EXPECT_EQ(m.size(), 2);

  EXPECT_EQ(m[9], 2);
  EXPECT_EQ(m[0], 5);
  EXPECT_FALSE(m.using_full_map());

  small_map<std::unordered_map<int, int>>::iterator iter(m.begin());
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair(0, 5));
  ++iter;
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair(9, 2));
  ++iter;
  EXPECT_TRUE(iter == m.end());

  m[8] = 23;
  m[1234] = 90;
  m[-5] = 6;

  EXPECT_EQ(m[9], 2);
  EXPECT_EQ(m[0], 5);
  EXPECT_EQ(m[1234], 90);
  EXPECT_EQ(m[8], 23);
  EXPECT_EQ(m[-5], 6);
  EXPECT_EQ(m.size(), 5);
  EXPECT_FALSE(m.empty());
  EXPECT_TRUE(m.using_full_map());

  iter = m.begin();
  for (int i = 0; i < 5; i++) {
    EXPECT_TRUE(iter != m.end());
    ++iter;
  }
  EXPECT_TRUE(iter == m.end());

  const small_map<std::unordered_map<int, int>>& ref = m;
  EXPECT_TRUE(ref.find(1234) != m.end());
  EXPECT_TRUE(ref.find(5678) == m.end());
}

TEST(SmallMap, PostFixIteratorIncrement) {
  small_map<std::unordered_map<int, int>> m;
  m[0] = 5;
  m[2] = 3;

  {
    small_map<std::unordered_map<int, int>>::iterator iter(m.begin());
    small_map<std::unordered_map<int, int>>::iterator last(iter++);
    ++last;
    EXPECT_TRUE(last == iter);
  }

  {
    small_map<std::unordered_map<int, int>>::const_iterator iter(m.begin());
    small_map<std::unordered_map<int, int>>::const_iterator last(iter++);
    ++last;
    EXPECT_TRUE(last == iter);
  }
}

// Based on the General testcase.
TEST(SmallMap, CopyConstructor) {
  small_map<std::unordered_map<int, int>> src;

  {
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
    small_map<std::unordered_map<int, int>> m(src);
    EXPECT_TRUE(m.empty());
  }

  src[0] = 5;

  {
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
    small_map<std::unordered_map<int, int>> m(src);
    EXPECT_FALSE(m.empty());
    EXPECT_EQ(m.size(), 1);
  }

  src[9] = 2;

  {
    small_map<std::unordered_map<int, int>> m(src);
    EXPECT_FALSE(m.empty());
    EXPECT_EQ(m.size(), 2);

    EXPECT_EQ(m[9], 2);
    EXPECT_EQ(m[0], 5);
    EXPECT_FALSE(m.using_full_map());
  }

  src[8] = 23;
  src[1234] = 90;
  src[-5] = 6;

  {
    small_map<std::unordered_map<int, int>> m(src);
    EXPECT_EQ(m[9], 2);
    EXPECT_EQ(m[0], 5);
    EXPECT_EQ(m[1234], 90);
    EXPECT_EQ(m[8], 23);
    EXPECT_EQ(m[-5], 6);
    EXPECT_EQ(m.size(), 5);
    EXPECT_FALSE(m.empty());
    EXPECT_TRUE(m.using_full_map());
  }
}

// Based on the General testcase.
TEST(SmallMap, InitializerListConstructor) {
  using Map = small_map<absl::flat_hash_map<int, int>>;
  {
    Map m{};
    EXPECT_TRUE(m.empty());
  }

  {
    Map m({{0, 5}});
    EXPECT_FALSE(m.empty());
    EXPECT_EQ(m.size(), 1);
  }

  {
    Map m{{0, 5}, {9, 2}};
    EXPECT_THAT(m, UnorderedElementsAre(Pair(9, 2), Pair(0, 5)));
    EXPECT_FALSE(m.using_full_map());
  }

  {
    Map m = {{0, 5}, {9, 2}, {1234, 90}, {-5, 6}, {8, 23}};
    EXPECT_THAT(m, UnorderedElementsAre(Pair(9, 2), Pair(0, 5), Pair(1234, 90),
                                        Pair(8, 23), Pair(-5, 6)));
    EXPECT_TRUE(m.using_full_map());
  }
}

// Based on the General testcase.
TEST(SmallMap, MoveConstructor) {
  small_map<std::map<int, int>> src;

  {
    small_map<std::map<int, int>> m(std::move(src));
    EXPECT_TRUE(m.empty());
    src = std::move(m);
  }

  src[0] = 5;

  {
    small_map<std::map<int, int>> m(std::move(src));
    EXPECT_FALSE(m.empty());
    EXPECT_EQ(m.size(), 1);
    src = std::move(m);
  }

  src[9] = 2;

  {
    small_map<std::map<int, int>> m(std::move(src));
    EXPECT_FALSE(m.empty());
    EXPECT_EQ(m.size(), 2);

    EXPECT_EQ(m[9], 2);
    EXPECT_EQ(m[0], 5);
    EXPECT_FALSE(m.using_full_map());
    src = std::move(m);
  }

  src[8] = 23;
  src[1234] = 90;
  src[-5] = 6;

  {
    small_map<std::map<int, int>> m(std::move(src));
    EXPECT_EQ(m[9], 2);
    EXPECT_EQ(m[0], 5);
    EXPECT_EQ(m[1234], 90);
    EXPECT_EQ(m[8], 23);
    EXPECT_EQ(m[-5], 6);
    EXPECT_EQ(m.size(), 5);
    EXPECT_FALSE(m.empty());
    EXPECT_TRUE(m.using_full_map());
    src = std::move(m);
  }
}

TEST(SmallMap, Swap) {
  using std::swap;

  small_map<std::map<int, int>> b1;
  for (int i = 0; i < 10; ++i) b1[i] = i;
  small_map<std::map<int, int>> b2;
  for (int i = 0; i < 20; ++i) b2[i] = i;
  small_map<std::map<int, int>> s1;
  for (int i = 0; i < 1; ++i) s1[i] = i;
  small_map<std::map<int, int>> s2;
  for (int i = 0; i < 2; ++i) s2[i] = i;

  auto m1 = s1;
  auto m2 = s2;
  m1.swap(m2);
  EXPECT_EQ(m2, s1);
  EXPECT_EQ(m1, s2);

  m1 = s1;
  m2 = b2;
  swap(m1, m2);
  EXPECT_EQ(m1, b2);
  EXPECT_EQ(m2, s1);

  m1 = b1;
  m2 = b2;
  m2.swap(m1);
  EXPECT_EQ(m1, b2);
  EXPECT_EQ(m2, b1);
}

TEST(SmallMap, SwapString) {
  using std::swap;

  small_map<std::map<std::string, std::string>> b1;
  for (int i = 0; i < 10; ++i) b1[StrCat(i)] = StrCat(i);
  small_map<std::map<std::string, std::string>> b2;
  for (int i = 0; i < 20; ++i) b2[StrCat(i)] = StrCat(i);
  small_map<std::map<std::string, std::string>> s1;
  for (int i = 0; i < 1; ++i) s1[StrCat(i)] = StrCat(i);
  small_map<std::map<std::string, std::string>> s2;
  for (int i = 0; i < 2; ++i) s2[StrCat(i)] = StrCat(i);

  auto m1 = s1;
  auto m2 = s2;
  m1.swap(m2);
  EXPECT_EQ(m2, s1);
  EXPECT_EQ(m1, s2);

  m1 = s1;
  m2 = b2;
  swap(m1, m2);
  EXPECT_EQ(m1, b2);
  EXPECT_EQ(m2, s1);

  m1 = b1;
  m2 = b2;
  m2.swap(m1);
  EXPECT_EQ(m1, b2);
  EXPECT_EQ(m2, b1);
}

TEST(SmallMap, SwapLargeString) {
  using std::swap;
  std::string a = "1234567890123456789012345678901234567890";

  small_map<std::map<std::string, std::string>> b1;
  for (int i = 0; i < 10; ++i) b1[StrCat(a, i)] = StrCat(a, i);
  small_map<std::map<std::string, std::string>> b2;
  for (int i = 0; i < 20; ++i) b2[StrCat(a, i)] = StrCat(a, i);
  small_map<std::map<std::string, std::string>> s1;
  for (int i = 0; i < 1; ++i) s1[StrCat(a, i)] = StrCat(a, i);
  small_map<std::map<std::string, std::string>> s2;
  for (int i = 0; i < 2; ++i) s2[StrCat(a, i)] = StrCat(a, i);

  auto m1 = s1;
  auto m2 = s2;
  m1.swap(m2);
  EXPECT_EQ(m2, s1);
  EXPECT_EQ(m1, s2);

  m1 = s1;
  m2 = b2;
  swap(m1, m2);
  EXPECT_EQ(m1, b2);
  EXPECT_EQ(m2, s1);

  m1 = b1;
  m2 = b2;
  m2.swap(m1);
  EXPECT_EQ(m1, b2);
  EXPECT_EQ(m2, b1);
}

MATCHER_P(MoveOnlyValue, value,
          std::string("is a MoveOnly object with value member equal to ") +
              testing::PrintToString(value)) {
  return arg.value() == value;
}

// Test for behavior when contents can only be moved, but not copied or
// default-constructed.
TEST(SmallMap, MoveOnlyContents) {
  InstanceTracker tracker;
  using MoveOnlyMap = small_map<std::map<int, MovableOnlyInstance>>;
  using ::testing::ElementsAre;
  using ::testing::Pair;
  using ::testing::UnorderedElementsAre;
  tracker.ResetCopiesMovesSwaps();
  {
    MoveOnlyMap src;
    // We can insert() a temporary (rvalue) pair, but we cannot use
    // operator[] with a non-default-constructible value.
    src.insert({0, MovableOnlyInstance(1)});
    src.insert({1, MovableOnlyInstance(2)});
    ASSERT_FALSE(src.using_full_map());
    EXPECT_THAT(src, UnorderedElementsAre(Pair(0, MoveOnlyValue(1)),
                                          Pair(1, MoveOnlyValue(2))));
    MoveOnlyMap dst(std::move(src));
    EXPECT_THAT(dst, UnorderedElementsAre(Pair(0, MoveOnlyValue(1)),
                                          Pair(1, MoveOnlyValue(2))));
  }
  ASSERT_EQ(0, tracker.instances()) << "Not all members were destroyed.";
  {
    MoveOnlyMap src;
    src.insert({0, MovableOnlyInstance(1)});
    src.insert({1, MovableOnlyInstance(2)});
    EXPECT_THAT(src, UnorderedElementsAre(Pair(0, MoveOnlyValue(1)),
                                          Pair(1, MoveOnlyValue(2))));
    MoveOnlyMap dst;
    dst = std::move(src);
    EXPECT_THAT(dst, UnorderedElementsAre(Pair(0, MoveOnlyValue(1)),
                                          Pair(1, MoveOnlyValue(2))));
  }
  ASSERT_EQ(0, tracker.instances()) << "Not all members were destroyed.";
}

// Test for behavior when contents can be moved or default-constructed, but
// not copied. std::unique_ptr follows this, so we just use that.
TEST(SmallMap, DefaultAndMoveOnlyContents) {
  using MoveOrDefaultMap = small_map<std::map<int, std::unique_ptr<int>>>;
  using ::std::make_unique;
  using ::testing::ElementsAre;
  using ::testing::Eq;
  using ::testing::Pair;
  using ::testing::Pointee;
  using ::testing::UnorderedElementsAre;
  MoveOrDefaultMap src;
  // We can insert() a temporary (rvalue) pair or use operator[] because
  // std::unique_ptr<> is default constructible.
  src.insert({0, std::make_unique<int>(1)});
  src.insert({1, std::make_unique<int>(2)});
  src[2] = std::make_unique<int>(99);
  // Assignment of move-only type should work as-is.
  src[2] = std::make_unique<int>(3);
  ASSERT_FALSE(src.using_full_map());
  EXPECT_THAT(src, UnorderedElementsAre(Pair(0, Pointee(Eq(1))),
                                        Pair(1, Pointee(Eq(2))),
                                        Pair(2, Pointee(Eq(3)))));
  MoveOrDefaultMap dst(std::move(src));
  EXPECT_THAT(dst, UnorderedElementsAre(Pair(0, Pointee(Eq(1))),
                                        Pair(1, Pointee(Eq(2))),
                                        Pair(2, Pointee(Eq(3)))));
  src.clear();
  EXPECT_THAT(src, ElementsAre());
  src = std::move(dst);
  EXPECT_THAT(src, UnorderedElementsAre(Pair(0, Pointee(Eq(1))),
                                        Pair(1, Pointee(Eq(2))),
                                        Pair(2, Pointee(Eq(3)))));
}

TEST(SmallMap, MovesToBigMap) {
  using MoveOnlyMap = small_map<std::map<int, MovableOnlyInstance>>;
  using ::testing::ElementsAre;
  using ::testing::Pair;
  using ::testing::UnorderedElementsAre;
  MoveOnlyMap src;
  for (int i = 0; i < 5; ++i) {
    src.insert({i, MovableOnlyInstance(i + 10)});
  }
  ASSERT_TRUE(src.using_full_map());
  EXPECT_THAT(src, UnorderedElementsAre(
                       Pair(0, MoveOnlyValue(10)), Pair(1, MoveOnlyValue(11)),
                       Pair(2, MoveOnlyValue(12)), Pair(3, MoveOnlyValue(13)),
                       Pair(4, MoveOnlyValue(14))));
  MoveOnlyMap dst(std::move(src));
  EXPECT_THAT(dst, UnorderedElementsAre(
                       Pair(0, MoveOnlyValue(10)), Pair(1, MoveOnlyValue(11)),
                       Pair(2, MoveOnlyValue(12)), Pair(3, MoveOnlyValue(13)),
                       Pair(4, MoveOnlyValue(14))));
  src.clear();
  EXPECT_THAT(src, ElementsAre());
  src = std::move(dst);
  EXPECT_THAT(src, UnorderedElementsAre(
                       Pair(0, MoveOnlyValue(10)), Pair(1, MoveOnlyValue(11)),
                       Pair(2, MoveOnlyValue(12)), Pair(3, MoveOnlyValue(13)),
                       Pair(4, MoveOnlyValue(14))));
}

template <class inner>
static void SmallMapToMap(small_map<inner> const& src, inner* dest) {
  typename small_map<inner>::const_iterator it;
  for (it = src.begin(); it != src.end(); ++it) {
    dest->insert(std::make_pair(it->first, it->second));
  }
}

template <class inner>
static bool SmallMapEqual(small_map<inner> const& a,
                          small_map<inner> const& b) {
  inner ia, ib;
  SmallMapToMap(a, &ia);
  SmallMapToMap(b, &ib);
  return ia == ib;
}

TEST(SmallMap, AssignmentOperator) {
  small_map<std::unordered_map<int, int>> src_small;
  small_map<std::unordered_map<int, int>> src_large;

  src_small[1] = 20;
  src_small[2] = 21;
  src_small[3] = 22;
  EXPECT_FALSE(src_small.using_full_map());

  src_large[1] = 20;
  src_large[2] = 21;
  src_large[3] = 22;
  src_large[5] = 23;
  src_large[6] = 24;
  src_large[7] = 25;
  EXPECT_TRUE(src_large.using_full_map());

  // Assignments to empty.
  small_map<std::unordered_map<int, int>> dest_small;
  dest_small = src_small;
  EXPECT_TRUE(SmallMapEqual(dest_small, src_small));
  EXPECT_EQ(dest_small.using_full_map(), src_small.using_full_map());

  small_map<std::unordered_map<int, int>> dest_large;
  dest_large = src_large;
  EXPECT_TRUE(SmallMapEqual(dest_large, src_large));
  EXPECT_EQ(dest_large.using_full_map(), src_large.using_full_map());

  // Assignments which assign from full to small, and vice versa.
  dest_small = src_large;
  EXPECT_TRUE(SmallMapEqual(dest_small, src_large));
  EXPECT_EQ(dest_small.using_full_map(), src_large.using_full_map());

  dest_large = src_small;
  EXPECT_TRUE(SmallMapEqual(dest_large, src_small));
  EXPECT_EQ(dest_large.using_full_map(), src_small.using_full_map());

  // Double check that SmallMapEqual works:
  dest_large[42] = 666;
  EXPECT_FALSE(SmallMapEqual(dest_large, src_small));
}

TEST(SmallMap, Insert) {
  small_map<std::unordered_map<int, int>> sm;

  // loop through the transition from small map to map.
  for (int i = 1; i <= 10; ++i) {
    VLOG(1) << "Iteration " << i;
    // insert an element
    std::pair<small_map<std::unordered_map<int, int>>::iterator, bool> ret;
    ret = sm.insert(std::make_pair(i, 100 * i));
    EXPECT_TRUE(ret.second);
    EXPECT_TRUE(ret.first == sm.find(i));
    EXPECT_EQ(ret.first->first, i);
    EXPECT_EQ(ret.first->second, 100 * i);

    // try to insert it again with different value, fails, but we still get an
    // iterator back with the original value.
    ret = sm.insert(std::make_pair(i, -i));
    EXPECT_FALSE(ret.second);
    EXPECT_TRUE(ret.first == sm.find(i));
    EXPECT_EQ(ret.first->first, i);
    EXPECT_EQ(ret.first->second, 100 * i);

    // check the state of the map.
    for (int j = 1; j <= i; ++j) {
      small_map<std::unordered_map<int, int>>::iterator it = sm.find(j);
      EXPECT_TRUE(it != sm.end());
      EXPECT_THAT(*it, Pair(j, j * 100));
    }
    EXPECT_EQ(sm.size(), i);
    EXPECT_FALSE(sm.empty());
  }
}

TEST(SmallMap, Emplace) {
  small_map<std::unordered_map<int, int>> sm;

  // loop through the transition from small map to map.
  for (int i = 1; i <= 10; ++i) {
    VLOG(1) << "Iteration " << i;
    // insert an element
    auto ret = sm.emplace(i, 100 * i);
    EXPECT_TRUE(ret.second);
    EXPECT_TRUE(ret.first == sm.find(i));
    EXPECT_EQ(ret.first->first, i);
    EXPECT_EQ(ret.first->second, 100 * i);

    // try to insert it again with different value, fails, but we still get an
    // iterator back with the original value.
    ret = sm.emplace(i, -i);
    EXPECT_FALSE(ret.second);
    EXPECT_TRUE(ret.first == sm.find(i));
    EXPECT_EQ(ret.first->first, i);
    EXPECT_EQ(ret.first->second, 100 * i);

    // check the state of the map.
    for (int j = 1; j <= i; ++j) {
      auto it = sm.find(j);
      EXPECT_TRUE(it != sm.end());
      EXPECT_THAT(*it, Pair(j, j * 100));
    }
    EXPECT_EQ(sm.size(), i);
    EXPECT_FALSE(sm.empty());
  }
}

TEST(SmallMap, EmplacePiecewise) {
  struct Foo {
    enum IsTuple { kUninitialized, kYes, kNo };

    const IsTuple is_tuple;
    explicit Foo(std::tuple<int, float>) : is_tuple(kYes) {}
    explicit Foo(int, float) : is_tuple(kNo) {}
    Foo() : is_tuple(kUninitialized) {}
  };

  small_map<std::unordered_map<std::string, Foo>> m;

  std::tuple<int, float> t(1, 3.14);
  m.emplace("xxx", Foo(t));
  m.emplace(std::piecewise_construct, std::forward_as_tuple("yyy"), t);

  EXPECT_EQ(Foo::kYes, m["xxx"].is_tuple);
  EXPECT_EQ(Foo::kNo, m["yyy"].is_tuple);
}

TEST(SmallMap, Reserve) {
  small_map<std::unordered_map<int, int>, 4> sm;
  EXPECT_FALSE(sm.using_full_map());
  // Reserve under the limit of 4 items, so it doesn't turn into a full map.
  sm.reserve(3);
  EXPECT_FALSE(sm.using_full_map());
  // Assign one of the items within the small map.
  sm[1] = 2;
  // It's still a small map.
  EXPECT_FALSE(sm.using_full_map());
  // Reserve over the limit of a small map.  It becomes a large map and the item
  // is moved into the large map.
  sm.reserve(5);
  EXPECT_TRUE(sm.using_full_map());
  EXPECT_EQ(1, sm.count(1));
  EXPECT_TRUE(sm.contains(1));
  EXPECT_EQ(2, sm[1]);
}

TEST(SmallMap, InsertRange) {
  // loop through the transition from small map to map.
  for (int elements = 0; elements <= 10; ++elements) {
    VLOG(1) << "Elements " << elements;
    std::unordered_map<int, int> normal_map;
    for (int i = 1; i <= elements; ++i) {
      normal_map.insert(std::make_pair(i, 100 * i));
    }

    small_map<std::unordered_map<int, int>> sm;
    sm.insert(normal_map.begin(), normal_map.end());
    EXPECT_EQ(normal_map.size(), sm.size());
    for (int i = 1; i <= elements; ++i) {
      VLOG(1) << "Iteration " << i;
      EXPECT_TRUE(sm.find(i) != sm.end());
      EXPECT_EQ(sm.find(i)->first, i);
      EXPECT_EQ(sm.find(i)->second, 100 * i);
    }
  }
}

TEST(SmallMap, Extract) {
  small_map<std::unordered_map<std::string, int>> m;

  m["monday"] = 1;
  m["tuesday"] = 2;
  m["wednesday"] = 3;
  EXPECT_FALSE(m.using_full_map());
  {
    auto node = m.extract("tuesday");
    EXPECT_FALSE(node.empty());
    EXPECT_TRUE(static_cast<bool>(node));
    EXPECT_EQ("tuesday", node.key());
    EXPECT_EQ(2, node.mapped());
    EXPECT_EQ(m.size(), 2);
  }
  {
    auto node = m.extract("tuesday");
    EXPECT_TRUE(node.empty());
    EXPECT_FALSE(static_cast<bool>(node));
    EXPECT_EQ(m.size(), 2);
  }
  {
    auto node = m.extract(m.find("monday"));
    EXPECT_FALSE(node.empty());
    EXPECT_TRUE(static_cast<bool>(node));
    EXPECT_EQ("monday", node.key());
    EXPECT_EQ(1, node.mapped());
    EXPECT_EQ(m.size(), 1);
  }

  m["monday"] = 1;
  m["tuesday"] = 2;
  m["wednesday"] = 3;
  m["thursday"] = 4;
  m["friday"] = 5;
  m["saturday"] = 6;
  m["sunday"] = 7;
  EXPECT_TRUE(m.using_full_map());
  {
    auto node = m.extract("friday");
    EXPECT_FALSE(node.empty());
    EXPECT_TRUE(static_cast<bool>(node));
    EXPECT_EQ("friday", node.key());
    EXPECT_EQ(5, node.mapped());
    EXPECT_EQ(m.size(), 6);
  }
  {
    auto node = m.extract("friday");
    EXPECT_TRUE(node.empty());
    EXPECT_FALSE(static_cast<bool>(node));
    EXPECT_EQ(m.size(), 6);
  }
  {
    auto node = m.extract(m.find("saturday"));
    EXPECT_FALSE(node.empty());
    EXPECT_TRUE(static_cast<bool>(node));
    EXPECT_EQ("saturday", node.key());
    EXPECT_EQ(6, node.mapped());
    EXPECT_EQ(m.size(), 5);
  }
}

TEST(SmallMap, MoveOnlyExtractCompiles) {
  small_map<absl::flat_hash_map<std::string, std::unique_ptr<int>>> m;

  m["monday"] = std::make_unique<int>(1);
  m["tuesday"] = std::make_unique<int>(2);
  m["wednesday"] = std::make_unique<int>(3);

  EXPECT_FALSE(m.extract("tuesday").empty());
  EXPECT_TRUE(m.extract("it is wednesday").empty());
}

TEST(SmallMap, Erase) {
  small_map<std::unordered_map<std::string, int>> m;
  small_map<std::unordered_map<std::string, int>>::iterator iter;

  m["monday"] = 1;
  m["tuesday"] = 2;
  m["wednesday"] = 3;

  EXPECT_EQ(m["monday"], 1);
  EXPECT_EQ(m["tuesday"], 2);
  EXPECT_EQ(m["wednesday"], 3);
  EXPECT_EQ(m.count("tuesday"), 1);
  EXPECT_TRUE(m.contains("tuesday"));
  EXPECT_FALSE(m.using_full_map());

  iter = m.begin();
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair("monday", 1));
  ++iter;
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair("tuesday", 2));
  ++iter;
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair("wednesday", 3));
  ++iter;
  EXPECT_TRUE(iter == m.end());

  EXPECT_EQ(m.erase("tuesday"), 1);

  EXPECT_EQ(m["monday"], 1);
  EXPECT_EQ(m["wednesday"], 3);
  EXPECT_EQ(m.count("tuesday"), 0);
  EXPECT_FALSE(m.contains("tuesday"));
  EXPECT_EQ(m.erase("tuesday"), 0);

  iter = m.begin();
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair("monday", 1));
  ++iter;
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair("wednesday", 3));
  ++iter;
  EXPECT_TRUE(iter == m.end());

  m["thursday"] = 4;
  m["friday"] = 5;
  EXPECT_EQ(m.size(), 4);
  EXPECT_FALSE(m.empty());
  EXPECT_FALSE(m.using_full_map());

  m["saturday"] = 6;
  EXPECT_TRUE(m.using_full_map());

  EXPECT_EQ(m.count("friday"), 1);
  EXPECT_TRUE(m.contains("friday"));
  EXPECT_EQ(m.erase("friday"), 1);
  EXPECT_TRUE(m.using_full_map());
  EXPECT_EQ(m.count("friday"), 0);
  EXPECT_FALSE(m.contains("friday"));
  EXPECT_EQ(m.erase("friday"), 0);

  EXPECT_EQ(m.size(), 4);
  EXPECT_FALSE(m.empty());
  EXPECT_EQ(m.erase("monday"), 1);
  EXPECT_EQ(m.size(), 3);
  EXPECT_FALSE(m.empty());

  m.clear();
  EXPECT_FALSE(m.using_full_map());
  EXPECT_EQ(m.size(), 0);
  EXPECT_TRUE(m.empty());
}

TEST(SmallMap, EraseMoveOnly) {
  small_map<std::unordered_map<std::string, MovableOnlyInstance>> m;
  small_map<std::unordered_map<std::string, MovableOnlyInstance>>::iterator
      iter;

  InstanceTracker tracker;

  m.insert({"monday", MovableOnlyInstance(1)});
  m.insert({"tuesday", MovableOnlyInstance(2)});
  m.insert({"wednesday", MovableOnlyInstance(3)});
  EXPECT_EQ(3, tracker.instances());

  EXPECT_EQ(m.erase("tuesday"), 1);
  EXPECT_EQ(2, tracker.instances());

  iter = m.find("monday");
  ASSERT_TRUE(iter != m.end());
  m.erase(iter);
  EXPECT_EQ(m.count("monday"), 0);
  EXPECT_FALSE(m.contains("monday"));

  EXPECT_EQ(1, tracker.instances());
  m.erase(m.begin());
}

TEST(SmallMap, EraseLeavesNextIteratorValid) {
  small_map<std::unordered_map<std::string, int>> m;
  small_map<std::unordered_map<std::string, int>>::iterator iter;

  m["monday"] = 1;
  m["tuesday"] = 2;
  m["wednesday"] = 3;
  m["thursday"] = 4;

  EXPECT_FALSE(m.using_full_map());

  iter = m.begin();
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair("monday", 1));
  ++iter;
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair("tuesday", 2));
  iter = m.erase(iter);
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair("thursday", 4));
  ++iter;
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair("wednesday", 3));
  iter = m.erase(iter);
  EXPECT_TRUE(iter == m.end());

  EXPECT_EQ(m.count("monday"), 1);
  EXPECT_EQ(m.count("tuesday"), 0);
  EXPECT_EQ(m.count("wednesday"), 0);
  EXPECT_EQ(m.count("thursday"), 1);

  iter = m.begin();
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair("monday", 1));
  iter = m.erase(iter);
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair("thursday", 4));
  iter = m.erase(iter);
  EXPECT_TRUE(iter == m.end());

  EXPECT_TRUE(m.empty());

  m["monday"] = 1;
  m["tuesday"] = 2;
  m["wednesday"] = 3;
  m["thursday"] = 4;
  m["friday"] = 5;
  EXPECT_TRUE(m.using_full_map());

  small_map<std::unordered_map<std::string, int>> observed;
  iter = m.begin();
  EXPECT_TRUE(iter != m.end());
  observed.insert(*(iter++));
  EXPECT_TRUE(iter != m.end());
  observed.insert(*(iter++));
  EXPECT_TRUE(iter != m.end());
  iter = m.erase(iter);
  EXPECT_TRUE(iter != m.end());
  observed.insert(*(iter++));
  EXPECT_TRUE(iter != m.end());
  iter = m.erase(iter);
  EXPECT_TRUE(iter == m.end());

  EXPECT_EQ(3, m.size());
  EXPECT_TRUE(observed == m);
}

TEST(SmallMap, EraseLeavesNextIteratorValidEraseReturnsVoid) {
  small_map<absl::flat_hash_map<std::string, int>> m;
  small_map<absl::flat_hash_map<std::string, int>>::iterator iter;

  m["monday"] = 1;
  m["tuesday"] = 2;
  m["wednesday"] = 3;
  m["thursday"] = 4;

  EXPECT_FALSE(m.using_full_map());

  iter = m.begin();
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair("monday", 1));
  ++iter;
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair("tuesday", 2));
  iter = m.erase(iter);
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair("thursday", 4));
  ++iter;
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair("wednesday", 3));
  iter = m.erase(iter);
  EXPECT_TRUE(iter == m.end());

  EXPECT_EQ(m.count("monday"), 1);
  EXPECT_EQ(m.count("tuesday"), 0);
  EXPECT_EQ(m.count("wednesday"), 0);
  EXPECT_EQ(m.count("thursday"), 1);

  iter = m.begin();
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair("monday", 1));
  iter = m.erase(iter);
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair("thursday", 4));
  iter = m.erase(iter);
  EXPECT_TRUE(iter == m.end());

  EXPECT_TRUE(m.empty());

  m["monday"] = 1;
  m["tuesday"] = 2;
  m["wednesday"] = 3;
  m["thursday"] = 4;
  m["friday"] = 5;
  EXPECT_TRUE(m.using_full_map());

  small_map<absl::flat_hash_map<std::string, int>> observed;
  iter = m.begin();
  EXPECT_TRUE(iter != m.end());
  observed.insert(*(iter++));
  EXPECT_TRUE(iter != m.end());
  observed.insert(*(iter++));
  EXPECT_TRUE(iter != m.end());
  iter = m.erase(iter);
  EXPECT_TRUE(iter != m.end());
  observed.insert(*(iter++));
  EXPECT_TRUE(iter != m.end());
  iter = m.erase(iter);
  EXPECT_TRUE(iter == m.end());

  EXPECT_EQ(3, m.size());
  EXPECT_TRUE(observed == m);
}

TEST(SmallMap, NonHashMap) {
  small_map<std::map<int, int>, 4, std::equal_to<int>> m;
  EXPECT_TRUE(m.empty());

  m[9] = 2;
  m[0] = 5;

  EXPECT_EQ(m[9], 2);
  EXPECT_EQ(m[0], 5);
  EXPECT_EQ(m.size(), 2);
  EXPECT_FALSE(m.empty());
  EXPECT_FALSE(m.using_full_map());

  small_map<std::map<int, int>, 4, std::equal_to<int>>::iterator iter(
      m.begin());
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair(9, 2));
  ++iter;
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair(0, 5));
  ++iter;
  EXPECT_TRUE(iter == m.end());
  --iter;
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair(0, 5));

  m[8] = 23;
  m[1234] = 90;
  m[-5] = 6;

  EXPECT_EQ(m[9], 2);
  EXPECT_EQ(m[0], 5);
  EXPECT_EQ(m[1234], 90);
  EXPECT_EQ(m[8], 23);
  EXPECT_EQ(m[-5], 6);
  EXPECT_EQ(m.size(), 5);
  EXPECT_FALSE(m.empty());
  EXPECT_TRUE(m.using_full_map());

  iter = m.begin();
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair(-5, 6));
  ++iter;
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair(0, 5));
  ++iter;
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair(8, 23));
  ++iter;
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair(9, 2));
  ++iter;
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair(1234, 90));
  ++iter;
  EXPECT_TRUE(iter == m.end());
  --iter;
  ASSERT_TRUE(iter != m.end());
  EXPECT_THAT(*iter, Pair(1234, 90));
}

TEST(SmallMap, MapEquality) {
  typedef small_map<std::map<int, int>, 4, std::equal_to<int>> Map;
  Map m;
  Map n;
  // The maps now have the same representation (arrays).
  EXPECT_EQ(m, n);
  m[8] = n[8] = 7;
  EXPECT_EQ(m, n);
  m[9] = 10;
  EXPECT_NE(m, n);
  n[9] = 10;
  EXPECT_EQ(m, n);
  // Vary the insertion order.
  m[1] = 2;
  EXPECT_NE(m, n);
  m[2] = 3;
  EXPECT_NE(m, n);
  n[2] = 3;
  EXPECT_NE(m, n);
  n[1] = 2;
  EXPECT_EQ(m, n);
  n[0] = 0;
  // The maps now have different representations.
  EXPECT_NE(m, n);
  n.erase(0);
  EXPECT_EQ(m, n);
  m[0] = 0;
  // The maps now have the same representation (maps).
  EXPECT_NE(m, n);
  n[0] = 0;
  EXPECT_EQ(m, n);
  m.clear();
  EXPECT_NE(m, n);
  n.clear();
  EXPECT_EQ(m, n);
}

TEST(SmallMap, HashMapEquality) {
#ifdef __GLIBCXX__
  using HashMap = __gnu_cxx::hash_map<int, int>;
#else
  using HashMap = std::unordered_map<int, int>;
#endif
  typedef small_map<HashMap, 4, std::equal_to<int>> Map;
  Map m;
  Map n;
  // The maps now have the same representation (arrays).
  EXPECT_EQ(m, n);
  m[8] = n[8] = 7;
  EXPECT_EQ(m, n);
  m[9] = 10;
  EXPECT_NE(m, n);
  n[9] = 10;
  EXPECT_EQ(m, n);
  // Vary the insertion order.
  m[1] = 2;
  EXPECT_NE(m, n);
  m[2] = 3;
  EXPECT_NE(m, n);
  n[2] = 3;
  EXPECT_NE(m, n);
  n[1] = 2;
  EXPECT_EQ(m, n);
  n[0] = 0;
  // The maps now have different representations.
  EXPECT_NE(m, n);
  n.erase(0);
  EXPECT_EQ(m, n);
  m[0] = 0;
  // The maps now have the same representation (maps).
  EXPECT_NE(m, n);
  n[0] = 0;
  EXPECT_EQ(m, n);
  m.clear();
  EXPECT_NE(m, n);
  n.clear();
  EXPECT_EQ(m, n);
  // Check that HashMap's broken operator== is dealt with correctly.
  HashMap raw_m;
  HashMap raw_n;
  for (int i = 0; i < 1000; ++i) {
    m[i] = i;
    raw_m[i] = i;
  }
  for (int i = 1; i < 1000; ++i) {
    m.erase(i);
    raw_m.erase(i);
  }
  n[0] = 0;
  raw_n[0] = 0;
  EXPECT_EQ(m, n);
#ifdef __GLIBCXX__
  EXPECT_NE(raw_m, raw_n);  // http://gcc.gnu.org/bugzilla/show_bug.cgi?id=32910
#endif
}

TEST(SmallMap, DefaultEqualKeyWorks) {
  // If these tests compile, they pass. The EXPECT calls are only there to avoid
  // unused variable warnings.
  small_map<std::unordered_map<int, int>> hm;
  EXPECT_EQ(0, hm.size());
  small_map<std::map<int, int>> m;
  EXPECT_EQ(0, m.size());
}

namespace {
class hash_map_add_item : public std::unordered_map<int, int> {
 public:
  hash_map_add_item() : std::unordered_map<int, int>() {}
  explicit hash_map_add_item(const std::pair<int, int>& item)
      : std::unordered_map<int, int>() {
    insert(item);
  }
};

void InitMap(ManualConstructor<hash_map_add_item>* map_ctor) {
  map_ctor->Init(std::make_pair(0, 0));
}

class hash_map_add_item_initializer {
 public:
  explicit hash_map_add_item_initializer(int item_to_add)
      : item_(item_to_add) {}
  hash_map_add_item_initializer() : item_(0) {}
  void operator()(ManualConstructor<hash_map_add_item>* map_ctor) const {
    map_ctor->Init(std::make_pair(item_, item_));
  }

  int item_;
};
}  // anonymous namespace

TEST(SmallMap, SubclassInitializationWithFunctionPointer) {
  small_map<hash_map_add_item, 4, hash_map_add_item::key_equal,
            void (&)(ManualConstructor<hash_map_add_item>*)>
      m(InitMap);

  EXPECT_TRUE(m.empty());

  m[1] = 1;
  m[2] = 2;
  m[3] = 3;
  m[4] = 4;

  EXPECT_EQ(4, m.size());
  EXPECT_EQ(0, m.count(0));

  m[5] = 5;
  EXPECT_EQ(6, m.size());
  // Our function adds an extra item when we convert to a map.
  EXPECT_EQ(1, m.count(0));
}

TEST(SmallMap, SubclassInitializationWithFunctionObject) {
  small_map<hash_map_add_item, 4, hash_map_add_item::key_equal,
            hash_map_add_item_initializer>
      m(hash_map_add_item_initializer(-1));

  EXPECT_TRUE(m.empty());

  m[1] = 1;
  m[2] = 2;
  m[3] = 3;
  m[4] = 4;

  EXPECT_EQ(4, m.size());
  EXPECT_EQ(0, m.count(-1));

  m[5] = 5;
  EXPECT_EQ(6, m.size());
  // Our functor adds an extra item when we convert to a map.
  EXPECT_EQ(1, m.count(-1));
}

template <typename NormalMap>
static size_t ExpectedSmallMapSize(int array_size) {
  // We're explicitly computing a sizeof for the SmallMap, since in practice
  // gcc has made some strange padding choices which required partial
  // rollbacks.
  int64_t size =
      (sizeof(int) +    // size_
       sizeof(void*) +  // functor_
       // the union:
       std::max(sizeof(NormalMap),
                sizeof(typename NormalMap::value_type) * array_size));
  util::math::round_mod_n(&size, 16);
  return size;
}

TEST(SmallMap, ByteFootprints) {
  typedef std::unordered_map<int, int> TypicalHashMap;

  EXPECT_GE(ExpectedSmallMapSize<TypicalHashMap>(4),
            sizeof(small_map<TypicalHashMap>));

  EXPECT_GE(ExpectedSmallMapSize<TypicalHashMap>(1024),
            sizeof(small_map<TypicalHashMap, 1024>));

  EXPECT_GE(ExpectedSmallMapSize<TypicalHashMap>(1),
            sizeof(small_map<TypicalHashMap, 1>));

  EXPECT_GE(ExpectedSmallMapSize<hash_map_add_item>(4),
            sizeof(small_map<hash_map_add_item, 4, hash_map_add_item::key_equal,
                             hash_map_add_item_initializer>));
}

template <typename M>
void CommonTransparencySupportTest(M* const map) {
  absl::string_view key = "key";

  EXPECT_TRUE(map->find(absl::string_view{}) == map->end());
  EXPECT_EQ(map->find(key)->second, 42);
  EXPECT_EQ(map->count(key), 1);
  EXPECT_TRUE(map->contains(key));

  const auto node = map->extract(key);
  EXPECT_FALSE(node.empty());
  EXPECT_EQ(map->count(key), 0);
  EXPECT_FALSE(map->contains(key));
  EXPECT_EQ(node.key(), key);
  EXPECT_EQ(node.mapped(), 42);

  (*map)[node.key()] = node.mapped();
  EXPECT_EQ(map->count(key), 1);
  EXPECT_TRUE(map->contains(key));
  EXPECT_EQ(map->erase(key), 1);
  EXPECT_EQ(map->erase(key), 0);
}

TEST(SmallMap, TransparentMethodsAreSupportedForHashMap) {
  // Test that string_view can be used to access elements of a string keyed map.
  gtl::small_map<absl::flat_hash_map<std::string, int>> map;
  absl::string_view key = "key";
  map["key"] = 42;

  EXPECT_EQ(map[key], 42);
  CommonTransparencySupportTest(&map);
  EXPECT_EQ(map[key], 0);
}

TEST(SmallMap, TransparentMethodsAreSupportedForStdMap) {
  // The std::less<> is a transparent comparison, which allows std::map to
  // compare std::string and absl::string_view.
  gtl::small_map<std::map<std::string, int, std::less<>>> map;
  map["key"] = 42;

  CommonTransparencySupportTest(&map);
}

struct NonTransparentLess {
  template <typename LK, typename RK>
  bool operator()(const LK& left, const RK& right) const {
    EXPECT_TRUE((std::is_same_v<LK, RK>));
    return left < right;
  }
};

struct NonTransparentEquals {
  template <typename LK, typename RK>
  bool operator()(const LK& left, const RK& right) const {
    EXPECT_TRUE((std::is_same_v<LK, RK>));
    return left == right;
  }
};

template <typename Map, int kMapSize = 10>
void DontUseTransparentMethodsImpl() {
  gtl::small_map<Map, kMapSize / 2> map;
  for (int i = 0; i < kMapSize; ++i) {
    const auto key = static_cast<size_t>(i);
    map[key] = i;
    EXPECT_EQ(map.find(key)->second, i);
    EXPECT_EQ(map.count(key), 1);
    EXPECT_EQ(map.count(key + 1), 0);
  }
  for (int i = 0; i < kMapSize; ++i) {
    map.erase(i);
  }
  for (int i = 0; i < kMapSize; ++i) {
    const auto key = static_cast<size_t>(i);
    EXPECT_TRUE(map.find(key) == map.end());
    EXPECT_EQ(map.count(key), 0);
  }
}

TEST(SmallMap, DontUseTransparentMethods) {
  DontUseTransparentMethodsImpl<std::map<int, int, NonTransparentLess>>();
  DontUseTransparentMethodsImpl<
      absl::flat_hash_map<int, int, absl::Hash<int>, NonTransparentEquals>>();
}

TEST(SmallMap, CompatibleWithMapView) {
  small_map<absl::flat_hash_map<int, int>> m;
  m[1] = 10;
  m[2] = 20;
  gtl::MapView<int, int> view = m;
  EXPECT_THAT(view, UnorderedElementsAre(Pair(1, 10), Pair(2, 20)));
}

}  // anonymous namespace
}  // namespace gtl
