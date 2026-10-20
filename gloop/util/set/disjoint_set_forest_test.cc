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

#include "gloop/util/set/disjoint_set_forest.h"

#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/hash/hash.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "gtest/gtest.h"

// Define this at global scope so that it can be named by DisjointSetForest's
// friend declaration.
template <typename T, typename HashFcn, typename EqualKey, typename Alloc>
void InvokeUnionManyOld(
    const std::vector<T>& data,
    DisjointSetForest<T, HashFcn, EqualKey, Alloc>& forest) {
  forest.UnionManyOld(data);
}

namespace {

class CopyTrackingInt {
 public:
  explicit CopyTrackingInt(int x, int* absl_nonnull copy_constructor_calls)
      : x_(x), copy_constructor_calls_(copy_constructor_calls) {}

  CopyTrackingInt(const CopyTrackingInt& other)
      : x_(other.x_), copy_constructor_calls_(other.copy_constructor_calls_) {
    ++(*copy_constructor_calls_);
  }
  CopyTrackingInt& operator=(const CopyTrackingInt& other) = default;

  bool operator==(const CopyTrackingInt& other) const { return x_ == other.x_; }
  template <typename H>
  friend H AbslHashValue(H h, const CopyTrackingInt& my_type) {
    return H::combine(std::move(h), my_type.x_);
  }

 private:
  int x_;
  int* absl_nonnull copy_constructor_calls_;
};

TEST(DisjointSetForestTest, TestUnionOnConnectedComponentsDoesNotCopyKeys) {
  int copy_constructor_calls = 0;
  DisjointSetForest<CopyTrackingInt> forest(
      CopyTrackingInt(-1, &copy_constructor_calls));
  forest.Union(CopyTrackingInt(1, &copy_constructor_calls),
               CopyTrackingInt(2, &copy_constructor_calls));
  int old_copy_constructor_calls = copy_constructor_calls;

  forest.Union(CopyTrackingInt(1, &copy_constructor_calls),
               CopyTrackingInt(2, &copy_constructor_calls));

  EXPECT_EQ(copy_constructor_calls, old_copy_constructor_calls);
}

TEST(DisjointSetForestTest, TestHeterogeneousLookups) {
  DisjointSetForest<std::string> forest("not found");
  absl::string_view set1 = "set 1";
  absl::string_view set2 = "set 2";
  forest.MakeSet(set1);
  EXPECT_TRUE(forest.IsSingleton(set1));
  EXPECT_EQ(forest.FindSet(set2), "not found");
  forest.Union(set1, set2);
  EXPECT_EQ(forest.FindSet(std::string("set 1")),
            forest.FindSet(std::string("set 2")));
  EXPECT_FALSE(forest.IsSingleton(set1));
  EXPECT_FALSE(forest.IsSingleton(set2));
}

typedef DisjointSetForest<int> TestForest;

TEST(DisjointSetForestTest, TestIsSingleton) {
  TestForest forest(-1);
  EXPECT_TRUE(forest.IsSingleton(1));
}

TEST(DisjointSetForestTest, TestUnionWithoutMakeSet) {
  TestForest forest(-1);

  // Union the first 10 elements.
  for (int i = 0; i < 9; ++i) {
    EXPECT_TRUE(forest.Union(i, i + 1));
  }
  // This should be a no-op.
  EXPECT_FALSE(forest.Union(2, 5));
  // Union the next 10 elements.
  for (int i = 10; i < 19; ++i) {
    EXPECT_TRUE(forest.Union(i, i + 1));
  }
  // We should have 20 elements in two disjoint sets.
  EXPECT_EQ(2, forest.num_disjoint_sets());
  EXPECT_EQ(20, forest.num_objects());
  // Union two disjoint sets.
  EXPECT_TRUE(forest.Union(7, 15));
  // The second union attempt becomes a no-op.
  EXPECT_FALSE(forest.Union(3, 12));
  EXPECT_EQ(1, forest.num_disjoint_sets());  // One disjoint set
  EXPECT_EQ(20, forest.num_objects());       // Still 20 elements.
}

TEST(DisjointSetForestTest, BasicTest) {
  TestForest forest(-1);

  // Create 100 sets.
  for (int i = 0; i < 100; i++) {
    forest.MakeSet(i);
  }

  // Union the first 25 sets via Union().
  for (int i = 0; i < 24; i++) {
    EXPECT_TRUE(forest.Union(i + 1, i));
  }

  // Union the next 25 sets via UnionMany().
  std::vector<int> union_elements;
  for (int i = 25; i < 50; i++) {
    union_elements.push_back(i);
  }

  forest.UnionMany(union_elements);

  // Union two size 25 sets.
  EXPECT_TRUE(forest.Union(15, 40));
  // Does nothing.  The 50 elements have been in the same set.
  EXPECT_FALSE(forest.Union(0, 49));

  // Check that unions have resulted in the same representative for
  // each member of a set.
  for (int i = 0; i < 49; i++) {
    EXPECT_EQ(forest.FindSet(i), forest.FindSet(i + 1));
  }

  // Check that for non-unioned members, all their keys are distinct.
  for (int i = 51; i < 100; i++) {
    for (int j = 0; j < 100; j++) {
      if (i != j) {
        EXPECT_NE(forest.FindSet(i), forest.FindSet(j));
      }
    }
  }

  // Check that IsSingleton() has the right values.
  for (int i = 0; i < 50; i++) EXPECT_FALSE(forest.IsSingleton(i));
  for (int i = 50; i < 100; i++) EXPECT_TRUE(forest.IsSingleton(i));
}

TEST(DisjointSetForestTest, CopyConstructorRemapsParentPointers) {
  auto original = std::make_unique<TestForest>(-1);
  // Build a multi-level tree (rank > 1) and singletons before copying.
  original->Union(0, 1);
  original->Union(2, 3);
  original->Union(0, 2);  // Creates a depth-2 path before path compression.
  original->MakeSet(10);  // Singleton.
  original->MakeSet(20);  // Singleton.

  // Copy-construct before path compression has flattened the tree.
  TestForest copy(*original);

  // Mutating `original` while both forests are alive must not affect `copy`.
  original->Union(10, 0);
  EXPECT_FALSE(original->IsSingleton(10));
  EXPECT_TRUE(copy.IsSingleton(10));
  EXPECT_NE(copy.FindSet(0), copy.FindSet(10));

  // Mutating `copy` while `original` is alive must not affect `original`.
  EXPECT_TRUE(copy.Union(20, 0));
  EXPECT_FALSE(copy.IsSingleton(20));
  EXPECT_TRUE(original->IsSingleton(20));
  EXPECT_NE(original->FindSet(0), original->FindSet(20));

  // Destroy `original` and verify `copy` has no dangling pointers into
  // `original->objects_`.
  original->clear();
  original.reset();

  EXPECT_EQ(2, copy.num_disjoint_sets());
  EXPECT_EQ(6, copy.num_objects());
  EXPECT_EQ(copy.FindSet(0), copy.FindSet(3));
  EXPECT_EQ(copy.FindSet(1), copy.FindSet(2));
  EXPECT_EQ(copy.FindSet(0), copy.FindSet(20));
  EXPECT_TRUE(copy.IsSingleton(10));
  EXPECT_NE(copy.FindSet(0), copy.FindSet(10));

  // Verify further mutations on the copy work properly.
  EXPECT_TRUE(copy.Union(3, 10));
  EXPECT_EQ(1, copy.num_disjoint_sets());
  EXPECT_EQ(copy.FindSet(0), copy.FindSet(10));
}

TEST(DisjointSetForestTest, MoveConstructorPreservesParentPointers) {
  TestForest original(-1);
  original.Union(0, 1);
  original.Union(2, 3);
  original.Union(0, 2);
  original.MakeSet(10);

  TestForest moved(std::move(original));
  EXPECT_EQ(2, moved.num_disjoint_sets());
  EXPECT_EQ(5, moved.num_objects());
  EXPECT_EQ(moved.FindSet(0), moved.FindSet(3));
  EXPECT_EQ(moved.FindSet(1), moved.FindSet(2));
  EXPECT_TRUE(moved.IsSingleton(10));
  EXPECT_NE(moved.FindSet(0), moved.FindSet(10));

  EXPECT_TRUE(moved.Union(3, 10));
  EXPECT_EQ(1, moved.num_disjoint_sets());
  EXPECT_EQ(moved.FindSet(0), moved.FindSet(10));
}

struct NoComparisonOperators {
  int value = (std::numeric_limits<int>::min)();

  // Delete comparison operators to verify the use of the custom EqualKey.
  bool operator==(const NoComparisonOperators&) const = delete;
  bool operator!=(const NoComparisonOperators&) const = delete;

  struct Hash {
    size_t operator()(const NoComparisonOperators& key) const {
      return absl::Hash<int>()(key.value);
    }
  };
  struct Equal {
    bool operator()(const NoComparisonOperators& a,
                    const NoComparisonOperators& b) const {
      return a.value == b.value;
    }
  };
};

TEST(DisjointSetForestTest, CopyConstructorUsesEqualKey) {
  using CustomForest =
      DisjointSetForest<NoComparisonOperators, NoComparisonOperators::Hash,
                        NoComparisonOperators::Equal>;
  CustomForest original({-1});
  NoComparisonOperators a = {1};
  NoComparisonOperators b = {2};
  NoComparisonOperators c = {3};
  EXPECT_TRUE(original.Union(a, b));
  EXPECT_TRUE(original.MakeSet(c));

  CustomForest copy(original);
  EXPECT_EQ(copy.num_disjoint_sets(), 2);
  EXPECT_EQ(copy.FindSet(a).value, copy.FindSet(b).value);
  EXPECT_TRUE(copy.IsSingleton(c));
}

TEST(UnionManyIterator, UsesEqualKey) {
  DisjointSetForest<NoComparisonOperators, NoComparisonOperators::Hash,
                    NoComparisonOperators::Equal>
      forest({-1});

  NoComparisonOperators a = {1};
  NoComparisonOperators b = {2};
  NoComparisonOperators c = {3};
  NoComparisonOperators d = {4};

  // Set 1: {a, b}
  EXPECT_TRUE(forest.Union(a, b));
  // Set 2: {c, d}
  EXPECT_TRUE(forest.Union(c, d));
  EXPECT_EQ(forest.num_disjoint_sets(), 2);

  // Passing {a, b, c} exercises map lookups via EqualKey as well as:
  // - b already has the same parent pointer as a (no union)
  // - c has a different parent pointer from a (union occurs)
  std::vector<NoComparisonOperators> elements = {a, b, c};
  forest.UnionManyIterator(elements.begin(), elements.end());
  EXPECT_EQ(forest.num_disjoint_sets(), 1);
}

TEST(UnionManyOld, UsesEqualKey) {
  DisjointSetForest<NoComparisonOperators, NoComparisonOperators::Hash,
                    NoComparisonOperators::Equal>
      forest({-1});

  NoComparisonOperators a = {1};
  NoComparisonOperators b = {2};
  NoComparisonOperators c = {3};
  NoComparisonOperators d = {4};

  // Set 1: {a, b}
  EXPECT_TRUE(forest.Union(a, b));
  // Set 2: {c, d}
  EXPECT_TRUE(forest.Union(c, d));
  EXPECT_EQ(forest.num_disjoint_sets(), 2);

  // UnionManyOldInvoker::Invoke({a, b, c}, forest);
  InvokeUnionManyOld({a, b, c}, forest);
  EXPECT_EQ(forest.num_disjoint_sets(), 1);
}

// An 'overlap_factor' of N means that there are approximately N repetitions
// of a given integer in the set.
void BenchmarkHelper(benchmark::State& state, int size, int overlap_factor,
                     void (*union_func)(const std::vector<int>& data,
                                        TestForest& forest)) {
  std::vector<int> values;
  for (int i = 0; i < size; ++i) {
    values.push_back(i / overlap_factor);
  }
  for (auto _ : state) {
    DisjointSetForest<int> forest(-1);
    union_func(values, forest);
  }
}

void DoItYourself(const std::vector<int>& data, TestForest& forest) {
  if (data.empty()) {
    return;
  }
  forest.MakeSet(data[0]);
  for (size_t i = 1; i < data.size(); ++i) {
    forest.Union(data[0], data[i]);
  }
}

void InvokeUnionManyNew(const std::vector<int>& data, TestForest& forest) {
  forest.UnionMany(data);
}

void BM_Unique_DoitYourself(benchmark::State& state) {
  int size = state.range(0);
  BenchmarkHelper(state, size, 1, &DoItYourself);
}

void BM_Unique_UnionManyOld(benchmark::State& state) {
  int size = state.range(0);
  BenchmarkHelper(state, size, 1, &InvokeUnionManyOld);
}

void BM_Unique_UnionManyNew(benchmark::State& state) {
  int size = state.range(0);
  BenchmarkHelper(state, size, 1, &InvokeUnionManyNew);
}

void BM_10Reps_DoitYourself(benchmark::State& state) {
  int size = state.range(0);
  BenchmarkHelper(state, size, 10, &DoItYourself);
}

void BM_10Reps_UnionManyOld(benchmark::State& state) {
  int size = state.range(0);
  BenchmarkHelper(state, size, 10, &InvokeUnionManyOld);
}

void BM_10Reps_UnionManyNew(benchmark::State& state) {
  int size = state.range(0);
  BenchmarkHelper(state, size, 10, &InvokeUnionManyNew);
}

BENCHMARK(BM_Unique_DoitYourself)->Range(1, 1 << 20);
BENCHMARK(BM_Unique_UnionManyOld)->Range(1, 1 << 20);
BENCHMARK(BM_Unique_UnionManyNew)->Range(1, 1 << 20);

BENCHMARK(BM_10Reps_DoitYourself)->Range(1, 1 << 20);
BENCHMARK(BM_10Reps_UnionManyOld)->Range(1, 1 << 20);
BENCHMARK(BM_10Reps_UnionManyNew)->Range(1, 1 << 20);

}  // namespace
