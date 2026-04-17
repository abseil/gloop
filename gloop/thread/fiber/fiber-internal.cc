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

#include "gloop/thread/fiber/fiber-internal.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/flags/flag.h"
#include "absl/time/time.h"
#include "gloop/base/walltime.h"
#include "gloop/thread/timedcall.h"

ABSL_FLAG(int32_t, fibers_default_thread_stack_size,
          thread::internal::kDefaultFiberStackSize,
          "Set the size of the default stack for a Fiber");

namespace thread {
namespace internal {

#ifdef FIBERS_UNOPTIMIZED_BUILD
constexpr int32_t kDefaultFiberStackSize = 128 * 1024;
#else
constexpr int32_t kDefaultFiberStackSize = 64 * 1024;
#endif

// Weak definitions of OneShotAlarm in case eventmanager is not linked in.

/*static*/ ABSL_ATTRIBUTE_WEAK void OneShotAlarm::Create(
    void* buffer, absl::Time when, InvocableImpl invocable) {
  TimedCall* tc = new (buffer) TimedCall;
  // No need to schedule an alarm if the deadline is infinite, just initialize
  // the TimedCall so it can be destroyed later.
  if (when == absl::InfiniteFuture()) return;
  // Ensure conversion of `when` is a valid wall time not an internal constant.
  WallTime wall_time = std::max(base::ToWallTime(when), WallTime(1));
  tc->Set(wall_time, std::move(invocable));
}

/*static*/ ABSL_ATTRIBUTE_WEAK void OneShotAlarm::Destroy(void* alarm) {
  static_cast<TimedCall*>(alarm)->~TimedCall();
}

}  // namespace internal
}  // namespace thread
