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

#include "gloop/util/intervaltree/intervaltree.h"

#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/node_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "benchmark/benchmark.h"
#include "gloop/base/arena.h"
#include "gloop/util/random/acmrandom.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Pointee;

MATCHER_P(NodeValue, inner_matcher, "") {
  if (arg == nullptr) {
    *result_listener << "is nullptr";
    return false;
  }
  return testing::ExplainMatchResult(inner_matcher, arg->value,
                                     result_listener);
}

template <typename Iter>
auto ExtractValues(Iter iter) {
  std::vector<const typename Iter::TreeNode*> nodes;
  while (iter.Get() != nullptr) {
    nodes.push_back(iter.Get());
    iter.Next();
  }
  return nodes;
}

template <typename Iter>
auto ExtractValuesReverse(Iter iter) {
  std::vector<const typename Iter::TreeNode*> nodes;
  while (iter.Get() != nullptr) {
    nodes.push_back(iter.Get());
    iter.Prev();
  }
  return nodes;
}

// Benchmarks
// ------------------------------------------------------------------
std::pair<int32_t, int32_t> GeneratePair(ACMRandom& random, int32_t range) {
  int32_t first = absl::Uniform<uint32_t>(random);
  int32_t last = first + absl::Uniform<int32_t>(random, 0, range);
  return std::make_pair(first < last ? first : last,
                        first < last ? last : first);
}

std::pair<int32_t, int32_t> GeneratePair(ACMRandom& random) {
  int32_t first = absl::Uniform<uint32_t>(random);
  int32_t last = absl::Uniform<uint32_t>(random);
  return std::make_pair(first < last ? first : last,
                        first < last ? last : first);
}

void FillRandom(int n, ACMRandom& random,
                IntervalTree<int32_t, int32_t>& tree) {
  int32_t range = (0x7FFFFFFF / n);
  for (int i = 0; i < n; ++i) {
    std::pair<int32_t, int32_t> p = GeneratePair(random, range);
    tree.InsertVal(p.first, p.second, i);
  }
}

void FillSequential(int n, IntervalTree<int32_t, int32_t>& tree) {
  int32_t range = (0x7FFFFFFF / n);
  int32_t first = 0;
  for (int i = 0; i < n; ++i) {
    int32_t next = first + range;
    tree.InsertVal(first, next, i);
    first = next;
  }
}

void IterateAll(IntervalTree<int32_t, int32_t>& tree) {
  IntervalIterator<int32_t, int32_t> iter(&tree,
                                          std::numeric_limits<int32_t>::min(),
                                          std::numeric_limits<int32_t>::max());
  while (iter.Get() != nullptr) {
    CHECK_GT(iter.Get()->value, -1);
    iter.Next();
  }
}

void IterateRandom(ACMRandom& random, IntervalTree<int32_t, int32_t>& tree) {
  std::pair<int32_t, int32_t> p = GeneratePair(random);
  IntervalIterator<int32_t, int32_t> iter(&tree, p.first, p.second);
  while (iter.Get() != nullptr) {
    CHECK_GT(iter.Get()->value, -1);
    iter.Next();
  }
}

// A Random Insert benchmark
void BM_RandomInsert(benchmark::State& state) {
  ACMRandom grandom(1);
  IntervalTree<int32_t, int32_t> tree;
  int32_t range = (0x7FFFFFFF / state.max_iterations);
  int i = 0;
  for (auto _ : state) {
    std::pair<int32_t, int32_t> p = GeneratePair(grandom, range);
    tree.InsertVal(p.first, p.second, i++);
  }
}
BENCHMARK(BM_RandomInsert);

// A Sequential insert benchmark
void BM_SequentialInsert(benchmark::State& state) {
  IntervalTree<int32_t, int32_t> tree;
  int32_t range = (0x7FFFFFFF / state.max_iterations);
  int i = 0;
  int32_t first = 0;
  for (auto _ : state) {
    int32_t next = first + range;
    tree.InsertVal(first, next, i++);
    first = next;
  }
}
BENCHMARK(BM_SequentialInsert);

// Random insert, random reads.
void BM_RandomInsert_RandomIterators(benchmark::State& state) {
  ACMRandom grandom(1);
  IntervalTree<int32_t, int32_t> tree;
  FillRandom(state.max_iterations, grandom, tree);
  for (auto _ : state) IterateRandom(grandom, tree);
}
BENCHMARK(BM_RandomInsert_RandomIterators);

// Random insert, all reads.
void BM_RandomInsert_AllIterators(benchmark::State& state) {
  ACMRandom grandom(1);
  IntervalTree<int32_t, int32_t> tree;
  FillRandom(state.max_iterations, grandom, tree);
  for (auto _ : state) IterateAll(tree);
}
BENCHMARK(BM_RandomInsert_AllIterators);

// Sequential insert, random reads.
void BM_SequentialInsert_RandomIterators(benchmark::State& state) {
  ACMRandom grandom(1);
  IntervalTree<int32_t, int32_t> tree;
  FillSequential(state.max_iterations, tree);
  for (auto _ : state) IterateRandom(grandom, tree);
}
BENCHMARK(BM_SequentialInsert_RandomIterators);

void BM_RandomDeletions(benchmark::State& state) {
  ACMRandom grandom(1);
  IntervalTree<int32_t, int32_t> tree;
  // Form a reasonably sized initial tree so
  // as to correctly measure the O(n)
  // behavior of the delete operation.
  FillRandom(state.max_iterations, grandom, tree);
  int deletes = 0;
  for (auto _ : state) {
    std::pair<int32_t, int32_t> p = GeneratePair(grandom);
    IntervalIterator<int32_t, int32_t> del(&tree, p.first, p.second,
                                           INTERVAL_SMALLEST);
    if (del.Get() != nullptr) {
      del.Delete();
      deletes++;
    }
  }
  std::string label = absl::StrFormat("d:%d", deletes);
  state.SetLabel(label);
}
BENCHMARK(BM_RandomDeletions);

// Random operations.  Assume INSERT is twice as common as DELETE.
enum { INSERT0, INSERT1, DELETE, ITERATE, ITERATE_ALL };

void BM_RandomOperations(benchmark::State& state) {
  ACMRandom grandom(1);
  IntervalTree<int32_t, int32_t> tree;
  int inserts = 0;
  int deletes = 0;
  int iterate = 0;
  for (auto _ : state) {
    int op = absl::Uniform<int32_t>(grandom, 0, 5);
    switch (op) {
      case INSERT0:
      case INSERT1: {
        std::pair<int32_t, int32_t> p = GeneratePair(grandom);
        tree.InsertVal(p.first, p.second, state.max_iterations);
        inserts++;
      } break;
      case DELETE: {
        std::pair<int32_t, int32_t> p = GeneratePair(grandom);
        IntervalIterator<int32_t, int32_t> del(&tree, p.first, p.second,
                                               INTERVAL_SMALLEST);
        if (del.Get() != nullptr) {
          del.Delete();
          deletes++;
        }
      } break;
      case ITERATE:
        IterateRandom(grandom, tree);
        iterate++;
        break;
      default:
        IterateAll(tree);
        iterate++;
    }
  }
  std::string label =
      absl::StrFormat("u:%d d:%d i:%d", inserts, deletes, iterate);
  state.SetLabel(label);
}
BENCHMARK(BM_RandomOperations);

// ------------------------------------------------------------------
// Unit Tests
// ------------------------------------------------------------------

enum class TestMode {
  kDefaultArena,
  kUserArena,
  kHeapAllocation,
};

class TreeFactory {
 public:
  explicit TreeFactory(TestMode mode) : mode_(mode) {}

  template <typename K, typename V, typename... KeyLess>
  IntervalTree<K, V, KeyLess...> Create(const KeyLess&... less) {
    switch (mode_) {
      case TestMode::kDefaultArena:
        return IntervalTree<K, V, KeyLess...>(less...);
      case TestMode::kUserArena:
        owned_arenas_.emplace_back(sizeof(IntervalTree<K, V, KeyLess...>) * 10);
        return IntervalTree<K, V, KeyLess...>(&owned_arenas_.back(), less...);
      case TestMode::kHeapAllocation:
        return IntervalTree<K, V, KeyLess...>(
            IntervalTree<K, V, KeyLess...>::kHeapAllocated, less...);
    }
  }

 private:
  TestMode mode_;
  std::deque<UnsafeArena> owned_arenas_;
};

class IntervalTreeTest : public ::testing::TestWithParam<TestMode> {
 protected:
  IntervalTreeTest() : factory_(GetParam()) {}
  TreeFactory factory_;
};

INSTANTIATE_TEST_SUITE_P(IntervalTreeTests, IntervalTreeTest,
                         ::testing::Values(TestMode::kDefaultArena,
                                           TestMode::kUserArena,
                                           TestMode::kHeapAllocation),
                         [](const ::testing::TestParamInfo<TestMode>& info) {
                           switch (info.param) {
                             case TestMode::kDefaultArena:
                               return "DefaultArena";
                             case TestMode::kUserArena:
                               return "UserArena";
                             case TestMode::kHeapAllocation:
                               return "HeapAllocation";
                           }
                         });

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(IntervalTreeTest);

TEST_P(IntervalTreeTest, CheckClosedIntervals) {
  auto tree = factory_.Create<int, char>();
  const int begin = 1;
  const int end = 3;
  tree.InsertVal(begin, end, 'a');
  IntervalIterator<int, char> left_it(&tree, begin - 1, begin,
                                      INTERVAL_SMALLEST);
  ASSERT_NE(left_it.Get(), nullptr);
  EXPECT_EQ(left_it.Get()->begin, begin);
  EXPECT_EQ(left_it.Get()->end, end);
  IntervalIterator<int, char> right_it(&tree, end, end + 1, INTERVAL_SMALLEST);
  ASSERT_NE(right_it.Get(), nullptr);
  EXPECT_EQ(right_it.Get()->begin, begin);
  EXPECT_EQ(right_it.Get()->end, end);
}

TEST_P(IntervalTreeTest, CheckFoo) {
  auto tree = factory_.Create<int, char>();  // Create an interval tree
  tree.InsertVal(1, 3, 'a');  // Insert an element (requires copy constructor)
  IntervalNode<int, char>* node =
      tree.Insert(2, 4);  // No copy constructor called
  ASSERT_NE(node, nullptr);
  node->value = 'b';  // Assign the value of interval [2, 4] to 'b'

  // Create an iterator that finds all intervals intersecting [2, 3],
  // The last flag decide if its initialized to smallest (or largest)
  IntervalIterator<int, char> iter(&tree, 2, 3, INTERVAL_SMALLEST);
  ASSERT_THAT(iter.Get(), NodeValue('a'));
  EXPECT_EQ(iter.Get()->begin, 1);
  EXPECT_EQ(iter.Get()->end, 3);
  iter.Next();  // move to the next intersecting interval
  EXPECT_THAT(iter.Get(), NodeValue('b'));
  iter.Prev();  // move to the previous intersecting interval
  EXPECT_THAT(iter.Get(), NodeValue('a'));
  iter.Delete();  // delete the interval 'a' and move to the next element
  EXPECT_THAT(iter.Get(), NodeValue('b'));
  iter.Next();
  EXPECT_EQ(iter.Get(), nullptr);  // we already moved to the end
}

TEST_P(IntervalTreeTest, CheckInsertValUniquePtr) {
  auto tree = factory_.Create<int, std::unique_ptr<int>>();
  tree.InsertVal(1, 3, std::make_unique<int>(3));
  IntervalTree<int, std::unique_ptr<int>>::iterator iter(&tree, 0, 10,
                                                         INTERVAL_SMALLEST);
  EXPECT_THAT(iter.Get(), NodeValue(Pointee(3)));
  iter.Next();
  EXPECT_EQ(iter.Get(), nullptr);  // we already moved to the end
}

TEST_P(IntervalTreeTest, CheckInsertValTemporaryValue) {
  auto tree = factory_.Create<int, std::string>();
  tree.InsertVal(1, 3, absl::StrCat(3, "aaa", 3));
  IntervalTree<int, std::string>::iterator iter(&tree, 0, 10,
                                                INTERVAL_SMALLEST);
  EXPECT_THAT(iter.Get(), NodeValue("3aaa3"));
  iter.Next();
  EXPECT_EQ(iter.Get(), nullptr);  // we already moved to the end
}

struct Aggregate {
  int a;
  int b;
  bool operator==(const Aggregate& other) const {
    return a == other.a && b == other.b;
  }
  friend std::ostream& operator<<(std::ostream& os, const Aggregate& v) {
    os << absl::StrCat(v.a, v.b);
    return os;
  }
};

TEST_P(IntervalTreeTest, CheckInsertValAggregate) {
  auto tree = factory_.Create<int, Aggregate>();
  tree.InsertVal(1, 3, {4, 5});
  IntervalTree<int, Aggregate>::iterator iter(&tree, 0, 10, INTERVAL_SMALLEST);
  EXPECT_THAT(iter.Get(), NodeValue(Aggregate({4, 5})));
  iter.Next();
  EXPECT_EQ(iter.Get(), nullptr);  // we already moved to the end
}

TEST_P(IntervalTreeTest, CheckBug) {
  // Bug regression test
  auto tree = factory_.Create<int, char>();
  tree.InsertVal(0, 0, 'x');
  tree.InsertVal(0, 7, 'y');

  IntervalIterator<int, char> iter(&tree, 4, 7, INTERVAL_SMALLEST);
  ASSERT_NE(iter.Get(), nullptr);
}

TEST_P(IntervalTreeTest, CheckNodeIteratorBug) {
  // Regression test for bug encountered with node-based iterators.
  auto tree = factory_.Create<int, char>();
  IntervalNode<int, char>* node_1 = tree.InsertVal(0, 10, 'x');
  tree.InsertVal(0, 10, 'y');
  IntervalIterator<int, char> node_1_iter(&tree, node_1);
  ASSERT_NE(node_1_iter.Get(), nullptr);
  EXPECT_EQ(node_1_iter.Get(), node_1);
}

template <typename T, typename U, typename KeyLess>
[[nodiscard]] bool InsertAndCheck(IntervalTree<T, U, KeyLess>* tree,
                                  const T& begin, const T& end, U data) {
  int size = tree->size();
  tree->InsertVal(begin, end, data);
  return (tree->size() == size + 1) && tree->CheckInvariants();
}

struct InitRequired {
  InitRequired() : field_(12345) {
    VLOG(1) << "cre: " << this;
    ++global_count_;
  }
  InitRequired(const InitRequired& rhs) : field_(rhs.field_) {
    VLOG(1) << "cpy: " << this;
    ++global_count_;
  }
  ~InitRequired() {
    field_ = 54321;
    VLOG(1) << "del: " << this;
    --global_count_;
    CHECK_GE(global_count_, 0);
  }
  int field_;
  static int global_count_;
};

// static
int InitRequired::global_count_ = 0;
TEST_P(IntervalTreeTest, TestCtorAndDtor) {
  InitRequired::global_count_ = 0;
  auto tree = factory_.Create<int, InitRequired>();

  IntervalNode<int, InitRequired>* node = tree.Insert(0, 1);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->value.field_, 12345);
  EXPECT_EQ(1, InitRequired::global_count_);

  // check destructor gets invoked.
  IntervalTree<int, InitRequired>::iterator iter(&tree, 0, 1);
  iter.Delete();
  EXPECT_EQ(0, InitRequired::global_count_);
  EXPECT_EQ(0, tree.size());

  // check constructor is invoked on re-use from freelist.
  node = tree.Insert(2, 3);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->value.field_, 12345);
  EXPECT_EQ(1, tree.size());

  // Tree will now get destroyed;  if the tree destruction re-invokes
  // destructors on deleted elements, this will be caught in the destructor
  // for InitRequired.
}

TEST(IntervalTreeBasicTest, TestCallerProvidedArena) {
  alignas(alignof(IntervalNode<int, InitRequired>)) char buffer[4096];
  UnsafeArena arena(buffer, sizeof(buffer));
  IntervalTree<int, InitRequired> tree(&arena);
  auto* node = tree.Insert(0, 1);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->value.field_, 12345);
  EXPECT_GE(static_cast<const void*>(node), static_cast<const void*>(buffer));
  EXPECT_LE(static_cast<const void*>(node + 1),
            static_cast<const void*>(buffer + sizeof(buffer)));
  // destroy tree before arena
}

TEST_P(IntervalTreeTest, TestCtorAndDtor2) {
  InitRequired::global_count_ = 0;
  auto tree = factory_.Create<int, InitRequired>();

  InitRequired my_obj;  // this will be copied.
  IntervalNode<int, InitRequired>* node = tree.InsertVal(0, 1, my_obj);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->value.field_, 12345);
  EXPECT_EQ(2, InitRequired::global_count_);

  // add a second.
  node = tree.Insert(0, 1);

  // remove one, and check destructor gets invoked.
  IntervalTree<int, InitRequired>::iterator iter(&tree, 0, 1);
  iter.Delete();
  EXPECT_EQ(2, InitRequired::global_count_);
  EXPECT_EQ(1, tree.size());

  // (non-empty) Tree will now get destroyed;  if the tree destruction
  // re-invokes destructors on deleted elements, this will be caught in
  // the destructor for InitRequired.
}

TEST_P(IntervalTreeTest, TestBasicInsertionAndInvariants) {
  auto tree = factory_.Create<int, char>();

  EXPECT_EQ(tree.size(), 0);
  ASSERT_TRUE(InsertAndCheck(&tree, -10, 2, 'a'));
  ASSERT_TRUE(InsertAndCheck(&tree, 5, 6, 'e'));
  ASSERT_TRUE(InsertAndCheck(&tree, 7, 8, 'f'));
  ASSERT_TRUE(InsertAndCheck(&tree, 2, 3, 'c'));
  ASSERT_TRUE(InsertAndCheck(&tree, 5, 7, 'd'));
  ASSERT_TRUE(InsertAndCheck(&tree, 2, 5, 'b'));
  ASSERT_TRUE(InsertAndCheck(&tree, 10, 12, 'g'));
}

TEST_P(IntervalTreeTest, TestBasicIteratorTraversal) {
  auto tree = factory_.Create<int, char>();
  ASSERT_TRUE(InsertAndCheck(&tree, -10, 2, 'a'));
  ASSERT_TRUE(InsertAndCheck(&tree, 5, 6, 'e'));
  ASSERT_TRUE(InsertAndCheck(&tree, 7, 8, 'f'));
  ASSERT_TRUE(InsertAndCheck(&tree, 2, 3, 'c'));
  ASSERT_TRUE(InsertAndCheck(&tree, 5, 7, 'd'));
  ASSERT_TRUE(InsertAndCheck(&tree, 2, 5, 'b'));
  ASSERT_TRUE(InsertAndCheck(&tree, 10, 12, 'g'));

  IntervalTree<int, char>::iterator iter(&tree, -2, 10, INTERVAL_SMALLEST);

  EXPECT_THAT(iter.Get(), NodeValue('a'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(iter.Next(), NodeValue('b'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(iter.Next(), NodeValue('c'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(iter.Next(), NodeValue('d'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(iter.Next(), NodeValue('e'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(iter.Get(), NodeValue('e'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(iter.Next(), NodeValue('f'));
  ASSERT_TRUE(tree.CheckInvariants());
  ASSERT_TRUE(InsertAndCheck(&tree, 10, 11, 'h'));
  EXPECT_THAT(iter.Next(), NodeValue('g'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(iter.Next(), NodeValue('h'));
  ASSERT_TRUE(tree.CheckInvariants());

  EXPECT_EQ(iter.Next(), nullptr);
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_EQ(iter.Get(), nullptr);
  ASSERT_TRUE(tree.CheckInvariants());
}

TEST_P(IntervalTreeTest, TestBasicConstIteratorTraversal) {
  auto tree = factory_.Create<int, char>();
  ASSERT_TRUE(InsertAndCheck(&tree, -10, 2, 'a'));
  ASSERT_TRUE(InsertAndCheck(&tree, 5, 6, 'e'));
  ASSERT_TRUE(InsertAndCheck(&tree, 7, 8, 'f'));
  ASSERT_TRUE(InsertAndCheck(&tree, 2, 3, 'c'));
  ASSERT_TRUE(InsertAndCheck(&tree, 5, 7, 'd'));
  ASSERT_TRUE(InsertAndCheck(&tree, 2, 5, 'b'));
  ASSERT_TRUE(InsertAndCheck(&tree, 10, 12, 'g'));
  ASSERT_TRUE(InsertAndCheck(&tree, 10, 11, 'h'));

  IntervalTree<int, char>::const_iterator iter2(&tree, 5, 7, INTERVAL_SMALLEST);
  EXPECT_THAT(iter2.Get(), NodeValue('b'));
  EXPECT_EQ(iter2.value(), 'b');
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(iter2.Next(), NodeValue('d'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(iter2.Next(), NodeValue('e'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(iter2.Next(), NodeValue('f'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_EQ(iter2.Next(), nullptr);
  ASSERT_TRUE(tree.CheckInvariants());
}

TEST_P(IntervalTreeTest, TestBasicDeletion) {
  auto tree = factory_.Create<int, char>();
  ASSERT_TRUE(InsertAndCheck(&tree, -10, 2, 'a'));
  ASSERT_TRUE(InsertAndCheck(&tree, 5, 6, 'e'));
  ASSERT_TRUE(InsertAndCheck(&tree, 7, 8, 'f'));
  ASSERT_TRUE(InsertAndCheck(&tree, 2, 3, 'c'));
  ASSERT_TRUE(InsertAndCheck(&tree, 5, 7, 'd'));
  ASSERT_TRUE(InsertAndCheck(&tree, 2, 5, 'b'));
  ASSERT_TRUE(InsertAndCheck(&tree, 10, 12, 'g'));
  ASSERT_TRUE(InsertAndCheck(&tree, 10, 11, 'h'));

  ASSERT_TRUE(tree.CheckInvariants());
  IntervalIterator<int, char> iter3(&tree, 5, 7, INTERVAL_SMALLEST);

  IntervalIterator<int, char> del(&tree, 7, 8, INTERVAL_SMALLEST);
  del.Delete();
  ASSERT_TRUE(tree.CheckInvariants());

  EXPECT_THAT(iter3.Get(), NodeValue('b'));
  // c is not in range, and d is deleted
  EXPECT_THAT(iter3.Next(), NodeValue('e'));
  EXPECT_THAT(iter3.Next(), NodeValue('f'));
  ASSERT_TRUE(tree.CheckInvariants());

  IntervalIterator<int, char> iter4(&tree, 1, 12, INTERVAL_SMALLEST);
  EXPECT_THAT(iter4.Get(), NodeValue('a'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(iter4.Delete(), NodeValue('b'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(iter4.Delete(), NodeValue('c'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(iter4.Delete(), NodeValue('e'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(iter4.Delete(), NodeValue('f'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(iter4.Delete(), NodeValue('g'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(iter4.Delete(), NodeValue('h'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_EQ(iter4.Delete(), nullptr);
  ASSERT_TRUE(tree.CheckInvariants());
}

TEST_P(IntervalTreeTest, TestConcurrentIteratorsWithUpdates) {
  auto tree = factory_.Create<int, char>();

  ASSERT_TRUE(InsertAndCheck(&tree, 1, 1, 'a'));
  IntervalIterator<int, char> i1(&tree, 1, 10, INTERVAL_SMALLEST);
  ASSERT_TRUE(InsertAndCheck(&tree, 2, 2, 'b'));
  IntervalIterator<int, char> i2(&tree, 2, 10, INTERVAL_SMALLEST);
  ASSERT_TRUE(InsertAndCheck(&tree, 3, 3, 'c'));
  IntervalIterator<int, char> i3(&tree, 3, 10, INTERVAL_SMALLEST);
  ASSERT_TRUE(InsertAndCheck(&tree, 4, 4, 'd'));
  ASSERT_TRUE(InsertAndCheck(&tree, 5, 5, 'e'));
  ASSERT_TRUE(InsertAndCheck(&tree, 7, 7, 'g'));
  ASSERT_TRUE(InsertAndCheck(&tree, 7, 7, 'g'));

  EXPECT_THAT(i1.Get(), NodeValue('a'));

  // (x, y, z) means:
  //   i1.Get()->value, x, i2.Get()->value, y, i3.Get()->value, z
  EXPECT_THAT(i1.Get(), NodeValue('a'));  // (a, b, c)
  EXPECT_THAT(i2.Get(), NodeValue('b'));
  EXPECT_THAT(i3.Get(), NodeValue('c'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(i1.Delete(), NodeValue('b'));  // (b, b, c) delete a
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(i3.Delete(), NodeValue('d'));  // (b, b, d) delete c
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(i2.Next(), NodeValue('d'));         // (b, d, d)
  EXPECT_THAT(i2.Next(), NodeValue('e'));         // (b, e, d)
  EXPECT_THAT(i3.Delete(), NodeValue('e'));       // (b, e, e) delete d
  EXPECT_THAT(i3.Next(), NodeValue('g'));         // (b, e, g)
  ASSERT_TRUE(InsertAndCheck(&tree, 3, 3, 'c'));  // (b, e, g) insert c
  ASSERT_TRUE(InsertAndCheck(&tree, 6, 6, 'f'));
  EXPECT_THAT(i1.Delete(), NodeValue('c'));  // (c, e, g) delete b
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(i1.Delete(), NodeValue('e'));  // (e, e, g) delete c
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_EQ(i1.Get(), i2.Get());
  EXPECT_THAT(i2.Next(), NodeValue('f'));  // (e, f, g)
  EXPECT_THAT(i2.Next(), NodeValue('g'));  // (e, g, g)
  EXPECT_EQ(i2.Get(), i3.Get());
  EXPECT_THAT(i2.Next(), NodeValue('g'));  // (e, g2, g)
  EXPECT_NE(i2.Get(), i3.Get());
  EXPECT_THAT(i3.Delete(), NodeValue('g'));  // (e, g2, g2) delete first g
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(i1.Delete(), NodeValue('f'));  // (f, g2, g2) delete e
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(i1.Delete(), NodeValue('g'));  // (g2, g2, g2) delete f
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_EQ(i1.Get(), i2.Get());
  EXPECT_EQ(i1.Get(), i3.Get());
  EXPECT_EQ(i2.Next(), nullptr);    // (g2, 0, g2)
  EXPECT_EQ(i3.Next(), nullptr);    // (g2, 0, 0)
  EXPECT_EQ(i1.Delete(), nullptr);  // (0, 0, 0) delete second g
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_EQ(tree.size(), 0);
  tree.InsertVal(4, 4, 'x');
  tree.InsertVal(5, 5, 'f');
  ASSERT_TRUE(tree.CheckInvariants());
}

TEST_P(IntervalTreeTest, TestIntervalTreeStrings) {
  auto str = factory_.Create<int, std::string>();

  ASSERT_TRUE(InsertAndCheck(&str, 1, 1, std::string("a1")));
  ASSERT_TRUE(InsertAndCheck(&str, 2, 2, std::string("b1")));
  ASSERT_TRUE(InsertAndCheck(&str, 3, 3, std::string("c5")));
  ASSERT_TRUE(InsertAndCheck(&str, 1, 1, std::string("a2")));
  ASSERT_TRUE(InsertAndCheck(&str, 2, 2, std::string("b2")));
  ASSERT_TRUE(InsertAndCheck(&str, 1, 3, std::string("ac")));
  ASSERT_TRUE(InsertAndCheck(&str, 3, 3, std::string("c4")));
  ASSERT_TRUE(InsertAndCheck(&str, 3, 3, std::string("c3")));
  ASSERT_TRUE(InsertAndCheck(&str, 2, 2, std::string("b3")));
  ASSERT_TRUE(InsertAndCheck(&str, 1, 1, std::string("a3")));
  ASSERT_TRUE(InsertAndCheck(&str, 2, 2, std::string("b4")));
  ASSERT_TRUE(InsertAndCheck(&str, 3, 3, std::string("c2")));
  ASSERT_TRUE(InsertAndCheck(&str, 2, 2, std::string("b5")));
  ASSERT_TRUE(InsertAndCheck(&str, 3, 3, std::string("c1")));
  ASSERT_TRUE(InsertAndCheck(&str, 1, 1, std::string("a4")));
  ASSERT_TRUE(InsertAndCheck(&str, 1, 1, std::string("a5")));
  ASSERT_TRUE(InsertAndCheck(&str, 1, 2, std::string("ab")));
  ASSERT_TRUE(InsertAndCheck(&str, 2, 4, std::string("bd")));

  IntervalIterator<int, std::string> nullIter1(&str, -10, 0, INTERVAL_SMALLEST);
  EXPECT_EQ(nullIter1.Get(), nullptr);
  IntervalIterator<int, std::string> nullIter2(&str, 5, 6, INTERVAL_LARGEST);
  EXPECT_EQ(nullIter2.Get(), nullptr);

  IntervalIterator<int, std::string> strIter1(&str, 1, 5, INTERVAL_SMALLEST);
  ASSERT_THAT(strIter1.Get(), NodeValue("ac"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter1.Next(), NodeValue("ab"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter1.Next(), NodeValue("a1"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter1.Next(), NodeValue("a2"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter1.Next(), NodeValue("a3"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter1.Next(), NodeValue("a4"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter1.Next(), NodeValue("a5"));
  ASSERT_TRUE(str.CheckInvariants());

  auto* inserted_node = str.Insert(1, 1);
  ASSERT_NE(inserted_node, nullptr);
  *(inserted_node->ptr()) = "a6";  // same as str.InsertVal(1, 1, "a6")
  str.InsertVal(1, 1, "a7");

  EXPECT_THAT(strIter1.Next(), NodeValue("a6"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter1.Next(), NodeValue("a7"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter1.Next(), NodeValue("bd"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter1.Next(), NodeValue("b1"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter1.Next(), NodeValue("b2"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter1.Next(), NodeValue("b3"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter1.Next(), NodeValue("b4"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter1.Next(), NodeValue("b5"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter1.Next(), NodeValue("c5"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter1.Next(), NodeValue("c4"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter1.Next(), NodeValue("c3"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter1.Next(), NodeValue("c2"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter1.Next(), NodeValue("c1"));
  ASSERT_TRUE(str.CheckInvariants());
}

TEST_P(IntervalTreeTest, TestStringsConstIteratorTraversal) {
  auto str = factory_.Create<int, std::string>();
  ASSERT_TRUE(InsertAndCheck(&str, 1, 1, std::string("a2")));
  ASSERT_TRUE(InsertAndCheck(&str, 2, 2, std::string("b2")));
  ASSERT_TRUE(InsertAndCheck(&str, 1, 3, std::string("ac")));
  ASSERT_TRUE(InsertAndCheck(&str, 3, 3, std::string("c4")));
  ASSERT_TRUE(InsertAndCheck(&str, 2, 2, std::string("b3")));
  ASSERT_TRUE(InsertAndCheck(&str, 1, 2, std::string("ab")));
  ASSERT_TRUE(InsertAndCheck(&str, 2, 4, std::string("bd")));

  // You can only create ConstIntervalIterator from const IntervalTree
  const ConstIntervalIterator<int, std::string> iter(&str, 1, 5,
                                                     INTERVAL_SMALLEST);
  EXPECT_THAT(iter.Get(), NodeValue("ac"));

  // One can not move to next in a const iterator
  ConstIntervalIterator<int, std::string> del(&str, 1, 5, INTERVAL_SMALLEST);
  EXPECT_THAT(del.Next(), NodeValue("ab"));
  // One can not delete from a const tree
}

TEST_P(IntervalTreeTest, TestStringsReverseIteration) {
  auto str = factory_.Create<int, std::string>();
  ASSERT_TRUE(InsertAndCheck(&str, 1, 1, std::string("a1")));
  ASSERT_TRUE(InsertAndCheck(&str, 2, 2, std::string("b1")));
  ASSERT_TRUE(InsertAndCheck(&str, 3, 3, std::string("c5")));
  ASSERT_TRUE(InsertAndCheck(&str, 1, 3, std::string("ac")));
  ASSERT_TRUE(InsertAndCheck(&str, 3, 3, std::string("c4")));
  ASSERT_TRUE(InsertAndCheck(&str, 3, 3, std::string("c3")));
  ASSERT_TRUE(InsertAndCheck(&str, 3, 3, std::string("c2")));
  ASSERT_TRUE(InsertAndCheck(&str, 2, 2, std::string("b5")));
  ASSERT_TRUE(InsertAndCheck(&str, 3, 3, std::string("c1")));
  ASSERT_TRUE(InsertAndCheck(&str, 1, 2, std::string("ab")));
  ASSERT_TRUE(InsertAndCheck(&str, 2, 4, std::string("bd")));

  IntervalIterator<int, std::string> strIterNull(&str, 3, 3, INTERVAL_SMALLEST);
  EXPECT_EQ(strIterNull.Prev(), nullptr);

  IntervalIterator<int, std::string> strIter1(&str, 1, 5, INTERVAL_SMALLEST);
  while (strIter1.Get() != nullptr && strIter1.value() != "c1") {
    strIter1.Next();
  }
  ASSERT_NE(strIter1.Get(), nullptr);

  EXPECT_THAT(strIter1.Prev(), NodeValue("c2"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter1.Prev(), NodeValue("c3"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter1.Prev(), NodeValue("c4"));
  ASSERT_TRUE(str.CheckInvariants());

  IntervalIterator<int, std::string> strIter2(&str, 3, 4, INTERVAL_LARGEST);
  IntervalIterator<int, std::string> strIter3(&str, 2, 3, INTERVAL_LARGEST);

  EXPECT_THAT(strIter2.Get(), NodeValue("c1"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter3.Get(), NodeValue("c1"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter2.Prev(), NodeValue("c2"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter3.Prev(), NodeValue("c2"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter2.Prev(), NodeValue("c3"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter3.Prev(), NodeValue("c3"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter1.Prev(), NodeValue("c5"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter2.Prev(), NodeValue("c4"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter3.Prev(), NodeValue("c4"));
  ASSERT_TRUE(str.CheckInvariants());
}

TEST_P(IntervalTreeTest, TestIntervalTreePrevDelete) {
  auto tree = factory_.Create<int, char>();
  ASSERT_TRUE(InsertAndCheck(&tree, 1, 1, 'a'));
  ASSERT_TRUE(InsertAndCheck(&tree, 2, 2, 'b'));
  ASSERT_TRUE(InsertAndCheck(&tree, 3, 3, 'c'));
  ASSERT_TRUE(InsertAndCheck(&tree, 4, 4, 'd'));
  ASSERT_TRUE(InsertAndCheck(&tree, 5, 5, 'e'));

  IntervalIterator<int, char> iter(&tree, 1, 5, INTERVAL_LARGEST);
  ASSERT_THAT(iter.Get(), NodeValue('e'));

  EXPECT_THAT(iter.PrevDelete(), NodeValue('d'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_EQ(tree.size(), 4);

  EXPECT_THAT(iter.PrevDelete(), NodeValue('c'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_EQ(tree.size(), 3);

  EXPECT_THAT(iter.PrevDelete(), NodeValue('b'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_EQ(tree.size(), 2);

  EXPECT_THAT(iter.PrevDelete(), NodeValue('a'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_EQ(tree.size(), 1);

  EXPECT_EQ(iter.PrevDelete(), nullptr);
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_EQ(tree.size(), 0);
}

TEST_P(IntervalTreeTest, TestConstIntervalIteratorPrev) {
  auto tree = factory_.Create<int, char>();
  ASSERT_TRUE(InsertAndCheck(&tree, 1, 1, 'a'));
  ASSERT_TRUE(InsertAndCheck(&tree, 2, 2, 'b'));
  ASSERT_TRUE(InsertAndCheck(&tree, 3, 3, 'c'));
  ASSERT_TRUE(InsertAndCheck(&tree, 4, 4, 'd'));
  ASSERT_TRUE(InsertAndCheck(&tree, 5, 5, 'e'));

  ConstIntervalIterator<int, char> iter(&tree, 1, 5, INTERVAL_LARGEST);
  ASSERT_THAT(iter.Get(), NodeValue('e'));

  EXPECT_THAT(iter.Prev(), NodeValue('d'));
  ASSERT_TRUE(tree.CheckInvariants());

  EXPECT_THAT(iter.Prev(), NodeValue('c'));
  ASSERT_TRUE(tree.CheckInvariants());

  EXPECT_THAT(iter.Prev(), NodeValue('b'));
  ASSERT_TRUE(tree.CheckInvariants());

  EXPECT_THAT(iter.Prev(), NodeValue('a'));
  ASSERT_TRUE(tree.CheckInvariants());

  EXPECT_EQ(iter.Prev(), nullptr);
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_EQ(tree.size(), 5);
}

TEST_P(IntervalTreeTest, TestStringsResetRange) {
  auto str = factory_.Create<int, std::string>();
  ASSERT_TRUE(InsertAndCheck(&str, 1, 1, std::string("a1")));
  ASSERT_TRUE(InsertAndCheck(&str, 2, 2, std::string("b1")));
  ASSERT_TRUE(InsertAndCheck(&str, 3, 3, std::string("c5")));
  ASSERT_TRUE(InsertAndCheck(&str, 1, 1, std::string("a2")));
  ASSERT_TRUE(InsertAndCheck(&str, 2, 2, std::string("b2")));
  ASSERT_TRUE(InsertAndCheck(&str, 1, 3, std::string("ac")));
  ASSERT_TRUE(InsertAndCheck(&str, 3, 3, std::string("c4")));
  ASSERT_TRUE(InsertAndCheck(&str, 3, 3, std::string("c3")));
  ASSERT_TRUE(InsertAndCheck(&str, 2, 2, std::string("b3")));
  ASSERT_TRUE(InsertAndCheck(&str, 1, 1, std::string("a3")));
  ASSERT_TRUE(InsertAndCheck(&str, 2, 2, std::string("b4")));
  ASSERT_TRUE(InsertAndCheck(&str, 3, 3, std::string("c2")));
  ASSERT_TRUE(InsertAndCheck(&str, 2, 2, std::string("b5")));
  ASSERT_TRUE(InsertAndCheck(&str, 3, 3, std::string("c1")));
  ASSERT_TRUE(InsertAndCheck(&str, 1, 1, std::string("a4")));
  ASSERT_TRUE(InsertAndCheck(&str, 1, 1, std::string("a5")));
  ASSERT_TRUE(InsertAndCheck(&str, 1, 2, std::string("ab")));
  ASSERT_TRUE(InsertAndCheck(&str, 2, 4, std::string("bd")));
  ASSERT_TRUE(InsertAndCheck(&str, 1, 1, std::string("a6")));
  ASSERT_TRUE(InsertAndCheck(&str, 1, 1, std::string("a7")));

  IntervalIterator<int, std::string> strIter2(&str, 3, 4, INTERVAL_LARGEST);
  EXPECT_THAT(strIter2.Get(), NodeValue("c1"));
  EXPECT_THAT(strIter2.Prev(), NodeValue("c2"));
  EXPECT_THAT(strIter2.Prev(), NodeValue("c3"));
  EXPECT_THAT(strIter2.Prev(), NodeValue("c4"));

  strIter2.ResetRange(1, 1);
  EXPECT_THAT(strIter2.Prev(), NodeValue("a7"));
  ASSERT_TRUE(str.CheckInvariants());

  strIter2.ResetRange(2, 2);
  EXPECT_THAT(strIter2.Next(), NodeValue("bd"));
  ASSERT_TRUE(str.CheckInvariants());

  EXPECT_THAT(strIter2.Next(), NodeValue("b1"));
  ASSERT_TRUE(str.CheckInvariants());

  strIter2.ResetRange(3, 3);
  EXPECT_THAT(strIter2.Next(), NodeValue("c5"));
  ASSERT_TRUE(str.CheckInvariants());
}

TEST_P(IntervalTreeTest, TestIntervalTreeReset) {
  auto tree = factory_.Create<int, char>();
  ASSERT_TRUE(InsertAndCheck(&tree, 1, 3, 'a'));
  ASSERT_TRUE(InsertAndCheck(&tree, 2, 4, 'b'));
  ASSERT_TRUE(InsertAndCheck(&tree, 5, 7, 'c'));

  tree.Reset();

  EXPECT_TRUE(tree.empty());
  EXPECT_EQ(tree.size(), 0);
  ASSERT_TRUE(tree.CheckInvariants());

  IntervalIterator<int, char> iter(&tree, 0, 10, INTERVAL_SMALLEST);
  EXPECT_EQ(iter.Get(), nullptr);
}

TEST_P(IntervalTreeTest, TestStringsAlternateConstructors) {
  auto str = factory_.Create<int, std::string>();
  ASSERT_TRUE(InsertAndCheck(&str, 1, 1, std::string("a1")));
  ASSERT_TRUE(InsertAndCheck(&str, 2, 2, std::string("b1")));
  ASSERT_TRUE(InsertAndCheck(&str, 3, 3, std::string("c5")));
  ASSERT_TRUE(InsertAndCheck(&str, 2, 2, std::string("b2")));
  ASSERT_TRUE(InsertAndCheck(&str, 1, 3, std::string("ac")));
  ASSERT_TRUE(InsertAndCheck(&str, 3, 3, std::string("c4")));
  ASSERT_TRUE(InsertAndCheck(&str, 3, 3, std::string("c3")));
  ASSERT_TRUE(InsertAndCheck(&str, 3, 3, std::string("c2")));
  ASSERT_TRUE(InsertAndCheck(&str, 2, 2, std::string("b5")));
  ASSERT_TRUE(InsertAndCheck(&str, 3, 3, std::string("c1")));
  ASSERT_TRUE(InsertAndCheck(&str, 1, 2, std::string("ab")));
  ASSERT_TRUE(InsertAndCheck(&str, 2, 4, std::string("bd")));

  IntervalIterator<int, std::string> find_c5(&str, 3, 3, INTERVAL_SMALLEST);
  while (find_c5.Get() != nullptr && find_c5.value() != "c5") {
    find_c5.Next();
  }
  auto* c5_node = find_c5.Get();
  EXPECT_THAT(c5_node, NodeValue("c5"));

  IntervalIterator<int, std::string> strIter4(&str, c5_node);
  strIter4.ResetRange(2, 4);
  EXPECT_THAT(strIter4.Next(), NodeValue("c4"));

  IntervalIterator<int, std::string> strIter5(&str, 2, 3);
  EXPECT_THAT(strIter5.Get(), NodeValue("b1"));

  IntervalIterator<int, std::string> strIter6(&str, 2, 2);
  EXPECT_THAT(strIter6.Get(), NodeValue("b1"));

  ASSERT_TRUE(InsertAndCheck(&str, 1, 3, std::string("ac2")));
  IntervalIterator<int, std::string> strIter7(&str, 1, 3);
  EXPECT_THAT(strIter7.Get(), NodeValue("ac"));
}

TEST_P(IntervalTreeTest, TestOverlappingRangesSmallest) {
  auto integer = factory_.Create<int, int>();
  integer.InsertVal(0, 0, 1);
  integer.InsertVal(0, 1, 0);
  integer.InsertVal(1, 2, 2);

  VLOG(2) << integer.DebugString();

  EXPECT_THAT(ExtractValues(IntervalIterator<int, int>(&integer, 0, 10,
                                                       INTERVAL_SMALLEST)),
              ElementsAre(NodeValue(0), NodeValue(1), NodeValue(2)));
}

TEST_P(IntervalTreeTest, TestOverlappingRangesLargest) {
  auto integer = factory_.Create<int, int>();
  integer.InsertVal(0, 1, 0);
  integer.InsertVal(1, 1, 1);
  integer.InsertVal(2, 2, 2);
  integer.InsertVal(3, 3, 4);
  integer.InsertVal(3, 5, 3);
  integer.InsertVal(5, 5, 7);
  integer.InsertVal(5, 6, 6);
  integer.InsertVal(5, 7, 5);
  integer.InsertVal(7, 8, 8);

  EXPECT_THAT(ExtractValuesReverse(IntervalIterator<int, int>(
                  &integer, 0, 10, INTERVAL_LARGEST)),
              ElementsAre(NodeValue(8), NodeValue(7), NodeValue(6),
                          NodeValue(5), NodeValue(4), NodeValue(3),
                          NodeValue(2), NodeValue(1), NodeValue(0)));
}

TEST_P(IntervalTreeTest, TestOverlappingRangesConstSmallest) {
  auto integer = factory_.Create<int, int>();
  integer.InsertVal(0, 0, 1);
  integer.InsertVal(0, 0, 2);
  integer.InsertVal(0, 2, 0);
  integer.InsertVal(3, 3, 4);
  integer.InsertVal(3, 5, 3);
  integer.InsertVal(5, 5, 7);
  integer.InsertVal(5, 5, 8);
  integer.InsertVal(5, 5, 9);
  integer.InsertVal(5, 5, 10);
  integer.InsertVal(5, 8, 5);
  integer.InsertVal(5, 7, 6);
  integer.InsertVal(7, 8, 11);
  integer.InsertVal(8, 8, 12);
  integer.InsertVal(9, 9, 16);
  integer.InsertVal(9, 10, 13);
  integer.InsertVal(9, 10, 14);
  integer.InsertVal(9, 10, 15);
  integer.InsertVal(10, 10, 17);

  EXPECT_THAT(
      ExtractValues(
          ConstIntervalIterator<int, int>(&integer, 0, 10, INTERVAL_SMALLEST)),
      ElementsAre(NodeValue(0), NodeValue(1), NodeValue(2), NodeValue(3),
                  NodeValue(4), NodeValue(5), NodeValue(6), NodeValue(7),
                  NodeValue(8), NodeValue(9), NodeValue(10), NodeValue(11),
                  NodeValue(12), NodeValue(13), NodeValue(14), NodeValue(15),
                  NodeValue(16), NodeValue(17)));
}

TEST_P(IntervalTreeTest, TestVectorDuplicateHandling) {
  auto integer = factory_.Create<int, int>();
  integer.InsertVal(1, 3, 1);
  integer.InsertVal(1, 3, 2);
  integer.InsertVal(3, 4, 3);
  integer.InsertVal(6, 6, 5);

  IntervalIterator<int, int> iter0(&integer, -5, -3, INTERVAL_SMALLEST);
  EXPECT_EQ(iter0.Get(), nullptr);

  IntervalIterator<int, int> iter1(&integer, 9, 9, INTERVAL_SMALLEST);
  EXPECT_EQ(iter1.Get(), nullptr);

  IntervalIterator<int, int> iter2(&integer, 3, 3, INTERVAL_SMALLEST);
  EXPECT_THAT(iter2.Get(), NodeValue(1));
  EXPECT_EQ(iter2.Prev(), nullptr);

  IntervalIterator<int, int> iter3(&integer, 5, 5, INTERVAL_SMALLEST);
  EXPECT_EQ(iter3.Get(), nullptr);

  IntervalIterator<int, int> iter4(&integer, 5, 6, INTERVAL_SMALLEST);
  EXPECT_THAT(iter4.Get(), NodeValue(5));

  IntervalIterator<int, int> iter5(&integer, 4, 5, INTERVAL_SMALLEST);
  EXPECT_THAT(iter5.Get(), NodeValue(3));
}

TEST_P(IntervalTreeTest, TestBackwardIteratorDuplicateHandling) {
  auto integer = factory_.Create<int, int>();
  integer.InsertVal(3, 4, 3);
  integer.InsertVal(3, 4, 4);
  integer.InsertVal(6, 6, 5);

  IntervalIterator<int, int> iter1(&integer, 5, 5, INTERVAL_LARGEST);
  EXPECT_EQ(iter1.Get(), nullptr);

  IntervalIterator<int, int> iter2(&integer, 3, 3, INTERVAL_LARGEST);
  EXPECT_THAT(iter2.Get(), NodeValue(4));

  IntervalIterator<int, int> iter3(&integer, 5, 6, INTERVAL_LARGEST);
  EXPECT_THAT(iter3.Get(), NodeValue(5));
}

TEST_P(IntervalTreeTest, TestIterableDisjointRanges) {
  IntervalTree<int, std::string> tree = factory_.Create<int, std::string>();
  tree.InsertVal(3, 4, "b");
  tree.InsertVal(5, 6, "c");
  tree.InsertVal(1, 2, "a");
  tree.UpdateStructure();

  std::vector<std::string> values;
  for (const auto& node : tree) {
    values.push_back(node.value);
  }
  EXPECT_THAT(values, ElementsAre("a", "b", "c"));

  const IntervalTree<int, std::string>& const_tree = tree;
  std::vector<std::string> const_values;
  for (const auto& node : const_tree) {
    const_values.push_back(node.value);
  }
  EXPECT_THAT(const_values, ElementsAre("a", "b", "c"));
}

TEST_P(IntervalTreeTest, TestIterableOverlappingRanges) {
  IntervalTree<int, std::string> tree = factory_.Create<int, std::string>();
  tree.InsertVal(2, 6, "b");
  tree.InsertVal(3, 7, "c");
  tree.InsertVal(1, 5, "a");
  tree.UpdateStructure();

  std::vector<std::string> values;
  for (const auto& node : tree) {
    values.push_back(node.value);
  }
  EXPECT_THAT(values, ElementsAre("a", "b", "c"));

  const IntervalTree<int, std::string>& const_tree = tree;
  std::vector<std::string> const_values;
  for (const auto& node : const_tree) {
    const_values.push_back(node.value);
  }
  EXPECT_THAT(const_values, ElementsAre("a", "b", "c"));
}

TEST_P(IntervalTreeTest, TestIterableEmptyTree) {
  IntervalTree<int, std::string> tree = factory_.Create<int, std::string>();
  tree.UpdateStructure();

  std::vector<std::string> values;
  for (const auto& node : tree) {
    values.push_back(node.value);
  }
  EXPECT_THAT(values, IsEmpty());

  const IntervalTree<int, std::string>& const_tree = tree;
  std::vector<std::string> const_values;
  for (const auto& node : const_tree) {
    const_values.push_back(node.value);
  }
  EXPECT_THAT(const_values, IsEmpty());
}

// Return true if two integer-based interval trees equal.
MATCHER_P(TreeEquals, expected_tree_ptr, "") {
  absl::node_hash_map<std::string, std::set<int>> ht1, ht2;
  char interval_buffer[100];
  ConstIntervalIterator<int, int> iter1(&arg, std::numeric_limits<int>::min(),
                                        std::numeric_limits<int>::max());
  ConstIntervalIterator<int, int> iter2(expected_tree_ptr,
                                        std::numeric_limits<int>::min(),
                                        std::numeric_limits<int>::max());
  while (iter1.Get() != nullptr) {
    snprintf(interval_buffer, sizeof(interval_buffer), "%d-%d",
             iter1.Get()->begin, iter1.Get()->end);
    if (ht1.find(interval_buffer) == ht1.end()) {
      ht1[interval_buffer] = {iter1.Get()->value};
    } else {
      ht1[interval_buffer].insert(iter1.Get()->value);
    }
    iter1.Next();
  }
  while (iter2.Get() != nullptr) {
    snprintf(interval_buffer, sizeof(interval_buffer), "%d-%d",
             iter2.Get()->begin, iter2.Get()->end);
    if (ht2.find(interval_buffer) == ht2.end()) {
      ht2[interval_buffer] = {iter2.Get()->value};
    } else {
      ht2[interval_buffer].insert(iter2.Get()->value);
    }
    iter2.Next();
  }
  return (ht1 == ht2);
}

void DefaultCopyFunction(const int src_value, int* dst_value) {
  *dst_value = src_value;
}

TEST_P(IntervalTreeTest, TestMakeLinearCopyEmpty) {
  auto tree = factory_.Create<int, int>();
  tree.UpdateStructure();
  auto tree_copy = absl::WrapUnique(tree.MakeLinearCopy(DefaultCopyFunction));
  ASSERT_NE(tree_copy, nullptr);
  EXPECT_THAT(*tree_copy, TreeEquals(&tree));
}

TEST_P(IntervalTreeTest, TestMakeLinearCopyVectorStructure) {
  absl::BitGen bitgen;
  for (int trail = 0; trail < 10; ++trail) {
    auto tree = factory_.Create<int, int>();
    std::vector<IntervalTree<int, int>::TreeNode> operation_nodes;
    for (int i = 1000; i >= 0; --i) {
      int span = absl::Uniform(bitgen, 0, 4);
      int begin = i * 10 - span;
      int end = i * 10 + span;
      if (i > 10)
        tree.InsertVal(begin, end, i);
      else
        operation_nodes.push_back(IntervalNode<int, int>(begin, end, i));
    }
    tree.UpdateStructure();
    auto tree_copy = absl::WrapUnique(tree.MakeLinearCopy(DefaultCopyFunction));
    ASSERT_NE(tree_copy, nullptr);
    EXPECT_THAT(*tree_copy, TreeEquals(&tree));
    // Add more nodes to tree and tree_copy, then check if they are equal.
    for (const auto& node : operation_nodes) {
      tree.InsertVal(node.begin, node.end, node.value);
      tree_copy->InsertVal(node.begin, node.end, node.value);
    }
    tree.UpdateStructure();
    tree_copy->UpdateStructure();
    EXPECT_THAT(*tree_copy, TreeEquals(&tree));
    // Delete nodes from tree and tree_copy, then check if they are equal.
    for (int i = 0; i < operation_nodes.size(); ++i) {
      int begin = operation_nodes[i].begin;
      int end = operation_nodes[i].end;
      IntervalIterator<int, int> iter1(&tree, begin, end, INTERVAL_SMALLEST);
      IntervalIterator<int, int> iter2(tree_copy.get(), begin, end,
                                       INTERVAL_SMALLEST);
      if (iter1.Get()) iter1.Delete();
      if (iter2.Get()) iter2.Delete();
      // Check iter reaches end after deleting the interval, because there
      // can only be one matching interval as intervals are not overlapping.
      EXPECT_EQ(nullptr, iter1.Get());
      EXPECT_EQ(nullptr, iter2.Get());
      // Check the trees are equal after deleting the same interval.
      tree.UpdateStructure();
      tree_copy->UpdateStructure();
      EXPECT_THAT(*tree_copy, TreeEquals(&tree));
    }
  }
}

TEST_P(IntervalTreeTest, TestMakeLinearCopyTreeStructure) {
  absl::BitGen bitgen;
  for (int trail = 0; trail < 10; ++trail) {
    std::vector<IntervalTree<int, int>::TreeNode> operation_nodes;
    auto tree = factory_.Create<int, int>();
    for (int i = 0; i < 1000; ++i) {
      int span = absl::Uniform(bitgen, 0, 15);
      int begin = i * 10 - span;
      int end = i * 10 + span;
      if (i > 10)
        tree.InsertVal(begin, end, i);
      else
        operation_nodes.push_back(IntervalNode<int, int>(begin, end, i));
    }
    tree.UpdateStructure();
    auto tree_copy = absl::WrapUnique(tree.MakeLinearCopy(DefaultCopyFunction));
    ASSERT_NE(tree_copy, nullptr);
    EXPECT_THAT(*tree_copy, TreeEquals(&tree));
    // Add more nodes to tree and tree_copy, then check if they are equal.
    for (const auto& node : operation_nodes) {
      tree.InsertVal(node.begin, node.end, node.value);
      tree_copy->InsertVal(node.begin, node.end, node.value);
    }
    tree.UpdateStructure();
    tree_copy->UpdateStructure();
    EXPECT_THAT(*tree_copy, TreeEquals(&tree));
    // Delete nodes from tree and tree_copy, then check if they are equal.
    int begin = 10;
    int end = 100;
    IntervalIterator<int, int> iter1(&tree, begin, end, INTERVAL_SMALLEST);
    IntervalIterator<int, int> iter2(tree_copy.get(), begin, end,
                                     INTERVAL_SMALLEST);
    while (iter1.Get()) {
      iter1.Delete();
    }
    while (iter2.Get()) {
      iter2.Delete();
    }
    tree.UpdateStructure();
    tree_copy->UpdateStructure();
    EXPECT_THAT(*tree_copy, TreeEquals(&tree));
  }
}

TEST(IntervalTreeBasicTest, TestMakeLinearCopyOnArena) {
  IntervalTree<int, int64_t> tree;
  tree.InsertVal(0, 1, 64);
  alignas(alignof(int64_t)) char buffer[1024];
  UnsafeArena arena(buffer, sizeof(buffer));
  std::unique_ptr<IntervalTree<int, int64_t, std::less<int>>> tree_copy =
      absl::WrapUnique(tree.MakeLinearCopy(
          [](const int64_t src_value, int64_t* dst_value) {
            *dst_value = src_value;
          },
          &arena));
  ASSERT_NE(tree_copy, nullptr);
  const IntervalNode<int, int64_t, std::less<int>>* node =
      IntervalIterator<int, int64_t>(tree_copy.get(), 0, 1, INTERVAL_SMALLEST)
          .Get();

  // check that the node was allocated on the arena.
  ASSERT_NE(node, nullptr);
  EXPECT_GE(static_cast<const void*>(node), static_cast<const void*>(buffer));
  EXPECT_LE(static_cast<const void*>(node + 1),
            static_cast<const void*>(buffer + sizeof(buffer)));
  EXPECT_EQ(node->value, 64);
}

// Test that struct key w/o comparison operators works well with the interval
// tree.
struct StructKey {
  explicit StructKey(int32_t value_in) : value(value_in) {}

  StructKey operator+(int delta) const {
    StructKey ret(value + delta);
    return ret;
  }

  StructKey operator-(int delta) const {
    StructKey ret(value - delta);
    return ret;
  }

  int32_t value = 0;
};

struct StructKeyComparator {
  bool operator()(const StructKey& l, const StructKey& r) const {
    return l.value < r.value;
  }
};

}  // namespace

template <>
class IntervalTreeLimits<StructKey> {
 public:
  static StructKey min() {
    return StructKey(std::numeric_limits<int32_t>::min());
  }
  static StructKey max() {
    return StructKey(std::numeric_limits<int32_t>::max());
  }
};

namespace {

template <typename T>
class IntervalNodeTest : public ::testing::Test {};

using IntervalNodeTypes = ::testing::Types<int, int64_t, double>;
TYPED_TEST_SUITE(IntervalNodeTest, IntervalNodeTypes);

TYPED_TEST(IntervalNodeTest, TestIntervalNode) {
  using T = TypeParam;
  VLOG(1) << "Check IntervalNode<T, int>";
  IntervalNode<T, int> node(1, 5);
  *(node.ptr()) = 15;

  EXPECT_EQ(node.value, 15);
  EXPECT_EQ(node.begin, 1);
  EXPECT_EQ(node.end, 5);

  EXPECT_GT(node.max_value(), node.min_value());
  EXPECT_GT(node.max_value(), T(0));
  EXPECT_LT(node.min_value(), T(0));
}

template <typename K, typename V = char, typename C = std::less<K>>
struct IntervalTreeTestParam {
  using Key = K;
  using Value = V;
  using Comparator = C;
  using Tree = IntervalTree<Key, Value, Comparator>;
  static Key k() { return Key(0); }
};

template <>
struct IntervalTreeTestParam<StructKey, char, StructKeyComparator> {
  using Key = StructKey;
  using Value = char;
  using Comparator = StructKeyComparator;
  using Tree = IntervalTree<Key, Value, Comparator>;
  static Key k() { return StructKey(100); }
};

template <typename T>
class IntervalTreeBasicTest : public ::testing::Test {};

using IntervalTreeTypes = ::testing::Types<
    IntervalTreeTestParam<int>, IntervalTreeTestParam<int64_t>,
    IntervalTreeTestParam<double>,
    IntervalTreeTestParam<StructKey, char, StructKeyComparator>>;
TYPED_TEST_SUITE(IntervalTreeBasicTest, IntervalTreeTypes);

TYPED_TEST(IntervalTreeBasicTest, TestIntervalTree) {
  using Param = TypeParam;
  using Key = typename Param::Key;
  using Comparator = typename Param::Comparator;
  using Tree = typename Param::Tree;

  Comparator less;
  Tree tree(less);
  Key k = Param::k();

  VLOG(1) << "Check Insert";
  EXPECT_EQ(tree.size(), 0);
  ASSERT_TRUE(InsertAndCheck(&tree, k - 10, k + 2, 'a'));
  ASSERT_TRUE(InsertAndCheck(&tree, k + 5, k + 6, 'e'));
  ASSERT_TRUE(InsertAndCheck(&tree, k + 7, k + 8, 'f'));
  ASSERT_TRUE(InsertAndCheck(&tree, k + 2, k + 3, 'c'));
  ASSERT_TRUE(InsertAndCheck(&tree, k + 5, k + 7, 'd'));
  ASSERT_TRUE(InsertAndCheck(&tree, k + 2, k + 5, 'b'));
  ASSERT_TRUE(InsertAndCheck(&tree, k + 10, k + 12, 'g'));

  typename decltype(tree)::iterator iter(&tree, k - 2, k + 10,
                                         INTERVAL_SMALLEST);

  VLOG(1) << "Check IntervalIterator";
  EXPECT_THAT(iter.Get(), NodeValue('a'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(iter.Next(), NodeValue('b'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(iter.Next(), NodeValue('c'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(iter.Next(), NodeValue('d'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(iter.Next(), NodeValue('e'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(iter.Get(), NodeValue('e'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(iter.Next(), NodeValue('f'));
  ASSERT_TRUE(tree.CheckInvariants());
  tree.InsertVal(k + 10, k + 11, 'h');
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(iter.Next(), NodeValue('g'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_THAT(iter.Next(), NodeValue('h'));
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_EQ(iter.Next(), nullptr);
  ASSERT_TRUE(tree.CheckInvariants());
  EXPECT_EQ(iter.Get(), nullptr);
  ASSERT_TRUE(tree.CheckInvariants());
}

}  // namespace
