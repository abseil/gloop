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

// Unit test for TopN.

#include "gloop/util/gtl/top_n.h"

#include <stdio.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "gloop/gloop_test.h"
#include "gloop/util/gtl/stl_util.h"
#include "gloop/util/random/acmrandom.h"

namespace gtl {
namespace {

using testing::ElementsAre;
using testing::Eq;
using testing::Pointee;
using testing::UnorderedElementsAre;

template <class Cmp>
void TestIntTopNHelper(size_t limit, size_t n_elements, const Cmp& cmp,
                       ACMRandom* random, bool test_peek,
                       bool test_extract_unsorted) {
  LOG(INFO) << "Testing limit=" << limit << ", n_elements=" << n_elements
            << ", test_peek=" << test_peek
            << ", test_extract_unsorted=" << test_extract_unsorted;
  TopN<int, Cmp> top(limit, cmp);
  std::vector<int> shadow(n_elements);
  for (int i = 0; i != n_elements; ++i)
    shadow[i] = absl::Uniform<int32_t>(*random, 0, limit);
  for (int e : shadow) top.push(e);
  std::sort(shadow.begin(), shadow.end(), cmp);
  size_t top_size = std::min(limit, n_elements);
  EXPECT_EQ(top_size, top.size());
  if (test_peek && top_size != 0) {
    EXPECT_EQ(shadow[top_size - 1], top.peek_bottom());
  }
  std::vector<int> v;
  if (test_extract_unsorted) {
    v = top.TakeUnsorted();
    std::sort(v.begin(), v.end(), cmp);
  } else {
    v = top.Take();
  }
  EXPECT_EQ(top_size, v.size());
  for (int i = 0; i != top_size; ++i) {
    VLOG(1) << "Top element " << v[i];
    EXPECT_EQ(shadow[i], v[i]);
  }
}

template <class Cmp>
void TestIntTopN(size_t limit, size_t n_elements, const Cmp& cmp,
                 ACMRandom* random) {
  // Test peek_bottom() and Take()
  TestIntTopNHelper(limit, n_elements, cmp, random, true, false);
  // Test Take()
  TestIntTopNHelper(limit, n_elements, cmp, random, false, false);
  // Test peek_bottom() and TakeUnsorted()
  TestIntTopNHelper(limit, n_elements, cmp, random, true, true);
  // Test TakeUnsorted()
  TestIntTopNHelper(limit, n_elements, cmp, random, false, true);
}

TEST(TopNTest, Misc) {
  ACMRandom random(ACMRandom::DeterministicSeed());
  TestIntTopN(0, 5, std::greater<int>(), &random);
  TestIntTopN(32, 0, std::greater<int>(), &random);
  TestIntTopN(6, 6, std::greater<int>(), &random);
  TestIntTopN(6, 6, std::less<int>(), &random);
  TestIntTopN(1000, 999, std::greater<int>(), &random);
  TestIntTopN(1000, 1000, std::greater<int>(), &random);
  TestIntTopN(1000, 1001, std::greater<int>(), &random);
  TestIntTopN(2300, 28393, std::less<int>(), &random);
  TestIntTopN(30, 100, std::greater<int>(), &random);
  TestIntTopN(100, 30, std::less<int>(), &random);
  TestIntTopN(size_t(-1), 3, std::greater<int>(), &random);
  TestIntTopN(size_t(-1), 0, std::greater<int>(), &random);
  TestIntTopN(0, 5, std::greater<int>(), &random);
}

TEST(TopNTest, String) {
  LOG(INFO) << "Testing strings";

  TopN<std::string> top(3);
  EXPECT_TRUE(top.empty());
  top.push("abracadabra");
  top.push("waldemar");
  EXPECT_EQ(2, top.size());
  EXPECT_EQ("abracadabra", top.peek_bottom());
  top.push("");
  EXPECT_EQ(3, top.size());
  EXPECT_EQ("", top.peek_bottom());
  top.push("top");
  EXPECT_EQ(3, top.size());
  EXPECT_EQ("abracadabra", top.peek_bottom());
  top.push("Google");
  top.push("test");
  EXPECT_EQ(3, top.size());
  EXPECT_EQ("test", top.peek_bottom());
  TopN<std::string> top2(top);
  TopN<std::string> top3(5);
  top3 = top;
  EXPECT_EQ("test", top3.peek_bottom());
  EXPECT_THAT(top.Take(), ElementsAre("waldemar", "top", "test"));

  top2.push("zero");
  EXPECT_EQ(top2.peek_bottom(), "top");

  EXPECT_THAT(top2.Take(), ElementsAre("zero", "waldemar", "top"));

  EXPECT_THAT(top3.Take(), ElementsAre("waldemar", "top", "test"));

  TopN<std::string> top4(3);
  // Run this test twice to check that TopN is properly cleared on Take().
  for (int i = 0; i < 2; ++i) {
    top4.push("abcd");
    top4.push("ijkl");
    top4.push("efgh");
    top4.push("mnop");
    EXPECT_THAT(top4.TakeUnsorted(),
                UnorderedElementsAre("mnop", "ijkl", "efgh"));
    EXPECT_TRUE(top4.empty());
  }
}

TEST(TopNTest, ResetAndChangeLimit) {
  TopN<int> top(3);
  EXPECT_EQ(top.limit(), 3);
  EXPECT_TRUE(top.empty());
  for (int i = 0; i < 10; ++i) {
    top.push(i);
  }
  EXPECT_EQ(top.size(), 3);
  top.Reset(5);
  EXPECT_EQ(top.limit(), 5);
  EXPECT_TRUE(top.empty());
  for (int i = 0; i < 10; ++i) {
    top.push(i);
  }
  EXPECT_EQ(top.size(), 5);
}

// Test that pointers aren't leaked from a TopN if we use the 2-argument version
// of push().
TEST(TopNTest, Ptr) {
  LOG(INFO) << "Testing 2-argument push()";
  TopN<std::string*> topn(3);
  for (int i = 0; i < 8; ++i) {
    std::string* dropped = nullptr;
    topn.push(new std::string(absl::StrCat(i)), &dropped);
    delete dropped;
  }

  for (int i = 8; i > 0; --i) {
    std::string* dropped = nullptr;
    topn.push(new std::string(absl::StrCat(i)), &dropped);
    delete dropped;
  }

  std::vector<std::string*> extract = topn.Take();
  STLDeleteElements(&extract);
}

struct PointeeGreater {
  template <typename T>
  bool operator()(const T& a, const T& b) const {
    return *a > *b;
  }
};

TEST(TopNTest, MoveOnly) {
  using StrPtr = std::unique_ptr<std::string>;
  TopN<StrPtr, PointeeGreater> topn(3);
  for (int i = 0; i < 8; ++i)
    topn.push(std::make_unique<std::string>(absl::StrCat(i)));
  for (int i = 8; i > 0; --i)
    topn.push(std::make_unique<std::string>(absl::StrCat(i)));

  std::vector<StrPtr> extract = topn.Take();
  EXPECT_THAT(extract, ElementsAre(Pointee(Eq("8")), Pointee(Eq("7")),
                                   Pointee(Eq("7"))));
}

// Test that Nondestructive extracts do not need a Reset() afterwards,
// and that pointers aren't leaked from a TopN after calling them.
TEST(TopNTest, Nondestructive) {
  LOG(INFO) << "Testing Nondestructive extracts";
  TopN<int> top4(4);
  for (int i = 0; i < 8; ++i) {
    top4.push(i);
    std::vector<int> v = top4.TakeNondestructive();
    EXPECT_EQ(std::min(i + 1, 4), v.size());
    for (int j = 0; j < v.size(); ++j) EXPECT_EQ(i - j, v[j]);
  }

  TopN<int> top3(3);
  for (int i = 0; i < 8; ++i) {
    top3.push(i);
    std::vector<int> v = top3.TakeUnsortedNondestructive();
    std::sort(v.begin(), v.end(), std::greater<int>());
    EXPECT_EQ(std::min(i + 1, 3), v.size());
    for (int j = 0; j < v.size(); ++j) EXPECT_EQ(i - j, v[j]);
  }
}

struct ForbiddenCmp {
  bool operator()(int lhs, int rhs) const {
    LOG(FATAL) << "ForbiddenCmp called " << lhs << " " << rhs;
  }
};

TEST(TopNTest, ZeroLimit) {
  TopN<int, ForbiddenCmp> top(0);
  top.push(1);
  top.push(2);

  int dropped = -1;
  top.push(1, &dropped);
  top.push(2, &dropped);

  std::vector<int> v;
  top.ExtractNondestructive(&v);
  EXPECT_EQ(0, v.size());
}

TEST(TopNTest, Iteration) {
  TopN<int> top(4);
  for (int i = 0; i < 8; ++i) top.push(i);
  std::vector<int> actual(top.unsorted_begin(), top.unsorted_end());
  EXPECT_THAT(actual, UnorderedElementsAre(4, 5, 6, 7));
}

TEST(TopNTest, ValueType) {
  static_assert(std::is_same<TopN<int>::value_type, int>(), "");
}

TEST(TopNTest, Comparator) {
  TopN<int> top(4);
  EXPECT_TRUE(top.key_comp()(2, 1));
}

// This comparator gets invalidated on move. Enables to verify that TopN always
// copies comparators on move.
struct MoveableComparator {
  MoveableComparator() = default;
  MoveableComparator(const MoveableComparator& other) = default;
  MoveableComparator& operator=(const MoveableComparator& other) = default;

  // Invalidate comparator on move.
  MoveableComparator(MoveableComparator&& other) { other.was_moved_out = true; }
  MoveableComparator& operator=(MoveableComparator&& other) {
    other.was_moved_out = true;
    return *this;
  }

  bool operator()(int a, int b) const {
    EXPECT_FALSE(was_moved_out);
    return a < b;
  }
  bool was_moved_out = false;
};

TEST(TopNTest, UsableAfterMoveForACopyableComparator) {
  // Create and fill an initial topN.
  TopN<int, MoveableComparator> top(2);
  top.push(1);
  top.push(2);
  top.push(3);
  EXPECT_FALSE(top.empty());

  // Move it.
  TopN<int, MoveableComparator> top2 = std::move(top);
  EXPECT_TRUE(top.empty());
  EXPECT_FALSE(top2.empty());

  // Refill once again and compare.
  top.push(1);
  top.push(2);
  top.push(3);
  EXPECT_EQ(top.TakeNondestructive(), top2.TakeNondestructive());

  // Add some more elements in both and compare.
  top.push(4);
  top2.push(4);
  EXPECT_EQ(top.TakeNondestructive(), top2.TakeNondestructive());

  // Reset top2 and move-assign into it, to test move assignment operator.
  top2.Reset();
  top2 = std::move(top);
  EXPECT_TRUE(top.empty());
  EXPECT_FALSE(top2.empty());

  // Refill once again and compare.
  top.push(1);
  top.push(2);
  top.push(3);
  top.push(4);
  EXPECT_EQ(top.TakeNondestructive(), top2.TakeNondestructive());
}

struct ParameterizedComparator {
  explicit ParameterizedComparator() = delete;
  explicit ParameterizedComparator(bool order) : order(order) {}

  bool operator()(int a, int b) { return order ? a < b : a > b; }

  bool order = false;
};

TEST(TopNTest, MakeTopN) {
  auto top = MakeTopN<int>(3, ParameterizedComparator(true));
  static_assert(
      std::is_same_v<decltype(top), TopN<int, ParameterizedComparator>>);

  top.push(1);
  top.push(2);
  top.push(3);
  top.push(4);
  EXPECT_THAT(top.Take(), ElementsAre(1, 2, 3));
}

}  // namespace
}  // namespace gtl
