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

//

#include "gloop/util/math/mathlimits.h"

#include <limits>

#include "absl/log/log.h"
#include "gtest/gtest.h"

template <typename Type, typename TypeTwo, typename TypeThree>
static void TestFPMathLimits() {
  constexpr Type kNaN = std::numeric_limits<Type>::quiet_NaN();
  constexpr Type kPosInf = std::numeric_limits<Type>::infinity();
  constexpr Type kNegInf = -std::numeric_limits<Type>::infinity();

  LOG(INFO) << "Size = " << sizeof(Type);
  LOG(INFO) << "kNaN = " << kNaN;
  LOG(INFO) << "kPosInf = " << kPosInf;
  LOG(INFO) << "kNegInf = " << kNegInf;

  // Special value compatibility:

  EXPECT_TRUE(MathLimits<TypeTwo>::IsPosInf(kPosInf));
  EXPECT_TRUE(MathLimits<TypeThree>::IsPosInf(kPosInf));

  // Special values and operations over them:

  EXPECT_TRUE(MathLimits<Type>::IsFinite(0));
  EXPECT_TRUE(MathLimits<Type>::IsFinite(1.1));
  EXPECT_TRUE(MathLimits<Type>::IsFinite(-1.1));
  EXPECT_FALSE(MathLimits<Type>::IsFinite(kNaN));
  EXPECT_FALSE(MathLimits<Type>::IsFinite(kPosInf));
  EXPECT_FALSE(MathLimits<Type>::IsFinite(kNegInf));

  EXPECT_FALSE(MathLimits<Type>::IsInf(0));
  EXPECT_FALSE(MathLimits<Type>::IsInf(1.1));
  EXPECT_FALSE(MathLimits<Type>::IsInf(-1.1));
  EXPECT_FALSE(MathLimits<Type>::IsInf(kNaN));
  EXPECT_TRUE(MathLimits<Type>::IsInf(kPosInf));
  EXPECT_TRUE(MathLimits<Type>::IsInf(kNegInf));

  EXPECT_FALSE(MathLimits<Type>::IsPosInf(0));
  EXPECT_FALSE(MathLimits<Type>::IsPosInf(1.1));
  EXPECT_FALSE(MathLimits<Type>::IsPosInf(-1.1));
  EXPECT_FALSE(MathLimits<Type>::IsPosInf(kNaN));
  EXPECT_TRUE(MathLimits<Type>::IsPosInf(kPosInf));
  EXPECT_FALSE(MathLimits<Type>::IsPosInf(kNegInf));

  EXPECT_TRUE(MathLimits<Type>::IsPosInf(kPosInf + 1));
  EXPECT_TRUE(MathLimits<Type>::IsPosInf(kPosInf - 1e30));
  EXPECT_TRUE(MathLimits<Type>::IsPosInf(kPosInf + kPosInf));
  EXPECT_TRUE(MathLimits<Type>::IsPosInf(kPosInf * kPosInf));
  EXPECT_TRUE(MathLimits<Type>::IsPosInf(kPosInf - kNegInf));
  EXPECT_TRUE(MathLimits<Type>::IsPosInf(kNegInf * kNegInf));

  EXPECT_NE(kNaN, 0);
  EXPECT_NE(kNaN, 1);
  EXPECT_NE(kNaN, kNegInf);
  EXPECT_NE(kNaN, kPosInf);
  EXPECT_NE(kNaN, kNaN);
  EXPECT_FALSE(kNaN < 0);  // NOLINT
  EXPECT_FALSE(kNaN > 0);  // NOLINT

  EXPECT_NE(kPosInf, 0);
  EXPECT_NE(kPosInf, 1);
  EXPECT_NE(kPosInf, kNegInf);
  EXPECT_FALSE(kPosInf < 0);  // NOLINT
  EXPECT_GT(kPosInf, 0);

  EXPECT_NE(kNegInf, 0);
  EXPECT_NE(kNegInf, 1);
  EXPECT_LT(kNegInf, 0);
  EXPECT_LE(kNegInf, 0);  // NOLINT
}

TEST(MathLimits, FPMathLimits) {
  TestFPMathLimits<float, double, long double>();
  TestFPMathLimits<double, float, long double>();
  TestFPMathLimits<long double, float, double>();
}
