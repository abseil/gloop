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

#include "gloop/util/gtl/heapqueue.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iterator>
#include <list>
#include <memory>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/random/random.h"
#include "benchmark/benchmark.h"
#include "gloop/util/gtl/comparator.h"
#include "gloop/util/gtl/stl_util.h"
#include "gloop/util/random/mt_random.h"
#include "gtest/gtest.h"

namespace gtl {
namespace {

TEST(HeapQueue, Basic) {
  HeapQueue<std::pair<float, char*> > heap;
  std::priority_queue<std::pair<float, char*> > pq;
  HeapQueue<std::pair<float, char*> > remove_queue;

  // Push a set of values onto both HeapQueue and priority_queue
  // Also compute the sum of those values
  double sum = 0;
  for (int i = 0; i < 10; ++i) {
    float value = 2.1 * i;
    sum += value;
    heap.push(std::pair<float, char*>(value, nullptr));
    pq.push(std::pair<float, char*>(value, nullptr));
    remove_queue.push(std::pair<float, char*>(value, nullptr));
  }

  // Test the iterator by using it to subtract all values from earlier sum
  HeapQueue<std::pair<float, char*> >::iterator it;
  for (it = heap.begin(); it != heap.end(); ++it) {
    sum -= it->first;
  }
  EXPECT_EQ(0, sum);

  // Testing pop() by poping values from both queues and compare
  float max = 1000;
  while (!heap.empty()) {
    float value = heap.top().first;
    EXPECT_EQ(value, pq.top().first);
    EXPECT_LE(value, max);
    max = value;
    heap.pop();
    pq.pop();
  }

  // Test erase() by removing values and ensuring the heap
  // condition is still met as miscellaneous elements are
  // removed from the heap.
  int iteration_mixer = 0;
  float previous_value = remove_queue.top().first;

  while (!remove_queue.empty()) {
    int iteration_count = iteration_mixer % remove_queue.size();

    HeapQueue<std::pair<float, char*> >::iterator iterator =
        remove_queue.begin();

    // Empty loop!
    for (int i = 0; i < iteration_count; ++i, ++iterator) {
    }

    remove_queue.erase(iterator);
    float value = remove_queue.top().first;
    remove_queue.pop();

    EXPECT_GE(previous_value, value);

    ++iteration_mixer;
    previous_value = value;
  }

  // Test replace_elements(), by doing the following:
  //   - build a vector of elements in some arbitrary order
  //   - build a HeapQueue from scratch by inserting each
  //     element from the vector
  //   - build a second HeapQueue by using replace_elements() on the
  //     original vector
  //   - make sure the two are equivalent
  std::vector<int> element_vector;
  for (int i = 0; i < 100; ++i) {
    for (int j = 0; j < 100; ++j) {
      element_vector.push_back(j * 100 + i);
    }
  }
  ASSERT_EQ(10000, element_vector.size());

  HeapQueue<int> q1;
  for (int i = 0; i < 10000; ++i) {
    q1.push(element_vector[i]);
  }
  EXPECT_EQ(10000, q1.size());

  HeapQueue<int> q2;
  q2.replace_elements(&element_vector);
  EXPECT_EQ(10000, q2.size());
  EXPECT_EQ(0, element_vector.size());

  for (int i = 10000 - 1; i >= 0; --i) {
    // Test consume_top here, top & pop have been tested above.
    EXPECT_EQ(i, q1.consume_top());
    EXPECT_EQ(i, q2.consume_top());
  }
  EXPECT_TRUE(q1.empty());
  EXPECT_TRUE(q2.empty());
}

TEST(HeapQueue, BasicWithMoveOnly) {
  HeapQueue<std::unique_ptr<int>, decltype(gtl::OrderByPointee())> heap;
  auto smaller = std::make_unique<int>(1);
  heap.push(std::move(smaller));
  EXPECT_EQ(1, *heap.top());
  auto bigger = std::make_unique<int>(2);
  heap.push(std::move(bigger));
  EXPECT_EQ(2, *heap.top());
  auto biggest = std::make_unique<int>(3);
  heap.push(std::move(biggest));
  // Biggest should be on top.
  EXPECT_EQ(3, *heap.top());

  biggest = heap.consume_top();
  EXPECT_EQ(3, *biggest);
  bigger = heap.consume_top();
  EXPECT_EQ(2, *bigger);
  smaller = heap.consume_top();
  EXPECT_EQ(1, *smaller);
  EXPECT_TRUE(heap.empty());
}

TEST(HeapQueue, MaxSize) {
  HeapQueue<int> q1;
  q1.set_max_size(6);
  for (int i = 1; i < 7; ++i) {
    EXPECT_TRUE(q1.push(i));
    EXPECT_EQ(i, q1.size());
  }

  EXPECT_FALSE(q1.push(7));
  EXPECT_TRUE(q1.push(0));
  EXPECT_EQ(6, q1.size());
}

TEST(HeapQueue, Copyable) {
  HeapQueue<int> q1;
  q1.push(1);

  HeapQueue<int> q2(q1);
  HeapQueue<int> q3;
  q3 = q2;

  q1.push(11);
  q1.push(12);
  q2.push(2);

  EXPECT_EQ(3, q1.size());
  EXPECT_EQ(12, q1.top());

  EXPECT_EQ(2, q2.size());
  EXPECT_EQ(2, q2.top());

  EXPECT_EQ(1, q3.size());
  EXPECT_EQ(1, q3.top());
}

TEST(HeapQueue, Movable) {
  using UniquePtrHeap =
      HeapQueue<std::unique_ptr<int>, decltype(gtl::OrderByPointee())>;
  UniquePtrHeap q1;
  q1.push(std::make_unique<int>(1));
  EXPECT_EQ(1, q1.size());

  UniquePtrHeap q2(std::move(q1));
  EXPECT_EQ(1, q2.size());
  EXPECT_EQ(1, *q2.top());
  q2.set_max_size(1);
  EXPECT_FALSE(q2.push(std::make_unique<int>(2)));

  UniquePtrHeap q3;
  EXPECT_TRUE(q3.push(std::make_unique<int>(10)));
  EXPECT_TRUE(q3.push(std::make_unique<int>(11)));
  EXPECT_TRUE(q3.push(std::make_unique<int>(12)));
  EXPECT_EQ(3, q3.size());
  EXPECT_EQ(12, *q3.top());

  q3 = std::move(q2);
  EXPECT_FALSE(q3.push(std::make_unique<int>(2)));
  EXPECT_EQ(1, q3.size());
  EXPECT_EQ(1, *q3.top());
}

using StringFreq = std::pair<std::string, int>;

class StringFreqRevCmp {
 public:
  bool operator()(const StringFreq& a, const StringFreq& b) const {
    return a.second > b.second;
  }
};

// Test with custom cmp function.
TEST(HeapQueue, CustomCmp) {
  HeapQueue<StringFreq, StringFreqRevCmp> freq;
  freq.set_max_size(2);
  const StringFreq zero("z", 0);
  const StringFreq one("a", 1);
  const StringFreq two("b", 2);
  const StringFreq three("c", 3);

  // push: [1, 0, 3, 2]
  // after these pushes expect: {2, 3}

  EXPECT_TRUE(freq.push(one));
  EXPECT_EQ(1, freq.size());
  EXPECT_EQ(freq.top(), one);

  EXPECT_TRUE(freq.push(zero));
  EXPECT_EQ(2, freq.size());
  EXPECT_EQ(freq.top(), zero);

  EXPECT_TRUE(freq.push(three));
  EXPECT_EQ(2, freq.size());
  EXPECT_EQ(freq.top(), one);

  EXPECT_TRUE(freq.push(two));
  EXPECT_EQ(2, freq.size());
  EXPECT_EQ(freq.top(), two);

  freq.pop();  // remove '3'
  EXPECT_EQ(1, freq.size());
  EXPECT_EQ(freq.top(), three);
}

TEST(HeapQueue, BulkRemovalRandom) {
  using UniquePtrHeap =
      HeapQueue<std::unique_ptr<int>, decltype(gtl::OrderByPointee())>;
  UniquePtrHeap heap;
  heap.push(std::make_unique<int>(1));
  heap.push(std::make_unique<int>(2));
  heap.push(std::make_unique<int>(3));
  heap.push(std::make_unique<int>(4));
  {
    HeapQueueBulkRemover<UniquePtrHeap> rem(&heap);
    for (auto it = heap.begin(); it != heap.end();) {
      if (**it % 2 == 0) {
        rem.remove(it);
      } else {
        ++it;
      }
    }
  }
  EXPECT_EQ(*heap.consume_top(), 3);
  EXPECT_EQ(*heap.consume_top(), 1);
  EXPECT_TRUE(heap.empty());
}

TEST(HeapQueue, BulkRemovalPredicateMoveOnly) {
  using UniquePtrHeap =
      HeapQueue<std::unique_ptr<int>, decltype(gtl::OrderByPointee())>;
  UniquePtrHeap heap;
  heap.push(std::make_unique<int>(1));
  heap.push(std::make_unique<int>(2));
  heap.push(std::make_unique<int>(3));
  heap.push(std::make_unique<int>(4));
  {
    HeapQueueBulkRemover<UniquePtrHeap> rem(&heap);
    rem.remove_if([](const auto& x) -> bool { return *x % 2 == 0; });
  }
  EXPECT_EQ(*heap.consume_top(), 3);
  EXPECT_EQ(*heap.consume_top(), 1);
  EXPECT_TRUE(heap.empty());
}

template <typename T>
class ExtendedHeapQueueTest : public ::testing::Test {
 protected:
  static constexpr int kMaxRandomValue = 5000;
  static constexpr int kRandomVectorIterations = 20;

  using ValueType = typename T::first_type;
  using Comp = typename T::second_type;
  using Vector = std::vector<ValueType>;
  using HeapType = HeapQueue<ValueType, Comp, Vector>;

  std::vector<ValueType> RandomVector(int elems) {
    std::vector<ValueType> s;
    for (int j = 0; j < elems; ++j) {
      s.push_back(rand_.RandDouble() * kMaxRandomValue);
    }
    return s;
  }

  void IsHeapWithSameElements(const HeapType& heap,
                              std::vector<ValueType>* expected) {
    EXPECT_TRUE(std::is_heap(heap.begin(), heap.end(), Comp()));
    std::vector<ValueType> real(heap.begin(), heap.end());
    std::sort(real.begin(), real.end(), Comp());
    std::sort(expected->begin(), expected->end(), Comp());
    EXPECT_EQ(*expected, real);
  }

  MTRandom rand_;
};

TYPED_TEST_SUITE_P(ExtendedHeapQueueTest);

TYPED_TEST_P(ExtendedHeapQueueTest, Assign) {
  for (int i = 0; i < TestFixture::kRandomVectorIterations; ++i) {
    typename TestFixture::HeapType heap;
    std::vector<typename TestFixture::ValueType> s =
        this->RandomVector((i + 1) * 13);
    heap.assign(s.begin(), s.end());
    this->IsHeapWithSameElements(heap, &s);
  }
}

TYPED_TEST_P(ExtendedHeapQueueTest, AssignFromList) {
  for (int i = 0; i < TestFixture::kRandomVectorIterations; ++i) {
    typename TestFixture::HeapType heap;
    std::vector<typename TestFixture::ValueType> s =
        this->RandomVector((i + 1) * 13);
    std::list<typename TestFixture::ValueType> l(s.begin(), s.end());
    heap.assign(l.begin(), l.end());
    this->IsHeapWithSameElements(heap, &s);
  }
}

TYPED_TEST_P(ExtendedHeapQueueTest, CopyUntil) {
  for (int i = 0; i < TestFixture::kRandomVectorIterations; ++i) {
    typename TestFixture::HeapType heap;
    typename TestFixture::Comp comp;
    typename TestFixture::Vector s = this->RandomVector((i + 1) * 13);
    // make sure we have at least one duplicate element in there.
    s.push_back(s[0]);
    typename TestFixture::Vector s2(s);
    heap.replace_elements(&s);

    std::sort(s2.rbegin(), s2.rend(), comp);
    for (const auto& v2 : s2) {
      typename TestFixture::Vector extracted;
      heap.copy_until(heap.begin(), heap.end(), v2,
                      std::back_inserter(extracted));

      for (const auto& e : extracted) {
        EXPECT_FALSE(comp(e, v2));
      }
      int false_count = 0;
      for (const auto& v : s2) {
        if (!comp(v, v2)) ++false_count;
      }
      EXPECT_EQ(extracted.size(), false_count);
    }
  }
}

TYPED_TEST_P(ExtendedHeapQueueTest, BulkRemovalRandom) {
  for (int i = 0; i < TestFixture::kRandomVectorIterations; ++i) {
    typename TestFixture::HeapType heap;
    typename TestFixture::Vector s = this->RandomVector((i + 1) * 13);
    heap.assign(s.begin(), s.end());
    {
      HeapQueueBulkRemover<typename TestFixture::HeapType> rem(&heap);
      for (int j = 0; j < (i + 1) * 3; ++j) {
        typename TestFixture::ValueType x = s.back();
        s.pop_back();
        typename TestFixture::HeapType::iterator rem_it =
            std::find(heap.begin(), heap.end(), x);
        EXPECT_NE(heap.end(), rem_it);
        rem.remove(rem_it);
      }
    }
    this->IsHeapWithSameElements(heap, &s);
  }
}

TYPED_TEST_P(ExtendedHeapQueueTest, BulkRemovalPredicate) {
  for (int i = 0; i < TestFixture::kRandomVectorIterations; ++i) {
    typename TestFixture::HeapType heap;
    typename TestFixture::Vector s = this->RandomVector((i + 1) * 13);
    gtl::STLSortAndRemoveDuplicates(&s);
    heap.assign(s.begin(), s.end());
    {
      typename TestFixture::Vector removed_from_vector;
      HeapQueueBulkRemover<typename TestFixture::HeapType> rem(&heap);
      for (int j = 0; j < (i + 1) * 3; ++j) {
        typename TestFixture::ValueType x = s.back();
        s.pop_back();
        removed_from_vector.push_back(x);
      }
      rem.remove_if([&removed_from_vector](const auto& x) -> bool {
        return absl::c_linear_search(removed_from_vector, x);
      });
    }
    this->IsHeapWithSameElements(heap, &s);
  }
}

REGISTER_TYPED_TEST_SUITE_P(ExtendedHeapQueueTest, Assign, AssignFromList,
                            CopyUntil, BulkRemovalRandom, BulkRemovalPredicate);

using TestTypes = ::testing::Types<
    std::pair<int, std::less<> >, std::pair<double, std::less<> >,
    std::pair<int, std::greater<> >, std::pair<double, std::greater<> > >;

INSTANTIATE_TYPED_TEST_SUITE_P(Enforcer, ExtendedHeapQueueTest, TestTypes);

// Generate random data.
constexpr int kCopyUntilBatchSize = 1024;
struct CopyUntilTestData {
  HeapQueue<uint32_t> queue;
  uint32_t cutoff;
};

auto MakeQueuesAndCutoffs(int count, double cutoff_percentage) {
  absl::BitGen gen;
  std::vector<CopyUntilTestData> queues_and_cutoffs(kCopyUntilBatchSize);
  for (auto& [queue, cutoff] : queues_and_cutoffs) {
    for (int i = 0; i < count; ++i) {
      queue.push(absl::Uniform<uint32_t>(gen));
    }

    // Computes the cutoff value.
    std::vector<uint32_t> data(queue.begin(), queue.end());
    const int cutoff_idx =
        std::max(0, static_cast<int>(cutoff_percentage * count) - 1);
    absl::c_nth_element(data, data.begin() + cutoff_idx);
    cutoff = data[cutoff_idx];
  }
  return queues_and_cutoffs;
}

class NullIterator {
 public:
  NullIterator& operator++() { return *this; }
  NullIterator operator*() { return *this; }
  NullIterator& operator=(uint32_t v) {
    benchmark::DoNotOptimize(v);
    return *this;
  }
};

void BM_CopyUntil(benchmark::State& state) {
  const int count = state.range(0);
  const double cutoff_percentage = static_cast<double>(state.range(1)) / 100;
  auto queues_and_cutoffs = MakeQueuesAndCutoffs(count, cutoff_percentage);
  while (state.KeepRunningBatch(kCopyUntilBatchSize)) {
    for (const auto& [queue, cutoff] : queues_and_cutoffs) {
      queue.copy_until(queue.begin(), queue.end(), cutoff, NullIterator{});
    }
  }
}

BENCHMARK(BM_CopyUntil)
    // Benchmarks that copy all the elements in heapqueue.
    ->ArgPair(1 << 3, 0)
    ->ArgPair(1 << 6, 0)
    ->ArgPair(1 << 9, 0)
    ->ArgPair(1 << 12, 0)
    ->ArgPair(1 << 15, 0)
    ->ArgPair(1 << 18, 0)
    // Benchmarks that copy 75% the elements in heapqueue.
    ->ArgPair(1 << 3, 25)
    ->ArgPair(1 << 6, 25)
    ->ArgPair(1 << 9, 25)
    ->ArgPair(1 << 12, 25)
    ->ArgPair(1 << 15, 25)
    ->ArgPair(1 << 18, 25)
    // Benchmarks that copy 50% the elements in heapqueue.
    ->ArgPair(1 << 3, 50)
    ->ArgPair(1 << 6, 50)
    ->ArgPair(1 << 9, 50)
    ->ArgPair(1 << 12, 50)
    ->ArgPair(1 << 15, 50)
    ->ArgPair(1 << 18, 50)
    // Benchmarks that copy 25% the elements in heapqueue.
    ->ArgPair(1 << 3, 75)
    ->ArgPair(1 << 6, 75)
    ->ArgPair(1 << 9, 75)
    ->ArgPair(1 << 12, 75)
    ->ArgPair(1 << 15, 75)
    ->ArgPair(1 << 18, 75)
    // Benchmarks that copy 0% the elements in heapqueue.
    ->ArgPair(1 << 3, 100)
    ->ArgPair(1 << 6, 100)
    ->ArgPair(1 << 9, 100)
    ->ArgPair(1 << 12, 100)
    ->ArgPair(1 << 15, 100)
    ->ArgPair(1 << 18, 100);

constexpr int kEraseBatchSize = 1024;
struct EraseTestData {
  HeapQueue<uint32_t> queue;
  HeapQueue<uint32_t>::iterator erase_position;
};
auto MakeQueuesAndErasePositions(int count, int erase_idx_upper_bound) {
  absl::BitGen gen;
  std::vector<EraseTestData> queues_and_erase_positions(kEraseBatchSize);
  for (auto& [queue, erase_position] : queues_and_erase_positions) {
    for (int i = 0; i < count; ++i) {
      queue.push(absl::Uniform<uint32_t>(gen));
    }
    erase_position =
        queue.begin() + absl::Uniform<int>(gen, 0, erase_idx_upper_bound);
  }
  return queues_and_erase_positions;
}

void BM_Erase(benchmark::State& state) {
  while (state.KeepRunningBatch(kEraseBatchSize)) {
    state.PauseTiming();
    const int count = state.range(0);
    const int erase_idx_upper_bound = state.range(1);
    auto queues_and_erase_positions =
        MakeQueuesAndErasePositions(count, erase_idx_upper_bound);
    state.ResumeTiming();
    for (auto& [queue, erase_position] : queues_and_erase_positions) {
      queue.erase(erase_position);
    }
  }
}

BENCHMARK(BM_Erase)
    // Benchmarks that erase the beginning of heapqueue, worst case in terms of
    // performance.
    ->ArgPair(1 << 3, 1)
    ->ArgPair(1 << 6, 1)
    ->ArgPair(1 << 9, 1)
    ->ArgPair(1 << 12, 1)
    ->ArgPair(1 << 15, 1)
    ->ArgPair(1 << 18, 1)
    // Benchmarks that erase random position in heapqueue, average case in terms
    // of performance.
    ->ArgPair(1 << 3, 1 << 3)
    ->ArgPair(1 << 6, 1 << 6)
    ->ArgPair(1 << 9, 1 << 9)
    ->ArgPair(1 << 12, 1 << 12)
    ->ArgPair(1 << 15, 1 << 15)
    ->ArgPair(1 << 18, 1 << 18);

}  // namespace
}  // namespace gtl
