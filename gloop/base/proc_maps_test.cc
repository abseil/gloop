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

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

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

TEST(ProcMapsIteratorTest, NextExtReadsCurrentProcessMaps) {
  ProcMapsIterator::Buffer buffer;
  ProcMapsIterator it(0, &buffer);
  ASSERT_TRUE(it.Valid());
  uint64_t start, end, offset;
  int64_t inode;
  char* flags;
  char* filename;
  dev_t dev;
  int count = 0;
  bool found_code = false;
  const uintptr_t self_addr =
      reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
  bool found_stack = false;
  while (it.NextExt(&start, &end, &flags, &offset, &inode, &filename, &dev)) {
    EXPECT_LT(start, end);
    ASSERT_NE(flags, nullptr);
    EXPECT_EQ(strlen(flags), 4u);
    ASSERT_NE(filename, nullptr);
    if (absl::StrContains(flags, 'x')) {
      found_code = true;
    }
    if (self_addr >= start && self_addr < end) {
      found_stack = true;
    }
    ++count;
  }
  EXPECT_GT(count, 0);
  EXPECT_TRUE(found_code);
  EXPECT_TRUE(found_stack);
}

#if defined(__linux__)
TEST(ProcMapsIteratorTest, ParseProcMapsLine) {
  proc_maps_internal::ParsedLine parsed{};
  ASSERT_TRUE(proc_maps_internal::ParseProcMapsLine(
      "  00001000-00002000   r-xp   00000100   fc:227   123456      /bin/cat",
      &parsed));
  EXPECT_EQ(parsed.start, 0x1000u);
  EXPECT_EQ(parsed.end, 0x2000u);
  EXPECT_STREQ(parsed.flags, "r-xp");
  EXPECT_EQ(parsed.offset, 0x100u);
  EXPECT_EQ(parsed.major, 0xfcu);
  EXPECT_EQ(parsed.minor, 0x227u);
  EXPECT_EQ(parsed.inode, 123456);
  EXPECT_EQ(parsed.filename, "/bin/cat");

  ASSERT_TRUE(proc_maps_internal::ParseProcMapsLine(
      "00001000-00002000 rw-s 00000000 00:00 0           ", &parsed));
  EXPECT_EQ(parsed.start, 0x1000u);
  EXPECT_EQ(parsed.end, 0x2000u);
  EXPECT_STREQ(parsed.flags, "rw-s");
  EXPECT_EQ(parsed.offset, 0u);
  EXPECT_EQ(parsed.major, 0u);
  EXPECT_EQ(parsed.minor, 0u);
  EXPECT_EQ(parsed.inode, 0);
  EXPECT_EQ(parsed.filename, "");

  EXPECT_FALSE(proc_maps_internal::ParseProcMapsLine("", &parsed));
  EXPECT_FALSE(proc_maps_internal::ParseProcMapsLine("   ", &parsed));
  EXPECT_FALSE(proc_maps_internal::ParseProcMapsLine("1000", &parsed));
  EXPECT_FALSE(proc_maps_internal::ParseProcMapsLine("zzzz-2000 r-xp 0 00:00 0",
                                                     &parsed));
  EXPECT_FALSE(proc_maps_internal::ParseProcMapsLine("1000-zzzz r-xp 0 00:00 0",
                                                     &parsed));
  EXPECT_FALSE(proc_maps_internal::ParseProcMapsLine(
      "1000-2000 rwxps 0 00:00 0", &parsed));
  EXPECT_FALSE(proc_maps_internal::ParseProcMapsLine(
      "1000\t-2000 r-xp 0 00:00 0", &parsed));
  EXPECT_FALSE(proc_maps_internal::ParseProcMapsLine(
      "1000-2000 r-xp zzzz 00:00 0", &parsed));
  EXPECT_FALSE(proc_maps_internal::ParseProcMapsLine("1000-2000 r-xp 0 zz:00 0",
                                                     &parsed));
  EXPECT_FALSE(proc_maps_internal::ParseProcMapsLine("1000-2000 r-xp 0 00:zz 0",
                                                     &parsed));
  EXPECT_FALSE(proc_maps_internal::ParseProcMapsLine(
      "1000-2000 r-xp 0 00:00 notanint", &parsed));
}
#endif

}  // namespace
