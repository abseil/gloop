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

// unit test for hugepage text

#include <string.h>
#include <sys/mman.h>

#include <cstdint>
#include <string>

#include "absl/base/casts.h"
#include "absl/debugging/symbolize.h"
#include "absl/flags/flag.h"
#include "absl/log/flags.h"
#include "absl/strings/match.h"
#include "gloop/base/commandlineflags.h"
#include "gloop/base/init_google.h"
#include "gloop/util/symbolize/symbolize.h"
#include "gtest/gtest.h"

DEFINE_string(
    proc_self_exe, "",
    "If non-empty, use file proxy to redirect /proc/self/exe elsewhere");

namespace {

using program_image_remapper::HugePageOptions;
using program_image_remapper::PopulateHugePageOptions;

#if defined(__x86_64__)
const int kHpageShift = 21;
const int kHpageSize = (1 << kHpageShift);
const int kHpageMask ABSL_ATTRIBUTE_UNUSED = (~(kHpageSize - 1));

// Force the binary to be large enough that a THP .text remap will succeed.
const char kHpageTextPadding[kHpageSize * 2] ABSL_ATTRIBUTE_UNUSED
    __attribute__((section(".text"))) = "";

bool CheckForSymbol();

// Test text reload via anonymous hugepage mapping.
TEST(HugepageTextAnon, Load) {
  HugePageOptions options = PopulateHugePageOptions();
  options.internal_allow_remap_on_tmpfs = true;

  ReloadElfTextInHugePages(options);

  EXPECT_TRUE(CheckForSymbol());
}

// Test read-only reload via anonymous hugepage mapping.
TEST(HugepageReadonly, AnonymousLoad) {
  HugePageOptions options = PopulateHugePageOptions();
  options.internal_allow_remap_on_tmpfs = true;
  options.allow_file_backed_thp = false;

  char* mapping =
      static_cast<char*>(mmap(nullptr, kHpageSize * 2, PROT_READ | PROT_WRITE,
                              MAP_ANONYMOUS | MAP_PRIVATE, -1, 0));
  ASSERT_NE(mapping, MAP_FAILED);
  char* firsthpage = reinterpret_cast<char*>(
      ((reinterpret_cast<uintptr_t>(mapping) + kHpageSize - 1) / kHpageSize) *
      kHpageSize);
  firsthpage[0] = 0x42;
  ASSERT_EQ(0, mprotect(mapping, kHpageSize * 2, PROT_READ));
  EXPECT_TRUE(
      ReloadInHugePages(mapping, PROT_READ, kHpageSize * 2, 0, "a42", options));
  EXPECT_EQ(0x42, firsthpage[0]);
  EXPECT_TRUE(CheckForSymbol());
}

// Test whether a remapped segment can resolve symbolic function name.
extern "C" {
void func() {
  volatile int a = 0;
  ++a;
}
}

bool CheckForSymbol() {
  char name[512];
  memset(name, 0, sizeof(name));
  absl::Symbolize(absl::bit_cast<void*>(&func), name, sizeof(name));

  const util::SymbolMap& map = util::SymbolMap::GetCached();
  const char* name2;
  uint64_t start, size;
  EXPECT_TRUE(map.GetSymbolInfoAtPosition(absl::bit_cast<uint64_t>(&func),
                                          &name2, &start, &size));
  EXPECT_STREQ(name2, "func");
  EXPECT_EQ(absl::bit_cast<void*>(&func), absl::bit_cast<void*>(start));

  return absl::EndsWith(name, "func");
}

#endif  // defined(__x86_64__)
}  // namespace

ABSL_ATTRIBUTE_NO_TAIL_CALL int main(int argc, char** argv) {
  absl::SetFlag(&FLAGS_logtostderr, true);
  InitGoogle(argv[0], &argc, &argv, true);

  absl::SetFlag(&FLAGS_hugepage_text, true);

  if (!absl::GetFlag(FLAGS_proc_self_exe).empty()) {
    // Redirect /proc/self/exe.
    SimpleFileProxy proxy{"/proc/self/exe", absl::GetFlag(FLAGS_proc_self_exe)};
    return RUN_ALL_TESTS();
  }
  return RUN_ALL_TESTS();
}
