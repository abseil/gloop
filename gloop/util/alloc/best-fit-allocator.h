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

#ifndef THIRD_PARTY_GLOOP_UTIL_ALLOC_BEST_FIT_ALLOCATOR_H_
#define THIRD_PARTY_GLOOP_UTIL_ALLOC_BEST_FIT_ALLOCATOR_H_

#include <cstdint>

#include "absl/base/thread_annotations.h"
#include "gloop/util/alloc/size-based-allocator.h"

namespace util {
namespace alloc {

// This class implements a BlockAllocator based on the best fit allocation
// algorithm: it allocates blocks in the smallest unallocated range which will
// hold the requested size, or take up the entire largest range, if no range
// will hold everything. It's a good simple default allocator.
//
// This class is thread-safe.
class BestFitAllocator : public SizeBasedAllocator {
 public:
  explicit BestFitAllocator(uint32_t num_blocks);

  // This type is neither copyable nor movable.
  BestFitAllocator(const BestFitAllocator&) = delete;
  BestFitAllocator& operator=(const BestFitAllocator&) = delete;

  virtual ~BestFitAllocator();

 private:
  // Picks the smallest range whose size >= requested_size. If no such
  // range can be found, returns the largest range with size < requested_size.
  virtual void PickAllocationRange(uint32_t requested_size, Block* start,
                                   uint32_t* actual_size,
                                   Range** chosen_range) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(lock_);
};

}  // namespace alloc
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_ALLOC_BEST_FIT_ALLOCATOR_H_
