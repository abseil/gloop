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

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "absl/log/check.h"
#include "gloop/base/init_google.h"

static void CheckOneAddr(const char* addr, bool expect, const char* test_case) {
  // Verify we don't touch errno.
  for (int e : {0, EFAULT, 123456}) {
    errno = e;
    CHECK_EQ(base::AddressIsReadable(addr), expect) << test_case;
    CHECK_EQ(errno, e) << test_case;
  }
}
// Test a page mapped with protection prot for readability.
static void TestCase(const char* test_case, int prot, bool expect) {
  static const int pagesize = getpagesize();
  // Map two pages.
  char* page = static_cast<char*>(
      mmap(nullptr, 2 * pagesize, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  PCHECK(page != MAP_FAILED);
  // Make the second page inaccessible.
  PCHECK(mprotect(page + pagesize, pagesize, PROT_NONE) == 0);

  CheckOneAddr(page, expect, test_case);
  CheckOneAddr(page + pagesize - 8, expect, test_case);
  // Second page is never readable, regardless of `prot`.
  CheckOneAddr(page + pagesize, false, test_case);

  // Check mis-aligned bytes at the start and end of the page.
  CheckOneAddr(page + 1, expect, test_case);
  CheckOneAddr(page + pagesize - 7, expect, test_case);
  CheckOneAddr(page + pagesize - 1, expect, test_case);

  PCHECK(munmap(page, 2 * pagesize) == 0);
}

static void TestAddressIsReadable() {
  // Check that AddressIsReadable() doesn't use more than 2 file descriptors
  // even if invoked multiple times.
  // First make a band of allocated file descriptors
  int topfd = 0;
  for (int i = 0; i != 10; i++) {
    topfd = open("/dev/null", 0);
  }
  // After that clear a band.  File descriptors due to AddressIsReadable()
  // will fall in this band.
  for (int i = 0; i != 10; i++) {
    close(topfd + i + 1);
  }
  CHECK_GE(topfd, 0);

  // Try mapped pages with various protections.
  TestCase("PROT_NONE", PROT_NONE, false);
  TestCase("PROT_READ", PROT_READ, true);
  TestCase("PROT_READ|PROT_WRITE", PROT_READ | PROT_WRITE, true);

  // nullptr is never readable
  CHECK(!base::AddressIsReadable(nullptr));
  CHECK(!base::AddressIsReadable(reinterpret_cast<void*>(7)));
  CHECK(!base::AddressIsReadable(reinterpret_cast<void*>(31)));

  // Kernel address space is never readable.
  CHECK(!base::AddressIsReadable(reinterpret_cast<void*>(1ULL << 63)));
  CHECK(!base::AddressIsReadable(reinterpret_cast<void*>((1ULL << 63) + 7)));
  CHECK(!base::AddressIsReadable(reinterpret_cast<void*>((1ULL << 63) + 31)));

  CHECK(!base::AddressIsReadable(reinterpret_cast<void*>(~0UL)));
  CHECK(!base::AddressIsReadable(reinterpret_cast<void*>(~0UL - 7)));
  CHECK(!base::AddressIsReadable(reinterpret_cast<void*>(~0UL - 31)));

  // Try a page that is unmapped.
  void* no_mapping = mmap(nullptr, getpagesize(), PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  PCHECK(no_mapping != MAP_FAILED);
  PCHECK(munmap(no_mapping, getpagesize()) == 0);
  CHECK_EQ(base::AddressIsReadable(no_mapping), false);

  CHECK_LE(open("/dev/null", 0), topfd + 3) << "topfd " << topfd;
}

int main(int argc, char* argv[]) {
  // Run before InitGoogle() to ensure that is only one thread of control,
  // and other file descriptors and mapppings cannot interfere.
  TestAddressIsReadable();

  // InitGoogle is not necessary for the test case above, but asan leak
  // detection won't work without it (b/12596175). (Note: unlike asan, the leak
  // sanitizer feature will detect leaks without relying on InitGoogle.)
  InitGoogle(argv[0], &argc, &argv, true);
  return (0);
}
