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

#include "gloop/base/dl_iterate_phdr_iterator.h"

#include <limits.h>
#include <link.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace gloop {

DlIteratePhdrIterator::DlIteratePhdrIterator() { Populate(); }

DlIteratePhdrIterator::~DlIteratePhdrIterator() {}

void DlIteratePhdrIterator::Populate() {
  segments_.clear();
  index_ = 0;
  // dl_iterate_phdr returns 0 if it successfully iterated over all objects.
  int rc = dl_iterate_phdr(Callback, this);
  valid_ = (rc == 0 && !segments_.empty());
}

int DlIteratePhdrIterator::Callback(struct dl_phdr_info* info, size_t /*size*/,
                                    void* data) {
  DlIteratePhdrIterator* self = static_cast<DlIteratePhdrIterator*>(data);

  std::string filename = info->dlpi_name;
  if (filename.empty()) {
    // Main binary. Resolve /proc/self/exe to get actual path.
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len != -1) {
      buf[len] = '\0';
      filename = buf;
    } else {
      filename = "/proc/self/exe";
    }
  }

  for (int i = 0; i < info->dlpi_phnum; ++i) {
    const auto& phdr = info->dlpi_phdr[i];
    if (phdr.p_type == PT_LOAD) {
      SegmentInfo seg;
      seg.start = info->dlpi_addr + phdr.p_vaddr;
      seg.end = seg.start + phdr.p_memsz;

      seg.flags[0] = (phdr.p_flags & PF_R) ? 'r' : '-';
      seg.flags[1] = (phdr.p_flags & PF_W) ? 'w' : '-';
      seg.flags[2] = (phdr.p_flags & PF_X) ? 'x' : '-';
      seg.flags[3] = 'p';  // Assume private mapping
      seg.flags[4] = '\0';

      seg.offset = phdr.p_offset;
      seg.inode = 0;
      seg.filename = filename;
      seg.dev = 0;

      self->segments_.push_back(seg);
    }
  }
  return 0;  // Keep iterating
}

bool DlIteratePhdrIterator::NextExt(uint64_t* start, uint64_t* end,
                                    char** flags, uint64_t* offset,
                                    int64_t* inode, char** filename,
                                    dev_t* dev) {
  if (index_ >= segments_.size()) {
    return false;
  }
  const auto& seg = segments_[index_];
  if (start) *start = seg.start;
  if (end) *end = seg.end;

  if (flags) {
    std::memcpy(current_flags_, seg.flags, sizeof(seg.flags));
    *flags = current_flags_;
  }
  if (offset) *offset = seg.offset;
  if (inode) *inode = seg.inode;

  if (filename) {
    current_filename_.assign(seg.filename.begin(), seg.filename.end());
    current_filename_.push_back('\0');
    *filename = current_filename_.data();
  }
  if (dev) *dev = seg.dev;

  index_++;
  return true;
}

}  // namespace gloop
