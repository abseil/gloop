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

#include "gloop/base/futex.h"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <ctime>

#include "absl/base/internal/raw_logging.h"
#include "gloop/base/inlined_syscall.h"

namespace base {

#ifndef __linux__
int Futex::Swap(std::atomic<int32_t>* v1, int32_t val,
                const struct timespec* reltime, std::atomic<int32_t>* v2) {
  ABSL_RAW_LOG(FATAL, "Do not invoke futexes on non Linux");
  return 0;
}
#else

namespace {

int futex_wake_wait(std::atomic<int32_t>* v1, int32_t val,
                    const struct timespec* reltime, std::atomic<int32_t>* v2) {
  Futex::Wake(v2, 1);
  return Futex::WaitRelativeTimeout(v1, val, reltime);
}

// Which way should Futex::Swap operate?
enum FutexSwapMode {
  kUnknown,   // Not yet determined.
  kWakeWait,  // Use FUTEX_WAKE + FUTEX_WAIT pair of syscalls.
};

std::atomic<FutexSwapMode> futex_swap_mode{kWakeWait};

}  // namespace

int Futex::Swap(std::atomic<int32_t>* v1, int32_t val,
                const struct timespec* reltime, std::atomic<int32_t>* v2) {
  FutexSwapMode mode = futex_swap_mode.load(std::memory_order_relaxed);

  switch (mode) {
    case kWakeWait:
      return futex_wake_wait(v1, val, reltime, v2);
    default:
      ABSL_RAW_LOG(FATAL, "Unexpected futex swap mode");
  }

  // Unreachable.
  return -1;
}

#endif
}  // namespace base
