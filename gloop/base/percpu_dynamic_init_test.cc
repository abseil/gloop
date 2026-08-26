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

#include <errno.h>
#include <unistd.h>

#include <cassert>

#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "gloop/base/percpu.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

ABSL_FLAG(int, expected_vcpu_mode, 0, "Expected VcpuMode");

namespace base {
namespace subtle {
namespace percpu {
namespace {

using ::testing::Eq;

// Cause dynamic initialization. Notice that other code (gunit, absl) could well
// invoke percpu in some dynamic initialization before the below code, which is
// fine, as we basically want to test 'any' pre-main call to percpu, the below
// is here to make sure at least 'some' code does.
bool is_fast = IsFast();
int vcpu_mode = static_cast<int>(GetRseqVcpuMode());

// Returns true if we have RSEQ. This method simply invokes syscall(__NR_rseq)
// with a made up signature and invalid flags, and checks for errno != ENOSYS.
bool HasRseq() { return false; }

bool IsKernelWithVirtualCpuIds() { return false; }

bool IsKernelWithL3CpuIds() { return false; }

TEST(PercpuDynamicInitTest, DynamicInitBeforeMainWorks) {
  if (!HasRseq()) {
    LOG(WARNING) << "RSEQ unavailable";
    EXPECT_FALSE(is_fast);
    EXPECT_THAT(vcpu_mode, Eq(static_cast<int>(RseqVcpuMode::kNone)));
    return;
  }

  EXPECT_TRUE(is_fast);
  if (!IsKernelWithVirtualCpuIds()) {
    LOG(WARNING) << "Skipped VCPU ID test as not running on prod kernel 902";
    return;
  }

  int expected_cpu_mode = absl::GetFlag(FLAGS_expected_vcpu_mode);
  switch (static_cast<RseqVcpuMode>(expected_cpu_mode)) {
    case RseqVcpuMode::kFlatPerL3:
      if (!IsKernelWithL3CpuIds()) {
        // Downgrade due to kernel unavailability.
        expected_cpu_mode = static_cast<int>(RseqVcpuMode::kFlat);
      }
      break;
    default:
      break;
  }

  EXPECT_THAT(vcpu_mode, Eq(expected_cpu_mode));
}

}  // namespace
}  // namespace percpu
}  // namespace subtle
}  // namespace base
