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

#ifndef THIRD_PARTY_GLOOP_UTIL_ALLOC_BLOCK_ALLOCATOR_H_
#define THIRD_PARTY_GLOOP_UTIL_ALLOC_BLOCK_ALLOCATOR_H_

#include <stddef.h>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "gloop/util/registration/registerer.h"

namespace util {
namespace alloc {

// BlockAllocator is an interface that can be used to define block allocators.
// Block allocators manage large contiguous address space and allow users
// to allocate and free arbitrarily sized regions in the address space.
//
// Example:
//   // Open a file and create an allocator to manage the file's capacity.
//   File* f = File::Open("/tmp/test", "w");
//   const int32 kMaximumFileSize = 1<<30;
//   const int32 kBlockSize = 4096;
//   const int32 kBlocksInFile = kMaximumFileSize / kBlockSize;
//   SomeBlockAllocator allocator = new SomeBlockAllocator(kBlocksInFile, ...);
//
//   // Allocate a block in the file and write to it.
//   cont int32 kBlocksToWrite = 101;
//   char data[kBlocksToWrite * kBlockSize];
//   Block file_offset;
//   uint32 actual_size;
//   allocator->Allocate(kBlocksToWrite, &file_offset, &actual_size);
//   f->PWrite(file_offset, data, sizeof(data));
//
//   // Free the block.
//   allocator->Release(file_offset, actual_size);
class BlockAllocator {
 public:
  typedef uint32_t Block;
  static constexpr Block kInvalidBlock = std::numeric_limits<uint32_t>::max();
  struct BlockRange {
    Block start_block;
    uint32_t num_blocks;
  };

  virtual ~BlockAllocator() {}

  // Allocate a contiguous region, as large as possible, of up to
  // requested_size blocks. Fill in start with the first block of the
  // allocation, and actual_size with the number of blocks allocated.
  // If unable to allocate, *start is set to kInvalidBlock.
  virtual void Allocate(uint32_t requested_size, Block* start,
                        uint32_t* actual_size) = 0;

  // Allocate up to requested_size blocks starting at 'start'. Fill in
  // actual_size with the number of blocks which could actually be allocated at
  // that position. (Which may be zero)
  virtual void AllocateAt(uint32_t requested_size, Block start,
                          uint32_t* actual_size) = 0;

  // Release all of the blocks in this range. This function assumes that the
  // region it gets passed is a contiguous region of stuff which has already
  // been allocated using some other function.
  virtual void Release(Block begin, uint32_t size) = 0;

  // Release *all* of the blocks.
  virtual void Clear() = 0;

  // Return the number of free blocks
  virtual uint32_t BlocksFree() const = 0;

  // Populates 'ranges' with free block ranges.
  // 'ranges' is cleared before being populated.
  virtual void GetFreeBlocks(std::vector<BlockRange>* ranges) const = 0;

  // Returns approximate memory usage.
  virtual size_t MemoryUsage() const = 0;

  virtual std::string DebugString() const = 0;
};

DEFINE_REGISTERER(BlockAllocator, uint32_t);

}  // namespace alloc
}  // namespace util

// Implementations of BlockAllocator can register themselves using
// REGISTER_BLOCK_ALLOCATOR(AllocatorName).
// All allocators using this mechanism should have a single argument
// constructor, which takes the number of blocks for the BlockAllocator.
// If more arguments are needed to construct the BlockAllocator consider
// passing those arguments as command line flags.
#define REGISTER_BLOCK_ALLOCATOR(name) \
  REGISTER_ENTITY(name, util::alloc::BlockAllocator)

#endif  // THIRD_PARTY_GLOOP_UTIL_ALLOC_BLOCK_ALLOCATOR_H_
