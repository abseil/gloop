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

// based on contributions of various authors in strings/strutil_unittest.cc
//
// These are functions for parsing host and port out of a string.

#include "gloop/strings/host_port.h"

#include <arpa/inet.h>
#include <netinet/in.h>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/macros.h"
#include "absl/log/check.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "gtest/gtest.h"

namespace strings {

template <typename Integral>
bool TestStdLess() {
  const char s[] = "za";
  typedef std::pair<const char*, Integral> P;
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
  const char s[] = "za";
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

  EXPECT_TRUE(ParseHostPortString("barehost:8080", &host, &port));
  EXPECT_EQ("barehost", host);
  EXPECT_EQ(8080, port);

  EXPECT_TRUE(ParseHostPortString("fqhost.google.com:23", &host, &port));
  EXPECT_EQ("fqhost.google.com", host);
  EXPECT_EQ(23, port);

  EXPECT_FALSE(ParseHostPortString("bad:host:6725", &host, &port));
  EXPECT_EQ("fqhost.google.com", host);
  EXPECT_EQ(23, port);

  EXPECT_FALSE(ParseHostPortString("badport:asdf", &host, &port));
  EXPECT_EQ("fqhost.google.com", host);
  EXPECT_EQ(23, port);

  EXPECT_FALSE(ParseHostPortString("suffixport:345a", &host, &port));
  EXPECT_EQ("fqhost.google.com", host);
  EXPECT_EQ(23, port);

  EXPECT_FALSE(ParseHostPortString("largeport:67256", &host, &port));
  EXPECT_EQ("fqhost.google.com", host);
  EXPECT_EQ(23, port);

  EXPECT_FALSE(ParseHostPortString("negport:-562", &host, &port));
  EXPECT_EQ("fqhost.google.com", host);
  EXPECT_EQ(23, port);

  std::string full = "stringalias:13256";
  EXPECT_TRUE(ParseHostPortString(full, &full, &port));
  EXPECT_EQ("stringalias", full);
  EXPECT_EQ(13256, port);
}

TEST(ParseHostPort, SuccessAndFailure) {
  uint16_t port = 0;
  absl::string_view host;

  EXPECT_TRUE(ParseHostPort("barehost:8080", &host, &port));
  EXPECT_EQ("barehost", host);
  EXPECT_EQ(8080, port);

  EXPECT_TRUE(ParseHostPort("fqhost.google.com:23", &host, &port));
  EXPECT_EQ("fqhost.google.com", host);
  EXPECT_EQ(23, port);

  EXPECT_FALSE(ParseHostPort("bad:host:6725", &host, &port));
  EXPECT_EQ("fqhost.google.com", host);
  EXPECT_EQ(23, port);

  EXPECT_FALSE(ParseHostPort("badport:asdf", &host, &port));
  EXPECT_EQ("fqhost.google.com", host);
  EXPECT_EQ(23, port);

  EXPECT_FALSE(ParseHostPort("suffixport:345a", &host, &port));
  EXPECT_EQ("fqhost.google.com", host);
  EXPECT_EQ(23, port);

  EXPECT_FALSE(ParseHostPort("largeport:67256", &host, &port));
  EXPECT_EQ("fqhost.google.com", host);
  EXPECT_EQ(23, port);

  EXPECT_FALSE(ParseHostPort("negport:-562", &host, &port));
  EXPECT_EQ("fqhost.google.com", host);
  EXPECT_EQ(23, port);

  absl::string_view full = "stringalias:13256";
  EXPECT_TRUE(ParseHostPort(full, &full, &port));
  EXPECT_EQ("stringalias", full);
  EXPECT_EQ(13256, port);
}

TEST(ParseHostPortListAndCleanup, SuccessAndFailure) {
  std::vector<HostPortPair> pairs;

  // Parse valid socket addresses.
  EXPECT_EQ(2, ParseHostPortList("barehost:8080 fqhost.google.com:23", &pairs));
  ASSERT_EQ(2, pairs.size());
  EXPECT_STREQ("barehost", pairs[0].first);
  EXPECT_EQ(8080, pairs[0].second);
  EXPECT_STREQ("fqhost.google.com", pairs[1].first);
  EXPECT_EQ(23, pairs[1].second);

  // Cleanup parsed socket addresses.
  HostPortPairVectorClear(&pairs);
  ASSERT_TRUE(pairs.empty());

  // Fail to parse a mixture of valid and invalid socket addresses.
  EXPECT_EQ(0, ParseHostPortList("barehost:8080 suffixport:345a", &pairs));
  ASSERT_TRUE(pairs.empty());

  // No-op cleanup.
  HostPortPairVectorClear(&pairs);
  ASSERT_TRUE(pairs.empty());  // Remain unchanged.
  HostPortPairVectorClear(nullptr);
}

TEST(HostOnlyString, Test) {
  EXPECT_EQ("foo", HostOnlyString("foo"));
  EXPECT_EQ("", HostOnlyString(""));
  EXPECT_EQ("[1::2]", HostOnlyString("1::2"));
  EXPECT_EQ("[::1]", HostOnlyString("[::1]"));
  EXPECT_EQ("[::1]", HostOnlyString("::1"));

  // Garbage in, garbage out.  But don't crash.
  EXPECT_EQ("[foo]", HostOnlyString("[foo]"));
  EXPECT_EQ("[::", HostOnlyString("[::"));
  EXPECT_EQ("[::]]", HostOnlyString("::]"));
  EXPECT_EQ(HostOnlyString(absl::string_view()),
            HostOnlyString(absl::string_view()));
}

TEST(HostPortString, Test) {
  char test1[] = "foo";
  char test2[] = "";
  char test3[] = "1::2";
  char test4[] = "[::1]";
  EXPECT_EQ("foo:101", HostPortString(HostPortPair(test1, 101)));
  EXPECT_EQ(":102", HostPortString(HostPortPair(test2, 102)));
  EXPECT_EQ("[1::2]:103", HostPortString(HostPortPair(test3, 103)));
  EXPECT_EQ("[::1]:104", HostPortString(HostPortPair(test4, 104)));
  EXPECT_EQ("[::1]:80", HostPortString("::1", 80));
  EXPECT_EQ(":80", HostPortString(absl::string_view(""), 80));

  // Garbage in, garbage out.  But don't crash.
  char test5[] = "[foo]";
  char test6[] = "[::";
  char test7[] = "::]";
  const char* test8 = nullptr;
  EXPECT_EQ("[foo]:105", HostPortString(HostPortPair(test5, 105)));
  EXPECT_EQ("[:::106", HostPortString(HostPortPair(test6, 106)));
  EXPECT_EQ("[::]]:107", HostPortString(HostPortPair(test7, 107)));
  EXPECT_TRUE(absl::EndsWith(HostPortString(HostPortPair(test8, 0)), ":0"));
  EXPECT_TRUE(absl::EndsWith(HostPortString(absl::string_view(), 108), ":108"));
  EXPECT_TRUE(absl::EndsWith(HostPortString(absl::string_view(), 109), ":109"));
}

TEST(ParseHostOptionalPort, SuccessAndFailure) {
  struct TestCases {
    const char* full;
    int default_port;
    bool expect_return;
    std::string expect_host;
    uint16_t expect_port;
  } test[] = {
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

  for (int i = 0; i < ABSL_ARRAYSIZE(test); i++) {
    absl::string_view host = "nochange";
    uint16_t port = 99;
    bool ret =
        ParseHostOptionalPort(test[i].full, test[i].default_port, &host, &port);
    EXPECT_EQ(ret, test[i].expect_return) << i;
    EXPECT_EQ(host, test[i].expect_host) << i;
    EXPECT_EQ(port, test[i].expect_port) << i;
  }
}

TEST(ParseHostOptionalPortString, SuccessAndFailure) {
  // Valid, overwrites input string.
  std::string host = "[::]:123";
  uint16_t port;
  EXPECT_TRUE(ParseHostOptionalPortString(host, -1, &host, &port));
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
  EXPECT_TRUE(
      ParseHostOptionalPort(absl::string_view(kHostPort, 4), 80, &host, &port));
  EXPECT_EQ("host", host);
  EXPECT_EQ(80, port);

  // "host:" (invalid)
  host = "unchanged";
  EXPECT_FALSE(
      ParseHostOptionalPort(absl::string_view(kHostPort, 5), 80, &host, &port));
  EXPECT_EQ("unchanged", host);

  // "host:123"
  EXPECT_TRUE(
      ParseHostOptionalPort(absl::string_view(kHostPort, 8), 80, &host, &port));
  EXPECT_EQ("host", host);
  EXPECT_EQ(123, port);
}

TEST(ParseIpRange, Simple) {
  uint32_t lowip;
  uint32_t highip;
  CHECK(ParseIpRange("10.20.*.*", &lowip, &highip));
  CHECK_EQ(lowip, ntohl(inet_addr("10.20.0.0")));
  CHECK_EQ(highip, ntohl(inet_addr("10.20.255.255")));

  CHECK(ParseIpRange("10.20.*.*-10.30.*.*", &lowip, &highip));
  CHECK_EQ(lowip, ntohl(inet_addr("10.20.0.0")));
  CHECK_EQ(highip, ntohl(inet_addr("10.30.255.255")));

  CHECK(ParseIpRange("10.20.30.0-10.20.30.255", &lowip, &highip));
  CHECK_EQ(lowip, ntohl(inet_addr("10.20.30.0")));
  CHECK_EQ(highip, ntohl(inet_addr("10.20.30.255")));

  CHECK(ParseIpRange("10.20.30.0/24", &lowip, &highip));
  CHECK_EQ(lowip, ntohl(inet_addr("10.20.30.0")));
  CHECK_EQ(highip, ntohl(inet_addr("10.20.30.255")));

  CHECK(!ParseIpRange("definitely not a range!", &lowip, &highip));
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

}  // namespace strings
