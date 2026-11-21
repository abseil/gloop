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

#include "gloop/base/percpu_test_util.h"

#include "gloop/base/percpu.h"

#if PERCPU_USE_RSEQ

#include <syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>

#include "absl/base/internal/raw_logging.h"
#include "tcmalloc/internal/linux_syscall_support.h"

namespace base {
namespace subtle {
namespace percpu {

namespace {

bool ThreadRegistered() { return RseqCpuId() >= kCpuIdInitialized; }

int UnregisterRseq() {
  constexpr uint32_t kRseqFlagUnregister = 1 << 0;
  if (syscall(__NR_rseq, &__rseq_abi, sizeof(__rseq_abi), kRseqFlagUnregister,
              PERCPU_RSEQ_SIGNATURE)) {
    return errno;
  }

  // Assumption check: we should now show up as unregistered again.
  ABSL_RAW_DCHECK(!ThreadRegistered(), "CPU ID not uninitialized");

  return 0;
}

}  // namespace

void UnregisterThread() {
  // If this thread is already not registered, there's nothing for us to do.
  if (!ThreadRegistered()) {
    return;
  }

  // Unregister our struct from the kernel.
  if (const int err = UnregisterRseq(); err != 0) {
#ifndef NDEBUG
    ABSL_RAW_LOG(FATAL, "Failed to unregister. errno: %d", err);
#endif
    return;
  }
}

}  // namespace percpu
}  // namespace subtle
}  // namespace base

#endif  // PERCPU_USE_RSEQ
