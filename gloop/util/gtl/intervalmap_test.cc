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

#include "gloop/util/gtl/intervalmap.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "benchmark/benchmark.h"
#include "gloop/util/random/acmrandom.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Pointee;
using ::testing::SizeIs;

// We are testing the following containers for gtl::IntervalMap
//   stl::set
template <class K, class V, class Comparison = std::less<K>,
          class Allocator = std::allocator<std::pair<const K, V>>>
struct MapTypes {
  using Entry = gtl::IntervalMapEntry<K, V, Comparison>;

  // stl::set
  using STL_IMap = gtl::IntervalMap<K, V, Comparison, Allocator>;
};

namespace test_int_int {

struct OpaqueInt {
  OpaqueInt() : value() {}
  // The implicit constructor and operator int allow OpaqueInt to be used
  // interchangeably with int in the cases we want (construction, assignment,
  // function calls, ...), but not the case that we don't want to allow
  // (comparison via operator< instead of the comparison type).
  OpaqueInt(int value)  // NOLINT
      : value(value) {}
  operator int() const { return value; }  // NOLINT

  // All comparisons need to be done through the comparator type, so these
  // overloads are explicitly deleted.
  bool operator<(const OpaqueInt&) const = delete;
  bool operator<(int) const = delete;

  int value;

  // Comparison object: compares two `OpaqueInt` objects.
  struct Compare {
    bool operator()(OpaqueInt l, OpaqueInt r) const {
      return l.value < r.value;
    }
  };

  // Stateful comparison object: compares two `OpaqueInt` objects, but
  // CHECK-fails if it has not been initialized or copied from an
  // initialized StatefulCompare.
  struct StatefulCompare {
    bool operator()(OpaqueInt l, OpaqueInt r) const {
      CHECK(initialized) << "StatefulCompare called that wasn't copied.";
      return l.value < r.value;
    }
    bool initialized = {};  // Set to true to indicate initialization done.
  };
};

// Helper function: returns a constructed comparison object.
template <class T>
T MakeComp() {
  return T();
}

// Helper function specialization: returns an initialized stateful
// comparison object.
template <>
OpaqueInt::StatefulCompare MakeComp<OpaqueInt::StatefulCompare>() {
  OpaqueInt::StatefulCompare comp;
  comp.initialized = true;
  return comp;
}

// IntervalMapTest tests IntervalMap<int, int, ...> with various
// container set types.

template <typename T>
class IntervalMapTest : public ::testing::Test {
 public:
  using IMap = T;

  // Helper function: constructs and returns an IntervalMap, including a
  // comparison object initialized by the `MakeComp` helper above, and an
  // allocator which is initialized with an UnsafeArena if this is an
  // ArenaAllocator.
  IMap ConstructMap() { return IMap(MakeComp<typename IMap::key_compare>()); }
};

using MyTypes = ::testing::Types<
    MapTypes<OpaqueInt, int, OpaqueInt::Compare>::STL_IMap,
    MapTypes<OpaqueInt, int, OpaqueInt::StatefulCompare>::STL_IMap,
    MapTypes<int, int>::STL_IMap>;

TYPED_TEST_SUITE(IntervalMapTest, MyTypes);

// Helpers

template <class IMap>
static void CheckMissingRange(const IMap& m, int start, int limit) {
  for (int i = start; i < limit; i++) {
    EXPECT_FALSE(m.contains(i));
    typename IMap::mapped_type v;
    typename IMap::key_type s, l;
    EXPECT_FALSE(m.Lookup(i, &v));
    EXPECT_FALSE(m.FindInterval(i, &s, &l, &v));
  }
  ASSERT_TRUE(m.IsEmptyInterval(start, limit));
  ASSERT_TRUE(m.IsEmptyInterval(start + 1, limit));
  ASSERT_TRUE(m.IsEmptyInterval(start + 1, limit - 1));
  ASSERT_TRUE(m.IsEmptyInterval(start, limit - 1));
}

template <class IMap>
static void CheckPresentRange(const IMap& m, int start, int limit, int val) {
  typename IMap::mapped_type v;
  typename IMap::key_type s, l;
  for (int i = start; i < limit; i++) {
    ASSERT_TRUE(m.contains(i));
    ASSERT_TRUE(m.Lookup(i, &v));
    ASSERT_EQ(v, val);
    ASSERT_TRUE(m.FindInterval(i, &s, &l, &v));
    ASSERT_EQ(s, start);
    ASSERT_EQ(l, limit);
    ASSERT_EQ(v, val);
    ASSERT_TRUE(m.FindNext(i, &s, &l, &v));
    ASSERT_EQ(s, start);
    ASSERT_EQ(l, limit);
    ASSERT_EQ(v, val);
  }

  // FindNext() from before range either yields an earlier range or this range
  ASSERT_TRUE(m.FindNext(start - 1, &s, &l, &v));
  if (s >= start) {
    ASSERT_EQ(s, start);
    ASSERT_EQ(l, limit);
    ASSERT_EQ(v, val);
  }

  // FindNext() from after range does not yield this range
  if (m.FindNext(limit, &s, &l, &v)) {
    ASSERT_GE(s, limit);
  }

  if (start < limit) {
    ASSERT_FALSE(m.IsEmptyInterval(start - 1, limit));
    ASSERT_FALSE(m.IsEmptyInterval(start - 1, limit + 1));
    ASSERT_FALSE(m.IsEmptyInterval(start, limit));
    ASSERT_FALSE(m.IsEmptyInterval(start, limit + 1));
    ASSERT_FALSE(m.IsEmptyInterval(start, start + 1));
    ASSERT_FALSE(m.IsEmptyInterval(limit - 1, limit));
    ASSERT_FALSE(m.IsEmptyInterval(limit - 1, limit + 1));

    if (start < limit - 1) {
      ASSERT_FALSE(m.IsEmptyInterval(start + 1, limit - 1));
    }
  }

  ASSERT_TRUE(m.IsEmptyInterval(start, start));
  ASSERT_TRUE(m.IsEmptyInterval(start, start - 1));
}

// Helper routine that returns either the result found by FindNextPoint or -1
template <class IMap>
static typename IMap::key_type NextPoint(const IMap& m, int key) {
  typename IMap::key_type r;
  if (m.FindNextPoint(key, &r)) {
    return r;
  } else {
    return -1;
  }
}

// Returns a string encoding of [iter->start,iter->limit]=>iter->value
template <class Iter>
static std::string Unparse(const Iter& iter) {
  return absl::StrFormat("[%d,%d]=>%d", static_cast<int>(iter->start),
                         static_cast<int>(iter->limit), iter->value);
}

template <typename K, typename V>
static auto EntryIs(K start, K limit, V value) {
  return testing::AllOf(
      testing::Field("start", &gtl::IntervalMap<K, V>::Entry::start, start),
      testing::Field("limit", &gtl::IntervalMap<K, V>::Entry::limit, limit),
      testing::Field("value", &gtl::IntervalMap<K, V>::Entry::value, value));
}

TYPED_TEST(IntervalMapTest, EmptyMap) {
  typename TestFixture::IMap m = TestFixture::ConstructMap();

  ASSERT_THAT(m, IsEmpty());
  ASSERT_TRUE(m.empty());
  CheckMissingRange(m, 0, 100);
}

TYPED_TEST(IntervalMapTest, SingleMap) {
  auto m = TestFixture::ConstructMap();
  m.Set(/*start=*/5, /*limit=*/10, /*value=*/100);

  ASSERT_THAT(m, SizeIs(1));
  ASSERT_FALSE(m.empty());
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 5, 10, 100);
  CheckMissingRange(m, 10, 100);
}

TYPED_TEST(IntervalMapTest, MultipleMap) {
  auto m = TestFixture::ConstructMap();

  m.Set(5, 10, 100);
  m.Set(15, 20, 200);
  m.Set(25, 30, 300);

  ASSERT_THAT(m, SizeIs(3));
  ASSERT_FALSE(m.empty());
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 5, 10, 100);
  CheckMissingRange(m, 10, 15);
  CheckPresentRange(m, 15, 20, 200);
  CheckMissingRange(m, 20, 25);
  CheckPresentRange(m, 25, 30, 300);
  CheckMissingRange(m, 30, 50);
}

TYPED_TEST(IntervalMapTest, SetResult) {
  auto m = TestFixture::ConstructMap();

  m.Set(0, 30, 100);
  auto it = m.Set(10, 20, 200);
  ASSERT_NE(it, m.end());
  EXPECT_EQ(it->start, 10);
  EXPECT_EQ(it->limit, 20);
  EXPECT_EQ(it->value, 200);
}

TYPED_TEST(IntervalMapTest, MutableValue) {
  auto m = TestFixture::ConstructMap();

  m.Set(0, 30, 100);
  auto it = m.begin();
  *m.MutableValue(it) = 150;
  EXPECT_EQ(it->value, 150);
}

TYPED_TEST(IntervalMapTest, SetNoOverlapResult) {
  auto m = TestFixture::ConstructMap();

  m.Set(0, 10, 100);
  auto it = m.SetNoOverlap(10, 20, 200);
  ASSERT_NE(it, m.end());
  EXPECT_EQ(it->start, 10);
  EXPECT_EQ(it->limit, 20);
  EXPECT_EQ(it->value, 200);
}

TYPED_TEST(IntervalMapTest, SetJustBefore) {
  auto m = TestFixture::ConstructMap();

  m.Set(10, 20, 200);
  m.Set(5, 10, 100);

  ASSERT_THAT(m, SizeIs(2));
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 5, 10, 100);
  CheckPresentRange(m, 10, 20, 200);
  CheckMissingRange(m, 20, 30);
}

TYPED_TEST(IntervalMapTest, SetJustAfter) {
  auto m = TestFixture::ConstructMap();

  m.Set(5, 10, 100);
  m.Set(10, 20, 200);

  ASSERT_THAT(m, SizeIs(2));
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 5, 10, 100);
  CheckPresentRange(m, 10, 20, 200);
  CheckMissingRange(m, 20, 30);
}

TYPED_TEST(IntervalMapTest, SetSame) {
  auto m = TestFixture::ConstructMap();

  m.Set(10, 20, 200);
  m.Set(10, 20, 100);

  ASSERT_THAT(m, SizeIs(1));
  CheckMissingRange(m, 0, 10);
  CheckPresentRange(m, 10, 20, 100);
  CheckMissingRange(m, 20, 30);
}

TYPED_TEST(IntervalMapTest, SetKillPrefix) {
  auto m = TestFixture::ConstructMap();

  m.Set(7, 20, 200);
  m.Set(5, 10, 100);

  ASSERT_THAT(m, SizeIs(2));
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 5, 10, 100);
  CheckPresentRange(m, 10, 20, 200);
  CheckMissingRange(m, 20, 30);
}

TYPED_TEST(IntervalMapTest, SetKillSuffix) {
  auto m = TestFixture::ConstructMap();

  m.Set(5, 15, 100);
  m.Set(10, 20, 200);

  ASSERT_THAT(m, SizeIs(2));
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 5, 10, 100);
  CheckPresentRange(m, 10, 20, 200);
  CheckMissingRange(m, 20, 30);
}

TYPED_TEST(IntervalMapTest, SetInterior) {
  auto m = TestFixture::ConstructMap();

  m.Set(10, 40, 100);
  m.Set(20, 30, 200);

  ASSERT_THAT(m, SizeIs(3));
  CheckMissingRange(m, 0, 9);
  CheckPresentRange(m, 10, 20, 100);
  CheckPresentRange(m, 20, 30, 200);
  CheckPresentRange(m, 30, 40, 100);
  CheckMissingRange(m, 40, 50);
}

TYPED_TEST(IntervalMapTest, SetCoverMultiple) {
  auto m = TestFixture::ConstructMap();

  m.Set(7, 12, 500);
  m.Set(13, 15, 600);
  m.Set(5, 15, 100);
  m.Set(10, 20, 200);

  ASSERT_THAT(m, SizeIs(2));
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 5, 10, 100);
  CheckPresentRange(m, 10, 20, 200);
  CheckMissingRange(m, 20, 30);
}

TYPED_TEST(IntervalMapTest, SplitAtEmpty) {
  auto m = TestFixture::ConstructMap();
  m.SplitAt(5);

  ASSERT_THAT(m, IsEmpty());
}

TYPED_TEST(IntervalMapTest, SplitAtMiddle) {
  auto m = TestFixture::ConstructMap();
  m.Set(10, 20, 100);
  m.SplitAt(15);

  ASSERT_THAT(m, SizeIs(2));
  CheckMissingRange(m, 0, 10);
  CheckPresentRange(m, 10, 15, 100);
  CheckPresentRange(m, 15, 20, 100);
  CheckMissingRange(m, 20, 30);
}

TYPED_TEST(IntervalMapTest, SplitAtBefore) {
  auto m = TestFixture::ConstructMap();
  m.Set(10, 20, 100);
  m.SplitAt(5);

  ASSERT_THAT(m, SizeIs(1));
  CheckMissingRange(m, 0, 10);
  CheckPresentRange(m, 10, 20, 100);
  CheckMissingRange(m, 20, 30);
}

TYPED_TEST(IntervalMapTest, SplitAtBeginning) {
  auto m = TestFixture::ConstructMap();
  m.Set(10, 20, 100);
  m.SplitAt(10);

  ASSERT_THAT(m, SizeIs(1));
  CheckMissingRange(m, 0, 10);
  CheckPresentRange(m, 10, 20, 100);
  CheckMissingRange(m, 20, 30);
}

TYPED_TEST(IntervalMapTest, SplitAtEnd) {
  auto m = TestFixture::ConstructMap();
  m.Set(10, 20, 100);
  m.SplitAt(20);

  ASSERT_THAT(m, SizeIs(1));
  CheckMissingRange(m, 0, 10);
  CheckPresentRange(m, 10, 20, 100);
  CheckMissingRange(m, 20, 30);
}
TYPED_TEST(IntervalMapTest, SplitAtAfterEnd) {
  auto m = TestFixture::ConstructMap();
  m.Set(10, 20, 100);
  m.SplitAt(25);

  ASSERT_THAT(m, SizeIs(1));
  CheckMissingRange(m, 0, 10);
  CheckPresentRange(m, 10, 20, 100);
  CheckMissingRange(m, 20, 30);
}

TYPED_TEST(IntervalMapTest, Coalesce) {
  auto m = TestFixture::ConstructMap();
  // First, coalesce an empty map and make sure it works.
  m.Coalesce();
  ASSERT_TRUE(m.empty());

  m.Set(1, 5, 1);
  m.Set(4, 7, 1);    // Slight overlap on left
  m.Set(7, 8, 1);    // Perfect abutment
  m.Set(8, 10, 2);   // Abutment, but different values
  m.Set(11, 12, 2);  // Same value, but no abutment.
  m.Set(12, 14, 2);  // Abutment
  m.Coalesce();

  CheckMissingRange(m, 0, 1);
  CheckPresentRange(m, 1, 8, 1);
  CheckPresentRange(m, 8, 10, 2);
  CheckMissingRange(m, 10, 11);
  CheckPresentRange(m, 11, 14, 2);
}

template <typename T, typename U>
const U* max_func(void* arg, const T& /* range_start */,
                  const T& /* range_limit */, U* old_value, U* scratch) {
  const U* v = reinterpret_cast<U*>(arg);
  if (old_value == nullptr) {
    return v;
  } else {
    *scratch = std::max(*v, *old_value);
    return scratch;
  }
}

template <class IMap>
void TestMergeValue(IMap* m, const typename IMap::key_type& start,
                    const typename IMap::key_type& limit,
                    typename IMap::mapped_type new_value) {
  m->MergeValue(start, limit, &max_func<typename IMap::key_type>, &new_value);
}

TYPED_TEST(IntervalMapTest, MergeValueEmpty) {
  auto m = TestFixture::ConstructMap();
  TestMergeValue(&m, 5, 10, 102);
  ASSERT_THAT(m, SizeIs(1));
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 5, 10, 102);
  CheckMissingRange(m, 10, 100);
}

TYPED_TEST(IntervalMapTest, MergeValuePartialOverlapAtBeginning) {
  auto m = TestFixture::ConstructMap();
  m.Set(5, 10, 100);
  TestMergeValue(&m, 5, 7, 102);
  ASSERT_THAT(m, SizeIs(2));
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 5, 7, 102);
  CheckPresentRange(m, 7, 10, 100);
  CheckMissingRange(m, 10, 100);
}

TYPED_TEST(IntervalMapTest, MergeValuePartialOverlapAtBeginning2) {
  auto m = TestFixture::ConstructMap();
  m.Set(7, 10, 100);
  TestMergeValue(&m, 5, 10, 102);
  ASSERT_THAT(m, SizeIs(2));
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 5, 7, 102);
  CheckPresentRange(m, 7, 10, 102);
  CheckMissingRange(m, 10, 100);
}

TYPED_TEST(IntervalMapTest, MergeValueExactOverlap) {
  auto m = TestFixture::ConstructMap();
  m.Set(5, 10, 100);
  TestMergeValue(&m, 5, 10, 102);
  ASSERT_THAT(m, SizeIs(1));
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 5, 10, 102);
  CheckMissingRange(m, 10, 100);
}

TYPED_TEST(IntervalMapTest, MergeValuePartialOverlapAtEnd) {
  auto m = TestFixture::ConstructMap();
  m.Set(5, 10, 100);
  TestMergeValue(&m, 7, 10, 102);
  ASSERT_THAT(m, SizeIs(2));
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 5, 7, 100);
  CheckPresentRange(m, 7, 10, 102);
  CheckMissingRange(m, 10, 100);
}

TYPED_TEST(IntervalMapTest, MergeValueCompletelyCovers) {
  auto m = TestFixture::ConstructMap();
  m.Set(5, 10, 100);
  TestMergeValue(&m, 0, 20, 102);
  ASSERT_THAT(m, SizeIs(3));
  CheckPresentRange(m, 0, 5, 102);
  CheckPresentRange(m, 5, 10, 102);
  CheckPresentRange(m, 10, 20, 102);
  CheckMissingRange(m, 20, 100);
}

TYPED_TEST(IntervalMapTest, MergeValueInsideRange) {
  auto m = TestFixture::ConstructMap();
  m.Set(0, 20, 100);
  TestMergeValue(&m, 5, 10, 102);
  ASSERT_THAT(m, SizeIs(3));
  CheckPresentRange(m, 0, 5, 100);
  CheckPresentRange(m, 5, 10, 102);
  CheckPresentRange(m, 10, 20, 100);
  CheckMissingRange(m, 20, 100);
}

static void Add(const int value, int* dst) { *dst += value; }

TYPED_TEST(IntervalMapTest, TypeSafeMerge) {
  auto m = TestFixture::ConstructMap();
  m.Set(5, 10, 100);
  m.Set(15, 20, 200);
  m.Set(25, 30, 300);
  // Test with both functor (lambda) and function (old-style) args.
  m.MergeValue(3, 27, 17, &Add);
  m.MergeValue(29, 45, 30, [](const int value, int* dst) { *dst += value; });
  ASSERT_THAT(m, SizeIs(9));
  CheckMissingRange(m, 0, 3);
  CheckPresentRange(m, 3, 5, 17);
  CheckPresentRange(m, 5, 10, 117);
  CheckPresentRange(m, 10, 15, 17);
  CheckPresentRange(m, 15, 20, 217);
  CheckPresentRange(m, 20, 25, 17);
  CheckPresentRange(m, 25, 27, 317);
  CheckPresentRange(m, 27, 29, 300);
  CheckPresentRange(m, 29, 30, 330);
  CheckPresentRange(m, 30, 45, 30);
  CheckMissingRange(m, 45, 50);
}

// Check that the next entry recorded in r matches start/limit/value
static void CheckEntry(absl::Span<const int> r, int i, int start, int limit,
                       int expected_old_value) {
  ASSERT_GE(r.size(), i + 3);
  ASSERT_EQ(r[i], start);
  ASSERT_EQ(r[i + 1], limit);
  ASSERT_EQ(r[i + 2], expected_old_value);
}

TYPED_TEST(IntervalMapTest, RangePassing) {
  auto m = TestFixture::ConstructMap();

  // Record passed ranges and values in "r"
  std::vector<int> r;
  m.Set(20, 30, 100);
  m.Set(40, 50, 200);
  m.MergeValue(10, 60,
               [&r](const int start, const int limit, int* old_value,
                    int* scratch) -> const int* {
                 r.push_back(start);
                 r.push_back(limit);
                 r.push_back(old_value ? *old_value : -1);
                 return old_value;
               });

  // Check that expected ranges and values were recorded
  CheckEntry(r, 0, 10, 20, -1);
  CheckEntry(r, 3, 20, 30, 100);
  CheckEntry(r, 6, 30, 40, -1);
  CheckEntry(r, 9, 40, 50, 200);
  CheckEntry(r, 12, 50, 60, -1);
  ASSERT_THAT(r, SizeIs(15));
}

// If a value did exist for a range, we eliminate it.  Otherwise, we
// insert the new value
template <typename T, typename U>
static const U* MergeThatClears(void* arg, const T& /* start */,
                                const T& /* limit */, U* old_value,
                                U* /* scratch */) {
  const U* v = reinterpret_cast<U*>(arg);
  if (old_value == nullptr) {
    return v;
  } else {
    return nullptr;
  }
}

template <class IMap>
void TestMergeThatClears(IMap* m, const typename IMap::key_type& start,
                         const typename IMap::key_type& limit,
                         typename IMap::mapped_type new_value) {
  m->MergeValue(
      start, limit,
      MergeThatClears<typename IMap::key_type, typename IMap::mapped_type>,
      &new_value);
}

TYPED_TEST(IntervalMapTest, MergeThatClears) {
  auto m = TestFixture::ConstructMap();
  m.Set(5, 7, 1);
  m.Set(10, 15, 2);
  TestMergeThatClears(&m, 3, 20, 102);
  ASSERT_THAT(m, SizeIs(3));
  CheckMissingRange(m, 0, 3);
  CheckPresentRange(m, 3, 5, 102);
  CheckMissingRange(m, 5, 7);
  CheckPresentRange(m, 7, 10, 102);
  CheckMissingRange(m, 10, 15);
  CheckPresentRange(m, 15, 20, 102);
  CheckMissingRange(m, 20, 100);
}

TYPED_TEST(IntervalMapTest, MergeValueMultipleRanges) {
  auto m = TestFixture::ConstructMap();
  m.Set(5, 6, 100);
  m.Set(6, 8, 100);
  m.Set(8, 10, 100);
  TestMergeValue(&m, 7, 10, 102);
  ASSERT_THAT(m, SizeIs(4));
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 5, 6, 100);
  CheckPresentRange(m, 6, 7, 100);
  CheckPresentRange(m, 7, 8, 102);
  CheckPresentRange(m, 8, 10, 102);
  CheckMissingRange(m, 10, 100);
}

TYPED_TEST(IntervalMapTest, MergeValuePartialOverlapBeginning) {
  auto m = TestFixture::ConstructMap();
  m.Set(5, 10, 100);
  TestMergeValue(&m, 4, 7, 102);
  ASSERT_THAT(m, SizeIs(3));
  CheckMissingRange(m, 0, 4);
  CheckPresentRange(m, 4, 5, 102);
  CheckPresentRange(m, 5, 7, 102);
  CheckPresentRange(m, 7, 10, 100);
  CheckMissingRange(m, 10, 100);
}

TYPED_TEST(IntervalMapTest, MergeValuePartial3) {
  auto m = TestFixture::ConstructMap();
  m.Set(5, 10, 100);
  TestMergeValue(&m, 4, 11, 102);
  ASSERT_THAT(m, SizeIs(3));
  CheckMissingRange(m, 0, 4);
  CheckPresentRange(m, 4, 5, 102);
  CheckPresentRange(m, 5, 10, 102);
  CheckPresentRange(m, 10, 11, 102);
  CheckMissingRange(m, 11, 100);
}

TYPED_TEST(IntervalMapTest, MergeValueFullInterval) {
  auto m = TestFixture::ConstructMap();
  m.Set(5, 10, 100);
  TestMergeValue(&m, 5, 10, 102);
  ASSERT_THAT(m, SizeIs(1));
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 5, 10, 102);
  CheckMissingRange(m, 11, 100);
}

TYPED_TEST(IntervalMapTest, MergeValuePartial4) {
  auto m = TestFixture::ConstructMap();
  m.Set(5, 10, 100);
  TestMergeValue(&m, 5, 10, 102);
  ASSERT_THAT(m, SizeIs(1));
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 5, 10, 102);
  CheckMissingRange(m, 11, 100);
}

TYPED_TEST(IntervalMapTest, EraseResult) {
  auto m = TestFixture::ConstructMap();

  m.Set(0, 10, 100);
  m.Set(10, 20, 200);
  m.Set(20, 30, 300);

  auto it = m.Erase(10, 20);
  EXPECT_EQ(it->start, 20);
  EXPECT_EQ(it->limit, 30);
  EXPECT_EQ(it->value, 300);

  it = m.Erase(5, 25);
  EXPECT_EQ(it->start, 25);
  EXPECT_EQ(it->limit, 30);
  EXPECT_EQ(it->value, 300);
}

TYPED_TEST(IntervalMapTest, EraseJustBefore) {
  auto m = TestFixture::ConstructMap();

  m.Set(5, 10, 100);
  m.Set(10, 20, 200);
  m.Erase(8, 10);

  ASSERT_THAT(m, SizeIs(2));
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 5, 8, 100);
  CheckMissingRange(m, 8, 10);
  CheckPresentRange(m, 10, 20, 200);
  CheckMissingRange(m, 20, 30);
}

TYPED_TEST(IntervalMapTest, EraseJustAfter) {
  auto m = TestFixture::ConstructMap();

  m.Set(5, 10, 100);
  m.Set(10, 20, 200);
  m.Erase(10, 12);

  ASSERT_THAT(m, SizeIs(2));
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 5, 10, 100);
  CheckMissingRange(m, 10, 12);
  CheckPresentRange(m, 12, 20, 200);
  CheckMissingRange(m, 20, 30);
}

TYPED_TEST(IntervalMapTest, EraseSame) {
  auto m = TestFixture::ConstructMap();

  m.Set(5, 10, 100);
  m.Set(10, 20, 200);
  m.Erase(5, 10);

  ASSERT_THAT(m, SizeIs(1));
  CheckMissingRange(m, 0, 10);
  CheckPresentRange(m, 10, 20, 200);
  CheckMissingRange(m, 20, 30);
}

TYPED_TEST(IntervalMapTest, ErasePrefix) {
  auto m = TestFixture::ConstructMap();

  m.Set(5, 10, 100);
  m.Set(10, 20, 200);
  m.Erase(5, 12);

  ASSERT_THAT(m, SizeIs(1));
  CheckMissingRange(m, 0, 12);
  CheckPresentRange(m, 12, 20, 200);
  CheckMissingRange(m, 20, 30);
}

TYPED_TEST(IntervalMapTest, EraseSuffix) {
  auto m = TestFixture::ConstructMap();

  m.Set(5, 10, 100);
  m.Set(10, 20, 200);
  m.Erase(7, 12);

  ASSERT_THAT(m, SizeIs(2));
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 5, 7, 100);
  CheckMissingRange(m, 7, 12);
  CheckPresentRange(m, 12, 20, 200);
  CheckMissingRange(m, 20, 30);
}

TYPED_TEST(IntervalMapTest, EraseSuffix2) {
  auto m = TestFixture::ConstructMap();

  m.Set(5, 10, 100);
  m.Erase(7, 10);

  ASSERT_THAT(m, SizeIs(1));
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 5, 7, 100);
  CheckMissingRange(m, 7, 30);
}

TYPED_TEST(IntervalMapTest, EraseInterior) {
  auto m = TestFixture::ConstructMap();

  m.Set(10, 40, 100);
  m.Erase(20, 30);

  ASSERT_THAT(m, SizeIs(2));
  CheckMissingRange(m, 0, 9);
  CheckPresentRange(m, 10, 20, 100);
  CheckMissingRange(m, 20, 30);
  CheckPresentRange(m, 30, 40, 100);
  CheckMissingRange(m, 40, 50);
}

TYPED_TEST(IntervalMapTest, EraseMultiple) {
  auto m = TestFixture::ConstructMap();

  m.Set(5, 10, 100);
  m.Set(15, 20, 200);
  m.Set(25, 30, 300);
  m.Set(35, 40, 400);
  m.Erase(7, 37);

  ASSERT_THAT(m, SizeIs(2));
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 5, 7, 100);
  CheckMissingRange(m, 7, 37);
  CheckPresentRange(m, 37, 40, 400);
  CheckMissingRange(m, 40, 50);
}

TYPED_TEST(IntervalMapTest, EraseMissing) {
  auto m = TestFixture::ConstructMap();

  m.Set(5, 10, 100);
  m.Set(15, 20, 200);
  m.Erase(12, 14);

  ASSERT_THAT(m, SizeIs(2));
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 5, 10, 100);
  CheckMissingRange(m, 10, 15);
  CheckPresentRange(m, 15, 20, 200);
  CheckMissingRange(m, 20, 50);
}

TYPED_TEST(IntervalMapTest, EraseFromFront) {
  auto m = TestFixture::ConstructMap();

  m.Set(5, 10, 100);
  m.Set(15, 20, 200);
  m.Erase(12, 18);

  ASSERT_THAT(m, SizeIs(2));
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 5, 10, 100);
  CheckMissingRange(m, 10, 18);
  CheckPresentRange(m, 18, 20, 200);
  CheckMissingRange(m, 20, 50);
}

TYPED_TEST(IntervalMapTest, EraseFromBehind) {
  auto m = TestFixture::ConstructMap();

  m.Set(5, 10, 100);
  m.Set(15, 20, 200);
  m.Erase(7, 12);

  ASSERT_THAT(m, SizeIs(2));
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 5, 7, 100);
  CheckMissingRange(m, 7, 15);
  CheckPresentRange(m, 15, 20, 200);
  CheckMissingRange(m, 20, 50);
}

TYPED_TEST(IntervalMapTest, ClearEmpty) {
  auto m = TestFixture::ConstructMap();
  m.Clear();
  ASSERT_THAT(m, IsEmpty());
  CheckMissingRange(m, 0, 100);
}

TYPED_TEST(IntervalMapTest, ClearNonEmpty) {
  auto m = TestFixture::ConstructMap();
  m.Set(5, 10, 100);
  m.Set(15, 20, 200);
  m.Erase(7, 12);
  m.Clear();
  ASSERT_THAT(m, IsEmpty());
  CheckMissingRange(m, 0, 100);
}

TYPED_TEST(IntervalMapTest, CoversRange) {
  auto map = TestFixture::ConstructMap();
  EXPECT_FALSE(map.CoversRange(0, 100));
  EXPECT_FALSE(map.CoversRange(10, 11));

  map.Set(5, 10, 100);
  EXPECT_TRUE(map.CoversRange(5, 10));
  EXPECT_TRUE(map.CoversRange(5, 7));
  EXPECT_TRUE(map.CoversRange(7, 10));
  EXPECT_TRUE(map.CoversRange(6, 9));
  EXPECT_FALSE(map.CoversRange(4, 10));
  EXPECT_FALSE(map.CoversRange(6, 11));
  EXPECT_FALSE(map.CoversRange(0, 20));
  EXPECT_FALSE(map.CoversRange(11, 20));

  // Another range that is not contiguous with the first.
  map.Set(15, 20, 200);
  EXPECT_TRUE(map.CoversRange(15, 20));
  EXPECT_FALSE(map.CoversRange(5, 20));
  EXPECT_FALSE(map.CoversRange(0, 30));
  EXPECT_FALSE(map.CoversRange(7, 12));
  EXPECT_FALSE(map.CoversRange(12, 20));

  // Add a range that makes all ranges contiguous.
  map.Set(10, 15, 300);
  EXPECT_TRUE(map.CoversRange(5, 20));
  EXPECT_TRUE(map.CoversRange(11, 19));
  EXPECT_TRUE(map.CoversRange(7, 19));
  EXPECT_FALSE(map.CoversRange(3, 20));
  EXPECT_FALSE(map.CoversRange(5, 21));
  EXPECT_FALSE(map.CoversRange(3, 21));
}

TYPED_TEST(IntervalMapTest, ConstDefaultInitialisation) {
  const auto empty = TestFixture::ConstructMap();
  EXPECT_THAT(empty, IsEmpty());
}

TYPED_TEST(IntervalMapTest, Copy) {
  auto src = TestFixture::ConstructMap();
  src.Set(5, 10, 100);
  const typename TestFixture::IMap m(src);
  EXPECT_THAT(m, SizeIs(src.size()));
}

TYPED_TEST(IntervalMapTest, Assign1) {
  auto src = TestFixture::ConstructMap();
  auto m = TestFixture::ConstructMap();
  m.Set(5, 10, 100);
  m = src;
  EXPECT_THAT(m, IsEmpty());
}

TYPED_TEST(IntervalMapTest, Assign2) {
  auto src = TestFixture::ConstructMap();
  auto m = TestFixture::ConstructMap();
  m.Set(5, 10, 100);
  src.Set(10, 20, 200);
  m = src;
  EXPECT_THAT(m, SizeIs(1));
  CheckPresentRange(m, 10, 20, 200);
}

TYPED_TEST(IntervalMapTest, SelfAssign) {
  auto m = TestFixture::ConstructMap();
  m.Set(5, 10, 100);
  const auto size = m.size();
  m = *&m;  // Avoid -Wself-assign.
  EXPECT_THAT(m, SizeIs(size));
}

TYPED_TEST(IntervalMapTest, MergeFrom_Empty) {
  auto src = TestFixture::ConstructMap();
  auto m = TestFixture::ConstructMap();
  m.MergeFrom(src);
  EXPECT_THAT(m, IsEmpty());
}

TYPED_TEST(IntervalMapTest, MergeFrom_EmptySrc) {
  auto src = TestFixture::ConstructMap();
  auto m = TestFixture::ConstructMap();
  m.Set(5, 10, 100);
  m.MergeFrom(src);

  EXPECT_THAT(m, SizeIs(1));
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 5, 10, 100);
  CheckMissingRange(m, 10, 100);
}

TYPED_TEST(IntervalMapTest, MergeFrom_EmptyDst) {
  auto src = TestFixture::ConstructMap();
  auto m = TestFixture::ConstructMap();
  src.Set(5, 10, 100);
  m.MergeFrom(src);

  EXPECT_THAT(m, SizeIs(1));
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 5, 10, 100);
  CheckMissingRange(m, 10, 100);
}

TYPED_TEST(IntervalMapTest, MergeFrom_General) {
  auto src = TestFixture::ConstructMap();
  auto m = TestFixture::ConstructMap();

  m.Set(10, 15, 100);  // Range not in src
  m.Set(20, 25, 200);  // Range exactly in src
  m.Set(30, 35, 300);  // Range whose prefix is in src
  m.Set(40, 45, 400);  // Range whose suffix is in src
  m.Set(50, 55, 500);  // Range that has two subranges in src
  m.Set(60, 65, 600);  // Range that is contained by a subrange in src

  src.Set(18, 20, 150);  // Range not in m
  src.Set(20, 25, 250);
  src.Set(30, 33, 350);
  src.Set(42, 47, 450);
  src.Set(51, 52, 550);
  src.Set(53, 54, 560);
  src.Set(59, 68, 650);

  m.MergeFrom(src);

  ASSERT_THAT(m, SizeIs(13));
  CheckMissingRange(m, 0, 10);
  CheckPresentRange(m, 10, 15, 100);
  CheckMissingRange(m, 15, 18);
  CheckPresentRange(m, 18, 20, 150);
  CheckPresentRange(m, 20, 25, 250);
  CheckMissingRange(m, 25, 30);
  CheckPresentRange(m, 30, 33, 350);
  CheckPresentRange(m, 33, 35, 300);
  CheckMissingRange(m, 35, 40);
  CheckPresentRange(m, 40, 42, 400);
  CheckPresentRange(m, 42, 47, 450);
  CheckMissingRange(m, 47, 50);
  CheckPresentRange(m, 50, 51, 500);
  CheckPresentRange(m, 51, 52, 550);
  CheckPresentRange(m, 52, 53, 500);
  CheckPresentRange(m, 53, 54, 560);
  CheckPresentRange(m, 54, 55, 500);
  CheckMissingRange(m, 55, 59);
  CheckPresentRange(m, 59, 68, 650);
  CheckMissingRange(m, 68, 100);
}

TYPED_TEST(IntervalMapTest, MergeFrom_SetManyNoOverlap) {
  auto src = TestFixture::ConstructMap();
  auto m = TestFixture::ConstructMap();
  typename TestFixture::IMap::Entry entries[5];
  for (int i = 0; i < 5; i++) {
    entries[i].start = i * 10;
    entries[i].limit = i * 10 + 8;
    entries[i].value = i * 1000;
  }
  m.SetManyNoOverlap(5, entries);

  ASSERT_THAT(m, SizeIs(5));
  CheckPresentRange(m, 0, 8, 0);
  CheckMissingRange(m, 8, 10);
  CheckPresentRange(m, 10, 18, 1000);
  CheckMissingRange(m, 18, 20);
  CheckPresentRange(m, 20, 28, 2000);
  CheckMissingRange(m, 28, 30);
  CheckPresentRange(m, 30, 38, 3000);
  CheckMissingRange(m, 38, 40);
  CheckPresentRange(m, 40, 48, 4000);
  CheckMissingRange(m, 48, 50);
}

TYPED_TEST(IntervalMapTest, MergeSubRangeFrom_EmptySrc) {
  auto src = TestFixture::ConstructMap();
  auto m = TestFixture::ConstructMap();

  m.Set(5, 10, 100);
  m.Set(10, 20, 200);
  m.MergeSubRangeFrom(src, 7, 18);

  ASSERT_THAT(m, SizeIs(2));
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 5, 10, 100);
  CheckPresentRange(m, 10, 20, 200);
  CheckMissingRange(m, 20, 30);
}

TYPED_TEST(IntervalMapTest, MergeSubRangeFrom_ManyRanges) {
  auto src = TestFixture::ConstructMap();
  auto m = TestFixture::ConstructMap();

  src.Set(5, 10, 100);
  for (int i = 10; i < 100; i++) {
    src.Set(i, i + 1, i);
  }
  m.MergeSubRangeFrom(src, 6, 95);

  ASSERT_THAT(m, SizeIs(86));
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 6, 10, 100);
  for (int i = 10; i < 95; i++) {
    CheckPresentRange(m, i, i + 1, i);
  }
  CheckMissingRange(m, 95, 100);
}

TYPED_TEST(IntervalMapTest, MergeSubRangeFrom_LeaveNonOverlappingRangesAlone) {
  auto src = TestFixture::ConstructMap();
  auto m = TestFixture::ConstructMap();

  m.Set(5, 8, 100);
  m.Set(10, 20, 200);

  src.Set(9, 15, 150);
  m.MergeSubRangeFrom(src, 7, 18);

  ASSERT_THAT(m, SizeIs(3));
  CheckMissingRange(m, 0, 5);
  CheckPresentRange(m, 5, 8, 100);
  CheckMissingRange(m, 8, 9);
  CheckPresentRange(m, 9, 15, 150);
  CheckPresentRange(m, 15, 20, 200);
  CheckMissingRange(m, 20, 30);
}

TYPED_TEST(IntervalMapTest, MergeSubRangeFrom_NothingAfterStart) {
  auto src = TestFixture::ConstructMap();
  auto m = TestFixture::ConstructMap();
  src.Set(10, 20, 100);
  m.MergeSubRangeFrom(src, 30, 40);

  ASSERT_THAT(m, IsEmpty());
}

TYPED_TEST(IntervalMapTest, MergeSubRangeFrom_DropSrcPrefix) {
  auto src = TestFixture::ConstructMap();
  auto m = TestFixture::ConstructMap();
  src.Set(10, 20, 100);
  m.MergeSubRangeFrom(src, 15, 25);

  ASSERT_THAT(m, SizeIs(1));
  CheckMissingRange(m, 0, 15);
  CheckPresentRange(m, 15, 20, 100);
  CheckMissingRange(m, 20, 100);
}

TYPED_TEST(IntervalMapTest, MergeSubRangeFrom_DropSrcSuffix) {
  auto src = TestFixture::ConstructMap();
  auto m = TestFixture::ConstructMap();
  src.Set(10, 20, 100);
  m.MergeSubRangeFrom(src, 10, 15);

  ASSERT_THAT(m, SizeIs(1));
  CheckMissingRange(m, 0, 10);
  CheckPresentRange(m, 10, 15, 100);
  CheckMissingRange(m, 15, 100);
}

TYPED_TEST(IntervalMapTest, MergeSubRangeFrom_ExactMatch) {
  auto src = TestFixture::ConstructMap();
  auto m = TestFixture::ConstructMap();
  src.Set(10, 20, 100);
  m.MergeSubRangeFrom(src, 10, 20);

  ASSERT_THAT(m, SizeIs(1));
  CheckMissingRange(m, 0, 10);
  CheckPresentRange(m, 10, 20, 100);
  CheckMissingRange(m, 20, 100);
}

TYPED_TEST(IntervalMapTest, MergeSubRangeFrom_Contained) {
  auto src = TestFixture::ConstructMap();
  auto m = TestFixture::ConstructMap();
  src.Set(10, 20, 100);
  m.MergeSubRangeFrom(src, 5, 25);

  ASSERT_THAT(m, SizeIs(1));
  CheckMissingRange(m, 0, 10);
  CheckPresentRange(m, 10, 20, 100);
  CheckMissingRange(m, 20, 100);
}

TYPED_TEST(IntervalMapTest, MergeSubRangeFrom_Multiple) {
  auto src = TestFixture::ConstructMap();
  auto m = TestFixture::ConstructMap();
  src.Set(10, 15, 100);
  src.Set(20, 25, 200);
  src.Set(30, 35, 300);
  src.Set(40, 45, 400);
  m.MergeSubRangeFrom(src, 5, 42);

  ASSERT_THAT(m, SizeIs(4));
  CheckMissingRange(m, 0, 10);
  CheckPresentRange(m, 10, 15, 100);
  CheckMissingRange(m, 15, 20);
  CheckPresentRange(m, 20, 25, 200);
  CheckMissingRange(m, 25, 30);
  CheckPresentRange(m, 30, 35, 300);
  CheckMissingRange(m, 35, 40);
  CheckPresentRange(m, 40, 42, 400);
  CheckMissingRange(m, 42, 100);
}

TYPED_TEST(IntervalMapTest, MergeSubRangeFrom_SimpleSubRange) {
  auto src = TestFixture::ConstructMap();
  auto m = TestFixture::ConstructMap();
  src.Set(5, 20, 100);
  m.MergeSubRangeFrom(src, 10, 15);

  ASSERT_THAT(m, SizeIs(1));
  CheckMissingRange(m, 0, 10);
  CheckPresentRange(m, 10, 15, 100);
  CheckMissingRange(m, 15, 100);
}

TYPED_TEST(IntervalMapTest, MergeSubRangeFrom_NonCopiedRangeAfterCopied) {
  auto src = TestFixture::ConstructMap();
  auto m = TestFixture::ConstructMap();
  src.Set(5, 20, 100);
  src.Set(50, 100, 200);
  m.Set(10, 15, 2);
  m.MergeSubRangeFrom(src, 10, 15);

  ASSERT_THAT(m, SizeIs(1));
  CheckMissingRange(m, 0, 10);
  CheckPresentRange(m, 10, 15, 100);
  CheckMissingRange(m, 15, 100);
}

TYPED_TEST(IntervalMapTest, MergeSubRangeFrom_SkipStuffBeforeStart) {
  auto src = TestFixture::ConstructMap();
  auto m = TestFixture::ConstructMap();
  src.Set(0, 5, 50);
  src.Set(10, 20, 100);
  m.MergeSubRangeFrom(src, 10, 20);

  ASSERT_THAT(m, SizeIs(1));
  CheckMissingRange(m, 0, 10);
  CheckPresentRange(m, 10, 20, 100);
  CheckMissingRange(m, 20, 100);
}

TYPED_TEST(IntervalMapTest, MergeSubRangeFrom_SkipStuffAfterLimit) {
  auto src = TestFixture::ConstructMap();
  auto m = TestFixture::ConstructMap();
  src.Set(10, 20, 100);
  src.Set(20, 30, 200);
  m.MergeSubRangeFrom(src, 10, 20);

  ASSERT_THAT(m, SizeIs(1));
  CheckMissingRange(m, 0, 10);
  CheckPresentRange(m, 10, 20, 100);
  CheckMissingRange(m, 20, 100);
}

TYPED_TEST(IntervalMapTest, FindNextPoint_Empty) {
  auto m = TestFixture::ConstructMap();
  EXPECT_EQ(NextPoint(m, 10), -1);
}

TYPED_TEST(IntervalMapTest, FindNextPoint_NoResult) {
  auto m = TestFixture::ConstructMap();
  m.Set(10, 20, 100);
  EXPECT_EQ(NextPoint(m, 20), -1);
  EXPECT_EQ(NextPoint(m, 30), -1);
}

TYPED_TEST(IntervalMapTest, FindNextPoint_KeyInInterval) {
  auto m = TestFixture::ConstructMap();
  m.Set(10, 20, 100);
  EXPECT_EQ(NextPoint(m, 10), 10);
  EXPECT_EQ(NextPoint(m, 15), 15);
  EXPECT_EQ(NextPoint(m, 20), -1);
}

TYPED_TEST(IntervalMapTest, FindNextPoint_KeyNotInInterval) {
  auto m = TestFixture::ConstructMap();
  m.Set(10, 20, 100);
  EXPECT_EQ(NextPoint(m, 9), 10);
  EXPECT_EQ(NextPoint(m, 20), -1);
  EXPECT_EQ(NextPoint(m, 25), -1);
}

TYPED_TEST(IntervalMapTest, SwapRefEmptyEmpty) {
  auto a = TestFixture::ConstructMap();
  auto b = TestFixture::ConstructMap();
  a.swap(b);
  EXPECT_THAT(a, IsEmpty());
  EXPECT_THAT(b, IsEmpty());
}

TYPED_TEST(IntervalMapTest, SwapRefNonEmptyNonEmpty) {
  auto a = TestFixture::ConstructMap();
  auto b = TestFixture::ConstructMap();
  a.Set(10, 20, 100);
  b.Set(20, 30, 200);
  a.swap(b);
  CheckMissingRange(a, 0, 20);
  CheckPresentRange(a, 20, 30, 200);
  CheckMissingRange(a, 30, 50);
  CheckMissingRange(b, 0, 10);
  CheckPresentRange(b, 10, 20, 100);
  CheckMissingRange(b, 20, 50);
}

TYPED_TEST(IntervalMapTest, SwapRefEmptyNonEmpty) {
  auto a = TestFixture::ConstructMap();
  auto b = TestFixture::ConstructMap();
  b.Set(20, 30, 200);
  a.swap(b);
  CheckMissingRange(a, 0, 20);
  CheckPresentRange(a, 20, 30, 200);
  CheckMissingRange(a, 30, 50);
  EXPECT_THAT(b, IsEmpty());
}

TYPED_TEST(IntervalMapTest, NonmemberSwapEmptyEmpty) {
  auto a = TestFixture::ConstructMap();
  auto b = TestFixture::ConstructMap();
  std::swap(a, b);
  EXPECT_THAT(a, IsEmpty());
  EXPECT_THAT(b, IsEmpty());
}

TYPED_TEST(IntervalMapTest, NonmemberSwapNonEmptyNonEmpty) {
  auto a = TestFixture::ConstructMap();
  auto b = TestFixture::ConstructMap();
  a.Set(10, 20, 100);
  b.Set(20, 30, 200);
  std::swap(a, b);
  CheckMissingRange(a, 0, 20);
  CheckPresentRange(a, 20, 30, 200);
  CheckMissingRange(a, 30, 50);
  CheckMissingRange(b, 0, 10);
  CheckPresentRange(b, 10, 20, 100);
  CheckMissingRange(b, 20, 50);
}

TYPED_TEST(IntervalMapTest, NonmemberSwapEmptyNonEmpty) {
  using std::swap;
  auto a = TestFixture::ConstructMap();
  auto b = TestFixture::ConstructMap();
  b.Set(20, 30, 200);
  swap(a, b);
  CheckMissingRange(a, 0, 20);
  CheckPresentRange(a, 20, 30, 200);
  CheckMissingRange(a, 30, 50);
  EXPECT_THAT(b, IsEmpty());
}

TYPED_TEST(IntervalMapTest, Iterator_Empty) {
  auto m = TestFixture::ConstructMap();
  EXPECT_EQ(m.begin(), m.end());
}

TYPED_TEST(IntervalMapTest, Iterator_Single) {
  auto m = TestFixture::ConstructMap();
  m.Set(10, 20, 100);

  typename TestFixture::IMap::const_iterator iter = m.begin();
  EXPECT_NE(iter, m.end());
  EXPECT_EQ(Unparse(iter), "[10,20]=>100");
  ++iter;
  EXPECT_EQ(iter, m.end());
}

TYPED_TEST(IntervalMapTest, Iterator_Multiple) {
  auto m = TestFixture::ConstructMap();
  m.Set(5, 10, 100);
  m.Set(15, 20, 200);
  m.Set(25, 30, 300);

  typename TestFixture::IMap::const_iterator iter = m.begin();
  EXPECT_NE(iter, m.end());
  EXPECT_EQ(Unparse(iter), "[5,10]=>100");
  ++iter;
  EXPECT_EQ(Unparse(iter), "[15,20]=>200");
  ++iter;
  EXPECT_EQ(Unparse(iter), "[25,30]=>300");
  ++iter;
  EXPECT_EQ(iter, m.end());
}

TYPED_TEST(IntervalMapTest, Iterator_Find) {
  auto m = TestFixture::ConstructMap();
  m.Set(5, 10, 100);
  m.Set(15, 20, 200);
  m.Set(25, 30, 300);

  // Check the supported types.
  typename TestFixture::IMap::iterator iter = m.find(7);
  EXPECT_EQ(Unparse(iter), "[5,10]=>100");
  typename TestFixture::IMap::const_iterator const_iter = m.find(7);
  EXPECT_EQ(Unparse(const_iter), "[5,10]=>100");

  EXPECT_EQ(Unparse(m.find(4)), "[5,10]=>100");
  EXPECT_EQ(Unparse(m.find(5)), "[5,10]=>100");
  EXPECT_EQ(Unparse(m.find(6)), "[5,10]=>100");
  EXPECT_EQ(Unparse(m.find(9)), "[5,10]=>100");

  EXPECT_EQ(Unparse(m.find(10)), "[15,20]=>200");
  EXPECT_EQ(Unparse(m.find(14)), "[15,20]=>200");
  EXPECT_EQ(Unparse(m.find(15)), "[15,20]=>200");
  EXPECT_EQ(Unparse(m.find(16)), "[15,20]=>200");
  EXPECT_EQ(Unparse(m.find(19)), "[15,20]=>200");

  EXPECT_EQ(Unparse(m.find(20)), "[25,30]=>300");
  EXPECT_EQ(Unparse(m.find(24)), "[25,30]=>300");
  EXPECT_EQ(Unparse(m.find(25)), "[25,30]=>300");
  EXPECT_EQ(Unparse(m.find(26)), "[25,30]=>300");
  EXPECT_EQ(Unparse(m.find(29)), "[25,30]=>300");

  EXPECT_EQ(m.find(30), m.end());
  EXPECT_EQ(m.find(31), m.end());

  // The iterator should go over all intervals from the first one returned.
  const_iter = m.find(17);
  EXPECT_EQ(Unparse(const_iter), "[15,20]=>200");
  ++const_iter;
  EXPECT_EQ(Unparse(const_iter), "[25,30]=>300");
  ++const_iter;
  EXPECT_EQ(const_iter, m.end());
}

TYPED_TEST(IntervalMapTest, Iterator_Contains) {
  auto m = TestFixture::ConstructMap();
  m.Set(5, 10, 100);

  EXPECT_FALSE(m.contains(4));
  EXPECT_TRUE(m.contains(5));
  EXPECT_TRUE(m.contains(9));
  EXPECT_FALSE(m.contains(10));
}

// A helper that checks that an interval map has the same elements in
// the same order as a given vector.  The vector has the start key of
// each interval in the tested interval map, thus only the start key
// is checked.
template <class IMap>
static void ExpectIntervalMapOrder(const IMap& interval_map,
                                   absl::Span<const int> start_keys) {
  ASSERT_THAT(interval_map, SizeIs(start_keys.size()));

  int i = 0;
  for (typename IMap::const_iterator iter = interval_map.begin();
       iter != interval_map.end(); ++iter, ++i) {
    EXPECT_EQ(iter->start, start_keys[i]);
  }
}

// Confirm that iterating through an IntervalMap gets intervals in
// sorted order.
TYPED_TEST(IntervalMapTest, Iterator_SortedOrder) {
  std::vector<int> start_keys;
  start_keys.push_back(111);
  start_keys.push_back(222);

  {
    // Add the intervals in order:
    auto interval_map = TestFixture::ConstructMap();
    interval_map.Set(111, 155, 0);
    interval_map.Set(222, 255, 0);
    ExpectIntervalMapOrder(interval_map, start_keys);
  }

  {
    // Add the intervals out of order:
    auto interval_map = TestFixture::ConstructMap();
    interval_map.Set(222, 255, 0);
    interval_map.Set(111, 155, 0);
    ExpectIntervalMapOrder(interval_map, start_keys);
  }
}

TYPED_TEST(IntervalMapTest, EqualsWithEmptyMaps) {
  auto m1 = TestFixture::ConstructMap();
  auto m2 = TestFixture::ConstructMap();

  EXPECT_TRUE(m1 == m2);
  EXPECT_TRUE(m2 == m1);
  EXPECT_FALSE(m1 != m2);
  EXPECT_FALSE(m2 != m1);
}

TYPED_TEST(IntervalMapTest, EqualsWithIdenticalSingleIntervalMaps) {
  auto m1 = TestFixture::ConstructMap();
  m1.Set(5, 10, 100);
  auto m2 = TestFixture::ConstructMap();
  m2.Set(5, 10, 100);

  EXPECT_TRUE(m1 == m2);
  EXPECT_TRUE(m2 == m1);
  EXPECT_FALSE(m1 != m2);
  EXPECT_FALSE(m2 != m1);
}

TYPED_TEST(IntervalMapTest, EqualsWithIdenticalMultipleIntervalMaps) {
  auto m1 = TestFixture::ConstructMap();
  m1.Set(5, 10, 100);
  m1.Set(15, 20, 200);
  auto m2 = TestFixture::ConstructMap();
  m2.Set(15, 20, 200);
  m2.Set(5, 10, 100);

  EXPECT_TRUE(m1 == m2);
  EXPECT_TRUE(m2 == m1);
  EXPECT_FALSE(m1 != m2);
  EXPECT_FALSE(m2 != m1);
}

TYPED_TEST(IntervalMapTest, EqualsWithEmptyAndNonEmptyMaps) {
  auto m1 = TestFixture::ConstructMap();
  auto m2 = TestFixture::ConstructMap();
  m2.Set(5, 10, 100);
  m2.Set(15, 20, 200);

  EXPECT_FALSE(m1 == m2);
  EXPECT_FALSE(m2 == m1);
  EXPECT_TRUE(m1 != m2);
  EXPECT_TRUE(m2 != m1);
}

TYPED_TEST(IntervalMapTest, EqualsWithMapsOfDifferentSizes) {
  auto m1 = TestFixture::ConstructMap();
  m1.Set(5, 10, 100);
  auto m2 = TestFixture::ConstructMap();
  m2.Set(15, 20, 200);
  m2.Set(5, 10, 100);

  EXPECT_FALSE(m1 == m2);
  EXPECT_FALSE(m2 == m1);
  EXPECT_TRUE(m1 != m2);
  EXPECT_TRUE(m2 != m1);
}

TYPED_TEST(IntervalMapTest, EqualsWithDifferentSingleIntervalMaps) {
  auto m1 = TestFixture::ConstructMap();
  m1.Set(5, 10, 100);
  auto m2 = TestFixture::ConstructMap();
  m2.Set(5, 10, 200);
  auto m3 = TestFixture::ConstructMap();
  m3.Set(5, 20, 100);
  auto m4 = TestFixture::ConstructMap();
  m4.Set(0, 10, 100);

  EXPECT_FALSE(m1 == m2);
  EXPECT_FALSE(m1 == m3);
  EXPECT_FALSE(m1 == m4);
  EXPECT_FALSE(m2 == m1);
  EXPECT_FALSE(m3 == m1);
  EXPECT_FALSE(m4 == m1);
  EXPECT_TRUE(m1 != m2);
  EXPECT_TRUE(m1 != m3);
  EXPECT_TRUE(m1 != m4);
  EXPECT_TRUE(m2 != m1);
  EXPECT_TRUE(m3 != m1);
  EXPECT_TRUE(m4 != m1);
}

TYPED_TEST(IntervalMapTest, EqualsWithDifferentTwoIntervalsMaps) {
  auto m1 = TestFixture::ConstructMap();
  m1.Set(5, 10, 100);
  m1.Set(20, 30, 200);
  auto m2 = TestFixture::ConstructMap();
  m2.Set(5, 10, 100);
  m2.Set(20, 30, 300);
  auto m3 = TestFixture::ConstructMap();
  m3.Set(5, 10, 100);
  m3.Set(20, 25, 200);
  auto m4 = TestFixture::ConstructMap();
  m4.Set(5, 10, 100);
  m4.Set(15, 20, 200);

  EXPECT_FALSE(m1 == m2);
  EXPECT_FALSE(m1 == m3);
  EXPECT_FALSE(m1 == m4);
  EXPECT_FALSE(m2 == m1);
  EXPECT_FALSE(m3 == m1);
  EXPECT_FALSE(m4 == m1);
  EXPECT_TRUE(m1 != m2);
  EXPECT_TRUE(m1 != m3);
  EXPECT_TRUE(m1 != m4);
  EXPECT_TRUE(m2 != m1);
  EXPECT_TRUE(m3 != m1);
  EXPECT_TRUE(m4 != m1);
}

// IntervalMapDanglingReferencesTests are designed to catch potential issues
// with dangling references when modifying an IntervalMap by using keys that are
// references stored in the map itself (which can get invalidated by map
// modifications). These tests were introduced to make sure that using
// `absl::btree_set` (instead of `std::set`) would not cause dangling reference
// issues.
TEST(IntervalMapDanglingReferencesTest, Erase) {
  gtl::IntervalMap<int, int> map;
  map.Set(0, 10, 1);
  map.Set(11, 15, 2);
  map.Set(16, 19, 3);
  map.Set(20, 23, 4);
  map.Set(24, 29, 5);
  map.Set(30, 33, 6);
  map.Set(35, 36, 7);

  auto it = map.find(18);
  CHECK(it != map.end());
  EXPECT_EQ(it->start, 16);
  EXPECT_EQ(it->limit, 19);
  EXPECT_EQ(map.begin()->limit, 10);
  // This should erase [10,19).
  map.Erase(map.begin()->limit, it->limit);
  EXPECT_THAT(map, ElementsAre(EntryIs(0, 10, 1), EntryIs(20, 23, 4),
                               EntryIs(24, 29, 5), EntryIs(30, 33, 6),
                               EntryIs(35, 36, 7)));
}

TEST(IntervalMapDanglingReferencesTest, Set) {
  gtl::IntervalMap<int, int> map;
  map.Set(0, 10, 1);
  map.Set(11, 12, 2);
  map.Set(16, 19, 3);
  map.Set(20, 23, 4);

  auto it = map.find(11);
  EXPECT_EQ(it->start, 11);
  EXPECT_EQ(it->limit, 12);
  // This should set the interval [11,22).
  map.Set(it->start, it->limit + 10, 5);
  EXPECT_THAT(map, ElementsAre(EntryIs(0, 10, 1), EntryIs(11, 22, 5),
                               EntryIs(22, 23, 4)));
}

TEST(IntervalMapDanglingReferencesTest, SetAndCoalesce) {
  gtl::IntervalMap<int, int> map;
  map.Set(0, 10, 1);
  map.Set(11, 12, 2);
  map.Set(16, 19, 3);
  map.Set(20, 23, 4);

  auto it = map.find(11);
  EXPECT_EQ(it->start, 11);
  EXPECT_EQ(it->limit, 12);
  // This should set the interval [3,12).
  map.SetAndCoalesce(3, it->limit, 5);
  EXPECT_THAT(map, ElementsAre(EntryIs(0, 3, 1), EntryIs(3, 12, 5),
                               EntryIs(16, 19, 3), EntryIs(20, 23, 4)));
}

TEST(IntervalMapDanglingReferencesTest, SetNoOverlap) {
  gtl::IntervalMap<int, int> map;
  map.Set(0, 10, 1);
  map.Set(11, 12, 2);
  map.Set(20, 23, 3);

  auto it1 = map.find(11);
  EXPECT_EQ(it1->start, 11);
  EXPECT_EQ(it1->limit, 12);
  auto it2 = map.find(22);
  EXPECT_EQ(it2->start, 20);
  EXPECT_EQ(it2->limit, 23);
  // This should set the interval [12,20).
  map.SetNoOverlap(it1->limit, it2->start, 4);
  EXPECT_THAT(map, ElementsAre(EntryIs(0, 10, 1), EntryIs(11, 12, 2),
                               EntryIs(12, 20, 4), EntryIs(20, 23, 3)));
}

TEST(IntervalMapDanglingReferencesTest, SplitAt) {
  gtl::IntervalMap<int, int> map;
  map.Set(0, 10, 1);
  map.Set(11, 40, 2);

  auto it1 = map.find(20);
  EXPECT_EQ(it1->start, 11);
  EXPECT_EQ(it1->limit, 40);
  map.SplitAt(it1->start);
  EXPECT_THAT(map, ElementsAre(EntryIs(0, 10, 1), EntryIs(11, 40, 2)));
}

TEST(IntervalMapDanglingReferencesTest, MergeValue) {
  gtl::IntervalMap<int, int> map;
  map.Set(0, 10, 1);
  map.Set(11, 15, 2);
  map.Set(16, 19, 3);
  map.Set(20, 23, 4);
  map.Set(24, 29, 5);
  map.Set(30, 33, 6);
  map.Set(35, 36, 7);

  auto it = map.find(32);
  CHECK(it != map.end());
  EXPECT_EQ(it->start, 30);
  EXPECT_EQ(it->limit, 33);
  EXPECT_EQ(map.begin()->limit, 10);
  // This should merge [10,33).
  map.MergeValue(map.begin()->limit, it->limit,
                 [](const int start, const int limit, int* existing_value,
                    int* new_value) {
                   if (existing_value != nullptr) {
                     *new_value += *existing_value;
                   } else {
                     *new_value = 42;
                   }
                   return new_value;
                 });
  EXPECT_THAT(map, ElementsAre(EntryIs(0, 10, 1), EntryIs(10, 11, 42),
                               EntryIs(11, 15, 44), EntryIs(15, 16, 42),
                               EntryIs(16, 19, 45), EntryIs(19, 20, 42),
                               EntryIs(20, 23, 46), EntryIs(23, 24, 42),
                               EntryIs(24, 29, 47), EntryIs(29, 30, 42),
                               EntryIs(30, 33, 48), EntryIs(35, 36, 7)));
}

}  // namespace test_int_int

TEST(IntervalMapArgumentForwarding, SetWithMoveableValue) {
  struct ConstructorCounter {
    // No counting for default constructed counters.
    ConstructorCounter() : copy_count(nullptr), move_count(nullptr) {}
    ConstructorCounter(int* copy_count, int* move_count)
        : copy_count(copy_count), move_count(move_count) {}
    ConstructorCounter(const ConstructorCounter& other) {
      copy_count = other.copy_count;
      move_count = other.move_count;
      if (copy_count) ++*copy_count;
    }
    ConstructorCounter(ConstructorCounter&& other) {
      copy_count = other.copy_count;
      move_count = other.move_count;
      if (move_count) ++*move_count;
    }
    bool operator==(const ConstructorCounter& other) const { return false; }
    int* copy_count;
    int* move_count;
  };

  gtl::IntervalMap<int, ConstructorCounter> imap;

  int copy_count = 0;
  int move_count = 0;
  ConstructorCounter value(&copy_count, &move_count);

  // Reading and writing can be done with zero copies.
  imap.Set(1, 2, std::move(value));
  imap.SetNoOverlap(2, 3, std::move(value));
  imap.SetAndCoalesce(2, 3, std::move(value));

  auto it = imap.find(1);
  ASSERT_NE(it, imap.end());
  EXPECT_GE(*(it->value.move_count), 3);
  EXPECT_EQ(*(it->value.copy_count), 0);
}

TEST(IntervalMapArgumentForwarding, SetImplicitConstructible) {
  struct ImplicitlyConstructible {
    // NOLINTNEXTLINE intentional implicit constructor.
    ImplicitlyConstructible(int) {}
    bool operator==(const ImplicitlyConstructible&) const { return false; }
  };
  gtl::IntervalMap<int, ImplicitlyConstructible> imap;
  imap.Set(1, 2, 9000);
  imap.SetNoOverlap(2, 3, 9000);
  imap.SetAndCoalesce(3, 4, 9000);
}

namespace test_specialtype_string {
// Varying container set types, SpecialTypeTest tests
// IntervalMap<SpecialType, string>.

// Int-like type that only has operator<(), and is not loggable.
class SpecialType {
 private:
  int value_;

 public:
  SpecialType() = default;
  explicit SpecialType(int v) : value_(v) {}
  bool operator<(const SpecialType& x) const { return value_ < x.value_; }
  std::string ToString() const { return absl::StrCat(value_); }
};

template <typename T>
class SpecialTypeTest : public ::testing::Test {
 public:
  using SMap = T;
};

using MyTypes = ::testing::Types<MapTypes<SpecialType, std::string>::STL_IMap>;
TYPED_TEST_SUITE(SpecialTypeTest, MyTypes);

// Check that special type works, notably that IntervalMap
// only relies on SpecialType::operator<().
TYPED_TEST(SpecialTypeTest, SpecialType) {
  typename TestFixture::SMap m;
  m.Set(SpecialType(10), SpecialType(200), "100");
  m.Erase(SpecialType(100), SpecialType(1000));

  EXPECT_NE(m.find(SpecialType(10)), m.end());
  EXPECT_TRUE(m.contains(SpecialType(10)));

  std::string v;
  ASSERT_TRUE(m.Lookup(SpecialType(50), &v));
  ASSERT_EQ(v, "100");

  SpecialType s, e;
  ASSERT_TRUE(m.FindInterval(SpecialType(50), &s, &e, &v));
  ASSERT_TRUE(m.FindNext(SpecialType(10), &s, &e, &v));
}

// Helper for testing SetAndCoalesce.
// Expects calls to B*, Go, Check.
template <class StrMap>
struct Tester {
  explicit Tester(int test_line) : line(test_line) {}
  std::string ToString() {
    std::string retval;
    for (typename StrMap::const_iterator iter = map.begin(); iter != map.end();
         ++iter) {
      absl::StrAppendFormat(&retval, "<%s,%s,%s>,", iter->start.ToString(),
                            iter->limit.ToString(), iter->value);
    }
    return retval;
  }
  // Adds [start, limit) -> value to ents.
  Tester& B(int start, int limit, const absl::string_view value) {
    typename StrMap::Entry ent;
    ent.start = SpecialType(start);
    ent.limit = SpecialType(limit);
    ent.value = std::string(value);
    ents.push_back(ent);
    return *this;
  }
  // Populates map from ents, calls SetAndCoalesce.
  Tester& Go(int start, int limit, const absl::string_view val) {
    std::string value = std::string(val);
    for (int i = 0; i < ents.size(); ++i) {
      map.Set(ents[i].start, ents[i].limit, ents[i].value);
    }
    map.SetAndCoalesce(SpecialType(start), SpecialType(limit), value);
    return *this;
  }
  Tester& Check(const absl::string_view expected) {
    EXPECT_EQ(expected, ToString()) << " test line " << line;
    return *this;
  }
  int line;
  std::vector<typename StrMap::Entry> ents;
  StrMap map;
};

// Iterates over permutations, for left and right, of
//  - none
//  - adjacent, value ==
//  - adjacent, val !=
//  - non-abutting, val ==
#define TESTER Tester<typename TestFixture::SMap>(__LINE__)
TYPED_TEST(SpecialTypeTest, SetAndCoalesce) {
  // No left, no right
  TESTER.Go(5, 10, "asdf").Check("<5,10,asdf>,");
  TESTER.Go(5, 10, "asdf").Check("<5,10,asdf>,");
  // No left, adjacent right, val ==
  TESTER.B(10, 20, "a").Go(5, 10, "a").Check("<5,20,a>,");
  // No left, overlapping right, val ==.
  //   (Overlapping hereafter mostly omitted, since it's the same as
  //    adjacent after the Erase() call.)
  TESTER.B(10, 20, "a").Go(5, 13, "a").Check("<5,20,a>,");
  // No left, adjacent right, val !=
  TESTER.B(10, 20, "a").Go(5, 10, "foo").Check("<5,10,foo>,<10,20,a>,");
  // No left, non-abutting right, val ==
  TESTER.B(10, 20, "a").Go(1, 5, "a").Check("<1,5,a>,<10,20,a>,");

  // Adjacent left, val ==; no right
  TESTER.B(10, 20, "a").Go(20, 30, "a").Check("<10,30,a>,");
  // Adjacent left, val ==; adjacent right, val ==
  TESTER.B(10, 20, "foo")
      .B(21, 30, "foo")
      .Go(20, 21, "foo")
      .Check("<10,30,foo>,");
  // Adjacent left, val ==; adjacent right, val !=
  TESTER.B(10, 20, "foo")
      .B(30, 40, "bar")
      .Go(20, 30, "foo")
      .Check("<10,30,foo>,<30,40,bar>,");
  // Adjacent left, val ==; non-abutting right, val ==
  TESTER.B(10, 20, "foo")
      .B(30, 40, "foo")
      .Go(20, 29, "foo")
      .Check("<10,29,foo>,<30,40,foo>,");

  // Adjacent left, val !=; no right
  TESTER.B(10, 20, "foo").Go(20, 30, "bar").Check("<10,20,foo>,<20,30,bar>,");
  // Adjacent left, val !=; adjacent right, val ==
  TESTER.B(10, 20, "foo")
      .B(25, 30, "bar")
      .Go(20, 25, "bar")
      .Check("<10,20,foo>,<20,30,bar>,");
  // Adjacent left, val !=; adjacent right, val !=
  TESTER.B(10, 20, "foo")
      .B(30, 40, "bar")
      .Go(20, 30, "baz")
      .Check("<10,20,foo>,<20,30,baz>,<30,40,bar>,");
  // Adjacent left, val !=; non-abutting right, val ==
  TESTER.B(10, 20, "foo")
      .B(30, 40, "bar")
      .Go(20, 25, "bar")
      .Check("<10,20,foo>,<20,25,bar>,<30,40,bar>,");

  // Non-abutting left, val ==; no right
  TESTER.B(1, 3, "foo").Go(10, 20, "foo").Check("<1,3,foo>,<10,20,foo>,");
  // Non-abutting left, val ==; adjacent right, val ==
  TESTER.B(1, 3, "foo")
      .B(6, 10, "foo")
      .Go(5, 6, "foo")
      .Check("<1,3,foo>,<5,10,foo>,");
  // Non-abutting left, val ==; adjacent right, val !=
  TESTER.B(1, 3, "foo")
      .B(6, 10, "bar")
      .Go(5, 6, "foo")
      .Check("<1,3,foo>,<5,6,foo>,<6,10,bar>,");
  // Non-abutting left, val ==; non-abutting right, val ==
  TESTER.B(1, 3, "foo")
      .B(10, 13, "foo")
      .Go(5, 7, "foo")
      .Check("<1,3,foo>,<5,7,foo>,<10,13,foo>,");

  // Some overlaps:
  TESTER.B(1, 100, "foo").Go(20, 40, "foo").Check("<1,100,foo>,");
  TESTER.B(1, 100, "foo")
      .B(100, 101, "beets")
      .Go(20, 40, "bar")
      .Check("<1,20,foo>,<20,40,bar>,<40,100,foo>,<100,101,beets>,");

  // One-by-one insertion
  TESTER.Go(0, 1, "foo")
      .Check("<0,1,foo>,")
      .Go(1, 2, "foo")
      .Check("<0,2,foo>,");

  // Several entries to right and left
  TESTER.B(1, 5, "foo")
      .B(5, 10, "foo")
      .B(10, 12, "bar")
      .B(12, 18, "asdf")
      .B(21, 40, "asdf")
      .Go(17, 20, "asdf")
      .Check("<1,5,foo>,<5,10,foo>,<10,12,bar>,<12,20,asdf>,<21,40,asdf>,");
}
#undef TESTER

}  // namespace test_specialtype_string

// Creates an IntervalMap with K string-like keys. The key can be either a Cord
// or a string.
template <class IMap>
static void SetupStringLikeMap(IMap* imap, int K) {
  // Populate IntervalMap with "K" entries
  for (int i = 0; i < K; i++) {
    typename IMap::key_type start(absl::StrFormat("%010d", i));
    typename IMap::key_type limit(absl::StrFormat("%010d.end", i));
    imap->Set(start, limit, true);
  }
}

// Measures the cost of erasing a range from an IntervalMap with string-like
// keys. The key can be either a Cord or a string.
template <class IMap>
static void BM_StringLikeKeyErase(benchmark::State& state) {
  const int arg = state.range(0);

  IMap src;
  SetupStringLikeMap(&src, arg);
  // Use a batch of maps to overflow the cache.
  const int kBatchSize = std::max(1, 1000 / arg);
  std::vector<IMap> maps(kBatchSize, src);
  std::seed_seq seed{1, 2, 3};
  std::mt19937 gen(seed);
  std::uniform_int_distribution<> distrib(0, arg);
  int num_items_processed = 0;
  while (state.KeepRunningBatch(kBatchSize)) {
    state.PauseTiming();
    maps.clear();
    maps.resize(kBatchSize, src);
    // Choose a random start index in the range [0, arg).
    const int start_index = distrib(gen);
    typename IMap::key_type start(absl::StrFormat("%010d", start_index));
    // The limit is chosen so that the deleted range is approximately 1/10th
    // of the size of the map. Note that if the start_index is close to arg,
    // the deleted range will be smaller.
    typename IMap::key_type limit(
        absl::StrFormat("%010d.end", start_index + arg / 10));
    state.ResumeTiming();
    for (auto& map : maps) {
      map.Erase(start, limit);
    }
    num_items_processed += kBatchSize;
  }
  state.SetItemsProcessed(num_items_processed);
}

namespace benchmark_cord_bool {

// IntervalMap types we benchmark.
using STLSet = MapTypes<absl::Cord, bool>::STL_IMap;

// Measure the cost of seeking into an IntervalMap with K short Cords
template <class IMap>
static void BM_CordIntervalSeek(benchmark::State& state) {
  const int K = state.range(0);

  IMap imap;
  SetupStringLikeMap(&imap, K);

  // Use a batch of maps to overflow the cache.
  const int kBatchSize = std::max(1, 1000 / K);
  const std::vector<IMap> maps(kBatchSize, imap);
  absl::Cord key(absl::StrFormat("%010d", K / 2));
  absl::Cord start, limit;
  bool b;

  int num_items_processed = 0;
  while (state.KeepRunningBatch(kBatchSize)) {
    for (const auto& map : maps) {
      map.FindNext(key, &start, &limit, &b);
    }
    num_items_processed += kBatchSize;
  }
  state.SetItemsProcessed(num_items_processed);
}
BENCHMARK_TEMPLATE(BM_CordIntervalSeek, STLSet)->Range(1, 256 << 10);

template <class IMap>
static void BM_CordFindNextIterate(benchmark::State& state) {
  const int size = state.range(0);

  IMap imap;
  SetupStringLikeMap(&imap, size);

  // Use a batch of maps to overflow the cache.
  const int kBatchSize = std::max(1, 1000 / size);
  const std::vector<IMap> maps(kBatchSize, imap);
  absl::Cord i, start, limit;
  bool b;
  int num_items_processed = 0;
  while (state.KeepRunningBatch(kBatchSize)) {
    for (const auto& map : maps) {
      if (map.FindNext(i, &start, &limit, &b)) {
        i = limit;
      } else {
        i.Clear();  // Restart at front
      }
    }
    num_items_processed += kBatchSize;
  }
  state.SetItemsProcessed(num_items_processed);
}
BENCHMARK_TEMPLATE(BM_CordFindNextIterate, STLSet)->Range(1, 256 << 10);

template <class IMap>
static void BM_CordIterate(benchmark::State& state) {
  const int size = state.range(0);

  gtl::IntervalMap<absl::Cord, bool> imap;
  SetupStringLikeMap(&imap, size);

  // Use a batch of maps to overflow the cache.
  const int kBatchSize = std::max(1, 1000 / size);
  const std::vector<IMap> maps(kBatchSize, imap);
  int num_items_processed = 0;
  while (state.KeepRunningBatch(kBatchSize)) {
    for (const auto& map : maps) {
      for (const auto& entry : map) {
        benchmark::DoNotOptimize(entry);
      }
      num_items_processed += map.size();
    }
  }
  state.SetItemsProcessed(num_items_processed);
}
BENCHMARK_TEMPLATE(BM_CordIterate, STLSet)->Range(1, 256 << 10);

BENCHMARK_TEMPLATE(BM_StringLikeKeyErase, STLSet)->Range(1, 64 << 10);

}  // namespace benchmark_cord_bool

namespace benchmark_string_bool {
using STLSet = MapTypes<std::string, bool>::STL_IMap;

// Measure the cost of seeking into an IntervalMap with K short strings
template <class IMap>
static void BM_StringIntervalSeek(benchmark::State& state) {
  const int K = state.range(0);

  // Populate IntervalMap with "K" entries
  gtl::IntervalMap<std::string, bool> imap;
  for (int i = 0; i < K; i++) {
    std::string start(absl::StrFormat("%010d", i));
    std::string limit(start);
    limit += ".end";
    imap.Set(start, limit, true);
  }

  // Use a batch of maps to overflow the cache.
  const int kBatchSize = std::max(1, 1000 / K);
  const std::vector<IMap> maps(kBatchSize, imap);
  std::string key(absl::StrFormat("%010d", K / 2));
  std::string start, limit;
  bool b;

  int num_items_processed = 0;
  while (state.KeepRunningBatch(kBatchSize)) {
    for (const auto& map : maps) {
      map.FindNext(key, &start, &limit, &b);
    }
    num_items_processed += kBatchSize;
  }
  state.SetItemsProcessed(num_items_processed);
}
BENCHMARK_TEMPLATE(BM_StringIntervalSeek, STLSet)->Range(1, 256 << 10);

BENCHMARK_TEMPLATE(BM_StringLikeKeyErase, STLSet)->Range(1, 64 << 10);

}  // namespace benchmark_string_bool

namespace benchmark_int_int {

using STLSet = MapTypes<int, int>::STL_IMap;

template <class IMap>
static void BM_EmptyCreateDestroy(benchmark::State& state) {
  int r = 1;
  for (auto s : state) {
    IMap m;
    r += m.size();
  }
  ASSERT_NE(r, 0);
}
BENCHMARK_TEMPLATE(BM_EmptyCreateDestroy, STLSet);

template <class IMap>
static void BM_MergeSubRangeFrom(benchmark::State& state) {
  const int arg = state.range(0);

  IMap src;
  for (int i = 0; i < arg; i++) {
    src.Set(2 * i, 2 * i + 1, true);
  }
  CHECK_EQ(src.size(), arg);

  // Use a batch of maps to overflow the cache.
  const int kBatchSize = std::max(1, 1000 / arg);
  std::vector<IMap> dst_maps(kBatchSize);
  int num_items_processed = 0;
  while (state.KeepRunningBatch(kBatchSize)) {
    state.PauseTiming();
    dst_maps.clear();
    dst_maps.resize(kBatchSize);
    state.ResumeTiming();
    for (auto& dst : dst_maps) {
      dst.MergeSubRangeFrom(src, 0, 2 * arg + 1);
      num_items_processed += src.size();
    }
  }
  state.SetItemsProcessed(num_items_processed);
  ASSERT_NE(num_items_processed, 0);
}
BENCHMARK_TEMPLATE(BM_MergeSubRangeFrom, STLSet)->Range(1, 100000);

template <class IMap>
static void BM_MergeFrom(benchmark::State& state) {
  const int arg = state.range(0);

  IMap src;
  for (int i = 0; i < arg; i++) {
    src.Set(2 * i, 2 * i + 1, true);
  }
  CHECK_EQ(src.size(), arg);

  // Use a batch of maps to overflow the cache.
  const int kBatchSize = std::max(1, 1000 / arg);
  std::vector<IMap> dst_maps(kBatchSize);
  int num_items_processed = 0;
  while (state.KeepRunningBatch(kBatchSize)) {
    state.PauseTiming();
    dst_maps.clear();
    dst_maps.resize(kBatchSize);
    state.ResumeTiming();
    for (auto& dst : dst_maps) {
      dst.MergeFrom(src);
      num_items_processed += src.size();
    }
  }
  state.SetItemsProcessed(num_items_processed);
  ASSERT_NE(num_items_processed, 0);
}
BENCHMARK_TEMPLATE(BM_MergeFrom, STLSet)->Range(1, 100000);

template <class IMap>
static void BM_IntKeyErase(benchmark::State& state) {
  const int arg = state.range(0);

  IMap src;
  // We add arg non-overlapping entries.
  for (int i = 0; i < arg; i++) {
    src.Set(2 * i, 2 * i + 1, true);
  }
  CHECK_EQ(src.size(), arg);
  // Use a batch of maps to overflow the cache.
  const int kBatchSize = std::max(1, 1000 / arg);
  std::vector<IMap> maps(kBatchSize, src);
  std::seed_seq seed{1, 2, 3};
  std::mt19937 gen(seed);
  std::uniform_int_distribution<> distrib(0, arg);
  int num_items_processed = 0;
  for (auto s : state) {
    state.PauseTiming();
    // Reset the maps.
    maps.clear();
    maps.resize(kBatchSize, src);
    // Choose a random start index in the range [0, arg).
    const int start = distrib(gen);
    // The limit is chosen so that the deleted range is approximately 1/10th
    // of the size of the map.
    const int limit = start + arg / 10;
    state.ResumeTiming();
    for (auto& map : maps) {
      map.Erase(start, limit);
    }
    num_items_processed += kBatchSize;
  }
  state.SetItemsProcessed(num_items_processed);
}
BENCHMARK_TEMPLATE(BM_IntKeyErase, STLSet)->Range(1, 64 << 10);

}  // namespace benchmark_int_int

namespace benchmark_cord_bool {

// Makes a Cord from an InputT.
template <typename InputT>
static absl::Cord GetCord(const InputT& input) {
  return absl::Cord(input);
}

// And forwards a Cord as is.
static const absl::Cord& GetCord(const absl::Cord& cord) { return cord; }

template <class IMap, class InputT>
static void BM_CordSet(benchmark::State& state) {
  const int arg = state.range(0);

  std::vector<InputT> inputs;
  for (int i = 0; i <= arg; i++) {
    inputs.push_back(InputT(absl::StrFormat("%010d", i)));
  }

  // Use a batch of maps to overflow the cache.
  const int kBatchSize = std::max(1, 1000 / arg);
  std::vector<IMap> maps(kBatchSize);
  int num_items_processed = 0;
  while (state.KeepRunningBatch(kBatchSize)) {
    state.PauseTiming();
    // Reset the maps.
    maps.clear();
    maps.resize(kBatchSize);
    state.ResumeTiming();
    for (auto& map : maps) {
      for (int i = 0; i < arg; i++) {
        map.Set(GetCord(inputs[i]), GetCord(inputs[i + 1]), true);
      }
      num_items_processed += arg;
    }
  }
  state.SetItemsProcessed(num_items_processed);
}
BENCHMARK_TEMPLATE(BM_CordSet, STLSet, absl::Cord)->Range(1, 16384);
BENCHMARK_TEMPLATE(BM_CordSet, STLSet, std::string)->Range(1, 16384);

template <class IMap, class InputT>
static void BM_CordSetNoOverlap(benchmark::State& state) {
  const int arg = state.range(0);

  std::vector<InputT> inputs;
  for (int i = 0; i <= arg; i++) {
    inputs.push_back(InputT(absl::StrFormat("%010d", i)));
  }

  // Use a batch of maps to overflow the cache.
  const int kBatchSize = std::max(1, 1000 / arg);
  std::vector<IMap> maps(kBatchSize);
  int num_items_processed = 0;
  while (state.KeepRunningBatch(kBatchSize)) {
    state.PauseTiming();
    maps.clear();
    maps.resize(kBatchSize);
    state.ResumeTiming();
    for (auto& map : maps) {
      for (int i = 0; i < arg; i++) {
        map.SetNoOverlap(GetCord(inputs[i]), GetCord(inputs[i + 1]), true);
      }
      num_items_processed += arg;
    }
  }
  state.SetItemsProcessed(num_items_processed);
}
BENCHMARK_TEMPLATE(BM_CordSetNoOverlap, STLSet, absl::Cord)->Range(1, 16384);
BENCHMARK_TEMPLATE(BM_CordSetNoOverlap, STLSet, std::string)->Range(1, 16384);

template <class IMap>
static void BM_CordSetManyNoOverlap(benchmark::State& state) {
  const int arg = state.range(0);

  std::vector<absl::Cord> cords;
  for (int i = 0; i <= arg; i++) {
    cords.push_back(absl::Cord(absl::StrFormat("%010d", i)));
  }
  std::vector<typename IMap::Entry> entries(arg);
  for (int i = 0; i < arg; i++) {
    entries[i].start = cords[i];
    entries[i].limit = cords[i + 1];
    entries[i].value = true;
  }

  // Use a batch of maps to overflow the cache.
  const int kBatchSize = std::max(1, 1000 / arg);
  std::vector<IMap> maps(kBatchSize);
  int num_items_processed = 0;
  while (state.KeepRunningBatch(kBatchSize)) {
    state.PauseTiming();
    // Reset the maps.
    maps.clear();
    maps.resize(kBatchSize);
    state.ResumeTiming();
    for (auto& map : maps) {
      map.SetManyNoOverlap(arg, &entries[0]);
      ASSERT_THAT(map, SizeIs(arg));
      num_items_processed += arg;
    }
  }
  state.SetItemsProcessed(num_items_processed);
}
BENCHMARK_TEMPLATE(BM_CordSetManyNoOverlap, STLSet)->Range(1, 16384);

TEST(IntervalMapLookupTest, Bounds) {
  gtl::IntervalMap<int, int> m;
  m.Set(10, 15, 1);
  m.Set(15, 20, 2);
  m.Set(50, 60, 3);

  // Lookup()
  int value;
  EXPECT_FALSE(m.Lookup(9, &value));
  EXPECT_TRUE(m.Lookup(10, &value));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(m.Lookup(14, &value));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(m.Lookup(15, &value));
  EXPECT_EQ(value, 2);
  EXPECT_TRUE(m.Lookup(19, &value));
  EXPECT_EQ(value, 2);
  EXPECT_FALSE(m.Lookup(20, &value));
  EXPECT_FALSE(m.Lookup(49, &value));
  EXPECT_TRUE(m.Lookup(50, &value));
  EXPECT_EQ(value, 3);
  EXPECT_TRUE(m.Lookup(59, &value));
  EXPECT_EQ(value, 3);
  EXPECT_FALSE(m.Lookup(60, &value));

  // LookupPtr()
  EXPECT_EQ(m.LookupPtr(9), nullptr);
  EXPECT_THAT(m.LookupPtr(10), Pointee(1));
  EXPECT_THAT(m.LookupPtr(14), Pointee(1));
  EXPECT_THAT(m.LookupPtr(15), Pointee(2));
  EXPECT_THAT(m.LookupPtr(19), Pointee(2));
  EXPECT_EQ(m.LookupPtr(20), nullptr);
  EXPECT_EQ(m.LookupPtr(49), nullptr);
  EXPECT_THAT(m.LookupPtr(50), Pointee(3));
  EXPECT_THAT(m.LookupPtr(59), Pointee(3));
  EXPECT_EQ(m.LookupPtr(60), nullptr);
}

TEST(IntervalMapLookupTest, LimitedPointerStability) {
  gtl::IntervalMap<int, int> m;
  m.Set(100, 101, 42);
  const int* pv100 = m.LookupPtr(100);
  ASSERT_THAT(pv100, Pointee(42));

  // The pointer remains valid after non-coalescing insertions around the value.
  for (int i = 0; i < 100; ++i) {
    m.Set(i, i + 1, 42);
    EXPECT_EQ(m.LookupPtr(100), pv100);
  }
  for (int i = 101; i < 200; ++i) {
    m.Set(i, i + 1, 42);
    EXPECT_EQ(m.LookupPtr(100), pv100);
  }
  const int* pv101 = m.LookupPtr(101);
  ASSERT_THAT(pv101, Pointee(42));
  ASSERT_NE(pv101, pv100);

  // All pointers but one become invalid after coalescing.
  m.Coalesce();
  ASSERT_TRUE(m.LookupPtr(100) != pv100 || m.LookupPtr(101) != pv101);
}

TEST(IntervalMapLookupTest, HeterogeneousLookup) {
  gtl::IntervalMap<std::string, int, std::less<>> m;
  m.Set("a", "b", 1);

  int value;
  EXPECT_TRUE(m.Lookup(absl::string_view("abc"), &value));
  EXPECT_EQ(value, 1);

  const int* pvalue = m.LookupPtr(absl::string_view("abc"));
  EXPECT_THAT(pvalue, Pointee(1));

  EXPECT_EQ(m.find(absl::string_view("abc"))->start, "a");

  EXPECT_TRUE(m.contains(absl::string_view("abc")));

  std::string start, limit;
  EXPECT_TRUE(m.FindInterval(absl::string_view("abc"), &start, &limit, &value));
  EXPECT_EQ(start, "a");
  EXPECT_EQ(limit, "b");

  EXPECT_TRUE(m.FindNext(absl::string_view(""), &start, &limit, &value));
  EXPECT_TRUE(m.FindInterval(absl::string_view("abc"), &start, &limit, &value));
  EXPECT_EQ(start, "a");

  EXPECT_TRUE(m.FindNextPoint(absl::string_view(""), &start));
  EXPECT_EQ(start, "a");
}

}  // namespace benchmark_cord_bool
