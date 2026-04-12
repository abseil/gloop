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

#include "gloop/util/alloc/size-based-allocator.h"

#include <cstdint>
#include <vector>

#include "absl/log/log.h"
#include "gloop/gloop_test.h"
#include "gloop/util/alloc/block-allocator.h"
#include "gloop/util/alloc/tested-allocator.h"

namespace util {
namespace alloc {

// TestAllocator adds a fatal implementation for pure virtual functions of
// SizeBasedAllocator, so that rest of the class can be tested.
class TestAllocator : public SizeBasedAllocator {
 public:
  explicit TestAllocator(uint32_t num_blocks)
      : SizeBasedAllocator(num_blocks) {}

  // This type is neither copyable nor movable.
  TestAllocator(const TestAllocator&) = delete;
  TestAllocator& operator=(const TestAllocator&) = delete;
  virtual ~TestAllocator() {}

 private:
  virtual void PickAllocationRange(uint32_t requested_size, Block* start,
                                   uint32_t* actual_size,
                                   Range** chosen_range) const {
    LOG(QFATAL) << "Not implemented";
  }
};

TEST(SizeBasedAllocator, AllocateAt) {
  TestedAllocator allocator(new TestAllocator(5));
  uint32_t actual_size;

  // Allocate a block right in the middle
  allocator.AllocateAt(3, 1, &actual_size);
  EXPECT_EQ(3, actual_size) << allocator.DebugString();

  // Try to do the same allocation again.
  allocator.AllocateAt(3, 1, &actual_size);
  EXPECT_EQ(0, actual_size) << allocator.DebugString();

  // Try to do part of the same allocation.
  allocator.AllocateAt(1, 2, &actual_size);
  EXPECT_EQ(0, actual_size) << allocator.DebugString();
}

TEST(SizeBasedAllocator, Release) {
  TestedAllocator allocator(new TestAllocator(5));

  uint32_t actual_size;
  // First, allocate everything
  for (int block = 0; block < 5; ++block) {
    allocator.AllocateAt(1, block, &actual_size);
    EXPECT_EQ(1, actual_size) << allocator.DebugString();
  }

  // Now we can't allocate
  allocator.AllocateAt(1, 0, &actual_size);
  EXPECT_EQ(0, actual_size) << allocator.DebugString();

  // Release a block and re-allocate it
  allocator.Release(0, 1);
  allocator.AllocateAt(1, 0, &actual_size);
  EXPECT_EQ(1, actual_size) << allocator.DebugString();
}

TEST(SizeBasedAllocator, ReleaseMergeRight) {
  TestedAllocator allocator(new TestAllocator(5));

  uint32_t actual_size;
  allocator.AllocateAt(5, 0, &actual_size);
  EXPECT_EQ(5, actual_size);
  allocator.Release(3, 1);
  allocator.Release(2, 1);
  allocator.AllocateAt(2, 2, &actual_size);
  EXPECT_EQ(2, actual_size) << allocator.DebugString();
}

TEST(SizeBasedAllocator, ReleaseMergeLeft) {
  TestedAllocator allocator(new TestAllocator(5));

  uint32_t actual_size;
  allocator.AllocateAt(5, 0, &actual_size);
  allocator.Release(2, 1);
  allocator.Release(3, 1);
  allocator.AllocateAt(2, 2, &actual_size);
  EXPECT_EQ(2, actual_size) << allocator.DebugString();
}

TEST(SizeBasedAllocator, ReleaseMergeBoth) {
  TestedAllocator allocator(new TestAllocator(5));
  uint32_t actual_size;

  allocator.AllocateAt(5, 0, &actual_size);
  allocator.Release(2, 1);
  allocator.Release(4, 1);
  allocator.Release(3, 1);
  allocator.AllocateAt(3, 2, &actual_size);
  EXPECT_EQ(3, actual_size) << allocator.DebugString();
}

TEST(SizeBasedAllocator, MemoryUsage) {
  TestedAllocator allocator(new TestAllocator(5));

  // The allocator starts out with only one range.
  const int one_range_size = allocator.MemoryUsage();
  uint32_t actual_size;

  // This allocation should result in two ranges.
  allocator.AllocateAt(1, 1, &actual_size);
  EXPECT_EQ(1, actual_size);
  EXPECT_EQ(2 * one_range_size, allocator.MemoryUsage());

  // This allocation should result in three ranges.
  allocator.AllocateAt(1, 3, &actual_size);
  EXPECT_EQ(1, actual_size);
  EXPECT_EQ(3 * one_range_size, allocator.MemoryUsage());

  // Release all allocations and ensure there is only one range.
  allocator.Release(1, 1);
  allocator.Release(3, 1);
  EXPECT_EQ(one_range_size, allocator.MemoryUsage());
}

TEST(SizeBasedAllocator, GetFreeBlocks) {
  TestedAllocator allocator(new TestAllocator(5));
  std::vector<BlockAllocator::BlockRange> ranges;

  // The allocator starts out with only one range.
  allocator.GetFreeBlocks(&ranges);
  EXPECT_EQ(1, ranges.size());
  EXPECT_EQ(0, ranges[0].start_block);
  EXPECT_EQ(5, ranges[0].num_blocks);

  // This allocation should result in two ranges.
  uint32_t actual_size;
  allocator.AllocateAt(1, 1, &actual_size);
  EXPECT_EQ(1, actual_size);
  allocator.GetFreeBlocks(&ranges);
  EXPECT_EQ(2, ranges.size());
  EXPECT_EQ(0, ranges[0].start_block);
  EXPECT_EQ(1, ranges[0].num_blocks);
  EXPECT_EQ(2, ranges[1].start_block);
  EXPECT_EQ(3, ranges[1].num_blocks);
}

TEST(SizeBasedAllocator, LargestFreeBlockRange) {
  auto* inner_allocator = new TestAllocator(5);
  TestedAllocator allocator(inner_allocator);
  uint32_t actual_size;

  // free, free, free, free, free
  EXPECT_EQ(5, inner_allocator->LargestFreeBlockRange());

  // free, USED, free, free, free
  allocator.AllocateAt(1, 1, &actual_size);
  EXPECT_EQ(3, inner_allocator->LargestFreeBlockRange());

  // free, USED, USED, USED, USED
  allocator.AllocateAt(3, 2, &actual_size);
  EXPECT_EQ(1, inner_allocator->LargestFreeBlockRange());

  // USED, USED, USED, USED, USED
  allocator.AllocateAt(1, 0, &actual_size);
  EXPECT_EQ(0, inner_allocator->LargestFreeBlockRange());

  // free, USED, USED, USED, USED
  allocator.Release(0, 1);
  EXPECT_EQ(1, inner_allocator->LargestFreeBlockRange());

  // free, free, USED, USED, USED
  allocator.Release(1, 1);
  EXPECT_EQ(2, inner_allocator->LargestFreeBlockRange());

  // free, free, free, free, free
  allocator.Release(2, 3);
  EXPECT_EQ(5, inner_allocator->LargestFreeBlockRange());
}

}  // namespace alloc
}  // namespace util
