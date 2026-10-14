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

#include "gloop/util/floatops/roundedops.h"

#include <cmath>
#include <cstdint>
#include <limits>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace util {
namespace floatops {
namespace {

using ::testing::AnyOf;
using ::testing::Eq;

TEST(RoundedOpsTest, RoundedAddSubtractWorks) {
  // Epsilon is the smallest e > 0, such that 1 + e != 1. To test rounding,
  // add something just a little less than epsilon to 1.0. Floating point
  // values are twice as dense just below 1.0 as they are just above 1.0,
  // so use half that value there.
  constexpr double kEpsPoint75 = std::numeric_limits<double>::epsilon() * 0.75;
  constexpr double kEpsPoint375 =
      std::numeric_limits<double>::epsilon() * 0.375;

  EXPECT_GT(RoundedAdd<kUpward>(1.0, kEpsPoint75), 1.0);
  EXPECT_EQ(RoundedAdd<kDownward>(1.0, kEpsPoint75), 1.0);
  EXPECT_EQ(RoundedAdd<kTowardZero>(1.0, kEpsPoint75), 1.0);
  EXPECT_GT(RoundedAdd<kToNearest>(1.0, kEpsPoint75), 1.0);
  EXPECT_EQ(RoundedAdd<kToNearest>(1.0, kEpsPoint375), 1.0);

  EXPECT_EQ(RoundedSubtract<kUpward>(1.0, kEpsPoint375), 1.0);
  EXPECT_LT(RoundedSubtract<kDownward>(1.0, kEpsPoint375), 1.0);
  EXPECT_LT(RoundedSubtract<kTowardZero>(1.0, kEpsPoint375), 1.0);
  EXPECT_LT(RoundedSubtract<kToNearest>(1.0, kEpsPoint375), 1.0);

  EXPECT_GT(RoundedAdd<kUpward>(-1.0, kEpsPoint375), -1.0);
  EXPECT_EQ(RoundedAdd<kDownward>(-1.0, kEpsPoint375), -1.0);
  EXPECT_GT(RoundedAdd<kTowardZero>(-1.0, kEpsPoint375), -1.0);
  EXPECT_GT(RoundedAdd<kToNearest>(-1.0, kEpsPoint375), -1.0);

  EXPECT_EQ(RoundedSubtract<kUpward>(-1.0, kEpsPoint75), -1.0);
  EXPECT_LT(RoundedSubtract<kDownward>(-1.0, kEpsPoint75), -1.0);
  EXPECT_EQ(RoundedSubtract<kTowardZero>(-1.0, kEpsPoint75), -1.0);
  EXPECT_LT(RoundedSubtract<kToNearest>(-1.0, kEpsPoint75), -1.0);
  EXPECT_EQ(RoundedSubtract<kToNearest>(-1.0, kEpsPoint375), -1.0);
}

TEST(RoundedOpsTest, RoundedDivideDownUpWorks) {
  const float float_one_third_down = RoundedDivide<kDownward>(1.0f, 3.0f);
  const float float_one_third_up = RoundedDivide<kUpward>(1.0f, 3.0f);

  const double double_one_third_down = RoundedDivide<kDownward>(1.0, 3.0);
  const double double_one_third_up = RoundedDivide<kUpward>(1.0, 3.0);

  EXPECT_GT(float_one_third_down, 0.333333f);
  EXPECT_LT(float_one_third_down, double_one_third_down);
  EXPECT_LT(double_one_third_down, double_one_third_up);
  EXPECT_LT(double_one_third_up, float_one_third_up);
  EXPECT_LT(float_one_third_up, 0.333334f);
}

TEST(RoundedOpsTest, RoundedDivideTowardZeroWorks) {
  EXPECT_EQ(RoundedDivide<kTowardZero>(1.0f, 3.0f),
            RoundedDivide<kDownward>(1.0f, 3.0f));
  EXPECT_EQ(RoundedDivide<kTowardZero>(1.0, 3.0),
            RoundedDivide<kDownward>(1.0, 3.0));

  EXPECT_EQ(RoundedDivide<kTowardZero>(-1.0f, 3.0f),
            RoundedDivide<kUpward>(-1.0f, 3.0f));
  EXPECT_EQ(RoundedDivide<kTowardZero>(-1.0, 3.0),
            RoundedDivide<kUpward>(-1.0, 3.0));

  EXPECT_EQ(RoundedDivide<kTowardZero>(1.0f, -3.0f),
            RoundedDivide<kUpward>(1.0f, -3.0f));
  EXPECT_EQ(RoundedDivide<kTowardZero>(1.0, -3.0),
            RoundedDivide<kUpward>(1.0, -3.0));
}

TEST(RoundedOpsTest, RoundedDivideToNearestWorks) {
  const float float_one_third_to_nearest =
      RoundedDivide<kToNearest>(1.0f, 3.0f);
  EXPECT_THAT(float_one_third_to_nearest,
              AnyOf(Eq(RoundedDivide<kUpward>(1.0f, 3.0f)),
                    Eq(RoundedDivide<kDownward>(1.0f, 3.0f))));

  const double double_one_third_to_nearest =
      RoundedDivide<kToNearest>(1.0, 3.0);
  EXPECT_THAT(double_one_third_to_nearest,
              AnyOf(Eq(RoundedDivide<kUpward>(1.0, 3.0)),
                    Eq(RoundedDivide<kDownward>(1.0, 3.0))));
}

TEST(RoundedOpsTest, RoundedMultiplyWorks) {
  const float one_third_up = RoundedDivide<kUpward>(1.0f, 3.0f);

  EXPECT_EQ(RoundedMultiply<kDownward>(one_third_up, 3.0f), 1.0f);
  EXPECT_GT(RoundedMultiply<kUpward>(one_third_up, 3.0f), 1.0f);
  EXPECT_EQ(RoundedMultiply<kToNearest>(one_third_up, 3.0f), 1.0f);
  EXPECT_EQ(RoundedMultiply<kTowardZero>(one_third_up, 3.0f), 1.0f);
}

template <typename T>
class RoundToNearestIntegerTiesToEvenTest : public ::testing::Test {};

using FloatTypes = ::testing::Types<float, double>;
TYPED_TEST_SUITE(RoundToNearestIntegerTiesToEvenTest, FloatTypes);

TYPED_TEST(RoundToNearestIntegerTiesToEvenTest,
           HalfwayCasesRoundToNearestEven) {
  using FloatType = TypeParam;
  // Halfway cases are rounded to nearest even integer.
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(0.5)), 0);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(1.5)), 2);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(2.5)), 2);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(3.5)), 4);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(4.5)), 4);

  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(-0.5)), 0);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(-1.5)), -2);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(-2.5)), -2);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(-3.5)), -4);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(-4.5)), -4);
}

TYPED_TEST(RoundToNearestIntegerTiesToEvenTest,
           AlmostHalfwayCasesRoundLikeStdRound) {
  using FloatType = TypeParam;
  // Almost halfway cases are handled as round() would handle them.
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(
                std::nextafter(FloatType(0.5), FloatType(0.0))),
            0);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(
                std::nextafter(FloatType(0.5), FloatType(1.0))),
            1);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(
                std::nextafter(FloatType(1.5), FloatType(1.0))),
            1);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(
                std::nextafter(FloatType(1.5), FloatType(2.0))),
            2);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(
                std::nextafter(FloatType(2.5), FloatType(2.0))),
            2);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(
                std::nextafter(FloatType(2.5), FloatType(3.0))),
            3);

  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(
                std::nextafter(FloatType(-0.5), FloatType(0.0))),
            0);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(
                std::nextafter(FloatType(-0.5), FloatType(-1.0))),
            -1);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(
                std::nextafter(FloatType(-1.5), FloatType(-1.0))),
            -1);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(
                std::nextafter(FloatType(-1.5), FloatType(-2.0))),
            -2);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(
                std::nextafter(FloatType(-2.5), FloatType(-2.0))),
            -2);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(
                std::nextafter(FloatType(-2.5), FloatType(-3.0))),
            -3);
}

TYPED_TEST(RoundToNearestIntegerTiesToEvenTest, OtherCasesRoundLikeStdRound) {
  using FloatType = TypeParam;
  // Other cases are also handled as round() would handle them.
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(0.0)), 0);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(
                std::nextafter(FloatType(0.0), FloatType(1.0))),
            0);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(0.25)), 0);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(0.75)), 1);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(
                std::nextafter(FloatType(1.0), FloatType(0.0))),
            1);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(1.0)), 1);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(
                std::nextafter(FloatType(1.0), FloatType(2.0))),
            1);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(1.25)), 1);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(1.75)), 2);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(
                std::nextafter(FloatType(2.0), FloatType(1.0))),
            2);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(2.0)), 2);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(
                std::nextafter(FloatType(2.0), FloatType(3.0))),
            2);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(2.25)), 2);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(2.75)), 3);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(
                std::nextafter(FloatType(3.0), FloatType(2.0))),
            3);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(3.0)), 3);

  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(-0.0)), 0);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(
                std::nextafter(FloatType(-0.0), FloatType(-1.0))),
            0);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(-0.25)), 0);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(-0.75)), -1);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(
                std::nextafter(FloatType(-1.0), FloatType(0.0))),
            -1);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(-1.0)), -1);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(
                std::nextafter(FloatType(-1.0), FloatType(-2.0))),
            -1);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(-1.25)), -1);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(-1.75)), -2);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(
                std::nextafter(FloatType(-2.0), FloatType(-1.0))),
            -2);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(-2.0)), -2);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(
                std::nextafter(FloatType(-2.0), FloatType(-3.0))),
            -2);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(-2.25)), -2);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(-2.75)), -3);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(
                std::nextafter(FloatType(-3.0), FloatType(-2.0))),
            -3);
  EXPECT_EQ(RoundToNearestIntegerTiesToEven<FloatType>(FloatType(-3.0)), -3);
}

template <typename T>
class RoundToNearestTiesToEvenTest : public ::testing::Test {};

TYPED_TEST_SUITE(RoundToNearestTiesToEvenTest, FloatTypes);

TYPED_TEST(RoundToNearestTiesToEvenTest, HalfwayCasesRoundToNearestEven) {
  using FloatType = TypeParam;
  // Halfway cases are rounded to nearest even integer.
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(0.5)),
            FloatType(0.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(1.5)),
            FloatType(2.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(2.5)),
            FloatType(2.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(3.5)),
            FloatType(4.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(4.5)),
            FloatType(4.0));

  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(-0.5)),
            FloatType(0.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(-1.5)),
            FloatType(-2.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(-2.5)),
            FloatType(-2.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(-3.5)),
            FloatType(-4.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(-4.5)),
            FloatType(-4.0));
}

TYPED_TEST(RoundToNearestTiesToEvenTest, AlmostHalfwayCasesRoundLikeStdRound) {
  using FloatType = TypeParam;
  // Almost halfway cases are handled as round() would handle them.
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(
                std::nextafter(FloatType(0.5), FloatType(0.0))),
            FloatType(0.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(
                std::nextafter(FloatType(0.5), FloatType(1.0))),
            FloatType(1.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(
                std::nextafter(FloatType(1.5), FloatType(1.0))),
            FloatType(1.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(
                std::nextafter(FloatType(1.5), FloatType(2.0))),
            FloatType(2.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(
                std::nextafter(FloatType(2.5), FloatType(2.0))),
            FloatType(2.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(
                std::nextafter(FloatType(2.5), FloatType(3.0))),
            FloatType(3.0));

  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(
                std::nextafter(FloatType(-0.5), FloatType(0.0))),
            FloatType(0.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(
                std::nextafter(FloatType(-0.5), FloatType(-1.0))),
            FloatType(-1.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(
                std::nextafter(FloatType(-1.5), FloatType(-1.0))),
            FloatType(-1.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(
                std::nextafter(FloatType(-1.5), FloatType(-2.0))),
            FloatType(-2.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(
                std::nextafter(FloatType(-2.5), FloatType(-2.0))),
            FloatType(-2.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(
                std::nextafter(FloatType(-2.5), FloatType(-3.0))),
            FloatType(-3.0));
}

TYPED_TEST(RoundToNearestTiesToEvenTest, OtherCasesRoundLikeStdRound) {
  using FloatType = TypeParam;
  // Other cases are also handled as round() would handle them.
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(0.0)),
            FloatType(0.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(
                std::nextafter(FloatType(0.0), FloatType(1.0))),
            FloatType(0.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(0.25)),
            FloatType(0.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(0.75)),
            FloatType(1.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(
                std::nextafter(FloatType(1.0), FloatType(0.0))),
            FloatType(1.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(1.0)),
            FloatType(1.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(
                std::nextafter(FloatType(1.0), FloatType(2.0))),
            FloatType(1.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(1.25)),
            FloatType(1.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(1.75)),
            FloatType(2.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(
                std::nextafter(FloatType(2.0), FloatType(1.0))),
            FloatType(2.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(2.0)),
            FloatType(2.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(
                std::nextafter(FloatType(2.0), FloatType(3.0))),
            FloatType(2.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(2.25)),
            FloatType(2.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(2.75)),
            FloatType(3.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(
                std::nextafter(FloatType(3.0), FloatType(2.0))),
            FloatType(3.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(3.0)),
            FloatType(3.0));

  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(-0.0)),
            FloatType(0.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(
                std::nextafter(FloatType(-0.0), FloatType(-1.0))),
            FloatType(0.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(-0.25)),
            FloatType(0.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(-0.75)),
            FloatType(-1.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(
                std::nextafter(FloatType(-1.0), FloatType(0.0))),
            FloatType(-1.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(-1.0)),
            FloatType(-1.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(
                std::nextafter(FloatType(-1.0), FloatType(-2.0))),
            FloatType(-1.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(-1.25)),
            FloatType(-1.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(-1.75)),
            FloatType(-2.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(
                std::nextafter(FloatType(-2.0), FloatType(-1.0))),
            FloatType(-2.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(-2.0)),
            FloatType(-2.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(
                std::nextafter(FloatType(-2.0), FloatType(-3.0))),
            FloatType(-2.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(-2.25)),
            FloatType(-2.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(-2.75)),
            FloatType(-3.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(
                std::nextafter(FloatType(-3.0), FloatType(-2.0))),
            FloatType(-3.0));
  EXPECT_EQ(RoundToNearestTiesToEven<FloatType>(FloatType(-3.0)),
            FloatType(-3.0));
}

}  // namespace
}  // namespace floatops
}  // namespace util
