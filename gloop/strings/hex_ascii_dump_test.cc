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

#include "gloop/strings/hex_ascii_dump.h"

#include <string>

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace strings {

// A test string and a hex+ASCII dump of the same string.

const char kString[] = {0x00, 0x90, 0x69, 0xbd, 0x54, 0x00, 0x00, 0x0d, 0x61,
                        0x0f, 0x01, 0x89, 0x08, 0x00, 0x45, 0x00, 0x00, 0x1c,
                        0xfb, 0x98, 0x40, 0x00, 0x40, 0x01, 0x7e, 0x18, 0xd8,
                        0xef, 0x23, 0x01, 0x45, 0x5d, 0x7f, 0xe2, 0x08, 0x00,
                        0x6b, 0xcb, 0x0b, 0xc6, 0x80, 0x6e};

const char kHexDump[] =
    "0x0000:  0090 69bd 5400 000d 610f 0189 0800 4500  ..i.T...a.....E.\n"
    "0x0010:  001c fb98 4000 4001 7e18 d8ef 2301 455d  ....@.@.~...#.E]\n"
    "0x0020:  7fe2 0800 6bcb 0bc6 806e                 ....k....n\n";

const char kHexDumpAt0800[] =
    "0x0800:  0090 69bd 5400 000d 610f 0189 0800 4500  ..i.T...a.....E.\n"
    "0x0810:  001c fb98 4000 4001 7e18 d8ef 2301 455d  ....@.@.~...#.E]\n"
    "0x0820:  7fe2 0800 6bcb 0bc6 806e                 ....k....n\n";

TEST(StringToHexASCIIDump, DumpArgTypes) {
  // Verify that char*, string and string_view are all valid argument types.
  std::string in = "original";
  std::string out =
      "0x0000:  6f72 6967 696e 616c                      original\n";
  EXPECT_EQ(out, StringToHexASCIIDump(in));                     // char*
  EXPECT_EQ(out, StringToHexASCIIDump(in));                     // string
  EXPECT_EQ(out, StringToHexASCIIDump(absl::string_view(in)));  // string_view
}

TEST(StringToHexASCIIDump, Success) {
  EXPECT_EQ(std::string(kHexDump),
            StringToHexASCIIDump(std::string(kString, sizeof(kString))));
}

TEST(StringToHexASCIIDumpAtOffset, Success) {
  EXPECT_EQ(std::string(kHexDumpAt0800),
            StringToHexASCIIDumpAtOffset(
                0x0800, std::string(kString, sizeof(kString))));
}

TEST(StringToHexASCIIDump, ZeroBytes) {
  EXPECT_EQ(std::string(""), StringToHexASCIIDump(""));
}

TEST(StringToHexASCIIDump, OneByte) {
  EXPECT_EQ(
      std::string("0x0000:  41                                       A\n"),
      StringToHexASCIIDump("A"));
}

TEST(StringToHexASCIIDump, TwoBytes) {
  EXPECT_EQ(
      std::string("0x0000:  4142                                     AB\n"),
      StringToHexASCIIDump("AB"));
}

TEST(StringToHexASCIIDump, ThreeBytes) {
  EXPECT_EQ(
      std::string("0x0000:  4142 43                                  ABC\n"),
      StringToHexASCIIDump("ABC"));
}

}  // namespace strings
