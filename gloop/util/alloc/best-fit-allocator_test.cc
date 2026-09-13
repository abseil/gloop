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

#include "gloop/util/alloc/best-fit-allocator.h"

#include <cstdint>

#include "gloop/util/alloc/block-allocator.h"
#include "gloop/util/alloc/tested-allocator.h"
#include "gtest/gtest.h"

namespace util {
namespace alloc {

typedef BestFitAllocator::Block Block;

TEST(BestFitAllocatorTest, Allocate) {
  TestedAllocator allocator(new BestFitAllocator(5));

  Block start;
  uint32_t actual_size;

  // Allocate a block in between the block space.
  allocator.AllocateAt(1, 1, &actual_size);
  EXPECT_EQ(1, actual_size);

  // Asking for 4 blocks should result in allocation of the largest
  // contiguous block space available, which is 3 blocks.
  allocator.Allocate(4, &start, &actual_size);
  EXPECT_EQ(3, actual_size);
  EXPECT_EQ(2, start);

  // Allocate blocks that can be satisfied completely.
  allocator.Allocate(1, &start, &actual_size);
  EXPECT_EQ(1, actual_size);
  EXPECT_EQ(0, start);

  // Allocation should result in no blocks as entire block space has
  // been allocated.
  EXPECT_EQ(0, allocator.BlocksFree());
  allocator.Allocate(1, &start, &actual_size);
  EXPECT_EQ(BlockAllocator::kInvalidBlock, start);
  EXPECT_EQ(0, actual_size);
}

TEST(BestFitAllocatorTest, FindsBestFit) {
  // Pattern: (3 free) + allocated + (1 free) + allocated + (2 free)
  TestedAllocator allocator(new BestFitAllocator(8));

  Block start;
  uint32_t actual_size;

  allocator.AllocateAt(1, 3, &actual_size);
  EXPECT_EQ(1, actual_size);
  allocator.AllocateAt(1, 5, &actual_size);
  EXPECT_EQ(1, actual_size);

  // Asking for one block should allocate in the middle, because it fits
  // perfectly.
  allocator.Allocate(1, &start, &actual_size);
  EXPECT_EQ(4, start);
  EXPECT_EQ(1, actual_size);

  // Asking for a second block should allocate at the end. This is not a perfect
  // fit, but is the best fit.
  allocator.Allocate(1, &start, &actual_size);
  EXPECT_EQ(6, start);
  EXPECT_EQ(1, actual_size);

  // We should still fit an allocation of size 3 at the beginning.
  allocator.Allocate(3, &start, &actual_size);
  EXPECT_EQ(0, start);
  EXPECT_EQ(3, actual_size);
}

// Tests that the allocator finds the best fit at zero, which validates that
// using a comparison range starting at zero in the implementation works.
TEST(BestFitAllocatorTest, BestFitBeginning) {
  // Pattern: (1 free) + allocated + (3 free)
  TestedAllocator allocator(new BestFitAllocator(5));

  Block start;
  uint32_t actual_size;

  allocator.AllocateAt(1, 1, &actual_size);
  EXPECT_EQ(1, actual_size);

  // Asking for one block should allocate at the beginning, because it fits
  // perfectly.
  allocator.Allocate(1, &start, &actual_size);
  EXPECT_EQ(0, start);
  EXPECT_EQ(1, actual_size);

  // We can still fit an allocation of size 3 at the end.
  allocator.Allocate(3, &start, &actual_size);
  EXPECT_EQ(2, start);
  EXPECT_EQ(3, actual_size);
}

}  // namespace alloc
}  // namespace util
