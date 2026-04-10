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

#include "gloop/base/init_google.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <memory>
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "gloop/base/config.h"
#include "gloop/base/internal/init_google.h"
#include "gtest/gtest.h"

#ifdef BASE_HAVE_PROCESS_STATE

#include "gloop/base/process_state.h"

namespace {

using ::base::internal::GetKernelVersionIfValid;
using ::base::internal::KernelVersionInfo;
using ::base::internal::ParseKernelVersionString;
using ::base::internal::ReadAndParseKernelVersionString;

KernelVersion ConstructKernelVersion(int major, int minor, int micro, int patch,
                                     int revision) {
  KernelVersion kv;
  kv.major = major;
  kv.minor = minor;
  kv.micro = micro;
  kv.patch = patch;
  kv.revision = revision;
  return kv;
}

bool EqualKernelVersion(const KernelVersion& x, const KernelVersion& y) {
  return ((x.major == y.major) && (x.minor == y.minor) &&
          (x.micro == y.micro) && (x.patch == y.patch) &&
          (x.revision == y.revision));
}

std::string ToString(const KernelVersion& kv) {
  return absl::StrCat("{", kv.major, ", ", kv.minor, ", ", kv.micro, ", ",
                      kv.patch, ", ", kv.revision, "}");
}

}  // namespace

#endif

#if GOOGLE_HAVE_MLOCK
TEST(InitGoogleMlockTests, MlockStyles) {
  using ::base::internal::IsValidMlockStyle;
  EXPECT_TRUE(IsValidMlockStyle(""));
  EXPECT_TRUE(IsValidMlockStyle("all"));
  EXPECT_TRUE(IsValidMlockStyle("executable"));
  EXPECT_TRUE(IsValidMlockStyle("none"));
  EXPECT_TRUE(IsValidMlockStyle("startup"));

  EXPECT_FALSE(IsValidMlockStyle("current"));
  EXPECT_FALSE(IsValidMlockStyle("everything"));
  EXPECT_FALSE(IsValidMlockStyle("future"));
  EXPECT_FALSE(IsValidMlockStyle("nothing"));
}
#endif

#if GTEST_HAS_DEATH_TEST
// This test verifies that InitGoogle crashes if called multiple times,
// instead of deadlocking as it used to do.
TEST(InitGoogleTestDeathTest, DoubleInitializationCrashes) {
  int argc = 1;
  char* argv0 = const_cast<char*>("fake_argv0");
  char** argv = &argv0;

  // gunit already called InitGoogle once, so this is the second call; make
  // sure it crashes.
  ASSERT_DEATH(
      { InitGoogle(argv[0], &argc, &argv, false); },
      "Check failed: previous_init_google_state == BEFORE_INIT_GOOGLE "
      "\\(2 vs. 0\\)");
}
#endif

#ifdef __linux__
TEST(InitGoogle, ParseKernelVersionString) {
  KernelVersion kv;

  EXPECT_TRUE(ParseKernelVersionString("Linux 1.2.3  #4", &kv));
  EXPECT_TRUE(EqualKernelVersion(kv, ConstructKernelVersion(1, 2, 3, 4, 0)));

  EXPECT_TRUE(ParseKernelVersionString("Linux 1.2.3-foobar #4", &kv));
  EXPECT_TRUE(EqualKernelVersion(kv, ConstructKernelVersion(1, 2, 3, 4, 0)));

  EXPECT_TRUE(ParseKernelVersionString("Linux 1.2-foobar  #4", &kv));
  EXPECT_TRUE(EqualKernelVersion(kv, ConstructKernelVersion(1, 2, 0, 4, 0)));

  EXPECT_TRUE(ParseKernelVersionString("Linux 1.2.3-foobar  #0", &kv));
  EXPECT_TRUE(EqualKernelVersion(kv, ConstructKernelVersion(1, 2, 3, 0, 0)));

  EXPECT_TRUE(ParseKernelVersionString("Linux 1.2.3-foobar  #0", &kv));
  EXPECT_TRUE(EqualKernelVersion(kv, ConstructKernelVersion(1, 2, 3, 0, 0)));

  EXPECT_TRUE(ParseKernelVersionString("Linux inae1 2.4.18-smp #40cap6", &kv));
  EXPECT_TRUE(EqualKernelVersion(kv, ConstructKernelVersion(2, 4, 18, 40, 0)));

  EXPECT_TRUE(ParseKernelVersionString("Linux inae1 2.4.18-smp #40.6", &kv));
  EXPECT_TRUE(EqualKernelVersion(kv, ConstructKernelVersion(2, 4, 18, 40, 6)));

  EXPECT_TRUE(ParseKernelVersionString(
      "Linux version 2.4.18-smp-175.13"
      "(recipient@example.com) (gcc version 3.3.3 20040201 (prerelease)) "
      "#175.13 SMP Thu Aug 2512:57:08 PDT 2005",
      &kv));
  EXPECT_TRUE(
      EqualKernelVersion(kv, ConstructKernelVersion(2, 4, 18, 175, 13)));

  EXPECT_TRUE(ParseKernelVersionString(
      "Linux version 2.6.11-netboot-200.0_rc25 (recipient@example.com) (gcc "
      "version 2.95.3 20010315 "
      "(release)) #1 [2261870] SMP Thu Mar 23 14:40:48 PST 2006",
      &kv));
  EXPECT_TRUE(EqualKernelVersion(kv, ConstructKernelVersion(2, 6, 11, 1, 0)));

  EXPECT_FALSE(ParseKernelVersionString("Linux 1.2.3-foobar #junk", &kv));
  EXPECT_FALSE(ParseKernelVersionString("Linux -1.2.3-foobar", &kv));
  EXPECT_FALSE(ParseKernelVersionString("Linux 1.-2.3-foobar", &kv));
  EXPECT_FALSE(ParseKernelVersionString("Linux 1.-foobar", &kv));
  EXPECT_FALSE(ParseKernelVersionString("Linux 1-foobar", &kv));
}

TEST(InitGoogle, ReadAndParseKernelVersionString) {
  const std::string kernel_version_filename =
      absl::StrCat(testing::TempDir(), "proc_version");
  FILE* file = fopen(kernel_version_filename.c_str(), "w");
  ASSERT_TRUE(file != nullptr) << "Failed to open " << kernel_version_filename
                               << " for writing: " << strerror(errno);
  const char kKernelVersionString[] =
      "Linux version 2.4.18-smp-175.13 "
      "(recipient@example.com) (gcc "
      "version 3.3.3 20040201 (prerelease)) "
      "#175.13 SMP Thu Aug 2512:57:08 PDT 2005\n";
  ASSERT_EQ(fwrite(kKernelVersionString, 1,
                   ABSL_ARRAYSIZE(kKernelVersionString), file),
            ABSL_ARRAYSIZE(kKernelVersionString))
      << strerror(errno);
  ASSERT_EQ(fclose(file), 0) << strerror(errno);

  std::unique_ptr<KernelVersionInfo> info(
      ReadAndParseKernelVersionString(kernel_version_filename.c_str()));
  EXPECT_EQ(info->raw_version_string, kKernelVersionString);
  const KernelVersion kExpectedKernelVersion = {2, 4, 18, 175, 13};
  EXPECT_TRUE(EqualKernelVersion(info->parsed_version, kExpectedKernelVersion));
}

TEST(InitGoogle, GetKernelVersionIfValid) {
  KernelVersionInfo input_info;
  const KernelVersion kInputVersion = {2, 4, 6, 8, 10};
  input_info.parsed_version = kInputVersion;
  KernelVersion output_version;
  ASSERT_TRUE(GetKernelVersionIfValid(input_info, &output_version));
  EXPECT_TRUE(EqualKernelVersion(output_version, kInputVersion))
      << "got: " << ToString(output_version)
      << " expected: " << ToString(kInputVersion);
}

TEST(InitGoogle, DontGetKernelVersionIfInvalid) {
  const KernelVersionInfo input_info;
  const KernelVersion kInputVersion = {1, 3, 5, 7, 9};
  KernelVersion output_version = kInputVersion;
  ASSERT_FALSE(GetKernelVersionIfValid(input_info, &output_version));
  EXPECT_TRUE(EqualKernelVersion(output_version, kInputVersion))
      << "got: " << ToString(output_version)
      << " expected: " << ToString(KernelVersionInfo::kDefaultKernelVersion);
}
#endif
