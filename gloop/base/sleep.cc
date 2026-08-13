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

// For google3 production Linux, provide an override of the weak
// symbol AbslInternalSleepFor() from
// https://github.com/abseil/abseil-cpp/tree/master/absl/base/sleep.cc.

#include <errno.h>
#include <stdint.h>
#include <time.h>

#include <algorithm>
#include <atomic>
#include <limits>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/time/time.h"
#include "gloop/base/per-thread-sem.h"
#include "gloop/base/scheduling/domain.h"

extern "C" {

// This version is scheduling-aware, and cooperates with fibers.
// TODO [[maybe_unused]] is to suppress scythe.
[[maybe_unused]] void ABSL_INTERNAL_C_SYMBOL(AbslInternalSleepFor)(
    absl::Duration duration) {
  std::atomic<int>* blocked_count_ptr =
      absl::synchronization_internal::PerThreadSem::GetThreadBlockedCounter();
  if (blocked_count_ptr != nullptr) {
    // Mark this thread as blocked (for benefit of auto-sizing threadpools).
    blocked_count_ptr->fetch_add(1, std::memory_order_relaxed);
  }
  {
    base::scheduling::ConditionalPotentiallyBlockingRegion blocking(
        duration >= absl::Microseconds(5));  // Arbitrary, small.
    // Each call to nanosleep() can only sleep for the number of
    // seconds that can fit into a time_t.
    constexpr absl::Duration kMaxSleep =
        absl::Seconds(int64_t{std::numeric_limits<time_t>::max()});
    while (duration > absl::ZeroDuration()) {
      absl::Duration to_sleep = std::min(duration, kMaxSleep);
      struct timespec sleep_time = absl::ToTimespec(to_sleep);
      while (nanosleep(&sleep_time, &sleep_time) != 0 && errno == EINTR) {
        // Ignore signals and wait for the full interval to elapse.
      }
      duration -= to_sleep;
    }
  }
  if (blocked_count_ptr != nullptr) {
    blocked_count_ptr->fetch_sub(1, std::memory_order_relaxed);
  }
}

}  // extern "C"
