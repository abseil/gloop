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

// This file tests string processing functions related to case:
// uppercase, lowercase, etc.

#include "gloop/strings/case.h"

#include <sstream>
#include <string>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace strings {

using ::testing::ElementsAre;

TEST(Case, GetAsciiCapitalizationTypeToString) {
  std::stringstream stream;
  stream << AsciiCapitalizationType::kLower << ","
         << AsciiCapitalizationType::kUpper << ","
         << AsciiCapitalizationType::kFirst << ","
         << AsciiCapitalizationType::kMixed << ","
         << AsciiCapitalizationType::kNoAlpha;
  EXPECT_THAT(absl::StrSplit(stream.str(), ","),
              ElementsAre("kLower", "kUpper", "kFirst", "kMixed", "kNoAlpha"));
}

TEST(Case, GetAsciiCapitalization) {
  ASSERT_EQ(GetAsciiCapitalization(""), AsciiCapitalizationType::kNoAlpha);
  ASSERT_EQ(GetAsciiCapitalization("*"), AsciiCapitalizationType::kNoAlpha);
  ASSERT_EQ(GetAsciiCapitalization("9123&(*4327"),
            AsciiCapitalizationType::kNoAlpha);
  ASSERT_EQ(GetAsciiCapitalization("k"), AsciiCapitalizationType::kLower);
  ASSERT_EQ(GetAsciiCapitalization("    k  "), AsciiCapitalizationType::kLower);
  ASSERT_EQ(GetAsciiCapitalization("jsdlkjfd asflj asdfj ewiei sdkl dj"),
            AsciiCapitalizationType::kLower);
  ASSERT_EQ(GetAsciiCapitalization("  #!&(!$($* sdf"),
            AsciiCapitalizationType::kLower);
  ASSERT_EQ(GetAsciiCapitalization("js&#sdj320asdf*$"),
            AsciiCapitalizationType::kLower);
  ASSERT_EQ(GetAsciiCapitalization("o'connell"),
            AsciiCapitalizationType::kLower);
  ASSERT_EQ(GetAsciiCapitalization("K"), AsciiCapitalizationType::kUpper);
  ASSERT_EQ(GetAsciiCapitalization("  K   "), AsciiCapitalizationType::kUpper);
  ASSERT_EQ(GetAsciiCapitalization("JKL JFJD EUOFGU ABFEIIIA  DOJJL"),
            AsciiCapitalizationType::kUpper);
  ASSERT_EQ(GetAsciiCapitalization("  30831*)#*)!#'', QWEOIOI"),
            AsciiCapitalizationType::kUpper);
  ASSERT_EQ(GetAsciiCapitalization("   Mjdof"),
            AsciiCapitalizationType::kFirst);
  ASSERT_EQ(GetAsciiCapitalization(" I nmx "), AsciiCapitalizationType::kFirst);
  ASSERT_EQ(GetAsciiCapitalization("jXqiXjfQ"),
            AsciiCapitalizationType::kMixed);
  ASSERT_EQ(GetAsciiCapitalization("  jX "), AsciiCapitalizationType::kMixed);
  ASSERT_EQ(GetAsciiCapitalization("Becky"), AsciiCapitalizationType::kFirst);
  ASSERT_EQ(GetAsciiCapitalization("O'Neill"), AsciiCapitalizationType::kMixed);
}

TEST(Case, AsciiCaseInsensitiveCompare) {
  const std::string a("abc");
  const std::string b("abC");
  ASSERT_EQ(AsciiCaseInsensitiveCompare(a, b), 0);
  ASSERT_FALSE(AsciiCaseInsensitiveLess()(a, b));
  ASSERT_TRUE(AsciiCaseInsensitiveEq()(a, b));

  const std::string c("abCa");
  ASSERT_LT(AsciiCaseInsensitiveCompare(a, c), 0);
  ASSERT_TRUE(AsciiCaseInsensitiveLess()(a, c));
  ASSERT_FALSE(AsciiCaseInsensitiveEq()(a, c));

  const std::string d("bcd");
  ASSERT_LT(AsciiCaseInsensitiveCompare(a, d), 0);
  ASSERT_TRUE(AsciiCaseInsensitiveLess()(a, d));
  ASSERT_FALSE(AsciiCaseInsensitiveEq()(a, d));

  const std::string e("aBd");
  ASSERT_LT(AsciiCaseInsensitiveCompare(a, e), 0);
  ASSERT_TRUE(AsciiCaseInsensitiveLess()(a, e));
  ASSERT_FALSE(AsciiCaseInsensitiveEq()(a, e));

  ASSERT_LT(AsciiCaseInsensitiveCompare("X_Z", "XYZ"), 0);
  ASSERT_TRUE(AsciiCaseInsensitiveLess()("X_Z", "XYZ"));
  ASSERT_FALSE(AsciiCaseInsensitiveEq()("X_Z", "XYZ"));
}

// Reproduce b/219968630.
TEST(Case, AsciiCaseInsensitiveCompareNull) {
  constexpr absl::string_view a{"X\0_Z", 4};
  constexpr absl::string_view b{"X\0YZ", 4};

  EXPECT_EQ(AsciiCaseInsensitiveCompare(a, a), 0);
  EXPECT_LT(AsciiCaseInsensitiveCompare(a, b), 0);
  EXPECT_TRUE(AsciiCaseInsensitiveLess()(a, b));
  EXPECT_FALSE(AsciiCaseInsensitiveLess()(b, a));
  EXPECT_FALSE(AsciiCaseInsensitiveEq()(a, b));
  EXPECT_TRUE(AsciiCaseInsensitiveEq()(a, a));
}

TEST(Case, AsciiCaseInsensitiveHash) {
  ASSERT_EQ(AsciiCaseInsensitiveHash()("A"), AsciiCaseInsensitiveHash()("a"));
}

TEST(Case, HeterogeneousLookupLess) {
  absl::btree_map<std::string, std::string, AsciiCaseInsensitiveLess> map;
  map.emplace("Key", "value");
  absl::string_view string_view_key("key");
  ASSERT_EQ(map.find(string_view_key)->second, "value");
}

TEST(Case, HeterogeneousLookupHashEq) {
  absl::flat_hash_map<std::string, std::string, AsciiCaseInsensitiveHash,
                      AsciiCaseInsensitiveEq>
      map;
  map.emplace("Key", "value");
  absl::string_view string_view_key("key");
  ASSERT_TRUE(map.contains(string_view_key));
  ASSERT_EQ(map.find(string_view_key)->second, "value");
}

// Test MakeAsciiTitlecase
TEST(Case, MakeAsciiTitlecase) {
  std::string s = "houston rockets";
  MakeAsciiTitlecase(&s, " ");
  ASSERT_EQ(s, "Houston Rockets");

  ASSERT_EQ(MakeAsciiTitlecase(s, " "), "Houston Rockets");

  s = "i am a googler";
  MakeAsciiTitlecase(&s, " ");
  ASSERT_EQ(s, "I Am A Googler");

  s = "";
  MakeAsciiTitlecase(&s, " ");
  ASSERT_EQ(s, "");

  s = "   ";
  MakeAsciiTitlecase(&s, " ");
  ASSERT_EQ(s, "   ");

  s = "i-am-A-GooGler";
  MakeAsciiTitlecase(&s, "-");
  ASSERT_EQ(s, "I-Am-A-GooGler");

  s = "i -_ am a googler.";
  MakeAsciiTitlecase(&s, "- _");
  ASSERT_EQ(s, "I -_ Am A Googler.");

  s = "i -_am a googler.";
  MakeAsciiTitlecase(&s, "- _");
  ASSERT_EQ(s, "I -_Am A Googler.");

  s = "how,are you?fine?";
  MakeAsciiTitlecase(&s, "?");
  ASSERT_EQ(s, "How,are you?Fine?");

  s = "how,are you?fine?";
  MakeAsciiTitlecase(&s, ", ");
  ASSERT_EQ(s, "How,Are You?fine?");

  s.assign("i\0am\0a\0googler", 14);
  MakeAsciiTitlecase(&s, " ");
  ASSERT_EQ(s, std::string("I\0am\0a\0googler", 14));

  s.assign("i\0am\0a\0googler", 14);
  MakeAsciiTitlecase(&s, absl::string_view("\0", 1));
  ASSERT_EQ(s, std::string("I\0Am\0A\0Googler", 14));
}

}  // namespace strings
