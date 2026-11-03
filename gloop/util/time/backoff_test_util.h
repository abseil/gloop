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

// Test utilities for testing code that uses backoff.h.
//
// Testing your code that uses ComputeBackoff() can be difficult because it
// returns random values. Tests for your code should be able to verify that:
//   - retry counts were correctly incremented or reset
//   - correct min/max arguments were supplied to ComputeBackoff()
//   - if unit conversions were performed, there were no mistakes
//
// The test utilities below help to achieve that verification by providing
// the expected backoff range for specific inputs.
// These utilities have implementation knowledge of ComputeBackoff(), but your
// code should never make any assumptions about the implementation.
//
// For example, to test that a duration d used by your code under test could
// have been created by a call to ComputeBackoff with a minimum delay of 100 ms,
// a maximum delay of 1000 ms, and a previous retry count of three:
//
//   const auto expected_min = absl::Milliseconds(100);
//   const auto expected_max = absl::Milliseconds(1000);
//   ASSERT_TRUE(IsExpectedBackoff(d, expected_min, expected_max, 3));
//
// When the value or state being verified is derived from the return value of
// ComputeBackoff() in a more complex way, you can use the BackoffRange()
// function. It returns the range of values returned by ComputeBackoff() given
// the same arguments. For example:
//
//   std::pair<absl::Duration, absl::Duration> backoff_range =
//       BackoffRange(100, 1000, 3);
//   std::pair<absl::Duration, absl::Duration> expected_range(
//       YourTransformFromBackoffToValue(backoff_range.first),
//       YourTransformFromBackoffToValue(backoff_range.second));
//   ASSERT_LE(expected_range.first, val);
//   ASSERT_GE(val, expected_range.second);
//
// If you do not have direct access to the backoff value - for example, if the
// value is passed to a mock clock via Sleep() - you can use the
// DurationIsExpectedBackoff GMock matcher to test values.
//
//   util::MockClock clock;
//
//   // Mock Foo object; the class under test will invoke method Foo::DoStuff()
//   // and retry when DoStuff() fails.
//   MockFoo foo;
//
//   // Backoff parameters.
//   absl::Duration min_delay = absl::Seconds(10);
//   absl::Duration max_delay = absl::Minutes(5);
//
//   // Set expectations on the clock and the Foo service. After five failed
//   // attempts (and subsequent backoff), DoStuff() succeeds.
//   testing::InSequence seq;
//   for (int retries = 0; retries < 5; ++retries) {
//     EXPECT_CALL(foo, DoStuff()).WillOnce(testing::Return(false));
//     EXPECT_CALL(clock, Sleep(DurationIsExpectedBackoff(min_delay, max_delay,
//                                                        retries)));
//   }
//   EXPECT_CALL(foo, DoStuff()).WillOnce(testing::Return(true));
//
//   // Create the object under test, passing it the mock clock and the mock Foo
//   // service. Calling client.DoWork() causes the client to repeatedly invoke
//   // Foo::DoStuff(), backing off between calls until DoStuff() returns true.
//   FooClient client(&clock, &foo, min_delay, max_delay);
//   client.DoWork();

#ifndef THIRD_PARTY_GLOOP_UTIL_TIME_BACKOFF_TEST_UTIL_H_
#define THIRD_PARTY_GLOOP_UTIL_TIME_BACKOFF_TEST_UTIL_H_

#include <utility>

#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace util_time {

// A gUnit predicate function that can be used for most testing. If d is in the
// expected inclusive range, AssertionSuccess is returned.
//
// All values are logged for assertion failures whether in range or not.
//
// Args:
//   d: value being verified
//   min_delay: min value supplied to ComputeBackoff()
//   max_delay: max value supplied to ComputeBackoff()
//   backoff_base: exponential backoff supplied to ComputeBackoff()
//   retries: retries value supplied to ComputeBackoff()
// Returns:
//   AssertionSuccess or AssertionFailure
testing::AssertionResult IsExpectedBackoff(absl::Duration d,
                                           absl::Duration min_delay,
                                           absl::Duration max_delay,
                                           double backoff_base, int retries);

// Convenience function that uses an exponential backoff base of 1.3.
testing::AssertionResult IsExpectedBackoff(absl::Duration d,
                                           absl::Duration min_delay,
                                           absl::Duration max_delay,
                                           int retries);

// Returns the inclusive range of possible return values for ComputeBackoff()
//
// ComputeBackoff may use system conditions to derive the acceptable range of
// backoffs, so do not cache the result of BackoffRange; instead call it every
// time you are testing that an observed delay was expected given a particular
// set of backoff parameters.
//
// Args:
//   min_delay: min value supplied to ComputeBackoff()
//   max_delay: max value supplied to ComputeBackoff()
//   backoff_base: exponential backoff base supplied to ComputeBackoff()
//   retries: retries value supplied to ComputeBackoff()
// Returns:
//   first: range min, second: range max
std::pair<absl::Duration, absl::Duration> BackoffRange(absl::Duration min_delay,
                                                       absl::Duration max_delay,
                                                       double backoff_base,
                                                       int retries);

// Convenience function that uses an exponential backoff base of 1.3.
std::pair<absl::Duration, absl::Duration> BackoffRange(absl::Duration min_delay,
                                                       absl::Duration max_delay,
                                                       int retries);

// GMock matcher for testing whether the client backs off for the expected
// amount of time between failed RPCs.
//
// See the comment at the top of this file for an example usage.
//
// Args:
//   min_delay: min value supplied to ComputeBackoff()
//   max_delay: max value supplied to ComputeBackoff()
//   backoff_base: exponential backoff base supplied to ComputeBackoff()
//   retries: retries value supplied to ComputeBackoff()
// Returns:
//   a GMock matcher for this range
testing::Matcher<absl::Duration> DurationIsExpectedBackoff(
    absl::Duration min_delay, absl::Duration max_delay, double backoff_base,
    int retries);

// Convenience function that uses an exponential backoff base of 1.3.
testing::Matcher<absl::Duration> DurationIsExpectedBackoff(
    absl::Duration min_delay, absl::Duration max_delay, int retries);

}  // namespace util_time

#endif  // THIRD_PARTY_GLOOP_UTIL_TIME_BACKOFF_TEST_UTIL_H_
