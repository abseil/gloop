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

#include "gloop/util/freelist/freelist.h"

#include <memory>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "benchmark/benchmark.h"
#include "gloop/util/freelist/freelist_param_testlib.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace freelist {
namespace {

using ::testing::NotNull;

// Test FreeList and ThreadSafeFreeList

constexpr int kSize = 15;

INSTANTIATE_TEST_SUITE_P(
    , FreeListConformanceTest,
    testing::ValuesIn<FreeListTestParams>(
        {{"FreeList",
          []() {
            return std::make_unique<
                FreeList<FreeListConformanceTest::EnsureDeleted>>(kSize);
          },
          kSize},
         {"ThreadSafeFreeList",
          []() {
            return std::make_unique<
                ThreadSafeFreeList<FreeListConformanceTest::EnsureDeleted>>(
                kSize);
          },
          kSize},
         {.test_name = "UnboundedFreeList",
          .create_freelist =
              []() {
                return std::make_unique<UnboundedFreeList<
                    FreeListConformanceTest::EnsureDeleted>>();
              },
          .size = kSize,
          .can_be_lazy = true},
         // A freelist with a max_length of 0 is effectively disabled, but
         // should still function properly.
         {.test_name = "ZeroLengthFreeList",
          .create_freelist =
              []() {
                return std::make_unique<
                    FreeList<FreeListConformanceTest::EnsureDeleted>>(0);
              },
          .size = 0}}),
    GetTestName);

using FreelistDisabledTest = FreeListBaseTest;

// Verifies that when FLAGS_use_freelists is false, FreeList sets max_length to
// 0 and directly allocates and deletes objects without retaining them in the
// freelist across single and batched operations.
TEST_F(FreelistDisabledTest, FreeList) {
  absl::SetFlag(&FLAGS_use_freelists, false);

  auto fl =
      std::make_unique<FreeList<FreeListConformanceTest::EnsureDeleted>>(kSize);

  // Free list fl is initially empty and has max_length 0 when disabled.
  EXPECT_TRUE(fl->CheckRep());
  EXPECT_EQ(fl->size(), 0);
  EXPECT_EQ(fl->max_length(), 0);
  EnsureDeleted* const ed = fl->New();
  ASSERT_THAT(ed, NotNull());
  EXPECT_TRUE(fl->CanDelete(ed));
  EXPECT_EQ(fl->size(), 0);
  EXPECT_TRUE(fl->CheckRep());
  fl->Delete(ed);
  EXPECT_EQ(fl->size(), 0);  // freelist should be empty
  EXPECT_EQ(EnsureDeleted::live(), 0);

  fl->NewMany(nullptr, 0);
  fl->DeleteMany(nullptr, 0);
  EXPECT_EQ(fl->size(), 0);
  EXPECT_EQ(EnsureDeleted::live(), 0);

  std::vector<EnsureDeleted*> items(4, nullptr);
  fl->NewMany(items.data(), items.size());
  EXPECT_EQ(fl->size(), 0);
  EXPECT_EQ(EnsureDeleted::live(), 4);
  fl->DeleteMany(items.data(), items.size());
  EXPECT_EQ(fl->size(), 0);
  EXPECT_EQ(EnsureDeleted::live(), 0);
  fl->Clear();
  EXPECT_EQ(fl->size(), 0);
}

// Verifies that when FLAGS_use_freelists is false, ThreadSafeFreeList sets
// max_length to 0 and directly allocates and deletes objects without retaining
// them in the freelist across single and batched operations.
TEST_F(FreelistDisabledTest, ThreadSafeFreeList) {
  absl::SetFlag(&FLAGS_use_freelists, false);

  auto fl = std::make_unique<
      ThreadSafeFreeList<FreeListConformanceTest::EnsureDeleted>>(kSize);

  // Free list fl is initially empty and has max_length 0 when disabled.
  EXPECT_TRUE(fl->CheckRep());
  EXPECT_EQ(fl->size(), 0);
  EXPECT_EQ(fl->max_length(), 0);
  EnsureDeleted* const ed = fl->New();
  ASSERT_THAT(ed, NotNull());
  EXPECT_TRUE(fl->CanDelete(ed));
  EXPECT_EQ(fl->size(), 0);
  EXPECT_TRUE(fl->CheckRep());
  fl->Delete(ed);
  EXPECT_EQ(fl->size(), 0);  // freelist should be empty
  EXPECT_EQ(EnsureDeleted::live(), 0);

  fl->NewMany(nullptr, 0);
  fl->DeleteMany(nullptr, 0);
  EXPECT_EQ(fl->size(), 0);
  EXPECT_EQ(EnsureDeleted::live(), 0);

  std::vector<EnsureDeleted*> items(4, nullptr);
  fl->NewMany(items.data(), items.size());
  EXPECT_EQ(fl->size(), 0);
  EXPECT_EQ(EnsureDeleted::live(), 4);
  fl->DeleteMany(items.data(), items.size());
  EXPECT_EQ(fl->size(), 0);
  EXPECT_EQ(EnsureDeleted::live(), 0);
  fl->Clear();
  EXPECT_EQ(fl->size(), 0);
}

// Verifies that when FLAGS_use_freelists is false, UnboundedFreeList directly
// allocates and deletes objects without retaining them in the freelist across
// single and batched operations.
TEST_F(FreelistDisabledTest, UnboundedFreeList) {
  absl::SetFlag(&FLAGS_use_freelists, false);

  auto fl = std::make_unique<
      UnboundedFreeList<FreeListConformanceTest::EnsureDeleted>>();

  EXPECT_TRUE(fl->CheckRep());
  EXPECT_EQ(fl->size(), 0);
  EnsureDeleted* const ed = fl->New();
  ASSERT_THAT(ed, NotNull());
  EXPECT_TRUE(fl->CanDelete(ed));
  EXPECT_EQ(fl->size(), 0);
  EXPECT_TRUE(fl->CheckRep());
  fl->Delete(ed);
  EXPECT_EQ(fl->size(), 0);
  EXPECT_EQ(EnsureDeleted::live(), 0);

  fl->NewMany(nullptr, 0);
  fl->DeleteMany(nullptr, 0);
  EXPECT_EQ(fl->size(), 0);
  EXPECT_EQ(EnsureDeleted::live(), 0);

  std::vector<EnsureDeleted*> items(4, nullptr);
  fl->NewMany(items.data(), items.size());
  EXPECT_EQ(fl->size(), 0);
  EXPECT_EQ(EnsureDeleted::live(), 4);
  fl->DeleteMany(items.data(), items.size());
  EXPECT_EQ(fl->size(), 0);
  EXPECT_EQ(EnsureDeleted::live(), 0);
  fl->Clear();
  EXPECT_EQ(fl->size(), 0);
}

// Test FastAllocator

class FastAllocatorTest;

class Object {
 public:
  enum State { CONSTRUCTED, INITIALIZED, TOUCHED, FINALIZED };
  Object() : state_(CONSTRUCTED) {}
  ~Object() { destroyed_++; }

  void Touch() { state_ = TOUCHED; }
  State state() const { return state_; }

  static void ResetCounts() {
    destroyed_ = 0;
    finalized_ = 0;
  }
  static int destroyed_count() { return destroyed_; }
  static int finalized_count() { return finalized_; }

 private:
  friend class FastAllocatorTest;

  // Initialize() and Finalize() are private methods so that FastAllocator
  // can be given pointers to Initialize() and Finalize(), ensuring that the
  // initializer and finalizer can be invoked by FastAllocator even when private
  // to Object. FastAllocatorTest is a friend to form these member pointers.
  void Initialize() {
    EXPECT_NE(state_, INITIALIZED);
    state_ = INITIALIZED;
  }
  void Finalize() {
    EXPECT_NE(state_, FINALIZED);
    state_ = FINALIZED;
    finalized_++;
  }

  State state_;
  static inline int destroyed_ = 0;
  static inline int finalized_ = 0;
};

class FastAllocatorTest : public FreeListBaseTest {
 protected:
  void SetUp() override { Object::ResetCounts(); }

  static constexpr auto kInitialize = &Object::Initialize;
  static constexpr auto kFinalize = &Object::Finalize;
};

TEST_F(FastAllocatorTest, DeleteNullIsNoOp) {
  auto alloc = std::make_unique<FastAllocator<Object>>(new FreeList<Object>(1),
                                                       nullptr, nullptr);
  EXPECT_EQ(alloc->size(), 0);
  alloc->Delete(nullptr);
  EXPECT_EQ(alloc->size(), 0);
  Object* const x = alloc->New();
  ASSERT_THAT(x, NotNull());  // Not the null pointer just deleted.
  alloc->Delete(x);
}

TEST_F(FastAllocatorTest, NoInitializerOrFinalizer) {
  auto alloc = std::make_unique<FastAllocator<Object>>(new FreeList<Object>(1),
                                                       nullptr, nullptr);
  Object* const x = alloc->New();
  ASSERT_THAT(x, NotNull());
  EXPECT_EQ(x->state(), Object::CONSTRUCTED);  // No initialization.
  x->Touch();
  EXPECT_EQ(x->state(), Object::TOUCHED);

  Object::ResetCounts();
  alloc->Delete(x);
  EXPECT_EQ(Object::destroyed_count(),
            0);  // x went on free list, not destroyed.
  EXPECT_EQ(Object::finalized_count(), 0);  // Nor finalized.
  EXPECT_EQ(x->state(),
            Object::TOUCHED);  // No finalization; x stays as it was.

  Object* const y = alloc->New();  // Reuse x.
  EXPECT_EQ(y, x);
  EXPECT_EQ(x->state(), Object::TOUCHED);  // No initialization.

  alloc->Delete(alloc->New());  // Fill the free list slot.
  Object::ResetCounts();
  x->Touch();
  alloc->Delete(x);
  EXPECT_EQ(Object::destroyed_count(), 1);  // Destructor run.
  EXPECT_EQ(Object::finalized_count(), 0);  // But no finalizer.
}

TEST_F(FastAllocatorTest, InitializerOnly) {
  auto alloc = std::make_unique<FastAllocator<Object>>(new FreeList<Object>(1),
                                                       kInitialize, nullptr);
  Object* const x = alloc->New();
  ASSERT_THAT(x, NotNull());
  EXPECT_EQ(x->state(), Object::INITIALIZED);  // Initialization.
  x->Touch();
  EXPECT_EQ(x->state(), Object::TOUCHED);

  Object::ResetCounts();
  alloc->Delete(x);
  EXPECT_EQ(Object::destroyed_count(),
            0);  // x went on free list, not destroyed.
  EXPECT_EQ(Object::finalized_count(), 0);  // Nor finalized.
  EXPECT_EQ(x->state(),
            Object::TOUCHED);  // No finalization; x stays as it was.

  Object* const y = alloc->New();  // Reuse x.
  EXPECT_EQ(y, x);
  EXPECT_EQ(x->state(), Object::INITIALIZED);  // Re-initialized.

  alloc->Delete(alloc->New());  // Fill the free list slot.
  Object::ResetCounts();
  x->Touch();
  alloc->Delete(x);
  EXPECT_EQ(Object::destroyed_count(), 1);  // Destructor run.
  EXPECT_EQ(Object::finalized_count(), 0);  // But no finalizer.
}

TEST_F(FastAllocatorTest, FinalizerOnly) {
  auto alloc = std::make_unique<FastAllocator<Object>>(new FreeList<Object>(1),
                                                       nullptr, kFinalize);
  Object* const x = alloc->New();
  ASSERT_THAT(x, NotNull());
  EXPECT_EQ(x->state(), Object::CONSTRUCTED);  // No initialization.
  x->Touch();
  EXPECT_EQ(x->state(), Object::TOUCHED);

  Object::ResetCounts();
  alloc->Delete(x);
  EXPECT_EQ(Object::destroyed_count(),
            0);  // x went on free list, not destroyed.
  EXPECT_EQ(Object::finalized_count(), 1);  // But finalized.
  EXPECT_EQ(x->state(), Object::FINALIZED);

  Object* const y = alloc->New();  // Reuse x.
  EXPECT_EQ(y, x);
  EXPECT_EQ(x->state(), Object::FINALIZED);  // No re-initialization.

  alloc->Delete(alloc->New());  // Fill the free list slot.
  Object::ResetCounts();
  x->Touch();
  alloc->Delete(x);
  EXPECT_EQ(Object::destroyed_count(), 1);  // Destructor run.
  EXPECT_EQ(Object::finalized_count(), 1);  // And so was finalizer.
}

TEST_F(FastAllocatorTest, InitializerAndFinalizer) {
  auto alloc = std::make_unique<FastAllocator<Object>>(new FreeList<Object>(1),
                                                       kInitialize, kFinalize);
  Object* const x = alloc->New();
  ASSERT_THAT(x, NotNull());
  EXPECT_EQ(x->state(), Object::INITIALIZED);  // Initialized.
  x->Touch();
  EXPECT_EQ(x->state(), Object::TOUCHED);

  Object::ResetCounts();
  alloc->Delete(x);
  EXPECT_EQ(Object::destroyed_count(),
            0);  // x went on free list, not destroyed.
  EXPECT_EQ(Object::finalized_count(), 1);  // But finalized.
  EXPECT_EQ(x->state(), Object::FINALIZED);

  Object* const y = alloc->New();  // Reuse x.
  EXPECT_EQ(y, x);
  EXPECT_EQ(x->state(), Object::INITIALIZED);  // Re-initialized.

  alloc->Delete(alloc->New());  // Fill the free list slot.
  Object::ResetCounts();
  x->Touch();
  alloc->Delete(x);
  EXPECT_EQ(Object::destroyed_count(), 1);  // Destructor run.
  EXPECT_EQ(Object::finalized_count(), 1);  // And so was finalizer.
}

using AbstractFreeListTest = FreeListBaseTest;
TEST_F(AbstractFreeListTest, NewUniquePtr) {
  FreeList<std::string> freelist(5);
  EXPECT_EQ(freelist.max_length(), 5);
  {
    auto ptr = freelist.NewUniquePtr();
    EXPECT_EQ(*ptr, "");
    ptr->push_back('H');
    EXPECT_EQ(*ptr, "H");
    EXPECT_EQ(freelist.size(), 0);  // No spare instance
  }
  EXPECT_EQ(freelist.size(), 1);  // Instance returned to list
}

TEST_F(AbstractFreeListTest, NewUniquePtrNullable) {
  FreeList<std::string> freelist(5);
  EXPECT_EQ(freelist.max_length(), 5);
  {
    auto ptr = freelist.NewUniquePtr();
    EXPECT_EQ(*ptr, "");
    ptr->push_back('H');
    EXPECT_EQ(*ptr, "H");
    EXPECT_EQ(freelist.size(), 0);  // No spare instance
    ptr = nullptr;
    EXPECT_EQ(freelist.size(), 1);  // Instance Returned.
  }
  EXPECT_EQ(freelist.size(), 1);  // Instance returned to list
}

TEST_F(AbstractFreeListTest, NewUniquePtrMovableDeleter) {
  FreeList<std::string> freelist(5);
  FreeList<std::string> freelist2(5);
  EXPECT_EQ(freelist.max_length(), 5);
  {
    auto ptr = freelist.NewUniquePtr();
    EXPECT_EQ(*ptr, "");
    ptr->push_back('H');
    EXPECT_EQ(*ptr, "H");
    EXPECT_EQ(freelist.size(), 0);
    EXPECT_EQ(freelist2.size(), 0);
    ptr = freelist2.NewUniquePtr();
    EXPECT_EQ(freelist.size(), 1);  // First Instance Returned.
  }
  EXPECT_EQ(freelist2.size(), 1);  // Second Instance Returned.
  EXPECT_EQ(freelist.size(), 1);   // Nothing else returned to first list.
}

using ScopedFreeListPointerTest = FreeListBaseTest;
TEST_F(ScopedFreeListPointerTest, BasicCase) {
  FreeList<std::string> free_list(5);
  EXPECT_EQ(free_list.max_length(), 5);
  {
    ScopedFreeListPointer<std::string> ptr(&free_list);
    EXPECT_EQ(*ptr, "");
    ptr->push_back('H');
    EXPECT_EQ(*ptr, "H");
    EXPECT_EQ(free_list.size(), 0);  // No spare instance
  }
  EXPECT_EQ(free_list.size(), 1);  // Instance returned to list
}

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
  const int batch_size = state.range(0);
  FreeList<int> freelist(batch_size);
  std::vector<int*> items(batch_size);
  int** const ptrs = items.data();
  freelist.NewMany(ptrs, batch_size);
  freelist.DeleteMany(ptrs, batch_size);
  FreeList<int>* fl = &freelist;
  while (state.KeepRunningBatch(batch_size)) {
    benchmark::DoNotOptimize(fl);
    fl->FreeList<int>::NewMany(ptrs, batch_size);
    benchmark::DoNotOptimize(ptrs);
    fl->FreeList<int>::DeleteMany(ptrs, batch_size);
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
  const int batch_size = state.range(0);
  ThreadSafeFreeList<int> freelist(batch_size);
  std::vector<int*> items(batch_size);
  int** const ptrs = items.data();
  freelist.NewMany(ptrs, batch_size);
  freelist.DeleteMany(ptrs, batch_size);
  ThreadSafeFreeList<int>* fl = &freelist;
  while (state.KeepRunningBatch(batch_size)) {
    benchmark::DoNotOptimize(fl);
    fl->ThreadSafeFreeList<int>::NewMany(ptrs, batch_size);
    benchmark::DoNotOptimize(ptrs);
    fl->ThreadSafeFreeList<int>::DeleteMany(ptrs, batch_size);
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
  const int batch_size = state.range(0);
  UnboundedFreeList<int> freelist;
  std::vector<int*> items(batch_size);
  int** const ptrs = items.data();
  freelist.NewMany(ptrs, batch_size);
  freelist.DeleteMany(ptrs, batch_size);
  UnboundedFreeList<int>* fl = &freelist;
  while (state.KeepRunningBatch(batch_size)) {
    benchmark::DoNotOptimize(fl);
    fl->UnboundedFreeList<int>::NewMany(ptrs, batch_size);
    benchmark::DoNotOptimize(ptrs);
    fl->UnboundedFreeList<int>::DeleteMany(ptrs, batch_size);
  }
}
BENCHMARK(BM_UnboundedFreeList_NewManyDeleteMany)->Arg(16)->Arg(64)->Arg(256);

}  // namespace
}  // namespace freelist
