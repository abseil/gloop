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

// Unit tests for all join.h functions

#include "gloop/strings/join.h"

#include <cstddef>
#include <cstdint>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "absl/base/macros.h"
#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "gloop/strings/util.h"
#include "gtest/gtest.h"

namespace strings {
namespace {

using RandomEngine = std::minstd_rand0;

struct JoinStringsTestCase {
  // dummy to keep std::vector<> happy
  // JoinStringsTestCase() : delim(nullptr), expected_result("") {}

  JoinStringsTestCase(const char* d, absl::string_view expected)
      : delim(d), expected_result(expected) {}
  ~JoinStringsTestCase() {
    for (int i = 0; i < sub_c_strings.size(); ++i) {
      delete[] const_cast<char*>(sub_c_strings[i]);
      delete string_ptr_array[i];
    }
  }
  void AddSubstring(const std::string& s) {
    string_ptr_array[substrings.size()] = new std::string(s);
    substringpieces.push_back(*string_ptr_array[substrings.size()]);
    substrings.push_back(s);
    sub_c_strings.push_back(strdup_with_new(s.c_str()));
    // If we hit this CHECK, just increase size of string_ptr_array.
    CHECK_LT(substrings.size(), 100)
        << "If you hit this CHECK, just increase the size of string_ptr_array";
  }

  const char* delim;
  std::string expected_result;
  std::vector<std::string> substrings;
  std::vector<absl::string_view> substringpieces;
  std::vector<const char*> sub_c_strings;
  std::string* string_ptr_array[100];
};

// Tests JoinStrings/JoinStringsIterator
// Note: this test case would probably be better if written using a gUnit test
// fixture class.
TEST(JoinStrings, UsingRandom) {
  const char* sample_strings[] = {"one", "two",   "three", "four", "five",
                                  "six", "seven", "eight", "nine", "ten"};
  const int num_sample_strings = ABSL_ARRAYSIZE(sample_strings);

  std::vector<JoinStringsTestCase*> testcases;
  // test empty
  testcases.push_back(new JoinStringsTestCase(" DELIM ", std::string("")));

  {  // test one entry
    JoinStringsTestCase* t =
        new JoinStringsTestCase(" DELIM ", std::string(sample_strings[0]));
    t->AddSubstring(sample_strings[0]);
    testcases.push_back(t);
  }

  {  // add some random tuples
    const char* delim = " DELIM ";

    RandomEngine rng(testing::UnitTest::GetInstance()->random_seed());
    std::uniform_int_distribution<int> random_2_to_10(2, 9);
    std::uniform_int_distribution<int> random_to_num_sample_strings(
        0, num_sample_strings - 1);
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
      JoinStringsTestCase* t = new JoinStringsTestCase(delim, result);
      for (int j = 0; j < num_substrings; ++j) t->AddSubstring(substrings[j]);
      testcases.push_back(t);
    }
  }

  // now loop thru to make sure for all even number i
  //   JoinStrings(substrings) == expected_result
  //
  // Testing JoinStrings
  for (const JoinStringsTestCase* testcase : testcases) {
    std::string test_result;
    test_result = absl::StrJoin(testcase->substrings, testcase->delim);
    EXPECT_EQ(testcase->expected_result, test_result);
    test_result = absl::StrJoin(testcase->substringpieces, testcase->delim);
    EXPECT_EQ(testcase->expected_result, test_result);
  }

  // Testing JoinStringsIterator
  for (const JoinStringsTestCase* testcase : testcases) {
    std::string actual_result =
        absl::StrJoin(testcase->substrings.begin(), testcase->substrings.end(),
                      testcase->delim);
    EXPECT_EQ(testcase->expected_result, actual_result);
    actual_result =
        absl::StrJoin(testcase->substringpieces.begin(),
                      testcase->substringpieces.end(), testcase->delim);
    EXPECT_EQ(testcase->expected_result, actual_result);
  }

  // cleanup
  for (JoinStringsTestCase* testcase : testcases) {
    delete testcase;
  }
  testcases.clear();
}

TEST(JoinCSVLine, Basics) {
  std::vector<std::string> test_vector;
  std::string answer_string;

  test_vector.push_back("Google");
  test_vector.push_back("x");
  test_vector.push_back("Buchheit, Paul");
  test_vector.push_back("string with \" quote in it");
  test_vector.push_back(" space ");

  JoinCSVLine(test_vector, &answer_string);
  EXPECT_EQ(answer_string,
            "Google,x,\"Buchheit, Paul\",\"string with \"\" quote in it\","
            "\" space \"")
      << "\n";
  EXPECT_EQ(JoinCSVLine(test_vector),
            "Google,x,\"Buchheit, Paul\",\"string with \"\" quote in it\","
            "\" space \"")
      << "\n";

  test_vector.clear();
  answer_string.clear();

  test_vector.push_back("Google");
  test_vector.push_back("I have a space");
  test_vector.push_back("");

  JoinCSVLine(test_vector, &answer_string);
  EXPECT_EQ(answer_string, "Google,I have a space,");
  EXPECT_EQ(JoinCSVLine(test_vector), "Google,I have a space,");

  test_vector.clear();
  answer_string.clear();

  test_vector.push_back(",");
  test_vector.push_back(" beginning");
  test_vector.push_back("end ");
  test_vector.push_back(" ");
  test_vector.push_back("\t");
  test_vector.push_back("\v");
  test_vector.push_back("\n");
  test_vector.push_back("\r");

  JoinCSVLine(test_vector, &answer_string);
  EXPECT_EQ(answer_string,
            "\",\",\" beginning\",\"end \",\" \",\"\t\",\"\v\",\"\n\",\"\r\"");
  EXPECT_EQ(JoinCSVLine(test_vector),
            "\",\",\" beginning\",\"end \",\" \",\"\t\",\"\v\",\"\n\",\"\r\"");
}

TEST(JoinCSVLineWithDelimiter, Basics) {
  std::vector<std::string> test_vector;
  std::string answer_string;

  test_vector.push_back("gooGle");
  JoinCSVLineWithDelimiter(test_vector, 'G', &answer_string);
  EXPECT_EQ(answer_string, "\"gooGle\"");
  test_vector.clear();
  answer_string.clear();

  test_vector.push_back("the");
  test_vector.push_back("google");
  JoinCSVLineWithDelimiter(test_vector, 'G', &answer_string);
  EXPECT_EQ(answer_string, "theGgoogle");
  test_vector.clear();
  answer_string.clear();

  test_vector.push_back("Going");
  test_vector.push_back("to");
  test_vector.push_back("gooGle");
  JoinCSVLineWithDelimiter(test_vector, 'G', &answer_string);
  EXPECT_EQ(answer_string, "\"Going\"GtoG\"gooGle\"");
  test_vector.clear();
  answer_string.clear();

  test_vector.push_back("this example");
  test_vector.push_back("  uses ");
  test_vector.push_back("spaces");
  JoinCSVLineWithDelimiter(test_vector, ' ', &answer_string);
  EXPECT_EQ(answer_string, "\"this example\" \"  uses \" spaces");
}

}  // namespace
}  // namespace strings
