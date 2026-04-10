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

#ifndef THIRD_PARTY_GLOOP_THREAD_FIBER_SLEEP_H_
#define THIRD_PARTY_GLOOP_THREAD_FIBER_SLEEP_H_

#include "absl/base/macros.h"
#include "absl/time/clock_interface.h"
#include "absl/time/time.h"

namespace thread {

// Sleeps until the specified time or until the calling thread is cancelled.
// Returns true iff sleep expired without a cancellation.
bool CancellableSleepUntil(absl::Time t);

// Sleeps for the specified duration, unless the calling thread
// is cancelled. Returns true iff sleep expired without a cancellation.
bool CancellableSleepFor(absl::Duration d);

// As above, but with a Clock. See SelectUntil for a discussion
// of the clock parameter (the handling is identical).
bool CancellableSleepUntil(absl::Clock* clock, absl::Time t);
bool CancellableSleepFor(absl::Clock* clock, absl::Duration d);

ABSL_DEPRECATE_AND_INLINE()
inline bool SleepUnlessCancelled(absl::Duration d) {
  return thread::CancellableSleepFor(d);
}

ABSL_DEPRECATE_AND_INLINE()
inline bool SleepUnlessCancelled(absl::Clock* clock, absl::Duration d) {
  return thread::CancellableSleepFor(clock, d);
}

}  // namespace thread

#endif  // THIRD_PARTY_GLOOP_THREAD_FIBER_SLEEP_H_
