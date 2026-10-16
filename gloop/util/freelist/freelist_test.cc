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

#include <stdio.h>

#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "gloop/util/freelist/freelist_param_testlib.h"
#include "gtest/gtest.h"

namespace freelist {
namespace {

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
TEST_F(FreelistDisabledTest, ThreadSafeFreeList) {
  absl::SetFlag(&FLAGS_use_freelists, false);

  auto fl = std::make_unique<
      ThreadSafeFreeList<FreeListConformanceTest::EnsureDeleted>>(kSize);

  // Free list fl is initially empty
  EXPECT_TRUE(fl->CheckRep());
  EXPECT_EQ(fl->size(), 0);
  EnsureDeleted* const ed = fl->New();
  ASSERT_NE(ed, nullptr);
  EXPECT_EQ(fl->size(), 0);
  EXPECT_TRUE(fl->CheckRep());
  fl->Delete(ed);
  EXPECT_EQ(fl->size(), 0);  // freelist should be empty
}

// Test FastAllocator

static int destroyed = 0;
static int finalized = 0;

class Object {
 public:
  enum State { CONSTRUCTED, INITIALIZED, TOUCHED, FINALIZED };
  Object() : state_(CONSTRUCTED) {}
  ~Object() { destroyed++; }

  void Touch() { state_ = TOUCHED; }
  State state() const { return state_; }

  // Declare TestFastAllocator() to be a static method of Object and
  // Initialize() and Finalize() to be private methods so that
  // TestFastAllocator can given pointers to Initialize() and Finalize() to
  // the FastAllocator object, and we can be sure that the initializer and
  // finalizer can be run by the FastAllocator object even if they are
  // private to Object.
  static void TestFastAllocator();

 private:
  void Initialize() {
    EXPECT_NE(state_, INITIALIZED);
    state_ = INITIALIZED;
  }
  void Finalize() {
    EXPECT_NE(state_, FINALIZED);
    state_ = FINALIZED;
    finalized++;
  }
  State state_;
};

void Object::TestFastAllocator() {
  FastAllocator<Object>* alloc = nullptr;
  Object* x = nullptr;
  Object* y = nullptr;

  // Delete(NULL) is nop
  alloc = new FastAllocator<Object>(new FreeList<Object>(1), nullptr, nullptr);
  EXPECT_EQ(alloc->size(), 0);
  alloc->Delete(nullptr);
  EXPECT_EQ(alloc->size(), 0);
  x = alloc->New();
  ASSERT_NE(x, nullptr);  // not the NULL we just "deleted"
  alloc->Delete(x);
  delete alloc;

  // Case 1: No initializer and finalizer
  alloc = new FastAllocator<Object>(new FreeList<Object>(1), nullptr, nullptr);
  x = alloc->New();
  ASSERT_NE(x, nullptr);
  EXPECT_EQ(x->state(), Object::CONSTRUCTED);  // no initialization
  x->Touch();
  EXPECT_EQ(x->state(), Object::TOUCHED);
  destroyed = finalized = 0;
  alloc->Delete(x);
  EXPECT_EQ(destroyed, 0);                 // x went on free list, not destroyed
  EXPECT_EQ(finalized, 0);                 // nor finalized
  EXPECT_EQ(x->state(), Object::TOUCHED);  // no finalization, x stays as it was

  y = alloc->New();  // reuse x
  EXPECT_EQ(x, y);
  EXPECT_EQ(x->state(), Object::TOUCHED);  // no initialization

  alloc->Delete(alloc->New());  // fill the free list slot
  destroyed = finalized = 0;    // x to be destroyed
  x->Touch();
  alloc->Delete(x);
  EXPECT_EQ(destroyed, 1);  // destructor run
  EXPECT_EQ(finalized, 0);  // but no finalizer
  delete alloc;

  // Case 2: Initializer but no finalizer
  alloc = new FastAllocator<Object>(new FreeList<Object>(1),
                                    &Object::Initialize, nullptr);
  x = alloc->New();
  ASSERT_NE(x, nullptr);
  EXPECT_EQ(x->state(), Object::INITIALIZED);  // initialization
  x->Touch();
  EXPECT_EQ(x->state(), Object::TOUCHED);
  destroyed = finalized = 0;
  alloc->Delete(x);
  EXPECT_EQ(destroyed, 0);                 // x went on free list, not destroyed
  EXPECT_EQ(finalized, 0);                 // nor finalized
  EXPECT_EQ(x->state(), Object::TOUCHED);  // no finalization, x stays as it was

  y = alloc->New();  // reuse x
  EXPECT_EQ(x, y);
  EXPECT_EQ(x->state(), Object::INITIALIZED);  // re-initialized

  alloc->Delete(alloc->New());  // fill the free list slot
  destroyed = finalized = 0;    // x to be destroyed
  x->Touch();
  alloc->Delete(x);
  EXPECT_EQ(destroyed, 1);  // destructor run
  EXPECT_EQ(finalized, 0);  // but no finalizer
  delete alloc;

  // Case 3: No initializer but has finalizer
  alloc = new FastAllocator<Object>(new FreeList<Object>(1), nullptr,
                                    &Object::Finalize);
  x = alloc->New();
  ASSERT_NE(x, nullptr);
  EXPECT_EQ(x->state(), Object::CONSTRUCTED);  // no initialization
  x->Touch();
  EXPECT_EQ(x->state(), Object::TOUCHED);
  destroyed = finalized = 0;
  alloc->Delete(x);
  EXPECT_EQ(destroyed, 0);  // x went on free list, not destroyed
  EXPECT_EQ(finalized, 1);  // but finalized
  EXPECT_EQ(x->state(), Object::FINALIZED);  // finalized

  y = alloc->New();  // reuse x
  EXPECT_EQ(x, y);
  EXPECT_EQ(x->state(), Object::FINALIZED);  // no re-initialization

  alloc->Delete(alloc->New());  // fill the free list slot
  destroyed = finalized = 0;    // x to be destroyed
  x->Touch();
  alloc->Delete(x);
  EXPECT_EQ(destroyed, 1);  // destructor run
  EXPECT_EQ(finalized, 1);  // and so was finalizer
  delete alloc;

  // Case 4: Has both initializer and finalizer
  alloc = new FastAllocator<Object>(new FreeList<Object>(1),
                                    &Object::Initialize, &Object::Finalize);
  x = alloc->New();
  ASSERT_NE(x, nullptr);
  EXPECT_EQ(x->state(), Object::INITIALIZED);  // initialized
  x->Touch();
  EXPECT_EQ(x->state(), Object::TOUCHED);
  destroyed = finalized = 0;
  alloc->Delete(x);
  EXPECT_EQ(destroyed, 0);  // x went on free list, not destroyed
  EXPECT_EQ(finalized, 1);  // but finalized
  EXPECT_EQ(x->state(), Object::FINALIZED);  // finalized

  y = alloc->New();  // reuse x
  EXPECT_EQ(x, y);
  EXPECT_EQ(x->state(), Object::INITIALIZED);  // re-initialized

  alloc->Delete(alloc->New());  // fill the free list slot
  destroyed = finalized = 0;    // x to be destroyed
  x->Touch();
  alloc->Delete(x);
  EXPECT_EQ(destroyed, 1);  // destructor run
  EXPECT_EQ(finalized, 1);  // and so was finalizer
  delete alloc;
}

using FastAllocatorTest = FreeListBaseTest;
TEST_F(FastAllocatorTest, FastAllocator) { Object::TestFastAllocator(); }

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
