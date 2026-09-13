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

// based on contributions of various authors in strings/strutil_unittest.cc
//
// These are functions for parsing host and port out of a string.

#include "gloop/strings/host_port.h"

#include <arpa/inet.h>
#include <netinet/in.h>

#include <cctype>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/macros.h"
#include "absl/log/check.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace strings {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::EndsWith;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::StrEq;

template <typename Integral>
bool TestStdLess() {
  constexpr char s[] = "za";
  using P = std::pair<const char*, Integral>;
  return std::less<P>()(P(&s[0], 0), P(&s[1], 0));
}

TEST(HostPortTest, NoStdLessInterference) {
  EXPECT_TRUE(TestStdLess<uint8_t>());
  EXPECT_TRUE(TestStdLess<uint16_t>());
  EXPECT_TRUE(TestStdLess<uint32_t>());
  EXPECT_TRUE(TestStdLess<uint64_t>());
}

TEST(HostPortTest, LessComparesByString) {
  // Make sure std::less<HostPortPair> does string comparison
  // rather than pointer comparison
  constexpr char s[] = "za";
  const HostPortPair a(&s[1], 0);
  const HostPortPair b(&s[0], 0);
  std::less<HostPortPair> order;

  EXPECT_TRUE(a < b);
  EXPECT_TRUE(order(a, b));
  EXPECT_FALSE(b < a);
  EXPECT_FALSE(order(b, a));
  EXPECT_FALSE(a < a);
  EXPECT_FALSE(order(a, a));
}

TEST(ParseHostPortString, SuccessAndFailure) {
  uint16_t port;
  std::string host;

  ASSERT_TRUE(ParseHostPortString("barehost:8080", &host, &port));
  EXPECT_EQ(host, "barehost");
  EXPECT_EQ(port, 8080);

  ASSERT_TRUE(ParseHostPortString("fqhost.google.com:23", &host, &port));
  EXPECT_EQ(host, "fqhost.google.com");
  EXPECT_EQ(port, 23);

  EXPECT_FALSE(ParseHostPortString("bad:host:6725", &host, &port));
  EXPECT_EQ(host, "fqhost.google.com");
  EXPECT_EQ(port, 23);

  EXPECT_FALSE(ParseHostPortString("badport:asdf", &host, &port));
  EXPECT_EQ(host, "fqhost.google.com");
  EXPECT_EQ(port, 23);

  EXPECT_FALSE(ParseHostPortString("suffixport:345a", &host, &port));
  EXPECT_EQ(host, "fqhost.google.com");
  EXPECT_EQ(port, 23);

  EXPECT_FALSE(ParseHostPortString("largeport:67256", &host, &port));
  EXPECT_EQ(host, "fqhost.google.com");
  EXPECT_EQ(port, 23);

  EXPECT_FALSE(ParseHostPortString("negport:-562", &host, &port));
  EXPECT_EQ(host, "fqhost.google.com");
  EXPECT_EQ(port, 23);

  std::string full = "stringalias:13256";
  ASSERT_TRUE(ParseHostPortString(full, &full, &port));
  EXPECT_EQ(full, "stringalias");
  EXPECT_EQ(port, 13256);
}

TEST(ParseHostPort, SuccessAndFailure) {
  uint16_t port = 0;
  absl::string_view host;

  ASSERT_TRUE(ParseHostPort("barehost:8080", &host, &port));
  EXPECT_EQ(host, "barehost");
  EXPECT_EQ(port, 8080);

  ASSERT_TRUE(ParseHostPort("fqhost.google.com:23", &host, &port));
  EXPECT_EQ(host, "fqhost.google.com");
  EXPECT_EQ(port, 23);

  EXPECT_FALSE(ParseHostPort("bad:host:6725", &host, &port));
  EXPECT_EQ(host, "fqhost.google.com");
  EXPECT_EQ(port, 23);

  EXPECT_FALSE(ParseHostPort("badport:asdf", &host, &port));
  EXPECT_EQ(host, "fqhost.google.com");
  EXPECT_EQ(port, 23);

  EXPECT_FALSE(ParseHostPort("suffixport:345a", &host, &port));
  EXPECT_EQ(host, "fqhost.google.com");
  EXPECT_EQ(port, 23);

  EXPECT_FALSE(ParseHostPort("largeport:67256", &host, &port));
  EXPECT_EQ(host, "fqhost.google.com");
  EXPECT_EQ(port, 23);

  EXPECT_FALSE(ParseHostPort("negport:-562", &host, &port));
  EXPECT_EQ(host, "fqhost.google.com");
  EXPECT_EQ(port, 23);

  absl::string_view full = "stringalias:13256";
  ASSERT_TRUE(ParseHostPort(full, &full, &port));
  EXPECT_EQ(full, "stringalias");
  EXPECT_EQ(port, 13256);
}

TEST(ParseHostPortListAndCleanup, SuccessAndFailure) {
  std::vector<HostPortPair> pairs;

  // Parse valid socket addresses.
  EXPECT_EQ(ParseHostPortList("barehost:8080 fqhost.google.com:23", &pairs), 2);
  EXPECT_THAT(
      pairs,
      ElementsAre(AllOf(Field(&HostPortPair::first, StrEq("barehost")),
                        Field(&HostPortPair::second, 8080)),
                  AllOf(Field(&HostPortPair::first, StrEq("fqhost.google.com")),
                        Field(&HostPortPair::second, 23))));

  // Cleanup parsed socket addresses.
  HostPortPairVectorClear(&pairs);
  EXPECT_THAT(pairs, IsEmpty());

  // Fail to parse a mixture of valid and invalid socket addresses.
  EXPECT_EQ(ParseHostPortList("barehost:8080 suffixport:345a", &pairs), 0);
  EXPECT_THAT(pairs, IsEmpty());

  // No-op cleanup.
  HostPortPairVectorClear(&pairs);
  EXPECT_THAT(pairs, IsEmpty());  // Remain unchanged.
  HostPortPairVectorClear(nullptr);
}

TEST(HostOnlyString, Test) {
  EXPECT_EQ(HostOnlyString("foo"), "foo");
  EXPECT_EQ(HostOnlyString(""), "");
  EXPECT_EQ(HostOnlyString("1::2"), "[1::2]");
  EXPECT_EQ(HostOnlyString("[::1]"), "[::1]");
  EXPECT_EQ(HostOnlyString("::1"), "[::1]");

  // Garbage in, garbage out.  But don't crash.
  EXPECT_EQ(HostOnlyString("[foo]"), "[foo]");
  EXPECT_EQ(HostOnlyString("[::"), "[::");
  EXPECT_EQ(HostOnlyString("::]"), "[::]]");
  EXPECT_EQ(HostOnlyString(absl::string_view()),
            HostOnlyString(absl::string_view()));
}

TEST(HostPortString, Test) {
  constexpr char test1[] = "foo";
  constexpr char test2[] = "";
  constexpr char test3[] = "1::2";
  constexpr char test4[] = "[::1]";
  EXPECT_EQ(HostPortString(HostPortPair(test1, 101)), "foo:101");
  EXPECT_EQ(HostPortString(HostPortPair(test2, 102)), ":102");
  EXPECT_EQ(HostPortString(HostPortPair(test3, 103)), "[1::2]:103");
  EXPECT_EQ(HostPortString(HostPortPair(test4, 104)), "[::1]:104");
  EXPECT_EQ(HostPortString("::1", 80), "[::1]:80");
  EXPECT_EQ(HostPortString(absl::string_view(""), 80), ":80");

  // Garbage in, garbage out.  But don't crash.
  constexpr char test5[] = "[foo]";
  constexpr char test6[] = "[::";
  constexpr char test7[] = "::]";
  constexpr const char* test8 = nullptr;
  EXPECT_EQ(HostPortString(HostPortPair(test5, 105)), "[foo]:105");
  EXPECT_EQ(HostPortString(HostPortPair(test6, 106)), "[:::106");
  EXPECT_EQ(HostPortString(HostPortPair(test7, 107)), "[::]]:107");
  EXPECT_THAT(HostPortString(HostPortPair(test8, 0)), EndsWith(":0"));
  EXPECT_THAT(HostPortString(absl::string_view(), 108), EndsWith(":108"));
  EXPECT_THAT(HostPortString(absl::string_view(), 109), EndsWith(":109"));
}

struct ParseHostOptionalPortTestCase {
  absl::string_view full;
  int default_port;
  bool expect_return;
  absl::string_view expect_host;
  uint16_t expect_port;
};

class ParseHostOptionalPortTest
    : public testing::TestWithParam<ParseHostOptionalPortTestCase> {};

TEST_P(ParseHostOptionalPortTest, SuccessAndFailure) {
  const ParseHostOptionalPortTestCase& test = GetParam();
  absl::string_view host = "nochange";
  uint16_t port = 99;
  bool ret = ParseHostOptionalPort(test.full, test.default_port, &host, &port);
  EXPECT_EQ(ret, test.expect_return);
  EXPECT_EQ(host, test.expect_host);
  EXPECT_EQ(port, test.expect_port);
}

constexpr ParseHostOptionalPortTestCase kParseHostOptionalPortTestCases[] = {
    // Well-formed inputs.
    {"google.com", 80, true, "google.com", 80},
    {"gmail.com:81", -1, true, "gmail.com", 81},
    {"192.0.2.1", 82, true, "192.0.2.1", 82},
    {"192.0.2.2:83", -1, true, "192.0.2.2", 83},
    {"[2001::1]", 84, true, "2001::1", 84},
    {"[2001::2]:85", -1, true, "2001::2", 85},
    {"2001::3", 86, true, "2001::3", 86},
    // No port, no default.
    {"google.com", -1, false, "nochange", 99},
    {"192.0.2.1", -1, false, "nochange", 99},
    {"[2001::1]", -1, false, "nochange", 99},
    {"2001::3", -1, false, "nochange", 99},
    // Default port, but unused.
    {"gmail.com:81", 77, true, "gmail.com", 81},
    {"192.0.2.2:83", 77, true, "192.0.2.2", 83},
    {"[2001::2]:85", 77, true, "2001::2", 85},
    // Out-of-range ports.
    {"google.com", 65536, false, "nochange", 99},
    {"google.com:65536", 1, false, "nochange", 99},
    {"google.com:9999999999", 1, false, "nochange", 99},
    // Invalid port parts.
    {"google.com:-25", 1, false, "nochange", 99},
    {"google.com:+25", 1, false, "nochange", 99},
    {"google.com:25  ", 1, false, "nochange", 99},
    {"google.com:25\t", 1, false, "nochange", 99},
    {"google.com:0x25 ", 1, false, "nochange", 99},
    // Some nonsense that causes parse failures.
    {"[", 1, false, "nochange", 99},
    {"host:", 1, false, "nochange", 99},
    {"[]:", 1, false, "nochange", 99},
    {"[]bad", 1, false, "nochange", 99},
    {"[]", 1, false, "nochange", 99},
    {"[192.0.2.1]", 1, false, "nochange", 99},
    // Examples of nonsense that gets through.
    {"[[:]]", 86, true, "[:]", 86},
    {"x:y:z", 87, true, "x:y:z", 87},
    {"", 88, true, "", 88},
    {":123", -1, true, "", 123},
    {"\nOMG\t", 89, true, "\nOMG\t", 89},
};

INSTANTIATE_TEST_SUITE_P(
    ParseHostOptionalPort, ParseHostOptionalPortTest,
    testing::ValuesIn(kParseHostOptionalPortTestCases),
    [](const testing::TestParamInfo<ParseHostOptionalPortTestCase>& info) {
      std::string name;
      for (char c : info.param.full) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
          name.push_back(c);
        } else {
          name.push_back('_');
        }
      }
      return absl::StrCat("idx_", info.index, "_", name);
    });

TEST(ParseHostOptionalPortString, SuccessAndFailure) {
  // Valid, overwrites input string.
  std::string host = "[::]:123";
  uint16_t port;
  ASSERT_TRUE(ParseHostOptionalPortString(host, -1, &host, &port));
  EXPECT_EQ(host, "::");
  EXPECT_EQ(port, 123);

  // Invalid, never touches the output.
  EXPECT_FALSE(ParseHostOptionalPortString("::", -1, nullptr, nullptr));
}

TEST(ParseHostOptionalPort, string_views) {
  // Test with non-NUL-terminated string_views.
  static const char kHostPort[] = "host:1234junk";
  absl::string_view host;
  uint16_t port;

  // "host"
  ASSERT_TRUE(
      ParseHostOptionalPort(absl::string_view(kHostPort, 4), 80, &host, &port));
  EXPECT_EQ(host, "host");
  EXPECT_EQ(port, 80);

  // "host:" (invalid)
  host = "unchanged";
  EXPECT_FALSE(
      ParseHostOptionalPort(absl::string_view(kHostPort, 5), 80, &host, &port));
  EXPECT_EQ(host, "unchanged");

  // "host:123"
  ASSERT_TRUE(
      ParseHostOptionalPort(absl::string_view(kHostPort, 8), 80, &host, &port));
  EXPECT_EQ(host, "host");
  EXPECT_EQ(port, 123);
}

TEST(ParseIpRange, Simple) {
  uint32_t lowip;
  uint32_t highip;
  ASSERT_TRUE(ParseIpRange("10.20.*.*", &lowip, &highip));
  EXPECT_EQ(lowip, ntohl(inet_addr("10.20.0.0")));
  EXPECT_EQ(highip, ntohl(inet_addr("10.20.255.255")));

  ASSERT_TRUE(ParseIpRange("10.20.*.*-10.30.*.*", &lowip, &highip));
  EXPECT_EQ(lowip, ntohl(inet_addr("10.20.0.0")));
  EXPECT_EQ(highip, ntohl(inet_addr("10.30.255.255")));

  ASSERT_TRUE(ParseIpRange("10.20.30.0-10.20.30.255", &lowip, &highip));
  EXPECT_EQ(lowip, ntohl(inet_addr("10.20.30.0")));
  EXPECT_EQ(highip, ntohl(inet_addr("10.20.30.255")));

  ASSERT_TRUE(ParseIpRange("10.20.30.0/24", &lowip, &highip));
  EXPECT_EQ(lowip, ntohl(inet_addr("10.20.30.0")));
  EXPECT_EQ(highip, ntohl(inet_addr("10.20.30.255")));

  EXPECT_FALSE(ParseIpRange("definitely not a range!", &lowip, &highip));
}

static void BM_HostPortString(benchmark::State& state) {
  const auto host = std::string(state.range(0), 'x');
  const auto port = state.range(1);
  for (auto _ : state) {
    const std::string s = HostPortString(host, port);
    benchmark::DoNotOptimize(s);
  }
}
BENCHMARK(BM_HostPortString)->RangePair(0, 128, 0, 65535);

}  // namespace
}  // namespace strings
