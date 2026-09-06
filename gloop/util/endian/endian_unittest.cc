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

#include "gloop/util/endian/endian.h"

#include <stdio.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ios>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "absl/base/casts.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/numeric/int128.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "benchmark/benchmark.h"
#include "gloop/base/port.h"
#include "gloop/base/uword.h"
#include "gtest/gtest.h"

static constexpr uint64_t kInitialNumber = 0x0123456789abcdef;
static constexpr uint64_t k64Value = kInitialNumber;
static constexpr uint32_t k32Value = 0x01234567;
static constexpr uint16_t k16Value = 0x0123;
static constexpr uint8_t k8Value = 0x01;
static constexpr int64_t k64IValue = 0xa123456789abcdef;
static constexpr int32_t k32IValue = 0x91234567;  // -1234567
static constexpr int16_t k16IValue = 0xff85;      // -123
static constexpr int8_t k8IValue = 0xff;          // -1
static constexpr double kDoubleValue = 3.14159;
static constexpr double kLargeDoubleValue = 2.38603e+64;
static_assert(kLargeDoubleValue > std::numeric_limits<int64_t>::max(),
              "kLargeDoubleValue must not fit in int64_t.");
static constexpr float kFloatValue = 3.14159;
static constexpr bool kBoolValue = true;
static constexpr absl::uint128 k128Value =
    absl::MakeUint128(k64Value, k64IValue);

#ifdef IS_BIG_ENDIAN
static constexpr uint64_t kInitialInNetworkOrder = kInitialNumber;
static constexpr uint64_t k64ValueLE = 0xefcdab8967452301;
static constexpr uint32_t k32ValueLE = 0x67452301;
static constexpr uint16_t k16ValueLE = 0x2301;
static constexpr uint8_t k8ValueLE = k8Value;
static constexpr uint64_t k64IValueLE = 0xefcdab89674523a1;
static constexpr uint32_t k32IValueLE = 0x67452391;
static constexpr uint16_t k16IValueLE = 0x85ff;
static constexpr uint8_t k8IValueLE = 0xff;
static constexpr uint64_t kDoubleValueLE = 0x6e861bf0f9210940;
static constexpr uint64_t kLargeDoubleValueLE = 0x683ade8b26004d4d;
static constexpr uint32_t kFloatValueLE = 0xd00f4940;
static constexpr uint8_t kBoolValueLE = 0x1;
static constexpr absl::uint128 k128ValueLE =
    absl::MakeUint128(k64IValueLE, k64ValueLE);

static constexpr uint64_t k64ValueBE = kInitialNumber;
static constexpr uint32_t k32ValueBE = k32Value;
static constexpr uint16_t k16ValueBE = k16Value;
static constexpr uint8_t k8ValueBE = k8Value;
static constexpr uint64_t k64IValueBE = 0xa123456789abcdef;
static constexpr uint32_t k32IValueBE = 0x91234567;
static constexpr uint16_t k16IValueBE = 0xff85;
static constexpr uint8_t k8IValueBE = 0xff;
static constexpr uint64_t kDoubleValueBE = 0x400921f9f01b866e;
static constexpr uint64_t kLargeDoubleValueBE = 0x4d4d00268bde3a68;
static constexpr uint32_t kFloatValueBE = 0x40490fd0;
static constexpr uint8_t kBoolValueBE = 0x1;
static constexpr absl::uint128 k128ValueBE =
    absl::MakeUint128(k64ValueBE, k64IValueBE);
#elif defined IS_LITTLE_ENDIAN
static constexpr uint64_t kInitialInNetworkOrder = 0xefcdab8967452301;
static constexpr uint64_t k64ValueLE = kInitialNumber;
static constexpr uint32_t k32ValueLE = k32Value;
static constexpr uint16_t k16ValueLE = k16Value;
static constexpr uint8_t k8ValueLE = k8Value;
static constexpr uint64_t k64IValueLE = 0xa123456789abcdef;
static constexpr uint32_t k32IValueLE = 0x91234567;
static constexpr uint16_t k16IValueLE = 0xff85;
static constexpr uint8_t k8IValueLE = 0xff;
static constexpr uint64_t kDoubleValueLE = 0x400921f9f01b866e;
static constexpr uint64_t kLargeDoubleValueLE = 0x4d4d00268bde3a68;
static constexpr uint32_t kFloatValueLE = 0x40490fd0;
static constexpr uint8_t kBoolValueLE = 0x1;
static constexpr absl::uint128 k128ValueLE =
    absl::MakeUint128(k64ValueLE, k64IValueLE);

static constexpr uint64_t k64ValueBE = 0xefcdab8967452301;
static constexpr uint32_t k32ValueBE = 0x67452301;
static constexpr uint16_t k16ValueBE = 0x2301;
static constexpr uint8_t k8ValueBE = k8Value;
static constexpr uint64_t k64IValueBE = 0xefcdab89674523a1;
static constexpr uint32_t k32IValueBE = 0x67452391;
static constexpr uint16_t k16IValueBE = 0x85ff;
static constexpr uint8_t k8IValueBE = 0xff;
static constexpr uint64_t kDoubleValueBE = 0x6e861bf0f9210940;
static constexpr uint64_t kLargeDoubleValueBE = 0x683ade8b26004d4d;
static constexpr uint32_t kFloatValueBE = 0xd00f4940;
static constexpr uint8_t kBoolValueBE = 0x1;
static constexpr absl::uint128 k128ValueBE =
    absl::MakeUint128(k64IValueBE, k64ValueBE);
#endif

TEST(EndianTest, StoreUint128) {
  const absl::uint128 v =
      absl::MakeUint128(0x0f0e0d0c0b0a0908ULL, 0x0706050403020100ULL);
  uint8_t bytes[18] = {0};
  bytes[0] = 126;
  bytes[17] = 127;
  LittleEndian::Store128(&bytes[1], v);
  for (int i = 0; i < 16; ++i) {
    EXPECT_EQ(bytes[1 + i], i);
  }
  EXPECT_EQ(126, bytes[0]);
  EXPECT_EQ(127, bytes[17]);

  const absl::uint128 v_h =
      absl::MakeUint128(0x0001020304050607ULL, 0x08090a0b0c0d0e0fULL);
  BigEndian::Store128(&bytes[1], v_h);
  for (int i = 0; i < 16; ++i) {
    EXPECT_EQ(bytes[1 + i], i);
  }
  EXPECT_EQ(126, bytes[0]);
  EXPECT_EQ(127, bytes[17]);
}

TEST(EndianTest, LoadUint128) {
  uint8_t ascending_values[16] = {0};
  for (int i = 0; i < 16; ++i) {
    ascending_values[i] = i;
  }
  const absl::uint128 value = LittleEndian::Load128(&ascending_values[0]);
  const absl::uint128 reference =
      absl::MakeUint128(0x0f0e0d0c0b0a0908ULL, 0x0706050403020100ULL);
  EXPECT_EQ(reference, value);

  const absl::uint128 value2 =
      LittleEndian::Load128VariableLength(&ascending_values[0], 16);
  EXPECT_EQ(reference, value2);

  const absl::uint128 value_h = BigEndian::Load128(&ascending_values[0]);
  const absl::uint128 reference_h =
      absl::MakeUint128(0x0001020304050607ULL, 0x08090a0b0c0d0e0fULL);
  EXPECT_EQ(reference_h, value_h);

  const absl::uint128 value2_h =
      BigEndian::Load128VariableLength(&ascending_values[0], 16);
  EXPECT_EQ(reference_h, value2_h);
}

TEST(EndianTest, LoadUint64WithLen) {
  uint8_t ascending_values[8] = {0};
  for (int i = 0; i < 8; ++i) {
    ascending_values[i] = i + 1;
  }
  uint64_t reference = 0x0807060504030201ULL;
  uint64_t byte_mask = 0x00ffffffffffffffULL;
  for (int i = 0; i < 8; ++i) {
    ASSERT_EQ(reference,
              LittleEndian::Load64VariableLength(&ascending_values[0], 8 - i))
        << i;
    reference = reference & byte_mask;
    byte_mask >>= 8;
  }
  reference = 0x0102030405060708ULL;
  for (int i = 0; i < 8; ++i) {
    ASSERT_EQ(reference,
              BigEndian::Load64VariableLength(&ascending_values[0], 8 - i))
        << i;
    reference >>= 8;
  }
  uint8_t partial_ascending_values[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
  ASSERT_EQ(uint64_t{0x0000000005040302}, LittleEndian::Load64VariableLength(
                                              &partial_ascending_values[1], 4));
  ASSERT_EQ(uint64_t{0x0000000002030405},
            BigEndian::Load64VariableLength(&partial_ascending_values[1], 4));
}

TEST(EndianTest, StoreUint64WithLen) {
  uint8_t ascending_values[8] = {0};
  for (int i = 0; i < 8; ++i) {
    ascending_values[i] = i + 1;
  }
  uint64_t reference = 0x0807060504030201ULL;
  uint64_t byte_mask = 0x00ffffffffffffffULL;
  for (int i = 0; i < 8; ++i) {
    char output[8] = {0};
    LittleEndian::Store64VariableLength(output, reference, 8 - i);
    for (int byte = 0; byte < 8; ++byte) {
      ASSERT_EQ(byte < (8 - i) ? ascending_values[byte] : 0, output[byte]);
    }
    reference = reference & byte_mask;
    byte_mask >>= 8;
  }

  reference = 0x0102030405060708ULL;
  for (int i = 0; i < 8; ++i) {
    char output[8] = {0};
    BigEndian::Store64VariableLength(output, reference, 8 - i);
    for (int byte = 0; byte < 8; ++byte) {
      ASSERT_EQ(byte < (8 - i) ? ascending_values[byte] : 0, output[byte]);
    }
    reference >>= 8;
  }
}

TEST(EndianTest, LoadUint128WithLen) {
  uint8_t ascending_values[16] = {0};
  for (int i = 0; i < 16; ++i) {
    ascending_values[i] = i + 1;
  }
  absl::uint128 mask = absl::MakeUint128(~0ULL, ~0ULL);
  for (int i = 0; i < 16; ++i) {
    absl::uint128 reference =
        absl::MakeUint128(0x100f0e0d0c0b0a09ULL, 0x0807060504030201ULL);
    EXPECT_EQ(reference & mask,
              LittleEndian::Load128VariableLength(&ascending_values[0], 16 - i))
        << i;
    EXPECT_EQ(
        0, LittleEndian::Load128VariableLength(&ascending_values[0], 16 - i) &
               ~mask)
        << i;
    mask = mask >> 8;
  }
  mask = absl::MakeUint128(~0ULL, ~0ULL);
  for (int i = 0; i < 16; ++i) {
    absl::uint128 reference =
        absl::MakeUint128(0x0102030405060708ULL, 0x090a0b0c0d0e0f10ULL);
    EXPECT_EQ((reference & (mask << (8 * i))) >> (8 * i),
              BigEndian::Load128VariableLength(&ascending_values[0], 16 - i))
        << i;
    EXPECT_EQ(
        0,
        BigEndian::Load128VariableLength(&ascending_values[0], 16 - i) & ~mask)
        << i;
    mask = mask >> 8;
  }
}

TEST(EndianTest, LoadUint64WithLenNonAligned) {
  uint8_t ascending_values[25] = {0};
  for (int i = 0; i < 25; ++i) {
    ascending_values[i] = i;
  }
  uint64_t reference = 0x0706050403020100ULL;
  for (int offset = 0; offset < 16; ++offset) {
    uint64_t mask = ~0ULL;
    for (int i = 0; i < 8; ++i, mask >>= 8) {
      EXPECT_EQ(reference & mask, LittleEndian::Load64VariableLength(
                                      &ascending_values[offset], 8 - i))
          << "i:" << i << " offset:" << offset;
    }
    reference += 0x0101010101010101ULL;
  }
  uint64_t reference_h = 0x0001020304050607ULL;
  for (int offset = 0; offset < 16; ++offset) {
    uint64_t mask = ~0ULL;
    for (int i = 0; i < 8; ++i, mask >>= 8) {
      EXPECT_EQ(
          (reference_h & (mask << (8 * i))) >> (8 * i),
          BigEndian::Load64VariableLength(&ascending_values[offset], 8 - i))
          << "i:" << i << " offset:" << offset;
    }
    reference_h += 0x0101010101010101ULL;
  }
}

TEST(EndianTest, StoreUint64WithLenNonAligned) {
  uint8_t ascending_values[25] = {0};
  for (int i = 0; i < 25; ++i) {
    ascending_values[i] = i;
  }
  uint64_t reference = 0x0706050403020100ULL;
  for (int offset = 0; offset < 16; ++offset) {
    uint64_t mask = ~0ULL;
    for (int i = 0; i < 8; ++i, mask >>= 8) {
      char output[8] = {0};
      LittleEndian::Store64VariableLength(output, reference & mask, 8 - i);
      for (int byte = 0; byte < 8; ++byte) {
        ASSERT_EQ(byte < (8 - i) ? ascending_values[offset + byte] : 0,
                  output[byte])
            << "i:" << i << " offset:" << offset << " byte:" << byte;
      }
    }
    reference += 0x0101010101010101ULL;
  }

  uint64_t reference_h = 0x0001020304050607ULL;
  for (int offset = 0; offset < 16; ++offset) {
    uint64_t mask = ~0ULL;
    for (int i = 0; i < 8; ++i, mask >>= 8) {
      char output[8] = {0};
      BigEndian::Store64VariableLength(
          output, (reference_h & (mask << (8 * i))) >> (8 * i), 8 - i);
      for (int byte = 0; byte < 8; ++byte) {
        ASSERT_EQ(byte < (8 - i) ? ascending_values[offset + byte] : 0,
                  output[byte])
            << "i:" << i << " offset:" << offset << " byte:" << byte;
      }
    }
    reference_h += 0x0101010101010101ULL;
  }
}

TEST(EndianTest, LoadUint128WithLenNonAligned) {
  uint8_t ascending_values[33] = {0};
  for (int i = 0; i < 33; ++i) {
    ascending_values[i] = i;
  }
  for (int offset = 0; offset < 16; ++offset) {
    absl::uint128 reference = absl::MakeUint128(
        0x0f0e0d0c0b0a0908ULL + offset * 0x0101010101010101ULL,
        0x0706050403020100ULL + offset * 0x0101010101010101ULL);
    absl::uint128 mask = absl::MakeUint128(~0ULL, ~0ULL);
    for (int i = 0; i < 16; ++i) {
      EXPECT_EQ(reference & mask, LittleEndian::Load128VariableLength(
                                      &ascending_values[offset], 16 - i))
          << "i:" << i << " offset:" << offset;
      mask = mask >> 8;
    }
  }
  for (int offset = 0; offset < 16; ++offset) {
    absl::uint128 reference = absl::MakeUint128(
        0x0001020304050607ULL + offset * 0x0101010101010101ULL,
        0x08090a0b0c0d0e0fULL + offset * 0x0101010101010101ULL);
    absl::uint128 mask = absl::MakeUint128(~0ULL, ~0ULL);
    for (int i = 0; i < 16; ++i) {
      EXPECT_EQ(
          (reference & (mask << (8 * i))) >> (8 * i),
          BigEndian::Load128VariableLength(&ascending_values[offset], 16 - i))
          << "i:" << i << " offset:" << offset;
      mask = mask >> 8;
    }
  }
}

TEST(EndianTest, LoadUnsignedWord) {
  const int uword_size = sizeof(uword_t);

  // Synthesize constants.
  uword_t ones = 0;        // 0x0101...0101
  uword_t ascending = 0;   // 0x000102 ....
  uword_t descending = 0;  // 0x.....020100
  for (int i = 0; i < uword_size; ++i) {
    ones = (ones << 8) | 0x01;
    ascending = (ascending << 8) | i;
    descending = (descending << 8) | (uword_size - i - 1);
  }

  uint8_t buffer[uword_size * 2];
  for (int i = 0; i < uword_size * 2; ++i) buffer[i] = i;

  // Test different alignments.
  for (int offset = 0; offset < uword_size; ++offset) {
    uword_t big = BigEndian::LoadUnsignedWord(buffer + offset);
    EXPECT_EQ(ascending + ones * offset, big);

    uword_t little = LittleEndian::LoadUnsignedWord(buffer + offset);
    EXPECT_EQ(descending + ones * offset, little);
  }
}

TEST(EndianTest, StoreUnsignedWord) {
  const int uword_size = sizeof(uword_t);

  // Synthesize constants.
  uword_t ones = 0;        // 0x0101...0101
  uword_t ascending = 0;   // 0x000102 ....
  uword_t descending = 0;  // 0x.....020100
  for (int i = 0; i < uword_size; ++i) {
    ones = (ones << 8) | 0x01;
    ascending = (ascending << 8) | i;
    descending = (descending << 8) | (uword_size - i - 1);
  }

  uint8_t reference[uword_size * 2];
  for (int i = 0; i < uword_size * 2; ++i) reference[i] = i;

  // Test different alignments.
  uint8_t buffer[uword_size * 2];
  for (int offset = 0; offset < uword_size; ++offset) {
    memset(buffer, ~0, sizeof(buffer));
    uword_t big = ascending + offset * ones;
    BigEndian::StoreUnsignedWord(buffer + offset, big);
    EXPECT_EQ(0, memcmp(buffer + offset, reference + offset, uword_size));

    memset(buffer, ~0, sizeof(buffer));
    uword_t little = descending + offset * ones;
    LittleEndian::StoreUnsignedWord(buffer + offset, little);
    EXPECT_EQ(0, memcmp(buffer + offset, reference + offset, uword_size));
  }
}

TEST(EndianTest, GeneralFromHost) {
  // Check LittleEndian::FromHost
  EXPECT_EQ(LittleEndian::FromHost(k64Value), k64ValueLE);
  EXPECT_EQ(LittleEndian::FromHost(k32Value), k32ValueLE);
  EXPECT_EQ(LittleEndian::FromHost(k16Value), k16ValueLE);
  EXPECT_EQ(LittleEndian::FromHost(k8Value), k8ValueLE);
  EXPECT_EQ(LittleEndian::FromHost(k64IValue), k64IValueLE);
  EXPECT_EQ(LittleEndian::FromHost(k32IValue), k32IValueLE);
  EXPECT_EQ(LittleEndian::FromHost(k16IValue), k16IValueLE);
  EXPECT_EQ(LittleEndian::FromHost(k8IValue), k8IValueLE);
  EXPECT_EQ(LittleEndian::FromHost(kDoubleValue), kDoubleValueLE);
  EXPECT_EQ(LittleEndian::FromHost(kLargeDoubleValue), kLargeDoubleValueLE);
  EXPECT_EQ(LittleEndian::FromHost(kFloatValue), kFloatValueLE);
  EXPECT_EQ(LittleEndian::FromHost(kBoolValue), kBoolValueLE);
  EXPECT_EQ(LittleEndian::FromHost(k128Value), k128ValueLE);

  // Check BigEndian::FromHost
  EXPECT_EQ(BigEndian::FromHost(k64Value), k64ValueBE);
  EXPECT_EQ(BigEndian::FromHost(k32Value), k32ValueBE);
  EXPECT_EQ(BigEndian::FromHost(k16Value), k16ValueBE);
  EXPECT_EQ(BigEndian::FromHost(k8Value), k8ValueBE);
  EXPECT_EQ(BigEndian::FromHost(k64IValue), k64IValueBE);
  EXPECT_EQ(BigEndian::FromHost(k32IValue), k32IValueBE);
  EXPECT_EQ(BigEndian::FromHost(k16IValue), k16IValueBE);
  EXPECT_EQ(BigEndian::FromHost(k8IValue), k8IValueBE);
  EXPECT_EQ(BigEndian::FromHost(kDoubleValue), kDoubleValueBE);
  EXPECT_EQ(BigEndian::FromHost(kLargeDoubleValue), kLargeDoubleValueBE);
  EXPECT_EQ(BigEndian::FromHost(kFloatValue), kFloatValueBE);
  EXPECT_EQ(BigEndian::FromHost(kBoolValue), kBoolValueBE);
  EXPECT_EQ(BigEndian::FromHost(k128Value), k128ValueBE);
}

TEST(EndianTest, GeneralToHost) {
  // Check LittleEndian::ToHost
  EXPECT_EQ(absl::bit_cast<uint64_t>(LittleEndian::ToHost(k64ValueLE)),
            k64Value);
  EXPECT_EQ(absl::bit_cast<uint32_t>(LittleEndian::ToHost(k32ValueLE)),
            k32Value);
  EXPECT_EQ(absl::bit_cast<uint16_t>(LittleEndian::ToHost(k16ValueLE)),
            k16Value);
  EXPECT_EQ(absl::bit_cast<uint8_t>(LittleEndian::ToHost(k8ValueLE)), k8Value);
  EXPECT_EQ(absl::bit_cast<int64_t>(LittleEndian::ToHost(k64IValueLE)),
            k64IValue);
  EXPECT_EQ(absl::bit_cast<int32_t>(LittleEndian::ToHost(k32IValueLE)),
            k32IValue);
  EXPECT_EQ(absl::bit_cast<int16_t>(LittleEndian::ToHost(k16IValueLE)),
            k16IValue);
  EXPECT_EQ(absl::bit_cast<int8_t>(LittleEndian::ToHost(k8IValueLE)), k8IValue);
  EXPECT_EQ(LittleEndian::ToHost(absl::bit_cast<double>(kDoubleValueLE)),
            kDoubleValue);
  EXPECT_EQ(LittleEndian::ToHost(absl::bit_cast<double>(kLargeDoubleValueLE)),
            kLargeDoubleValue);
  EXPECT_EQ(LittleEndian::ToHost(absl::bit_cast<float>(kFloatValueLE)),
            kFloatValue);
  EXPECT_EQ(absl::bit_cast<bool>(LittleEndian::ToHost(kBoolValueLE)),
            kBoolValue);
  EXPECT_EQ(absl::bit_cast<absl::uint128>(LittleEndian::ToHost(k128ValueLE)),
            k128Value);

  // Check BigEndian::ToHost
  EXPECT_EQ(absl::bit_cast<uint64_t>(BigEndian::ToHost(k64ValueBE)), k64Value);
  EXPECT_EQ(absl::bit_cast<uint32_t>(BigEndian::ToHost(k32ValueBE)), k32Value);
  EXPECT_EQ(absl::bit_cast<uint16_t>(BigEndian::ToHost(k16ValueBE)), k16Value);
  EXPECT_EQ(absl::bit_cast<uint8_t>(BigEndian::ToHost(k8ValueBE)), k8Value);
  EXPECT_EQ(absl::bit_cast<int64_t>(BigEndian::ToHost(k64IValueBE)), k64IValue);
  EXPECT_EQ(absl::bit_cast<int32_t>(BigEndian::ToHost(k32IValueBE)), k32IValue);
  EXPECT_EQ(absl::bit_cast<int16_t>(BigEndian::ToHost(k16IValueBE)), k16IValue);
  EXPECT_EQ(absl::bit_cast<int8_t>(BigEndian::ToHost(k8IValueBE)), k8IValue);
  EXPECT_EQ(BigEndian::ToHost(absl::bit_cast<double>(kDoubleValueBE)),
            kDoubleValue);
  EXPECT_EQ(BigEndian::ToHost(absl::bit_cast<double>(kLargeDoubleValueBE)),
            kLargeDoubleValue);
  EXPECT_EQ(BigEndian::ToHost(absl::bit_cast<float>(kFloatValueBE)),
            kFloatValue);
  EXPECT_EQ(absl::bit_cast<bool>(BigEndian::ToHost(kBoolValueBE)), kBoolValue);
  EXPECT_EQ(absl::bit_cast<absl::uint128>(BigEndian::ToHost(k128ValueBE)),
            k128Value);
}

namespace {

template <typename T>
std::vector<T> GenerateAllValuesForType() {
  std::vector<T> result;  // For RVO.
  T next = std::numeric_limits<T>::min();
  while (true) {
    result.push_back(next);
    if (next == std::numeric_limits<T>::max()) {
      return result;
    }
    ++next;
  }
}

// Use an explicit specialization for bool to prevent a boolean increment.
template <>
std::vector<bool> GenerateAllValuesForType<bool>() {
  return {false, true};
}

static constexpr int kRandomSeed = 12345;

template <typename T>
std::vector<T> GenerateRandomIntegers(size_t numValuesToTest) {
  std::vector<T> result;  // For RVO.
  std::mt19937_64 rng(kRandomSeed);
  for (size_t i = 0; i < numValuesToTest; ++i) {
    result.push_back(rng());
  }
  return result;
}

template <>
std::vector<absl::uint128> GenerateRandomIntegers<absl::uint128>(
    size_t numValuesToTest) {
  std::vector<absl::uint128> result;  // For RVO.
  std::mt19937_64 rng(kRandomSeed);
  for (size_t i = 0; i < numValuesToTest; ++i) {
    result.push_back(absl::MakeUint128(rng(), rng()));
  }
  return result;
}

template <typename T>
std::vector<T> GenerateRandomFloatingPointNumbers(size_t numValuesToTest) {
  std::vector<T> result;  // For RVO.
  std::mt19937_64 rng(kRandomSeed);
  std::uniform_real_distribution<double> real_distribution;
  for (size_t i = 0; i < numValuesToTest; ++i) {
    result.push_back(real_distribution(rng));
  }
  return result;
}

template <typename T, typename ByteSwapper>
static void GenericLoadStoreHelper(const std::vector<T>& host_values_to_test,
                                   const ByteSwapper& byte_swapper) {
  // Compare the result of {Little,Big}Endian::Store<T> to blob of
  // independently-calculated bytes.
  for (const T host_value : host_values_to_test) {
    // Perform Little- and BigEndian conversion.
    char le_wire_value[sizeof(host_value)];
    char be_wire_value[sizeof(host_value)];
    LittleEndian::Store(host_value, le_wire_value);
    BigEndian::Store(host_value, be_wire_value);

    // Calculate expected results for Little- and BigEndian conversion.
    char expected_le_wire_value[sizeof(host_value)];
    char expected_be_wire_value[sizeof(host_value)];
    memcpy(expected_le_wire_value, &host_value, sizeof(host_value));
    memcpy(expected_be_wire_value, &host_value, sizeof(host_value));
#ifdef IS_LITTLE_ENDIAN
    byte_swapper(expected_be_wire_value);
#else
    byte_swapper(expected_le_wire_value);
#endif

    // These are asserts rather than expects because if they fail, a *lot* of
    // them will fail, and there's no point in wading through bazillions of
    // pages of logs.
    ASSERT_EQ(0, memcmp(le_wire_value, expected_le_wire_value,
                        sizeof(le_wire_value)));
    ASSERT_EQ(0, memcmp(be_wire_value, expected_be_wire_value,
                        sizeof(be_wire_value)));
  }

  // Round-trip test.
  for (const T host_value : host_values_to_test) {
    char le_wire_value[sizeof(host_value)];
    char be_wire_value[sizeof(host_value)];
    LittleEndian::Store(host_value, le_wire_value);
    BigEndian::Store(host_value, be_wire_value);

    T host_from_le = LittleEndian::Load<T>(le_wire_value);
    T host_from_be = BigEndian::Load<T>(be_wire_value);

    ASSERT_EQ(host_value, host_from_le);
    ASSERT_EQ(host_value, host_from_be);
  }
}

void Swap8(char* bytes) { /* do nothing */ }

void Swap16(char* bytes) {
  UNALIGNED_STORE16(bytes, absl::byteswap(UNALIGNED_LOAD16(bytes)));
}

void Swap32(char* bytes) {
  UNALIGNED_STORE32(bytes, absl::byteswap(UNALIGNED_LOAD32(bytes)));
}

void Swap64(char* bytes) {
  UNALIGNED_STORE64(bytes, absl::byteswap(UNALIGNED_LOAD64(bytes)));
}

void Swap128(char* bytes) {
  absl::uint128 val = absl::MakeUint128(
      UNALIGNED_LOAD64(bytes), UNALIGNED_LOAD64(bytes + sizeof(uint64_t)));
  val = gbswap_128(val);
  UNALIGNED_STORE64(bytes, absl::Uint128High64(val));
  UNALIGNED_STORE64(bytes + sizeof(uint64_t), absl::Uint128Low64(val));
}

}  // namespace

static constexpr int kNumValuesToTest = 1000000;

TEST(SwapTest, ChangeOfWidth) {
  EXPECT_EQ(absl::byteswap<uint64_t>(uint32_t{0xDEADBEEF}),
            uint64_t{0xEFBEADDE00000000});
  EXPECT_EQ(absl::byteswap<uint32_t>(uint16_t{0xDEAD}), uint32_t{0xADDE0000});
  EXPECT_EQ(absl::byteswap<uint16_t>(uint32_t{0xDEADBEEF}), uint16_t{0xEFBE});
}

TEST(GenericLoadStore, TestBool) {
  GenericLoadStoreHelper(GenerateAllValuesForType<bool>(), &Swap8);
}

TEST(GenericLoadStore, Test8BitTypes) {
  GenericLoadStoreHelper(GenerateAllValuesForType<uint8_t>(), &Swap8);
  GenericLoadStoreHelper(GenerateAllValuesForType<int8_t>(), &Swap8);
}

TEST(GenericLoadStore, Test16BitTypes) {
  GenericLoadStoreHelper(GenerateAllValuesForType<uint16_t>(), &Swap16);
  GenericLoadStoreHelper(GenerateAllValuesForType<int16_t>(), &Swap16);
}

TEST(GenericLoadStore, Test32BitTypes) {
  GenericLoadStoreHelper(GenerateRandomIntegers<uint32_t>(kNumValuesToTest),
                         &Swap32);
  GenericLoadStoreHelper(GenerateRandomIntegers<int32_t>(kNumValuesToTest),
                         &Swap32);
}

TEST(GenericLoadStore, Test64BitTypes) {
  GenericLoadStoreHelper(GenerateRandomIntegers<uint64_t>(kNumValuesToTest),
                         &Swap64);
  GenericLoadStoreHelper(GenerateRandomIntegers<int64_t>(kNumValuesToTest),
                         &Swap64);
}

TEST(GenericLoadStore, Test128BitTypes) {
  GenericLoadStoreHelper(
      GenerateRandomIntegers<absl::uint128>(kNumValuesToTest), &Swap128);
}

TEST(GenericLoadStore, TestFloat) {
  GenericLoadStoreHelper(
      GenerateRandomFloatingPointNumbers<float>(kNumValuesToTest), &Swap32);
}

TEST(GenericLoadStore, TestDouble) {
  GenericLoadStoreHelper(
      GenerateRandomFloatingPointNumbers<double>(kNumValuesToTest), &Swap64);
}

static void ManualByteSwap(char* bytes, int length) {
  if (length == 1) return;

  CHECK_EQ(0, length % 2);
  for (int i = 0; i < length / 2; ++i) {
    int j = (length - 1) - i;
    using std::swap;
    swap(bytes[i], bytes[j]);
  }
}

template <typename T>
inline T UnalignedLoad(const char* p) {
  switch (sizeof(T)) {
    case 1:
      return *reinterpret_cast<const T*>(p);
    case 2:
      return UNALIGNED_LOAD16(p);
    case 4:
      return UNALIGNED_LOAD32(p);
    case 8:
      return UNALIGNED_LOAD64(p);
    default: {
      LOG(FATAL) << "Unreachable";
      return 0;
    }
  }
}

template <>
inline absl::uint128 UnalignedLoad<absl::uint128>(const char* p) {
  return absl::MakeUint128(UNALIGNED_LOAD64(p),
                           UNALIGNED_LOAD64(p + sizeof(uint64_t)));
}

template <typename T, typename ByteSwapper>
static void GBSwapHelper(const std::vector<T>& host_values_to_test,
                         const ByteSwapper& byte_swapper) {
  // Test byte_swapper against a manual byte swap.
  for (typename std::vector<T>::const_iterator it = host_values_to_test.begin();
       it != host_values_to_test.end(); ++it) {
    T host_value = *it;

    char actual_value[sizeof(host_value)];
    memcpy(actual_value, &host_value, sizeof(host_value));
    byte_swapper(actual_value);

    char expected_value[sizeof(host_value)];
    memcpy(expected_value, &host_value, sizeof(host_value));
    ManualByteSwap(expected_value, sizeof(host_value));

    ASSERT_EQ(0, memcmp(actual_value, expected_value, sizeof(host_value)))
        << "Swap output for 0x" << std::hex << host_value << " does not match. "
        << "Expected: 0x" << UnalignedLoad<T>(expected_value) << "; "
        << "actual: 0x" << UnalignedLoad<T>(actual_value);
  }
}

TEST(GBSwap, Uint16) {
  GBSwapHelper(GenerateAllValuesForType<uint16_t>(), &Swap16);
}

TEST(GBSwap, Uint32) {
  GBSwapHelper(GenerateRandomIntegers<uint32_t>(kNumValuesToTest), &Swap32);
}

TEST(GBSwap, Uint64) {
  GBSwapHelper(GenerateRandomIntegers<uint64_t>(kNumValuesToTest), &Swap64);
}

TEST(GBSwap, Uint128) {
  GBSwapHelper(GenerateRandomIntegers<absl::uint128>(kNumValuesToTest),
               &Swap128);
}

// helper class used to support testing at different alignments.
class TestLoadStore24 : public ::testing::TestWithParam<int> {
 protected:
  // We initialize enough storage to get a pointer to three bytes, (un)aligned
  // to whatever number of bytes off natural alignment GetParam() returns.
  //
  // The actual storage includes a spare byte beyond the end to act as a
  // redzone for corruption, etc.
  TestLoadStore24()
      : storage_(GetParam() + 4, 0), data_(&storage_[GetParam()]) {
    CHECK_GE(GetParam(), 0) << "GetParam() = " << GetParam()
                            << " which is really going to mess up this code!";
  }

  ~TestLoadStore24() override {
    // GetParam() is the number of bytes to offset for unaligned testing, and
    // we index from zero, so +3 means "the fourth byte"...
    CHECK_EQ(storage_[GetParam() + 3], 0)
        << "Corruption of redzone byte detected";
  }

  void SetUp() override { std::fill(storage_.begin(), storage_.end(), 0); }

  // Underlying storage, used for getting unaligned pointers.
  std::vector<uint8_t> storage_;
  // Pointer to three bytes with the configured alignment.
  uint8_t* data_;
};

INSTANTIATE_TEST_SUITE_P(WithAlignment, TestLoadStore24,
                         ::testing::Range(0, 15));

TEST_P(TestLoadStore24, LittleEndianLoad24) {
  // manually place LE constant
  data_[2] = 0x12;
  data_[1] = 0x34;
  data_[0] = 0x56;
  EXPECT_EQ(LittleEndian::Load24(data_), 0x123456);
}

TEST_P(TestLoadStore24, LittleEndianStore24) {
  LittleEndian::Store24(data_, 0xABCDEF);
  EXPECT_EQ(data_[2], 0xAB);
  EXPECT_EQ(data_[1], 0xCD);
  EXPECT_EQ(data_[0], 0xEF);
}

// Verify that we don't fail, or corrupt anything, when the input is larger
// than the maximum uint24 value, and that our truncation works as expected.
TEST_P(TestLoadStore24, LittleEndianStore24Overflow) {
  LittleEndian::Store24(data_, 0x12345678);
  EXPECT_EQ(LittleEndian::Load24(data_), 0x345678);
  EXPECT_EQ(data_[0], 0x78);
  EXPECT_EQ(data_[1], 0x56);
  EXPECT_EQ(data_[2], 0x34);
  // check for write-beyond-end - value will always init to zero.
  EXPECT_EQ(data_[3], 0x00);
}

TEST_P(TestLoadStore24, BigEndianLoad24) {
  data_[0] = 0x12;
  data_[1] = 0x34;
  data_[2] = 0x56;
  EXPECT_EQ(BigEndian::Load24(data_), 0x123456);
}

TEST_P(TestLoadStore24, BigEndianStore24) {
  BigEndian::Store24(data_, 0xABCDEF);
  EXPECT_EQ(data_[0], 0xAB);
  EXPECT_EQ(data_[1], 0xCD);
  EXPECT_EQ(data_[2], 0xEF);
}

// Verify that we don't fail, or corrupt anything, when the input is larger
// than the maximum uint24 value, and that our truncation works as expected.
TEST_P(TestLoadStore24, BigEndianStore24Overflow) {
  BigEndian::Store24(data_, 0x12345678);
  EXPECT_EQ(BigEndian::Load24(data_), 0x345678);
  EXPECT_EQ(data_[0], 0x34);
  EXPECT_EQ(data_[1], 0x56);
  EXPECT_EQ(data_[2], 0x78);
  // check for write-beyond-end - value will always init to zero.
  EXPECT_EQ(data_[3], 0x00);
}

TEST(Store64VariableLength, Zero) {
  uint8_t zeros_dst = 0;
  uint8_t ones_dst = 0xFF;
  LittleEndian::Store64VariableLength(&zeros_dst, k64Value, 0);
  ASSERT_EQ(0, zeros_dst);
  LittleEndian::Store64VariableLength(&ones_dst, k64Value, 0);
  ASSERT_EQ(0xFF, ones_dst);
  BigEndian::Store64VariableLength(&zeros_dst, k64Value, 0);
  ASSERT_EQ(0, zeros_dst);
  BigEndian::Store64VariableLength(&ones_dst, k64Value, 0);
  ASSERT_EQ(0xFF, ones_dst);
}

TEST(VariableLengthRoundTrip, LittleEndian) {
  static constexpr int kTrials = 1000;
  for (int trial = 0; trial < kTrials; ++trial) {
    absl::BitGen gen;
    for (int bytes = 1; bytes < sizeof(uint64_t); ++bytes) {
      const uint64_t val = absl::Uniform(gen, 0ULL, 1ULL << (bytes * 8));
      char buffer[sizeof(uint64_t)] = {0};
      LittleEndian::Store64VariableLength(buffer, val, bytes);
      for (int byte = bytes; byte < sizeof(uint64_t); ++byte) {
        EXPECT_EQ(0, buffer[byte])
            << "Byte " << byte
            << " should have been left unmodified as 0 but was instead "
            << static_cast<int>(buffer[byte]);
      }
      EXPECT_EQ(val, LittleEndian::Load64VariableLength(buffer, bytes))
          << "bytes = " << bytes << " val = " << val;
    }
  }
}

TEST(VariableLengthRoundTrip, BigEndian) {
  static constexpr int kTrials = 10000;
  for (int trial = 0; trial < kTrials; ++trial) {
    absl::BitGen gen;
    for (int bytes = 1; bytes < sizeof(uint64_t); ++bytes) {
      const uint64_t val = absl::Uniform(gen, 0ULL, 1ULL << (bytes * 8));
      char buffer[sizeof(uint64_t)] = {0};
      BigEndian::Store64VariableLength(buffer, val, bytes);
      for (int byte = bytes; byte < sizeof(uint64_t); ++byte) {
        EXPECT_EQ(0, buffer[byte])
            << "Byte " << byte
            << " should have been left unmodified as 0 but was instead "
            << static_cast<int>(buffer[byte]);
      }
      EXPECT_EQ(val, BigEndian::Load64VariableLength(buffer, bytes))
          << "bytes = " << bytes << " val = " << val;
    }
  }
}

void BM_LittleEndian_Load64VariableLength(benchmark::State& state) {
  int bytes = state.range(0);
  static constexpr int kNumValues = 1000;
  std::vector<std::string> values(kNumValues);
  absl::BitGen gen;
  for (int i = 0; i < kNumValues; ++i) {
    values[i] = std::string(sizeof(uint64_t), '\0');
    uint64_t rand = bytes < 8 ? absl::Uniform(gen, 0UL, 1UL << (8 * bytes))
                              : absl::Uniform<uint64_t>(gen);
    LittleEndian::Store64(values[i].data(), rand);
  }

  int idx = 0;
  for (auto s : state) {
    benchmark::DoNotOptimize(bytes);
    uint64_t v =
        LittleEndian::Load64VariableLength(values[idx++].data(), bytes);
    benchmark::DoNotOptimize(v);
    if (idx >= kNumValues) idx = 0;
  }
}

void BM_BigEndian_Load64VariableLength(benchmark::State& state) {
  int bytes = state.range(0);
  static constexpr int kNumValues = 1000;
  std::vector<std::string> values(kNumValues);
  absl::BitGen gen;
  for (int i = 0; i < kNumValues; ++i) {
    values[i] = std::string(sizeof(uint64_t), '\0');
    uint64_t rand = bytes < 8 ? absl::Uniform(gen, 0UL, 1UL << (8 * bytes))
                              : absl::Uniform<uint64_t>(gen);
    BigEndian::Store64(values[i].data(), rand);
  }

  int idx = 0;
  for (auto s : state) {
    benchmark::DoNotOptimize(bytes);
    uint64_t v = BigEndian::Load64VariableLength(values[idx++].data(), bytes);
    benchmark::DoNotOptimize(v);
    if (idx >= kNumValues) idx = 0;
  }
}

void BM_LittleEndian_Store64VariableLength(benchmark::State& state) {
  int bytes = state.range(0);
  static constexpr int kNumValues = 1000;
  std::vector<uint64_t> values(kNumValues);
  absl::BitGen gen;
  for (int i = 0; i < kNumValues; ++i) {
    values[i] = bytes < 8 ? absl::Uniform(gen, 0UL, 1UL << (8 * bytes))
                          : absl::Uniform<uint64_t>(gen);
  }

  std::string dest(sizeof(uint64_t), '\0');
  int idx = 0;
  for (auto s : state) {
    benchmark::DoNotOptimize(bytes);
    LittleEndian::Store64VariableLength(dest.data(), values[idx++], bytes);
    if (idx >= kNumValues) idx = 0;
  }
  benchmark::DoNotOptimize(dest);
}

void BM_BigEndian_Store64VariableLength(benchmark::State& state) {
  int bytes = state.range(0);
  static constexpr int kNumValues = 1000;
  std::vector<uint64_t> values(kNumValues);
  absl::BitGen gen;
  for (int i = 0; i < kNumValues; ++i) {
    values[i] = bytes < 8 ? absl::Uniform(gen, 0UL, 1UL << (8 * bytes))
                          : absl::Uniform<uint64_t>(gen);
  }

  std::string dest(sizeof(uint64_t), '\0');
  int idx = 0;
  for (auto s : state) {
    benchmark::DoNotOptimize(bytes);
    BigEndian::Store64VariableLength(dest.data(), values[idx++], bytes);
    if (idx >= kNumValues) idx = 0;
  }
  benchmark::DoNotOptimize(dest);
}

BENCHMARK(BM_LittleEndian_Load64VariableLength)->DenseRange(1, 8);
BENCHMARK(BM_BigEndian_Load64VariableLength)->DenseRange(1, 8);
BENCHMARK(BM_LittleEndian_Store64VariableLength)->DenseRange(1, 8);
BENCHMARK(BM_BigEndian_Store64VariableLength)->DenseRange(1, 8);

TEST(BigEndianTest, HtonllNtohllRoundtrip) {
  uint64_t comp = htonll(kInitialNumber);
  EXPECT_EQ(comp, kInitialInNetworkOrder);
  comp = ntohll(kInitialInNetworkOrder);
  EXPECT_EQ(comp, kInitialNumber);
}

TEST(LittleEndianTest, FromHostToHost) {
  // Check LittleEndian uint16.
  uint64_t comp = LittleEndian::FromHost16(k16Value);
  EXPECT_EQ(comp, k16ValueLE);
  comp = LittleEndian::ToHost16(k16ValueLE);
  EXPECT_EQ(comp, k16Value);

  // Check LittleEndian uint32.
  comp = LittleEndian::FromHost32(k32Value);
  EXPECT_EQ(comp, k32ValueLE);
  comp = LittleEndian::ToHost32(k32ValueLE);
  EXPECT_EQ(comp, k32Value);

  // Check LittleEndian uint64.
  comp = LittleEndian::FromHost64(k64Value);
  EXPECT_EQ(comp, k64ValueLE);
  comp = LittleEndian::ToHost64(k64ValueLE);
  EXPECT_EQ(comp, k64Value);
}

TEST(LittleEndianTest, StoreLoad) {
  // Check little-endian Load and store functions.
  uint16_t u16Buf;
  uint32_t u32Buf;
  uint64_t u64Buf;

  LittleEndian::Store16(&u16Buf, k16Value);
  EXPECT_EQ(u16Buf, k16ValueLE);
  uint64_t comp = LittleEndian::Load16(&u16Buf);
  EXPECT_EQ(comp, k16Value);

  LittleEndian::Store32(&u32Buf, k32Value);
  EXPECT_EQ(u32Buf, k32ValueLE);
  comp = LittleEndian::Load32(&u32Buf);
  EXPECT_EQ(comp, k32Value);

  LittleEndian::Store64(&u64Buf, k64Value);
  EXPECT_EQ(u64Buf, k64ValueLE);
  comp = LittleEndian::Load64(&u64Buf);
  EXPECT_EQ(comp, k64Value);
}

TEST(BigEndianTest, StoreLoad) {
  // Check big-endian Load and store functions.
  uint16_t u16Buf;
  uint32_t u32Buf;
  uint64_t u64Buf;
  unsigned char buffer[10];

  BigEndian::Store16(&u16Buf, k16Value);
  EXPECT_EQ(u16Buf, k16ValueBE);
  uint64_t comp = BigEndian::Load16(&u16Buf);
  EXPECT_EQ(comp, k16Value);

  BigEndian::Store32(&u32Buf, k32Value);
  EXPECT_EQ(u32Buf, k32ValueBE);
  comp = BigEndian::Load32(&u32Buf);
  EXPECT_EQ(comp, k32Value);

  BigEndian::Store64(&u64Buf, k64Value);
  EXPECT_EQ(u64Buf, k64ValueBE);
  comp = BigEndian::Load64(&u64Buf);
  EXPECT_EQ(comp, k64Value);

  BigEndian::Store16(buffer + 1, k16Value);
  EXPECT_EQ(u16Buf, k16ValueBE);
  comp = BigEndian::Load16(buffer + 1);
  EXPECT_EQ(comp, k16Value);

  BigEndian::Store32(buffer + 1, k32Value);
  EXPECT_EQ(u32Buf, k32ValueBE);
  comp = BigEndian::Load32(buffer + 1);
  EXPECT_EQ(comp, k32Value);

  BigEndian::Store64(buffer + 1, k64Value);
  EXPECT_EQ(u64Buf, k64ValueBE);
  comp = BigEndian::Load64(buffer + 1);
  EXPECT_EQ(comp, k64Value);
}

TEST(NetworkByteOrderTest, StoreLoad) {
  // Verify typedef of NetworkByteOrder works
  uint64_t u64Buf;
  unsigned char buffer[10];

  NetworkByteOrder::Store64(buffer + 1, k64Value);
  u64Buf = BigEndian::FromHost64(k64Value);
  EXPECT_EQ(u64Buf, k64ValueBE);
  uint64_t comp = NetworkByteOrder::Load64(buffer + 1);
  EXPECT_EQ(comp, k64Value);
}

TEST(BigEndianTest, HtonllNtohllLoop) {
  // Test that htonll and ntohll are each others' inverse functions on a
  // somewhat assorted batch of numbers. 37 is chosen to not be anything
  // particularly nice base 2.
  uint64_t value = 1;
  for (int i = 0; i < 100; ++i) {
    VLOG(1) << value;
    uint64_t comp = htonll(ntohll(value));
    EXPECT_EQ(value, comp);
    comp = ntohll(htonll(value));
    EXPECT_EQ(value, comp);
    value *= 37;
  }
}

TEST(BigEndianTest, GhtonlGntohlRoundtrip) {
  // Test that ghtonl compiles correctly
  uint32_t test = 0x01234567;
  EXPECT_EQ(gntohl(ghtonl(test)), test);
}
