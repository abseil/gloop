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

}  // namespace
}  // namespace freelist
