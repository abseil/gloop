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

// Unit tests for all split.h functions

#include "gloop/strings/split.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <cstdint>
#include <deque>
#include <limits>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-W#warnings"
#include <ext/hash_set>
#pragma clang diagnostic pop

#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/strings/charset.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gloop/strings/numbers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace strings {

using ::testing::DoubleNear;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::SizeIs;
using ::testing::StrEq;
using ::testing::UnorderedElementsAre;

TEST(OldSplit, SplitOneString) {
  LOG(INFO) << "Testing SplitOneString";
  // Parse strings
  const char* teststrings[] = {
      "alongword",  "alongword ", "alongword  ", "alongword anotherword",
      " alongword", "",           "a;b.c;d",
  };
  const char* source;

  source = teststrings[0];

  // Parse ints
  const char* testints[] = {
      "", "1", "1 ", "1Z ", "Z1 ", "-1",
  };
  int value;

  source = testints[0];
  ASSERT_TRUE(!SplitOneIntToken(&source, " ", &value));

  source = testints[1];
  ASSERT_TRUE(SplitOneIntToken(&source, " ", &value));
  ASSERT_EQ(source, nullptr);
  ASSERT_EQ(value, 1);

  source = testints[2];
  ASSERT_TRUE(SplitOneIntToken(&source, " ", &value));
  ASSERT_TRUE(source == testints[2] + 2);
  ASSERT_EQ(value, 1);

  source = testints[3];
  ASSERT_TRUE(!SplitOneIntToken(&source, " ", &value));

  source = testints[4];
  ASSERT_TRUE(!SplitOneIntToken(&source, " ", &value));

  source = testints[5];
  ASSERT_TRUE(SplitOneIntToken(&source, " ", &value));
  ASSERT_EQ(source, nullptr);
  ASSERT_EQ(value, -1);

  // Parse decimal ints, with overflows
  const char* testoverflowints[] = {"2147483647",
                                    "-2147483648",
                                    "2147483648",
                                    "-2147483649",
                                    "9223372036854775807",
                                    "-9223372036854775808",
                                    "922337203685477580700000",
                                    "-922337203685477580800000"};

  source = testoverflowints[0];
  ASSERT_TRUE(SplitOneIntToken(&source, " ", &value));
  ASSERT_EQ(source, nullptr);
  ASSERT_EQ(value, std::numeric_limits<int32_t>::max());

  source = testoverflowints[1];
  ASSERT_TRUE(SplitOneIntToken(&source, " ", &value));
  ASSERT_EQ(source, nullptr);
  ASSERT_EQ(value, std::numeric_limits<int32_t>::min());

  source = testoverflowints[2];
  ASSERT_TRUE(SplitOneIntToken(&source, " ", &value));
  ASSERT_EQ(source, nullptr);
  ASSERT_EQ(value, std::numeric_limits<int32_t>::max());

  source = testoverflowints[3];
  ASSERT_TRUE(SplitOneIntToken(&source, " ", &value));
  ASSERT_EQ(source, nullptr);
  ASSERT_EQ(value, std::numeric_limits<int32_t>::min());

  source = testoverflowints[4];
  ASSERT_TRUE(SplitOneIntToken(&source, " ", &value));
  ASSERT_EQ(source, nullptr);
  ASSERT_EQ(value, std::numeric_limits<int32_t>::max());

  source = testoverflowints[5];
  ASSERT_TRUE(SplitOneIntToken(&source, " ", &value));
  ASSERT_EQ(source, nullptr);
  ASSERT_EQ(value, std::numeric_limits<int32_t>::min());

  source = testoverflowints[6];
  ASSERT_TRUE(SplitOneIntToken(&source, " ", &value));
  ASSERT_EQ(source, nullptr);
  ASSERT_EQ(value, std::numeric_limits<int32_t>::max());

  source = testoverflowints[7];
  ASSERT_TRUE(SplitOneIntToken(&source, " ", &value));
  ASSERT_EQ(source, nullptr);
  ASSERT_EQ(value, std::numeric_limits<int32_t>::min());

  // Parse decimal ints
  const char* testdecints[] = {"",   "1",     "1 ",    "1Z ", "Z1 ",
                               "-1", "00010", "00090", "0x22"};

  source = testdecints[0];
  ASSERT_TRUE(!SplitOneDecimalIntToken(&source, " ", &value));

  source = testdecints[1];
  ASSERT_TRUE(SplitOneDecimalIntToken(&source, " ", &value));
  ASSERT_EQ(source, nullptr);
  ASSERT_EQ(value, 1);

  source = testdecints[2];
  ASSERT_TRUE(SplitOneDecimalIntToken(&source, " ", &value));
  ASSERT_TRUE(source == testints[2] + 2);
  ASSERT_EQ(value, 1);

  source = testdecints[3];
  ASSERT_TRUE(!SplitOneDecimalIntToken(&source, " ", &value));

  source = testdecints[4];
  ASSERT_TRUE(!SplitOneDecimalIntToken(&source, " ", &value));

  source = testdecints[5];
  ASSERT_TRUE(SplitOneDecimalIntToken(&source, " ", &value));
  ASSERT_EQ(source, nullptr);
  ASSERT_EQ(value, -1);

  source = testdecints[6];
  ASSERT_TRUE(SplitOneDecimalIntToken(&source, " ", &value));
  ASSERT_EQ(source, nullptr);
  ASSERT_EQ(value, 10);

  source = testdecints[7];
  ASSERT_TRUE(SplitOneDecimalIntToken(&source, " ", &value));
  ASSERT_EQ(source, nullptr);
  ASSERT_EQ(value, 90);

  source = testdecints[8];
  ASSERT_TRUE(!SplitOneDecimalIntToken(&source, " ", &value));

  // Parse hexadecimal ints
  const char* testhexints[] = {
      "",
      "0",
      "011 2",
      "ff",
      "deadbeef",
      "ffffffff0000000d",
      "ffffffff0000000dz",
      "0xff",
      "0xffx",
  };

  uint32_t uvalue32;
  uint64_t uvalue64;

  source = testhexints[0];
  ASSERT_TRUE(!SplitOneHexUint32Token(&source, " ", &uvalue32));

  source = testhexints[1];
  ASSERT_TRUE(SplitOneHexUint32Token(&source, " ", &uvalue32));
  ASSERT_EQ(source, nullptr);
  ASSERT_EQ(uvalue32, 0);

  source = testhexints[2];
  ASSERT_TRUE(SplitOneHexUint32Token(&source, " ", &uvalue32));
  ASSERT_TRUE(source == testhexints[2] + 4);
  ASSERT_EQ(uvalue32, 0x11);

  source = testhexints[3];
  ASSERT_TRUE(SplitOneHexUint32Token(&source, " ", &uvalue32));
  ASSERT_EQ(source, nullptr);
  ASSERT_EQ(uvalue32, 0xff);

  source = testhexints[4];
  ASSERT_TRUE(SplitOneHexUint32Token(&source, " ", &uvalue32));
  ASSERT_EQ(source, nullptr);
  ASSERT_EQ(uvalue32, 0xdeadbeefU);

  source = testhexints[5];
  ASSERT_TRUE(SplitOneHexUint64Token(&source, " ", &uvalue64));
  ASSERT_EQ(source, nullptr);
  ASSERT_EQ(uvalue64, uint64_t{0xffffffff0000000du});

  source = testhexints[6];
  ASSERT_TRUE(!SplitOneHexUint32Token(&source, " ", &uvalue32));

  source = testhexints[7];
  ASSERT_TRUE(SplitOneHexUint32Token(&source, " ", &uvalue32));
  ASSERT_EQ(source, nullptr);
  ASSERT_EQ(uvalue32, 0xff);

  source = testhexints[8];
  ASSERT_TRUE(!SplitOneHexUint32Token(&source, " ", &uvalue32));

  // Parse decimal ints, with overflows
  const char* testdecoverflowints[] = {"2147483647",
                                       "-2147483648",
                                       "2147483648",
                                       "-2147483649",
                                       "9223372036854775807",
                                       "-9223372036854775808",
                                       "922337203685477580700000",
                                       "-922337203685477580800000"};

  source = testdecoverflowints[0];
  ASSERT_TRUE(SplitOneDecimalIntToken(&source, " ", &value));
  ASSERT_EQ(source, nullptr);
  ASSERT_EQ(value, std::numeric_limits<int32_t>::max());

  source = testdecoverflowints[1];
  ASSERT_TRUE(SplitOneDecimalIntToken(&source, " ", &value));
  ASSERT_EQ(source, nullptr);
  ASSERT_EQ(value, std::numeric_limits<int32_t>::min());

  source = testdecoverflowints[2];
  ASSERT_TRUE(SplitOneDecimalIntToken(&source, " ", &value));
  ASSERT_EQ(source, nullptr);
  ASSERT_EQ(value, std::numeric_limits<int32_t>::max());

  source = testdecoverflowints[3];
  ASSERT_TRUE(SplitOneDecimalIntToken(&source, " ", &value));
  ASSERT_EQ(source, nullptr);
  ASSERT_EQ(value, std::numeric_limits<int32_t>::min());

  source = testdecoverflowints[4];
  ASSERT_TRUE(SplitOneDecimalIntToken(&source, " ", &value));
  ASSERT_EQ(source, nullptr);
  ASSERT_EQ(value, std::numeric_limits<int32_t>::max());

  source = testdecoverflowints[5];
  ASSERT_TRUE(SplitOneDecimalIntToken(&source, " ", &value));
  ASSERT_EQ(source, nullptr);
  ASSERT_EQ(value, std::numeric_limits<int32_t>::min());

  source = testdecoverflowints[6];
  ASSERT_TRUE(SplitOneDecimalIntToken(&source, " ", &value));
  ASSERT_EQ(source, nullptr);
  ASSERT_EQ(value, std::numeric_limits<int32_t>::max());

  source = testdecoverflowints[7];
  ASSERT_TRUE(SplitOneDecimalIntToken(&source, " ", &value));
  ASSERT_EQ(source, nullptr);
  ASSERT_EQ(value, std::numeric_limits<int32_t>::min());
}

// use Uint64 as representative to test repeated splitting of various
// SplitOne*Token() functions
TEST(OldSplit, RepeatedSplitOneUint64Token) {
  LOG(INFO) << "Testing Repeated SplitOneUint64Token";

  const char* const delim = ",";
  const char* curr_pos;
  uint64_t value;
  // test null parse
  curr_pos = nullptr;
  ASSERT_EQ(SplitOneUint64Token(&curr_pos, delim, &value), false);
  ASSERT_EQ(curr_pos, (const char*)nullptr);

  curr_pos = "";
  ASSERT_EQ(SplitOneUint64Token(&curr_pos, delim, &value), false);
  ASSERT_NE(curr_pos, (const char*)nullptr);
  ASSERT_EQ(curr_pos[0], '\0');

  // test repeated split
  const uint64_t testnumbers[] = {
      0x24,
      24,
      771030399567649802,
  };
  std::string parse_string = "0x24,24,771030399567649802";
  curr_pos = parse_string.c_str();
  std::vector<uint64_t> parse_numbers;
  while (SplitOneUint64Token(&curr_pos, delim, &value))
    parse_numbers.push_back(value);

  ASSERT_EQ(curr_pos, (const char*)nullptr);
  EXPECT_THAT(parse_numbers, ElementsAreArray(testnumbers));

  // test negative numbers (it should get converted to large positive numbers)
  parse_string.assign("0,-100");
  curr_pos = parse_string.c_str();
  parse_numbers.clear();
  while (SplitOneUint64Token(&curr_pos, delim, &value))
    parse_numbers.push_back(value);
  EXPECT_THAT(parse_numbers, ElementsAre(0, static_cast<uint64_t>(-100)));
  ASSERT_EQ(curr_pos, (const char*)nullptr);

  // test error string
  parse_string.assign("0,abc");
  curr_pos = parse_string.c_str();
  parse_numbers.clear();
  while (SplitOneUint64Token(&curr_pos, delim, &value))
    parse_numbers.push_back(value);
  EXPECT_THAT(parse_numbers, ElementsAre(0));
  ASSERT_EQ(curr_pos, parse_string.c_str() + 2);
}

TEST(OldSplit, SplitStructuredLine) {
  LOG(INFO) << "Testing SplitStructuredLine";
  char* t;
  std::vector<char*> results;

  // Same test cases as SplitStringAllowEmpty
  ASSERT_TRUE(!SplitStructuredLine(t = strdup(""), '#', "", &results));
  EXPECT_THAT(results, ElementsAre(StrEq("")));
  results.clear();
  free(t);

  ASSERT_TRUE(!SplitStructuredLine(t = strdup("#"), '#', "", &results));
  EXPECT_THAT(results, ElementsAre(StrEq(""), StrEq("")));
  results.clear();
  free(t);

  ASSERT_TRUE(!SplitStructuredLine(t = strdup("ab"), '#', "", &results));
  EXPECT_THAT(results, ElementsAre(StrEq("ab")));
  results.clear();
  free(t);

  ASSERT_TRUE(!SplitStructuredLine(t = strdup("ab#"), '#', "", &results));
  EXPECT_THAT(results, ElementsAre(StrEq("ab"), StrEq("")));
  results.clear();
  free(t);

  ASSERT_TRUE(!SplitStructuredLine(t = strdup("#cd"), '#', "", &results));
  EXPECT_THAT(results, ElementsAre(StrEq(""), StrEq("cd")));
  results.clear();
  free(t);

  ASSERT_TRUE(!SplitStructuredLine(t = strdup("ab#cd"), '#', "", &results));
  EXPECT_THAT(results, ElementsAre(StrEq("ab"), StrEq("cd")));
  results.clear();
  free(t);

  ASSERT_TRUE(!SplitStructuredLine(t = strdup("ab#cd#"), '#', "", &results));
  EXPECT_THAT(results, ElementsAre(StrEq("ab"), StrEq("cd"), StrEq("")));
  results.clear();
  free(t);

  ASSERT_TRUE(!SplitStructuredLine(t = strdup("ab##cd"), '#', "", &results));
  EXPECT_THAT(results, ElementsAre(StrEq("ab"), StrEq(""), StrEq("cd")));
  results.clear();
  free(t);

  ASSERT_TRUE(!SplitStructuredLine(t = strdup("##ab"), '#', "", &results));
  EXPECT_THAT(results, ElementsAre(StrEq(""), StrEq(""), StrEq("ab")));
  results.clear();
  free(t);

  ASSERT_TRUE(!SplitStructuredLine(t = strdup("ab##"), '#', "", &results));
  EXPECT_THAT(results, ElementsAre(StrEq("ab"), StrEq(""), StrEq("")));
  results.clear();
  free(t);

  // test cases for the "structured" part
  ASSERT_TRUE(
      !SplitStructuredLine(t = strdup("a,b([)4,45],c"), ',', "[]", &results));
  EXPECT_THAT(results, ElementsAre(StrEq("a"), StrEq("b([)4,45]"), StrEq("c")));
  results.clear();
  free(t);

  ASSERT_TRUE(!SplitStructuredLine(t = strdup("a,b(23,[4,45],c),d"), ',',
                                   "()[]", &results));
  EXPECT_THAT(results,
              ElementsAre(StrEq("a"), StrEq("b(23,[4,45],c)"), StrEq("d")));
  results.clear();
  free(t);

  ASSERT_TRUE(!SplitStructuredLine(t = strdup("ab,b(23,[4,45],c),[,]"), ',',
                                   "()[]", &results));
  EXPECT_THAT(results,
              ElementsAre(StrEq("ab"), StrEq("b(23,[4,45],c)"), StrEq("[,]")));
  results.clear();
  free(t);

  ASSERT_TRUE(!SplitStructuredLine(t = strdup("abc,b'23,[4,45],c'"), ',', "[]'",
                                   &results));
  EXPECT_THAT(results, ElementsAre(StrEq("abc"), StrEq("b'23,[4,45],c'")));
  results.clear();
  free(t);

  // Check some error detection
  ASSERT_TRUE(
      SplitStructuredLine(t = strdup("a,b([)4,45],c"), ',', "[]()", &results));
  results.clear();
  free(t);

  ASSERT_TRUE(
      SplitStructuredLine(t = strdup("a,b(4,45,c"), ',', "[]()", &results));
  results.clear();
  free(t);

  ASSERT_TRUE(
      SplitStructuredLine(t = strdup("a,b(4,45)),c"), ',', "[]()", &results));
  results.clear();
  free(t);

  // Check SplitStructuredLineWithEscapes allows escaped chars that
  // plain SplitStructuredLine would not
  ASSERT_TRUE(
      SplitStructuredLine(t = strdup("Blue\\'s Clues"), ',', "'", &results));
  results.clear();
  free(t);

  ASSERT_TRUE(!SplitStructuredLineWithEscapes(t = strdup("Blue\\'s Clues"), ',',
                                              "'", &results));
  EXPECT_THAT(results, ElementsAre(StrEq("Blue\\'s Clues")));
  results.clear();
  free(t);

  // Verify documented behavior.
  ASSERT_TRUE(!SplitStructuredLineWithEscapes(
      t = strdup("\\{item1\\},it\\\\em2,{\\{subitem1\\},sub\\\\item2},"
                 "item4\\,item5,[5,{6,7}]"),
      ',', "{}[]", &results));
  EXPECT_THAT(results, ElementsAre(StrEq("\\{item1\\}"), StrEq("it\\\\em2"),
                                   StrEq("{\\{subitem1\\},sub\\\\item2}"),
                                   StrEq("item4\\,item5"), StrEq("[5,{6,7}]")));
  results.clear();
  free(t);

  // string_view versions of the same tests.
  std::vector<absl::string_view> pieces;

  ASSERT_TRUE(SplitStructuredLine("", '#', "", &pieces));
  EXPECT_THAT(pieces, ElementsAre(""));
  pieces.clear();

  ASSERT_TRUE(SplitStructuredLine("#", '#', "", &pieces));
  EXPECT_THAT(pieces, ElementsAre("", ""));
  pieces.clear();

  ASSERT_TRUE(SplitStructuredLine("ab", '#', "", &pieces));
  EXPECT_THAT(pieces, ElementsAre("ab"));
  pieces.clear();

  ASSERT_TRUE(SplitStructuredLine("ab#", '#', "", &pieces));
  EXPECT_THAT(pieces, ElementsAre("ab", ""));
  pieces.clear();

  ASSERT_TRUE(SplitStructuredLine("#cd", '#', "", &pieces));
  EXPECT_THAT(pieces, ElementsAre("", "cd"));
  pieces.clear();

  ASSERT_TRUE(SplitStructuredLine("ab#cd", '#', "", &pieces));
  EXPECT_THAT(pieces, ElementsAre("ab", "cd"));
  pieces.clear();

  ASSERT_TRUE(SplitStructuredLine("ab#cd#", '#', "", &pieces));
  EXPECT_THAT(pieces, ElementsAre("ab", "cd", ""));
  pieces.clear();

  ASSERT_TRUE(SplitStructuredLine("ab##cd", '#', "", &pieces));
  EXPECT_THAT(pieces, ElementsAre("ab", "", "cd"));
  pieces.clear();

  ASSERT_TRUE(SplitStructuredLine("##ab", '#', "", &pieces));
  EXPECT_THAT(pieces, ElementsAre("", "", "ab"));
  pieces.clear();

  ASSERT_TRUE(SplitStructuredLine("ab##", '#', "", &pieces));
  EXPECT_THAT(pieces, ElementsAre("ab", "", ""));
  pieces.clear();

  // test cases for the "structured" part
  ASSERT_TRUE(SplitStructuredLine("a,b([)4,45],c", ',', "[]", &pieces));
  EXPECT_THAT(pieces, ElementsAre("a", "b([)4,45]", "c"));
  pieces.clear();

  ASSERT_TRUE(SplitStructuredLine("a,b(23,[4,45],c),d", ',', "()[]", &pieces));
  EXPECT_THAT(pieces, ElementsAre("a", "b(23,[4,45],c)", "d"));
  pieces.clear();

  ASSERT_TRUE(
      SplitStructuredLine("ab,b(23,[4,45],c),[,]", ',', "()[]", &pieces));
  EXPECT_THAT(pieces, ElementsAre("ab", "b(23,[4,45],c)", "[,]"));
  pieces.clear();

  ASSERT_TRUE(SplitStructuredLine("abc,b'23,[4,45],c'", ',', "[]'", &pieces));
  EXPECT_THAT(pieces, ElementsAre("abc", "b'23,[4,45],c'"));
  pieces.clear();

  // Check some error detection
  ASSERT_TRUE(!SplitStructuredLine("a,b([)4,45],c", ',', "[]()", &pieces));
  ASSERT_TRUE(!SplitStructuredLine("a,b(4,45,c", ',', "[]()", &pieces));
  ASSERT_TRUE(!SplitStructuredLine("a,b(4,45)),c", ',', "[]()", &pieces));
  pieces.clear();

  // Check SplitStructuredLineWithEscapes allows escaped chars that
  // plain SplitStructuredLine would not
  ASSERT_TRUE(!SplitStructuredLine("Blue\\'s Clues", ',', "'", &pieces));
  pieces.clear();
  ASSERT_TRUE(
      SplitStructuredLineWithEscapes("Blue\\'s Clues", ',', "'", &pieces));
  EXPECT_THAT(pieces, ElementsAre("Blue\\'s Clues"));
  pieces.clear();

  // Verify documented behavior.
  ASSERT_TRUE(SplitStructuredLineWithEscapes(
      "\\{item1\\},it\\\\em2,{\\{subitem1\\},sub\\\\item2},item4\\,item5,"
      "[5,{6,7}]",
      ',', "{}[]", &pieces));
  EXPECT_THAT(pieces, ElementsAre("\\{item1\\}", "it\\\\em2",
                                  "{\\{subitem1\\},sub\\\\item2}",
                                  "item4\\,item5", "[5,{6,7}]"));
  pieces.clear();
}

/* Copy from source to destination, ensuring there is enough space by
   allocating memory for destination.  This memory must be freed.  */
static void SafeStrCopy(char** destination, const char* source) {
  const int length = strlen(source) + 1;
  *destination = new char[length];
  strncpy(*destination, source, length);
}

TEST(OldSplit, SplitCSVLineWithDelimiter) {
  // Test a simple array of strings.
  const char* const_test_strings[] = {"Detroit\tTigers", "lead,ALC",
                                      "\"by\teight\"\tgames\t", "\t\t\t"};

  int test = 0;
  char* test_string = nullptr;
  std::vector<char*> answer_vector;

  SafeStrCopy(&test_string, const_test_strings[test]);
  SplitCSVLineWithDelimiter(test_string, '\t', &answer_vector);
  ASSERT_EQ(answer_vector.size(), 2);
  ASSERT_STREQ(answer_vector[0], "Detroit");
  ASSERT_STREQ(answer_vector[1], "Tigers");
  delete[] test_string;
  answer_vector.clear();
  test += 1;

  SafeStrCopy(&test_string, const_test_strings[test]);
  SplitCSVLineWithDelimiter(test_string, '\t', &answer_vector);
  ASSERT_EQ(answer_vector.size(), 1);
  ASSERT_STREQ(answer_vector[0], "lead,ALC");
  delete[] test_string;
  answer_vector.clear();
  test += 1;

  SafeStrCopy(&test_string, const_test_strings[test]);
  SplitCSVLineWithDelimiter(test_string, '\t', &answer_vector);
  ASSERT_EQ(answer_vector.size(), 4);
  ASSERT_STREQ(answer_vector[0], "\"by");
  ASSERT_STREQ(answer_vector[1], "eight\"");
  ASSERT_STREQ(answer_vector[2], "games");
  ASSERT_STREQ(answer_vector[3], "");
  delete[] test_string;
  answer_vector.clear();
  test += 1;

  SafeStrCopy(&test_string, const_test_strings[test]);
  SplitCSVLineWithDelimiter(test_string, '\t', &answer_vector);
  ASSERT_EQ(answer_vector.size(), 4);
  ASSERT_STREQ(answer_vector[0], "");
  ASSERT_STREQ(answer_vector[1], "");
  ASSERT_STREQ(answer_vector[2], "");
  ASSERT_STREQ(answer_vector[3], "");
  delete[] test_string;
  answer_vector.clear();
  test += 1;

  const std::string sentence =
      "Google, x , \"Buchheit, Paul\", \"string with \"\" quote in it\"";
  // Test the string wrapper function.
  std::vector<std::string> str_answer_vector;
  SplitCSVLineWithDelimiterForStrings(sentence, ',', &str_answer_vector);
  ASSERT_EQ(str_answer_vector.size(), 4);
  ASSERT_STREQ(str_answer_vector[0].c_str(), "Google");
  ASSERT_STREQ(str_answer_vector[1].c_str(), "x");
  ASSERT_STREQ(str_answer_vector[2].c_str(), "Buchheit, Paul");
  ASSERT_STREQ(str_answer_vector[3].c_str(), "string with \" quote in it");
}

TEST(OldSplit, SplitCSVLineWithDelimiterForStringsEmbeddedNUL) {
  std::string nul_char(1, '\0');
  std::vector<std::string> cols;
  SplitCSVLineWithDelimiterForStrings(absl::StrCat("a,b", nul_char, ",c"), ',',
                                      &cols);
  EXPECT_THAT(cols, ElementsAre("a", "b", "c"));
}

TEST(OldSplit, SplitCSVLine) {
  // Test a simple array of strings.
  const char* const_test_strings[] = {
      "Google, x , \"Buchheit, Paul\", \"string with \"\" quote in it\"",
      "Google   , hello,",
      "Google rocks,hello,",
      ",,\"\",,",
      "\",\", hello",
      "\"abc\"   , hello",
      "\"string without quotes\","};

  int test = 0;
  char* test_string = nullptr;
  std::vector<char*> answer_vector;

  SafeStrCopy(&test_string, const_test_strings[test]);
  SplitCSVLine(test_string, &answer_vector);
  EXPECT_THAT(answer_vector,
              ElementsAre(StrEq("Google"), StrEq("x"), StrEq("Buchheit, Paul"),
                          StrEq("string with \" quote in it")));
  delete[] test_string;
  answer_vector.clear();
  test += 1;

  // Test both a string ending in whitespace and a comma at the end of
  // a line.
  SafeStrCopy(&test_string, const_test_strings[test]);
  SplitCSVLine(test_string, &answer_vector);
  EXPECT_THAT(answer_vector,
              ElementsAre(StrEq("Google"), StrEq("hello"), StrEq("")));
  delete[] test_string;
  answer_vector.clear();
  test += 1;

  // Test strings without any whitespaces surrounding them.  Also test
  // a term with whitespace.
  SafeStrCopy(&test_string, const_test_strings[test]);
  SplitCSVLine(test_string, &answer_vector);
  EXPECT_THAT(answer_vector,
              ElementsAre(StrEq("Google rocks"), StrEq("hello"), StrEq("")));
  delete[] test_string;
  answer_vector.clear();
  test += 1;

  // Test empty strings.
  SafeStrCopy(&test_string, const_test_strings[test]);
  SplitCSVLine(test_string, &answer_vector);
  EXPECT_THAT(answer_vector, ElementsAre(StrEq(""), StrEq(""), StrEq(""),
                                         StrEq(""), StrEq("")));
  delete[] test_string;
  answer_vector.clear();
  test += 1;

  // Test a string containing a comma.
  SafeStrCopy(&test_string, const_test_strings[test]);
  SplitCSVLine(test_string, &answer_vector);
  EXPECT_THAT(answer_vector, ElementsAre(StrEq(","), StrEq("hello")));
  delete[] test_string;
  answer_vector.clear();
  test += 1;

  // Test omission of whitespace after a quoted string.
  SafeStrCopy(&test_string, const_test_strings[test]);
  SplitCSVLine(test_string, &answer_vector);
  EXPECT_THAT(answer_vector, ElementsAre(StrEq("abc"), StrEq("hello")));
  delete[] test_string;
  answer_vector.clear();
  test += 1;

  // Test a quoted string followed by a comma.
  SafeStrCopy(&test_string, const_test_strings[test]);
  SplitCSVLine(test_string, &answer_vector);
  EXPECT_THAT(answer_vector,
              ElementsAre(StrEq("string without quotes"), StrEq("")));
  delete[] test_string;
  answer_vector.clear();
  test += 1;

  // Test an empty string.
  SafeStrCopy(&test_string, "");
  SplitCSVLine(test_string, &answer_vector);
  ASSERT_EQ(answer_vector.size(), 0);
  delete[] test_string;
  answer_vector.clear();
  test += 1;

  // Test with a single newline.
  SafeStrCopy(&test_string, "\n");
  SplitCSVLine(test_string, &answer_vector);
  ASSERT_EQ(answer_vector.size(), 1);
  EXPECT_STREQ(answer_vector[0], "");
  delete[] test_string;
  answer_vector.clear();
  test += 1;
}

class SplitStringIntoKeyValuesTest : public testing::Test {
 protected:
  std::string key;
  std::vector<std::string> values;
};

TEST_F(SplitStringIntoKeyValuesTest, EmptyInputMultipleValues) {
  EXPECT_FALSE(SplitStringIntoKeyValues("",     // Empty input
                                        "\t ",  // Key separators
                                        " ,",   // Value separators
                                        &key, &values));
  EXPECT_THAT(key, IsEmpty());
  EXPECT_THAT(values, IsEmpty());
}

TEST_F(SplitStringIntoKeyValuesTest, EmptyValueInputMultipleValues) {
  EXPECT_FALSE(SplitStringIntoKeyValues("key_with_no_value ",
                                        "\t ",  // Key separators
                                        " ,",   // Value separators
                                        &key, &values));
  EXPECT_EQ(key, "key_with_no_value");
  EXPECT_THAT(values, IsEmpty());
}

TEST_F(SplitStringIntoKeyValuesTest, EmptyKeyInputMultipleValues) {
  EXPECT_TRUE(SplitStringIntoKeyValues(" value for empty key",
                                       "\t ",  // Key separators
                                       " ,",   // Value separators
                                       &key, &values));
  EXPECT_THAT(key, IsEmpty());
  EXPECT_THAT(values, ElementsAre("value", "for", "empty", "key"));
}

TEST_F(SplitStringIntoKeyValuesTest, KeyWithMultipleValues) {
  EXPECT_TRUE(SplitStringIntoKeyValues("key1 \t value1,   value2   value3",
                                       "\t ",  // Key separators
                                       " ,",   // Value separators
                                       &key, &values));
  EXPECT_EQ(key, "key1");
  EXPECT_THAT(values, ElementsAre("value1", "value2", "value3"));
}

TEST_F(SplitStringIntoKeyValuesTest, EmptyInputSingleValue) {
  EXPECT_FALSE(SplitStringIntoKeyValues("",     // Empty input
                                        "\t ",  // Key separators
                                        "",     // No value separators
                                        &key, &values));
  EXPECT_THAT(key, IsEmpty());
  EXPECT_THAT(values, IsEmpty());
}

TEST_F(SplitStringIntoKeyValuesTest, EmptyValueInputSingleValue) {
  EXPECT_FALSE(SplitStringIntoKeyValues("key_with_no_value ",
                                        "\t ",  // Key separators
                                        "",     // No value separators
                                        &key, &values));
  EXPECT_EQ(key, "key_with_no_value");
  EXPECT_THAT(values, IsEmpty());
}

TEST_F(SplitStringIntoKeyValuesTest, EmptyKeyInputSingleValue) {
  EXPECT_TRUE(SplitStringIntoKeyValues(" value for empty key",
                                       "\t ",  // Key separators
                                       "",     // No value separators
                                       &key, &values));
  EXPECT_THAT(key, IsEmpty());
  EXPECT_THAT(values, ElementsAre("value for empty key"));
}

TEST_F(SplitStringIntoKeyValuesTest, KeyWithSingleValue) {
  EXPECT_TRUE(SplitStringIntoKeyValues("key1 \t value1,   value2   value3",
                                       "\t ",  // Key separators
                                       "",     // No value separators
                                       &key, &values));
  EXPECT_EQ(key, "key1");
  EXPECT_THAT(values, ElementsAre("value1,   value2   value3"));
}

class SplitStringIntoKeyValuePairsTest : public testing::Test {
 protected:
  std::vector<std::pair<std::string, std::string>> kv_pairs;
};

TEST_F(SplitStringIntoKeyValuePairsTest, EmptyString) {
  EXPECT_TRUE(SplitStringIntoKeyValuePairs("",
                                           ":",   // Key-value delimiters
                                           ", ",  // Key-value pair delims
                                           &kv_pairs));
  EXPECT_THAT(kv_pairs, IsEmpty());
}

TEST_F(SplitStringIntoKeyValuePairsTest, EmptySecondValue) {
  EXPECT_FALSE(SplitStringIntoKeyValuePairs("key1:value1 , key2:",
                                            ":",   // Key-value delimiters
                                            ", ",  // Key-value pair delims
                                            &kv_pairs));
  EXPECT_THAT(kv_pairs, ElementsAre(Pair("key1", "value1"), Pair("key2", "")));
}

TEST_F(SplitStringIntoKeyValuePairsTest, DelimiterInValue) {
  EXPECT_TRUE(SplitStringIntoKeyValuePairs("key1:va:ue1 , key2:value2",
                                           ":",   // Key-value delimiters
                                           ", ",  // Key-value pair delims
                                           &kv_pairs));
  EXPECT_THAT(kv_pairs,
              ElementsAre(Pair("key1", "va:ue1"), Pair("key2", "value2")));
}

TEST(OldSplit, SplitStringToLines) {
  LOG(INFO) << "Testing SplitStringToLines";
  char to_be_split[42] = "a quick brown fox jumped over a lazy dog";
  std::string s_to_be_split(to_be_split);
  std::vector<std::string> result;
  SplitStringToLines(to_be_split, 41, -1, &result);
  ASSERT_EQ(result.size(), 1);
  ASSERT_EQ(result.at(0), "a quick brown fox jumped over a lazy dog");
  result.clear();
  SplitStringToLines(to_be_split, 1000, 0, &result);
  ASSERT_EQ(result.size(), 1);
  ASSERT_EQ(result.at(0), "a quick brown fox jumped over a lazy dog");
  result.clear();
  SplitStringToLines(to_be_split, 0, 100, &result);
  ASSERT_EQ(result.size(), 0);
  result.clear();
  SplitStringToLines(to_be_split, 1, 100, &result);
  ASSERT_EQ(result.size(), 40);
  for (int i = 0; i < 40; i++) {
    ASSERT_EQ(result.at(i), s_to_be_split.substr(i, 1));
  }
  result.clear();
  SplitStringToLines(to_be_split, 3, 100, &result);
  ASSERT_EQ(result.size(), 17);
  ASSERT_EQ(result.at(0), "a");
  ASSERT_EQ(result.at(1), " qu");
  ASSERT_EQ(result.at(2), "ick");
  ASSERT_EQ(result.at(3), " br");
  ASSERT_EQ(result.at(4), "own");
  ASSERT_EQ(result.at(5), " fo");
  ASSERT_EQ(result.at(6), "x");
  ASSERT_EQ(result.at(7), " ju");
  ASSERT_EQ(result.at(8), "mpe");
  ASSERT_EQ(result.at(9), "d");
  ASSERT_EQ(result.at(10), " ov");
  ASSERT_EQ(result.at(11), "er");
  ASSERT_EQ(result.at(12), " a");
  ASSERT_EQ(result.at(13), " la");
  ASSERT_EQ(result.at(14), "zy");
  ASSERT_EQ(result.at(15), " do");
  ASSERT_EQ(result.at(16), "g");
  result.clear();
  SplitStringToLines(to_be_split, 3, 1, &result);
  ASSERT_EQ(result.size(), 1);
  ASSERT_EQ(result.at(0), "a");
  result.clear();
  SplitStringToLines(to_be_split, 22, -1, &result);
  ASSERT_EQ(result.size(), 3);
  ASSERT_EQ(result.at(0), "a quick brown fox");
  ASSERT_EQ(result.at(1), " jumped over a lazy");
  ASSERT_EQ(result.at(2), " dog");
  result.clear();
  SplitStringToLines(to_be_split, 22, 1, &result);
  ASSERT_EQ(result.size(), 1);
  ASSERT_EQ(result.at(0), "a quick brown fox...");
  result.clear();
  SplitStringToLines(to_be_split, 22, 2, &result);
  ASSERT_EQ(result.size(), 2);
  ASSERT_EQ(result.at(0), "a quick brown fox");
  ASSERT_EQ(result.at(1), " jumped over a lazy...");
  result.clear();
  SplitStringToLines(to_be_split, 22, 3, &result);
  ASSERT_EQ(result.size(), 3);
  ASSERT_EQ(result.at(0), "a quick brown fox");
  ASSERT_EQ(result.at(1), " jumped over a lazy");
  ASSERT_EQ(result.at(2), " dog");
  result.clear();
  SplitStringToLines(to_be_split, 22, 4, &result);
  ASSERT_EQ(result.size(), 3);
  ASSERT_EQ(result.at(0), "a quick brown fox");
  ASSERT_EQ(result.at(1), " jumped over a lazy");
  ASSERT_EQ(result.at(2), " dog");
}

TEST(OldSplit, SplitLeadingIntValues) {
  LOG(INFO) << "Testing SplitLeadingIntValue Family";
  const char* t;

  // Empty:
  std::vector<int32_t> vec32;
  t = "";
  EXPECT_EQ(SplitLeadingDec32Values(t, &vec32), t);
  EXPECT_THAT(vec32, IsEmpty());
  t = " ";
  EXPECT_EQ(SplitLeadingDec32Values(t, &vec32), t);
  EXPECT_THAT(vec32, IsEmpty());
  t = " \t\r\n ";
  EXPECT_EQ(SplitLeadingDec32Values(t, &vec32), t);
  EXPECT_THAT(vec32, IsEmpty());

  // Single:
  t = "42";
  EXPECT_EQ(SplitLeadingDec32Values(t, &vec32) - t, strlen(t));
  EXPECT_THAT(vec32, ElementsAre(42));

  // Nonempty vector:
  t = "43 44";
  EXPECT_EQ(SplitLeadingDec32Values(t, &vec32) - t, strlen(t));
  EXPECT_THAT(vec32, ElementsAre(42, 43, 44));
  vec32.clear();

  // Simple:
  t = "0 1 2 3 4 16";
  EXPECT_EQ(SplitLeadingDec32Values(t, &vec32) - t, strlen(t));
  EXPECT_THAT(vec32, ElementsAre(0, 1, 2, 3, 4, 16));
  vec32.clear();

  // Whitepace:
  t = "  42313 \n  123\t432  ";
  EXPECT_EQ(SplitLeadingDec32Values(t, &vec32) - t, strlen(t) - 2);
  EXPECT_THAT(vec32, ElementsAre(42313, 123, 432));
  vec32.clear();

  // Bad:
  t = "0 1 x 3 4 5";
  EXPECT_EQ(SplitLeadingDec32Values(t, &vec32) - t, 3);
  EXPECT_THAT(vec32, ElementsAre(0, 1));
  vec32.clear();
  t = " 423u 1 2 3 4 5 ";
  EXPECT_EQ(SplitLeadingDec32Values(t, &vec32) - t, 4);
  EXPECT_THAT(vec32, ElementsAre(423));
  vec32.clear();

  // Number forms:
  t = "+4 -3 0010";
  EXPECT_EQ(SplitLeadingDec32Values(t, &vec32) - t, strlen(t));
  EXPECT_THAT(vec32, ElementsAre(4, -3, 10));
  vec32.clear();

  t = "0x20";
  EXPECT_EQ(SplitLeadingDec32Values(t, &vec32) - t, 1);
  EXPECT_THAT(vec32, ElementsAre(0));
  vec32.clear();

  t = "4294967296";
  EXPECT_EQ(SplitLeadingDec32Values(t, &vec32) - t, strlen(t));
  EXPECT_THAT(vec32, ElementsAre(std::numeric_limits<int32_t>::max()));
  vec32.clear();

  t = "3-2";
  EXPECT_EQ(SplitLeadingDec32Values(t, &vec32) - t, 1);
  EXPECT_THAT(vec32, ElementsAre(3));
  vec32.clear();

  t = "-9223372036854775808";
  EXPECT_EQ(SplitLeadingDec32Values(t, &vec32) - t, strlen(t));
  EXPECT_THAT(vec32, ElementsAre(INT_MIN));
  vec32.clear();

  t = "-100000000000000000000000000";
  EXPECT_EQ(SplitLeadingDec32Values(t, &vec32) - t, strlen(t));
  EXPECT_THAT(vec32, ElementsAre(INT_MIN));
  vec32.clear();

  // Now 64 bits.

  // Empty:
  std::vector<int64_t> vec64;
  t = "";
  EXPECT_EQ(SplitLeadingDec64Values(t, &vec64), t);
  EXPECT_THAT(vec64, IsEmpty());
  t = " ";
  EXPECT_EQ(SplitLeadingDec64Values(t, &vec64), t);
  EXPECT_THAT(vec64, IsEmpty());
  t = " \t\r\n ";
  EXPECT_EQ(SplitLeadingDec64Values(t, &vec64), t);
  EXPECT_THAT(vec64, IsEmpty());

  // Single:
  t = "42";
  EXPECT_EQ(SplitLeadingDec64Values(t, &vec64) - t, strlen(t));
  EXPECT_THAT(vec64, ElementsAre(42));

  // Nonempty vector:
  t = "43 44";
  EXPECT_EQ(SplitLeadingDec64Values(t, &vec64) - t, strlen(t));
  EXPECT_THAT(vec64, ElementsAre(42, 43, 44));
  vec64.clear();

  // Simple:
  t = "0 1 2 3 4 16";
  EXPECT_EQ(SplitLeadingDec64Values(t, &vec64) - t, strlen(t));
  EXPECT_THAT(vec64, ElementsAre(0, 1, 2, 3, 4, 16));
  vec64.clear();

  // Whitepace:
  t = "  42313 \n  123\t464  ";
  EXPECT_EQ(SplitLeadingDec64Values(t, &vec64) - t, strlen(t) - 2);
  EXPECT_THAT(vec64, ElementsAre(42313, 123, 464));
  vec64.clear();

  // Bad:
  t = "0 1 x 3 4 5";
  EXPECT_EQ(SplitLeadingDec64Values(t, &vec64) - t, 3);
  EXPECT_THAT(vec64, ElementsAre(0, 1));
  vec64.clear();
  t = " 423u 1 2 3 4 5 ";
  EXPECT_EQ(SplitLeadingDec64Values(t, &vec64) - t, 4);
  EXPECT_THAT(vec64, ElementsAre(423));
  vec64.clear();

  // Number forms:
  t = "+4 -3 0010";
  EXPECT_EQ(SplitLeadingDec64Values(t, &vec64) - t, strlen(t));
  EXPECT_THAT(vec64, ElementsAre(4, -3, 10));
  vec64.clear();

  t = "0x20";
  EXPECT_EQ(SplitLeadingDec64Values(t, &vec64) - t, 1);
  EXPECT_THAT(vec64, ElementsAre(0));
  vec64.clear();

  t = "4294967296";
  EXPECT_EQ(SplitLeadingDec64Values(t, &vec64) - t, strlen(t));
  EXPECT_THAT(vec64, ElementsAre(int64_t{4294967296}));
  vec64.clear();

  t = "3-2";
  EXPECT_EQ(SplitLeadingDec64Values(t, &vec64) - t, 1);
  EXPECT_THAT(vec64, ElementsAre(3));
  vec64.clear();

  t = "-9223372036854775808";
  EXPECT_EQ(SplitLeadingDec64Values(t, &vec64) - t, strlen(t));
  EXPECT_THAT(vec64, ElementsAre(-0x7fffffffffffffffLL - 1));
  vec64.clear();

  t = "-100000000000000000000000000";
  EXPECT_EQ(SplitLeadingDec64Values(t, &vec64) - t, strlen(t));
  EXPECT_THAT(vec64, ElementsAre(-0x7fffffffffffffffLL - 1));
  vec64.clear();
}

TEST(OldSplit, SplitRange) {
  int from, to;

  // A value of 42 signifies that the value wasn't modified.
  from = to = 42;
  EXPECT_TRUE(SplitRange("", &from, &to));
  EXPECT_EQ(from, 42);
  EXPECT_EQ(to, 42);

  from = to = 42;
  EXPECT_TRUE(SplitRange(" ", &from, &to));
  EXPECT_EQ(from, 42);
  EXPECT_EQ(to, 42);

  from = to = 42;
  EXPECT_TRUE(SplitRange("-", &from, &to));
  EXPECT_EQ(from, 42);
  EXPECT_EQ(to, 42);

  from = to = 42;
  EXPECT_TRUE(SplitRange(" - ", &from, &to));
  EXPECT_EQ(from, 42);
  EXPECT_EQ(to, 42);

  from = to = 42;
  EXPECT_TRUE(SplitRange("7", &from, &to));
  EXPECT_EQ(from, 7);
  EXPECT_EQ(to, 42);

  from = to = 42;
  EXPECT_TRUE(SplitRange("7-", &from, &to));
  EXPECT_EQ(from, 7);
  EXPECT_EQ(to, 42);

  from = to = 42;
  EXPECT_TRUE(SplitRange("-7", &from, &to));
  EXPECT_EQ(from, 42);
  EXPECT_EQ(to, 7);

  from = to = 42;
  EXPECT_TRUE(SplitRange("7-17", &from, &to));
  EXPECT_EQ(from, 7);
  EXPECT_EQ(to, 17);

  from = to = 42;
  EXPECT_TRUE(SplitRange("7-17 ", &from, &to));
  EXPECT_EQ(from, 7);
  EXPECT_EQ(to, 17);

  from = to = 42;
  EXPECT_TRUE(SplitRange("009-0017", &from, &to));
  EXPECT_EQ(from, 9);
  EXPECT_EQ(to, 17);

  from = to = 42;
  EXPECT_TRUE(SplitRange("009- 0017", &from, &to));
  EXPECT_EQ(from, 9);
  EXPECT_EQ(to, 42);

  from = to = 42;
  EXPECT_TRUE(SplitRange("7-17 ignore after whitespace", &from, &to));
  EXPECT_EQ(from, 7);
  EXPECT_EQ(to, 17);

  //
  // Error cases
  //
  from = to = 42;

  EXPECT_FALSE(SplitRange("1-2-", &from, &to));
  EXPECT_EQ(from, 42);
  EXPECT_EQ(to, 42);

  EXPECT_FALSE(SplitRange("-2-", &from, &to));
  EXPECT_EQ(from, 42);
  EXPECT_EQ(to, 42);

  EXPECT_FALSE(SplitRange("1-2-3", &from, &to));
  EXPECT_EQ(from, 42);
  EXPECT_EQ(to, 42);
}

TEST(SplitStringWithEscaping, EmptyString) {
  std::vector<std::string> result;
  SplitStringWithEscaping("", '=', &result);
  EXPECT_THAT(result, IsEmpty());
  SplitStringWithEscaping(absl::string_view(nullptr, 0), '=', &result);
  EXPECT_THAT(result, IsEmpty());
}

TEST(SplitStringWithEscaping, SimpleCase) {
  std::vector<std::string> result;
  SplitStringWithEscaping("cat,dog,bird", ',', &result);
  EXPECT_THAT(result, ElementsAre("cat", "dog", "bird"));
}

TEST(SplitStringWithEscaping, DocumentedCase) {
  //   Eg. 'foo\=bar=baz\\qu\ux' split on '=' becomes ('foo=bar', 'baz\qu\ux')
  std::vector<std::string> result;
  SplitStringWithEscaping("foo\\=bar=baz\\\\qu\\ux", '=', &result);
  EXPECT_THAT(result, ElementsAre("foo=bar", "baz\\qu\\ux"));
}

TEST(SplitStringWithEscaping, EmptyPartsRemoved) {
  std::vector<std::string> result;
  SplitStringWithEscaping(",foo,,,bar,,", ',', &result);
  EXPECT_THAT(result, ElementsAre("foo", "bar"));
}

TEST(SplitStringWithEscaping, EscapedDelimiter) {
  std::vector<std::string> result;
  SplitStringWithEscaping("\\,one\\,two,\\,,\\,three\\,", ',', &result);
  EXPECT_THAT(result, ElementsAre(",one,two", ",", ",three,"));
}

TEST(SplitStringWithEscaping, EscapedBackslash) {
  std::vector<std::string> result;
  SplitStringWithEscaping("\\\\splish\\\\,splash", ',', &result);
  EXPECT_THAT(result, ElementsAre("\\splish\\", "splash"));
}

TEST(SplitStringWithEscaping, TrailingEscapedBackslash) {
  std::vector<std::string> result;
  SplitStringWithEscaping("taking,bath\\\\", ',', &result);
  EXPECT_THAT(result, ElementsAre("taking", "bath\\"));
}

TEST(SplitStringWithEscaping, UnrecognisedEscapeSequenceKeepsBackslash) {
  std::vector<std::string> result;
  SplitStringWithEscaping("\\a,\\b", ',', &result);
  EXPECT_THAT(result, ElementsAre("\\a", "\\b"));
}

TEST(SplitStringWithEscaping, TrailingUnescapedBackslashPreserved) {
  std::vector<std::string> result;
  SplitStringWithEscaping("i,can,haz\\", ',', &result);
  EXPECT_THAT(result, ElementsAre("i", "can", "haz\\"));
}

TEST(SplitStringWithEscaping, MultipleDelimitersCharmap) {
  std::vector<std::string> result;
  SplitStringWithEscaping("a,b.c:d\\,\\.\\:;e", absl::CharSet(".,;:"), &result);
  EXPECT_THAT(result, ElementsAre("a", "b", "c", "d,.:", "e"));
}

TEST(SplitStringWithEscapingAllowEmpty, EmptyString) {
  std::vector<std::string> result;
  SplitStringWithEscapingAllowEmpty("", '=', &result);
  EXPECT_THAT(result, ElementsAre(""));
}

TEST(SplitStringWithEscapingAllowEmpty, SimpleCase) {
  std::vector<std::string> result;
  SplitStringWithEscapingAllowEmpty("cat,dog,bird", ',', &result);
  EXPECT_THAT(result, ElementsAre("cat", "dog", "bird"));
}

TEST(SplitStringWithEscapingAllowEmpty, EmptyPartsRemoved) {
  std::vector<std::string> result;
  SplitStringWithEscapingAllowEmpty(",foo,,,bar,,", ',', &result);
  EXPECT_THAT(result, ElementsAre("", "foo", "", "", "bar", "", ""));
}

TEST(SplitStringWithEscapingToSet, SimpleCase) {
  std::set<std::string> result;
  SplitStringWithEscapingToSet("cat\\,dog,bird", ',', &result);
  EXPECT_THAT(result, ElementsAre("bird", "cat,dog"));
}

TEST(SplitStringAndParseTest, Death) {
  std::string s = "1.0,2.0,3.0";
  std::vector<double> v;
  EXPECT_DEATH_IF_SUPPORTED(
      SplitStringAndParse<double>(s, ",", &safe_strtod, nullptr),
      "Output container must not be null.");
  EXPECT_DEATH_IF_SUPPORTED(
      SplitStringAndParse<double>(
          s, ",", (bool (*)(absl::string_view, double*)) nullptr, &v),
      "Parsing function must not be null.");
  EXPECT_DEATH_IF_SUPPORTED(
      SplitStringAndParse<double>(s, absl::string_view(), &safe_strtod, &v),
      "Delimiters must not be null.");
  EXPECT_DEATH_IF_SUPPORTED(
      SplitStringAndParse<double>(s, "", &safe_strtod, &v),
      "Delimiters must have non-zero length.");
}

TEST(SplitStringAndParseTest, Double) {
  std::vector<double> values;

  values.clear();
  EXPECT_TRUE(SplitStringAndParse("1.0,2.0,3.0", ",", &safe_strtod, &values));
  EXPECT_THAT(values,
              ElementsAre(DoubleNear(1.0, 0.001), DoubleNear(2.0, 0.001),
                          DoubleNear(3.0, 0.001)));

  // Test empty values and appending
  EXPECT_TRUE(
      SplitStringAndParse(",,,5.5,,,8.21,13.7,,,", ",", &safe_strtod, &values));
  EXPECT_THAT(values,
              ElementsAre(DoubleNear(1.0, 0.001), DoubleNear(2.0, 0.001),
                          DoubleNear(3.0, 0.001), DoubleNear(5.50, 0.001),
                          DoubleNear(8.21, 0.001), DoubleNear(13.70, 0.001)));

  // Test parsing failed and no change to output
  EXPECT_FALSE(
      SplitStringAndParse(",,1.0,,dsf,,asdf", ",", &safe_strtod, &values));
  EXPECT_FALSE(SplitStringAndParse(",,,,asdf", ",", &safe_strtod, &values));
  EXPECT_FALSE(SplitStringAndParse(",,,,1.23kjd", ",", &safe_strtod, &values));
  EXPECT_FALSE(SplitStringAndParse(",,,,1.23M", ",", &safe_strtod, &values));

  // Test parsing of empty string
  values.clear();
  EXPECT_TRUE(SplitStringAndParse(",,,,", ",", &safe_strtod, &values));
  EXPECT_THAT(values, IsEmpty());
}

namespace {

// This function is compatible with SplitStringAndParse(). Return true for
// "true" and false for "false". No other use cases are supported since this
// function is only for testing.
bool ParseBool(absl::string_view str, bool* value) {
  if (str == "true") {
    *value = true;
    return true;
  } else if (str == "false") {
    *value = false;
    return true;
  }
  return false;
}

}  // namespace

TEST(SplitStringAndParseTest, Boolean) {
  std::vector<bool> values;

  values.clear();
  EXPECT_TRUE(
      SplitStringAndParse(",,true,false,true", ",", &ParseBool, &values));
  EXPECT_THAT(values, ElementsAre(true, false, true));

  // Test empty values and appending
  EXPECT_TRUE(
      SplitStringAndParse("true,,,true,true,,,", ",", &ParseBool, &values));
  EXPECT_THAT(values, ElementsAre(true, false, true, true, true, true));

  // Test parsing failed
  values.clear();
  EXPECT_FALSE(
      SplitStringAndParse(",,true,,dsf,,asdf", ",", &ParseBool, &values));
  EXPECT_FALSE(SplitStringAndParse(",,,,asdf", ",", &ParseBool, &values));

  // Test parsing of empty string
  values.clear();
  EXPECT_TRUE(SplitStringAndParse(",,,,", ",", &ParseBool, &values));
  EXPECT_THAT(values, IsEmpty());
}

TEST(SplitStringAndParseTest, EmbeddedNulls) {
  // Compiler gets confused if I don't put the NULs in a separate string.
  // Also, there are two extraneous zeros on the end to ensure that this string
  // gets parsed correctly according to its length and not according to the
  // terminating nul.  (It should be parsed as -5 instead of -500)
  std::string hasnulls(
      "33"
      "\0"
      "42"
      "\0"
      "64"
      "\0"
      "-500",
      11);
  std::string delim("\0", 1);
  ASSERT_EQ(hasnulls.length(), 11);  // sanity check
  std::vector<int32_t> values;
  EXPECT_TRUE(SplitStringAndParse(hasnulls, delim, &safe_strto32, &values));
  EXPECT_THAT(values, ElementsAre(33, 42, 64, -5));
}

TEST(SplitStringAndParseToContainerDeathTest, InvalidInputs) {
  std::string s = "1.0,2.0,3.0";
  std::set<double> v;
  EXPECT_DEATH_IF_SUPPORTED(SplitStringAndParseToContainer<std::set<double>>(
                                s, ",", &safe_strtod, nullptr),
                            "Output container must not be null.");
  EXPECT_DEATH_IF_SUPPORTED(
      SplitStringAndParseToContainer<std::set<double>>(
          s, ",", (bool (*)(absl::string_view, double*)) nullptr, &v),
      "Parsing function must not be null.");
  EXPECT_DEATH_IF_SUPPORTED(SplitStringAndParseToContainer<std::set<double>>(
                                s, absl::string_view(), &safe_strtod, &v),
                            "Delimiters must not be null.");
  EXPECT_DEATH_IF_SUPPORTED(
      SplitStringAndParseToContainer<std::set<double>>(s, "", &safe_strtod, &v),
      "Delimiters must have non-zero length.");
}

TEST(SplitStringAndParseToContainerTest, UInt32) {
  std::set<uint32_t> values;

  values.clear();
  EXPECT_TRUE(
      SplitStringAndParseToContainer("1,2,3", ",", &safe_strtou32, &values));
  EXPECT_THAT(values, ElementsAre(1, 2, 3));

  // Test empty values and duplicates and insertion into non-empty set
  EXPECT_TRUE(SplitStringAndParseToContainer(",,,5,5,,,8,21,,,,", ",",
                                             &safe_strtou32, &values));
  EXPECT_THAT(values, ElementsAre(1, 2, 3, 5, 8, 21));

  // Test parsing failed and no change to output
  EXPECT_FALSE(SplitStringAndParseToContainer(",,1.0,,dsf,,asdf", ",",
                                              &safe_strtou32, &values));
  EXPECT_FALSE(
      SplitStringAndParseToContainer(",,,,asdf", ",", &safe_strtou32, &values));
  EXPECT_FALSE(SplitStringAndParseToContainer(",,,,1.23kjd", ",",
                                              &safe_strtou32, &values));
  EXPECT_FALSE(SplitStringAndParseToContainer(",,,,1.23M", ",", &safe_strtou32,
                                              &values));

  // Test parsing of empty string
  values.clear();
  EXPECT_TRUE(
      SplitStringAndParseToContainer(",,,,", ",", &safe_strtou32, &values));
  EXPECT_THAT(values, IsEmpty());
}

TEST(SplitStringAndParseToContainerTest, Parse_UInt32_UnorderedSet) {
  std::unordered_set<uint32_t> values;

  values.clear();
  EXPECT_TRUE(
      SplitStringAndParseToContainer("1,2,3", ",", &safe_strtou32, &values));
  EXPECT_GE(values.bucket_size(0), 0);  // Prevent cleanups to switch to Abseil.
  EXPECT_THAT(values, UnorderedElementsAre(1, 2, 3));

  // Test empty values and duplicates and insertion into non-empty set
  EXPECT_TRUE(SplitStringAndParseToContainer(",,,5,5,,,8,21,,,,", ",",
                                             &safe_strtou32, &values));
  EXPECT_THAT(values, UnorderedElementsAre(1, 2, 3, 5, 8, 21));

  // Test parsing failed and no change to output
  EXPECT_FALSE(SplitStringAndParseToContainer(",,1.0,,dsf,,asdf", ",",
                                              &safe_strtou32, &values));
  EXPECT_FALSE(
      SplitStringAndParseToContainer(",,,,asdf", ",", &safe_strtou32, &values));
  EXPECT_FALSE(SplitStringAndParseToContainer(",,,,1.23kjd", ",",
                                              &safe_strtou32, &values));
  EXPECT_FALSE(SplitStringAndParseToContainer(",,,,1.23M", ",", &safe_strtou32,
                                              &values));

  // Test parsing of empty string
  values.clear();
  EXPECT_TRUE(
      SplitStringAndParseToContainer(",,,,", ",", &safe_strtou32, &values));
  EXPECT_THAT(values, IsEmpty());
}

#ifndef _MSC_VER
TEST(SplitStringAndParseToContainerTest, Parse_UInt32_HashSet) {
  __gnu_cxx::hash_set<uint32_t> values;

  values.clear();
  EXPECT_TRUE(
      SplitStringAndParseToContainer("1,2,3", ",", &safe_strtou32, &values));
  EXPECT_GE(values.elems_in_bucket(0), 0);  // Prevent switch to Abseil.
  EXPECT_THAT(values, UnorderedElementsAre(1, 2, 3));

  // Test empty values and duplicates and insertion into non-empty set
  EXPECT_TRUE(SplitStringAndParseToContainer(",,,5,5,,,8,21,,,,", ",",
                                             &safe_strtou32, &values));
  EXPECT_THAT(values, UnorderedElementsAre(1, 2, 3, 5, 8, 21));

  // Test parsing failed and no change to output
  EXPECT_FALSE(SplitStringAndParseToContainer(",,1.0,,dsf,,asdf", ",",
                                              &safe_strtou32, &values));
  EXPECT_FALSE(
      SplitStringAndParseToContainer(",,,,asdf", ",", &safe_strtou32, &values));
  EXPECT_FALSE(SplitStringAndParseToContainer(",,,,1.23kjd", ",",
                                              &safe_strtou32, &values));
  EXPECT_FALSE(SplitStringAndParseToContainer(",,,,1.23M", ",", &safe_strtou32,
                                              &values));

  // Test parsing of empty string
  values.clear();
  EXPECT_TRUE(
      SplitStringAndParseToContainer(",,,,", ",", &safe_strtou32, &values));
  EXPECT_THAT(values, IsEmpty());
}
#endif

TEST(SplitStringAndParseToContainerTest, Parse_UInt32_AbseilHashSet) {
  absl::flat_hash_set<uint32_t> values;

  values.clear();
  EXPECT_TRUE(
      SplitStringAndParseToContainer("1,2,3", ",", &safe_strtou32, &values));
  EXPECT_THAT(values, UnorderedElementsAre(1, 2, 3));

  // Test empty values and duplicates and insertion into non-empty set
  EXPECT_TRUE(SplitStringAndParseToContainer(",,,5,5,,,8,21,,,,", ",",
                                             &safe_strtou32, &values));
  EXPECT_THAT(values, UnorderedElementsAre(1, 2, 3, 5, 8, 21));

  // Test parsing failed and no change to output
  EXPECT_FALSE(SplitStringAndParseToContainer(",,1.0,,dsf,,asdf", ",",
                                              &safe_strtou32, &values));
  EXPECT_FALSE(
      SplitStringAndParseToContainer(",,,,asdf", ",", &safe_strtou32, &values));
  EXPECT_FALSE(SplitStringAndParseToContainer(",,,,1.23kjd", ",",
                                              &safe_strtou32, &values));
  EXPECT_FALSE(SplitStringAndParseToContainer(",,,,1.23M", ",", &safe_strtou32,
                                              &values));

  // Test parsing of empty string
  values.clear();
  EXPECT_TRUE(
      SplitStringAndParseToContainer(",,,,", ",", &safe_strtou32, &values));
  EXPECT_THAT(values, IsEmpty());
}

TEST(SplitStringAndParseToContainerTest, EmbeddedNulls) {
  // Compiler gets confused if I don't put the NULs in a separate string.
  // Also, there are two extraneous zeros on the end to ensure that this string
  // gets parsed correctly according to its length and not according to the
  // terminating NUL.  (It should be parsed as -5 instead of -500)
  std::string hasnulls(
      "33"
      "\0"
      "42"
      "\0"
      "64"
      "\0"
      "-500",
      11);
  std::string delim("\0", 1);
  ASSERT_EQ(hasnulls.length(), 11);  // sanity check
  std::set<int32_t> values;
  EXPECT_TRUE(
      SplitStringAndParseToContainer(hasnulls, delim, &safe_strto32, &values));
  EXPECT_THAT(values, ElementsAre(-5, 33, 42, 64));
}

// Test that SplitStringAndParseToContainer works with SimpleAtoi.
TEST(SplitStringAndParseToContainerTest, SimpleAtoi) {
  std::set<int> values;
  ASSERT_TRUE(SplitStringAndParseToContainer("1,2,3", ",",
                                             &absl::SimpleAtoi<int>, &values));
  EXPECT_THAT(values, ElementsAre(1, 2, 3));
}

// Test that SplitStringAndParseToContainer fails with SimpleAtoi given an
// unparseable value.
TEST(SplitStringAndParseToContainerTest, SimpleAtoiFailure) {
  std::set<int32_t> values;
  EXPECT_FALSE(SplitStringAndParseToContainer(
      "a,102", ",", &absl::SimpleAtoi<int32_t>, &values));
  // Parseable values will still go in the set.
  EXPECT_THAT(values, ElementsAre(102));
}

// Test that SplitStringAndParseToList works with SimpleAtoi.
TEST(SplitStringAndParseToListTest, SimpleAtoi) {
  std::deque<int> values;
  ASSERT_TRUE(
      SplitStringAndParseToList("1,2,3", ",", &absl::SimpleAtoi<int>, &values));
  EXPECT_THAT(values, ElementsAre(1, 2, 3));
}

TEST(SplitStringAndParseToListTest, SimpleAtoiFailure) {
  std::deque<int32_t> values;
  EXPECT_FALSE(SplitStringAndParseToList("a,102", ",",
                                         &absl::SimpleAtoi<int32_t>, &values));
  // Parseable values will still go in the list.
  EXPECT_THAT(values, ElementsAre(102));
}

namespace {

// Overloaded parse functions which scale the returned values
// differently based on type so that it is easy to test that correct
// one is selected for SplitStringAndParseToContainer.
bool OverloadedParse(const std::string& str, int32_t* value) {
  if (safe_strto32(str, value)) {
    *value *= 10;
    return true;
  }
  return false;
}

bool OverloadedParse(const std::string& str, int64_t* value) {
  if (safe_strto64(str, value)) {
    *value *= 100;
    return true;
  }
  return false;
}

}  // namespace

TEST(SplitStringAndParseToContainerTest, OverloadedParse) {
  std::set<int32_t> values32;
  ASSERT_TRUE(SplitStringAndParseToContainer(
      "1,2,3", ",", (bool (*)(const std::string&, int32_t*))&OverloadedParse,
      &values32));
  EXPECT_THAT(values32, ElementsAre(10, 20, 30));

  std::set<int64_t> values64;
  ASSERT_TRUE(SplitStringAndParseToContainer(
      "1,2,3", ",", (bool (*)(const std::string&, int64_t*))&OverloadedParse,
      &values64));
  EXPECT_THAT(values64, ElementsAre(100, 200, 300));
}

// Tests usage examples given in split.h for SplitStringAndParseToContainer().
TEST(SplitStringAndParseToContainerTest, UsageExamples) {
  {
    std::vector<double> values;
    EXPECT_TRUE(SplitStringAndParse("1.0,2.0,3.0", ",", &safe_strtod, &values));
    EXPECT_THAT(values, SizeIs(3));
  }
  {
    std::set<int64_t> values;
    EXPECT_TRUE(
        SplitStringAndParseToContainer("3,1,1,2", ",", &safe_strto64, &values));
    EXPECT_THAT(values, SizeIs(3));
  }
  {
    std::deque<int64_t> values;
    EXPECT_TRUE(
        SplitStringAndParseToList("3,1,1,2", ",", &safe_strto64, &values));
    EXPECT_THAT(values, SizeIs(4));
  }
}

TEST(Util, ClipString) {
  LOG(INFO) << "Testing TestClipString (w/ string)";

  // Clip on a word boundary and make sure the string is as long as possible
  std::string s_to_be_clipped("a quick brown fox jumped over a lazy dog");
  ClipString(&s_to_be_clipped, 28);  // after fox
  EXPECT_EQ(s_to_be_clipped, "a quick brown fox jumped...");

  // A clip boundary equal to the max overcut length
  ClipString(&s_to_be_clipped, 12);
  EXPECT_EQ(s_to_be_clipped, "a quick...");

  // Clip on word boundary even when clipping to short lengths
  ClipString(&s_to_be_clipped, 3);
  EXPECT_EQ(s_to_be_clipped, "a");

  // When there are no boundaries, just clip to the clip length.  The test
  // string has length equal to the max overcut length.
  std::string s_no_spaces("0123456789ab");
  ClipString(&s_no_spaces, 7);
  EXPECT_EQ(s_no_spaces, "0123...");

  // Do not clip strings of length <= the clip length
  std::string s_not_to_be_clipped("a quick");
  ClipString(&s_not_to_be_clipped, 18);
  EXPECT_EQ(s_not_to_be_clipped, "a quick");
  ClipString(&s_not_to_be_clipped, 7);
  EXPECT_EQ(s_not_to_be_clipped, "a quick");

  // Test clip lengths that are around the length of "..."
  std::string s_short_string("a quick");
  ClipString(&s_short_string, 4);
  EXPECT_EQ(s_short_string, "a...");
  ClipString(&s_short_string, 3);
  EXPECT_EQ(s_short_string, "a..");
  ClipString(&s_short_string, 2);
  EXPECT_EQ(s_short_string, "a.");
  ClipString(&s_short_string, 1);
  EXPECT_EQ(s_short_string, "a");
}

}  // namespace strings
