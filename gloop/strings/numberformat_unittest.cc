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

#include <features.h>
#include <stdio.h>
#include <sys/types.h>

#include <cstdint>
#include <limits>
#include <string>

#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace strings {

static bool CheckBinaryEng(int64_t val, double thresh,
                           absl::string_view expect) {
  std::string got = strings::BinaryEng(val, thresh, 1);
  CHECK_EQ(got, expect);
  return true;
}

static bool CheckDecimalEng(int64_t val, double thresh,
                            absl::string_view expect) {
  std::string got = strings::DecimalEng(val, thresh, 1);
  CHECK_EQ(got, expect);
  return true;
}

static bool CheckDecimalEng(int64_t val, double thresh, uint precision,
                            absl::string_view expect) {
  std::string got = strings::DecimalEng(val, thresh, precision);
  CHECK_EQ(got, expect);
  return true;
}

static const int kDefaultVal = 123;

static void CheckParseInt64(int64_t val, const char* str) {
  const int64_t kZero = kDefaultVal;
  const int64_t get = strings::ParseSuffixedInt64(str, kZero);
  CHECK(val == get) << " exp=" << val << " got=" << get << " on=(" << str
                    << ")";
}

static void CheckParseDouble(double val, const char* str) {
  const double kZero = kDefaultVal;
  const double get = strings::ParseSuffixedDouble(str, kZero);
  CHECK(val == get) << " exp=" << val << " got=" << get << " on=(" << str
                    << ")";
}

static void CheckParseDecimalInt64(int64_t val, const char* str) {
  const int64_t kZero = kDefaultVal;
  const int64_t get = strings::ParseDecimalSuffixedInt64(str, kZero);
  CHECK(val == get) << " exp=" << val << " got=" << get << " on=(" << str
                    << ")";
}

static void CheckParseDecimalDouble(double val, const char* str) {
  const double kZero = kDefaultVal;
  const double get = strings::ParseDecimalSuffixedDouble(str, kZero);
  CHECK(val == get) << " exp=" << val << " got=" << get << " on=(" << str
                    << ")";
}

TEST(NumberFormatTest, AllTests) {
  const int64_t k87M = 87654321;
  CheckBinaryEng(k87M, 10, "83.6M");
  CheckBinaryEng(k87M, 1, "83.6M");
  CheckDecimalEng(k87M, 10, "87.7M");
  CheckDecimalEng(k87M, 1, "87.7M");
  CheckDecimalEng(k87M, 10, 0, "88M");
  CheckDecimalEng(k87M, 10, 1, "87.7M");
  CheckDecimalEng(k87M, 10, 2, "87.65M");
  CheckDecimalEng(k87M, 10, 4, "87.6543M");
  CheckDecimalEng(k87M, 10, 6, "87.654321M");

  // Fails due to precision.  get =    87.654320999999996M
  // CheckDecimalEng(k87M, 10, 15, "87.654321000000000M");

  // This works
  CheckDecimalEng(k87M, 10, 14, "87.65432100000000M");

  // Check negative numbers
  CheckBinaryEng(-k87M, 10, "-83.6M");
  CheckBinaryEng(-k87M, 1, "-83.6M");
  CheckDecimalEng(-k87M, 10, "-87.7M");
  CheckDecimalEng(-k87M, 1, "-87.7M");

  const int64_t k7M = 7654321;
  CheckBinaryEng(k7M, 10, "7474.9K");
  CheckBinaryEng(k7M, 1, "7.3M");
  CheckBinaryEng(k7M, 0, "7.3M");
  CheckDecimalEng(k7M, 10, "7654.3K");
  CheckDecimalEng(k7M, 1, "7.7M");
  CheckDecimalEng(k7M, 0, "7.7M");

  // Test floating point thresh
  const int64_t k500K = 500000;
  CheckDecimalEng(k500K, 0, "0.5M");
  CheckDecimalEng(k500K, 0.1, "0.5M");
  CheckDecimalEng(k500K, 0.6, "500K");
  // Don't want to start printing in Tera unless we specify a high precision
  CheckDecimalEng(k500K, 0.0000001, 2, "0.50M");
  CheckDecimalEng(k500K, 0.0000001, 7, "0.0000005T");

  // Test that large precision doesn't explode.
  CheckDecimalEng(550000, 1, 12, "550K");
  CheckDecimalEng(550000, 1, 37, "550K");

  const int64_t kM = 1LL << 20;
  CheckBinaryEng(kM, 10, "1M");
  CheckBinaryEng(kM, 1, "1M");
  CheckBinaryEng(kM, 0, "1M");

  const int64_t kMillion = 1000 * 1000;
  CheckDecimalEng(kMillion, 10, "1M");
  CheckDecimalEng(kMillion, 1, "1M");
  CheckDecimalEng(kMillion, 0, "1M");

  const int64_t kOne = 1;
  CheckBinaryEng(kOne, 1, "1");
  CheckBinaryEng(kOne, 10, "1");

  const int64_t kZero = 0;
  CheckDecimalEng(kZero, 0.1, "0");
  CheckDecimalEng(kZero, 5, "0");

  // Check the example in numberformat.h:
  CHECK_EQ(strings::DecimalEng(12345), "12.35K");
  // which is different from the check precision for these tests:
  CheckDecimalEng(12345, 1, "12.3K");

  const int64_t kRoundDown = static_cast<int64_t>(kM * 4.449);
  const int64_t kRoundUp = static_cast<int64_t>(kM * 4.49);
  CheckBinaryEng(kRoundDown, 4, "4.4M");
  CheckBinaryEng(kRoundUp, 4, "4.5M");

  const int64_t k8p4M = static_cast<int64_t>(kM * 8.4);
  CheckParseInt64(k8p4M, "8.4M");
  CheckParseInt64(k8p4M, "8.4m");

  const double k8p4Mf = kM * 8.4;
  CheckParseDouble(k8p4Mf, "8.4M");
  CheckParseDouble(k8p4Mf, "8.4m");

  CheckParseInt64(kM, "1024k");
  CheckParseInt64(kM, "1M");
  CheckParseInt64(kM, "1m");

  const int kIntHalfM = 1 << 19;
  CheckParseInt64(kIntHalfM, "0.5M");
  CheckParseInt64(kIntHalfM, "512k");
  CheckParseDecimalInt64(500000, "0.5M");
  CheckParseDecimalInt64(512000, "512k");

  const double kDoubleHalfM = 1.0 * (1 << 19);
  CheckParseDouble(kDoubleHalfM, "0.5M");
  CheckParseDouble(kDoubleHalfM, "512k");
  CheckParseDecimalDouble(500000.0, "0.5M");
  CheckParseDecimalDouble(512000.0, "512k");

  // error cases, should get default value of 123
  CheckParseInt64(512, "512a");
  CheckParseInt64(kDefaultVal, "");
  CheckParseInt64(kDefaultVal, "WhatThe?");

  // C99 allows hex floating point constants.
  CheckParseInt64(1, "0x1.0");

  // 1ULL<<63, when stored in an int64, is negative, so special handling is
  // needed for that value. Make sure that this special handling is
  // correct.
  CheckBinaryEng(static_cast<int64_t>(uint64_t{1} << 63), 1, "-8E");

  // Test overflow/underflow for ParseSuffixedInt64
  CheckParseInt64(std::numeric_limits<int64_t>::max(),
                  "9223372036854775808");        // 2^63, overflows by 1
  CheckParseInt64(kDefaultVal, "10000000000G");  // overflows
  CheckParseInt64(std::numeric_limits<int64_t>::min(),
                  "-9223372036854775809");        // -2^63 - 1, underflows
  CheckParseInt64(kDefaultVal, "-10000000000G");  // underflows

  // Test overflow/underflow for ParseDecimalSuffixedInt64
  CheckParseDecimalInt64(std::numeric_limits<int64_t>::max(),
                         "9223372036854775808");        // overflows
  CheckParseDecimalInt64(kDefaultVal, "10000000000G");  // overflows
  CheckParseDecimalInt64(std::numeric_limits<int64_t>::min(),
                         "-9223372036854775809");        // underflows
  CheckParseDecimalInt64(kDefaultVal, "-10000000000G");  // underflows
}

}  // namespace strings
