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

// For google3 production Linux, provide overrides of the weak
// symbol definitions in
// https://github.com/abseil/abseil-cpp/tree/master/absl/base/internal/spinlock_wait.cc.
//
// These versions are scheduling-aware, and cooperate with fibers.

#include "absl/base/internal/spinlock_wait.h"

#include <errno.h>
#include <limits.h>
#include <linux/futex.h>
#include <stdint.h>
#include <syscall.h>
#include <time.h>
#include <unistd.h>

#include <atomic>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/flags/flag.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "gloop/base/scheduling/domain.h"
#include "gloop/base/scheduling/scheduling_mode.h"

// The SpinLock lockword is `std::atomic<uint32_t>`. Here we assert that
// `std::atomic<uint32_t>` is bitwise equivalent of the `int` expected
// by SYS_futex. We also assume that reads/writes done to the lockword
// by SYS_futex have rational semantics with regard to the
// std::atomic<> API. C++ provides no guarantees of these assumptions,
// but they are believed to hold in practice.
static_assert(sizeof(std::atomic<uint32_t>) == sizeof(int),
              "SpinLock lockword has the wrong size for a futex");

ABSL_FLAG(absl::Duration, experimental_spinlock_wait_max_delay, {},
          "DEPRECATED");

extern "C" {

// TODO [[maybe_unused]] is to suppress scythe.
[[maybe_unused]] void ABSL_INTERNAL_C_SYMBOL(AbslInternalSpinLockDelay)(
    std::atomic<uint32_t>* w, uint32_t value, int loop,
    absl::base_internal::SchedulingMode scheduling_mode) {
  int save_errno = errno;
  base::scheduling::ConditionalPotentiallyBlockingRegion region(
      scheduling_mode == absl::base_internal::SCHEDULE_COOPERATIVE_AND_KERNEL,
      true);
  syscall(SYS_futex, w, FUTEX_WAIT_PRIVATE, value, nullptr);
  errno = save_errno;
}

[[maybe_unused]] void ABSL_INTERNAL_C_SYMBOL(AbslInternalSpinLockWake)(
    std::atomic<uint32_t>* w, bool all) {
  syscall(SYS_futex, w, FUTEX_WAKE_PRIVATE, all ? INT_MAX : 1, 0);
}

}  // extern "C"
