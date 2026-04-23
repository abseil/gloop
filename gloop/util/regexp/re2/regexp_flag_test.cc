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

#include "gloop/util/regexp/re2/regexp_flag.h"

#include <optional>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/reflection.h"
#include "absl/strings/string_view.h"
#include "gloop/base/commandlineflags.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "re2/re2.h"

ABSL_FLAG(re2::RegexpFlag, empty_flag, re2::RegexpFlag::OrDie(""),
          "a flag whose value is the empty string");
ABSL_FLAG(re2::RegexpFlag, nonempty_flag, re2::RegexpFlag::OrDie("a+"),
          "a flag whose value is not the empty string");
ABSL_FLAG(re2::RegexpFlag, uninitialized_flag, {},
          "a flag whose value is uninitialized");
ABSL_FLAG(std::optional<re2::RegexpFlag>, optional_flag, std::nullopt,
          "a flag whose value is optional");

ABSL_FLAG(re2::RegexpListFlag, empty_list_flag, {}, "empty regexp list");
ABSL_FLAG(re2::RegexpListFlag, list_flag,
          re2::RegexpListFlag::OrDie({"a+", "b+"}), "list with 2 regexps");

namespace {
using testing::ElementsAre;
using testing::Property;

testing::Matcher<re2::RegexpFlag> HasPattern(absl::string_view pattern) {
  return Property(&re2::RegexpFlag::operator*,
                  Property(&RE2::pattern, pattern));
}

TEST(RegexpFlagTest, OperatorsAndGet) {
  auto empty_flag = absl::GetFlag(FLAGS_empty_flag);
  EXPECT_EQ("", (*empty_flag).pattern());

  auto nonempty_flag = absl::GetFlag(FLAGS_nonempty_flag);
  EXPECT_EQ("a+", nonempty_flag->pattern());

  auto empty_copy = absl::GetFlag(FLAGS_empty_flag);
  EXPECT_EQ(empty_flag.get(), empty_copy.get());

  auto nonempty_copy = absl::GetFlag(FLAGS_nonempty_flag);
  EXPECT_EQ(nonempty_flag.get(), nonempty_copy.get());

  auto uninitialized_flag = absl::GetFlag(FLAGS_uninitialized_flag);
  EXPECT_EQ(uninitialized_flag.get(), nullptr);

  auto optional_flag = absl::GetFlag(FLAGS_optional_flag);
  EXPECT_EQ(optional_flag, std::nullopt);
}

TEST(RegexpFlagTest, OrDie_InvalidRegexp) {
  EXPECT_DEATH(re2::RegexpFlag::OrDie("(a+"), "Check failed: AbslParseFlag\\(");
}

TEST(RegexpFlagTest, ParseFlag_ValidRegexp) {
  // TODO - Remove explicit flagsaver.
  absl::FlagSaver saved_flags;
  EXPECT_NE("", SetCommandLineOption("nonempty_flag", "b+"));
  EXPECT_THAT(absl::GetFlag(FLAGS_nonempty_flag), HasPattern("b+"));
}

TEST(RegexpFlagTest, ParseFlag_InvalidRegexp) {
  // TODO - Remove explicit flagsaver.
  absl::FlagSaver saved_flags;
  EXPECT_EQ("", SetCommandLineOption("nonempty_flag", "(b+"));
  EXPECT_THAT(absl::GetFlag(FLAGS_nonempty_flag), HasPattern("a+"));
}

TEST(RegexpFlagTest, UnparseFlag) {
  std::string pattern;
  EXPECT_TRUE(GetCommandLineOption("nonempty_flag", &pattern));
  EXPECT_EQ("a+", pattern);
  EXPECT_TRUE(GetCommandLineOption("uninitialized_flag", &pattern));
  EXPECT_EQ("", pattern);
}

TEST(RegexpListFlagTest, GetFlag) {
  EXPECT_THAT(absl::GetFlag(FLAGS_empty_list_flag), ElementsAre());
  EXPECT_THAT(absl::GetFlag(FLAGS_list_flag),
              ElementsAre(HasPattern("a+"), HasPattern("b+")));
}

TEST(RegexpListFlagTest, OrDie_Comma) {
  EXPECT_DEATH(re2::RegexpListFlag::OrDie({"a,b"}), "commas in the patterns");
}

TEST(RegexpListFlagTest, OrDie_InvalidRegexp) {
  EXPECT_DEATH(re2::RegexpListFlag::OrDie({"(a+"}),
               "Check failed: AbslParseFlag\\(");
}

TEST(RegexpListFlagTest, ParseFlag_Empty) {
  // TODO - Remove explicit flagsaver.
  absl::FlagSaver saved_flags;
  EXPECT_NE("", SetCommandLineOption("list_flag", "a+,b+"));
  EXPECT_THAT(absl::GetFlag(FLAGS_list_flag),
              ElementsAre(HasPattern("a+"), HasPattern("b+")));
}

TEST(RegexpListFlagTest, ParseFlag_Comma) {
  // TODO - Remove explicit flagsaver.
  absl::FlagSaver saved_flags;
  EXPECT_NE("", SetCommandLineOption("list_flag", "a+,b+"));
  EXPECT_THAT(absl::GetFlag(FLAGS_list_flag),
              ElementsAre(HasPattern("a+"), HasPattern("b+")));
}

TEST(RegexpListFlagTest, ParseFlag_EscapedComma) {
  // TODO - Remove explicit flagsaver.
  absl::FlagSaver saved_flags;
  EXPECT_NE("", SetCommandLineOption("list_flag", "a+\\x2cb+"));
  EXPECT_THAT(absl::GetFlag(FLAGS_list_flag),
              ElementsAre(HasPattern("a+\\x2cb+")));
}

TEST(RegexpListFlagTest, ParseFlag_InvalidRegexp) {
  // TODO - Remove explicit flagsaver.
  absl::FlagSaver saved_flags;
  EXPECT_EQ("", SetCommandLineOption("list_flag", "a+,(b+,c+"));
  EXPECT_THAT(absl::GetFlag(FLAGS_list_flag),
              ElementsAre(HasPattern("a+"), HasPattern("b+")));
}

TEST(RegexpListFlagTest, UnparseFlag) {
  std::string pattern_list;
  EXPECT_TRUE(GetCommandLineOption("empty_list_flag", &pattern_list));
  EXPECT_EQ(pattern_list, "");
  EXPECT_TRUE(GetCommandLineOption("list_flag", &pattern_list));
  EXPECT_EQ(pattern_list, "a+,b+");
}

}  // namespace

struct UTF8Options {
  RE2::Options operator()() const {
    RE2::Options options;
    options.set_encoding(RE2::Options::EncodingUTF8);
    return options;
  }
};

struct Latin1Options {
  RE2::Options operator()() const {
    RE2::Options options;
    options.set_encoding(RE2::Options::EncodingLatin1);
    return options;
  }
};

ABSL_FLAG(re2::RegexpFlagWithOptions<UTF8Options>, utf8_flag,
          re2::RegexpFlagWithOptions<UTF8Options>::OrDie(".*"),
          "a flag whose encoding option is set to UTF-8");
ABSL_FLAG(re2::RegexpFlagWithOptions<Latin1Options>, latin1_flag,
          re2::RegexpFlagWithOptions<Latin1Options>::OrDie(".*"),
          "a flag whose encoding option is set to Latin-1");

namespace {

TEST(RegexpFlagWithOptionsTest, UTF8VersusLatin1) {
  auto utf8_flag = absl::GetFlag(FLAGS_utf8_flag);
  auto latin1_flag = absl::GetFlag(FLAGS_latin1_flag);
  EXPECT_EQ(utf8_flag->pattern(), latin1_flag->pattern());
  // We are relying on the fact that the Prog for .* in UTF-8 is
  // much bigger than the Prog for .* in Latin-1.
  EXPECT_GT(utf8_flag->ProgramSize(), latin1_flag->ProgramSize());
}

}  // namespace
