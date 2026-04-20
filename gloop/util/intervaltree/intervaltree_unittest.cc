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

#include "gloop/util/intervaltree/intervaltree.h"

#include <stdio.h>  // why not use <cstdio>?

#include <cstdint>
#include <cstdlib>
#include <ctime>
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
#include "gloop/base/init_google.h"
#include "gloop/util/random/acmrandom.h"

namespace {

// ------------------------------------------------------------------
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

void CheckClosedIntervals(TreeFactory& factory) {
  auto tree = factory.Create<int, char>();
  const int begin = 1;
  const int end = 3;
  tree.InsertVal(begin, end, 'a');
  IntervalIterator<int, char> left_it(&tree, begin - 1, begin,
                                      INTERVAL_SMALLEST);
  CHECK_EQ(left_it.Get()->begin, begin);
  CHECK_EQ(left_it.Get()->end, end);
  IntervalIterator<int, char> right_it(&tree, end, end + 1, INTERVAL_SMALLEST);
  CHECK_EQ(left_it.Get()->begin, begin);
  CHECK_EQ(left_it.Get()->end, end);
}

void CheckFoo(TreeFactory& factory) {
  auto tree = factory.Create<int, char>();  // Create an interval tree
  tree.InsertVal(1, 3, 'a');  // Insert an element (requires copy constructor)
  IntervalNode<int, char>* node =
      tree.Insert(2, 4);  // No copy constructor called
  node->value = 'b';      // Assign the value of interval [2, 4] to 'b'

  // Create an iterator that finds all intervals intersecting [2, 3],
  // The last flag decide if its initialized to smallest (or largest)
  IntervalIterator<int, char> iter(&tree, 2, 3, INTERVAL_SMALLEST);
  CHECK_EQ(iter.Get()->value, 'a');
  CHECK_EQ(iter.Get()->begin, 1);
  CHECK_EQ(iter.Get()->end, 3);
  iter.Next();  // move to the next intersecting interval
  CHECK_EQ(iter.Get()->value, 'b');
  iter.Prev();  // move to the previous intersecting interval
  CHECK_EQ(iter.Get()->value, 'a');
  iter.Delete();  // delete the interval 'a' and move to the next element
  CHECK_EQ(iter.Get()->value, 'b');
  iter.Next();
  CHECK(iter.Get() == nullptr);  // we already moved to the end
}

void CheckInsertValUniquePtr(TreeFactory& factory) {
  auto tree = factory.Create<int, std::unique_ptr<int>>();
  tree.InsertVal(1, 3, std::make_unique<int>(3));
  IntervalTree<int, std::unique_ptr<int>>::iterator iter(&tree, 0, 10,
                                                         INTERVAL_SMALLEST);
  CHECK_EQ(*iter.Get()->value, 3);
  iter.Next();
  CHECK(iter.Get() == nullptr);  // we already moved to the end
}

void CheckInsertValTemporaryValue(TreeFactory& factory) {
  auto tree = factory.Create<int, std::string>();
  tree.InsertVal(1, 3, absl::StrCat(3, "aaa", 3));
  IntervalTree<int, std::string>::iterator iter(&tree, 0, 10,
                                                INTERVAL_SMALLEST);
  CHECK_EQ(iter.Get()->value, "3aaa3");
  iter.Next();
  CHECK(iter.Get() == nullptr);  // we already moved to the end
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

void CheckInsertValAggregate(TreeFactory& factory) {
  auto tree = factory.Create<int, Aggregate>();
  tree.InsertVal(1, 3, {4, 5});
  IntervalTree<int, Aggregate>::iterator iter(&tree, 0, 10, INTERVAL_SMALLEST);
  auto value = iter.Get()->value;
  CHECK_EQ(value, Aggregate({4, 5}));
  iter.Next();
  CHECK(iter.Get() == nullptr);  // we already moved to the end
}

void CheckBug(TreeFactory& factory) {
  // Bug regression test
  auto tree = factory.Create<int, char>();
  tree.InsertVal(0, 0, 'x');
  tree.InsertVal(0, 7, 'y');

  IntervalIterator<int, char> iter(&tree, 4, 7, INTERVAL_SMALLEST);
  CHECK(iter.Get() != nullptr);
}

void CheckNodeIteratorBug(TreeFactory& factory) {
  // Regression test for bug encountered with node-based iterators.
  auto tree = factory.Create<int, char>();
  IntervalNode<int, char>* node_1 = tree.InsertVal(0, 10, 'x');
  tree.InsertVal(0, 10, 'y');
  IntervalIterator<int, char> node_1_iter(&tree, node_1);
}

void CheckConst(const IntervalTree<int, std::string>* tree) {
  // You can only create ConstIntervalIterator from const IntervalTree
  const ConstIntervalIterator<int, std::string> iter(tree, 1, 5,
                                                     INTERVAL_SMALLEST);
  CHECK_EQ(iter.value(), "ac");
  // One can not move to next in a const iterator

  ConstIntervalIterator<int, std::string> del(tree, 1, 5, INTERVAL_SMALLEST);
  del.Next();
  // One can not delete from a const tree
}

template <typename T, typename U, typename KeyLess>
void InsertAndCheck(IntervalTree<T, U, KeyLess>* tree, const T& begin,
                    const T& end, U data) {
  int size = tree->size();
  tree->InsertVal(begin, end, data);
  CHECK_EQ(size + 1, tree->size());
  tree->CheckInvariants();
}

}  // namespace

template <class K>
void TestIntervalNode(const K& begin, const K& end) {
  VLOG(1) << "Check IntervalNode<int, int>";
  IntervalNode<K, int> node(begin, end);
  *(node.ptr()) = 15;

  CHECK_EQ(node.value, 15);
  CHECK_EQ(node.begin, begin);
  CHECK_EQ(node.end, end);

  CHECK(node.max_value() > node.min_value());
  CHECK(node.max_value() > 0);
  CHECK(node.min_value() < 0);
}

template <class K, class KeyLess = std::less<K>>
void TestIntervalTree(const K& k, const KeyLess& less = KeyLess()) {
  IntervalTree<K, char, KeyLess> tree(less);

  VLOG(1) << "Check Insert";
  CHECK_EQ(tree.size(), 0);
  InsertAndCheck(&tree, k - 10, k + 2, 'a');
  InsertAndCheck(&tree, k + 5, k + 6, 'e');
  InsertAndCheck(&tree, k + 7, k + 8, 'f');
  InsertAndCheck(&tree, k + 2, k + 3, 'c');
  InsertAndCheck(&tree, k + 5, k + 7, 'd');
  InsertAndCheck(&tree, k + 2, k + 5, 'b');
  InsertAndCheck(&tree, k + 10, k + 12, 'g');

  typename IntervalTree<K, char, KeyLess>::iterator iter(&tree, k - 2, k + 10,
                                                         INTERVAL_SMALLEST);

  VLOG(1) << "Check IntervalIterator";
  CHECK_EQ(iter.Get()->value, 'a');
  tree.CheckInvariants();
  CHECK_EQ(iter.Next()->value, 'b');
  tree.CheckInvariants();
  CHECK_EQ(iter.Next()->value, 'c');
  tree.CheckInvariants();
  CHECK_EQ(iter.Next()->value, 'd');
  tree.CheckInvariants();
  CHECK_EQ(iter.Next()->value, 'e');
  tree.CheckInvariants();
  CHECK_EQ(iter.Get()->value, 'e');
  tree.CheckInvariants();
  CHECK_EQ(iter.Next()->value, 'f');
  tree.CheckInvariants();
  tree.InsertVal(k + 10, k + 11, 'h');
  tree.CheckInvariants();
  CHECK_EQ(iter.Next()->value, 'g');
  tree.CheckInvariants();
  CHECK_EQ(iter.Next()->value, 'h');
  tree.CheckInvariants();
  CHECK(iter.Next() == nullptr);
  tree.CheckInvariants();
  CHECK(iter.Get() == nullptr);
  tree.CheckInvariants();
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
    CHECK(global_count_ >= 0);
  }
  int field_;
  static int global_count_;
};

// static
int InitRequired::global_count_ = 0;

void TestCtorAndDtor(TreeFactory& factory) {
  auto tree = factory.Create<int, InitRequired>();

  IntervalNode<int, InitRequired>* node = tree.Insert(0, 1);
  CHECK_EQ(node->value.field_, 12345);
  CHECK_EQ(1, InitRequired::global_count_);

  // check destructor gets invoked.
  IntervalTree<int, InitRequired>::iterator iter(&tree, 0, 1);
  iter.Delete();
  CHECK_EQ(0, InitRequired::global_count_);
  CHECK_EQ(0, tree.size());

  // check constructor is invoked on re-use from freelist.
  node = tree.Insert(2, 3);
  CHECK_EQ(node->value.field_, 12345);
  CHECK_EQ(1, tree.size());

  // Tree will now get destroyed;  if the tree destruction re-invokes
  // destructors on deleted elements, this will be caught in the destructor
  // for InitRequired.
}

void TestCallerProvidedArena() {
  UnsafeArena arena(1024);
  {
    IntervalTree<int, InitRequired> tree(&arena);
    tree.Insert(0, 1);
    // destroy tree before arena
  }
}

void TestCtorAndDtor2(TreeFactory& factory) {
  auto tree = factory.Create<int, InitRequired>();

  InitRequired my_obj;  // this will be copied.
  IntervalNode<int, InitRequired>* node = tree.InsertVal(0, 1, my_obj);
  CHECK_EQ(node->value.field_, 12345);
  CHECK_EQ(2, InitRequired::global_count_);

  // add a second.
  node = tree.Insert(0, 1);

  // remove one, and check destructor gets invoked.
  IntervalTree<int, InitRequired>::iterator iter(&tree, 0, 1);
  iter.Delete();
  CHECK_EQ(2, InitRequired::global_count_);
  CHECK_EQ(1, tree.size());

  // (non-empty) Tree will now get destroyed;  if the tree destruction
  // re-invokes destructors on deleted elements, this will be caught in
  // the destructor for InitRequired.
}

void TestIntervalTreeBasic(TreeFactory& factory) {
  auto tree = factory.Create<int, char>();

  VLOG(1) << "Check Insert";
  CHECK_EQ(tree.size(), 0);
  InsertAndCheck(&tree, -10, 2, 'a');
  InsertAndCheck(&tree, 5, 6, 'e');
  InsertAndCheck(&tree, 7, 8, 'f');
  InsertAndCheck(&tree, 2, 3, 'c');
  InsertAndCheck(&tree, 5, 7, 'd');
  InsertAndCheck(&tree, 2, 5, 'b');
  InsertAndCheck(&tree, 10, 12, 'g');

  IntervalTree<int, char>::iterator iter(&tree, -2, 10, INTERVAL_SMALLEST);

  VLOG(1) << "Check IntervalIterator";
  CHECK_EQ(iter.Get()->value, 'a');
  tree.CheckInvariants();
  CHECK_EQ(iter.Next()->value, 'b');
  tree.CheckInvariants();
  CHECK_EQ(iter.Next()->value, 'c');
  tree.CheckInvariants();
  CHECK_EQ(iter.Next()->value, 'd');
  tree.CheckInvariants();
  CHECK_EQ(iter.Next()->value, 'e');
  tree.CheckInvariants();
  CHECK_EQ(iter.Get()->value, 'e');
  tree.CheckInvariants();
  CHECK_EQ(iter.Next()->value, 'f');
  tree.CheckInvariants();
  tree.InsertVal(10, 11, 'h');
  tree.CheckInvariants();
  CHECK_EQ(iter.Next()->value, 'g');
  tree.CheckInvariants();
  CHECK_EQ(iter.Next()->value, 'h');
  tree.CheckInvariants();
  CHECK(iter.Next() == nullptr);
  tree.CheckInvariants();
  CHECK(iter.Get() == nullptr);
  tree.CheckInvariants();

  VLOG(1) << "Check ConstIntervalIterator";
  IntervalTree<int, char>::const_iterator iter2(&tree, 5, 7, INTERVAL_SMALLEST);
  CHECK_EQ(iter2.Get()->value, 'b');
  CHECK_EQ(iter2.value(), 'b');
  tree.CheckInvariants();
  CHECK_EQ(iter2.Next()->value, 'd');
  tree.CheckInvariants();
  CHECK_EQ(iter2.Next()->value, 'e');
  tree.CheckInvariants();
  CHECK_EQ(iter2.Next()->value, 'f');
  tree.CheckInvariants();
  CHECK(iter2.Next() == nullptr);
  tree.CheckInvariants();

  VLOG(1) << "Check Delete";

  tree.CheckInvariants();
  IntervalIterator<int, char> iter3(&tree, 5, 7, INTERVAL_SMALLEST);

  IntervalIterator<int, char> del(&tree, 7, 8, INTERVAL_SMALLEST);
  del.Delete();
  tree.CheckInvariants();

  CHECK_EQ(iter3.Get()->value, 'b');
  // c is not in range, and d is deleted
  CHECK_EQ(iter3.Next()->value, 'e');
  CHECK_EQ(iter3.Next()->value, 'f');
  tree.CheckInvariants();

  VLOG(1) << "Check Delete 2";
  IntervalIterator<int, char> iter4(&tree, 1, 12, INTERVAL_SMALLEST);
  CHECK_EQ(iter4.Get()->value, 'a');
  tree.CheckInvariants();
  CHECK_EQ(iter4.Delete()->value, 'b');
  tree.CheckInvariants();
  CHECK_EQ(iter4.Delete()->value, 'c');
  tree.CheckInvariants();
  CHECK_EQ(iter4.Delete()->value, 'e');
  tree.CheckInvariants();
  CHECK_EQ(iter4.Delete()->value, 'f');
  tree.CheckInvariants();
  CHECK_EQ(iter4.Delete()->value, 'g');
  tree.CheckInvariants();
  CHECK_EQ(iter4.Delete()->value, 'h');
  tree.CheckInvariants();
  CHECK(iter4.Delete() == nullptr);
  tree.CheckInvariants();

  VLOG(1) << "Check multiple IntervalIterator's with updates";

  InsertAndCheck(&tree, 1, 1, 'a');
  IntervalIterator<int, char> i1(&tree, 1, 10, INTERVAL_SMALLEST);
  InsertAndCheck(&tree, 2, 2, 'b');
  IntervalIterator<int, char> i2(&tree, 2, 10, INTERVAL_SMALLEST);
  InsertAndCheck(&tree, 3, 3, 'c');
  IntervalIterator<int, char> i3(&tree, 3, 10, INTERVAL_SMALLEST);
  InsertAndCheck(&tree, 4, 4, 'd');
  InsertAndCheck(&tree, 5, 5, 'e');
  InsertAndCheck(&tree, 7, 7, 'g');
  InsertAndCheck(&tree, 7, 7, 'g');

  // (x, y, z) means:
  //   i1.Get()->value, x, i2.Get()->value, y, i3.Get()->value, z
  CHECK_EQ(i1.Get()->value, 'a');  // (a, b, c)
  CHECK_EQ(i2.Get()->value, 'b');
  CHECK_EQ(i3.Get()->value, 'c');
  tree.CheckInvariants();
  CHECK_EQ(i1.Delete()->value, 'b');  // (b, b, c) delete a
  tree.CheckInvariants();
  CHECK_EQ(i3.Delete()->value, 'd');  // (b, b, d) delete c
  tree.CheckInvariants();
  CHECK_EQ(i2.Next()->value, 'd');    // (b, d, d)
  CHECK_EQ(i2.Next()->value, 'e');    // (b, e, d)
  CHECK_EQ(i3.Delete()->value, 'e');  // (b, e, e) delete d
  CHECK_EQ(i3.Next()->value, 'g');    // (b, e, g)
  tree.InsertVal(3, 3, 'c');          // (b, e, g) insert c
  tree.InsertVal(6, 6, 'f');
  CHECK_EQ(i1.Delete()->value, 'c');  // (c, e, g) delete b
  tree.CheckInvariants();
  CHECK_EQ(i1.Delete()->value, 'e');  // (e, e, g) delete c
  tree.CheckInvariants();
  CHECK_EQ(i1.Get(), i2.Get());
  CHECK_EQ(i2.Next()->value, 'f');  // (e, f, g)
  CHECK_EQ(i2.Next()->value, 'g');  // (e, g, g)
  CHECK_EQ(i2.Get(), i3.Get());
  CHECK_EQ(i2.Next()->value, 'g');  // (e, g2, g)
  CHECK_NE(i2.Get(), i3.Get());
  CHECK_EQ(i3.Delete()->value, 'g');  // (e, g2, g2) delete first g
  tree.CheckInvariants();
  CHECK_EQ(i1.Delete()->value, 'f');  // (f, g2, g2) delete e
  tree.CheckInvariants();
  CHECK_EQ(i1.Delete()->value, 'g');  // (g2, g2, g2) delete f
  tree.CheckInvariants();
  CHECK_EQ(i1.Get(), i2.Get());
  CHECK_EQ(i1.Get(), i3.Get());
  CHECK(i2.Next() == nullptr);    // (g2, 0, g2)
  CHECK(i3.Next() == nullptr);    // (g2, 0, 0
  CHECK(i1.Delete() == nullptr);  // (0, 0, 0) delete second g
  tree.CheckInvariants();
  CHECK_EQ(tree.size(), 0);
  tree.InsertVal(4, 4, 'x');
  tree.InsertVal(5, 5, 'f');
  tree.CheckInvariants();
}

void TestIntervalTreeStrings(TreeFactory& factory) {
  VLOG(1) << "Check Stable";

  auto str = factory.Create<int, std::string>();

  InsertAndCheck(&str, 1, 1, std::string("a1"));
  InsertAndCheck(&str, 2, 2, std::string("b1"));
  InsertAndCheck(&str, 3, 3, std::string("c5"));
  InsertAndCheck(&str, 1, 1, std::string("a2"));
  InsertAndCheck(&str, 2, 2, std::string("b2"));
  InsertAndCheck(&str, 1, 3, std::string("ac"));
  InsertAndCheck(&str, 3, 3, std::string("c4"));
  InsertAndCheck(&str, 3, 3, std::string("c3"));
  InsertAndCheck(&str, 2, 2, std::string("b3"));
  InsertAndCheck(&str, 1, 1, std::string("a3"));
  InsertAndCheck(&str, 2, 2, std::string("b4"));
  InsertAndCheck(&str, 3, 3, std::string("c2"));
  InsertAndCheck(&str, 2, 2, std::string("b5"));
  InsertAndCheck(&str, 3, 3, std::string("c1"));
  InsertAndCheck(&str, 1, 1, std::string("a4"));
  InsertAndCheck(&str, 1, 1, std::string("a5"));
  InsertAndCheck(&str, 1, 2, std::string("ab"));
  InsertAndCheck(&str, 2, 4, std::string("bd"));

  IntervalIterator<int, std::string> nullIter1(&str, -10, 0, INTERVAL_SMALLEST);
  CHECK(nullIter1.Get() == nullptr);
  IntervalIterator<int, std::string> nullIter2(&str, 5, 6, INTERVAL_LARGEST);
  CHECK(nullIter2.Get() == nullptr);

  IntervalIterator<int, std::string> strIter1(&str, 1, 5, INTERVAL_SMALLEST);
  CHECK_EQ(strIter1.value(), "ac");
  str.CheckInvariants();
  CHECK_EQ(strIter1.Next()->value, "ab");
  str.CheckInvariants();
  CHECK_EQ(strIter1.Next()->value, "a1");
  str.CheckInvariants();
  CHECK_EQ(strIter1.Next()->value, "a2");
  str.CheckInvariants();
  CHECK_EQ(strIter1.Next()->value, "a3");
  str.CheckInvariants();
  CHECK_EQ(strIter1.Next()->value, "a4");
  str.CheckInvariants();
  CHECK_EQ(strIter1.Next()->value, "a5");
  str.CheckInvariants();
  *(str.Insert(1, 1)->ptr()) = "a6";  // same as str.InsertVal(1, 1, "a6");
  str.InsertVal(1, 1, "a7");
  CHECK_EQ(strIter1.Next()->value, "a6");
  str.CheckInvariants();
  CHECK_EQ(strIter1.Next()->value, "a7");
  str.CheckInvariants();
  CHECK_EQ(strIter1.Next()->value, "bd");
  str.CheckInvariants();
  CHECK_EQ(strIter1.Next()->value, "b1");
  str.CheckInvariants();
  CHECK_EQ(strIter1.Next()->value, "b2");
  str.CheckInvariants();
  CHECK_EQ(strIter1.Next()->value, "b3");
  str.CheckInvariants();
  CHECK_EQ(strIter1.Next()->value, "b4");
  str.CheckInvariants();
  CHECK_EQ(strIter1.Next()->value, "b5");
  str.CheckInvariants();
  CHECK_EQ(strIter1.Next()->value, "c5");
  str.CheckInvariants();
  CHECK_EQ(strIter1.Next()->value, "c4");
  str.CheckInvariants();
  CHECK_EQ(strIter1.Next()->value, "c3");
  str.CheckInvariants();
  CHECK_EQ(strIter1.Next()->value, "c2");
  str.CheckInvariants();
  CHECK_EQ(strIter1.Next()->value, "c1");
  str.CheckInvariants();

  VLOG(1) << "Check Const";
  CheckConst(&str);

  VLOG(1) << "Check Previous";

  IntervalIterator<int, std::string> strIterNull(&str, 3, 3, INTERVAL_SMALLEST);
  CHECK(strIterNull.Prev() == nullptr);

  CHECK_EQ(strIter1.Prev()->value, "c2");
  str.CheckInvariants();
  CHECK_EQ(strIter1.Prev()->value, "c3");
  str.CheckInvariants();
  CHECK_EQ(strIter1.Prev()->value, "c4");
  str.CheckInvariants();

  IntervalIterator<int, std::string> strIter2(&str, 3, 4, INTERVAL_LARGEST);
  IntervalIterator<int, std::string> strIter3(&str, 2, 3, INTERVAL_LARGEST);

  CHECK_EQ(strIter2.Get()->value, "c1");
  str.CheckInvariants();
  CHECK_EQ(strIter3.Get()->value, "c1");
  str.CheckInvariants();

  CHECK_EQ(strIter2.Prev()->value, "c2");
  str.CheckInvariants();
  CHECK_EQ(strIter3.Prev()->value, "c2");
  str.CheckInvariants();
  CHECK_EQ(strIter2.Prev()->value, "c3");
  str.CheckInvariants();
  CHECK_EQ(strIter3.Prev()->value, "c3");
  str.CheckInvariants();

  CHECK_EQ(strIter1.Prev()->value, "c5");
  str.CheckInvariants();
  CHECK_EQ(strIter2.Prev()->value, "c4");
  str.CheckInvariants();
  CHECK_EQ(strIter3.Prev()->value, "c4");
  str.CheckInvariants();

  VLOG(1) << "Check ResetRange";

  strIter2.ResetRange(1, 1);
  CHECK_EQ(strIter2.Prev()->value, "a7");
  str.CheckInvariants();
  strIter2.ResetRange(2, 2);
  CHECK_EQ(strIter2.Next()->value, "bd");
  str.CheckInvariants();
  CHECK_EQ(strIter2.Next()->value, "b1");
  str.CheckInvariants();
  strIter2.ResetRange(3, 3);
  CHECK_EQ(strIter2.Next()->value, "c5");
  str.CheckInvariants();

  VLOG(1) << "Check other constructors";

  IntervalIterator<int, std::string> strIter4(&str, strIter2.Get());
  strIter4.ResetRange(2, 4);
  CHECK_EQ(strIter4.Next()->value, "c4");

  IntervalIterator<int, std::string> strIter5(&str, 2, 3);
  CHECK_EQ(strIter5.value(), "b1");

  IntervalIterator<int, std::string> strIter6(&str, 2, 2);
  CHECK_EQ(strIter6.value(), "b1");

  InsertAndCheck(&str, 1, 3, std::string("ac2"));
  IntervalIterator<int, std::string> strIter7(&str, 1, 3);
  CHECK_EQ(strIter7.value(), "ac");
}

void TestIntervalTreeRanges(TreeFactory& factory) {
  VLOG(1) << "Check overlaps";
  auto integer = factory.Create<int, int>();
  integer.InsertVal(0, 0, 1);
  integer.InsertVal(0, 1, 0);
  integer.InsertVal(1, 2, 2);
  integer.InsertVal(3, 3, 4);
  integer.InsertVal(3, 5, 3);
  integer.InsertVal(5, 5, 6);
  integer.InsertVal(5, 5, 7);
  integer.InsertVal(5, 7, 5);
  integer.InsertVal(7, 8, 8);
  int max;

  VLOG(2) << integer.DebugString();

  max = 0;
  for (IntervalIterator<int, int> iter(&integer, 0, 10, INTERVAL_SMALLEST);
       iter.Next() != nullptr;) {
    CHECK_EQ(iter.value(), max + 1);
    max = iter.value();
  }

  integer.Reset();
  integer.InsertVal(0, 1, 0);
  integer.InsertVal(1, 1, 1);
  integer.InsertVal(2, 2, 2);
  integer.InsertVal(3, 3, 4);
  integer.InsertVal(3, 5, 3);
  integer.InsertVal(5, 5, 7);
  integer.InsertVal(5, 6, 6);
  integer.InsertVal(5, 7, 5);
  integer.InsertVal(7, 8, 8);

  max = 8;
  for (IntervalIterator<int, int> iter(&integer, 0, 10, INTERVAL_LARGEST);
       iter.Prev() != nullptr;) {
    CHECK_EQ(iter.value(), max - 1);
    max = iter.value();
  }
  integer.Reset();
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

  max = 0;
  for (ConstIntervalIterator<int, int> iter(&integer, 0, 10, INTERVAL_SMALLEST);
       iter.Next() != nullptr;) {
    CHECK_EQ(iter.value(), max + 1);
    max = iter.value();
  }

  VLOG(1) << "Check how Vector handles duplicate";

  integer.Reset();
  integer.InsertVal(1, 3, 1);
  integer.InsertVal(1, 3, 2);
  integer.InsertVal(3, 4, 3);
  integer.InsertVal(3, 4, 4);
  integer.InsertVal(6, 6, 5);

  {
    IntervalIterator<int, int> iter0(&integer, -5, -3, INTERVAL_SMALLEST);
    CHECK(iter0.Get() == nullptr);

    IntervalIterator<int, int> iter1(&integer, 9, 9, INTERVAL_SMALLEST);
    CHECK(iter1.Get() == nullptr);

    IntervalIterator<int, int> iter2(&integer, 3, 3, INTERVAL_SMALLEST);
    CHECK_EQ(iter2.value(), 1);
    CHECK(iter2.Prev() == nullptr);

    IntervalIterator<int, int> iter3(&integer, 5, 5, INTERVAL_SMALLEST);
    CHECK(iter3.Get() == nullptr);

    IntervalIterator<int, int> iter4(&integer, 5, 6, INTERVAL_SMALLEST);
    CHECK_EQ(iter4.value(), 5);

    IntervalIterator<int, int> iter5(&integer, 4, 5, INTERVAL_SMALLEST);
    CHECK_EQ(iter5.value(), 3);
  }

  VLOG(1) << "Check how backward iterator handles duplicate";
  {
    IntervalIterator<int, int> iter1(&integer, 5, 5, INTERVAL_LARGEST);
    CHECK(iter1.Get() == nullptr);

    IntervalIterator<int, int> iter2(&integer, 3, 3, INTERVAL_LARGEST);
    CHECK_EQ(iter2.value(), 4);

    IntervalIterator<int, int> iter3(&integer, 5, 6, INTERVAL_LARGEST);
    CHECK_EQ(iter3.value(), 5);
  }
}

// Return true if two integer-based interval trees equal.
bool TreeEqual(const IntervalTree<int, int>& tree1,
               const IntervalTree<int, int>& tree2) {
  absl::node_hash_map<std::string, std::set<int>> ht1, ht2;
  char interval_buffer[100];
  ConstIntervalIterator<int, int> iter1(&tree1, std::numeric_limits<int>::min(),
                                        std::numeric_limits<int>::max());
  ConstIntervalIterator<int, int> iter2(&tree2, std::numeric_limits<int>::min(),
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

void TestMakeLinearCopy(TreeFactory& factory) {
  unsigned int seed = time(nullptr);
  const auto& default_copy_function = [](const int src_value,
                                         int* dst_value) -> void {
    *dst_value = src_value;
  };
  // empty tree.
  {
    auto tree = factory.Create<int, int>();
    tree.UpdateStructure();
    auto tree_copy = tree.MakeLinearCopy(default_copy_function);
    CHECK(tree_copy != nullptr);
    CHECK(TreeEqual(*tree_copy, tree));
    delete tree_copy;
  }
  // vector structure.
  {
    for (int trail = 0; trail < 10; ++trail) {
      auto tree = factory.Create<int, int>();
      std::vector<IntervalTree<int, int>::TreeNode> operation_nodes;
      for (int i = 1000; i >= 0; --i) {
        int span = rand_r(&seed) % 4;
        int begin = i * 10 - span;
        int end = i * 10 + span;
        if (i > 10)
          tree.InsertVal(begin, end, i);
        else
          operation_nodes.push_back(IntervalNode<int, int>(begin, end, i));
      }
      tree.UpdateStructure();
      auto tree_copy = tree.MakeLinearCopy(default_copy_function);
      CHECK(tree_copy != nullptr);
      CHECK(TreeEqual(*tree_copy, tree));
      // Add more nodes to tree and tree_copy, then check if they are equal.
      for (const auto& node : operation_nodes) {
        tree.InsertVal(node.begin, node.end, node.value);
        tree_copy->InsertVal(node.begin, node.end, node.value);
      }
      tree.UpdateStructure();
      tree_copy->UpdateStructure();
      CHECK(TreeEqual(*tree_copy, tree));
      // Delete nodes from tree and tree_copy, then check if they are equal.
      for (int i = 0; i < operation_nodes.size(); ++i) {
        int begin = operation_nodes[i].begin;
        int end = operation_nodes[i].end;
        IntervalIterator<int, int> iter1(&tree, begin, end, INTERVAL_SMALLEST);
        IntervalIterator<int, int> iter2(tree_copy, begin, end,
                                         INTERVAL_SMALLEST);
        if (iter1.Get()) iter1.Delete();
        if (iter2.Get()) iter2.Delete();
        // Check iter reaches end after deleting the interval, because there
        // can only be one matching interval as intervals are not overlapping.
        CHECK_EQ(nullptr, iter1.Get());
        CHECK_EQ(nullptr, iter2.Get());
        // Check the trees are equal after deleting the same interval.
        tree.UpdateStructure();
        tree_copy->UpdateStructure();
        CHECK(TreeEqual(*tree_copy, tree));
      }
      // Clean up.
      delete tree_copy;
    }
  }
  // tree structure.
  {
    std::vector<IntervalTree<int, int>::TreeNode> operation_nodes;
    for (int trail = 0; trail < 10; ++trail) {
      auto tree = factory.Create<int, int>();
      for (int i = 0; i < 1000; ++i) {
        int span = rand_r(&seed) % 15;
        int begin = i * 10 - span;
        int end = i * 10 + span;
        if (i > 10)
          tree.InsertVal(begin, end, i);
        else
          operation_nodes.push_back(IntervalNode<int, int>(begin, end, i));
      }
      tree.UpdateStructure();
      auto tree_copy = tree.MakeLinearCopy(default_copy_function);
      CHECK(tree_copy != nullptr);
      CHECK(TreeEqual(*tree_copy, tree));
      // Add more nodes to tree and tree_copy, then check if they are equal.
      for (const auto& node : operation_nodes) {
        tree.InsertVal(node.begin, node.end, node.value);
        tree_copy->InsertVal(node.begin, node.end, node.value);
      }
      tree.UpdateStructure();
      tree_copy->UpdateStructure();
      CHECK(TreeEqual(*tree_copy, tree));
      // Delete nodes from tree and tree_copy, then check if they are equal.
      int begin = 10;
      int end = 100;
      IntervalIterator<int, int> iter1(&tree, begin, end, INTERVAL_SMALLEST);
      IntervalIterator<int, int> iter2(tree_copy, begin, end,
                                       INTERVAL_SMALLEST);
      while (iter1.Get()) {
        iter1.Delete();
      }
      while (iter2.Get()) {
        iter2.Delete();
      }
      tree.UpdateStructure();
      tree_copy->UpdateStructure();
      CHECK(TreeEqual(*tree_copy, tree));
      // Clean up.
      delete tree_copy;
    }
  }
}

void TestMakeLinearCopyOnArena() {
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
  const IntervalNode<int, int64_t, std::less<int>>* node =
      IntervalIterator<int, int64_t>(tree_copy.get(), 0, 1, INTERVAL_SMALLEST)
          .Get();

  // check that the node was allocated on the arena.
  CHECK_GE(static_cast<const void*>(node), static_cast<const void*>(buffer));
  CHECK_LE(static_cast<const void*>(node + 1),
           static_cast<const void*>(buffer + sizeof(buffer)));
  CHECK_EQ(node->value, 64);
}

// Test that struct key w/o comparison operators works well with the interval
// tree.
namespace {
struct StructKey {
  explicit StructKey(int32_t value_in) : value(value_in) {}

  StructKey operator+(int delta) const {
    StructKey ret(value + delta);
    ;
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

}  // anonymous namespace

template <>
class IntervalTreeLimits<StructKey> {
 public:
  static const StructKey min() {
    return StructKey(std::numeric_limits<int32_t>::min());
  }
  static const StructKey max() {
    return StructKey(std::numeric_limits<int32_t>::max());
  }
};

void RunIntervalTreeTests(TreeFactory& factory) {
  VLOG(1) << "Check Closed Intervals";
  CheckClosedIntervals(factory);

  VLOG(1) << "Check Foo";
  CheckFoo(factory);

  VLOG(1) << "Check InsertVal";
  CheckInsertValUniquePtr(factory);
  CheckInsertValTemporaryValue(factory);
  CheckInsertValAggregate(factory);

  VLOG(1) << "Check Bug";
  CheckBug(factory);
  CheckNodeIteratorBug(factory);

  TestCtorAndDtor(factory);
  TestCtorAndDtor2(factory);
  TestIntervalNode<int>(1, 5);
  TestIntervalNode<int64_t>(1, 5);
  TestIntervalNode<double>(1, 5);
  TestIntervalTree<int>(0);
  TestIntervalTree<int64_t>(0);
  TestIntervalTree<double>(0.0);
  TestIntervalTree<StructKey, StructKeyComparator>(StructKey(100),
                                                   StructKeyComparator());

  TestIntervalTreeBasic(factory);
  TestIntervalTreeStrings(factory);
  TestIntervalTreeRanges(factory);
  TestMakeLinearCopy(factory);
  TestMakeLinearCopyOnArena();
}

int main(int argc, char** argv) {
  InitGoogle(argv[0], &argc, &argv, true);
  if (!benchmark::GetBenchmarkFilter().empty()) {
    benchmark::RunSpecifiedBenchmarks();
    exit(0);
  }
  for (TestMode mode : {TestMode::kDefaultArena, TestMode::kUserArena,
                        TestMode::kHeapAllocation}) {
    TreeFactory factory(mode);
    RunIntervalTreeTests(factory);
  }

  printf("PASS\n");
  return 0;
}
