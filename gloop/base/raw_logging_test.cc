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

// This test serves primarily as a compilation test for base/raw_logging.h.
// Raw logging testing is covered by logging_unittest.cc, which is not as
// portable as this test.

#include "gloop/base/raw_logging.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>

#include "absl/base/config.h"  // IWYU pragma: keep
#include "gtest/gtest.h"

namespace {

TEST(RawLoggingCompilationTest, Log) {
  ABSL_RAW_LOG(INFO, "RAW INFO: %d", 1);
  ABSL_RAW_LOG(ERROR, "RAW ERROR: %d", 1);
}

TEST(RawLoggingCompilationTest, LogTruncated) {
  char very_long[3000];
  char* cursor = very_long;
  char* end = very_long + sizeof(very_long);
  static constexpr char kLine[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz"
      "0123456789\n";
  size_t len = sizeof(very_long) - 1;
  very_long[len] = '\0';
  while (cursor < end) {
    strncpy(cursor, kLine, len);
    len -= sizeof(kLine) - 1;
    cursor += sizeof(kLine) - 1;
  }

  ABSL_RAW_LOG(INFO, "RAW INFO: %s", very_long);
}

TEST(RawLoggingCompilationTest, PassingCheck) {
  ABSL_RAW_CHECK(true, "RAW CHECK");
}

TEST(RawLoggingCompilationTest, DebugLog) {
  ABSL_RAW_DLOG(INFO, "RAW DLOG: %d", 1);
}

TEST(RawLoggingCompilationTest, PassingDebugCheck) {
  ABSL_RAW_DCHECK(true, "failure message");
}

#if !defined(NDEBUG)  // if debug build
TEST(RawLoggingDeathTest, FailingDebugCheck) {
  EXPECT_DEATH_IF_SUPPORTED(ABSL_RAW_DCHECK(1 == 0, "explanation"),
                            "explanation");
}
#endif  // if debug build

TEST(RawLoggingDeathTest, FailingCheck) {
  EXPECT_DEATH_IF_SUPPORTED(ABSL_RAW_CHECK(1 == 0, "explanation"),
                            "explanation");
}

TEST(RawLoggingDeathTest, LogFatal) {
  EXPECT_DEATH_IF_SUPPORTED(ABSL_RAW_LOG(FATAL, "my dog has fleas"),
                            "my dog has fleas");
}

}  // namespace
