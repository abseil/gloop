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

#include "gloop/util/time/backoff.h"

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace util_time {
namespace {

constexpr double kBackoffBase = 1.3;

MATCHER_P2(InRange, min, max, "") { return arg >= min && arg <= max; }

TEST(ComputeBackoffTest, Simple) {
  const absl::Duration ns = absl::Nanoseconds(1);
  const absl::Duration min_d = 100 * ns;
  const absl::Duration max_d = 1000 * ns;

  // Lower bound = (0.4 + 0.6 * 1.3^retries) * min_delay
  // Upper bound = (0.4 + 1.3^retries) * min_delay
  EXPECT_THAT(ComputeBackoff(min_d, max_d, kBackoffBase, 0),
              InRange(100 * ns, 140 * ns));
  EXPECT_THAT(ComputeBackoff(min_d, max_d, kBackoffBase, 1),
              InRange(118 * ns, 170 * ns));
  EXPECT_THAT(ComputeBackoff(min_d, max_d, kBackoffBase, 2),
              InRange(141 * ns, 209 * ns));
  EXPECT_THAT(ComputeBackoff(min_d, max_d, kBackoffBase, 3),
              InRange(171 * ns, 260 * ns));
  EXPECT_THAT(ComputeBackoff(min_d, max_d, kBackoffBase, 4),
              InRange(211 * ns, 326 * ns));
  // Once maxed out, min will be 0.6 * (max - 0.4 * min) + 0.4 * min
  EXPECT_THAT(ComputeBackoff(min_d, max_d, kBackoffBase, 100),
              InRange(0.6 * (max_d - 0.4 * min_d) + 0.4 * min_d, 1000 * ns));
}

TEST(ComputeBackoffTest, AlwaysInMinMaxRange) {
  const absl::Duration ns = absl::Nanoseconds(1);
  const absl::Duration min_d = 100 * ns;
  const absl::Duration max_d = 1000 * ns;
  static const int kNumIterations = 10000;

  // kMaxRetries is large enough so 1.3^kMaxRetries is significantly
  // bigger than max_delay/min_delay
  static const int kMaxRetries = 20;
  for (int i = 0; i < kNumIterations; ++i) {
    for (int retries = 0; retries <= kMaxRetries; ++retries) {
      ASSERT_THAT(ComputeBackoff(min_d, max_d, kBackoffBase, retries),
                  InRange(min_d, max_d));
    }
  }
}

TEST(ComputeBackoffTest, MinIsNegative) {
  const auto ns ABSL_ATTRIBUTE_UNUSED = absl::Nanoseconds(1);
  const auto infinity ABSL_ATTRIBUTE_UNUSED = absl::InfiniteDuration();

#ifdef NDEBUG
  // A negative value should be treated as a small positive value.
  const auto b = ComputeBackoff(-1 * ns, infinity, kBackoffBase, 0);
  EXPECT_LE(absl::Nanoseconds(1), b);
  EXPECT_LT(b, absl::Milliseconds(10));
#elif GTEST_HAS_DEATH_TEST
  EXPECT_DEATH(
      { (void)ComputeBackoff(-1 * ns, infinity, kBackoffBase, 0); },
      "min_delay >= absl::Duration\\(\\)");
#endif
}

TEST(ComputeBackoffTest, MinIsZero) {
  const auto zero = absl::Duration();
  const auto ns = absl::Nanoseconds(1);
  const auto infinity = absl::InfiniteDuration();

  EXPECT_GE(ComputeBackoff(zero, infinity, kBackoffBase, 0), 1 * ns);
  EXPECT_GE(ComputeBackoff(zero, infinity, kBackoffBase, 1), 1.18 * ns);
  EXPECT_GE(ComputeBackoff(zero, infinity, kBackoffBase, 10), 8.6 * ns);
}

TEST(ComputeBackoffTest, MaxIsLessThanMin) {
  const auto ns ABSL_ATTRIBUTE_UNUSED = absl::Nanoseconds(1);

#ifdef NDEBUG
  // A smaller value should be treated as equal to the min value.
  EXPECT_EQ(100 * ns, ComputeBackoff(100 * ns, 90 * ns, kBackoffBase, 0));
#elif GTEST_HAS_DEATH_TEST
  EXPECT_DEATH(
      { (void)ComputeBackoff(100 * ns, 90 * ns, kBackoffBase, 0); },
      "max_delay >= min_delay");
#endif
}

TEST(ComputeBackoffTest, MaxEqualsMin) {
  const auto ns = absl::Nanoseconds(1);
  EXPECT_EQ(100 * ns, ComputeBackoff(100 * ns, 100 * ns, kBackoffBase, 0));
}

TEST(ComputeBackoffTest, PreviousRetriesIsNegative) {
  const auto ns ABSL_ATTRIBUTE_UNUSED = absl::Nanoseconds(1);
  const auto infinity ABSL_ATTRIBUTE_UNUSED = absl::InfiniteDuration();

#ifdef NDEBUG
  // A negative value should be treated as zero.
  EXPECT_GE(ComputeBackoff(100 * ns, infinity, kBackoffBase, -1), 100 * ns);
#elif GTEST_HAS_DEATH_TEST
  EXPECT_DEATH(
      { (void)ComputeBackoff(100 * ns, infinity, kBackoffBase, -1); },
      "retries >= 0");
#endif
}

TEST(ComputeBackoffTest, BackoffBaseEqualsToOne) {
  const auto ns ABSL_ATTRIBUTE_UNUSED = absl::Nanoseconds(1);
  const auto infinity ABSL_ATTRIBUTE_UNUSED = absl::InfiniteDuration();

#ifdef NDEBUG
  // A backoff base <= 1.0 should be treated as 1.3.
  // Lower bound = (0.4 + 0.6 * 1.3) * 100 ns = 118 ns.
  // Upper bound = (0.4 + 1.0 * 1.3) * 100 ns = 170 ns.
  EXPECT_THAT(ComputeBackoff(100 * ns, infinity, 1.0, 1),
              InRange(118 * ns, 170 * ns));
#elif GTEST_HAS_DEATH_TEST
  EXPECT_DEATH(
      { (void)ComputeBackoff(100 * ns, infinity, 1.0, 1); },
      "backoff_base > 1.0");
#endif
}

TEST(ComputeBackoffTest, BackoffBaseIsLessThanOne) {
  const auto ns ABSL_ATTRIBUTE_UNUSED = absl::Nanoseconds(1);
  const auto infinity ABSL_ATTRIBUTE_UNUSED = absl::InfiniteDuration();

#ifdef NDEBUG
  // A backoff base <= 1.0 should be treated as 1.3.
  // Lower bound = (0.4 + 0.6 * 1.3) * 100 ns = 118 ns.
  // Upper bound = (0.4 + 1.0 * 1.3) * 100 ns = 170 ns.
  EXPECT_THAT(ComputeBackoff(100 * ns, infinity, 0.9, 1),
              InRange(118 * ns, 170 * ns));
#elif GTEST_HAS_DEATH_TEST
  EXPECT_DEATH(
      { (void)ComputeBackoff(100 * ns, infinity, 0.9, 1); },
      "backoff_base > 1.0");
#endif
}

TEST(ComputeBackoffTest, SmallDurations) {
  // Make sure we do actually exponentially back off for low minimum delays.
  EXPECT_LT(absl::Nanoseconds(1),
            ComputeBackoff(absl::Nanoseconds(1), absl::Nanoseconds(1000),
                           kBackoffBase, 10));
  EXPECT_LT(ComputeBackoff(absl::Nanoseconds(1), absl::Nanoseconds(1000),
                           kBackoffBase, 10),
            ComputeBackoff(absl::Nanoseconds(1), absl::Nanoseconds(1000),
                           kBackoffBase, 20));
}

TEST(ComputeBackoffTest, ManyRetries) {
  // Make sure we don't suffer any weird overflow issues.
  const auto backoff = ComputeBackoff(
      absl::Nanoseconds(1), absl::InfiniteDuration(), kBackoffBase, 1 << 30);

  EXPECT_LT(absl::Nanoseconds(1), backoff);
  EXPECT_LE(backoff, absl::InfiniteDuration());
}

TEST(ComputeBackoffTest, Distribution) {
  // Run ComputeBackoff() a bunch of times and keep track of how many
  // times each value occurred.
  int counts[1001];
  for (int i = 0; i < ABSL_ARRAYSIZE(counts); i++) {
    counts[i] = 0;
  }
  for (int i = 0; i < 100000; i++) {
    const auto d = ComputeBackoff(absl::Nanoseconds(10),
                                  absl::Nanoseconds(1000), kBackoffBase, 100);

    const auto ns = d / absl::Nanoseconds(1);
    ASSERT_GT(ABSL_ARRAYSIZE(counts), ns);
    counts[ns]++;
  }
  int too_small = 0;
  int too_large = 0;
  for (int i = 0; i < ABSL_ARRAYSIZE(counts); i++) {
    if (i < 600) {
      EXPECT_EQ(0, counts[i]) << i;
    } else {
      // The 400 acceptable slots must each have ~ 100000/400 = 250.
      if (counts[i] < 200) too_small++;
      if (counts[i] > 300) too_large++;
    }
  }
  EXPECT_GE(10, too_small);
  EXPECT_GE(10, too_large);
}

}  // namespace
}  // namespace util_time
