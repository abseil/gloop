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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"

namespace util {
namespace alloc {

SizeBasedAllocator::SizeBasedAllocator(uint32_t num_blocks)
    : num_blocks_(num_blocks) {
  Clear();
}

SizeBasedAllocator::~SizeBasedAllocator() {
  for (auto p : ranges_by_pos_) {
    delete p;
  }
}

void SizeBasedAllocator::Allocate(uint32_t requested_size, Block* start,
                                  uint32_t* actual_size) {
  absl::MutexLock l(lock_);
  VLOG(5) << "Req alloc size " << requested_size << ": prev "
          << DebugStringLocked();
  Range* chosen_range = nullptr;
  *actual_size = 0;
  PickAllocationRange(requested_size, start, actual_size, &chosen_range);
  if (*actual_size == 0) {
    VLOG(5) << "No free space; returning zero";
    return;
  }
  Mark(*start, *actual_size, chosen_range);
  VLOG(5) << "Allocated " << *actual_size << " at " << *start << ": now "
          << DebugStringLocked();

  DCHECK_EQ(ranges_by_pos_.size(), ranges_by_size_.size())
      << DebugStringLocked();
}

void SizeBasedAllocator::AllocateAt(uint32_t requested_size, Block start,
                                    uint32_t* actual_size) {
  *actual_size = 0;

  absl::MutexLock l(lock_);
  VLOG(5) << "Req alloc size " << requested_size << " at " << start << ": prev "
          << DebugStringLocked();
  if (ranges_by_pos_.empty()) return;

  Range r;
  r.start = start;
  // This is the first range whose position > start
  RangeByPos::iterator p = ranges_by_pos_.upper_bound(&r);
  if (p == ranges_by_pos_.begin()) return;
  --p;

  DCHECK_LE((*p)->start, start);
  const Block range_end = (*p)->end();
  if (range_end > start) {
    *actual_size = std::min(range_end - start, requested_size);
    Mark(start, *actual_size, *p);
    VLOG(5) << "Successful AllocateAt: now " << DebugStringLocked();
  }

  DCHECK_EQ(ranges_by_pos_.size(), ranges_by_size_.size())
      << DebugStringLocked();
}

void SizeBasedAllocator::Mark(Block start, uint32_t size, Range* old_range) {
  VLOG(5) << "Mark from " << start << " size " << size << " in range from "
          << old_range->start << " size " << old_range->size;
  DCHECK_GE(blocks_free_, size);
  DCHECK_GE(start, old_range->start);
  DCHECK_LE(start + size, old_range->start + old_range->size);

  RangeByPos::iterator pos_iterator = ranges_by_pos_.find(old_range);
  DCHECK(pos_iterator != ranges_by_pos_.end()) << DebugStringLocked();
  RangeBySize::iterator size_iterator = ranges_by_size_.find(old_range);
  DCHECK(size_iterator != ranges_by_size_.end()) << DebugStringLocked();

  // Nuke the old ranges!
  ranges_by_size_.erase(*size_iterator);
  ranges_by_pos_.erase(*pos_iterator);

  // Create new ranges
  if (start > old_range->start) {
    Range* new_left = new Range;
    new_left->start = old_range->start;
    new_left->size = start - old_range->start;
    ranges_by_pos_.insert(new_left);
    ranges_by_size_.insert(new_left);
  }

  if (old_range->size + old_range->start > start + size) {
    Range* new_right = new Range;
    new_right->start = start + size;
    new_right->size = old_range->start + old_range->size - new_right->start;
    ranges_by_pos_.insert(new_right);
    ranges_by_size_.insert(new_right);
  }

  delete old_range;
  blocks_free_ -= size;
}

inline SizeBasedAllocator::RangeByPos::iterator SizeBasedAllocator::Insert(
    Range* r) {
  CHECK(ranges_by_size_.insert(r).second)
      << "Couldn't insert start " << r->start << " size " << r->size
      << " into size array: " << DebugStringLocked();
  std::pair<RangeByPos::iterator, bool> p = ranges_by_pos_.insert(r);
  CHECK(p.second) << "Couldn't insert start " << r->start << " size " << r->size
                  << " into pos array: " << DebugStringLocked();
  return p.first;
}

SizeBasedAllocator::RangeByPos::iterator SizeBasedAllocator::MergeRanges(
    RangeByPos::iterator* left, RangeByPos::iterator* right) {
  // For legibility
  Range* l = **left;
  Range* r = **right;

  DCHECK_EQ(l->end(), r->start);
  Range* merged = new Range;
  merged->start = l->start;
  merged->size = r->end() - l->start;

  ranges_by_size_.erase(l);
  ranges_by_size_.erase(r);
  ranges_by_pos_.erase(l);
  ranges_by_pos_.erase(r);

  delete l;
  delete r;

  return Insert(merged);
}

void SizeBasedAllocator::Release(Block begin, uint32_t size) {
  if (size == 0) return;

  Range* new_range = new Range;
  new_range->start = begin;
  new_range->size = size;

  absl::MutexLock l(lock_);
  VLOG(5) << "Release " << size << " from " << begin << ": prev "
          << DebugStringLocked();
  RangeByPos::iterator p = Insert(new_range);

  if (p != ranges_by_pos_.begin()) {
    RangeByPos::iterator left = p;
    --left;
    CHECK_LE((*left)->end(), (*p)->start);
    if ((*left)->end() == (*p)->start) {
      p = MergeRanges(&left, &p);
    }
  }

  RangeByPos::iterator right = p;
  ++right;
  if (right != ranges_by_pos_.end()) {
    CHECK_LE((*p)->end(), (*right)->start);
    if ((*p)->end() == (*right)->start) {
      p = MergeRanges(&p, &right);
    }
  }

  blocks_free_ += size;

  VLOG(5) << "Released; now " << DebugStringLocked();

  DCHECK_EQ(ranges_by_pos_.size(), ranges_by_size_.size())
      << DebugStringLocked();
}

void SizeBasedAllocator::Clear() {
  absl::MutexLock l(lock_);
  for (auto p : ranges_by_pos_) {
    delete p;
  }
  ranges_by_pos_.clear();
  ranges_by_size_.clear();
  blocks_free_ = num_blocks_;

  Range* blank_range = new Range;
  blank_range->start = 0;
  blank_range->size = num_blocks_;
  ranges_by_pos_.insert(blank_range);
  ranges_by_size_.insert(blank_range);
}

std::string SizeBasedAllocator::DebugString() const {
  absl::ReaderMutexLock l(lock_);
  return DebugStringLocked();
}

uint32_t SizeBasedAllocator::BlocksFree() const {
  absl::ReaderMutexLock l(lock_);
  return blocks_free_;
}

void SizeBasedAllocator::GetFreeBlocks(std::vector<BlockRange>* ranges) const {
  ranges->clear();
  absl::ReaderMutexLock l(lock_);
  for (auto p : ranges_by_pos_) {
    BlockRange range;
    range.start_block = p->start;
    range.num_blocks = p->size;
    ranges->push_back(range);
  }
}

uint32_t SizeBasedAllocator::LargestFreeBlockRange() const {
  absl::ReaderMutexLock l(lock_);
  if (ranges_by_size_.empty()) {
    return 0;
  }
  return (*ranges_by_size_.crbegin())->size;
}

// This method accounts for all memory allocated directly by this class, but
// does not account for the memory allocated internally by the ranges_by_pos_
// and ranges_by_size_ containers.
size_t SizeBasedAllocator::MemoryUsage() const {
  absl::ReaderMutexLock l(lock_);
  DCHECK_EQ(ranges_by_pos_.size(), ranges_by_size_.size());
  return ranges_by_pos_.size() * kRangeMemoryUsage;
}

std::string SizeBasedAllocator::DebugStringLocked() const {
  std::string result =
      absl::StrFormat("Total blocks: %u free: %u\n", num_blocks_, blocks_free_);
  int range_num = 0;
  Block last_end = 0;
  for (auto p : ranges_by_pos_) {
    const Range& r = *p;
    if (r.start > last_end) {
      absl::StrAppendFormat(&result, "%5d Allocated: start %5u size %5u\n",
                            range_num, last_end, r.start - last_end);
      ++range_num;
    }
    absl::StrAppendFormat(&result, "%5d Free:      start %5u size %5u\n",
                          range_num, r.start, r.size);
    ++range_num;
    last_end = r.start + r.size;
  }
  if (last_end < num_blocks_) {
    absl::StrAppendFormat(&result, "%5d Allocated: start %5u size %5u\n",
                          range_num, last_end, num_blocks_ - last_end);
  }
  return result;
}

}  // namespace alloc
}  // namespace util
