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

#include "gloop/strings/string_view_utils.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gloop/gloop_test.h"
#include "re2/re2.h"

namespace {

TEST(StringPieceUtils, RemoveLeadingWhitespace) {
  std::string text = "  \t   \n  \r Quick\t";
  absl::string_view data(text);
  // check that all whitespace is removed
  EXPECT_EQ(strings::RemoveLeadingWhitespace(&data), 11);
  EXPECT_EQ(*(data.data()), 'Q');
  // check that non-whitespace is not removed
  EXPECT_EQ(strings::RemoveLeadingWhitespace(&data), 0);
  EXPECT_EQ(*(data.data()), 'Q');
  data.remove_prefix(sizeof("Quick") - 1);

  // check termination handling
  EXPECT_EQ(strings::RemoveLeadingWhitespace(&data), 1);
  EXPECT_EQ(data.size(), 0);

  // check termination handling again
  EXPECT_EQ(strings::RemoveLeadingWhitespace(&data), 0);
  EXPECT_EQ(data.size(), 0);
}

TEST(StringPieceUtils, RemoveTrailingWhitespace) {
  std::string text = "\tQuick  \t   \n  \r ";
  absl::string_view data(text);
  // check that all whitespace is removed
  EXPECT_EQ(strings::RemoveTrailingWhitespace(&data), 11);
  EXPECT_EQ(data[data.length() - 1], 'k');
  // check that non-whitespace is not removed
  EXPECT_EQ(strings::RemoveTrailingWhitespace(&data), 0);
  EXPECT_EQ(data[data.length() - 1], 'k');
  data.remove_suffix(sizeof("Quick") - 1);

  // check termination handling
  EXPECT_EQ(strings::RemoveTrailingWhitespace(&data), 1);
  EXPECT_EQ(data.size(), 0);

  // check termination handling again
  EXPECT_EQ(strings::RemoveTrailingWhitespace(&data), 0);
  EXPECT_EQ(data.size(), 0);

  // check that it works on null absl::string_view
  absl::string_view null_data;
  EXPECT_EQ(strings::RemoveTrailingWhitespace(&null_data), 0);
}

TEST(StringPieceUtils, RemoveWhitespaceContext) {
  std::string text = "  \t   \n  \r Quick Brown\t\r ";
  absl::string_view data(text);
  EXPECT_EQ(strings::RemoveWhitespaceContext(&data), 14);
}

TEST(StringPieceUtils, RemoveUntil) {
  std::string text = "The Quick Brown Fox\nJumped Over A\n\nLazy Dog";
  absl::string_view data(text);
  EXPECT_EQ(strings::RemoveUntil(&data, '\n'), 20);
  EXPECT_EQ(*(data.data()), 'J');
  // check that we don't remove multiple copies of the sentinel
  EXPECT_EQ(strings::RemoveUntil(&data, '\n'), 14);
  EXPECT_EQ(*(data.data()), '\n');
  // check removing a singleton sentinel
  EXPECT_EQ(strings::RemoveUntil(&data, '\n'), 1);
  EXPECT_EQ(*(data.data()), 'L');

  // check that termination without sentinel works
  EXPECT_EQ(strings::RemoveUntil(&data, '\n'), 8);
  EXPECT_EQ(data.size(), 0);

  // check that we do the right thing for an empty stringpiece
  EXPECT_EQ(strings::RemoveUntil(&data, '\n'), 0);
  EXPECT_EQ(data.size(), 0);
}

TEST(StringPieceUtils, FindIgnoreCase) {
  EXPECT_EQ(0, strings::FindIgnoreCase("", ""));
  EXPECT_EQ(0, strings::FindIgnoreCase("foo", ""));
  EXPECT_EQ(absl::string_view::npos, strings::FindIgnoreCase("", "foo"));

  EXPECT_EQ(0, strings::FindIgnoreCase("foo", "Foo"));
  EXPECT_EQ(0, strings::FindIgnoreCase("foofoo", "Foo"));
  EXPECT_EQ(-1, strings::FindIgnoreCase("foofoo", "bar"));
  EXPECT_EQ(absl::string_view::npos, strings::FindIgnoreCase("foofoo", "bar"));
  EXPECT_EQ(std::string::npos, strings::FindIgnoreCase("foofoo", "bar"));
  EXPECT_EQ(3, strings::FindIgnoreCase("FOOBAR", "bar"));

  // This idiom appears in some places, so test it for future-compatibility.
  size_t stv = strings::FindIgnoreCase("foofoo", "bar");
  EXPECT_EQ(std::string::npos, stv);
  stv = strings::FindIgnoreCase("foofoo", "fOo");
  EXPECT_NE(std::string::npos, stv);

  // With initial position
  EXPECT_EQ(0, strings::FindIgnoreCase("foofoo", "foo", 0));
  EXPECT_EQ(3, strings::FindIgnoreCase("fooFOO", "foo", 1));
  EXPECT_EQ(3, strings::FindIgnoreCase("fooFOO", "foo", 3));
  EXPECT_EQ(absl::string_view::npos,
            strings::FindIgnoreCase("fooFOO", "foo", 4));
  EXPECT_EQ(absl::string_view::npos,
            strings::FindIgnoreCase("foofoo", "foo", 6));
}

TEST(StringPieceUtils, FindLongestCommonPrefix) {
  EXPECT_EQ(absl::FindLongestCommonPrefix("", ""), "");
  EXPECT_EQ(absl::FindLongestCommonPrefix("", "abc"), "");
  EXPECT_EQ(absl::FindLongestCommonPrefix("abc", ""), "");
  EXPECT_EQ(absl::FindLongestCommonPrefix("ab", "abc"), "ab");
  EXPECT_EQ(absl::FindLongestCommonPrefix("abc", "ab"), "ab");
  EXPECT_EQ(absl::FindLongestCommonPrefix("abc", "abd"), "ab");
  EXPECT_EQ(absl::FindLongestCommonPrefix("abc", "abcd"), "abc");
  EXPECT_EQ(absl::FindLongestCommonPrefix("abcd", "abcd"), "abcd");
  EXPECT_EQ(absl::FindLongestCommonPrefix("abcd", "efgh"), "");

  // "abcde" v. "abc" but in the middle of other data
  EXPECT_EQ(absl::FindLongestCommonPrefix(
                absl::string_view("1234 abcdef").substr(5, 5),
                absl::string_view("5678 abcdef").substr(5, 3)),
            "abc");
}

// Since the little-endian implementation involves a bit of if-else and various
// return paths, the following tests aims to provide full test coverage of the
// implementation.
TEST(StringPieceUtils, FindLongestCommonPrefixLoad16Mismatch) {
  const std::string x1 = "abcdefgh";
  const std::string x2 = "abcde_";
  EXPECT_EQ(absl::FindLongestCommonPrefix(x1, x2), "abcde");
  EXPECT_EQ(absl::FindLongestCommonPrefix(x2, x1), "abcde");
}

TEST(StringPieceUtils, FindLongestCommonPrefixLoad16MatchesNoLast) {
  const std::string x1 = "abcdef";
  const std::string x2 = "abcdef";
  EXPECT_EQ(absl::FindLongestCommonPrefix(x1, x2), "abcdef");
  EXPECT_EQ(absl::FindLongestCommonPrefix(x2, x1), "abcdef");
}

TEST(StringPieceUtils, FindLongestCommonPrefixLoad16MatchesLastCharMismatches) {
  const std::string x1 = "abcdefg";
  const std::string x2 = "abcdef_h";
  EXPECT_EQ(absl::FindLongestCommonPrefix(x1, x2), "abcdef");
  EXPECT_EQ(absl::FindLongestCommonPrefix(x2, x1), "abcdef");
}

TEST(StringPieceUtils, FindLongestCommonPrefixLoad16MatchesLastMatches) {
  const std::string x1 = "abcde";
  const std::string x2 = "abcdefgh";
  EXPECT_EQ(absl::FindLongestCommonPrefix(x1, x2), "abcde");
  EXPECT_EQ(absl::FindLongestCommonPrefix(x2, x1), "abcde");
}

TEST(StringPieceUtils, FindLongestCommonPrefixSize8Load64Mismatches) {
  const std::string x1 = "abcdefghijk";
  const std::string x2 = "abcde_g_";
  EXPECT_EQ(absl::FindLongestCommonPrefix(x1, x2), "abcde");
  EXPECT_EQ(absl::FindLongestCommonPrefix(x2, x1), "abcde");
}

TEST(StringPieceUtils, FindLongestCommonPrefixSize8Load64Matches) {
  const std::string x1 = "abcdefgh";
  const std::string x2 = "abcdefgh";
  EXPECT_EQ(absl::FindLongestCommonPrefix(x1, x2), "abcdefgh");
  EXPECT_EQ(absl::FindLongestCommonPrefix(x2, x1), "abcdefgh");
}

TEST(StringPieceUtils, FindLongestCommonPrefixSize15Load64Mismatches) {
  const std::string x1 = "012345670123456";
  const std::string x2 = "0123456701_34_6";
  EXPECT_EQ(absl::FindLongestCommonPrefix(x1, x2), "0123456701");
  EXPECT_EQ(absl::FindLongestCommonPrefix(x2, x1), "0123456701");
}

TEST(StringPieceUtils, FindLongestCommonPrefixSize15Load64Matches) {
  const std::string x1 = "012345670123456";
  const std::string x2 = "0123456701234567";
  EXPECT_EQ(absl::FindLongestCommonPrefix(x1, x2), "012345670123456");
  EXPECT_EQ(absl::FindLongestCommonPrefix(x2, x1), "012345670123456");
}

TEST(StringPieceUtils,
     FindLongestCommonPrefixSizeFirstByteOfLast8BytesMismatch) {
  const std::string x1 = "012345670123456701234567";
  const std::string x2 = "0123456701234567_1234567";
  EXPECT_EQ(absl::FindLongestCommonPrefix(x1, x2), "0123456701234567");
  EXPECT_EQ(absl::FindLongestCommonPrefix(x2, x1), "0123456701234567");
}

TEST(StringPieceUtils, FindLongestCommonPrefixLargeLastCharMismatches) {
  const std::string x1(300, 'x');
  std::string x2 = x1;
  x2.back() = '#';
  EXPECT_EQ(absl::FindLongestCommonPrefix(x1, x2), std::string(299, 'x'));
  EXPECT_EQ(absl::FindLongestCommonPrefix(x2, x1), std::string(299, 'x'));
}

TEST(StringPieceUtils, FindLongestCommonPrefixLargeFullMatch) {
  const std::string x1(300, 'x');
  const std::string x2 = x1;
  EXPECT_EQ(absl::FindLongestCommonPrefix(x1, x2), std::string(300, 'x'));
  EXPECT_EQ(absl::FindLongestCommonPrefix(x2, x1), std::string(300, 'x'));
}

TEST(StringPieceUtils, FindLongestCommonSuffix) {
  EXPECT_EQ(absl::FindLongestCommonSuffix("", ""), "");
  EXPECT_EQ(absl::FindLongestCommonSuffix("", "abc"), "");
  EXPECT_EQ(absl::FindLongestCommonSuffix("abc", ""), "");
  EXPECT_EQ(absl::FindLongestCommonSuffix("bc", "abc"), "bc");
  EXPECT_EQ(absl::FindLongestCommonSuffix("abc", "bc"), "bc");
  EXPECT_EQ(absl::FindLongestCommonSuffix("abc", "dbc"), "bc");
  EXPECT_EQ(absl::FindLongestCommonSuffix("bcd", "abcd"), "bcd");
  EXPECT_EQ(absl::FindLongestCommonSuffix("abcd", "abcd"), "abcd");
  EXPECT_EQ(absl::FindLongestCommonSuffix("abcd", "efgh"), "");

  // "abcde" v. "cde" but in the middle of other data
  EXPECT_EQ(absl::FindLongestCommonSuffix(
                absl::string_view("1234 abcdef").substr(5, 5),
                absl::string_view("5678 abcdef").substr(7, 3)),
            "cde");
}

TEST(StringPieceUtils, StringPieceCase) {
  EXPECT_EQ(StringPieceCaseHash()("string"), StringPieceCaseHash()("STRing"));
  EXPECT_TRUE(StringPieceCaseEqual()("string", "STRing"));
  EXPECT_FALSE(
      StringPieceCaseEqual()(absl::string_view("a\0", 2),
                             absl::string_view("a\0b", 3)));  // embedded nulls
}

TEST(StringPieceUtils, StringPieceCaseHashQuality) {
  // Minimal test for hash quality: every bit should be about half the time on.
  StringPieceCaseHash h;

  constexpr size_t kSamples = 1000;
  constexpr size_t kNumBits = sizeof(size_t) * 8;
  std::array<int, kNumBits> bits{};
  // We'll use some sequential looking strings.
  for (int i = 0; i < kSamples; ++i) {
    size_t hash = h(absl::StrCat(i));
    for (int bit = 0; bit < kNumBits; ++bit) {
      bits[bit] += (hash >> bit) & 1;
    }
  }
  for (const int& bit : bits) {
    double time_on = static_cast<double>(bit) / kSamples;
    // Just check that the difference is at most 10%. Value chosen without any
    // particular significance.
    EXPECT_LT(std::abs(.5 - time_on), .1) << "Bit " << (&bit - &bits[0]);
  }
}

void TestConsumeLeadingDigits(absl::string_view s, int64_t expected,
                              absl::string_view remaining) {
  uint64_t v;
  absl::string_view input(s);
  if (strings::ConsumeLeadingDigits(&input, &v)) {
    EXPECT_EQ(v, static_cast<uint64_t>(expected));
    EXPECT_EQ(input, remaining);
  } else {
    EXPECT_LT(expected, 0);
    EXPECT_EQ(input, remaining);
  }
}

TEST(strings, ConsumeLeadingDigits) {
  using strings::ConsumeLeadingDigits;

  TestConsumeLeadingDigits("123", 123, "");
  TestConsumeLeadingDigits("a123", -1, "a123");
  TestConsumeLeadingDigits("9_", 9, "_");
  TestConsumeLeadingDigits("11111111111xyz", 11111111111ll, "xyz");

  // Overflow case
  TestConsumeLeadingDigits("1111111111111111111111111111111xyz", -1,
                           "1111111111111111111111111111111xyz");

  // 2^64
  TestConsumeLeadingDigits("18446744073709551616xyz", -1,
                           "18446744073709551616xyz");
  // 2^64-1
  TestConsumeLeadingDigits("18446744073709551615xyz", 18446744073709551615ull,
                           "xyz");
  // (2^64-1)*10+9
  TestConsumeLeadingDigits("184467440737095516159yz", -1,
                           "184467440737095516159yz");

#if !(__ANDROID__ && __arm__)
  // Pseudo-exhaustive test.
  // We run through every possible 16-20 digit number, where the middle
  // 14 digits consist of the same 2 digits repeated 7 times, as well as
  // every 16-digit number where the middle 14 digits are 67440737095516,
  // which is the middle digits of 2^64.  And we run them twice, once as
  // just the number, and again with a trailing "X".
  // The "same 2 digits" are all combos 00-99 for low and high values of
  // leading 4 digits, and all combos divisible by 3 when the first 4
  // numbers are 1000-8999.
  bool saw_264 = false, saw_264_minus_1 = false;
  for (int hi4 = 0; hi4 <= 9999; ++hi4) {
    for (int mid2 = 0; mid2 <= 100; ++mid2) {
      char buf[32];
      if (mid2 < 100) {
        snprintf(buf, sizeof(buf), "%d%02d%02d%02d%02d%02d%02d%02d", hi4, mid2,
                 mid2, mid2, mid2, mid2, mid2, mid2);
      } else {
        snprintf(buf, sizeof(buf), "%d67440737095516", hi4);
      }
      uint64_t expected = hi4 * 100000000000000 + mid2 * 1010101010101;
      if (mid2 == 100) expected = hi4 * 100000000000000 + 67440737095516;

      uint64_t v = 0;
      absl::string_view input(buf);
      EXPECT_TRUE(ConsumeLeadingDigits(&input, &v)) << " given " << buf;
      EXPECT_EQ(expected, v);
      EXPECT_EQ(0, input.size());

      // To save time, use the leading digits over and over again, hand-placing
      // the final digits at the end of buf.
      char* write = &buf[strlen(buf)];
      int lo2_inc = 1;
      if (hi4 > 999 && hi4 < 9000 && hi4 != 1844) {
        // To save time, only do 1/3rd of the possibilites for the last
        // digits, when the first four digits are between 1000 and 9000.
        // This doubles the speed while still still doing a lot of testing.
        lo2_inc += 2;
      }
      for (int lo2 = 0; lo2 <= 99; lo2 += lo2_inc) {
        write[0] = '0' + lo2 / 10;
        write[1] = '0' + lo2 % 10;
        write[2] = '\0';
        uint64_t big_v = 0;
        absl::string_view big_input(buf);
        uint64_t big_expected = expected * 100 + lo2;
        if (big_input == "18446744073709551616") saw_264 = true;
        if (big_input == "18446744073709551615") saw_264_minus_1 = true;
        if (big_expected / 100 != expected) {  // overflow
          EXPECT_FALSE(ConsumeLeadingDigits(&big_input, &big_v))
              << " given overflowing " << buf;
        } else {
          EXPECT_TRUE(ConsumeLeadingDigits(&big_input, &big_v))
              << " given " << buf;
          EXPECT_EQ(big_expected, big_v) << " given " << buf;
          EXPECT_EQ(0, big_input.size());
          write[2] = 'X';
          write[3] = '\0';
          big_input = buf;
          big_v = -1;
          EXPECT_TRUE(ConsumeLeadingDigits(&big_input, &big_v))
              << " given " << buf;
          EXPECT_EQ(big_expected, big_v) << " given " << buf;
          EXPECT_EQ(1, big_input.size());
        }
      }
    }
  }
  EXPECT_TRUE(saw_264);
  EXPECT_TRUE(saw_264_minus_1);
#endif
}

TEST(strings, ConsumeLeadingChar) {
  std::string s("abc");
  absl::string_view input(s);
  EXPECT_TRUE(strings::ConsumeLeadingChar(&input, 'a'));
  EXPECT_EQ(input, "bc");

  EXPECT_FALSE(strings::ConsumeLeadingChar(&input, 'x'));
  EXPECT_EQ(input, "bc");

  EXPECT_TRUE(strings::ConsumeLeadingChar(&input, 'b'));
  EXPECT_EQ(input, "c");

  EXPECT_TRUE(strings::ConsumeLeadingChar(&input, 'c'));
  EXPECT_EQ(input, "");

  EXPECT_FALSE(strings::ConsumeLeadingChar(&input, 'a'));
  EXPECT_EQ(input, "");
}

TEST(strings, ConsumeCasePrefix) {
  std::string s("abCdEf");
  absl::string_view input(s);
  EXPECT_FALSE(strings::ConsumeCasePrefix(&input, "abcdefg"));
  EXPECT_EQ("abCdEf", input);

  EXPECT_FALSE(strings::ConsumeCasePrefix(&input, "abce"));
  EXPECT_EQ("abCdEf", input);

  EXPECT_TRUE(strings::ConsumeCasePrefix(&input, ""));
  EXPECT_EQ("abCdEf", input);

  EXPECT_FALSE(strings::ConsumeCasePrefix(&input, "abcdeg"));
  EXPECT_EQ("abCdEf", input);

  EXPECT_TRUE(strings::ConsumeCasePrefix(&input, "aBcDef"));
  EXPECT_EQ("", input);

  input = s;
  EXPECT_TRUE(strings::ConsumeCasePrefix(&input, "AbcdE"));
  EXPECT_EQ(input, "f");
}

TEST(strings, ConsumeCaseSuffix) {
  std::string s("abCdEf");
  absl::string_view input(s);
  EXPECT_FALSE(strings::ConsumeCaseSuffix(&input, "abcdefg"));
  EXPECT_EQ(input, "abCdEf");

  EXPECT_TRUE(strings::ConsumeCaseSuffix(&input, ""));
  EXPECT_EQ(input, "abCdEf");

  EXPECT_TRUE(strings::ConsumeCaseSuffix(&input, "def"));
  EXPECT_EQ(input, "abC");

  input = s;
  EXPECT_FALSE(strings::ConsumeCaseSuffix(&input, "abCdEg"));
  EXPECT_EQ(input, "abCdEf");

  EXPECT_TRUE(strings::ConsumeCaseSuffix(&input, "F"));
  EXPECT_EQ(input, "abCdE");

  EXPECT_TRUE(strings::ConsumeCaseSuffix(&input, "ABcdE"));
  EXPECT_EQ(input, "");
}

static void BM_CopyToString(benchmark::State& state) {
  std::string s(state.range(0), 'a');
  for (auto _ : state) {
    absl::string_view src(s);
    std::string dst;
    strings::CopyToString(src, &dst);
    benchmark::DoNotOptimize(dst);
  }
}
BENCHMARK(BM_CopyToString)->DenseRange(0, 64)->Range(128, 1 << 16);

static void BM_CopyToStringFixed_SSO(benchmark::State& state) {
  std::string s;
  for (auto _ : state) {
    strings::CopyToString("src.data(), src.size()", &s);
    benchmark::DoNotOptimize(s);
  }
}
BENCHMARK(BM_CopyToStringFixed_SSO);

static void BM_CopyToStringFixed_EXT(benchmark::State& state) {
  std::string s = "String that exceeds small-string-optimization limit";
  for (auto _ : state) {
    strings::CopyToString("src.data(), src.size()", &s);
    benchmark::DoNotOptimize(s);
  }
}
BENCHMARK(BM_CopyToStringFixed_EXT);

static void BM_BigCopyToStringFixed_EXT(benchmark::State& state) {
  std::string s = "String that exceeds small-string-optimization limit";
  for (auto _ : state) {
    strings::CopyToString("Source that exceeds small-string-optimization limit",
                          &s);
    benchmark::DoNotOptimize(s);
  }
}
BENCHMARK(BM_BigCopyToStringFixed_EXT);

// benchmark StringPieceCaseHash.
static void BM_StringPieceCaseHash(benchmark::State& state) {
  const int size = state.range(0);
  char* data = new char[size + 1];
  memset(data, 'A', size);
  data[size] = '\0';
  for (auto _ : state) {
    benchmark::DoNotOptimize(
        StringPieceCaseHash()(absl::string_view(data, size)));
  }
  delete[] data;
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * size);
}
BENCHMARK(BM_StringPieceCaseHash)->Range(1, 2 << 20);

static void BM_ConsumeDigits(benchmark::State& state) {
  std::string s("9123 1 37 56 97 932 1 34 22 999 1011 100011");
  [[maybe_unused]] uint64_t sum = 0;
  for (auto _ : state) {
    absl::string_view input(s);
    uint64_t v;
    if (strings::ConsumeLeadingDigits(&input, &v)) {
      sum += v;
      input.remove_prefix(1);  // Skip space
    } else {
      input = s;
    }
  }
}
BENCHMARK(BM_ConsumeDigits);

static void BM_ConsumeDigitsWithRE(benchmark::State& state) {
  std::string s("9123 1 37 56 97 932 1 34 22 999 1011 100011");
  RE2 re("(\\d+)");
  [[maybe_unused]] uint64_t sum = 0;
  for (auto _ : state) {
    absl::string_view input(s);
    uint64_t v;
    if (RE2::Consume(&input, re, &v)) {
      sum += v;
      input.remove_prefix(1);  // Skip space
    } else {
      input = s;
    }
  }
}
BENCHMARK(BM_ConsumeDigitsWithRE);

static void BM_FindLongestCommonPrefix(benchmark::State& state) {
  const int len = state.range(0);
  const std::string x(len, 'x');
  const std::string y = x;
  const absl::string_view a = x;
  const absl::string_view b = y;
  for (auto _ : state) {
    benchmark::DoNotOptimize(a);
    benchmark::DoNotOptimize(b);
    benchmark::DoNotOptimize(absl::FindLongestCommonPrefix(a, b));
  }
}
BENCHMARK(BM_FindLongestCommonPrefix)
    ->DenseRange(0, 17)
    ->Arg(24)
    ->Arg(31)
    ->Range(32, 1 << 16);

// String benchmarks can be influenced heavily by whether or not the branches
// can be predicted.  So this test uses strings of random length and difference,
// to test for performance in unpredictable situations.
static void BM_FindLongestCommonPrefixNoPredict(benchmark::State& state) {
  const std::string x(32768, 'x');

  const int len = state.range(0);
  QCHECK_GE(len, 0);
  QCHECK_LT(len, x.size());

  std::string y = x;
  std::vector<int> random_lengths(32768);
  std::vector<int> random_terms(random_lengths.size());
  absl::BitGen gen;
  for (int i = 0; i < random_lengths.size(); ++i) {
    random_lengths[i] = absl::Uniform(gen, 0, len);
    random_terms[i] = absl::Uniform(gen, 0, random_lengths[i] + 1);
  }

  int index = 0;
  for (auto _ : state) {
    int rlen = random_lengths[index];
    const absl::string_view a(&x[0], rlen);
    const absl::string_view b(&y[0], rlen);

    int rterm = random_terms[index];
    y[rterm] = '*';
    absl::string_view prefix = absl::FindLongestCommonPrefix(a, b);
    QCHECK_EQ(prefix.size(), rterm) << "a = " << a << " b = " << b;
    y[rterm] = 'x';
    index = (index + 1) % random_lengths.size();
  }
}
BENCHMARK(BM_FindLongestCommonPrefixNoPredict)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)
    ->Arg(24)
    ->Arg(32)
    ->Arg(64)
    ->Arg(512)
    ->Arg(4096);

}  // namespace
