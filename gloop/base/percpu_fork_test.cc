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

#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>

#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/percpu.h"
#include "gloop/base/raw_logging.h"
#include "gloop/thread/thread_control.h"
#include "gtest/gtest.h"
#include "tcmalloc/internal/linux_syscall_support.h"

namespace base {
namespace subtle {
namespace percpu {
namespace {

TEST(PerCpuTest, ForkSafe) {
  if (!IsFast()) {
    GTEST_SKIP() << "must have fast per-CPU";
  }

#if PERCPU_USE_RSEQ

  int offset = 0;

  switch (GetRseqVcpuMode()) {
    case RseqVcpuMode::kNone:
    case RseqVcpuMode::kFlat:
    case RseqVcpuMode::kFlatPerL3:
      offset = offsetof(kernel_rseq, vcpu_id);
      break;
    case RseqVcpuMode::kMM:
      offset = offsetof(kernel_rseq, mm_cid);
      break;
  }

  const int num_cpus = NumCPUs();

  ASSERT_GE(__rseq_abi.cpu_id, 0);
  ASSERT_LT(__rseq_abi.cpu_id, num_cpus);

  ASSERT_GE(*reinterpret_cast<volatile uint16_t*>(
                reinterpret_cast<volatile char*>(&__rseq_abi) + offset),
            0);
  ASSERT_LT(*reinterpret_cast<volatile uint16_t*>(
                reinterpret_cast<volatile char*>(&__rseq_abi) + offset),
            num_cpus);

  pid_t p = fork();
  ABSL_RAW_CHECK(p >= 0, "fork failed");

  if (p == 0) {
    // Child.  Do not allocate here, as incorrect CPU IDs may prevent TCMalloc
    // from working correctly.
    for (int i = 0; i < 1000; i++) {
      int cpu = __rseq_abi.cpu_id;
      if (cpu < 0) {
        ABSL_RAW_LOG(FATAL, "cpu (%d) < 0", cpu);
      } else if (cpu >= num_cpus) {
        ABSL_RAW_LOG(FATAL, "cpu (%d) > num_cpus (%d)", cpu, num_cpus);
      }

      int vcpu = *reinterpret_cast<volatile uint16_t*>(
          reinterpret_cast<volatile char*>(&__rseq_abi) + offset);

      if (vcpu < 0) {
        ABSL_RAW_LOG(FATAL, "vcpu (%d) < 0", vcpu);
      } else if (vcpu >= num_cpus) {
        ABSL_RAW_LOG(FATAL, "vcpu (%d) > num_cpus (%d)", vcpu, num_cpus);
      }

      __rseq_abi.cpu_id = -1;
      __rseq_abi.numa_node_id = -1;
      __rseq_abi.vcpu_id = -1;
      __rseq_abi.mm_cid = -1;
      absl::SleepFor(absl::Milliseconds(1));
    }

    _exit(0);
  }

  int status;
  ASSERT_GE(waitpid(p, &status, /* options = */ 0), 0);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
#else
  GTEST_SKIP() << "rseq not supported";
#endif  // PERCPU_USE_RSEQ
}

}  // namespace
}  // namespace percpu
}  // namespace subtle
}  // namespace base

int main(int argc, char** argv) {
  thread::DeprecatedThreadControl::AvoidBackgroundThreads();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
