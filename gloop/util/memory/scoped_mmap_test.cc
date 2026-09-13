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

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "gloop/base/proc_maps.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

std::string GetVmaName(const void* addr) {
  ProcMapsIterator::Buffer buf;
  ProcMapsIterator it(0, &buf);

  const uint64_t target = reinterpret_cast<uintptr_t>(addr);
  uint64_t begin, end;
  char* path;
  while (it.Next(&begin, &end, nullptr, nullptr, nullptr, &path)) {
    if (begin <= target && target < end) {
      return path;
    }
  }
  return "";
}

TEST(ScopedMmapTest, NamedVmaOnAnonymousPrivateMapping) {
  ScopedMmap scoped_mmap;
  const size_t page_size = static_cast<size_t>(getpagesize());
  void* addr =
      scoped_mmap.Map(-1, 0, page_size, page_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS);
  ASSERT_NE(addr, nullptr);
  EXPECT_TRUE(scoped_mmap.IsMapped());

  EXPECT_THAT(GetVmaName(addr), testing::Eq("[anon:scoped_mmap]"));

  scoped_mmap.Unmap();
  EXPECT_FALSE(scoped_mmap.IsMapped());
  EXPECT_THAT(GetVmaName(addr),
              testing::Not(testing::HasSubstr("scoped_mmap")));
}

TEST(ScopedMmapTest, NamedVmaOnAnonymousSharedMapping) {
  ScopedMmap scoped_mmap;
  const size_t page_size = static_cast<size_t>(getpagesize());
  void* addr =
      scoped_mmap.Map(-1, 0, page_size, page_size, PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_ANONYMOUS);
  ASSERT_NE(addr, nullptr);
  EXPECT_TRUE(scoped_mmap.IsMapped());

  EXPECT_THAT(GetVmaName(addr), testing::Eq("[anon_shmem:scoped_mmap]"));

  scoped_mmap.Unmap();
  EXPECT_FALSE(scoped_mmap.IsMapped());
  EXPECT_THAT(GetVmaName(addr),
              testing::Not(testing::HasSubstr("scoped_mmap")));
}

TEST(ScopedMmapTest, NoNamedVmaOnNonAnonymousMapping) {
  const int fd = open("/dev/zero", O_RDONLY);
  ASSERT_GE(fd, 0);

  ScopedMmap scoped_mmap;
  const size_t page_size = static_cast<size_t>(getpagesize());
  void* addr =
      scoped_mmap.Map(fd, 0, page_size, page_size, PROT_READ, MAP_PRIVATE);
  ASSERT_NE(addr, nullptr);
  EXPECT_TRUE(scoped_mmap.IsMapped());

  EXPECT_THAT(GetVmaName(addr),
              testing::Not(testing::HasSubstr("scoped_mmap")));
  EXPECT_EQ(GetVmaName(addr), "/dev/zero");

  scoped_mmap.Unmap();
  EXPECT_FALSE(scoped_mmap.IsMapped());
  close(fd);
}

}  // namespace
