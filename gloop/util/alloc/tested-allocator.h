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

#ifndef THIRD_PARTY_GLOOP_UTIL_ALLOC_TESTED_ALLOCATOR_H_
#define THIRD_PARTY_GLOOP_UTIL_ALLOC_TESTED_ALLOCATOR_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "gloop/util/alloc/block-allocator.h"

namespace util {
namespace alloc {

// TestedAllocator can be used to test implementations of BlockAllocator,
// it wraps a BlockAllocator and keeps track of what it thinks ought to be
// marked as in use or free at any given time.
class TestedAllocator : public BlockAllocator {
 public:
  // All the blocks in 'allocator' should be free.
  explicit TestedAllocator(BlockAllocator* allocator);

  // This type is neither copyable nor movable.
  TestedAllocator(const TestedAllocator&) = delete;
  TestedAllocator& operator=(const TestedAllocator&) = delete;

  void Allocate(uint32_t requested_size, Block* start,
                uint32_t* actual_size) override;
  void AllocateAt(uint32_t requested_size, Block start,
                  uint32_t* actual_size) override;
  void Release(Block begin, uint32_t size) override;
  void Clear() override;
  uint32_t BlocksFree() const override { return allocator_->BlocksFree(); }
  size_t MemoryUsage() const override { return allocator_->MemoryUsage(); }
  void GetFreeBlocks(std::vector<BlockRange>* ranges) const override {
    return allocator_->GetFreeBlocks(ranges);
  }
  std::string DebugString() const override;

 private:
  void ValidateAllocation(absl::string_view method_name,
                          uint32_t requested_size, Block start,
                          uint32_t actual_size);

  const uint32_t size_;
  uint32_t expected_free_;
  std::unique_ptr<BlockAllocator> allocator_;
  std::vector<bool> alloc_bitmap_;
};

}  // namespace alloc
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_ALLOC_TESTED_ALLOCATOR_H_
