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

#include <vector>

#include "absl/flags/flag.h"
#include "benchmark/benchmark.h"
#include "gloop/util/freelist/freelist.h"

namespace freelist {
namespace {

void BM_FreeList_NewDelete(benchmark::State& state) {
  // Interleave 4 independent freelists in the loop body (reported per single
  // New+Delete pair via KeepRunningBatch(4)). A single inlined depth-1
  // New+Delete loop is latency-pinned on the loop-carried store-to-load
  // forwarding recurrence on a single FreeList::size_ field (~8.5 cycles with
  // ~70% of CPU issue slots idle), whereas interleaving independent pools lets
  // the out-of-order engine overlap those latencies and measure instruction
  // and branch retirement throughput.
  FreeList<int> fl0(/*max_length=*/64);
  FreeList<int> fl1(/*max_length=*/64);
  FreeList<int> fl2(/*max_length=*/64);
  FreeList<int> fl3(/*max_length=*/64);
  fl0.Delete(fl0.New());
  fl1.Delete(fl1.New());
  fl2.Delete(fl2.New());
  fl3.Delete(fl3.New());
  FreeList<int>* p_fl0 = &fl0;
  FreeList<int>* p_fl1 = &fl1;
  FreeList<int>* p_fl2 = &fl2;
  FreeList<int>* p_fl3 = &fl3;
  int total = 0;
  while (state.KeepRunningBatch(4)) {
    benchmark::DoNotOptimize(p_fl0);
    benchmark::DoNotOptimize(p_fl1);
    benchmark::DoNotOptimize(p_fl2);
    benchmark::DoNotOptimize(p_fl3);
    int* a = p_fl0->FreeList<int>::New();
    int* b = p_fl1->FreeList<int>::New();
    int* c = p_fl2->FreeList<int>::New();
    int* d = p_fl3->FreeList<int>::New();
    // Touch the allocated objects to model realistic usage.
    total += ++*a + ++*b + ++*c + ++*d;
    p_fl0->FreeList<int>::Delete(a);
    p_fl1->FreeList<int>::Delete(b);
    p_fl2->FreeList<int>::Delete(c);
    p_fl3->FreeList<int>::Delete(d);
  }
  benchmark::DoNotOptimize(total);
}
BENCHMARK(BM_FreeList_NewDelete);

void BM_FreeList_VirtualNewDelete(benchmark::State& state) {
  // Interleave 4 independent freelists in the loop body (reported per single
  // New+Delete pair via KeepRunningBatch(4)) to overlap per-list size_
  // store-to-load forwarding latency across independent pools. Complete each
  // New+Delete pair before the next New() virtual call so the returned pointer
  // does not have to be spilled across caller-clobbering virtual calls.
  FreeList<int> fl0(/*max_length=*/64);
  FreeList<int> fl1(/*max_length=*/64);
  FreeList<int> fl2(/*max_length=*/64);
  FreeList<int> fl3(/*max_length=*/64);
  fl0.Delete(fl0.New());
  fl1.Delete(fl1.New());
  fl2.Delete(fl2.New());
  fl3.Delete(fl3.New());
  AbstractFreeList<int>* p_fl0 = &fl0;
  AbstractFreeList<int>* p_fl1 = &fl1;
  AbstractFreeList<int>* p_fl2 = &fl2;
  AbstractFreeList<int>* p_fl3 = &fl3;
  int total = 0;
  while (state.KeepRunningBatch(4)) {
    benchmark::DoNotOptimize(p_fl0);
    benchmark::DoNotOptimize(p_fl1);
    benchmark::DoNotOptimize(p_fl2);
    benchmark::DoNotOptimize(p_fl3);
    int* a = p_fl0->New();
    total += ++*a;
    p_fl0->Delete(a);
    int* b = p_fl1->New();
    total += ++*b;
    p_fl1->Delete(b);
    int* c = p_fl2->New();
    total += ++*c;
    p_fl2->Delete(c);
    int* d = p_fl3->New();
    total += ++*d;
    p_fl3->Delete(d);
  }
  benchmark::DoNotOptimize(total);
}
BENCHMARK(BM_FreeList_VirtualNewDelete);

void BM_FreeList_DisabledNewDelete(benchmark::State& state) {
  absl::SetFlag(&FLAGS_use_freelists, false);
  FreeList<int> freelist(/*max_length=*/64);
  FreeList<int>* fl = &freelist;
  int total = 0;
  for (auto s : state) {
    benchmark::DoNotOptimize(fl);
    int* p = fl->FreeList<int>::New();
    // Touch the allocated object to model realistic usage.
    total += ++*p;
    fl->FreeList<int>::Delete(p);
  }
  benchmark::DoNotOptimize(total);
  absl::SetFlag(&FLAGS_use_freelists, true);
}
BENCHMARK(BM_FreeList_DisabledNewDelete);

void BM_FreeList_BatchedNewDelete(benchmark::State& state) {
  const int batch_size = state.range(0);
  FreeList<int> freelist(batch_size);
  std::vector<int*> items(batch_size);
  int** const ptrs = items.data();
  for (int i = 0; i < batch_size; ++i) {
    ptrs[i] = freelist.New();
  }
  for (int i = 0; i < batch_size; ++i) {
    freelist.Delete(ptrs[i]);
  }
  FreeList<int>* fl = &freelist;
  int total = 0;
  while (state.KeepRunningBatch(batch_size)) {
    benchmark::DoNotOptimize(fl);
    for (int i = 0; i < batch_size; ++i) {
      int* p = fl->FreeList<int>::New();
      // Touch each allocated object to model realistic usage; without a data
      // access between operations, back-to-back stores and loads on
      // FreeList::size_ run into microarchitectural store-to-load forwarding
      // stalls.
      ++*p;
      ptrs[i] = p;
    }
    benchmark::DoNotOptimize(ptrs);
    for (int i = 0; i < batch_size; ++i) {
      int* p = ptrs[i];
      total += ++*p;
      fl->FreeList<int>::Delete(p);
    }
  }
  benchmark::DoNotOptimize(total);
}
BENCHMARK(BM_FreeList_BatchedNewDelete)->Arg(16)->Arg(64)->Arg(256);

void BM_FreeList_NewManyDeleteMany(benchmark::State& state) {
  // Interleave 2 independent freelists and pointer buffers in the loop body
  // (reported per item via KeepRunningBatch(2 * batch_size)) so that the
  // 8-byte scalar stores to list_ in DeleteMany and 32-byte AVX stores to ptrs
  // in NewMany's memcpy drain from the Store Buffer before the next operation
  // on the same buffer reads them with a different access width.
  const int batch_size = state.range(0);
  FreeList<int> fl0(batch_size);
  FreeList<int> fl1(batch_size);
  std::vector<int*> items0(batch_size);
  std::vector<int*> items1(batch_size);
  int** const ptrs0 = items0.data();
  int** const ptrs1 = items1.data();
  fl0.NewMany(ptrs0, batch_size);
  fl0.DeleteMany(ptrs0, batch_size);
  fl1.NewMany(ptrs1, batch_size);
  fl1.DeleteMany(ptrs1, batch_size);
  FreeList<int>* p_fl0 = &fl0;
  FreeList<int>* p_fl1 = &fl1;
  while (state.KeepRunningBatch(2 * batch_size)) {
    benchmark::DoNotOptimize(p_fl0);
    benchmark::DoNotOptimize(p_fl1);
    p_fl0->FreeList<int>::NewMany(ptrs0, batch_size);
    p_fl1->FreeList<int>::NewMany(ptrs1, batch_size);
    benchmark::DoNotOptimize(ptrs0);
    benchmark::DoNotOptimize(ptrs1);
    p_fl0->FreeList<int>::DeleteMany(ptrs0, batch_size);
    p_fl1->FreeList<int>::DeleteMany(ptrs1, batch_size);
  }
}
BENCHMARK(BM_FreeList_NewManyDeleteMany)->Arg(16)->Arg(64)->Arg(256);

void BM_ThreadSafeFreeList_NewDelete(benchmark::State& state) {
  ThreadSafeFreeList<int> freelist(/*max_length=*/64);
  freelist.Delete(freelist.New());
  ThreadSafeFreeList<int>* fl = &freelist;
  int total = 0;
  for (auto s : state) {
    benchmark::DoNotOptimize(fl);
    int* p = fl->ThreadSafeFreeList<int>::New();
    // Touch the allocated object to model realistic usage.
    total += ++*p;
    fl->ThreadSafeFreeList<int>::Delete(p);
  }
  benchmark::DoNotOptimize(total);
}
BENCHMARK(BM_ThreadSafeFreeList_NewDelete);

void BM_ThreadSafeFreeList_DisabledNewDelete(benchmark::State& state) {
  absl::SetFlag(&FLAGS_use_freelists, false);
  ThreadSafeFreeList<int> freelist(/*max_length=*/64);
  ThreadSafeFreeList<int>* fl = &freelist;
  int total = 0;
  for (auto s : state) {
    benchmark::DoNotOptimize(fl);
    int* p = fl->ThreadSafeFreeList<int>::New();
    // Touch the allocated object to model realistic usage.
    total += ++*p;
    fl->ThreadSafeFreeList<int>::Delete(p);
  }
  benchmark::DoNotOptimize(total);
  absl::SetFlag(&FLAGS_use_freelists, true);
}
BENCHMARK(BM_ThreadSafeFreeList_DisabledNewDelete);

void BM_ThreadSafeFreeList_NewManyDeleteMany(benchmark::State& state) {
  // Interleave 2 independent freelists and pointer buffers in the loop body
  // (reported per item via KeepRunningBatch(2 * batch_size)) to avoid
  // back-to-back Store Buffer narrow-to-wide forwarding stalls across batches.
  const int batch_size = state.range(0);
  ThreadSafeFreeList<int> fl0(batch_size);
  ThreadSafeFreeList<int> fl1(batch_size);
  std::vector<int*> items0(batch_size);
  std::vector<int*> items1(batch_size);
  int** const ptrs0 = items0.data();
  int** const ptrs1 = items1.data();
  fl0.NewMany(ptrs0, batch_size);
  fl0.DeleteMany(ptrs0, batch_size);
  fl1.NewMany(ptrs1, batch_size);
  fl1.DeleteMany(ptrs1, batch_size);
  ThreadSafeFreeList<int>* p_fl0 = &fl0;
  ThreadSafeFreeList<int>* p_fl1 = &fl1;
  while (state.KeepRunningBatch(2 * batch_size)) {
    benchmark::DoNotOptimize(p_fl0);
    benchmark::DoNotOptimize(p_fl1);
    p_fl0->ThreadSafeFreeList<int>::NewMany(ptrs0, batch_size);
    p_fl1->ThreadSafeFreeList<int>::NewMany(ptrs1, batch_size);
    benchmark::DoNotOptimize(ptrs0);
    benchmark::DoNotOptimize(ptrs1);
    p_fl0->ThreadSafeFreeList<int>::DeleteMany(ptrs0, batch_size);
    p_fl1->ThreadSafeFreeList<int>::DeleteMany(ptrs1, batch_size);
  }
}
BENCHMARK(BM_ThreadSafeFreeList_NewManyDeleteMany)->Arg(16)->Arg(64)->Arg(256);

void BM_UnboundedFreeList_NewDelete(benchmark::State& state) {
  // Interleave 4 independent freelists in the loop body (reported per single
  // New+Delete pair via KeepRunningBatch(4)) to overlap per-list size
  // store-to-load forwarding latency across independent pools.
  UnboundedFreeList<int> fl0;
  UnboundedFreeList<int> fl1;
  UnboundedFreeList<int> fl2;
  UnboundedFreeList<int> fl3;
  fl0.Delete(fl0.New());
  fl1.Delete(fl1.New());
  fl2.Delete(fl2.New());
  fl3.Delete(fl3.New());
  UnboundedFreeList<int>* p_fl0 = &fl0;
  UnboundedFreeList<int>* p_fl1 = &fl1;
  UnboundedFreeList<int>* p_fl2 = &fl2;
  UnboundedFreeList<int>* p_fl3 = &fl3;
  int total = 0;
  while (state.KeepRunningBatch(4)) {
    benchmark::DoNotOptimize(p_fl0);
    benchmark::DoNotOptimize(p_fl1);
    benchmark::DoNotOptimize(p_fl2);
    benchmark::DoNotOptimize(p_fl3);
    int* a = p_fl0->UnboundedFreeList<int>::New();
    int* b = p_fl1->UnboundedFreeList<int>::New();
    int* c = p_fl2->UnboundedFreeList<int>::New();
    int* d = p_fl3->UnboundedFreeList<int>::New();
    // Touch the allocated objects to model realistic usage.
    total += ++*a + ++*b + ++*c + ++*d;
    p_fl0->UnboundedFreeList<int>::Delete(a);
    p_fl1->UnboundedFreeList<int>::Delete(b);
    p_fl2->UnboundedFreeList<int>::Delete(c);
    p_fl3->UnboundedFreeList<int>::Delete(d);
  }
  benchmark::DoNotOptimize(total);
}
BENCHMARK(BM_UnboundedFreeList_NewDelete);

void BM_UnboundedFreeList_NewManyDeleteMany(benchmark::State& state) {
  // Interleave 2 independent freelists and pointer buffers in the loop body
  // (reported per item via KeepRunningBatch(2 * batch_size)) to avoid
  // back-to-back Store Buffer narrow-to-wide forwarding stalls across batches.
  const int batch_size = state.range(0);
  UnboundedFreeList<int> fl0;
  UnboundedFreeList<int> fl1;
  std::vector<int*> items0(batch_size);
  std::vector<int*> items1(batch_size);
  int** const ptrs0 = items0.data();
  int** const ptrs1 = items1.data();
  fl0.NewMany(ptrs0, batch_size);
  fl0.DeleteMany(ptrs0, batch_size);
  fl1.NewMany(ptrs1, batch_size);
  fl1.DeleteMany(ptrs1, batch_size);
  UnboundedFreeList<int>* p_fl0 = &fl0;
  UnboundedFreeList<int>* p_fl1 = &fl1;
  while (state.KeepRunningBatch(2 * batch_size)) {
    benchmark::DoNotOptimize(p_fl0);
    benchmark::DoNotOptimize(p_fl1);
    p_fl0->UnboundedFreeList<int>::NewMany(ptrs0, batch_size);
    p_fl1->UnboundedFreeList<int>::NewMany(ptrs1, batch_size);
    benchmark::DoNotOptimize(ptrs0);
    benchmark::DoNotOptimize(ptrs1);
    p_fl0->UnboundedFreeList<int>::DeleteMany(ptrs0, batch_size);
    p_fl1->UnboundedFreeList<int>::DeleteMany(ptrs1, batch_size);
  }
}
BENCHMARK(BM_UnboundedFreeList_NewManyDeleteMany)->Arg(16)->Arg(64)->Arg(256);

}  // namespace
}  // namespace freelist
