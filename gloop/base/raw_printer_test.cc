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

#include "gloop/base/raw_printer.h"

#include <string>

#include "absl/base/macros.h"
#include "gtest/gtest.h"

TEST(RawPrinter, Empty) {
  char buffer[1];
  base::RawPrinter printer(buffer, ABSL_ARRAYSIZE(buffer));
  EXPECT_EQ(0, printer.length());
  EXPECT_EQ(std::string(""), buffer);
  EXPECT_EQ(0, printer.space_left());
  printer.Printf("foo");
  EXPECT_EQ(std::string(""), std::string(buffer));
  EXPECT_EQ(0, printer.length());
  EXPECT_EQ(0, printer.space_left());
}

TEST(RawPrinter, PartiallyFilled) {
  char buffer[100];
  base::RawPrinter printer(buffer, ABSL_ARRAYSIZE(buffer));
  printer.Printf("%s %s", "hello", "world");
  EXPECT_EQ(std::string("hello world"), std::string(buffer));
  EXPECT_EQ(11, printer.length());
  EXPECT_LT(0, printer.space_left());
}

TEST(RawPrinter, Truncated) {
  char buffer[3];
  base::RawPrinter printer(buffer, ABSL_ARRAYSIZE(buffer));
  printer.Printf("%d", 12345678);
  EXPECT_EQ(std::string("12"), std::string(buffer));
  EXPECT_EQ(2, printer.length());
  EXPECT_EQ(0, printer.space_left());
}

TEST(RawPrinter, ExactlyFilled) {
  char buffer[12];
  base::RawPrinter printer(buffer, ABSL_ARRAYSIZE(buffer));
  printer.Printf("%s %s", "hello", "world");
  EXPECT_EQ(std::string("hello world"), std::string(buffer));
  EXPECT_EQ(11, printer.length());
  EXPECT_EQ(0, printer.space_left());
}

TEST(RawPrinter, Reset) {
  char buffer[2];
  base::RawPrinter printer(buffer, ABSL_ARRAYSIZE(buffer));
  printer.Printf("x");
  EXPECT_EQ(std::string("x"), std::string(buffer));
  printer.reset();
  EXPECT_EQ(0, printer.length());
  EXPECT_EQ(1, printer.space_left());
  printer.Printf("y");
  EXPECT_EQ(std::string("y"), std::string(buffer));
  EXPECT_EQ(1, printer.length());
  EXPECT_EQ(0, printer.space_left());
}
