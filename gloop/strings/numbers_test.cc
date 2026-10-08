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

// This file tests string processing functions related to numeric values.

#include "gloop/strings/numbers.h"

#include <sys/types.h>

#include <algorithm>
#include <cctype>
#include <cfenv>  // NOLINT(build/c++11)
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <ios>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "absl/container/btree_set.h"
#include "absl/log/log.h"
#include "absl/numeric/int128.h"
#include "absl/random/random.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "gloop/base/fprint.h"
#include "gloop/strings/numbers_test_common.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace strings {

namespace {

// Run all ParseDoubleRange() tests. Otherwise runs a few.
const bool all_parserange_tests = false;

using ::testing::Optional;

}  // namespace

TEST(ToString, FpToString) {
  EXPECT_EQ(
      "0000"
      "0000"
      "0000"
      "0005",
      FpToString(uint64_t{5}));
  EXPECT_EQ(
      "0000"
      "0000"
      "0000"
      "000a",
      FpToString(uint64_t{10}));
  EXPECT_EQ(
      "0000"
      "0000"
      "0000"
      "0010",
      FpToString(uint64_t{16}));
}

TEST(StringTest, StringToFp) {
  Fprint result;
  EXPECT_TRUE(
      StringToFp("0000"
                 "0000"
                 "0000"
                 "0005",
                 &result));
  EXPECT_EQ(5, result);
  EXPECT_TRUE(
      StringToFp("0000"
                 "0000"
                 "0000"
                 "0010",
                 &result));
  EXPECT_EQ(16, result);
  // Negative testing.
  EXPECT_FALSE(StringToFp("", &result));
  EXPECT_FALSE(StringToFp("42", &result));
  EXPECT_FALSE(
      StringToFp("0000"
                 "0000"
                 "0000"
                 "00042",
                 &result));
  EXPECT_FALSE(
      StringToFp("g000"
                 "0000"
                 "0000"
                 "0042",
                 &result));
  EXPECT_FALSE(
      StringToFp("0000"
                 "000 "
                 "0000"
                 "0042",
                 &result));
  EXPECT_FALSE(
      StringToFp("0x00"
                 "0000"
                 "0000"
                 "0042",
                 &result));
}

TEST(ToString, Uint128ToHexString) {
  EXPECT_EQ(
      "0000"
      "0000"
      "0000"
      "0000"
      "0000"
      "0000"
      "0000"
      "0005",
      Uint128ToHexString(absl::uint128(5)));
  EXPECT_EQ(
      "0000"
      "0000"
      "0000"
      "0000"
      "0000"
      "0000"
      "0000"
      "0010",
      Uint128ToHexString(absl::uint128(16)));
  EXPECT_EQ(
      "0000"
      "0000"
      "0000"
      "0000"
      "0000"
      "0000"
      "7fff"
      "ffff",
      Uint128ToHexString(absl::uint128(uint32_t{0x7fffffff})));
  EXPECT_EQ(
      "0000"
      "0000"
      "0000"
      "0000"
      "0000"
      "0000"
      "8000"
      "0000",
      Uint128ToHexString(absl::uint128(uint32_t{0x80000000})));
  EXPECT_EQ(
      "0000"
      "0000"
      "0000"
      "0000"
      "0000"
      "0000"
      "ffff"
      "ffff",
      Uint128ToHexString(absl::uint128(uint32_t{0xffffffff})));
  EXPECT_EQ(
      "0000"
      "0000"
      "0000"
      "0000"
      "0000"
      "0001"
      "0000"
      "0000",
      Uint128ToHexString(absl::uint128(uint64_t{0x100000000})));
  EXPECT_EQ(
      "0000"
      "0000"
      "0000"
      "0000"
      "7fff"
      "ffff"
      "ffff"
      "ffff",
      Uint128ToHexString(absl::uint128(uint64_t{0x7fffffffffffffff})));
  EXPECT_EQ(
      "0000"
      "0000"
      "0000"
      "0000"
      "8000"
      "0000"
      "0000"
      "0000",
      Uint128ToHexString(absl::uint128(uint64_t{0x8000000000000000})));
  EXPECT_EQ(
      "0000"
      "0000"
      "0000"
      "0000"
      "ffff"
      "ffff"
      "ffff"
      "ffff",
      Uint128ToHexString(absl::uint128(uint64_t{0xffffffffffffffff})));
  EXPECT_EQ(
      "0000"
      "0000"
      "0000"
      "0001"
      "0000"
      "0000"
      "0000"
      "0000",
      Uint128ToHexString(absl::MakeUint128(1, 0)));
  EXPECT_EQ(
      "8000"
      "0000"
      "0000"
      "0000"
      "0000"
      "0000"
      "0000"
      "0000",
      Uint128ToHexString(absl::MakeUint128(uint64_t{0x8000000000000000}, 0)));
  EXPECT_EQ(
      "7fff"
      "ffff"
      "ffff"
      "ffff"
      "ffff"
      "ffff"
      "ffff"
      "ffff",
      Uint128ToHexString(absl::MakeUint128(uint64_t{0x7fffffffffffffff},
                                           uint64_t{0xffffffffffffffff})));
  EXPECT_EQ(
      "ffff"
      "ffff"
      "ffff"
      "ffff"
      "ffff"
      "ffff"
      "ffff"
      "ffff",
      Uint128ToHexString(absl::MakeUint128(uint64_t{0xffffffffffffffff},
                                           uint64_t{0xffffffffffffffff})));
  EXPECT_EQ(
      "ffff"
      "ffff"
      "ffff"
      "ffff"
      "ffff"
      "ffff"
      "ffff"
      "ffff",
      Uint128ToHexString(absl::uint128(-1)));
}

TEST(StringTest, HexStringToUint128) {
  absl::uint128 result;
  EXPECT_TRUE(HexStringToUint128("5", &result));
  EXPECT_EQ(result, absl::uint128(5));
  EXPECT_TRUE(
      HexStringToUint128("0000"
                         "0000"
                         "0000"
                         "0000"
                         "0000"
                         "0000"
                         "0000"
                         "0010",
                         &result));
  EXPECT_EQ(result, absl::uint128(16));
  EXPECT_TRUE(
      HexStringToUint128("0000"
                         "0000"
                         "0000"
                         "0000"
                         "0000"
                         "0000"
                         "7fff"
                         "ffff",
                         &result));
  EXPECT_EQ(result, absl::uint128(uint32_t{0x7fffffff}));
  EXPECT_TRUE(
      HexStringToUint128("0000"
                         "0000"
                         "0000"
                         "0000"
                         "0000"
                         "0000"
                         "8000"
                         "0000",
                         &result));
  EXPECT_EQ(result, absl::uint128(uint32_t{0x80000000}));
  EXPECT_TRUE(
      HexStringToUint128("0000"
                         "0000"
                         "0000"
                         "0000"
                         "0000"
                         "0000"
                         "ffff"
                         "ffff",
                         &result));
  EXPECT_EQ(result, absl::uint128(uint32_t{0xffffffff}));
  EXPECT_TRUE(
      HexStringToUint128("0000"
                         "0000"
                         "0000"
                         "0000"
                         "0000"
                         "0001"
                         "0000"
                         "0000",
                         &result));
  EXPECT_EQ(result, absl::uint128(uint64_t{0x100000000}));
  EXPECT_TRUE(
      HexStringToUint128("0000"
                         "0000"
                         "0000"
                         "0000"
                         "7fff"
                         "ffff"
                         "ffff"
                         "ffff",
                         &result));
  EXPECT_EQ(result, absl::uint128(uint64_t{0x7fffffffffffffff}));
  EXPECT_TRUE(
      HexStringToUint128("0000"
                         "0000"
                         "0000"
                         "0000"
                         "8000"
                         "0000"
                         "0000"
                         "0000",
                         &result));
  EXPECT_EQ(result, absl::uint128(uint64_t{0x8000000000000000}));
  EXPECT_TRUE(
      HexStringToUint128("0000"
                         "0000"
                         "0000"
                         "0000"
                         "ffff"
                         "ffff"
                         "ffff"
                         "ffff",
                         &result));
  EXPECT_EQ(result, absl::uint128(uint64_t{0xffffffffffffffff}));
  EXPECT_TRUE(
      HexStringToUint128("0000"
                         "0000"
                         "0000"
                         "0001"
                         "0000"
                         "0000"
                         "0000"
                         "0000",
                         &result));
  EXPECT_EQ(result, absl::MakeUint128(1, 0));
  EXPECT_TRUE(
      HexStringToUint128("8000"
                         "0000"
                         "0000"
                         "0000"
                         "0000"
                         "0000"
                         "0000"
                         "0000",
                         &result));
  EXPECT_EQ(result, absl::MakeUint128(uint64_t{0x8000000000000000}, 0));
  EXPECT_TRUE(
      HexStringToUint128("7fff"
                         "ffff"
                         "ffff"
                         "ffff"
                         "ffff"
                         "ffff"
                         "ffff"
                         "ffff",
                         &result));
  EXPECT_EQ(result, absl::MakeUint128(uint64_t{0x7fffffffffffffff},
                                      uint64_t{0xffffffffffffffff}));
  EXPECT_TRUE(
      HexStringToUint128("ffff"
                         "ffff"
                         "ffff"
                         "ffff"
                         "ffff"
                         "ffff"
                         "ffff"
                         "ffff",
                         &result));
  EXPECT_EQ(result, absl::MakeUint128(uint64_t{0xffffffffffffffff},
                                      uint64_t{0xffffffffffffffff}));
  EXPECT_TRUE(
      HexStringToUint128("ffff"
                         "ffff"
                         "ffff"
                         "ffff"
                         "ffff"
                         "ffff"
                         "ffff"
                         "ffff",
                         &result));
  EXPECT_EQ(result, absl::uint128(-1));
  // Negative testing.
  EXPECT_FALSE(HexStringToUint128("", &result));
  EXPECT_FALSE(
      HexStringToUint128("0000"
                         "0000"
                         "0000"
                         "0000"
                         "0000"
                         "0000"
                         "0000"
                         "00042",
                         &result));
  EXPECT_FALSE(
      HexStringToUint128("g000"
                         "0000"
                         "0000"
                         "0000"
                         "0000"
                         "0000"
                         "0000"
                         "0000",
                         &result));
  EXPECT_FALSE(
      HexStringToUint128("0000"
                         "0000"
                         "0000"
                         "000 "
                         "0000"
                         "0000"
                         "0000"
                         "0042",
                         &result));
  EXPECT_FALSE(HexStringToUint128("0x01", &result));
  EXPECT_FALSE(HexStringToUint128("0X01", &result));
}

void CheckInt32(int32_t x) {
  char buffer[kFastToBufferSize];
  char* actual = FastInt32ToBuffer(x, buffer);
  std::string expected = std::to_string(x);
  ASSERT_TRUE(expected == actual)
      << "Expected \"" << expected << "\", Actual \"" << actual << "\", Input "
      << x;
}

void CheckInt64(int64_t x) {
  char buffer[kFastToBufferSize + 3];
  buffer[0] = '*';
  buffer[23] = '*';
  buffer[24] = '*';
  char* actual = FastInt64ToBuffer(x, &buffer[1]);
  std::string expected = std::to_string(x);
  ASSERT_TRUE(expected == actual)
      << "Expected \"" << expected << "\", Actual \"" << actual << "\", Input "
      << x;
  ASSERT_EQ(buffer[0], '*');
  ASSERT_EQ(buffer[23], '*');
  ASSERT_EQ(buffer[24], '*');
}

void CheckUInt32(uint32_t x) {
  char buffer[kFastToBufferSize];
  char* actual = FastUInt32ToBuffer(x, buffer);
  std::string expected = std::to_string(x);
  ASSERT_TRUE(expected == actual)
      << "Expected \"" << expected << "\", Actual \"" << actual << "\", Input "
      << x;
}

void CheckUInt64(uint64_t x) {
  char buffer[kFastToBufferSize + 1];
  char* actual = FastUInt64ToBuffer(x, &buffer[1]);
  std::string expected = std::to_string(x);
  ASSERT_TRUE(expected == actual)
      << "Expected \"" << expected << "\", Actual \"" << actual << "\", Input "
      << x;
}

void CheckHex64(uint64_t v) {
  char expected[kFastToBufferSize];
  std::string actual = absl::StrCat(absl::Hex(v, absl::kZeroPad16));
  absl::SNPrintF(expected, sizeof(expected), "%016x", static_cast<uint64_t>(v));
  ASSERT_TRUE(expected == actual)
      << "Expected \"" << expected << "\", Actual \"" << actual << "\"";
}

void TestFastPrints() {
  for (int i = -100; i <= 100; i++) {
    CheckInt32(i);
    CheckInt64(i);
  }
  for (int i = 0; i <= 100; i++) {
    CheckUInt32(i);
    CheckUInt64(i);
  }
  // Test min int to make sure that works
  CheckInt32(INT_MIN);
  CheckInt32(INT_MAX);
  CheckInt64(LONG_MIN);
  CheckInt64(uint64_t{1000000000});
  CheckInt64(uint64_t{9999999999});
  CheckInt64(uint64_t{100000000000000});
  CheckInt64(uint64_t{999999999999999});
  CheckInt64(uint64_t{1000000000000000000});
  CheckInt64(uint64_t{1199999999999999999});
  CheckInt64(int64_t{-700000000000000000});
  CheckInt64(LONG_MAX);
  CheckUInt32(std::numeric_limits<uint32_t>::max());
  CheckUInt64(uint64_t{1000000000});
  CheckUInt64(uint64_t{9999999999});
  CheckUInt64(uint64_t{100000000000000});
  CheckUInt64(uint64_t{999999999999999});
  CheckUInt64(uint64_t{1000000000000000000});
  CheckUInt64(uint64_t{1199999999999999999});
  CheckUInt64(std::numeric_limits<uint64_t>::max());

  for (int i = 0; i < 10000; i++) {
    CheckHex64(i);
  }
  CheckHex64(uint64_t{0x123456789abcdef0});
}

// Test Fast*ToBufferLeft
void CheckFastInt32ToBufferLeft(int32_t num) {
  char buffer[kFastToBufferSize];
  char* buffer_end = FastInt32ToBufferLeft(num, buffer);
  ASSERT_LE(buffer_end - buffer, kFastToBufferSize);
  ASSERT_EQ(*buffer_end, '\0');

  std::string expected = std::to_string(num);
  ASSERT_EQ(expected, buffer)
      << "Expected \"" << expected << "\", Actual \"" << buffer << "\"";
}

void CheckFastInt64ToBufferLeft(int64_t num) {
  char buffer[kFastToBufferSize];
  char* buffer_end = FastInt64ToBufferLeft(num, buffer);
  ASSERT_LE(buffer_end - buffer, kFastToBufferSize);
  ASSERT_EQ(*buffer_end, '\0');

  std::string expected = std::to_string(num);
  ASSERT_EQ(expected, buffer)
      << "Expected \"" << expected << "\", Actual \"" << buffer << "\"";
}

void CheckFastUInt32ToBufferLeft(uint32_t num) {
  char buffer[kFastToBufferSize];
  char* buffer_end = FastUInt32ToBufferLeft(num, buffer);
  ASSERT_LE(buffer_end - buffer, kFastToBufferSize);
  ASSERT_EQ(*buffer_end, '\0');

  std::string expected = std::to_string(num);
  ASSERT_EQ(expected, buffer)
      << "Expected \"" << expected << "\", Actual \"" << buffer << "\"";
}

void CheckFastUInt64ToBufferLeft(uint64_t num) {
  char buffer[kFastToBufferSize];
  char* buffer_end = FastUInt64ToBufferLeft(num, buffer);
  ASSERT_LE(buffer_end - buffer, kFastToBufferSize);
  ASSERT_EQ(*buffer_end, '\0');

  std::string expected = std::to_string(num);
  ASSERT_EQ(expected, buffer)
      << "Expected \"" << expected << "\", Actual \"" << buffer << "\"";
}

template <typename int_type>
void CheckFastIntToBufferLeft(int_type num) {
  char buffer[kFastToBufferSize];
  char* buffer_end = FastIntToBufferLeft(num, buffer);
  ASSERT_LE(buffer_end - buffer, kFastToBufferSize);
  ASSERT_EQ(*buffer_end, '\0');

  std::string expected = std::to_string(num);
  ASSERT_EQ(expected, buffer);
}

void CompareFastInt32ToBufferLeft() {
  int sum1 = 0;
  char buf[kFastToBufferSize];
  for (int32_t i = 1; i <= 2100000000; i += 1 + (i / 1024)) {
    char* buf_start = &buf[0];
    char* buf_end = FastInt32ToBufferLeft(i, buf);
    sum1 += buf_start[0] + buf_start[1] + buf_end[-1];
    buf_start = &buf[0];
    buf_end = FastInt32ToBufferLeft(-i, buf);
    sum1 += buf_start[0] + buf_start[1] + buf_end[-1];
  }
  int sum2 = 0;
  for (int32_t i = 1; i <= 2100000000; i += 1 + (i / 1024)) {
    char* buf_start = FastInt32ToBuffer(i, buf);
    char* buf_end = buf_start;
    while (*++buf_end != 0) {
    }
    sum2 += buf_start[0] + buf_start[1] + buf_end[-1];
    buf_start = FastInt32ToBuffer(-i, buf);
    buf_end = buf_start;
    while (*++buf_end != 0) {
    }
    sum2 += buf_start[0] + buf_start[1] + buf_end[-1];
  }
  ASSERT_EQ(sum1, sum2);
}

void CompareFastInt64ToBufferLeft() {
  int sum1 = 0;
  char buf[kFastToBufferSize];
  // Use of 1ULL here forces the calculation to be done with unsigned types,
  // and avoids overflow (and hence undefined behavior).
  for (int64_t i = 1;
       i + 1ULL + (i / 1024) < std::numeric_limits<int64_t>::max();
       i += 1 + (i / 1024)) {
    char* buf_start = &buf[0];
    char* buf_end = FastInt64ToBufferLeft(i, buf);
    sum1 += buf_start[0] + buf_start[1] + buf_end[-1];
    buf_start = &buf[0];
    buf_end = FastInt64ToBufferLeft(-i, buf);
    sum1 += buf_start[0] + buf_start[1] + buf_end[-1];
  }
  int sum2 = 0;
  for (int64_t i = 1;
       i + 1ULL + (i / 1024) < std::numeric_limits<int64_t>::max();
       i += 1 + (i / 1024)) {
    char* buf_start = FastInt64ToBuffer(i, buf);
    char* buf_end = buf_start;
    while (*++buf_end != 0) {
    }
    sum2 += buf_start[0] + buf_start[1] + buf_end[-1];
    buf_start = FastInt64ToBuffer(-i, buf);
    buf_end = buf_start;
    while (*++buf_end != 0) {
    }
    sum2 += buf_start[0] + buf_start[1] + buf_end[-1];
  }
  ASSERT_EQ(sum1, sum2);
}

void TestFastToBufferLefts() {
  for (int8_t i = INT8_MIN;; ++i) {
    CheckFastUInt32ToBufferLeft(i);
    CheckFastUInt64ToBufferLeft(i);
    CheckFastInt32ToBufferLeft(i);
    CheckFastInt64ToBufferLeft(i);
    CheckFastIntToBufferLeft(i);
    if (i == INT8_MAX) break;
  }
  for (uint8_t i = 0;; ++i) {
    CheckFastUInt32ToBufferLeft(i);
    CheckFastUInt64ToBufferLeft(i);
    CheckFastInt32ToBufferLeft(i);
    CheckFastInt64ToBufferLeft(i);
    CheckFastIntToBufferLeft(i);
    if (i == UINT8_MAX) break;
  }
  for (int16_t i = INT16_MIN;; ++i) {
    CheckFastUInt32ToBufferLeft(i);
    CheckFastUInt64ToBufferLeft(i);
    CheckFastInt32ToBufferLeft(i);
    CheckFastInt64ToBufferLeft(i);
    CheckFastIntToBufferLeft(i);
    if (i == INT16_MAX) break;
  }
  for (uint16_t i = 0;; ++i) {
    CheckFastUInt32ToBufferLeft(i);
    CheckFastUInt64ToBufferLeft(i);
    CheckFastInt32ToBufferLeft(i);
    CheckFastInt64ToBufferLeft(i);
    CheckFastIntToBufferLeft(i);
    if (i == UINT16_MAX) break;
  }

  for (int i = 0; i <= 100; i++) {
    CheckFastUInt32ToBufferLeft(i);
    CheckFastUInt64ToBufferLeft(i);
    CheckFastInt32ToBufferLeft(i);
    CheckFastInt64ToBufferLeft(i);
    CheckFastInt32ToBufferLeft(-i);
    CheckFastInt64ToBufferLeft(-i);
    CheckFastIntToBufferLeft(i);
    CheckFastIntToBufferLeft(-i);
  }

  for (uint32_t i = 10; i <= 1000000000; i *= 10) {
    CheckFastUInt32ToBufferLeft(i - 1);
    CheckFastUInt32ToBufferLeft(i);
    CheckFastUInt32ToBufferLeft(i + 1);
    CheckFastInt32ToBufferLeft(i - 1);
    CheckFastInt32ToBufferLeft(i);
    CheckFastInt32ToBufferLeft(i + 1);
    CheckFastInt32ToBufferLeft(-(i - 1));
    CheckFastInt32ToBufferLeft(-i);
    CheckFastInt32ToBufferLeft(-(i + 1));
    CheckFastIntToBufferLeft(i - 1);
    CheckFastIntToBufferLeft(i);
    CheckFastIntToBufferLeft(i + 1);
    CheckFastIntToBufferLeft(-(i - 1));
    CheckFastIntToBufferLeft(-i);
    CheckFastIntToBufferLeft(-(i + 1));
  }

  for (uint64_t i = 10; i <= uint64_t{10000000000000000000u}; i *= 10) {
    CheckFastUInt64ToBufferLeft(i - 1);
    CheckFastUInt64ToBufferLeft(i);
    CheckFastUInt64ToBufferLeft(i + 1);
    CheckFastInt64ToBufferLeft(i - 1);
    CheckFastInt64ToBufferLeft(i);
    CheckFastInt64ToBufferLeft(i + 1);
    CheckFastInt64ToBufferLeft(-(i - 1));
    CheckFastInt64ToBufferLeft(-i);
    CheckFastInt64ToBufferLeft(-(i + 1));
    CheckFastIntToBufferLeft(i - 1);
    CheckFastIntToBufferLeft(i);
    CheckFastIntToBufferLeft(i + 1);
    CheckFastIntToBufferLeft(-(i - 1));
    CheckFastIntToBufferLeft(-i);
    CheckFastIntToBufferLeft(-(i + 1));
  }

  // Test the limits to make sure they work
  CheckFastInt32ToBufferLeft(std::numeric_limits<int32_t>::min());
  CheckFastInt32ToBufferLeft(std::numeric_limits<int32_t>::max());
  CheckFastIntToBufferLeft(std::numeric_limits<int32_t>::min());
  CheckFastIntToBufferLeft(std::numeric_limits<int32_t>::max());

  CheckFastInt64ToBufferLeft(std::numeric_limits<int64_t>::min());
  CheckFastInt64ToBufferLeft(uint64_t{1199999999999999999});
  CheckFastInt64ToBufferLeft(int64_t{-700000000000000000});
  CheckFastInt64ToBufferLeft(std::numeric_limits<int64_t>::max());
  CheckFastIntToBufferLeft(std::numeric_limits<int64_t>::min());
  CheckFastIntToBufferLeft(uint64_t{1199999999999999999});
  CheckFastIntToBufferLeft(int64_t{-700000000000000000});
  CheckFastIntToBufferLeft(std::numeric_limits<int64_t>::max());

  CheckFastUInt32ToBufferLeft(std::numeric_limits<uint32_t>::max());
  CheckFastUInt64ToBufferLeft(std::numeric_limits<uint64_t>::max());
  CheckFastIntToBufferLeft(std::numeric_limits<uint32_t>::max());
  CheckFastIntToBufferLeft(std::numeric_limits<uint64_t>::max());

  const uint64_t top_11_limit = (uint64_t{1} << 32) * 1000000000;
  CheckFastUInt64ToBufferLeft(top_11_limit - 1);
  CheckFastUInt64ToBufferLeft(top_11_limit);
  CheckFastUInt64ToBufferLeft(top_11_limit + 1);
  CheckFastUInt64ToBufferLeft(uint64_t{1199999999999999999});
  CheckFastUInt64ToBufferLeft(std::numeric_limits<uint64_t>::max());
  CheckFastIntToBufferLeft(top_11_limit - 1);
  CheckFastIntToBufferLeft(top_11_limit);
  CheckFastIntToBufferLeft(top_11_limit + 1);
  CheckFastIntToBufferLeft(uint64_t{1199999999999999999});
  CheckFastIntToBufferLeft(std::numeric_limits<uint64_t>::max());

  // Compare results of FastIntXXToBufferLeft against FastIntXXToBuffer
  CompareFastInt64ToBufferLeft();
  CompareFastInt32ToBufferLeft();
}

inline void TestParseDoubleRange(const char* from_str, const char* to_str,
                                 const char* sep, const char* tail,
                                 bool negfrom, bool negto, bool curr1,
                                 bool curr2, int len, const char* allowed_seps,
                                 bool req_seps, int terminators, bool allow_unb,
                                 uint32_t bounds, bool dont_mod, bool dollar_ok,
                                 const char* comparator, bool comparators_ok) {
  // not interested in these meaningless test cases
  if (*comparator != '\0') {
    // sep, allowed_seps, req_seps, allow_unb, bounds irrelevant, so
    // one test case only
    if ((*sep != '\0') || req_seps || allow_unb || (bounds > 0) ||
        allowed_seps[0] != '-' || allowed_seps[1] != '\0')
      return;
    // similarly, either 'from' or 'to' irrelevant, so one test case only
    if ((*comparator == '>') && (negto || (*to_str) || (*from_str == '\0')))
      return;
    if ((*comparator == '<') && (negfrom || (*from_str) || (*to_str == '\0')))
      return;
  }

  if (len == 1 && isdigit(tail[0])) return;

  if ((*sep == '\0') && (negto || curr2 || (*to_str != '\0'))) return;

  if ((*from_str == '\0') &&
      (negfrom || (*sep == '-' && !strchr(allowed_seps, '-'))))
    return;

  if ((terminators > 0) && (*tail == '\0')) return;

  if (negfrom && (*sep != '-') && strchr(allowed_seps, '-')) return;

  if (curr1 && (*from_str == '\0') && (*sep == '-')) return;

  if ((terminators > 0) && (*tail == ' ') &&
      ((*to_str == ' ') || (*from_str == ' ')))
    return;

  if ((*sep == '.') && (*to_str == '.') && !curr2) {
    // if from_str concatenated with a '.' could be mistaken for a
    // number, this is a malformed test.
    char* end;
    const std::string buf = std::string(from_str) + '.';
    strtod(buf.c_str(), &end);
    if (end - buf.c_str() == buf.size()) return;
  }

  static int trials = 0;
  ++trials;

  // Check policy on acceptable terminators. Here are the meanings:
  // termintors=0: Only \0 is an acceptable terminator.
  // termintors=1: Only tail[0] is an acceptable terminator.
  // termintors=2: Both \0 and tail[0] are acceptable terminators.
  char buf[2] = {tail[0], '\0'};
  const char* acceptable_terminators = buf;
  if (terminators == 0) ++acceptable_terminators;  // tail[0] not ok
  bool null_terminator_ok = (terminators != 1);

  double kDefault = 3.14159;  // arbitrary

  // Figure out whether the input is acceptable & what to expect
  bool expected_ok = false;
  double expected_from = dont_mod ? kDefault : (-HUGE_VAL);
  double expected_to = dont_mod ? kDefault : (HUGE_VAL);
  bool expected_dollar = (curr1 || curr2) && dollar_ok;
  while (true) {  // This while loop is only for the 'break' statements within.
    if (req_seps && (*sep == '\0')) break;
    if (bounds > (*from_str != '\0') + (*to_str != '\0')) break;
    if (!allow_unb && ((*from_str == '?') || (*to_str == '?'))) break;
    if (terminators == 1 && len != 1) break;
    if (terminators == 0 && len == 1) break;
    if ((curr1 || curr2) && !dollar_ok) break;
    if ((curr1 && (*from_str == '\0')) || (curr2 && (*to_str == '\0'))) break;
    if (dollar_ok && !curr1 && (*from_str != '\0') && curr2) break;
    if (*from_str == ' ' || *to_str == ' ')  // blanks not allowed
      break;
    if (*from_str == '\t' || *to_str == '\t')  // blanks not allowed
      break;
    if (negfrom && ((*from_str == '\0') || (*from_str == '?'))) break;
    if (negto && ((*to_str == '\0') || (*to_str == '?'))) break;
    if ((*comparator != '\0') && !comparators_ok) break;
    char* end;
    if ((*from_str != '\0') && strcmp(from_str, "?")) {
      expected_from = strtod(from_str, &end);
      if (negfrom) expected_from = -expected_from;
      if (end - from_str != strlen(from_str)) break;
    }
    if ((*to_str != '\0') && strcmp(to_str, "?")) {
      expected_to = strtod(to_str, &end);
      if (negto) expected_to = -expected_to;
      if (end - to_str != strlen(to_str)) break;
      // It could happen that some prefix of to_str is an acceptable
      // string. However, all of our test inputs have the property
      // that junk within to_str is not among the acceptable
      // terminators, so all such cases should be errors.
    }
    if (strchr(allowed_seps, *sep) == nullptr) break;

    expected_ok = true;
    break;
  }

  // construct input string
  char text[1024];
  int input_len =
      absl::SNPrintF(text, sizeof(text), "%s%s%s%s%s%s%s%s", comparator,
                     curr1 ? "$" : "", negfrom ? "-" : "", from_str, sep,
                     curr2 ? "$" : "", negto ? "-" : "", to_str);
  ASSERT_GT(sizeof(text), input_len);
  if (len >= 0) {
    int tail_len =
        absl::SNPrintF(text + input_len, sizeof(text) - input_len, "%s", tail);
    ASSERT_GT(sizeof(text), input_len + tail_len);
    if (len == 0)
      len = input_len;
    else
      len = input_len + tail_len;
  }

  const char* end = nullptr;
  double from = kDefault;
  double to = kDefault;
  bool dollar_result = false;

  DoubleRangeOptions opts;
  opts.separators = allowed_seps;
  opts.require_separator = req_seps;
  opts.acceptable_terminators = acceptable_terminators;
  opts.null_terminator_ok = null_terminator_ok;
  opts.allow_unbounded_markers = allow_unb;
  opts.num_required_bounds = bounds;
  opts.dont_modify_unbounded = dont_mod;
  opts.allow_currency = dollar_ok;
  opts.allow_comparators = comparators_ok;
  bool result =
      ParseDoubleRange(text, len, &end, &from, &to, &dollar_result, opts);
  ASSERT_FALSE(result != expected_ok ||
               (result && (from != expected_from || to != expected_to ||
                           dollar_result != expected_dollar ||
                           (end - text != input_len))))
      << "FAILED trial " << trials << " of ParseDoubleRange(\"" << text
      << "\", len:" << len << ", seps:\"" << allowed_seps
      << "\" require_separators:" << req_seps
      << " acceptable_terminator_policy:" << terminators
      << " allow_unbounded_markers:" << allow_unb
      << " min_required_bounds:" << bounds
      << " dont_modify_unbounded:" << dont_mod
      << " allow_currency:" << dollar_ok << " comparators_ok:" << comparators_ok
      << "): " << " Result:" << result << " (expected " << expected_ok << ")"
      << "   From:" << from << " (expected " << expected_from << ")"
      << "   To:" << to << " (expected " << expected_to << ")"
      << "   Length:" << (end - text) << " (expected " << input_len
      << ")   Currency:" << dollar_result << " (expected " << expected_dollar
      << ")";
}

TEST(Numbers, ParseDoubleRange) {
  // some numbers to test
  const char* test_strings[] = {"",    "?",     "300e-5",  "234.",   "15.2987",
                                "1",   "0.348", ".0035",   "0",      "crap",
                                "  1", "\t1",   "23foo23", ".23foo", nullptr};
  const char* separator_list[] = {"", "-", "~", "..", nullptr};
  const char* tails[] = {" ", "0", "1", "e", "qqqq", nullptr};
  const char* allowed_seps_list[] = {"-.", "-", "~", "-~", "~-", ".", nullptr};
  const char* comparators[] = {"", "<", "<=", ">", ">=", nullptr};

  int string_tuples = 0;
  for (const char** allowed_seps = allowed_seps_list; *allowed_seps;
       ++allowed_seps)
    for (const char** from_str = test_strings; *from_str; ++from_str)
      for (const char** to_str = test_strings; *to_str; ++to_str)
        for (const char** sep = separator_list; *sep; ++sep)
          for (const char** tail = tails; *tail; ++tail)
            for (const char** comp = comparators; *comp; ++comp) {
              // Only run about 1% of tests normally
              ++string_tuples;
              if (!all_parserange_tests && (string_tuples % 101 != 0)) {
                continue;
              }

              for (int comp_ok = 0; comp_ok < 2; ++comp_ok)
                for (int negfrom = 0; negfrom < 2; ++negfrom)
                  for (int negto = 0; negto < 2; ++negto)
                    for (int curr1 = 0; curr1 < 2; ++curr1)
                      for (int curr2 = 0; curr2 < 2; ++curr2)
                        for (int len = -1; len < 2; ++len)
                          for (int req_seps = 0; req_seps < 2; ++req_seps)
                            for (int allow_unb = 0; allow_unb < 2; ++allow_unb)
                              for (int termins = 0; termins < 3; ++termins)
                                for (int bounds = 0; bounds < 3; ++bounds)
                                  for (int dolr_ok = 0; dolr_ok < 2; ++dolr_ok)
                                    for (int no_mod = 0; no_mod < 2; ++no_mod)
                                      TestParseDoubleRange(
                                          *from_str, *to_str, *sep, *tail,
                                          negfrom, negto, curr1, curr2, len,
                                          *allowed_seps, req_seps, termins,
                                          allow_unb, bounds, no_mod, dolr_ok,
                                          *comp, comp_ok);
            }
}

// Wrapper to test AutoDigitLessThan with a pair of strings, where the
// first string is supposed to be less than the second.
void AutoCompare(const char* a, const char* b) {
  absl::string_view apiece(a), bpiece(b);
  EXPECT_TRUE(AutoDigitLessThan(apiece, bpiece)) << ": " << a << "/" << b;
  EXPECT_FALSE(AutoDigitLessThan(bpiece, apiece)) << ": " << a << "/" << b;
  EXPECT_FALSE(AutoDigitLessThan(apiece, apiece)) << ": " << a;
  EXPECT_FALSE(AutoDigitLessThan(bpiece, bpiece)) << ": " << b;
  EXPECT_LT(AutoDigitStrCmp(apiece, bpiece, false), 0);
  EXPECT_GT(AutoDigitStrCmp(bpiece, apiece, false), 0);
  EXPECT_LT(AutoDigitStrCmpZ(a, b, false), 0);
  EXPECT_GT(AutoDigitStrCmpZ(b, a, false), 0);
}

// Like the above, but performs strict checks.
void StrictAutoCompare(const char* a, const char* b) {
  absl::string_view apiece(a), bpiece(b);
  EXPECT_TRUE(StrictAutoDigitLessThan(apiece, bpiece)) << ": " << a << "/" << b;
  EXPECT_FALSE(StrictAutoDigitLessThan(bpiece, apiece))
      << ": " << a << "/" << b;
  EXPECT_FALSE(StrictAutoDigitLessThan(apiece, apiece)) << ": " << a;
  EXPECT_FALSE(StrictAutoDigitLessThan(bpiece, bpiece)) << ": " << b;
  EXPECT_LT(AutoDigitStrCmp(apiece, bpiece, true), 0);
  EXPECT_GT(AutoDigitStrCmp(bpiece, apiece, true), 0);
  EXPECT_LT(AutoDigitStrCmpZ(a, b, true), 0);
  EXPECT_GT(AutoDigitStrCmpZ(b, a, true), 0);
}

void TestAutoDigit() {
  // Normal lexicographic checks: no digits
  AutoCompare("", "a");
  AutoCompare("a", "ab");
  AutoCompare("ab", "b");
  AutoCompare("abcd", "abd");
  AutoCompare("abcd", "xbcd");

  // Numerical comparisons
  AutoCompare("0", "1");
  AutoCompare("0", "0001");
  AutoCompare("1", "00002");
  AutoCompare("2", "10");

  // Mixed case
  AutoCompare("exaf2:3830", "exaf10:3830");

  // Test strict comparison
  StrictAutoCompare("0", "1");
  StrictAutoCompare("1", "2");
  // Leading zeroes make you smaller
  StrictAutoCompare("01", "1");
  StrictAutoCompare("001", "01");
  StrictAutoCompare("00", "0");
  StrictAutoCompare("0000", "000");
  // Leading zeroes don't change the fact that x < y
  StrictAutoCompare("1", "0002");
  StrictAutoCompare("01", "0010");
  // Leading zeroes don't make you less than the empty string
  StrictAutoCompare("", "0");

  const char* a = "012";
  const char* b = "12";
  absl::string_view apiece(a), bpiece(b);
  // Non-strict should compare them as equal
  EXPECT_EQ(AutoDigitStrCmp(apiece, bpiece, false), 0);
  EXPECT_EQ(AutoDigitStrCmpZ(a, b, false), 0);
  // Strict should yield a < b
  EXPECT_LT(AutoDigitStrCmp(apiece, bpiece, true), 0);
  EXPECT_LT(AutoDigitStrCmpZ(a, b, true), 0);

  // A fun case: in strict mode, c < d because 01 < 1.
  // In relaxed mode, c > d because 3 > 2.
  const char* c = "01x3";
  const char* d = "1x2";
  StrictAutoCompare(c, d);
  AutoCompare(d, c);

  const std::vector<std::string> vref = {
      "0", "1", "2", "010", "020", "z01", "z1", "021", "0010", "0001", "1z"};
  const std::vector<std::string> sorted = {
      "0", "0001", "1", "1z", "2", "0010", "010", "020", "021", "z01", "z1"};
  const std::vector<std::string> rsorted{sorted.rbegin(), sorted.rend()};

  auto v = vref;
  std::sort(v.begin(), v.end(), strict_autodigit_less());
  EXPECT_EQ(sorted, v);

  v = vref;
  std::sort(v.begin(), v.end(), strict_autodigit_greater());
  EXPECT_EQ(rsorted, v);

  const std::vector<std::string> stablesorted = {
      "0", "1", "0001", "1z", "2", "010", "0010", "020", "021", "z01", "z1"};
  const std::vector<std::string> rstablesorted = {
      "z01", "z1", "021", "020", "010", "0010", "2", "1z", "1", "0001", "0"};

  v = vref;
  std::stable_sort(v.begin(), v.end(), autodigit_less());
  EXPECT_EQ(stablesorted, v);

  v = vref;
  std::stable_sort(v.begin(), v.end(), autodigit_greater());
  EXPECT_EQ(rstablesorted, v);

  // Test heterogeneous lookups.
  constexpr absl::string_view view0 = "0";
  constexpr absl::string_view view01 = "01";
  {
    absl::btree_set<std::string, autodigit_less> s = {"0", "1", "10"};
    EXPECT_TRUE(s.contains(view0));
    EXPECT_TRUE(s.contains(view01));
  }

  {
    absl::btree_set<std::string, autodigit_greater> s = {"0", "1", "10"};
    EXPECT_TRUE(s.contains(view0));
    EXPECT_TRUE(s.contains(view01));
  }

  {
    absl::btree_set<std::string, strict_autodigit_less> s = {"0", "1", "10"};
    EXPECT_TRUE(s.contains(view0));
    EXPECT_FALSE(s.contains(view01));
  }

  {
    absl::btree_set<std::string, strict_autodigit_greater> s = {"0", "1", "10"};
    EXPECT_TRUE(s.contains(view0));
    EXPECT_FALSE(s.contains(view01));
  }
}

void TestParseLeadingDoubleValue() {
  const int num_tests = 8;
  const double default_out = 1000.0;
  const char* inputs[] = {
      "1.000010",  "-1.000010", "sdsdsdsdksj", "1.0e+2", "1.0e-2", "",
      "1.0e+1000",  // overflow
      "1.0e-1000"   // underflow
  };

  const double outputs[] = {1.000010, -1.000010,   default_out, 1.0e+2,
                            1.0e-2,   default_out, default_out, default_out};

  for (int i = 0; i < num_tests; i++) {
    EXPECT_EQ(ParseLeadingDoubleValue(inputs[i], default_out), outputs[i]);
    EXPECT_EQ(ParseLeadingDoubleValue(std::string(inputs[i]), default_out),
              outputs[i]);
  }
}

void TestConsumeStrayLeadingZeroes() {
  struct TestCase {
    const char* before;
    const char* after;
  };

  static const TestCase tests[] = {
      // noop
      {"", ""},

      // noop
      {"0", "0"},

      // noop
      {"1", "1"},

      // noop
      {"xyz", "xyz"},

      // all zeroes
      {"000", "0"},

      // a regular old test
      {"0123", "123"},

      // more exotic tests that disambiguate
      // any numeric notions of zero...
      {"0\t", "\t"},
      {"03", "3"},
      {"0x027", "x027"},
      {"-007", "-007"},
      {"0.25", ".25"},
      {"0.0125", ".0125"},
      {"0.", "."},
      {".01", ".01"},
      {"0e2", "e2"},
  };

  for (const TestCase& test : tests) {
    std::string s(test.before);
    ConsumeStrayLeadingZeroes(&s);
    EXPECT_STREQ(s.c_str(), test.after);
  }
}

void TestLeading32(const char* str, int32_t deflt, int32_t i32, int32_t d32,
                   uint32_t ui32, uint32_t ud32) {
  EXPECT_EQ(i32, ParseLeadingInt32Value(str, deflt)) << str;
  EXPECT_EQ(d32, ParseLeadingDec32Value(str, deflt)) << str;
  EXPECT_EQ(ui32, ParseLeadingUInt32Value(str, deflt)) << str;
  EXPECT_EQ(ud32, ParseLeadingUDec32Value(str, deflt)) << str;

  const std::string s(str);
  EXPECT_EQ(i32, ParseLeadingInt32Value(s, deflt)) << s;
  EXPECT_EQ(d32, ParseLeadingDec32Value(s, deflt)) << s;
  EXPECT_EQ(ui32, ParseLeadingUInt32Value(s, deflt)) << s;
  EXPECT_EQ(ud32, ParseLeadingUDec32Value(s, deflt)) << s;

  const std::string space_prefix = "\n\t " + s;
  EXPECT_EQ(i32, ParseLeadingInt32Value(space_prefix, deflt)) << space_prefix;
  EXPECT_EQ(d32, ParseLeadingDec32Value(space_prefix, deflt)) << space_prefix;
  EXPECT_EQ(ui32, ParseLeadingUInt32Value(space_prefix, deflt)) << space_prefix;
  EXPECT_EQ(ud32, ParseLeadingUDec32Value(space_prefix, deflt)) << space_prefix;

  // Test that we are indeed parsing successfully the 'leading' part
  const std::string suffixed = s + ":";  // Typical field separator
  EXPECT_EQ(i32, ParseLeadingInt32Value(suffixed, deflt)) << suffixed;
  EXPECT_EQ(d32, ParseLeadingDec32Value(suffixed, deflt)) << suffixed;
  EXPECT_EQ(ui32, ParseLeadingUInt32Value(suffixed, deflt)) << suffixed;
  EXPECT_EQ(ud32, ParseLeadingUDec32Value(suffixed, deflt)) << suffixed;

  // If there are valid digits in memory right after the absl::string_view, make
  // sure that is not parsed beyond the end of the chosen range.
  const std::string with_suffix_digit = s + "123";
  const absl::string_view prefix_piece =
      absl::string_view(with_suffix_digit).substr(0, s.length());
  EXPECT_EQ(i32, ParseLeadingInt32Value(prefix_piece, deflt)) << prefix_piece;
  EXPECT_EQ(d32, ParseLeadingDec32Value(prefix_piece, deflt)) << prefix_piece;
  EXPECT_EQ(ui32, ParseLeadingUInt32Value(prefix_piece, deflt)) << prefix_piece;
  EXPECT_EQ(ud32, ParseLeadingUDec32Value(prefix_piece, deflt)) << prefix_piece;
}

void TestLeading64(const char* str, int64_t deflt, int64_t i64, int64_t d64,
                   uint64_t ui64, uint64_t ud64, uint64_t uh64) {
  EXPECT_EQ(i64, ParseLeadingInt64Value(str, deflt)) << str;
  EXPECT_EQ(d64, ParseLeadingDec64Value(str, deflt)) << str;
  EXPECT_EQ(ui64, ParseLeadingUInt64Value(str, deflt)) << str;
  EXPECT_EQ(ud64, ParseLeadingUDec64Value(str, deflt)) << str;
  EXPECT_EQ(uh64, ParseLeadingHex64Value(str, deflt)) << str;

  const std::string s(str);
  EXPECT_EQ(i64, ParseLeadingInt64Value(s, deflt)) << s;
  EXPECT_EQ(d64, ParseLeadingDec64Value(s, deflt)) << s;
  EXPECT_EQ(ui64, ParseLeadingUInt64Value(s, deflt)) << s;
  EXPECT_EQ(ud64, ParseLeadingUDec64Value(s, deflt)) << s;
  EXPECT_EQ(uh64, ParseLeadingHex64Value(s, deflt)) << s;

  const std::string space_prefix = "\n\t " + s;
  EXPECT_EQ(i64, ParseLeadingInt64Value(space_prefix, deflt)) << space_prefix;
  EXPECT_EQ(d64, ParseLeadingDec64Value(space_prefix, deflt)) << space_prefix;
  EXPECT_EQ(ui64, ParseLeadingUInt64Value(space_prefix, deflt)) << space_prefix;
  EXPECT_EQ(ud64, ParseLeadingUDec64Value(space_prefix, deflt)) << space_prefix;
  EXPECT_EQ(uh64, ParseLeadingHex64Value(space_prefix, deflt)) << space_prefix;

  // Test that we are indeed parsing successfully the 'leading' part
  const std::string suffixed = s + ":";  // Typical field separator
  EXPECT_EQ(i64, ParseLeadingInt64Value(suffixed, deflt)) << suffixed;
  EXPECT_EQ(d64, ParseLeadingDec64Value(suffixed, deflt)) << suffixed;
  EXPECT_EQ(ui64, ParseLeadingUInt64Value(suffixed, deflt)) << suffixed;
  EXPECT_EQ(ud64, ParseLeadingUDec64Value(suffixed, deflt)) << suffixed;
  EXPECT_EQ(uh64, ParseLeadingHex64Value(suffixed, deflt)) << suffixed;

  // If there are valid digits in memory right after the absl::string_view, make
  // sure that is not parsed beyond the end of the chosen range.
  const std::string with_suffix_digit = s + "123";
  const absl::string_view prefix_piece =
      absl::string_view(with_suffix_digit).substr(0, s.length());
  EXPECT_EQ(i64, ParseLeadingInt64Value(prefix_piece, deflt)) << prefix_piece;
  EXPECT_EQ(d64, ParseLeadingDec64Value(prefix_piece, deflt)) << prefix_piece;
  EXPECT_EQ(ui64, ParseLeadingUInt64Value(prefix_piece, deflt)) << prefix_piece;
  EXPECT_EQ(ud64, ParseLeadingUDec64Value(prefix_piece, deflt)) << prefix_piece;
  EXPECT_EQ(uh64, ParseLeadingHex64Value(prefix_piece, deflt)) << prefix_piece;
}

void TestParseLeadingIntValue() {
  using std::numeric_limits;

  // Using distinctive default value of 42.
  //               default  int dec uint  udec
  TestLeading32("", 42, 42, 42, 42, 42);  // fallback to default.
  TestLeading32("0", 42, 0, 0, 0, 0);
  TestLeading32("16", 0, 16, 16, 16, 16);
  TestLeading32("foo", 0, 0, 0, 0, 0);        // fallback to default.
  TestLeading32("foo", 42, 42, 42, 42, 42);   // fallback to default.
  TestLeading32("zero", 0, 0, 0, 0, 0);       // fallback to default.
  TestLeading32("zero", 42, 42, 42, 42, 42);  // fallback to default.
  TestLeading32("true", 0, 0, 0, 0, 0);       // fallback to default.
  TestLeading32("true", 42, 42, 42, 42, 42);  // fallback to default.
  TestLeading32("0xF", 0, 0xf, 0, 0xf, 0);    // various valid interpretations
  TestLeading32("0xF", 42, 0xf, 0, 0xf, 0);   // ditto. Never using default.
  TestLeading32("0xFF", 42, 0xff, 0, 0xff, 0);
  TestLeading32("077", 42, 077, 77, 077, 77);  // Possibly octal.

  // If we have a 0 or 0x indicating some possible octal or hex
  // interpreation which then is followed by an invalid digit in that
  // base, this is parsed as zero followed by invalid characters.
  TestLeading32("08", 42, 0, 8, 0, 8);  // Valid 0, followed by 8
#ifndef _MSC_VER
  // MSVC C library has a bug with this case. When base == 0, it inteprets 0xG
  // as a hex integer with an invalid form.
  TestLeading32("0xG", 42, 0, 0, 0, 0);  // Valid 0, followed by xG
#endif

  TestLeading32("-1", 42, -1, -1, std::numeric_limits<uint32_t>::max(),
                std::numeric_limits<uint32_t>::max());
  TestLeading32("-2", 42, -2, -2,  // wrapping around properly.
                std::numeric_limits<uint32_t>::max() - 1,
                std::numeric_limits<uint32_t>::max() - 1);
  TestLeading32("0xffffffff", 42,                     // max unsigned value
                std::numeric_limits<int32_t>::max(),  // signed int reached max
                0,                                    // parsed leading zero.
                std::numeric_limits<uint32_t>::max(),
                0);                                   // parsed leading zero
  TestLeading32("0xffffffffffffffffffffff", 42,       // way beyond max
                std::numeric_limits<int32_t>::max(),  // signed int reached max
                0,                                    // parsed leading zero.
                std::numeric_limits<uint32_t>::max(),
                0);                                   // parsed leading zero
  TestLeading32("0xfffffffe", 42,                     // unsigned32_max - 1
                std::numeric_limits<int32_t>::max(),  // signed int reached max
                0,                                    // parsed leading zero.
                std::numeric_limits<uint32_t>::max() - 1,
                0);  // parsed leading zero

  TestLeading32("4294967295", 42,                     // max unsigned decimal
                std::numeric_limits<int32_t>::max(),  // Limited at pos maximum
                std::numeric_limits<int32_t>::max(),  // Limited at pos maximum
                std::numeric_limits<uint32_t>::max(),
                std::numeric_limits<uint32_t>::max());
  TestLeading32(
      "2147483647", 42,  // max signed decimal
      std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::max(),
      std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::max());
  TestLeading32(
      "-2147483648", 42,  // min signed decimal
      std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::min(),
      std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::min());

  // Parsing negative hex values.
  TestLeading32("-0xffff", 42, -0xffff, 0, -0xffff, 0);

  TestLeading32("-500000000", 0, -500000000, -500000000,
                std::numeric_limits<uint32_t>::max() - 500000000 + 1,
                std::numeric_limits<uint32_t>::max() - 500000000 + 1);
  TestLeading32("5000000000", 0, std::numeric_limits<int32_t>::max(),
                std::numeric_limits<int32_t>::max(),
                std::numeric_limits<uint32_t>::max(),
                std::numeric_limits<uint32_t>::max());
  TestLeading32("-5000000000", 0, std::numeric_limits<int32_t>::min(),
                std::numeric_limits<int32_t>::min(),
                std::numeric_limits<uint32_t>::max(),
                std::numeric_limits<uint32_t>::max());

  //               default  int  dec  uint udec uhex
  TestLeading64("", 42, 42, 42, 42, 42, 42);  // fallback to default.
  TestLeading64("0", 42, 0, 0, 0, 0, 0);
  TestLeading64("16", 0, 16, 16, 16, 16, 0x16);
  TestLeading64("foo", 0, 0, 0, 0, 0, 0xf);       // partial hex
  TestLeading64("foo", 42, 42, 42, 42, 42, 0xf);  // ditto.
  TestLeading64("zero", 0, 0, 0, 0, 0, 0);        // fallback to default.
  TestLeading64("zero", 42, 42, 42, 42, 42, 42);  // fallback to default.
  TestLeading64("true", 0, 0, 0, 0, 0, 0);        // fallback to default.
  TestLeading64("true", 42, 42, 42, 42, 42, 42);  // fallback to default.
  TestLeading64("0xF", 0, 0xf, 0, 0xf, 0, 0xf);   // var. interpretations
  TestLeading64("0xF", 42, 0xf, 0, 0xf, 0, 0xf);  // ditto. Never use dflt
  TestLeading64("0xFF", 42, 0xff, 0, 0xff, 0, 0xff);
  TestLeading64("077", 42, 077, 77, 077, 77, 0x77);  // Possibly octal.

  // Invalid digit after octal/hex prefix results in the leading zero to be
  // interpreted. See above.
  TestLeading64("08", 42, 0, 8, 0, 8, 0x8);  // Valid 0, followed by 8
#ifndef _MSC_VER
  // MSVC C library has a bug with this case. When base is 0 or 16, it inteprets
  // 0xG as a hex integer with an invalid form.
  TestLeading64("0xG", 42, 0, 0, 0, 0, 0);  // Valid 0, followed by xG
#endif

  TestLeading64("-1", 42, -1, -1, std::numeric_limits<uint64_t>::max(),
                std::numeric_limits<uint64_t>::max(),
                std::numeric_limits<uint64_t>::max());
  TestLeading64("-2", 42, -2, -2,  // wrapping around properly.
                std::numeric_limits<uint64_t>::max() - 1,
                std::numeric_limits<uint64_t>::max() - 1,
                std::numeric_limits<uint64_t>::max() - 1);

  TestLeading64("0xffffffffffffffff", 42,             // max unsigned value
                std::numeric_limits<int64_t>::max(),  // signed int reached max
                0,                                    // parsed leading zero
                std::numeric_limits<uint64_t>::max(),
                0,  // parsed leading zero
                std::numeric_limits<uint64_t>::max());
  TestLeading64("0xffffffffffffffffffffff", 42,       // way beyond max
                std::numeric_limits<int64_t>::max(),  // signed int reached max
                0,                                    // parsed leading zero.
                std::numeric_limits<uint64_t>::max(),
                0,  // parsed leading zero
                std::numeric_limits<uint64_t>::max());
  TestLeading64("0xfffffffffffffffe", 42,             // unsigned64_max - 1
                std::numeric_limits<int64_t>::max(),  // signed int reached max
                0,                                    // parsed leading zero
                std::numeric_limits<uint64_t>::max() - 1,
                0,  // parsed leading zero
                std::numeric_limits<uint64_t>::max() - 1);

  TestLeading64("18446744073709551615", 42,           // max unsigned decimal
                std::numeric_limits<int64_t>::max(),  // Limited at pos maximum
                std::numeric_limits<int64_t>::max(),  // Limited at pos maximum
                std::numeric_limits<uint64_t>::max(),
                std::numeric_limits<uint64_t>::max(),
                std::numeric_limits<uint64_t>::max());  // digits as hex: > max.
  TestLeading64(
      "9223372036854775807", 42,  // max signed decimal
      std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::max(),
      std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::max(),
      std::numeric_limits<uint64_t>::max());  // digits as hex: > max.
  TestLeading64(
      "-9223372036854775808", 42,  // min signed decimal
      std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::min(),
      std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::min(),
      std::numeric_limits<uint64_t>::max());  // Why not <int64>::min ?

  // Parsing negative hex values. Even though it does not make
  // sense for the unsigned cases, ParseLeading* traditionally parsed
  // them as int64 and cast it to uint64.
  TestLeading64("-0xffff", 42, -0xffff, 0, -0xffff, 0, -0xffff);

  // Too many digits results in overflow.
  // Parsing negative values as unsigned is supported, parsed as signed, then
  // cast to unsigned.
  // Oddly, a _negative_ number with too many digits then results in uint64
  // max instead of a static_cast<uint64>(std::numeric_limits<int64>::min())
  TestLeading64(
      "-0xfffffffffffffffffffffff", 42,  // Overflow: Too many Digits.
      std::numeric_limits<int64_t>::min(),
      0,                                      // expected: leading zero as dec
      std::numeric_limits<uint64_t>::max(),   // Why not <int64>::min ?
      0,                                      // expected: leading zero as dec
      std::numeric_limits<uint64_t>::max());  // Why not <int64>::min ?

  TestLeading64("5000000000", 0, int64_t{5000000000}, int64_t{5000000000},
                uint64_t{5000000000}, uint64_t{5000000000},
                uint64_t{0x5000000000});
  TestLeading64(
      "-5000000000", 0, int64_t{-5000000000}, int64_t{-5000000000},
      std::numeric_limits<uint64_t>::max() - uint64_t{5000000000} + 1,
      std::numeric_limits<uint64_t>::max() - uint64_t{5000000000} + 1,
      std::numeric_limits<uint64_t>::max() - uint64_t{0x5000000000} + 1);
}

void TestParseLeadingBoolValue() {
  // Simple:
  EXPECT_EQ(false, ParseLeadingBoolValue("0", true));
  EXPECT_EQ(true, ParseLeadingBoolValue("1", false));
  EXPECT_EQ(false, ParseLeadingBoolValue("false", true));
  EXPECT_EQ(true, ParseLeadingBoolValue("true", false));
  EXPECT_EQ(false, ParseLeadingBoolValue("no", true));
  EXPECT_EQ(true, ParseLeadingBoolValue("yes", false));
  EXPECT_EQ(false, ParseLeadingBoolValue("n", true));
  EXPECT_EQ(true, ParseLeadingBoolValue("y", false));

  // Distractions:
  EXPECT_EQ(false, ParseLeadingBoolValue("  \nfalse\n", true));
  EXPECT_EQ(false, ParseLeadingBoolValue("  NO! ", true));
  EXPECT_EQ(false, ParseLeadingBoolValue("\tN\t", true));
  EXPECT_EQ(true, ParseLeadingBoolValue("yes|no", false));

  // Bad values:
  EXPECT_EQ(true, ParseLeadingBoolValue("", true));
  EXPECT_EQ(false, ParseLeadingBoolValue("", false));
  EXPECT_EQ(true, ParseLeadingBoolValue("00", true));
  EXPECT_EQ(true, ParseLeadingBoolValue("0x", true));
  EXPECT_EQ(true, ParseLeadingBoolValue("falseness", true));
  EXPECT_EQ(false, ParseLeadingBoolValue("1e-7", false));
  EXPECT_EQ(false, ParseLeadingBoolValue("yessirree", false));
}

void TestStringsItoa() {
  const struct s32 {
    int32_t n;
    const char* with_comma;
  } values32[] = {
      // INT_MAX
      {2147483647, "2,147,483,647"},
      {123456789, "123,456,789"},
      {24681357, "24,681,357"},
      {4698683, "4,698,683"},
      {100000, "100,000"},
      {65536, "65,536"},
      {2003, "2,003"},
      {409, "409"},
      {22, "22"},
      {7, "7"},
      {0, "0"},
      {-1, "-1"},
      {-999, "-999"},
      {-1000, "-1,000"},
      {-2147483647, "-2,147,483,647"},
      // 2147483648 -> icc "error #68: integer conversion resulted in a
      // change of sign"
      // -2147483648 -> gcc "error: decimal constant is so large that it
      // is unsigned" so use 1<<31.
      {1 << 31, "-2,147,483,648"}};

  const struct su32 {
    uint32_t n;
    const char* with_comma;
  } valuesu32[] = {// UINT_MAX
                   {4294967295U, "4,294,967,295"},
                   {4000000000U, "4,000,000,000"},
                   {2147483648U, "2,147,483,648"},
                   {2147483647U, "2,147,483,647"},
                   {123456789U, "123,456,789"},
                   {24681357U, "24,681,357"},
                   {4698683U, "4,698,683"},
                   {100000U, "100,000"},
                   {65536U, "65,536"},
                   {2003U, "2,003"},
                   {409U, "409"},
                   {7U, "7"},
                   {0U, "0"}};

  const struct s64 {
    int64_t n;
    const char* with_comma;
  } values64[] = {
      // LONG_MAX
      {int64_t{9223372036854775807}, "9,223,372,036,854,775,807"},
      {int64_t{123456789012345678}, "123,456,789,012,345,678"},
      {int64_t{98765432109876543}, "98,765,432,109,876,543"},
      {int64_t{4646464646464646}, "4,646,464,646,464,646"},
      {int64_t{373737373737373}, "373,737,373,737,373"},
      {int64_t{28282828282828}, "28,282,828,282,828"},
      {int64_t{1919191919191}, "1,919,191,919,191"},
      {int64_t{123456789012}, "123,456,789,012"},
      {int64_t{98765432101}, "98,765,432,101"},
      {int64_t{4294967295}, "4,294,967,295"},
      // Note that MSVC has what seems to be a frontend bug in handling
      // of negative ints with magnitude => INT_MIN. (In an explicit
      // initialization context the constant shouldn't be coerced to int
      // first, which is what appears to be happening without the extra
      // 'll' here.)
      {int64_t{-2147483649ll}, "-2,147,483,649"},
      {int64_t{-2147483647}, "-2,147,483,647"},
      {int64_t{-123456789012345678}, "-123,456,789,012,345,678"},
      {int64_t{-9223372036854775807}, "-9,223,372,036,854,775,807"},
      // 9223372036854775808LL -> icc "integer conversion resulted in a
      // change of sign"
      // -9223372036854775808LL -> gcc "decimal constant so large that it
      // is unsigned" so use 1LL << 63.
      {int64_t{1} << 63, "-9,223,372,036,854,775,808"}};

  const struct su64 {
    uint64_t n;
    const char* with_comma;
  } valuesu64[] = {
      // ULONG_MAX
      {uint64_t{18446744073709551615u}, "18,446,744,073,709,551,615"},
      {uint64_t{10000000000000000000u}, "10,000,000,000,000,000,000"},
      {uint64_t{9223372036854775808u}, "9,223,372,036,854,775,808"},
      {uint64_t{9223372036854775807}, "9,223,372,036,854,775,807"},
      {uint64_t{123456789012345678}, "123,456,789,012,345,678"},
      {uint64_t{98765432109876543}, "98,765,432,109,876,543"},
      {uint64_t{4646464646464646}, "4,646,464,646,464,646"},
      {uint64_t{373737373737373}, "373,737,373,737,373"},
      {uint64_t{28282828282828}, "28,282,828,282,828"},
      {uint64_t{1919191919191}, "1,919,191,919,191"},
      {uint64_t{123456789012}, "123,456,789,012"},
      {uint64_t{98765432101}, "98,765,432,101"},
      {uint64_t{4294967296}, "4,294,967,296"}};

  const struct s2 {
    int64_t n;
    std::string kmgt;
  } values2[] = {
      {int64_t{3298534883328}, "3T"},
      {int64_t{3000000000000}, "2T"},
      {int64_t{2199023255552}, "2T"},
      {int64_t{2199023255551}, "2047G"},
      {int64_t{1099511627776}, "1024G"},
      {int64_t{3221225472}, "3G"},
      {int64_t{2147483648}, "2G"},
      {int64_t{2147483647}, "2047M"},
      {int64_t{1073741825}, "1024M"},
      {int64_t{1073741824}, "1024M"},
      {3145728, "3M"},
      {2097152, "2M"},
      {2097151, "2047K"},
      {1048577, "1024K"},
      {1048576, "1024K"},
      {1048575, "1023K"},
      {3072, "3K"},
      {2048, "2K"},
      {2047, "2047"},
      {1024, "1024"},
      {3, "3"},
      {1, "1"},
      {0, "0"},
  };

  for (const su64& ps : valuesu64) {
    ASSERT_EQ(SimpleItoaWithCommas(ps.n), ps.with_comma);
  }

  for (const s64& ps : values64) {
    ASSERT_EQ(SimpleItoaWithCommas(ps.n), ps.with_comma);
  }

  for (const struct s32& ps : values32) {
    ASSERT_EQ(SimpleItoaWithCommas(ps.n), ps.with_comma);
    int64_t n = ps.n;
    ASSERT_EQ(SimpleItoaWithCommas(n), ps.with_comma);
  }

  for (const struct su32& ps : valuesu32) {
    ASSERT_EQ(SimpleItoaWithCommas(ps.n), ps.with_comma);
    uint64_t n = ps.n;
    ASSERT_EQ(SimpleItoaWithCommas(n), ps.with_comma);
  }

  for (const struct s2& ps : values2) {
    std::string kmgt = ItoaKMGT(ps.n);
    ASSERT_EQ(kmgt, ps.kmgt);
    if (ps.n != 0) {
      std::string minus_kmgt = ItoaKMGT(-ps.n);
      ASSERT_EQ(minus_kmgt, "-" + ps.kmgt);
    }
  }

  std::string one = "1";
#define VERIFY_SIMPLE_TYPE_TO_A(int_type)          \
  do {                                             \
    int_type integer = 1;                          \
    EXPECT_EQ(SimpleItoaWithCommas(integer), one); \
  } while (0)

  VERIFY_SIMPLE_TYPE_TO_A(int);  // NOLINT(readability/function)
  VERIFY_SIMPLE_TYPE_TO_A(unsigned int);
  VERIFY_SIMPLE_TYPE_TO_A(long);           // NOLINT(runtime/int)
  VERIFY_SIMPLE_TYPE_TO_A(unsigned long);  // NOLINT(runtime/int)
  VERIFY_SIMPLE_TYPE_TO_A(int32_t);
  VERIFY_SIMPLE_TYPE_TO_A(uint32_t);
  VERIFY_SIMPLE_TYPE_TO_A(int64_t);
  VERIFY_SIMPLE_TYPE_TO_A(uint64_t);
  VERIFY_SIMPLE_TYPE_TO_A(ptrdiff_t);
  VERIFY_SIMPLE_TYPE_TO_A(size_t);
  VERIFY_SIMPLE_TYPE_TO_A(intptr_t);
  VERIFY_SIMPLE_TYPE_TO_A(uintptr_t);
#undef VERIFY_SIMPLE_TYPE_TO_A
}

TEST(stringtest, safe_strto32_base) {
  int32_t value;
  EXPECT_TRUE(absl::SimpleHexAtoi("0x34234324", &value));
  EXPECT_EQ(0x34234324, value);

  EXPECT_TRUE(absl::SimpleHexAtoi("0X34234324", &value));
  EXPECT_EQ(0x34234324, value);

  EXPECT_TRUE(absl::SimpleHexAtoi("34234324", &value));
  EXPECT_EQ(0x34234324, value);

  EXPECT_TRUE(absl::SimpleHexAtoi("0", &value));
  EXPECT_EQ(0, value);

  EXPECT_TRUE(absl::SimpleHexAtoi(" \t\n -0x34234324", &value));
  EXPECT_EQ(-0x34234324, value);

  EXPECT_TRUE(absl::SimpleHexAtoi(" \t\n -34234324", &value));
  EXPECT_EQ(-0x34234324, value);

  EXPECT_TRUE(safe_strto32_base("7654321", &value, 8));
  EXPECT_EQ(07654321, value);

  EXPECT_TRUE(safe_strto32_base("-01234", &value, 8));
  EXPECT_EQ(-01234, value);

  EXPECT_FALSE(safe_strto32_base("1834", &value, 8));

  // Autodetect base.
  EXPECT_TRUE(safe_strto32_base("0", &value, 0));
  EXPECT_EQ(0, value);

  EXPECT_TRUE(safe_strto32_base("077", &value, 0));
  EXPECT_EQ(077, value);  // Octal interpretation

  // Leading zero indicates octal, but then followed by invalid digit.
  EXPECT_FALSE(safe_strto32_base("088", &value, 0));

  // Leading 0x indicated hex, but then followed by invalid digit.
  EXPECT_FALSE(safe_strto32_base("0xG", &value, 0));

  // Base-10 version.
  EXPECT_TRUE(safe_strto32("34234324", &value));
  EXPECT_EQ(34234324, value);

  EXPECT_TRUE(safe_strto32("0", &value));
  EXPECT_EQ(0, value);

  EXPECT_TRUE(safe_strto32(" \t\n -34234324", &value));
  EXPECT_EQ(-34234324, value);

  EXPECT_TRUE(safe_strto32("34234324 \n\t ", &value));
  EXPECT_EQ(34234324, value);

  // Invalid ints.
  EXPECT_FALSE(safe_strto32("", &value));
  EXPECT_FALSE(safe_strto32("  ", &value));
  EXPECT_FALSE(safe_strto32("abc", &value));
  EXPECT_FALSE(safe_strto32("34234324a", &value));
  EXPECT_FALSE(safe_strto32("34234.3", &value));

  // Out of bounds.
  EXPECT_FALSE(safe_strto32("2147483648", &value));
  EXPECT_FALSE(safe_strto32("-2147483649", &value));

  // String version.
  EXPECT_TRUE(absl::SimpleHexAtoi(std::string("0x1234"), &value));
  EXPECT_EQ(0x1234, value);

  // Base-10 string version.
  EXPECT_TRUE(safe_strto32(std::string("1234"), &value));
  EXPECT_EQ(1234, value);
}

TEST(stringtest, safe_strto32_range) {
  // These tests verify underflow/overflow behaviour.
  int32_t value;
  EXPECT_FALSE(safe_strto32_base("2147483648", &value, 10));
  EXPECT_EQ(std::numeric_limits<int32_t>::max(), value);

  EXPECT_TRUE(safe_strto32_base("-2147483648", &value, 10));
  EXPECT_EQ(std::numeric_limits<int32_t>::min(), value);

  EXPECT_FALSE(safe_strto32_base("-2147483649", &value, 10));
  EXPECT_EQ(std::numeric_limits<int32_t>::min(), value);
}

TEST(stringtest, safe_strto64_range) {
  // These tests verify underflow/overflow behaviour.
  int64_t value;
  EXPECT_FALSE(safe_strto64_base("9223372036854775808", &value, 10));
  EXPECT_EQ(std::numeric_limits<int64_t>::max(), value);
  EXPECT_EQ(std::numeric_limits<int64_t>::max(), value);

  EXPECT_TRUE(safe_strto64_base("-9223372036854775808", &value, 10));
  EXPECT_EQ(std::numeric_limits<int64_t>::min(), value);
  EXPECT_EQ(std::numeric_limits<int64_t>::min(), value);

  EXPECT_FALSE(safe_strto64_base("-9223372036854775809", &value, 10));
  EXPECT_EQ(std::numeric_limits<int64_t>::min(), value);
}

TEST(stringtest, safe_strto32_leading_substring) {
  // These tests verify this comment in numbers.h:
  // On error, returns false, and sets *value to: [...]
  //   conversion of leading substring if available ("123" "@@@" -> 123)
  //   0 if no leading substring available
  int32_t value;
  EXPECT_FALSE(
      safe_strto32_base("04069"
                        "@@@",
                        &value, 10));
  EXPECT_EQ(4069, value);

  EXPECT_FALSE(
      safe_strto32_base("04069"
                        "@@@",
                        &value, 8));
  EXPECT_EQ(0406, value);

  EXPECT_FALSE(safe_strto32_base("04069balloons", &value, 10));
  EXPECT_EQ(4069, value);

  EXPECT_FALSE(absl::SimpleHexAtoi("04069balloons", &value));
  EXPECT_EQ(0x4069ba, value);

  EXPECT_FALSE(safe_strto32_base("@@@", &value, 10));
  EXPECT_EQ(0, value);  // there was no leading substring
}

TEST(stringtest, safe_strto64_leading_substring) {
  // These tests verify this comment in numbers.h:
  // On error, returns false, and sets *value to: [...]
  //   conversion of leading substring if available ("123" "@@@" -> 123)
  //   0 if no leading substring available
  int64_t value;
  EXPECT_FALSE(
      safe_strto64_base("04069"
                        "@@@",
                        &value, 10));
  EXPECT_EQ(4069, value);

  EXPECT_FALSE(
      safe_strto64_base("04069"
                        "@@@",
                        &value, 8));
  EXPECT_EQ(0406, value);

  EXPECT_FALSE(safe_strto64_base("04069balloons", &value, 10));
  EXPECT_EQ(4069, value);

  EXPECT_FALSE(absl::SimpleHexAtoi("04069balloons", &value));
  EXPECT_EQ(0x4069ba, value);

  EXPECT_FALSE(safe_strto64_base("@@@", &value, 10));
  EXPECT_EQ(0, value);  // there was no leading substring
}

TEST(stringtest, safe_strto64_base) {
  int64_t value;
  EXPECT_TRUE(absl::SimpleHexAtoi("0x3423432448783446", &value));
  EXPECT_EQ(int64_t{0x3423432448783446}, value);

  EXPECT_TRUE(absl::SimpleHexAtoi("3423432448783446", &value));
  EXPECT_EQ(int64_t{0x3423432448783446}, value);

  EXPECT_TRUE(absl::SimpleHexAtoi("0", &value));
  EXPECT_EQ(0, value);

  EXPECT_TRUE(absl::SimpleHexAtoi(" \t\n -0x3423432448783446", &value));
  EXPECT_EQ(int64_t{-0x3423432448783446}, value);

  EXPECT_TRUE(absl::SimpleHexAtoi(" \t\n -3423432448783446", &value));
  EXPECT_EQ(int64_t{-0x3423432448783446}, value);

  EXPECT_TRUE(safe_strto64_base("123456701234567012", &value, 8));
  EXPECT_EQ(int64_t{0123456701234567012}, value);

  EXPECT_TRUE(safe_strto64_base("-017777777777777", &value, 8));
  EXPECT_EQ(int64_t{-017777777777777}, value);

  EXPECT_FALSE(safe_strto64_base("19777777777777", &value, 8));

  // Autodetect base.
  EXPECT_TRUE(safe_strto64_base("0", &value, 0));
  EXPECT_EQ(0, value);

  EXPECT_TRUE(safe_strto64_base("077", &value, 0));
  EXPECT_EQ(077, value);  // Octal interpretation

  // Leading zero indicates octal, but then followed by invalid digit.
  EXPECT_FALSE(safe_strto64_base("088", &value, 0));

  // Leading 0x indicated hex, but then followed by invalid digit.
  EXPECT_FALSE(safe_strto64_base("0xG", &value, 0));

  // Base-10 version.
  EXPECT_TRUE(safe_strto64("34234324487834466", &value));
  EXPECT_EQ(int64_t{34234324487834466}, value);

  EXPECT_TRUE(safe_strto64("0", &value));
  EXPECT_EQ(0, value);

  EXPECT_TRUE(safe_strto64(" \t\n -34234324487834466", &value));
  EXPECT_EQ(int64_t{-34234324487834466}, value);

  EXPECT_TRUE(safe_strto64("34234324487834466 \n\t ", &value));
  EXPECT_EQ(int64_t{34234324487834466}, value);

  // Invalid ints.
  EXPECT_FALSE(safe_strto64("", &value));
  EXPECT_FALSE(safe_strto64("  ", &value));
  EXPECT_FALSE(safe_strto64("abc", &value));
  EXPECT_FALSE(safe_strto64("34234324487834466a", &value));
  EXPECT_FALSE(safe_strto64("34234487834466.3", &value));

  // Out of bounds.
  EXPECT_FALSE(safe_strto64("9223372036854775808", &value));
  EXPECT_FALSE(safe_strto64("-9223372036854775809", &value));

  // String version.
  EXPECT_TRUE(absl::SimpleHexAtoi(std::string("0x1234"), &value));
  EXPECT_EQ(0x1234, value);

  // Base-10 string version.
  EXPECT_TRUE(safe_strto64(std::string("1234"), &value));
  EXPECT_EQ(1234, value);
}

const size_t kNumRandomTests = 10000;

template <typename IntType, bool parse_func(absl::string_view, IntType*),
          typename URBG>
void test_random_integer_parse(URBG& rng) {
  std::uniform_int_distribution<IntType> random_int(
      std::numeric_limits<IntType>::min());
  for (size_t i = 0; i < kNumRandomTests; i++) {
    IntType value = random_int(rng);
    IntType parsed_value;

    // Test successful parse
    EXPECT_TRUE(parse_func(absl::StrCat(value), &parsed_value));
    EXPECT_EQ(parsed_value, value);

    // Test overflow
    EXPECT_FALSE(
        parse_func(absl::StrCat(std::numeric_limits<IntType>::max(), value),
                   &parsed_value));

    // Test underflow
    if (std::numeric_limits<IntType>::min() < 0) {
      EXPECT_FALSE(
          parse_func(absl::StrCat(std::numeric_limits<IntType>::min(), value),
                     &parsed_value));
    } else {
      EXPECT_FALSE(parse_func(absl::StrCat("-", value), &parsed_value));
    }
  }
}

template <typename IntType,
          bool parse_func(absl::string_view, IntType* value, int base),
          typename URBG>
void test_random_integer_parse_base(URBG& rng) {
  std::uniform_int_distribution<IntType> random_int(
      std::numeric_limits<IntType>::min());
  for (size_t i = 0; i < kNumRandomTests; i++) {
    IntType value = random_int(rng);
    int base = absl::Uniform(absl::IntervalClosedClosed, rng, 2, 36);
    std::string str_value;
    EXPECT_TRUE(Itoa<IntType>(value, base, &str_value));
    IntType parsed_value;

    // Test successful parse
    EXPECT_TRUE(parse_func(str_value, &parsed_value, base));
    EXPECT_EQ(parsed_value, value);

    // Test overflow
    EXPECT_FALSE(
        parse_func(absl::StrCat(std::numeric_limits<IntType>::max(), value),
                   &parsed_value, base));

    // Test underflow
    if (std::numeric_limits<IntType>::min() < 0) {
      EXPECT_FALSE(
          parse_func(absl::StrCat(std::numeric_limits<IntType>::min(), value),
                     &parsed_value, base));
    } else {
      EXPECT_FALSE(parse_func(absl::StrCat("-", value), &parsed_value, base));
    }
  }
}

TEST(stringtest, safe_strto32_random) {
  absl::InsecureBitGen rng;
  test_random_integer_parse<int32_t, safe_strto32>(rng);
  test_random_integer_parse_base<int32_t, safe_strto32_base>(rng);
}
TEST(stringtest, safe_strto64_random) {
  absl::InsecureBitGen rng;
  test_random_integer_parse<int64_t, safe_strto64>(rng);
  test_random_integer_parse_base<int64_t, safe_strto64_base>(rng);
}
TEST(stringtest, safe_strtou32_random) {
  absl::InsecureBitGen rng;
  test_random_integer_parse<uint32_t, safe_strtou32>(rng);
  test_random_integer_parse_base<uint32_t, safe_strtou32_base>(rng);
}
TEST(stringtest, safe_strtou64_random) {
  absl::InsecureBitGen rng;
  test_random_integer_parse<uint64_t, safe_strtou64>(rng);
  test_random_integer_parse_base<uint64_t, safe_strtou64_base>(rng);
}

struct uint32_test_case {
  const char* str;
  bool expect_ok;
  int base;  // base to pass to the conversion function
  uint32_t expected;
};

static constexpr const uint32_test_case strtouint32_test_cases[] = {
    {"0xffffffff", true, 16, std::numeric_limits<uint32_t>::max()},
    {"0x34234324", true, 16, 0x34234324},
    {"34234324", true, 16, 0x34234324},
    {"0", true, 16, 0},
    {" \t\n 0xffffffff", true, 16, std::numeric_limits<uint32_t>::max()},
    {" \f\v 46", true, 10, 46},  // must accept weird whitespace
    {" \t\n 72717222", true, 8, 072717222},
    {" \t\n 072717222", true, 8, 072717222},
    {" \t\n 072717228", false, 8, 07271722},
    {"0", true, 0, 0},

    // Base-10 version.
    {"34234324", true, 0, 34234324},
    {"4294967295", true, 0, std::numeric_limits<uint32_t>::max()},
    {"34234324 \n\t", true, 10, 34234324},

    // Unusual base
    {"0", true, 3, 0},
    {"2", true, 3, 2},
    {"11", true, 3, 4},

    // Invalid uints.
    {"", false, 0, 0},
    {"  ", false, 0, 0},
    {"abc", false, 0, 0},  // would be valid hex, but prefix is missing
    {"34234324a", false, 0, 34234324},
    {"34234.3", false, 0, 34234},
    {"-1", false, 0, 0},
    {"   -123", false, 0, 0},
    {" \t\n -123", false, 0, 0},

    // Out of bounds.
    {"4294967296", false, 0, std::numeric_limits<uint32_t>::max()},
    {"0x100000000", false, 0, std::numeric_limits<uint32_t>::max()},
};

TEST(stringtest, safe_strtou32_base) {
  for (const auto& e : strtouint32_test_cases) {
    uint32_t value;
    EXPECT_EQ(e.expect_ok, safe_strtou32_base(e.str, &value, e.base))
        << "str=\"" << e.str << "\" base=" << e.base;
    if (e.expect_ok) {
      EXPECT_EQ(e.expected, value) << "str=\"" << e.str << "\" base=" << e.base;
    }
  }
}

TEST(stringtest, safe_strtou32_stringpiece) {
  for (const auto& e : strtouint32_test_cases) {
    if (e.base != 0) {  // Skip non-base-10 tests.
      continue;
    }
    uint32_t value;
    EXPECT_EQ(e.expect_ok, safe_strtou32(absl::string_view(e.str), &value))
        << "str=\"" << e.str;
    if (e.expect_ok) {
      EXPECT_EQ(e.expected, value) << "str=\"" << e.str << "\"";
    }
  }
}

TEST(stringtest, safe_strtou32_base_length_delimited) {
  for (const auto& e : strtouint32_test_cases) {
    std::string tmp(e.str);
    tmp.append("12");  // Adds garbage at the end.

    uint32_t value;
    EXPECT_EQ(e.expect_ok,
              safe_strtou32_base(absl::string_view(tmp.data(), strlen(e.str)),
                                 &value, e.base))
        << "str=\"" << e.str << "\" base=" << e.base;
    if (e.expect_ok) {
      EXPECT_EQ(e.expected, value) << "str=" << e.str << " base=" << e.base;
    }
  }
}

struct uint64_test_case {
  const char* str;
  bool expect_ok;
  int base;
  uint64_t expected;
};

static constexpr const uint64_test_case strtouint64_test_cases[] = {
    {"0x3423432448783446", true, 16, int64_t{0x3423432448783446}},
    {"3423432448783446", true, 16, int64_t{0x3423432448783446}},

    {"0", true, 16, 0},
    {"000", true, 0, 0},
    {"0", true, 0, 0},
    {" \t\n 0xffffffffffffffff", true, 16,
     std::numeric_limits<uint64_t>::max()},

    {"012345670123456701234", true, 8, int64_t{012345670123456701234}},
    {"12345670123456701234", true, 8, int64_t{012345670123456701234}},

    {"12845670123456701234", false, 8, 0},

    // Base-10 version.
    {"34234324487834466", true, 0, int64_t{34234324487834466}},

    {" \t\n 18446744073709551615", true, 0,
     std::numeric_limits<uint64_t>::max()},

    {"34234324487834466 \n\t ", true, 0, int64_t{34234324487834466}},

    {" \f\v 46", true, 10, 46},  // must accept weird whitespace

    // Unusual base
    {"0", true, 3, 0},
    {"2", true, 3, 2},
    {"11", true, 3, 4},

    {"0", true, 0, 0},

    // Invalid uints.
    {"", false, 0, 0},
    {"  ", false, 0, 0},
    {"abc", false, 0, 0},
    {"34234324487834466a", false, 0, 0},
    {"34234487834466.3", false, 0, 0},
    {"-1", false, 0, 0},
    {"   -123", false, 0, 0},
    {" \t\n -123", false, 0, 0},

    // Out of bounds.
    {"18446744073709551616", false, 10, 0},
    {"18446744073709551616", false, 0, 0},
    {"0x10000000000000000", false, 16, std::numeric_limits<uint64_t>::max()},
    {"0X10000000000000000", false, 16,
     std::numeric_limits<uint64_t>::max()},  // 0X versus 0x.
    {"0x10000000000000000", false, 0, std::numeric_limits<uint64_t>::max()},
    {"0X10000000000000000", false, 0,
     std::numeric_limits<uint64_t>::max()},  // 0X versus 0x.

    {"0x1234", true, 16, 0x1234},

    // Base-10 string version.
    {"1234", true, 0, 1234},
};

TEST(stringtest, safe_strtou64_base) {
  for (const auto& e : strtouint64_test_cases) {
    uint64_t value;
    EXPECT_EQ(e.expect_ok, safe_strtou64_base(e.str, &value, e.base))
        << "str=\"" << e.str << "\" base=" << e.base;
    if (e.expect_ok) {
      EXPECT_EQ(e.expected, value) << "str=" << e.str << " base=" << e.base;
    }
  }
}

TEST(stringtest, safe_strtou64_stringpiece) {
  for (const auto& e : strtouint64_test_cases) {
    if (e.base != 0) {  // Skip non-base-10 tests.
      continue;
    }
    uint64_t value;
    EXPECT_EQ(e.expect_ok, safe_strtou64(absl::string_view(e.str), &value))
        << "str=\"" << e.str;
    if (e.expect_ok) {
      EXPECT_EQ(e.expected, value) << "str=\"" << e.str << "\"";
    }
  }
}

TEST(stringtest, safe_strtou64_base_length_delimited) {
  for (const auto& e : strtouint64_test_cases) {
    std::string tmp(e.str);
    tmp.append("12");  // Adds garbage at the end.

    uint64_t value;
    EXPECT_EQ(e.expect_ok,
              safe_strtou64_base(absl::string_view(tmp.data(), strlen(e.str)),
                                 &value, e.base))
        << "str=\"" << e.str << "\" base=" << e.base;
    if (e.expect_ok) {
      EXPECT_EQ(e.expected, value) << "str=\"" << e.str << "\" base=" << e.base;
    }
  }
}

TEST(stringtest, safe_strtof) {
  float value;
  EXPECT_TRUE(safe_strtof("342.5", &value));
  EXPECT_FLOAT_EQ(342.5, value);

  EXPECT_TRUE(safe_strtof("0", &value));
  EXPECT_FLOAT_EQ(0.0, value);

  EXPECT_TRUE(safe_strtof(" \t\n -342.5", &value));
  EXPECT_FLOAT_EQ(-342.5, value);

  EXPECT_TRUE(safe_strtof("2.1e2", &value));
  EXPECT_FLOAT_EQ(210.0, value);

  EXPECT_TRUE(safe_strtof("34234324 \n\t ", &value));
  EXPECT_FLOAT_EQ(34234324.0, value);

  // Invalid floats.
  EXPECT_FALSE(safe_strtof("", &value));
  EXPECT_FALSE(safe_strtof("  ", &value));
  EXPECT_FALSE(safe_strtof("abc", &value));
  EXPECT_FALSE(safe_strtof("34234324a", &value));

  // Out of bounds - but not an error.
  EXPECT_TRUE(safe_strtof("1e100", &value));
  EXPECT_FLOAT_EQ(HUGE_VALF, value);
  EXPECT_TRUE(safe_strtof("-1e100", &value));
  EXPECT_FLOAT_EQ(-HUGE_VALF, value);
  EXPECT_TRUE(safe_strtof("1e-500", &value));
  EXPECT_GE(FLT_MIN, value);
  EXPECT_LE(0, value);
  EXPECT_TRUE(safe_strtof("-1e-500", &value));
  EXPECT_LE(-FLT_MIN, value);
  EXPECT_GE(0, value);

  // String version.
  EXPECT_TRUE(safe_strtof(std::string("1234.5"), &value));
  EXPECT_FLOAT_EQ(1234.5, value);

  // absl::string_view version. Make sure trailing characters are ignored.
  EXPECT_TRUE(safe_strtof(absl::string_view("12.345x", 6), &value));
  EXPECT_FLOAT_EQ(12.345, value);

  // std::string version.
  EXPECT_TRUE(safe_strtof(std::string("1234.5"), &value));
  EXPECT_FLOAT_EQ(1234.5, value);
}

TEST(stringtest, safe_strtod) {
  double value;
  EXPECT_TRUE(safe_strtod("34253465.5", &value));
  EXPECT_DOUBLE_EQ(34253465.5, value);

  EXPECT_TRUE(safe_strtod("0", &value));
  EXPECT_DOUBLE_EQ(0.0, value);

  EXPECT_TRUE(safe_strtod(" \t\n -34253465.5", &value));
  EXPECT_DOUBLE_EQ(-34253465.5, value);

  EXPECT_TRUE(safe_strtod("2.1e2", &value));
  EXPECT_DOUBLE_EQ(210.0, value);

  EXPECT_TRUE(safe_strtod("34234324 \n\t ", &value));
  EXPECT_DOUBLE_EQ(34234324, value);

  // Invalid doubles.
  EXPECT_FALSE(safe_strtod("", &value));
  EXPECT_FALSE(safe_strtod("  ", &value));
  EXPECT_FALSE(safe_strtod("abc", &value));
  EXPECT_FALSE(safe_strtod("34234324a", &value));

  // Out of bounds - rounds up/down.
  EXPECT_TRUE(safe_strtod("1e1000", &value));
  EXPECT_DOUBLE_EQ(HUGE_VAL, value);
  EXPECT_TRUE(safe_strtod("-1e1000", &value));
  EXPECT_DOUBLE_EQ(-HUGE_VAL, value);
  EXPECT_TRUE(safe_strtod("1e-1000", &value));
  EXPECT_GE(DBL_MIN, value);
  EXPECT_LE(0, value);
  EXPECT_TRUE(safe_strtod("-1e-1000", &value));
  EXPECT_LE(-DBL_MIN, value);
  EXPECT_GE(0, value);

  // String version.
  EXPECT_TRUE(safe_strtod(std::string("1234.5"), &value));
  EXPECT_DOUBLE_EQ(1234.5, value);

  // absl::string_view version. Make sure trailing characters are ignored.
  EXPECT_TRUE(safe_strtod(absl::string_view("12.345x", 6), &value));
  EXPECT_DOUBLE_EQ(12.345, value);

  // std::string version.
  EXPECT_TRUE(safe_strtod(std::string("1234.5"), &value));
  EXPECT_DOUBLE_EQ(1234.5, value);
}

TEST(stringtest, u64tostr_base36) {
  char buf[100];
  uint64_t test_numbers[] = {
      0,  1,  2,   3,   5,      35,         36,
      64, 65, 127, 128, 100000, 0xffffffff, uint64_t{0xffffffffffffffff}};
  const char* max_u64_base36 = "3w5e11264sgsf";
  size_t max_len = strlen(max_u64_base36);

  EXPECT_EQ(max_len,
            u64tostr_base36(uint64_t{0xffffffffffffffff}, sizeof(buf), buf));
  EXPECT_STREQ(max_u64_base36, buf);

  for (uint64_t n : test_numbers) {
    size_t len = u64tostr_base36(n, sizeof(buf), buf);
    EXPECT_EQ(strlen(buf), len);
    uint64_t m;
    EXPECT_TRUE(safe_strtou64_base(buf, &m, 36));
    EXPECT_EQ(n, m);
  }

  // Buffer is big enough to hold any 64 bit number in base-36
  for (uint64_t n : test_numbers) {
    size_t len = u64tostr_base36(n, max_len + 1, buf);
    EXPECT_EQ(strlen(buf), len);
    uint64_t m;
    EXPECT_TRUE(safe_strtou64_base(buf, &m, 36));
    EXPECT_EQ(n, m);
  }

  // Buffer is just big enough
  char buf2[2];
  EXPECT_EQ(1, u64tostr_base36(35, sizeof(buf2), buf2));

  // Buffer too small
  char buf1[1];
  EXPECT_EQ(0, u64tostr_base36(35, sizeof(buf1), buf1));
  EXPECT_EQ(0, u64tostr_base36(36, sizeof(buf1), buf1));
  EXPECT_EQ(0, u64tostr_base36(36, sizeof(buf2), buf2));

  // Incorrect arguments
  EXPECT_DEATH_IF_SUPPORTED(u64tostr_base36(1, 0, buf), "");
  EXPECT_DEATH_IF_SUPPORTED(u64tostr_base36(1, sizeof(buf), nullptr), "");
}

// feenableexcept() and fedisableexcept() are missing on Mac OS X, MSVC,
// and WebAssembly.
#if defined(_MSC_VER) || defined(__APPLE__) || defined(__EMSCRIPTEN__)
#define ABSL_MISSING_FEENABLEEXCEPT 1
#define ABSL_MISSING_FEDISABLEEXCEPT 1
#endif

class SimpleDtoaTest : public testing::Test {
 protected:
  void SetUp() override {
    // Store the current floating point env & clear away any pending exceptions.
    feholdexcept(&fp_env_);
#ifndef ABSL_MISSING_FEENABLEEXCEPT
    // Turn on floating point exceptions.
    feenableexcept(FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW);
#endif
  }

  void TearDown() override {
    // Restore the floating point environment to the original state.
    // In theory fedisableexcept is unnecessary; fesetenv will also do it.
    // In practice, our toolchains have subtle bugs.
#ifndef ABSL_MISSING_FEDISABLEEXCEPT
    fedisableexcept(FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW);
#endif
    fesetenv(&fp_env_);
  }

  std::string ToNineDigits(double value) {
    char buffer[kFastToBufferSize];  // more than enough for %.9g
    absl::SNPrintF(buffer, sizeof(buffer), "%.9g", value);
    return buffer;
  }

  void TestDtoaRoundTrip(double value, int min_length) {
    std::string str = SimpleDtoa(value);
    SCOPED_TRACE("SimpleDtoa() returned: " + str);
    if (std::isnan(value)) {
      EXPECT_TRUE(std::isnan(strtod(str.c_str(), nullptr)));
    } else {
      EXPECT_EQ(value, strtod(str.c_str(), nullptr));
    }
    EXPECT_LT(str.size(), kFastToBufferSize);
    EXPECT_GE(str.size(), min_length);
  }

  void TestFtoaRoundTrip(float value, int min_length) {
    std::string str = SimpleFtoa(value);
    SCOPED_TRACE("SimpleFtoa() returned: " + str);
    float rt = strtof(str.c_str(), nullptr);
    if (std::isnan(value)) {
      EXPECT_TRUE(std::isnan(rt));
    } else {
      EXPECT_EQ(value, rt) << " " << ToNineDigits(value)
                           << "!=" << ToNineDigits(rt);
    }
    EXPECT_LT(str.size(), kFastToBufferSize);
    EXPECT_GE(str.size(), min_length);
  }

  fenv_t fp_env_;
};

TEST_F(SimpleDtoaTest, SimpleDtoa) {
  // Make sure that nice, round decimal numbers are printed using few digits,
  // even if they can't be represented exactly in binary.
  EXPECT_EQ("0", SimpleDtoa(0.0));
  EXPECT_EQ("1", SimpleDtoa(1.0));
  EXPECT_EQ("-1", SimpleDtoa(-1.0));
  EXPECT_EQ("0.2", SimpleDtoa(0.2));
  EXPECT_EQ("1.1", SimpleDtoa(1.1));
  EXPECT_EQ("1e+23", SimpleDtoa(1e23));
  EXPECT_EQ("47.8", SimpleDtoa(47.8));
  EXPECT_EQ("1000.2", SimpleDtoa(1000.2));

  // Make sure round-trips with strtod() work.  Note that even though we're
  // dealing with floating points, we expect the results to be *exactly*
  // equal, not approximately.
  TestDtoaRoundTrip(1.2345678901234567, 18);
  TestDtoaRoundTrip(1.2345678901234565, 18);
  TestDtoaRoundTrip(1.2345678901234569, 18);
  TestDtoaRoundTrip(47.800000000000001, 18);
  TestDtoaRoundTrip(0.10000000000000005, 18);
  TestDtoaRoundTrip(0.010000000000000005, 18);
  TestDtoaRoundTrip(0.000000010000000000000005, 18);
  TestDtoaRoundTrip(1.0000000000000005, 18);
  TestDtoaRoundTrip(10.000000000000005, 18);
  TestDtoaRoundTrip(100.00000000000005, 18);
  TestDtoaRoundTrip(1000.0000000000005, 18);
  TestDtoaRoundTrip(100000000000000.05, 18);

  // IEEE-754 double finite values furthest from zero.
  TestDtoaRoundTrip(1.7976931348623157e308, 22);
  TestDtoaRoundTrip(-1.7976931348623157e308, 23);

  // IEEE-754 double normalized values closest to zero.
  TestDtoaRoundTrip(2.225073858507202e-308, 22);
  TestDtoaRoundTrip(-2.225073858507202e-308, 23);

  // IEEE-754 double denormalized values closest to zero.
  TestDtoaRoundTrip(5e-324, 6);
  TestDtoaRoundTrip(-5e-324, 7);

  // Biggest and lowest valid numbers.
  TestDtoaRoundTrip(std::numeric_limits<double>::max(), 3);
  TestDtoaRoundTrip(std::numeric_limits<double>::lowest(), 3);

  // Infinity and NaN.
  TestDtoaRoundTrip(std::numeric_limits<double>::infinity(), 3);
  TestDtoaRoundTrip(-std::numeric_limits<double>::infinity(), 3);
  TestDtoaRoundTrip(std::numeric_limits<double>::quiet_NaN(), 3);
}

TEST_F(SimpleDtoaTest, SimpleFtoa) {
  // Make sure that nice, round decimal numbers are printed using few digits,
  // even if they can't be represented exactly in binary.
  EXPECT_EQ("0", SimpleFtoa(0.0f));
  EXPECT_EQ("-0", SimpleFtoa(-0.0f));
  EXPECT_EQ("1", SimpleFtoa(1.0f));
  EXPECT_EQ("-1", SimpleFtoa(-1.0f));
  EXPECT_EQ("0.2", SimpleFtoa(0.2f));
  EXPECT_EQ("1.1", SimpleFtoa(1.1f));
  EXPECT_EQ("1e+23", SimpleFtoa(1e23f));
  EXPECT_EQ("47.8", SimpleFtoa(47.8f));
  EXPECT_EQ("1000.2", SimpleFtoa(1000.2f));
  EXPECT_EQ("1.1754944e-38", SimpleFtoa(1.17549435e-38f));

  // Make sure round-trips with strtod() work.  Note that even though we're
  // dealing with floating points, we expect the results to be *exactly*
  // equal, not approximately.
  TestFtoaRoundTrip(1.2345678901234567, 9);
  TestFtoaRoundTrip(1.2345678901234565, 9);
  TestFtoaRoundTrip(1.2345678901234569, 9);
  TestFtoaRoundTrip(47.800005, 9);
  TestFtoaRoundTrip(0.10000005, 9);
  TestFtoaRoundTrip(0.010000005, 9);
  TestFtoaRoundTrip(0.000000010000005, 9);
  TestFtoaRoundTrip(1.0000005, 9);
  TestFtoaRoundTrip(10.000005, 9);
  TestFtoaRoundTrip(100.00005, 9);
  TestFtoaRoundTrip(1000.0005, 9);
  TestFtoaRoundTrip(100000.05, 9);
  TestFtoaRoundTrip(10.0000095, 8);
  TestFtoaRoundTrip(10.0000100, 8);
  TestFtoaRoundTrip(10.0000105, 8);
  TestFtoaRoundTrip(10.0000110, 8);
  TestFtoaRoundTrip(10.0000115, 8);

  // IEEE-754 float finite values furthest from zero.
  TestFtoaRoundTrip(+3.4028234e38f, 9);
  TestFtoaRoundTrip(-3.4028234e38f, 9);

  // IEEE-754 float normalized values closest to zero.
  TestFtoaRoundTrip(+1.175494351e-38, 9);
  TestFtoaRoundTrip(-1.175494351e-38, 9);

  // IEEE-754 double denormalized values closest to zero.
  TestFtoaRoundTrip(+1.4e-45, 5);
  TestFtoaRoundTrip(-1.4e-45, 6);

  // Biggest and lowest valid numbers.
  TestDtoaRoundTrip(std::numeric_limits<float>::max(), 3);
  TestDtoaRoundTrip(std::numeric_limits<float>::lowest(), 3);

  // Infinity and NaN.
  TestFtoaRoundTrip(std::numeric_limits<float>::infinity(), 3);
  TestFtoaRoundTrip(-std::numeric_limits<float>::infinity(), 3);
  TestFtoaRoundTrip(std::numeric_limits<float>::quiet_NaN(), 3);
}

TEST(Numbers, TestAtoiKMGT) {
  // Valid inputs
  EXPECT_THAT(AtoiKMGT("321k"), Optional(uint64_t{321} << 10));
  EXPECT_THAT(AtoiKMGT("321m"), Optional(uint64_t{321} << 20));
  EXPECT_THAT(AtoiKMGT("321g"), Optional(uint64_t{321} << 30));
  EXPECT_THAT(AtoiKMGT("321t"), Optional(uint64_t{321} << 40));

  EXPECT_THAT(AtoiKMGT("123K"), Optional(uint64_t{123} << 10));
  EXPECT_THAT(AtoiKMGT("123M"), Optional(uint64_t{123} << 20));
  EXPECT_THAT(AtoiKMGT("123G"), Optional(uint64_t{123} << 30));
  EXPECT_THAT(AtoiKMGT("123T"), Optional(uint64_t{123} << 40));

  EXPECT_THAT(AtoiKMGT("321"), Optional(321));
  EXPECT_THAT(AtoiKMGT(" 321K "), Optional(uint64_t{321} << 10));
  EXPECT_THAT(AtoiKMGT("0"), Optional(0));

  // Invalid inputs
  EXPECT_EQ(AtoiKMGT(""), std::nullopt);
  EXPECT_EQ(AtoiKMGT("  "), std::nullopt);
  EXPECT_EQ(AtoiKMGT("321x"), std::nullopt);
  EXPECT_EQ(AtoiKMGT("321Kx"), std::nullopt);
  EXPECT_EQ(AtoiKMGT("321xK"), std::nullopt);
  EXPECT_EQ(AtoiKMGT("abc"), std::nullopt);
  EXPECT_EQ(AtoiKMGT("-321K"), std::nullopt);
  EXPECT_EQ(AtoiKMGT("321.5K"), std::nullopt);

  // Overflow cases
  EXPECT_EQ(AtoiKMGT("18446744073709551616"), std::nullopt);
  EXPECT_EQ(AtoiKMGT("18446744073709551T"), std::nullopt);
  EXPECT_EQ(AtoiKMGT("999999999999999999999K"), std::nullopt);
}

TEST(Numbers, TestFunctionsMovedOverFromStrutilUnittestMain) {
  TestFastPrints();
  TestFastToBufferLefts();
  TestAutoDigit();
  TestParseLeadingDoubleValue();
  TestParseLeadingIntValue();
  TestParseLeadingBoolValue();
  TestConsumeStrayLeadingZeroes();
  TestStringsItoa();
}

TEST(BooleanConversion, BoolToString) {
  EXPECT_STREQ("true", SimpleBtoa(true).c_str());
  EXPECT_STREQ("false", SimpleBtoa(false).c_str());
}

TEST(BooleanConversion, StringToBool) {
  bool val = false;

  // Strings that should never successfully convert to a boolean.
  // Be sure to note the placement of space characters within the strings!
  // This is not a complete list.
  const std::string kFailureStrings[] = {"",   "hello",  "maybe",  "2",
                                         "-1", " true ", "false ", " false"};
  for (const std::string& str : kFailureStrings) {
    SCOPED_TRACE(absl::StrCat(
        "Testing string that shouldn't be convertible to a boolean: ", str));
    EXPECT_FALSE(safe_strtob(str, &val));
    // Note that the value of "val" should be unchanged after a failure.
    EXPECT_FALSE(val);
  }

  // Strings that should successfully convert to a boolean which equals true.
  // This is not a complete list.
  const std::string kTrueStrings[] = {"TRUE", "tRuE", "True", "true", "T", "t",
                                      "Yes",  "yes",  "Y",    "y",    "1"};
  for (const std::string& str : kTrueStrings) {
    SCOPED_TRACE(
        absl::StrCat("Testing string that should evaluate to true: ", str));
    val = false;  // Make sure the value is actually changed
    EXPECT_TRUE(safe_strtob(str, &val));
    EXPECT_TRUE(val);
  }

  // Strings that should successfully convert to a boolean which equals false.
  // This is not a complete list.
  const std::string kFalseStrings[] = {
      "FALSE", "fALSe", "False", "false", "F", "f",
      "No",    "NO",    "no",    "N",     "n", "0",
  };
  for (const std::string& str : kFalseStrings) {
    SCOPED_TRACE(
        absl::StrCat("Testing string that should evaluate to false: ", str));
    val = true;  // Make sure the value is actually changed
    EXPECT_TRUE(safe_strtob(str, &val));
    EXPECT_FALSE(val);
  }

  // The only process failure should be if a nullptr is provided for the
  // output boolean.
  EXPECT_DEATH_IF_SUPPORTED(safe_strtob("true", nullptr), "");
}

TEST(BooleanConversion, StringToBoolAndBack) {
  bool val = false;

  // Ensure we can convert from bool->string->bool accurately.
  ASSERT_TRUE(safe_strtob(SimpleBtoa(true), &val));
  EXPECT_EQ(true, val);
  ASSERT_TRUE(safe_strtob(SimpleBtoa(false), &val));
  EXPECT_EQ(false, val);

  // Ensure we can convert from string->bool->string accurately.
  std::string string_val = "true";
  ASSERT_TRUE(safe_strtob(string_val, &val));
  EXPECT_STREQ(string_val.c_str(), SimpleBtoa(val).c_str());
  string_val = "false";
  ASSERT_TRUE(safe_strtob(string_val, &val));
  EXPECT_STREQ(string_val.c_str(), SimpleBtoa(val).c_str());
}

TEST(StrToInt32, Partial) {
  struct Int32TestLine {
    std::string input;
    bool status;
    int32_t value;
  };
  const int32_t int32_min = std::numeric_limits<int32_t>::min();
  const int32_t int32_max = std::numeric_limits<int32_t>::max();
  Int32TestLine int32_test_line[] = {
      {"", false, 0},
      {" ", false, 0},
      {"-", false, 0},
      {"123"
       "@@@",
       false, 123},
      {absl::StrCat(int32_min, int32_max), false, int32_min},
      {absl::StrCat(int32_max, int32_max), false, int32_max},
  };

  for (const Int32TestLine& test_line : int32_test_line) {
    int32_t value = -2;
    bool status = safe_strto32(test_line.input, &value);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
    value = -2;
    status = safe_strto32(test_line.input, &value);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
    value = -2;
    status = safe_strto32(absl::string_view(test_line.input), &value);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
  }
}

TEST(StrToUint32, Partial) {
  struct Uint32TestLine {
    std::string input;
    bool status;
    uint32_t value;
  };
  const uint32_t uint32_max = std::numeric_limits<uint32_t>::max();
  Uint32TestLine uint32_test_line[] = {
      {"", false, 0},
      {" ", false, 0},
      {"-", false, 0},
      {"123"
       "@@@",
       false, 123},
      {absl::StrCat(uint32_max, uint32_max), false, uint32_max},
  };

  for (const Uint32TestLine& test_line : uint32_test_line) {
    uint32_t value = 2;
    bool status = safe_strtou32(test_line.input, &value);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
    value = 2;
    status = safe_strtou32(test_line.input, &value);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
    value = 2;
    status = safe_strtou32(absl::string_view(test_line.input), &value);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
  }
}

TEST(StrToInt64, Partial) {
  struct Int64TestLine {
    std::string input;
    bool status;
    int64_t value;
  };
  const int64_t int64_min = std::numeric_limits<int64_t>::min();
  const int64_t int64_max = std::numeric_limits<int64_t>::max();
  Int64TestLine int64_test_line[] = {
      {"", false, 0},
      {" ", false, 0},
      {"-", false, 0},
      {"123"
       "@@@",
       false, 123},
      {absl::StrCat(int64_min, int64_max), false, int64_min},
      {absl::StrCat(int64_max, int64_max), false, int64_max},
  };

  for (const Int64TestLine& test_line : int64_test_line) {
    int64_t value = -2;
    bool status = safe_strto64(test_line.input, &value);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
    value = -2;
    status = safe_strto64(test_line.input, &value);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
    value = -2;
    status = safe_strto64(absl::string_view(test_line.input), &value);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
  }
}

TEST(StrToUint64, Partial) {
  struct Uint64TestLine {
    std::string input;
    bool status;
    uint64_t value;
  };
  const uint64_t uint64_max = std::numeric_limits<uint64_t>::max();
  Uint64TestLine uint64_test_line[] = {
      {"", false, 0},
      {" ", false, 0},
      {"-", false, 0},
      {"123"
       "@@@",
       false, 123},
      {absl::StrCat(uint64_max, uint64_max), false, uint64_max},
  };

  for (const Uint64TestLine& test_line : uint64_test_line) {
    uint64_t value = 2;
    bool status = safe_strtou64(test_line.input, &value);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
    value = 2;
    status = safe_strtou64(test_line.input, &value);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
    value = 2;
    status = safe_strtou64(absl::string_view(test_line.input), &value);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
  }
}

TEST(StrToInt32Base, PrefixOnly) {
  struct Int32TestLine {
    std::string input;
    bool status;
    int32_t value;
  };
  Int32TestLine int32_test_line[] = {
      {"", false, 0}, {"-", false, 0},  {"-0", true, 0},
      {"0", true, 0}, {"0x", false, 0}, {"-0x", false, 0},
  };
  const int base_array[] = {0, 2, 8, 10, 16};

  for (const Int32TestLine& line : int32_test_line) {
    for (const int base : base_array) {
      int32_t value = 2;
      bool status = safe_strto32_base(line.input, &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
      value = 2;
      status = safe_strto32_base(line.input, &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
      value = 2;
      status = safe_strto32_base(absl::string_view(line.input), &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
    }
  }
}

TEST(StrToUint32Base, PrefixOnly) {
  struct Uint32TestLine {
    std::string input;
    bool status;
    uint32_t value;
  };
  Uint32TestLine uint32_test_line[] = {
      {"", false, 0},
      {"0", true, 0},
      {"0x", false, 0},
  };
  const int base_array[] = {0, 2, 8, 10, 16};

  for (const Uint32TestLine& line : uint32_test_line) {
    for (const int base : base_array) {
      uint32_t value = 2;
      bool status = safe_strtou32_base(line.input, &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
      value = 2;
      status = safe_strtou32_base(line.input, &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
      value = 2;
      status = safe_strtou32_base(absl::string_view(line.input), &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
    }
  }
}

TEST(StrToInt64Base, PrefixOnly) {
  struct Int64TestLine {
    std::string input;
    bool status;
    int64_t value;
  };
  Int64TestLine int64_test_line[] = {
      {"", false, 0}, {"-", false, 0},  {"-0", true, 0},
      {"0", true, 0}, {"0x", false, 0}, {"-0x", false, 0},
  };
  const int base_array[] = {0, 2, 8, 10, 16};

  for (const Int64TestLine& line : int64_test_line) {
    for (const int base : base_array) {
      int64_t value = 2;
      bool status = safe_strto64_base(line.input, &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
      value = 2;
      status = safe_strto64_base(line.input, &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
      value = 2;
      status = safe_strto64_base(absl::string_view(line.input), &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
    }
  }
}

TEST(StrToUint64Base, PrefixOnly) {
  struct Uint64TestLine {
    std::string input;
    bool status;
    uint64_t value;
  };
  Uint64TestLine uint64_test_line[] = {
      {"", false, 0},
      {"0", true, 0},
      {"0x", false, 0},
  };
  const int base_array[] = {0, 2, 8, 10, 16};

  for (const Uint64TestLine& line : uint64_test_line) {
    for (const int base : base_array) {
      uint64_t value = 2;
      bool status = safe_strtou64_base(line.input, &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
      value = 2;
      status = safe_strtou64_base(line.input, &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
      value = 2;
      status = safe_strtou64_base(absl::string_view(line.input), &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
    }
  }
}

const int64_t kMaxReasonableTime{253402300799};  // 9999-12-31 23:59:59 +00:00

static const int kFastTimeToBufferSize = 30;

// Return true if time is valid, else false.
// In 32-bit mode, this routine should always return true,
// so we should not code an expectation of a false return in that case.
static bool CheckTime(time_t t) {
  char buffer[100];
  // The documentation says 30 bytes is enough for the buffer.
  // Put a sentinel character at the 30th position.
  const char kSentinelChar = '?';
  const unsigned char kMinBufferSize = 30;
  static_assert(kMinBufferSize == kFastTimeToBufferSize, "");
  buffer[kMinBufferSize] = kSentinelChar;
  const char* actual = FastTimeToBuffer(t, buffer);
  LOG(INFO) << "FastTimeToBuffer for " << std::hex << t
            << " (hex) returned: " << actual;
  struct tm tm_struct;
  bool valid = false;

  if ((sizeof(time_t) == 4 || (t >= 0 && t <= kMaxReasonableTime)) &&
      gmtime_r(&t, &tm_struct) != nullptr) {
    valid = true;
    char expected[kFastToBufferSize];
    strftime(expected, sizeof(expected), "%a, %d %b %Y %H:%M:%S GMT",
             &tm_struct);
    EXPECT_STREQ(expected, actual) << "Time: " << t;
  } else {
    std::string expected = absl::StrCat("Invalid:", int64_t{t});
    EXPECT_EQ(expected, actual);
  }

  EXPECT_LT(strlen(actual), kMinBufferSize);         // Fits in buffer.
  EXPECT_EQ(kSentinelChar, buffer[kMinBufferSize]);  // No buffer overrun.
  // No garbage characters.
  for (size_t i = 0; i < strlen(actual); ++i) {
    EXPECT_TRUE(isprint(actual[i]));
  }
  return valid;
}

TEST(stringtest, CheckTime) {
  const bool kIs32Bit = (sizeof(time_t) == 4);
  EXPECT_EQ(kIs32Bit, CheckTime(std::numeric_limits<time_t>::min()));
  EXPECT_EQ(kIs32Bit, CheckTime(std::numeric_limits<time_t>::max()));
  EXPECT_EQ(kIs32Bit, CheckTime(static_cast<time_t>(-6076574518398440533LL)));
  EXPECT_TRUE(CheckTime(1));
  EXPECT_TRUE(CheckTime(1210274577));
  EXPECT_TRUE(CheckTime(std::numeric_limits<int32_t>::max()));
  // The following line used to cause a DCHECK.
  EXPECT_EQ(kIs32Bit, CheckTime(static_cast<time_t>(1370480385048124LL)));
  // Verify largest reasonable time and one second later.
  EXPECT_TRUE(CheckTime(static_cast<time_t>(kMaxReasonableTime)));
  EXPECT_EQ(kIs32Bit, CheckTime(static_cast<time_t>(kMaxReasonableTime + 1)));
}

}  // namespace strings
