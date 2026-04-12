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

#include "gloop/base/proc_maps.h"

#include <sys/sysmacros.h>

#include "absl/strings/string_view.h"
#include "gloop/gloop_test.h"

namespace {

TEST(ProcMapsIteratorTest, FormatLineWithLargeMinorDeviceNumber) {
  char line[ProcMapsIterator::Buffer::kBufSize];
  int len = ProcMapsIterator::FormatLine(line, sizeof(line), 0x1000, 0x2000,
                                         "r-xp", 0x100, 123456, "/bin/cat",
                                         makedev(0xfc, 0x227));
  EXPECT_EQ(absl::string_view(line, len),
            "00001000-00002000 r-xp 00000100 fc:227 123456      /bin/cat\n");
}

TEST(ProcMapsIteratorTest, FormatLineWithSharedMapping) {
  char line[ProcMapsIterator::Buffer::kBufSize];
  int len = ProcMapsIterator::FormatLine(line, sizeof(line), 0x1000, 0x2000,
                                         "rw-s", 0, 0, "", 0);
  EXPECT_EQ(absl::string_view(line, len),
            "00001000-00002000 rw-s 00000000 00:00 0           \n");
}

}  // namespace
