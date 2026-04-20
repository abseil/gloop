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

#ifndef THIRD_PARTY_GLOOP_BASE_DL_ITERATE_PHDR_ITERATOR_H_
#define THIRD_PARTY_GLOOP_BASE_DL_ITERATE_PHDR_ITERATOR_H_

#include <link.h>
#include <sys/types.h>

#include <cstdint>
#include <string>
#include <vector>

namespace gloop {

class DlIteratePhdrIterator {
 public:
  struct SegmentInfo {
    uint64_t start;
    uint64_t end;
    char flags[5];  // "rwxp" + null
    uint64_t offset;
    int64_t inode;
    std::string filename;
    dev_t dev;
  };

  DlIteratePhdrIterator();
  ~DlIteratePhdrIterator();

  bool NextExt(uint64_t* start, uint64_t* end, char** flags, uint64_t* offset,
               int64_t* inode, char** filename, dev_t* dev);

  bool Valid() const { return valid_; }

 private:
  void Populate();
  static int Callback(struct dl_phdr_info* info, size_t size, void* data);

  std::vector<SegmentInfo> segments_;
  size_t index_ = 0;
  bool valid_ = false;

  // Buffers to return mutable pointers in NextExt
  char current_flags_[5];
  std::vector<char> current_filename_;
};

}  // namespace gloop

#endif  // THIRD_PARTY_GLOOP_BASE_DL_ITERATE_PHDR_ITERATOR_H_
