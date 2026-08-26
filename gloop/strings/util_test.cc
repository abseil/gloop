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

#include "gloop/strings/util.h"

#include <sys/types.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <random>
#include <string>
#include <vector>

#include "absl/container/fixed_array.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "gtest/gtest.h"

using RandomEngine = std::minstd_rand0;

// check (a_ == b_).  If not, log an error
// NOTE: #a_ output the actual variable name used for a_
#define TEST_EQ(a_, b_, error_)                                   \
  if (a_ != b_) {                                                 \
    LOG(ERROR) << #a_ << "=" << a_ << " != " << #b_ << "=" << b_; \
    ++(error_);                                                   \
  }

// Compare bytes 0..len-1 of x and y.  If not equal, abort with verbose error
// message showing position and numeric value that differed.
// Handles embedded nulls just like any other byte.
// Only added because std::string.compare() in gcc-3.3.3 seems to misbehave with
// embedded nulls.
// TODO: switch back to std::string::compare() if/when gcc is fixed
#define CHECK_EQ_ARRAY(len, x, y, msg)                                      \
  for (int j = 0; j < len; ++j) {                                           \
    if (x[j] != y[j]) {                                                     \
      LOG(FATAL) << "" #x << " != " #y << " byte " << j << " msg: " << msg; \
    }                                                                       \
  }

TEST(Util, GetPrintableString) {
  LOG(INFO) << "Testing GetPrintableString";

  int num_errors = 0;
  const char* p1 = nullptr;
  const char* p2 = "Hello world";
  TEST_EQ(strcmp(GetPrintableString(p1), "(null)"), 0, num_errors);
  TEST_EQ(GetPrintableString(p2), p2, num_errors);
  ASSERT_EQ(0, num_errors);
}

TEST(Util, strnchr) {
  LOG(INFO) << "Testing strnchr";
  const char* input = "1234567890";
  struct TestCases {
    char search;
    int cutoff;
  } testcases[] = {{'5', 20}, {'1', 0}, {'9', 8}, {'8', 8}};

  int num_errors = 0;
  for (const TestCases& testcase : testcases) {
    const char* test_result = strnchr(input, testcase.search, testcase.cutoff);
    const char* golden_result = strchr(input, testcase.search);
    if ((golden_result - input) >= testcase.cutoff) golden_result = nullptr;

    if (golden_result != test_result) {
      LOG(ERROR) << "strnchr(" << input << ", " << testcase.search << ", "
                 << testcase.cutoff << ") = " << test_result
                 << ".  Expect=" << golden_result;
      ++num_errors;
    }
  }
  ASSERT_EQ(0, num_errors);
}

TEST(Util, strnstr) {
  LOG(INFO) << "Testing strnstr";
  const char* input = "1234567890";
  const struct TestCases {
    const char* search;
    int cutoff;
  } testcases[] = {
      {"5", 20},  {"1", 0},   {"9", 8},    {"8", 8}, {"456", 5},
      {"456", 6}, {"456", 7}, {"e", 9999}, {"", 9},
  };

  int num_errors = 0;
  for (const TestCases& testcase : testcases) {
    const char* test_result = strnstr(input, testcase.search, testcase.cutoff);
    const char* golden_result = strstr(input, testcase.search);
    if (golden_result &&
        (golden_result + strlen(testcase.search) - input) > testcase.cutoff)
      golden_result = nullptr;

    if (golden_result != test_result) {
      LOG(ERROR) << "strnstr(" << input << ", " << testcase.search << ", "
                 << testcase.cutoff << ") = " << test_result
                 << ".  Expect=" << golden_result;
      ++num_errors;
    }
  }
  ASSERT_EQ(0, num_errors);
}

TEST(Util, strprefix_family) {
  LOG(INFO) << "Testing strprefix family";
  const char* const foobar = "foobar";
  const char* const FOOBAR = "FOOBAR";
  const char* const empty = "";
  const char* const null = nullptr;

  CHECK_EQ(strprefix(foobar, "foo"), foobar + 3);
  CHECK_EQ(strprefix(foobar, ""), foobar);
  CHECK_EQ(strprefix(foobar, "foobar"), foobar + 6);
  CHECK_EQ(strprefix(foobar, "bar"), null);
  CHECK_EQ(strprefix(foobar, "foobarr"), null);
  CHECK_EQ(strprefix(empty, ""), empty);

  CHECK_EQ(strcaseprefix(foobar, "FOO"), foobar + 3);
  CHECK_EQ(strcaseprefix(FOOBAR, "foo"), FOOBAR + 3);

  CHECK_EQ(strnprefix(foobar, 6, "foo", 3), foobar + 3);
  CHECK_EQ(strnprefix(foobar, 6, "", 0), foobar);
  CHECK_EQ(strnprefix(foobar, 6, "foobar", 6), foobar + 6);
  CHECK_EQ(strnprefix(foobar, 6, "bar", 3), null);
  CHECK_EQ(strnprefix(foobar, 6, "foobarr", 7), null);
  CHECK_EQ(strnprefix(empty, 0, "", 0), empty);
}

static void TestOne_gstrncasestr_split(const char* haystack, const char* prefix,
                                       char non_alpa, const char* suffix,
                                       int pos, int subtract = 0) {
  const char* where = gstrncasestr_split(haystack, prefix, non_alpa, suffix,
                                         strlen(haystack) - subtract);
  if (pos == -1) {
    CHECK(where == nullptr);
    return;
  }
  CHECK(pos == where - haystack);
}

TEST(Util, gstrncasestr_split) {
  TestOne_gstrncasestr_split("abc.def", "abc", '.', "def", 0);
  TestOne_gstrncasestr_split("abc.def", "abc", '.', "defg", -1);
  TestOne_gstrncasestr_split("bc.def", "abc", '.', "def", -1);
  TestOne_gstrncasestr_split("abc.de.abc.def", "abc", '.', "def", 7);
  TestOne_gstrncasestr_split("abc.de.abc.de", "abc", '.', "def", -1);
  TestOne_gstrncasestr_split("abc.de.abc.def", "abc", '.', "def", -1, 1);

  TestOne_gstrncasestr_split("Abc.Def", "abc", '.', "def", 0);
  TestOne_gstrncasestr_split("abc.def", "aBc", '.', "dEf", 0);
}

TEST(Util, ScanForFirstWord) {
  struct testcase {
    int casenum;
    const char* word;
    int startpos, len;
  } testcases[] = {
      {1, "       hello    ", 7, 5},
      {2, "     h    ", 5, 1},
      {3, "\t\v\n\t xx", 5, 2},
      {4, "hello\t    ", 0, 5},
      {5, "  \t\t\t   ", -1, 0},
      {0, nullptr, 0, 0}  // sentinel: casenum = 0
  };

  LOG(INFO) << "Testing ScanForFirstWord";

  for (struct testcase* i = testcases; i->casenum; ++i) {
    // check const case
    absl::string_view result = strings::ScanForFirstWord(i->word);
    if (i->startpos == -1) {
      CHECK(result.empty());
    } else {
      CHECK(i->word + i->startpos == result.data()) << i->casenum;
      CHECK(result.length() == i->len) << i->casenum;
    }
  }
}

TEST(ScanForFirstWord, EmptyInput) {
  absl::string_view sp;
  EXPECT_TRUE(strings::ScanForFirstWord(sp).empty());
  sp = absl::string_view();
  EXPECT_TRUE(strings::ScanForFirstWord(sp).empty());
  sp = absl::string_view("");
  EXPECT_TRUE(strings::ScanForFirstWord(sp).empty());
  sp = absl::string_view(sp.data() + 1000, 0);
  EXPECT_TRUE(strings::ScanForFirstWord(sp).empty());
}

TEST(ScanForFirstWord, AllSpace) {
  absl::string_view sp(" ");
  EXPECT_TRUE(strings::ScanForFirstWord(sp).empty());
  sp = absl::string_view("  \t\t\t   ");
  EXPECT_TRUE(strings::ScanForFirstWord(sp).empty());
}

TEST(ScanForFirstWord, AllWord) {
  absl::string_view sp("x");
  absl::string_view word = strings::ScanForFirstWord(sp);
  EXPECT_EQ(sp.data(), word.data());
  EXPECT_EQ(sp.size(), word.size());
  sp = absl::string_view("hello");
  word = strings::ScanForFirstWord(sp);
  EXPECT_EQ(sp.data(), word.data());
  EXPECT_EQ(sp.size(), word.size());
}

TEST(ScanForFirstWord, WordAtStart) {
  absl::string_view sp("hello\t    ");
  absl::string_view word = strings::ScanForFirstWord(sp);
  EXPECT_EQ(sp.data(), word.data());
  EXPECT_EQ(5, word.size());
}

TEST(ScanForFirstWord, WordAtEnd) {
  absl::string_view sp("\t\v\n\t xx");
  absl::string_view word = strings::ScanForFirstWord(sp);
  EXPECT_EQ(sp.data() + 5, word.data());
  EXPECT_EQ(2, word.size());
}

TEST(ScanForFirstWord, WordInMiddle) {
  absl::string_view sp("       hello    ");
  absl::string_view word = strings::ScanForFirstWord(sp);
  EXPECT_EQ(sp.data() + 7, word.data());
  EXPECT_EQ(5, word.size());
  sp = absl::string_view("     h    ");
  word = strings::ScanForFirstWord(sp);
  EXPECT_EQ(sp.data() + 5, word.data());
  EXPECT_EQ(1, word.size());
}

TEST(ScanForFirstWord, MultiWord) {
  absl::string_view sp("hello world");
  absl::string_view word = strings::ScanForFirstWord(sp);
  EXPECT_EQ(sp.data(), word.data());
  EXPECT_EQ(5, word.size());
  sp = absl::string_view("   \ta\vb\nc d");
  word = strings::ScanForFirstWord(sp);
  EXPECT_EQ(sp.data() + 4, word.data());
  EXPECT_EQ(1, word.size());
}

TEST(Util, safestrncpy) {
  LOG(INFO) << "Testing safestrncpy";
  static const char src[] = "abcdefg";
  char dst[12];

  // Each test fills dst with 'x' so that errors in copying
  // and padding may be detected
  memset(dst, 'x', sizeof(dst));
  CHECK_EQ(safestrncpy(dst, src, 0), dst);
  CHECK_EQ_ARRAY(sizeof(dst), dst, "xxxxxxxxxxxx", "n = 0");

  memset(dst, 'x', sizeof(dst));
  CHECK_EQ(safestrncpy(dst, src, 1), dst);
  CHECK_EQ_ARRAY(sizeof(dst), dst, "\0xxxxxxxxxxx", "n = 1");

  memset(dst, 'x', sizeof(dst));
  CHECK_EQ(safestrncpy(dst, src, 2), dst);
  CHECK_EQ_ARRAY(sizeof(dst), dst, "a\0xxxxxxxxxx", "n = 2");

  memset(dst, 'x', sizeof(dst));
  CHECK_EQ(safestrncpy(dst, src, 7), dst);
  CHECK_EQ_ARRAY(sizeof(dst), dst, "abcdef\0xxxxx", "n = 7");

  memset(dst, 'x', sizeof(dst));
  CHECK_EQ(safestrncpy(dst, src, 8), dst);
  CHECK_EQ_ARRAY(sizeof(dst), dst, "abcdefg\0xxxx", "n = 8");

  memset(dst, 'x', sizeof(dst));
  CHECK_EQ(safestrncpy(dst, src, 9), dst);
  CHECK_EQ_ARRAY(sizeof(dst), dst, "abcdefg\0xxxx", "n = 9");
}

TEST(Util, PrefixSuccessor) {
  LOG(INFO) << "Testing PrefixSuccessor";
  CHECK_EQ(PrefixSuccessor("a"), "b");
  CHECK_EQ(PrefixSuccessor("aaAA"), "aaAB");
  CHECK_EQ(PrefixSuccessor("aaa\xff"), "aab");
  CHECK_EQ(PrefixSuccessor(std::string("\x00", 1)), "\x01");
  CHECK_EQ(PrefixSuccessor("az\xe0"), "az\xe1");
  CHECK_EQ(PrefixSuccessor("\xff\xff\xff"), "");
  CHECK_EQ(PrefixSuccessor(""), "");
}

TEST(Util, PrefixSuccessor_InPlace) {
  LOG(INFO) << "Testing PrefixSuccessor (in place)";
  std::string str = "a";
  PrefixSuccessor(&str);
  EXPECT_EQ(str, "b");

  str = "aaAA";
  PrefixSuccessor(&str);
  EXPECT_EQ(str, "aaAB");

  str = "aaa\xff";
  PrefixSuccessor(&str);
  EXPECT_EQ(str, "aab");

  str = std::string("\x00", 1);
  PrefixSuccessor(&str);
  EXPECT_EQ(str, "\x01");

  str = "az\xe0";
  PrefixSuccessor(&str);
  EXPECT_EQ(str, "az\xe1");

  str = "\xff\xff\xff";
  PrefixSuccessor(&str);
  EXPECT_EQ(str, "");

  str = "";
  PrefixSuccessor(&str);
  EXPECT_EQ(str, "");
}

TEST(Util, ImmediateSuccessor) {
  LOG(INFO) << "Testing ImmediateSuccessor";
  CHECK_EQ(ImmediateSuccessor("hello"), absl::string_view("hello\0", 6));
  CHECK_EQ(ImmediateSuccessor(""), absl::string_view("\0", 1));
}

static std::string ShortSeparator(absl::string_view a, absl::string_view b) {
  std::string result;
  FindShortestSeparator(a, b, &result);
  return result;
}

TEST(FindEol, Test) {
  std::string s0 = "Hello";
  absl::string_view sp0 = strings::FindEol(s0);
  EXPECT_TRUE(sp0.empty());
  EXPECT_EQ(0, sp0.length());
  EXPECT_EQ(5, sp0.data() - s0.data());

  std::string s1 = "Hello\nUnix";
  absl::string_view sp1 = strings::FindEol(s1);
  EXPECT_EQ(1, sp1.length());
  EXPECT_EQ(5, sp1.data() - s1.data());

  std::string s2 = "Hello\rmacOS9";
  absl::string_view sp2 = strings::FindEol(s2);
  EXPECT_EQ(1, sp2.length());
  EXPECT_EQ(5, sp2.data() - s2.data());

  std::string s3 = "Hello\r\nWindows";
  absl::string_view sp3 = strings::FindEol(s3);
  EXPECT_EQ(2, sp3.length());
  EXPECT_EQ(5, sp3.data() - s3.data());

  // Two-character sequences.
  std::string snn = "Hello\n\n";
  absl::string_view spnn = strings::FindEol(snn);
  EXPECT_EQ(1, spnn.length());
  EXPECT_EQ(5, spnn.data() - snn.data());

  std::string snr = "Hello\n\r";
  absl::string_view spnr = strings::FindEol(snr);
  EXPECT_EQ(1, spnr.length());  // \n\r is not a thing
  EXPECT_EQ(5, spnr.data() - snr.data());

  std::string srn = "Hello\r\n";
  absl::string_view sprn = strings::FindEol(srn);
  EXPECT_EQ(2, sprn.length());
  EXPECT_EQ(5, sprn.data() - srn.data());

  std::string srr = "Hello\r\r";
  absl::string_view sprr = strings::FindEol(srr);
  EXPECT_EQ(1, sprr.length());
  EXPECT_EQ(5, sprr.data() - srr.data());
}

TEST(FindShortestSeparator, Empty) {
  EXPECT_EQ("", ShortSeparator("", ""));
  EXPECT_EQ("", ShortSeparator("", "x"));
  EXPECT_EQ("x", ShortSeparator("x", ""));
}

TEST(FindShortestSeparator, Prefix) {
  EXPECT_EQ("foo", ShortSeparator("foo", "foo"));
  EXPECT_EQ("foo", ShortSeparator("foo", "foob"));
  EXPECT_EQ("foo", ShortSeparator("foo", "fo"));
}

TEST(FindShortestSeparator, DiffInMiddle) {
  EXPECT_EQ("fop", ShortSeparator("foobar", "foxhunt"));
}

TEST(FindShortestSeparator, DiffAtStart) {
  EXPECT_EQ("b", ShortSeparator("abracadabra", "bacradabra"));
}

TEST(FindShortestSeparator, DiffAtEnd) {
  EXPECT_EQ("foo", ShortSeparator("foo", "fop"));
}

TEST(FindShortestSeparator, AvoidOverflow) {
  EXPECT_EQ("fo\377a", ShortSeparator("fo\377a", "foobar"));
}

TEST(FindShortestSeparator, OutOfOrder) {
  EXPECT_EQ("foxhunt", ShortSeparator("foxhunt", "foobar"));
}

TEST(FindShortestSeparator, DoNotHitB) {
  EXPECT_EQ("3499", ShortSeparator("3499", "35"));
}

static void BM_ImmediateSuccessor(benchmark::State& state) {
  std::string arg(state.range(0), 'x');
  for (auto _ : state) {
    ImmediateSuccessor(arg);
  }
}
BENCHMARK(BM_ImmediateSuccessor)->Range(0, 1 << 20);

TEST(Util, strcasestr_alnum) {
  LOG(INFO) << "Testing strcasestr_alnum";
  CHECK(strcasestr_alnum("", "") != nullptr);
  CHECK(strcasestr_alnum("test", " TeSt! ") != nullptr);
  CHECK(strcasestr_alnum(" TeSt! ", "test") != nullptr);
  CHECK(strcasestr_alnum("#$%^", "^&*(") != nullptr);
  CHECK(strcasestr_alnum("#$%^", "^&*(a") == nullptr);
  CHECK(strcasestr_alnum("This is a longer test string", "ISALONGER") !=
        nullptr);
  CHECK(strcasestr_alnum("This is a longer test string", "ISALONGEL") ==
        nullptr);
  CHECK(strcasestr_alnum("This is a longer test string", "IS-A-LONGER") !=
        nullptr);
  CHECK(strcasestr_alnum("This is a longer test string", "IS-A-LONGEL") ==
        nullptr);
}

TEST(Util, UniformInsertString) {
  LOG(INFO) << "Testing UniformInsertString";

  struct TestCase {
    const char* orig;
    const char* seperator;
    int interval;
    const char* expected;
  };

  TestCase tests[] = {
      {"", "ef", 1,  // empty string
       ""},
      {"abcdabcdabcdabcdabcdabcd", "ef", 0,  // interval == 0
       "abcdabcdabcdabcdabcdabcd"},
      {"abcdabcdabcdabcdabcdabcd", "", 2,  // empty seperator
       "abcdabcdabcdabcdabcdabcd"},
      {"abcdabcdabcdabcdabcdabcd", "ef", 24,  // interval too big
       "abcdabcdabcdabcdabcdabcd"},
      {"abcdabcdabcdabcdabcdabcd", "e", 1,  // interval == 1
       "aebecedeaebecedeaebecedeaebecedeaebecedeaebeced"},
      {
          "abcdabcdabcdabcdabcdabcd",
          "ef",
          4,
          "abcdefabcdefabcdefabcdefabcdefabcd",
      },
      {
          "abcdabcdabcdabcdabcdabc",
          "ef",
          4,
          "abcdefabcdefabcdefabcdefabcdefabc",
      },
      {
          "abcdabcdabcdabcdabcdab",
          "ef",
          4,
          "abcdefabcdefabcdefabcdefabcdefab",
      },
      {
          "abcdabcdabcdabcdabcda",
          "ef",
          4,
          "abcdefabcdefabcdefabcdefabcdefa",
      },
      {
          "abcdabcdabcdabcdabcd",
          "ef",
          4,
          "abcdefabcdefabcdefabcdefabcd",
      },
  };

  for (const TestCase& test : tests) {
    std::string s = test.orig;
    UniformInsertString(&s, test.interval, test.seperator);
    CHECK_STREQ(s.c_str(), test.expected);
  }
}

TEST(Util, AdvanceIdentifier) {
  struct Cases {
    std::string input;
    std::string output;
    bool success;
  };
  std::vector<Cases> cases = {
      {"A9__b*", "*", true}, {"9__b*", "9__b*", false}, {"", "", false},
      {"String", "", true},  {"g00gle", "", true},      {"space ", " ", true},
      {"42", "42", false},
  };
  for (const auto& c : cases) {
    absl::string_view in = c.input;
    EXPECT_EQ(AdvanceIdentifier(&in), c.success);
    EXPECT_EQ(in, c.output);
  }
}

TEST(Util, IsIdentifier) {
  struct Cases {
    std::string input;
    bool success;
  };
  std::vector<Cases> cases = {
      {"A9__b*", false}, {"A9__b", true},  {"9__b*", false},  {"", false},
      {"String", true},  {"g00gle", true}, {"space ", false}, {"42", false},
  };
  for (const auto& c : cases) {
    EXPECT_EQ(IsIdentifier(c.input), c.success);
  }
}

TEST(Util, AdvanceIdentifierDeprecated) {
  LOG(INFO) << "Testing AdvanceIdentifier and IsIdentifier";

  const char* id = "A9__b*";
  CHECK_EQ(AdvanceIdentifier(id), id + 5);
  CHECK(AdvanceIdentifier(id + 1) == nullptr);
  CHECK_EQ(AdvanceIdentifier(id + 2), id + 5);
  CHECK(!IsIdentifier(id));
  id = "String";
  CHECK_EQ(AdvanceIdentifier(id), id + 6);
  CHECK(AdvanceIdentifier("") == nullptr);
  CHECK(!IsIdentifier(""));
  CHECK(IsIdentifier("gOOgle"));
  CHECK(!IsIdentifier("space "));
  CHECK(!IsIdentifier("42"));
}

TEST(Util, FindNth) {
  LOG(INFO) << "Testing FindNth";
  const std::string helloworld("hello, world");
  CHECK_EQ(FindNth(helloworld, 'l', 1), 2);
  CHECK_EQ(FindNth(helloworld, 'l', 2), 3);
  CHECK_EQ(FindNth(helloworld, 'l', 3), 10);
  CHECK_EQ(FindNth(helloworld, 'x', 1), std::string::npos);
  CHECK_EQ(FindNth(helloworld, 'l', 4), std::string::npos);
  CHECK_EQ(FindNth(helloworld, 'l', 0), std::string::npos);
  CHECK_EQ(FindNth(helloworld, 'l', -2), std::string::npos);
  CHECK_EQ(FindNth(helloworld, 'd', 0), std::string::npos);
  CHECK_EQ(FindNth(helloworld, 'd', 1), 11);
  CHECK_EQ(FindNth(helloworld, 'd', 2), std::string::npos);
  CHECK_EQ(FindNth(helloworld, 'h', 0), std::string::npos);
  CHECK_EQ(FindNth(helloworld, 'h', 1), 0);
  CHECK_EQ(FindNth(helloworld, 'h', 2), std::string::npos);

  CHECK_EQ(FindNth("", 'd', 0), std::string::npos);
  CHECK_EQ(FindNth("", 'd', 1), std::string::npos);

  CHECK_EQ(FindNth("d", 'd', 0), std::string::npos);
  CHECK_EQ(FindNth("d", 'd', 1), 0);
  CHECK_EQ(FindNth("d", 'd', 2), std::string::npos);
  CHECK_EQ(FindNth("d", 'e', 1), std::string::npos);

  CHECK_EQ(FindNth("dd", 'd', 0), std::string::npos);
  CHECK_EQ(FindNth("dd", 'd', 1), 0);
  CHECK_EQ(FindNth("dd", 'd', 2), 1);
  CHECK_EQ(FindNth("dd", 'd', 3), std::string::npos);
  CHECK_EQ(FindNth("dd", 'e', 1), std::string::npos);
}

TEST(Util, ReverseFindNth) {
  LOG(INFO) << "Testing ReverseFindNth";
  const std::string helloworld("hello, world");
  CHECK_EQ(ReverseFindNth(helloworld, 'l', 1), 10);
  CHECK_EQ(ReverseFindNth(helloworld, 'l', 2), 3);
  CHECK_EQ(ReverseFindNth(helloworld, 'l', 3), 2);
  CHECK_EQ(ReverseFindNth(helloworld, 'x', 1), std::string::npos);
  CHECK_EQ(ReverseFindNth(helloworld, 'l', 4), std::string::npos);
  CHECK_EQ(ReverseFindNth(helloworld, 'l', 0), std::string::npos);
  CHECK_EQ(ReverseFindNth(helloworld, 'l', -2), std::string::npos);
  CHECK_EQ(ReverseFindNth(helloworld, 'h', 0), std::string::npos);
  CHECK_EQ(ReverseFindNth(helloworld, 'h', 1), 0);
  CHECK_EQ(ReverseFindNth(helloworld, 'h', 2), std::string::npos);
  CHECK_EQ(ReverseFindNth(helloworld, 'h', 3), std::string::npos);
  CHECK_EQ(ReverseFindNth(helloworld, 'e', 0), std::string::npos);
  CHECK_EQ(ReverseFindNth(helloworld, 'e', 1), 1);
  CHECK_EQ(ReverseFindNth(helloworld, 'e', 2), std::string::npos);
  CHECK_EQ(ReverseFindNth(helloworld, 'e', 3), std::string::npos);
  CHECK_EQ(ReverseFindNth(helloworld, 'd', 0), std::string::npos);
  CHECK_EQ(ReverseFindNth(helloworld, 'd', 1), 11);
  CHECK_EQ(ReverseFindNth(helloworld, 'd', 2), std::string::npos);
  CHECK_EQ(ReverseFindNth(helloworld, 'd', 3), std::string::npos);

  CHECK_EQ(ReverseFindNth("", 'd', 0), std::string::npos);
  CHECK_EQ(ReverseFindNth("", 'd', 1), std::string::npos);

  CHECK_EQ(ReverseFindNth("d", 'd', 0), std::string::npos);
  CHECK_EQ(ReverseFindNth("d", 'd', 1), 0);
  CHECK_EQ(ReverseFindNth("d", 'd', 2), std::string::npos);
  CHECK_EQ(ReverseFindNth("d", 'e', 1), std::string::npos);

  CHECK_EQ(ReverseFindNth("dd", 'd', 0), std::string::npos);
  CHECK_EQ(ReverseFindNth("dd", 'd', 1), 1);
  CHECK_EQ(ReverseFindNth("dd", 'd', 2), 0);
  CHECK_EQ(ReverseFindNth("dd", 'd', 3), std::string::npos);
  CHECK_EQ(ReverseFindNth("dd", 'e', 1), std::string::npos);
}

TEST(Util, OnlyWhitespace) {
  LOG(INFO) << "Testing OnlyWhitespace";
  CHECK_EQ(OnlyWhitespace("tt"), false);
  CHECK_EQ(OnlyWhitespace("tt\f \n  "), false);
  CHECK_EQ(OnlyWhitespace("\t \r  tt"), false);
  CHECK_EQ(OnlyWhitespace("\t\v  \n "), true);
  CHECK_EQ(OnlyWhitespace("  "), true);
  CHECK_EQ(OnlyWhitespace(" "), true);
  CHECK_EQ(OnlyWhitespace(""), true);
}

TEST(Util, ContainsWhitespace) {
  LOG(INFO) << "Testing ContainsWhitespace";
  CHECK_EQ(strings::ContainsWhitespace("tt"), false);
  CHECK_EQ(strings::ContainsWhitespace("tt\f \n  "), true);
  CHECK_EQ(strings::ContainsWhitespace("\t \r  tt"), true);
  CHECK_EQ(strings::ContainsWhitespace("\t\v  \n "), true);
  CHECK_EQ(strings::ContainsWhitespace("  "), true);
  CHECK_EQ(strings::ContainsWhitespace(" "), true);
  CHECK_EQ(strings::ContainsWhitespace(""), false);
}

TEST(Util, StringSuffix) {
  LOG(INFO) << "Testing strsuffix";
  const std::string with_suffix("string with a suffix");
  const std::string without_suffix("not in this string");
  const std::string suffix("suffix");
  const char* first_check = strsuffix(with_suffix.c_str(), suffix.c_str());
  CHECK(first_check != nullptr);
  CHECK_EQ(strcmp(first_check, suffix.c_str()), 0);
  const char* second_check = strsuffix(without_suffix.c_str(), suffix.c_str());
  CHECK(second_check == nullptr);
  const char* third_check = strnsuffix(with_suffix.c_str(), with_suffix.size(),
                                       suffix.c_str(), suffix.size());
  CHECK(third_check != nullptr);
  CHECK_EQ(strcmp(third_check, suffix.c_str()), 0);

  struct SuffixTestCase {
    // Test inputs...
    const char* const haystack;  // string in which we're searching
    const char* const needle;    // suffix we're looking for
    // The expected result for case sensitive and insensitive suffix
    // matches, respectively:
    bool case_sensitive_matches;    // expect case-sensitive match?
    bool case_insensitive_matches;  // expect case-insensitive match?
  };
  SuffixTestCase test_cases[] = {
      {"haystack", "needle", false, false},      // needle shorter than haystack
      {"haystack", "big needle", false, false},  // needle longer than haystack
      {"haystack", "!haystack", false, false},   // needle contains haystack
      {"haystack", "stack", true, true},         // normal case
      {"haystack", "haystack", true, true},      // boundary cases...
      {"haystack", "", true, true},
      {"", "haystack", false, false},
      {"", "", true, true},
      {"haystack", "STACK", false, true},  // case-insensitive matches...
      {"haystack", "sTaCK", false, true},
      {"HAYSTACK", "stack", false, true},
      {"HaYsTaCk", "sTaCK", false, true},
      {"aAyStAcK", "sTaCK", false, true},
  };
  for (const SuffixTestCase& t : test_cases) {
    const char* suffix = strsuffix(t.haystack, t.needle);
    const char* nsuffix =
        strnsuffix(t.haystack, strlen(t.haystack), t.needle, strlen(t.needle));
    const char* casesuffix = strcasesuffix(t.haystack, t.needle);
    const char* ncasesuffix = strncasesuffix(t.haystack, strlen(t.haystack),
                                             t.needle, strlen(t.needle));
    if (t.case_sensitive_matches) {
      CHECK_STREQ(suffix, t.needle);
      CHECK_STREQ(nsuffix, t.needle);
    } else {
      CHECK(!suffix);
      CHECK(!nsuffix);
    }
    if (t.case_insensitive_matches) {
      CHECK_STRCASEEQ(casesuffix, t.needle);
      CHECK_STRCASEEQ(ncasesuffix, t.needle);
    } else {
      CHECK(!casesuffix);
      CHECK(!ncasesuffix);
    }
  }
}

TEST(Util, StrStrDelimited) {
  LOG(INFO) << "Testing strstr_delimited()";

  const char* haystack = "foo";
  CHECK_EQ(strstr_delimited(haystack, haystack, 'z'), haystack);
  CHECK(strstr_delimited(haystack, "fo", 'z') == nullptr);
  CHECK_EQ(strstr_delimited(haystack, "foo", ','), haystack);

  haystack = "foo,bar";
  CHECK_EQ(strstr_delimited(haystack, "foo", ','), haystack);
  CHECK_EQ(strstr_delimited(haystack, "bar", ','), haystack + 4);

  // If needle is empty, should always return haystack.
  CHECK_EQ(strstr_delimited(haystack, "", ','), haystack);

  // Substring contains a delimiter, but isn't delimited on both sides.
  CHECK(strstr_delimited(haystack, "oo,b", ',') == nullptr);

  haystack = "foo,bar,foofoo";
  CHECK_EQ(strstr_delimited(haystack, "foo", ','), haystack);
  CHECK_EQ(strstr_delimited(haystack, "bar", ','), haystack + 4);
  CHECK_EQ(strstr_delimited(haystack, "foofoo", ','), haystack + 8);

  // A haystack always contains itself, even if needle contains delimiters.
  CHECK_EQ(strstr_delimited(haystack, haystack, ','), haystack);

  // Substring contains a delimiter, and is delimited on both sides.
  CHECK_EQ(strstr_delimited(haystack, "foo,bar", ','), haystack);
  CHECK_EQ(strstr_delimited(haystack, "bar,foofoo", ','), haystack + 4);

  // strstr() would return true on substrings of items, but we shouldn't.
  CHECK(strstr_delimited(haystack, "foof", ',') == nullptr);

  // Substring contains a delimiter, but isn't delimited on both sides.
  CHECK(strstr_delimited(haystack, "o,bar,f", ',') == nullptr);

  // Courtesy of turnidge.
  haystack = "aaaa,a,a,b";
  CHECK_EQ(strstr_delimited(haystack, "a,a", ','), haystack + 5);

  // Should return the first match
  haystack = "foo,bar,foo";
  CHECK_EQ(strstr_delimited(haystack, "foo", ','), haystack);
  haystack = "baz,foo,bar,foo";
  CHECK_EQ(strstr_delimited(haystack, "foo", ','), haystack + 4);
  haystack = "foo,bar,foo,baz";
  CHECK_EQ(strstr_delimited(haystack, "foo", ','), haystack);
  haystack = "baz,foo,bar,foo,baz";
  CHECK_EQ(strstr_delimited(haystack, "foo", ','), haystack + 4);

  haystack = "";
  // An empty haystack only contains an empty needle, nothing else.
  CHECK(strstr_delimited(haystack, "foo", '!') == nullptr);
  CHECK_EQ(strstr_delimited(haystack, "", '!'), haystack);

  // Consecutive delimiters shouldn't throw us off.
  haystack = ",,,,foo";
  CHECK_EQ(strstr_delimited(haystack, "foo", ','), haystack + 4);
  haystack = "foo,,,,";
  CHECK_EQ(strstr_delimited(haystack, "foo", ','), haystack);

  // If either needle/haystack is nullptr, should return nullptr.
  CHECK(strstr_delimited(nullptr, nullptr, ',') == nullptr);
  CHECK(strstr_delimited("", nullptr, ',') == nullptr);
  CHECK(strstr_delimited(nullptr, "", ',') == nullptr);

  // needles beginning or ending with delimiters are tricky. We
  // currently return nullptr in these cases, which seems fine enough.
  haystack = "a,,,b,c";
  CHECK(strstr_delimited(haystack, ",,,b", ',') == nullptr);
  CHECK(strstr_delimited(haystack, "b,", ',') == nullptr);
  CHECK(strstr_delimited(haystack, ",,b,", ',') == nullptr);
}

void BM_StrStrDelimited(benchmark::State& state) {
  const int num_haystack_items = state.range(0);
  const int key_len = state.range(1);
  const int32_t kDeterministicSeed = 301;

  const char kDelim = '|';
  const std::string alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890";

  RandomEngine rng(kDeterministicSeed);
  std::uniform_int_distribution<uint64_t> random_alphabet_index(
      0, alphabet.size() - 1);

  // Construct a haystack of `num_haystack_items` items of `key_len`.
  std::vector<std::string> haystack_elements;
  while (haystack_elements.size() < num_haystack_items) {
    std::string key;
    key.reserve(key_len);
    for (int i = 0; i < key_len; ++i) {
      key.push_back(alphabet[random_alphabet_index(rng)]);
    }
    haystack_elements.push_back(key);
  }

  const std::string haystack =
      absl::StrJoin(haystack_elements.begin(), haystack_elements.end(),
                    std::string(kDelim, 1));

  // Determine randomly the order of haystack elements we search for during
  // iteration.
  std::vector<size_t> indices(haystack_elements.size());
  for (size_t i = 0; i < indices.size(); ++i) {
    indices[i] = i;
  }
  std::shuffle(indices.begin(), indices.end(), rng);

  size_t i = 0;
  for (auto _ : state) {
    const std::string& needle = haystack_elements[indices[i]];
    benchmark::DoNotOptimize(
        strstr_delimited(haystack.c_str(), needle.c_str(), kDelim));
    ++i;
    if (i == indices.size()) i = 0;
  }
}

BENCHMARK(BM_StrStrDelimited)
    ->RangePair(1, 64 * 1024, 1, 256)
    // Cases for key_len=2 and small haystacks
    ->ArgPair(2, 2)
    ->ArgPair(4, 2)
    ->ArgPair(8, 2)
    ->ArgPair(16, 2)
    ->ArgPair(32, 2)
    // Cases for key_len=4 and small haystacks
    ->ArgPair(2, 4)
    ->ArgPair(4, 4)
    ->ArgPair(8, 4)
    ->ArgPair(16, 4)
    ->ArgPair(32, 4)
    // Cases for large haystacks
    ->ArgPair(1024 * 1024, 2)
    ->ArgPair(1024 * 1024, 4)
    ->ArgPair(1024 * 1024, 16)
    ->ArgPair(1024 * 1024, 32)
    ->ArgPair(1024 * 1024, 256);

TEST(Util, GStrNCaseStr) {
  LOG(INFO) << "Testing gstrncasestr()";

  char haystack[] = "abCDEfGhIj";
  const int hay_len = strlen(haystack);
  CHECK_EQ(gstrncasestr(haystack, haystack, hay_len), haystack);
  CHECK(gstrncasestr(haystack, "jk", hay_len) == nullptr);
  CHECK(gstrncasestr(haystack, "ij", hay_len) == haystack + 8);
  CHECK(gstrncasestr(haystack, "ij", hay_len - 2) == nullptr);
  CHECK(gstrncasestr(haystack, "ij", hay_len - 1) == nullptr);
  CHECK(gstrncasestr(haystack, "Ij", hay_len - 1) == nullptr);
  // If needle is empty, should always return haystack.
  CHECK_EQ(gstrncasestr(haystack, "", hay_len), haystack);

  // should not be checking beyond the '\0'
  haystack[3] = '\0';
  CHECK(gstrncasestr(haystack, "ij", hay_len) == nullptr);
  CHECK(gstrncasestr(haystack, "Ij", hay_len) == nullptr);
  CHECK(gstrncasestr(haystack, "I", 0) == nullptr);
  char* null_ptr = nullptr;
  CHECK(gstrncasestr(null_ptr, "I", 0) == nullptr);
  CHECK(gstrncasestr(haystack, "", 5) == haystack);
  CHECK(gstrncasestr(haystack, "", 0) == haystack);
  CHECK(gstrncasestr(null_ptr, "", 0) == nullptr);
}

static int StringByReferenceRoutine(absl::string_view s) {
  (void)s;
  return 0;
}

static int StringByReferenceWithCopyRoutine(absl::string_view s) {
  std::string copy(s);
  return copy.size();
}

static int StringByValueRoutine(std::string s) {
  (void)s;
  return 0;
}

//
// 3 benchmarks passing a variable (not a temporary) to a function
// - by const reference
// - by const reference and making a local copy
// - by value
//

static void BM_string_by_reference(benchmark::State& state) {
  std::string arg(state.range(0), 'x');
  for (auto _ : state) {
    StringByReferenceRoutine(arg);
  }
}
BENCHMARK(BM_string_by_reference)->Range(0, 1 << 20);

static void BM_string_by_reference_with_copy(benchmark::State& state) {
  std::string arg(state.range(0), 'x');
  for (auto _ : state) {
    StringByReferenceWithCopyRoutine(arg);
  }
}
BENCHMARK(BM_string_by_reference_with_copy)->Range(0, 1 << 20);

static void BM_string_by_value(benchmark::State& state) {
  std::string arg(state.range(0), 'x');
  for (auto _ : state) {
    StringByValueRoutine(arg);
  }
}
BENCHMARK(BM_string_by_value)->Range(0, 1 << 20);

//
// 3 benchmarks passing a *temporary* to a function
// - by const reference
// - by const reference and making a local copy
// - by value
//

// Helper function that is used to create a temporary.
std::string GetArg(int len) { return std::string(len, 'x'); }

static void BM_string_by_reference_from_temp(benchmark::State& state) {
  const int len = state.range(0);
  for (auto _ : state) {
    StringByReferenceRoutine(GetArg(len));
  }
}
BENCHMARK(BM_string_by_reference_from_temp)->Range(0, 1 << 20);

static void BM_string_by_reference_with_copy_from_temp(
    benchmark::State& state) {
  const int len = state.range(0);
  for (auto _ : state) {
    StringByReferenceWithCopyRoutine(GetArg(len));
  }
}
BENCHMARK(BM_string_by_reference_with_copy_from_temp)->Range(0, 1 << 20);

static void BM_string_by_value_from_temp(benchmark::State& state) {
  const int len = state.range(0);
  for (auto _ : state) {
    StringByValueRoutine(GetArg(len));
  }
}
BENCHMARK(BM_string_by_value_from_temp)->Range(0, 1 << 20);

static void BM_StringAppend(benchmark::State& state) {
  const int len = state.range(0);
  std::string x;
  std::string arg(len + 1, 'x');
  for (auto _ : state) {
    x.clear();
    x += 'c';  // To prevent alignment
    x.append(arg, 1, len);
  }
}
BENCHMARK(BM_StringAppend)->Range(0, 1 << 20);

static std::string ReturnStringRoutine(int len) {
  // Require Named Return Value Optimization.
  std::string result(len, 'x');
  return result;
}

static std::string ReturnStringRVORoutine(int len) {
  // Allow non-Named Return Value Optimization.
  return std::string(len, 'x');
}

static void StringOutParamRoutine(int len, std::string* s) {
  s->assign(len, 'x');
}

static void BM_return_string(benchmark::State& state) {
  const int len = state.range(0);
  for (auto _ : state) {
    std::string result = ReturnStringRoutine(len);
    if (len > 0) {
      // Force copy if necessary
      result[0] = 'a';
    }
  }
}
BENCHMARK(BM_return_string)->Range(0, 1 << 20);

static void BM_assign_return_string(benchmark::State& state) {
  const int len = state.range(0);
  for (auto _ : state) {
    std::string result;
    result = ReturnStringRoutine(len);
    if (len > 0) {
      // Force copy if necessary
      result[0] = 'a';
    }
  }
}
BENCHMARK(BM_assign_return_string)->Range(0, 1 << 20);

static void BM_swap_return_string(benchmark::State& state) {
  const int len = state.range(0);
  for (auto _ : state) {
    std::string result;
    ReturnStringRoutine(len).swap(result);
    if (len > 0) {
      // Force copy if necessary
      result[0] = 'a';
    }
  }
}
BENCHMARK(BM_swap_return_string)->Range(0, 1 << 20);

static void BM_return_string_rvo(benchmark::State& state) {
  const int len = state.range(0);
  for (auto _ : state) {
    std::string result = ReturnStringRVORoutine(len);
    if (len > 0) {
      // Force copy if necessary
      result[0] = 'a';
    }
  }
}
BENCHMARK(BM_return_string_rvo)->Range(0, 1 << 20);

static void BM_return_const_ref_string(benchmark::State& state) {
  const int len = state.range(0);
  [[maybe_unused]] char c;
  for (auto _ : state) {
    const std::string& result = ReturnStringRoutine(len);
    if (len > 0) {
      // Shut up compiler
      c = result[0];
    }
  }
}
BENCHMARK(BM_return_const_ref_string)->Range(0, 1 << 20);

static void BM_return_string_out_param(benchmark::State& state) {
  const int len = state.range(0);
  for (auto _ : state) {
    std::string result;
    StringOutParamRoutine(len, &result);
    if (len > 0) {
      // Force copy if necessary
      result[0] = 'a';
    }
  }
}
BENCHMARK(BM_return_string_out_param)->Range(0, 1 << 20);

static void BM_return_string_out_param_noalloc(benchmark::State& state) {
  const int len = state.range(0);
  std::string result;
  for (auto _ : state) {
    StringOutParamRoutine(len, &result);
    if (len > 0) {
      // Force copy if necessary
      result[0] = 'a';
    }
  }
}
BENCHMARK(BM_return_string_out_param_noalloc)->Range(0, 1 << 20);

TEST(stringtest, strcount) {
  const std::string buf("/i/married/an/aardvark/by/accident/");
  ASSERT_EQ(strcount(buf, '/'), 7);
  ASSERT_EQ(strcount(buf.c_str(), '/'), 7);
  ASSERT_EQ(strcount(buf.c_str(), buf.size(), '/'), 7);
  ASSERT_EQ(strcount(buf.c_str(), buf.c_str() + buf.size(), '/'), 7);

  ASSERT_EQ(strcount(buf.c_str(), 5, '/'),
            2);  // stops after 5 chars.
  ASSERT_EQ(strcount(buf + std::string(1, '\0') + "/mr/khawaja/", '/'),
            10);  // goes past null char.
}

TEST(stringtest, StrCntStringViewEmpty) {
  // string_view can be default-initialised, resulting in an empty
  // string (with a nullpointer behind data())
  constexpr absl::string_view kNullView;
  constexpr absl::string_view kEmptyView("");
  for (int c = 0; c <= 0xff; ++c) {
    EXPECT_EQ(strcount(kNullView, static_cast<char>(c)), 0);
    EXPECT_EQ(strcount(kEmptyView, static_cast<char>(c)), 0);
  }
}

TEST(stringtest, StrCntStringView) {
  EXPECT_EQ(strcount(absl::string_view("Hello World!"), 'z'), 0);
  EXPECT_EQ(strcount(absl::string_view("Hello World!"), ' '), 1);
  EXPECT_EQ(strcount(absl::string_view("Hello World!"), 'o'), 2);
  EXPECT_EQ(strcount(absl::string_view("Hello World!"), 'l'), 3);
}

TEST(stringtest, SafeSnprintf) {
  const int kBufferSize = 10;
  char buffer[kBufferSize + 1];
  buffer[kBufferSize] = 0x55;  // magic number to detect buffer overrun

  // zero args
  int ret = SafeSnprintf(buffer, kBufferSize, "hello");
  EXPECT_EQ(5, ret);
  EXPECT_STREQ("hello", buffer);

  // one arg; barely fits in the buffer
  char arg_a[] = "123456789";
  ret = SafeSnprintf(buffer, kBufferSize, "%s", arg_a);
  EXPECT_EQ(9, ret);
  EXPECT_STREQ(arg_a, buffer);

  // one arg; slightly too big for the buffer
  char arg_b[] = "1234567890";
  ret = SafeSnprintf(buffer, kBufferSize, "%s", arg_b);
  EXPECT_EQ(0, ret);

  // multiple args
  char arg_c[] = "ans";
  char arg_d = '=';
  int arg_e = 42;
  ret = SafeSnprintf(buffer, kBufferSize, "(%s%c%d)", arg_c, arg_d, arg_e);
  EXPECT_EQ(8, ret);
  EXPECT_STREQ("(ans=42)", buffer);

  // verify we didn't write past end of buffer
  EXPECT_EQ(buffer[kBufferSize], 0x55) << "Buffer Overrun";
}

TEST(stringtest, CountSubstring) {
  EXPECT_EQ(2, CountSubstring("abcb", "b"));
  const std::string text = "123444444456789444";
  EXPECT_EQ(6, CountSubstring(text, "444"));
  ASSERT_DEATH(CountSubstring("abcd", ""), "");
}

TEST(stringtest, GetlineFromStdioFileOne) {
  std::string fname =
      ::testing::SrcDir() + "/_main/gloop/strings/testdata/getline-1.txt";
  FILE* fp = fopen(fname.c_str(), "r");
  CHECK(fp != nullptr) << fname;

  std::string str;
  EXPECT_TRUE(GetlineFromStdioFile(fp, &str, '\n'));
  EXPECT_EQ("alpha", str);
  EXPECT_TRUE(GetlineFromStdioFile(fp, &str, '\n'));
  EXPECT_EQ("", str);
  EXPECT_TRUE(GetlineFromStdioFile(fp, &str, '\n'));
  EXPECT_EQ("beta gamma", str);
  EXPECT_FALSE(GetlineFromStdioFile(fp, &str, '\n'));
  CHECK_EQ(fclose(fp), 0) << fname;
}

TEST(stringtest, TestIsPrintSingleChar) {
  for (int ci = 0; ci <= 255; ci++) {
    char c = static_cast<char>(ci);
    std::string s(1, c);
    EXPECT_EQ(absl::ascii_isprint(c), strings::IsPrint(s));
  }
}

TEST(stringtest, TestIsPrintInString) {
  for (int ci = 0; ci <= 255; ci++) {
    char c = static_cast<char>(ci);
    std::string t = absl::StrCat("abc", std::string(1, c), "def");
    EXPECT_EQ(absl::ascii_isprint(c), strings::IsPrint(t));
  }
}

TEST(stringtest, TestIsPrintNeverPrint) {
  for (int ci = 0; ci <= 255; ci++) {
    char c = static_cast<char>(ci);
    std::string t =
        absl::StrCat("abc", std::string(1, c), "de", std::string(1, '\0'));
    EXPECT_FALSE(strings::IsPrint(t));
  }
}

TEST(stringtest, GetlineFromStdioFileTwo) {
  std::string fname =
      ::testing::SrcDir() + "/_main/gloop/strings/testdata/getline-2.txt";
  FILE* fp = fopen(fname.c_str(), "r");
  CHECK(fp != nullptr) << fname;

  std::string str;
  EXPECT_TRUE(GetlineFromStdioFile(fp, &str, '.'));
  EXPECT_EQ("one", str);
  EXPECT_TRUE(GetlineFromStdioFile(fp, &str, '.'));
  EXPECT_EQ("two", str);
  EXPECT_FALSE(GetlineFromStdioFile(fp, &str, '.'));
  EXPECT_EQ("three", str.substr(0, 5));
  CHECK_EQ(fclose(fp), 0) << fname;
}

class PutTwoDigitsTest : public ::testing::TestWithParam<int> {};

TEST_P(PutTwoDigitsTest, RoundTrip) {
  char buffer[3];
  const int value = GetParam();
  PutTwoDigits(value, buffer);
  buffer[2] = '\0';
  int32_t resolved;
  EXPECT_TRUE(absl::numbers_internal::safe_strto32_base(buffer, &resolved, 10));
  EXPECT_EQ(value, resolved);
}
namespace {
INSTANTIATE_TEST_SUITE_P(FullRange, PutTwoDigitsTest, ::testing::Range(0, 100));
}

static void BM_PutTwoDigits(benchmark::State& state) {
  // Each iteration of the benchmark will resolve each of the numbers in the
  // 0..99 range.
  char buffer[2];
  for (auto _ : state) {
    for (int j = 0; j < 100; ++j) {
      PutTwoDigits(j, buffer);
      benchmark::DoNotOptimize(buffer);
    }
  }
  state.SetItemsProcessed(100 * state.iterations());
  state.SetBytesProcessed(100 * state.iterations() * 2);
}

BENCHMARK(BM_PutTwoDigits);

// Naive implementation of PutTwoDigits() for comparison.
inline void PutTwoDigitsNaive(size_t i, char* buf) {
  buf[0] = '0' + i / 10;
  buf[1] = '0' + i % 10;
}

static void BM_PutTwoDigitsNaive(benchmark::State& state) {
  // Each iteration of the benchmark will resolve each of the numbers in the
  // 0..99 range.
  char buffer[2];
  for (auto _ : state) {
    for (int j = 0; j < 100; ++j) {
      PutTwoDigitsNaive(j, buffer);
      benchmark::DoNotOptimize(buffer);
    }
  }
  state.SetItemsProcessed(100 * state.iterations());
  state.SetBytesProcessed(100 * state.iterations() * 2);
}

BENCHMARK(BM_PutTwoDigitsNaive);

// Ditto with no division
inline void PutTwoDigitsNaiveNoDivision(size_t i, char* buf) {
  size_t high = (i * 205) >> 11;  // same as i / 10, if i < 1029
  size_t low = i - high * 10;
  buf[0] = high + '0';
  buf[1] = low + '0';
}

static void BM_PutTwoDigitsNaiveNoDivision(benchmark::State& state) {
  // Each iteration of the benchmark will resolve each of the numbers in the
  // 0..99 range.
  char buffer[2];
  for (auto _ : state) {
    for (int j = 0; j < 100; ++j) {
      PutTwoDigitsNaiveNoDivision(j, buffer);
      benchmark::DoNotOptimize(buffer);
    }
  }
  state.SetItemsProcessed(100 * state.iterations());
  state.SetBytesProcessed(100 * state.iterations() * 2);
}

BENCHMARK(BM_PutTwoDigitsNaiveNoDivision);
