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

// A test for base::AddressIsReadable()

#include "gloop/base/address_is_readable.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/base/config.h"  // IWYU pragma: keep
#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "gloop/base/strerror.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using ::testing::HasSubstr;

// Verify we don't touch errno.
MATCHER_P(HasNoError, expect, "has no error") {
  for (int e : {0, EFAULT, 123456}) {
    errno = e;
    bool actual = base::AddressIsReadable(arg);
    if (actual != expect) {
      *result_listener << "AddressIsReadable(" << arg << ") returned "
                       << (actual ? "true" : "false") << " instead of "
                       << (expect ? "true" : "false");
      return false;
    }
    if (errno != e) {
      *result_listener << "errno changed from " << e << " to " << errno;
      return false;
    }
  }
  return true;
}

TEST(HasNoErrorMatcherTest, ExplainsFailuresCorrectly) {
  testing::StringMatchResultListener listener;

  // nullptr is not readable. Verifying that it is readable (expect = true)
  // should fail and explain the mismatch.
  EXPECT_FALSE(ExplainMatchResult(HasNoError(true), nullptr, &listener));
  EXPECT_THAT(listener.str(), HasSubstr("returned false instead of true"));
}

size_t GetSystemPageSize() {
  const int64_t pagesize = sysconf(_SC_PAGESIZE);
  CHECK_GT(pagesize, 0);
  return static_cast<size_t>(pagesize);
}

// Check that AddressIsReadable() doesn't leak file descriptors.
TEST(AddressIsReadableTest, DoesNotLeakFds) {
  // Perform a mmap/unmap sequence outside the fd-tracking window.
  const size_t pagesize = GetSystemPageSize();
  void* page =
      mmap(nullptr, pagesize, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ASSERT_NE(page, MAP_FAILED) << base::StrError(errno);
  absl::Cleanup cleanup = [page, pagesize]() {
    EXPECT_EQ(munmap(page, pagesize), 0) << base::StrError(errno);
  };

  const int initial_fd = open("/dev/null", O_RDONLY);
  ASSERT_GE(initial_fd, 0) << base::StrError(errno);
  close(initial_fd);

  // Probe readability
  EXPECT_FALSE(base::AddressIsReadable(nullptr));
  EXPECT_TRUE(base::AddressIsReadable(page));

  // Probe the next available file descriptor again.
  const int final_fd = open("/dev/null", O_RDONLY);
  ASSERT_GE(final_fd, 0) << base::StrError(errno);
  close(final_fd);

#ifdef ABSL_HAVE_THREAD_SANITIZER
  // TSAN background threads can transiently allocate FDs.
  EXPECT_LE(final_fd, initial_fd + 1)
      << "initial_fd=" << initial_fd << ", final_fd=" << final_fd;
#else
  // Ensure no file descriptors were leaked.
  EXPECT_EQ(final_fd, initial_fd);
#endif
}

struct ProtectionParam {
  std::string name;
  int prot;
  bool expect;
};

// Test a page mapped with protection prot for readability.
class AddressIsReadableProtectionTest
    : public ::testing::TestWithParam<ProtectionParam> {};

// Try mapped pages with various protections.
TEST_P(AddressIsReadableProtectionTest, VerifiesReadability) {
  const auto& [name, prot, expect] = GetParam();
  const size_t pagesize = GetSystemPageSize();

  // Map two pages.
  char* page = static_cast<char*>(
      mmap(nullptr, 2 * pagesize, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  ASSERT_NE(page, MAP_FAILED) << base::StrError(errno);

  absl::Cleanup cleanup = [page, pagesize]() {
    EXPECT_EQ(munmap(page, 2 * pagesize), 0) << base::StrError(errno);
  };

  // Make the second page inaccessible.
  ASSERT_EQ(mprotect(page + pagesize, pagesize, PROT_NONE), 0)
      << base::StrError(errno);

  EXPECT_THAT(page, HasNoError(expect)) << name;
  EXPECT_THAT(page + pagesize - 8, HasNoError(expect)) << name;
  // Second page is never readable, regardless of `prot`.
  EXPECT_THAT(page + pagesize, HasNoError(false)) << name;

  // Check mis-aligned bytes at the start and end of the page.
  EXPECT_THAT(page + 1, HasNoError(expect)) << name;
  EXPECT_THAT(page + pagesize - 7, HasNoError(expect)) << name;
  EXPECT_THAT(page + pagesize - 1, HasNoError(expect)) << name;
}

INSTANTIATE_TEST_SUITE_P(
    ProtectionModes, AddressIsReadableProtectionTest,
    ::testing::Values(ProtectionParam{"PROT_NONE", PROT_NONE, false},
                      ProtectionParam{"PROT_READ", PROT_READ, true},
                      ProtectionParam{"PROT_READ_PROT_WRITE",
                                      PROT_READ | PROT_WRITE, true}),
    [](const ::testing::TestParamInfo<ProtectionParam>& info) {
      return info.param.name;
    });

// nullptr is never readable
TEST(AddressIsReadableTest, NullptrIsUnreadable) {
  EXPECT_FALSE(base::AddressIsReadable(nullptr));
}

TEST(AddressIsReadableTest, LowAddressesAreUnreadable) {
  EXPECT_FALSE(base::AddressIsReadable(reinterpret_cast<void*>(7)));
  EXPECT_FALSE(base::AddressIsReadable(reinterpret_cast<void*>(31)));
}

TEST(AddressIsReadableTest, KernelAddressesAreUnreadable) {
  if constexpr (sizeof(void*) != 8) {
    GTEST_SKIP() << "Skipping test on non-64-bit platforms";
  }
  const uintptr_t kernel_base = static_cast<uintptr_t>(uint64_t{1} << 63);
  EXPECT_FALSE(base::AddressIsReadable(reinterpret_cast<void*>(kernel_base)));
  EXPECT_FALSE(
      base::AddressIsReadable(reinterpret_cast<void*>(kernel_base + 7)));
  EXPECT_FALSE(
      base::AddressIsReadable(reinterpret_cast<void*>(kernel_base + 31)));
}

TEST(AddressIsReadableTest, EndOfAddressSpaceIsUnreadable) {
  EXPECT_FALSE(base::AddressIsReadable(reinterpret_cast<void*>(~uintptr_t{0})));
  EXPECT_FALSE(
      base::AddressIsReadable(reinterpret_cast<void*>(~uintptr_t{0} - 7)));
  EXPECT_FALSE(
      base::AddressIsReadable(reinterpret_cast<void*>(~uintptr_t{0} - 31)));
}

// Try a page that is unmapped.
TEST(AddressIsReadableTest, UnmappedPageIsUnreadable) {
  const size_t pagesize = GetSystemPageSize();
  // Map 3 contiguous pages.
  char* pages =
      static_cast<char*>(mmap(nullptr, 3 * pagesize, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  ASSERT_NE(pages, MAP_FAILED) << base::StrError(errno);

  // Unmap the middle page to create a 1-page unmapped hole.
  char* middle_page = pages + pagesize;
  ASSERT_EQ(munmap(middle_page, pagesize), 0) << base::StrError(errno);

  // Clean up the remaining pages.
  char* third_page = pages + 2 * pagesize;
  absl::Cleanup cleanup = [pages, third_page, pagesize]() {
    EXPECT_EQ(munmap(pages, pagesize), 0) << base::StrError(errno);
    EXPECT_EQ(munmap(third_page, pagesize), 0) << base::StrError(errno);
  };

  EXPECT_FALSE(base::AddressIsReadable(middle_page));
}

}  // namespace
