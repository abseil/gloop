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

#include "gloop/util/alloc/tested-allocator.h"

#include <cstdint>
#include <string>

#include "absl/strings/string_view.h"
#include "gloop/util/alloc/block-allocator.h"
#include "gtest/gtest.h"

namespace util {
namespace alloc {

TestedAllocator::TestedAllocator(BlockAllocator* allocator)
    : size_(allocator->BlocksFree()),
      expected_free_(size_),
      allocator_(allocator),
      alloc_bitmap_(size_, false) {}

void TestedAllocator::Allocate(uint32_t requested_size, Block* start,
                               uint32_t* actual_size) {
  allocator_->Allocate(requested_size, start, actual_size);
  ValidateAllocation("Allocate", requested_size, *start, *actual_size);
}

void TestedAllocator::AllocateAt(uint32_t requested_size, Block start,
                                 uint32_t* actual_size) {
  allocator_->AllocateAt(requested_size, start, actual_size);
  ValidateAllocation("AllocateAt", requested_size, start, *actual_size);
}

void TestedAllocator::Release(Block begin, uint32_t size) {
  allocator_->Release(begin, size);
  for (Block b = begin; b < begin + size; ++b) {
    EXPECT_TRUE(alloc_bitmap_[b])
        << ": Release error: Released block " << b << " that isn't marked "
        << "allocated!";
    alloc_bitmap_[b] = false;
  }
  expected_free_ += size;
  EXPECT_EQ(expected_free_, allocator_->BlocksFree());
}

void TestedAllocator::Clear() {
  allocator_->Clear();
  for (Block b = 0; b < size_; ++b) {
    alloc_bitmap_[b] = false;
  }
  expected_free_ = size_;
  EXPECT_EQ(expected_free_, allocator_->BlocksFree());
}

std::string TestedAllocator::DebugString() const {
  std::string result = allocator_->DebugString();
  result += "\nBitmap: ";
  for (int i = 0; i < alloc_bitmap_.size(); ++i) {
    result += (alloc_bitmap_[i] ? "1" : "0");
  }
  return result;
}

void TestedAllocator::ValidateAllocation(absl::string_view method_name,
                                         uint32_t requested_size, Block start,
                                         uint32_t actual_size) {
  for (Block b = start; b < start + actual_size; ++b) {
    EXPECT_FALSE(alloc_bitmap_[b])
        << ": Allocation error: " << method_name << "(" << requested_size
        << ") returned a block of size " << actual_size << " starting at "
        << start << ", but block " << b << " is already in use!";
    alloc_bitmap_[b] = true;
  }
  expected_free_ -= actual_size;
  EXPECT_EQ(expected_free_, allocator_->BlocksFree());
}

}  // namespace alloc
}  // namespace util
