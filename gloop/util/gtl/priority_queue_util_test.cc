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

#include "gloop/util/gtl/priority_queue_util.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <numeric>
#include <queue>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "benchmark/benchmark.h"
#include "gloop/util/gtl/comparator.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace gtl {
namespace {

using testing::ElementsAre;
using testing::ElementsAreArray;
using testing::Pointee;

TEST(PriorityQueueTest, ConsumeTop) {
  using IntPtr = std::unique_ptr<int>;
  struct Comp {
    bool operator()(const IntPtr& lhs, const IntPtr& rhs) const {
      return *lhs < *rhs;
    }
  };
  std::priority_queue<IntPtr, std::vector<IntPtr>, Comp> queue;
  for (int i = 0; i < 10; ++i) {
    queue.emplace(new int(i));
  }

  for (int i = 9; i >= 0; --i) {
    EXPECT_EQ(i, *queue.top());
    EXPECT_EQ(i + 1, queue.size());
    auto t = ConsumeTop(&queue);
    EXPECT_EQ(i, *t);
    EXPECT_EQ(i, queue.size());
  }
}

class HeapRootPushTest : public testing::Test {
 protected:
  std::vector<int> RootPushed(std::vector<int> x) {
    push_root_heap(x.begin(), x.end(), std::greater<int>());
    return x;
  }
};

TEST_F(HeapRootPushTest, HeapInProperOrderTest) {
  EXPECT_THAT(RootPushed({1, 2, 3}), ElementsAre(1, 2, 3));
}

TEST_F(HeapRootPushTest, GoingLeftOnceTest) {
  EXPECT_THAT(RootPushed({2, 1, 3}), ElementsAre(1, 2, 3));
}

TEST_F(HeapRootPushTest, GoingRightOnceTest) {
  EXPECT_THAT(RootPushed({2, 3, 1}), ElementsAre(1, 3, 2));
}

TEST_F(HeapRootPushTest, GoingLeftMultipleTimesTest) {
  EXPECT_THAT(RootPushed({10, 1, 2, 3, 4, 5, 6, 7}),
              ElementsAre(1, 3, 2, 7, 4, 5, 6, 10));
}

TEST_F(HeapRootPushTest, GoingRightMultipleTimesTest) {
  EXPECT_THAT(RootPushed({11, 2, 1, 3, 4, 6, 5, 7, 8, 10, 9}),
              ElementsAreArray({1, 2, 5, 3, 4, 6, 11, 7, 8, 10, 9}));
}

TEST_F(HeapRootPushTest, GoingLeftThenRightTest) {
  EXPECT_THAT(RootPushed({10, 1, 2, 4, 3, 5, 6, 7}),
              ElementsAre(1, 3, 2, 4, 10, 5, 6, 7));
}

TEST_F(HeapRootPushTest, GoingRightThenLeftTest) {
  EXPECT_THAT(
      RootPushed({15, 2, 1, 3, 4, 6, 5, 7, 8, 10, 9, 11, 12, 14, 13}),
      ElementsAreArray({1, 2, 5, 3, 4, 6, 13, 7, 8, 10, 9, 11, 12, 14, 15}));
}

TEST_F(HeapRootPushTest, LargeTreeTest) {
  std::vector<int> a;
  for (int i = 0; i < 100; ++i) a.push_back(i);
  std::vector<int> expected_a = a;
  // We'll watch this disturbance propagate into position.
  a[0] = 100;
  // Always going left
  for (const auto& p : std::vector<std::vector<int>>{
           {0, 1}, {1, 3}, {3, 7}, {7, 15}, {15, 31}, {31, 63}, {63, 100}}) {
    expected_a[p[0]] = p[1];
  }
  EXPECT_THAT(RootPushed(a), ElementsAreArray(expected_a));
}

class PushDownHeapTest : public testing::Test {
 protected:
  std::vector<int> PushedDown(size_t hole, std::vector<int> x) {
    push_down_heap(hole, x.begin(), x.end(), std::greater<int>());
    return x;
  }
};

TEST_F(PushDownHeapTest, HeapInProperOrder) {
  EXPECT_THAT(PushedDown(1, {1, 2, 3, 4, 5}), ElementsAre(1, 2, 3, 4, 5));
}

TEST_F(PushDownHeapTest, GoingLeftOnce) {
  EXPECT_THAT(PushedDown(1, {1, 3, 2, 2}), ElementsAre(1, 2, 2, 3));
}

TEST_F(PushDownHeapTest, GoingRightOnce) {
  EXPECT_THAT(PushedDown(1, {1, 3, 2, 1}), ElementsAre(1, 1, 2, 3));
}

TEST_F(PushDownHeapTest, GoingLeftMultipleTimes) {
  EXPECT_THAT(PushedDown(1, {1, 10, 2, 3, 4, 5, 6, 7}),
              ElementsAre(1, 3, 2, 7, 4, 5, 6, 10));
}

TEST_F(PushDownHeapTest, GoingRightMultipleTimes) {
  EXPECT_THAT(PushedDown(2, {1, 2, 11, 3, 4, 6, 5, 7, 8, 10, 9}),
              ElementsAreArray({1, 2, 5, 3, 4, 6, 11, 7, 8, 10, 9}));
}

TEST_F(PushDownHeapTest, GoingLeftThenRight) {
  EXPECT_THAT(PushedDown(0, {10, 1, 2, 4, 3, 5, 6, 7}),
              ElementsAre(1, 3, 2, 4, 10, 5, 6, 7));
}

TEST_F(PushDownHeapTest, GoingRightThenLeft) {
  EXPECT_THAT(
      PushedDown(0, {15, 2, 1, 3, 4, 6, 5, 7, 8, 10, 9, 11, 12, 14, 13}),
      ElementsAreArray({1, 2, 5, 3, 4, 6, 13, 7, 8, 10, 9, 11, 12, 14, 15}));
}

TEST_F(PushDownHeapTest, LargeTree) {
  std::vector<int> a;
  for (int i = 0; i < 100; ++i) {
    a.push_back(i);
  }
  std::vector<int> expected_a = a;
  // We'll watch this disturbance propagate into position.
  a[0] = 100;
  // Always going left
  for (const auto& p : std::vector<std::vector<int>>{
           {0, 1}, {1, 3}, {3, 7}, {7, 15}, {15, 31}, {31, 63}, {63, 100}}) {
    expected_a[p[0]] = p[1];
  }
  EXPECT_THAT(PushedDown(0, a), ElementsAreArray(expected_a));
}

class PushDownHeapUniquePtrTest : public testing::Test {
 public:
  std::vector<std::unique_ptr<int>> ToUniquePtrVec(std::vector<int> x) const {
    std::vector<std::unique_ptr<int>> r;
    for (int i : x) r.emplace_back(new int(i));
    return r;
  }

  std::vector<testing::Matcher<const std::unique_ptr<int>&>>
  ToUniquePtrMatcherVec(std::vector<int> x) const {
    std::vector<testing::Matcher<const std::unique_ptr<int>&>> r;
    for (int i : x) r.push_back(Pointee(i));
    return r;
  }

  std::vector<std::unique_ptr<int>> PushedDown(size_t hole,
                                               std::vector<int> x) const {
    auto upx = ToUniquePtrVec(x);
    push_down_heap(hole, upx.begin(), upx.end(),
                   gtl::OrderByPointee(std::greater<int>()));
    return upx;
  }

  auto VecMatcher(std::vector<int> v)
      -> decltype(ElementsAreArray(ToUniquePtrMatcherVec(v))) {
    return ElementsAreArray(ToUniquePtrMatcherVec(v));
  }
};

TEST_F(PushDownHeapUniquePtrTest, UniquePtr) {
  // push_down_heap should work with move-only elements.
  EXPECT_THAT(PushedDown(1, {1, 10, 2, 3, 4, 5, 6, 7}),
              VecMatcher({1, 3, 2, 7, 4, 5, 6, 10}));
  EXPECT_THAT(PushedDown(1, {1, 2, 3, 4, 5}), VecMatcher({1, 2, 3, 4, 5}));
  EXPECT_THAT(PushedDown(1, {1, 3, 2, 2}), VecMatcher({1, 2, 2, 3}));
  EXPECT_THAT(PushedDown(1, {1, 3, 2, 1}), VecMatcher({1, 1, 2, 3}));
  EXPECT_THAT(PushedDown(1, {1, 10, 2, 3, 4, 5, 6, 7}),
              VecMatcher({1, 3, 2, 7, 4, 5, 6, 10}));
  EXPECT_THAT(PushedDown(2, {1, 2, 11, 3, 4, 6, 5, 7, 8, 10, 9}),
              VecMatcher({1, 2, 5, 3, 4, 6, 11, 7, 8, 10, 9}));
  EXPECT_THAT(PushedDown(0, {10, 1, 2, 4, 3, 5, 6, 7}),
              VecMatcher({1, 3, 2, 4, 10, 5, 6, 7}));
  EXPECT_THAT(
      PushedDown(0, {15, 2, 1, 3, 4, 6, 5, 7, 8, 10, 9, 11, 12, 14, 13}),
      VecMatcher({1, 2, 5, 3, 4, 6, 13, 7, 8, 10, 9, 11, 12, 14, 15}));
}

std::vector<int> TopOf(std::vector<int> heap) {
  std::make_heap(heap.begin(), heap.end());
  std::vector<size_t> top;
  get_all_top_heap(heap.begin(), heap.end(), std::equal_to<int>(), &top);
  CHECK_LE(top.size(), heap.size());
  for (size_t i = 0; i != top.size(); ++i) {
    CHECK_LE(i, top[i]);
    heap[i] = heap[top[i]];
  }
  heap.resize(top.size());
  return heap;
}

TEST(GetAllTopTest, One) { EXPECT_THAT(TopOf({1}), ElementsAre(1)); }

TEST(GetAllTopTest, Simple) {
  EXPECT_THAT(TopOf({1, 2, 3, 4}), ElementsAre(4));
}

TEST(GetAllTopTest, Dups) {
  EXPECT_THAT(TopOf({1, 4, 4, 4}), ElementsAre(4, 4, 4));
}

TEST(GetAllTopTest, LotsOfDups) {
  std::vector<int> h(567);
  std::iota(h.begin(), h.end(), 0);
  h.resize(1000, 1000);
  EXPECT_THAT(TopOf(h), ElementsAreArray(h.begin() + 567, h.end()));
}

namespace bench {

static const int kMaxSize = 4 << 10;

template <typename Element, typename Generator>
void PushDownImpl(benchmark::State& state, int vec_size,
                  const Generator& make_element) {
  auto cmp = std::greater<Element>();
  std::vector<std::vector<Element>> tmp_reservoir;
  for (auto s : state) {
    if (tmp_reservoir.empty()) {
      state.PauseTiming();
      std::vector<Element> src;
      for (int i = 0; i < vec_size; ++i) {
        src.push_back(make_element(i));
      }
      if (src.size() > 1) {
        using std::swap;
        swap(src.front(), src.back());
      }
      tmp_reservoir.resize(100, src);
      state.ResumeTiming();
    }
    auto& tmp = tmp_reservoir.back();
    push_root_heap(tmp.begin(), tmp.end(), cmp);
    tmp_reservoir.pop_back();
  }
}

void BM_PushDownInt(benchmark::State& state) {
  const int vec_size = state.range(0);

  PushDownImpl<int>(state, vec_size, [](int i) { return i; });
}
BENCHMARK(BM_PushDownInt)->Range(1, kMaxSize);

void BM_PushDownString(benchmark::State& state) {
  const int vec_size = state.range(0);

  PushDownImpl<std::string>(
      state, vec_size, [](int i) { return absl::StrFormat("%0*d", 30, i); });
}
BENCHMARK(BM_PushDownString)->Range(1, kMaxSize);

}  // namespace bench

}  // namespace
}  // namespace gtl
