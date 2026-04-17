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

#include "gloop/strings/escaping.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/log_severity.h"
#include "absl/base/macros.h"
#include "absl/log/scoped_mock_log.h"
#include "absl/strings/charset.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace strings {
namespace {

using strings_internal::EncodeUTF8Char;
using strings_internal::kMaxEncodedUTF8Size;
using ::testing::_;
using ::testing::EndsWith;
using ::testing::Eq;

TEST(BackslashEscape, Escape) {
  struct Case {
    int line;
    const char* in;
    const char* escapes;
    const char* expect;
  };
  const Case cases[] = {
      {__LINE__, "foobar", "xyz", "foobar"},
      {__LINE__, "foobar", "r", "fooba\\r"},
      {__LINE__, "foobar", "xyzo", "f\\o\\obar"},
      {__LINE__, "", "", ""},
      {__LINE__, "", ":", ""},
      {__LINE__, "", "\\", ""},
      {__LINE__, "", "\\:", ""},
      {__LINE__, "\\", "", "\\"},
      {__LINE__, "\\", ":", "\\"},
      {__LINE__, "\\", "\\", "\\\\"},
      {__LINE__, "\\", "\\:", "\\\\"},
      {__LINE__, "\\\\", "", "\\\\"},
      {__LINE__, "\\\\", ":", "\\\\"},
      {__LINE__, "\\\\", "\\", "\\\\\\\\"},
      {__LINE__, "\\\\", "\\:", "\\\\\\\\"},
      {__LINE__, ":", "", ":"},
      {__LINE__, ":", ":", "\\:"},
      {__LINE__, ":", "\\", ":"},
      {__LINE__, ":", "\\:", "\\:"},
      {__LINE__, "\\:", "", "\\:"},
      {__LINE__, "\\:", ":", "\\\\:"},
      {__LINE__, "\\:", "\\", "\\\\:"},
      {__LINE__, "\\:", "\\:", "\\\\\\:"},
  };
  for (const Case* p = cases; p != cases + ABSL_ARRAYSIZE(cases); ++p) {
    EXPECT_EQ(p->expect, BackslashEscape(p->in, absl::CharSet(p->escapes)))
        << "BackslashEscape('" << p->in << "'" << ", '" << p->escapes
        << "') line:" << p->line;
  }
}

TEST(BackslashEscape, Unescape) {
  struct Case {
    int line;
    const char* in;
    const char* escapes;
    const char* expect;
  };
  const Case cases[] = {
      {__LINE__, "f\\o\\ob\\ar", "xyz", "f\\o\\ob\\ar"},
      {__LINE__, "f\\o\\ob\\ar", "xyzo", "foob\\ar"},
      {__LINE__, "", "", ""},
      {__LINE__, "", ":", ""},
      {__LINE__, "", "\\", ""},
      {__LINE__, "", "\\:", ""},
      {__LINE__, "\\", "", "\\"},
      {__LINE__, "\\", ":", "\\"},
      {__LINE__, "\\", "\\", "\\"},
      {__LINE__, "\\", "\\:", "\\"},
      {__LINE__, "\\\\", "", "\\\\"},
      {__LINE__, "\\\\", ":", "\\\\"},
      {__LINE__, "\\\\", "\\", "\\"},
      {__LINE__, "\\\\", "\\:", "\\"},
      {__LINE__, "\\\\\\", "", "\\\\\\"},
      {__LINE__, "\\\\\\", ":", "\\\\\\"},
      {__LINE__, "\\\\\\", "\\", "\\\\"},
      {__LINE__, "\\\\\\", "\\:", "\\\\"},
      {__LINE__, "\\\\\\\\", "", "\\\\\\\\"},
      {__LINE__, "\\\\\\\\", ":", "\\\\\\\\"},
      {__LINE__, "\\\\\\\\", "\\", "\\\\"},
      {__LINE__, "\\\\\\\\", "\\:", "\\\\"},
      {__LINE__, ":", "", ":"},
      {__LINE__, ":", ":", ":"},
      {__LINE__, ":", "\\", ":"},
      {__LINE__, ":", "\\:", ":"},
      {__LINE__, "\\:", "", "\\:"},
      {__LINE__, "\\:", ":", ":"},
      {__LINE__, "\\:", "\\", "\\:"},
      {__LINE__, "\\:", "\\:", ":"},
      {__LINE__, "\\a", "", "\\a"},
      {__LINE__, "\\a", ":", "\\a"},
      {__LINE__, "\\a", "\\", "\\a"},
      {__LINE__, "\\a", "\\:", "\\a"},
      {__LINE__, "a\\", "", "a\\"},
      {__LINE__, "a\\", ":", "a\\"},
      {__LINE__, "a\\", "\\", "a\\"},
      {__LINE__, "a\\", "\\:", "a\\"},
      {__LINE__, "\\\\:", "", "\\\\:"},
      {__LINE__, "\\\\:", ":", "\\:"},
      {__LINE__, "\\\\:", "\\", "\\:"},
      {__LINE__, "\\\\:", "\\:", "\\:"},
      {__LINE__, "\\\\\\:", "", "\\\\\\:"},
      {__LINE__, "\\\\\\:", ":", "\\\\:"},
      {__LINE__, "\\\\\\:", "\\", "\\\\:"},
      {__LINE__, "\\\\\\:", "\\:", "\\:"},
  };
  for (const Case* p = cases; p != cases + ABSL_ARRAYSIZE(cases); ++p) {
    EXPECT_EQ(p->expect, BackslashUnescape(p->in, absl::CharSet(p->escapes)))
        << "BackslashUnescape('" << p->in << "'" << ", '" << p->escapes
        << "') line:" << p->line;
  }
}

TEST(BackslashEscape, UnescapedFind) {
  struct Case {
    int line;
    const char* in;
    const char* escapes;
    std::basic_string_view<char>::difference_type expect;
  };
  const std::basic_string_view<char>::difference_type npos =
      absl::string_view::npos;
  const Case cases[] = {
      {__LINE__, "", "", npos},
      {__LINE__, "abc", "a", 0},
      {__LINE__, "abc", "b", 1},
      {__LINE__, "abc", "c", 2},
      {__LINE__, "\\abc", "a", npos},
      {__LINE__, "a\\bc", "b", npos},
      {__LINE__, "ab\\c", "c", npos},
      {__LINE__, "\\\\ab\\c", "a", 2},
      {__LINE__, "\\abc", "b", 2},
      {__LINE__, "a\\bc", "c", 3},
      {__LINE__, "\\ab", "\\", 0},
      {__LINE__, "a\\b", "\\", 1},
      {__LINE__, "ab\\", "\\", 2},
      {__LINE__, "ab", "\\", npos},
      {__LINE__, "\\\\ab", "\\", npos},
      {__LINE__, "a\\\\b", "\\", npos},
      {__LINE__, "ab\\\\", "\\", npos},
      {__LINE__, "\\\\\\ab", "\\", 2},
      {__LINE__, "a\\\\\\b", "\\", 3},
      {__LINE__, "ab\\\\\\", "\\", 4},
      {__LINE__, "", "", npos},
      {__LINE__, "", ":", npos},
      {__LINE__, "", "\\", npos},
      {__LINE__, "", "\\:", npos},
      {__LINE__, "\\", "", npos},
      {__LINE__, "\\", ":", npos},
      {__LINE__, "\\", "\\", 0},
      {__LINE__, "\\", "\\:", 0},
      {__LINE__, "\\\\", "", npos},
      {__LINE__, "\\\\", ":", npos},
      {__LINE__, "\\\\", "\\", npos},
      {__LINE__, "\\\\", "\\:", npos},
      {__LINE__, "\\\\\\", "", npos},
      {__LINE__, "\\\\\\", ":", npos},
      {__LINE__, "\\\\\\", "\\", 2},
      {__LINE__, "\\\\\\", "\\:", 2},
      {__LINE__, "\\\\\\\\", "", npos},
      {__LINE__, "\\\\\\\\", ":", npos},
      {__LINE__, "\\\\\\\\", "\\", npos},   // 2 escaped delimiters
      {__LINE__, "\\\\\\\\", "\\:", npos},  // 2 escaped delimiters
      {__LINE__, ":", "", npos},
      {__LINE__, ":", ":", 0},
      {__LINE__, ":", "\\", npos},
      {__LINE__, ":", "\\:", 0},
      {__LINE__, "\\:", "", npos},
      {__LINE__, "\\:", ":", npos},  // 1 escaped delimiter
      {__LINE__, "\\:", "\\", 0},
      {__LINE__, "\\:", "\\:", npos},  // 1 escaped delimiter
      {__LINE__, "\\a", "", npos},
      {__LINE__, "\\a", ":", npos},
      {__LINE__, "\\a", "\\", 0},
      {__LINE__, "\\a", "\\:", 0},
      {__LINE__, "a\\", "", npos},
      {__LINE__, "a\\", ":", npos},
      {__LINE__, "a\\", "\\", 1},
      {__LINE__, "a\\", "\\:", 1},
      {__LINE__, "\\\\:", "", npos},
      {__LINE__, "\\\\:", ":", 2},
      {__LINE__, "\\\\:", "\\", npos},
      {__LINE__, "\\\\:", "\\:", 2},
      {__LINE__, "\\\\\\:", "", npos},
      {__LINE__, "\\\\\\:", ":", npos},
      {__LINE__, "\\\\\\:", "\\", 2},
      {__LINE__, "\\\\\\:", "\\:", npos},  // 2 escaped delimiters
  };
  for (const Case* p = cases; p != cases + ABSL_ARRAYSIZE(cases); ++p) {
    EXPECT_EQ(p->expect,
              BackslashUnescapedFind(p->in, absl::CharSet(p->escapes)))
        << "BackslashUnescapedFind('" << p->in << "'" << ", '" << p->escapes
        << "') line:" << p->line;
  }
}

std::string RoundTrip(absl::string_view src, const absl::CharSet& escapes) {
  std::string encoded = BackslashEscape(src, escapes);
  return BackslashUnescape(encoded, escapes);
}

TEST(BackslashEscape, RoundTrip) {
  std::string tests[] = {
      "",   "\\",   "\\\\",   "\\\\\\",   "\\\\\\\\",
      ":",  "\\:",  "\\\\:",  "\\\\\\:",  "\\\\\\\\:",
      ":a", "\\:a", "\\\\:a", "\\\\\\:a", "\\\\\\\\:a",
  };
  for (const std::string& src : tests) {
    EXPECT_EQ(src, RoundTrip(src, absl::CharSet::Char(':')));
    EXPECT_EQ(src, RoundTrip(src, absl::CharSet(":\\")));
  }
}

TEST(BackslashEscape, UsageExample) {
  static constexpr absl::CharSet kDelims = absl::CharSet(":\\");

  // Example 1:
  //  Join arbitrary string fields with ':'.
  //  Any ':' and '\\' occurring in any of the fields will be escaped.
  //  Backslashes have to be escaped to prevent backslashes in the input from
  //  changing the output.

  const std::string arr[] = {"a", "bc\\", "", "12:30", "xyz"};
  std::vector<std::string> fields(arr, arr + ABSL_ARRAYSIZE(arr));

  std::string encoded;

  {
    const char* sep = "";
    for (const std::string& field : fields) {
      absl::StrAppend(&encoded, sep);
      BackslashEscape(field, kDelims, &encoded);
      sep = ":";
    }
  }

  EXPECT_EQ("a:bc\\\\::12\\:30:xyz", encoded);

  // Example 2:
  //  Find the field boundaries in such an encoded string.

  std::vector<absl::string_view> encoded_fields;

  {
    absl::string_view encoded_sp = encoded;
    while (!encoded_sp.empty()) {
      std::basic_string_view<char>::difference_type pos =
          BackslashUnescapedFind(encoded_sp, kDelims);
      if (pos == absl::string_view::npos) {
        pos = encoded_sp.size();
      }
      encoded_fields.push_back(encoded_sp.substr(0, pos));
      if (pos < encoded_sp.size()) {
        ++pos;
      }
      encoded_sp.remove_prefix(pos);
    }
  }

  EXPECT_THAT(encoded_fields,
              testing::ElementsAre("a", "bc\\\\", "", "12\\:30", "xyz"));

  // Example 3:
  //  Unescape the fields identified in Example 2.

  std::vector<std::string> decoded_fields;

  {
    for (absl::string_view enc : encoded_fields) {
      std::string f;
      BackslashUnescape(enc, kDelims, &f);
      decoded_fields.push_back(f);
    }
  }

  EXPECT_EQ(fields, decoded_fields);
}

TEST(BackslashEscape, BackslashFreeUsageExample) {
  constexpr unsigned char kDelim = ':';

  // Example 1:
  //  Join backslash-free string fields with ':'.
  //  Any ':' occurring in any of the fields will be escaped.

  const std::string arr[] = {"a", "bc", "", "12:30", "xyz"};
  std::vector<std::string> fields(arr, arr + ABSL_ARRAYSIZE(arr));

  std::string encoded;

  {
    const char* sep = "";
    for (const std::string& field : fields) {
      absl::StrAppend(&encoded, sep);
      BackslashEscape(field, kDelim, &encoded);
      sep = ":";
    }
  }

  EXPECT_EQ("a:bc::12\\:30:xyz", encoded);

  // Example 2:
  //  Find the field boundaries in such an encoded string.

  std::vector<absl::string_view> encoded_fields;

  {
    absl::string_view encoded_sp = encoded;
    while (!encoded_sp.empty()) {
      std::basic_string_view<char>::difference_type pos =
          BackslashUnescapedFind(encoded_sp, kDelim);
      if (pos == absl::string_view::npos) {
        pos = encoded_sp.size();
      }
      encoded_fields.push_back(encoded_sp.substr(0, pos));
      if (pos < encoded_sp.size()) {
        ++pos;
      }
      encoded_sp.remove_prefix(pos);
    }
  }

  EXPECT_THAT(encoded_fields,
              testing::ElementsAre("a", "bc", "", "12\\:30", "xyz"));

  // Example 3:
  //  Unescape the fields identified in Example 2.

  std::vector<std::string> decoded_fields;

  {
    for (absl::string_view enc : encoded_fields) {
      std::string f;
      BackslashUnescape(enc, kDelim, &f);
      decoded_fields.push_back(f);
    }
  }

  EXPECT_EQ(fields, decoded_fields);
}

TEST(BackslashEscape, SingleChar) {
  constexpr absl::string_view start = "this is a string";
  std::string after = BackslashEscape(start, ' ');
  ASSERT_THAT(after, Eq("this\\ is\\ a\\ string"));
  EXPECT_THAT(BackslashUnescape(after, ' '), Eq(start));
}

TEST(EscapeStrForCSV, BasicFunctions) {
  char outbuf[128];

  // No quotes test.
  //                         0    0    1    1    2    2    3    3
  //                         0    5    0    5    0    5    0    5
  EXPECT_EQ(strings::EscapeStrForCSV("some quoteless string, just to test",
                                     outbuf, 36),
            35);
  EXPECT_STREQ(outbuf, "some quoteless string, just to test");
  // error case: off by one
  EXPECT_EQ(strings::EscapeStrForCSV("some quoteless string, just to test",
                                     outbuf, 35),
            -1);

  // Quotes tests.
  EXPECT_EQ(strings::EscapeStrForCSV("some \"string\" to test", outbuf, 24),
            23);
  EXPECT_STREQ(outbuf, "some \"\"string\"\" to test");

  // error case: off by one
  EXPECT_EQ(strings::EscapeStrForCSV("some \"string\" to test", outbuf, 23),
            -1);

  // Shows pathological output length behavior (2*input size + 1)
  EXPECT_EQ(strings::EscapeStrForCSV("\"\"\"\"\"", outbuf, 10), -1);
  EXPECT_EQ(strings::EscapeStrForCSV("\"\"\"\"\"", outbuf, 11), 10);
  EXPECT_STREQ(outbuf, "\"\"\"\"\"\"\"\"\"\"");

  // error case: off by one
  EXPECT_EQ(strings::EscapeStrForCSV("\"\"\"\"\"", outbuf, 10), -1);

  // Quotes+Spaces tests
  EXPECT_EQ(strings::EscapeStrForCSV("   \"   \"   \"   ", outbuf, 19), 18);
  EXPECT_STREQ(outbuf, "   \"\"   \"\"   \"\"   ");

  // error case: off by one
  EXPECT_EQ(strings::EscapeStrForCSV("   \"   \"   \"   ", outbuf, 18), -1);

  // error case: null destination, 0 dest_len.
  char* null_dest = nullptr;
  EXPECT_EQ(strings::EscapeStrForCSV("Something \" string", null_dest, 0), -1);
}

TEST(EncodeUTF8Char, BasicFunction) {
  std::pair<char32_t, std::string> tests[] = {{0x0030, "\u0030"},
                                              {0x00A3, "\u00A3"},
                                              {0x00010000, "\U00010000"},
                                              {0x0000FFFF, "\U0000FFFF"},
                                              {0x0010FFFD, "\U0010FFFD"}};
  for (auto& test : tests) {
    char buf0[7] = {'\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00'};
    char buf1[7] = {'\xFF', '\xFF', '\xFF', '\xFF', '\xFF', '\xFF', '\xFF'};
    char* buf0_written = &buf0[EncodeUTF8Char(buf0, test.first)];
    char* buf1_written = &buf1[EncodeUTF8Char(buf1, test.first)];
    int apparent_length = 7;
    while (buf0[apparent_length - 1] == '\x00' &&
           buf1[apparent_length - 1] == '\xFF') {
      if (--apparent_length == 0) break;
    }
    EXPECT_EQ(apparent_length, buf0_written - buf0);
    EXPECT_EQ(apparent_length, buf1_written - buf1);
    EXPECT_EQ(apparent_length, test.second.length());
    EXPECT_EQ(std::string(buf0, apparent_length), test.second);
    EXPECT_EQ(std::string(buf1, apparent_length), test.second);
  }
  char buf[32] = "Don't Tread On Me";
  EXPECT_LE(EncodeUTF8Char(buf, 0x00110000), kMaxEncodedUTF8Size);
  char buf2[32] = "Negative is invalid but sane";
  EXPECT_LE(EncodeUTF8Char(buf2, -1), kMaxEncodedUTF8Size);
}

static void BM_EncodeUTF8Char(benchmark::State& state) {
  char buffer[32];
  char32_t rune = 0;
  for (auto _ : state) {
    ++rune;
    if (rune > 0x10FFFF) rune = 0;
    EncodeUTF8Char(buffer, rune);
  }
}
BENCHMARK(BM_EncodeUTF8Char);

struct epair {
  std::string escaped;
  std::string unescaped;
};

static struct {
  absl::string_view plaintext;
  absl::string_view cyphertext;
} const base64_tests[] = {
    // Empty string.
    {{"", 0}, {"", 0}},
    {{nullptr, 0},
     {"", 0}},  // if length is zero, plaintext ptr must be ignored!

    // Basic bit patterns;
    // values obtained with "echo -n '...' | uuencode -m test", with padding
    // removed

    {{"\000", 1}, "AA"},
    {{"\001", 1}, "AQ"},
    {{"\002", 1}, "Ag"},
    {{"\004", 1}, "BA"},
    {{"\010", 1}, "CA"},
    {{"\020", 1}, "EA"},
    {{"\040", 1}, "IA"},
    {{"\100", 1}, "QA"},
    {{"\200", 1}, "gA"},

    {{"\377", 1}, "/w"},
    {{"\376", 1}, "/g"},
    {{"\375", 1}, "/Q"},
    {{"\373", 1}, "+w"},
    {{"\367", 1}, "9w"},
    {{"\357", 1}, "7w"},
    {{"\337", 1}, "3w"},
    {{"\277", 1}, "vw"},
    {{"\177", 1}, "fw"},
    {{"\000\000", 2}, "AAA"},
    {{"\000\001", 2}, "AAE"},
    {{"\000\002", 2}, "AAI"},
    {{"\000\004", 2}, "AAQ"},
    {{"\000\010", 2}, "AAg"},
    {{"\000\020", 2}, "ABA"},
    {{"\000\040", 2}, "ACA"},
    {{"\000\100", 2}, "AEA"},
    {{"\000\200", 2}, "AIA"},
    {{"\001\000", 2}, "AQA"},
    {{"\002\000", 2}, "AgA"},
    {{"\004\000", 2}, "BAA"},
    {{"\010\000", 2}, "CAA"},
    {{"\020\000", 2}, "EAA"},
    {{"\040\000", 2}, "IAA"},
    {{"\100\000", 2}, "QAA"},
    {{"\200\000", 2}, "gAA"},

    {{"\377\377", 2}, "//8"},
    {{"\377\376", 2}, "//4"},
    {{"\377\375", 2}, "//0"},
    {{"\377\373", 2}, "//s"},
    {{"\377\367", 2}, "//c"},
    {{"\377\357", 2}, "/+8"},
    {{"\377\337", 2}, "/98"},
    {{"\377\277", 2}, "/78"},
    {{"\377\177", 2}, "/38"},
    {{"\376\377", 2}, "/v8"},
    {{"\375\377", 2}, "/f8"},
    {{"\373\377", 2}, "+/8"},
    {{"\367\377", 2}, "9/8"},
    {{"\357\377", 2}, "7/8"},
    {{"\337\377", 2}, "3/8"},
    {{"\277\377", 2}, "v/8"},
    {{"\177\377", 2}, "f/8"},

    {{"\000\000\000", 3}, "AAAA"},
    {{"\000\000\001", 3}, "AAAB"},
    {{"\000\000\002", 3}, "AAAC"},
    {{"\000\000\004", 3}, "AAAE"},
    {{"\000\000\010", 3}, "AAAI"},
    {{"\000\000\020", 3}, "AAAQ"},
    {{"\000\000\040", 3}, "AAAg"},
    {{"\000\000\100", 3}, "AABA"},
    {{"\000\000\200", 3}, "AACA"},
    {{"\000\001\000", 3}, "AAEA"},
    {{"\000\002\000", 3}, "AAIA"},
    {{"\000\004\000", 3}, "AAQA"},
    {{"\000\010\000", 3}, "AAgA"},
    {{"\000\020\000", 3}, "ABAA"},
    {{"\000\040\000", 3}, "ACAA"},
    {{"\000\100\000", 3}, "AEAA"},
    {{"\000\200\000", 3}, "AIAA"},
    {{"\001\000\000", 3}, "AQAA"},
    {{"\002\000\000", 3}, "AgAA"},
    {{"\004\000\000", 3}, "BAAA"},
    {{"\010\000\000", 3}, "CAAA"},
    {{"\020\000\000", 3}, "EAAA"},
    {{"\040\000\000", 3}, "IAAA"},
    {{"\100\000\000", 3}, "QAAA"},
    {{"\200\000\000", 3}, "gAAA"},

    {{"\377\377\377", 3}, "////"},
    {{"\377\377\376", 3}, "///+"},
    {{"\377\377\375", 3}, "///9"},
    {{"\377\377\373", 3}, "///7"},
    {{"\377\377\367", 3}, "///3"},
    {{"\377\377\357", 3}, "///v"},
    {{"\377\377\337", 3}, "///f"},
    {{"\377\377\277", 3}, "//+/"},
    {{"\377\377\177", 3}, "//9/"},
    {{"\377\376\377", 3}, "//7/"},
    {{"\377\375\377", 3}, "//3/"},
    {{"\377\373\377", 3}, "//v/"},
    {{"\377\367\377", 3}, "//f/"},
    {{"\377\357\377", 3}, "/+//"},
    {{"\377\337\377", 3}, "/9//"},
    {{"\377\277\377", 3}, "/7//"},
    {{"\377\177\377", 3}, "/3//"},
    {{"\376\377\377", 3}, "/v//"},
    {{"\375\377\377", 3}, "/f//"},
    {{"\373\377\377", 3}, "+///"},
    {{"\367\377\377", 3}, "9///"},
    {{"\357\377\377", 3}, "7///"},
    {{"\337\377\377", 3}, "3///"},
    {{"\277\377\377", 3}, "v///"},
    {{"\177\377\377", 3}, "f///"},

    // Random numbers: values obtained with
    //
    //  #! /bin/bash
    //  dd bs=$1 count=1 if=/dev/random of=/tmp/bar.random
    //  od -N $1 -t o1 /tmp/bar.random
    //  uuencode -m test < /tmp/bar.random
    //
    // where $1 is the number of bytes (2, 3)
    // Padding has been removed

    {{"\243\361", 2}, "o/E"},
    {{"\024\167", 2}, "FHc"},
    {{"\313\252", 2}, "y6o"},
    {{"\046\041", 2}, "JiE"},
    {{"\145\236", 2}, "ZZ4"},
    {{"\254\325", 2}, "rNU"},
    {{"\061\330", 2}, "Mdg"},
    {{"\245\032", 2}, "pRo"},
    {{"\006\000", 2}, "BgA"},
    {{"\375\131", 2}, "/Vk"},
    {{"\303\210", 2}, "w4g"},
    {{"\040\037", 2}, "IB8"},
    {{"\261\372", 2}, "sfo"},
    {{"\335\014", 2}, "3Qw"},
    {{"\233\217", 2}, "m48"},
    {{"\373\056", 2}, "+y4"},
    {{"\247\232", 2}, "p5o"},
    {{"\107\053", 2}, "Rys"},
    {{"\204\077", 2}, "hD8"},
    {{"\276\211", 2}, "vok"},
    {{"\313\110", 2}, "y0g"},
    {{"\363\376", 2}, "8/4"},
    {{"\251\234", 2}, "qZw"},
    {{"\103\262", 2}, "Q7I"},
    {{"\142\312", 2}, "Yso"},
    {{"\067\211", 2}, "N4k"},
    {{"\220\001", 2}, "kAE"},
    {{"\152\240", 2}, "aqA"},
    {{"\367\061", 2}, "9zE"},
    {{"\133\255", 2}, "W60"},
    {{"\176\035", 2}, "fh0"},
    {{"\032\231", 2}, "Gpk"},

    {{"\013\007\144", 3}, "Cwdk"},
    {{"\030\112\106", 3}, "GEpG"},
    {{"\047\325\046", 3}, "J9Um"},
    {{"\310\160\022", 3}, "yHAS"},
    {{"\131\100\237", 3}, "WUCf"},
    {{"\064\342\134", 3}, "NOJc"},
    {{"\010\177\004", 3}, "CH8E"},
    {{"\345\147\205", 3}, "5WeF"},
    {{"\300\343\360", 3}, "wOPw"},
    {{"\061\240\201", 3}, "MaCB"},
    {{"\225\333\044", 3}, "ldsk"},
    {{"\215\137\352", 3}, "jV/q"},
    {{"\371\147\160", 3}, "+Wdw"},
    {{"\030\320\051", 3}, "GNAp"},
    {{"\044\174\241", 3}, "JHyh"},
    {{"\260\127\037", 3}, "sFcf"},
    {{"\111\045\033", 3}, "SSUb"},
    {{"\202\114\107", 3}, "gkxH"},
    {{"\057\371\042", 3}, "L/ki"},
    {{"\223\247\244", 3}, "k6ek"},
    {{"\047\216\144", 3}, "J45k"},
    {{"\203\070\327", 3}, "gzjX"},
    {{"\247\140\072", 3}, "p2A6"},
    {{"\124\115\116", 3}, "VE1O"},
    {{"\157\162\050", 3}, "b3Io"},
    {{"\357\223\004", 3}, "75ME"},
    {{"\052\117\156", 3}, "Kk9u"},
    {{"\347\154\000", 3}, "52wA"},
    {{"\303\012\142", 3}, "wwpi"},
    {{"\060\035\362", 3}, "MB3y"},
    {{"\130\226\361", 3}, "WJbx"},
    {{"\173\013\071", 3}, "ews5"},
    {{"\336\004\027", 3}, "3gQX"},
    {{"\357\366\234", 3}, "7/ac"},
    {{"\353\304\111", 3}, "68RJ"},
    {{"\024\264\131", 3}, "FLRZ"},
    {{"\075\114\251", 3}, "PUyp"},
    {{"\315\031\225", 3}, "zRmV"},
    {{"\154\201\276", 3}, "bIG+"},
    {{"\200\066\072", 3}, "gDY6"},
    {{"\142\350\267", 3}, "Yui3"},
    {{"\033\000\166", 3}, "GwB2"},
    {{"\210\055\077", 3}, "iC0/"},
    {{"\341\037\124", 3}, "4R9U"},
    {{"\161\103\152", 3}, "cUNq"},
    {{"\270\142\131", 3}, "uGJZ"},
    {{"\337\076\074", 3}, "3z48"},
    {{"\375\106\362", 3}, "/Uby"},
    {{"\227\301\127", 3}, "l8FX"},
    {{"\340\002\234", 3}, "4AKc"},
    {{"\121\064\033", 3}, "UTQb"},
    {{"\157\134\143", 3}, "b1xj"},
    {{"\247\055\327", 3}, "py3X"},
    {{"\340\142\005", 3}, "4GIF"},
    {{"\060\260\143", 3}, "MLBj"},
    {{"\075\203\170", 3}, "PYN4"},
    {{"\143\160\016", 3}, "Y3AO"},
    {{"\313\013\063", 3}, "ywsz"},
    {{"\174\236\135", 3}, "fJ5d"},
    {{"\103\047\026", 3}, "QycW"},
    {{"\365\005\343", 3}, "9QXj"},
    {{"\271\160\223", 3}, "uXCT"},
    {{"\362\255\172", 3}, "8q16"},
    {{"\113\012\015", 3}, "SwoN"},

    // various lengths, generated by this python script:
    //
    // from std::string import lowercase as lc
    // for i in range(27):
    //   print '{ %2d, "%s",%s "%s" },' % (i, lc[:i], ' ' * (26-i),
    //                                     lc[:i].encode('base64').strip())
    // Padding has been removed

    {{"", 0}, {"", 0}},
    {"a", "YQ"},
    {"ab", "YWI"},
    {"abc", "YWJj"},
    {"abcd", "YWJjZA"},
    {"abcde", "YWJjZGU"},
    {"abcdef", "YWJjZGVm"},
    {"abcdefg", "YWJjZGVmZw"},
    {"abcdefgh", "YWJjZGVmZ2g"},
    {"abcdefghi", "YWJjZGVmZ2hp"},
    {"abcdefghij", "YWJjZGVmZ2hpag"},
    {"abcdefghijk", "YWJjZGVmZ2hpams"},
    {"abcdefghijkl", "YWJjZGVmZ2hpamts"},
    {"abcdefghijklm", "YWJjZGVmZ2hpamtsbQ"},
    {"abcdefghijklmn", "YWJjZGVmZ2hpamtsbW4"},
    {"abcdefghijklmno", "YWJjZGVmZ2hpamtsbW5v"},
    {"abcdefghijklmnop", "YWJjZGVmZ2hpamtsbW5vcA"},
    {"abcdefghijklmnopq", "YWJjZGVmZ2hpamtsbW5vcHE"},
    {"abcdefghijklmnopqr", "YWJjZGVmZ2hpamtsbW5vcHFy"},
    {"abcdefghijklmnopqrs", "YWJjZGVmZ2hpamtsbW5vcHFycw"},
    {"abcdefghijklmnopqrst", "YWJjZGVmZ2hpamtsbW5vcHFyc3Q"},
    {"abcdefghijklmnopqrstu", "YWJjZGVmZ2hpamtsbW5vcHFyc3R1"},
    {"abcdefghijklmnopqrstuv", "YWJjZGVmZ2hpamtsbW5vcHFyc3R1dg"},
    {"abcdefghijklmnopqrstuvw", "YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnc"},
    {"abcdefghijklmnopqrstuvwx", "YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4"},
    {"abcdefghijklmnopqrstuvwxy", "YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eQ"},
    {"abcdefghijklmnopqrstuvwxyz", "YWJjZGVmZ2hpamtsbW5vcHFyc3R1dnd4eXo"},
};

TEST(Base64, EscapeAndUnescape) {
  // Check the short strings; this tests the math (and boundaries)
  for (const auto& tc : base64_tests) {
    std::string encoded("this junk should be ignored");
    LegacyBase64EscapeWithoutPadding(tc.plaintext, &encoded);
    EXPECT_EQ(encoded, tc.cyphertext);
  }
}

std::vector<epair> AppendEpairsOfInterestingLengths(std::vector<epair> vec) {
  std::string tmpl = "1234567890abcdef";
  constexpr int kMinNumBits = 3;
  constexpr int kMaxNumBits = 8;
  while (tmpl.size() < (1 << kMaxNumBits) + 1) {
    // Double the length.
    tmpl += tmpl;
  }

  for (int num_bits = kMinNumBits; num_bits <= kMaxNumBits; ++num_bits) {
    for (int offset : {-1, 0, 1}) {
      int len = (1 << num_bits) + offset;
      std::string str = tmpl.substr(0, len);
      vec.push_back({str, str});
    }
  }

  return vec;
}

TEST(Unescape, BasicFunction) {
  std::vector<epair> tests =
      AppendEpairsOfInterestingLengths({{"\\u0030", "0"},
                                        {"\\u00A3", "\xC2\xA3"},
                                        {"\\u22FD", "\xE2\x8B\xBD"},
                                        {"\\U00010000", "\xF0\x90\x80\x80"},
                                        {"\\U0010FFFD", "\xF4\x8F\xBF\xBD"}});

  for (const epair& val : tests) {
    for (bool same_buffer : {false, true}) {
      absl::ScopedMockLog mock_log;
      std::string out;
      if (same_buffer) out = val.escaped;
      mock_log.StartCapturingLogs();
      ptrdiff_t out_len =
          UnescapeCEscapeString(same_buffer ? out : val.escaped, &out);
      mock_log.StopCapturingLogs();
      EXPECT_EQ(out_len, out.size());
      // This is implicitly covered by the test after it, but it makes errors
      // more apparent than attempting to visually diff strings that may be
      // hundreds of bytes long and differ by one character added.
      EXPECT_EQ(val.unescaped.size(), out.size());
      EXPECT_EQ(out, val.unescaped);
    }
  }
  epair bad[] = {{"\\u1", ""},         // too short
                 {"\\U1", ""},         // too short
                 {"\\Uffffff", ""},    // exceeds 0x10ffff (largest Unicode)
                 {"\\U00110000", ""},  // exceeds 0x10ffff (largest Unicode)
                 {"\\uD835", ""},      // surrogate character (D800-DFFF)
                 {"\\U0000DD04", ""},  // surrogate character (D800-DFFF)
                 {"\\777", ""},        // exceeds 0xff
                 {"\\xABCD", ""},      // exceeds 0xff
                 {"\\U41Z", "Z"}};     // contains non-hex
  for (const epair& val : bad) {
    for (bool same_buffer : {false, true}) {
      absl::ScopedMockLog mock_log;
      EXPECT_CALL(mock_log,
                  Log(absl::LogSeverity::kError, EndsWith("/escaping.cc"), _));
      std::string out;
      if (same_buffer) out = val.escaped;
      mock_log.StartCapturingLogs();
      ptrdiff_t out_len =
          UnescapeCEscapeString(same_buffer ? out : val.escaped, &out);
      mock_log.StopCapturingLogs();
      EXPECT_EQ(out_len, out.size());
      // This is implicitly covered by the test after it, but it makes errors
      // more apparent than attempting to visually diff strings that may be
      // hundreds of bytes long and differ by one character added.
      EXPECT_EQ(val.unescaped.size(), out.size());
      EXPECT_EQ(out, val.unescaped) << val.escaped;
    }
  }
}

void BM_BackslashEscape(benchmark::State& state) {
  const int has_escapes = state.range(0);
  const int len = state.range(1);
  std::string src;
  for (int i = 0; i < len; i++) {
    src += 'A' + (i % 20);
  }
  absl::CharSet to_escape(has_escapes ? "A" : "xyz");
  state.SetLabel(has_escapes ? "escapes" : "nothing escaped");

  std::string result;
  int total_len = 0;
  for (auto _ : state) {
    result.clear();
    BackslashEscape(src, to_escape, &result);
    total_len += result.size();
  }
  benchmark::DoNotOptimize(total_len);
}
BENCHMARK(BM_BackslashEscape)
    ->ArgPair(0, 0)
    ->ArgPair(0, 10)
    ->ArgPair(0, 100)
    ->ArgPair(1, 0)
    ->ArgPair(1, 10)
    ->ArgPair(1, 100);

}  // namespace
}  // namespace strings
