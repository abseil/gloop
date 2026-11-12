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

#include "gloop/util/time/backoff_test_util.h"

#include <utility>

#include "absl/time/time.h"
#include "gloop/util/time/backoff.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Not;

namespace util_time {

TEST(BackoffTestUtil, Test) {
  const absl::Duration ns = absl::Nanoseconds(1);
  const absl::Duration min_delay = 100 * ns;
  const absl::Duration max_delay = 1000 * ns;
  const double backoff_base = 1.3;
  absl::Duration d;
  for (int retries = 0; retries < 10; ++retries) {
    const auto range =
        BackoffRange(min_delay, max_delay, backoff_base, retries);
    for (int i = 0; i < 1000; ++i) {
      d = ComputeBackoff(min_delay, max_delay, backoff_base, retries);
      ASSERT_GE(d, range.first);
      ASSERT_LE(d, range.second);
      ASSERT_TRUE(
          IsExpectedBackoff(d, min_delay, max_delay, backoff_base, retries));

      // Exercise the predicate failure path too.
      ASSERT_FALSE(IsExpectedBackoff(range.first - absl::Nanoseconds(1),
                                     min_delay, max_delay, backoff_base,
                                     retries));
      ASSERT_FALSE(IsExpectedBackoff(range.second + absl::Nanoseconds(1),
                                     min_delay, max_delay, backoff_base,
                                     retries));

      // Test the GMock matcher.
      ASSERT_THAT(d, DurationIsExpectedBackoff(min_delay, max_delay,
                                               backoff_base, retries));
      ASSERT_THAT(range.first - absl::Nanoseconds(1),
                  Not(DurationIsExpectedBackoff(min_delay, max_delay,
                                                backoff_base, retries)));
      ASSERT_THAT(range.second + absl::Nanoseconds(1),
                  Not(DurationIsExpectedBackoff(min_delay, max_delay,
                                                backoff_base, retries)));
    }
  }
}

}  // namespace util_time
