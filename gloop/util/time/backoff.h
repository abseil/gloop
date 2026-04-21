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

// A library for computing backoff delay times.
//
// For testing code that uses this library, see backoff_test_util.h

#ifndef THIRD_PARTY_GLOOP_UTIL_TIME_BACKOFF_H_
#define THIRD_PARTY_GLOOP_UTIL_TIME_BACKOFF_H_

#include "absl/time/time.h"

namespace util_time {

// Return a duration for which an operation should back off, given the specified
// minimum and maximum delay for this operation, an exponential base and a count
// of how many times it has previously been retried.
//
// NOTE: This function uses an internally seeded random number generator to
// randomize backoff times slightly. This prevents clusters of requests starting
// at the same time from executing in lockstep.
[[nodiscard]]
absl::Duration ComputeBackoff(absl::Duration min_delay,
                              absl::Duration max_delay, double backoff_base,
                              int previous_retries);

// Convenience function that uses an exponential backoff base of 1.3.
[[nodiscard]]
absl::Duration ComputeBackoff(absl::Duration min_delay,
                              absl::Duration max_delay, int previous_retries);

}  // namespace util_time

#endif  // THIRD_PARTY_GLOOP_UTIL_TIME_BACKOFF_H_
