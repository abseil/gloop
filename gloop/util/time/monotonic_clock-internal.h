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

#ifndef THIRD_PARTY_GLOOP_UTIL_TIME_MONOTONIC_CLOCK_INTERNAL_H_
#define THIRD_PARTY_GLOOP_UTIL_TIME_MONOTONIC_CLOCK_INTERNAL_H_

#include "absl/time/clock_interface.h"

namespace util {
class MonotonicClock;
}  // namespace util
// Test interface only -- do not #include this file!

namespace util {
namespace monotonic_clock_internal {

// Reset internal global state.  Should only be called by test code.
void SynchronizedMonotonicClockReset();

// Create and delete a State object.
struct State;

State* CreateMonotonicClockState(absl::Clock* raw_clock);
void DeleteMonotonicClockState(State* state);
// Create a monotonic clock based on the given state.  Caller still owns state
// so that multiple such clocks can be created from the same state.
MonotonicClock* CreateMonotonicClock(State* state);

}  // namespace monotonic_clock_internal
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TIME_MONOTONIC_CLOCK_INTERNAL_H_
