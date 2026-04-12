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

#include "gloop/util/atomic_danger/atomic_danger.h"

#include <atomic>
#include <cstdint>

#include "gloop/gloop_test.h"

namespace {

TEST(AtomicDangerTest, CompareAndSwapWhenPtrEqualsExpected) {
  std::atomic<int32_t> atomic_value{1};
  EXPECT_EQ(atomic_danger::CompareAndSwap(&atomic_value, 1, 2,
                                          std::memory_order_seq_cst),
            1);
  EXPECT_EQ(atomic_value.load(std::memory_order_seq_cst), 2);
}

TEST(AtomicDangerTest, CompareAndSwapWhenPtrNotEqualsExpected) {
  std::atomic<int32_t> atomic_value{1};
  EXPECT_EQ(atomic_danger::CompareAndSwap(&atomic_value, 100, 100,
                                          std::memory_order_seq_cst),
            1);
  EXPECT_EQ(atomic_value.load(std::memory_order_seq_cst), 1);
}

TEST(AtomicDangerTest, CompareAndSwapLongWithIntLiterals) {
  std::atomic<intptr_t> atomic_value{1};
  EXPECT_EQ(atomic_danger::CompareAndSwap(&atomic_value, 100, 100,
                                          std::memory_order_seq_cst),
            1);
  EXPECT_EQ(atomic_value.load(std::memory_order_seq_cst), 1);
}

TEST(AtomicDangerTest, CompareAndSwapLongWithDifferentCategory) {
  std::atomic<intptr_t> atomic_value{1};
  int expected = 100;
  EXPECT_EQ(atomic_danger::CompareAndSwap(&atomic_value, expected, 100,
                                          std::memory_order_seq_cst),
            1);
  EXPECT_EQ(atomic_value.load(std::memory_order_seq_cst), 1);
}

TEST(AtomicDangerTest, IsZeroAfterDecrementReturnsFalseWhenNonzero) {
  std::atomic<int32_t> atomic_value{10};
  EXPECT_FALSE(atomic_danger::IsZeroAfterDecrement(&atomic_value, 1));
  EXPECT_EQ(atomic_value.load(std::memory_order_seq_cst), 9);
}

TEST(AtomicDangerTest, IsZeroAfterDecrementReturnsTrueWhenZero) {
  std::atomic<int32_t> atomic_value{10};
  EXPECT_TRUE(atomic_danger::IsZeroAfterDecrement(&atomic_value, 10));
  EXPECT_EQ(atomic_value.load(std::memory_order_seq_cst), 0);
}

TEST(AtomicDangerTest, IsZeroAfterDecrementLongWithIntLiterals) {
  std::atomic<intptr_t> atomic_value{10};
  EXPECT_FALSE(atomic_danger::IsZeroAfterDecrement(&atomic_value, 1));
  EXPECT_EQ(atomic_value.load(std::memory_order_seq_cst), 9);
}

template <typename IntType>
class CastToIntegralTest : public testing::Test {};
using IntTypes =
    testing::Types<int, int32_t, int32_t, int64_t, int64_t, intptr_t,  //
                   uint, uint32_t, uint32_t, uint64_t, uint64_t, uintptr_t>;
TYPED_TEST_SUITE(CastToIntegralTest, IntTypes);

TYPED_TEST(CastToIntegralTest, CastsCorrectly) {
  std::atomic<TypeParam> atomic_value{10};
  TypeParam* casted_address = atomic_danger::CastToIntegral(&atomic_value);
  EXPECT_EQ(*casted_address, atomic_value.load(std::memory_order_seq_cst));
}

}  // namespace
