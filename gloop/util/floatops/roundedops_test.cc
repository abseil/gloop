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

#include "gtest/gtest.h"

using util::floatops::kDownward;
using util::floatops::kToNearest;
using util::floatops::kTowardZero;
using util::floatops::kUpward;
using util::floatops::RoundedAdd;
using util::floatops::RoundedDivide;
using util::floatops::RoundedMultiply;
using util::floatops::RoundedSubtract;
using util::floatops::RoundToNearestIntegerTiesToEven;
using util::floatops::RoundToNearestTiesToEven;

namespace {

TEST(RoundedOpsTest, RoundedAddSubtractWorks) {
  // Epsilon is the smallest e > 0, such that 1 + e != 1. To test rounding,
  // add something just a little less than epsilon to 1.0. Floating point
  // values are twice as dense just below 1.0 as they are just above 1.0,
  // so use half that value there.
  double EpsPoint75 = std::numeric_limits<double>::epsilon() * 0.75;
  double EpsPoint375 = std::numeric_limits<double>::epsilon() * 0.375;

  EXPECT_LT(1.0, RoundedAdd<kUpward>(1.0, EpsPoint75));
  EXPECT_EQ(1.0, RoundedAdd<kDownward>(1.0, EpsPoint75));
  EXPECT_EQ(1.0, RoundedAdd<kTowardZero>(1.0, EpsPoint75));
  EXPECT_LT(1.0, RoundedAdd<kToNearest>(1.0, EpsPoint75));
  EXPECT_EQ(1.0, RoundedAdd<kToNearest>(1.0, EpsPoint375));

  EXPECT_EQ(1.0, RoundedSubtract<kUpward>(1.0, EpsPoint375));
  EXPECT_GT(1.0, RoundedSubtract<kDownward>(1.0, EpsPoint375));
  EXPECT_GT(1.0, RoundedSubtract<kTowardZero>(1.0, EpsPoint375));
  EXPECT_GT(1.0, RoundedSubtract<kToNearest>(1.0, EpsPoint375));

  EXPECT_LT(-1.0, RoundedAdd<kUpward>(-1.0, EpsPoint375));
  EXPECT_EQ(-1.0, RoundedAdd<kDownward>(-1.0, EpsPoint375));
  EXPECT_LT(-1.0, RoundedAdd<kTowardZero>(-1.0, EpsPoint375));
  EXPECT_LT(-1.0, RoundedAdd<kToNearest>(-1.0, EpsPoint375));

  EXPECT_EQ(-1.0, RoundedSubtract<kUpward>(-1.0, EpsPoint75));
  EXPECT_GT(-1.0, RoundedSubtract<kDownward>(-1.0, EpsPoint75));
  EXPECT_EQ(-1.0, RoundedSubtract<kTowardZero>(-1.0, EpsPoint75));
  EXPECT_GT(-1.0, RoundedSubtract<kToNearest>(-1.0, EpsPoint75));
  EXPECT_EQ(-1.0, RoundedSubtract<kToNearest>(-1.0, EpsPoint375));
}

TEST(RoundedOpsTest, RoundedDivideDownUpWorks) {
  float FloatOneThirdDown = RoundedDivide<kDownward>(1.0f, 3.0f);
  float FloatOneThirdUp = RoundedDivide<kUpward>(1.0f, 3.0f);

  double DoubleOneThirdDown = RoundedDivide<kDownward>(1.0, 3.0);
  double DoubleOneThirdUp = RoundedDivide<kUpward>(1.0, 3.0);

  EXPECT_LT(0.333333, FloatOneThirdDown);
  EXPECT_LT(FloatOneThirdDown, DoubleOneThirdDown);
  EXPECT_LT(DoubleOneThirdDown, DoubleOneThirdUp);
  EXPECT_LT(DoubleOneThirdUp, FloatOneThirdUp);
  EXPECT_LT(FloatOneThirdUp, 0.333334);
}

TEST(RoundedOpsTest, RoundedDivideTowardZeroWorks) {
  EXPECT_EQ(RoundedDivide<kDownward>(1.0f, 3.0f),
            RoundedDivide<kTowardZero>(1.0f, 3.0f));
  EXPECT_EQ(RoundedDivide<kDownward>(1.0, 3.0),
            RoundedDivide<kTowardZero>(1.0, 3.0));

  EXPECT_EQ(RoundedDivide<kUpward>(-1.0f, 3.0f),
            RoundedDivide<kTowardZero>(-1.0f, 3.0f));
  EXPECT_EQ(RoundedDivide<kUpward>(-1.0, 3.0),
            RoundedDivide<kTowardZero>(-1.0, 3.0));

  EXPECT_EQ(RoundedDivide<kUpward>(1.0f, -3.0f),
            RoundedDivide<kTowardZero>(1.0f, -3.0f));
  EXPECT_EQ(RoundedDivide<kUpward>(1.0, -3.0),
            RoundedDivide<kTowardZero>(1.0, -3.0));
}

TEST(RoundedOpsTest, RoundedDivideToNearestWorks) {
  float FloatOneThirdToNearest = RoundedDivide<kToNearest>(1.0f, 3.0f);
  EXPECT_TRUE(FloatOneThirdToNearest == RoundedDivide<kUpward>(1.0f, 3.0f) ||
              FloatOneThirdToNearest == RoundedDivide<kDownward>(1.0f, 3.0f));

  double DoubleOneThirdToNearest = RoundedDivide<kToNearest>(1.0f, 3.0f);
  EXPECT_TRUE(DoubleOneThirdToNearest == RoundedDivide<kUpward>(1.0f, 3.0f) ||
              DoubleOneThirdToNearest == RoundedDivide<kDownward>(1.0f, 3.f));
}

TEST(RoundedOpsTest, RoundedMultiplyWorks) {
  float OneThirdDown = RoundedDivide<kUpward>(1.0f, 3.0f);

  EXPECT_EQ(RoundedMultiply<kDownward>(OneThirdDown, 3.0f), 1.0);
  EXPECT_GT(RoundedMultiply<kUpward>(OneThirdDown, 3.0f), 1.0);
  EXPECT_EQ(RoundedMultiply<kToNearest>(OneThirdDown, 3.0f), 1.0);
  EXPECT_EQ(RoundedMultiply<kTowardZero>(OneThirdDown, 3.0f), 1.0);
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
