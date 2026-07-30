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

#include <string>

#include "gloop/base/init_google.h"
#include "gloop/testing/production_stub/testvalue.h"
#include "gloop/util/symbolize/elf_reader.h"
#include "gtest/gtest.h"

namespace {

TEST(ElfReaderSandboxTest, ProcSelfCmdlineFallbackSucceeds) {
  testing::testvalue::Enable();
  // Set the override to a nonexistent path so that the primary open() fails,
  // triggering the fallback logic exactly as it does for the broken
  // /proc/self/exe symlink in NSJail.
  const std::string nonexistent_path = "/no/such/file";
  testing::testvalue::Force("elf_reader_proc_self_exe", nonexistent_path);

  // Call ElfReader with the exact same path.
  util::ElfReader reader(nonexistent_path);

  EXPECT_TRUE(reader.IsNativeElfFile());
}

}  // namespace

int main(int argc, char** argv) {
  // Simulate the absence of a valid GetArgv0() (as happens in Deploy JAR
  // launchers that do not call InitGoogle) by setting argv[0] to an empty
  // string before initialization, while still satisfying GoogleTest's strict
  // requirement that InitGoogle is called.
  //
  // Note: We only alter the user-space pointer, rather than mutating the
  // underlying buffer (e.g. via argv[0][0] = '\0'). The kernel's implementation
  // of /proc/self/cmdline reads directly from the underlying buffer. Mutating
  // it would truncate the first string returned by reading /proc/self/cmdline
  // and break our own fallback logic, violating how Deploy JARs actually behave
  // in production (where the kernel buffer is left untouched, but InitGoogle is
  // simply omitted).
  static char empty_argv0[] = "";
  argv[0] = empty_argv0;
  InitGoogle(argv[0], &argc, &argv, true);
  return RUN_ALL_TESTS();
}
