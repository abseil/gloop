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

#include "gloop/util/random/distributions.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "absl/numeric/int128.h"
#include "absl/random/internal/distribution_test_util.h"
#include "absl/random/mocking_bit_gen.h"
#include "absl/random/random.h"
#include "gloop/util/random/mock_distributions.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

static constexpr int kSize = 400000;

class DistributionsTest : public testing::Test {};

TEST_F(DistributionsTest, SkewedLow) {
  std::vector<double> values(kSize);

  absl::InsecureBitGen gen;
  for (int i = 0; i < kSize; i++) {
    values[i] = util_random::SkewedLow<int64_t>(gen, 0, (1 << 10) - 1);
  }

  // The mean is the sum of the fractional means of the uniform distributions:
  // [0..0][0..1][0..3][0..7][0..15][0..31][0..63]
  // [0..127][0..255][0..511][0..1023]
  const double mean =
      (0 + 1 + 3 + 7 + 15 + 31 + 63 + 127 + 255 + 511 + 1023) / (11.0 * 2.0);

  const auto moments =
      absl::random_internal::ComputeDistributionMoments(values);
  EXPECT_NEAR(mean, moments.mean, 2.0) << moments;
}

TEST_F(DistributionsTest, SkewedLowOverloads) {
  absl::InsecureBitGen gen;

  util_random::SkewedLow<int>(gen, 0, 100);
  util_random::SkewedLow<int8_t>(gen, 0, 100);
  util_random::SkewedLow<int16_t>(gen, 0, 100);
  util_random::SkewedLow<uint16_t>(gen, 0, 100);
  util_random::SkewedLow<int32_t>(gen, 0, 1 << 10);
  util_random::SkewedLow<uint32_t>(gen, 0, 1 << 10);
  util_random::SkewedLow<int64_t>(gen, 0, 1 << 10);
  util_random::SkewedLow<uint64_t>(gen, 0, 1 << 10);
  util_random::SkewedLow<absl::int128>(gen, 0, 1 << 10);
  util_random::SkewedLow<absl::uint128>(gen, 0, 1 << 10);

  util_random::SkewedLow<uint64_t>(absl::InsecureBitGen(), 0, 1 << 10);
}

TEST_F(DistributionsTest, YuleSimon) {
  std::vector<double> values(kSize);

  absl::InsecureBitGen gen;
  for (int i = 0; i < kSize; i++) {
    values[i] = util_random::YuleSimon<int64_t>(gen, 3);
  }

  const auto moments =
      absl::random_internal::ComputeDistributionMoments(values);
  // mean := a / (a-1),  stddev := a / ((a-1) * sqrt(a-2))
  // given a = 3.0, mean = 1.5, stddev = 1.5
  // std of mean converges to std::sqrt(kSize) * stddev
  // 3 std is used to reduce the flakiness
  EXPECT_NEAR(1.5, moments.mean, std::sqrt(kSize) * 1.5 * 3) << moments;
}

TEST_F(DistributionsTest, YuleSimonOverloads) {
  absl::InsecureBitGen gen;

  util_random::YuleSimon<int>(gen);
  util_random::YuleSimon<int8_t>(gen);
  util_random::YuleSimon<int16_t>(gen);
  util_random::YuleSimon<uint16_t>(gen);
  util_random::YuleSimon<int32_t>(gen);
  util_random::YuleSimon<uint32_t>(gen);
  util_random::YuleSimon<int64_t>(gen);
  util_random::YuleSimon<uint64_t>(gen);
  util_random::YuleSimon<absl::int128>(gen);
  util_random::YuleSimon<absl::uint128>(gen);

  util_random::YuleSimon<uint64_t>(absl::InsecureBitGen());
}

TEST_F(DistributionsTest, SmallPrimeOverloads) {
  absl::InsecureBitGen gen;

  util_random::SmallPrime<int>(gen);
  util_random::SmallPrime<int>(gen, 3, 100);
  util_random::SmallPrime<int8_t>(gen, 3, 100);
  util_random::SmallPrime<int16_t>(gen, 3, 100);
  util_random::SmallPrime<uint16_t>(gen, 3, 100);
  util_random::SmallPrime<int32_t>(gen, 3, 1 << 10);
  util_random::SmallPrime<uint32_t>(gen, 3, 1 << 10);
  util_random::SmallPrime<int64_t>(gen, 3, 1 << 10);
  util_random::SmallPrime<uint64_t>(gen, 3, 1 << 10);

  util_random::SmallPrime<uint64_t>(absl::InsecureBitGen(), 3, 1 << 10);
}

TEST(MockDistributions, Examples) {
  absl::MockingBitGen gen;

  EXPECT_NE(util_random::YuleSimon<int>(gen, 2.0), 10010);
  EXPECT_CALL(util_random::MockYuleSimon<int>(), Call(gen, 2.0))
      .WillOnce(::testing::Return(10010));
  EXPECT_EQ(util_random::YuleSimon<int>(gen, 2.0), 10010);

  EXPECT_NE(1221, util_random::SkewedLow<int>(gen, 1, 10000, 3));
  EXPECT_CALL(util_random::MockSkewedLow<int>(), Call(gen, 1, 10000, 3))
      .WillOnce(::testing::Return(1221));
  EXPECT_EQ(1221, util_random::SkewedLow<int>(gen, 1, 10000, 3));

  EXPECT_NE(7, util_random::SmallPrime<int>(gen, 3, 1000));
  EXPECT_CALL(util_random::MockSmallPrime<int>(), Call(gen, 3, 1000))
      .WillOnce(::testing::Return(7));
  EXPECT_EQ(7, util_random::SmallPrime<int>(gen, 3, 1000));
}

}  // namespace
