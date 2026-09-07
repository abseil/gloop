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

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "absl/strings/str_cat.h"
#include "fuzztest/fuzztest.h"
#include "gloop/util/symbolize/elf_reader.h"
#include "gloop/util/symbolize/symbolize.h"

namespace util {
namespace {

void FuzzElfReader(const std::string& data) {
  const char* tmp = getenv("TMP");
  if (tmp == nullptr) tmp = "/tmp";

  std::string path = absl::StrCat(tmp, "/fuzz_elf_reader.", getpid(), ".elf");

  int fd = open(path.c_str(), O_WRONLY | O_CREAT, 0770);
  if (fd == -1) {
    fprintf(stderr, "open %s: %s\n", path.c_str(), strerror(errno));
    return;
  }
  if (write(fd, data.data(), data.size()) != data.size()) {
    fprintf(stderr, "write %s %zu: %s\n", path.c_str(), data.size(),
            strerror(errno));
  }
  close(fd);

  ElfReader reader(path);
  std::unique_ptr<SymbolMap> symbols(
      SymbolMap::GetEmpty(/*compression_level=*/0));
  reader.AddSymbols(symbols.get(), 0, 0, data.size());
  unlink(path.c_str());
}
FUZZ_TEST(ElfReaderFuzz, FuzzElfReader);  // NOLINT(whitespace/semicolon)

}  // namespace
}  // namespace util
