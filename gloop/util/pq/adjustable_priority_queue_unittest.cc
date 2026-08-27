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

#include "gloop/util/pq/adjustable_priority_queue.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iosfwd>
#include <iostream>
#include <ostream>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/random/random.h"
#include "benchmark/benchmark.h"
#include "gloop/base/init_google.h"
#include "gloop/util/gtl/stl_util.h"
#include "gloop/util/random/acmrandom.h"
#include "gtest/gtest.h"

struct TestElement {
  bool operator<(const TestElement& other) const {
    return priority < other.priority;
  }
  void SetHeapIndex(int64_t h) { heap_index = h; }
  int64_t GetHeapIndex() const { return heap_index; }
  double priority;
  int64_t heap_index;
  int64_t label;
  TestElement() {
    heap_index = -777777;
    priority = 0.0;
  }
};

void CheckPQTop(std::vector<TestElement>* elements,
                AdjustablePriorityQueue<TestElement>* pq,
                absl::flat_hash_set<TestElement*>* inpq) {
  double max_priority = -1;
  std::vector<int> max_priority_indices;
  for (int64_t i = 0; i < elements->size(); ++i) {
    if (inpq->find(&((*elements)[i])) != inpq->end() &&
        (*elements)[i].priority >= max_priority) {
      if ((*elements)[i].priority > max_priority) {
        max_priority_indices.clear();
        max_priority = (*elements)[i].priority;
      }
      max_priority_indices.push_back((*elements)[i].heap_index);
    }
  }
  std::sort(max_priority_indices.begin(), max_priority_indices.end());
  std::vector<TestElement*> top_elements;
  pq->AllTop(&top_elements);
  CHECK_EQ(top_elements.size(), max_priority_indices.size());
  std::vector<int> pqtop_indices(top_elements.size());
  for (int64_t i = 0; i < top_elements.size(); ++i) {
    pqtop_indices[i] = top_elements[i]->heap_index;
  }
  CHECK(max_priority_indices == pqtop_indices);
}

void Test1(ACMRandom* r) {
  AdjustablePriorityQueue<TestElement> pq;
  constexpr int64_t num_test_elements = 200;
  std::vector<TestElement> test_elements(num_test_elements);
  CHECK(pq.IsEmpty());
  CHECK(!pq.Contains(&test_elements[0]));
  absl::flat_hash_set<TestElement*> inpq;
  for (int64_t i = 0; i < num_test_elements; ++i) {
    test_elements[i].priority =
        (double)(int)(absl::Uniform<float>(absl::IntervalOpen, *r, 0, 1) * 10);
    test_elements[i].label = i;  // for easier debugging
    CHECK(!pq.Contains(&test_elements[i]));
    pq.Add(&test_elements[i]);
    CHECK(pq.Contains(&test_elements[i]));
    inpq.insert(&test_elements[i]);
    CheckPQTop(&test_elements, &pq, &inpq);
  }
  for (int64_t i = 0; i < num_test_elements; ++i) {
    // Test that check still succeeds even for element that wasn't just
    // inserted.
    CHECK(pq.Contains(&test_elements[i]));
  }
  TestElement not_in_pq;
  not_in_pq.heap_index = 5;  // Make it plausible.
  CHECK(!pq.Contains(&not_in_pq));

  int num_test_operations = 10000;
  CHECK_EQ(pq.Size(), num_test_elements);

  CheckPQTop(&test_elements, &pq, &inpq);

  for (int i = 0; i < num_test_operations; ++i) {
    int64_t elem_num = absl::Uniform<int32_t>(*r, 0, num_test_elements);
    TestElement* el = &(test_elements[elem_num]);
    if (inpq.find(el) == inpq.end()) {  // not in pq
      el->priority =
          (double)(int)(absl::Uniform<float>(absl::IntervalOpen, *r, 0, 1) *
                        10);
      pq.Add(el);
      inpq.insert(el);
    } else {
      if (absl::Uniform<int32_t>(*r, 0, 2)) {
        el->priority =
            (double)(int)(absl::Uniform<float>(absl::IntervalOpen, *r, 0, 1) *
                          10);
        pq.NoteChangedPriority(el);
      } else {
        pq.Remove(el);
        CHECK(!pq.Contains(el));
        inpq.erase(el);
      }
    }
    CheckPQTop(&test_elements, &pq, &inpq);
    pq.CheckValid();
  }

  AdjustablePriorityQueue<TestElement> pq2 = std::move(pq);
  CheckPQTop(&test_elements, &pq2, &inpq);
  AdjustablePriorityQueue<TestElement> pq3;
  pq3 = std::move(pq2);
  CheckPQTop(&test_elements, &pq3, &inpq);
}

void ClearTest(ACMRandom* r) {
  AdjustablePriorityQueue<TestElement> pq;

  const int64_t kNumElements = absl::Uniform<int32_t>(*r, 0, 500) + 100;
  std::vector<TestElement*> reference;

  // Create a queue with a many elements.
  for (int64_t i = 0; i < kNumElements; ++i) {
    CHECK_EQ(i, pq.Size());
    TestElement* const element = new TestElement;
    pq.Add(element);
    reference.push_back(element);
  }

  // Clear the queue and validate its size.
  pq.Clear();
  CHECK_EQ(0, pq.Size());

  gtl::STLDeleteElements(&reference);
}

void HeapifyTest(ACMRandom* r) {
  const int64_t kNumElements = absl::Uniform<int64_t>(*r, 0, 500) + 100;
  std::vector<TestElement*> refs(kNumElements);
  for (TestElement*& ref : refs) {
    ref = new TestElement;
    ref->priority = absl::Uniform<int32_t>(*r, 0, 10000);
  }
  AdjustablePriorityQueue<TestElement> pq(refs.begin(), refs.end());
  CHECK_EQ(kNumElements, pq.Size());
  pq.CheckValid();

  gtl::STLDeleteElements(&refs);
}

static void BM_Add(benchmark::State& state) {
  int64_t heap_size = state.range(0);
  AdjustablePriorityQueue<TestElement> pq;
  ACMRandom rnd(301);
  std::vector<TestElement> test_elements(heap_size);
  for (int64_t i = 0; i < heap_size; i++) {
    test_elements[i].priority = absl::Uniform<int32_t>(rnd, 0, 10000);
  }
  int64_t next = 0;
  for (auto _ : state) {
    int64_t N = pq.Size();
    if (N == heap_size) {
      state.PauseTiming();
      pq.Clear();
      next = 0;
      state.ResumeTiming();
    }
    int64_t elem_num = next;
    pq.Add(&(test_elements[elem_num]));
  }
}
BENCHMARK(BM_Add)->Range(64, 8192);

static void BM_Remove(benchmark::State& state) {
  int64_t heap_size = state.range(0);
  AdjustablePriorityQueue<TestElement> pq;
  ACMRandom rnd(301);
  std::vector<TestElement> test_elements(heap_size);
  for (int64_t i = 0; i < heap_size; i++) {
    test_elements[i].priority = absl::Uniform<int32_t>(rnd, 0, 10000);
  }
  for (auto _ : state) {
    int64_t N = pq.Size();
    if (N == 0) {
      state.PauseTiming();
      for (int64_t i = 0; i < heap_size; i++) {
        pq.Add(&(test_elements[i]));
      }
      N = heap_size;
      state.ResumeTiming();
    }
    int64_t elem_num = N - 1;
    pq.Remove(&(test_elements[elem_num]));
  }
}
BENCHMARK(BM_Remove)->Range(64, 8192);

TEST(AdjustablePriorityQueueTest, FunctorCompression) {
  // Do not rely in the exact size of `std::vector`.
  static constexpr int kBaseSize = sizeof(std::vector<TestElement*>);

  EXPECT_EQ(sizeof(AdjustablePriorityQueue<TestElement>), kBaseSize);

  static constexpr int kFunctorSize = 8;
  struct Cmp {
    bool operator()(const TestElement& a, const TestElement& b) { return true; }
    char dummy[kFunctorSize];
  };
  EXPECT_GT(sizeof(AdjustablePriorityQueue<TestElement, Cmp>),
            kBaseSize + kFunctorSize);
}

static void BM_Pop(benchmark::State& state) {
  int64_t heap_size = state.range(0);
  AdjustablePriorityQueue<TestElement> pq;
  ACMRandom rnd(301);
  std::vector<TestElement> test_elements(heap_size);
  for (int64_t i = 0; i < heap_size; i++) {
    test_elements[i].priority = absl::Uniform<int32_t>(rnd, 0, 10000);
  }
  for (auto _ : state) {
    if (pq.IsEmpty()) {
      state.PauseTiming();
      for (int64_t i = 0; i < heap_size; i++) {
        pq.Add(&(test_elements[i]));
      }
      state.ResumeTiming();
    }
    pq.Pop();
  }
}
BENCHMARK(BM_Pop)->Range(64, 8192);

static void BM_Heapify(benchmark::State& state) {
  const int64_t heap_size = state.range(0);
  ACMRandom rnd(301);
  std::vector<TestElement*> elems(heap_size);
  for (TestElement*& elem : elems) {
    elem = new TestElement;
    elem->priority = absl::Uniform<int32_t>(rnd, 0, 10000);
  }
  for (auto _ : state) {
    AdjustablePriorityQueue<TestElement> pq(elems.begin(), elems.end());
    benchmark::DoNotOptimize(pq);
  }

  gtl::STLDeleteElements(&elems);
}
BENCHMARK(BM_Heapify)->Range(64, 8192);

static void BM_AddAll(benchmark::State& state) {
  const int64_t heap_size = state.range(0);
  ACMRandom rnd(301);
  std::vector<TestElement*> elems(heap_size);
  for (TestElement*& elem : elems) {
    elem = new TestElement;
    elem->priority = absl::Uniform<int32_t>(rnd, 0, 10000);
  }
  for (auto _ : state) {
    AdjustablePriorityQueue<TestElement> pq;
    for (TestElement* elem : elems) {
      pq.Add(elem);
    }
    benchmark::DoNotOptimize(pq);
  }

  gtl::STLDeleteElements(&elems);
}
BENCHMARK(BM_AddAll)->Range(64, 8192);

int main(int argc, char* argv[]) {
  InitGoogle(argv[0], &argc, &argv, true);
  if (!benchmark::GetBenchmarkFilter().empty()) {
    benchmark::RunSpecifiedBenchmarks();
    exit(0);
  }

  ACMRandom r(314159);
  int num_tests = 5;
  for (int test_num = 0; test_num < num_tests; ++test_num) {
    Test1(&r);
    ClearTest(&r);
    HeapifyTest(&r);
  }
  std::cout << "PASS" << std::endl;
  return 0;
}
