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

#include "gloop/base/internal/temp_directories.h"

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <cstdlib>
#include <iterator>
#include <string>
#include <vector>

#include "absl/algorithm/container.h"

namespace base {
namespace internal {

std::vector<std::string> TempDirectories() {
  std::vector<std::string> dirs;
#ifdef _WIN32
  // On windows we'll try to find a directory in this order:
  //   C:/Documents & Settings/whomever/TEMP (or whatever GetTempPath() is)
  //   C:/TMP/
  //   C:/TEMP/
  //   .
  char tmp[MAX_PATH];
  // GetTempPath can fail with either 0 or with a space requirement > bufsize.
  // See http://msdn.microsoft.com/en-us/library/aa364992(v=vs.85).aspx
  uint32_t n = GetTempPathA(MAX_PATH, tmp);
  if (n > 0 && n <= MAX_PATH) dirs.push_back(tmp);
  dirs.push_back("C:\\tmp\\");
  dirs.push_back("C:\\temp\\");
#else
  // Directories, in order of preference. If we find a dir that
  // exists, we stop adding other less-preferred dirs
  const char* candidates[] = {
      // Non-null only during unittest/regtest
      getenv("TEST_TMPDIR"),

      // Explicitly-supplied temp dirs
      getenv("TMPDIR"),
      getenv("TMP"),

      // If all else fails
      "/tmp",
  };

  for (const char* d : candidates) {
    if (!d || d[0] == '\0') continue;  // Empty env var

    // Make sure we don't surprise anyone who's expecting a '/'
    std::string dstr = d;
    if (dstr[dstr.size() - 1] != '/') {
      dstr += '/';
    }
    dirs.push_back(dstr);
  }
#endif
  return dirs;
}

std::vector<std::string> ExistingTempDirectories() {
  std::vector<std::string> res;
#ifdef _WIN32
  auto access_mode = 0;
#else
  auto access_mode = F_OK;
#endif
  absl::c_copy_if(TempDirectories(), std::back_inserter(res),
                  [access_mode](const std::string& dir) {
                    return ::access(dir.c_str(), access_mode) == 0;
                  });
  return res;
}
}  // namespace internal
}  // namespace base
