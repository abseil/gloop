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

#include "gloop/base/internal/munge_output.h"

#include <optional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace base_logging {
namespace logging_testing {
namespace {
using ::testing::Eq;
using ::testing::Optional;

TEST(MungeOutputTest, SimpleLine) {
  EXPECT_THAT(MungeLine("I0000 00:00:1497628097.764476 1005820 file.cc:497] "
                        "Message goes here"),
              Optional(Eq("IDATE TIME__ file.cc:LINE] Message goes here")));
}

TEST(MungeOutputTest, DifferentErrorString) {
  EXPECT_THAT(
      MungeLine(
          "I0218 12:16:00.097585       2 file.cc:497] Undefined error: 0"),
      Optional(Eq("IDATE TIME__ file.cc:LINE] Success")));
  EXPECT_THAT(
      MungeLine("I0218 12:16:00.097585       2 file.cc:497] Device not "
                "configured [6]"),
      Optional(Eq("IDATE TIME__ file.cc:LINE] No such device or address [6]")));
}

TEST(MungeOutputTest, NullPtr0x0) {
  EXPECT_THAT(MungeLine("I0218 12:16:00.097585       2 file.cc:49] ptr 0x0"),
              Optional(Eq("IDATE TIME__ file.cc:LINE] ptr (nil)")));
}

TEST(MungeOutputTest, FlagSaverLine) {
  EXPECT_THAT(MungeLine("I0000 00:00:1497628097.764476 1005820 flag.cc:295] "
                        "Restore saved value of logtostderr: true"),
              Eq(std::nullopt));
}

TEST(MungeOutputTest, NegativeThreadId) {
  EXPECT_THAT(MungeLine("I0000 00:00:1497628097.764476 -105820 file.cc:497] "
                        "Message goes here"),
              Optional(Eq("IDATE TIME__ file.cc:LINE] Message goes here")));
}

}  // namespace
}  // namespace logging_testing
}  // namespace base_logging
