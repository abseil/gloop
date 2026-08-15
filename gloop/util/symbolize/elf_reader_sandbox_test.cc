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

#include <cstddef>
#include <cstring>
#include <string>

#include "absl/cleanup/cleanup.h"
#include "gloop/base/init_google.h"
#include "gloop/testing/production_stub/testvalue.h"
#include "gloop/util/symbolize/elf_reader.h"
#include "gtest/gtest.h"

namespace {

char* g_original_argv0_buffer = nullptr;

TEST(ElfReaderSandboxTest, ProcSelfCmdlineFallback) {
  testing::testvalue::Enable();
  // Set the override to a nonexistent path so that the primary open() fails,
  // triggering the fallback logic exactly as it does for the broken
  // /proc/self/exe symlink in NSJail.
  const std::string nonexistent_path = "/no/such/file";
  testing::testvalue::Force("elf_reader_proc_self_exe", nonexistent_path);
  // Force Auxv fallback to fail by setting execfn to an empty string, covering
  // the failure branch in OpenExeFromAuxv().
  const char* empty_execfn = "";
  testing::testvalue::Force("elf_reader_auxv_execfn", empty_execfn);

  absl::Cleanup cleanup = [] {
    testing::testvalue::Clear("elf_reader_proc_self_exe");
    testing::testvalue::Clear("elf_reader_auxv_execfn");
  };

  // Call ElfReader with the exact same path.
  util::ElfReader reader(nonexistent_path);

  EXPECT_TRUE(reader.IsNativeElfFile());
}

TEST(ElfReaderSandboxTest, ProcSelfGetArgv0Fallback) {
  testing::testvalue::Enable();
  const std::string nonexistent_path = "/no/such/file";
  testing::testvalue::Force("elf_reader_proc_self_exe", nonexistent_path);
  // Force Auxv fallback to fail by setting execfn to empty.
  const char* empty_execfn = "";
  testing::testvalue::Force("elf_reader_auxv_execfn", empty_execfn);

  ASSERT_NE(g_original_argv0_buffer, nullptr);
  std::string original = g_original_argv0_buffer;
  size_t len = original.size();
  ASSERT_GT(len, 0);

  absl::Cleanup restore = [&] {
    memcpy(g_original_argv0_buffer, original.data(), len + 1);
    testing::testvalue::Clear("elf_reader_proc_self_exe");
    testing::testvalue::Clear("elf_reader_auxv_execfn");
    testing::testvalue::Clear("elf_reader_argv0");
  };

  // Invalidate /proc/self/cmdline so the test doesn't pass because of it.
  strncpy(g_original_argv0_buffer, "123_name_not_on_disk", len);
  g_original_argv0_buffer[len - 1] = '\0';

  const char* valid_argv0 = original.c_str();
  testing::testvalue::Force("elf_reader_argv0", valid_argv0);

  util::ElfReader reader(nonexistent_path);

  EXPECT_TRUE(reader.IsNativeElfFile());
}

TEST(ElfReaderSandboxTest, ProcSelfAuxvFallback) {
  testing::testvalue::Enable();
  const std::string nonexistent_path = "/no/such/file";
  testing::testvalue::Force("elf_reader_proc_self_exe", nonexistent_path);

  // Simulate launcher behavior where a self-contained executable overwrites
  // its argv[0] with a custom process title (e.g. "custom_app(workspace)").
  // We mutate the buffer here to verify that the AT_EXECFN auxiliary vector
  // mechanism successfully finds the real binary on disk anyway.
  ASSERT_NE(g_original_argv0_buffer, nullptr);
  std::string original = g_original_argv0_buffer;
  size_t len = original.size();

  ASSERT_GT(len, 0);

  absl::Cleanup restore = [&] {
    memcpy(g_original_argv0_buffer, original.data(), len + 1);
    testing::testvalue::Clear("elf_reader_proc_self_exe");
  };

  // Overwrite with a non-existent name so /proc/self/cmdline cannot find it.
  // Note: If original argv[0] is shorter than our mock string, it will be
  // safely truncated. As long as the resulting path doesn't exist on disk, the
  // test works.
  strncpy(g_original_argv0_buffer, "123_name_not_on_disk", len);
  g_original_argv0_buffer[len - 1] = '\0';

  util::ElfReader reader(nonexistent_path);
  EXPECT_TRUE(reader.IsNativeElfFile());
}

TEST(ElfReaderSandboxTest, ProcSelfAllFallbackFails) {
  testing::testvalue::Enable();
  const std::string nonexistent_path = "/no/such/file";
  testing::testvalue::Force("elf_reader_proc_self_exe", nonexistent_path);

  // 1. Auxv fallback fails (supplying nullptr to also test null safety).
  const char* null_execfn = nullptr;
  testing::testvalue::Force("elf_reader_auxv_execfn", null_execfn);

  // 2. GetArgv0 fallback fails (already empty from main()).
  // 3. Cmdline fallback fails by mutating buffer to non-existent path.
  ASSERT_NE(g_original_argv0_buffer, nullptr);
  std::string original = g_original_argv0_buffer;
  size_t len = original.size();
  ASSERT_GT(len, 0);

  absl::Cleanup restore = [&] {
    memcpy(g_original_argv0_buffer, original.data(), len + 1);
    testing::testvalue::Clear("elf_reader_proc_self_exe");
    testing::testvalue::Clear("elf_reader_auxv_execfn");
  };

  strncpy(g_original_argv0_buffer, "123_name_not_on_disk", len);
  g_original_argv0_buffer[len - 1] = '\0';

  util::ElfReader reader(nonexistent_path);
  EXPECT_FALSE(reader.IsNativeElfFile());
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 0) {
    g_original_argv0_buffer = argv[0];
  }

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
