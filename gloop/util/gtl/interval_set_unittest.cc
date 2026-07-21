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

#include "gloop/util/gtl/interval_set.h"

#include <stdarg.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <ostream>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/internal/container_memory.h"
#include "absl/hash/hash_testing.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "benchmark/benchmark.h"
#include "gloop/util/gtl/interval.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace gtl {
namespace {

using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::IsEmpty;

class IntervalSetTest : public testing::Test {
 protected:
  void SetUp() override {
    // Initialize two IntervalSets for union, intersection, and difference
    // tests
    is.Add(100, 200);
    is.Add(300, 400);
    is.Add(500, 600);
    is.Add(700, 800);
    is.Add(900, 1000);
    is.Add(1100, 1200);
    is.Add(1300, 1400);
    is.Add(1500, 1600);
    is.Add(1700, 1800);
    is.Add(1900, 2000);
    is.Add(2100, 2200);

    // Lots of different cases:
    other.Add(50, 70);      // disjoint, at the beginning
    other.Add(2250, 2270);  // disjoint, at the end
    other.Add(650, 670);    // disjoint, in the middle
    other.Add(350, 360);    // included
    other.Add(370, 380);    // also included (two at once)
    other.Add(470, 530);    // overlaps low end
    other.Add(770, 830);    // overlaps high end
    other.Add(870, 900);    // meets at low end
    other.Add(1200, 1230);  // meets at high end
    other.Add(1270, 1830);  // overlaps multiple ranges
  }

  void TearDown() override {
    is.Clear();
    EXPECT_TRUE(is.empty());
    other.Clear();
    EXPECT_TRUE(other.empty());
  }
  IntervalSet<int> is;
  IntervalSet<int> other;
};

TEST_F(IntervalSetTest, IsDisjoint) {
  EXPECT_TRUE(is.IsDisjoint(Interval<int>(0, 99)));
  EXPECT_TRUE(is.IsDisjoint(Interval<int>(0, 100)));
  EXPECT_TRUE(is.IsDisjoint(Interval<int>(200, 200)));
  EXPECT_TRUE(is.IsDisjoint(Interval<int>(200, 299)));
  EXPECT_TRUE(is.IsDisjoint(Interval<int>(400, 407)));
  EXPECT_TRUE(is.IsDisjoint(Interval<int>(405, 499)));
  EXPECT_TRUE(is.IsDisjoint(Interval<int>(2300, 2300)));
  EXPECT_TRUE(
      is.IsDisjoint(Interval<int>(2300, std::numeric_limits<int>::max())));
  EXPECT_FALSE(is.IsDisjoint(Interval<int>(100, 105)));
  EXPECT_FALSE(is.IsDisjoint(Interval<int>(199, 300)));
  EXPECT_FALSE(is.IsDisjoint(Interval<int>(250, 450)));
  EXPECT_FALSE(is.IsDisjoint(Interval<int>(299, 400)));
  EXPECT_FALSE(is.IsDisjoint(Interval<int>(250, 2000)));
  EXPECT_FALSE(
      is.IsDisjoint(Interval<int>(2199, std::numeric_limits<int>::max())));
  // Empty intervals.
  EXPECT_TRUE(is.IsDisjoint(Interval<int>(90, 90)));
  EXPECT_TRUE(is.IsDisjoint(Interval<int>(100, 100)));
  EXPECT_TRUE(is.IsDisjoint(Interval<int>(100, 90)));
  EXPECT_TRUE(is.IsDisjoint(Interval<int>(150, 150)));
  EXPECT_TRUE(is.IsDisjoint(Interval<int>(200, 200)));
  EXPECT_TRUE(is.IsDisjoint(Interval<int>(400, 300)));
}

// Base helper method for verifying the contents of an interval set.
// Returns true iff <is> contains <count> intervals whose successive
// endpoints match the sequence of args in <ap>:
static bool VA_Check(const IntervalSet<int>& is, size_t count, va_list ap) {
  std::vector<Interval<int>> intervals(is.begin(), is.end());
  if (count != intervals.size()) {
    LOG(ERROR) << "Expected " << count << " intervals, got " << intervals.size()
               << ": " << is;
    return false;
  }
  if (count != is.size()) {
    LOG(ERROR) << "Expected " << count << " intervals, got Size " << is.size()
               << ": " << is;
    return false;
  }
  bool result = true;
  for (size_t i = 0; i < count; i++) {
    int min = va_arg(ap, int);
    int max = va_arg(ap, int);
    if (min != intervals[i].start() || max != intervals[i].limit()) {
      LOG(ERROR) << "Expected: [" << min << ", " << max << ") got "
                 << intervals[i] << " in " << is;
      result = false;
    }
  }
  return result;
}

static bool Check(const IntervalSet<int>& is, int count, ...) {
  va_list ap;
  va_start(ap, count);
  const bool result = VA_Check(is, count, ap);
  va_end(ap);
  return result;
}

// Some helper functions for testing Contains and Find, which are logically the
// same.
static void TestContainsAndFind(const IntervalSet<int>& is, int value) {
  EXPECT_TRUE(is.Contains(value)) << "Set does not contain " << value;
  auto it = is.Find(value);
  EXPECT_NE(it, is.end()) << "No iterator to interval containing " << value;
  EXPECT_TRUE(it->contains(value)) << "Iterator does not contain " << value;
}

static void TestContainsAndFind(const IntervalSet<int>& is, int min, int max) {
  EXPECT_TRUE(is.Contains(min, max))
      << "Set does not contain interval with min " << min << "and max " << max;
  auto it = is.Find(min, max);
  EXPECT_NE(it, is.end()) << "No iterator to interval with min " << min
                          << "and max " << max;
  EXPECT_TRUE(it->contains(Interval<int>(min, max)))
      << "Iterator does not contain interval with min " << min << "and max "
      << max;
}

static void TestNotContainsAndFind(const IntervalSet<int>& is, int value) {
  EXPECT_FALSE(is.Contains(value)) << "Set contains " << value;
  auto it = is.Find(value);
  EXPECT_EQ(it, is.end()) << "There is iterator to interval containing "
                          << value;
}

static void TestNotContainsAndFind(const IntervalSet<int>& is, int min,
                                   int max) {
  EXPECT_FALSE(is.Contains(min, max))
      << "Set contains interval with min " << min << "and max " << max;
  auto it = is.Find(min, max);
  EXPECT_EQ(it, is.end()) << "There is iterator to interval with min " << min
                          << "and max " << max;
}

TEST_F(IntervalSetTest, IntervalSetBasic) {
  // Test Add, Get, Contains and Find
  IntervalSet<int> iset;
  EXPECT_TRUE(iset.empty());
  EXPECT_EQ(0, iset.size());
  iset.Add(100, 200);
  EXPECT_FALSE(iset.empty());
  EXPECT_EQ(1, iset.size());
  iset.Add(100, 150);
  iset.Add(150, 200);
  iset.Add(130, 170);
  iset.Add(90, 150);
  iset.Add(170, 220);
  iset.Add(300, 400);
  iset.Add(250, 450);
  EXPECT_FALSE(iset.empty());
  EXPECT_EQ(2, iset.size());
  EXPECT_TRUE(Check(iset, 2, 90, 220, 250, 450));

  // Test two intervals with a.max == b.min, that will just join up.
  iset.Clear();
  iset.Add(100, 200);
  iset.Add(200, 300);
  EXPECT_FALSE(iset.empty());
  EXPECT_EQ(1, iset.size());
  EXPECT_TRUE(Check(iset, 1, 100, 300));

  // Test adding two sets together.
  iset.Clear();
  IntervalSet<int> iset_add;
  iset.Add(100, 200);
  iset.Add(100, 150);
  iset.Add(150, 200);
  iset.Add(130, 170);
  iset_add.Add(90, 150);
  iset_add.Add(170, 220);
  iset_add.Add(300, 400);
  iset_add.Add(250, 450);

  iset.Add(iset_add);
  EXPECT_FALSE(iset.empty());
  EXPECT_EQ(2, iset.size());
  EXPECT_TRUE(Check(iset, 2, 90, 220, 250, 450));

  // Test begin()/end(), and rbegin()/rend()
  // to iterate over intervals.
  {
    std::vector<Interval<int>> expected(iset.begin(), iset.end());

    std::vector<Interval<int>> actual1;
    std::copy(iset.begin(), iset.end(), std::back_inserter(actual1));
    ASSERT_EQ(expected.size(), actual1.size());

    std::vector<Interval<int>> actual2;
    std::copy(iset.begin(), iset.end(), std::back_inserter(actual2));
    ASSERT_EQ(expected.size(), actual2.size());

    for (size_t i = 0; i < expected.size(); i++) {
      EXPECT_EQ(expected[i].start(), actual1[i].start());
      EXPECT_EQ(expected[i].limit(), actual1[i].limit());

      EXPECT_EQ(expected[i].start(), actual2[i].start());
      EXPECT_EQ(expected[i].limit(), actual2[i].limit());
    }

    // Ensure that the rbegin()/rend() iterators correctly yield the intervals
    // in reverse order.
    EXPECT_THAT(std::vector<Interval<int>>(iset.rbegin(), iset.rend()),
                ElementsAreArray(expected.rbegin(), expected.rend()));
  }

  TestNotContainsAndFind(iset, 89);
  TestContainsAndFind(iset, 90);
  TestContainsAndFind(iset, 120);
  TestContainsAndFind(iset, 219);
  TestNotContainsAndFind(iset, 220);
  TestNotContainsAndFind(iset, 235);
  TestNotContainsAndFind(iset, 249);
  TestContainsAndFind(iset, 250);
  TestContainsAndFind(iset, 300);
  TestContainsAndFind(iset, 449);
  TestNotContainsAndFind(iset, 450);
  TestNotContainsAndFind(iset, 451);

  TestNotContainsAndFind(iset, 50, 60);
  TestNotContainsAndFind(iset, 50, 90);
  TestNotContainsAndFind(iset, 50, 200);
  TestNotContainsAndFind(iset, 90, 90);
  TestContainsAndFind(iset, 90, 200);
  TestContainsAndFind(iset, 100, 200);
  TestContainsAndFind(iset, 100, 220);
  TestNotContainsAndFind(iset, 100, 221);
  TestNotContainsAndFind(iset, 220, 220);
  TestNotContainsAndFind(iset, 240, 300);
  TestContainsAndFind(iset, 250, 300);
  TestContainsAndFind(iset, 260, 300);
  TestContainsAndFind(iset, 300, 450);
  TestNotContainsAndFind(iset, 300, 451);

  IntervalSet<int> iset_contains;
  iset_contains.Add(50, 90);
  EXPECT_FALSE(iset.Contains(iset_contains));
  iset_contains.Clear();

  iset_contains.Add(90, 200);
  EXPECT_TRUE(iset.Contains(iset_contains));
  iset_contains.Add(100, 200);
  EXPECT_TRUE(iset.Contains(iset_contains));
  iset_contains.Add(100, 220);
  EXPECT_TRUE(iset.Contains(iset_contains));
  iset_contains.Add(250, 300);
  EXPECT_TRUE(iset.Contains(iset_contains));
  iset_contains.Add(300, 450);
  EXPECT_TRUE(iset.Contains(iset_contains));
  iset_contains.Add(300, 451);
  EXPECT_FALSE(iset.Contains(iset_contains));
  EXPECT_FALSE(iset.Contains(Interval<int>()));
  EXPECT_FALSE(iset.Contains(IntervalSet<int>()));
}

TEST_F(IntervalSetTest, IntervalSetCompactionTwoEqualIntervals) {
  IntervalSet<int> interval_set({{1, 3}, {1, 3}});
  EXPECT_EQ(interval_set.size(), 1);
  IntervalSet<int> existing(1, 3);
  EXPECT_TRUE(interval_set.Contains(existing));

  IntervalSet<int> added(8, 10);
  interval_set.Add(added);
  EXPECT_TRUE(interval_set.Contains(existing));
  EXPECT_TRUE(interval_set.Contains(added));
  EXPECT_EQ(interval_set.size(), 2);
}

TEST_F(IntervalSetTest,
       IntervalSetCompactionNextIntervalNotASubsetOfCurrentInterval) {
  IntervalSet<int> interval_set({{1, 3}, {1, 4}});
  EXPECT_EQ(interval_set.size(), 1);
  IntervalSet<int> existing(1, 4);
  EXPECT_TRUE(interval_set.Contains(existing));

  IntervalSet<int> added(4, 5);
  interval_set.Add(added);
  EXPECT_TRUE(interval_set.Contains(existing));
  EXPECT_TRUE(interval_set.Contains(added));
  EXPECT_EQ(interval_set.size(), 1);
}

TEST_F(IntervalSetTest,
       IntervalSetCompactionNextIntervalASubsetOfCurrentInterval) {
  IntervalSet<int> interval_set({{1, 3}, {1, 2}});
  EXPECT_EQ(interval_set.size(), 1);
  IntervalSet<int> existing(1, 3);
  EXPECT_TRUE(interval_set.Contains(existing));

  IntervalSet<int> added(5, 10);
  interval_set.Add(added);
  EXPECT_TRUE(interval_set.Contains(existing));
  EXPECT_TRUE(interval_set.Contains(added));
  EXPECT_EQ(interval_set.size(), 2);
}

TEST_F(IntervalSetTest, IntervalSetContainsEmpty) {
  const IntervalSet<int> empty;
  const IntervalSet<int> other_empty;
  const IntervalSet<int> non_empty({{10, 20}, {40, 50}});
  EXPECT_FALSE(empty.Contains(empty));
  EXPECT_FALSE(empty.Contains(other_empty));
  EXPECT_FALSE(empty.Contains(non_empty));
  EXPECT_FALSE(non_empty.Contains(empty));
}

TEST_F(IntervalSetTest, Equality) {
  IntervalSet<int> is_copy = is;
  IntervalSet<int> empty;
  EXPECT_EQ(is, is);
  EXPECT_EQ(is, is_copy);
  EXPECT_NE(is, other);
  EXPECT_NE(is, empty);
  EXPECT_EQ(empty, empty);

  EXPECT_TRUE(
      absl::VerifyTypeImplementsAbslHashCorrectly({empty, is, is_copy, other}));
}

TEST_F(IntervalSetTest, LowerAndUpperBound) {
  IntervalSet<int> intervals;
  intervals.Add(10, 20);
  intervals.Add(30, 40);

  //   [10, 20)  [30, 40)  end
  //   ^                        LowerBound(5)
  //   ^                        LowerBound(10)
  //   ^                        LowerBound(15)
  //             ^              LowerBound(20)
  //             ^              LowerBound(25)
  //             ^              LowerBound(30)
  //             ^              LowerBound(35)
  //                       ^    LowerBound(40)
  //                       ^    LowerBound(50)
  EXPECT_EQ(intervals.LowerBound(5)->start(), 10);
  EXPECT_EQ(intervals.LowerBound(10)->start(), 10);
  EXPECT_EQ(intervals.LowerBound(15)->start(), 10);
  EXPECT_EQ(intervals.LowerBound(20)->start(), 30);
  EXPECT_EQ(intervals.LowerBound(25)->start(), 30);
  EXPECT_EQ(intervals.LowerBound(30)->start(), 30);
  EXPECT_EQ(intervals.LowerBound(35)->start(), 30);
  EXPECT_EQ(intervals.LowerBound(40), intervals.end());
  EXPECT_EQ(intervals.LowerBound(50), intervals.end());

  //   [10, 20)  [30, 40)  end
  //   ^                        UpperBound(5)
  //             ^              UpperBound(10)
  //             ^              UpperBound(15)
  //             ^              UpperBound(20)
  //             ^              UpperBound(25)
  //                       ^    UpperBound(30)
  //                       ^    UpperBound(35)
  //                       ^    UpperBound(40)
  //                       ^    UpperBound(50)
  EXPECT_EQ(intervals.UpperBound(5)->start(), 10);
  EXPECT_EQ(intervals.UpperBound(10)->start(), 30);
  EXPECT_EQ(intervals.UpperBound(15)->start(), 30);
  EXPECT_EQ(intervals.UpperBound(20)->start(), 30);
  EXPECT_EQ(intervals.UpperBound(25)->start(), 30);
  EXPECT_EQ(intervals.UpperBound(30), intervals.end());
  EXPECT_EQ(intervals.UpperBound(35), intervals.end());
  EXPECT_EQ(intervals.UpperBound(40), intervals.end());
  EXPECT_EQ(intervals.UpperBound(50), intervals.end());
}

TEST_F(IntervalSetTest, SpanningInterval) {
  // Spanning interval of an empty set is empty:
  {
    IntervalSet<int> iset;
    const Interval<int>& ival = iset.SpanningInterval();
    EXPECT_TRUE(ival.empty());
  }

  // Spanning interval of a set with one interval is that interval:
  {
    IntervalSet<int> iset;
    iset.Add(100, 200);
    const Interval<int>& ival = iset.SpanningInterval();
    EXPECT_EQ(100, ival.start());
    EXPECT_EQ(200, ival.limit());
  }

  // Spanning interval of a set with multiple elements is determined
  // by the endpoints of the first and last element:
  {
    const Interval<int>& ival = is.SpanningInterval();
    EXPECT_EQ(100, ival.start());
    EXPECT_EQ(2200, ival.limit());
  }
  {
    const Interval<int>& ival = other.SpanningInterval();
    EXPECT_EQ(50, ival.start());
    EXPECT_EQ(2270, ival.limit());
  }
}

TEST_F(IntervalSetTest, IntervalSetUnion) {
  is.Union(other);
  EXPECT_TRUE(Check(is, 12, 50, 70, 100, 200, 300, 400, 470, 600, 650, 670, 700,
                    830, 870, 1000, 1100, 1230, 1270, 1830, 1900, 2000, 2100,
                    2200, 2250, 2270));
}

TEST_F(IntervalSetTest, FreeIntervalSetUnion) {
  using gtl::IntervalSetUnion;

  // Empty set
  {
    IntervalSet<int> empty;
    auto result = IntervalSetUnion({&empty});
    EXPECT_TRUE(result.empty());
  }

  // Two sets.
  {
    auto result = IntervalSetUnion({&is, &other});
    EXPECT_TRUE(Check(result, 12, 50, 70, 100, 200, 300, 400, 470, 600, 650,
                      670, 700, 830, 870, 1000, 1100, 1230, 1270, 1830, 1900,
                      2000, 2100, 2200, 2250, 2270));
  }

  // Multiples of the same set.
  {
    auto result = IntervalSetUnion({&is, &other, &is, &is});
    EXPECT_TRUE(Check(result, 12, 50, 70, 100, 200, 300, 400, 470, 600, 650,
                      670, 700, 830, 870, 1000, 1100, 1230, 1270, 1830, 1900,
                      2000, 2100, 2200, 2250, 2270));
  }

  // Adjacent intervals.
  {
    IntervalSet<int> a;
    a.Add(100, 101);
    a.Add(200, 201);

    IntervalSet<int> b;
    b.Add(101, 102);
    b.Add(199, 200);

    auto result = IntervalSetUnion({&a, &b});
    EXPECT_TRUE(Check(result, 2, 100, 102, 199, 201));
  }

  // Multiple sets.
  {
    IntervalSet<int> a;
    a.Add(40, 49);
    a.Add(100, 1000);
    a.Add(2000, 3000);

    IntervalSet<int> b;
    for (int i = 100; i < 900; i += 3) {
      b.Add(i, i + 1);
    }

    auto result = IntervalSetUnion({&a, &b, &is, &other});
    EXPECT_TRUE(Check(result, 6, 40, 49, 50, 70, 100, 1000, 1100, 1230, 1270,
                      1830, 1900, 3000));
  }
}

TEST_F(IntervalSetTest, IntervalSetIntersection) {
  EXPECT_TRUE(is.Intersects(other));
  EXPECT_TRUE(other.Intersects(is));
  is.Intersection(other);
  EXPECT_TRUE(Check(is, 7, 350, 360, 370, 380, 500, 530, 770, 800, 1300, 1400,
                    1500, 1600, 1700, 1800));
  EXPECT_TRUE(is.Intersects(other));
  EXPECT_TRUE(other.Intersects(is));
}

TEST_F(IntervalSetTest, IntervalSetIntersectionBothEmpty) {
  IntervalSet<std::string> mine, theirs;
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
  mine.Intersection(theirs);
  EXPECT_TRUE(mine.empty());
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
}

TEST_F(IntervalSetTest, IntervalSetIntersectionEmptyMine) {
  IntervalSet<std::string> mine;
  IntervalSet<std::string> theirs("a", "b");
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(mine.IntersectsInterval({"a", "b"}));
  EXPECT_FALSE(theirs.Intersects(mine));
  mine.Intersection(theirs);
  EXPECT_TRUE(mine.empty());
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
}

TEST_F(IntervalSetTest, IntervalSetIntersectionEmptyTheirs) {
  IntervalSet<std::string> mine("a", "b");
  IntervalSet<std::string> theirs;
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
  mine.Intersection(theirs);
  EXPECT_TRUE(mine.empty());
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
}

TEST_F(IntervalSetTest, IntervalSetIntersectionTheirsBeforeMine) {
  IntervalSet<std::string> mine("y", "z");
  IntervalSet<std::string> theirs;
  theirs.Add("a", "b");
  theirs.Add("c", "d");
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
  mine.Intersection(theirs);
  EXPECT_TRUE(mine.empty());
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
}

TEST_F(IntervalSetTest, IntervalSetIntersectionMineBeforeTheirs) {
  IntervalSet<std::string> mine;
  mine.Add("a", "b");
  mine.Add("c", "d");
  IntervalSet<std::string> theirs("y", "z");
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
  mine.Intersection(theirs);
  EXPECT_TRUE(mine.empty());
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
}

TEST_F(IntervalSetTest,
       IntervalSetIntersectionTheirsBeforeMineInt64Singletons) {
  IntervalSet<int64_t> mine({{10, 15}});
  IntervalSet<int64_t> theirs({{-20, -5}});
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
  mine.Intersection(theirs);
  EXPECT_TRUE(mine.empty());
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
}

TEST_F(IntervalSetTest, IntervalSetIntersectionMineBeforeTheirsIntSingletons) {
  IntervalSet<int> mine({{10, 15}});
  IntervalSet<int> theirs({{90, 95}});
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
  mine.Intersection(theirs);
  EXPECT_TRUE(mine.empty());
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
}

TEST_F(IntervalSetTest, IntervalSetIntersectionTheirsBetweenMine) {
  IntervalSet<int64_t> mine({{0, 5}, {40, 50}});
  IntervalSet<int64_t> theirs({{10, 15}});
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
  mine.Intersection(theirs);
  EXPECT_TRUE(mine.empty());
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
}

TEST_F(IntervalSetTest, IntervalSetIntersectionMineBetweenTheirs) {
  IntervalSet<int> mine({{20, 25}});
  IntervalSet<int> theirs({{10, 15}, {30, 32}});
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
  mine.Intersection(theirs);
  EXPECT_TRUE(mine.empty());
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
}

TEST_F(IntervalSetTest, IntervalSetIntersectionAlternatingIntervals) {
  IntervalSet<int> mine, theirs;
  mine.Add(10, 20);
  mine.Add(40, 50);
  mine.Add(60, 70);
  theirs.Add(25, 39);
  theirs.Add(55, 59);
  theirs.Add(75, 79);
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
  mine.Intersection(theirs);
  EXPECT_TRUE(mine.empty());
  EXPECT_FALSE(mine.Intersects(theirs));
  EXPECT_FALSE(theirs.Intersects(mine));
}

TEST_F(IntervalSetTest,
       IntervalSetIntersectionAdjacentAlternatingNonIntersectingIntervals) {
  // Make sure that intersection with adjacent interval set is empty.
  const IntervalSet<int> x1({{0, 10}});
  const IntervalSet<int> y1({{-50, 0}, {10, 95}});

  IntervalSet<int> result1 = x1;
  result1.Intersection(y1);
  EXPECT_TRUE(result1.empty()) << result1;

  const IntervalSet<int16_t> x2({{0, 10}, {20, 30}, {40, 90}});
  const IntervalSet<int16_t> y2(
      {{-50, -40}, {-2, 0}, {10, 20}, {32, 40}, {90, 95}});

  IntervalSet<int16_t> result2 = x2;
  result2.Intersection(y2);
  EXPECT_TRUE(result2.empty()) << result2;

  const IntervalSet<int64_t> x3({{-1, 5}, {5, 10}});
  const IntervalSet<int64_t> y3({{-10, -1}, {10, 95}});

  IntervalSet<int64_t> result3 = x3;
  result3.Intersection(y3);
  EXPECT_TRUE(result3.empty()) << result3;
}

TEST_F(IntervalSetTest,
       IntervalSetIntersectionAlternatingIntersectingIntervals) {
  const IntervalSet<int> x1({{0, 10}});
  const IntervalSet<int> y1({{-50, 1}, {9, 95}});
  const IntervalSet<int> expected_result1({{0, 1}, {9, 10}});

  IntervalSet<int> result1 = x1;
  result1.Intersection(y1);
  EXPECT_EQ(result1, expected_result1);

  const IntervalSet<int16_t> x2({{0, 10}, {20, 30}, {40, 90}});
  const IntervalSet<int16_t> y2(
      {{-50, -40}, {-2, 2}, {9, 21}, {32, 41}, {85, 95}});
  const IntervalSet<int16_t> expected_result2(
      {{0, 2}, {9, 10}, {20, 21}, {40, 41}, {85, 90}});

  IntervalSet<int16_t> result2 = x2;
  result2.Intersection(y2);
  EXPECT_EQ(result2, expected_result2);

  const IntervalSet<int64_t> x3({{-1, 5}, {5, 10}});
  const IntervalSet<int64_t> y3({{-10, 3}, {4, 95}});
  const IntervalSet<int64_t> expected_result3({{-1, 3}, {4, 10}});

  IntervalSet<int64_t> result3 = x3;
  result3.Intersection(y3);
  EXPECT_EQ(result3, expected_result3);
}

TEST_F(IntervalSetTest, IntervalSetIntersectsIntervalIntersectingIntervals) {
  const IntervalSet<int> x1({{0, 10}});

  EXPECT_TRUE(x1.IntersectsInterval({-1, 9}));
  EXPECT_TRUE(x1.IntersectsInterval({-1, 10}));
  EXPECT_TRUE(x1.IntersectsInterval({-1, 11}));
  EXPECT_TRUE(x1.IntersectsInterval({0, 9}));
  EXPECT_TRUE(x1.IntersectsInterval({0, 10}));
  EXPECT_TRUE(x1.IntersectsInterval({0, 11}));
  EXPECT_TRUE(x1.IntersectsInterval({1, 9}));
  EXPECT_TRUE(x1.IntersectsInterval({1, 10}));
  EXPECT_TRUE(x1.IntersectsInterval({1, 11}));
  EXPECT_TRUE(x1.IntersectsInterval({9, 10}));
  EXPECT_TRUE(x1.IntersectsInterval({9, 11}));

  const IntervalSet<int> x2({{0, 10}, {20, 30}, {40, 90}});

  EXPECT_TRUE(x2.IntersectsInterval({-1, 9}));
  EXPECT_TRUE(x2.IntersectsInterval({-1, 10}));
  EXPECT_TRUE(x2.IntersectsInterval({-1, 11}));
  EXPECT_TRUE(x2.IntersectsInterval({0, 9}));
  EXPECT_TRUE(x2.IntersectsInterval({0, 10}));
  EXPECT_TRUE(x2.IntersectsInterval({0, 11}));
  EXPECT_TRUE(x2.IntersectsInterval({1, 9}));
  EXPECT_TRUE(x2.IntersectsInterval({1, 10}));
  EXPECT_TRUE(x2.IntersectsInterval({1, 11}));
  EXPECT_TRUE(x2.IntersectsInterval({9, 10}));
  EXPECT_TRUE(x2.IntersectsInterval({9, 11}));

  EXPECT_TRUE(x2.IntersectsInterval({19, 29}));
  EXPECT_TRUE(x2.IntersectsInterval({19, 30}));
  EXPECT_TRUE(x2.IntersectsInterval({19, 31}));
  EXPECT_TRUE(x2.IntersectsInterval({20, 29}));
  EXPECT_TRUE(x2.IntersectsInterval({20, 30}));
  EXPECT_TRUE(x2.IntersectsInterval({20, 31}));
  EXPECT_TRUE(x2.IntersectsInterval({21, 29}));
  EXPECT_TRUE(x2.IntersectsInterval({21, 30}));
  EXPECT_TRUE(x2.IntersectsInterval({21, 31}));
  EXPECT_TRUE(x2.IntersectsInterval({29, 30}));
  EXPECT_TRUE(x2.IntersectsInterval({29, 31}));

  EXPECT_TRUE(x2.IntersectsInterval({39, 89}));
  EXPECT_TRUE(x2.IntersectsInterval({39, 90}));
  EXPECT_TRUE(x2.IntersectsInterval({39, 91}));
  EXPECT_TRUE(x2.IntersectsInterval({40, 89}));
  EXPECT_TRUE(x2.IntersectsInterval({40, 90}));
  EXPECT_TRUE(x2.IntersectsInterval({40, 91}));
  EXPECT_TRUE(x2.IntersectsInterval({41, 89}));
  EXPECT_TRUE(x2.IntersectsInterval({41, 90}));
  EXPECT_TRUE(x2.IntersectsInterval({41, 91}));
  EXPECT_TRUE(x2.IntersectsInterval({89, 90}));
  EXPECT_TRUE(x2.IntersectsInterval({89, 91}));

  EXPECT_TRUE(x2.IntersectsInterval({-1, 91}));
  EXPECT_TRUE(x2.IntersectsInterval({9, 21}));
  EXPECT_TRUE(x2.IntersectsInterval({9, 41}));
  EXPECT_TRUE(x2.IntersectsInterval({9, 91}));
  EXPECT_TRUE(x2.IntersectsInterval({29, 41}));
  EXPECT_TRUE(x2.IntersectsInterval({29, 91}));
}

TEST_F(IntervalSetTest, IntervalSetIntersectsIntervalEmptyInterval) {
  const IntervalSet<int> is({{1, 3}});
  EXPECT_FALSE(is.IntersectsInterval({2, 2}));
}

TEST_F(IntervalSetTest, IntervalSetIntersectionIdentical) {
  IntervalSet<int> copy(is);
  EXPECT_TRUE(copy.Intersects(is));
  EXPECT_TRUE(is.Intersects(copy));
  is.Intersection(copy);
  EXPECT_EQ(copy, is);
}

TEST_F(IntervalSetTest, IntervalSetIntersectionSuperset) {
  IntervalSet<int> mine(-1, 10000);
  EXPECT_TRUE(mine.Intersects(is));
  EXPECT_TRUE(is.Intersects(mine));
  EXPECT_TRUE(is.IntersectsInterval({-1, 10000}));
  mine.Intersection(is);
  EXPECT_EQ(is, mine);
}

TEST_F(IntervalSetTest, IntervalSetIntersectionSubset) {
  IntervalSet<int> copy(is);
  IntervalSet<int> theirs(-1, 10000);
  EXPECT_TRUE(copy.Intersects(theirs));
  EXPECT_TRUE(theirs.Intersects(copy));
  is.Intersection(theirs);
  EXPECT_EQ(copy, is);
}

TEST_F(IntervalSetTest, IntervalSetIntersectionLargeSet) {
  IntervalSet<int> mine, theirs;
  // mine: [0, 9), [10, 19), ..., [990, 999)
  for (int i = 0; i < 1000; i += 10) {
    mine.Add(i, i + 9);
  }

  theirs.Add(500, 520);
  theirs.Add(535, 545);
  theirs.Add(801, 809);
  EXPECT_TRUE(mine.Intersects(theirs));
  EXPECT_TRUE(theirs.Intersects(mine));
  mine.Intersection(theirs);
  EXPECT_TRUE(Check(mine, 5, 500, 509, 510, 519, 535, 539, 540, 545, 801, 809));
  EXPECT_TRUE(mine.Intersects(theirs));
  EXPECT_TRUE(theirs.Intersects(mine));
}

TEST_F(IntervalSetTest, IntervalSetIntersectionIntervalSelfAndOtherEmpty) {
  const IntervalSet<int> expected;
  IntervalSet<int> actual(is);
  {
    actual.Intersection(100, 100);
    EXPECT_EQ(actual, expected);
  }
  {
    actual.Intersection(100, 10);
    EXPECT_EQ(actual, expected);
  }

  IntervalSet<int> empty_set;
  {
    empty_set.Intersection(10, 100);
    EXPECT_EQ(empty_set, expected);
  }
}

TEST_F(IntervalSetTest, IntervalSetIntersectionIntervalBeforeAndAfter) {
  const IntervalSet<int> expected;
  {
    IntervalSet<int> actual(is);
    actual.Intersection(10, 100);
    EXPECT_EQ(actual, expected);
  }

  {
    IntervalSet<int> actual(is);
    actual.Intersection(0, 10);
    EXPECT_EQ(actual, expected);
  }

  {
    IntervalSet<int> actual(is);
    actual.Intersection(3000, 4000);
    EXPECT_EQ(actual, expected);
  }

  {
    IntervalSet<int> actual(is);
    actual.Intersection(2200, 2300);
    EXPECT_EQ(actual, expected);
  }
}

TEST_F(IntervalSetTest, IntervalSetIntersectionIntervalSubset) {
  {
    IntervalSet<int> actual(is);
    actual.Intersection(100, 200);
    EXPECT_EQ(actual, IntervalSet<int>({{100, 200}}));
  }

  {
    IntervalSet<int> actual(is);
    actual.Intersection(300, 400);
    EXPECT_EQ(actual, IntervalSet<int>({{300, 400}}));
  }

  {
    IntervalSet<int> actual(is);
    actual.Intersection(2100, 2200);
    EXPECT_EQ(actual, IntervalSet<int>({{2100, 2200}}));
  }
}

TEST_F(IntervalSetTest, IntervalSetIntersectionIntervalSuperset) {
  const IntervalSet<int> expected(is);
  is.Intersection(100, 2200);
  EXPECT_EQ(is, expected);

  is.Intersection(0, 3000);
  EXPECT_EQ(is, expected);
}

TEST_F(IntervalSetTest, IntervalSetIntersectionIntervalIntersects) {
  {
    IntervalSet<int> actual(is);
    actual.Intersection(50, 150);
    EXPECT_EQ(actual, IntervalSet<int>({{100, 150}}));
  }

  {
    IntervalSet<int> actual(is);
    actual.Intersection(50, 200);
    EXPECT_EQ(actual, IntervalSet<int>({{100, 200}}));
  }

  {
    IntervalSet<int> actual(is);
    actual.Intersection(50, 250);
    EXPECT_EQ(actual, IntervalSet<int>({{100, 200}}));
  }

  {
    IntervalSet<int> actual(is);
    actual.Intersection(50, 350);
    EXPECT_EQ(actual, IntervalSet<int>({{100, 200}, {300, 350}}));
  }

  {
    IntervalSet<int> actual(is);
    actual.Intersection(850, 950);
    EXPECT_EQ(actual, IntervalSet<int>({{900, 950}}));
  }

  {
    IntervalSet<int> actual(is);
    actual.Intersection(850, 950);
    EXPECT_EQ(actual, IntervalSet<int>({{900, 950}}));
  }

  {
    IntervalSet<int> actual(is);
    actual.Intersection(900, 1450);
    EXPECT_EQ(actual,
              IntervalSet<int>({{900, 1000}, {1100, 1200}, {1300, 1400}}));
  }

  {
    IntervalSet<int> actual(is);
    actual.Intersection(1950, 2000);
    EXPECT_EQ(actual, IntervalSet<int>({{1950, 2000}}));
  }

  {
    IntervalSet<int> actual(is);
    actual.Intersection(1950, 2150);
    EXPECT_EQ(actual, IntervalSet<int>({{1950, 2000}, {2100, 2150}}));
  }

  {
    IntervalSet<int> actual(is);
    actual.Intersection(1950, 2250);
    EXPECT_EQ(actual, IntervalSet<int>({{1950, 2000}, {2100, 2200}}));
  }
}

TEST_F(IntervalSetTest, IntervalSetGetIntersection) {
  IntervalSet<int> copy(is);
  const IntervalSet<int> expected({{1950, 2000}, {2100, 2200}});

  EXPECT_EQ(copy.GetIntersection(Interval<int>(1950, 2250)), expected);
  EXPECT_EQ(copy, is);

  EXPECT_EQ(copy.GetIntersection(1950, 2250), expected);
  EXPECT_EQ(copy, is);

  EXPECT_EQ(copy.GetIntersection(IntervalSet<int>({{1950, 2250}})), expected);
  EXPECT_EQ(copy, is);
}

TEST_F(IntervalSetTest, IntervalSetDifference) {
  is.Difference(other);
  EXPECT_TRUE(Check(is, 10, 100, 200, 300, 350, 360, 370, 380, 400, 530, 600,
                    700, 770, 900, 1000, 1100, 1200, 1900, 2000, 2100, 2200));
  IntervalSet<int> copy = is;
  is.Difference(copy);
  EXPECT_TRUE(is.empty());
}

TEST_F(IntervalSetTest, WithoutInterval) {
  IntervalSet<int> one_interval_set{Interval<int>(1, 10)};
  EXPECT_THAT(one_interval_set.Without(Interval<int>(3, 5)),
              ElementsAre(Interval<int>(1, 3), Interval<int>(5, 10)));

  EXPECT_THAT(one_interval_set.Without(Interval<int>(3, 15)),
              ElementsAre(Interval<int>(1, 3)));
  EXPECT_THAT(one_interval_set.Without(Interval<int>(-3, 5)),
              ElementsAre(Interval<int>(5, 10)));

  EXPECT_THAT(one_interval_set.Without(Interval<int>(-3, 15)), IsEmpty());

  IntervalSet<int> empty;
  EXPECT_THAT(empty.Without(Interval<int>(3, 5)), IsEmpty());
  EXPECT_THAT(empty.Without(Interval<int>(3, 15)), IsEmpty());
  EXPECT_THAT(empty.Without(Interval<int>(-3, 5)), IsEmpty());
  EXPECT_THAT(empty.Without(Interval<int>(-3, 15)), IsEmpty());

  IntervalSet<int> set{Interval<int>(1, 10), Interval<int>(20, 30)};
  EXPECT_THAT(set.Without(Interval<int>(5, 25)),
              ElementsAre(Interval<int>(1, 5), Interval<int>(25, 30)));

  EXPECT_THAT(set.Without(Interval<int>(22, 25)),
              ElementsAre(Interval<int>(1, 10), Interval<int>(20, 22),
                          Interval<int>(25, 30)));
}

TEST_F(IntervalSetTest, WithoutIntervalSet) {
  IntervalSet<int> difference = is.Without(other);
  EXPECT_TRUE(Check(difference, 10, 100, 200, 300, 350, 360, 370, 380, 400, 530,
                    600, 700, 770, 900, 1000, 1100, 1200, 1900, 2000, 2100,
                    2200));

  IntervalSet<int> empty = difference.Without(difference);
  EXPECT_THAT(empty, IsEmpty());
}

TEST_F(IntervalSetTest, IntervalSetErase) {
  IntervalSet<int> is;
  is.Add(10, 20);
  is.Add(30, 40);
  auto erased_it = is.erase(is.begin());
  EXPECT_EQ(erased_it, is.begin());
  EXPECT_EQ(is, IntervalSet<int>(30, 40));

  EXPECT_EQ(is.erase(is.begin()), is.end());
  EXPECT_TRUE(is.empty());
}

TEST_F(IntervalSetTest, IntervalSetSelfOps) {
  const auto copy = is;

  is.Union(is);
  EXPECT_EQ(copy, is);

  is.Intersection(is);
  EXPECT_EQ(copy, is);

  is.Difference(is);
  EXPECT_TRUE(is.empty());

  is = copy;
  is.Difference(*is.begin());
  EXPECT_EQ(is.size(), copy.size() - 1);
}

TEST_F(IntervalSetTest, IntervalSetDifferenceSingleBounds) {
  std::vector<Interval<int>> ivals(other.begin(), other.end());
  for (const Interval<int>& ival : ivals) {
    is.Difference(ival.start(), ival.limit());
  }
  EXPECT_TRUE(Check(is, 10, 100, 200, 300, 350, 360, 370, 380, 400, 530, 600,
                    700, 770, 900, 1000, 1100, 1200, 1900, 2000, 2100, 2200));
}

TEST_F(IntervalSetTest, IntervalSetDifferenceSingleInterval) {
  std::vector<Interval<int>> ivals(other.begin(), other.end());
  for (const Interval<int>& ival : ivals) {
    is.Difference(ival);
  }
  EXPECT_TRUE(Check(is, 10, 100, 200, 300, 350, 360, 370, 380, 400, 530, 600,
                    700, 770, 900, 1000, 1100, 1200, 1900, 2000, 2100, 2200));
}

TEST_F(IntervalSetTest, IntervalSetDifferenceAlternatingIntervals) {
  IntervalSet<int> mine, theirs;
  mine.Add(10, 20);
  mine.Add(40, 50);
  mine.Add(60, 70);
  theirs.Add(25, 39);
  theirs.Add(55, 59);
  theirs.Add(75, 79);

  mine.Difference(theirs);
  EXPECT_TRUE(Check(mine, 3, 10, 20, 40, 50, 60, 70));
}

TEST_F(IntervalSetTest, IntervalSetDifferenceEmptyMine) {
  IntervalSet<std::string> mine, theirs;
  theirs.Add("a", "b");

  mine.Difference(theirs);
  EXPECT_TRUE(mine.empty());
}

TEST_F(IntervalSetTest, IntervalSetDifferenceEmptyTheirs) {
  IntervalSet<std::string> mine, theirs;
  mine.Add("a", "b");

  mine.Difference(theirs);
  EXPECT_EQ(1, mine.size());
  EXPECT_EQ("a", mine.begin()->start());
  EXPECT_EQ("b", mine.begin()->limit());
}

TEST_F(IntervalSetTest, IntervalSetDifferenceTheirsBeforeMine) {
  IntervalSet<std::string> mine, theirs;
  mine.Add("y", "z");
  theirs.Add("a", "b");

  mine.Difference(theirs);
  EXPECT_EQ(1, mine.size());
  EXPECT_EQ("y", mine.begin()->start());
  EXPECT_EQ("z", mine.begin()->limit());
}

TEST_F(IntervalSetTest, IntervalSetDifferenceMineBeforeTheirs) {
  IntervalSet<std::string> mine, theirs;
  mine.Add("a", "b");
  theirs.Add("y", "z");

  mine.Difference(theirs);
  EXPECT_EQ(1, mine.size());
  EXPECT_EQ("a", mine.begin()->start());
  EXPECT_EQ("b", mine.begin()->limit());
}

TEST_F(IntervalSetTest, IntervalSetDifferenceIdentical) {
  IntervalSet<std::string> mine;
  mine.Add("a", "b");
  mine.Add("c", "d");
  IntervalSet<std::string> theirs(mine);

  mine.Difference(theirs);
  EXPECT_TRUE(mine.empty());
}

TEST_F(IntervalSetTest, EmptyComplement) {
  // The complement of an empty set is the input interval:
  IntervalSet<int> iset;
  iset.Complement(100, 200);
  EXPECT_TRUE(Check(iset, 1, 100, 200));
}

TEST(IntervalSetMultipleCompactionTest, OuterCovering) {
  IntervalSet<int> iset;
  // First add a bunch of disjoint ranges
  iset.Add(100, 150);
  iset.Add(200, 250);
  iset.Add(300, 350);
  iset.Add(400, 450);
  EXPECT_TRUE(Check(iset, 4, 100, 150, 200, 250, 300, 350, 400, 450));
  // Now add a big range that covers all of these ranges
  iset.Add(0, 500);
  EXPECT_TRUE(Check(iset, 1, 0, 500));
}

TEST(IntervalSetMultipleCompactionTest, InnerCovering) {
  IntervalSet<int> iset;
  // First add a bunch of disjoint ranges
  iset.Add(100, 150);
  iset.Add(200, 250);
  iset.Add(300, 350);
  iset.Add(400, 450);
  EXPECT_TRUE(Check(iset, 4, 100, 150, 200, 250, 300, 350, 400, 450));
  // Now add a big range that partially covers the left and right most ranges.
  iset.Add(125, 425);
  EXPECT_TRUE(Check(iset, 1, 100, 450));
}

TEST(IntervalSetMultipleCompactionTest, LeftCovering) {
  IntervalSet<int> iset;
  // First add a bunch of disjoint ranges
  iset.Add(100, 150);
  iset.Add(200, 250);
  iset.Add(300, 350);
  iset.Add(400, 450);
  EXPECT_TRUE(Check(iset, 4, 100, 150, 200, 250, 300, 350, 400, 450));
  // Now add a big range that partially covers the left most range.
  iset.Add(125, 500);
  EXPECT_TRUE(Check(iset, 1, 100, 500));
}

TEST(IntervalSetMultipleCompactionTest, RightCovering) {
  IntervalSet<int> iset;
  // First add a bunch of disjoint ranges
  iset.Add(100, 150);
  iset.Add(200, 250);
  iset.Add(300, 350);
  iset.Add(400, 450);
  EXPECT_TRUE(Check(iset, 4, 100, 150, 200, 250, 300, 350, 400, 450));
  // Now add a big range that partially covers the right most range.
  iset.Add(0, 425);
  EXPECT_TRUE(Check(iset, 1, 0, 450));
}

// Helper method for testing and verifying the results of a one-interval
// completement case.
static bool CheckOneComplement(int add_min, int add_max, int comp_min,
                               int comp_max, int count, ...) {
  IntervalSet<int> iset;
  iset.Add(add_min, add_max);
  iset.Complement(comp_min, comp_max);
  bool result = true;
  va_list ap;
  va_start(ap, count);
  if (!VA_Check(iset, count, ap)) {
    result = false;
  }
  va_end(ap);
  return result;
}

TEST_F(IntervalSetTest, SingleIntervalComplement) {
  // Verify the complement of a set with one interval (i):
  //                     |-----   i  -----|
  // |----- args -----|
  EXPECT_TRUE(CheckOneComplement(0, 10, 50, 150, 1, 50, 150));

  //          |-----   i  -----|
  //    |----- args -----|
  EXPECT_TRUE(CheckOneComplement(50, 150, 0, 100, 1, 0, 50));

  //    |-----   i  -----|
  //    |----- args -----|
  EXPECT_TRUE(CheckOneComplement(50, 150, 50, 150, 0));

  //    |----------   i  ----------|
  //        |----- args -----|
  EXPECT_TRUE(CheckOneComplement(50, 500, 100, 300, 0));

  //        |----- i -----|
  //    |---------- args  ----------|
  EXPECT_TRUE(CheckOneComplement(50, 500, 0, 800, 2, 0, 50, 500, 800));

  //    |-----   i  -----|
  //          |----- args -----|
  EXPECT_TRUE(CheckOneComplement(50, 150, 100, 300, 1, 150, 300));

  //    |-----   i  -----|
  //                        |----- args -----|
  EXPECT_TRUE(CheckOneComplement(50, 150, 200, 300, 1, 200, 300));
}

// Helper method that copies <iset> and takes its complement,
// returning false if Check succeeds.
static bool CheckComplement(const IntervalSet<int>& iset, int comp_min,
                            int comp_max, int count, ...) {
  IntervalSet<int> iset_copy = iset;
  iset_copy.Complement(comp_min, comp_max);
  bool result = true;
  va_list ap;
  va_start(ap, count);
  if (!VA_Check(iset_copy, count, ap)) {
    result = false;
  }
  va_end(ap);
  return result;
}

TEST_F(IntervalSetTest, MultiIntervalComplement) {
  // Initialize a small test set:
  IntervalSet<int> iset;
  iset.Add(100, 200);
  iset.Add(300, 400);
  iset.Add(500, 600);

  //                     |-----   i  -----|
  // |----- comp -----|
  EXPECT_TRUE(CheckComplement(iset, 0, 50, 1, 0, 50));

  //          |-----   i  -----|
  //    |----- comp -----|
  EXPECT_TRUE(CheckComplement(iset, 0, 200, 1, 0, 100));
  EXPECT_TRUE(CheckComplement(iset, 0, 220, 2, 0, 100, 200, 220));

  //    |-----   i  -----|
  //    |----- comp -----|
  EXPECT_TRUE(CheckComplement(iset, 100, 600, 2, 200, 300, 400, 500));

  //    |----------   i  ----------|
  //        |----- comp -----|
  EXPECT_TRUE(CheckComplement(iset, 300, 400, 0));
  EXPECT_TRUE(CheckComplement(iset, 250, 400, 1, 250, 300));
  EXPECT_TRUE(CheckComplement(iset, 300, 450, 1, 400, 450));
  EXPECT_TRUE(CheckComplement(iset, 250, 450, 2, 250, 300, 400, 450));

  //        |----- i -----|
  //    |---------- comp  ----------|
  EXPECT_TRUE(
      CheckComplement(iset, 0, 700, 4, 0, 100, 200, 300, 400, 500, 600, 700));

  //    |-----   i  -----|
  //          |----- comp -----|
  EXPECT_TRUE(CheckComplement(iset, 400, 700, 2, 400, 500, 600, 700));
  EXPECT_TRUE(CheckComplement(iset, 350, 700, 2, 400, 500, 600, 700));

  //    |-----   i  -----|
  //                        |----- comp -----|
  EXPECT_TRUE(CheckComplement(iset, 700, 800, 1, 700, 800));
}

// Verifies ToString, operator<< don't assert.
TEST_F(IntervalSetTest, ToString) {
  IntervalSet<int> iset;
  iset.Add(300, 400);
  iset.Add(100, 200);
  iset.Add(500, 600);
  EXPECT_TRUE(!iset.ToString().empty());
  VLOG(2) << iset;
  // Order and format of ToString() output is guaranteed.
  EXPECT_EQ("[100, 200) [300, 400) [500, 600)", iset.ToString());
  EXPECT_EQ("[1, 2)", IntervalSet<int>(1, 2).ToString());
  EXPECT_EQ("", IntervalSet<int>().ToString());
}

TEST_F(IntervalSetTest, ConstructionDiscardsEmptyInterval) {
  EXPECT_TRUE(IntervalSet<int>(Interval<int>(2, 2)).empty());
  EXPECT_TRUE(IntervalSet<int>(2, 2).empty());
  EXPECT_FALSE(IntervalSet<int>(Interval<int>(2, 3)).empty());
  EXPECT_FALSE(IntervalSet<int>(2, 3).empty());
}

TEST_F(IntervalSetTest, Swap) {
  IntervalSet<int> a, b;
  a.Add(300, 400);
  b.Add(100, 200);
  b.Add(500, 600);
  a.Swap(&b);
  EXPECT_TRUE(Check(a, 2, 100, 200, 500, 600));
  EXPECT_TRUE(Check(b, 1, 300, 400));
  swap(a, b);
  EXPECT_TRUE(Check(a, 1, 300, 400));
  EXPECT_TRUE(Check(b, 2, 100, 200, 500, 600));
}

TEST_F(IntervalSetTest, OutputReturnsOstreamRef) {
  std::stringstream ss;
  const IntervalSet<int> v(Interval<int>(1, 2));
  auto return_type_is_a_ref = [](std::ostream&) {};
  return_type_is_a_ref(ss << v);
}

TEST_F(IntervalSetTest, HeterogeneousIntervals) {
  IntervalSet<std::string> set;
  set.Add("a", "b");
  set.Add("g", "h");

  EXPECT_TRUE(set.IsDisjoint(Interval<absl::string_view>("bdc", "def")));
  EXPECT_FALSE(set.IsDisjoint(Interval<absl::string_view>("abc", "def")));
  EXPECT_FALSE(set.IsDisjoint(Interval<absl::string_view>("def", "ghi")));

  EXPECT_TRUE(set.IsDisjoint(
      Interval<absl::Cord>(absl::Cord("bdc"), absl::Cord("def"))));
  EXPECT_FALSE(set.IsDisjoint(
      Interval<absl::Cord>(absl::Cord("abc"), absl::Cord("def"))));
  EXPECT_FALSE(set.IsDisjoint(
      Interval<absl::Cord>(absl::Cord("def"), absl::Cord("ghi"))));
}

struct NotOstreamable {
  bool operator<(const NotOstreamable& other) const { return false; }
  bool operator>(const NotOstreamable& other) const { return false; }
  bool operator!=(const NotOstreamable& other) const { return false; }
  bool operator>=(const NotOstreamable& other) const { return true; }
  bool operator<=(const NotOstreamable& other) const { return true; }
  bool operator==(const NotOstreamable& other) const { return true; }
};

TEST_F(IntervalSetTest, IntervalOfTypeWithNoOstreamSupport) {
  const NotOstreamable v;
  const IntervalSet<NotOstreamable> d(Interval<NotOstreamable>(v, v));
  // EXPECT_EQ builds a string representation of d. If d::operator<<()
  // would be defined then this test would not compile because NotOstreamable
  // objects lack the operator<<() support.
  EXPECT_EQ(d, d);
}

struct IntFactory {
  using type = int;
  type operator()(int i) const { return i; }
};

struct ShortStringFactory {
  using type = std::string;
  type operator()(int i) const { return absl::StrCat(i); }
};

struct LongStringFactory {
  using type = std::string;
  type operator()(int i) const {
    return absl::StrCat("a long string value ", i);
  }
};

template <typename TFactory>
static void BM_Contains(benchmark::State& state) {
  const int num_intervals = state.range(0);
  const TFactory factory;
  IntervalSet<typename TFactory::type> set;
  for (int i = 0; i < num_intervals; ++i) {
    set.Add(factory(2 * i), factory(2 * i + 1));
  }

  std::vector<typename TFactory::type> values;
  for (int i = 0; i < 2 * num_intervals; ++i) {
    values.push_back(factory(i));
  }

  std::minstd_rand bit_gen(0);  // Deterministic RNG.
  absl::c_shuffle(values, bit_gen);

  while (state.KeepRunningBatch(values.size())) {
    for (auto& v : values) {
      benchmark::DoNotOptimize(set);
      benchmark::DoNotOptimize(set.Contains(v));
    }
  }
}

BENCHMARK(BM_Contains<IntFactory>)->Arg(1)->Arg(10);
BENCHMARK(BM_Contains<ShortStringFactory>)->Arg(1)->Arg(10);
BENCHMARK(BM_Contains<LongStringFactory>)->Arg(1)->Arg(10);

static void BM_Difference(benchmark::State& state) {
  IntervalSet<int> difference_set;
  int start = 10;
  for (int i = 0; i < 1000000; ++i) {
    difference_set.Add(start, start + 5);
    start += 7;
  }

  for (auto s : state) {
    // Create an interval somewhere in the middle of the difference set.
    IntervalSet<int> initial(1000000, 1000020);
    initial.Difference(difference_set);
  }
}

BENCHMARK(BM_Difference);

static void BM_IntersectionSmallAndLarge(benchmark::State& state) {
  const int size = state.range(0);

  // Intersects constant size 'mine' with large 'theirs'.
  IntervalSet<int> theirs;
  for (int i = 0; i < size; ++i) {
    theirs.Add(2 * i, 2 * i + 1);
  }

  for (auto s : state) {
    // 'mine' starts in the middle of 'theirs'.
    IntervalSet<int> mine(size, size + 10);
    mine.Intersection(theirs);
  }
}

BENCHMARK(BM_IntersectionSmallAndLarge)->Range(0, 1 << 23);

static void BM_IntersectionIdentical(benchmark::State& state) {
  const int size = state.range(0);

  // Intersects identical 'mine' and 'theirs'.
  IntervalSet<int> mine;
  for (int i = 0; i < size; ++i) {
    mine.Add(2 * i, 2 * i + 1);
  }
  IntervalSet<int> theirs(mine);

  for (auto s : state) {
    mine.Intersection(theirs);
  }
}

BENCHMARK(BM_IntersectionIdentical)->Range(0, 1 << 23);

constexpr size_t kUnionIntervals = 10000000;

std::vector<IntervalSet<int>> InitIntervals() {
  // The worst-performance for the iterative Union is the non-overlapping
  // case, but that's not interesting. Here we construct a repeating pattern
  // that merges 3 intervals into a single one.
  // i%3 == 0: BASE + [0..4]
  // i%3 == 1: BASE + [3..7]
  // i%3 == 2: BASE + [6..7]
  constexpr int kOffset[] = {4, 4, 1};

  std::vector<IntervalSet<int>> sets(101, IntervalSet<int>());
  for (size_t i = 0; i < kUnionIntervals; ++i) {
    const size_t idx = i % sets.size();
    sets[idx].Add(static_cast<int>(3 * i),
                  static_cast<int>(3 * i + kOffset[i % 3]));
  }
  return sets;
}

void BM_Union(benchmark::State& state) {
  auto sets = InitIntervals();
  for (auto running : state) {
    IntervalSet<int> result;
    for (const auto& s : sets) {
      result.Union(s);
    }
  }
}

BENCHMARK(BM_Union);

void BM_IntervalSetUnion(benchmark::State& state) {
  auto sets = InitIntervals();
  std::vector<const IntervalSet<int>*> pointers;
  pointers.reserve(sets.size());
  for (auto running : state) {
    pointers.clear();
    for (const auto& s : sets) {
      pointers.push_back(&s);
    }
    gtl::IntervalSetUnion(absl::MakeConstSpan(pointers));
  }
}

BENCHMARK(BM_IntervalSetUnion);

class IntervalSetInitTest : public testing::Test {
 protected:
  const std::vector<Interval<int>> intervals_{{0, 1}, {2, 4}};
};

TEST_F(IntervalSetInitTest, DirectInit) {
  std::initializer_list<Interval<int>> il = {{0, 1}, {2, 3}, {3, 4}};
  IntervalSet<int> s(il);
  EXPECT_THAT(s, ElementsAreArray(intervals_));
}

TEST_F(IntervalSetInitTest, CopyInit) {
  std::initializer_list<Interval<int>> il = {{0, 1}, {2, 3}, {3, 4}};
  IntervalSet<int> s = il;
  EXPECT_THAT(s, ElementsAreArray(intervals_));
}

TEST_F(IntervalSetInitTest, AssignIterPair) {
  IntervalSet<int> s(0, 1000);  // Make sure assign clears.
  s.assign(intervals_.begin(), intervals_.end());
  EXPECT_THAT(s, ElementsAreArray(intervals_));
}

TEST_F(IntervalSetInitTest, AssignInitList) {
  IntervalSet<int> s(0, 1000);  // Make sure assign clears.
  s.assign({{0, 1}, {2, 3}, {3, 4}});
  EXPECT_THAT(s, ElementsAreArray(intervals_));
}

TEST_F(IntervalSetInitTest, AssignmentInitList) {
  std::initializer_list<Interval<int>> il = {{0, 1}, {2, 3}, {3, 4}};
  IntervalSet<int> s;
  s = il;
  EXPECT_THAT(s, ElementsAreArray(intervals_));
}

TEST_F(IntervalSetInitTest, BracedInitThenBracedAssign) {
  IntervalSet<int> s{{0, 1}, {2, 3}, {3, 4}};
  s = {{0, 1}, {2, 4}};
  EXPECT_THAT(s, ElementsAreArray(intervals_));
}

}  // namespace
}  // namespace gtl

// A class representing an entity being memory managed for this test.
class DummyClass {
 public:
  static size_t allocated_bytes() { return allocated_bytes_; }

 private:
  static size_t allocated_bytes_;

  // Make `allocated_bytes_` available to the allocator that is used for the
  // allocations in the tests.
};

size_t DummyClass::allocated_bytes_ = 0;

namespace gtl {
namespace {

using IntervalSetAllocatorTest = testing::Test;

TEST_F(IntervalSetAllocatorTest, StdAllocator) {
  IntervalSet<int, std::allocator<Interval<int>>> i{{0, 3}, {5, 6}};
  EXPECT_TRUE(i.Contains(0));
  EXPECT_TRUE(i.Contains(1));
  EXPECT_TRUE(i.Contains(5));
  EXPECT_FALSE(i.Contains(3));
  EXPECT_FALSE(i.Contains(7));

  // Interaction with other IntervalSets with explicit equivalent allocators.
  EXPECT_TRUE(
      i.Intersects(IntervalSet<int, std::allocator<Interval<int>>>({{3, 6}})));
  EXPECT_FALSE(
      i.Intersects(IntervalSet<int, std::allocator<Interval<int>>>({{3, 5}})));

  // Interaction with other IntervalSets with default allocators.
  EXPECT_TRUE(i.Intersects(IntervalSet<int>({{3, 6}})));
  EXPECT_FALSE(i.Intersects(IntervalSet<int>({{3, 5}})));
}

// The template argument to the allocator passed does not matter, it is rebound
// to what the internal Set requires.
TEST_F(IntervalSetAllocatorTest, WrongAllocator) {
  IntervalSet<int, std::allocator<Interval<bool>>> i{{0, 3}, {5, 6}};
  EXPECT_TRUE(i.Contains(0));
  EXPECT_TRUE(i.Contains(1));
  EXPECT_TRUE(i.Contains(5));
  EXPECT_FALSE(i.Contains(3));
  EXPECT_FALSE(i.Contains(7));

  // Interaction with other IntervalSets with explicit equivalent allocators.
  EXPECT_TRUE(
      i.Intersects(IntervalSet<int, std::allocator<Interval<bool>>>({{3, 6}})));
  EXPECT_FALSE(
      i.Intersects(IntervalSet<int, std::allocator<Interval<bool>>>({{3, 5}})));
}

template <typename TFactory>
static void BM_AddRandom(benchmark::State& state) {
  const TFactory factory;
  const int num_intervals = state.range(0);

  std::vector<std::pair<int, int>> intervals;
  intervals.reserve(num_intervals);
  std::minstd_rand bit_gen(0);  // Deterministic RNG.
  for (int i = 0; i < num_intervals; ++i) {
    const int start = absl::Uniform(bit_gen, 0, 100);
    const int width = absl::Uniform(bit_gen, 0, 10);
    intervals.emplace_back(start, start + width);
  }

  while (state.KeepRunningBatch(num_intervals)) {
    IntervalSet<typename TFactory::type> set;
    for (const auto& [start, limit] : intervals) {
      benchmark::DoNotOptimize(set);
      set.Add(factory(start), factory(limit));
    }
    benchmark::DoNotOptimize(set);
  }
}
BENCHMARK(BM_AddRandom<IntFactory>)->Arg(10)->Arg(100);
BENCHMARK(BM_AddRandom<ShortStringFactory>)->Arg(10)->Arg(100);
BENCHMARK(BM_AddRandom<LongStringFactory>)->Arg(10)->Arg(100);

// Base helper method for verifying the contents of an interval set.
// Returns true iff <is> contains <count> intervals whose successive
// endpoints match the sequence of args in <ap>:
template <typename T>
bool CheckTyped(const IntervalSet<T>& is,
                absl::Span<const std::pair<T, T>> reference) {
  std::vector<Interval<T>> intervals(is.begin(), is.end());
  const size_t expected_count = reference.size();

  if (expected_count != intervals.size()) {
    LOG(ERROR) << "Expected " << expected_count << " intervals, got "
               << intervals.size() << ": " << is;
    return false;
  }
  if (expected_count != is.size()) {
    LOG(ERROR) << "Expected " << expected_count << " intervals, got Size "
               << is.size() << ": " << is;
    return false;
  }
  bool result = true;
  for (size_t i = 0; i < expected_count; i++) {
    T min = reference[i].first;
    T max = reference[i].second;
    if (min != intervals[i].start() || max != intervals[i].limit()) {
      LOG(ERROR) << "Expected: [" << min << ", " << max << ") got "
                 << intervals[i] << " in " << is;
      result = false;
    }
  }
  return result;
}

// struct IntFactory {
//   using type = int;
//   type operator()(int i) const { return i; }
// };

struct ShortFactory {
  using type = int16_t;
  type operator()(int16_t i) const { return i; }
};

struct CharFactory {
  using type = char;
  type operator()(char i) const { return i; }
};

// A reference implementation of the identical algorithm but not
// finding contiguous intervals in the input.
template <typename T>
void AddIntsToIntervalSetReference(absl::Span<T> sorted_ints,
                                   IntervalSet<T>& interval_set) {
  if (sorted_ints.empty()) return;
  for (size_t i = 0; i < sorted_ints.size(); ++i) {
    interval_set.Add(sorted_ints[i], sorted_ints[i] + 1);
  }
}

// Create a sorted array of randomly chosen num_items distinct integers in the
// range [0, max_value).
std::vector<int> CreateRandomArray(int num_items, int max_value) {
  const int num_empty_items = max_value - num_items;
  const bool inverted = num_empty_items < num_items;
  std::set<int> x;
  std::minstd_rand bit_gen(123);  // Deterministic RNG.

  const size_t items_in_set = std::min(num_items, num_empty_items);
  while (x.size() < items_in_set) {
    const int value = absl::Uniform(bit_gen, 0, max_value);
    x.insert(value);
  }

  std::vector<int> vec;
  vec.reserve(num_items);
  for (int i = 0; i < max_value; ++i) {
    const bool in_set = x.find(i) != x.end();
    if (in_set != inverted) {
      vec.push_back(i);
    }
  }
  return vec;
}

TEST(CreateRandomArray, Simple) {
  const int kMaxValue = 5;
  for (int num_items = 0; num_items < kMaxValue; ++num_items) {
    std::vector<int> tmp = CreateRandomArray(num_items, kMaxValue);
    // int num_set = 0;
    ASSERT_EQ(tmp.size(), num_items);
    if (tmp.empty()) continue;
    EXPECT_GE(tmp[0], 0);
    for (size_t idx = 1; idx < tmp.size(); ++idx) {
      EXPECT_LT(tmp[idx - 1], tmp[idx]);
    }
    EXPECT_LT(*tmp.rbegin(), kMaxValue);
  }
}

template <typename IntType>
std::vector<IntType> CreateSortedArray(int num_items, int increment) {
  std::vector<IntType> vec;
  vec.reserve(num_items);
  int val = 0;
  for (int i = 0; i < num_items; ++i) {
    vec.push_back(val);
    val += increment;
  }
  return vec;
}

template <typename TFactory>
void BM_AddSequentialReference(benchmark::State& state) {
  const int num_items = state.range(0);
  const int increment = state.range(1);
  std::vector<typename TFactory::type> sorted_vec =
      CreateSortedArray<typename TFactory::type>(num_items, increment);
  for (auto s : state) {
    IntervalSet<typename TFactory::type> set;
    AddIntsToIntervalSetReference(
        absl::Span<typename TFactory::type>(sorted_vec), set);
    benchmark::DoNotOptimize(set);
  }
}

template <typename TFactory>
void BM_AddSequential(benchmark::State& state) {
  const int num_items = state.range(0);
  const int increment = state.range(1);
  std::vector<typename TFactory::type> sorted_vec =
      CreateSortedArray<typename TFactory::type>(num_items, increment);
  for (auto s : state) {
    IntervalSet<typename TFactory::type> set;
    AddIntsToIntervalSet(absl::Span<typename TFactory::type>(sorted_vec), set);
    benchmark::DoNotOptimize(set);
  }
}

TEST(IntervalSetIntCreateHelper, Empty) {
  IntervalSet<int> iset;
  std::vector<int> inputs;
  AddIntsToIntervalSet(absl::Span<int>(inputs), iset);
  EXPECT_TRUE(CheckTyped(iset, {}));
}

TEST(IntervalSetIntCreateHelper, SingleItem) {
  IntervalSet<int> iset;
  std::vector<int> inputs{1};
  AddIntsToIntervalSet(absl::Span<int>(inputs), iset);
  EXPECT_TRUE(CheckTyped(iset, {{1, 2}}));
}

TEST(IntervalSetIntCreateHelper, TwoItem) {
  IntervalSet<int> iset;
  std::vector<int> inputs{1, 2};
  AddIntsToIntervalSet(absl::Span<int>(inputs), iset);
  EXPECT_TRUE(CheckTyped(iset, {{1, 3}}));
}

TEST(IntervalSetIntCreateHelper, ThreeItem) {
  IntervalSet<int> iset;
  std::vector<int> inputs{1, 2, 3};
  AddIntsToIntervalSet(absl::Span<int>(inputs), iset);
  EXPECT_TRUE(CheckTyped(iset, {{1, 4}}));
}

TEST(IntervalSetIntCreateHelper, ThreeItemAndStraggler) {
  IntervalSet<int> iset;
  std::vector<int> inputs{1, 2, 3, 5};
  AddIntsToIntervalSet(absl::Span<int>(inputs), iset);
  EXPECT_TRUE(CheckTyped(iset, {{1, 4}, {5, 6}}));
}

TEST(IntervalSetIntCreateHelper, OutOfOrderCheck) {
  const uint16_t max_item = (1L << 16) - 1;
  const uint16_t penultimate = max_item - 1;
  IntervalSet<uint16_t> iset;
  std::vector<uint16_t> inputs{0, 1, 2, 3, 5, penultimate, max_item};

  // Make sure that every permutation gives the same output.
  do {
    // Beginning with the sorted data, this loop will run for each permutation
    // of v.
    AddIntsToIntervalSet(absl::Span<uint16_t>(inputs), iset);
    EXPECT_TRUE(CheckTyped(iset, {{0, 4}, {5, 6}, {65534, 65535}}));
  } while (std::next_permutation(std::begin(inputs), std::end(inputs)));
}

TEST(IntervalSetIntCreateHelper, OutOfOrderCheckAndOverflow) {
  const uint16_t max_item = (1L << 16) - 1;
  const uint16_t penultimate = max_item - 1;
  IntervalSet<uint16_t> iset;
  std::vector<uint16_t> inputs{penultimate - 1, penultimate, max_item, 0};

  AddIntsToIntervalSet(absl::Span<uint16_t>(inputs), iset);
  EXPECT_TRUE(CheckTyped(iset, {{0, 1}, {penultimate - 1, max_item}}));
}

TEST(IntervalSetIntCreateHelper, OverflowAndOutOfOrderCheck) {
  const uint16_t max_item = (1L << 16) - 1;
  IntervalSet<uint16_t> iset;
  std::vector<uint16_t> inputs{max_item, max_item, max_item, 0, max_item, 1};

  AddIntsToIntervalSet(absl::Span<uint16_t>(inputs), iset);
  EXPECT_TRUE(CheckTyped(iset, {{0, 2}}));
}

TEST(IntervalSetIntCreateHelper, NonSpanContainer) {
  const uint16_t max_item = (1L << 16) - 1;
  IntervalSet<uint16_t> iset;
  std::set<uint16_t> inputs{max_item, 0, 1};
  AddIntsToIntervalSet(inputs, iset);
  EXPECT_TRUE(CheckTyped(iset, {{0, 2}}));
}

TEST(IntervalSetIntCreateHelper, Bounds) {
  IntervalSet<signed char> iset;
  std::vector<signed char> inputs{-128};
  AddIntsToIntervalSet(absl::Span<signed char>(inputs), iset);
  EXPECT_TRUE(CheckTyped(iset, {{-128, -127}}));
}

TEST(IntervalSetUtil, UpperBound) {
  IntervalSet<signed char> iset;
  // The max int of the type cannot be contained in an interval set.
  std::vector<signed char> inputs{127};
  AddIntsToIntervalSet(absl::Span<signed char>(inputs), iset);
  EXPECT_TRUE(CheckTyped(iset, {}));
}

TEST(IntervalSetUtil, LowerBound) {
  IntervalSet<signed char> iset;
  // The max int of the type cannot be contained in an interval set.
  std::vector<signed char> inputs{-128};
  AddIntsToIntervalSet(absl::Span<signed char>(inputs), iset);
  EXPECT_TRUE(CheckTyped(iset, {{-128, -127}}));
}

TEST(IntervalSetUtil, LargeTypes) {
  using IntType = int64_t;
  const IntType min_val = std::numeric_limits<IntType>::min();
  const IntType max_val = std::numeric_limits<IntType>::max();
  const IntType penultimate = max_val - 1;
  {
    IntervalSet<IntType> iset;
    std::vector<IntType> inputs{min_val};
    AddIntsToIntervalSet(absl::Span<IntType>(inputs), iset);
    EXPECT_TRUE(CheckTyped(iset, {{min_val, min_val + 1}}));
  }

  {
    IntervalSet<IntType> iset;
    std::vector<IntType> inputs{max_val};
    AddIntsToIntervalSet(absl::Span<IntType>(inputs), iset);
    EXPECT_TRUE(CheckTyped(iset, {}));
  }
  // Make sure that the a sequence that includes the maximum integral value
  // Still gets the entire representable range (which cannot include the
  // maximum integral value).
  {
    IntervalSet<IntType> iset;
    std::vector<IntType> inputs{penultimate, max_val};
    AddIntsToIntervalSet(absl::Span<IntType>(inputs), iset);
    EXPECT_TRUE(CheckTyped(iset, {{penultimate, max_val}}));
  }
}

BENCHMARK(BM_AddSequentialReference<IntFactory>)
    ->ArgPair(1000000, 0)
    ->ArgPair(1000000, 1)
    ->ArgPair(1000000, 2);

BENCHMARK(BM_AddSequential<IntFactory>)
    ->ArgPair(1000000, 0)
    ->ArgPair(1000000, 1)
    ->ArgPair(1000000, 2);

BENCHMARK(BM_AddSequentialReference<ShortFactory>)
    ->ArgPair(30000, 0)
    ->ArgPair(30000, 1)
    ->ArgPair(30000, 2);

BENCHMARK(BM_AddSequential<ShortFactory>)
    ->ArgPair(30000, 0)
    ->ArgPair(30000, 1)
    ->ArgPair(30000, 2);

BENCHMARK(BM_AddSequentialReference<CharFactory>)
    ->ArgPair(255, 0)
    ->ArgPair(255, 1)
    ->ArgPair(255, 2);

BENCHMARK(BM_AddSequential<CharFactory>)
    ->ArgPair(255, 0)
    ->ArgPair(255, 1)
    ->ArgPair(255, 2);

template <typename TFactory>
static void BM_IsDisjoint(benchmark::State& state) {
  const int num_intervals = state.range(0);
  const TFactory factory;
  IntervalSet<typename TFactory::type> set;
  for (int i = 0; i < num_intervals; ++i) {
    set.Add(factory(2 * i), factory(2 * i + 1));
  }

  std::minstd_rand bit_gen(0);  // Deterministic RNG.

  // Create a bunch of query intervals covering between 0 and 2 intervals.
  std::vector<Interval<typename TFactory::type>> intervals;
  for (int i = 0; i < 2 * num_intervals; ++i) {
    int width = absl::Uniform(bit_gen, 0, 4);
    intervals.emplace_back(factory(2 * i), factory(i + width));
  }
  absl::c_shuffle(intervals, bit_gen);

  while (state.KeepRunningBatch(intervals.size())) {
    for (auto& interval : intervals) {
      benchmark::DoNotOptimize(set);
      bool result = set.IsDisjoint(interval);
      benchmark::DoNotOptimize(result);
    }
  }
}

BENCHMARK(BM_IsDisjoint<IntFactory>)->Arg(1)->Arg(10);
BENCHMARK(BM_IsDisjoint<ShortStringFactory>)->Arg(1)->Arg(10);
BENCHMARK(BM_IsDisjoint<LongStringFactory>)->Arg(1)->Arg(10);

}  // namespace
}  // namespace gtl
