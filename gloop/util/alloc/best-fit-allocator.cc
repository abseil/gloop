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

#include "gloop/util/alloc/best-fit-allocator.h"

#include <cstdint>

#include "absl/log/check.h"
#include "gloop/util/alloc/block-allocator.h"
#include "gloop/util/alloc/size-based-allocator.h"

namespace util {
namespace alloc {

BestFitAllocator::BestFitAllocator(uint32_t num_blocks)
    : SizeBasedAllocator(num_blocks) {}

BestFitAllocator::~BestFitAllocator() {}

void BestFitAllocator::PickAllocationRange(uint32_t requested_size,
                                           Block* start, uint32_t* actual_size,
                                           Range** chosen_range) const {
  if (ranges_by_size_.empty()) {
    *start = BlockAllocator::kInvalidBlock;
    *actual_size = 0;
    return;
  }

  Range r;
  // Ties between sizes are broken by 'start', and it is important that existing
  // ranges of the same size do not compare as being smaller than 'r' or we do
  // not get "best fit" behavior. Thus we use zero.
  r.start = 0;
  r.size = requested_size;
  // This will point to the smallest range whose size >= requested_size, i.e.,
  // the best fit.
  RangeBySize::const_iterator p = ranges_by_size_.lower_bound(&r);
  if (p == ranges_by_size_.end()) {
    // This won't fit in even the largest range we have free. Grab the entirety
    // of the largest range we have.
    --p;
    *start = (*p)->start;
    *actual_size = (*p)->size;
  } else {
    DCHECK_GE((*p)->size, requested_size);
    *start = (*p)->start;
    *actual_size = requested_size;
  }
  *chosen_range = *p;
}

REGISTER_BLOCK_ALLOCATOR(BestFitAllocator);

}  // namespace alloc
}  // namespace util
