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

#include "gloop/util/endian/endian-fp.h"

#include <cstdint>

#include "absl/strings/str_cat.h"
#include "gloop/util/endian/endian.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using ::testing::ElementsAreArray;

struct FloatTestValue {
  float f;     // The value as a host-order floating point.
  uint32_t i;  // The value as a host-order integer (bit_cast of the float).
  char le[4];  // The value as a little-endian byte array.
  char be[4];  // The value as a big-endian byte array.
};

constexpr FloatTestValue kFloatTestValues[] = {
    {3.14159, 0x40490fd0, {0xd0, 0x0f, 0x49, 0x40}, {0x40, 0x49, 0x0f, 0xd0}},
    {0.0, 0x00000000, {0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00}},
    {10e2, 0x447a0000, {0x00, 0x00, 0x7a, 0x44}, {0x44, 0x7a, 0x00, 0x00}},
    {-10.0, 0xc1200000, {0x00, 0x00, 0x20, 0xc1}, {0xc1, 0x20, 0x00, 0x00}},
    {-42e6, 0xcc2037a0, {0xa0, 0x37, 0x20, 0xcc}, {0xcc, 0x20, 0x37, 0xa0}},
    {123e-2, 0x3f9d70a4, {0xa4, 0x70, 0x9d, 0x3f}, {0x3f, 0x9d, 0x70, 0xa4}},
};

struct DoubleTestValue {
  double d;    // The value as a host-order double floating point.
  uint64_t i;  // The value as a host-order integer (bit_cast of the double).
  char le[8];  // The value as a little-endian byte array.
  char be[8];  // The value as a big-endian byte array.
};

constexpr DoubleTestValue kDoubleTestValues[] = {
    {3.14159,
     0x400921f9f01b866e,
     {0x6e, 0x86, 0x1b, 0xf0, 0xf9, 0x21, 0x09, 0x40},
     {0x40, 0x09, 0x21, 0xf9, 0xf0, 0x1b, 0x86, 0x6e}},
    {0.0,
     0x0000000000000000,
     {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
     {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {10e2,
     0x408f400000000000,
     {0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x8f, 0x40},
     {0x40, 0x8f, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {-10.0,
     0xc024000000000000,
     {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x24, 0xc0},
     {0xc0, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {-42e6,
     0xc18406f400000000,
     {0x00, 0x00, 0x00, 0x00, 0xf4, 0x06, 0x84, 0xc1},
     {0xc1, 0x84, 0x06, 0xf4, 0x00, 0x00, 0x00, 0x00}},
    {123e-2,
     0x3ff3ae147ae147ae,
     {0xae, 0x47, 0xe1, 0x7a, 0x14, 0xae, 0xf3, 0x3f},
     {0x3f, 0xf3, 0xae, 0x14, 0x7a, 0xe1, 0x47, 0xae}},
};

class EndianFloatTest : public testing::TestWithParam<FloatTestValue> {};

TEST_P(EndianFloatTest, ConstructWFFWithFloat) {
  const FloatTestValue& tv = GetParam();

  // Little-endian wire format.
  EXPECT_EQ(LittleEndian::ToHost32(LittleEndianFloat::FromHostFP(tv.f)), tv.i);

  // Big-endian wire format.
  EXPECT_EQ(BigEndian::ToHost32(BigEndianFloat::FromHostFP(tv.f)), tv.i);
}

TEST_P(EndianFloatTest, ConstructWFFWithWireInt) {
  const FloatTestValue& tv = GetParam();

  // Little-endian wire format.
  EXPECT_EQ(LittleEndianFloat::ToHostFP(LittleEndian::FromHost32(tv.i)), tv.f);

  // Big-endian wire format.
  EXPECT_EQ(BigEndianFloat::ToHostFP(BigEndian::FromHost32(tv.i)), tv.f);
}

TEST_P(EndianFloatTest, WFFLoadStore) {
  const FloatTestValue& tv = GetParam();

  // Little-endian wire format.
  EXPECT_EQ(LittleEndianFloat::LoadFromBuf(tv.le), tv.f);
  char le_output[4];
  LittleEndianFloat::StoreToBuf(le_output, tv.f);
  EXPECT_THAT(le_output, ElementsAreArray(tv.le));

  // Big-endian wire format.
  EXPECT_EQ(BigEndianFloat::LoadFromBuf(tv.be), tv.f);
  char be_output[4];
  BigEndianFloat::StoreToBuf(be_output, tv.f);
  EXPECT_THAT(be_output, ElementsAreArray(tv.be));
}

INSTANTIATE_TEST_SUITE_P(
    , EndianFloatTest, testing::ValuesIn(kFloatTestValues),
    [](const testing::TestParamInfo<FloatTestValue>& info) {
      return absl::StrCat("0x", absl::Hex(info.param.i, absl::kZeroPad8));
    });

class EndianDoubleTest : public testing::TestWithParam<DoubleTestValue> {};

TEST_P(EndianDoubleTest, ConstructWFDWithDouble) {
  const DoubleTestValue& tv = GetParam();

  // Little-endian wire format.
  EXPECT_EQ(LittleEndian::ToHost64(LittleEndianDouble::FromHostFP(tv.d)), tv.i);

  // Big-endian wire format.
  EXPECT_EQ(BigEndian::ToHost64(BigEndianDouble::FromHostFP(tv.d)), tv.i);
}

TEST_P(EndianDoubleTest, ConstructWFDWithWireInt) {
  const DoubleTestValue& tv = GetParam();

  // Little-endian wire format.
  EXPECT_EQ(LittleEndianDouble::ToHostFP(LittleEndian::FromHost64(tv.i)), tv.d);

  // Big-endian wire format.
  EXPECT_EQ(BigEndianDouble::ToHostFP(BigEndian::FromHost64(tv.i)), tv.d);
}

TEST_P(EndianDoubleTest, WFDLoadStore) {
  const DoubleTestValue& tv = GetParam();

  // Little-endian wire format.
  EXPECT_EQ(LittleEndianDouble::LoadFromBuf(tv.le), tv.d);
  char le_output[8];
  LittleEndianDouble::StoreToBuf(le_output, tv.d);
  EXPECT_THAT(le_output, ElementsAreArray(tv.le));

  // Big-endian wire format.
  EXPECT_EQ(BigEndianDouble::LoadFromBuf(tv.be), tv.d);
  char be_output[8];
  BigEndianDouble::StoreToBuf(be_output, tv.d);
  EXPECT_THAT(be_output, ElementsAreArray(tv.be));
}

INSTANTIATE_TEST_SUITE_P(
    , EndianDoubleTest, testing::ValuesIn(kDoubleTestValues),
    [](const testing::TestParamInfo<DoubleTestValue>& info) {
      return absl::StrCat("0x", absl::Hex(info.param.i, absl::kZeroPad16));
    });

}  // namespace
