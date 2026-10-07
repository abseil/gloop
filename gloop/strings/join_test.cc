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

// Apache 2.0 Notice: 2026

// Unit tests for all join.h functions

#include "gloop/strings/join.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace strings {
namespace {

using RandomEngine = std::minstd_rand0;

struct JoinStringsTestCase {
  JoinStringsTestCase(absl::string_view d ABSL_ATTRIBUTE_LIFETIME_BOUND,
                      absl::string_view expected ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : delim(d), expected_result(expected) {}

  void AddSubstring(absl::string_view s) {
    string_ptr_array.push_back(std::make_unique<std::string>(s));
    substringpieces.push_back(*string_ptr_array.back());
    substrings.emplace_back(s);
  }

  absl::string_view delim;
  std::string expected_result;
  std::vector<std::string> substrings;
  std::vector<absl::string_view> substringpieces;
  std::vector<std::unique_ptr<std::string>> string_ptr_array;
};

// Tests JoinStrings/JoinStringsIterator
// Note: this test case would probably be better if written using a gUnit test
// fixture class.
TEST(JoinStrings, UsingRandom) {
  constexpr std::array<absl::string_view, 10> sample_strings = {
      "one", "two",   "three", "four", "five",
      "six", "seven", "eight", "nine", "ten"};

  std::vector<JoinStringsTestCase> testcases;
  // test empty
  testcases.emplace_back(" DELIM ", "");

  {  // test one entry
    JoinStringsTestCase t(" DELIM ", sample_strings[0]);
    t.AddSubstring(sample_strings[0]);
    testcases.push_back(std::move(t));
  }

  {  // add some random tuples
    constexpr absl::string_view delim = " DELIM ";

    RandomEngine rng(testing::UnitTest::GetInstance()->random_seed());
    std::uniform_int_distribution<int> random_2_to_10(2, 9);
    std::uniform_int_distribution<int> random_to_num_sample_strings(
        0, sample_strings.size() - 1);
    for (int i = 0; i < 20; ++i) {
      const int num_substrings = random_2_to_10(rng);
      std::vector<std::string> substrings;
      std::string result;
      for (int j = 0; j < num_substrings; ++j) {
        const std::string ss(sample_strings[random_to_num_sample_strings(rng)]);
        substrings.push_back(ss);
        if (j == 0)
          result = ss;
        else
          absl::StrAppend(&result, delim, ss);
      }
      JoinStringsTestCase t(delim, result);
      for (int j = 0; j < num_substrings; ++j) t.AddSubstring(substrings[j]);
      testcases.push_back(std::move(t));
    }
  }

  // now loop thru to make sure for all even number i
  //   JoinStrings(substrings) == expected_result
  //
  // Testing JoinStrings
  for (const JoinStringsTestCase& testcase : testcases) {
    EXPECT_EQ(absl::StrJoin(testcase.substrings, testcase.delim),
              testcase.expected_result);
    EXPECT_EQ(absl::StrJoin(testcase.substringpieces, testcase.delim),
              testcase.expected_result);
  }

  // Testing JoinStringsIterator
  for (const JoinStringsTestCase& testcase : testcases) {
    EXPECT_EQ(absl::StrJoin(testcase.substrings.begin(),
                            testcase.substrings.end(), testcase.delim),
              testcase.expected_result);
    EXPECT_EQ(absl::StrJoin(testcase.substringpieces.begin(),
                            testcase.substringpieces.end(), testcase.delim),
              testcase.expected_result);
  }
}

TEST(JoinCSVLine, Basics) {
  std::vector<std::string> test_vector = {
      "Google", "x", "Buchheit, Paul", "string with \" quote in it", " space "};
  std::string answer_string;

  JoinCSVLine(test_vector, &answer_string);
  EXPECT_EQ(answer_string,
            "Google,x,\"Buchheit, Paul\",\"string with \"\" quote in it\","
            "\" space \"")
      << "\n";
  EXPECT_EQ(JoinCSVLine(test_vector),
            "Google,x,\"Buchheit, Paul\",\"string with \"\" quote in it\","
            "\" space \"")
      << "\n";

  test_vector = {"Google", "I have a space", ""};
  answer_string.clear();

  JoinCSVLine(test_vector, &answer_string);
  EXPECT_EQ(answer_string, "Google,I have a space,");
  EXPECT_EQ(JoinCSVLine(test_vector), "Google,I have a space,");

  test_vector = {",", " beginning", "end ", " ", "\t", "\v", "\n", "\r"};
  answer_string.clear();

  JoinCSVLine(test_vector, &answer_string);
  EXPECT_EQ(answer_string,
            "\",\",\" beginning\",\"end \",\" \",\"\t\",\"\v\",\"\n\",\"\r\"");
  EXPECT_EQ(JoinCSVLine(test_vector),
            "\",\",\" beginning\",\"end \",\" \",\"\t\",\"\v\",\"\n\",\"\r\"");
}

TEST(JoinCSVLineWithDelimiter, Basics) {
  std::string answer_string;

  JoinCSVLineWithDelimiter({"a\rb"}, '.', &answer_string);
  EXPECT_EQ(answer_string, "\"a\rb\"");
  answer_string.clear();

  JoinCSVLineWithDelimiter({"a\nb"}, '.', &answer_string);
  EXPECT_EQ(answer_string, "\"a\nb\"");
  answer_string.clear();

  JoinCSVLineWithDelimiter({"gooGle"}, 'G', &answer_string);
  EXPECT_EQ(answer_string, "\"gooGle\"");
  answer_string.clear();

  JoinCSVLineWithDelimiter({"the", "google"}, 'G', &answer_string);
  EXPECT_EQ(answer_string, "theGgoogle");
  answer_string.clear();

  JoinCSVLineWithDelimiter({"a", "b"}, '\0', &answer_string);
  EXPECT_EQ(answer_string, std::string("a\0b", 3));
  answer_string.clear();

  JoinCSVLineWithDelimiter({"a", "b\""}, '\0', &answer_string);
  EXPECT_EQ(answer_string, std::string("a\0\"b\"\"\"", 7));
  answer_string.clear();

  JoinCSVLineWithDelimiter({"Going", "to", "gooGle"}, 'G', &answer_string);
  EXPECT_EQ(answer_string, "\"Going\"GtoG\"gooGle\"");
  answer_string.clear();

  JoinCSVLineWithDelimiter({"this example", "  uses ", "spaces"}, ' ',
                           &answer_string);
  EXPECT_EQ(answer_string, "\"this example\" \"  uses \" spaces");
  answer_string.clear();

  JoinCSVLineWithDelimiter(
      {absl::StrCat("a\"", std::string(1, '\0'), "bcd"),
       absl::StrCat("hello ", std::string(2, '\0'), "\" world\"")},
      ' ', &answer_string);
  EXPECT_EQ(answer_string,
            absl::StrCat("\"a\"\"", std::string(1, '\0'), "bcd\" \"hello ",
                         std::string(2, '\0'), "\"\" world\"\"\""));
}

TEST(LegacyFormatter, FormatterAPI) {
  std::string s = "Testing: ";
  s += absl::StrJoin(std::set<int>{1}, "", LegacyFormatter());
  s += absl::StrJoin(std::set<int16_t>{2}, "", LegacyFormatter());
  s += absl::StrJoin(std::set<int64_t>{3}, "", LegacyFormatter());
  s += absl::StrJoin(std::set<float>{4.123456f}, "", LegacyFormatter());
  s += absl::StrJoin(std::set<double>{789.1011121314}, "", LegacyFormatter());
  s += absl::StrJoin(std::set<unsigned>{15}, "", LegacyFormatter());
  s += absl::StrJoin(std::set<size_t>{16}, "", LegacyFormatter());
  s +=
      absl::StrJoin(std::set<absl::string_view>{" OK "}, "", LegacyFormatter());
  EXPECT_EQ(s, "Testing: 1234.123456789.10111213141516 OK ");
}

}  // namespace
}  // namespace strings
