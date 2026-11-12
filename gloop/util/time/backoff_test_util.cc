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

#include <algorithm>
#include <ostream>
#include <utility>

#include "absl/log/check.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace util_time {
namespace {

constexpr double kDefaultBackoffBase = 1.3;

// This matcher compares a given duration to the possible range of backoff
// durations.
class DurationIsExpectedBackoffMatcher
    : public testing::MatcherInterface<absl::Duration> {
 public:
  DurationIsExpectedBackoffMatcher(absl::Duration min_delay,
                                   absl::Duration max_delay,
                                   double backoff_base, int retries)
      : min_delay_(min_delay),
        max_delay_(max_delay),
        backoff_base_(backoff_base),
        retries_(retries),
        range_(BackoffRange(min_delay, max_delay, backoff_base, retries)) {}

  bool MatchAndExplain(absl::Duration d,
                       testing::MatchResultListener*) const override {
    return range_.first <= d && d <= range_.second;
  }

  void DescribeTo(std::ostream* os) const override {
    *os << "is in range [" << range_.first << ',' << range_.second
        << "] for min_delay = " << min_delay_ << ", max_delay = " << max_delay_
        << ", backoff_base = " << backoff_base_ << ", retries = " << retries_;
  }

  void DescribeNegationTo(std::ostream* os) const override {
    *os << "is not in range [" << range_.first << ',' << range_.second
        << "] for min_delay = " << min_delay_ << ", max_delay = " << max_delay_
        << ", backoff_base = " << backoff_base_ << ", retries = " << retries_;
  }

 private:
  absl::Duration min_delay_;
  absl::Duration max_delay_;
  double backoff_base_;
  int retries_;
  std::pair<absl::Duration, absl::Duration> range_;
};

}  // namespace

testing::AssertionResult IsExpectedBackoff(absl::Duration d,
                                           absl::Duration min_delay,
                                           absl::Duration max_delay,
                                           double backoff_base, int retries) {
  const auto range = BackoffRange(min_delay, max_delay, backoff_base, retries);
  if (range.first <= d && d <= range.second) {
    return testing::AssertionSuccess()
           << d << " is in range [" << range.first << ',' << range.second
           << "] for min_delay = " << min_delay << ", max_delay = " << max_delay
           << ", backoff_base = " << backoff_base << ", retries = " << retries;
  }
  return testing::AssertionFailure()
         << d << " is not in range [" << range.first << ',' << range.second
         << "] for min_delay = " << min_delay << ", max_delay = " << max_delay
         << ", backoff_base = " << backoff_base << ", retries = " << retries;
}

testing::AssertionResult IsExpectedBackoff(absl::Duration d,
                                           absl::Duration min_delay,
                                           absl::Duration max_delay,
                                           int retries) {
  return IsExpectedBackoff(d, min_delay, max_delay, kDefaultBackoffBase,
                           retries);
}

std::pair<absl::Duration, absl::Duration> BackoffRange(absl::Duration min_delay,
                                                       absl::Duration max_delay,
                                                       double backoff_base,
                                                       int retries) {
  // These are DCHECKs and log warnings in ComputeBackoff, but this is
  // just a test, so let's elevate to CHECK.
  CHECK_GE(min_delay, absl::Nanoseconds(1));
  CHECK_GE(max_delay, min_delay);
  CHECK_GT(backoff_base, 1.0);
  CHECK_GE(retries, 0);

  // The following is a near copy of the ComputeBackoff impl, except it
  // turns the random modifier part into a min and max. This must be changed if
  // ComputeBackoff changes. If ComputeBackoff is changed
  // without changing this code, tests using this lib will start failing unless
  // the new impl only shrinks the expected range.
  constexpr double kBackoffRandMult = 0.4;
  const absl::Duration first_term = kBackoffRandMult * min_delay;
  absl::Duration uncapped_second_term = min_delay;
  while (retries > 0 && uncapped_second_term < max_delay - first_term) {
    retries--;
    uncapped_second_term *= backoff_base;
  }
  absl::Duration second_term =
      std::min(uncapped_second_term, max_delay - first_term);
  absl::Duration lower_bound =
      first_term + second_term * (1.0 - kBackoffRandMult);
  absl::Duration upper_bound = first_term + second_term;

  return {
      std::clamp(lower_bound, min_delay, max_delay),
      std::clamp(upper_bound, min_delay, max_delay),
  };
}

std::pair<absl::Duration, absl::Duration> BackoffRange(absl::Duration min_delay,
                                                       absl::Duration max_delay,
                                                       int retries) {
  return BackoffRange(min_delay, max_delay, kDefaultBackoffBase, retries);
}

testing::Matcher<absl::Duration> DurationIsExpectedBackoff(
    absl::Duration min_delay, absl::Duration max_delay, double backoff_base,
    int retries) {
  return testing::MakeMatcher(new DurationIsExpectedBackoffMatcher(
      min_delay, max_delay, backoff_base, retries));
}

testing::Matcher<absl::Duration> DurationIsExpectedBackoff(
    absl::Duration min_delay, absl::Duration max_delay, int retries) {
  return DurationIsExpectedBackoff(min_delay, max_delay, kDefaultBackoffBase,
                                   retries);
}

}  // namespace util_time
