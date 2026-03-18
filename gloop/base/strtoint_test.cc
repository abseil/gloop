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

#include "gloop/base/strtoint.h"

#include <errno.h>
#include <stdint.h>

#include <limits>
#include <string>

#include "gtest/gtest.h"

namespace {

TEST(StrutilTest, StrtoFunctions) {
  char* endptr;

  // 64-bit conversions are pass-through on all current platforms
  errno = 0;
  EXPECT_EQ(0, strto64("0", nullptr, 0));
  EXPECT_EQ(0, errno);

  errno = 0;
  EXPECT_EQ(std::numeric_limits<int64_t>::max(),
            strto64("9223372036854775807", &endptr, 0));
  EXPECT_EQ(0, errno);
  EXPECT_EQ(std::string(""), endptr);

  errno = 0;
  EXPECT_EQ(std::numeric_limits<int64_t>::min(),
            strto64("-9223372036854775808", &endptr, 0));
  EXPECT_EQ(0, errno);
  EXPECT_EQ(std::string(""), endptr);

  errno = 0;
  EXPECT_EQ(std::numeric_limits<uint64_t>::max(),
            strtou64("18446744073709551615", &endptr, 0));
  EXPECT_EQ(0, errno);
  EXPECT_EQ(std::string(""), endptr);

  // safe signed 32-bit conversions within 32-bit range
  errno = 0;
  EXPECT_EQ(0, strto32("0", nullptr, 0));
  EXPECT_EQ(0, errno);

  errno = 0;
  EXPECT_EQ(std::numeric_limits<int32_t>::max(),
            strto32("2147483647", &endptr, 0));
  EXPECT_EQ(0, errno);
  EXPECT_EQ(std::string(""), endptr);

  errno = 0;
  EXPECT_EQ(std::numeric_limits<int32_t>::min(),
            strto32("-2147483648", &endptr, 0));
  EXPECT_EQ(0, errno);
  EXPECT_EQ(std::string(""), endptr);

  // signed 32-bit conversions outside 32-bit range, but inside 64-bit range
  errno = 0;
  EXPECT_EQ(std::numeric_limits<int32_t>::max(),
            strto32("2147483648", &endptr, 0));
  EXPECT_EQ(ERANGE, errno);
  EXPECT_EQ(std::string(""), endptr);

  errno = 0;
  EXPECT_EQ(std::numeric_limits<int32_t>::max(),
            strto32("9223372036854775807", &endptr, 0));
  EXPECT_EQ(ERANGE, errno);
  EXPECT_EQ(std::string(""), endptr);

  errno = 0;
  EXPECT_EQ(std::numeric_limits<int32_t>::min(),
            strto32("-2147483649", &endptr, 0));
  EXPECT_EQ(ERANGE, errno);
  EXPECT_EQ(std::string(""), endptr);

  errno = 0;
  EXPECT_EQ(std::numeric_limits<int32_t>::min(),
            strto32("-9223372036854775808", &endptr, 0));
  EXPECT_EQ(ERANGE, errno);
  EXPECT_EQ(std::string(""), endptr);

  // signed 32-bit conversions outside both 32 and 64-bit ranges
  errno = 0;
  EXPECT_EQ(std::numeric_limits<int32_t>::max(),
            strto32("922337203685477580700000", &endptr, 0));
  EXPECT_EQ(ERANGE, errno);
  EXPECT_EQ(std::string(""), endptr);

  errno = 0;
  EXPECT_EQ(std::numeric_limits<int32_t>::min(),
            strto32("-922337203685477580800000", &endptr, 0));
  EXPECT_EQ(ERANGE, errno);
  EXPECT_EQ(std::string(""), endptr);

  // safe unsigned 32-bit conversions within 32-bit range
  errno = 0;
  EXPECT_EQ(0, strtou32("0", nullptr, 0));
  EXPECT_EQ(0, errno);

  errno = 0;
  EXPECT_EQ(std::numeric_limits<uint32_t>::max(),
            strtou32("4294967295", &endptr, 0));
  EXPECT_EQ(0, errno);
  EXPECT_EQ(std::string(""), endptr);

  // unsigned 32-bit conversions outside 32-bit range, but inside 64-bit range
  errno = 0;
  EXPECT_EQ(std::numeric_limits<uint32_t>::max(),
            strtou32("4294967296", &endptr, 0));
  EXPECT_EQ(ERANGE, errno);
  EXPECT_EQ(std::string(""), endptr);

  errno = 0;
  EXPECT_EQ(std::numeric_limits<uint32_t>::max(),
            strtou32("18446744073709551615", &endptr, 0));
  EXPECT_EQ(ERANGE, errno);
  EXPECT_EQ(std::string(""), endptr);

  // unsigned 32-bit conversions outside both 32 and 64-bit ranges
  errno = 0;
  EXPECT_EQ(std::numeric_limits<uint32_t>::max(),
            strtou32("1844674407370955161500000", &endptr, 0));
  EXPECT_EQ(ERANGE, errno);
  EXPECT_EQ(std::string(""), endptr);

  // verify endptr return values for signed and unsigned 32-bit conversions
  errno = 0;
  EXPECT_EQ(0, strto32("xyz", &endptr, 0));
  EXPECT_EQ(std::string("xyz"), endptr);

  errno = 0;
  EXPECT_EQ(std::numeric_limits<int32_t>::max(),
            strto32("922337203685477580700000abc", &endptr, 0));
  EXPECT_EQ(ERANGE, errno);
  EXPECT_EQ(std::string("abc"), endptr);

  errno = 0;
  EXPECT_EQ(std::numeric_limits<int32_t>::min(),
            strto32("-922337203685477580800000abc", &endptr, 0));
  EXPECT_EQ(ERANGE, errno);
  EXPECT_EQ(std::string("abc"), endptr);

  errno = 0;
  EXPECT_EQ(0, strtou32("xyz", &endptr, 0));
  EXPECT_EQ(std::string("xyz"), endptr);

  errno = 0;
  EXPECT_EQ(std::numeric_limits<uint32_t>::max(),
            strtou32("1844674407370955161500000abc", &endptr, 0));
  EXPECT_EQ(ERANGE, errno);
  EXPECT_EQ(std::string("abc"), endptr);

  // verify errno preservation for valid conversions, and try other bases
  errno = 12345;
  EXPECT_EQ(std::numeric_limits<int32_t>::max(),
            strto32("0x7fffffffxyz", &endptr, 0));
  EXPECT_EQ(12345, errno);
  EXPECT_EQ(std::string("xyz"), endptr);

  errno = 23456;
  EXPECT_EQ(std::numeric_limits<int32_t>::max(),
            strto32("017777777777xyz", &endptr, 0));
  EXPECT_EQ(23456, errno);
  EXPECT_EQ(std::string("xyz"), endptr);

  errno = 34567;
  EXPECT_EQ(std::numeric_limits<uint32_t>::max(),
            strtou32("0xffffffffxyz", &endptr, 0));
  EXPECT_EQ(34567, errno);
  EXPECT_EQ(std::string("xyz"), endptr);

  errno = 45678;
  EXPECT_EQ(std::numeric_limits<uint32_t>::max(),
            strtou32("037777777777xyz", &endptr, 0));
  EXPECT_EQ(45678, errno);
  EXPECT_EQ(std::string("xyz"), endptr);
}

TEST(StrutilTest, AtoiFunctions) {
  // basic atoi32/64 functions, including checks for overflow equivalency
  // even in invalid conversions
  EXPECT_EQ(0, atoi64("0"));
  EXPECT_EQ(12345, atoi64("12345"));
  EXPECT_EQ(-12345, atoi64("-12345"));
  EXPECT_EQ(std::numeric_limits<int64_t>::max(), atoi64("9223372036854775807"));
  EXPECT_EQ(std::numeric_limits<int64_t>::min(),
            atoi64("-9223372036854775808"));
  EXPECT_EQ(std::numeric_limits<int64_t>::max(), atoi64("9223372036854775808"));
  EXPECT_EQ(std::numeric_limits<int64_t>::min(),
            atoi64("-9223372036854775809"));
  EXPECT_EQ(std::numeric_limits<int64_t>::max(),
            atoi64("18446744073709551615"));

  EXPECT_EQ(0, atoi32("0"));
  EXPECT_EQ(12345, atoi32("12345"));
  EXPECT_EQ(-12345, atoi32("-12345"));
  EXPECT_EQ(std::numeric_limits<int32_t>::max(), atoi32("2147483647"));
  EXPECT_EQ(std::numeric_limits<int32_t>::min(), atoi32("-2147483648"));
  EXPECT_EQ(std::numeric_limits<int32_t>::max(), atoi32("2147483648"));
  EXPECT_EQ(std::numeric_limits<int32_t>::min(), atoi32("-2147483649"));
  EXPECT_EQ(std::numeric_limits<int32_t>::max(), atoi32("4294967295"));

  // string convenience interfaces
  EXPECT_EQ(0, atoi64("0"));
  EXPECT_EQ(12345, atoi64("12345"));
  EXPECT_EQ(-12345, atoi64("-12345"));
  EXPECT_EQ(std::numeric_limits<int64_t>::max(),
            atoi64("18446744073709551615"));

  EXPECT_EQ(0, atoi32("0"));
  EXPECT_EQ(12345, atoi32("12345"));
  EXPECT_EQ(-12345, atoi32("-12345"));
  EXPECT_EQ(std::numeric_limits<int32_t>::max(), atoi32("4294967295"));
}

}  // namespace
