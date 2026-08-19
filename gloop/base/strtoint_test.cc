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

#include "gloop/base/strtoint.h"

#include <errno.h>
#include <stdint.h>

#include <limits>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using ::testing::StrEq;

TEST(StrutilTest, StrtoFunctions) {
  char* endptr = nullptr;

  // 64-bit conversions are pass-through on all current platforms
  errno = 0;
  EXPECT_EQ(strto64("0", nullptr, 0), 0);
  EXPECT_EQ(errno, 0);

  errno = 0;
  EXPECT_EQ(strto64("9223372036854775807", &endptr, 0),
            std::numeric_limits<int64_t>::max());
  EXPECT_EQ(errno, 0);
  EXPECT_THAT(endptr, StrEq(""));

  errno = 0;
  EXPECT_EQ(strto64("-9223372036854775808", &endptr, 0),
            std::numeric_limits<int64_t>::min());
  EXPECT_EQ(errno, 0);
  EXPECT_THAT(endptr, StrEq(""));

  errno = 0;
  EXPECT_EQ(strtou64("18446744073709551615", &endptr, 0),
            std::numeric_limits<uint64_t>::max());
  EXPECT_EQ(errno, 0);
  EXPECT_THAT(endptr, StrEq(""));

  // safe signed 32-bit conversions within 32-bit range
  errno = 0;
  EXPECT_EQ(strto32("0", nullptr, 0), 0);
  EXPECT_EQ(errno, 0);

  errno = 0;
  EXPECT_EQ(strto32("2147483647", &endptr, 0),
            std::numeric_limits<int32_t>::max());
  EXPECT_EQ(errno, 0);
  EXPECT_THAT(endptr, StrEq(""));

  errno = 0;
  EXPECT_EQ(strto32("-2147483648", &endptr, 0),
            std::numeric_limits<int32_t>::min());
  EXPECT_EQ(errno, 0);
  EXPECT_THAT(endptr, StrEq(""));

  // signed 32-bit conversions outside 32-bit range, but inside 64-bit range
  errno = 0;
  EXPECT_EQ(strto32("2147483648", &endptr, 0),
            std::numeric_limits<int32_t>::max());
  EXPECT_EQ(errno, ERANGE);
  EXPECT_THAT(endptr, StrEq(""));

  errno = 0;
  EXPECT_EQ(strto32("9223372036854775807", &endptr, 0),
            std::numeric_limits<int32_t>::max());
  EXPECT_EQ(errno, ERANGE);
  EXPECT_THAT(endptr, StrEq(""));

  errno = 0;
  EXPECT_EQ(strto32("-2147483649", &endptr, 0),
            std::numeric_limits<int32_t>::min());
  EXPECT_EQ(errno, ERANGE);
  EXPECT_THAT(endptr, StrEq(""));

  errno = 0;
  EXPECT_EQ(strto32("-9223372036854775808", &endptr, 0),
            std::numeric_limits<int32_t>::min());
  EXPECT_EQ(errno, ERANGE);
  EXPECT_THAT(endptr, StrEq(""));

  // signed 32-bit conversions outside both 32 and 64-bit ranges
  errno = 0;
  EXPECT_EQ(strto32("922337203685477580700000", &endptr, 0),
            std::numeric_limits<int32_t>::max());
  EXPECT_EQ(errno, ERANGE);
  EXPECT_THAT(endptr, StrEq(""));

  errno = 0;
  EXPECT_EQ(strto32("-922337203685477580800000", &endptr, 0),
            std::numeric_limits<int32_t>::min());
  EXPECT_EQ(errno, ERANGE);
  EXPECT_THAT(endptr, StrEq(""));

  // safe unsigned 32-bit conversions within 32-bit range
  errno = 0;
  EXPECT_EQ(strtou32("0", nullptr, 0), 0);
  EXPECT_EQ(errno, 0);

  errno = 0;
  EXPECT_EQ(strtou32("4294967295", &endptr, 0),
            std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(errno, 0);
  EXPECT_THAT(endptr, StrEq(""));

  // unsigned 32-bit conversions outside 32-bit range, but inside 64-bit range
  errno = 0;
  EXPECT_EQ(strtou32("4294967296", &endptr, 0),
            std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(errno, ERANGE);
  EXPECT_THAT(endptr, StrEq(""));

  errno = 0;
  EXPECT_EQ(strtou32("18446744073709551615", &endptr, 0),
            std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(errno, ERANGE);
  EXPECT_THAT(endptr, StrEq(""));

  // unsigned 32-bit conversions outside both 32 and 64-bit ranges
  errno = 0;
  EXPECT_EQ(strtou32("1844674407370955161500000", &endptr, 0),
            std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(errno, ERANGE);
  EXPECT_THAT(endptr, StrEq(""));

  // verify endptr return values for signed and unsigned 32-bit conversions
  errno = 0;
  EXPECT_EQ(strto32("xyz", &endptr, 0), 0);
  EXPECT_THAT(endptr, StrEq("xyz"));

  errno = 0;
  EXPECT_EQ(strto32("922337203685477580700000abc", &endptr, 0),
            std::numeric_limits<int32_t>::max());
  EXPECT_EQ(errno, ERANGE);
  EXPECT_THAT(endptr, StrEq("abc"));

  errno = 0;
  EXPECT_EQ(strto32("-922337203685477580800000abc", &endptr, 0),
            std::numeric_limits<int32_t>::min());
  EXPECT_EQ(errno, ERANGE);
  EXPECT_THAT(endptr, StrEq("abc"));

  errno = 0;
  EXPECT_EQ(strtou32("xyz", &endptr, 0), 0);
  EXPECT_THAT(endptr, StrEq("xyz"));

  errno = 0;
  EXPECT_EQ(strtou32("1844674407370955161500000abc", &endptr, 0),
            std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(errno, ERANGE);
  EXPECT_THAT(endptr, StrEq("abc"));

  // verify errno preservation for valid conversions, and try other bases
  errno = 12345;
  EXPECT_EQ(strto32("0x7fffffffxyz", &endptr, 0),
            std::numeric_limits<int32_t>::max());
  EXPECT_EQ(errno, 12345);
  EXPECT_THAT(endptr, StrEq("xyz"));

  errno = 23456;
  EXPECT_EQ(strto32("017777777777xyz", &endptr, 0),
            std::numeric_limits<int32_t>::max());
  EXPECT_EQ(errno, 23456);
  EXPECT_THAT(endptr, StrEq("xyz"));

  errno = 34567;
  EXPECT_EQ(strtou32("0xffffffffxyz", &endptr, 0),
            std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(errno, 34567);
  EXPECT_THAT(endptr, StrEq("xyz"));

  errno = 45678;
  EXPECT_EQ(strtou32("037777777777xyz", &endptr, 0),
            std::numeric_limits<uint32_t>::max());
  EXPECT_EQ(errno, 45678);
  EXPECT_THAT(endptr, StrEq("xyz"));
}

TEST(StrutilTest, AtoiFunctions) {
  // basic atoi32/64 functions, including checks for overflow equivalency
  // even in invalid conversions
  EXPECT_EQ(atoi64("0"), 0);
  EXPECT_EQ(atoi64("12345"), 12345);
  EXPECT_EQ(atoi64("-12345"), -12345);
  EXPECT_EQ(atoi64("9223372036854775807"), std::numeric_limits<int64_t>::max());
  EXPECT_EQ(atoi64("-9223372036854775808"),
            std::numeric_limits<int64_t>::min());
  EXPECT_EQ(atoi64("9223372036854775808"), std::numeric_limits<int64_t>::max());
  EXPECT_EQ(atoi64("-9223372036854775809"),
            std::numeric_limits<int64_t>::min());
  EXPECT_EQ(atoi64("18446744073709551615"),
            std::numeric_limits<int64_t>::max());

  EXPECT_EQ(atoi32("0"), 0);
  EXPECT_EQ(atoi32("12345"), 12345);
  EXPECT_EQ(atoi32("-12345"), -12345);
  EXPECT_EQ(atoi32("2147483647"), std::numeric_limits<int32_t>::max());
  EXPECT_EQ(atoi32("-2147483648"), std::numeric_limits<int32_t>::min());
  EXPECT_EQ(atoi32("2147483648"), std::numeric_limits<int32_t>::max());
  EXPECT_EQ(atoi32("-2147483649"), std::numeric_limits<int32_t>::min());
  EXPECT_EQ(atoi32("4294967295"), std::numeric_limits<int32_t>::max());

  // string convenience interfaces
  EXPECT_EQ(atoi64(std::string("0")), 0);
  EXPECT_EQ(atoi64(std::string("12345")), 12345);
  EXPECT_EQ(atoi64(std::string("-12345")), -12345);
  EXPECT_EQ(atoi64(std::string("18446744073709551615")),
            std::numeric_limits<int64_t>::max());

  EXPECT_EQ(atoi32(std::string("0")), 0);
  EXPECT_EQ(atoi32(std::string("12345")), 12345);
  EXPECT_EQ(atoi32(std::string("-12345")), -12345);
  EXPECT_EQ(atoi32(std::string("4294967295")),
            std::numeric_limits<int32_t>::max());
}

}  // namespace
