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

#include "gloop/strings/numberformat.h"

#include <cstdint>
#include <limits>

#include "gtest/gtest.h"

namespace strings {
namespace {

constexpr int kDefaultVal = 123;

TEST(NumberFormatTest, AllTests) {
  constexpr int64_t k87M = 87654321;
  EXPECT_EQ(BinaryEng(k87M, 10, 1), "83.6M");
  EXPECT_EQ(BinaryEng(k87M, 1, 1), "83.6M");
  EXPECT_EQ(DecimalEng(k87M, 10, 1), "87.7M");
  EXPECT_EQ(DecimalEng(k87M, 1, 1), "87.7M");
  EXPECT_EQ(DecimalEng(k87M, 10, 0), "88M");
  EXPECT_EQ(DecimalEng(k87M, 10, 1), "87.7M");
  EXPECT_EQ(DecimalEng(k87M, 10, 2), "87.65M");
  EXPECT_EQ(DecimalEng(k87M, 10, 4), "87.6543M");
  EXPECT_EQ(DecimalEng(k87M, 10, 6), "87.654321M");

  // Fails due to precision.  get =    87.654320999999996M
  // EXPECT_EQ(DecimalEng(k87M, 10, 15), "87.654321000000000M");

  // This works
  EXPECT_EQ(DecimalEng(k87M, 10, 14), "87.65432100000000M");

  // Check negative numbers
  EXPECT_EQ(BinaryEng(-k87M, 10, 1), "-83.6M");
  EXPECT_EQ(BinaryEng(-k87M, 1, 1), "-83.6M");
  EXPECT_EQ(DecimalEng(-k87M, 10, 1), "-87.7M");
  EXPECT_EQ(DecimalEng(-k87M, 1, 1), "-87.7M");

  constexpr int64_t k7M = 7654321;
  EXPECT_EQ(BinaryEng(k7M, 10, 1), "7474.9K");
  EXPECT_EQ(BinaryEng(k7M, 1, 1), "7.3M");
  EXPECT_EQ(BinaryEng(k7M, 0, 1), "7.3M");
  EXPECT_EQ(DecimalEng(k7M, 10, 1), "7654.3K");
  EXPECT_EQ(DecimalEng(k7M, 1, 1), "7.7M");
  EXPECT_EQ(DecimalEng(k7M, 0, 1), "7.7M");

  // Test floating point thresh
  constexpr int64_t k500K = 500000;
  EXPECT_EQ(DecimalEng(k500K, 0, 1), "0.5M");
  EXPECT_EQ(DecimalEng(k500K, 0.1, 1), "0.5M");
  EXPECT_EQ(DecimalEng(k500K, 0.6, 1), "500K");
  // Don't want to start printing in Tera unless we specify a high precision
  EXPECT_EQ(DecimalEng(k500K, 0.0000001, 2), "0.50M");
  EXPECT_EQ(DecimalEng(k500K, 0.0000001, 7), "0.0000005T");

  // Test that large precision doesn't explode.
  EXPECT_EQ(DecimalEng(550000, 1, 12), "550K");
  EXPECT_EQ(DecimalEng(550000, 1, 37), "550K");

  constexpr int64_t kM = 1LL << 20;
  EXPECT_EQ(BinaryEng(kM, 10, 1), "1M");
  EXPECT_EQ(BinaryEng(kM, 1, 1), "1M");
  EXPECT_EQ(BinaryEng(kM, 0, 1), "1M");

  constexpr int64_t kMillion = 1000 * 1000;
  EXPECT_EQ(DecimalEng(kMillion, 10, 1), "1M");
  EXPECT_EQ(DecimalEng(kMillion, 1, 1), "1M");
  EXPECT_EQ(DecimalEng(kMillion, 0, 1), "1M");

  constexpr int64_t kOne = 1;
  EXPECT_EQ(BinaryEng(kOne, 1, 1), "1");
  EXPECT_EQ(BinaryEng(kOne, 10, 1), "1");

  constexpr int64_t kZero = 0;
  EXPECT_EQ(DecimalEng(kZero, 0.1, 1), "0");
  EXPECT_EQ(DecimalEng(kZero, 5, 1), "0");

  // Check the example in numberformat.h:
  EXPECT_EQ(DecimalEng(12345), "12.35K");
  // which is different from the check precision for these tests:
  EXPECT_EQ(DecimalEng(12345, 1, 1), "12.3K");
  EXPECT_EQ(BinaryEng(12345), "12.06K");

  constexpr int64_t kRoundDown = static_cast<int64_t>(kM * 4.449);
  constexpr int64_t kRoundUp = static_cast<int64_t>(kM * 4.49);
  EXPECT_EQ(BinaryEng(kRoundDown, 4, 1), "4.4M");
  EXPECT_EQ(BinaryEng(kRoundUp, 4, 1), "4.5M");

  constexpr int64_t k8p4M = static_cast<int64_t>(kM * 8.4);
  EXPECT_EQ(ParseSuffixedInt64("8.4M", kDefaultVal), k8p4M);
  EXPECT_EQ(ParseSuffixedInt64("8.4m", kDefaultVal), k8p4M);

  constexpr double k8p4Mf = kM * 8.4;
  EXPECT_DOUBLE_EQ(ParseSuffixedDouble("8.4M", kDefaultVal), k8p4Mf);
  EXPECT_DOUBLE_EQ(ParseSuffixedDouble("8.4m", kDefaultVal), k8p4Mf);

  EXPECT_EQ(ParseSuffixedInt64("1024k", kDefaultVal), kM);
  EXPECT_EQ(ParseSuffixedInt64("1M", kDefaultVal), kM);
  EXPECT_EQ(ParseSuffixedInt64("1m", kDefaultVal), kM);
  EXPECT_EQ(ParseSuffixedInt64("2G", kDefaultVal), 2LL << 30);
  EXPECT_EQ(ParseSuffixedInt64("3T", kDefaultVal), 3LL << 40);
  EXPECT_EQ(ParseSuffixedInt64("4P", kDefaultVal), 4LL << 50);
  EXPECT_EQ(ParseDecimalSuffixedInt64("2G", kDefaultVal), 2'000'000'000LL);
  EXPECT_EQ(ParseDecimalSuffixedInt64("3T", kDefaultVal), 3'000'000'000'000LL);
  EXPECT_EQ(ParseDecimalSuffixedInt64("4P", kDefaultVal),
            4'000'000'000'000'000LL);

  constexpr int kIntHalfM = 1 << 19;
  EXPECT_EQ(ParseSuffixedInt64("0.5M", kDefaultVal), kIntHalfM);
  EXPECT_EQ(ParseSuffixedInt64("512k", kDefaultVal), kIntHalfM);
  EXPECT_EQ(ParseDecimalSuffixedInt64("0.5M", kDefaultVal), 500000);
  EXPECT_EQ(ParseDecimalSuffixedInt64("512k", kDefaultVal), 512000);

  constexpr double kDoubleHalfM = 1.0 * (1 << 19);
  EXPECT_DOUBLE_EQ(ParseSuffixedDouble("0.5M", kDefaultVal), kDoubleHalfM);
  EXPECT_DOUBLE_EQ(ParseSuffixedDouble("512k", kDefaultVal), kDoubleHalfM);
  EXPECT_DOUBLE_EQ(ParseDecimalSuffixedDouble("0.5M", kDefaultVal), 500000.0);
  EXPECT_DOUBLE_EQ(ParseDecimalSuffixedDouble("512k", kDefaultVal), 512000.0);

  // error cases, should get default value of 123
  EXPECT_EQ(ParseSuffixedInt64("512a", kDefaultVal), 512);
  EXPECT_EQ(ParseSuffixedInt64("", kDefaultVal), kDefaultVal);
  EXPECT_EQ(ParseSuffixedInt64("WhatThe?", kDefaultVal), kDefaultVal);

  // C99 allows hex floating point constants.
  EXPECT_EQ(ParseSuffixedInt64("0x1.0", kDefaultVal), 1);

  // 1ULL<<63, when stored in an int64, is negative, so special handling is
  // needed for that value. Make sure that this special handling is
  // correct.
  EXPECT_EQ(BinaryEng(static_cast<int64_t>(uint64_t{1} << 63), 1, 1), "-8E");

  // Check minimum/maximum values that don't overflow.
  EXPECT_EQ(ParseSuffixedInt64("-9223372036854775808", kDefaultVal),
            std::numeric_limits<int64_t>::min());  // -2^63
  EXPECT_EQ(ParseSuffixedInt64("9223372036854775295", kDefaultVal),
            std::numeric_limits<int64_t>::max() - 1023);  // 2^63-513
  EXPECT_EQ(ParseSuffixedInt64("9223372036854774784", kDefaultVal),
            std::numeric_limits<int64_t>::max() - 1023);  // 2^63-1024

  // Test overflow/underflow for ParseSuffixedInt64
  EXPECT_EQ(ParseSuffixedInt64("9223372036854775808", kDefaultVal),
            std::numeric_limits<int64_t>::max());  // 2^63, overflows by 1
  EXPECT_EQ(ParseSuffixedInt64("10000000000G", kDefaultVal),
            kDefaultVal);  // overflows
  EXPECT_EQ(ParseSuffixedInt64("-9223372036854775809", kDefaultVal),
            std::numeric_limits<int64_t>::min());  // -2^63 - 1, underflows
  EXPECT_EQ(ParseSuffixedInt64("-10000000000G", kDefaultVal),
            kDefaultVal);  // underflows

  // Test overflow/underflow for ParseDecimalSuffixedInt64
  EXPECT_EQ(ParseDecimalSuffixedInt64("9223372036854775808", kDefaultVal),
            std::numeric_limits<int64_t>::max());  // overflows
  EXPECT_EQ(ParseDecimalSuffixedInt64("10000000000G", kDefaultVal),
            kDefaultVal);  // overflows
  EXPECT_EQ(ParseDecimalSuffixedInt64("-9223372036854775809", kDefaultVal),
            std::numeric_limits<int64_t>::min());  // underflows
  EXPECT_EQ(ParseDecimalSuffixedInt64("-10000000000G", kDefaultVal),
            kDefaultVal);  // underflows
}

TEST(NumberFormatTest, CustomUnitConvert) {
  constexpr int64_t kTimeUnits[] = {365 * 24 * 3600, 24 * 3600, 3600, 60, 1};
  const char* kTimeSuff[] = {"yr", "day", "hr", "min", ""};
  EXPECT_EQ(UnitConvert(0, kTimeUnits, 0, kTimeSuff, 5, 1, 1), "0");
  EXPECT_EQ(UnitConvert(90, kTimeUnits, 0, kTimeSuff, 5, 1, 1), "1.5min");
  EXPECT_EQ(UnitConvert(7200, kTimeUnits, 0, kTimeSuff, 5, 1, 1), "2hr");
}

}  // namespace
}  // namespace strings
