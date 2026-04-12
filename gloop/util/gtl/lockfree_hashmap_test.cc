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

#include "gloop/util/gtl/lockfree_hashmap.h"

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstddef>
#include <cstdint>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-W#warnings"
#include <ext/__hash>
#include <ext/hash_map>
#pragma clang diagnostic pop
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/casts.h"
#include "absl/base/dynamic_annotations.h"
#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/sysinfo.h"
#include "gloop/gloop_test.h"
#include "gloop/util/gtl/extend/equality.h"
#include "gloop/util/gtl/extend/extend.h"
#include "gloop/util/hash/transparent_hash.h"
#include "gloop/util/random/acmrandom.h"

using __gnu_cxx::hash;

namespace gtl {
namespace {

using ::testing::Gt;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

using HashMap = LockFreeHashMap<int, int>;

TEST(AppendonlyHashTest, Empty) {
  HashMap h;
  EXPECT_EQ(h.size(), 0);
  EXPECT_TRUE(h.find(0) == h.end());
  EXPECT_TRUE(h.begin() == h.end());

  const HashMap& ch = h;
  EXPECT_TRUE(ch.find(0) == ch.end());
  EXPECT_TRUE(ch.begin() == ch.end());
}

void CheckHashContents(HashMap* h, int key, int expected_value) {
  HashMap::iterator iter = h->find(key);
  ASSERT_TRUE(iter != h->end());
  EXPECT_EQ(iter->first, key);
  EXPECT_EQ(iter->second, expected_value);
}

struct MyHash {
  int state;
  size_t operator()(int x) const { return 0; }
};
struct MyEq {
  int state;
  bool operator()(int, int) const { return false; }
};

TEST(LockFreeHashTest, FunctorAccess) {
  MyHash my_hash = {123};
  MyEq my_eq = {456};
  LockFreeHashMap<int, int, MyHash, MyEq> h(8, my_hash, my_eq);
  EXPECT_EQ(h.find(0), h.end());
  EXPECT_EQ(my_hash.state, h.hash_function().state);
  EXPECT_EQ(my_eq.state, h.key_eq().state);
}

TEST(LockFreeHashTest, SimpleLValue) {
  HashMap h;
  auto e1 = std::make_pair(1, 1001);
  EXPECT_TRUE(h.insert(e1).second);
  EXPECT_EQ(h.size(), 1);
  auto e2 = std::make_pair(2, 1002);
  EXPECT_TRUE(h.insert(e2).second);
  EXPECT_EQ(h.size(), 2);
  CheckHashContents(&h, 1, 1001);
  CheckHashContents(&h, 2, 1002);
}

TEST(LockFreeHashTest, SimpleRValue) {
  HashMap h;
  EXPECT_TRUE(h.insert(std::make_pair(1, 1001)).second);
  EXPECT_EQ(h.size(), 1);
  EXPECT_TRUE(h.insert(std::make_pair(2, 1002)).second);
  EXPECT_EQ(h.size(), 2);
  CheckHashContents(&h, 1, 1001);
  CheckHashContents(&h, 2, 1002);
}

TEST(LockFreeHashTest, DuplicateKey) {
  HashMap h;
  EXPECT_TRUE(h.insert(std::make_pair(1, 1001)).second);
  EXPECT_EQ(h.size(), 1);
  EXPECT_FALSE(h.insert(std::make_pair(1, 2001)).second);
  EXPECT_EQ(h.size(), 1);
  CheckHashContents(&h, 1, 1001);
  EXPECT_EQ(h.erase(1), 1);
  EXPECT_TRUE(h.empty());
}

TEST(LockFreeHashTest, SquareBracketsOperatorRValue) {
  HashMap h;
  EXPECT_TRUE(h.empty());
  EXPECT_EQ(0, h[1]);
  EXPECT_EQ(h.size(), 1);
  h[1] = 25;
  EXPECT_EQ(25, h[1]);
  EXPECT_EQ(h.size(), 1);
  EXPECT_EQ(h.erase(1), 1);
  EXPECT_TRUE(h.empty());
}

TEST(LockFreeHashTest, SquareBracketsOperatorLValue) {
  HashMap h;
  int index = 1;
  EXPECT_TRUE(h.empty());
  EXPECT_EQ(0, h[index]);
  EXPECT_EQ(h.size(), 1);
  h[index] = 25;
  EXPECT_EQ(25, h[index]);
  EXPECT_EQ(h.size(), 1);
  EXPECT_EQ(h.erase(1), 1);
  EXPECT_TRUE(h.empty());
}

TEST(LockFreeHashTest, Iter) {
  HashMap h;
  h[1] = 11;
  h[2] = 22;
  h[3] = 33;

  std::vector<HashMap::value_type> elements;
  elements.reserve(h.size());

  HashMap::iterator it(h.begin());
  EXPECT_TRUE(it == h.begin());
  ASSERT_FALSE(it == h.end());
  elements.push_back(*it);
  ASSERT_FALSE(it == h.end());
  ++it;
  elements.push_back(*it);
  ASSERT_FALSE(it == h.end());
  EXPECT_EQ(*it++, elements.back());
  ASSERT_FALSE(it == h.end());
  elements.push_back(*it++);
  EXPECT_TRUE(it == h.end());

  EXPECT_THAT(elements,
              UnorderedElementsAre(Pair(1, 11), Pair(2, 22), Pair(3, 33)));
}

TEST(LockFreeHashTest, ConstIter) {
  HashMap h;
  h[1] = 11;
  h[2] = 22;
  h[3] = 33;

  std::vector<HashMap::value_type> elements;
  elements.reserve(h.size());

  HashMap::const_iterator it(absl::implicit_cast<const HashMap&>(h).begin());
  EXPECT_TRUE(it == h.begin());
  ASSERT_FALSE(it == h.end());
  elements.push_back(*it);
  ASSERT_FALSE(it == h.end());
  ++it;
  elements.push_back(*it);
  ASSERT_FALSE(it == h.end());
  EXPECT_EQ(*it++, elements.back());
  ASSERT_FALSE(it == h.end());
  elements.push_back(*it++);
  EXPECT_TRUE(it == h.end());

  EXPECT_THAT(elements,
              UnorderedElementsAre(Pair(1, 11), Pair(2, 22), Pair(3, 33)));
}

TEST(LockFreeHashTest, Erase) {
  HashMap h;
  EXPECT_TRUE(h.insert(std::make_pair(1, 1001)).second);
  EXPECT_FALSE(h.empty());
  EXPECT_EQ(h.size(), 1);
  HashMap::iterator iter = h.find(1);
  ASSERT_TRUE(iter != h.end());
  h.erase(iter);
  EXPECT_EQ(h.size(), 0);
  EXPECT_TRUE(h.empty());
  ASSERT_TRUE(h.find(1) == h.end());
}

TEST(LockFreeHashTest, EraseWithKey) {
  HashMap h;
  EXPECT_TRUE(h.insert(std::make_pair(1, 1001)).second);
  EXPECT_EQ(h.erase(1), 1);
  EXPECT_EQ(h.erase(2), 0);
  EXPECT_TRUE(h.empty());
  ASSERT_TRUE(h.find(1) == h.end());
}

TEST(LockFreeHashTest, Clear) {
  for (int i = 0; i < 1000; ++i) {
    HashMap h;
    for (int j = 0; j < i; ++j) {
      ASSERT_TRUE(h.insert(std::make_pair(j, j + 1000)).second);
    }
    h.clear();
    EXPECT_TRUE(h.empty());
    EXPECT_EQ(h.size(), 0);
    for (int j = 0; j < i; ++j) {
      EXPECT_TRUE(h.find(j) == h.end());
    }
  }
}

TEST(KeyValueDestroyTest, Erase) {
  // Since reads don't lock, keys and values can't simply be destroyed when
  // removed from the map.

  auto key = std::make_shared<int>(0);
  auto value = std::make_shared<int>(0);
  {
    LockFreeHashMap<std::shared_ptr<int>, std::shared_ptr<int>> h;
    h.insert({key, value});

    auto it = h.find(key);
    h.erase(it);

    EXPECT_EQ(h.size(), 0);
    // We didn't destroy anything yet, because a concurrent reader should still
    // be able to use the retrieved objects.
    EXPECT_EQ(key.use_count(), 2);
    EXPECT_EQ(value.use_count(), 2);
  }
  // But we do destroy things in the destructor.
  EXPECT_EQ(key.use_count(), 1);
  EXPECT_EQ(value.use_count(), 1);
}

TEST(KeyValueDestroyTest, EraseWithKey) {
  // Since reads don't lock, keys and values can't simply be destroyed when
  // removed from the map.

  auto key = std::make_shared<int>(0);
  auto value = std::make_shared<int>(0);
  {
    LockFreeHashMap<std::shared_ptr<int>, std::shared_ptr<int>> h;
    h.insert({key, value});

    h.erase(key);

    EXPECT_EQ(h.size(), 0);
    // We didn't destroy anything yet, because a concurrent reader should still
    // be able to use the retrieved objects.
    EXPECT_EQ(key.use_count(), 2);
    EXPECT_EQ(value.use_count(), 2);
  }
  // But we do destroy things in the destructor.
  EXPECT_EQ(key.use_count(), 1);
  EXPECT_EQ(value.use_count(), 1);
}

TEST(KeyValueDestroyTest, Clear) {
  // Since reads don't lock, keys and values can't simply be destroyed when
  // removed from the map.

  auto key = std::make_shared<int>(0);
  auto value = std::make_shared<int>(0);
  {
    LockFreeHashMap<std::shared_ptr<int>, std::shared_ptr<int>> h;
    h.insert({key, value});

    h.clear();

    EXPECT_EQ(h.size(), 0);
    // We didn't destroy anything yet, because a concurrent reader should still
    // be able to use the retrieved objects.
    EXPECT_EQ(key.use_count(), 2);
    EXPECT_EQ(value.use_count(), 2);
  }
  // But we do destroy things in the destructor.
  EXPECT_EQ(key.use_count(), 1);
  EXPECT_EQ(value.use_count(), 1);
}

TEST(LockFreeHashTest, Emplace) {
  LockFreeHashMap<int, std::atomic<int>> data;
  EXPECT_TRUE(data.emplace(std::piecewise_construct, std::forward_as_tuple(1),
                           std::forward_as_tuple(5))
                  .second);
  auto emplace_ret =
      data.emplace(std::piecewise_construct, std::forward_as_tuple(1),
                   std::forward_as_tuple(3));
  EXPECT_FALSE(emplace_ret.second);
  EXPECT_EQ(5, emplace_ret.first->second.load(std::memory_order_relaxed));
  EXPECT_EQ(5, data.find(1)->second.load(std::memory_order_relaxed));
}

TEST(LockFreeHashTest, EmplaceMoveOnly) {
  LockFreeHashMap<int, std::unique_ptr<int>> data;
  data.emplace(1, std::make_unique<int>(7));
  EXPECT_THAT(data.find(1)->second, testing::Pointee(7));
}

TEST(LockFreeHashTest, TryEmplace) {
  LockFreeHashMap<int, std::atomic<int>> data;
  EXPECT_TRUE(data.try_emplace(1, 5).second);
  auto emplace_ret = data.try_emplace(1, 3);
  EXPECT_FALSE(emplace_ret.second);
  EXPECT_EQ(5, emplace_ret.first->second.load(std::memory_order_relaxed));
  EXPECT_EQ(5, data.find(1)->second.load(std::memory_order_relaxed));
}

TEST(LockFreeHashTest, TryEmplaceMoveOnly) {
  LockFreeHashMap<int, std::unique_ptr<int>> data;
  data.try_emplace(1, new int(7));
  EXPECT_THAT(data.find(1)->second, testing::Pointee(7));
}

TEST(LockFreeHashTest, TryEmplaceAllocCounts) {
  // Verifies that calls to try_emplace do not create nodes when they are not
  // doing inserts.
  struct CtorCounter {
    explicit CtorCounter(int* n) { ++*n; }

    // Ensures that users can only construct these with side effects.
    CtorCounter(CtorCounter&&) = delete;
  };
  int ctor_calls = 0;
  LockFreeHashMap<int, CtorCounter> data;
  EXPECT_TRUE(data.try_emplace(1, &ctor_calls).second);
  EXPECT_EQ(ctor_calls, 1);
  EXPECT_FALSE(data.try_emplace(1, &ctor_calls).second);
  EXPECT_EQ(ctor_calls, 1);
}

// IntWrapper wraps an int but is not implicitly convertible.
struct IntWrapper {
  IntWrapper() : val(0) {}
  explicit IntWrapper(int v) : val(v) {}

  bool operator==(const IntWrapper& x) const { return val == x.val; }

  bool operator==(int x) const { return val == x; }

  int val;
};

// A transparent hasher for int/IntWrapper.
struct IntWrapperHasher {
  using is_transparent = void;

  size_t operator()(int x) const { return hash<int>()(x); }
  size_t operator()(const IntWrapper& x) const { return hash<int>()(x.val); }
};

TEST(LockFreeHashTest, HeterogeneousFind) {
  LockFreeHashMap<IntWrapper, int, IntWrapperHasher, util_hash::TransparentEq>
      data;

  data[IntWrapper(1)] = 2;
  data[IntWrapper(3)] = 4;

  EXPECT_TRUE(data.find(1) != data.end());
  EXPECT_EQ(1, data.find(1)->first.val);
  EXPECT_TRUE(data.find(2) == data.end());
  EXPECT_TRUE(data.find(3) != data.end());
  EXPECT_EQ(3, data.find(3)->first.val);
}

TEST(LockFreeHashTest, HeterogeneousErase) {
  LockFreeHashMap<IntWrapper, int, IntWrapperHasher, util_hash::TransparentEq>
      data;

  data[IntWrapper(1)] = 2;
  data.erase(1);

  EXPECT_TRUE(data.find(IntWrapper(1)) == data.end());
}

TEST(LockFreeHashTest, HeterogeneousEqualRange) {
  LockFreeHashMap<IntWrapper, int, IntWrapperHasher, util_hash::TransparentEq>
      data;

  data[IntWrapper(1)] = 2;

  auto range = data.equal_range(1);
  EXPECT_TRUE(range.first != range.second);
  EXPECT_TRUE(range.first != data.end());
  EXPECT_EQ(1, range.first->first.val);

  range = data.equal_range(0);
  EXPECT_TRUE(range.first == range.second);
  EXPECT_TRUE(range.first == data.end());
}

// Friend function cannot be defined in a local class.
struct MyKey : gtl::Extend<MyKey>::With<gtl::EqualityExtension> {
  int i;
};

TEST(LockFreeHashTest, WorksWithAbslHashOnly) {
  LockFreeHashMap<MyKey, int> h;
  h[{.i = 1}] = 11;
  h[{.i = 2}] = 22;
  h[{.i = 3}] = 33;

  EXPECT_THAT(
      h, UnorderedElementsAre(Pair(MyKey{.i = 1}, 11), Pair(MyKey{.i = 2}, 22),
                              Pair(MyKey{.i = 3}, 33)));
}

TEST(LockFreeHashTest, IteratorHasNonConstValue) {
  static_assert(
      std::is_same_v<HashMap::iterator::reference, std::pair<const int, int>&>,
      "Iterator reference type must not be const");
  static_assert(std::is_same_v<decltype(HashMap().begin()->first), const int>,
                "Key must be const");
  static_assert(std::is_same_v<decltype(HashMap().begin()->second), int>,
                "Value must be modifiable");
}

struct BenchmarkContext {
  HashMap h ABSL_CACHELINE_ALIGNED;
};
BenchmarkContext* g_bm_context = nullptr;

void BenchmarkSetup(const benchmark::State&) {
  g_bm_context = new BenchmarkContext();
}
void BenchmarkTeardown(const benchmark::State&) {
  delete g_bm_context;
  g_bm_context = nullptr;
}

// Do random insertions of values up to max_key.
void BM_ConcurrentInsertions(benchmark::State& state) {
  const int max_key = state.range(0);

  absl::InsecureBitGen arand;
  HashMap* h = &g_bm_context->h;
  CHECK(h->empty());
  for (auto s : state) {
    const int key = absl::Uniform<int32_t>(arand, 0, max_key);
    h->insert(std::make_pair(key, 1));
  }
}
BENCHMARK(BM_ConcurrentInsertions)
    ->Setup(BenchmarkSetup)
    ->Teardown(BenchmarkTeardown)
    ->ThreadRange(1, NumCPUs())
    ->Range(1, 1024);

// Insert max_key / 10 values in the range [0..max_values), then do random
// lookups of values in that range.
void BM_ConcurrentLookups(benchmark::State& state) {
  const int max_key = state.range(0);

  absl::InsecureBitGen arand;
  HashMap* h = &g_bm_context->h;

  // Only initialize the map from a single thread. Otherwise, higher thread
  // counts running this benchmark would result in a higher fill rate, instead
  // of the intended ~10%.
  if (state.thread_index() == 0) {
    CHECK(h->empty());
    const int intended_count = std::max(1, max_key / 10);
    while (h->size() < intended_count) {
      const int key = absl::Uniform<int32_t>(arand, 0, max_key);
      h->insert(std::make_pair(key, 1));
    }
  }
  for (auto s : state) {
    const int key = absl::Uniform<int32_t>(arand, 0, max_key);
    auto it = h->find(key);
    if (it != h->end()) {
      benchmark::DoNotOptimize(it->second);
    }
  }
}
BENCHMARK(BM_ConcurrentLookups)
    ->Setup(BenchmarkSetup)
    ->Teardown(BenchmarkTeardown)
    ->ThreadRange(1, NumCPUs())
    ->Range(1, 1024);

}  // namespace
}  // namespace gtl
