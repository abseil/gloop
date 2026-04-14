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

#ifndef THIRD_PARTY_GLOOP_UTIL_ALLOC_SIZE_BASED_ALLOCATOR_H_
#define THIRD_PARTY_GLOOP_UTIL_ALLOC_SIZE_BASED_ALLOCATOR_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/btree_set.h"
#include "absl/synchronization/mutex.h"
#include "gloop/util/alloc/block-allocator.h"

namespace util {
namespace alloc {

// This class implements a BlockAllacator except for the Allocate() method.
// Sub-classes of this class can decide on an implementation for Allocate().
//
// This class is thread-safe.
class SizeBasedAllocator : public BlockAllocator {
 public:
  explicit SizeBasedAllocator(uint32_t num_blocks);

  // This type is neither copyable nor movable.
  SizeBasedAllocator(const SizeBasedAllocator&) = delete;
  SizeBasedAllocator& operator=(const SizeBasedAllocator&) = delete;

  virtual ~SizeBasedAllocator();

  // Allocates a contiguous region of blocks. Fill in start with the
  // first block of the allocation, and actual_size with the number of blocks
  // allocated.
  // The region of blocks that is allocated is based on PickAllocationRange()
  // implemented by the sub-classes.
  virtual void Allocate(uint32_t requested_size, Block* start,
                        uint32_t* actual_size);

  // Allocate up to requested_size blocks starting at 'start'. Fill in
  // actual_size with the number of blocks which could actually be allocated at
  // that position. (Which may be zero).
  // This method is fully implemented in SizeBasedAllocator compared to
  // Allocate(), as the notion of continuing an allocation should be same
  // across all size-based allocators.
  virtual void AllocateAt(uint32_t requested_size, Block start,
                          uint32_t* actual_size);

  // Release all of the blocks in this range. This function assumes that the
  // region it gets passed is a contiguous region of stuff which has already
  // been allocated using some other function.
  virtual void Release(Block begin, uint32_t size);

  // Release *all* of the blocks.
  virtual void Clear();

  // Return the number of free blocks
  virtual uint32_t BlocksFree() const;

  // Populates 'ranges' with free block ranges.
  // 'ranges' is cleared before being populated.
  virtual void GetFreeBlocks(std::vector<BlockRange>* ranges) const;

  // Returns approximate memory usage.
  virtual size_t MemoryUsage() const;

  virtual std::string DebugString() const;

  // Returns size (in blocks) of the largest range of contiguous free blocks.
  uint32_t LargestFreeBlockRange() const;

 protected:
  struct Range {
    Block start;
    uint32_t size;

    inline Block end() const { return start + size; }
  };

  class ByPos {
   public:
    bool operator()(const Range* r1, const Range* r2) const {
      return r1->start < r2->start;
    }
  };
  using RangeByPos = absl::btree_set<Range*, ByPos>;

  // Sort ranges by size, ascending.
  class BySize {
   public:
    bool operator()(const Range* r1, const Range* r2) const {
      if (r1->size < r2->size) {
        return true;
      } else if (r1->size > r2->size) {
        return false;
      } else {
        // Break ties between ranges that are actually distinct so that they can
        // both be inserted into the same set.
        return r1->start < r2->start;
      }
    }
  };
  using RangeBySize = absl::btree_set<Range*, BySize>;

  // Given a request for 'requested_size' blocks, it picks a range from
  // ranges_by_{pos, size}_ and returns it in 'chosen_range' along with and
  // 'start' point and number of blocks (in 'actual_size') chosen in the range.
  // *chosen_range should point to an element in ranges_by_{pos, size}_.
  // If there is no range that can be picked, *actual_size should be set to
  // zero.
  // If unable to allocate, *start is set to kInvalidBlock.
  virtual void PickAllocationRange(uint32_t requested_size, Block* start,
                                   uint32_t* actual_size,
                                   Range** chosen_range) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_) = 0;

  mutable absl::Mutex lock_;
  // These hold the same set of pointers; the ranges describe the ranges of
  // blocks which are currently free.
  RangeByPos ranges_by_pos_ ABSL_GUARDED_BY(lock_);
  RangeBySize ranges_by_size_ ABSL_GUARDED_BY(lock_);

 private:
  // We approximate the memory usage of a new Range as the sizeof the Range plus
  // two pointers to the Range.  One pointer is in the ranges_by_pos_ container
  // and another pointer is in the ranges_by_size_ container.
  static constexpr int32_t kRangeMemoryUsage =
      sizeof(Range) + 2 * sizeof(Range*);

  // Actually mark the given blocks as "in use". The given blocks must entirely
  // fit into a single range. old_range should be a pointer into the sets,
  // which will be consumed by this call.
  void Mark(Block start, uint32_t size, Range* old_range)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  // Merge two adjacent ranges, return a new iterator to the combined range.
  RangeByPos::iterator MergeRanges(RangeByPos::iterator* left,
                                   RangeByPos::iterator* right)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  // Insert the given range into ranges_* and return the position iterator
  RangeByPos::iterator Insert(Range* r) ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  std::string DebugStringLocked() const ABSL_SHARED_LOCKS_REQUIRED(lock_);

  const uint32_t num_blocks_;
  uint32_t blocks_free_ ABSL_GUARDED_BY(lock_);
};

}  // namespace alloc
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_ALLOC_SIZE_BASED_ALLOCATOR_H_
