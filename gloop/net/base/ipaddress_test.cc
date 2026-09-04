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

#include "gloop/net/base/ipaddress.h"

#include <stdint.h>

#ifdef _WIN32

#include <winsock2.h>

// winsock2.h must come before windows.h.

#include <windows.h>
#include <ws2ipdef.h>
#include <ws2tcpip.h>

#else  // !defined(_WIN32)

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#endif  // !defined(_WIN32)

#include <algorithm>
#include <cerrno>
#include <compare>
#include <cstring>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/base/log_severity.h"
#include "absl/container/fixed_array.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_set.h"
#include "absl/hash/hash_testing.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/log/log_streamer.h"
#include "absl/log/scoped_mock_log.h"
#include "absl/numeric/int128.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/types/compare.h"
#include "absl/types/source_location.h"
#include "fuzztest/fuzztest.h"
#include "gloop/strings/host_port.h"
#include "gloop/util/endian/endian.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#ifdef NDEBUG
constexpr bool DEBUG_MODE = false;
#else
constexpr bool DEBUG_MODE = true;
#endif

// TODO benchmark not portable yet.
#if !defined(GUNIT_NO_GOOGLE3)
#include "benchmark/benchmark.h"
#endif

#ifdef _WIN32
#define s6_addr16 u.Word
#endif

namespace net_base {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::_;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::MatchesRegex;

MATCHER_P(IpRangeEquals, expected, "") { return arg.ToString() == expected; }

class ScopedMockLogVerifier {
 public:
  explicit ScopedMockLogVerifier(absl::string_view substr) {
    EXPECT_CALL(log_, Log(absl::LogSeverity::kError, _, HasSubstr(substr)));
    log_.StartCapturingLogs();
  }

 private:
  ::absl::ScopedMockLog log_;
};

struct BogusTestCase {
  absl::string_view test_name;
  absl::string_view str;
};

// Portably constructs an in_addr. (The in_addr layout differs between Windows
// and POSIX.)
constexpr in_addr InAddrFromUint32(uint32_t quad) {
#ifdef _WIN32
  // in_addr holds a union with options. Make the one corresponding to the POSIX
  // .s_addr active.
  return in_addr{.S_un = {.S_addr = quad}};
#else
  return in_addr{.s_addr = quad};
#endif
}

IPAddress MakeScopedIP(const IPAddress& addr, uint32_t scope_id) {
  if (scope_id == 0) return addr;

  CHECK_EQ(addr.address_family(), AF_INET6);
  return MakeIPAddressWithScopeId(addr.ipv6_address(), scope_id).value();
}

TEST(IPAddressTest, BasicTests) {
  in_addr addr4;
  in6_addr addr6;

  inet_pton(AF_INET, "1.2.3.4", &addr4);
  inet_pton(AF_INET6, "2001:700:300:1800::f", &addr6);

  IPAddress addr(addr4);
  in_addr returned_addr4 = addr.ipv4_address();
  ASSERT_EQ(addr.address_family(), AF_INET);
  EXPECT_TRUE(addr.is_ipv4());
  EXPECT_FALSE(addr.is_ipv6());
  EXPECT_EQ(memcmp(&addr4, &returned_addr4, sizeof(addr4)), 0);

  addr = IPAddress(addr6);
  in6_addr returned_addr6 = addr.ipv6_address();
  ASSERT_EQ(addr.address_family(), AF_INET6);
  EXPECT_FALSE(addr.is_ipv4());
  EXPECT_TRUE(addr.is_ipv6());
  EXPECT_EQ(memcmp(&addr6, &returned_addr6, sizeof(addr6)), 0);

  addr = IPAddress();
  ASSERT_EQ(addr.address_family(), AF_UNSPEC);
}

TEST(IPAddressTest, ConstexprIPv4) {
  constexpr IPAddress addr4(InAddrFromUint32(0x12345678));
  ASSERT_EQ(addr4, IPAddress(InAddrFromUint32(0x12345678)));
}

TEST(IPAddressTest, ToAndFromString4) {
  const std::string kIPString = "1.2.3.4";
  const std::string kBogusIPString = "1.2.3.256";
  const std::string kPTRString = "4.3.2.1.in-addr.arpa";
  in_addr addr4;
  ASSERT_GT(inet_pton(AF_INET, kIPString.c_str(), &addr4), 0);

  IPAddress addr;
  EXPECT_FALSE(StringToIPAddress(kBogusIPString, nullptr));
  EXPECT_FALSE(StringToIPAddress(kBogusIPString, &addr));
  ASSERT_TRUE(StringToIPAddress(kIPString, nullptr));
  ASSERT_TRUE(StringToIPAddress(kIPString, &addr));

  in_addr returned_addr4 = addr.ipv4_address();
  EXPECT_EQ(addr.address_family(), AF_INET);
  EXPECT_EQ(memcmp(&addr4, &returned_addr4, sizeof(addr4)), 0);

  std::string packed = addr.ToPackedString();
  EXPECT_EQ(packed.length(), sizeof(addr4));
  EXPECT_EQ(memcmp(packed.data(), &addr4, sizeof(addr4)), 0);

  EXPECT_TRUE(PackedStringToIPAddress(packed, nullptr));
  IPAddress unpacked;
  EXPECT_TRUE(PackedStringToIPAddress(packed, &unpacked));
  EXPECT_EQ(addr, unpacked);

  EXPECT_EQ(addr.ToString(), kIPString);
  EXPECT_EQ(IPAddressToURIString(addr), kIPString);
  EXPECT_EQ(IPAddressToPTRString(addr), "4.3.2.1.in-addr.arpa");
  EXPECT_TRUE(PTRStringToIPAddress(kPTRString, &addr));
  EXPECT_EQ(addr.ToString(), kIPString);
}

TEST(IPAddressTest, ThoroughToString4) {
  // This thoroughly tests the internal function FastOctetToBuffer by
  // evaluating every possible number in every possible position.
  for (int i = 0; i < 256; ++i) {
    const std::string expected = absl::StrCat(i, ".0.0.0");
    EXPECT_EQ(StringToIPAddressOrDie(expected).ToString(), expected);
  }
  for (int i = 0; i < 256; ++i) {
    const std::string expected = absl::StrCat("0.", i, ".0.0");
    EXPECT_EQ(StringToIPAddressOrDie(expected).ToString(), expected);
  }
  for (int i = 0; i < 256; ++i) {
    const std::string expected = absl::StrCat("0.0.", i, ".0");
    EXPECT_EQ(StringToIPAddressOrDie(expected).ToString(), expected);
  }
  for (int i = 0; i < 256; ++i) {
    const std::string expected = absl::StrCat("0.0.0.", i);
    EXPECT_EQ(StringToIPAddressOrDie(expected).ToString(), expected);
  }
}

// These IPv4 string literal formats are supported by inet_aton(3).
// They are one source of "spoofed" addresses in URLs and generally
// considered unsafe.  We follow inet_pton(3) and explicitly do not
// support them.
class UnsafeIPv4StringsParamTest : public testing::TestWithParam<const char*> {
};

TEST_P(UnsafeIPv4StringsParamTest, StringToIPAddressFails) {
  IPAddress ip;
  EXPECT_FALSE(StringToIPAddress(GetParam(), &ip));
}

INSTANTIATE_TEST_SUITE_P(
    , UnsafeIPv4StringsParamTest,
    testing::Values("016.016.016",          // 14.14.0.14
                    "016.016",              // 14.0.0.14
                    "016",                  // 0.0.0.14
                    "0x0a.0x0a.0x0a.0x0a",  // 10.10.10.10
                    "0x0a.0x0a.0x0a",       // 10.10.0.10
                    "0x0a.0x0a",            // 10.0.0.10
                    "0x0a",                 // 0.0.0.10
                    "42.42.42",             // 42.42.0.42
                    "42.42",                // 42.0.0.42
                    "42",                   // 0.0.0.42
                    // On Darwin `inet_pton` ignores leading zeros so this
                    // would be a valid 16.16.16.16 address, but
                    // `StringToIPAddress` does not use `inet_pton`.
                    "016.016.016.016"  // 14.14.14.14
                    ));

TEST(IPAddressTest, IPv4TooShort) {
  IPAddress ip;
  EXPECT_FALSE(StringToIPAddress("", &ip));
  EXPECT_FALSE(StringToIPAddress("0", &ip));
  EXPECT_FALSE(StringToIPAddress("a", &ip));
  EXPECT_FALSE(StringToIPAddress(".", &ip));
}

TEST(IPAddressTest, IPv4LeadingSpaceInvalid) {
  IPAddress ip;
  EXPECT_FALSE(StringToIPAddress("1.2.3.4 ", &ip));
}

TEST(IPAddressTest, IPv4TrailingSpaceInvalid) {
  IPAddress ip;
  EXPECT_FALSE(StringToIPAddress("1.2.3.4 ", &ip));
}

TEST(IPAddressTest, IPv4LeadingDotInvalid) {
  IPAddress ip;
  EXPECT_FALSE(StringToIPAddress(".1.2.3.4", &ip));
}

TEST(IPAddressTest, IPv4TrailingDotInvalid) {
  IPAddress ip;
  EXPECT_FALSE(StringToIPAddress("1.2.3.4.", &ip));
}

TEST(IPAddressTest, IPv4OctetOutOfRange) {
  IPAddress ip;
  EXPECT_FALSE(StringToIPAddress("0.0.0.256", &ip));
  EXPECT_FALSE(StringToIPAddress("0.0.0.-1", &ip));
}

TEST(IPAddressTest, IPv4LeadingZerosInvalid) {
  IPAddress ip;
  EXPECT_FALSE(StringToIPAddress("00.00.00.00", &ip));
  EXPECT_FALSE(StringToIPAddress("01.01.01.01", &ip));
}

TEST(IPAddressTest, IPv4MissingFieldInvalid) {
  IPAddress ip;
  EXPECT_FALSE(StringToIPAddress("1.2.3", &ip));
  EXPECT_FALSE(StringToIPAddress("1.2..4", &ip));
  EXPECT_FALSE(StringToIPAddress(".2.3.4", &ip));
  EXPECT_FALSE(StringToIPAddress("1.2.3.", &ip));
}

TEST(IPAddressTest, IPv4TooManyFieldsInvalid) {
  IPAddress ip;
  EXPECT_FALSE(StringToIPAddress("1.2.3.4.5", &ip));
}

TEST(IPAddressTest, ToAndFromString6) {
  const std::string kIPString = "2001:db8:300:1800::f";
  const std::string kIPLiteral = "[2001:db8:300:1800::f]";
  const std::string kPTRString =
      "f.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0."
      "0.0.8.1.0.0.3.0.8.b.d.0.1.0.0.2.ip6.arpa";

  in6_addr addr6;
  ASSERT_GT(inet_pton(AF_INET6, kIPString.c_str(), &addr6), 0);

  IPAddress addr;
  ASSERT_TRUE(StringToIPAddress(kIPString, nullptr));
  ASSERT_TRUE(StringToIPAddress(kIPString, &addr));

  in6_addr returned_addr6 = addr.ipv6_address();
  EXPECT_EQ(addr.address_family(), AF_INET6);
  EXPECT_EQ(memcmp(&addr6, &returned_addr6, sizeof(addr6)), 0);

  std::string packed = addr.ToPackedString();
  EXPECT_EQ(packed.length(), sizeof(addr6));
  EXPECT_EQ(memcmp(packed.data(), &addr6, sizeof(addr6)), 0);

  EXPECT_TRUE(PackedStringToIPAddress(packed, nullptr));
  IPAddress unpacked;
  EXPECT_TRUE(PackedStringToIPAddress(packed, &unpacked));
  EXPECT_EQ(addr, unpacked);

  EXPECT_EQ(addr.ToString(), kIPString);
  EXPECT_EQ(IPAddressToURIString(addr), kIPLiteral);
  EXPECT_EQ(IPAddressToPTRString(addr), kPTRString);
  EXPECT_TRUE(PTRStringToIPAddress(kPTRString, &addr));
  EXPECT_EQ(addr.ToString(), kIPString);
}

// The main purpose of this test is to validate that
// StringToIPAddressWithOptionalScope has feature parity with StringToIPAddress.
//
// TODO: More thorough validation of features that are unique to the
// "WithOptionalScope" variant.
TEST(IPAddressTest, ToAndFromString6WithOptionalScope) {
  constexpr char kIPString[] = "2001:db8:300:1800::f";
  constexpr char kIPLiteral[] = "[2001:db8:300:1800::f]";
  constexpr char kPTRString[] =
      "f.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0."
      "0.0.8.1.0.0.3.0.8.b.d.0.1.0.0.2.ip6.arpa";

  in6_addr addr6;
  ASSERT_GT(inet_pton(AF_INET6, kIPString, &addr6), 0);

  auto addr_or = StringToIPAddressWithOptionalScope(kIPString);
  ABSL_ASSERT_OK(addr_or.status());
  IPAddress addr = addr_or.value();

  in6_addr returned_addr6 = addr.ipv6_address();
  EXPECT_EQ(addr.address_family(), AF_INET6);
  EXPECT_EQ(memcmp(&addr6, &returned_addr6, sizeof(addr6)), 0);

  std::string packed = addr.ToPackedString();
  EXPECT_EQ(packed.length(), sizeof(addr6));
  EXPECT_EQ(memcmp(packed.data(), &addr6, sizeof(addr6)), 0);

  EXPECT_TRUE(PackedStringToIPAddress(packed, nullptr));
  IPAddress unpacked;
  EXPECT_TRUE(PackedStringToIPAddress(packed, &unpacked));
  EXPECT_EQ(addr, unpacked);

  EXPECT_EQ(addr.ToString(), kIPString);
  EXPECT_EQ(IPAddressToURIString(addr), kIPLiteral);
  EXPECT_EQ(IPAddressToPTRString(addr), kPTRString);
  EXPECT_TRUE(PTRStringToIPAddress(kPTRString, &addr));
  EXPECT_EQ(addr.ToString(), kIPString);
}

class BogusIPAddressTest : public testing::TestWithParam<BogusTestCase> {};

TEST_P(BogusIPAddressTest, StringToIPAddressFails) {
  IPAddress addr;
  EXPECT_FALSE(StringToIPAddress(GetParam().str, nullptr));
  EXPECT_FALSE(StringToIPAddress(GetParam().str, &addr));
}

INSTANTIATE_TEST_SUITE_P(
    , BogusIPAddressTest,
    testing::Values(BogusTestCase{"TooManyGroups",
                                  "2001:db8:300:1800:1:2:3:4:5"},
                    BogusTestCase{"InvalidHex", "2001:db8::g"}),
    [](const testing::TestParamInfo<BogusTestCase>& info) {
      return std::string(info.param.test_name);
    });

class BogusIPAddressWithOptionalScopeTest
    : public testing::TestWithParam<BogusTestCase> {};

TEST_P(BogusIPAddressWithOptionalScopeTest, FailsOnBogusInput) {
  // This sets the environment for an error-handling bug which would only
  // trigger when errno == 0.  The bug has since been fixed, and this allows
  // us to detect regressions.
  errno = 0;
  EXPECT_FALSE(StringToIPAddressWithOptionalScope(GetParam().str).ok());
}

INSTANTIATE_TEST_SUITE_P(
    , BogusIPAddressWithOptionalScopeTest,
    testing::Values(BogusTestCase{"TooManyGroups",
                                  "2001:db8:300:1800:1:2:3:4:5"},
                    BogusTestCase{"InvalidHex", "2001:db8::g"},
                    BogusTestCase{"TooManyGroupsWithScope",
                                  "2001:db8:300:1800:1:2:3:4:5%1"},
                    BogusTestCase{"InvalidHexWithScope", "2001:db8::g%1"}),
    [](const testing::TestParamInfo<BogusTestCase>& info) {
      return std::string(info.param.test_name);
    });

TEST(IPAddressTest, EmptyStrings) {
  IPAddress ip;
  EXPECT_FALSE(StringToIPAddress(absl::string_view(nullptr, 0), &ip));
  EXPECT_FALSE(StringToIPAddress("", &ip));
  std::string empty;
  EXPECT_FALSE(StringToIPAddress(empty, &ip));
}

TEST(IPAddressTest, SameAsInetNToP6) {
  // Test that for various classes of IP addresses IPAddress::ToString generates
  // the same result as inet_ntop.  Assumes all test cases are in canonical
  // form.
  const std::string cases[] = {
      "::1",
      "::",
      "1::",
      "1:2:3:4:5:6:7:8",
      "ffff:ffff:100::808:808",
      "2001:0:0:4::8",
      "2001::4:5:6:7:8",
      "2001:2:3:4:5:6:7:8",
      "0:0:3::ffff",
      "::5:0:0:ffff",
      "1::4:0:0:7:8",
      "2001:658:22a:cafe::",
      "::ffff",
      "::abcd",
      "::ffff:1.2.3.4",
      "::ffff:ffff:1:1",
      "1234:abcd::",
      "1234::abcd:0:0:5678",
      "1234:0:0:abcd::5678",
      "::ffff:192.168.90.1",
      "1234:0:0:abcd::5678",
      "1234:5678:2:9abc:def0:3:1234:5678",
// Fuchsia's inet_ntop does not follow RFC 5952 so we skip the following tests.
#if !defined(__Fuchsia__)
      // Fuchsia uses musl, which fixed the zero compression in
      // https://git.musl-libc.org/cgit/musl/commit/src/network/inet_ntop.c?id=947b4574
      // (inet_ntop: fix the IPv6 leading zero sequence compression) on
      // 2024-06-22.  This should eventually be picked up by Fuchsia and these
      // special cases can then be removed.
      "::4:0:0:0:ffff",
      "2001:0:3:4:5:6:7:8",
      "1234:5678:0:9abc:def0:0:1234:5678",
      // IPv4-compatible addresses are deprecated.  musl doesn't support them,
      // so these will not round-trip as dotted quads.
      // https://datatracker.ietf.org/doc/html/rfc4291#section-2.5.5.1
      "::1.2.3.4",
      "::0.1.0.0",
      "::192.168.90.1",
#endif
  };
  char buf[INET6_ADDRSTRLEN];
  IPAddress addr;

  for (const auto& c : cases) {
    EXPECT_TRUE(StringToIPAddress(c, &addr)) << c;
    EXPECT_EQ(addr.ToString(), c);
    std::string packed = addr.ToPackedString();
    EXPECT_EQ(inet_ntop(AF_INET6, packed.data(), buf, INET6_ADDRSTRLEN),
              &buf[0])
        << c;
    EXPECT_EQ(addr.ToString(), std::string(buf)) << c;
  }
}

TEST(IPAddressTest, NonCanonicalIPv6StringValid) {
  IPAddress ip1, ip2;
  // Leading 0.
  EXPECT_TRUE(StringToIPAddress("::01", &ip1));
  EXPECT_TRUE(StringToIPAddress("::1", &ip2));
  EXPECT_EQ(ip1, ip2);
}

TEST(IPAddressTest, IPv6FieldOutOfRange) {
  IPAddress ip;
  EXPECT_FALSE(StringToIPAddress("::fffff", &ip));
}

TEST(IPAddressTest, IPv6FieldTooLong) {
  IPAddress ip;
  EXPECT_FALSE(StringToIPAddress("::0ffff", &ip));
}

TEST(IPAddressTest, IPv6MultipleDoubleColonsInvalid) {
  IPAddress ip;
  EXPECT_FALSE(StringToIPAddress("1::2::3", &ip));
  EXPECT_FALSE(StringToIPAddress("1::::3", &ip));
  EXPECT_FALSE(StringToIPAddress("::2::3", &ip));
  EXPECT_FALSE(StringToIPAddress("1::2::", &ip));
}

TEST(IPAddressTest, IPv6TooFewFields) {
  IPAddress ip;
  EXPECT_FALSE(StringToIPAddress("1:2", &ip));
}

TEST(IPAddressTest, IPv6TooManyFields) {
  IPAddress ip;
  EXPECT_FALSE(StringToIPAddress("1:2:3:4:5:6:7:8:9", &ip));
}

TEST(IPAddressTest, IPv6SkippingZeroFieldsInvalid) {
  IPAddress ip;
  EXPECT_FALSE(StringToIPAddress("1:2:3:4:5:6:7::8", &ip));
  EXPECT_FALSE(StringToIPAddress("::1:2:3:4:5:6:7:8", &ip));
  EXPECT_FALSE(StringToIPAddress("1:2:3:4:5:6:7:8::", &ip));
}

TEST(IPAddressTest, IPv6LeadingColonInvalid) {
  IPAddress ip;
  EXPECT_FALSE(StringToIPAddress(":1:2:3:4:5:6:7:8", &ip));
}

TEST(IPAddressTest, IPv6TrailingColonInvalid) {
  IPAddress ip;
  EXPECT_FALSE(StringToIPAddress("1:2:3:4:5:6:7:8:", &ip));
}

TEST(IPAddressTest, IPv6OnlyColonInvalid) {
  IPAddress ip;
  EXPECT_FALSE(StringToIPAddress(":", &ip));
}

TEST(IPAddressTest, Equality) {
  const std::string kIPv4String1 = "1.2.3.4";
  const std::string kIPv4String2 = "2.3.4.5";
  const std::string kIPv6String1 = "2001:700:300:1800::f";
  const std::string kIPv6String2 = "2001:700:300:1800:0:0:0:f";
  const std::string kIPv6String3 = "::1";

  IPAddress empty;
  IPAddress addr4_1, addr4_2;
  IPAddress addr6_1, addr6_2, addr6_3;

  ASSERT_TRUE(StringToIPAddress(kIPv4String1, &addr4_1));
  ASSERT_TRUE(StringToIPAddress(kIPv4String2, &addr4_2));
  ASSERT_TRUE(StringToIPAddress(kIPv6String1, &addr6_1));
  ASSERT_TRUE(StringToIPAddress(kIPv6String2, &addr6_2));
  ASSERT_TRUE(StringToIPAddress(kIPv6String3, &addr6_3));

  // operator==
  EXPECT_TRUE(empty == empty);
  EXPECT_FALSE(empty == addr4_1);
  EXPECT_FALSE(empty == addr4_2);
  EXPECT_FALSE(empty == addr6_1);
  EXPECT_FALSE(empty == addr6_2);
  EXPECT_FALSE(empty == addr6_3);

  EXPECT_FALSE(addr4_1 == empty);
  EXPECT_TRUE(addr4_1 == addr4_1);
  EXPECT_FALSE(addr4_1 == addr4_2);
  EXPECT_FALSE(addr4_1 == addr6_1);
  EXPECT_FALSE(addr4_1 == addr6_2);
  EXPECT_FALSE(addr4_1 == addr6_3);

  EXPECT_FALSE(addr4_2 == empty);
  EXPECT_FALSE(addr4_2 == addr4_1);
  EXPECT_TRUE(addr4_2 == addr4_2);
  EXPECT_FALSE(addr4_2 == addr6_1);
  EXPECT_FALSE(addr4_2 == addr6_2);
  EXPECT_FALSE(addr4_2 == addr6_3);

  EXPECT_FALSE(addr6_1 == empty);
  EXPECT_FALSE(addr6_1 == addr4_1);
  EXPECT_FALSE(addr6_1 == addr4_2);
  EXPECT_TRUE(addr6_1 == addr6_1);
  EXPECT_TRUE(addr6_1 == addr6_2);
  EXPECT_FALSE(addr6_1 == addr6_3);

  EXPECT_FALSE(addr6_2 == empty);
  EXPECT_FALSE(addr6_2 == addr4_1);
  EXPECT_FALSE(addr6_2 == addr4_2);
  EXPECT_TRUE(addr6_2 == addr6_1);
  EXPECT_TRUE(addr6_2 == addr6_2);
  EXPECT_FALSE(addr6_2 == addr6_3);

  EXPECT_FALSE(addr6_3 == empty);
  EXPECT_FALSE(addr6_3 == addr4_1);
  EXPECT_FALSE(addr6_3 == addr4_2);
  EXPECT_FALSE(addr6_3 == addr6_1);
  EXPECT_FALSE(addr6_3 == addr6_2);
  EXPECT_TRUE(addr6_3 == addr6_3);

  // operator!= (same tests, just inverted)
  EXPECT_FALSE(empty != empty);
  EXPECT_TRUE(empty != addr4_1);
  EXPECT_TRUE(empty != addr4_2);
  EXPECT_TRUE(empty != addr6_1);
  EXPECT_TRUE(empty != addr6_2);
  EXPECT_TRUE(empty != addr6_3);

  EXPECT_TRUE(addr4_1 != empty);
  EXPECT_FALSE(addr4_1 != addr4_1);
  EXPECT_TRUE(addr4_1 != addr4_2);
  EXPECT_TRUE(addr4_1 != addr6_1);
  EXPECT_TRUE(addr4_1 != addr6_2);
  EXPECT_TRUE(addr4_1 != addr6_3);

  EXPECT_TRUE(addr4_2 != empty);
  EXPECT_TRUE(addr4_2 != addr4_1);
  EXPECT_FALSE(addr4_2 != addr4_2);
  EXPECT_TRUE(addr4_2 != addr6_1);
  EXPECT_TRUE(addr4_2 != addr6_2);
  EXPECT_TRUE(addr4_2 != addr6_3);

  EXPECT_TRUE(addr6_1 != empty);
  EXPECT_TRUE(addr6_1 != addr4_1);
  EXPECT_TRUE(addr6_1 != addr4_2);
  EXPECT_FALSE(addr6_1 != addr6_1);
  EXPECT_FALSE(addr6_1 != addr6_2);
  EXPECT_TRUE(addr6_1 != addr6_3);

  EXPECT_TRUE(addr6_2 != empty);
  EXPECT_TRUE(addr6_2 != addr4_1);
  EXPECT_TRUE(addr6_2 != addr4_2);
  EXPECT_FALSE(addr6_2 != addr6_1);
  EXPECT_FALSE(addr6_2 != addr6_2);
  EXPECT_TRUE(addr6_2 != addr6_3);

  EXPECT_TRUE(addr6_3 != empty);
  EXPECT_TRUE(addr6_3 != addr4_1);
  EXPECT_TRUE(addr6_3 != addr4_2);
  EXPECT_TRUE(addr6_3 != addr6_1);
  EXPECT_TRUE(addr6_3 != addr6_2);
  EXPECT_FALSE(addr6_3 != addr6_3);
}

TEST(IPAddressTest, UInt32ToIPAddress) {
  uint32_t addr1 = htonl(0);
  uint32_t addr2 = htonl(0x7f000001);
  uint32_t addr3 = htonl(0xffffffff);

  EXPECT_EQ(UInt32ToIPAddress(addr1).ToString(), "0.0.0.0");
  EXPECT_EQ(UInt32ToIPAddress(addr2).ToString(), "127.0.0.1");
  EXPECT_EQ(UInt32ToIPAddress(addr3).ToString(), "255.255.255.255");
}

TEST(IPAddressTest, HostUInt32ToIPAddress) {
  uint32_t addr1 = 0;
  uint32_t addr2 = 0x7f000001;
  uint32_t addr3 = 0xffffffff;

  EXPECT_EQ(HostUInt32ToIPAddress(addr1).ToString(), "0.0.0.0");
  EXPECT_EQ(HostUInt32ToIPAddress(addr2).ToString(), "127.0.0.1");
  EXPECT_EQ(HostUInt32ToIPAddress(addr3).ToString(), "255.255.255.255");
}

TEST(IPAddressTest, IPAddressToHostUInt32) {
  IPAddress addr = StringToIPAddressOrDie("1.2.3.4");
  EXPECT_EQ(IPAddressToHostUInt32(addr), 0x01020304);
}

TEST(IPAddressTest, UInt128ToIPAddress) {
  absl::uint128 addr1(0);
  absl::uint128 addr2(1);
  absl::uint128 addr3 = absl::MakeUint128(std::numeric_limits<uint64_t>::max(),
                                          std::numeric_limits<uint64_t>::max());

  EXPECT_EQ(UInt128ToIPAddress(addr1).ToString(), "::");
  EXPECT_EQ(UInt128ToIPAddress(addr2).ToString(), "::1");
  EXPECT_EQ(UInt128ToIPAddress(addr3).ToString(),
            "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
}

TEST(IPAddressTest, Constants) {
  EXPECT_EQ(IPAddress::Any4().ToString(), "0.0.0.0");
  EXPECT_EQ(IPAddress::Loopback4().ToString(), "127.0.0.1");
  EXPECT_EQ(IPAddress::Any6().ToString(), "::");
  EXPECT_EQ(IPAddress::Loopback6().ToString(), "::1");

  EXPECT_TRUE(IsAnyIPAddress(IPAddress::Any4()));
  EXPECT_TRUE(IsAnyIPAddress(IPAddress::Any6()));
}

TEST(IPAddressTest, Loopback) {
  IPAddress ip;

  // Canonical loopback IP addresses.
  ip = IPAddress::Loopback4();
  EXPECT_TRUE(IsCanonicalLoopbackIPAddress(ip));
  EXPECT_TRUE(IsLoopbackIPAddress(ip));

  ip = IPAddress::Loopback6();
  EXPECT_TRUE(IsCanonicalLoopbackIPAddress(ip));
  EXPECT_TRUE(IsLoopbackIPAddress(ip));

  // Various addresses near or within 127.0.0.0/8.
  ip = StringToIPAddressOrDie("126.255.255.255");  // before 127
  EXPECT_FALSE(IsCanonicalLoopbackIPAddress(ip));
  EXPECT_FALSE(IsLoopbackIPAddress(ip));

  ip = StringToIPAddressOrDie("127.0.0.0");  // 127/8 begins
  EXPECT_FALSE(IsCanonicalLoopbackIPAddress(ip));
  EXPECT_TRUE(IsLoopbackIPAddress(ip));

  ip = StringToIPAddressOrDie("127.0.0.1");  // standard loopback
  EXPECT_TRUE(IsCanonicalLoopbackIPAddress(ip));
  EXPECT_TRUE(IsLoopbackIPAddress(ip));

  ip = StringToIPAddressOrDie("::ffff:127.0.0.0");  // ipv4 mapped notation
  EXPECT_FALSE(IsCanonicalLoopbackIPAddress(ip));
  EXPECT_TRUE(IsLoopbackIPAddress(ip));

  ip = StringToIPAddressOrDie("::ffff:127.0.0.1");  // ipv4 mapped notation
  EXPECT_TRUE(IsCanonicalLoopbackIPAddress(ip));
  EXPECT_TRUE(IsLoopbackIPAddress(ip));

  ip = StringToIPAddressOrDie("127.1.2.3");  // non-standard loopback
  EXPECT_FALSE(IsCanonicalLoopbackIPAddress(ip));
  EXPECT_TRUE(IsLoopbackIPAddress(ip));

  ip = StringToIPAddressOrDie("127.255.255.255");  // 127/8 ends
  EXPECT_FALSE(IsCanonicalLoopbackIPAddress(ip));
  EXPECT_TRUE(IsLoopbackIPAddress(ip));

  ip = StringToIPAddressOrDie("128.0.0.0");  // after 127
  EXPECT_FALSE(IsCanonicalLoopbackIPAddress(ip));
  EXPECT_FALSE(IsLoopbackIPAddress(ip));

  // before Google IPv6 Loopback ULA /48
  ip = StringToIPAddressOrDie("fd14:988a:50ed:ffff:ffff:ffff:ffff:ffff");
  EXPECT_FALSE(IsCanonicalLoopbackIPAddress(ip));
  EXPECT_FALSE(IsLoopbackIPAddress(ip));

  // beginning of the Google IPv6 loopback ULA /48,
  // this is also in the loopback ULA /64
  ip = StringToIPAddressOrDie("fd14:988a:50ee:1006::");
  EXPECT_FALSE(IsCanonicalLoopbackIPAddress(ip));
  EXPECT_TRUE(IsLoopbackIPAddress(ip));

  ip = StringToIPAddressOrDie("fd14:988a:50ee:1006::1");
  EXPECT_FALSE(IsCanonicalLoopbackIPAddress(ip));
  EXPECT_TRUE(IsLoopbackIPAddress(ip));

  // first address outside the loopback ULA /64
  ip = StringToIPAddressOrDie("fd14:988a:50ee:1007::");
  EXPECT_FALSE(IsCanonicalLoopbackIPAddress(ip));
  EXPECT_FALSE(IsLoopbackIPAddress(ip));

  // before gprivce
  ip = StringToIPAddressOrDie("fd14:988a:50ee:10b:ffff:ffff:ffff:ffff");
  EXPECT_FALSE(IsCanonicalLoopbackIPAddress(ip));
  EXPECT_FALSE(IsLoopbackIPAddress(ip));

  // first gprivce ULA /64 address
  ip = StringToIPAddressOrDie("fd14:988a:50ee:10c::");
  EXPECT_FALSE(IsCanonicalLoopbackIPAddress(ip));
  EXPECT_FALSE(IsLoopbackIPAddress(ip));

  // last gprivce ULA /64 address
  ip = StringToIPAddressOrDie("fd14:988a:50ee:10c:ffff:ffff:ffff:ffff");
  EXPECT_FALSE(IsCanonicalLoopbackIPAddress(ip));
  EXPECT_FALSE(IsLoopbackIPAddress(ip));

  // after gprivce
  ip = StringToIPAddressOrDie("fd14:988a:50ee:10d::");
  EXPECT_FALSE(IsCanonicalLoopbackIPAddress(ip));
  EXPECT_FALSE(IsLoopbackIPAddress(ip));

  // end of Google IPv6 loopback /48
  ip = StringToIPAddressOrDie("fd14:988a:50ee:ffff:ffff:ffff:ffff:ffff");
  EXPECT_FALSE(IsCanonicalLoopbackIPAddress(ip));
  EXPECT_FALSE(IsLoopbackIPAddress(ip));

  // first address past the end of the Google IPv6 loopback /48
  ip = StringToIPAddressOrDie("fd14:988a:50ef::");
  EXPECT_FALSE(IsCanonicalLoopbackIPAddress(ip));
  EXPECT_FALSE(IsLoopbackIPAddress(ip));

  // Some random non-loopback addresses.
  ip = StringToIPAddressOrDie("10.0.0.1");
  EXPECT_FALSE(IsCanonicalLoopbackIPAddress(ip));
  EXPECT_FALSE(IsLoopbackIPAddress(ip));

  ip = StringToIPAddressOrDie("2001:700:300:1803:b0ff::12");
  EXPECT_FALSE(IsCanonicalLoopbackIPAddress(ip));
  EXPECT_FALSE(IsLoopbackIPAddress(ip));

  // 0.0.0.0 and ::.
  ip = IPAddress::Any4();
  EXPECT_FALSE(IsCanonicalLoopbackIPAddress(ip));
  EXPECT_FALSE(IsLoopbackIPAddress(ip));

  ip = IPAddress::Any6();
  EXPECT_FALSE(IsCanonicalLoopbackIPAddress(ip));
  EXPECT_FALSE(IsLoopbackIPAddress(ip));
}

TEST(IPAddressTest, Logging) {
  const std::string kIPv4String = "1.2.3.4";
  const std::string kIPv6String = "2001:700:300:1800::f";
  IPAddress addr4, addr6;

  ASSERT_TRUE(StringToIPAddress(kIPv4String, &addr4));
  ASSERT_TRUE(StringToIPAddress(kIPv6String, &addr6));

  EXPECT_EQ(absl::StrCat(addr4, " ", addr6), "1.2.3.4 2001:700:300:1800::f");

  std::ostringstream out;
  out << addr4 << " " << addr6;
  EXPECT_EQ(out.str(), "1.2.3.4 2001:700:300:1800::f");
}

TEST(IPAddressTest, LoggingUninitialized) {
  EXPECT_EQ(absl::StrCat(IPAddress()), "<uninitialized IPAddress>");

  std::ostringstream out;
  out << IPAddress();
  EXPECT_EQ(out.str(), "<uninitialized IPAddress>");
}

// Adapted from dnscache_unittest.cc.

TEST(IPAddressTest, Joining) {
  std::vector<IPAddress> v = {
      StringToIPAddressOrDie("192.0.2.0"), StringToIPAddressOrDie("2001:db8::"),
      StringToIPAddressOrDie("0.0.0.0"), StringToIPAddressOrDie("::")};
  EXPECT_EQ(absl::StrJoin(v, "!!!", IPAddressJoinFormatter()),
            "192.0.2.0!!!2001:db8::!!!0.0.0.0!!!::");
}

class ChooseRandomAddressParamTest : public testing::TestWithParam<int> {};

TEST_P(ChooseRandomAddressParamTest, Vector) {
  const int N = GetParam();

  absl::FixedArray<int> count(N);
  std::vector<IPAddress> ipvec;
  ipvec.reserve(N);
  for (int i = 0; i < N; i++) {
    in6_addr ip6;
    for (int j = 0; j < 8; ++j) {
      ip6.s6_addr16[j] = i * (j + 1);
    }

    ipvec.push_back(IPAddress(ip6));
    count[i] = 0;
  }

  // Ensure that if we do 100*N, we get at least each address once.
  for (int i = 0; i < N * 100; i++) {
    IPAddress ip = ChooseRandomIPAddress(ipvec);
    const int id = ip.ipv6_address().s6_addr16[0];
    ASSERT_GE(id, 0);
    ASSERT_LT(id, N);
    EXPECT_EQ(ip, ipvec[id]);
    count[id]++;
  }

  for (int i = 0; i < N; i++) {
    EXPECT_GT(count[i], 0);
  }
}

INSTANTIATE_TEST_SUITE_P(, ChooseRandomAddressParamTest,
                         testing::Values(1, 2, 10, 40));

TEST(IPAddressTest, ChooseRandomAddressInvalid) {
  std::vector<IPAddress> vec;
  EXPECT_DEBUG_DEATH(ChooseRandomIPAddress(vec), "empty list");
  if (!DEBUG_MODE) {
    EXPECT_EQ(IPAddress(), ChooseRandomIPAddress(vec));
  }

  IPRange uninitialized_range;
  EXPECT_DEBUG_DEATH(ChooseRandomIPAddress(uninitialized_range),
                     "uninitialized range");
  if (!DEBUG_MODE) {
    EXPECT_FALSE(
        IsInitializedAddress(ChooseRandomIPAddress(uninitialized_range)));
  }
}

TEST(IPAddressTest, IPAddressOrdering) {
  const std::string kIPv4String1 = "1.2.3.4";
  const std::string kIPv4String2 = "4.3.2.1";
  const std::string kIPv6String1 = "2001:700:300:1800::f";
  const std::string kIPv6String2 = "2001:700:300:1800:0:0:0:f";
  const std::string kIPv6String3 = "::1";
  const std::string kIPv6String4 = "::4";

  IPAddress addr0;  // uninitialized
  IPAddress addr4_1, addr4_2;
  IPAddress addr6_1, addr6_2, addr6_3, addr6_4;

  ASSERT_TRUE(StringToIPAddress(kIPv4String1, &addr4_1));
  ASSERT_TRUE(StringToIPAddress(kIPv4String2, &addr4_2));
  ASSERT_TRUE(StringToIPAddress(kIPv6String1, &addr6_1));
  ASSERT_TRUE(StringToIPAddress(kIPv6String2, &addr6_2));
  ASSERT_TRUE(StringToIPAddress(kIPv6String3, &addr6_3));
  ASSERT_TRUE(StringToIPAddress(kIPv6String4, &addr6_4));

  // std::set
  EXPECT_THAT((std::set<IPAddress>{
                  addr6_2,
                  addr4_2,
                  addr6_1,
                  addr4_1,
                  addr0,
                  addr6_3,
                  addr6_4,
              }),
              ElementsAreArray({
                  addr0,
                  addr4_1,
                  addr4_2,
                  addr6_3,
                  addr6_4,
                  addr6_1,
              }));

  // Pairwise checks
  EXPECT_FALSE(addr0 < addr0);
  EXPECT_TRUE(addr0 < addr4_1);
  EXPECT_FALSE(addr4_1 < addr0);

  EXPECT_TRUE(addr0 <= addr0);
  EXPECT_TRUE(addr0 <= addr4_1);
  EXPECT_FALSE(addr4_1 <= addr0);

  EXPECT_TRUE(addr4_1 >= addr4_1);
  EXPECT_TRUE(addr4_1 >= addr0);
  EXPECT_FALSE(addr0 >= addr4_1);

  EXPECT_FALSE(addr4_1 > addr4_1);
  EXPECT_TRUE(addr4_1 > addr0);
  EXPECT_FALSE(addr0 > addr4_1);

#if __cplusplus >= 202002L
  EXPECT_EQ(std::weak_ordering::equivalent, addr0 <=> addr0);
  EXPECT_EQ(std::weak_ordering::equivalent, addr4_1 <=> addr4_1);
  EXPECT_EQ(std::weak_ordering::less, addr0 <=> addr4_1);
  EXPECT_EQ(std::weak_ordering::greater, addr4_1 <=> addr0);
#endif
}

TEST(IPAddressTest, Hash) {
  const std::string kIPv4String1 = "1.2.3.4";
  const std::string kIPv4String2 = "4.3.2.1";
  const std::string kIPv6String1 = "2001:700:300:1800::f";
  const std::string kIPv6String2 = "2001:700:300:1800:0:0:0:f";
  const std::string kIPv6String3 = "::1";
  const std::string kIPv6String4 = "::4";

  IPAddress addr0;
  IPAddress addr4_1, addr4_2;
  IPAddress addr6_1, addr6_2, addr6_3, addr6_4;

  ASSERT_TRUE(StringToIPAddress(kIPv4String1, &addr4_1));
  ASSERT_TRUE(StringToIPAddress(kIPv4String2, &addr4_2));
  ASSERT_TRUE(StringToIPAddress(kIPv6String1, &addr6_1));
  ASSERT_TRUE(StringToIPAddress(kIPv6String2, &addr6_2));
  ASSERT_TRUE(StringToIPAddress(kIPv6String3, &addr6_3));
  ASSERT_TRUE(StringToIPAddress(kIPv6String4, &addr6_4));

  absl::node_hash_set<IPAddress> addrs;
  addrs.insert(addr0);
  addrs.insert(IPAddress());
  addrs.insert(addr6_2);
  addrs.insert(addr4_2);
  addrs.insert(addr6_1);
  addrs.insert(addr4_1);
  addrs.insert(addr6_3);
  addrs.insert(addr6_4);

  EXPECT_EQ(6, addrs.size());

  EXPECT_EQ(1, addrs.count(addr0));
  EXPECT_EQ(1, addrs.count(addr4_1));
  EXPECT_EQ(1, addrs.count(addr4_2));
  EXPECT_EQ(1, addrs.count(addr6_1));
  EXPECT_EQ(1, addrs.count(addr6_2));
  EXPECT_EQ(1, addrs.count(addr6_3));
  EXPECT_EQ(1, addrs.count(addr6_4));

  // Also test the absl::Hash version.
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(std::make_tuple(
      addr0, addr4_1, addr4_2, addr6_1, addr6_2, addr6_3, addr6_4)));
}

TEST(IPAddressTest, v6Mapped) {
  const std::string kIPv4String = "1.2.3.4";
  const std::string kCompatibleIPString = "::1.2.3.4";
  const std::string kMappedIPString = "::ffff:1.2.3.4";
  IPAddress addr4, compatible_addr, mapped_addr;

  ASSERT_TRUE(StringToIPAddress(kIPv4String, &addr4));
  ASSERT_TRUE(StringToIPAddress(kCompatibleIPString, &compatible_addr));
  ASSERT_TRUE(StringToIPAddress(kMappedIPString, &mapped_addr));
  EXPECT_EQ(mapped_addr.ToString(), kMappedIPString);
  EXPECT_EQ(compatible_addr.ToString(), kCompatibleIPString);

  // We've specified explicitly that these should be distinct --
  // one might agree or disagree with the decision, but as long as
  // it stands, we should test the behavior.
  EXPECT_FALSE(addr4 == mapped_addr);
  EXPECT_TRUE(addr4 != mapped_addr);

  IPAddress compare4 = IPAddress::Any4();
  EXPECT_FALSE(GetCompatIPv4Address(mapped_addr, nullptr));
  EXPECT_TRUE(GetMappedIPv4Address(mapped_addr, nullptr));
  EXPECT_TRUE(GetMappedIPv4Address(mapped_addr, &compare4));
  EXPECT_TRUE(addr4 == compare4);

  EXPECT_FALSE(addr4 == compatible_addr);
  EXPECT_TRUE(addr4 != compatible_addr);

  compare4 = IPAddress::Any4();
  EXPECT_FALSE(GetMappedIPv4Address(compatible_addr, nullptr));
  EXPECT_TRUE(GetCompatIPv4Address(compatible_addr, nullptr));
  EXPECT_TRUE(GetCompatIPv4Address(compatible_addr, &compare4));
  EXPECT_TRUE(addr4 == compare4);

  EXPECT_FALSE(mapped_addr == compatible_addr);
  EXPECT_TRUE(mapped_addr != compatible_addr);

  // Test ordering.
  EXPECT_TRUE(addr4 < mapped_addr);
  EXPECT_FALSE(mapped_addr < addr4);

  EXPECT_TRUE(addr4 < compatible_addr);
  EXPECT_FALSE(compatible_addr < addr4);

  // Test hashing.
  absl::node_hash_set<IPAddress> addrs;
  addrs.insert(addr4);
  addrs.insert(mapped_addr);
  addrs.insert(compatible_addr);
  EXPECT_EQ(3, addrs.size());
}

TEST(IPAddressTest, IPv6LinkLocal) {
  const IPAddress fe80_1 = StringToIPAddressOrDie("fe80::1");
  const IPAddress fe80_2 = StringToIPAddressOrDie("fe80::2");
  const IPAddress fe80_1_if17(MakeScopedIP(fe80_1, 17));
  const IPAddress fe80_2_if17(MakeScopedIP(fe80_2, 17));
  const IPAddress fe80_2_if22(MakeScopedIP(fe80_2, 22));
  const IPAddress ff02_2 = StringToIPAddressOrDie("ff02::2");  // all-routers
  const IPAddress ff02_2_if17(MakeScopedIP(ff02_2, 17));

  // IPAddress::scope_id()
  EXPECT_EQ(0, fe80_1.scope_id());
  EXPECT_EQ(0, fe80_2.scope_id());
  EXPECT_EQ(17, fe80_1_if17.scope_id());
  EXPECT_EQ(17, fe80_2_if17.scope_id());
  EXPECT_EQ(22, fe80_2_if22.scope_id());
  EXPECT_EQ(0, ff02_2.scope_id());
  EXPECT_EQ(17, ff02_2_if17.scope_id());

  for (const auto& ip : {fe80_1, fe80_2, fe80_1_if17, fe80_2_if17, fe80_2_if22,
                         ff02_2, ff02_2_if17}) {
    EXPECT_TRUE(IsInitializedAddress(ip));
    EXPECT_EQ(AF_INET6, ip.address_family());
    EXPECT_EQ(128, IPAddressLength(ip));
  }

  // !=
  EXPECT_TRUE(fe80_1 != fe80_2);
  EXPECT_TRUE(fe80_1 != fe80_1_if17);
  EXPECT_TRUE(fe80_2 != fe80_2_if17);
  EXPECT_TRUE(fe80_2 != fe80_2_if22);
  EXPECT_TRUE(fe80_1_if17 != fe80_2_if17);
  EXPECT_TRUE(fe80_2_if17 != fe80_2_if22);

  // ==
  EXPECT_TRUE(fe80_1_if17 == fe80_1_if17);
  EXPECT_TRUE(fe80_2_if17 == fe80_2_if17);
  EXPECT_TRUE(fe80_2_if22 == fe80_2_if22);
  EXPECT_TRUE(fe80_1 == IPAddress(fe80_1_if17.ipv6_address()));
  EXPECT_TRUE(fe80_2 == IPAddress(fe80_2_if17.ipv6_address()));
  EXPECT_TRUE(fe80_2 == IPAddress(fe80_2_if22.ipv6_address()));

  // Check that we can't be super tricksy with the implementation to create
  // a collision.
  in6_addr addr6 = fe80_2_if17.ipv6_address();
  EXPECT_NE(fe80_2_if17, IPAddress(addr6));
  LittleEndian::Store32(addr6.s6_addr16 + 2, 17);
  EXPECT_NE(fe80_2_if17, IPAddress(addr6));
  BigEndian::Store32(addr6.s6_addr16 + 2, 17);
  EXPECT_NE(fe80_2_if17, IPAddress(addr6));

  // Ordering
  EXPECT_TRUE(IPAddress::Any4() < fe80_1);
  EXPECT_TRUE(IPAddress::Any6() < fe80_1);
  EXPECT_TRUE(fe80_1 < fe80_2);
  EXPECT_TRUE(fe80_1 < fe80_1_if17);
  EXPECT_TRUE(fe80_1_if17 < fe80_2);
  EXPECT_TRUE(fe80_1_if17 < fe80_2_if17);
  EXPECT_TRUE(fe80_2_if17 < fe80_2_if22);
  EXPECT_TRUE(fe80_2_if22 < ff02_2);
  EXPECT_TRUE(ff02_2 < ff02_2_if17);

#if __cplusplus >= 202002L
  EXPECT_EQ(std::weak_ordering::less, IPAddress::Any4() <=> fe80_1);
  EXPECT_EQ(std::weak_ordering::less, IPAddress::Any6() <=> fe80_1);
  EXPECT_EQ(std::weak_ordering::less, fe80_1 <=> fe80_2);
  EXPECT_EQ(std::weak_ordering::less, fe80_1 <=> fe80_1_if17);
  EXPECT_EQ(std::weak_ordering::less, fe80_1_if17 <=> fe80_2);
  EXPECT_EQ(std::weak_ordering::less, fe80_1_if17 <=> fe80_2_if17);
  EXPECT_EQ(std::weak_ordering::less, fe80_2_if17 <=> fe80_2_if22);
  EXPECT_EQ(std::weak_ordering::less, fe80_2_if22 <=> ff02_2);
  EXPECT_EQ(std::weak_ordering::less, ff02_2 <=> ff02_2_if17);
#endif

  // Hash
  absl::flat_hash_set<IPAddress> addrs;
  addrs.insert(fe80_1);
  addrs.insert(fe80_2);
  addrs.insert(fe80_1_if17);
  addrs.insert(fe80_2_if17);
  addrs.insert(fe80_2_if22);
  addrs.insert(ff02_2);
  addrs.insert(ff02_2_if17);

  EXPECT_EQ(7, addrs.size());

  EXPECT_EQ(1, addrs.count(fe80_1));
  EXPECT_EQ(1, addrs.count(fe80_2));
  EXPECT_EQ(1, addrs.count(fe80_1_if17));
  EXPECT_EQ(1, addrs.count(fe80_2_if17));
  EXPECT_EQ(1, addrs.count(fe80_2_if22));
  EXPECT_EQ(1, addrs.count(ff02_2));
  EXPECT_EQ(1, addrs.count(ff02_2_if17));

  // Also test the absl::Hash version.
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      std::make_tuple(fe80_1, fe80_2, fe80_1_if17, fe80_2_if17, fe80_2_if22,
                      ff02_2, ff02_2_if17)));

  // Double-check that absence of a scope descriptor is not evidence of the
  // absence of a working IP string literal parser.
  EXPECT_EQ(ff02_2,
            StringToIPAddressWithOptionalScope(ff02_2.ToString()).value());

  // Appending a scope delimiter ('%') without a following zone_id does not
  // seem to comport with any of this text:
  //
  //     https://tools.ietf.org/html/rfc4007#section-11.2
  //     https://tools.ietf.org/html/rfc4007#section-11.6
  //     https://tools.ietf.org/html/rfc6874#section-2
  //
  // so check that it's treated as an error.
  EXPECT_FALSE(StringToIPAddressWithOptionalScope("fe80::%").ok());
  // Appending a default zone_id of "0" is, however, perfectly fine.
  EXPECT_EQ(StringToIPAddressOrDie("fe80::"),
            StringToIPAddressWithOptionalScope("fe80::%0").value());

  EXPECT_EQ(fe80_2_if22, NormalizeIPAddress(fe80_2_if22));
  EXPECT_EQ(fe80_2_if22, DualstackIPAddress(fe80_2_if22));

  // For now verify that PackedString representation is identical to the
  // the un-scoped address implementation. How to meaningfully serialize and
  // de-serialize an interface index or name is left as an exercise for the
  // application.
  EXPECT_EQ(fe80_2.ToPackedString(), fe80_2_if17.ToPackedString());
  EXPECT_EQ(fe80_2.ToPackedString(), fe80_2_if22.ToPackedString());
}

TEST(IPAddressTest, IPv6ScopeIdCompactSafety) {
  using ::absl_testing::StatusIs;

  // Repro b/519294482
  EXPECT_NE(StringToIPAddressOrDie("fe80:0:1::2"),
            StringToIPAddressOrDie("fe80::2"));
  EXPECT_NE(StringToIPAddressOrDie("ff02:0:1::2"),
            StringToIPAddressOrDie("ff02::2"));

  // Verify that we cannot assign a scope ID to these addresses because they
  // cannot be compactly stored without stomping.
  const IPAddress fe80_0_1_2 = StringToIPAddressOrDie("fe80:0:1::2");
  EXPECT_THAT(MakeIPAddressWithScopeId(fe80_0_1_2.ipv6_address(), 5),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(StringToIPAddressWithOptionalScope("fe80:0:1::2%5"),
              StatusIs(absl::StatusCode::kInvalidArgument));

  const IPAddress ff02_0_1_2 = StringToIPAddressOrDie("ff02:0:1::2");
  EXPECT_THAT(MakeIPAddressWithScopeId(ff02_0_1_2.ipv6_address(), 5),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(StringToIPAddressWithOptionalScope("ff02:0:1::2%5"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

// Test case shamelessly lifted from:
//
//     http://en.wikipedia.org/wiki/6to4#Address_block_allocation
//
// """
// Thus for the global IPv4 address 207.142.131.202, the corresponding
// 6to4 prefix would be 2002:CF8E:83CA::/48.
// """
TEST(IPAddressTest, Get6to4IPv4Address) {
  const IPAddress addr4 = StringToIPAddressOrDie("207.142.131.202");
  const IPAddress addr6 = StringToIPAddressOrDie("2002:cf8e:83ca::");
  IPAddress compare4;

  EXPECT_FALSE(Get6to4IPv4Address(addr4, nullptr));
  EXPECT_TRUE(Get6to4IPv4Address(addr6, nullptr));
  EXPECT_TRUE(Get6to4IPv4Address(addr6, &compare4));
  EXPECT_EQ(addr4, compare4);
}

TEST(IPAddressTest, Get6to4IPv6Range) {
  IPRange iprange6;

  const IPAddress addr4 = StringToIPAddressOrDie("207.142.131.202");
  const IPAddress addr6 = StringToIPAddressOrDie("2002:cf8e:83ca::");

  EXPECT_FALSE(Get6to4IPv6Range(IPRange(addr6), nullptr));
  EXPECT_FALSE(Get6to4IPv6Range(IPRange::Any6(), nullptr));

  EXPECT_TRUE(Get6to4IPv6Range(IPRange::Any4(), &iprange6));
  EXPECT_EQ(StringToIPRangeOrDie("2002::/16"), iprange6);

  EXPECT_TRUE(Get6to4IPv6Range(IPRange(addr4), nullptr));
  EXPECT_TRUE(Get6to4IPv6Range(IPRange(addr4), &iprange6));
  EXPECT_EQ(StringToIPRangeOrDie("2002:cf8e:83ca::/48"), iprange6);

  for (int len4 = 0; len4 <= 32; len4++) {
    const int len6 = len4 + 16;
    EXPECT_TRUE(Get6to4IPv6Range(IPRange(addr4, len4), &iprange6));
    EXPECT_EQ(IPRange(addr6, len6), iprange6);
    EXPECT_EQ(TruncateIPAddress(addr6, len6), NthAddressInRange(iprange6, 0));
    // Make sure reverse direction also works.
    IPAddress compare4;
    EXPECT_TRUE(Get6to4IPv4Address(NthAddressInRange(iprange6, 0), &compare4));
    EXPECT_EQ(TruncateIPAddress(addr4, len4), compare4);
  }
}

TEST(IPAddressTest, GetIsatapIPv4Address) {
  const std::string kIPv4Address = "207.142.131.202";
  const std::string kBadIsatapAddress = "2001:db8::0040:5efe:cf8e:83ca";
  const std::string kTeredoAddress = "2001:0:102:203:200:5efe:506:708";
  const char* kIsatapAddresses[] = {
      "2001:db8::5efe:cf8e:83ca",
      "2001:db8::100:5efe:cf8e:83ca",  // Private Multicast? Not likely.
      "2001:db8::200:5efe:cf8e:83ca",
      "2001:db8::300:5efe:cf8e:83ca"  // Public Multicast? Also unlikely.
  };
  IPAddress addr4, addr6, compare4;

  ASSERT_TRUE(StringToIPAddress(kIPv4Address, &addr4));
  EXPECT_FALSE(GetIsatapIPv4Address(addr4, nullptr));

  ASSERT_TRUE(StringToIPAddress(kBadIsatapAddress, &addr6));
  EXPECT_FALSE(GetIsatapIPv4Address(addr6, nullptr));

  ASSERT_TRUE(StringToIPAddress(kTeredoAddress, &addr6));
  EXPECT_TRUE(GetTeredoInfo(addr6, nullptr, nullptr, nullptr, nullptr));
  EXPECT_FALSE(GetIsatapIPv4Address(addr6, nullptr));

  for (int i = 0; i < std::size(kIsatapAddresses); i++) {
    ASSERT_TRUE(StringToIPAddress(kIsatapAddresses[i], &addr6));
    EXPECT_TRUE(GetIsatapIPv4Address(addr6, nullptr));
    EXPECT_TRUE(GetIsatapIPv4Address(addr6, &compare4));
    EXPECT_TRUE(addr4 == compare4);
  }
}

// Shamelessly lifted from:
//
//     http://en.wikipedia.org/wiki/Teredo_tunneling#Teredo_IPv6_addressing
//
// """
// As an example, 2001:0000:4136:e378:8000:63bf:3fff:fdd2 refers to a
// Teredo client:
//
//     * using Teredo server at address 65.54.227.120
//       (4136e378 in hexadecimal),
//     * located behind a cone NAT (bit 64 is set),
//     * using UDP mapped port 40000 on its NAT
//       (in hexadecimal 63bf xor ffff equals 9c40, or decimal number 40000),
//     * whose NAT has public IPv4 address 192.0.2.45
//       (3ffffdd2 xor ffffffff equals c000022d, which is to say 192.0.2.45).
// """
TEST(IPAddressTest, GetTeredoInfo) {
  const std::string kTeredoAddress = "2001:0000:4136:e378:8000:63bf:3fff:fdd2";
  const std::string kTeredoServer = "65.54.227.120";
  const uint16_t kFlags = 0x8000;
  const uint16_t kPort = 40000;
  const std::string kTeredoClient = "192.0.2.45";

  IPAddress addr4c, addr4s, addr6, client, server;
  uint16_t flags, port;

  ASSERT_TRUE(StringToIPAddress(kTeredoAddress, &addr6));
  ASSERT_TRUE(StringToIPAddress(kTeredoClient, &addr4c));
  ASSERT_TRUE(StringToIPAddress(kTeredoServer, &addr4s));
  EXPECT_FALSE(GetTeredoInfo(addr4c, nullptr, nullptr, nullptr, nullptr));
  EXPECT_TRUE(GetTeredoInfo(addr6, nullptr, nullptr, nullptr, nullptr));
  EXPECT_TRUE(GetTeredoInfo(addr6, &server, &flags, &port, &client));
  EXPECT_EQ(server, addr4s);
  EXPECT_EQ(flags, kFlags);
  EXPECT_EQ(port, kPort);
  EXPECT_EQ(client, addr4c);
}

TEST(IPAddressTest, GetEmbeddedIPv4ClientAddress) {
  const std::string kIPv4String = "1.2.3.4";
  const std::string kCompatibleIPString = "::1.2.3.4";
  const std::string kMappedIPString = "::ffff:1.2.3.4";
  const std::string kTeredoClient = "192.0.2.45";
  const std::string kTeredoAddress = "2001:0000:4136:e378:8000:63bf:3fff:fdd2";
  const std::string kIPv4Address = "207.142.131.202";
  const std::string k6to4Address = "2002:cf8e:83ca::";
  const std::string kIsatapAddress = "2001:db8::200:5efe:cf8e:83ca";

  IPAddress ip4, ip6, embedded;

  // IPv4 address.
  ASSERT_TRUE(StringToIPAddress(kIPv4String, &ip4));
  EXPECT_FALSE(GetEmbeddedIPv4ClientAddress(ip4, nullptr));

  // Compatible IPv4 address.
  ASSERT_TRUE(StringToIPAddress(kCompatibleIPString, &ip6));
  EXPECT_TRUE(GetEmbeddedIPv4ClientAddress(ip6, &embedded));
  EXPECT_EQ(ip4, embedded);

  // Mapped IPv6 address.
  ASSERT_TRUE(StringToIPAddress(kMappedIPString, &ip6));
  EXPECT_TRUE(GetEmbeddedIPv4ClientAddress(ip6, &embedded));
  EXPECT_EQ(ip4, embedded);

  // Teredo.
  ASSERT_TRUE(StringToIPAddress(kTeredoClient, &ip4));
  ASSERT_TRUE(StringToIPAddress(kTeredoAddress, &ip6));
  EXPECT_TRUE(GetEmbeddedIPv4ClientAddress(ip6, &embedded));
  EXPECT_EQ(ip4, embedded);

  // 6to4.
  ASSERT_TRUE(StringToIPAddress(kIPv4Address, &ip4));
  ASSERT_TRUE(StringToIPAddress(k6to4Address, &ip6));
  EXPECT_TRUE(GetEmbeddedIPv4ClientAddress(ip6, &embedded));
  EXPECT_EQ(ip4, embedded);

  // ISATAP: Assert that ISATAP addresses, so easily spoofable,
  // do not find their way into this method by some future chance.
  ASSERT_TRUE(StringToIPAddress(kIPv4Address, &ip4));
  ASSERT_TRUE(StringToIPAddress(kIsatapAddress, &ip6));
  EXPECT_FALSE(GetEmbeddedIPv4ClientAddress(ip6, &embedded));
}

TEST(IPAddressTest, GetCoercedIPv4Address_Special) {
  const std::string kIPv4String = "1.2.3.4";
  const std::string kCompatibleIPString = "::1.2.3.4";
  const std::string kMappedIPString = "::ffff:1.2.3.4";
  const std::string kTeredoClient = "192.0.2.45";
  const std::string kTeredoAddress = "2001:0000:4136:e378:8000:63bf:3fff:fdd2";
  const std::string kIPv4Address = "207.142.131.202";
  const std::string k6to4Address = "2002:cf8e:83ca::";
  const std::string kLocalhost6Address = "::1";
  const std::string kLocalhost4Address = "127.0.0.1";
  const std::string kAny6Address = "::";
  const std::string kAny4Address = "0.0.0.0";

  IPAddress ip4, ip6, coerced;

  // IPv4 address.
  ASSERT_TRUE(StringToIPAddress(kIPv4String, &ip4));
  coerced = GetCoercedIPv4Address(ip4);
  EXPECT_EQ(ip4, coerced);

  // Compatible IPv4 address.
  ASSERT_TRUE(StringToIPAddress(kCompatibleIPString, &ip6));
  coerced = GetCoercedIPv4Address(ip6);
  EXPECT_NE(ip4, coerced);

  // Mapped IPv6 address.
  ASSERT_TRUE(StringToIPAddress(kMappedIPString, &ip6));
  coerced = GetCoercedIPv4Address(ip6);
  EXPECT_NE(ip4, coerced);

  // Teredo.
  ASSERT_TRUE(StringToIPAddress(kTeredoClient, &ip4));
  ASSERT_TRUE(StringToIPAddress(kTeredoAddress, &ip6));
  coerced = GetCoercedIPv4Address(ip6);
  EXPECT_NE(ip4, coerced);

  // 6to4.
  ASSERT_TRUE(StringToIPAddress(kIPv4Address, &ip4));
  ASSERT_TRUE(StringToIPAddress(k6to4Address, &ip6));
  coerced = GetCoercedIPv4Address(ip6);
  EXPECT_NE(ip4, coerced);

  // Localhost (special case).
  ASSERT_TRUE(StringToIPAddress(kLocalhost4Address, &ip4));
  ASSERT_TRUE(StringToIPAddress(kLocalhost6Address, &ip6));
  coerced = GetCoercedIPv4Address(ip6);
  EXPECT_EQ(ip4, coerced);

  // Any address (special case).
  ASSERT_TRUE(StringToIPAddress(kAny4Address, &ip4));
  ASSERT_TRUE(StringToIPAddress(kAny6Address, &ip6));
  coerced = GetCoercedIPv4Address(ip6);
  EXPECT_EQ(ip4, coerced);
}

TEST(IPAddressTest, GetCoercedIPv4Address_Hashed_GeneralProperties) {
  const int kMaxIterations = 300;
  IPAddress ip6, coerced;
  in6_addr addr6;

  absl::BitGen gen;
  for (int i = 0; i < kMaxIterations; i++) {
    std::generate(std::begin(addr6.s6_addr16), std::end(addr6.s6_addr16),
                  [&gen]() { return absl::Uniform<uint16_t>(gen); });

    // Make sure the address doesn't randomly end up being any kind of
    // address that would return a "fixed" IPv4 address, i.e. make sure
    // it's not 6to4, Teredo, etc.  So just pretend it's a 6bone (v2)
    // address.  See http://tools.ietf.org/html/rfc3701 for 6bone phaseout.
    addr6.s6_addr16[0] = ghtons(0x3ffe);

    ip6 = IPAddress(addr6);
    coerced = GetCoercedIPv4Address(ip6);

    SCOPED_TRACE(absl::StrFormat("iter[%d]: ip6 '%s', coerced '%s'", i,
                                 ip6.ToString(), coerced.ToString()));

    // Make sure it's in the multicast + 240reserved space.
    uint32_t high_byte =
        ((gntohl(coerced.ipv4_address().s_addr) >> 24) & 0x000000ff);
    EXPECT_GE(high_byte, 224);

    // Make sure it's not all 1's.
    EXPECT_NE(coerced.ipv4_address().s_addr, 0xffffffff);

    // Make sure it's repeatable.
    EXPECT_EQ(coerced, GetCoercedIPv4Address(ip6));
  }
}

// Although the mapping is arbitrary, we want consistent IPv6 -> IPv4 hashing
// over time per platform. Thus, this test makes a basic sanity check for
// a specific address.
TEST(IPAddressTest, GetCoercedIPv4Address_Hashed_SpecificExample) {
  IPAddress addr, coerced;

  ASSERT_TRUE(StringToIPAddress("2001:4860::1", &addr));
  const std::string expected_address = "231.31.35.241";
  ASSERT_TRUE(StringToIPAddress(expected_address, &coerced));

  EXPECT_EQ(coerced, GetCoercedIPv4Address(addr));
}

TEST(IPAddressTest, NormalizeIPAddress) {
  IPAddress addr4, mapped_addr, compat_addr;

  ASSERT_TRUE(StringToIPAddress("129.241.93.35", &addr4));
  ASSERT_TRUE(StringToIPAddress("::ffff:129.241.93.35", &mapped_addr));
  ASSERT_TRUE(StringToIPAddress("::129.241.93.35", &compat_addr));

  EXPECT_EQ(addr4, NormalizeIPAddress(addr4));
  EXPECT_EQ(addr4, NormalizeIPAddress(mapped_addr));
  EXPECT_EQ(compat_addr, NormalizeIPAddress(compat_addr));

  IPAddress addr6;
  ASSERT_TRUE(StringToIPAddress("2001:700:300:1803::1", &addr6));
  EXPECT_EQ(addr6, NormalizeIPAddress(addr6));
  EXPECT_EQ(IPAddress::Loopback6(), NormalizeIPAddress(IPAddress::Loopback6()));
  EXPECT_EQ(IPAddress::Any6(), NormalizeIPAddress(IPAddress::Any6()));

  EXPECT_EQ(IPAddress(), NormalizeIPAddress(IPAddress()));
}

TEST(IPAddressTest, DualstackIPAddress) {
  IPAddress addr4 = StringToIPAddressOrDie("192.0.2.1");
  IPAddress mapped_addr = StringToIPAddressOrDie("::ffff:192.0.2.1");
  IPAddress compat_addr = StringToIPAddressOrDie("::192.0.2.1");

  EXPECT_EQ(mapped_addr, DualstackIPAddress(addr4));
  EXPECT_EQ(mapped_addr, DualstackIPAddress(mapped_addr));
  EXPECT_EQ(compat_addr, DualstackIPAddress(compat_addr));

  EXPECT_EQ(StringToIPAddressOrDie("::ffff:127.0.0.1"),
            DualstackIPAddress(IPAddress::Loopback4()));
  EXPECT_EQ(StringToIPAddressOrDie("::ffff:0.0.0.0"),
            DualstackIPAddress(IPAddress::Any4()));

  IPAddress addr6;
  ASSERT_TRUE(StringToIPAddress("2001:db8::1", &addr6));
  EXPECT_EQ(addr6, DualstackIPAddress(addr6));
  EXPECT_EQ(IPAddress::Loopback6(), DualstackIPAddress(IPAddress::Loopback6()));
  EXPECT_EQ(IPAddress::Any6(), DualstackIPAddress(IPAddress::Any6()));
}

TEST(IPAddressTest, IsInitializedAddress) {
  IPAddress uninit_addr, addr4, addr6;

  EXPECT_FALSE(IsInitializedAddress(uninit_addr));
  EXPECT_FALSE(IsInitializedAddress(addr4));
  EXPECT_FALSE(IsInitializedAddress(addr6));

  ASSERT_TRUE(StringToIPAddress("129.241.93.35", &addr4));
  ASSERT_TRUE(StringToIPAddress("2001:700:300:1803::1", &addr6));

  EXPECT_FALSE(IsInitializedAddress(uninit_addr));
  EXPECT_TRUE(IsInitializedAddress(addr4));
  EXPECT_TRUE(IsInitializedAddress(addr6));
}

TEST(IPAddressTest, IPAddressLength) {
  IPAddress ip;
  ASSERT_TRUE(StringToIPAddress("1.2.3.4", &ip));
  EXPECT_EQ(32, IPAddressLength(ip));
  ASSERT_TRUE(StringToIPAddress("2001:db8::1", &ip));
  EXPECT_EQ(128, IPAddressLength(ip));
}

TEST(IPAddressTest, PTRStringToIPAddress) {
  // Test malformed addresses only, valid addresses are tested for v4/v6 in the
  // corresponding v4/v6 conversion methods above.
  IPAddress ip;
  EXPECT_FALSE(PTRStringToIPAddress("1.0.127.in-addr.arpa", &ip));
  EXPECT_FALSE(PTRStringToIPAddress("1..0.127.in-addr.arpa", &ip));
  EXPECT_FALSE(PTRStringToIPAddress("1.0.0.256.in-addr.arpa", &ip));
  EXPECT_FALSE(PTRStringToIPAddress("1.0.-1.127.in-addr.arpa", &ip));
  EXPECT_FALSE(PTRStringToIPAddress("1.0.1a.127.in-addr.arpa", &ip));
  EXPECT_FALSE(PTRStringToIPAddress(" 1.0.0.127.in-addr.arpa", &ip));
  EXPECT_FALSE(PTRStringToIPAddress("+1.0.0.127.in-addr.arpa", &ip));
  EXPECT_FALSE(PTRStringToIPAddress("1.0.0.127.ip6.arpa", &ip));
  EXPECT_FALSE(
      PTRStringToIPAddress("1.1.0.1.0.0.0.0.0.0.0.0.0.0.0.0.3.0.8.0."
                           "1.0.0.4.0.6.8.4.1.0.0.ip6.arpa.",
                           &ip));
  EXPECT_FALSE(
      PTRStringToIPAddress("1..0.1.0.0.0.0.0.0.0.0.0.0.0.0.3.0.8.0."
                           "1.0.0.4.0.6.8.4.1.0.0.2.ip6.arpa",
                           &ip));
  EXPECT_FALSE(
      PTRStringToIPAddress("1.10.0.1.0.0.0.0.0.0.0.0.0.0.0.0.3.0.8.0."
                           "1.0.0.4.0.6.8.4.1.0.0.2.ip6.arpa",
                           &ip));
  EXPECT_FALSE(
      PTRStringToIPAddress("1.0.0.1.0.0.0.0.0.0.0.0.0.0.0.0.3.0.8.0."
                           "1.0.0.4.0.6.8.4.1...0.2.ip6.arpa",
                           &ip));
  EXPECT_FALSE(
      PTRStringToIPAddress("1.G.0.1.0.0.0.0.0.0.0.0.0.0.0.0.3.0.8.0."
                           "1.0.0.4.0.6.8.4.1.0.0.2.ip6.arpa",
                           &ip));
  EXPECT_FALSE(
      PTRStringToIPAddress("1.g.0.1.0.0.0.0.0.0.0.0.0.0.0.0.3.0.8.0."
                           "1.0.0.4.0.6.8.4.1.0.0.2.ip6.arpa",
                           &ip));
}

TEST(IPAddressDeathTest, IPAddressLength) {
  IPAddress ip;
  int bitlength = 0;

  ASSERT_FALSE(IsInitializedAddress(ip));

  if (DEBUG_MODE) {
    EXPECT_DEBUG_DEATH(bitlength = IPAddressLength(ip), "");
  } else {
    ScopedMockLogVerifier log(
        "IPAddressLength() of object with invalid address family");
    bitlength = IPAddressLength(ip);
    EXPECT_EQ(-1, bitlength);
  }
}

TEST(IPAddressTest, IPAddressToUInt128) {
  IPAddress addr;
  ASSERT_TRUE(StringToIPAddress("2001:700:300:1803:b0ff::12", &addr));
  EXPECT_EQ(absl::MakeUint128(0x2001070003001803ULL, 0xb0ff000000000012ULL),
            IPAddressToUInt128(addr));
}

TEST(IPAddressMulticastIpv4Test, ValidMulticast) {
  IPAddress addr;
  ASSERT_TRUE(StringToIPAddress("224.0.0.1", &addr));
  EXPECT_TRUE(IsV4MulticastIPAddress(addr));

  ASSERT_TRUE(StringToIPAddress("224.0.0.0", &addr));
  EXPECT_TRUE(IsV4MulticastIPAddress(addr));

  ASSERT_TRUE(StringToIPAddress("224.0.1.1", &addr));
  EXPECT_TRUE(IsV4MulticastIPAddress(addr));

  ASSERT_TRUE(StringToIPAddress("224.10.3.1", &addr));
  EXPECT_TRUE(IsV4MulticastIPAddress(addr));

  ASSERT_TRUE(StringToIPAddress("224.0.1.255", &addr));
  EXPECT_TRUE(IsV4MulticastIPAddress(addr));

  ASSERT_TRUE(StringToIPAddress("239.255.255.255", &addr));
  EXPECT_TRUE(IsV4MulticastIPAddress(addr));

  ASSERT_TRUE(StringToIPAddress("239.0.0.1", &addr));
  EXPECT_TRUE(IsV4MulticastIPAddress(addr));

  ASSERT_TRUE(StringToIPAddress("233.255.255.255", &addr));
  EXPECT_TRUE(IsV4MulticastIPAddress(addr));

  ASSERT_TRUE(StringToIPAddress("239.0.0.1", &addr));
  EXPECT_TRUE(IsV4MulticastIPAddress(addr));

  ASSERT_TRUE(StringToIPAddress("234.255.255.255", &addr));
  EXPECT_TRUE(IsV4MulticastIPAddress(addr));
}

TEST(IPAddressMulticastIpv4Test, NonMulticast) {
  IPAddress addr;
  ASSERT_TRUE(StringToIPAddress("0.0.0.0", &addr));
  EXPECT_FALSE(IsV4MulticastIPAddress(addr));

  ASSERT_TRUE(StringToIPAddress("1.12.200.5", &addr));

  EXPECT_FALSE(IsV4MulticastIPAddress(addr));

  ASSERT_TRUE(StringToIPAddress("72.18.45.220", &addr));
  EXPECT_FALSE(IsV4MulticastIPAddress(addr));

  ASSERT_TRUE(StringToIPAddress("192.168.1.155", &addr));
  EXPECT_FALSE(IsV4MulticastIPAddress(addr));

  ASSERT_TRUE(StringToIPAddress("203.0.113.10", &addr));
  EXPECT_FALSE(IsV4MulticastIPAddress(addr));
}

TEST(IPAddressMulticastIpv4Test, NonIpv4) {
  IPAddress addr;
  ASSERT_TRUE(StringToIPAddress("2001:700:300:1803::1", &addr));
  EXPECT_FALSE(IsV4MulticastIPAddress(addr));

  ASSERT_TRUE(StringToIPAddress("2001:db8:666::1", &addr));
  EXPECT_FALSE(IsV4MulticastIPAddress(addr));

  addr = IPAddress();
  EXPECT_FALSE(IsV4MulticastIPAddress(addr));
}

// Various death tests for IPAddress emergency behavior in production that
// should simply result in CHECK failures in debug mode.

TEST(IPAddressDeathTest, EmergencyCompatibility) {
  const std::string kIPv4Address = "129.240.2.40";
  IPAddress addr;
  in6_addr addr6;

  ASSERT_TRUE(StringToIPAddress(kIPv4Address, &addr));

  if (DEBUG_MODE) {
    EXPECT_DEBUG_DEATH(addr6 = addr.ipv6_address(), "Check failed");
  } else {
    ScopedMockLogVerifier log("returning IPv6 mapped address");
    addr6 = addr.ipv6_address();
    EXPECT_EQ(IPAddress(addr6).ToString(), "::ffff:129.240.2.40");
  }
}

TEST(IPAddressDeathTest, EmergencyEmptyString) {
  IPAddress empty;

  if (DEBUG_MODE) {
    EXPECT_DEBUG_DEATH(empty.ToString(), "empty IPAddress");
  } else {
    ScopedMockLogVerifier log("empty IPAddress");
    EXPECT_EQ("", empty.ToString());
  }
}

TEST(IPAddressDeathTest, EmergencyEmptyURIString) {
  IPAddress empty;

  if (DEBUG_MODE) {
    EXPECT_DEBUG_DEATH(IPAddressToURIString(empty), "empty IPAddress");
  } else {
    ScopedMockLogVerifier log("empty IPAddress");
    EXPECT_EQ("", IPAddressToURIString(empty));
  }
}

TEST(IPAddressDeathTest, EmergencyEmptyPTRString) {
  IPAddress empty;

  if (DEBUG_MODE) {
    EXPECT_DEBUG_DEATH(IPAddressToPTRString(empty), "empty IPAddress");
  } else {
    ScopedMockLogVerifier log("empty IPAddress");
    EXPECT_EQ("unspecified.arpa", IPAddressToPTRString(empty));
  }
}

TEST(IPAddressDeathTest, EmergencyIsNotAnyOrLoopback) {
  IPAddress empty;

  if (DEBUG_MODE) {
    EXPECT_DEBUG_DEATH(IsAnyIPAddress(empty), "empty IPAddress");
    EXPECT_DEBUG_DEATH(IsLoopbackIPAddress(empty), "empty IPAddress");
  } else {
    {
      ScopedMockLogVerifier log("empty IPAddress");
      EXPECT_FALSE(IsAnyIPAddress(empty));
    }
    {
      ScopedMockLogVerifier log("empty IPAddress");
      EXPECT_FALSE(IsLoopbackIPAddress(empty));
    }
  }
}

// Invalid conversion in *OrDie() functions.
TEST(IPAddressDeathTest, InvalidStringConversion) {
  // Invalid conversion.
  EXPECT_DEATH(StringToIPAddressOrDie("foo"), "Invalid IP foo");
  EXPECT_DEATH(StringToIPAddressOrDie("172.1.1.300"), "Invalid IP");
  EXPECT_DEATH(StringToIPAddressOrDie("::g"), "Invalid IP");
  EXPECT_DEATH(StringToIPAddressOrDie(absl::string_view("::g")), "Invalid IP");

  // Valid conversion.
  EXPECT_EQ(StringToIPAddressOrDie("1.2.3.4").ToString(), "1.2.3.4");
  EXPECT_EQ(StringToIPAddressOrDie("1.2.3.4").ToString(), "1.2.3.4");
  EXPECT_EQ(StringToIPAddressOrDie(absl::string_view("1.2.3.4")).ToString(),
            "1.2.3.4");
  EXPECT_EQ(StringToIPAddressOrDie("2001:700:300:1803::1").ToString(),
            "2001:700:300:1803::1");
  EXPECT_EQ(StringToIPAddressOrDie("2001:700:300:1803::1").ToString(),
            "2001:700:300:1803::1");
  EXPECT_EQ(StringToIPAddressOrDie(absl::string_view("2001:700:300:1803::1"))
                .ToString(),
            "2001:700:300:1803::1");
}

TEST(IPAddressDeathTest, InvalidPackedStringConversion) {
  // Invalid conversion.
  EXPECT_DEATH(PackedStringToIPAddressOrDie("foo", 3), "Invalid packed IP");
  EXPECT_DEATH(PackedStringToIPAddressOrDie("bar"), "Invalid packed IP");

  // Valid conversion.
  const std::string packed = StringToIPAddressOrDie("1.2.3.4").ToPackedString();
  EXPECT_EQ(PackedStringToIPAddressOrDie(packed).ToString(), "1.2.3.4");
}

class BogusColonlessHexToIPv6AddressTest
    : public testing::TestWithParam<BogusTestCase> {};

TEST_P(BogusColonlessHexToIPv6AddressTest, FailsOnBogusInput) {
  IPAddress dummy;
  EXPECT_FALSE(ColonlessHexToIPv6Address(GetParam().str, nullptr));
  EXPECT_FALSE(ColonlessHexToIPv6Address(GetParam().str, &dummy));
}

INSTANTIATE_TEST_SUITE_P(
    , BogusColonlessHexToIPv6AddressTest,
    testing::Values(BogusTestCase{"Empty", ""},
                    BogusTestCase{"BogusString", "bogus"},
                    BogusTestCase{"Deadbeef", "deadbeef"},
                    BogusTestCase{"TooLongByOneCharacter",
                                  "fe80000000000000000573fffea000650"},
                    BogusTestCase{"TooShortByOneCharacter",
                                  "fe80000000000000000573fffea0006"},
                    BogusTestCase{"NotAllHexTrailing",
                                  "fe80000000000000000573fffea0006x"},
                    BogusTestCase{"NotAllHexLeadingPlus",
                                  "+e80000000000000000573fffea00065"},
                    BogusTestCase{"NotAllHexLeading0x",
                                  "0x80000000000000000573fffea00065"}),
    [](const testing::TestParamInfo<BogusTestCase>& info) {
      return std::string(info.param.test_name);
    });

TEST(ColonlessHexToIPv6AddressTest, BasicValidity) {
  const char* hex_str = "fe80000000000000000573fFfEa00065";
  const char* ip6_str = "fe80::5:73ff:fea0:65";
  IPAddress expected, parsed;

  ASSERT_TRUE(StringToIPAddress(ip6_str, &expected));
  EXPECT_TRUE(ColonlessHexToIPv6Address(hex_str, nullptr));
  EXPECT_TRUE(ColonlessHexToIPv6Address(hex_str, &parsed));
  EXPECT_EQ(expected, parsed);
}

#ifndef _WIN32
// A socket address should be no longer than an IP address, since we can fit the
// port into tail padding.
TEST(SocketAddressTest, Size) {
  static_assert(sizeof(SocketAddress) == sizeof(IPAddress));
}
#endif

TEST(SocketAddressTest, BasicTest4) {
  const uint16_t kPort = 64738;
  const uint16_t kNetworkByteOrderPort = htons(kPort);

  IPAddress addr4;
  ASSERT_TRUE(StringToIPAddress("1.2.3.4", &addr4));
  SocketAddress sockaddr(addr4, kPort);

  EXPECT_EQ(sockaddr.host(), addr4);
  EXPECT_EQ(sockaddr.port(), kPort);

  sockaddr_in returned_addr4 = sockaddr.ipv4_address();
  EXPECT_EQ(returned_addr4.sin_family, AF_INET);
  EXPECT_EQ(IPAddress(returned_addr4.sin_addr), addr4);
  EXPECT_EQ(returned_addr4.sin_port, kNetworkByteOrderPort);

  std::string packed = sockaddr.ToPackedString();
  EXPECT_EQ(packed.length(), sizeof(returned_addr4.sin_addr) + 2);
  EXPECT_EQ(memcmp(packed.data(), &returned_addr4.sin_addr,
                   sizeof(returned_addr4.sin_addr)),
            0);
  EXPECT_EQ(memcmp(packed.data() + sizeof(returned_addr4.sin_addr),
                   &kNetworkByteOrderPort, sizeof(kNetworkByteOrderPort)),
            0);

  EXPECT_TRUE(PackedStringToSocketAddress(packed, nullptr));
  SocketAddress unpacked;
  EXPECT_TRUE(PackedStringToSocketAddress(packed, &unpacked));
  EXPECT_EQ(unpacked, sockaddr);

  sockaddr_storage returned_addr_generic = sockaddr.generic_address();
  EXPECT_EQ(returned_addr_generic.ss_family, AF_INET);
  sockaddr_in* cast_addr4 =
      reinterpret_cast<sockaddr_in*>(&returned_addr_generic);

  EXPECT_EQ(IPAddress(cast_addr4->sin_addr), addr4);
  EXPECT_EQ(cast_addr4->sin_port, kNetworkByteOrderPort);

  // Test copy construction.
  SocketAddress another_sockaddr = sockaddr;
  EXPECT_EQ(another_sockaddr.host(), addr4);
  EXPECT_EQ(another_sockaddr.port(), kPort);
}

TEST(SocketAddressTest, BasicTest6) {
  const uint16_t kPort = 65320;
  const uint16_t kNetworkByteOrderPort = htons(kPort);

  IPAddress addr6;
  ASSERT_TRUE(StringToIPAddress("2001:700:300:1800::f", &addr6));
  SocketAddress sockaddr(addr6, kPort);

  EXPECT_EQ(sockaddr.host(), addr6);
  EXPECT_EQ(sockaddr.port(), kPort);

  sockaddr_in6 returned_addr6 = sockaddr.ipv6_address();
  EXPECT_EQ(returned_addr6.sin6_family, AF_INET6);
  EXPECT_EQ(IPAddress(returned_addr6.sin6_addr), addr6);
  EXPECT_EQ(returned_addr6.sin6_port, kNetworkByteOrderPort);

  std::string packed = sockaddr.ToPackedString();
  EXPECT_EQ(packed.length(), sizeof(returned_addr6.sin6_addr) + 2);
  EXPECT_EQ(memcmp(packed.data(), &returned_addr6.sin6_addr,
                   sizeof(returned_addr6.sin6_addr)),
            0);
  EXPECT_EQ(memcmp(packed.data() + sizeof(returned_addr6.sin6_addr),
                   &kNetworkByteOrderPort, sizeof(kNetworkByteOrderPort)),
            0);

  EXPECT_TRUE(PackedStringToSocketAddress(packed, nullptr));
  SocketAddress unpacked;
  EXPECT_TRUE(PackedStringToSocketAddress(packed, &unpacked));
  EXPECT_EQ(unpacked, sockaddr);

  sockaddr_storage returned_addr_generic = sockaddr.generic_address();
  EXPECT_EQ(returned_addr_generic.ss_family, AF_INET6);
  sockaddr_in6* cast_addr6 =
      reinterpret_cast<sockaddr_in6*>(&returned_addr_generic);

  EXPECT_EQ(IPAddress(cast_addr6->sin6_addr), addr6);
  EXPECT_EQ(cast_addr6->sin6_port, kNetworkByteOrderPort);

  // Test assignment.
  SocketAddress another_sockaddr;
  another_sockaddr = sockaddr;
  EXPECT_EQ(another_sockaddr.host(), addr6);
  EXPECT_EQ(another_sockaddr.port(), kPort);
}

TEST(SocketAddressTest, GenericInput4) {
  const uint16_t kPort = 6502;
  const char kIPAddress[] = "1.2.3.4";

  sockaddr_storage addr4_storage{};
  sockaddr_in* addr4 = reinterpret_cast<sockaddr_in*>(&addr4_storage);
  addr4->sin_family = AF_INET;
  inet_pton(AF_INET, kIPAddress, &addr4->sin_addr);
  addr4->sin_port = htons(kPort);

  SocketAddress sockaddr1(*sockaddr_cast(addr4));
  SocketAddress sockaddr2(addr4_storage);

  ASSERT_EQ(sockaddr1.host().address_family(), AF_INET);
  ASSERT_EQ(sockaddr2.host().address_family(), AF_INET);
  EXPECT_EQ(sockaddr1.host().ToString(), kIPAddress);
  EXPECT_EQ(sockaddr2.host().ToString(), kIPAddress);
  EXPECT_EQ(sockaddr1.port(), kPort);
  EXPECT_EQ(sockaddr2.port(), kPort);
}

TEST(SocketAddressTest, GenericInput6) {
  const uint16_t kPort = 1542;
  const char kIPAddress[] = "2001:700:300:1800::f";

  sockaddr_storage addr6_storage{};
  sockaddr_in6* addr6 = reinterpret_cast<sockaddr_in6*>(&addr6_storage);
  addr6->sin6_family = AF_INET6;
  inet_pton(AF_INET6, kIPAddress, &addr6->sin6_addr);
  addr6->sin6_port = htons(kPort);

  SocketAddress sockaddr1(*sockaddr_cast(addr6));
  SocketAddress sockaddr2(addr6_storage);

  ASSERT_EQ(sockaddr1.host().address_family(), AF_INET6);
  ASSERT_EQ(sockaddr2.host().address_family(), AF_INET6);
  EXPECT_EQ(sockaddr1.host().ToString(), kIPAddress);
  EXPECT_EQ(sockaddr2.host().ToString(), kIPAddress);
  EXPECT_EQ(sockaddr1.port(), kPort);
  EXPECT_EQ(sockaddr2.port(), kPort);
}

TEST(SocketAddressTest, GenericInputInvalid) {
  sockaddr sa = {};
  sockaddr_storage ss = {};
  sockaddr_in sin = {};
  sockaddr_in6 sin6 = {};

  // Zero-initialized structs have family AF_UNSPEC.
  EXPECT_EQ(SocketAddress(), SocketAddress(sa));
  EXPECT_EQ(SocketAddress(), SocketAddress(ss));
  EXPECT_DEATH(SocketAddress foo(sin), "sin_family");
  EXPECT_DEATH(SocketAddress foo(sin6), "sin6_family");

  // Test with an invalid family.
  sa.sa_family = 255;
  ss.ss_family = 255;
  sin.sin_family = 255;
  sin6.sin6_family = 255;
  EXPECT_DEBUG_DEATH(SocketAddress foo(sa), "Unknown address family 255");
  EXPECT_DEBUG_DEATH(SocketAddress foo(ss), "Unknown address family 255");
  if (!DEBUG_MODE) {
    EXPECT_EQ(SocketAddress(), SocketAddress(sa));
    EXPECT_EQ(SocketAddress(), SocketAddress(ss));
  }
  EXPECT_DEATH(SocketAddress foo(sin), "sin_family");
  EXPECT_DEATH(SocketAddress foo(sin6), "sin6_family");
}

TEST(SocketAddressTest, EmptySockaddr) {
  sockaddr empty;
  sockaddr_storage empty_generic;

  empty.sa_family = AF_UNSPEC;
  empty_generic.ss_family = AF_UNSPEC;

  SocketAddress empty1(empty);
  SocketAddress empty2(empty_generic);

  EXPECT_EQ(empty1.host().address_family(), AF_UNSPEC);
  EXPECT_EQ(empty2.host().address_family(), AF_UNSPEC);
  EXPECT_EQ(empty1, empty2);
}

TEST(SocketAddressTest, ToAndFromString4) {
  const std::string kIPString = "1.2.3.4";
  const int kPort = 1234;
  const std::string kSockaddrString = kIPString + absl::StrFormat(":%u", kPort);

  sockaddr_in addr4;
  addr4.sin_family = AF_INET;
  ASSERT_GT(inet_pton(AF_INET, kIPString.c_str(), &addr4.sin_addr), 0);
  addr4.sin_port = htons(kPort);

  SocketAddress addr;
  EXPECT_TRUE(StringToSocketAddress("1.2.3.4:0", &addr));
  EXPECT_TRUE(StringToSocketAddress("1.2.3.4:65535", &addr));
  ASSERT_TRUE(StringToSocketAddress(kSockaddrString, nullptr));
  ASSERT_TRUE(StringToSocketAddress(kSockaddrString, &addr));

  sockaddr_in returned_addr4 = addr.ipv4_address();
  EXPECT_EQ(returned_addr4.sin_family, addr4.sin_family);
  EXPECT_EQ(
      memcmp(&addr4.sin_addr, &returned_addr4.sin_addr, sizeof(addr4.sin_addr)),
      0);
  EXPECT_EQ(returned_addr4.sin_port, addr4.sin_port);

  EXPECT_EQ(addr.ToString(), kSockaddrString);
}

TEST(SocketAddressTest, ToAndFromString6) {
  constexpr char kIPString[] = "2001:700:300:1800::f";
  const int kPort = 50000;
  const std::string kSockaddrString =
      absl::StrFormat("[%s]:%u", kIPString, kPort);

  sockaddr_in6 addr6;
  addr6.sin6_family = AF_INET6;
  ASSERT_GT(inet_pton(AF_INET6, kIPString, &addr6.sin6_addr), 0);
  addr6.sin6_port = htons(kPort);

  SocketAddress addr;
  EXPECT_TRUE(StringToSocketAddress("[2001:700:300:1800::f]:0", &addr));
  EXPECT_TRUE(StringToSocketAddress("[2001:700:300:1800::f]:65535", &addr));
  ASSERT_TRUE(StringToSocketAddress(kSockaddrString, nullptr));
  ASSERT_TRUE(StringToSocketAddress(kSockaddrString, &addr));

  sockaddr_in6 returned_addr6 = addr.ipv6_address();
  EXPECT_EQ(returned_addr6.sin6_family, addr6.sin6_family);
  EXPECT_EQ(memcmp(&addr6.sin6_addr, &returned_addr6.sin6_addr,
                   sizeof(addr6.sin6_addr)),
            0);
  EXPECT_EQ(returned_addr6.sin6_port, addr6.sin6_port);

  EXPECT_EQ(addr.ToString(), kSockaddrString);
}

class BogusSocketAddressTest : public testing::TestWithParam<BogusTestCase> {};

TEST_P(BogusSocketAddressTest, StringToSocketAddressFails) {
  SocketAddress addr;
  EXPECT_FALSE(StringToSocketAddress(GetParam().str, &addr));
}

INSTANTIATE_TEST_SUITE_P(
    , BogusSocketAddressTest,
    testing::Values(BogusTestCase{"OctetOutOfRange", "1.2.3.256:1234"},
                    BogusTestCase{"PortOutOfRange", "1.2.3.4:123456"},
                    BogusTestCase{"NegativePort", "1.2.3.4:-1"},
                    BogusTestCase{"PlusSignInPort", "1.2.3.4:+1"},
                    BogusTestCase{"MissingPort", "1.2.3.4:"},
                    BogusTestCase{"MultipleColons", "1.2.3.4:1:2"},
                    BogusTestCase{"TrailingSpace", "1.2.3.4:1234 "},
                    BogusTestCase{"LeadingSpace", " 1.2.3.4:1234"},
                    BogusTestCase{"SpaceBeforeColon", "1.2.3.4 :1234"},
                    BogusTestCase{"NoPort", "1.2.3.4"},
                    BogusTestCase{"IPv4InBrackets", "[1.2.3.4]:5"}),
    [](const testing::TestParamInfo<BogusTestCase>& info) {
      return std::string(info.param.test_name);
    });

TEST(SocketAddressTest, Equality) {
  const std::string kIPv4String1 = "1.2.3.4:1234";
  const std::string kIPv4String2 = "1.2.3.4:53764";
  const std::string kIPv6String1 = "[2001:700:300:1800::f]:1234";
  const std::string kIPv6String2 = "[2001:700:300:1800:0:0:0:f]:1234";
  const std::string kIPv6String3 = "[2001:700:300:1800::f]:53764";

  SocketAddress empty1;
  SocketAddress empty2{IPAddress(), 0};
  SocketAddress empty3{IPAddress(), 123};

  ASSERT_FALSE(IsInitializedSocketAddress(empty1));
  ASSERT_FALSE(IsInitializedSocketAddress(empty2));
  ASSERT_FALSE(IsInitializedSocketAddress(empty3));

  SocketAddress addr4_1, addr4_2;
  SocketAddress addr6_1, addr6_2, addr6_3;

  ASSERT_TRUE(StringToSocketAddress(kIPv4String1, &addr4_1));
  ASSERT_TRUE(StringToSocketAddress(kIPv4String2, &addr4_2));
  ASSERT_TRUE(StringToSocketAddress(kIPv6String1, &addr6_1));
  ASSERT_TRUE(StringToSocketAddress(kIPv6String2, &addr6_2));
  ASSERT_TRUE(StringToSocketAddress(kIPv6String3, &addr6_3));

  // operator==
  EXPECT_TRUE(empty1 == empty1);
  EXPECT_TRUE(empty1 == empty2);
  EXPECT_TRUE(empty1 == empty3);
  EXPECT_TRUE(empty2 == empty1);
  EXPECT_TRUE(empty2 == empty2);
  EXPECT_TRUE(empty2 == empty3);
  EXPECT_TRUE(empty3 == empty1);
  EXPECT_TRUE(empty3 == empty2);
  EXPECT_TRUE(empty3 == empty3);

  EXPECT_FALSE(empty1 == addr4_1);
  EXPECT_FALSE(empty1 == addr4_2);
  EXPECT_FALSE(empty1 == addr6_1);
  EXPECT_FALSE(empty1 == addr6_2);
  EXPECT_FALSE(empty1 == addr6_3);

  EXPECT_FALSE(addr4_1 == empty1);
  EXPECT_TRUE(addr4_1 == addr4_1);
  EXPECT_FALSE(addr4_1 == addr4_2);
  EXPECT_FALSE(addr4_1 == addr6_1);
  EXPECT_FALSE(addr4_1 == addr6_2);
  EXPECT_FALSE(addr4_1 == addr6_3);

  EXPECT_FALSE(addr4_2 == empty1);
  EXPECT_FALSE(addr4_2 == addr4_1);
  EXPECT_TRUE(addr4_2 == addr4_2);
  EXPECT_FALSE(addr4_2 == addr6_1);
  EXPECT_FALSE(addr4_2 == addr6_2);
  EXPECT_FALSE(addr4_2 == addr6_3);

  EXPECT_FALSE(addr6_1 == empty1);
  EXPECT_FALSE(addr6_1 == addr4_1);
  EXPECT_FALSE(addr6_1 == addr4_2);
  EXPECT_TRUE(addr6_1 == addr6_1);
  EXPECT_TRUE(addr6_1 == addr6_2);
  EXPECT_FALSE(addr6_1 == addr6_3);

  EXPECT_FALSE(addr6_2 == empty1);
  EXPECT_FALSE(addr6_2 == addr4_1);
  EXPECT_FALSE(addr6_2 == addr4_2);
  EXPECT_TRUE(addr6_2 == addr6_1);
  EXPECT_TRUE(addr6_2 == addr6_2);
  EXPECT_FALSE(addr6_2 == addr6_3);

  EXPECT_FALSE(addr6_3 == empty1);
  EXPECT_FALSE(addr6_3 == addr4_1);
  EXPECT_FALSE(addr6_3 == addr4_2);
  EXPECT_FALSE(addr6_3 == addr6_1);
  EXPECT_FALSE(addr6_3 == addr6_2);
  EXPECT_TRUE(addr6_3 == addr6_3);

  // operator!= (same tests, just inverted)
  EXPECT_FALSE(empty1 != empty1);
  EXPECT_FALSE(empty1 != empty2);
  EXPECT_FALSE(empty1 != empty3);
  EXPECT_FALSE(empty2 != empty1);
  EXPECT_FALSE(empty2 != empty2);
  EXPECT_FALSE(empty2 != empty3);
  EXPECT_FALSE(empty3 != empty1);
  EXPECT_FALSE(empty3 != empty2);
  EXPECT_FALSE(empty3 != empty3);

  EXPECT_TRUE(empty1 != addr4_1);
  EXPECT_TRUE(empty1 != addr4_2);
  EXPECT_TRUE(empty1 != addr6_1);
  EXPECT_TRUE(empty1 != addr6_2);
  EXPECT_TRUE(empty1 != addr6_3);

  EXPECT_TRUE(addr4_1 != empty1);
  EXPECT_FALSE(addr4_1 != addr4_1);
  EXPECT_TRUE(addr4_1 != addr4_2);
  EXPECT_TRUE(addr4_1 != addr6_1);
  EXPECT_TRUE(addr4_1 != addr6_2);
  EXPECT_TRUE(addr4_1 != addr6_3);

  EXPECT_TRUE(addr4_2 != empty1);
  EXPECT_TRUE(addr4_2 != addr4_1);
  EXPECT_FALSE(addr4_2 != addr4_2);
  EXPECT_TRUE(addr4_2 != addr6_1);
  EXPECT_TRUE(addr4_2 != addr6_2);
  EXPECT_TRUE(addr4_2 != addr6_3);

  EXPECT_TRUE(addr6_1 != empty1);
  EXPECT_TRUE(addr6_1 != addr4_1);
  EXPECT_TRUE(addr6_1 != addr4_2);
  EXPECT_FALSE(addr6_1 != addr6_1);
  EXPECT_FALSE(addr6_1 != addr6_2);
  EXPECT_TRUE(addr6_1 != addr6_3);

  EXPECT_TRUE(addr6_2 != empty1);
  EXPECT_TRUE(addr6_2 != addr4_1);
  EXPECT_TRUE(addr6_2 != addr4_2);
  EXPECT_FALSE(addr6_2 != addr6_1);
  EXPECT_FALSE(addr6_2 != addr6_2);
  EXPECT_TRUE(addr6_2 != addr6_3);

  EXPECT_TRUE(addr6_3 != empty1);
  EXPECT_TRUE(addr6_3 != addr4_1);
  EXPECT_TRUE(addr6_3 != addr4_2);
  EXPECT_TRUE(addr6_3 != addr6_1);
  EXPECT_TRUE(addr6_3 != addr6_2);
  EXPECT_FALSE(addr6_3 != addr6_3);
}

// Invalid SocketAddress conversion in *OrDie functions.
TEST(SocketAddressDeathTest, InvalidSocketAddressString) {
  EXPECT_DEATH(StringToSocketAddressOrDie("foo"), "Invalid SocketAddress foo");
  EXPECT_DEATH(StringToSocketAddressOrDie("172.1.1.100"),
               "Invalid SocketAddress");
  EXPECT_DEATH(StringToSocketAddressOrDie("172.1.1.100:-1"),
               "Invalid SocketAddress");
  EXPECT_DEATH(StringToSocketAddressOrDie("172.1.1.100:test"),
               "Invalid SocketAddress");
  EXPECT_DEATH(StringToSocketAddressOrDie("172.1.1.100:65536"),
               "Invalid SocketAddress");
  EXPECT_DEATH(StringToSocketAddressOrDie("2001:700:300:183::1:1"),
               "Invalid SocketAddress");
  EXPECT_DEATH(StringToSocketAddressOrDie("[2001:700:300:183::g]"),
               "Invalid SocketAddress");
  EXPECT_DEATH(StringToSocketAddressOrDie("[2001:700:300:183::1]:"),
               "Invalid SocketAddress");
  EXPECT_DEATH(StringToSocketAddressOrDie("[2001:700:300:183::1]:foo"),
               "Invalid SocketAddress");
  EXPECT_DEATH(StringToSocketAddressOrDie("[2001:700:300:183::1]:-1"),
               "Invalid SocketAddress");
  EXPECT_DEATH(StringToSocketAddressOrDie("[2001:700:300:183::1]:65536"),
               "Invalid SocketAddress");

  EXPECT_EQ(StringToSocketAddressOrDie("1.2.3.4:5").ToString(), "1.2.3.4:5");
  EXPECT_EQ(StringToSocketAddressOrDie("[::1]:6").ToString(), "[::1]:6");
}

TEST(SocketAddressTest, FromStringWithDefaultPort4) {
  const int kDefaultPort = 50000;
  const std::string kSockaddrStringWithoutPort = "1.2.3.4";
  const std::string kEdgeSockaddrString1 = "1.2.3.4:0";
  const std::string kEdgeSockaddrString2 = "1.2.3.4:65535";

  SocketAddress addr;
  EXPECT_TRUE(StringToSocketAddressWithDefaultPort(kSockaddrStringWithoutPort,
                                                   kDefaultPort, &addr));
  EXPECT_EQ(addr.port(), kDefaultPort);
  EXPECT_TRUE(StringToSocketAddressWithDefaultPort(kEdgeSockaddrString1,
                                                   kDefaultPort, &addr));
  EXPECT_EQ(addr.port(), 0);
  EXPECT_TRUE(StringToSocketAddressWithDefaultPort(kEdgeSockaddrString2,
                                                   kDefaultPort, &addr));
  EXPECT_EQ(addr.port(), 65535);
}

TEST(SocketAddressTest, FromStringWithDefaultPort6) {
  const int kDefaultPort = 50000;
  const std::string kSockaddrStringWithoutPort = "[2001:700:300:1800::f]";
  const std::string kEdgeSockaddrString1 = "[2001:700:300:1800::f]:0";
  const std::string kEdgeSockaddrString2 = "[2001:700:300:1800::f]:65535";

  SocketAddress addr;
  EXPECT_TRUE(StringToSocketAddressWithDefaultPort(kSockaddrStringWithoutPort,
                                                   kDefaultPort, &addr));
  EXPECT_EQ(addr.port(), kDefaultPort);
  EXPECT_TRUE(StringToSocketAddressWithDefaultPort(kEdgeSockaddrString1,
                                                   kDefaultPort, &addr));
  EXPECT_EQ(addr.port(), 0);
  EXPECT_TRUE(StringToSocketAddressWithDefaultPort(kEdgeSockaddrString2,
                                                   kDefaultPort, &addr));
  EXPECT_EQ(addr.port(), 65535);
}

class BogusSocketAddressWithDefaultPort4Test
    : public testing::TestWithParam<BogusTestCase> {};

TEST_P(BogusSocketAddressWithDefaultPort4Test,
       StringToSocketAddressWithDefaultPortFails) {
  const int kDefaultPort = 50000;
  SocketAddress addr;
  EXPECT_FALSE(StringToSocketAddressWithDefaultPort(GetParam().str,
                                                    kDefaultPort, &addr));
}

INSTANTIATE_TEST_SUITE_P(
    , BogusSocketAddressWithDefaultPort4Test,
    testing::Values(BogusTestCase{"OctetOutOfRange", "1.2.3.256:1234"},
                    BogusTestCase{"PortOutOfRange", "1.2.3.4:123456"},
                    BogusTestCase{"NegativePort", "1.2.3.4:-1"},
                    BogusTestCase{"PlusSignInPort", "1.2.3.4:+1"},
                    BogusTestCase{"MissingPort", "1.2.3.4:"},
                    BogusTestCase{"MultipleColons", "1.2.3.4:1:2"},
                    BogusTestCase{"TrailingSpace", "1.2.3.4:1234 "},
                    BogusTestCase{"LeadingSpace", " 1.2.3.4:1234"},
                    BogusTestCase{"SpaceBeforeColon", "1.2.3.4 :1234"}),
    [](const testing::TestParamInfo<BogusTestCase>& info) {
      return std::string(info.param.test_name);
    });

class BogusSocketAddressWithDefaultPort6Test
    : public testing::TestWithParam<BogusTestCase> {};

TEST_P(BogusSocketAddressWithDefaultPort6Test,
       StringToSocketAddressWithDefaultPortFails) {
  const int kDefaultPort = 50000;
  SocketAddress addr;
  EXPECT_FALSE(StringToSocketAddressWithDefaultPort(GetParam().str,
                                                    kDefaultPort, &addr));
}

INSTANTIATE_TEST_SUITE_P(
    , BogusSocketAddressWithDefaultPort6Test,
    testing::Values(
        BogusTestCase{"InvalidHex", "[2001:700:300:180g::f]:1234"},
        BogusTestCase{"PortOutOfRange", "[2001:700:300:1800::f]:123456"},
        BogusTestCase{"NegativePort", "[2001:700:300:1800::f]:-1"},
        BogusTestCase{"PlusSignInPort", "[2001:700:300:1800::f]:+1"},
        BogusTestCase{"MissingPort", "[2001:700:300:1800::f]:"},
        BogusTestCase{"MultipleColonsInPort", "[2001:700:300:1800::f]:1:2"},
        BogusTestCase{"TrailingSpace", "[2001:700:300:1800::f]:1234 "},
        BogusTestCase{"LeadingSpace", "[ 2001:700:300:1800::f]:1234"},
        BogusTestCase{"SpaceBeforeClosingBracket",
                      "[2001:700:300:1800::f ]:1234"},
        BogusTestCase{"DoubleClosingBracket", "[2001:700:300:1800::f]]:1234"},
        BogusTestCase{"MissingClosingBracket", "[2001:700:300:1800::f:1234"}),
    [](const testing::TestParamInfo<BogusTestCase>& info) {
      return std::string(info.param.test_name);
    });

TEST(SocketAddressTest, Logging) {
  const std::string kIPv4String = "1.2.3.4:1337";
  const std::string kIPv6String = "[2001:700:300:1800::f]:1338";
  SocketAddress sockaddr4, sockaddr6;

  ASSERT_TRUE(StringToSocketAddress(kIPv4String, &sockaddr4));
  ASSERT_TRUE(StringToSocketAddress(kIPv6String, &sockaddr6));

  EXPECT_EQ(absl::StrCat(sockaddr4, " ", sockaddr6),
            "1.2.3.4:1337 [2001:700:300:1800::f]:1338");

  std::ostringstream out;
  out << sockaddr4 << " " << sockaddr6;
  EXPECT_EQ("1.2.3.4:1337 [2001:700:300:1800::f]:1338", out.str());
}

TEST(SocketAddressTest, LoggingUninitialized) {
  EXPECT_EQ(absl::StrCat(SocketAddress()), "<uninitialized SocketAddress>");

  std::ostringstream out;
  out << SocketAddress();
  EXPECT_EQ("<uninitialized SocketAddress>", out.str());
}

TEST(SocketAddressTest, Joining) {
  std::vector<SocketAddress> v = {
      StringToSocketAddressOrDie("192.0.2.0:80"),
      StringToSocketAddressOrDie("[2001:db8::]:443"),
      StringToSocketAddressOrDie("0.0.0.0:80"),
      StringToSocketAddressOrDie("[::]:443")};
  EXPECT_EQ("192.0.2.0:80***[2001:db8::]:443***0.0.0.0:80***[::]:443",
            absl::StrJoin(v, "***", SocketAddressJoinFormatter()));
}

TEST(SocketAddressTest, SocketAddressOrdering) {
  const std::string kIPString1 = "1.2.3.4";
  const std::string kIPString2 = "4.3.2.1";

  IPAddress addr1, addr2;

  ASSERT_TRUE(StringToIPAddress(kIPString1, &addr1));
  ASSERT_TRUE(StringToIPAddress(kIPString2, &addr2));

  SocketAddress sock_addr0;
  SocketAddress sock_addr1(addr1, 5);
  SocketAddress sock_addr2(addr2, 3);
  SocketAddress sock_addr3(addr1, 4);
  SocketAddress sock_addr4(addr2, 8);
  SocketAddress sock_addr5(addr1, 40000);  // port >= 2^15 to check signness.

  // std::set
  EXPECT_THAT((std::set<SocketAddress>{
                  sock_addr1,
                  sock_addr2,
                  sock_addr3,
                  sock_addr4,
                  sock_addr5,
                  sock_addr0,
              }),
              ElementsAreArray({
                  sock_addr0,
                  sock_addr3,
                  sock_addr1,
                  sock_addr5,
                  sock_addr2,
                  sock_addr4,
              }));

  // Pairwise checks
  EXPECT_FALSE(sock_addr0 < sock_addr0);
  EXPECT_TRUE(sock_addr0 < sock_addr3);
  EXPECT_FALSE(sock_addr3 < sock_addr0);

  EXPECT_TRUE(sock_addr0 <= sock_addr0);
  EXPECT_TRUE(sock_addr0 <= sock_addr3);
  EXPECT_FALSE(sock_addr3 <= sock_addr0);

  EXPECT_TRUE(sock_addr3 >= sock_addr3);
  EXPECT_TRUE(sock_addr3 >= sock_addr0);
  EXPECT_FALSE(sock_addr0 >= sock_addr3);

  EXPECT_FALSE(sock_addr3 > sock_addr3);
  EXPECT_TRUE(sock_addr3 > sock_addr0);
  EXPECT_FALSE(sock_addr0 > sock_addr3);

#if __cplusplus >= 202002L
  EXPECT_EQ(std::weak_ordering::equivalent, sock_addr0 <=> sock_addr0);
  EXPECT_EQ(std::weak_ordering::equivalent, sock_addr1 <=> sock_addr1);
  EXPECT_EQ(std::weak_ordering::less, sock_addr0 <=> sock_addr1);
  EXPECT_EQ(std::weak_ordering::greater, sock_addr1 <=> sock_addr0);
#endif
}

TEST(SocketAddressTest, Hash) {
  const std::string kIPString1 = "1.2.3.4";
  const std::string kIPString2 = "4.3.2.1";

  IPAddress addr1, addr2;

  ASSERT_TRUE(StringToIPAddress(kIPString1, &addr1));
  ASSERT_TRUE(StringToIPAddress(kIPString2, &addr2));

  SocketAddress sock_addr0;
  SocketAddress sock_addr1(addr1, 5);
  SocketAddress sock_addr2(addr2, 3);
  SocketAddress sock_addr3(addr1, 4);
  SocketAddress sock_addr4(addr2, 8);
  SocketAddress sock_addr5(addr1, 40000);  // port >= 2^15 to check signedness.

  absl::node_hash_set<SocketAddress> sock_addrs;
  sock_addrs.insert(sock_addr0);
  sock_addrs.insert(SocketAddress());
  sock_addrs.insert(sock_addr1);
  sock_addrs.insert(sock_addr2);
  sock_addrs.insert(sock_addr3);
  sock_addrs.insert(sock_addr4);
  sock_addrs.insert(sock_addr5);

  EXPECT_EQ(6, sock_addrs.size());

  EXPECT_EQ(1, sock_addrs.count(sock_addr0));
  EXPECT_EQ(1, sock_addrs.count(sock_addr1));
  EXPECT_EQ(1, sock_addrs.count(sock_addr2));
  EXPECT_EQ(1, sock_addrs.count(sock_addr3));
  EXPECT_EQ(1, sock_addrs.count(sock_addr4));
  EXPECT_EQ(1, sock_addrs.count(sock_addr5));

  // Also test the absl::Hash version.
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(std::make_tuple(
      sock_addr0, sock_addr1, sock_addr2, sock_addr3, sock_addr4, sock_addr5)));
}

struct NormalizeSocketAddressCase {
  std::string test_name;
  std::string input;
  std::string normalized;
};

class NormalizeSocketAddressTest
    : public testing::TestWithParam<NormalizeSocketAddressCase> {};

TEST_P(NormalizeSocketAddressTest, NormalizesCorrectly) {
  const auto& c = GetParam();
  const SocketAddress addr = StringToSocketAddressOrDie(c.input);
  EXPECT_EQ(NormalizeSocketAddress(addr).ToString(), c.normalized);

  const bool is_ipv6 = addr.host().address_family() == AF_INET6;
  sockaddr_storage storage;
  ASSERT_TRUE(SocketAddressToFamily(AF_UNSPEC, addr, &storage, nullptr));
  auto* sa = sockaddr_cast(&storage);
  auto* sin6 = reinterpret_cast<sockaddr_in6*>(&storage);

  // Sanity check.
  EXPECT_EQ(SocketAddress(storage).ToString(), c.input);
  EXPECT_EQ(SocketAddress(*sa).ToString(), c.input);
  if (is_ipv6) {
    EXPECT_EQ(SocketAddress(MakeSocketAddressFromSockaddrIn6(*sin6).value())
                  .ToString(),
              c.input);
  }

  // NormalizeSocketAddress() should accept sockaddr_* directly.
  EXPECT_EQ(NormalizeSocketAddress(storage).ToString(), c.normalized);
  EXPECT_EQ(NormalizeSocketAddress(*sa).ToString(), c.normalized);
  if (is_ipv6) {
    EXPECT_EQ(NormalizeSocketAddress(*sin6).ToString(), c.normalized);
  }
}

INSTANTIATE_TEST_SUITE_P(
    , NormalizeSocketAddressTest,
    testing::Values(
        NormalizeSocketAddressCase{"IPv4", "129.241.93.35:21",
                                   "129.241.93.35:21"},
        NormalizeSocketAddressCase{
            "IPv4MappedIPv6", "[::ffff:129.241.93.35]:21", "129.241.93.35:21"},
        NormalizeSocketAddressCase{"IPv4CompatibleIPv6", "[::129.241.93.35]:21",
                                   "[::129.241.93.35]:21"},
        NormalizeSocketAddressCase{"IPv6", "[2001:700:300:1803::1]:21",
                                   "[2001:700:300:1803::1]:21"},
        NormalizeSocketAddressCase{"LoopbackIPv6", "[::1]:21", "[::1]:21"}),
    [](const testing::TestParamInfo<NormalizeSocketAddressCase>& info) {
      return info.param.test_name;
    });

TEST(SocketAddressTest, NormalizeUninitialized) {
  EXPECT_EQ(NormalizeSocketAddress(SocketAddress()), SocketAddress());
}

TEST(SocketAddressTest, DualstackSocketAddress) {
  SocketAddress addr4 = StringToSocketAddressOrDie("192.0.2.1:21");
  SocketAddress mapped_addr =
      StringToSocketAddressOrDie("[::ffff:192.0.2.1]:21");
  SocketAddress compat_addr = StringToSocketAddressOrDie("[::192.0.2.1]:21");

  EXPECT_EQ(mapped_addr, DualstackSocketAddress(addr4));
  EXPECT_EQ(mapped_addr, DualstackSocketAddress(mapped_addr));
  EXPECT_EQ(compat_addr, DualstackSocketAddress(compat_addr));

  EXPECT_EQ(StringToSocketAddressOrDie("[::ffff:127.0.0.1]:123"),
            DualstackSocketAddress(SocketAddress(IPAddress::Loopback4(), 123)));
  EXPECT_EQ(StringToSocketAddressOrDie("[::ffff:0.0.0.0]:123"),
            DualstackSocketAddress(SocketAddress(IPAddress::Any4(), 123)));

  SocketAddress addr6 = StringToSocketAddressOrDie("[2001:db8::1]:21");
  EXPECT_EQ(addr6, DualstackSocketAddress(addr6));
  SocketAddress loopback6 = StringToSocketAddressOrDie("[::1]:21");
  EXPECT_EQ(loopback6, DualstackSocketAddress(loopback6));
  SocketAddress any6 = StringToSocketAddressOrDie("[::]:21");
  EXPECT_EQ(any6, DualstackSocketAddress(any6));
}

TEST(SocketAddressTest, IsInitializedSocketAddress) {
  SocketAddress uninit_addr, addr4, addr6;

  EXPECT_FALSE(IsInitializedSocketAddress(uninit_addr));
  EXPECT_FALSE(IsInitializedSocketAddress(addr4));
  EXPECT_FALSE(IsInitializedSocketAddress(addr6));

  ASSERT_TRUE(StringToSocketAddress("129.241.93.35:4919", &addr4));
  ASSERT_TRUE(StringToSocketAddress("[2001:67c:a4::1]:4919", &addr6));

  EXPECT_FALSE(IsInitializedSocketAddress(uninit_addr));
  EXPECT_TRUE(IsInitializedSocketAddress(addr4));
  EXPECT_TRUE(IsInitializedSocketAddress(addr6));
}

TEST(SocketAddressTest, SocketAddressToFamily) {
  constexpr char kFail[] = "<fail>";
  const struct {
    std::string input;
    std::string out_inet;
    std::string out_inet6;
  } cases[] = {
      {kFail, kFail, kFail},
      {"192.0.2.1:80", "192.0.2.1:80", "[::ffff:192.0.2.1]:80"},
      {"0.0.0.0:80", "0.0.0.0:80", "[::ffff:0.0.0.0]:80"},
      {"[::]:80", "0.0.0.0:80", "[::]:80"},
      {"[2001::]:80", kFail, "[2001::]:80"},
      // Note: We don't auto-convert IPv6 to IPv4.
      {"[::ffff:0.0.0.0]:80", kFail, "[::ffff:0.0.0.0]:80"},
      {"[::ffff:1.2.3.4]:80", kFail, "[::ffff:1.2.3.4]:80"},
  };

  for (const auto& c : cases) {
    SCOPED_TRACE(absl::StrCat("Case input: ", c.input));
    SocketAddress addr;
    if (c.input != kFail) {
      addr = StringToSocketAddressOrDie(c.input);
    }
    bool ok;
    sockaddr_storage kaddr;
    socklen_t kaddr_size;

    // Convert to AF_INET.
    ok = SocketAddressToFamily(AF_INET, addr, &kaddr, &kaddr_size);
    if (c.out_inet != kFail) {
      EXPECT_TRUE(ok);
      EXPECT_EQ(sizeof(sockaddr_in), kaddr_size);
      EXPECT_EQ(AF_INET, kaddr.ss_family);
      EXPECT_EQ(c.out_inet, SocketAddress(kaddr).ToString());
    } else {
      EXPECT_FALSE(ok);
      EXPECT_EQ(std::numeric_limits<decltype(kaddr.ss_family)>::max(),
                kaddr.ss_family);
      EXPECT_EQ(0, kaddr_size);
    }

    // Convert to AF_INET6.
    ok = SocketAddressToFamily(AF_INET6, addr, &kaddr, &kaddr_size);
    if (c.out_inet6 != kFail) {
      EXPECT_TRUE(ok);
      EXPECT_EQ(sizeof(sockaddr_in6), kaddr_size);
      EXPECT_EQ(AF_INET6, kaddr.ss_family);
      EXPECT_EQ(c.out_inet6, SocketAddress(kaddr).ToString());
    } else {
      EXPECT_FALSE(ok);
      EXPECT_EQ(std::numeric_limits<decltype(kaddr.ss_family)>::max(),
                kaddr.ss_family);
      EXPECT_EQ(0, kaddr_size);
    }

    // Convert to AF_UNSPEC (no change in family).
    ok = SocketAddressToFamily(AF_UNSPEC, addr, &kaddr, &kaddr_size);
    if (IsInitializedSocketAddress(addr)) {
      EXPECT_TRUE(ok);
      EXPECT_EQ(addr, SocketAddress(kaddr));
    } else {
      EXPECT_FALSE(ok);
      EXPECT_EQ(std::numeric_limits<decltype(kaddr.ss_family)>::max(),
                kaddr.ss_family);
      EXPECT_EQ(0, kaddr_size);
    }

    // Convert to a nonsensical family.
    ok = SocketAddressToFamily(0xBEEF, addr, &kaddr, &kaddr_size);
    EXPECT_FALSE(ok);
    EXPECT_EQ(std::numeric_limits<decltype(kaddr.ss_family)>::max(),
              kaddr.ss_family);
    EXPECT_EQ(0, kaddr_size);
  }
}

TEST(SocketAddressTest, SocketAddressToFamilyForBind) {
  constexpr char kFail[] = "<fail>";
  const struct {
    std::string input;
    std::string out_inet;
    std::string out_inet6;
  } cases[] = {
      {kFail, kFail, kFail},
      {"192.0.2.1:80", "192.0.2.1:80", "[::ffff:192.0.2.1]:80"},
      {"0.0.0.0:80", "0.0.0.0:80", "[::]:80"},
      {"[::]:80", "0.0.0.0:80", "[::]:80"},
      {"[2001::]:80", kFail, "[2001::]:80"},
      // Note: We don't auto-convert IPv6 to IPv4.
      {"[::ffff:0.0.0.0]:80", kFail, "[::ffff:0.0.0.0]:80"},
      {"[::ffff:1.2.3.4]:80", kFail, "[::ffff:1.2.3.4]:80"},
  };

  for (const auto& c : cases) {
    SCOPED_TRACE(absl::StrCat("Case input: ", c.input));
    SocketAddress addr;
    if (c.input != kFail) {
      addr = StringToSocketAddressOrDie(c.input);
    }
    bool ok;
    sockaddr_storage kaddr;
    socklen_t kaddr_size;

    // Convert to AF_INET.
    ok = SocketAddressToFamilyForBind(AF_INET, addr, &kaddr, &kaddr_size);
    if (c.out_inet != kFail) {
      EXPECT_TRUE(ok);
      EXPECT_EQ(sizeof(sockaddr_in), kaddr_size);
      EXPECT_EQ(AF_INET, kaddr.ss_family);
      EXPECT_EQ(c.out_inet, SocketAddress(kaddr).ToString());
    } else {
      EXPECT_FALSE(ok);
      EXPECT_EQ(std::numeric_limits<decltype(kaddr.ss_family)>::max(),
                kaddr.ss_family);
      EXPECT_EQ(0, kaddr_size);
    }

    // Convert to AF_INET6.
    ok = SocketAddressToFamilyForBind(AF_INET6, addr, &kaddr, &kaddr_size);
    if (c.out_inet6 != kFail) {
      EXPECT_TRUE(ok);
      EXPECT_EQ(sizeof(sockaddr_in6), kaddr_size);
      EXPECT_EQ(AF_INET6, kaddr.ss_family);
      EXPECT_EQ(c.out_inet6, SocketAddress(kaddr).ToString());
    } else {
      EXPECT_FALSE(ok);
      EXPECT_EQ(std::numeric_limits<decltype(kaddr.ss_family)>::max(),
                kaddr.ss_family);
      EXPECT_EQ(0, kaddr_size);
    }

    // Convert to AF_UNSPEC (no change in family).
    ok = SocketAddressToFamilyForBind(AF_UNSPEC, addr, &kaddr, &kaddr_size);
    if (IsInitializedSocketAddress(addr)) {
      EXPECT_TRUE(ok);
      EXPECT_EQ(addr, SocketAddress(kaddr));
    } else {
      EXPECT_FALSE(ok);
      EXPECT_EQ(std::numeric_limits<decltype(kaddr.ss_family)>::max(),
                kaddr.ss_family);
      EXPECT_EQ(0, kaddr_size);
    }

    // Convert to a nonsensical family.
    ok = SocketAddressToFamilyForBind(0xBEEF, addr, &kaddr, &kaddr_size);
    EXPECT_FALSE(ok);
    EXPECT_EQ(std::numeric_limits<decltype(kaddr.ss_family)>::max(),
              kaddr.ss_family);
    EXPECT_EQ(0, kaddr_size);
  }
}

TEST(SocketAddressTest, SocketAddressToFamilyNoSizeOut) {
  sockaddr_storage kaddr;

  EXPECT_TRUE(SocketAddressToFamily(
      AF_UNSPEC, StringToSocketAddressOrDie("1.2.3.4:80"), &kaddr, nullptr));
  EXPECT_EQ(AF_INET, kaddr.ss_family);

  EXPECT_TRUE(SocketAddressToFamily(
      AF_UNSPEC, StringToSocketAddressOrDie("[::1]:80"), &kaddr, nullptr));
  EXPECT_EQ(AF_INET6, kaddr.ss_family);

  EXPECT_FALSE(
      SocketAddressToFamily(AF_UNSPEC, SocketAddress(), &kaddr, nullptr));
  EXPECT_EQ(std::numeric_limits<decltype(kaddr.ss_family)>::max(),
            kaddr.ss_family);
}

TEST(SocketAddressTest, IPv6LinkLocal) {
  const IPAddress fe80_1 = StringToIPAddressOrDie("fe80::1");
  const IPAddress fe80_1_if17(MakeScopedIP(fe80_1, 17));
  const IPAddress ff02_2 = StringToIPAddressOrDie("ff02::2");  // all-routers
  const IPAddress ff02_2_if17(MakeScopedIP(ff02_2, 17));

  const SocketAddress ll_https(fe80_1_if17, 443);
  EXPECT_EQ(17, ll_https.host().scope_id());
  const struct sockaddr_storage generic{ll_https.generic_address()};
  EXPECT_EQ(AF_INET6, generic.ss_family);
  auto* sap = reinterpret_cast<const struct sockaddr*>(&generic);
  EXPECT_EQ(AF_INET6, sap->sa_family);
  auto* sin6p = reinterpret_cast<const struct sockaddr_in6*>(&generic);
  EXPECT_EQ(AF_INET6, sin6p->sin6_family);
  EXPECT_EQ(fe80_1, IPAddress(sin6p->sin6_addr));
  EXPECT_EQ(443, gntohs(sin6p->sin6_port));
  EXPECT_EQ(17, sin6p->sin6_scope_id);
  EXPECT_EQ(ll_https, SocketAddress(generic));

  const SocketAddress all_routers_dns(ff02_2_if17, 53);
  const struct sockaddr_in6 sin6{all_routers_dns.ipv6_address()};
  EXPECT_EQ(AF_INET6, sin6.sin6_family);
  EXPECT_EQ(ff02_2, IPAddress(sin6.sin6_addr));
  EXPECT_EQ(53, gntohs(sin6.sin6_port));
  EXPECT_EQ(17, sin6.sin6_scope_id);
  EXPECT_EQ(all_routers_dns,
            SocketAddress(MakeSocketAddressFromSockaddrIn6(sin6).value()));

  const IPAddress fe80_2 = StringToIPAddressOrDie("fe80::2");
  const IPAddress fe80_2_if17(MakeScopedIP(fe80_2, 17));
  const IPAddress fe80_2_if22(MakeScopedIP(fe80_2, 22));

  EXPECT_NE(SocketAddress(fe80_1, 80), SocketAddress(fe80_1_if17, 80));
  EXPECT_NE(SocketAddress(fe80_1_if17, 80), SocketAddress(fe80_2_if17, 80));
  EXPECT_NE(SocketAddress(fe80_2_if17, 80), SocketAddress(fe80_2_if22, 80));

  // Hash
  absl::flat_hash_set<SocketAddress> addrs;
  for (const auto& ip : {fe80_1, fe80_2, fe80_1_if17, fe80_2_if17, fe80_2_if22,
                         ff02_2, ff02_2_if17}) {
    addrs.insert(SocketAddress(ip, 80));
  }

  EXPECT_EQ(7, addrs.size());

  EXPECT_EQ(1, addrs.count(SocketAddress(fe80_1, 80)));
  EXPECT_EQ(1, addrs.count(SocketAddress(fe80_2, 80)));
  EXPECT_EQ(1, addrs.count(SocketAddress(fe80_1_if17, 80)));
  EXPECT_EQ(1, addrs.count(SocketAddress(fe80_2_if17, 80)));
  EXPECT_EQ(1, addrs.count(SocketAddress(fe80_2_if22, 80)));
  EXPECT_EQ(1, addrs.count(SocketAddress(ff02_2, 80)));
  EXPECT_EQ(1, addrs.count(SocketAddress(ff02_2_if17, 80)));

  // Also test the absl::Hash version.
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(std::make_tuple(
      SocketAddress(fe80_1, 22), SocketAddress(fe80_2, 22),
      SocketAddress(fe80_1_if17, 22), SocketAddress(fe80_2_if17, 22),
      SocketAddress(fe80_2_if22, 22), SocketAddress(ff02_2, 22),
      SocketAddress(ff02_2_if17, 22))));

  // TODO: from/to string
  // TODO: from/to packed string
}

TEST(SocketAddressDeathTest, SocketAddressToFamilyError) {
#ifdef __Fuchsia__
  // This will work with `cc_fuchsia_emulator_test`, but that requires
  // complicated setup in the BUILD file.  Not worth it for this one test.
  GTEST_SKIP() << "socket() fails with EPERM on Fuchsia";
#endif
  // Create a SocketAddressToFamily() error address.
  const SocketAddress empty;
  sockaddr_storage kaddr;
  socklen_t kaddr_size;
  EXPECT_FALSE(SocketAddressToFamily(AF_INET6, empty, &kaddr, &kaddr_size));

  // Make sure that we can't bind() to an error address.
  int fd = socket(PF_INET6, SOCK_DGRAM, IPPROTO_UDP);
  EXPECT_GE(fd, 0);
  EXPECT_LT(bind(fd, sockaddr_cast(&kaddr), kaddr_size), 0);
#ifdef _WIN32
  closesocket(fd);
#else
  close(fd);
#endif

  // Make sure that we can't cast an error back to SocketAddress.
  EXPECT_DEBUG_DEATH(
      SocketAddress foo(kaddr),
      absl::StrCat("Unknown address family ",
                   std::numeric_limits<decltype(kaddr.ss_family)>::max()));
}

TEST(SocketAddressDeathTest, UninitializedGenericAddress) {
  const SocketAddress empty;
  EXPECT_DEATH(empty.generic_address(),
               "Called generic_address.. on <uninitialized SocketAddress>");
}

TEST(SocketAddressDeathTest, EmergencyZeroPort) {
  SocketAddress empty;

  if (DEBUG_MODE) {
    EXPECT_DEBUG_DEATH(empty.port(), "empty SocketAddress");
  } else {
    ScopedMockLogVerifier log("empty SocketAddress");
    EXPECT_EQ(0, empty.port());
  }
}

TEST(SocketAddressDeathTest, EmergencyEmptyString) {
  SocketAddress empty;

  if (DEBUG_MODE) {
    EXPECT_DEBUG_DEATH(empty.ToString(), "empty SocketAddress");
  } else {
    ScopedMockLogVerifier log("empty SocketAddress");
    EXPECT_EQ("", empty.ToString());
  }
}

#ifndef _WIN32
// An IP range should be no longer than an IP address, since we can fit the
// length into tail padding.
TEST(IPRangeTest, Size) { static_assert(sizeof(IPRange) == sizeof(IPAddress)); }
#endif

// All of the methods of creating an uninitialized range should give the same
// result.
TEST(IPRangeTest, Uninitialized) {
  const IPRange a;
  const IPRange b{IPAddress()};
  const IPRange c = {IPAddress(), 1234};
  const IPRange d = {IPAddress(), -1234};

  EXPECT_FALSE(IsInitializedRange(a));
  EXPECT_FALSE(IsInitializedRange(b));
  EXPECT_FALSE(IsInitializedRange(c));
  EXPECT_FALSE(IsInitializedRange(d));

  EXPECT_EQ(a, b);
  EXPECT_EQ(a, c);
  EXPECT_EQ(a, d);
}

TEST(IPRangeTest, BasicTest4) {
  IPAddress addr;
  const uint16_t kPrefixLength = 16;
  ASSERT_TRUE(StringToIPAddress("192.168.0.0", &addr));
  IPRange subnet(addr, kPrefixLength);
  EXPECT_EQ(addr, subnet.host());
  EXPECT_EQ(kPrefixLength, subnet.length());

  // Test copy construction.
  IPRange another_subnet = subnet;
  EXPECT_EQ(addr, another_subnet.host());
  EXPECT_EQ(kPrefixLength, another_subnet.length());

  // Test IPAddress constructor.
  EXPECT_EQ(addr, IPRange(addr).host());
  EXPECT_EQ(32, IPRange(addr).length());
}

TEST(IPRangeTest, BasicTest6) {
  IPAddress addr;
  const uint16_t kPrefixLength = 64;
  ASSERT_TRUE(StringToIPAddress("2001:700:300:1800::", &addr));
  IPRange subnet(addr, kPrefixLength);
  EXPECT_EQ(addr, subnet.host());
  EXPECT_EQ(kPrefixLength, subnet.length());

  // Test copy construction.
  IPRange another_subnet = subnet;
  EXPECT_EQ(addr, another_subnet.host());
  EXPECT_EQ(kPrefixLength, another_subnet.length());

  // Test IPAddress constructor.
  EXPECT_EQ(addr, IPRange(addr).host());
  EXPECT_EQ(128, IPRange(addr).length());
}

TEST(IPRangeTest, AnyRanges) {
  EXPECT_EQ("0.0.0.0/0", IPRange::Any4().ToString());
  EXPECT_EQ("::/0", IPRange::Any6().ToString());
}

TEST(IPRangeTest, ToAndFromString4) {
  const std::string kIPString = "192.168.0.0";
  const int kLength = 16;
  const std::string kSubnetString = kIPString + absl::StrFormat("/%u", kLength);
  const std::string kBogusSubnetString1 = "192.168.0.0/8";
  const std::string kBogusSubnetString2 = "192.256.0.0/16";
  const std::string kBogusSubnetString3 = "192.168.0.0/34";
  const std::string kBogusSubnetString4 = "0.0.0.0/-1";
  const std::string kBogusSubnetString5 = "0.0.0.0/+1";
  const std::string kBogusSubnetString6 = "0.0.0.0/";
  const std::string kBogusSubnetString7 = "192.168.0.0/16/16";
  const std::string kBogusSubnetString8 = "192.168.0.0/16 ";
  const std::string kBogusSubnetString9 = " 192.168.0.0/16";
  const std::string kBogusSubnetString10 = "192.168.0.0 /16";

  IPRange subnet;
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString1, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString2, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString3, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString4, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString5, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString6, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString7, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString8, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString9, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString10, &subnet));
  ASSERT_TRUE(StringToIPRange(kSubnetString, nullptr));
  ASSERT_TRUE(StringToIPRange(kSubnetString, &subnet));

  IPAddress addr4;
  ASSERT_TRUE(StringToIPAddress(kIPString, &addr4));
  EXPECT_EQ(addr4, subnet.host());
  EXPECT_EQ(kLength, subnet.length());

  EXPECT_EQ(kSubnetString, subnet.ToString());

  EXPECT_TRUE(StringToIPRangeAndTruncate(kBogusSubnetString1, &subnet));
  EXPECT_EQ("192.0.0.0/8", subnet.ToString());
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString2, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString3, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString4, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString5, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString6, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString7, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString8, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString9, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString10, &subnet));
}

TEST(IPRangeTest, DottedQuadNetmasks) {
  const std::string kIPString = "192.168.0.0";
  const std::string kDottedQuadNetmaskString = "255.255.0.0";
  const int kLength = 16;
  const std::string kSubnetString = kIPString + absl::StrFormat("/%u", kLength);
  const std::string kDottedQuadSubnetString =
      kIPString + "/" + kDottedQuadNetmaskString;

  // Check valid strings.
  IPRange cidr;
  IPRange dotted_quad;
  ASSERT_TRUE(StringToIPRangeAndTruncate(kSubnetString, &cidr));
  ASSERT_TRUE(
      StringToIPRangeAndTruncate(kDottedQuadSubnetString, &dotted_quad));
  ASSERT_TRUE(cidr == dotted_quad);

  // Check some corner cases.
  EXPECT_TRUE(StringToIPRange("0.0.0.0/0.0.0.0", &cidr));
  EXPECT_EQ(0, cidr.length());
  EXPECT_EQ(IPAddress::Any4(), cidr.host());

  ASSERT_TRUE(StringToIPRange("127.0.0.1/255.255.255.255", &cidr));
  EXPECT_EQ(32, cidr.length());
  EXPECT_EQ(IPAddress::Loopback4(), cidr.host());

  // If .expected_host_string is empty then .dotted_quad_string is
  // expected to FAIL StringToIPRangeAndTruncate().
  const struct DottedQuadExpectations {
    std::string dotted_quad_string;
    std::string expected_host_string;
    int expected_length;
  } dotted_quad_tests[] = {
      {"1.2.3.4/0.0.0.1", "", -1},
      {"1.2.3.4/1.0.0.0", "", -1},
      {"1.2.3.4/127.255.255.255", "", -1},
      {"1.2.3.4/254.255.255.255", "", -1},
      {"1.2.3.4/255.255.255.254", "1.2.3.4", 31},
      {"1.2.3.4/0.0.0.0", "0.0.0.0", 0},
  };

  for (size_t i = 0; i < std::size(dotted_quad_tests); ++i) {
    IPRange range;
    IPAddress host;

    if (dotted_quad_tests[i].expected_host_string.empty()) {
      // The dotted quad string should be rejected as invalid.
      ASSERT_FALSE(StringToIPRangeAndTruncate(
          dotted_quad_tests[i].dotted_quad_string, &range));
      continue;
    }
    ASSERT_TRUE(StringToIPRangeAndTruncate(
        dotted_quad_tests[i].dotted_quad_string, &range));
    ASSERT_TRUE(
        StringToIPAddress(dotted_quad_tests[i].expected_host_string, &host));
    EXPECT_EQ(host, range.host()) << dotted_quad_tests[i].dotted_quad_string
                                  << " host equality expectation failed";
    EXPECT_EQ(dotted_quad_tests[i].expected_length, range.length())
        << dotted_quad_tests[i].dotted_quad_string
        << " length equality expectation failed";
  }
}

class BogusDottedQuadNetmaskTest
    : public testing::TestWithParam<BogusTestCase> {};

TEST_P(BogusDottedQuadNetmaskTest, StringToIPRangeAndTruncateFails) {
  EXPECT_FALSE(StringToIPRangeAndTruncate(GetParam().str, nullptr));
}

INSTANTIATE_TEST_SUITE_P(
    , BogusDottedQuadNetmaskTest,
    testing::Values(
        BogusTestCase{"NonContiguousMask", "192.168.0.0/128.255.0.0"},
        BogusTestCase{"IPv6WithDottedQuadMask", "3ffe::1/255.255.0.0"},
        BogusTestCase{"OneOctetMask", "1.2.3.4/255"},
        BogusTestCase{"OneOctetTrailingDotMask", "1.2.3.4/255."},
        BogusTestCase{"TwoOctetMask", "1.2.3.4/255.255"},
        BogusTestCase{"TwoOctetTrailingDotMask", "1.2.3.4/255.255."},
        BogusTestCase{"ThreeOctetMask", "1.2.3.4/255.255.255"},
        BogusTestCase{"ThreeOctetTrailingDotMask", "1.2.3.4/255.255.255."},
        BogusTestCase{"MaskOctetOutOfRange", "1.2.3.4/255.255.255.256"},
        BogusTestCase{"NegativeMaskOctet", "1.2.3.4/255.255.255.-255"},
        BogusTestCase{"PlusSignInMask", "1.2.3.4/255.255.255.+255"},
        BogusTestCase{"GarbageInMask", "1.2.3.4/255.255.255.garbage"},
        BogusTestCase{"LeadingZeroInMask", "1.2.3.4/0255.255.255.255"},
        BogusTestCase{"MultipleLeadingZerosInMask",
                      "1.2.3.4/255.255.255.000255"}),
    [](const testing::TestParamInfo<BogusTestCase>& info) {
      return std::string(info.param.test_name);
    });

TEST(IPRangeTest, FromAddressString4) {
  const std::string kIPString = "192.168.0.0";
  IPAddress addr4;
  ASSERT_TRUE(StringToIPAddress(kIPString, &addr4));

  IPRange subnet;
  EXPECT_TRUE(StringToIPRange(kIPString, &subnet));
  EXPECT_EQ(addr4, subnet.host());
  EXPECT_EQ(32, subnet.length());

  EXPECT_TRUE(StringToIPRangeAndTruncate(kIPString, &subnet));
  EXPECT_EQ(addr4, subnet.host());
  EXPECT_EQ(32, subnet.length());
}

TEST(IPRangeTest, ToAndFromString6) {
  const std::string kIPString = "2001:700:300:1800::";
  const int kLength = 64;
  const std::string kSubnetString = kIPString + absl::StrFormat("/%u", kLength);
  const std::string kBogusSubnetString1 = "2001:700:300:1800::/48";
  const std::string kBogusSubnetString2 = "2001:700:300:180g::/64";
  const std::string kBogusSubnetString3 = "2001:700:300:1800::/129";
  const std::string kBogusSubnetString4 = "::/-1";
  const std::string kBogusSubnetString5 = "::/+1";
  const std::string kBogusSubnetString6 = "::/";
  const std::string kBogusSubnetString7 = "2001:700:300:1800::/64/64";
  const std::string kBogusSubnetString8 = "2001:700:300:1800::/64 ";
  const std::string kBogusSubnetString9 = " 2001:700:300:1800::/64";
  const std::string kBogusSubnetString10 = "2001:700:300:1800:: /64";

  IPRange subnet;
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString1, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString2, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString3, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString4, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString5, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString6, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString7, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString8, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString9, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString10, &subnet));
  ASSERT_TRUE(StringToIPRange(kSubnetString, nullptr));
  ASSERT_TRUE(StringToIPRange(kSubnetString, &subnet));

  IPAddress addr6;
  ASSERT_TRUE(StringToIPAddress(kIPString, &addr6));
  EXPECT_EQ(addr6, subnet.host());
  EXPECT_EQ(kLength, subnet.length());

  EXPECT_EQ(kSubnetString, subnet.ToString());

  EXPECT_TRUE(StringToIPRangeAndTruncate(kBogusSubnetString1, &subnet));
  EXPECT_EQ("2001:700:300::/48", subnet.ToString());
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString2, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString3, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString4, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString5, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString6, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString7, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString8, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString9, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString10, &subnet));
}

TEST(IPRangeTest, FromAddressString6) {
  const std::string kIPString = "2001:700:300:1800::";
  IPAddress addr6;
  ASSERT_TRUE(StringToIPAddress(kIPString, &addr6));

  IPRange subnet;
  EXPECT_TRUE(StringToIPRange(kIPString, &subnet));
  EXPECT_EQ(addr6, subnet.host());
  EXPECT_EQ(128, subnet.length());

  EXPECT_TRUE(StringToIPRangeAndTruncate(kIPString, &subnet));
  EXPECT_EQ(addr6, subnet.host());
  EXPECT_EQ(128, subnet.length());
}

TEST(IPRangeTest, Joining) {
  std::vector<IPRange> v = {StringToIPRangeAndTruncateOrDie("192.0.2.0/24"),
                            StringToIPRangeAndTruncateOrDie("2001:db8::/32"),
                            StringToIPRangeAndTruncateOrDie("0.0.0.0/0"),
                            StringToIPRangeAndTruncateOrDie("::/0")};
  EXPECT_EQ("192.0.2.0/24 <> 2001:db8::/32 <> 0.0.0.0/0 <> ::/0",
            absl::StrJoin(v, " <> ", IPRangeJoinFormatter()));
}

TEST(IPRangeTest, Equality) {
  const std::string kIPv4String1 = "192.168.0.0/16";
  const std::string kIPv4String2 = "192.168.0.0/24";
  const std::string kIPv6String1 = "2001:700:300:1800::/64";
  const std::string kIPv6String2 = "2001:700:300:1800:0:0::/64";
  const std::string kIPv6String3 = "2001:700:300:dc0f::/64";

  IPRange subnet4_1, subnet4_2;
  IPRange subnet6_1, subnet6_2, subnet6_3;

  ASSERT_TRUE(StringToIPRange(kIPv4String1, &subnet4_1));
  ASSERT_TRUE(StringToIPRange(kIPv4String2, &subnet4_2));
  ASSERT_TRUE(StringToIPRange(kIPv6String1, &subnet6_1));
  ASSERT_TRUE(StringToIPRange(kIPv6String2, &subnet6_2));
  ASSERT_TRUE(StringToIPRange(kIPv6String3, &subnet6_3));

  // operator==
  EXPECT_TRUE(subnet4_1 == subnet4_1);
  EXPECT_FALSE(subnet4_1 == subnet4_2);
  EXPECT_FALSE(subnet4_1 == subnet6_1);
  EXPECT_FALSE(subnet4_1 == subnet6_2);
  EXPECT_FALSE(subnet4_1 == subnet6_3);

  EXPECT_FALSE(subnet4_2 == subnet4_1);
  EXPECT_TRUE(subnet4_2 == subnet4_2);
  EXPECT_FALSE(subnet4_2 == subnet6_1);
  EXPECT_FALSE(subnet4_2 == subnet6_2);
  EXPECT_FALSE(subnet4_2 == subnet6_3);

  EXPECT_FALSE(subnet6_1 == subnet4_1);
  EXPECT_FALSE(subnet6_1 == subnet4_2);
  EXPECT_TRUE(subnet6_1 == subnet6_1);
  EXPECT_TRUE(subnet6_1 == subnet6_2);
  EXPECT_FALSE(subnet6_1 == subnet6_3);

  EXPECT_FALSE(subnet6_2 == subnet4_1);
  EXPECT_FALSE(subnet6_2 == subnet4_2);
  EXPECT_TRUE(subnet6_2 == subnet6_1);
  EXPECT_TRUE(subnet6_2 == subnet6_2);
  EXPECT_FALSE(subnet6_2 == subnet6_3);

  EXPECT_FALSE(subnet6_3 == subnet4_1);
  EXPECT_FALSE(subnet6_3 == subnet4_2);
  EXPECT_FALSE(subnet6_3 == subnet6_1);
  EXPECT_FALSE(subnet6_3 == subnet6_2);
  EXPECT_TRUE(subnet6_3 == subnet6_3);

  // operator!= (same tests, just inverted)
  EXPECT_FALSE(subnet4_1 != subnet4_1);
  EXPECT_TRUE(subnet4_1 != subnet4_2);
  EXPECT_TRUE(subnet4_1 != subnet6_1);
  EXPECT_TRUE(subnet4_1 != subnet6_2);
  EXPECT_TRUE(subnet4_1 != subnet6_3);

  EXPECT_TRUE(subnet4_2 != subnet4_1);
  EXPECT_FALSE(subnet4_2 != subnet4_2);
  EXPECT_TRUE(subnet4_2 != subnet6_1);
  EXPECT_TRUE(subnet4_2 != subnet6_2);
  EXPECT_TRUE(subnet4_2 != subnet6_3);

  EXPECT_TRUE(subnet6_1 != subnet4_1);
  EXPECT_TRUE(subnet6_1 != subnet4_2);
  EXPECT_FALSE(subnet6_1 != subnet6_1);
  EXPECT_FALSE(subnet6_1 != subnet6_2);
  EXPECT_TRUE(subnet6_1 != subnet6_3);

  EXPECT_TRUE(subnet6_2 != subnet4_1);
  EXPECT_TRUE(subnet6_2 != subnet4_2);
  EXPECT_FALSE(subnet6_2 != subnet6_1);
  EXPECT_FALSE(subnet6_2 != subnet6_2);
  EXPECT_TRUE(subnet6_2 != subnet6_3);

  EXPECT_TRUE(subnet6_3 != subnet4_1);
  EXPECT_TRUE(subnet6_3 != subnet4_2);
  EXPECT_TRUE(subnet6_3 != subnet6_1);
  EXPECT_TRUE(subnet6_3 != subnet6_2);
  EXPECT_FALSE(subnet6_3 != subnet6_3);
}

TEST(IPRangeTest, LowerAndUpper4) {
  IPAddress expected, ip;
  IPRange range;

  ASSERT_TRUE(StringToIPAddress("1.2.3.4", &ip));

  // 1.2.3.4/0
  range = IPRange(ip, 0);
  ASSERT_TRUE(StringToIPAddress("0.0.0.0", &expected));
  EXPECT_EQ(expected, range.host());
  EXPECT_EQ(expected, range.network_address());
  ASSERT_TRUE(StringToIPAddress("255.255.255.255", &expected));
  EXPECT_EQ(expected, range.broadcast_address());

  // 1.2.3.4/25
  range = IPRange(ip, 25);
  ASSERT_TRUE(StringToIPAddress("1.2.3.0", &expected));
  EXPECT_EQ(expected, range.host());
  EXPECT_EQ(expected, range.network_address());
  ASSERT_TRUE(StringToIPAddress("1.2.3.127", &expected));
  EXPECT_EQ(expected, range.broadcast_address());

  // 1.2.3.4/31
  range = IPRange(ip, 31);
  EXPECT_EQ(ip, range.host());
  EXPECT_EQ(ip, range.network_address());
  ASSERT_TRUE(StringToIPAddress("1.2.3.5", &expected));
  EXPECT_EQ(expected, range.broadcast_address());

  // 1.2.3.4/32
  range = IPRange(ip, 32);
  EXPECT_EQ(ip, range.host());
  EXPECT_EQ(ip, range.network_address());
  EXPECT_EQ(ip, range.broadcast_address());
}

TEST(IPRangeTest, LowerAndUpper6) {
  IPAddress expected, ip;
  IPRange range;

  ASSERT_TRUE(StringToIPAddress("1:2:3:4:5:6:7:8", &ip));

  // 1:2:3:4:5:6:7:8/0
  range = IPRange(ip, 0);
  ASSERT_TRUE(StringToIPAddress("::", &expected));
  EXPECT_EQ(expected, range.host());
  EXPECT_EQ(expected, range.network_address());
  ASSERT_TRUE(
      StringToIPAddress("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", &expected));
  EXPECT_EQ(expected, range.broadcast_address());

  // 1:2:3:4:5:6:7:8/113
  range = IPRange(ip, 113);
  ASSERT_TRUE(StringToIPAddress("1:2:3:4:5:6:7:0", &expected));
  EXPECT_EQ(expected, range.host());
  EXPECT_EQ(expected, range.network_address());
  ASSERT_TRUE(StringToIPAddress("1:2:3:4:5:6:7:7fff", &expected));
  EXPECT_EQ(expected, range.broadcast_address());

  // 1:2:3:4:5:6:7:8/127
  range = IPRange(ip, 127);
  EXPECT_EQ(ip, range.host());
  EXPECT_EQ(ip, range.network_address());
  ASSERT_TRUE(StringToIPAddress("1:2:3:4:5:6:7:9", &expected));
  EXPECT_EQ(expected, range.broadcast_address());

  // 1:2:3:4:5:6:7:8/128
  range = IPRange(ip, 128);
  EXPECT_EQ(ip, range.host());
  EXPECT_EQ(ip, range.network_address());
  EXPECT_EQ(ip, range.broadcast_address());
}

TEST(IPRangeTest, IsWithinSubnet) {
  const IPRange subnet1 = StringToIPRangeOrDie("192.168.0.0/16");
  const IPRange subnet2 = StringToIPRangeOrDie("192.168.0.0/24");
  const IPRange subnet3 = StringToIPRangeOrDie("2001:700:300:1800::/64");
  const IPRange subnet4 = StringToIPRangeOrDie("::/0");

  const IPAddress addr1 = StringToIPAddressOrDie("192.168.1.5");
  const IPAddress addr2 = StringToIPAddressOrDie("2001:700:300:1800::1");
  const IPAddress addr3 = StringToIPAddressOrDie("2001:700:300:1801::1");

  EXPECT_TRUE(IsWithinSubnet(subnet1, addr1));
  EXPECT_FALSE(IsWithinSubnet(subnet2, addr1));
  EXPECT_FALSE(IsWithinSubnet(subnet3, addr1));
  EXPECT_FALSE(IsWithinSubnet(subnet4, addr1));

  EXPECT_FALSE(IsWithinSubnet(subnet1, addr2));
  EXPECT_FALSE(IsWithinSubnet(subnet2, addr2));
  EXPECT_TRUE(IsWithinSubnet(subnet3, addr2));
  EXPECT_TRUE(IsWithinSubnet(subnet4, addr2));

  EXPECT_FALSE(IsWithinSubnet(subnet1, addr3));
  EXPECT_FALSE(IsWithinSubnet(subnet2, addr3));
  EXPECT_FALSE(IsWithinSubnet(subnet3, addr3));
  EXPECT_TRUE(IsWithinSubnet(subnet4, addr3));

  EXPECT_FALSE(IsWithinSubnet(subnet1, IPAddress()));
  EXPECT_FALSE(IsWithinSubnet(IPRange(), addr1));
}

TEST(IPRangeTest, IsProperSubRange) {
  const std::string kRangeString[] = {
      "192.168.0.0/15",  "192.169.0.0/16", "192.168.0.0/24",
      "192.168.0.80/28", "::/0",           "2001:700:300:1800::/64",
  };

  IPRange range[std::size(kRangeString)];
  for (int i = 0; i < std::size(kRangeString); ++i) {
    ASSERT_TRUE(StringToIPRange(kRangeString[i], &range[i]));
    EXPECT_FALSE(IsProperSubRange(range[i], range[i]));
  }

  EXPECT_TRUE(IsProperSubRange(range[0], range[1]));
  EXPECT_TRUE(IsProperSubRange(range[0], range[2]));
  EXPECT_TRUE(IsProperSubRange(range[0], range[3]));
  EXPECT_FALSE(IsProperSubRange(range[0], range[4]));
  EXPECT_FALSE(IsProperSubRange(range[0], range[5]));

  EXPECT_FALSE(IsProperSubRange(range[1], range[0]));
  EXPECT_FALSE(IsProperSubRange(range[1], range[2]));
  EXPECT_FALSE(IsProperSubRange(range[1], range[3]));
  EXPECT_FALSE(IsProperSubRange(range[1], range[4]));
  EXPECT_FALSE(IsProperSubRange(range[1], range[5]));

  EXPECT_FALSE(IsProperSubRange(range[2], range[0]));
  EXPECT_FALSE(IsProperSubRange(range[2], range[1]));
  EXPECT_TRUE(IsProperSubRange(range[2], range[3]));
  EXPECT_FALSE(IsProperSubRange(range[2], range[4]));
  EXPECT_FALSE(IsProperSubRange(range[2], range[5]));

  EXPECT_FALSE(IsProperSubRange(range[3], range[0]));
  EXPECT_FALSE(IsProperSubRange(range[3], range[1]));
  EXPECT_FALSE(IsProperSubRange(range[3], range[2]));
  EXPECT_FALSE(IsProperSubRange(range[3], range[4]));
  EXPECT_FALSE(IsProperSubRange(range[3], range[5]));

  EXPECT_FALSE(IsProperSubRange(range[4], range[0]));
  EXPECT_FALSE(IsProperSubRange(range[4], range[1]));
  EXPECT_FALSE(IsProperSubRange(range[4], range[2]));
  EXPECT_FALSE(IsProperSubRange(range[4], range[3]));
  EXPECT_TRUE(IsProperSubRange(range[4], range[5]));

  EXPECT_FALSE(IsProperSubRange(range[5], range[0]));
  EXPECT_FALSE(IsProperSubRange(range[5], range[1]));
  EXPECT_FALSE(IsProperSubRange(range[5], range[2]));
  EXPECT_FALSE(IsProperSubRange(range[5], range[3]));
  EXPECT_FALSE(IsProperSubRange(range[5], range[4]));

  for (const IPRange& r : range) {
    EXPECT_FALSE(IsProperSubRange(IPRange(), r));
    EXPECT_FALSE(IsProperSubRange(r, IPRange()));
  }
}

TEST(IPRangeTest, IPRangeOverlap) {
  // Basic overlap test
  EXPECT_TRUE(IPRangesOverlap(StringToIPRangeOrDie("192.168.0.0/15"),
                              StringToIPRangeOrDie("192.169.0.0/16")));
  EXPECT_FALSE(IPRangesOverlap(StringToIPRangeOrDie("192.168.0.0/15"),
                               StringToIPRangeOrDie("193.169.0.0/16")));

  // Overlap with Any ranges
  EXPECT_TRUE(
      IPRangesOverlap(StringToIPRangeOrDie("192.168.0.0/15"), IPRange::Any4()));
  EXPECT_TRUE(IPRangesOverlap(StringToIPRangeOrDie("2001:700:300:1800::/64"),
                              IPRange::Any6()));

  // Default c-tor doesn't overlap
  EXPECT_FALSE(IPRangesOverlap(IPRange(), IPRange::Any4()));
  EXPECT_FALSE(IPRangesOverlap(IPRange(), IPRange::Any6()));

  // Overlap with self
  EXPECT_TRUE(IPRangesOverlap(StringToIPRangeOrDie("192.168.0.0/15"),
                              StringToIPRangeOrDie("192.168.0.0/15")));
  EXPECT_TRUE(IPRangesOverlap(StringToIPRangeOrDie("2001:700:300:1800::/64"),
                              StringToIPRangeOrDie("2001:700:300:1800::/64")));
}

TEST(IPRangeTest, TruncateIPAddress) {
  // Basic truncation.
  EXPECT_EQ(StringToIPAddressOrDie("192.0.2.0"),
            TruncateIPAddress(StringToIPAddressOrDie("192.0.2.1"), 24));
  EXPECT_EQ(StringToIPAddressOrDie("2001:db8::"),
            TruncateIPAddress(StringToIPAddressOrDie("2001:db8:f00::1"), 32));

  // Large lengths are okay.
  EXPECT_EQ(StringToIPAddressOrDie("192.0.2.1"),
            TruncateIPAddress(StringToIPAddressOrDie("192.0.2.1"), 999));
  EXPECT_EQ(StringToIPAddressOrDie("2001:db8:f00::1"),
            TruncateIPAddress(StringToIPAddressOrDie("2001:db8:f00::1"), 999));

  // The length parameter doesn't do anything surprising.
  int length = 999;
  EXPECT_EQ(StringToIPAddressOrDie("192.0.2.1"),
            TruncateIPAddress(StringToIPAddressOrDie("192.0.2.1"), length));
  EXPECT_EQ(999, length);

  // Negative lengths are prohibited in debug mode.
  EXPECT_DEBUG_DEATH(TruncateIPAddress(StringToIPAddressOrDie("192.0.2.0"), -1),
                     "Invalid truncation:");
  EXPECT_DEBUG_DEATH(
      TruncateIPAddress(StringToIPAddressOrDie("2001:db8::"), -1),
      "Invalid truncation:");
  if (!DEBUG_MODE) {
    EXPECT_EQ(IPAddress(),
              TruncateIPAddress(StringToIPAddressOrDie("192.0.2.0"), -1));
    EXPECT_EQ(IPAddress(),
              TruncateIPAddress(StringToIPAddressOrDie("2001:db8::"), -1));
  }

  // Empty addresses are prohibited in debug mode.
  EXPECT_DEBUG_DEATH(TruncateIPAddress(IPAddress(), -1),
                     "Can't truncate <uninitialized IPAddress>");
  EXPECT_DEBUG_DEATH(TruncateIPAddress(IPAddress(), 24),
                     "Can't truncate <uninitialized IPAddress>");
  if (!DEBUG_MODE) {
    EXPECT_EQ(IPAddress(), TruncateIPAddress(IPAddress(), -1));
    EXPECT_EQ(IPAddress(), TruncateIPAddress(IPAddress(), 24));
  }
}

TEST(IPRangeTest, Truncation) {
  {
    IPAddress addr;
    ASSERT_TRUE(StringToIPAddress("129.240.2.3", &addr));
    EXPECT_EQ("0.0.0.0/0", TruncatedAddressToIPRange(addr, 0).ToString());
    EXPECT_EQ("129.192.0.0/10", TruncatedAddressToIPRange(addr, 10).ToString());
    EXPECT_EQ("129.240.2.3/32", TruncatedAddressToIPRange(addr, 32).ToString());
  }

  {
    IPAddress addr;
    ASSERT_TRUE(StringToIPAddress("8001:700:300:1800::1", &addr));
    EXPECT_EQ("::/0", TruncatedAddressToIPRange(addr, 0).ToString());
    EXPECT_EQ("8001:700:300::/48",
              TruncatedAddressToIPRange(addr, 48).ToString());
    EXPECT_EQ("8001:700:300:1800::1/128",
              TruncatedAddressToIPRange(addr, 128).ToString());
  }

  {
    IPAddress addr;
    ASSERT_TRUE(
        StringToIPAddress("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", &addr));
    EXPECT_EQ("::/0", TruncatedAddressToIPRange(addr, 0).ToString());
    EXPECT_EQ("8000::/1", TruncatedAddressToIPRange(addr, 1).ToString());

    EXPECT_EQ("ffff:fffe::/31", TruncatedAddressToIPRange(addr, 31).ToString());
    EXPECT_EQ("ffff:ffff::/32", TruncatedAddressToIPRange(addr, 32).ToString());
    EXPECT_EQ("ffff:ffff:8000::/33",
              TruncatedAddressToIPRange(addr, 33).ToString());

    EXPECT_EQ("ffff:ffff:ffff:fffe::/63",
              TruncatedAddressToIPRange(addr, 63).ToString());
    EXPECT_EQ("ffff:ffff:ffff:ffff::/64",
              TruncatedAddressToIPRange(addr, 64).ToString());
    EXPECT_EQ("ffff:ffff:ffff:ffff:8000::/65",
              TruncatedAddressToIPRange(addr, 65).ToString());

    EXPECT_EQ("ffff:ffff:ffff:ffff:ffff:fffe::/95",
              TruncatedAddressToIPRange(addr, 95).ToString());
    EXPECT_EQ("ffff:ffff:ffff:ffff:ffff:ffff::/96",
              TruncatedAddressToIPRange(addr, 96).ToString());
    const std::string expected_address =
        "ffff:ffff:ffff:ffff:ffff:ffff:8000:0/97";
    EXPECT_EQ(expected_address, TruncatedAddressToIPRange(addr, 97).ToString());

    EXPECT_EQ("ffff:ffff:ffff:ffff:ffff:ffff:ffff:fffe/127",
              TruncatedAddressToIPRange(addr, 127).ToString());
    EXPECT_EQ("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff/128",
              TruncatedAddressToIPRange(addr, 128).ToString());
  }

  {
    IPAddress addr;
    ASSERT_TRUE(StringToIPAddress("2001:4860:ffff::", &addr));
    EXPECT_EQ("2001:4860:f000::/36",
              TruncatedAddressToIPRange(addr, 36).ToString());
  }

  {
    IPAddress addr;
    ASSERT_TRUE(StringToIPAddress("127.0.0.1", &addr));
    EXPECT_EQ("127.0.0.1/32", TruncatedAddressToIPRange(addr, 33).ToString());
  }

  {
    IPAddress addr;
    ASSERT_TRUE(StringToIPAddress("::1", &addr));
    EXPECT_EQ("::1/128", TruncatedAddressToIPRange(addr, 129).ToString());
  }

  {
    const IPRange truncated = TruncatedAddressToIPRange(IPAddress(), -1234);
    EXPECT_EQ(IPRange(), truncated);
    EXPECT_EQ(IPAddress(), truncated.host());
    EXPECT_EQ(-1, truncated.length());
  }

  // MxN test of various bit positions and prefix lengths.
  {
    for (int bit = 0; bit < 128; bit++) {
      const IPAddress addr =
          UInt128ToIPAddress(absl::uint128(1) << (127 - bit));
      EXPECT_NE(IPAddress::Any6(), addr);
      for (int len = std::max(0, bit - 5); len <= std::min(128, bit + 5);
           len++) {
        const IPAddress truncated = TruncatedAddressToIPRange(addr, len).host();
        if (bit < len) {
          EXPECT_EQ(addr, truncated);
        } else {
          EXPECT_EQ(IPAddress::Any6(), truncated);
        }
      }
    }
    for (int bit = 0; bit < 32; bit++) {
      const IPAddress addr = HostUInt32ToIPAddress(1U << (31 - bit));
      EXPECT_NE(IPAddress::Any4(), addr);
      for (int len = 0; len <= 32; len++) {
        const IPAddress truncated = TruncatedAddressToIPRange(addr, len).host();
        if (bit < len) {
          EXPECT_EQ(addr, truncated);
        } else {
          EXPECT_EQ(IPAddress::Any4(), truncated);
        }
      }
    }
  }
}

// IPRange tests for ToPackedString() and PackedStringToIPRange().
TEST(IPRangeTest, PacksEmptyRange) {
  EXPECT_DEBUG_DEATH(IPRange().ToPackedString(), "Uninitialized address");
  if (!DEBUG_MODE) {
    EXPECT_EQ("", IPRange().ToPackedString());
  }
  IPRange result;
  EXPECT_FALSE(PackedStringToIPRange("", &result));
}

// This test takes a sample IPv4 and IPv6 address, and for each mask length,
// generates an IPRange and a truncated IPRange, then packs and unpacks these
// to verify that the truncated IPRange is reconstructed in both cases.
TEST(IPRangeTest, PacksIPv4AndIPv6Range) {
  std::string ips[] = {"172.16.255.47",
                       "1.2.3.4",
                       "0.0.0.0",
                       "0.0.1.0",
                       "0.1.0.1",
                       "1234:5678:aaaa:bbbb:cccc:dddd:eeee:ffff",
                       "2001:dead::1",
                       "2001::1",
                       "2001::",
                       "::1",
                       "0::",
                       "127.0.0.1",
                       "2001:dead:beaf::1",
                       "2001:dead::"};
  for (int i = 0; i < std::size(ips); ++i) {
    IPAddress ip = StringToIPAddressOrDie(ips[i]);
    int max_subnet_length = (ip.address_family() == AF_INET ? 32 : 128);
    std::string packed;
    IPRange unpacked;
    for (int subnet_length = 0; subnet_length <= max_subnet_length;
         ++subnet_length) {
      const IPRange truncated = TruncatedAddressToIPRange(ip, subnet_length);
      packed = truncated.ToPackedString();
      ASSERT_TRUE(PackedStringToIPRange(packed, &unpacked));
      EXPECT_EQ(truncated, unpacked);

      // We expect the result from unpacking to be the original IPRange but
      // truncated.
      const IPRange original = IPRange(ip, subnet_length);
      packed = original.ToPackedString();
      ASSERT_TRUE(PackedStringToIPRange(packed, &unpacked));
      EXPECT_EQ(truncated, unpacked);
    }
  }
}

TEST(IPRangeTest, VerifyPackedStringFormat) {
  std::string ipranges[] = {"0.0.0.0/0", "::/0"};
  std::string expected_packed[] = {"\xc8", std::string("\x00", 1)};
  for (int i = 0; i < std::size(ipranges); ++i) {
    IPRange iprange = StringToIPRangeOrDie(ipranges[i]);
    std::string packed;
    IPRange unpacked;
    packed = iprange.ToPackedString();
    EXPECT_EQ(expected_packed[i], packed);
    ASSERT_TRUE(PackedStringToIPRange(packed, &unpacked));
    EXPECT_EQ(iprange, unpacked);
  }
}

TEST(IPRangeTest, AcceptsNull) {
  IPAddress kIpv6(
      StringToIPAddressOrDie("8888:9999:1234:abcd:cdef:efab:ab12:1012"));
  const IPRange original = TruncatedAddressToIPRange(kIpv6, 27);
  const std::string packed = original.ToPackedString();
  EXPECT_TRUE(PackedStringToIPRange(packed, nullptr));
  EXPECT_FALSE(PackedStringToIPRange(std::string(), nullptr));
}

TEST(IPRangeTest, FailsOnBadHeaderLengths) {
  IPAddress kIpv6(
      StringToIPAddressOrDie("1111:2222:3333:4444:5555:6666:7777:8888"));
  const IPRange original = TruncatedAddressToIPRange(kIpv6, 52);
  const std::string packed = original.ToPackedString();
  int bad_lengths[] = {129, 199, 233, 255, -1, 256, 1000};
  for (int i = 0; i < std::size(bad_lengths); ++i) {
    IPRange result;
    std::string bad_packed = static_cast<char>(bad_lengths[i]) + packed;
    EXPECT_FALSE(PackedStringToIPRange(bad_packed, &result));
  }
}

TEST(IPRangeTest, FailsOnBadStringLengths) {
  IPAddress kIpv6(
      StringToIPAddressOrDie("8888:9999:aaaa:bbbb:cccc:dddd:eeee:ffff"));
  const IPRange original = TruncatedAddressToIPRange(kIpv6, 52);
  std::string packed = original.ToPackedString();
  IPRange result;
  EXPECT_TRUE(PackedStringToIPRange(packed, &result));
  packed.push_back('x');
  EXPECT_FALSE(PackedStringToIPRange(packed, &result));
}

TEST(IPRangeTest, InvalidPackedStringConversion) {
  IPRange ip_range;
  // Invalid conversion.
  EXPECT_FALSE(PackedStringToIPRange("something very bad", &ip_range));
  // Valid conversion.
  const std::string packed = StringToIPRangeOrDie("1.0.0.0/8").ToPackedString();
  ASSERT_TRUE(PackedStringToIPRange(packed, &ip_range));
  EXPECT_EQ(ip_range.ToString(), "1.0.0.0/8");
}

TEST(IPRangeDeathTest, InvalidPackedStringConversion) {
  // Invalid conversion.
  EXPECT_DEATH(PackedStringToIPRangeOrDie("something very bad"),
               "Invalid packed IP range");
}

TEST(IPRangeTest, IPv6LinkLocal) {
  const IPAddress fe80_1 = StringToIPAddressOrDie("fe80::1");
  const IPAddress fe80_1_if17(MakeScopedIP(fe80_1, 17));

  const IPRange linklocal64(IPRange(fe80_1, 64));
  const IPRange linklocal64_if17(IPRange(fe80_1_if17, 64));

  EXPECT_NE(linklocal64, linklocal64_if17);
  EXPECT_EQ(StringToIPAddressWithOptionalScope("fe80::%17").value(),
            linklocal64_if17.network_address());
  EXPECT_EQ(StringToIPAddressWithOptionalScope("fe80::ffff:ffff:ffff:ffff%17")
                .value(),
            linklocal64_if17.broadcast_address());

  // Truncation beyond the boundary of what qualifies a prefix as being
  // scope_id-applicable doesn't really make sense. In order to prevent the
  // creation of some kind of ::%eth0/0 IPRange, truncation beyond scope_id
  // qualification discards the scope_id.
  //
  // IPv6 unicast link-local prefix is fe80::/10 (but this is [presently]
  // indistinguishable from fe80::/9).
  EXPECT_EQ(17, IPRange(fe80_1_if17, 10).host().scope_id());
  EXPECT_EQ(0, IPRange(fe80_1_if17, 8).host().scope_id());
  // IPv6 multicast link-local prefix is ff02::/16 (but this is [presently]
  // indistinguishable from ff02::/15).
  const IPAddress ff02_2_if17(
      StringToIPAddressWithOptionalScope("ff02::2%17").value());
  EXPECT_EQ(17, IPRange(ff02_2_if17, 16).host().scope_id());
  EXPECT_EQ(0, IPRange(ff02_2_if17, 14).host().scope_id());

  EXPECT_TRUE(linklocal64 < linklocal64_if17);
  EXPECT_TRUE(linklocal64_if17 < IPRange(ff02_2_if17, 16));

  // IsWithinSubnet follows the truncation logic above.
  EXPECT_FALSE(IsWithinSubnet(linklocal64, fe80_1_if17));
  EXPECT_FALSE(IsWithinSubnet(linklocal64_if17, fe80_1));
  EXPECT_TRUE(IsWithinSubnet(linklocal64_if17, fe80_1_if17));
  EXPECT_TRUE(IsWithinSubnet(IPRange(fe80_1_if17, 10), fe80_1_if17));
  EXPECT_FALSE(IsWithinSubnet(IPRange(fe80_1, 10), fe80_1_if17));
  EXPECT_TRUE(IsWithinSubnet(IPRange(fe80_1, 8), fe80_1_if17));
  EXPECT_TRUE(IsWithinSubnet(IPRange::Any6(), fe80_1_if17));

  // IsProperSubRange also follows the truncation logic above.
  EXPECT_FALSE(IsProperSubRange(linklocal64, IPRange(fe80_1_if17)));
  EXPECT_FALSE(IsProperSubRange(linklocal64_if17, IPRange(fe80_1)));
  EXPECT_TRUE(IsProperSubRange(linklocal64_if17, IPRange(fe80_1_if17)));
  EXPECT_TRUE(IsProperSubRange(IPRange(fe80_1_if17, 10), linklocal64_if17));
  EXPECT_FALSE(IsProperSubRange(IPRange(fe80_1, 10), linklocal64_if17));
  EXPECT_TRUE(IsProperSubRange(IPRange(fe80_1, 8), linklocal64_if17));
  EXPECT_TRUE(IsProperSubRange(IPRange::Any6(), linklocal64_if17));

  // TODO: +1, +N, SubtractIPRange
  // TODO: to/from string
  // TODO: hash/set test

  // Also test the absl::Hash version.
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(std::make_tuple(
      IPRange(fe80_1), IPRange(fe80_1_if17), linklocal64, linklocal64_if17,
      IPRange(ff02_2_if17), IPRange(ff02_2_if17, 14))));
}

TEST(IPAddressPlusNTest, AddZeroDoesNotChangeIPv4) {
  IPAddress addr = StringToIPAddressOrDie("10.1.1.150");
  IPAddress result;
  EXPECT_TRUE(IPAddressPlusN(addr, 0, &result));
  IPAddress expected_result = StringToIPAddressOrDie("10.1.1.150");
  EXPECT_EQ(expected_result, result);
}

TEST(IPAddressPlusNTest, AddOneToIPv4) {
  IPAddress addr = StringToIPAddressOrDie("10.1.1.150");
  IPAddress result;
  EXPECT_TRUE(IPAddressPlusN(addr, 1, &result));
  IPAddress expected_result = StringToIPAddressOrDie("10.1.1.151");
  EXPECT_EQ(expected_result, result);

  // Also test override cases.
  EXPECT_TRUE(IPAddressPlusN(addr, 1, &addr));
  EXPECT_EQ(expected_result, addr);
}

TEST(IPAddressPlusNTest, AddToIPv4CrossesLastOctetBoundary) {
  IPAddress addr = StringToIPAddressOrDie("10.1.1.150");
  IPAddress result;
  EXPECT_TRUE(IPAddressPlusN(addr, 150, &result));
  IPAddress expected_result = StringToIPAddressOrDie("10.1.2.44");
  EXPECT_EQ(expected_result, result);
}

TEST(IPAddressPlusNTest, SubtractFromIPv4) {
  IPAddress addr = StringToIPAddressOrDie("10.1.1.1");

  IPAddress result;
  EXPECT_TRUE(IPAddressPlusN(addr, -1, &result));
  IPAddress expected_result = StringToIPAddressOrDie("10.1.1.0");
  EXPECT_EQ(expected_result, result);

  EXPECT_TRUE(IPAddressPlusN(addr, -2, &result));
  expected_result = StringToIPAddressOrDie("10.1.0.255");
  EXPECT_EQ(expected_result, result);
}

TEST(IPAddressPlusNTest, AddToIPv6) {
  IPAddress addr = StringToIPAddressOrDie("8002:12::aab0");
  IPAddress result;
  EXPECT_TRUE(IPAddressPlusN(addr, 15, &result));
  IPAddress expected_result = StringToIPAddressOrDie("8002:12::aabf");
  EXPECT_EQ(expected_result, result);

  EXPECT_TRUE(IPAddressPlusN(addr, absl::MakeInt128(1, 15), &result));
  expected_result = StringToIPAddressOrDie("8002:12:0:1::aabf");
  EXPECT_EQ(expected_result, result);
}

TEST(IPAddressPlusNTest, SubtractFromIPv6) {
  IPAddress addr = StringToIPAddressOrDie("8002:12::aab0");
  IPAddress result;
  EXPECT_TRUE(IPAddressPlusN(addr, -0xaab1, &result));
  IPAddress expected_result =
      StringToIPAddressOrDie("8002:11:ffff:ffff:ffff:ffff:ffff:ffff");
  EXPECT_EQ(expected_result, result);

  EXPECT_TRUE(
      IPAddressPlusN(addr, -absl::MakeInt128(0x1200000000, 0xaab0), &result));
  expected_result = StringToIPAddressOrDie("8002::");
  EXPECT_EQ(expected_result, result);
}

TEST(IPAddressPlusNTest, AddCrossesIPv4SpaceBoundary) {
  IPAddress addr = StringToIPAddressOrDie("192.0.0.0");

  IPAddress result;
  EXPECT_TRUE(IPAddressPlusN(addr, 0x3fffffff, &result));
  IPAddress expected_result = StringToIPAddressOrDie("255.255.255.255");
  EXPECT_EQ(expected_result, result);

  EXPECT_FALSE(IPAddressPlusN(addr, 0x40000000, &result));
}

TEST(IPAddressPlusNTest, AddLargeIntegerCrossesIPv4SpaceBoundary) {
  IPAddress addr = StringToIPAddressOrDie("0.0.0.0");

  constexpr absl::int128 kOne = 1;
  IPAddress result;
  EXPECT_TRUE(IPAddressPlusN(addr, (kOne << 32) - 1, &result));
  IPAddress expected_result = StringToIPAddressOrDie("255.255.255.255");
  EXPECT_EQ(expected_result, result);

  EXPECT_FALSE(IPAddressPlusN(addr, kOne << 32, &result));
  EXPECT_FALSE(IPAddressPlusN(addr, kOne << 100, &result));
}

TEST(IPAddressPlusNTest, SubtractCrossesIPv4SpaceBoundary) {
  IPAddress addr = StringToIPAddressOrDie("4.0.0.0");

  IPAddress result;
  EXPECT_TRUE(IPAddressPlusN(addr, -0x4000000, &result));
  IPAddress expected_result = StringToIPAddressOrDie("0.0.0.0");
  EXPECT_EQ(expected_result, result);

  EXPECT_FALSE(IPAddressPlusN(addr, -0x4000001, &result));
}

TEST(IPAddressPlusNTest, SubtractLargeIntegerCrossesIPv4SpaceBoundary) {
  IPAddress addr = StringToIPAddressOrDie("255.255.255.255");

  constexpr absl::int128 kOne = 1;
  IPAddress result;
  EXPECT_TRUE(IPAddressPlusN(addr, -((kOne << 32) - 1), &result));
  IPAddress expected_result = StringToIPAddressOrDie("0.0.0.0");
  EXPECT_EQ(expected_result, result);

  EXPECT_FALSE(IPAddressPlusN(addr, -(kOne << 32), &result));
  EXPECT_FALSE(IPAddressPlusN(addr, -(kOne << 100), &result));
}

TEST(IPAddressPlusNTest, AddCrossesIPv6SpaceBoundary) {
  IPAddress addr = StringToIPAddressOrDie("ffff:ffff:ffff:ffff::");

  IPAddress result;
  EXPECT_TRUE(IPAddressPlusN(addr, 0xffffffffffffffff, &result));
  IPAddress expected_result =
      StringToIPAddressOrDie("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
  EXPECT_EQ(expected_result, result);

  constexpr uint64_t kMaxUint64 = 0xffffffffffffffff;
  EXPECT_TRUE(IPAddressPlusN(addr, kMaxUint64, &result));
  expected_result =
      StringToIPAddressOrDie("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
  EXPECT_EQ(expected_result, result);

  constexpr int64_t kNegOne = 0xffffffffffffffff;
  EXPECT_TRUE(IPAddressPlusN(addr, kNegOne, &result));
  expected_result =
      StringToIPAddressOrDie("ffff:ffff:ffff:fffe:ffff:ffff:ffff:ffff");
  EXPECT_EQ(expected_result, result);

  EXPECT_FALSE(IPAddressPlusN(addr, absl::MakeInt128(1, 0), &result));
}

TEST(IPAddressPlusNTest, SubtractCrossesIPv6SpaceBoundary) {
  IPAddress addr = StringToIPAddressOrDie("::ffff:ffff:ffff:ffff");

  IPAddress result;
  EXPECT_TRUE(IPAddressPlusN(addr, -0xffff, &result));
  IPAddress expected_result = StringToIPAddressOrDie("::ffff:ffff:ffff:0");
  EXPECT_EQ(expected_result, result);

  EXPECT_TRUE(
      IPAddressPlusN(addr, -absl::MakeInt128(0, 0xffffffffffffffff), &result));
  expected_result = StringToIPAddressOrDie("::");
  EXPECT_EQ(expected_result, result);

  EXPECT_FALSE(IPAddressPlusN(addr, -absl::MakeInt128(1, 0), &result));
}

TEST(IPAddressPlusNDeathTest, InvalidAddressFamily) {
  IPAddress uninit_addr, result_addr;
  bool result = true;
  EXPECT_DEBUG_DEATH(result = IPAddressPlusN(uninit_addr, 1, &result_addr),
                     "Invalid address family");
  if constexpr (!DEBUG_MODE) {
    EXPECT_FALSE(IPAddressPlusN(uninit_addr, 1, &result_addr));
  } else {
    EXPECT_TRUE(result);
  }
}

TEST(IPRangeTest, SubtractIPv4SubRange) {
  IPRange range, sub_range;
  ASSERT_TRUE(StringToIPRange("0.0.0.0/0", &range));
  ASSERT_TRUE(StringToIPRange("10.0.0.0/7", &sub_range));

  std::vector<IPRange> diff_range;
  EXPECT_TRUE(SubtractIPRange(range, sub_range, &diff_range));
  EXPECT_THAT(
      diff_range,
      ElementsAre(IpRangeEquals("8.0.0.0/7"), IpRangeEquals("12.0.0.0/6"),
                  IpRangeEquals("0.0.0.0/5"), IpRangeEquals("16.0.0.0/4"),
                  IpRangeEquals("32.0.0.0/3"), IpRangeEquals("64.0.0.0/2"),
                  IpRangeEquals("128.0.0.0/1")));
}

TEST(IPRangeTest, SubtractIPv4HalfSpace) {
  const IPRange range = StringToIPRangeOrDie("0.0.0.0/0");
  const IPRange sub_range = StringToIPRangeOrDie("0.0.0.0/1");

  std::vector<IPRange> diff_range;
  EXPECT_TRUE(SubtractIPRange(range, sub_range, &diff_range));
  EXPECT_THAT(diff_range, ElementsAre(IpRangeEquals("128.0.0.0/1")));
}

TEST(IPRangeTest, SubtractIPv6SubRange) {
  IPRange range, sub_range;
  ASSERT_TRUE(StringToIPRange("8002::/15", &range));
  ASSERT_TRUE(StringToIPRange("8003:aaa0::/28", &sub_range));

  std::vector<IPRange> diff_range;
  EXPECT_TRUE(SubtractIPRange(range, sub_range, &diff_range));
  EXPECT_THAT(
      diff_range,
      ElementsAre(
          IpRangeEquals("8003:aab0::/28"), IpRangeEquals("8003:aa80::/27"),
          IpRangeEquals("8003:aac0::/26"), IpRangeEquals("8003:aa00::/25"),
          IpRangeEquals("8003:ab00::/24"), IpRangeEquals("8003:a800::/23"),
          IpRangeEquals("8003:ac00::/22"), IpRangeEquals("8003:a000::/21"),
          IpRangeEquals("8003:b000::/20"), IpRangeEquals("8003:8000::/19"),
          IpRangeEquals("8003:c000::/18"), IpRangeEquals("8003::/17"),
          IpRangeEquals("8002::/16")));
}

TEST(IPRangeTest, SubtractIPv6MaxRange) {
  IPRange range, sub_range;
  ASSERT_TRUE(StringToIPRange("::0/0", &range));
  ASSERT_TRUE(StringToIPRange("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff/128",
                              &sub_range));

  std::vector<IPRange> diff_range;
  EXPECT_TRUE(SubtractIPRange(range, sub_range, &diff_range));
  ASSERT_EQ(diff_range.size(), 128);
  EXPECT_EQ(diff_range[0].ToString(),
            "ffff:ffff:ffff:ffff:ffff:ffff:ffff:fffe/128");
  EXPECT_EQ(diff_range[80].ToString(), "ffff:ffff:fffe::/48");
  EXPECT_EQ(diff_range[127].ToString(), "::/1");
}

TEST(IPRangeTest, SubtractNotSubRangeReturnsFalse) {
  IPRange range, sub_range;
  ASSERT_TRUE(StringToIPRange("10.0.0.0/7", &range));
  ASSERT_TRUE(StringToIPRange("12.1.0.0/16", &sub_range));

  std::vector<IPRange> diff_range;
  // Return false if not a sub-range.
  EXPECT_FALSE(SubtractIPRange(range, sub_range, &diff_range));
  EXPECT_THAT(diff_range, IsEmpty());
}

TEST(IPRangeTest, SubtractAddressFamilyMismatchReturnsFalse) {
  IPRange range, sub_range;
  ASSERT_TRUE(StringToIPRange("10.0.0.0/7", &range));
  ASSERT_TRUE(StringToIPRange("ab0::/16", &sub_range));

  std::vector<IPRange> diff_range;
  // Return false if not a sub-range.
  EXPECT_FALSE(SubtractIPRange(range, sub_range, &diff_range));
  EXPECT_THAT(diff_range, IsEmpty());
}

TEST(IPRangeTest, Ordering) {
  const std::string kIPString1 = "1.2.3.4";
  const std::string kIPString2 = "4.3.2.1";
  const std::string kIPString3 = "2001:db8::";
  const std::string kIPString4 = "3ffe::";

  IPAddress addr1, addr2, addr3, addr4;

  ASSERT_TRUE(StringToIPAddress(kIPString1, &addr1));
  ASSERT_TRUE(StringToIPAddress(kIPString2, &addr2));
  ASSERT_TRUE(StringToIPAddress(kIPString3, &addr3));
  ASSERT_TRUE(StringToIPAddress(kIPString4, &addr4));

  IPRange range0;
  IPRange range1_1(addr1, 8);
  IPRange range1_2(addr1, 16);
  IPRange range1_3(addr1, 24);
  IPRange range2_1(addr2, 8);
  IPRange range2_2(addr2, 16);
  IPRange range2_3(addr2, 24);
  IPRange range3(addr3, 32);
  IPRange range4(addr4, 16);

  // std::set
  EXPECT_THAT((std::set<IPRange>{
                  range4,
                  range3,
                  range3,
                  range2_3,
                  range2_2,
                  range2_1,
                  range2_1,
                  range0,
                  range1_3,
                  range1_2,
                  range1_1,
                  range1_1,
              }),
              ElementsAreArray({
                  range0,
                  range1_1,
                  range1_2,
                  range1_3,
                  range2_1,
                  range2_2,
                  range2_3,
                  range3,
                  range4,
              }));

  // Pairwise checks
  EXPECT_FALSE(range0 < range0);
  EXPECT_TRUE(range0 < range1_1);
  EXPECT_FALSE(range1_1 < range0);

  EXPECT_TRUE(range0 <= range0);
  EXPECT_TRUE(range0 <= range1_1);
  EXPECT_FALSE(range1_1 <= range0);

  EXPECT_TRUE(range1_1 >= range1_1);
  EXPECT_TRUE(range1_1 >= range0);
  EXPECT_FALSE(range0 >= range1_1);

  EXPECT_FALSE(range1_1 > range1_1);
  EXPECT_TRUE(range1_1 > range0);
  EXPECT_FALSE(range0 > range1_1);

#if __cplusplus >= 202002L
  EXPECT_EQ(std::weak_ordering::equivalent, range0 <=> range0);
  EXPECT_EQ(std::weak_ordering::equivalent, range1_1 <=> range1_1);
  EXPECT_EQ(std::weak_ordering::less, range0 <=> range1_1);
  EXPECT_EQ(std::weak_ordering::greater, range1_1 <=> range0);
#endif
}

TEST(IPRangeTest, Hash) {
  const std::string kIPString1 = "1.2.3.4";
  const std::string kIPString2 = "4.3.2.1";
  const std::string kIPString3 = "2001:db8::";
  const std::string kIPString4 = "3ffe::";

  IPAddress addr1, addr2, addr3, addr4;

  ASSERT_TRUE(StringToIPAddress(kIPString1, &addr1));
  ASSERT_TRUE(StringToIPAddress(kIPString2, &addr2));
  ASSERT_TRUE(StringToIPAddress(kIPString3, &addr3));
  ASSERT_TRUE(StringToIPAddress(kIPString4, &addr4));

  IPRange range0;
  IPRange range1_1(addr1, 8);
  IPRange range1_2(addr1, 16);
  IPRange range1_3(addr1, 24);
  IPRange range2_1(addr2, 8);
  IPRange range2_2(addr2, 16);
  IPRange range2_3(addr2, 24);
  IPRange range3(addr3, 32);
  IPRange range4(addr4, 16);

  absl::node_hash_set<IPRange> range_map;
  range_map.insert(range4);
  range_map.insert(range3);
  range_map.insert(range3);
  range_map.insert(range2_3);
  range_map.insert(range2_2);
  range_map.insert(range2_1);
  range_map.insert(range2_1);
  range_map.insert(range1_3);
  range_map.insert(range1_2);
  range_map.insert(range1_1);
  range_map.insert(range1_1);
  range_map.insert(range0);
  range_map.insert(IPRange());

  EXPECT_EQ(9, range_map.size());
  EXPECT_EQ(1, range_map.count(range0));
  EXPECT_EQ(1, range_map.count(range1_1));
  EXPECT_EQ(1, range_map.count(range1_2));
  EXPECT_EQ(1, range_map.count(range1_3));
  EXPECT_EQ(1, range_map.count(range2_1));
  EXPECT_EQ(1, range_map.count(range2_2));
  EXPECT_EQ(1, range_map.count(range2_3));
  EXPECT_EQ(1, range_map.count(range3));
  EXPECT_EQ(1, range_map.count(range4));

  // Also test the absl::Hash version.
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      std::make_tuple(range0, range1_1, range1_2, range1_3, range2_1, range2_2,
                      range2_3, range3, range4)));
}

TEST(IPRangeTest, IsInitializedRange) {
  IPRange uninit_range;
  EXPECT_FALSE(IsInitializedRange(uninit_range));

  IPAddress addr4;
  ASSERT_TRUE(StringToIPAddress("129.224.0.0", &addr4));
  IPRange invalid_range4(addr4, 10);
  EXPECT_TRUE(IsInitializedRange(invalid_range4));

  ASSERT_TRUE(StringToIPAddress("129.192.0.0", &addr4));
  IPRange valid_range4(addr4, 10);
  EXPECT_TRUE(IsInitializedRange(valid_range4));

  IPAddress addr6;
  ASSERT_TRUE(StringToIPAddress("8001:700:300::", &addr6));
  IPRange invalid_range6(addr6, 39);
  EXPECT_TRUE(IsInitializedRange(invalid_range6));

  IPRange valid_range6(addr6, 40);
  EXPECT_TRUE(IsInitializedRange(valid_range6));
}

TEST(IPRangeTest, UnsafeConstruct) {
  // Valid inputs.
  IPRange::UnsafeConstruct(IPAddress(), -1);
  IPRange::UnsafeConstruct(StringToIPAddressOrDie("192.0.2.0"), 24);
  IPRange::UnsafeConstruct(StringToIPAddressOrDie("2001:db8::"), 32);

  // Invalid inputs fail only in debug mode.
  EXPECT_DEBUG_DEATH(IPRange::UnsafeConstruct(IPAddress(), -2),
                     "Length is inconsistent with address family");
  EXPECT_DEBUG_DEATH(
      IPRange::UnsafeConstruct(StringToIPAddressOrDie("192.0.2.1"), 33),
      "Length is inconsistent with address family");
  EXPECT_DEBUG_DEATH(
      IPRange::UnsafeConstruct(StringToIPAddressOrDie("2001:db8::1"), 129),
      "Length is inconsistent with address family");
  EXPECT_DEBUG_DEATH(
      IPRange::UnsafeConstruct(StringToIPAddressOrDie("192.0.2.1"), 24),
      "Host has bits set beyond the prefix length");
  EXPECT_DEBUG_DEATH(
      IPRange::UnsafeConstruct(StringToIPAddressOrDie("2001:db8::1"), 32),
      "Host has bits set beyond the prefix length");
  EXPECT_DEBUG_DEATH(
      IPRange::UnsafeConstruct(StringToIPAddressOrDie("192.0.2.0"), -1),
      "Invalid truncation");
  EXPECT_DEBUG_DEATH(
      IPRange::UnsafeConstruct(StringToIPAddressOrDie("2001:db8::"), -1),
      "Invalid truncation");
}

TEST(IPRangeTest, IsValidRange) {
  IPRange uninit_range;
  EXPECT_FALSE(IsValidRange(uninit_range));

  IPAddress addr4;
  ASSERT_TRUE(StringToIPAddress("129.192.0.0", &addr4));
  IPRange valid_range4(addr4, 10);
  EXPECT_TRUE(IsValidRange(valid_range4));

  IPAddress addr6;
  ASSERT_TRUE(StringToIPAddress("8001:700:300::", &addr6));
  IPRange valid_range6(addr6, 40);
  EXPECT_TRUE(IsValidRange(valid_range6));

  // Paranoid case.  Production code shouldn't be doing this:
  if (!DEBUG_MODE) {
    EXPECT_FALSE(IsValidRange(
        IPRange::UnsafeConstruct(StringToIPAddressOrDie("129.224.0.0"), 10)));
    EXPECT_FALSE(IsValidRange(IPRange::UnsafeConstruct(
        StringToIPAddressOrDie("8001:700:300::"), 39)));
  }
}

TEST(IPRangeTest, IPAddressIntervalToSubnets_UninitializedIPAddresses) {
  IPAddress first_addr, last_addr;
  std::vector<IPRange> covering_subnets;
  EXPECT_FALSE(
      IPAddressIntervalToSubnets(first_addr, last_addr, &covering_subnets));
  EXPECT_THAT(covering_subnets, IsEmpty());
}

TEST(IPRangeTest, IPAddressIntervalToSubnets_AddressFamilyMismatch) {
  IPAddress first_addr = StringToIPAddressOrDie("4.1.0.1");
  IPAddress last_addr = StringToIPAddressOrDie("8001:700:300::11");
  std::vector<IPRange> covering_subnets;
  EXPECT_FALSE(
      IPAddressIntervalToSubnets(first_addr, last_addr, &covering_subnets));
  EXPECT_THAT(covering_subnets, IsEmpty());
}

TEST(IPRangeTest, IPAddressIntervalToSubnets_InvalidInterval) {
  IPAddress first_addr = StringToIPAddressOrDie("4.1.0.1");
  IPAddress last_addr = StringToIPAddressOrDie("4.1.0.0");
  std::vector<IPRange> covering_subnets;
  EXPECT_FALSE(
      IPAddressIntervalToSubnets(first_addr, last_addr, &covering_subnets));
  EXPECT_THAT(covering_subnets, IsEmpty());
}

TEST(IPRangeTest, IPAddressIntervalToSubnets_SingleAddressInterval) {
  IPAddress first_addr = StringToIPAddressOrDie("4.1.0.1");
  IPAddress last_addr = first_addr;
  std::vector<IPRange> covering_subnets;
  EXPECT_TRUE(
      IPAddressIntervalToSubnets(first_addr, last_addr, &covering_subnets));
  EXPECT_THAT(covering_subnets, ElementsAre(IPRange(first_addr)));
}

TEST(IPRangeTest, IPAddressIntervalToSubnets_MaxIPv4Interval) {
  IPAddress first_addr = StringToIPAddressOrDie("0.0.0.0");
  IPAddress last_addr = StringToIPAddressOrDie("255.255.255.255");
  std::vector<IPRange> covering_subnets;
  EXPECT_TRUE(
      IPAddressIntervalToSubnets(first_addr, last_addr, &covering_subnets));
  EXPECT_THAT(covering_subnets, ElementsAre(IpRangeEquals("0.0.0.0/0")));
}

TEST(IPRangeTest, IPAddressIntervalToSubnets_MaxIPv6Interval) {
  IPAddress first_addr = StringToIPAddressOrDie("::0");
  IPAddress last_addr =
      StringToIPAddressOrDie("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
  std::vector<IPRange> covering_subnets;
  EXPECT_TRUE(
      IPAddressIntervalToSubnets(first_addr, last_addr, &covering_subnets));
  EXPECT_THAT(covering_subnets, ElementsAre(StringToIPRangeOrDie("::0/0")));
}

TEST(IPRangeTest, IPAddressIntervalToSubnets_TestIPv4_Case1) {
  IPAddress first_addr = StringToIPAddressOrDie("255.255.254.0");
  IPAddress last_addr = StringToIPAddressOrDie("255.255.255.255");

  std::vector<IPRange> covering_subnets;
  EXPECT_TRUE(
      IPAddressIntervalToSubnets(first_addr, last_addr, &covering_subnets));
  EXPECT_THAT(covering_subnets, ElementsAre(IpRangeEquals("255.255.254.0/23")));
}

TEST(IPRangeTest, IPAddressIntervalToSubnets_TestIPv4_Case2) {
  IPAddress first_addr = StringToIPAddressOrDie("4.191.0.0");
  IPAddress last_addr = StringToIPAddressOrDie("6.1.0.255");

  std::vector<IPRange> expected_covering_subnets;
  expected_covering_subnets.push_back(StringToIPRangeOrDie("4.191.0.0/16"));
  expected_covering_subnets.push_back(StringToIPRangeOrDie("4.192.0.0/10"));
  expected_covering_subnets.push_back(StringToIPRangeOrDie("5.0.0.0/8"));
  expected_covering_subnets.push_back(StringToIPRangeOrDie("6.0.0.0/16"));
  expected_covering_subnets.push_back(StringToIPRangeOrDie("6.1.0.0/24"));

  std::vector<IPRange> covering_subnets;
  EXPECT_TRUE(
      IPAddressIntervalToSubnets(first_addr, last_addr, &covering_subnets));
  EXPECT_EQ(expected_covering_subnets, covering_subnets);
}

TEST(IPRangeTest, IPAddressIntervalToSubnets_TestIPv6) {
  IPAddress first_addr = StringToIPAddressOrDie("2001:db8::");
  IPAddress last_addr = StringToIPAddressOrDie("2001:2000::");

  std::vector<IPRange> expected_covering_subnets;
  expected_covering_subnets.push_back(StringToIPRangeOrDie("2001:db8::/29"));
  expected_covering_subnets.push_back(StringToIPRangeOrDie("2001:dc0::/26"));
  expected_covering_subnets.push_back(StringToIPRangeOrDie("2001:e00::/23"));
  expected_covering_subnets.push_back(StringToIPRangeOrDie("2001:1000::/20"));
  expected_covering_subnets.push_back(StringToIPRangeOrDie("2001:2000::/128"));

  std::vector<IPRange> covering_subnets;
  EXPECT_TRUE(
      IPAddressIntervalToSubnets(first_addr, last_addr, &covering_subnets));
  EXPECT_EQ(expected_covering_subnets, covering_subnets);
}

TEST(IPRangeTest, IsRangeIndexValid) {
  IPAddress base_addr4 = StringToIPAddressOrDie("1.2.3.4");
  for (int length = 1; length <= 32; ++length) {
    IPRange range(base_addr4, length);
    absl::uint128 size1((absl::uint128(1) << (32 - length)) - 1);
    EXPECT_TRUE(IsRangeIndexValid(range, size1))
        << "length=" << length << " size1=" << size1;
    absl::uint128 size2(absl::uint128(1) << (32 - length));
    EXPECT_FALSE(IsRangeIndexValid(range, size2))
        << "length=" << length << " size2=" << size2;
  }

  IPAddress base_addr6 = StringToIPAddressOrDie("2001:db8::");
  for (int length = 1; length < 128; ++length) {
    IPRange range(base_addr6, length);
    absl::uint128 size1((absl::uint128(1) << (128 - length)) - 1);
    EXPECT_TRUE(IsRangeIndexValid(range, size1))
        << "length=" << length << " size1=" << size1;
    absl::uint128 size2(absl::uint128(1) << (128 - length));
    EXPECT_FALSE(IsRangeIndexValid(range, size2))
        << "length=" << length << " size2=" << size2;
  }
  // 1 << 128 doesn't fit into a uint128, so use a different test
  // when length = 0.
  IPRange range(base_addr6, 0);
  EXPECT_TRUE(IsRangeIndexValid(range, absl::Uint128Max()));
}

TEST(IPRangeTest, NthAddressInRange) {
  const auto ip = [](const char* s) { return StringToIPAddressOrDie(s); };

  IPRange range = StringToIPRangeOrDie("1.2.3.4/32");
  EXPECT_EQ(ip("1.2.3.4"), NthAddressInRange(range, 0));

  EXPECT_DEBUG_DEATH(NthAddressInRange(range, 1),
                     "1.2.3.4/32 does not contain index 1");
  if (!DEBUG_MODE) {
    EXPECT_EQ(IPAddress(), NthAddressInRange(range, 1));
  }

  range = StringToIPRangeOrDie("1.2.3.0/24");
  EXPECT_EQ(ip("1.2.3.0"), NthAddressInRange(range, 0));
  EXPECT_EQ(ip("1.2.3.255"), NthAddressInRange(range, 255));

  range = StringToIPRangeOrDie("0.0.0.0/0");
  EXPECT_EQ(ip("0.0.255.255"), NthAddressInRange(range, 0xffff));
  EXPECT_EQ(ip("255.255.255.255"),
            NthAddressInRange(range, std::numeric_limits<uint32_t>::max()));

  range = StringToIPRangeOrDie("fedc:ba98:7654:3210:123:4567:89ab:cdef/128");
  EXPECT_EQ(ip("fedc:ba98:7654:3210:123:4567:89ab:cdef"),
            NthAddressInRange(range, 0));
  EXPECT_DEBUG_DEATH(NthAddressInRange(range, 1),
                     ":cdef/128 does not contain index 1");
  if (!DEBUG_MODE) {
    EXPECT_EQ(IPAddress(), NthAddressInRange(range, 1));
  }

  range = StringToIPRangeOrDie("fedc:ba98:7654:3210:123::/80");
  EXPECT_EQ(ip("fedc:ba98:7654:3210:123::f"), NthAddressInRange(range, 15));
  EXPECT_EQ(ip("fedc:ba98:7654:3210:123:0:ffff:ffff"),
            NthAddressInRange(range, std::numeric_limits<uint32_t>::max()));

  range = StringToIPRangeOrDie("::/0");
  EXPECT_EQ(ip("::0.1.0.0"), NthAddressInRange(range, 0x10000));
  EXPECT_EQ(ip("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"),
            NthAddressInRange(range, absl::Uint128Max()));
}

TEST(IPRangeTest, ChooseRandomIPAddress_FromIPRange) {
  const auto ip = [](const char* s) { return StringToIPAddressOrDie(s); };

  auto range = StringToIPRangeOrDie("1.2.3.4/32");
  auto random_ip = ChooseRandomIPAddress(range);
  EXPECT_EQ(ip("1.2.3.4"), random_ip) << random_ip;

  range = StringToIPRangeOrDie("1.2.3.0/24");
  random_ip = ChooseRandomIPAddress(range);
  EXPECT_TRUE(IsWithinSubnet(range, random_ip)) << random_ip;

  range = IPRange::Any4();
  random_ip = ChooseRandomIPAddress(range);
  EXPECT_TRUE(IsWithinSubnet(range, random_ip)) << random_ip;

  range = StringToIPRangeOrDie("fedc:ba98:7654:3210:123:4567:89ab:cdef/128");
  EXPECT_EQ(ip("fedc:ba98:7654:3210:123:4567:89ab:cdef"),
            ChooseRandomIPAddress(range));

  range = StringToIPRangeOrDie("fedc:ba98:7654:3210:123::/80");
  random_ip = ChooseRandomIPAddress(range);
  EXPECT_TRUE(IsWithinSubnet(range, random_ip)) << random_ip;

  range = StringToIPRangeOrDie("fedc:ba98:7654:3210::/64");
  random_ip = ChooseRandomIPAddress(range);
  EXPECT_TRUE(IsWithinSubnet(range, random_ip)) << random_ip;

  range = StringToIPRangeOrDie("fedc:ba98:7600::/40");
  random_ip = ChooseRandomIPAddress(range);
  EXPECT_TRUE(IsWithinSubnet(range, random_ip)) << random_ip;

  range = IPRange::Any6();
  random_ip = ChooseRandomIPAddress(range);
  EXPECT_TRUE(IsWithinSubnet(range, random_ip)) << random_ip;
}

TEST(IPAddress, IndexInRange) {
  EXPECT_EQ(0, IndexInRange(StringToIPRangeOrDie("1.1.1.0/24"),
                            StringToIPAddressOrDie("1.1.1.0")));
  EXPECT_EQ(200, IndexInRange(StringToIPRangeOrDie("1.1.1.0/24"),
                              StringToIPAddressOrDie("1.1.1.200")));
  EXPECT_EQ(266, IndexInRange(StringToIPRangeOrDie("192.1.192.0/22"),
                              StringToIPAddressOrDie("192.1.193.10")));
  EXPECT_EQ(8, IndexInRange(StringToIPRangeOrDie("1.1.1.240/28"),
                            StringToIPAddressOrDie("1.1.1.248")));
  EXPECT_EQ(1, IndexInRange(IPRange(StringToIPAddressOrDie("1.1.1.1"), 24),
                            StringToIPAddressOrDie("1.1.1.1")));

  EXPECT_EQ(
      128, IndexInRange(
               StringToIPRangeOrDie("2001:718:1001:700:200:5efe:c0a8:0300/120"),
               StringToIPAddressOrDie("2001:718:1001:700:200:5efe:c0a8:0380")));
  EXPECT_EQ(
      286326784,
      IndexInRange(
          StringToIPRangeOrDie("2001:718:1001:700:0000:0000:0000:0000/64"),
          StringToIPAddressOrDie("2001:718:1001:700:0000:0000:1111:0000")));
  EXPECT_EQ(
      16, IndexInRange(IPRange(StringToIPAddressOrDie("0:0:0:0:0:0:8:1"), 120),
                       StringToIPAddressOrDie("0:0:0:0:0:0:8:10")));

  EXPECT_DEBUG_DEATH(IndexInRange(StringToIPRangeOrDie("1.1.1.0/24"),
                                  StringToIPAddressOrDie("1.1.2.0")),
                     "is not within");
  EXPECT_DEBUG_DEATH(
      IndexInRange(
          StringToIPRangeOrDie("2001:718:1001:700:200:5efe:c0a8:0300/120"),
          StringToIPAddressOrDie("3001:718:1001:700:200:5efe:c0a8:0380")),
      "is not within");

  EXPECT_DEBUG_DEATH(
      IndexInRange(StringToIPRangeOrDie("0:0:0:0:0:0:c0a8:0/120"),
                   StringToIPAddressOrDie("192.168.0.10")),
      "is not within");
  EXPECT_DEBUG_DEATH(
      IndexInRange(StringToIPRangeOrDie("192.168.0.0/24"),
                   StringToIPAddressOrDie("0:0:0:0:0:0:c0a8:000a")),
      "is not within");

  if (!DEBUG_MODE) {
    EXPECT_EQ(absl::Uint128Max(),
              IndexInRange(StringToIPRangeOrDie("1.1.1.0/24"),
                           StringToIPAddressOrDie("1.1.2.0")));
    EXPECT_EQ(
        absl::Uint128Max(),
        IndexInRange(
            StringToIPRangeOrDie("2001:718:1001:700:200:5efe:c0a8:0300/120"),
            StringToIPAddressOrDie("3001:718:1001:700:200:5efe:c0a8:0380")));
  }
}

TEST(IPRangeTest, LoggingUninitialized) {
  EXPECT_EQ(absl::StrCat(IPRange()), "<uninitialized IPRange>");

  std::ostringstream out;
  out << IPRange();
  EXPECT_EQ("<uninitialized IPRange>", out.str());
}

TEST(IPRangeTest, IPRangeIsSingleIP) {
  IPRange single_ip4_range = StringToIPRangeOrDie("1.2.3.4");
  EXPECT_THAT(IsSingleIPRange(single_ip4_range), IsTrue());

  IPRange multiple_ip4_range = StringToIPRangeOrDie("1.2.3.0/24");
  EXPECT_THAT(IsSingleIPRange(multiple_ip4_range), IsFalse());

  IPRange single_ip6_range = StringToIPRangeOrDie("dead::beef");
  EXPECT_THAT(IsSingleIPRange(single_ip6_range), IsTrue());

  IPRange multiple_ip6_range = StringToIPRangeOrDie("1234:5678::abc0/127");
  EXPECT_THAT(IsSingleIPRange(multiple_ip6_range), IsFalse());

  IPRange uninitialized_range;
  EXPECT_THAT(IsSingleIPRange(uninitialized_range), IsFalse());
}

TEST(IPRangeDeathTest, MiscUninitialized) {
  EXPECT_EQ(IPAddress(), IPRange().host());
  EXPECT_DEBUG_DEATH(IPRange().network_address(), "Unknown address family");
  EXPECT_DEBUG_DEATH(IPRange().broadcast_address(), "Unknown address family");

  // This constructor is quite strange, but some callers use it.
  const IPRange bad_range(IPAddress(), 0);
  EXPECT_DEBUG_DEATH(bad_range.network_address(), "Unknown address family");
}

// Invalid conversion in *OrDie() functions.
TEST(IPRangeDeathTest, InvalidStringConversion) {
  // Invalid conversions.
  EXPECT_DEATH(StringToIPRangeOrDie("foo/10"), "Invalid IP range foo/10");
  EXPECT_DEATH(StringToIPRangeOrDie("128.59.16.20/16"), "Invalid IP range");
  EXPECT_DEATH(StringToIPRangeOrDie("::g/42"), "Invalid IP range ::g/42");
  EXPECT_DEATH(StringToIPRangeOrDie("2001:db8:1234::/32"),
               "Invalid IP range 2001:db8:1234::/32");

  EXPECT_DEATH(StringToIPRangeAndTruncateOrDie("foo/10"),
               "Invalid IP range foo/10");
  EXPECT_DEATH(StringToIPRangeAndTruncateOrDie("128.59.16.320/16"),
               "Invalid IP range 128.59.16.320/16");
  EXPECT_DEATH(StringToIPRangeAndTruncateOrDie("::g/42"),
               "Invalid IP range ::g/42");
  EXPECT_DEATH(StringToIPRangeAndTruncateOrDie("2001:db8:1234::/132"),
               "Invalid IP range 2001:db8:1234::/132");

  // Valid conversions.
  EXPECT_EQ(StringToIPRangeOrDie("192.168.253.0/24").ToString(),
            "192.168.253.0/24");
  EXPECT_EQ(StringToIPRangeOrDie("2001:db8:1234::/48").ToString(),
            "2001:db8:1234::/48");
  EXPECT_EQ(StringToIPRangeAndTruncateOrDie("1.2.3.4/16").ToString(),
            "1.2.0.0/16");
  EXPECT_EQ(StringToIPRangeAndTruncateOrDie("2001:db8:1234::/32").ToString(),
            "2001:db8::/32");
}

TEST(MaskLengthToIPAddress, InvalidConversions) {
  IPAddress result;
  EXPECT_FALSE(MaskLengthToIPAddress(AF_INET, -1, &result));
  EXPECT_FALSE(MaskLengthToIPAddress(AF_INET, 33, &result));
  EXPECT_FALSE(MaskLengthToIPAddress(AF_INET6, -1, &result));
  EXPECT_FALSE(MaskLengthToIPAddress(AF_INET6, 129, &result));
  EXPECT_FALSE(MaskLengthToIPAddress(AF_UNSPEC, 12, &result));
}

struct MaskLengthIPv4Case {
  int length;
  std::string expected;
};

class MaskLengthToIPAddressIPv4Test
    : public testing::TestWithParam<MaskLengthIPv4Case> {};

TEST_P(MaskLengthToIPAddressIPv4Test, ConvertsCorrectly) {
  const auto& param = GetParam();
  IPAddress mask;
  EXPECT_TRUE(MaskLengthToIPAddress(AF_INET, param.length, &mask));
  EXPECT_EQ(mask.ToString(), param.expected);
}

INSTANTIATE_TEST_SUITE_P(
    , MaskLengthToIPAddressIPv4Test,
    testing::Values(
        MaskLengthIPv4Case{32, "255.255.255.255"},
        MaskLengthIPv4Case{31, "255.255.255.254"},
        MaskLengthIPv4Case{30, "255.255.255.252"},
        MaskLengthIPv4Case{29, "255.255.255.248"},
        MaskLengthIPv4Case{28, "255.255.255.240"},
        MaskLengthIPv4Case{27, "255.255.255.224"},
        MaskLengthIPv4Case{26, "255.255.255.192"},
        MaskLengthIPv4Case{25, "255.255.255.128"},
        MaskLengthIPv4Case{24, "255.255.255.0"},
        MaskLengthIPv4Case{23, "255.255.254.0"},
        MaskLengthIPv4Case{22, "255.255.252.0"},
        MaskLengthIPv4Case{21, "255.255.248.0"},
        MaskLengthIPv4Case{20, "255.255.240.0"},
        MaskLengthIPv4Case{19, "255.255.224.0"},
        MaskLengthIPv4Case{18, "255.255.192.0"},
        MaskLengthIPv4Case{17, "255.255.128.0"},
        MaskLengthIPv4Case{16, "255.255.0.0"},
        MaskLengthIPv4Case{15, "255.254.0.0"},
        MaskLengthIPv4Case{14, "255.252.0.0"},
        MaskLengthIPv4Case{13, "255.248.0.0"},
        MaskLengthIPv4Case{12, "255.240.0.0"},
        MaskLengthIPv4Case{11, "255.224.0.0"},
        MaskLengthIPv4Case{10, "255.192.0.0"},
        MaskLengthIPv4Case{9, "255.128.0.0"},
        MaskLengthIPv4Case{8, "255.0.0.0"}, MaskLengthIPv4Case{7, "254.0.0.0"},
        MaskLengthIPv4Case{6, "252.0.0.0"}, MaskLengthIPv4Case{5, "248.0.0.0"},
        MaskLengthIPv4Case{4, "240.0.0.0"}, MaskLengthIPv4Case{3, "224.0.0.0"},
        MaskLengthIPv4Case{2, "192.0.0.0"}, MaskLengthIPv4Case{1, "128.0.0.0"},
        MaskLengthIPv4Case{0, "0.0.0.0"}));

struct MaskLengthIPv6Case {
  int length;
  std::string expected;
};

class MaskLengthToIPAddressIPv6Test
    : public testing::TestWithParam<MaskLengthIPv6Case> {};

TEST_P(MaskLengthToIPAddressIPv6Test, ConvertsCorrectly) {
  const auto& param = GetParam();
  IPAddress mask;
  EXPECT_TRUE(MaskLengthToIPAddress(AF_INET6, param.length, &mask));
  EXPECT_EQ(mask.ToString(), param.expected);
}

INSTANTIATE_TEST_SUITE_P(
    , MaskLengthToIPAddressIPv6Test,
    testing::Values(
        MaskLengthIPv6Case{0, "::"}, MaskLengthIPv6Case{1, "8000::"},
        MaskLengthIPv6Case{15, "fffe::"}, MaskLengthIPv6Case{31, "ffff:fffe::"},
        MaskLengthIPv6Case{47, "ffff:ffff:fffe::"},
        MaskLengthIPv6Case{59, "ffff:ffff:ffff:ffe0::"},
        MaskLengthIPv6Case{63, "ffff:ffff:ffff:fffe::"},
        MaskLengthIPv6Case{64, "ffff:ffff:ffff:ffff::"},
        MaskLengthIPv6Case{65, "ffff:ffff:ffff:ffff:8000::"},
        MaskLengthIPv6Case{79, "ffff:ffff:ffff:ffff:fffe::"},
        MaskLengthIPv6Case{95, "ffff:ffff:ffff:ffff:ffff:fffe::"},
        MaskLengthIPv6Case{111, "ffff:ffff:ffff:ffff:ffff:ffff:fffe:0"},
        MaskLengthIPv6Case{127, "ffff:ffff:ffff:ffff:ffff:ffff:ffff:fffe"},
        MaskLengthIPv6Case{128, "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"}));

TEST(NetMaskToMaskLength, Uninitialized) {
  IPAddress uninitialized;
  EXPECT_FALSE(NetMaskToMaskLength(uninitialized, nullptr));
}

class NetMaskToMaskLengthInvalidTest
    : public testing::TestWithParam<const char*> {};

TEST_P(NetMaskToMaskLengthInvalidTest, Fails) {
  EXPECT_FALSE(
      NetMaskToMaskLength(StringToIPAddressOrDie(GetParam()), nullptr));
}

INSTANTIATE_TEST_SUITE_P(
    , NetMaskToMaskLengthInvalidTest,
    testing::Values("127.0.0.0", "255.255.0.255", "255.254.255.255",
                    "255.0.0.1", "ffff:ffff:7fff::", "7fff:ffff:ffff::",
                    "ffff:ff7f:ffff::", "ffff:ffff:ffff:7fff::",
                    "ffff:ffff:ffff:ffff:ffff:ffff:ffff:fffd",
                    "ffff:ffff:ffff:ffff:ffff:ffff:fffd::",
                    "ffff:ffff:ffff:ffff:ffff:fffd::",
                    "ffff:ffff:ffff:ffff:fffd::", "ffff:ffff:ffff:fffd::"));

class NetMaskToMaskLengthIPv4Test : public testing::TestWithParam<int> {};

TEST_P(NetMaskToMaskLengthIPv4Test, RoundTrip) {
  const int length = GetParam();
  IPAddress mask;
  EXPECT_TRUE(MaskLengthToIPAddress(AF_INET, length, &mask));

  int result_length;
  EXPECT_TRUE(NetMaskToMaskLength(mask, &result_length));
  EXPECT_EQ(result_length, length);
}

INSTANTIATE_TEST_SUITE_P(, NetMaskToMaskLengthIPv4Test, testing::Range(0, 33));

class NetMaskToMaskLengthIPv6Test : public testing::TestWithParam<int> {};

TEST_P(NetMaskToMaskLengthIPv6Test, RoundTrip) {
  const int length = GetParam();
  IPAddress mask;
  EXPECT_TRUE(MaskLengthToIPAddress(AF_INET6, length, &mask));

  int result_length;
  EXPECT_TRUE(NetMaskToMaskLength(mask, &result_length));
  EXPECT_EQ(result_length, length);
}

INSTANTIATE_TEST_SUITE_P(, NetMaskToMaskLengthIPv6Test, testing::Range(0, 129));

TEST(AddressFamilyToString, BasicTest) {
  EXPECT_EQ(AddressFamilyToString(AF_INET), "IPv4");
  EXPECT_EQ(AddressFamilyToString(AF_INET6), "IPv6");
  EXPECT_EQ(AddressFamilyToString(AF_UNSPEC), "unspecified");
  EXPECT_EQ(AddressFamilyToString(-1), "unknown");
}

sockaddr_storage InitSockaddrStorage(int family) {
  sockaddr_storage s = {};
  s.ss_family = family;
  return s;
}

sockaddr_in InitSockaddrIn(int family) {
  sockaddr_in s = {};
  s.sin_family = family;
  return s;
}

sockaddr_in6 InitSockaddrIn6(int family) {
  sockaddr_in6 s = {};
  s.sin6_family = family;
  return s;
}

TEST(SockaddrCast, SimpleReads) {
  sockaddr_storage addr1 = InitSockaddrStorage(1);
  sockaddr_in addr2 = InitSockaddrIn(2);
  sockaddr_in6 addr3 = InitSockaddrIn6(3);
  const sockaddr_storage addr4 = InitSockaddrStorage(4);
  const sockaddr_in addr5 = InitSockaddrIn(5);
  const sockaddr_in6 addr6 = InitSockaddrIn6(6);

  EXPECT_EQ(1, sockaddr_cast(&addr1)->sa_family);
  EXPECT_EQ(2, sockaddr_cast(&addr2)->sa_family);
  EXPECT_EQ(3, sockaddr_cast(&addr3)->sa_family);
  EXPECT_EQ(4, sockaddr_cast(&addr4)->sa_family);
  EXPECT_EQ(5, sockaddr_cast(&addr5)->sa_family);
  EXPECT_EQ(6, sockaddr_cast(&addr6)->sa_family);
}

TEST(SockaddrCast, SimpleWrites) {
  sockaddr_storage addr1 = InitSockaddrStorage(1);
  sockaddr_in addr2 = InitSockaddrIn(2);
  sockaddr_in6 addr3 = InitSockaddrIn6(3);
  for (sockaddr* s :
       {sockaddr_cast(&addr1), sockaddr_cast(&addr2), sockaddr_cast(&addr3)}) {
    s->sa_family *= 10;
  }
  EXPECT_EQ(10, addr1.ss_family);
  EXPECT_EQ(20, addr2.sin_family);
  EXPECT_EQ(30, addr3.sin6_family);
}

TEST(ParseUnparseFlag, IPv4Address) {
  std::string err;
  IPAddress ip;
  EXPECT_TRUE(AbslParseFlag("1.2.3.4", &ip, &err));
  EXPECT_EQ(ip, StringToIPAddressOrDie("1.2.3.4"));
  EXPECT_THAT(err, IsEmpty());
  EXPECT_EQ("1.2.3.4", AbslUnparseFlag(ip));
}

TEST(ParseUnparseFlag, IPv6Address) {
  std::string err;
  IPAddress ip;
  EXPECT_TRUE(AbslParseFlag("1234:abcd::", &ip, &err));
  EXPECT_EQ(ip, StringToIPAddressOrDie("1234:abcd::"));
  EXPECT_THAT(err, IsEmpty());
  EXPECT_EQ("1234:abcd::", AbslUnparseFlag(ip));
}

TEST(ParseUnparseFlag, EmptyAddress) {
  std::string err;
  IPAddress ip;
  EXPECT_TRUE(AbslParseFlag("", &ip, &err));
  EXPECT_FALSE(IsInitializedAddress(ip));
  EXPECT_THAT(err, IsEmpty());
  EXPECT_EQ("", AbslUnparseFlag(ip));
}

TEST(ParseUnparseFlag, IllegalIPAddress) {
  std::string err;
  IPAddress ip;
  EXPECT_FALSE(AbslParseFlag("Illegal String", &ip, &err));
  EXPECT_FALSE(IsInitializedAddress(ip));
  // err could have some value in the future, so we do not check it now.
}

TEST(ParseUnparseFlag, IPv4Range) {
  std::string err;
  IPRange range;
  EXPECT_TRUE(AbslParseFlag("1.2.3.0/24", &range, &err));
  EXPECT_EQ(range, StringToIPRangeOrDie("1.2.3.0/24"));
  EXPECT_THAT(err, IsEmpty());
  EXPECT_EQ("1.2.3.0/24", AbslUnparseFlag(range));
}

TEST(ParseUnparseFlag, IPv6Range) {
  std::string err;
  IPRange range;
  EXPECT_TRUE(AbslParseFlag("1234:abcd::/32", &range, &err));
  EXPECT_EQ(range, StringToIPRangeOrDie("1234:abcd::/32"));
  EXPECT_THAT(err, IsEmpty());
  EXPECT_EQ("1234:abcd::/32", AbslUnparseFlag(range));
}

TEST(ParseUnparseFlag, EmptyRange) {
  std::string err;
  IPRange range;
  EXPECT_TRUE(AbslParseFlag("", &range, &err));
  EXPECT_FALSE(IsInitializedRange(range));
  EXPECT_THAT(err, IsEmpty());
  EXPECT_EQ("", AbslUnparseFlag(range));
}

TEST(ParseUnparseFlag, IllegalRange) {
  std::string err;
  IPRange range;
  EXPECT_FALSE(AbslParseFlag("Illegal String", &range, &err));
  EXPECT_FALSE(IsInitializedRange(range));
  // err could have some value in the future, so we do not check it now.
}

TEST(ParseUnparseFlag, IPv4SocketAddress) {
  std::string err;
  SocketAddress sa;
  EXPECT_TRUE(AbslParseFlag("1.2.3.4:5678", &sa, &err));
  EXPECT_EQ(sa, StringToSocketAddressOrDie("1.2.3.4:5678"));
  EXPECT_THAT(err, IsEmpty());
  EXPECT_EQ("1.2.3.4:5678", AbslUnparseFlag(sa));
}

TEST(ParseUnparseFlag, IPv6SocketAddress) {
  std::string err;
  SocketAddress sa;
  EXPECT_TRUE(AbslParseFlag("[1234:abcd::]:5678", &sa, &err));
  EXPECT_EQ(sa, StringToSocketAddressOrDie("[1234:abcd::]:5678"));
  EXPECT_THAT(err, IsEmpty());
  EXPECT_EQ("[1234:abcd::]:5678", AbslUnparseFlag(sa));
}

TEST(ParseUnparseFlag, IPv6LinkLocalSocketAddress) {
  std::string err;
  SocketAddress sa;
  EXPECT_TRUE(AbslParseFlag("[fe80::abcd%3]:5678", &sa, &err));
  EXPECT_THAT(StringToSocketAddressWithOptionalScope("[fe80::abcd%3]:5678"),
              IsOkAndHolds(sa));
  EXPECT_THAT(err, IsEmpty());
  EXPECT_EQ("[fe80::abcd%3]:5678", AbslUnparseFlag(sa));
}

TEST(ParseUnparseFlag, EmptySocketAddress) {
  std::string err;
  SocketAddress sa;
  EXPECT_TRUE(AbslParseFlag("", &sa, &err));
  EXPECT_FALSE(IsInitializedSocketAddress(sa));
  EXPECT_THAT(err, IsEmpty());
  EXPECT_EQ("", AbslUnparseFlag(sa));
}

TEST(ParseUnparseFlag, IllegalSocketAddress) {
  std::string err;
  SocketAddress sa;
  EXPECT_FALSE(AbslParseFlag("Illegal String", &sa, &err));
  EXPECT_FALSE(IsInitializedSocketAddress(sa));
  // err could have some value in the future, so we do not check it now.
}

TEST(ParseUnparseFlag, IPv4v6AddressList) {
  std::string err;
  IPAddressList value;
  EXPECT_TRUE(AbslParseFlag("1.2.3.4,56::78", &value, &err));
  EXPECT_EQ(value, IPAddressList({StringToIPAddressOrDie("1.2.3.4"),
                                  StringToIPAddressOrDie("56::78")}));
  EXPECT_EQ(AbslUnparseFlag(value), "1.2.3.4,56::78");
}

TEST(ParseUnparseFlag, EmptyAddressList) {
  std::string err;
  IPAddressList value;
  EXPECT_TRUE(AbslParseFlag("", &value, &err));
  EXPECT_EQ(value, IPAddressList());
  EXPECT_EQ(AbslUnparseFlag(value), "");
}

TEST(ParseUnparseFlag, AddressListWithEmptyString) {
  std::string err;
  IPAddressList value;
  EXPECT_TRUE(AbslParseFlag("11::11,2.2.2.2,3::3,", &value, &err));
  EXPECT_EQ(value,
            IPAddressList({StringToIPAddressOrDie("11::11"),
                           StringToIPAddressOrDie("2.2.2.2"),
                           StringToIPAddressOrDie("3::3"), IPAddress()}));
  EXPECT_EQ(AbslUnparseFlag(value), "11::11,2.2.2.2,3::3,");
}

TEST(ParseUnparseFlag, AddressListWithEmptyStringAndTrailingEmptyString) {
  std::string err;
  IPAddressList value;
  EXPECT_TRUE(AbslParseFlag("12::34,,", &value, &err));
  EXPECT_EQ(value, IPAddressList({StringToIPAddressOrDie("12::34"), IPAddress(),
                                  IPAddress()}));
  EXPECT_EQ(AbslUnparseFlag(value), "12::34,,");
}

TEST(ParseUnparseFlag, AddressListEmptyItemsOnly) {
  std::string err;
  IPAddressList value;
  EXPECT_TRUE(AbslParseFlag(",", &value, &err));
  EXPECT_EQ(value, IPAddressList({IPAddress(), IPAddress()}));
  EXPECT_EQ(AbslUnparseFlag(value), ",");
}

TEST(ParseUnparseFlag, IllegalAddressList) {
  std::string err;
  IPAddressList value;
  EXPECT_FALSE(AbslParseFlag("abc", &value, &err));
}

TEST(ParseUnparseFlag, AddressListWithInvalidString) {
  std::string err;
  IPAddressList value;
  EXPECT_FALSE(AbslParseFlag("5.6.7.8,def", &value, &err));
}

TEST(ParseUnparseFlag, AddressListWithSpaces) {
  std::string err;
  IPAddressList value;
  EXPECT_FALSE(AbslParseFlag("1.1.1.1, 2.2.2.2,3.3.3.3 ", &value, &err));
}

TEST(ParseUnparseFlag, UnparseUninitializedAddressOnly) {
  EXPECT_EQ(AbslUnparseFlag(IPAddressList({IPAddress()})), "");
}

////////////////////////////////////////////////////////////////////////
// Benchmarks
////////////////////////////////////////////////////////////////////////

// BENCHMARK is not supported in portable builds.
#if defined(BENCHMARK)
static void BM_IPAddressToCharBuf(benchmark::State& state) {
  const IPAddress addr = StringToIPAddressOrDie("210.100.123.67");
  char buf[INET6_ADDRSTRLEN];
  for (auto s : state) {
    addr.ToCharBuf(buf);
    benchmark::DoNotOptimize(buf);
  }
}
BENCHMARK(BM_IPAddressToCharBuf);

static void BM_IPAddressToString(benchmark::State& state) {
  const IPAddress addr = StringToIPAddressOrDie("210.100.123.67");
  for (auto s : state) {
    benchmark::DoNotOptimize(addr.ToString());
  }
}
BENCHMARK(BM_IPAddressToString);

static void BM_IPAddressToPackedString(benchmark::State& state) {
  const IPAddress addr = StringToIPAddressOrDie("210.100.123.67");
  for (auto s : state) {
    benchmark::DoNotOptimize(addr.ToPackedString());
  }
}
BENCHMARK(BM_IPAddressToPackedString);

static std::vector<IPAddress> GetIPv6sForBenchmark() {
  const std::vector<IPAddress> addrs = {
      StringToIPAddressOrDie("::"),
      StringToIPAddressOrDie("::1"),
      StringToIPAddressOrDie("1234:abcd::"),
      StringToIPAddressOrDie("::abcd"),
      StringToIPAddressOrDie("1234::abcd:0:0:5678"),
      StringToIPAddressOrDie("1234:0:0:abcd::5678"),
      StringToIPAddressOrDie("::192.168.90.1"),
      StringToIPAddressOrDie("::ffff:192.168.90.1"),
      StringToIPAddressOrDie("::ffff:127.0.0.1"),
      StringToIPAddressOrDie("1234:0:0:abcd::5678"),
      StringToIPAddressOrDie("1234:5678:0:9abc:def0:0:1234:5678"),
      StringToIPAddressOrDie("fd14:988a:50ee:10c:1:2:3:4"),
      StringToIPAddressOrDie("fd14:988a:50ee:1006:1:2:3:4")};
  return addrs;
}

static void BM_IPV6AddressToCharBuf(benchmark::State& state) {
  const std::vector<IPAddress> addrs = GetIPv6sForBenchmark();
  const size_t batch_size = addrs.size();
  char buf[INET6_ADDRSTRLEN];
  while (state.KeepRunningBatch(batch_size)) {
    for (const IPAddress& addr : addrs) {
      addr.ToCharBuf(buf);
      benchmark::DoNotOptimize(buf);
    }
  }
}
BENCHMARK(BM_IPV6AddressToCharBuf);

static void BM_IPV6AddressToString(benchmark::State& state) {
  const std::vector<IPAddress> addrs = GetIPv6sForBenchmark();
  const size_t batch_size = addrs.size();
  while (state.KeepRunningBatch(batch_size)) {
    for (const IPAddress& addr : addrs) {
      benchmark::DoNotOptimize(addr.ToString());
    }
  }
}
BENCHMARK(BM_IPV6AddressToString);

static void BM_IPV6AddressToPackedString(benchmark::State& state) {
  const std::vector<IPAddress> addrs = GetIPv6sForBenchmark();
  const size_t batch_size = addrs.size();
  while (state.KeepRunningBatch(batch_size)) {
    for (const IPAddress& addr : addrs) {
      benchmark::DoNotOptimize(addr.ToPackedString());
    }
  }
}
BENCHMARK(BM_IPV6AddressToPackedString);

static std::vector<IPAddress> GetIPsForBenchmark() {
  std::vector<IPAddress> addrs = GetIPv6sForBenchmark();
  addrs.push_back(StringToIPAddressOrDie("210.100.123.67"));
  return addrs;
}

static void BM_SocketAddressToString(benchmark::State& state) {
  const std::vector<IPAddress> addrs = GetIPsForBenchmark();
  const size_t batch_size = addrs.size();
  while (state.KeepRunningBatch(batch_size)) {
    for (const IPAddress& addr : addrs) {
      // Intentionally measures the cost of constructing a SocketAddress from an
      // IPAddress.
      const std::string s = SocketAddress(addr, 80).ToString();
      benchmark::DoNotOptimize(s);
    }
  }
}
BENCHMARK(BM_SocketAddressToString);

static void BM_IPAddressToStringWithPort(benchmark::State& state) {
  const std::vector<IPAddress> addrs = GetIPsForBenchmark();
  const size_t batch_size = addrs.size();
  while (state.KeepRunningBatch(batch_size)) {
    for (const IPAddress& addr : addrs) {
      // This is wrong, but lots of code in google3 does this. It serves only to
      // compare run time to the BM_SocketAddressToString benchmark.
      const std::string s = absl::StrCat(addr.ToString(), ":", 80);
      benchmark::DoNotOptimize(s);
    }
  }
}
BENCHMARK(BM_IPAddressToStringWithPort);

static void BM_IPAddressToStringHostPortString(benchmark::State& state) {
  const std::vector<IPAddress> addrs = GetIPsForBenchmark();
  const size_t batch_size = addrs.size();
  while (state.KeepRunningBatch(batch_size)) {
    for (const IPAddress& addr : addrs) {
      const std::string s = strings::HostPortString(addr.ToString(), 80);
      benchmark::DoNotOptimize(s);
    }
  }
}
BENCHMARK(BM_IPAddressToStringHostPortString);

static void BM_IsLoopbackIPAddress(benchmark::State& state) {
  const std::vector<IPAddress> addrs = GetIPsForBenchmark();
  const size_t batch_size = addrs.size();
  while (state.KeepRunningBatch(batch_size)) {
    for (const IPAddress& addr : addrs) {
      const bool is_local = IsLoopbackIPAddress(addr);
      benchmark::DoNotOptimize(is_local);
    }
  }
}
BENCHMARK(BM_IsLoopbackIPAddress);

void BM_IPRangeOrdering(benchmark::State& state) {
  absl::InsecureBitGen rng;
  std::vector<IPRange> in;
  for (int i = 0; i < 64; i++) {
    in.emplace_back(HostUInt32ToIPAddress(absl::Uniform<uint32_t>(rng)),
                    absl::Uniform(rng, 0, 31));
  }
  std::map<IPRange, uint32_t> ranges;
  for (auto s : state) {
    ranges.clear();
    for (const auto& range : in) {
      ranges[range] = 42;
    }
  }
}
BENCHMARK(BM_IPRangeOrdering);

void BM_IPRangeOrdering6(benchmark::State& state) {
  absl::InsecureBitGen rng;
  std::vector<IPRange> in;
  for (int i = 0; i < 64; i++) {
    in.emplace_back(
        UInt128ToIPAddress(absl::MakeUint128(absl::Uniform<uint64_t>(rng),
                                             absl::Uniform<uint64_t>(rng))),
        absl::Uniform(rng, 0, 96));
  }
  std::map<IPRange, uint32_t> ranges;
  for (auto s : state) {
    ranges.clear();
    for (const auto& range : in) {
      ranges[range] = 42;
    }
  }
}
BENCHMARK(BM_IPRangeOrdering6);

// Measures std::adjacent_find() in a vector testing operator==() for both
// 64-bit aligned and misaligned IPAddress.
void BM_IPAddressFind(benchmark::State& state) {
  const int ip_version = state.range(0);
  absl::InsecureBitGen rng;
  // If IPAddress doesn't have 64-bit alignment then if an array element happens
  // to be 64-bit aligned, its neighbors will not.
  std::vector<IPAddress> array;
  while (array.size() != 100) {
    IPAddress ip;
    if (ip_version == 4) {
      in_addr addr;
      uint8_t raw_bytes[sizeof(addr)];
      std::generate(std::begin(raw_bytes), std::end(raw_bytes),
                    [&rng]() { return absl::Uniform<uint8_t>(rng); });
      memcpy(&addr, raw_bytes, sizeof(addr));
      ip = IPAddress(addr);
    } else {
      in6_addr addr;
      uint8_t raw_bytes[sizeof(addr)];
      std::generate(std::begin(raw_bytes), std::end(raw_bytes),
                    [&rng]() { return absl::Uniform<uint8_t>(rng); });
      memcpy(&addr, raw_bytes, sizeof(addr));
      ip = IPAddress(addr);
    }
    if (array.empty() || ip != array.back()) {
      array.push_back(ip);
    }
  }
  CHECK(std::adjacent_find(array.cbegin(), array.cend()) == array.cend());
  for (auto _ : state) {
    benchmark::DoNotOptimize(array);
    benchmark::DoNotOptimize(std::adjacent_find(array.cbegin(), array.cend()));
  }
}
BENCHMARK(BM_IPAddressFind)->Arg(4)->Arg(6);

void BM_IPAddressEqual(benchmark::State& state) {
  int ip_version = state.range(0);
  IPAddress a =
      StringToIPAddressOrDie(ip_version == 4 ? "192.0.2.1" : "2001:db8:666::1");
  IPAddress b = a;
  for (auto _ : state) {
    benchmark::DoNotOptimize(a);
    benchmark::DoNotOptimize(b);
    benchmark::DoNotOptimize(a == b);
  }
}
BENCHMARK(BM_IPAddressEqual)->Arg(4)->Arg(6);

void BM_IPAddressGetIpv4(benchmark::State& state) {
  IPAddress a = StringToIPAddressOrDie("192.0.2.1");
  for (auto _ : state) {
    benchmark::DoNotOptimize(a.ipv4_address());
  }
}
BENCHMARK(BM_IPAddressGetIpv4);

void BM_IPAddressGetIpv6(benchmark::State& state) {
  IPAddress a = StringToIPAddressOrDie("2001:db8:666::1");
  for (auto _ : state) {
    benchmark::DoNotOptimize(a.ipv6_address());
  }
}
BENCHMARK(BM_IPAddressGetIpv6);

void BM_IPAddressFromBSDAddress(benchmark::State& state) {
  const in6_addr a = StringToIPAddressOrDie("2001:db8:666::1").ipv6_address();
  for (auto _ : state) {
    benchmark::DoNotOptimize(IPAddress(a));
  }
}
BENCHMARK(BM_IPAddressFromBSDAddress);

void BM_IPAddressAbslHash(benchmark::State& state) {
  const int ip_version = state.range(0);
  const IPAddress a =
      StringToIPAddressOrDie(ip_version == 4 ? "192.0.2.1" : "2001:db8:666::1");
  for (auto _ : state) {
    benchmark::DoNotOptimize(a.Hash());
  }
}
BENCHMARK(BM_IPAddressAbslHash)->Arg(4)->Arg(6);

void BM_IPAddressIsWithinSubnet(benchmark::State& state) {
  const int ip_version = state.range(0);
  const IPAddress needle =
      StringToIPAddressOrDie(ip_version == 4 ? "192.0.2.1" : "2001:db8:666::1");
  const IPRange haystack =
      StringToIPRangeOrDie(ip_version == 4 ? "192.0.0.0/16" : "2001:db8::/32");
  for (auto _ : state) {
    benchmark::DoNotOptimize(IsWithinSubnet(haystack, needle));
  }
}
BENCHMARK(BM_IPAddressIsWithinSubnet)->Arg(4)->Arg(6);

void BM_StringToIPAddress(benchmark::State& state) {
  const int ip_version = state.range(0);
  const std::string address = ip_version == 4 ? "192.0.2.1" : "2001:db8:666::1";
  IPAddress ip;
  for (auto _ : state) {
    benchmark::DoNotOptimize(StringToIPAddress(address, &ip));
  }
}
BENCHMARK(BM_StringToIPAddress)->Arg(4)->Arg(6);

void BM_StringToIPAddress_CharPtr(benchmark::State& state) {
  const int ip_version = state.range(0);
  const char* const address = ip_version == 4 ? "192.0.2.1" : "2001:db8:666::1";
  IPAddress ip;
  for (auto _ : state) {
    benchmark::DoNotOptimize(StringToIPAddress(address, &ip));
  }
}
BENCHMARK(BM_StringToIPAddress_CharPtr)->Arg(4)->Arg(6);

void BM_StringToIPAddress_StringView(benchmark::State& state) {
  const int ip_version = state.range(0);
  const absl::string_view address =
      ip_version == 4 ? "192.0.2.1" : "2001:db8:666::1";
  IPAddress ip;
  for (auto _ : state) {
    benchmark::DoNotOptimize(StringToIPAddress(address, &ip));
  }
}
BENCHMARK(BM_StringToIPAddress_StringView)->Arg(4)->Arg(6);

void BM_IPRangeConstruction(benchmark::State& state) {
  const int ip_version = state.range(0);
  const IPAddress address =
      StringToIPAddressOrDie(ip_version == 4 ? "192.0.2.1" : "2001:db8:666::1");
  for (auto _ : state) {
    benchmark::DoNotOptimize(IPRange(address, 8));
  }
}
BENCHMARK(BM_IPRangeConstruction)->Arg(4)->Arg(6);

// Original, unoptimized implementation of `IsAnyIPAddress`.  If this becomes
// as fast as `IsAnyIPAddress`, this can be moved back to `IsAnyIPAddress`.
bool REFERENCE_IsAnyIPAddress(const IPAddress& ip) {
  switch (ip.address_family()) {
    case AF_INET:
      return ip == IPAddress::Any4();
    case AF_INET6:
      return ip == IPAddress::Any6();
    case AF_UNSPEC:
      LOG(DFATAL) << "Calling IsAnyIPAddress() on an empty IPAddress";
      break;
    default:
      LOG(DFATAL) << "Calling IsAnyIPAddress() on an IPAddress "
                  << "with unknown address family " << ip.address_family();
  }
  return false;
}

void BM_REFERENCE_IsAnyIPAddress(benchmark::State& state) {
  const int ip_version = state.range(0);
  const bool use_any = state.range(1);
  const IPAddress address = StringToIPAddressOrDie(
      use_any ? (ip_version == 4 ? "0.0.0.0" : "::")
              : (ip_version == 4 ? "192.0.2.1" : "2001:db8:666::1"));
  for (auto _ : state) {
    benchmark::DoNotOptimize(REFERENCE_IsAnyIPAddress(address));
  }
}
BENCHMARK(BM_REFERENCE_IsAnyIPAddress)
    ->ArgPair(4, false)
    ->ArgPair(4, true)
    ->ArgPair(6, false)
    ->ArgPair(6, true);

void BM_IsAnyIPAddress(benchmark::State& state) {
  const int ip_version = state.range(0);
  const bool use_any = state.range(1);
  const IPAddress address = StringToIPAddressOrDie(
      use_any ? (ip_version == 4 ? "0.0.0.0" : "::")
              : (ip_version == 4 ? "192.0.2.1" : "2001:db8:666::1"));
  for (auto _ : state) {
    benchmark::DoNotOptimize(IsAnyIPAddress(address));
  }
}
BENCHMARK(BM_IsAnyIPAddress)
    ->ArgPair(4, false)
    ->ArgPair(4, true)
    ->ArgPair(6, false)
    ->ArgPair(6, true);

#endif  // defined(BENCHMARK)

}  // namespace
}  // namespace net_base
