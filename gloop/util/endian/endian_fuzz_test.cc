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

#include <cstdint>

#include "absl/base/casts.h"
#include "absl/numeric/int128.h"
#include "fuzztest/fuzztest.h"
#include "gloop/util/endian/endian.h"
#include "gtest/gtest.h"

namespace {

template <typename EndianClass, typename T>
void FromHostToHostRoundtrips(T v) {
  auto v_wire = EndianClass::FromHost(v);
  // `FromHost` returns an unsigned int.
  using WireType = decltype(v_wire);
  static_assert((std::is_integral_v<WireType> && !std::is_signed_v<WireType>) ||
                std::is_same_v<WireType, absl::uint128>);

  // `ToHost` returns `T`; write it with `auto` to make sure.
  auto v_roundtrip = EndianClass::ToHost(absl::bit_cast<T>(v_wire));
  static_assert(std::is_same_v<decltype(v_roundtrip), T>);

  // Compare as the unsigned int wire type to handle NaN etc.
  EXPECT_EQ(absl::bit_cast<WireType>(v_roundtrip), absl::bit_cast<WireType>(v))
      << " v_roundtrip: " << v_roundtrip << " v: " << v;
}

void LittleEndianFromHostToHostRoundtripsBool(bool v) {
  FromHostToHostRoundtrips<LittleEndian>(v);
}
FUZZ_TEST(EndianFuzzTest, LittleEndianFromHostToHostRoundtripsBool);

void LittleEndianFromHostToHostRoundtripsUint8(uint8_t v) {
  FromHostToHostRoundtrips<LittleEndian>(v);
}
FUZZ_TEST(EndianFuzzTest, LittleEndianFromHostToHostRoundtripsUint8);

void LittleEndianFromHostToHostRoundtripsInt8(int8_t v) {
  FromHostToHostRoundtrips<LittleEndian>(v);
}
FUZZ_TEST(EndianFuzzTest, LittleEndianFromHostToHostRoundtripsInt8);

void LittleEndianFromHostToHostRoundtripsUint16(uint16_t v) {
  FromHostToHostRoundtrips<LittleEndian>(v);
}
FUZZ_TEST(EndianFuzzTest, LittleEndianFromHostToHostRoundtripsUint16);

void LittleEndianFromHostToHostRoundtripsInt16(int16_t v) {
  FromHostToHostRoundtrips<LittleEndian>(v);
}
FUZZ_TEST(EndianFuzzTest, LittleEndianFromHostToHostRoundtripsInt16);

void LittleEndianFromHostToHostRoundtripsUint32(uint32_t v) {
  FromHostToHostRoundtrips<LittleEndian>(v);
}
FUZZ_TEST(EndianFuzzTest, LittleEndianFromHostToHostRoundtripsUint32);

void LittleEndianFromHostToHostRoundtripsInt32(int32_t v) {
  FromHostToHostRoundtrips<LittleEndian>(v);
}
FUZZ_TEST(EndianFuzzTest, LittleEndianFromHostToHostRoundtripsInt32);

void LittleEndianFromHostToHostRoundtripsUint64(uint64_t v) {
  FromHostToHostRoundtrips<LittleEndian>(v);
}
FUZZ_TEST(EndianFuzzTest, LittleEndianFromHostToHostRoundtripsUint64);

void LittleEndianFromHostToHostRoundtripsInt64(int64_t v) {
  FromHostToHostRoundtrips<LittleEndian>(v);
}
FUZZ_TEST(EndianFuzzTest, LittleEndianFromHostToHostRoundtripsInt64);

void LittleEndianFromHostToHostRoundtripsUint128(absl::uint128 v) {
  FromHostToHostRoundtrips<LittleEndian>(v);
}
FUZZ_TEST(EndianFuzzTest, LittleEndianFromHostToHostRoundtripsUint128);

void LittleEndianFromHostToHostRoundtripsFloat(float v) {
  FromHostToHostRoundtrips<LittleEndian>(v);
}
FUZZ_TEST(EndianFuzzTest, LittleEndianFromHostToHostRoundtripsFloat);

void LittleEndianFromHostToHostRoundtripsDouble(double v) {
  FromHostToHostRoundtrips<LittleEndian>(v);
}
FUZZ_TEST(EndianFuzzTest, LittleEndianFromHostToHostRoundtripsDouble);

void BigEndianFromHostToHostRoundtripsBool(bool v) {
  FromHostToHostRoundtrips<BigEndian>(v);
}
FUZZ_TEST(EndianFuzzTest, BigEndianFromHostToHostRoundtripsBool);

void BigEndianFromHostToHostRoundtripsUint8(uint8_t v) {
  FromHostToHostRoundtrips<BigEndian>(v);
}
FUZZ_TEST(EndianFuzzTest, BigEndianFromHostToHostRoundtripsUint8);

void BigEndianFromHostToHostRoundtripsInt8(int8_t v) {
  FromHostToHostRoundtrips<BigEndian>(v);
}
FUZZ_TEST(EndianFuzzTest, BigEndianFromHostToHostRoundtripsInt8);

void BigEndianFromHostToHostRoundtripsUint16(uint16_t v) {
  FromHostToHostRoundtrips<BigEndian>(v);
}
FUZZ_TEST(EndianFuzzTest, BigEndianFromHostToHostRoundtripsUint16);

void BigEndianFromHostToHostRoundtripsInt16(int16_t v) {
  FromHostToHostRoundtrips<BigEndian>(v);
}
FUZZ_TEST(EndianFuzzTest, BigEndianFromHostToHostRoundtripsInt16);

void BigEndianFromHostToHostRoundtripsUint32(uint32_t v) {
  FromHostToHostRoundtrips<BigEndian>(v);
}
FUZZ_TEST(EndianFuzzTest, BigEndianFromHostToHostRoundtripsUint32);

void BigEndianFromHostToHostRoundtripsInt32(int32_t v) {
  FromHostToHostRoundtrips<BigEndian>(v);
}
FUZZ_TEST(EndianFuzzTest, BigEndianFromHostToHostRoundtripsInt32);

void BigEndianFromHostToHostRoundtripsUint64(uint64_t v) {
  FromHostToHostRoundtrips<BigEndian>(v);
}
FUZZ_TEST(EndianFuzzTest, BigEndianFromHostToHostRoundtripsUint64);

void BigEndianFromHostToHostRoundtripsInt64(int64_t v) {
  FromHostToHostRoundtrips<BigEndian>(v);
}
FUZZ_TEST(EndianFuzzTest, BigEndianFromHostToHostRoundtripsInt64);

void BigEndianFromHostToHostRoundtripsUint128(absl::uint128 v) {
  FromHostToHostRoundtrips<BigEndian>(v);
}
FUZZ_TEST(EndianFuzzTest, BigEndianFromHostToHostRoundtripsUint128);

void BigEndianFromHostToHostRoundtripsFloat(float v) {
  FromHostToHostRoundtrips<BigEndian>(v);
}
FUZZ_TEST(EndianFuzzTest, BigEndianFromHostToHostRoundtripsFloat);

void BigEndianFromHostToHostRoundtripsDouble(double v) {
  FromHostToHostRoundtrips<BigEndian>(v);
}
FUZZ_TEST(EndianFuzzTest, BigEndianFromHostToHostRoundtripsDouble);

}  // namespace
