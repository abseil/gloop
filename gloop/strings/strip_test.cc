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

#include "gloop/strings/strip.h"

#include <cstdlib>
#include <cstring>
#include <string>

#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace strings {
namespace {

using testing::IsEmpty;

TEST(Strip, StripDupCharacters) {
  // <input, golden output> pairs
  const char* input_goldens[] = {
      "",
      "",
      "/cfs/abc",
      "/cfs/abc",
      "/bigfile//a/b///c",
      "/bigfile/a/b/c",
      "/bigfile//a/b///c///",
      "/bigfile/a/b/c/",
      "///////bigfile////a/b///c",
      "/bigfile/a/b/c",
  };
  const int num_entries = sizeof(input_goldens) / sizeof(const char*);

  // now loop thru to make sure for all even number i
  //   StripDupCharacters(input_goldens[i]) == input_goldens[i+1]
  for (int i = 0; i < num_entries; i += 2) {
    std::string stripped_src(input_goldens[i]);
    StripDupCharacters(&stripped_src, '/', 0);
    EXPECT_EQ(stripped_src, std::string(input_goldens[i + 1]));
  }
}

TEST(StripTrailingNewline, Strip) {
  std::string strip_me("useless\tstring\r\n ");
  std::string copy(strip_me);

  // Make sure the input is left alone when the newlines are not
  // trailing.
  EXPECT_FALSE(StripTrailingNewline(&strip_me));
  EXPECT_EQ(strip_me, copy);

  // Make sure it properly handles a single \n
  strip_me += '\n';
  EXPECT_TRUE(StripTrailingNewline(&strip_me));
  EXPECT_EQ(strip_me, copy);

  // Make sure it properly handles a CR+LF (\r\n)
  strip_me += "\r\n";
  EXPECT_TRUE(StripTrailingNewline(&strip_me));
  EXPECT_EQ(strip_me, copy);

  // Make sure it doesn't do anything for LF+CR
  strip_me = copy + "\n\r";
  EXPECT_FALSE(StripTrailingNewline(&strip_me));
  EXPECT_NE(strip_me, copy);

  // Make sure it doesn't strip things like trailing tabs that preceed
  // the newline like StripTrailingWhitespace would do.
  strip_me = copy + "\t\r\n";
  copy += '\t';
  EXPECT_TRUE(StripTrailingNewline(&strip_me));
  EXPECT_EQ(strip_me, copy);
}

TEST(Strip, StripCurlyBraces) {
  std::string test1 = "{}foo{dgfkk:',)}";
  StripCurlyBraces(&test1);
  EXPECT_EQ(test1, "foo");
  std::string test2 = "a}foo{{b}{";
  StripCurlyBraces(&test2);
  EXPECT_EQ(test2, "a}foo{");
}

TEST(Strip, StripBrackets) {
  std::string test1 = "[]foo[dgfkk:',)]";
  StripBrackets('[', ']', &test1);
  EXPECT_EQ(test1, "foo");
  std::string test2 = "a)foo((b)(";
  StripBrackets('(', ')', &test2);
  EXPECT_EQ(test2, "a)foo(");
}

TEST(Strip, StripMarkupTags) {
  std::string test1 = "the quick <b>brown</b> fox";
  StripMarkupTags(&test1);
  EXPECT_EQ(test1, "the quick brown fox");
  EXPECT_EQ(OutputWithMarkupTagsStripped("the quick <b>brown</b> fox"),
            "the quick brown fox");
  std::string test2 = "sneak attack <script";
  StripMarkupTags(&test2);
  EXPECT_EQ(test2, "sneak attack ");
  EXPECT_EQ(OutputWithMarkupTagsStripped("sneak attack <script"),
            "sneak attack ");
  EXPECT_EQ(OutputWithMarkupTagsStripped(
                "consecutive <td></td><td></td><td>m</td>arkup"),
            "consecutive markup");
  EXPECT_EQ(OutputWithMarkupTagsStripped("no markup"), "no markup");
  EXPECT_EQ(OutputWithMarkupTagsStripped("<leading"), "");
  EXPECT_EQ(OutputWithMarkupTagsStripped("trailing<"), "trailing");
  // Huge string full of 2^15 tags to test worst-case behavior
  std::string huge = "<>";
  for (int i = 0; i < 15; i++) huge = huge + huge;
  EXPECT_EQ(OutputWithMarkupTagsStripped(huge), "");
}

TEST(Strip, TrimString) {
  const char* white = " \n\t";
  std::string expected;
  std::string s;
  absl::string_view sp;
  // TrimStringLeft
  sp = "";  // empty
  s = std::string(sp);
  expected = "";
  EXPECT_EQ(TrimStringLeft(&s, white), 0);
  EXPECT_EQ(TrimStringLeft(&sp, white), 0);
  EXPECT_EQ(expected, s);
  EXPECT_EQ(expected, sp);

  sp = " \n\t";  // all bad
  s = std::string(sp);
  expected = "";
  EXPECT_EQ(TrimStringLeft(&s, white), 3);
  EXPECT_EQ(TrimStringLeft(&sp, white), 3);
  EXPECT_EQ(expected, s);
  EXPECT_EQ(expected, sp);

  sp = "dog";  // nothing bad
  s = std::string(sp);
  expected = "dog";
  EXPECT_EQ(TrimStringLeft(&s, white), 0);
  EXPECT_EQ(TrimStringLeft(&sp, white), 0);
  EXPECT_EQ(expected, s);
  EXPECT_EQ(expected, sp);

  sp = " dog ";  // some bad
  s = std::string(sp);
  expected = "dog ";
  EXPECT_EQ(TrimStringLeft(&s, white), 1);
  EXPECT_EQ(TrimStringLeft(&sp, white), 1);
  EXPECT_EQ(expected, s);
  EXPECT_EQ(expected, sp);

  sp = " \n\t\t I love my little dog \n\t ";
  s = std::string(sp);
  expected = "I love my little dog \n\t ";
  EXPECT_EQ(TrimStringLeft(&s, white), 5);
  EXPECT_EQ(TrimStringLeft(&sp, white), 5);
  EXPECT_EQ(expected, s);
  EXPECT_EQ(expected, sp);

  // TrimStringRight
  sp = "";
  s = std::string(sp);
  expected = "";
  EXPECT_EQ(TrimStringRight(&s, white), 0);
  EXPECT_EQ(TrimStringRight(&sp, white), 0);
  EXPECT_EQ(expected, s);
  EXPECT_EQ(expected, sp);

  sp = " \n\t";
  s = std::string(sp);
  expected = "";
  EXPECT_EQ(TrimStringRight(&s, white), 3);
  EXPECT_EQ(TrimStringRight(&sp, white), 3);
  EXPECT_EQ(expected, s);
  EXPECT_EQ(expected, sp);

  sp = "dog";
  s = std::string(sp);
  expected = "dog";
  EXPECT_EQ(TrimStringRight(&s, white), 0);
  EXPECT_EQ(TrimStringRight(&sp, white), 0);
  EXPECT_EQ(expected, s);
  EXPECT_EQ(expected, sp);

  sp = " dog ";
  s = std::string(sp);
  expected = " dog";
  EXPECT_EQ(TrimStringRight(&s, white), 1);
  EXPECT_EQ(TrimStringRight(&sp, white), 1);
  EXPECT_EQ(expected, s);
  EXPECT_EQ(expected, sp);

  sp = " \n\t\t I love my little dog \n\t ";
  s = std::string(sp);
  expected = " \n\t\t I love my little dog";
  EXPECT_EQ(TrimStringRight(&s, white), 4);
  EXPECT_EQ(TrimStringRight(&sp, white), 4);
  EXPECT_EQ(expected, s);
  EXPECT_EQ(expected, sp);

  // TrimString
  sp = "";
  s = std::string(sp);
  expected = "";
  EXPECT_EQ(TrimString(&s, white), 0);
  EXPECT_EQ(TrimString(&sp, white), 0);
  EXPECT_EQ(expected, s);
  EXPECT_EQ(expected, sp);

  sp = " \n\t";
  s = std::string(sp);
  expected = "";
  EXPECT_EQ(TrimString(&s, white), 3);
  EXPECT_EQ(TrimString(&sp, white), 3);
  EXPECT_EQ(expected, s);
  EXPECT_EQ(expected, sp);

  sp = "dog";
  s = std::string(sp);
  expected = "dog";
  EXPECT_EQ(TrimString(&s, white), 0);
  EXPECT_EQ(TrimString(&sp, white), 0);
  EXPECT_EQ(expected, s);
  EXPECT_EQ(expected, sp);

  sp = " dog ";
  s = std::string(sp);
  expected = "dog";
  EXPECT_EQ(TrimString(&s, white), 2);
  EXPECT_EQ(TrimString(&sp, white), 2);
  EXPECT_EQ(expected, s);
  EXPECT_EQ(expected, sp);

  sp = " \n\t\t I love my little dog \n\t ";
  s = std::string(sp);
  expected = "I love my little dog";
  EXPECT_EQ(TrimString(&s, white), 9);
  EXPECT_EQ(TrimString(&sp, white), 9);
  EXPECT_EQ(expected, s);
  EXPECT_EQ(expected, sp);

  // There was a bug that caused the leading and trailing null bytes to be
  // trimmed.
  sp = absl::string_view("\0abc\0", 5);
  s = std::string(sp);
  expected.assign("\0abc\0", 5);
  EXPECT_EQ(TrimString(&s, white), 0);
  EXPECT_EQ(TrimString(&sp, white), 0);
  EXPECT_EQ(expected, s);
  EXPECT_EQ(expected, sp);

  // Test that null characters are stripped if they are explicitly part
  // of the remove string.
  std::string white_nulls(" \n\t\0", 4);
  std::string tmp;
  tmp.assign("\0\t \t ", 5);
  tmp.append("I love my little dog");
  tmp.append(" \n\0\t ", 5);
  sp = tmp;
  s = tmp;
  expected = "I love my little dog";
  EXPECT_EQ(TrimString(&s, white_nulls), 10);
  EXPECT_EQ(TrimString(&sp, white_nulls), 10);
  EXPECT_EQ(expected, s);
  EXPECT_EQ(expected, sp);

  // TrimRunsInString
  s = "";
  TrimRunsInString(&s, " \t\r\n");
  EXPECT_EQ("", s);

  s = "plain vanilla String";
  TrimRunsInString(&s, " \t\r\n");
  EXPECT_EQ("plain vanilla String", s);
  TrimRunsInString(&s, "xyz");
  EXPECT_EQ("plain vanilla String", s);

  s = " a   b - - c -...d.e..-f -";
  TrimRunsInString(&s, " -.");
  EXPECT_EQ("a b c d e f", s);

  s = "   a    test of   the Trim-Runs-In-String function   ";
  TrimRunsInString(&s, " ");
  EXPECT_EQ("a test of the Trim-Runs-In-String function", s);
  TrimRunsInString(&s, "-of");
  EXPECT_EQ("a test - the Trim-Runs-In-String -uncti-n", s);
  TrimRunsInString(&s, " -g");
  EXPECT_EQ("a test the Trim Runs In Strin uncti n", s);

  s.assign(".\0-\0String..\0with-\0-nulls\0-.\0", 29);
  TrimRunsInString(&s, ".");
  EXPECT_EQ(std::string("\0-\0String.\0with-\0-nulls\0-.\0", 27), s);
  TrimRunsInString(&s, absl::string_view("\0.-", 3));
  EXPECT_EQ(std::string("String\0with\0nulls", 17), s);
}

TEST(Strip, RemoveNullsInString) {
  std::string s;

  s = "Hey there";
  RemoveNullsInString(&s);
  EXPECT_EQ("Hey there", s);

  s = std::string("\0", 1);
  RemoveNullsInString(&s);
  EXPECT_EQ("", s);

  s = std::string("\0\0\0", 3);
  RemoveNullsInString(&s);
  EXPECT_EQ("", s);

  s = std::string("a\0b\0c\0d", 7);
  RemoveNullsInString(&s);
  EXPECT_EQ("abcd", s);

  s = std::string("\0\0\0aaa\0\0bb\0c", 12);
  RemoveNullsInString(&s);
  EXPECT_EQ("aaabbc", s);

  s = std::string("a\0bb\0\0ccc\0\0\0", 12);
  RemoveNullsInString(&s);
  EXPECT_EQ("abbccc", s);
}

void test_strrmm(const char* original, const char* remove, const char* expect) {
  // first, the char* version
#ifdef _MSC_VER
  char* tmp = _strdup(original);
#else
  char* tmp = strdup(original);
#endif
  strrmm(tmp, remove);
  EXPECT_STREQ(expect, tmp);
  free(tmp);

  // then, the string version
  std::string str_tmp(original);
  strrmm(&str_tmp, remove);
  EXPECT_EQ(expect, str_tmp);
}

// Test strrmm
TEST(stringtest, strrmm) {
  test_strrmm(
      "New%York"
      "@ Yankees./#Super.So_nics",
      "#_"
      "@% /.",
      "NewYorkYankeesSuperSonics");

  test_strrmm("    ", " ", "");

  test_strrmm(" a.b,c  d ", ",d. ", "abc");

  test_strrmm(" a.\"b,c\"  d ", ",d. ", "a\"bc\"");

  test_strrmm(" a.\"b,c\"  d ", ",d.\" ", "abc");

  // Make sure strrmm works with CoW strings
  const char kOrig[] = "does it work for me?";
  std::string orig(kOrig);
  std::string cpy(orig);
  strrmm(&orig, " ?");
  EXPECT_EQ(orig, "doesitworkforme");
  EXPECT_EQ(cpy, kOrig);

  // Make sure the string version of strrmm is binary safe.
  std::string str;

  str.assign("abc\0def", 7);
  strrmm(&str, "bcde");
  EXPECT_EQ(std::string("a\0f", 3), str);
  EXPECT_NE(std::string("a\0c", 3), str);

  str.assign("abcdef");
  strrmm(&str, std::string("bc\0de", 5));
  EXPECT_EQ("af", str);

  str.assign("abc\0def", 7);
  strrmm(&str, std::string("bc\0de", 5));
  EXPECT_EQ("af", str);
}

TEST(Strip, StripPrefixString) {
  const std::string foobar = "foobar";
  const std::string FOOBAR = "FOOBAR";
  const absl::string_view null_stringpiece;

  EXPECT_EQ(std::string(absl::StripPrefix(foobar, "foo")), std::string("bar"));
  EXPECT_EQ(std::string(absl::StripPrefix(foobar, "")), foobar);
  EXPECT_EQ(std::string(absl::StripPrefix(foobar, null_stringpiece)), foobar);
  EXPECT_THAT(std::string(absl::StripPrefix(foobar, "foobar")), IsEmpty());
  EXPECT_EQ(std::string(absl::StripPrefix(foobar, "bar")), foobar);
  EXPECT_EQ(std::string(absl::StripPrefix(foobar, "foobarr")), foobar);
  EXPECT_THAT(std::string(absl::StripPrefix("", "")), IsEmpty());
}

TEST(Strip, TryStripPrefixString) {
  const std::string foobar = "foobar";
  const absl::string_view null_stringpiece;
  std::string result;

  EXPECT_TRUE(TryStripPrefixString(foobar, "foo", &result));
  EXPECT_EQ(result, "bar");
  EXPECT_TRUE(TryStripPrefixString(foobar, "", &result));
  EXPECT_EQ(result, foobar);
  EXPECT_TRUE(TryStripPrefixString(foobar, null_stringpiece, &result));
  EXPECT_EQ(result, foobar);
  EXPECT_TRUE(TryStripPrefixString(foobar, "foobar", &result));
  EXPECT_THAT(result, IsEmpty());
  EXPECT_FALSE(TryStripPrefixString(foobar, "bar", &result));
  EXPECT_EQ(result, foobar);
  EXPECT_FALSE(TryStripPrefixString(foobar, "foobarr", &result));
  EXPECT_EQ(result, foobar);
  EXPECT_TRUE(TryStripPrefixString("", "", &result));
  EXPECT_THAT(result, IsEmpty());

  // Make sure we can replace the input string.
  result = foobar;
  EXPECT_TRUE(TryStripPrefixString(result, "foo", &result));
  EXPECT_EQ(result, "bar");
}

TEST(Strip, StripSuffixString) {
  const std::string foobar = "foobar";
  const std::string FOOBAR = "FOOBAR";
  const absl::string_view null_stringpiece;

  EXPECT_EQ(std::string(absl::StripSuffix(foobar, "bar")), std::string("foo"));
  EXPECT_EQ(std::string(absl::StripSuffix(foobar, "")), foobar);
  EXPECT_EQ(std::string(absl::StripSuffix(foobar, null_stringpiece)), foobar);
  EXPECT_THAT(std::string(absl::StripSuffix(foobar, "foobar")), IsEmpty());
  EXPECT_EQ(std::string(absl::StripSuffix(foobar, "foo")), foobar);
  EXPECT_EQ(std::string(absl::StripSuffix(foobar, "ffoobar")), foobar);
  EXPECT_THAT(std::string(absl::StripSuffix("", "")), IsEmpty());
}

TEST(Strip, TryStripSuffixString) {
  const std::string foobar = "foobar";
  const absl::string_view null_stringpiece;
  std::string result;

  EXPECT_TRUE(TryStripSuffixString(foobar, "bar", &result));
  EXPECT_EQ(result, "foo");
  EXPECT_TRUE(TryStripSuffixString(foobar, "", &result));
  EXPECT_EQ(result, foobar);
  EXPECT_TRUE(TryStripSuffixString(foobar, null_stringpiece, &result));
  EXPECT_EQ(result, foobar);
  EXPECT_TRUE(TryStripSuffixString(foobar, "foobar", &result));
  EXPECT_THAT(result, IsEmpty());
  EXPECT_FALSE(TryStripSuffixString(foobar, "foo", &result));
  EXPECT_EQ(result, foobar);
  EXPECT_FALSE(TryStripSuffixString(foobar, "ffoobar", &result));
  EXPECT_EQ(result, foobar);
  EXPECT_TRUE(TryStripSuffixString("", "", &result));
  EXPECT_THAT(result, IsEmpty());

  // Make sure we can replace the input string.
  result = foobar;
  EXPECT_TRUE(TryStripSuffixString(result, "bar", &result));
  EXPECT_EQ(result, "foo");
}

TEST(Strip, ReplaceCharacter) {
  char test[256];
  absl::SNPrintF(test, sizeof(test), "%s", "");
  ReplaceCharacter(test, strlen(test), '-', 'x');
  EXPECT_STREQ("", test);

  absl::SNPrintF(test, sizeof(test), "%s", "no occurrences");
  ReplaceCharacter(test, strlen(test), '-', 'x');
  EXPECT_STREQ("no occurrences", test);

  absl::SNPrintF(test, sizeof(test), "%s", "-a--bc-");
  ReplaceCharacter(test, strlen(test), '-', 'x');
  EXPECT_STREQ("xaxxbcx", test);

  // Replacing '-' with the null character.
  absl::SNPrintF(test, sizeof(test), "%s", "-a--bc-");
  ReplaceCharacter(test, strlen(test), '-', '\0');
  EXPECT_EQ(std::string("\0a\0\0bc\0", 7), std::string(test, 7));
}

TEST(Strip, ReplaceCharactersWithLength) {
  char test[256];
  absl::SNPrintF(test, sizeof(test), "%s", "-a--bc-");
  ReplaceCharacters(test, 0, "-", 'x');  // Passing empty prefix.
  EXPECT_STREQ("-a--bc-", test);

  absl::SNPrintF(test, sizeof(test), "%s", "no occurrences--");
  ReplaceCharacters(test, strlen(test) - 2, "-", 'x');
  EXPECT_STREQ("no occurrences--", test);

  absl::SNPrintF(test, sizeof(test), "%s", "-a--bc--");
  ReplaceCharacters(test, strlen(test) - 1, "-", 'x');
  EXPECT_STREQ("xaxxbcx-", test);

  memcpy(test, "a-b\0c-d", 7);  // Null character in input, not replaced.
  ReplaceCharacters(test, 7, "-", 'x');
  EXPECT_EQ(std::string("axb\0cxd", 7), std::string(test, 7));

  memcpy(test, "\0a\0\0bc\0", 7);  // Replacing null character.
  ReplaceCharacters(test, 7, absl::string_view("\0", 1), 'x');
  EXPECT_EQ("xaxxbcx", std::string(test, 7));

  // Replacing '-' with the null character.
  absl::SNPrintF(test, sizeof(test), "%s", "-a--bc-");
  ReplaceCharacters(test, strlen(test), "-", '\0');
  EXPECT_EQ(std::string("\0a\0\0bc\0", 7), std::string(test, 7));

  // Similar tests with multiple characters to remove.
  absl::SNPrintF(test, sizeof(test), "%s", "qa-qbc-");
  ReplaceCharacters(test, 0, "qz-", 'x');  // Passing empty prefix.
  EXPECT_STREQ("qa-qbc-", test);

  absl::SNPrintF(test, sizeof(test), "%s", "no occurrences-q");
  ReplaceCharacters(test, strlen(test) - 2, "qz-", 'x');
  EXPECT_STREQ("no occurrences-q", test);

  absl::SNPrintF(test, sizeof(test), "%s", "qa-qbc-z");
  ReplaceCharacters(test, strlen(test) - 1, "qz-", 'x');
  EXPECT_STREQ("xaxxbcxz", test);

  memcpy(test, "azb\0c-d", 7);  // Null character in input, not replaced.
  ReplaceCharacters(test, 7, "qz-", 'x');
  EXPECT_EQ(std::string("axb\0cxd", 7), std::string(test, 7));

  memcpy(test, "\0a\0zbcz", 7);  // Replacing null character.
  ReplaceCharacters(test, 7, absl::string_view("q\0z", 3), 'x');
  EXPECT_EQ("xaxxbcx", std::string(test, 7));

  // Replacing with the null character.
  absl::SNPrintF(test, sizeof(test), "%s", "qa-qbc-");
  ReplaceCharacters(test, strlen(test), "qz-", '\0');
  EXPECT_EQ(std::string("\0a\0\0bc\0", 7), std::string(test, 7));

  // No characters to remove.
  absl::SNPrintF(test, sizeof(test), "%s", "abc");
  ReplaceCharacters(test, strlen(test), "", 'x');
  EXPECT_STREQ("abc", test);
}

TEST(Strip, ReplaceCharactersObject) {
  std::string test;
  ReplaceCharacters(&test, "-", 'x');
  EXPECT_EQ("", test);

  test = "no occurrences";
  ReplaceCharacters(&test, "-", 'x');
  EXPECT_EQ("no occurrences", test);

  test = "-a--bc-";
  ReplaceCharacters(&test, "-", 'x');
  EXPECT_EQ("xaxxbcx", test);

  test.assign("a-b\0c-d", 7);  // Null character in input, not replaced.
  ReplaceCharacters(&test, "-", 'x');
  EXPECT_EQ(std::string("axb\0cxd", 7), test);

  test.assign("\0a\0\0bc\0", 7);  // Replacing null character.
  ReplaceCharacters(&test, absl::string_view("\0", 1), 'x');
  EXPECT_EQ("xaxxbcx", test);

  test = ("-a--bc-");  // Replacing '-' with the null character.
  ReplaceCharacters(&test, "-", '\0');
  EXPECT_EQ(std::string("\0a\0\0bc\0", 7), test);

  // Similar tests with multiple characters to remove.
  test = "";
  ReplaceCharacters(&test, "qz-", 'x');
  EXPECT_EQ("", test);

  test = "no occurrences";
  ReplaceCharacters(&test, "qz-", 'x');
  EXPECT_EQ("no occurrences", test);

  test = ("qa-qbc-");
  ReplaceCharacters(&test, "qz-", 'x');
  EXPECT_EQ("xaxxbcx", test);

  test.assign("azb\0c-d", 7);  // Null character in input, not replaced.
  ReplaceCharacters(&test, "qz-", 'x');
  EXPECT_EQ(std::string("axb\0cxd", 7), test);

  test.assign("\0a\0zbcz", 7);  // Replacing null character.
  ReplaceCharacters(&test, absl::string_view("q\0z", 3), 'x');
  EXPECT_EQ("xaxxbcx", test);

  test = "qa-qbc-";  // Replacing with the null character.
  ReplaceCharacters(&test, "qz-", '\0');
  EXPECT_EQ(std::string("\0a\0\0bc\0", 7), test);

  // No characters to remove.
  test = "abc";
  ReplaceCharacters(&test, "", 'x');
  EXPECT_EQ("abc", test);
}

TEST(Strip, SkipLeadingWhitespace) {
  const char* id = " \t\n\r\v  \0 ";
  EXPECT_EQ(strings::SkipLeadingWhitespace(id), id + 7);
  EXPECT_EQ(strings::SkipLeadingWhitespace(id + 7), id + 7);
  EXPECT_EQ(strings::SkipLeadingWhitespace(id + 8), id + 9);
  id = "\040\tabc";
  EXPECT_EQ(strings::SkipLeadingWhitespace(id), id + 2);
  EXPECT_EQ(strings::SkipLeadingWhitespace(id + 2), id + 2);
  id = "\240xyz";
  EXPECT_EQ(strings::SkipLeadingWhitespace(id), id);
}

}  // namespace
}  // namespace strings
