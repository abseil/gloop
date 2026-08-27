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

#include "gloop/util/memory/scoped_mmap.h"

#include <errno.h>
#include <sys/mman.h>
#include <sys/prctl.h>

#include <cstddef>
#include <cstdint>

#include "absl/log/check.h"
#include "absl/log/log.h"

#ifndef PR_SET_VMA
#define PR_SET_VMA 0x53564d41
#endif
#ifndef PR_SET_VMA_ANON_NAME
#define PR_SET_VMA_ANON_NAME 0
#endif

ScopedMmap::ScopedMmap() { Reset(); }

ScopedMmap::~ScopedMmap() {
  if (IsMapped()) Unmap();
}

void* ScopedMmap::Map(const int file_descriptor, const size_t offset,
                      const size_t size, const size_t alignment,
                      const int mmap_prot, const int mmap_flags) {
  CHECK_GE(file_descriptor, -1);
  CHECK(size);
  CHECK(alignment);

  // align map offset to specified alignment
  const size_t file_map_offset = offset % alignment;
  const size_t aligned_file_map_offset = offset - file_map_offset;
  // adjust the mapped region size so the entire
  // offset...offset+size region is included
  aligned_file_map_size_ = size + file_map_offset;

  // map the specified page(s) into this process' address space as read only
  process_address_ = mmap(nullptr, aligned_file_map_size_, mmap_prot,
                          mmap_flags, file_descriptor, aligned_file_map_offset);
  if (MAP_FAILED == process_address_) {
    LOG(WARNING) << "Failed to map file region " << offset << " length "
                 << size;
    Reset();
    return nullptr;
  }
  if ((mmap_flags & MAP_ANONYMOUS) != 0) {
    (void)prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, process_address_,
                aligned_file_map_size_, "scoped_mmap");
  }
  return static_cast<uint8_t*>(process_address_) + file_map_offset;
}

void ScopedMmap::Unmap() {
  // unmap page(s)
  if (munmap(process_address_, aligned_file_map_size_) < 0) {
    LOG(WARNING) << "Failed to unmap address " << process_address_ << " size "
                 << aligned_file_map_size_ << " errno=" << errno;
  }
  Reset();
}

bool ScopedMmap::IsMapped() const {
  return process_address_ && aligned_file_map_size_;
}

void ScopedMmap::Reset() {
  process_address_ = nullptr;
  aligned_file_map_size_ = 0;
}
