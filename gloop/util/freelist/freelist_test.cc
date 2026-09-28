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
  CHECK(fl->CheckRep());
  CHECK_EQ(fl->size(), 0);
  EnsureDeleted* const ed = fl->New();
  CHECK(ed != nullptr);
  CHECK_EQ(fl->size(), 0);
  CHECK(fl->CheckRep());
  fl->Delete(ed);
  CHECK_EQ(fl->size(), 0);  // freelist should be empty
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
    CHECK_NE(state_, INITIALIZED);
    state_ = INITIALIZED;
  }
  void Finalize() {
    CHECK_NE(state_, FINALIZED);
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
  CHECK_EQ(alloc->size(), 0);
  alloc->Delete(nullptr);
  CHECK_EQ(alloc->size(), 0);
  x = alloc->New();
  CHECK(x != nullptr);  // not the NULL we just "deleted"
  alloc->Delete(x);
  delete alloc;

  // Case 1: No initializer and finalizer
  alloc = new FastAllocator<Object>(new FreeList<Object>(1), nullptr, nullptr);
  x = alloc->New();
  CHECK(x != nullptr);
  CHECK_EQ(x->state(), Object::CONSTRUCTED);  // no initialization
  x->Touch();
  CHECK_EQ(x->state(), Object::TOUCHED);
  destroyed = finalized = 0;
  alloc->Delete(x);
  CHECK_EQ(destroyed, 0);                 // x went on free list, not destroyed
  CHECK_EQ(finalized, 0);                 // nor finalized
  CHECK_EQ(x->state(), Object::TOUCHED);  // no finalization, x stays as it was

  y = alloc->New();  // reuse x
  CHECK(x == y);
  CHECK_EQ(x->state(), Object::TOUCHED);  // no initialization

  alloc->Delete(alloc->New());  // fill the free list slot
  destroyed = finalized = 0;    // x to be destroyed
  x->Touch();
  alloc->Delete(x);
  CHECK_EQ(destroyed, 1);  // destructor run
  CHECK_EQ(finalized, 0);  // but no finalizer
  delete alloc;

  // Case 2: Initializer but no finalizer
  alloc = new FastAllocator<Object>(new FreeList<Object>(1),
                                    &Object::Initialize, nullptr);
  x = alloc->New();
  CHECK(x != nullptr);
  CHECK_EQ(x->state(), Object::INITIALIZED);  // initialization
  x->Touch();
  CHECK_EQ(x->state(), Object::TOUCHED);
  destroyed = finalized = 0;
  alloc->Delete(x);
  CHECK_EQ(destroyed, 0);                 // x went on free list, not destroyed
  CHECK_EQ(finalized, 0);                 // nor finalized
  CHECK_EQ(x->state(), Object::TOUCHED);  // no finalization, x stays as it was

  y = alloc->New();  // reuse x
  CHECK(x == y);
  CHECK_EQ(x->state(), Object::INITIALIZED);  // re-initialized

  alloc->Delete(alloc->New());  // fill the free list slot
  destroyed = finalized = 0;    // x to be destroyed
  x->Touch();
  alloc->Delete(x);
  CHECK_EQ(destroyed, 1);  // destructor run
  CHECK_EQ(finalized, 0);  // but no finalizer
  delete alloc;

  // Case 3: No initializer but has finalizer
  alloc = new FastAllocator<Object>(new FreeList<Object>(1), nullptr,
                                    &Object::Finalize);
  x = alloc->New();
  CHECK(x != nullptr);
  CHECK_EQ(x->state(), Object::CONSTRUCTED);  // no initialization
  x->Touch();
  CHECK_EQ(x->state(), Object::TOUCHED);
  destroyed = finalized = 0;
  alloc->Delete(x);
  CHECK_EQ(destroyed, 0);  // x went on free list, not destroyed
  CHECK_EQ(finalized, 1);  // but finalized
  CHECK_EQ(x->state(), Object::FINALIZED);  // finalized

  y = alloc->New();  // reuse x
  CHECK(x == y);
  CHECK_EQ(x->state(), Object::FINALIZED);  // no re-initialization

  alloc->Delete(alloc->New());  // fill the free list slot
  destroyed = finalized = 0;    // x to be destroyed
  x->Touch();
  alloc->Delete(x);
  CHECK_EQ(destroyed, 1);  // destructor run
  CHECK_EQ(finalized, 1);  // and so was finalizer
  delete alloc;

  // Case 4: Has both initializer and finalizer
  alloc = new FastAllocator<Object>(new FreeList<Object>(1),
                                    &Object::Initialize, &Object::Finalize);
  x = alloc->New();
  CHECK(x != nullptr);
  CHECK_EQ(x->state(), Object::INITIALIZED);  // initialized
  x->Touch();
  CHECK_EQ(x->state(), Object::TOUCHED);
  destroyed = finalized = 0;
  alloc->Delete(x);
  CHECK_EQ(destroyed, 0);  // x went on free list, not destroyed
  CHECK_EQ(finalized, 1);  // but finalized
  CHECK_EQ(x->state(), Object::FINALIZED);  // finalized

  y = alloc->New();  // reuse x
  CHECK(x == y);
  CHECK_EQ(x->state(), Object::INITIALIZED);  // re-initialized

  alloc->Delete(alloc->New());  // fill the free list slot
  destroyed = finalized = 0;    // x to be destroyed
  x->Touch();
  alloc->Delete(x);
  CHECK_EQ(destroyed, 1);  // destructor run
  CHECK_EQ(finalized, 1);  // and so was finalizer
  delete alloc;
}

using FastAllocatorTest = FreeListBaseTest;
TEST_F(FastAllocatorTest, FastAllocator) { Object::TestFastAllocator(); }

using AbstractFreeListTest = FreeListBaseTest;
TEST_F(AbstractFreeListTest, NewUniquePtr) {
  FreeList<std::string> freelist(5);
  EXPECT_EQ(5, freelist.max_length());
  {
    auto ptr = freelist.NewUniquePtr();
    EXPECT_EQ("", *ptr);
    ptr->push_back('H');
    EXPECT_EQ("H", *ptr);
    EXPECT_EQ(0, freelist.size());  // No spare instance
  }
  EXPECT_EQ(1, freelist.size());  // Instance returned to list
}

TEST_F(AbstractFreeListTest, NewUniquePtrNullable) {
  FreeList<std::string> freelist(5);
  EXPECT_EQ(5, freelist.max_length());
  {
    auto ptr = freelist.NewUniquePtr();
    EXPECT_EQ("", *ptr);
    ptr->push_back('H');
    EXPECT_EQ("H", *ptr);
    EXPECT_EQ(0, freelist.size());  // No spare instance
    ptr = nullptr;
    EXPECT_EQ(1, freelist.size());  // Instance Returned.
  }
  EXPECT_EQ(1, freelist.size());  // Instance returned to list
}

TEST_F(AbstractFreeListTest, NewUniquePtrMovableDeleter) {
  FreeList<std::string> freelist(5);
  FreeList<std::string> freelist2(5);
  EXPECT_EQ(5, freelist.max_length());
  {
    auto ptr = freelist.NewUniquePtr();
    EXPECT_EQ("", *ptr);
    ptr->push_back('H');
    EXPECT_EQ("H", *ptr);
    EXPECT_EQ(0, freelist.size());
    EXPECT_EQ(0, freelist2.size());
    ptr = freelist2.NewUniquePtr();
    EXPECT_EQ(1, freelist.size());  // First Instance Returned.
  }
  EXPECT_EQ(1, freelist2.size());  // Second Instance Returned.
  EXPECT_EQ(1, freelist.size());   // Nothing else returned to first list.
}

using ScopedFreeListPointerTest = FreeListBaseTest;
TEST_F(ScopedFreeListPointerTest, BasicCase) {
  FreeList<std::string> free_list(5);
  EXPECT_EQ(5, free_list.max_length());
  {
    ScopedFreeListPointer<std::string> ptr(&free_list);
    EXPECT_EQ("", *ptr);
    ptr->push_back('H');
    EXPECT_EQ("H", *ptr);
    EXPECT_EQ(0, free_list.size());  // No spare instance
  }
  EXPECT_EQ(1, free_list.size());  // Instance returned to list
}

}  // namespace
}  // namespace freelist
