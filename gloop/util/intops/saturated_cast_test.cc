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

// Unit tests for saturated_cast. Since there is some trickiness in terms with
// conversions between types, especially between signed and unsigned, types, we
// test from all supported types to all supported types, including uint128.
#include "gloop/util/intops/saturated_cast.h"

#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <typeinfo>

#include "absl/base/casts.h"
#include "absl/numeric/int128.h"
#include "absl/strings/substitute.h"
#include "gloop/util/symbolize/demangle.h"
#include "gtest/gtest.h"

namespace util_intops {
namespace {

const int8_t kSomeInt8{123};
const int16_t kSomeInt16{12300};
const int32_t kSomeInt32{12300000};
const int64_t kSomeInt64{1230000000000LL};

static_assert(kSomeInt16 > std::numeric_limits<int8_t>::max(), "");
static_assert(kSomeInt32 > std::numeric_limits<int16_t>::max(), "");
static_assert(kSomeInt64 > std::numeric_limits<int32_t>::max(), "");

const int8_t kSomeNegativeInt8{-123};
const int16_t kSomeNegativeInt16{-12300};
const int32_t kSomeNegativeInt32{-12300000};
const int64_t kSomeNegativeInt64{-1230000000000LL};

static_assert(kSomeNegativeInt16 < std::numeric_limits<int8_t>::min(), "");
static_assert(kSomeNegativeInt32 < std::numeric_limits<int16_t>::min(), "");
static_assert(kSomeNegativeInt64 < std::numeric_limits<int32_t>::min(), "");

const uint8_t kSomeUnsignedInt8{200};
const uint16_t kSomeUnsignedInt16{60000};
const uint32_t kSomeUnsignedInt32{3000000000};
const uint64_t kSomeUnsignedInt64{10000000000000000000ULL};

static_assert(kSomeUnsignedInt16 > std::numeric_limits<int16_t>::max(), "");
static_assert(kSomeUnsignedInt32 > std::numeric_limits<int32_t>::max(), "");
static_assert(kSomeUnsignedInt64 > std::numeric_limits<int64_t>::max(), "");

// Converts from any integral type to uint128. This is needed since uint128
// disallows some implicit conversions because they are "unsafe".
template <typename T, typename Enabled = typename std::enable_if<
                          std::is_convertible<T, uint64_t>::value>::type>
absl::uint128 ConvertToUint128(T from) {
  return absl::uint128(absl::implicit_cast<uint64_t>(from));
}

// Overload of ConvertToUint128 that allows use in a generic context when the
// argument is in fact already a uint128.
absl::uint128 ConvertToUint128(absl::uint128 from) { return from; }

template <typename TypeParam>
class SaturatedCastTest : public testing::Test {
 protected:
  bool IsTypeParamUnsigned() {
    return std::is_unsigned<TypeParam>::value ||
           std::is_same<TypeParam, absl::uint128>::value;
  }

  template <typename From>
  void ExpectLossless(From from) {
    EXPECT_EQ(ConvertToTypeParam(from), saturated_cast<TypeParam>(from))
        << DebugStringForCastFrom(from)
        << " did not have a lossless conversion as expected";
  }

  template <typename From>
  void ExpectSaturatesMin(From from) {
    EXPECT_EQ(std::numeric_limits<TypeParam>::min(),
              saturated_cast<TypeParam>(from))
        << DebugStringForCastFrom(from)
        << " did not saturate to the minimum value as expected";
  }

  template <typename From>
  void ExpectSaturatesMax(From from) {
    EXPECT_EQ(std::numeric_limits<TypeParam>::max(),
              saturated_cast<TypeParam>(from))
        << DebugStringForCastFrom(from)
        << " did not saturate to the maximum value as expected";
  }

 private:
  template <typename From>
  TypeParam ConvertToTypeParam(From from);

  template <typename From>
  std::string DebugStringForCastFrom(From from) {
    // We use PrintToString so that it works also with uint128.
    const std::string from_str = testing::PrintToString(from);
    return absl::Substitute("saturated_cast<$0>($1($2))",
                            util::Demangle(typeid(TypeParam).name()),
                            util::Demangle(typeid(From).name()), from_str);
  }
};

template <typename TypeParam>
template <typename From>
TypeParam SaturatedCastTest<TypeParam>::ConvertToTypeParam(From from) {
  return from;
}

template <>
template <typename From>
absl::uint128 SaturatedCastTest<absl::uint128>::ConvertToTypeParam(From from) {
  return ConvertToUint128(from);
}

using TypesToTest = testing::Types<int8_t, int16_t, int32_t, int64_t, uint8_t,
                                   uint16_t, uint32_t, uint64_t, absl::uint128>;

TYPED_TEST_SUITE(SaturatedCastTest, TypesToTest);

TYPED_TEST(SaturatedCastTest, FromZero) {
  this->ExpectLossless(absl::implicit_cast<int8_t>(0));
  this->ExpectLossless(absl::implicit_cast<uint8_t>(0));
  this->ExpectLossless(absl::implicit_cast<int16_t>(0));
  this->ExpectLossless(absl::implicit_cast<uint16_t>(0));
  this->ExpectLossless(absl::implicit_cast<int32_t>(0));
  this->ExpectLossless(absl::implicit_cast<uint32_t>(0));
  this->ExpectLossless(absl::implicit_cast<int64_t>(0));
  this->ExpectLossless(absl::implicit_cast<uint64_t>(0));
}

TYPED_TEST(SaturatedCastTest, FromNegativeOne) {
  if (this->IsTypeParamUnsigned()) {
    this->ExpectSaturatesMin(absl::implicit_cast<int8_t>(-1));
    this->ExpectSaturatesMin(absl::implicit_cast<int16_t>(-1));
    this->ExpectSaturatesMin(absl::implicit_cast<int32_t>(-1));
    this->ExpectSaturatesMin(absl::implicit_cast<int64_t>(-1));
  } else {
    this->ExpectLossless(absl::implicit_cast<int8_t>(-1));
    this->ExpectLossless(absl::implicit_cast<int16_t>(-1));
    this->ExpectLossless(absl::implicit_cast<int32_t>(-1));
    this->ExpectLossless(absl::implicit_cast<int64_t>(-1));
  }
}

TYPED_TEST(SaturatedCastTest, FromInt8) {
  const auto from = kSomeInt8;
  this->ExpectLossless(from);
}

TYPED_TEST(SaturatedCastTest, FromNegativeInt8) {
  const auto from = kSomeNegativeInt8;
  if (this->IsTypeParamUnsigned()) {
    this->ExpectSaturatesMin(from);
  } else {
    this->ExpectLossless(from);
  }
}

TYPED_TEST(SaturatedCastTest, FromUnsignedInt8) {
  const auto from = kSomeUnsignedInt8;
  if (std::is_same<TypeParam, int8_t>::value) {
    this->ExpectSaturatesMax(from);
  } else {
    this->ExpectLossless(from);
  }
}

TYPED_TEST(SaturatedCastTest, FromInt16) {
  const auto from = kSomeInt16;
  if (std::is_same<TypeParam, int8_t>::value ||
      std::is_same<TypeParam, uint8_t>::value) {
    this->ExpectSaturatesMax(from);
  } else {
    this->ExpectLossless(from);
  }
}

TYPED_TEST(SaturatedCastTest, FromNegativeInt16) {
  const auto from = kSomeNegativeInt16;
  if (this->IsTypeParamUnsigned() || std::is_same<TypeParam, int8_t>::value) {
    this->ExpectSaturatesMin(from);
  } else {
    this->ExpectLossless(from);
  }
}

TYPED_TEST(SaturatedCastTest, FromUnsignedInt16) {
  const auto from = kSomeUnsignedInt16;
  if (std::is_same<TypeParam, int8_t>::value ||
      std::is_same<TypeParam, uint8_t>::value ||
      std::is_same<TypeParam, int16_t>::value) {
    this->ExpectSaturatesMax(from);
  } else {
    this->ExpectLossless(from);
  }
}

TYPED_TEST(SaturatedCastTest, FromInt32) {
  const auto from = kSomeInt32;
  if (std::is_same<TypeParam, int8_t>::value ||
      std::is_same<TypeParam, uint8_t>::value ||
      std::is_same<TypeParam, int16_t>::value ||
      std::is_same<TypeParam, uint16_t>::value) {
    this->ExpectSaturatesMax(from);
  } else {
    this->ExpectLossless(from);
  }
}

TYPED_TEST(SaturatedCastTest, FromNegativeInt32) {
  const auto from = kSomeNegativeInt32;
  if (this->IsTypeParamUnsigned() || std::is_same<TypeParam, int8_t>::value ||
      std::is_same<TypeParam, int16_t>::value) {
    this->ExpectSaturatesMin(from);
  } else {
    this->ExpectLossless(from);
  }
}

TYPED_TEST(SaturatedCastTest, FromUnsignedInt32) {
  const auto from = kSomeUnsignedInt32;
  if (std::is_same<TypeParam, int8_t>::value ||
      std::is_same<TypeParam, uint8_t>::value ||
      std::is_same<TypeParam, int16_t>::value ||
      std::is_same<TypeParam, uint16_t>::value ||
      std::is_same<TypeParam, int32_t>::value) {
    this->ExpectSaturatesMax(from);
  } else {
    this->ExpectLossless(from);
  }
}

TYPED_TEST(SaturatedCastTest, FromInt64) {
  const auto from = kSomeInt64;
  if (std::is_same<TypeParam, int8_t>::value ||
      std::is_same<TypeParam, uint8_t>::value ||
      std::is_same<TypeParam, int16_t>::value ||
      std::is_same<TypeParam, uint16_t>::value ||
      std::is_same<TypeParam, int32_t>::value ||
      std::is_same<TypeParam, uint32_t>::value) {
    this->ExpectSaturatesMax(from);
  } else {
    this->ExpectLossless(from);
  }
}

TYPED_TEST(SaturatedCastTest, FromNegativeInt64) {
  const auto from = kSomeNegativeInt64;
  if (this->IsTypeParamUnsigned() || std::is_same<TypeParam, int8_t>::value ||
      std::is_same<TypeParam, int16_t>::value ||
      std::is_same<TypeParam, int32_t>::value) {
    this->ExpectSaturatesMin(from);
  } else {
    this->ExpectLossless(from);
  }
}

TYPED_TEST(SaturatedCastTest, FromUnsignedInt64) {
  const auto from = kSomeUnsignedInt64;
  if (std::is_same<TypeParam, int8_t>::value ||
      std::is_same<TypeParam, uint8_t>::value ||
      std::is_same<TypeParam, int16_t>::value ||
      std::is_same<TypeParam, uint16_t>::value ||
      std::is_same<TypeParam, int32_t>::value ||
      std::is_same<TypeParam, uint32_t>::value ||
      std::is_same<TypeParam, int64_t>::value) {
    this->ExpectSaturatesMax(from);
  } else {
    this->ExpectLossless(from);
  }
}

TYPED_TEST(SaturatedCastTest, FromUint128) {
  EXPECT_EQ(0, saturated_cast<TypeParam>(absl::uint128(0)));
  EXPECT_EQ(42, saturated_cast<TypeParam>(absl::uint128(42)));

  const TypeParam max_saturated_cast =
      saturated_cast<TypeParam>(absl::Uint128Max());
  if (std::is_same<TypeParam, absl::uint128>::value) {
    EXPECT_EQ(absl::Uint128Max(), ConvertToUint128(max_saturated_cast));
  } else {
    EXPECT_EQ(std::numeric_limits<TypeParam>::max(), max_saturated_cast);
  }
}

}  // namespace
}  // namespace util_intops
