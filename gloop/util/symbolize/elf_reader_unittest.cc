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

#include "gloop/util/symbolize/elf_reader.h"

#include <elf.h>
#include <endian.h>
#include <stdlib.h>
#include <sys/mman.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/log_severity.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/flags.h"
#include "absl/log/log.h"
#include "absl/log/scoped_mock_log.h"
#include "absl/status/status.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "gloop/base/proc_maps.h"
#include "gloop/base/strerror.h"
#include "gloop/util/symbolize/symbolize-inl.h"
#include "gloop/util/symbolize/symbolize.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using absl::ScopedMockLog;
using testing::_;
using testing::ContainerEq;
using testing::HasSubstr;
using testing::UnorderedElementsAre;
using util::ElfReader;
using util::SymbolMap;

ABSL_FLAG(std::string, input, "",
          "Dump symbols in the input file, if it's not empty.");
ABSL_DECLARE_FLAG(bool, elfreader_process_dynsyms);

namespace {
// Flags to select a particular flavor of ELF binary.
enum ElfBinaryFlavor {
  ELF32 = 1 << 0,
  ELF64 = 1 << 1,
  RDYNAMIC = 1 << 2,
  STRIPPED = 1 << 3,
  HAS_DEBUG_INFO = 1 << 4,
  MANY_SECTIONS = 1 << 5,
  PPC = 1 << 6,
  SPLIT_TEXT_SECTIONS = 1 << 7,
};

// Checks if the file name is likely an executable of the running program.
// Not very rigorous but good enough here.
bool FileNameIsLikelySelfExec(const char* file_name) {
  return strstr(file_name, "elf_reader_unittest") != nullptr;
}

// Checks if the file name is likely a dynamic shared object (DSO).
// Not very rigorous but good enough here.
bool FileNameIsLikelyDSO(const char* file_name) {
  int file_name_length = strlen(file_name);
  // Check if the file name ends with .so.
  return (file_name_length >= 3 &&
          strcmp(file_name + file_name_length - 3, ".so") == 0);
}

TEST(ElfReader, IsNativeElfFile) {
  // Test a text file and a file that doesn't exist. These should not
  // be valid ELF files.
  EXPECT_FALSE(ElfReader("/proc/cpuinfo").IsNativeElfFile());
  EXPECT_FALSE(ElfReader("/some_garbage_file").IsNativeElfFile());

  // This unittest has to be a native ELF file, or it couldn't be
  // running.
  EXPECT_TRUE(ElfReader("/proc/self/exe").IsNativeElfFile());

  // Iterate over all of the code maps loaded into this process. They
  // should all be native as well.
  ProcMapsIterator it(0);
  if (it.Valid()) {
    uint64_t begin, end, pos;
    char *file_name, *flags;
    while (it.Next(&begin, &end, &flags, &pos, nullptr, &file_name)) {
      // Not our problem, since we handle these gracefully, but might
      // as well check that the iterator still returns these as
      // valid.
      EXPECT_NE(flags[0], '\0');
      EXPECT_NE(flags[1], '\0');
      EXPECT_NE(file_name, nullptr);

      // Collect only executable maps, and exclude maps like "[heap]",
      // which don't correspond to files.  Sometimes files like
      // /var/db/nscd/passwd are mapped with an executable flag for
      // some reason (this happened on goobuntu dapper for x86_64),
      // hence we only collect maps with file names likely to be an
      // executable or dynamic shared object.
      if ((flags[2] == 'x' && file_name[0] != '\0' && file_name[0] != '[') &&
          (FileNameIsLikelySelfExec(file_name) ||
           FileNameIsLikelyDSO(file_name))) {
        ElfReader reader(file_name);
        EXPECT_TRUE(reader.IsNativeElfFile());
      }
    }
  }
}

TEST(ElfReader, NonexistentFile) {
  ScopedMockLog log;
  std::string nonexistent_file = "this_file_does_not_exist";
  EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, _,
                       HasSubstr("Could not open " + nonexistent_file)));
  log.StartCapturingLogs();
  ElfReader reader(nonexistent_file);  // Generates warning after open fails.
}

TEST(ElfReader, IgnoreVdso) {
  ScopedMockLog log;
  EXPECT_CALL(log, Log(_, _, _)).Times(0);  // No log messages expected.
  log.StartCapturingLogs();
  ElfReader reader("[vdso]");
}

TEST(ElfReader, IgnoreVsyscall) {
  ScopedMockLog log;
  EXPECT_CALL(log, Log(_, _, _)).Times(0);  // No log messages expected.
  log.StartCapturingLogs();
  ElfReader reader("[vsyscall]");
}

}  // namespace

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);

  // Dumps symbols and exits if FLAGS_input is not empty.
  if (!absl::GetFlag(FLAGS_input).empty()) {
    ElfReader reader(absl::GetFlag(FLAGS_input));
    std::unique_ptr<SymbolMap> symbol_map(util::SymbolMap::GetEmpty());
    reader.AddSymbols(symbol_map.get(), 0, 0, 0);

    std::unique_ptr<SymbolMap::Iterator> iter(symbol_map->GetIterator());
    while (!iter->done()) {
      absl::PrintF("0x%08x 0x%08x %s\n", iter->start(), iter->size(),
                   iter->name());
      iter->Next();
    }
    return 0;
  }
  return RUN_ALL_TESTS();
}
