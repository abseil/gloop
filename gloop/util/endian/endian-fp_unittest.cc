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

#include "gloop/util/endian/endian-fp.h"

#include <cstdint>
#include <cstring>
#include <iterator>

#include "absl/log/check.h"
#include "gloop/util/endian/endian.h"
#include "gtest/gtest.h"

struct FloatTestValue {
  float f;     // The value as a host-order floating point.
  uint32_t i;  // The value as a host-order integer (bit_cast of the float).
  char le[4];  // The value as a little-endian byte array.
  char be[4];  // The value as a big-endian byte array.
};

static constexpr FloatTestValue kFloatTestValues[] = {
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

static constexpr DoubleTestValue kDoubleTestValues[] = {
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
constexpr int kNumFloatTestValues = std::size(kFloatTestValues);
constexpr int kNumDoubleTestValues = std::size(kDoubleTestValues);

TEST(EndianFpTest, ConstructWFFWithFloat) {
  // Little-endian wire format.
  for (int i = 0; i < kNumFloatTestValues; ++i) {
    const FloatTestValue& tv = kFloatTestValues[i];
    uint32_t wire_value = LittleEndianFloat::FromHostFP(tv.f);
    uint32_t test_value = LittleEndian::ToHost32(wire_value);
    CHECK_EQ(test_value, tv.i) << "LE index=" << i;
  }

  // Big-endian wire format.
  for (int i = 0; i < kNumFloatTestValues; ++i) {
    const FloatTestValue& tv = kFloatTestValues[i];
    uint32_t wire_value = BigEndianFloat::FromHostFP(tv.f);
    uint32_t test_value = BigEndian::ToHost32(wire_value);
    CHECK_EQ(test_value, tv.i) << "BE index=" << i;
  }
}

TEST(EndianFpTest, ConstructWFFWithWireInt) {
  // Little-endian wire format.
  for (int i = 0; i < kNumFloatTestValues; ++i) {
    const FloatTestValue& tv = kFloatTestValues[i];
    uint32_t wire_value = LittleEndian::FromHost32(tv.i);
    float test_value = LittleEndianFloat::ToHostFP(wire_value);
    CHECK_EQ(test_value, tv.f) << "LE index=" << i;
  }

  // Big-endian wire format.
  for (int i = 0; i < kNumFloatTestValues; ++i) {
    const FloatTestValue& tv = kFloatTestValues[i];
    uint32_t wire_value = BigEndian::FromHost32(tv.i);
    float test_value = BigEndianFloat::ToHostFP(wire_value);
    CHECK_EQ(test_value, tv.f) << "BE index=" << i;
  }
}

TEST(EndianFpTest, WFFLoadStore) {
  // Little-endian wire format.
  for (int i = 0; i < kNumFloatTestValues; ++i) {
    const FloatTestValue& tv = kFloatTestValues[i];
    CHECK_EQ(LittleEndianFloat::LoadFromBuf(tv.le), tv.f) << "LE index=" << i;
    char output[4];
    LittleEndianFloat::StoreToBuf(output, tv.f);
    CHECK_EQ(memcmp(tv.le, output, 4) == 0, true) << "LE index=" << i;
  }

  // Big-endian wire format.
  for (int i = 0; i < kNumFloatTestValues; ++i) {
    const FloatTestValue& tv = kFloatTestValues[i];
    CHECK_EQ(BigEndianFloat::LoadFromBuf(tv.be), tv.f) << "BE index=" << i;
    char output[4];
    BigEndianFloat::StoreToBuf(output, tv.f);
    CHECK_EQ(memcmp(tv.be, output, 4) == 0, true) << "BE index=" << i;
  }
}

TEST(EndianFpTest, ConstructWFDWithDouble) {
  // Little-endian wire format.
  for (int i = 0; i < kNumDoubleTestValues; ++i) {
    const DoubleTestValue& tv = kDoubleTestValues[i];
    uint64_t wire_value = LittleEndianDouble::FromHostFP(tv.d);
    uint64_t test_value = LittleEndian::ToHost64(wire_value);
    CHECK_EQ(test_value, tv.i) << "LE index=" << i;
  }

  // Big-endian wire format.
  for (int i = 0; i < kNumDoubleTestValues; ++i) {
    const DoubleTestValue& tv = kDoubleTestValues[i];
    uint64_t wire_value = BigEndianDouble::FromHostFP(tv.d);
    uint64_t test_value = BigEndian::ToHost64(wire_value);
    CHECK_EQ(test_value, tv.i) << "BE index=" << i;
  }
}

TEST(EndianFpTest, ConstructWFDWithWireInt) {
  // Little-endian wire format.
  for (int i = 0; i < kNumDoubleTestValues; ++i) {
    const DoubleTestValue& tv = kDoubleTestValues[i];
    uint64_t wire_value = LittleEndian::FromHost64(tv.i);
    double test_value = LittleEndianDouble::ToHostFP(wire_value);
    CHECK_EQ(test_value, tv.d) << "LE index=" << i;
  }

  // Big-endian wire format.
  for (int i = 0; i < kNumDoubleTestValues; ++i) {
    const DoubleTestValue& tv = kDoubleTestValues[i];
    uint64_t wire_value = BigEndian::FromHost64(tv.i);
    double test_value = BigEndianDouble::ToHostFP(wire_value);
    CHECK_EQ(test_value, tv.d) << "BE index=" << i;
  }
}

TEST(EndianFpTest, WFDLoadStore) {
  // Little-endian wire format.
  for (int i = 0; i < kNumDoubleTestValues; ++i) {
    const DoubleTestValue& tv = kDoubleTestValues[i];
    CHECK_EQ(LittleEndianDouble::LoadFromBuf(tv.le), tv.d) << "LE index=" << i;
    char output[8];
    LittleEndianDouble::StoreToBuf(output, tv.d);
    CHECK_EQ(memcmp(tv.le, output, 8) == 0, true) << "LE index=" << i;
  }

  // Big-endian wire format.
  for (int i = 0; i < kNumDoubleTestValues; ++i) {
    const DoubleTestValue& tv = kDoubleTestValues[i];
    CHECK_EQ(BigEndianDouble::LoadFromBuf(tv.be), tv.d) << "BE index=" << i;
    char output[8];
    BigEndianDouble::StoreToBuf(output, tv.d);
    CHECK_EQ(memcmp(tv.be, output, 8) == 0, true) << "BE index=" << i;
  }
}
