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

#include <cstdint>
#include <string>
#include <vector>

#include "absl/strings/match.h"
#include "gtest/gtest.h"

namespace gloop {
namespace {

TEST(DlIteratePhdrIteratorTest, Basic) {
  DlIteratePhdrIterator it;
  EXPECT_TRUE(it.Valid());

  uint64_t start, end, offset;
  char* flags;
  int64_t inode;
  char* filename;
  dev_t dev;

  bool found_self = false;
  while (it.NextExt(&start, &end, &flags, &offset, &inode, &filename, &dev)) {
    if (absl::StrContains(filename, "dl_Uiterate_Uphdr")) {
      found_self = true;
      EXPECT_EQ(flags[3], 'p');  // Private
    }
  }
  EXPECT_TRUE(found_self);
}

}  // namespace
}  // namespace gloop
