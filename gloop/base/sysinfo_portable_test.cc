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

#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/sysinfo.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>  // for TARGET_OS_* defines
#endif                           // __APPLE__

namespace {

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::Lt;

TEST(Sysinfo, ProcessName) {
#if defined(__APPLE__)
#if TARGET_OS_OSX
  // Accept either a name based on this binary name, or a PSHA2 text digest (a
  // long sequence of hex digits with '_' near the end).  When running on
  // Forge-on-Mac, the process name ends up being a digest, because the
  // ProcessName() implementation resolves symlinks to the executable's real
  // file name (unlike Linux). The name is truncated to 31 characters for the
  // user's own processes or 15 characters for other users' processes.
  using ::testing::AnyOf;
  using ::testing::MatchesRegex;
  EXPECT_THAT(ProcessName(0), AnyOf(Eq("sysinfo_portable_test"),
                                    MatchesRegex("[0-9a-fA-F_]{31}")));

  EXPECT_THAT(ProcessName(1), Eq("launchd"));
#else
  // ProcessName is a no-op stub on other Apple targets (iOS and friends).
  EXPECT_THAT(ProcessName(0), Eq(""));
#endif  // TARGET_OS_OSX
#elif defined(_WIN32)
  EXPECT_THAT(ProcessName(0), HasSubstr("sysinfo_portable_test"));
#elif defined(__Fuchsia__)
  // Fuchsia doesn't have a concept of pid, but can get the current process
  // name; therefore nonzero pids are invalid.
  EXPECT_THAT(ProcessName(0), HasSubstr("sysinfo_portabl"));
  EXPECT_THAT(ProcessName(1), Eq(""));
#else
  // On most platforms we verify that the process name is the first
  // 15 characters of the test name.
  EXPECT_THAT(ProcessName(0), HasSubstr("sysinfo_portabl"));
#endif

  EXPECT_THAT(ProcessName(999999999), Eq(""));
}

TEST(Sysinfo, ProcessStartTime) {
#if defined(__arm__)
  // ProcessStartTime gives unusual results - O(hours ago)
#elif defined(__linux__) || TARGET_OS_OSX
  // Make sure the process start time is in a reasonable range (this will fail
  // for absl::InfinitePast() or the Unix epoch).
  auto process_age = absl::Now() - base::ProcessStartTime();
  EXPECT_THAT(process_age,
              AllOf(Lt(absl::Minutes(5)), Ge(absl::ZeroDuration())));
#endif
}

TEST(Sysinfo, CPUUsage) {
#if defined(__linux__) || defined(__APPLE__)
  auto process_cpu_usage = base::CPUUsage();
  EXPECT_THAT(process_cpu_usage, Gt(absl::ZeroDuration()));
#endif
}

}  // namespace
