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

//
// Unit test cases for SafeInt.  Some of this overlaps with the testing for
// StrongInt, but it's important to test not only that SafeInt fails when
// expected, but that it passes when expected.

#include "gloop/util/intops/safe_int.h"

#include <climits>
#include <cstdint>
#include <limits>
#include <string>

#include "gtest/gtest.h"

namespace util_intops {
namespace {

DEFINE_SAFE_INT_TYPE(SafeInt8, int8_t, ::util_intops::LogFatalOnError);
DEFINE_SAFE_INT_TYPE(SafeUInt8, uint8_t, ::util_intops::LogFatalOnError);
DEFINE_SAFE_INT_TYPE(SafeInt16, int16_t, ::util_intops::LogFatalOnError);
DEFINE_SAFE_INT_TYPE(SafeUInt16, uint16_t, ::util_intops::LogFatalOnError);
DEFINE_SAFE_INT_TYPE(SafeInt32, int32_t, ::util_intops::LogFatalOnError);
DEFINE_SAFE_INT_TYPE(SafeInt64, int64_t, ::util_intops::LogFatalOnError);
DEFINE_SAFE_INT_TYPE(SafeUInt32, uint32_t, ::util_intops::LogFatalOnError);
DEFINE_SAFE_INT_TYPE(SafeUInt64, uint64_t, ::util_intops::LogFatalOnError);

//
// Test cases that apply to signed and unsigned types equally.
//

template <typename T>
class SignNeutralSafeIntTest : public ::testing::Test {
 public:
  using SafeIntTypeUnderTest = T;
};

using AllSafeIntTypes =
    ::testing::Types<SafeInt8, SafeUInt8, SafeInt16, SafeUInt16, SafeInt32,
                     SafeUInt32, SafeInt64, SafeUInt64>;

class SafeIntTypeNames {
 public:
  template <typename T>
  static std::string GetName(int) {
    return std::string{T::TypeName()};
  }
};

TYPED_TEST_SUITE(SignNeutralSafeIntTest, AllSafeIntTypes, SafeIntTypeNames);

TYPED_TEST(SignNeutralSafeIntTest, TestCtors) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  {  // Test default construction.
    T x;
    EXPECT_EQ(x.value(), V());
  }

  {  // Test construction from a value.
    T x(93);
    EXPECT_EQ(x.value(), V(93));
  }

  {  // Test copy construction.
    T x(76);
    T y(x);
    EXPECT_EQ(y.value(), V(76));
  }
}

TYPED_TEST(SignNeutralSafeIntTest, TestUnaryOperators) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  {  // Test unary plus of positive values.
    T x(123);
    EXPECT_EQ((+x).value(), V(123));
  }
  {  // Test logical not of positive values.
    T x(123);
    EXPECT_FALSE(!x);
    EXPECT_TRUE(!!x);
  }
  {  // Test logical not of zero.
    T x(0);
    EXPECT_TRUE(!x);
    EXPECT_FALSE(!!x);
  }
}

TYPED_TEST(SignNeutralSafeIntTest, TestCtorFailures) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  {  // Test out-of-bounds construction.
    if (std::numeric_limits<V>::is_signed || sizeof(V) < sizeof(uint64_t)) {
      EXPECT_DEATH((T(std::numeric_limits<uint64_t>::max())), "bounds");
    }
  }
  {  // Test out-of-bounds construction from float.
    EXPECT_DEATH((T(std::numeric_limits<float>::max())), "bounds");
    EXPECT_DEATH((T(-std::numeric_limits<float>::max())), "bounds");
  }
  {  // Test out-of-bounds construction from double.
    EXPECT_DEATH((T(std::numeric_limits<double>::max())), "bounds");
    EXPECT_DEATH((T(-std::numeric_limits<double>::max())), "bounds");
  }
  {  // Test out-of-bounds construction from long double.
    EXPECT_DEATH((T(std::numeric_limits<long double>::max())), "bounds");
    EXPECT_DEATH((T(-std::numeric_limits<long double>::max())), "bounds");
  }
}

TYPED_TEST(SignNeutralSafeIntTest, TestIncrementDecrement) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  {  // Test simple increments and decrements.
    T x(0);
    EXPECT_EQ(x.value(), V(0));
    EXPECT_EQ((x++).value(), V(0));
    EXPECT_EQ(x.value(), V(1));
    EXPECT_EQ((++x).value(), V(2));
    EXPECT_EQ(x.value(), V(2));
    EXPECT_EQ((x--).value(), V(2));
    EXPECT_EQ(x.value(), V(1));
    EXPECT_EQ((--x).value(), V(0));
    EXPECT_EQ(x.value(), V(0));
  }
}

TYPED_TEST(SignNeutralSafeIntTest, TestIncrementDecrementFailures) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  {  // Test overflowing increment.
    T x(std::numeric_limits<V>::max() - 1);
    EXPECT_EQ((++x).value(), std::numeric_limits<V>::max());
    EXPECT_DEATH(x++, "overflow");
    EXPECT_DEATH(++x, "overflow");
  }
  {  // Test underflowing decrement.
    T x(std::numeric_limits<V>::min() + 1);
    EXPECT_EQ((--x).value(), std::numeric_limits<V>::min());
    EXPECT_DEATH(x--, "underflow");
    EXPECT_DEATH(--x, "underflow");
  }
}

#define TEST_T_OP_T(xval, op, yval)            \
  {                                            \
    T x(xval);                                 \
    T y(yval);                                 \
    V expected = x.value() op y.value();       \
    EXPECT_EQ((x op y).value(), expected);     \
    EXPECT_EQ((x op## = y).value(), expected); \
    EXPECT_EQ(x.value(), expected);            \
  }

TYPED_TEST(SignNeutralSafeIntTest, TestAdd) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  // Test positive vs. positive addition.
  TEST_T_OP_T(9, +, 3)
  // Test addition by zero.
  TEST_T_OP_T(93, +, 0);
}

TYPED_TEST(SignNeutralSafeIntTest, TestAddFailures) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  {  // Test overflowing addition.
    T x(std::numeric_limits<V>::max());
    EXPECT_DEATH(x + T(1), "overflow");
    EXPECT_DEATH(x += T(1), "overflow");
  }
  {  // Test overflowing addition.
    T x(std::numeric_limits<V>::max());
    EXPECT_DEATH(x + T(std::numeric_limits<V>::max()), "overflow");
    EXPECT_DEATH(x += T(std::numeric_limits<V>::max()), "overflow");
  }
}

TYPED_TEST(SignNeutralSafeIntTest, TestSubtract) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  // Test positive vs. positive subtraction.
  TEST_T_OP_T(9, -, 3)
  // Test subtraction of zero.
  TEST_T_OP_T(93, -, 0);
}

TYPED_TEST(SignNeutralSafeIntTest, TestSubtractFailures) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  {  // Test underflowing subtraction.
    T x(std::numeric_limits<V>::min());
    EXPECT_DEATH(x - T(1), "underflow");
    EXPECT_DEATH(x -= T(1), "underflow");
  }
  {  // Test underflowing subtraction.
    T x(std::numeric_limits<V>::min());
    EXPECT_DEATH(x - T(std::numeric_limits<V>::max()), "underflow");
    EXPECT_DEATH(x -= T(std::numeric_limits<V>::max()), "underflow");
  }
}

#define TEST_T_OP_NUM(xval, op, numtype, yval) \
  {                                            \
    T x(xval);                                 \
    numtype y = yval;                          \
    V expected = x.value() op y;               \
    EXPECT_EQ((x op y).value(), expected);     \
    EXPECT_EQ((x op## = y).value(), expected); \
    EXPECT_EQ(x.value(), expected);            \
  }

TYPED_TEST(SignNeutralSafeIntTest, TestMultiply) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  // Test positive vs. positive multiplication across types.
  TEST_T_OP_NUM(9, *, int32_t, 3);
  TEST_T_OP_NUM(9, *, uint32_t, 3);
  TEST_T_OP_NUM(9, *, float, 3);
  TEST_T_OP_NUM(9, *, double, 3);

  // Test positive vs. zero multiplication commutatively across types.  This
  // was a real bug.
  TEST_T_OP_NUM(93, *, int32_t, 0);
  TEST_T_OP_NUM(93, *, uint32_t, 0);
  TEST_T_OP_NUM(93, *, float, 0);
  TEST_T_OP_NUM(93, *, double, 0);

  TEST_T_OP_NUM(0, *, int32_t, 76);
  TEST_T_OP_NUM(0, *, uint32_t, 76);
  TEST_T_OP_NUM(0, *, float, 76);
  TEST_T_OP_NUM(0, *, double, 76);

  // Test positive vs. epsilon multiplication.
  TEST_T_OP_NUM(93, *, float, std::numeric_limits<float>::epsilon());
  TEST_T_OP_NUM(93, *, double, std::numeric_limits<float>::epsilon());

  {  // Test multiplication by float.
     // Multiplication is the only operator that takes one numeric type and
     // one StrongInt type *and* is commutative.  This was a real bug.
    T x(0);
    EXPECT_EQ((x * static_cast<float>(1.1)).value(), 0);
    EXPECT_EQ((static_cast<float>(1.1) * x).value(), 0);
  }
}

TYPED_TEST(SignNeutralSafeIntTest, TestMultiplyFailures) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  {  // Test overflowing multiplication.
    T x(std::numeric_limits<V>::max());
    EXPECT_DEATH(x * 2, "overflow");
    EXPECT_DEATH(x *= 2, "overflow");
  }
}

TYPED_TEST(SignNeutralSafeIntTest, TestDivide) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  // Test positive vs. positive division across types.
  TEST_T_OP_NUM(9, /, int32_t, 3);
  TEST_T_OP_NUM(9, /, uint32_t, 3);
  TEST_T_OP_NUM(9, /, float, 3);
  TEST_T_OP_NUM(9, /, double, 3);

  // Test zero vs. positive division across types.
  TEST_T_OP_NUM(0, /, int32_t, 76);
  TEST_T_OP_NUM(0, /, uint32_t, 76);
  TEST_T_OP_NUM(0, /, float, 76);
  TEST_T_OP_NUM(0, /, double, 76);
}

TYPED_TEST(SignNeutralSafeIntTest, TestDivideFailures) {
  using T = typename TestFixture::SafeIntTypeUnderTest;

  {  // Test divide by zero.
    T x(93);
    EXPECT_DEATH(x / 0, "divide by zero");
    EXPECT_DEATH(x /= 0, "divide by zero");
  }
}

TYPED_TEST(SignNeutralSafeIntTest, TestModulo) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  // Test positive vs. positive modulo across signedness.
  TEST_T_OP_NUM(7, %, int32_t, 6);
  TEST_T_OP_NUM(7, %, uint32_t, 6);

  // Test zero vs. positive modulo across signedness.
  TEST_T_OP_NUM(0, %, int32_t, 6);
  TEST_T_OP_NUM(0, %, uint32_t, 6);
}

TYPED_TEST(SignNeutralSafeIntTest, TestModuloFailures) {
  using T = typename TestFixture::SafeIntTypeUnderTest;

  {  // Test modulo by zero.
    T x(93);
    EXPECT_DEATH(x % 0, "divide by zero");
    EXPECT_DEATH(x %= 0, "divide by zero");
  }
}

TYPED_TEST(SignNeutralSafeIntTest, TestLeftShift) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  // Test basic shift.
  TEST_T_OP_NUM(0x09, <<, int, 3);
  // Test shift by zero.
  TEST_T_OP_NUM(0x09, <<, int, 0);
}

TYPED_TEST(SignNeutralSafeIntTest, TestLeftShiftFailures) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  {  // Test shift by a negative.
    T x(9);
    EXPECT_DEATH(x << -1, "shift by negative");
    EXPECT_DEATH(x <<= -1, "shift by negative");
  }
  {  // Test shift by a too-large.
    T x(9);
    EXPECT_DEATH(x << sizeof(T) * CHAR_BIT, "shift by large");
    EXPECT_DEATH(x <<= sizeof(T) * CHAR_BIT, "shift by large");
    EXPECT_DEATH(x <<= 0x100000001ULL, "shift by large");
  }
  {  // Test overflowing shift.
    T x(std::numeric_limits<V>::max());
    EXPECT_DEATH(x << 1, "overflow");
    EXPECT_DEATH(x <<= 1, "overflow");
  }
}

TYPED_TEST(SignNeutralSafeIntTest, TestRightShift) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  // Test basic shift.
  TEST_T_OP_NUM(0x09, >>, int, 3);
  // Test shift by zero.
  TEST_T_OP_NUM(0x09, >>, int, 0);
}

TYPED_TEST(SignNeutralSafeIntTest, TestRightShiftFailures) {
  using T = typename TestFixture::SafeIntTypeUnderTest;

  {  // Test shift by a negative.
    T x(9);
    EXPECT_DEATH(x >> -1, "shift by negative");
    EXPECT_DEATH(x >>= -1, "shift by negative");
  }
  {  // Test shift by a too-large.
    T x(9);
    EXPECT_DEATH(x >> sizeof(T) * CHAR_BIT, "shift by large");
    EXPECT_DEATH(x >>= sizeof(T) * CHAR_BIT, "shift by large");
  }
}

TYPED_TEST(SignNeutralSafeIntTest, TestFloatToIntTruncation) {
  using T = typename TestFixture::SafeIntTypeUnderTest;

  // Test construction from float.
  {
    float f = 93.123;
    T x(f);
    EXPECT_EQ(x.value(), 93);
  }
  {
    float f = 93.76;
    T x(f);
    EXPECT_EQ(x.value(), 93);
  }
  // Test construction from double.
  {
    double f = 93.123;
    T x(f);
    EXPECT_EQ(x.value(), 93);
  }
  {
    double f = 93.76;
    T x(f);
    EXPECT_EQ(x.value(), 93);
  }
  // Test construction from long double.
  {
    long double f = 93.123;
    T x(f);
    EXPECT_EQ(x.value(), 93);
  }
  {
    long double f = 93.76;
    T x(f);
    EXPECT_EQ(x.value(), 93);
  }
}

TYPED_TEST(SignNeutralSafeIntTest, TestAddConstexpr) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  constexpr T sum = T{12} + T{34};
  EXPECT_EQ(sum, T{12 + 34});
}

TYPED_TEST(SignNeutralSafeIntTest, TestSubtractConstexpr) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  constexpr T diff = T{34} - T{12};
  EXPECT_EQ(diff, T{34 - 12});
}

TYPED_TEST(SignNeutralSafeIntTest, TestMultiplyConstexpr) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  constexpr T product = T{12} * 3;
  EXPECT_EQ(product, T{12 * 3});
}

TYPED_TEST(SignNeutralSafeIntTest, TestDivideConstexpr) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  constexpr T quotient = T{34} / 12;
  EXPECT_EQ(quotient, T{34 / 12});
}

TYPED_TEST(SignNeutralSafeIntTest, TestModuloConstexpr) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  constexpr T remainder = T{34} % 12;
  EXPECT_EQ(remainder, T{34 % 12});
}

TYPED_TEST(SignNeutralSafeIntTest, TestLeftShiftConstexpr) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  constexpr T shifted = T{1} << 3;
  EXPECT_EQ(shifted, T{1 << 3});
}

TYPED_TEST(SignNeutralSafeIntTest, TestRightShiftConstexpr) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  constexpr T shifted = T{0b1111} >> 3;
  EXPECT_EQ(shifted, T{0b1111 >> 3});
}

TYPED_TEST(SignNeutralSafeIntTest, TestUnaryOperatorsConstexpr) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  constexpr T pos = +T{12};
  EXPECT_EQ(pos, T{+12});
}

//
// Test cases that apply only to signed types.
//

template <typename T>
class SignedSafeIntTest : public ::testing::Test {
 public:
  using SafeIntTypeUnderTest = T;

  static constexpr T kTestConstexprIntPos{93};
  static constexpr T kTestConstexprIntNeg{-93};

  static constexpr T kTestConstexprFloatPos{13.47};
  static constexpr T kTestConstexprFloatNeg{-13.47};
};

template <typename T>
constexpr T SignedSafeIntTest<T>::kTestConstexprIntPos;
template <typename T>
constexpr T SignedSafeIntTest<T>::kTestConstexprIntNeg;
template <typename T>
constexpr T SignedSafeIntTest<T>::kTestConstexprFloatPos;
template <typename T>
constexpr T SignedSafeIntTest<T>::kTestConstexprFloatNeg;

using SignedSafeIntTypes =
    ::testing::Types<SafeInt8, SafeInt16, SafeInt32, SafeInt64>;

TYPED_TEST_SUITE(SignedSafeIntTest, SignedSafeIntTypes, SafeIntTypeNames);

TYPED_TEST(SignedSafeIntTest, ConstexprInitWorks) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  EXPECT_EQ(SignedSafeIntTest<T>::kTestConstexprIntPos.value(), 93);
  EXPECT_EQ(SignedSafeIntTest<T>::kTestConstexprIntNeg.value(), -93);
  EXPECT_EQ(SignedSafeIntTest<T>::kTestConstexprFloatPos.value(), 13);
  EXPECT_EQ(SignedSafeIntTest<T>::kTestConstexprFloatNeg.value(), -13);
}

TYPED_TEST(SignedSafeIntTest, TestCtors) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  {  // Test construction from a negative value.
    T x(-1);
    EXPECT_EQ(x.value(), V(-1));
  }
}

TYPED_TEST(SignedSafeIntTest, TestUnaryOperators) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  {  // Test unary plus and minus of positive values.
    T x(123);
    EXPECT_EQ((+x).value(), V(123));
    EXPECT_EQ((-x).value(), V(-123));
  }
  {  // Test unary plus and minus of negative values.
    T x(-123);
    EXPECT_EQ((+x).value(), V(-123));
    EXPECT_EQ((-x).value(), V(123));
  }
  {  // Test logical not of negative values.
    T x(-123);
    EXPECT_FALSE(!x);
    EXPECT_TRUE(!!x);
  }
}

TYPED_TEST(SignedSafeIntTest, TestUnaryOperatorsConstexpr) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  constexpr T pos = +T{123};
  EXPECT_EQ(pos, T{+123});

  constexpr T neg = -T{123};
  EXPECT_EQ(neg, T{-123});
}

TYPED_TEST(SignedSafeIntTest, TestUnaryOperatorsFailures) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  {  // Test unary minus of negative values.
    T y(std::numeric_limits<V>::min());
    EXPECT_DEATH(-y, "overflow");
  }
}

TYPED_TEST(SignedSafeIntTest, TestAdd) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  // Test negative vs. positive addition.
  TEST_T_OP_T(-9, +, 3)
  // Test positive vs. negative addition.
  TEST_T_OP_T(9, +, -3)
  // Test negative vs. negative addition.
  TEST_T_OP_T(-9, +, -3)
}

TYPED_TEST(SignedSafeIntTest, TestAddFailures) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  {  // Test underflow by addition of a negative.
    T x(std::numeric_limits<V>::min());
    EXPECT_DEATH(x + T(-1), "underflow");
    EXPECT_DEATH(x += T(-1), "underflow");
  }
}

TYPED_TEST(SignedSafeIntTest, TestSubtract) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  // Test negative vs. positive subtraction.
  TEST_T_OP_T(-9, -, 3)
  // Test positive vs. negative subtraction.
  TEST_T_OP_T(9, -, -3)
  // Test negative vs. negative subtraction.
  TEST_T_OP_T(-9, -, -3)
  // Test positive vs. positive subtraction resulting in negative.
  TEST_T_OP_T(3, -, 9);
  // Test subtraction from zero.
  TEST_T_OP_T(0, -, 93);
}

TYPED_TEST(SignedSafeIntTest, TestSubtractConstexpr) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  constexpr T diff = T{34} - T{12};
  EXPECT_EQ(diff, T{34 - 12});

  constexpr T diff2 = T{12} - T{34};
  EXPECT_EQ(diff2, T{12 - 34});

  constexpr T diff3 = T{12} - T{-34};
  EXPECT_EQ(diff3, T{12 + 34});

  constexpr T diff4 = T{0} - T{34};
  EXPECT_EQ(diff4, T{-34});
}

TYPED_TEST(SignedSafeIntTest, TestSubtractFailures) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  {  // Test overflow by subtraction of a negative.
    T x(std::numeric_limits<V>::max());
    EXPECT_DEATH(x - T(-1), "overflow");
    EXPECT_DEATH(x -= T(-1), "overflow");
  }
}

TYPED_TEST(SignedSafeIntTest, TestMultiply) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  // Test negative vs. positive multiplication across types.
  TEST_T_OP_NUM(-9, *, int32_t, 3);
  TEST_T_OP_NUM(-9, *, uint32_t, 3);
  TEST_T_OP_NUM(-9, *, float, 3);
  TEST_T_OP_NUM(-9, *, double, 3);
  // Test positive vs. negative multiplication across types.
  TEST_T_OP_NUM(9, *, int32_t, -3);
  // Don't cover unsigneds that are initialized from negative values.
  TEST_T_OP_NUM(9, *, float, -3);
  TEST_T_OP_NUM(9, *, double, -3);
  // Test negative vs. negative multiplication across types.
  TEST_T_OP_NUM(-9, *, int32_t, -3);
  // Don't cover unsigneds that are initialized from negative values.
  TEST_T_OP_NUM(-9, *, float, -3);
  TEST_T_OP_NUM(-9, *, double, -3);

  // Test negative vs. zero multiplication commutatively across types.
  TEST_T_OP_NUM(-93, *, int32_t, 0);
  TEST_T_OP_NUM(-93, *, uint32_t, 0);
  TEST_T_OP_NUM(-93, *, float, 0);
  TEST_T_OP_NUM(-93, *, double, 0);
  TEST_T_OP_NUM(0, *, int32_t, -76);
  TEST_T_OP_NUM(0, *, uint32_t, -76);
  TEST_T_OP_NUM(0, *, float, -76);
  TEST_T_OP_NUM(0, *, double, -76);

  // Test negative vs. epsilon multiplication.
  TEST_T_OP_NUM(-93, *, float, std::numeric_limits<float>::epsilon());
  TEST_T_OP_NUM(-93, *, double, std::numeric_limits<float>::epsilon());
}

TYPED_TEST(SignedSafeIntTest, TestMultiplyFailures) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  {  // Test underflowing multiplication.
    T x(std::numeric_limits<V>::min());
    EXPECT_DEATH(x * 2, "underflow");
    EXPECT_DEATH(x *= 2, "underflow");
  }
  {  // Test underflowing multiplication.
    T x(std::numeric_limits<V>::max());
    EXPECT_DEATH(x * -2, "underflow");
    EXPECT_DEATH(x *= -2, "underflow");
  }
  {  // Test overflowing multiplication.
    T x(std::numeric_limits<V>::min());
    EXPECT_DEATH(x * -2, "overflow");
    EXPECT_DEATH(x *= -2, "overflow");
  }
  {  // Test overflowing multiplication.
    T x(std::numeric_limits<V>::min());
    EXPECT_DEATH(x * -1, "overflow");
    EXPECT_DEATH(x *= -1, "overflow");
  }
  {  // Test underflowing multiplication where rhs type is uint64.
    T x(-2);
    EXPECT_DEATH(x * std::numeric_limits<uint64_t>::max(), "underflow");
    EXPECT_DEATH(x *= std::numeric_limits<uint64_t>::max(), "underflow");
  }
}

TYPED_TEST(SignedSafeIntTest, TestDivide) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  // Test negative vs. positive division across types.
  TEST_T_OP_NUM(-9, /, int32_t, 3);
  TEST_T_OP_NUM(-9, /, uint32_t, 3);
  TEST_T_OP_NUM(-9, /, float, 3);
  TEST_T_OP_NUM(-9, /, double, 3);
  // Test positive vs. negative division across types.
  TEST_T_OP_NUM(9, /, int32_t, -3);
  TEST_T_OP_NUM(9, /, uint32_t, -3);
  TEST_T_OP_NUM(9, /, float, -3);
  TEST_T_OP_NUM(9, /, double, -3);
  // Test negative vs. negative division across types.
  TEST_T_OP_NUM(-9, /, int32_t, -3);
  TEST_T_OP_NUM(-9, /, uint32_t, -3);
  TEST_T_OP_NUM(-9, /, float, -3);
  TEST_T_OP_NUM(-9, /, double, -3);

  // Test zero vs. negative division across types.
  TEST_T_OP_NUM(0, /, int32_t, -76);
  TEST_T_OP_NUM(0, /, uint32_t, -76);
  TEST_T_OP_NUM(0, /, float, -76);
  TEST_T_OP_NUM(0, /, double, -76);
}

TYPED_TEST(SignedSafeIntTest, TestDivideFailures) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  {  // Test overflowing division.
    T x(std::numeric_limits<V>::min());
    EXPECT_DEATH(x / -1, "overflow");
    EXPECT_DEATH(x /= -1, "overflow");
  }
}

TYPED_TEST(SignedSafeIntTest, TestModulo) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  // Test negative vs. positive modulo across signedness.
  TEST_T_OP_NUM(-7, %, int32_t, 6);
  TEST_T_OP_NUM(-7, %, uint32_t, 6);
  // Test positive vs. negative modulo across signedness.
  TEST_T_OP_NUM(7, %, int32_t, -6);
  TEST_T_OP_NUM(7, %, uint32_t, -6);
  // Test negative vs. negative modulo across signedness.
  TEST_T_OP_NUM(-7, %, int32_t, -6);
  TEST_T_OP_NUM(-7, %, uint32_t, -6);

  // Test zero vs. negative modulo across signedness.
  TEST_T_OP_NUM(0, %, int32_t, -6);
  TEST_T_OP_NUM(0, %, uint32_t, -6);
}

TYPED_TEST(SignedSafeIntTest, TestModuloFailures) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  {  // Test overflowing modulo.
    T x(std::numeric_limits<V>::min());
    EXPECT_DEATH(x % -1, "overflow");
    EXPECT_DEATH(x %= -1, "overflow");
  }
}

TYPED_TEST(SignedSafeIntTest, TestLeftShiftFailures) {
  using T = typename TestFixture::SafeIntTypeUnderTest;

  {  // Test shift of a negative.
    T x(-9);
    EXPECT_DEATH(x << 1, "shift of negative");
    EXPECT_DEATH(x <<= 1, "shift of negative");
  }
}

TYPED_TEST(SignedSafeIntTest, TestRightShiftFailures) {
  using T = typename TestFixture::SafeIntTypeUnderTest;

  {  // Test shift of a negative.
    T x(-9);
    EXPECT_DEATH(x >> 1, "shift of negative");
    EXPECT_DEATH(x >>= 1, "shift of negative");
  }
}

//
// Test cases that apply only to unsigned types.
//

template <typename T>
class UnsignedSafeIntTest : public ::testing::Test {
 public:
  using SafeIntTypeUnderTest = T;

  static constexpr T kTestConstexprInt{203};
  static constexpr T kTestConstexprFloat{173.81};
};

template <typename T>
constexpr T UnsignedSafeIntTest<T>::kTestConstexprInt;
template <typename T>
constexpr T UnsignedSafeIntTest<T>::kTestConstexprFloat;

using UnsignedSafeIntTypes =
    ::testing::Types<SafeUInt8, SafeUInt16, SafeUInt32, SafeUInt64>;

TYPED_TEST_SUITE(UnsignedSafeIntTest, UnsignedSafeIntTypes, SafeIntTypeNames);

TYPED_TEST(UnsignedSafeIntTest, ConstexprInitWorks) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  EXPECT_EQ(UnsignedSafeIntTest<T>::kTestConstexprInt.value(), 203);
  EXPECT_EQ(UnsignedSafeIntTest<T>::kTestConstexprFloat.value(), 173);
}

TYPED_TEST(UnsignedSafeIntTest, TestCtors) {
  using T = typename TestFixture::SafeIntTypeUnderTest;

  {  // Test out-of-bounds construction.
    EXPECT_DEATH(T(-1), "bounds");
  }
  {  // Test out-of-bounds construction from float.
    EXPECT_DEATH((T(static_cast<float>(-1))), "bounds");
  }
  {  // Test out-of-bounds construction from double.
    EXPECT_DEATH((T(static_cast<double>(-1))), "bounds");
  }
  {  // Test out-of-bounds construction from long double.
    EXPECT_DEATH((T(static_cast<long double>(-1))), "bounds");
  }
}

TYPED_TEST(UnsignedSafeIntTest, TestUnaryOperators) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = typename T::ValueType;

  {  // Test bitwise not of positive values.
    T x(123);
    EXPECT_EQ((~x).value(), V(~(x.value())));
    EXPECT_EQ((~~x).value(), x.value());
  }
  {  // Test bitwise not of zero.
    T x(0x00);
    EXPECT_EQ((~x).value(), V(~(x.value())));
    EXPECT_EQ((~~x).value(), x.value());
  }
}

TYPED_TEST(UnsignedSafeIntTest, TestUnaryOperatorsConstexpr) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  using V = T::ValueType;

  constexpr T x = T{123};
  constexpr T y = T{~x};
  constexpr T z = ~~T{123};
  EXPECT_EQ(x, T{123});
  EXPECT_EQ(y, T{V{std::numeric_limits<V>::max() & ~V{123}}});
  EXPECT_EQ(z, x);
}

TYPED_TEST(UnsignedSafeIntTest, TestBitwiseAndConstexpr) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  constexpr T conjunction = T{0b0011} & T{0b0001};
  EXPECT_EQ(conjunction, T{0b0011 & 0b0001});
}

TYPED_TEST(UnsignedSafeIntTest, TestBitwiseOrConstexpr) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  constexpr T disjunction = T{0b0011} | T{0b0001};
  EXPECT_EQ(disjunction, T{0b0011 | 0b0001});
}

TYPED_TEST(UnsignedSafeIntTest, TestBitwiseXorConstexpr) {
  using T = typename TestFixture::SafeIntTypeUnderTest;
  constexpr T xord = T{0b1001} ^ T { 0b0101 };
  EXPECT_EQ(xord, T{0b1001 ^ 0b0101});
}

TYPED_TEST(UnsignedSafeIntTest, TestMultiply) {
  using T = typename TestFixture::SafeIntTypeUnderTest;

  {  // Test multiplication by a negative.
    T x(93);
    EXPECT_DEATH(x * -1, "negation");
    EXPECT_DEATH(x *= -1, "negation");
  }
}

TYPED_TEST(UnsignedSafeIntTest, TestDivide) {
  using T = typename TestFixture::SafeIntTypeUnderTest;

  {  // Test division by a negative.
    T x(93);
    EXPECT_DEATH(x / -1, "negation");
    EXPECT_DEATH(x /= -1, "negation");
  }
}

TYPED_TEST(UnsignedSafeIntTest, TestModulo) {
  using T = typename TestFixture::SafeIntTypeUnderTest;

  {  // Test modulo by a negative.
    T x(93);
    EXPECT_DEATH(x % -5, "negation");
    EXPECT_DEATH(x %= -5, "negation");
  }
}

#undef TEST_T_OP_T
#undef TEST_T_OP_NUM

}  // namespace
}  // namespace util_intops
