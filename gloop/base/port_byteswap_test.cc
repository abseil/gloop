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

// Test including base/port.h after <byteswap.h> has been included.
// The bswap_16, bswap_32, and bswap_64 macros should work.

// clang-format off
#include <byteswap.h>
#include <cstdint>
#include "gloop/base/port.h"
// clang-format on

#include "gtest/gtest.h"

namespace port_test {

TEST(PortTest, TestBSwapWithByteSwapIncluded) {
  EXPECT_EQ(0xAABB, bswap_16(0xBBAA));
  EXPECT_EQ(0xAABBCCDD, bswap_32(0xDDCCBBAA));
  EXPECT_EQ(uint64_t{uint64_t{0xAABBCCDDEEFF0011}},
            bswap_64(uint64_t{0x1100FFEEDDCCBBAA}));
}

}  // namespace port_test
