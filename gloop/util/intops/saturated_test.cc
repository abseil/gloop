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

#include "gloop/util/intops/saturated.h"

#include <cstdint>
#include <limits>
#include <ostream>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace util_intops {

template <typename T>
void PrintTo(const Saturated<T>& s, std::ostream* os) {
  *os << "{value: " << +s.Value() << ", overflow: " << s.DidOverflow()
      << ", underflow: " << s.DidUnderflow()
      << ", indeterminate: " << s.DidBecomeIndeterminate() << "}";
}

namespace {

template <typename T>
class SaturatedTest : public ::testing::Test {};

using SaturatedTypes = ::testing::Types<uint8_t, uint16_t, uint32_t, uint64_t,
                                        int8_t, int16_t, int32_t, int64_t>;
TYPED_TEST_SUITE(SaturatedTest, SaturatedTypes);

TYPED_TEST(SaturatedTest, MinMax) {
  EXPECT_EQ(Saturated<TypeParam>::Min(), std::numeric_limits<TypeParam>::min());
  EXPECT_EQ(Saturated<TypeParam>::Max(), std::numeric_limits<TypeParam>::max());
}

TYPED_TEST(SaturatedTest, Construction) {
  using T = TypeParam;
  Saturated<T> t0;
  EXPECT_EQ(t0.Value(), T());
  EXPECT_FALSE(t0.DidOverflow());
  EXPECT_FALSE(t0.DidUnderflow());
  EXPECT_FALSE(t0.DidBecomeIndeterminate());

  T t_default = T();
  Saturated<T> t1(t_default);
  EXPECT_EQ(t1.Value(), T());
  EXPECT_FALSE(t1.DidOverflow());
  EXPECT_FALSE(t1.DidUnderflow());
  EXPECT_FALSE(t1.DidBecomeIndeterminate());

  Saturated<T> t2(0);
  EXPECT_EQ(t2.Value(), 0);
  EXPECT_FALSE(t2.DidOverflow());
  EXPECT_FALSE(t2.DidUnderflow());
  EXPECT_FALSE(t2.DidBecomeIndeterminate());

  Saturated<T> t3(std::numeric_limits<T>::min());
  EXPECT_EQ(t3.Value(), t3.Min());
  EXPECT_FALSE(t3.DidOverflow());
  EXPECT_FALSE(t3.DidUnderflow());
  EXPECT_FALSE(t3.DidBecomeIndeterminate());

  Saturated<T> t4(std::numeric_limits<T>::max());
  EXPECT_EQ(t4.Value(), t4.Max());
  EXPECT_FALSE(t4.DidOverflow());
  EXPECT_FALSE(t4.DidUnderflow());
  EXPECT_FALSE(t4.DidBecomeIndeterminate());
}

TYPED_TEST(SaturatedTest, Addition) {
  using T = TypeParam;
  Saturated<T> t(0);
  t.Add(t.Max() - 1);
  t.Add(1);
  EXPECT_EQ(t.Value(), t.Max());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());

  t.Add(1);
  EXPECT_EQ(t.Value(), t.Max());
  EXPECT_TRUE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());

  t = Saturated<T>(0);
  t.Add(t.Min());
  EXPECT_EQ(t.Value(), t.Min());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());

  if (t.Min() < 0) {
    t.Add(-1);
    EXPECT_EQ(t.Value(), t.Min());
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_TRUE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());
  }
}

TYPED_TEST(SaturatedTest, Subtraction) {
  using T = TypeParam;
  Saturated<T> t(0);
  t.Sub(0);
  EXPECT_EQ(t.Value(), 0);
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());

  t.Add(t.Min());
  EXPECT_EQ(t.Value(), t.Min());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());

  t.Sub(1);
  EXPECT_EQ(t.Value(), t.Min());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_TRUE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());

  t = Saturated<T>(t.Max());
  if (t.Min() < 0) {
    t.Sub(-1);
    EXPECT_EQ(t.Value(), t.Max());
    EXPECT_TRUE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());
  }
}

TYPED_TEST(SaturatedTest, Multiplication) {
  using T = TypeParam;
  Saturated<T> t;

  // Zero result.

  t = Saturated<T>(0);
  t.Mul(0);
  EXPECT_EQ(t.Value(), 0);
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());

  t = Saturated<T>(1);
  t.Mul(0);
  EXPECT_EQ(t.Value(), 0);
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());

  t = Saturated<T>(0);
  t.Mul(1);
  EXPECT_EQ(t.Value(), 0);
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());

  // Positive and negative results.
  // Assumes range of T is at least [0, 6].

  t = Saturated<T>(2);
  t.Mul(3);
  EXPECT_EQ(t.Value(), 6);
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());

  if (t.Min() < 0) {
    t = Saturated<T>(2);
    t.Mul(-3);
    EXPECT_EQ(t.Value(), -6);
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());

    t = Saturated<T>(-2);
    t.Mul(3);
    EXPECT_EQ(t.Value(), -6);
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());

    t = Saturated<T>(-2);
    t.Mul(-3);
    EXPECT_EQ(t.Value(), 6);
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());
  }

  // Overflow.
  t = Saturated<T>(t.Max());
  t.Mul(2);
  EXPECT_EQ(t.Value(), t.Max());
  EXPECT_TRUE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());

  if (t.Min() < 0) {
    // Underflow
    t = Saturated<T>(t.Min());
    t.Mul(2);
    EXPECT_EQ(t.Value(), t.Min());
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_TRUE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());
  }

  if (t.Min() < 0) {
    // Asymmetric multiplication by -1.
    // Twos complement integers will execute the first test and not the second.
    if (t.Min() + t.Max() < 0) {
      t = Saturated<T>(t.Min());
      t.Mul(-1);
      EXPECT_EQ(t.Value(), t.Max());
      EXPECT_TRUE(t.DidOverflow());
      EXPECT_FALSE(t.DidUnderflow());
      EXPECT_FALSE(t.DidBecomeIndeterminate());
    }

    if (t.Min() + t.Max() > 0) {
      t = Saturated<T>(t.Max());
      t.Mul(-1);
      EXPECT_EQ(t.Value(), t.Min());
      EXPECT_FALSE(t.DidOverflow());
      EXPECT_TRUE(t.DidUnderflow());
      EXPECT_FALSE(t.DidBecomeIndeterminate());
    }
  }
}

TYPED_TEST(SaturatedTest, DivisionRemainder) {
  using T = TypeParam;
  Saturated<T> t;

  // Division / remainder by zero.

  if (t.Min() < 0) {
    t = Saturated<T>(-1);
    t.Div(0);
    EXPECT_EQ(t.Value(), t.Min());
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_TRUE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());

    t = Saturated<T>(-1);
    t.Rem(0);
    EXPECT_EQ(t.Value(), T());
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_TRUE(t.DidBecomeIndeterminate());
  }

  t = Saturated<T>(0);
  t.Div(0);
  EXPECT_EQ(t.Value(), T());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_TRUE(t.DidBecomeIndeterminate());

  t = Saturated<T>(0);
  t.Rem(0);
  EXPECT_EQ(t.Value(), T());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_TRUE(t.DidBecomeIndeterminate());

  if (t.Max() > 0) {
    t = Saturated<T>(1);
    t.Div(0);
    EXPECT_EQ(t.Value(), t.Max());
    EXPECT_TRUE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());

    t = Saturated<T>(1);
    t.Rem(0);
    EXPECT_EQ(t.Value(), T());
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_TRUE(t.DidBecomeIndeterminate());
  }

  // Simple non-zero division.
  if (t.Min() < 0 && t.Max() > 0) {
    t = Saturated<T>(-1);
    t.Div(-1);
    EXPECT_EQ(t.Value(), 1);
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());

    t = Saturated<T>(-1);
    t.Rem(-1);
    EXPECT_EQ(t.Value(), 0);
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());

    //

    t = Saturated<T>(-1);
    t.Div(1);
    EXPECT_EQ(t.Value(), -1);
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());

    t = Saturated<T>(-1);
    t.Rem(1);
    EXPECT_EQ(t.Value(), 0);
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());

    //

    t = Saturated<T>(1);
    t.Div(-1);
    EXPECT_EQ(t.Value(), -1);
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());

    t = Saturated<T>(1);
    t.Rem(-1);
    EXPECT_EQ(t.Value(), 0);
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());
  }

  if (t.Max() > 0) {
    t = Saturated<T>(1);
    t.Div(1);
    EXPECT_EQ(t.Value(), 1);
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());

    t = Saturated<T>(1);
    t.Rem(1);
    EXPECT_EQ(t.Value(), 0);
    EXPECT_FALSE(t.DidOverflow());
    EXPECT_FALSE(t.DidUnderflow());
    EXPECT_FALSE(t.DidBecomeIndeterminate());
  }
}

TYPED_TEST(SaturatedTest, Status) {
  using T = TypeParam;
  // These tests are chained to test status stickiness.
  Saturated<T> t(0);
  EXPECT_EQ(t.Value(), 0);
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());
  EXPECT_TRUE(t.IsValid());

  t.Add(t.Max());
  t.Add(1);
  EXPECT_EQ(t.Value(), t.Max());
  EXPECT_TRUE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());
  EXPECT_FALSE(t.IsValid());

  t.Sub(t.Max());
  EXPECT_EQ(t.Value(), t.Max());  // no change
  EXPECT_TRUE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());
  EXPECT_FALSE(t.IsValid());

  t.ResetStatus();
  t.Sub(t.Max());
  EXPECT_EQ(t.Value(), 0);
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());
  EXPECT_TRUE(t.IsValid());

  t.Add(t.Min());
  EXPECT_EQ(t.Value(), t.Min());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());
  EXPECT_TRUE(t.IsValid());

  t.Sub(1);
  EXPECT_EQ(t.Value(), t.Min());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_TRUE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());
  EXPECT_FALSE(t.IsValid());

  t.Sub(t.Min());
  EXPECT_EQ(t.Value(), t.Min());  // no change
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_TRUE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());
  EXPECT_FALSE(t.IsValid());

  t.ResetStatus();
  t.Sub(t.Min());
  t.Div(0);
  EXPECT_EQ(t.Value(), T());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_TRUE(t.DidBecomeIndeterminate());
  EXPECT_FALSE(t.IsValid());

  t.ResetStatus();
  EXPECT_EQ(t.Value(), T());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());
  EXPECT_TRUE(t.IsValid());
}

TYPED_TEST(SaturatedTest, Chaining) {
  using T = TypeParam;
  EXPECT_EQ(Saturated<T>(1).Add(9).Value(), 10);
  EXPECT_EQ(Saturated<T>(1).Add(9).Mul(7).Value(), 70);
  EXPECT_EQ(Saturated<T>(1).Add(9).Mul(7).Div(2).Value(), 35);
  EXPECT_EQ(Saturated<T>(1).Add(9).Mul(7).Div(2).Sub(1).Value(), 34);
  EXPECT_EQ(Saturated<T>(1).Add(9).Mul(7).Div(2).Sub(1).Rem(10).Value(), 4);

  Saturated<T> t(1);
  // Overflow sticks after Add(t.Max()).
  t.Add(t.Max()).Sub(1);
  EXPECT_EQ(t.Value(), t.Max());
  EXPECT_TRUE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_FALSE(t.DidBecomeIndeterminate());
  EXPECT_FALSE(t.IsValid());

  t.ResetStatus();
  t.Sub(t.Value());
  // Indeterminate sticks after Div(0).
  t.Add(3).Mul(10).Rem(6).Div(0).Add(3);
  EXPECT_EQ(t.Value(), T());
  EXPECT_FALSE(t.DidOverflow());
  EXPECT_FALSE(t.DidUnderflow());
  EXPECT_TRUE(t.DidBecomeIndeterminate());
  EXPECT_FALSE(t.IsValid());
}

// Test ALL the values!
// OP(T,T) must fit in a TBIG.

MATCHER_P3(MatchesSaturated, tb_expect, tb_min, tb_max, "") {
  if (tb_expect < tb_min) {
    if (arg.Value() != arg.Min() || !arg.DidUnderflow() || arg.DidOverflow() ||
        arg.DidBecomeIndeterminate()) {
      *result_listener << "expected underflow with value " << +arg.Min()
                       << ", but got value " << +arg.Value()
                       << " (overflow=" << arg.DidOverflow()
                       << ", underflow=" << arg.DidUnderflow()
                       << ", indeterminate=" << arg.DidBecomeIndeterminate()
                       << ")";
      return false;
    }
    return true;
  }
  if (tb_expect > tb_max) {
    if (arg.Value() != arg.Max() || arg.DidUnderflow() || !arg.DidOverflow() ||
        arg.DidBecomeIndeterminate()) {
      *result_listener << "expected overflow with value " << +arg.Max()
                       << ", but got value " << +arg.Value()
                       << " (overflow=" << arg.DidOverflow()
                       << ", underflow=" << arg.DidUnderflow()
                       << ", indeterminate=" << arg.DidBecomeIndeterminate()
                       << ")";
      return false;
    }
    return true;
  }
  if (arg.Value() != tb_expect || arg.DidUnderflow() || arg.DidOverflow() ||
      arg.DidBecomeIndeterminate()) {
    *result_listener << "expected value " << +tb_expect
                     << " without overflow/underflow, but got value "
                     << +arg.Value() << " (overflow=" << arg.DidOverflow()
                     << ", underflow=" << arg.DidUnderflow()
                     << ", indeterminate=" << arg.DidBecomeIndeterminate()
                     << ")";
    return false;
  }
  return true;
}

template <typename T>
class SaturatedTotalTest : public ::testing::Test {};

using SaturatedTotalTypes = ::testing::Types<uint8_t, int8_t>;
TYPED_TEST_SUITE(SaturatedTotalTest, SaturatedTotalTypes);

TYPED_TEST(SaturatedTotalTest, ExhaustiveOperations) {
  using T = TypeParam;
  using TBIG = int32_t;
  const TBIG tb_min = std::numeric_limits<T>::min();
  const TBIG tb_max = std::numeric_limits<T>::max();
  for (TBIG tb1 = tb_min; tb1 <= tb_max; ++tb1) {
    for (TBIG tb2 = tb_min; tb2 <= tb_max; ++tb2) {
      SCOPED_TRACE(::testing::Message() << "tb1=" << tb1 << ", tb2=" << tb2);
      T t1 = static_cast<T>(tb1);
      T t2 = static_cast<T>(tb2);
      Saturated<T> v1;

      // Addition.
      v1 = Saturated<T>(t1);
      v1.Add(t2);
      EXPECT_THAT(v1, MatchesSaturated(tb1 + tb2, tb_min, tb_max));

      // Subtraction.
      v1 = Saturated<T>(t1);
      v1.Sub(t2);
      EXPECT_THAT(v1, MatchesSaturated(tb1 - tb2, tb_min, tb_max));

      // Multiplication.
      v1 = Saturated<T>(t1);
      v1.Mul(t2);
      EXPECT_THAT(v1, MatchesSaturated(tb1 * tb2, tb_min, tb_max));

      // Division.
      v1 = Saturated<T>(t1);
      v1.Div(t2);
      if (tb2 == 0) {
        EXPECT_EQ(v1.Value(), (tb1 < 0) ? tb_min : (tb1 > 0) ? tb_max : T());
        EXPECT_EQ(v1.DidUnderflow(), tb1 < 0);
        EXPECT_EQ(v1.DidOverflow(), tb1 > 0);
        EXPECT_EQ(v1.DidBecomeIndeterminate(), tb1 == 0);
      } else {
        EXPECT_THAT(v1, MatchesSaturated(tb1 / tb2, tb_min, tb_max));
      }
      // For Remainder.
      const bool division_failed =
          v1.DidUnderflow() || v1.DidOverflow() || v1.DidBecomeIndeterminate();

      // Remainder.
      v1 = Saturated<T>(t1);
      v1.Rem(t2);
      if (division_failed || tb2 == 0) {
        EXPECT_EQ(v1.Value(), T());
        EXPECT_FALSE(v1.DidUnderflow());
        EXPECT_FALSE(v1.DidOverflow());
        EXPECT_TRUE(v1.DidBecomeIndeterminate());
      } else {
        EXPECT_THAT(v1, MatchesSaturated(tb1 % tb2, tb_min, tb_max));
      }
    }
  }
}

}  // anonymous namespace
}  // namespace util_intops
