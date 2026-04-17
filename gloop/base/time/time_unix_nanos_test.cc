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

#include "gloop/base/time/time_unix_nanos.h"

#include <cstdint>
#include <limits>
#include <sstream>
#include <string>

#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace base {
namespace {

using ::testing::Eq;
using ::testing::Ge;
using ::testing::Le;

constexpr int64_t kMinNanos = std::numeric_limits<int64_t>::min();
constexpr int64_t kMaxNanos = std::numeric_limits<int64_t>::max();
constexpr absl::Time kMinTime = absl::FromUnixNanos(kMinNanos);
constexpr absl::Time kMaxTime = absl::FromUnixNanos(kMaxNanos);

TEST(TimeUnixNanosTest, DefaultConstruct) {
  TimeUnixNanos time;
  EXPECT_THAT(time.AsUnixNanos(), Eq(0));
  EXPECT_THAT(time.AsTime(), absl::UnixEpoch());
}

TEST(TimeUnixNanosTest, FromUnixNanos) {
  TimeUnixNanos time = TimeUnixNanos::FromUnixNanos(1234567);
  EXPECT_THAT(time.AsUnixNanos(), Eq(1234567));
  EXPECT_THAT(time.AsTime(), absl::FromUnixNanos(1234567));

  time = TimeUnixNanos::FromUnixNanos(kMinNanos);
  EXPECT_THAT(time.AsUnixNanos(), Eq(kMinNanos));
  EXPECT_THAT(time.AsTime(), Eq(kMinTime));

  time = TimeUnixNanos::FromUnixNanos(kMaxNanos);
  EXPECT_THAT(time.AsUnixNanos(), Eq(kMaxNanos));
  EXPECT_THAT(time.AsTime(), Eq(kMaxTime));
}

TEST(TimeUnixNanosTest, FromTime) {
  absl::Time now = absl::Now();
  TimeUnixNanos time = TimeUnixNanos::FromTime(now);
  EXPECT_THAT(time.AsUnixNanos(), Eq(absl::ToUnixNanos(now)));
  EXPECT_THAT(time.AsTime(), Eq(now));

  time = TimeUnixNanos::FromTime(kMinTime);
  EXPECT_THAT(time.AsUnixNanos(), Eq(kMinNanos));
  EXPECT_THAT(time.AsTime(), Eq(kMinTime));

  time = TimeUnixNanos::FromTime(kMaxTime);
  EXPECT_THAT(time.AsUnixNanos(), Eq(kMaxNanos));
  EXPECT_THAT(time.AsTime(), Eq(kMaxTime));

#if GTEST_HAS_DEATH_TEST
  constexpr auto one_ns = absl::Nanoseconds(1);
  EXPECT_DEBUG_DEATH(time = TimeUnixNanos::FromTime(kMinTime - one_ns), ".*");
  EXPECT_DEBUG_DEATH(time = TimeUnixNanos::FromTime(kMaxTime + one_ns), ".*");
#endif  // GTEST_HAS_DEATH_TEST
}

TEST(TimeUnixNanosTest, Now) {
  absl::Time now_before = absl::Now();
  TimeUnixNanos time = TimeUnixNanos::Now();
  absl::Time now_after = absl::Now();
  EXPECT_THAT(time.AsTime(), Ge(now_before));
  EXPECT_THAT(time.AsTime(), Le(now_after));
}

TEST(TimeUnixNanosTest, MinMax) {
  EXPECT_THAT(TimeUnixNanos::min().AsUnixNanos(),
              Eq(std::numeric_limits<int64_t>::min()));
  EXPECT_THAT(TimeUnixNanos::max().AsUnixNanos(),
              Eq(std::numeric_limits<int64_t>::max()));
}

TEST(TimeUnixNanosTest, Comparison) {
  // Values of interest.
  const int64_t kValues[] = {std::numeric_limits<int64_t>::min(),
                             std::numeric_limits<int64_t>::min() / 2,
                             -1,
                             0,
                             1,
                             std::numeric_limits<int64_t>::max() / 2,
                             std::numeric_limits<int64_t>::max()};
  for (int64_t lvalue : kValues) {
    for (int64_t rvalue : kValues) {
      TimeUnixNanos lhs = TimeUnixNanos::FromUnixNanos(lvalue);
      TimeUnixNanos rhs = TimeUnixNanos::FromUnixNanos(rvalue);
      EXPECT_THAT(lhs == rhs, Eq(lvalue == rvalue));
      EXPECT_THAT(lhs != rhs, Eq(lvalue != rvalue));
      EXPECT_THAT(lhs < rhs, Eq(lvalue < rvalue));
      EXPECT_THAT(lhs > rhs, Eq(lvalue > rvalue));
      EXPECT_THAT(lhs <= rhs, Eq(lvalue <= rvalue));
      EXPECT_THAT(lhs >= rhs, Eq(lvalue >= rvalue));
    }
  }
}

TEST(TimeUnixNanosTest, StreamInsertion) {
  TimeUnixNanos value = TimeUnixNanos::Now();
  std::string time_unix_nanos;
  std::stringstream time_unix_nanos_ss;
  time_unix_nanos_ss << value;
  std::stringstream time_ss;
  time_ss << value.AsTime();
  EXPECT_THAT(time_unix_nanos_ss.str(), Eq(time_ss.str()));
}

TEST(TimeUnixNanosTest, StrCat) {
  TimeUnixNanos value = TimeUnixNanos::Now();
  EXPECT_THAT(absl::StrCat(value), Eq(absl::StrCat(value.AsTime())));
}

}  // namespace
}  // namespace base
