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

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "absl/strings/str_cat.h"
#include "file/base/path.h"
#include "gloop/util/symbolize/elf_reader.h"
#include "gloop/util/symbolize/symbolize.h"

void FuzzElfReader(const uint8_t* data, size_t size) {
  const char* tmp = getenv("TMP");
  if (tmp == nullptr) tmp = "/tmp";
  const std::string path =
      file::JoinPath(tmp, absl::StrCat("fuzz_elf_reader.", getpid(), ".elf"));
  int fd = open(path.c_str(), O_WRONLY | O_CREAT, 0770);
  if (fd == -1) {
    fprintf(stderr, "open %s: %s\n", path.c_str(), strerror(errno));
    return;
  }
  if (write(fd, data, size) != size) {
    fprintf(stderr, "write %s %zu: %s\n", path.c_str(), size, strerror(errno));
  }
  close(fd);

  util::ElfReader reader(path);
  std::unique_ptr<util::SymbolMap> symbols(
      util::SymbolMap::GetEmpty(/*compression_level=*/0));
  reader.AddSymbols(symbols.get(), 0, 0, size);
  unlink(path.c_str());
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzElfReader(data, size);
  return 0;
}
