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
  EXPECT_THAT(RoundedDivide<kToNearest>(1.0f, 3.0f),
              AnyOf(Eq(RoundedDivide<kUpward>(1.0f, 3.0f)),
                    Eq(RoundedDivide<kDownward>(1.0f, 3.0f))));

  EXPECT_THAT(RoundedDivide<kToNearest>(1.0, 3.0),
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

void RunRoundingTest(int64_t (*RoundingFuncFloat)(float val),
                     int64_t (*RoundingFuncDouble)(double val)) {
  // Halfway cases are rounded to nearest even integer (for float).
  EXPECT_EQ(RoundingFuncFloat(0.5f), 0);
  EXPECT_EQ(RoundingFuncFloat(1.5f), 2);
  EXPECT_EQ(RoundingFuncFloat(2.5f), 2);
  EXPECT_EQ(RoundingFuncFloat(3.5f), 4);
  EXPECT_EQ(RoundingFuncFloat(4.5f), 4);

  EXPECT_EQ(RoundingFuncFloat(-0.5f), 0);
  EXPECT_EQ(RoundingFuncFloat(-1.5f), -2);
  EXPECT_EQ(RoundingFuncFloat(-2.5f), -2);
  EXPECT_EQ(RoundingFuncFloat(-3.5f), -4);
  EXPECT_EQ(RoundingFuncFloat(-4.5f), -4);

  // Halfway cases are rounded to nearest even integer (for double).
  EXPECT_EQ(RoundingFuncDouble(0.5), 0);
  EXPECT_EQ(RoundingFuncDouble(1.5), 2);
  EXPECT_EQ(RoundingFuncDouble(2.5), 2);
  EXPECT_EQ(RoundingFuncDouble(3.5), 4);
  EXPECT_EQ(RoundingFuncDouble(4.5), 4);

  EXPECT_EQ(RoundingFuncDouble(-0.5), 0);
  EXPECT_EQ(RoundingFuncDouble(-1.5), -2);
  EXPECT_EQ(RoundingFuncDouble(-2.5), -2);
  EXPECT_EQ(RoundingFuncDouble(-3.5), -4);
  EXPECT_EQ(RoundingFuncDouble(-4.5), -4);

  // Almost halfway cases are handled as round() would handle them (for float).
  EXPECT_EQ(RoundingFuncFloat(std::nextafter(0.5f, 0.0f)), 0);
  EXPECT_EQ(RoundingFuncFloat(std::nextafter(0.5f, 1.0f)), 1);
  EXPECT_EQ(RoundingFuncFloat(std::nextafter(1.5f, 1.0f)), 1);
  EXPECT_EQ(RoundingFuncFloat(std::nextafter(1.5f, 2.0f)), 2);
  EXPECT_EQ(RoundingFuncFloat(std::nextafter(2.5f, 2.0f)), 2);
  EXPECT_EQ(RoundingFuncFloat(std::nextafter(2.5f, 3.0f)), 3);

  EXPECT_EQ(RoundingFuncFloat(std::nextafter(-0.5f, 0.0f)), 0);
  EXPECT_EQ(RoundingFuncFloat(std::nextafter(-0.5f, -1.0f)), -1);
  EXPECT_EQ(RoundingFuncFloat(std::nextafter(-1.5f, -1.0f)), -1);
  EXPECT_EQ(RoundingFuncFloat(std::nextafter(-1.5f, -2.0f)), -2);
  EXPECT_EQ(RoundingFuncFloat(std::nextafter(-2.5f, -2.0f)), -2);
  EXPECT_EQ(RoundingFuncFloat(std::nextafter(-2.5f, -3.0f)), -3);

  // Almost halfway cases are handled as round() would handle them (for double).
  EXPECT_EQ(RoundingFuncDouble(std::nextafter(0.5, 0)), 0);
  EXPECT_EQ(RoundingFuncDouble(std::nextafter(0.5, 1)), 1);
  EXPECT_EQ(RoundingFuncDouble(std::nextafter(1.5, 1)), 1);
  EXPECT_EQ(RoundingFuncDouble(std::nextafter(1.5, 2)), 2);
  EXPECT_EQ(RoundingFuncDouble(std::nextafter(2.5, 2)), 2);
  EXPECT_EQ(RoundingFuncDouble(std::nextafter(2.5, 3)), 3);

  EXPECT_EQ(RoundingFuncDouble(std::nextafter(-0.5, 0)), 0);
  EXPECT_EQ(RoundingFuncDouble(std::nextafter(-0.5, -1)), -1);
  EXPECT_EQ(RoundingFuncDouble(std::nextafter(-1.5, -1)), -1);
  EXPECT_EQ(RoundingFuncDouble(std::nextafter(-1.5, -2)), -2);
  EXPECT_EQ(RoundingFuncDouble(std::nextafter(-2.5, -2)), -2);
  EXPECT_EQ(RoundingFuncDouble(std::nextafter(-2.5, -3)), -3);

  // Other cases are also handled as round() would handle them (for float).
  EXPECT_EQ(RoundingFuncFloat(0.0f), 0);
  EXPECT_EQ(RoundingFuncFloat(std::nextafter(0.0f, 1)), 0);
  EXPECT_EQ(RoundingFuncFloat(0.25f), 0);
  EXPECT_EQ(RoundingFuncFloat(0.75f), 1);
  EXPECT_EQ(RoundingFuncFloat(std::nextafter(1.0f, 0)), 1);
  EXPECT_EQ(RoundingFuncFloat(1.0f), 1);
  EXPECT_EQ(RoundingFuncFloat(std::nextafter(1.0f, 2)), 1);
  EXPECT_EQ(RoundingFuncFloat(1.25f), 1);
  EXPECT_EQ(RoundingFuncFloat(1.75f), 2);
  EXPECT_EQ(RoundingFuncFloat(std::nextafter(2.0f, 1)), 2);
  EXPECT_EQ(RoundingFuncFloat(2.0f), 2);
  EXPECT_EQ(RoundingFuncFloat(std::nextafter(2.0f, 3)), 2);
  EXPECT_EQ(RoundingFuncFloat(2.25f), 2);
  EXPECT_EQ(RoundingFuncFloat(2.75f), 3);
  EXPECT_EQ(RoundingFuncFloat(std::nextafter(3.0f, 2)), 3);
  EXPECT_EQ(RoundingFuncFloat(3.0f), 3);

  EXPECT_EQ(RoundingFuncFloat(-0.0f), 0);
  EXPECT_EQ(RoundingFuncFloat(std::nextafter(-0.0f, -1)), 0);
  EXPECT_EQ(RoundingFuncFloat(-0.25f), 0);
  EXPECT_EQ(RoundingFuncFloat(-0.75f), -1);
  EXPECT_EQ(RoundingFuncFloat(std::nextafter(-1.0f, 0)), -1);
  EXPECT_EQ(RoundingFuncFloat(-1.0f), -1);
  EXPECT_EQ(RoundingFuncFloat(std::nextafter(-1.0f, -2)), -1);
  EXPECT_EQ(RoundingFuncFloat(-1.25f), -1);
  EXPECT_EQ(RoundingFuncFloat(-1.75f), -2);
  EXPECT_EQ(RoundingFuncFloat(std::nextafter(-2.0f, -1)), -2);
  EXPECT_EQ(RoundingFuncFloat(-2.0f), -2);
  EXPECT_EQ(RoundingFuncFloat(std::nextafter(-2.0f, -3)), -2);
  EXPECT_EQ(RoundingFuncFloat(-2.25f), -2);
  EXPECT_EQ(RoundingFuncFloat(-2.75f), -3);
  EXPECT_EQ(RoundingFuncFloat(std::nextafter(-3.0f, -2)), -3);
  EXPECT_EQ(RoundingFuncFloat(-3.0f), -3);

  // Other cases are also handled as round() would handle them (for double).
  EXPECT_EQ(RoundingFuncDouble(0.0), 0);
  EXPECT_EQ(RoundingFuncDouble(std::nextafter(0.0, 1)), 0);
  EXPECT_EQ(RoundingFuncDouble(0.25), 0);
  EXPECT_EQ(RoundingFuncDouble(0.75), 1);
  EXPECT_EQ(RoundingFuncDouble(std::nextafter(1.0, 0)), 1);
  EXPECT_EQ(RoundingFuncDouble(1.0), 1);
  EXPECT_EQ(RoundingFuncDouble(std::nextafter(1.0, 2)), 1);
  EXPECT_EQ(RoundingFuncDouble(1.25), 1);
  EXPECT_EQ(RoundingFuncDouble(1.75), 2);
  EXPECT_EQ(RoundingFuncDouble(std::nextafter(2.0, 1)), 2);
  EXPECT_EQ(RoundingFuncDouble(2.0), 2);
  EXPECT_EQ(RoundingFuncDouble(std::nextafter(2.0, 3)), 2);
  EXPECT_EQ(RoundingFuncDouble(2.25), 2);
  EXPECT_EQ(RoundingFuncDouble(2.75), 3);
  EXPECT_EQ(RoundingFuncDouble(std::nextafter(3.0, 2)), 3);
  EXPECT_EQ(RoundingFuncDouble(3.0), 3);

  EXPECT_EQ(RoundingFuncDouble(-0.0), 0);
  EXPECT_EQ(RoundingFuncDouble(std::nextafter(-0.0, -1)), 0);
  EXPECT_EQ(RoundingFuncDouble(-0.25), 0);
  EXPECT_EQ(RoundingFuncDouble(-0.75), -1);
  EXPECT_EQ(RoundingFuncDouble(std::nextafter(-1.0, 0)), -1);
  EXPECT_EQ(RoundingFuncDouble(-1.0), -1);
  EXPECT_EQ(RoundingFuncDouble(std::nextafter(-1.0, -2)), -1);
  EXPECT_EQ(RoundingFuncDouble(-1.25), -1);
  EXPECT_EQ(RoundingFuncDouble(-1.75), -2);
  EXPECT_EQ(RoundingFuncDouble(std::nextafter(-2.0, -1)), -2);
  EXPECT_EQ(RoundingFuncDouble(-2.0), -2);
  EXPECT_EQ(RoundingFuncDouble(std::nextafter(-2.0, -3)), -2);
  EXPECT_EQ(RoundingFuncDouble(-2.25), -2);
  EXPECT_EQ(RoundingFuncDouble(-2.75), -3);
  EXPECT_EQ(RoundingFuncDouble(std::nextafter(-3.0, -2)), -3);
  EXPECT_EQ(RoundingFuncDouble(-3.0), -3);
}

TEST(RoundedOpsTest, RoundToNearestIntegerTiesToEven) {
  RunRoundingTest(
      [](float val) -> int64_t {
        return RoundToNearestIntegerTiesToEven<float>(val);
      },
      [](double val) -> int64_t {
        return RoundToNearestIntegerTiesToEven<double>(val);
      });
}

TEST(RoundedOpsTest, RoundToNearestTiesToEven) {
  RunRoundingTest(
      [](float val) {
        return static_cast<int64_t>(RoundToNearestTiesToEven(val));
      },
      [](double val) {
        return static_cast<int64_t>(RoundToNearestTiesToEven(val));
      });
}

}  // namespace
}  // namespace floatops
}  // namespace util
