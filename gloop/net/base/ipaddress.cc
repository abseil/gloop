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

// Definition of classes IPAddress, SocketAddress, and IPRange.

#include "gloop/net/base/ipaddress.h"

#include <stdio.h>  // for snprintf

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/log_severity.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/base/port.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/numeric/int128.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/compare.h"
#include "absl/types/span.h"
#include "gloop/base/port.h"
#include "gloop/strings/host_port.h"
#include "gloop/strings/numbers.h"
#include "gloop/strings/util.h"
#include "gloop/util/bits/bits.h"
#include "gloop/util/endian/endian.h"
#include "gloop/util/gtl/comparator.h"
#include "gloop/util/hash/builtin_type_hash.h"
#include "gloop/util/hash/hash.h"
#include "gloop/util/status/errno_mapping.h"
#include "gloop/util/status/status_macros.h"
#include "gloop/util/tuple/components/dump_vars.h"

#ifdef _WIN32

#include <winsock2.h>

// winsock2.h must come before windows.h.

#include <windows.h>
#include <ws2def.h>
#include <ws2ipdef.h>

// windows.h must come before iphlapi.h, as it declares some type aliases that
// are used by that header. ws2def.h and ws2ipdef.h must also come before
// iphlapi.h, as iphlapi.h only defines some symbols if they are included.

#include <iphlpapi.h>

#define s6_addr16 u.Word

#else  // !defined(_WIN32)

#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#endif  // !defined(_WIN32)

namespace net_base {

// Sanity check: be sure INET_ADDRSTRLEN fits into INET6_ADDRSTRLEN.
// ToCharBuf() (below) depends on this.
static_assert(INET_ADDRSTRLEN <= INET6_ADDRSTRLEN, "ipv6_larger_than_ipv4");

namespace {

const int kMaxNetmaskIPv4 = 32;
const int kMaxNetmaskIPv6 = 128;

IPAddress MakeIPAddressWithOptionalScopeId(absl::uint128 ip6uint,
                                           uint32_t scope_id) {
  const IPAddress ip6(UInt128ToIPAddress(ip6uint));
  const auto rval = MakeIPAddressWithScopeId(ip6.ipv6_address(), scope_id);
  return rval.ok() ? rval.value() : ip6;
}

}  // namespace

IPAddress IPAddress::Any4() { return HostUInt32ToIPAddress(INADDR_ANY); }

IPAddress IPAddress::Loopback4() {
  return HostUInt32ToIPAddress(INADDR_LOOPBACK);
}

IPAddress IPAddress::Any6() { return IPAddress(in6addr_any); }

IPAddress IPAddress::Loopback6() { return IPAddress(in6addr_loopback); }

in_addr IPAddress::ipv4_address_slowpath() const {
  // If we're here then we know this is not an actual IPv4 address.
  DCHECK(!is_ipv4());

  // Otherwise we always crash due to calling ipv4_address on a non-IPv4
  // address.
  LOG(FATAL) << "Cannot call ipv4_address on this object: "
             << DUMP_VARS(address_family());
}

in6_addr IPAddress::ipv6_address_slowpath() const {
  // See the comment in ipv4_address_slowpath() for rationale for this test.
#ifdef NDEBUG
  if (is_ipv4()) {
    // If you see this message in production, it's likely to be due to a
    // programming error (you should never call ipv6_address() on an
    // IPAddress object containing an IPv4 address).
    LOG(ERROR) << "Trying to call ipv6_address() on the IPv4 address "
               << this->ToString()
               << " -- returning IPv6 mapped address as a last measure.";

    in6_addr addr6 = {};
    addr6.s6_addr16[5] = htons(0xffff);
    UNALIGNED_STORE32(addr6.s6_addr16 + 6, ipv4_address().s_addr);

    return addr6;
  }
#endif

  // Otherwise we always crash due to calling ipv6_address on a non-IPv6
  // address.
  CHECK(is_ipv6()) << "Cannot call ipv6_address on this object";

  // If we made it all the way here, this must be an IPv6 address with a compact
  // scope ID. In this case we must clear that out of our result.
  in6_addr copy = address_.get_ipv6();

  DCHECK(address_.has_scope());
  copy.s6_addr16[2] = 0;  // clear the scope_id (interface index)
  copy.s6_addr16[3] = 0;

  return copy;
}

IPAddress HostUInt32ToIPAddress(uint32_t address) {
  in_addr addr;
  addr.s_addr = htonl(address);
  return IPAddress(addr);
}

IPAddress UInt128ToIPAddress(const absl::uint128 bigint) {
  in6_addr addr6;
  BigEndian::Store64(addr6.s6_addr16, absl::Uint128High64(bigint));
  BigEndian::Store64(addr6.s6_addr16 + 4, absl::Uint128Low64(bigint));
  return IPAddress(addr6);
}

bool IsAnyIPAddress(const IPAddress& ip) {
  switch (ip.address_.type()) {
    case IPAddress::Variant::Type::kIpv4:
      return ip.address_.get_ipv4().s_addr == 0;

    case IPAddress::Variant::Type::kIpv6: {
      if (ip.address_.has_scope()) return false;
      // Note that we need our own static variable so it can be optimized
      // away. Using `in6addr_any` won't work.
      static constexpr in6_addr kAnyIPv6 = IN6ADDR_ANY_INIT;

      return std::equal(ip.address_.get_ipv6().s6_addr16,
                        std::end(ip.address_.get_ipv6().s6_addr16),
                        kAnyIPv6.s6_addr16);
    }

    case IPAddress::Variant::Type::kUninitialized:
      LOG(DFATAL) << "Calling IsAnyIPAddress() on an empty IPAddress";
      return false;
  }

  ABSL_UNREACHABLE();
}

bool IsV4MulticastIPAddress(const IPAddress& ip) {
  if (!ip.is_ipv4()) {
    return false;
  }
  // Check if the address falls within the multicast range (224.0.0.0 -
  // 239.255.255.255)
  return (ip.ipv4_address().s_addr & htonl(0xf0000000)) == htonl(0xe0000000);
}

namespace {

enum class LoopbackMode {
  // Just 127.0.0.1 and ::1
  DO_NOT_INCLUDE_ENTIRE_LOOPBACK_NETWORK,
  // 127.0.0.0/8, ::1, and old and new Google ULA /64's for loopback testing
  INCLUDE_ENTIRE_LOOPBACK_NETWORK,
};

bool IsLoopbackIPAddress(const IPAddress& ip, const LoopbackMode mode) {
  static const in6_addr ula_ipv6_loopback_range =
      // <link>=RFC1918_GOOGLE.fd14%3A988a%3A50ee%3A1006%3A%3A%2F64
      StringToIPAddressOrDie("fd14:988a:50ee:1006::").ipv6_address();
  switch (ip.address_family()) {
    case AF_INET:
      if (mode == LoopbackMode::DO_NOT_INCLUDE_ENTIRE_LOOPBACK_NETWORK) {
        return ip == IPAddress::Loopback4();
      } else {
        return (IPAddressToHostUInt32(ip) & 0xff000000U) == 0x7f000000U;
      }
    case AF_INET6:
      if (ip == IPAddress::Loopback6()) {
        return true;
      }
      {
        const in6_addr ipv6_ip = ip.ipv6_address();
        // Compare the first 64 bits of the in6_addr with the appropriate
        // address ranges.
        if (mode != LoopbackMode::DO_NOT_INCLUDE_ENTIRE_LOOPBACK_NETWORK &&
            UNALIGNED_LOAD64(ipv6_ip.s6_addr16) ==
                UNALIGNED_LOAD64(ula_ipv6_loopback_range.s6_addr16)) {
          return true;
        }
      }
      {
        // Handles IPv4 mapped IPV6 addresses as loopback address.
        // ie. ::ffff:127.0.0.1 should be considered loopback address.
        IPAddress addr4;
        return GetMappedIPv4Address(ip, &addr4) &&
               IsLoopbackIPAddress(addr4, mode);
      }
    case AF_UNSPEC:
      LOG(DFATAL) << "Calling IsLoopbackIPAddress() on an empty IPAddress";
      break;
    default:
      LOG(DFATAL) << "Calling IsLoopbackIPAddress() on an IPAddress "
                  << "with unknown address family " << ip.address_family();
  }
  return false;
}

char* FastOctetToBuffer(int octet, char* buf) {
  if (octet >= 100) {
    // If it's >= 100, we write the first two digits and then
    // let the single-digit case below handle the rest.
    PutTwoDigits(octet / 10, buf);
    octet %= 10;
    buf += 2;
  } else if (octet >= 10) {
    // If it's from [10,99] we write the two digits and return.
    PutTwoDigits(octet, buf);
    return buf + 2;
  }
  // If it's < 10 (or if we fell through after the >= 100 case) we
  // write the single digit.
  *buf++ = '0' + octet;
  return buf;
}

char* IPv4ToCharBuf(const uint8_t* octets, char* buffer) {
  buffer = FastOctetToBuffer(octets[0], buffer);
  *buffer++ = '.';
  buffer = FastOctetToBuffer(octets[1], buffer);
  *buffer++ = '.';
  buffer = FastOctetToBuffer(octets[2], buffer);
  *buffer++ = '.';
  buffer = FastOctetToBuffer(octets[3], buffer);
  *buffer = '\0';
  return buffer;
}

char* FastWordToBuffer(uint16_t word, char* buffer) {
  static constexpr char hexdigits[] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                       '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
  if (word >> 12) {
    *buffer++ = hexdigits[word >> 12];
  }
  if (word >> 8) {
    *buffer++ = hexdigits[(word >> 8) & 0xf];
  }
  if (word >> 4) {
    *buffer++ = hexdigits[(word >> 4) & 0xf];
  }
  *buffer++ = hexdigits[word & 0xf];
  return buffer;
}

// Returns start of the longest sequence of zero words of length at least 2.
// Returns -1 if no such sequence exists. Returns first such sequence in the
// event of multiple such sequences.
int FindLongestZeroWordSequence(const uint16_t* addr) {
  int cnt = 0;
  int best_len = 1;
  int best_start = -1;
  for (int i = 0; i < 8; ++i) {
    if (addr[i] == 0) {
      ++cnt;
      if (cnt > best_len) {
        best_len = cnt;
        best_start = i + 1 - cnt;
      }
    } else {
      cnt = 0;
    }
  }
  return best_start;
}

char* IPv6ToCharBuf(const in6_addr& addr, char* buffer) {
  if (UNALIGNED_LOAD64(addr.s6_addr16) == 0) {
    // If lower half of address is zero, it starts with :: and it may be
    // embedded IPv4 address.
    *buffer++ = ':';
    // Check for IPv6 embedded IPv4 address.
    if (addr.s6_addr16[4] == 0 &&
        (addr.s6_addr16[5] == 0xffff ||
         (addr.s6_addr16[5] == 0 && addr.s6_addr16[6] != 0))) {
      if (addr.s6_addr16[5] != 0) {
        *buffer++ = ':';
        buffer = FastWordToBuffer(0xffff, buffer);
      }
      *buffer++ = ':';
      return IPv4ToCharBuf(&addr.s6_addr[12], buffer);
    }
    int i = 4;
    // Skip remaining zero words.
    while (i < 8 && addr.s6_addr16[i] == 0) {
      ++i;
    }
    if (i < 8) {
      for (; i < 8; ++i) {
        *buffer++ = ':';
        buffer = FastWordToBuffer(gntohs(addr.s6_addr16[i]), buffer);
      }
    } else {
      *buffer++ = ':';
    }
  } else {
    const int start = FindLongestZeroWordSequence(addr.s6_addr16);
    for (int i = 0; i < 8; ++i) {
      if (i == start) {
        // At least two words are guaranteed to be zero.
        i += 2;
        while (i < 8 && addr.s6_addr16[i] == 0) {
          ++i;
        }
        *buffer++ = ':';
        if (i == 8) {
          *buffer++ = ':';
          break;
        }
      }
      if (i) {
        *buffer++ = ':';
      }
      buffer = FastWordToBuffer(gntohs(addr.s6_addr16[i]), buffer);
    }
  }
  *buffer = '\0';
  return buffer;
}

}  // namespace

bool IsCanonicalLoopbackIPAddress(const IPAddress& ip) {
  return IsLoopbackIPAddress(
      ip, LoopbackMode::DO_NOT_INCLUDE_ENTIRE_LOOPBACK_NETWORK);
}

bool IsLoopbackIPAddress(const IPAddress& ip) {
  return IsLoopbackIPAddress(ip, LoopbackMode::INCLUDE_ENTIRE_LOOPBACK_NETWORK);
}

// <buffer> must have room for at least INET6_ADDRSTRLEN bytes,
// including the final NUL.
char* IPAddress::ToCharBuf(char* const buffer) const {
  switch (address_.type()) {
    case IPAddress::Variant::Type::kIpv4: {
      const in_addr& a = address_.get_ipv4();
      return IPv4ToCharBuf(reinterpret_cast<const uint8_t*>(&a.s_addr), buffer);
    }

    case IPAddress::Variant::Type::kIpv6: {
      return IPv6ToCharBuf(ipv6_address(), buffer);
    }

    case IPAddress::Variant::Type::kUninitialized:
      LOG(DFATAL) << "Calling ToCharBuf() on an empty IPAddress";
      *buffer = '\0';
      return buffer;
  }

  ABSL_UNREACHABLE();
}

std::string IPAddress::ToString() const {
  char buf[INET6_ADDRSTRLEN];
  return std::string(buf, ToCharBuf(buf) - buf);
}

std::string IPAddress::ToPackedString() const {
  switch (address_.type()) {
    case IPAddress::Variant::Type::kIpv4: {
      const in_addr& a = address_.get_ipv4();
      return std::string{
          reinterpret_cast<const char*>(&a),
          sizeof(a),
      };
    }

    case IPAddress::Variant::Type::kIpv6: {
      if (ABSL_PREDICT_FALSE(address_.has_scope())) {
        // Calling ToPackedString() on an IPv6 link-local address is somewhat
        // suspect. When later de-serialized, even on the same machine, there is
        // no inherent guarantee that a given interface index remains valid. For
        // now, output them the same way as their un-scoped cousins -- what to
        // do with the interface index and/or name is likely to be an
        // application-dependent matter.
        VLOG(2) << "ToPackedString() dropping scope ID";
        const auto addr6 = ipv6_address();
        return std::string{
            reinterpret_cast<const char*>(&addr6),
            sizeof(addr6),
        };
      }
      const in6_addr& a = address_.get_ipv6();
      return std::string{
          reinterpret_cast<const char*>(&a),
          sizeof(a),
      };
    }

    case IPAddress::Variant::Type::kUninitialized:
      LOG(DFATAL) << "Calling ToPackedString() on an empty IPAddress";
      return "";
  }

  ABSL_UNREACHABLE();
}

absl::StatusOr<IPAddress> MakeIPAddressWithScopeId(const in6_addr& addr,
                                                   uint32_t scope_id) {
  if (scope_id == 0) return IPAddress(addr);

  if (!IPAddress::MayUseScopeIds(addr)) {
    return absl::InvalidArgumentError("address does not use scope_ids");
  } else if (!IPAddress::MayUseCompactScopeIds(addr)) {
    return absl::InvalidArgumentError("address cannot use compact scope_ids");
  } else if (!IPAddress::MayStoreCompactScopeId(addr)) {
    return absl::InvalidArgumentError("address cannot safely compact scope_id");
  }

  return IPAddress(addr, scope_id);
}

absl::StatusOr<IPAddress> MakeIPAddressWithScopeId(const in6_addr& addr,
                                                   absl::string_view ifname) {
  char interface_name[IF_NAMESIZE]{};
  ifname.copy(interface_name, IF_NAMESIZE - 1);
  const unsigned int ifindex = if_nametoindex(interface_name);
  return (ifindex != 0) ? MakeIPAddressWithScopeId(addr, ifindex)
                        : absl::InvalidArgumentError(
                              absl::StrCat("bad interface: ", ifname));
}

static absl::StatusOr<IPAddress> GetIPAddressFromSockaddrIn6(
    const sockaddr_in6& sin6) {
  // Validate the address family.
  //
  // This field is redundant when we know we're looking at a sockaddr_in6; it
  // exists for use in telling us whether we can cast a sockaddr to a
  // sockaddr_in6 in the first place.
  if (sin6.sin6_family != AF_INET6) {
    return util::InvalidArgumentErrorBuilder()
           << "invalid address family: " << DUMP_VARS(sin6.sin6_family);
  }

  if (ABSL_PREDICT_TRUE(sin6.sin6_scope_id == 0)) {
    return IPAddress(sin6.sin6_addr);
  }

  return MakeIPAddressWithScopeId(sin6.sin6_addr, sin6.sin6_scope_id);
}

absl::StatusOr<SocketAddress> MakeSocketAddressFromSockaddrIn6(
    const sockaddr_in6& sin6) {
  ABSL_ASSIGN_OR_RETURN(const IPAddress host, GetIPAddressFromSockaddrIn6(sin6),
                        _ << "getting IP address");

  return SocketAddress{
      host,
      gntohs(sin6.sin6_port),
  };
}

namespace {

// Parses a decimal dotted quad IPv4 address.  This is the same format
// supported by `inet_pton(AF_INET, ...)`.  Returns true on success.
// Contents of `*addr` are unspecified on failure.
bool StringToInAddr(absl::string_view ip, in_addr* absl_nonnull addr) {
  // Whether a digit has been seen in the current field.  This is reset to
  // `false` when we encounter a dot.
  bool saw_digit = false;
  // Number of octets for which parsing has completed.
  int num_octets = 0;
  // Current octet value.  Must be larger than uint8_t to detect octets
  // that are out of range.
  uint32_t octet_val = 0;
  // IPv4 bytes in network order.
  uint8_t ip_bytes[4];

  for (const char ch : ip) {
    if (ch >= '0' && ch <= '9') {
      // Initial 0s are not allowed.
      if (ABSL_PREDICT_FALSE(saw_digit && octet_val == 0)) return false;

      octet_val = 10 * octet_val + (ch - '0');
      saw_digit = true;

      if (ABSL_PREDICT_FALSE(octet_val > 255)) return false;
    } else if (ch == '.' && saw_digit) {
      // Too many dots.
      if (ABSL_PREDICT_FALSE(num_octets == 3)) return false;
      ip_bytes[num_octets] = octet_val;
      ++num_octets;
      octet_val = 0;
      saw_digit = false;
    } else {
      // Bad char or '.' without preceding digit.
      return false;
    }
  }

  // The last octet should be "in progress".
  if (ABSL_PREDICT_FALSE(num_octets < 3)) return false;
  if (ABSL_PREDICT_FALSE(!saw_digit)) return false;
  // Store final octet.  This is usually stored with '.' handling, but there is
  // no trailing '.'.
  ip_bytes[3] = octet_val;
  memcpy(&addr->s_addr, &ip_bytes, sizeof(ip_bytes));
  return true;
}

// Parses a single hex digit.  Returns its numeric value, or -1 on failure.
int HexDigitValue(char ch) {
  if ('0' <= ch && ch <= '9') return ch - '0';
  if ('a' <= ch && ch <= 'f') return ch - 'a' + 10;
  if ('A' <= ch && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

// Parses an IPv6 address in RFC 4291 format.  This is the same format
// supported by `inet_pton(AF_INET6, ...)`.  Returns true on success.
// Contents of `*addr` are unspecified on failure.
bool StringToIn6Addr(absl::string_view ip, in6_addr* absl_nonnull addr) {
  // Shortest valid IPv6 address is "::".
  if (ABSL_PREDICT_FALSE(ip.length() < 2)) return false;

  auto it = ip.begin();
  const auto end = ip.end();

  if (*it == ':') {
    // Leading colon must be followed by another one.  In order to simplify
    // the loop below, we process the first colon here *without setting
    // `double_colon_field_index`.  This way, in the loop below, we can
    // assume that a colon with `num_nibbles == 0` means a double colon.
    ++it;
    if (ABSL_PREDICT_FALSE(it == end || *it != ':')) return false;
  }

  // The index of the current colon-separated fields we are parsing.
  int field_index = 0;
  // Value of the current colon-separated field.
  uint_fast16_t field_val = 0;
  // Number of nibbles parsed in current colon-separated field.
  int num_nibbles = 0;

  // We need to remember the beginning of the current colon-separated field
  // so we can re-start parsing an IPv4 address if a dot is encountered.
  auto field_begin = ip.begin();
  // If non-negative, the field index of "::".  This is needed so we can
  // insert zeros after we're done parsing the string.
  int double_colon_field_index = -1;
  uint8_t ip_bytes[16];

  for (; it != end; ++it) {
    const char ch = *it;
    if (const int nibble_val = HexDigitValue(ch); nibble_val >= 0) {
      if (ABSL_PREDICT_FALSE(num_nibbles == 4)) return false;
      field_val <<= 4;
      field_val |= nibble_val;
      ++num_nibbles;
    } else if (ch == ':') {
      // Remember the next position in case we need to restart with IPv4
      // parsing.
      field_begin = it + 1;

      if (num_nibbles == 0) {
        // Double colon.  Multiple double colons is an error.
        if (ABSL_PREDICT_FALSE(double_colon_field_index >= 0)) return false;
        double_colon_field_index = field_index;
      } else {
        // Single trailing colon is not allowed.
        if (ABSL_PREDICT_FALSE(field_begin == end)) return false;
        // Colon after all fields are done is not allowed.
        if (ABSL_PREDICT_FALSE(field_index == 7)) return false;
        // Write completed field.
        ip_bytes[2 * field_index] = (field_val >> 8) & 0xFF;
        ip_bytes[2 * field_index + 1] = field_val & 0xFF;
        ++field_index;
        field_val = 0;
        num_nibbles = 0;
      }
    } else if (in_addr addr4;
               ch == '.' && field_index <= 6 &&
               // TODO: Switch to `string_view(field_begin, end)`
               // when we can depend on C++20.
               StringToInAddr(
                   absl::string_view(&*field_begin, end - field_begin),
                   &addr4)) {
      // `ip_bytes[2 * field_index]` is not 4-byte aligned, so we can't pass
      // that directly to `StringToInAddr`.  Parsing into a temporary
      // `in_addr` and copying is ~3.5% faster than having an having a
      // version of `StringToInAddr` that deals with unaligned output.
      // `field_index <= 6` ensures there are >= 4 bytes available.
      static_assert(sizeof(addr4) == 4);
      memcpy(&ip_bytes[2 * field_index], &addr4, sizeof(addr4));
      field_index += 2;
      num_nibbles = 0;
      break;
    } else {
      return false;
    }
  }

  // Store the current field, if any.
  if (num_nibbles > 0) {
    DCHECK_LE(field_index, 7);
    ip_bytes[2 * field_index] = (field_val >> 8) & 0xFF;
    ip_bytes[2 * field_index + 1] = field_val & 0xFF;
    ++field_index;
  }

  // Handle double colon.
  if (double_colon_field_index >= 0) {
    const int num_fields_skipped = 8 - field_index;
    const int num_fields_to_move = field_index - double_colon_field_index;
    // Skipping 0 fields not allowed.
    if (ABSL_PREDICT_FALSE(num_fields_skipped == 0)) return false;
    memmove(&ip_bytes[2 * (8 - num_fields_to_move)],
            &ip_bytes[2 * double_colon_field_index], 2 * num_fields_to_move);
    memset(&ip_bytes[2 * double_colon_field_index], 0, 2 * num_fields_skipped);
    field_index = 8;
  }

  if (ABSL_PREDICT_FALSE(field_index != 8)) return false;
  memcpy(&addr->s6_addr, &ip_bytes, sizeof(ip_bytes));
  return true;
}

}  // namespace

bool StringToIPAddress(absl::string_view str, IPAddress* out) {
  // These calls may happen in either order.  It takes 1-4 chars to
  // identify an IPv6 address passed to StringToInAddr, and 2-4 chars to
  // identify an IPv4 address passed to StringToIn6Addr.
  // TODO: Determine the frequency of v4 vs v6 calls and possibly
  // swap order if that will be faster.
  in_addr addr4;
  if (StringToInAddr(str, &addr4)) {
    if (out) {
      *out = IPAddress(addr4);
    }
    return true;
  }

  in6_addr addr6;
  if (StringToIn6Addr(str, &addr6)) {
    if (out) {
      *out = IPAddress(addr6);
    }
    return true;
  }

  return false;
}

namespace {

// Maps error values from getaddrinfo(3) to canonical Status codes.
absl::Status InternalGetaddrinfoErrorToStatus(int rval, int copied_errno) {
  if (rval == 0) return absl::OkStatus();

  const char* error_str = gai_strerror(rval);
  // Note that getaddrinfo is only guaranteed to set errno when the return value
  // is EAI_SYSTEM.  Otherwise, errno is unreliable.
  //
  // Plausible error values are from
  // https://tools.ietf.org/html/rfc3493#section-6.1
  switch (rval) {
    case EAI_AGAIN:
      return absl::UnavailableError(absl::StrCat("EAI_AGAIN: ", error_str));
    case EAI_BADFLAGS:
      return absl::InvalidArgumentError(
          absl::StrCat("EAI_BADFLAGS: ", error_str));
    case EAI_FAIL:
      return absl::NotFoundError(absl::StrCat("EAI_FAIL: ", error_str));
    case EAI_FAMILY:
      return absl::InvalidArgumentError(
          absl::StrCat("EAI_FAMILY: ", error_str));
    case EAI_MEMORY:
      return absl::ResourceExhaustedError(
          absl::StrCat("EAI_MEMORY: ", error_str));
    case EAI_NONAME:
      return absl::NotFoundError(absl::StrCat("EAI_NONAME: ", error_str));
    case EAI_SERVICE:
      return absl::InvalidArgumentError(
          absl::StrCat("EAI_SERVICE: ", error_str));
    case EAI_SOCKTYPE:
      return absl::InvalidArgumentError(
          absl::StrCat("EAI_SOCKTYPE: ", error_str));
#ifdef EAI_SYSTEM  // not defined on Windows
    case EAI_SYSTEM:
      return util::ErrnoToCanonicalStatus(copied_errno,
                                          "getaddrinfo EAI_SYSTEM");
#endif
    default:
      return absl::UnknownError(
          absl::StrCat("getaddrinfo returned ", rval, " (", error_str, ")"));
  }
}

}  // namespace

absl::StatusOr<IPAddress> StringToIPAddressWithOptionalScope(
    const absl::string_view str) {
  const auto scope_delimiter = str.rfind('%');
  if (scope_delimiter == absl::string_view::npos) {
    IPAddress ip{};
    if (StringToIPAddress(str, &ip)) {
      return ip;
    } else {
      return absl::InvalidArgumentError("bad IP string literal");
    }
  }

  // Addresses with a scope delimiter ('%') but without a following zone_id
  // does not seem to comport with any of this text:
  //
  //     https://tools.ietf.org/html/rfc4007#section-11.2
  //     https://tools.ietf.org/html/rfc4007#section-11.6
  //     https://tools.ietf.org/html/rfc6874#section-2
  //
  // However, it seems at least one getaddrinfo() implementation accepts this
  // syntax. Until further review of text and use cases comes to a different
  // conclusion, check for this case and return an error.
  if (str.substr(scope_delimiter).size() == 1) {  // EndsWith('%')
    return absl::InvalidArgumentError("missing zone_id");
  }

  const std::string str_null_terminated(str);

  // Trust getaddrinfo()'s ability to parse scope_ids and interface names.
  struct addrinfo hints{};
  hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
  hints.ai_family = AF_INET6;
  // Hint that getaddrinfo() need not return a linked list of answers.
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;

  struct addrinfo* res{nullptr};
  const int rval =
      getaddrinfo(str_null_terminated.c_str(), nullptr, &hints, &res);
  std::unique_ptr<struct addrinfo, decltype(&freeaddrinfo)> cleanup(
      res, freeaddrinfo);
  if (rval != 0) {
    return InternalGetaddrinfoErrorToStatus(rval, errno);
  }
  if (res == nullptr || res->ai_addr == nullptr ||
      res->ai_addrlen < sizeof(struct sockaddr_in6)) {
    return absl::InternalError("getaddrinfo returned nonsensical response");
  }
  const auto* sin6 = reinterpret_cast<sockaddr_in6*>(res->ai_addr);
  return MakeIPAddressWithScopeId(sin6->sin6_addr, sin6->sin6_scope_id);
}

bool PackedStringToIPAddress(absl::string_view str, IPAddress* out) {
  if (str.length() == sizeof(in_addr)) {
    if (out) {
      in_addr addr;
      memcpy(&addr, str.data(), sizeof(addr));
      *out = IPAddress(addr);
    }
    return true;
  } else if (str.length() == sizeof(in6_addr)) {
    if (out) {
      in6_addr addr;
      memcpy(&addr, str.data(), sizeof(addr));
      *out = IPAddress(addr);
    }
    return true;
  }

  return false;
}

bool PackedStringToSocketAddress(absl::string_view str, SocketAddress* out) {
  IPAddress ip;
  uint16_t network_order_port;

  if (str.size() < sizeof(network_order_port)) {
    return false;
  }

  absl::string_view ip_piece = str;
  ip_piece.remove_suffix(sizeof(network_order_port));
  if (!PackedStringToIPAddress(ip_piece, &ip)) {
    return false;
  }

  absl::string_view port_piece = str;
  port_piece.remove_prefix(ip_piece.size());
  network_order_port = UNALIGNED_LOAD16(port_piece.data());
  const uint16_t port = ntohs(network_order_port);
  if (out) {
    *out = SocketAddress(ip, port);
  }
  return true;
}

namespace {

// Parse a 64-bit number, using exactly 16 hex digits.
bool InternalParseHexUInt64(absl::string_view hex_str, uint64_t* out) {
  const int kExpectedLength = 16;
  if (hex_str.length() != kExpectedLength) {
    return false;
  }
  for (int ix = 0; ix < kExpectedLength; ix++) {
    if (!absl::ascii_isxdigit(hex_str[ix])) {
      return false;
    }
  }
  return absl::SimpleHexAtoi(hex_str, out);
}

}  // namespace

bool ColonlessHexToIPv6Address(absl::string_view hex_str, IPAddress* ip6) {
  // Parse a hex string like "fe80000000000000000573fffea00065" into a
  // valid IPv6 address, if possible.
  uint64_t hi;
  uint64_t lo;
  if (InternalParseHexUInt64(hex_str.substr(0, 16), &hi) &&
      InternalParseHexUInt64(absl::ClippedSubstr(hex_str, 16), &lo)) {
    if (ip6) {
      *ip6 = UInt128ToIPAddress(absl::MakeUint128(hi, lo));
    }
    return true;
  }
  return false;
}

std::string IPAddressToStringWithScopeId(const IPAddress& ip) {
  std::string ip_literal(ip.ToString());
  const uint32_t scope_id = ip.scope_id();
  if (scope_id != 0) {
    absl::StrAppend(&ip_literal, "%", scope_id);
  }
  return ip_literal;
}

std::string IPAddressToStringWithInterfaceName(const IPAddress& ip) {
  std::string ip_literal(ip.ToString());
  const uint32_t scope_id = ip.scope_id();
  if (scope_id != 0) {
    char ifname[IF_NAMESIZE]{'\0'};
    if (if_indextoname(scope_id, ifname) == ifname) {
      absl::StrAppend(&ip_literal, "%", ifname);
    } else {
      absl::StrAppend(&ip_literal, "%", scope_id);
    }
  }
  return ip_literal;
}

std::string IPAddressToURIString(const IPAddress& ip) {
  switch (ip.address_family()) {
    case AF_INET6:
      return absl::StrCat("[", ip.ToString(), "]");
    default:
      return ip.ToString();
  }
}

std::string IPAddressToPTRString(const IPAddress& ip) {
  char
      ptr_name[sizeof("0.1.2.3.4.5.6.7.8.9.a.b.c.d.e.f."
                      "0.1.2.3.4.5.6.7.8.9.a.b.c.d.e.f.ip6.arpa")];
  memset(ptr_name, 0, sizeof(ptr_name));

  switch (ip.address_family()) {
    case AF_INET: {
      int a1, a2, a3, a4;
      const uint32_t addr = IPAddressToHostUInt32(ip);
      a1 = static_cast<int>((addr >> 24) & 0xff);
      a2 = static_cast<int>((addr >> 16) & 0xff);
      a3 = static_cast<int>((addr >> 8) & 0xff);
      a4 = static_cast<int>(addr & 0xff);
      snprintf(ptr_name, sizeof(ptr_name), "%d.%d.%d.%d.in-addr.arpa", a4, a3,
               a2, a1);
      return ptr_name;
    }
    case AF_INET6: {
      const struct in6_addr addr = ip.ipv6_address();
      const unsigned char* bytes =
          reinterpret_cast<const unsigned char*>(addr.s6_addr);
      snprintf(ptr_name, sizeof(ptr_name),
               "%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x."
               "%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.%x.ip6.arpa",
               bytes[15] & 0xf, bytes[15] >> 4, bytes[14] & 0xf, bytes[14] >> 4,
               bytes[13] & 0xf, bytes[13] >> 4, bytes[12] & 0xf, bytes[12] >> 4,
               bytes[11] & 0xf, bytes[11] >> 4, bytes[10] & 0xf, bytes[10] >> 4,
               bytes[9] & 0xf, bytes[9] >> 4, bytes[8] & 0xf, bytes[8] >> 4,
               bytes[7] & 0xf, bytes[7] >> 4, bytes[6] & 0xf, bytes[6] >> 4,
               bytes[5] & 0xf, bytes[5] >> 4, bytes[4] & 0xf, bytes[4] >> 4,
               bytes[3] & 0xf, bytes[3] >> 4, bytes[2] & 0xf, bytes[2] >> 4,
               bytes[1] & 0xf, bytes[1] >> 4, bytes[0] & 0xf, bytes[0] >> 4);
      return ptr_name;
    }
    case AF_UNSPEC:
      LOG(DFATAL) << "Calling IPAddressToPTRString() on an empty IPAddress";
      return "unspecified.arpa";
    default:
      LOG(FATAL) << "Unknown address family " << ip.address_family();
  }
}

namespace {

bool InternalParseIPv4PtrString(absl::string_view host, IPAddress* out) {
  // Treat the input as an IPv4 address with reversed octets.
  in_addr addr;
  if (StringToInAddr(host, &addr)) {
    addr.s_addr = bswap_32(addr.s_addr);
    if (out != nullptr) *out = IPAddress(addr);
    return true;
  }
  return false;
}

bool InternalParseIPv6PtrString(absl::string_view host, IPAddress* out) {
  static constexpr int kIP6HexDigits = 32;
  static constexpr int kIP6WithDotsLength = (kIP6HexDigits * 2) - 1;
  if (host.length() != kIP6WithDotsLength) return false;

  std::string reversed_without_dots;
  reversed_without_dots.reserve(kIP6HexDigits);
  for (int i = kIP6WithDotsLength; i > 0; i -= 2) {
    DCHECK_EQ(i % 2, 1);
    if (i != kIP6WithDotsLength && host[i] != '.') return false;

    reversed_without_dots.push_back(host[i - 1]);
  }
  return ColonlessHexToIPv6Address(reversed_without_dots, out);
}

}  // anonymous namespace

bool PTRStringToIPAddress(absl::string_view ptr_address, IPAddress* out) {
  absl::ConsumeSuffix(&ptr_address, ".");  // optional trailing dot
  static constexpr absl::string_view kIPv4Suffix = ".in-addr.arpa";
  static constexpr absl::string_view kIPv6Suffix = ".ip6.arpa";
  if (absl::ConsumeSuffix(&ptr_address, kIPv4Suffix)) {
    return InternalParseIPv4PtrString(ptr_address, out);
  } else if (absl::ConsumeSuffix(&ptr_address, kIPv6Suffix)) {
    return InternalParseIPv6PtrString(ptr_address, out);
  }
  return false;
}

// Return a random IP from the choices in hp. Assumes that hp->h_addrtype is
// AF_INET or AF_INET6. (Adapted from net/base/dnscache.cc.)
IPAddress ChooseRandomAddress(const struct hostent* hp) {
  int num;
  for (num = 0; hp->h_addr_list[num] != nullptr; num++) {
  }
  if (num <= 0) {
    LOG(DFATAL) << "Cannot choose from an empty list";
    return IPAddress();
  }

  int index = 0;
  if (num > 1) {
    absl::InsecureBitGen bitgen;
    index = absl::Uniform(bitgen, 0, num);
    DCHECK_GE(index, 0);
    DCHECK_LT(index, num);
  }

  switch (hp->h_addrtype) {
    case AF_INET:
      return IPAddress(*(reinterpret_cast<in_addr*>(hp->h_addr_list[index])));
    case AF_INET6:
      return IPAddress(*(reinterpret_cast<in6_addr*>(hp->h_addr_list[index])));
    default:
      LOG(DFATAL) << "Unknown address family " << hp->h_addrtype;
      return IPAddress();
  }
}

// Return a random IPAddress from a span of same.
IPAddress ChooseRandomIPAddress(absl::Span<const IPAddress> ips) {
  if (ips.empty()) {
    LOG(DFATAL) << "Cannot choose from an empty list";
    return IPAddress();
  }

  int index = 0;
  if (ips.size() > 1) {
    absl::InsecureBitGen bitgen;
    index = absl::Uniform(bitgen, 0u, ips.size());
    CHECK_GE(index, 0);
    CHECK_LT(index, static_cast<int64_t>(ips.size()));
  }

  return ips[index];
}

IPAddress ChooseRandomIPAddress(const IPRange& range) {
  if (!IsInitializedRange(range)) {
    LOG(DFATAL) << "Cannot choose from uninitialized range";
    return IPAddress();
  }
  absl::InsecureBitGen bitgen;
  if (range.host().address_family() == AF_INET) {
    if (range.length() == 0) {
      return NthAddressInRange(range, absl::Uniform<uint32_t>(bitgen));
    }
    return NthAddressInRange(
        range,
        absl::Uniform<uint32_t>(bitgen, 0U, 1U << (32 - range.length())));
  }

  DCHECK(range.host().address_family() == AF_INET6);

  // TODO: Use Uniform<absl::uint128> when it works.
  if (range.length() == 0) {
    return NthAddressInRange(
        range, absl::MakeUint128(absl::Uniform<uint64_t>(bitgen),
                                 absl::Uniform<uint64_t>(bitgen)));
  }
  if (range.length() < 64) {
    return NthAddressInRange(
        range,
        absl::MakeUint128(absl::Uniform<uint64_t>(
                              bitgen, 0, uint64_t{1} << (64 - range.length())),
                          absl::Uniform<uint64_t>(bitgen)));
  }
  if (range.length() == 64) {
    return NthAddressInRange(range,
                             absl::uint128(absl::Uniform<uint64_t>(bitgen)));
  }
  return NthAddressInRange(
      range, absl::uint128(absl::Uniform<uint64_t>(
                 bitgen, 0, uint64_t{1} << (128 - range.length()))));
}

// REQUIRES: both addresses are IPv6.
ABSL_ATTRIBUTE_NOINLINE static absl::weak_ordering ThreeWayCompare_SlowPath(
    const IPAddress& lhs, const IPAddress& rhs) {
  DCHECK(lhs.is_ipv6());
  DCHECK(rhs.is_ipv6());

  return absl::compare_internal::do_three_way_comparison(
      gtl::ChainComparators(
          gtl::OrderBy(
              [](const IPAddress& ip) { return IPAddressToUInt128(ip); }),
          gtl::OrderBy([](const IPAddress& ip) { return ip.scope_id(); })),
      lhs, rhs);
}

absl::weak_ordering ThreeWayCompare(const IPAddress& lhs,
                                    const IPAddress& rhs) {
  if (const auto r = absl::compare_internal::do_three_way_comparison(
          gtl::Less{}, lhs.address_.type(), rhs.address_.type());
      r != absl::weak_ordering::equivalent) {
    return r;
  }

  const IPAddress::Variant& a = lhs.address_;
  const IPAddress::Variant& b = rhs.address_;

  switch (a.type()) {
    case IPAddress::Variant::Type::kIpv4:
      // IPv4 addresses are ordered by their 32-bit integer values.
      return absl::compare_internal::do_three_way_comparison(
          gtl::Less{}, ntohl(a.get_ipv4().s_addr), ntohl(b.get_ipv4().s_addr));

    case IPAddress::Variant::Type::kIpv6: {
      // Compare the bytes of the IPv6 address in big endian order. This is
      // equivalent to comparing as 128-bit integers.
      const in6_addr& addr_a = a.get_ipv6();
      const in6_addr& addr_b = b.get_ipv6();

      const int r =
          std::memcmp(addr_a.s6_addr, addr_b.s6_addr, sizeof(addr_a.s6_addr));

      // Fast path: if the bytes are equal, then we have our answer no matter
      // what the addresses are.
      if (r == 0) {
        if (a.has_scope() == b.has_scope()) {
          return absl::weak_ordering::equivalent;
        }
        return ThreeWayCompare_SlowPath(lhs, rhs);
      }

      // Fast and common path; if we know there are no compact scope IDs
      // involved, then the numeric comparison gives us our answer.
      if (ABSL_PREDICT_TRUE(!a.has_scope() && !b.has_scope())) {
        return r < 0 ? absl::weak_ordering::less : absl::weak_ordering::greater;
      }

      // Otherwise we must unpack the compact scope IDs and compare again.
      return ThreeWayCompare_SlowPath(lhs, rhs);
    }

    case IPAddress::Variant::Type::kUninitialized:
      // Uninitialized addresses are all considered equivalent.
      return absl::weak_ordering::equivalent;
  }

  ABSL_UNREACHABLE();
}

absl::weak_ordering ThreeWayCompare(const SocketAddress& lhs,
                                    const SocketAddress& rhs) {
  // Treat all uninitialized addresses equivalently, ordered before all
  // initialized ones.
  //
  // This ensures we'll only get to the code below that accesses the (host,
  // port) tuple when the port is actually meaningful. Without this we might
  // have two logically uninitialized addresses compare as non-equal due to
  // having differing port numbers.
  {
    const bool lhs_initialized = IsInitializedSocketAddress(lhs);
    const bool rhs_initialized = IsInitializedSocketAddress(rhs);

    if (ABSL_PREDICT_FALSE(!lhs_initialized || !rhs_initialized)) {
      return absl::compare_internal::do_three_way_comparison(
          gtl::Less{}, lhs_initialized, rhs_initialized);
    }
  }

  if (const absl::weak_ordering r = ThreeWayCompare(lhs.host_, rhs.host_);
      r != absl::weak_ordering::equivalent) {
    return r;
  }

  return absl::compare_internal::do_three_way_comparison(gtl::Less{}, lhs.port_,
                                                         rhs.port_);
}

absl::weak_ordering ThreeWayCompare(const IPRange& lhs, const IPRange& rhs) {
  // Treat all uninitialized ranges equivalently, ordered before all initialized
  // ones.
  //
  // This ensures we'll only get to the code below that accesses the (host,
  // length) tuple when the port is actually meaningful. Without this we might
  // have two logically uninitialized ranges compare as non-equal due to having
  // differing lengths.
  {
    const bool lhs_initialized = IsInitializedRange(lhs);
    const bool rhs_initialized = IsInitializedRange(rhs);

    if (ABSL_PREDICT_FALSE(!lhs_initialized || !rhs_initialized)) {
      return absl::compare_internal::do_three_way_comparison(
          gtl::Less{}, lhs_initialized, rhs_initialized);
    }
  }

  // We can use host() here, although logically we are operating on
  // network_address(), because we know that both sides are initialized.  There
  // is no difference between host() and network_address() for IPRange, except
  // that network_address() checks an invariant in debug mode and is slower.  So
  // we repeat that check here and use the faster host().
  DCHECK_EQ(lhs.host(), TruncateIPAddress(lhs.host(), lhs.length()));
  DCHECK_EQ(rhs.host(), TruncateIPAddress(rhs.host(), rhs.length()));
  if (lhs.host() != rhs.host()) {
    return ThreeWayCompare(lhs.host(), rhs.host());
  }

  return absl::compare_internal::do_three_way_comparison(
      gtl::Less{}, lhs.length(), rhs.length());
}

bool GetCompatIPv4Address(const IPAddress& ip6, IPAddress* ip4) {
  if (ip6.address_family() != AF_INET6) {
    DCHECK_NE(AF_UNSPEC, ip6.address_family());
    return false;
  }

  in6_addr addr6 = ip6.ipv6_address();
  if (UNALIGNED_LOAD64(addr6.s6_addr16) != 0 ||
      UNALIGNED_LOAD32(addr6.s6_addr16 + 4) != 0) {
    return false;
  }

  // :: and ::1 are special cases and should not be treated as compatible
  // addresses; see http://en.wikipedia.org/wiki/IPv4-compatible_address.
  if (uint32_t last = BigEndian::Load32(addr6.s6_addr16 + 6);
      last == 0 || last == 1) {
    return false;
  }

  if (ip4) {
    in_addr ipv4;
    ipv4.s_addr = UNALIGNED_LOAD32(addr6.s6_addr16 + 6);
    *ip4 = IPAddress(ipv4);
  }

  return true;
}

bool GetMappedIPv4Address(const IPAddress& ip6, IPAddress* ip4) {
  if (ip6.address_family() != AF_INET6) {
    DCHECK_NE(AF_UNSPEC, ip6.address_family());
    return false;
  }

  in6_addr addr6 = ip6.ipv6_address();
  if (UNALIGNED_LOAD64(addr6.s6_addr16) != 0 || addr6.s6_addr16[4] != 0 ||
      addr6.s6_addr16[5] != 0xffff) {
    return false;
  }

  if (ip4) {
    in_addr ipv4;
    ipv4.s_addr = UNALIGNED_LOAD32(addr6.s6_addr16 + 6);
    *ip4 = IPAddress(ipv4);
  }

  return true;
}

bool Get6to4IPv4Address(const IPAddress& ip6, IPAddress* ip4) {
  if (ip6.address_family() != AF_INET6) {
    DCHECK_NE(AF_UNSPEC, ip6.address_family());
    return false;
  }

  in6_addr addr6 = ip6.ipv6_address();
  if (addr6.s6_addr16[0] != ghtons(0x2002)) {
    return false;
  }

  if (ip4) {
    in_addr addr4;
    DCHECK_EQ(4U, sizeof(addr4));
    memcpy(&addr4, &addr6.s6_addr16[1], sizeof(addr4));
    *ip4 = IPAddress(addr4);
  }

  return true;
}

bool Get6to4IPv6Range(const IPRange& iprange4, IPRange* iprange6) {
  if (iprange4.host().address_family() != AF_INET) {
    DCHECK_NE(AF_UNSPEC, iprange4.host().address_family());
    return false;
  }

  if (iprange6) {
    in6_addr addr6;
    in_addr addr4 = iprange4.host().ipv4_address();

    memset(&addr6, 0, sizeof(addr6));
    addr6.s6_addr16[0] = ghtons(0x2002);
    DCHECK_EQ(4U, sizeof(addr4));
    memcpy(&addr6.s6_addr16[1], &addr4, sizeof(addr4));

    *iprange6 =
        IPRange::UnsafeConstruct(IPAddress(addr6), iprange4.length() + 16);
  }
  return true;
}

bool GetIsatapIPv4Address(const IPAddress& ip6, IPAddress* ip4) {
  if (ip6.address_family() != AF_INET6) {
    DCHECK_NE(AF_UNSPEC, ip6.address_family());
    return false;
  }

  // If it's a Teredo address with the right port (41217, or 0xa101) which
  // would be encoded as 0x5efe then it can't be an ISATAP address.
  if (GetTeredoInfo(ip6, nullptr, nullptr, nullptr, nullptr)) {
    return false;
  }

  in6_addr addr6 = ip6.ipv6_address();
  // ISATAP addresses are identifiable by the 32bit 0000:5efe
  // prepended to the client's IPv4 address to form the 64bit
  // interface identifier.  The usual rules about U/L and G bits
  // apply as well, hence we mask those bits when testing for equality.
  if (addr6.s6_addr16[5] != ghtons(0x5efe) ||
      (addr6.s6_addr16[4] | ghtons(0x0300)) != ghtons(0x0300)) {
    return false;
  }

  if (ip4) {
    in_addr ipv4;
    ipv4.s_addr = UNALIGNED_LOAD32(addr6.s6_addr16 + 6);
    *ip4 = IPAddress(ipv4);
  }

  return true;
}

bool GetTeredoInfo(const IPAddress& ip6, IPAddress* server, uint16_t* flags,
                   uint16_t* port, IPAddress* client) {
  if (ip6.address_family() != AF_INET6) {
    DCHECK_NE(AF_UNSPEC, ip6.address_family());
    return false;
  }

  in6_addr addr6 = ip6.ipv6_address();
  if (addr6.s6_addr16[0] != ghtons(0x2001) || addr6.s6_addr16[1] != 0)
    return false;

  in_addr ipv4;
  if (client) {
    ipv4.s_addr = ~UNALIGNED_LOAD32(addr6.s6_addr16 + 6);
    *client = IPAddress(ipv4);
  }
  if (server) {
    ipv4.s_addr = UNALIGNED_LOAD32(addr6.s6_addr16 + 2);
    *server = IPAddress(ipv4);
  }

  if (port) {
    *port = gntohs(~addr6.s6_addr16[5]);
  }
  if (flags) {
    *flags = gntohs(addr6.s6_addr16[4]);
  }

  return true;
}

bool GetEmbeddedIPv4ClientAddress(const IPAddress& ip6, IPAddress* ip4) {
  // Return the IPv4 Compat, IPv4 Mapped, 6to4, or Teredo client address,
  // if applicable.  NOTE: ISATAP addresses are explicitly NOT returned:
  // the client addresses are not part of the routing information and
  // are, consequently, considerably more spoofable.
  return (GetCompatIPv4Address(ip6, ip4) || GetMappedIPv4Address(ip6, ip4) ||
          Get6to4IPv4Address(ip6, ip4) ||
          GetTeredoInfo(ip6, nullptr, nullptr, nullptr, ip4));
}

IPAddress GetCoercedIPv4Address(const IPAddress& ip6) {
  // Return the same address if it's already IPv4.
  if (ip6.address_family() == AF_INET) {
    return ip6;
  }
  CHECK_EQ(AF_INET6, ip6.address_family());

  // Special-case "any", so IPv6 "any" becomes IPv4 "any".
  if (ip6 == IPAddress::Any6()) {
    return IPAddress::Any4();
  }

  // Special-case localhost, so IPv6 localhost becomes IPv4 localhost.
  if (ip6 == IPAddress::Loopback6()) {
    return IPAddress::Loopback4();
  }

  in_addr coerced;

  IPAddress ip4;
  uint32_t ip_hash;
  if (GetEmbeddedIPv4ClientAddress(ip6, &ip4)) {
    // Hash the 32 bit embedded IPv4 address.  Other parts of the IPv6
    // address can be arbitrarily varied by the client.
    struct in_addr address = ip4.ipv4_address();
    ip_hash = HashTo32(
        absl::string_view(reinterpret_cast<const char*>(&address.s_addr), 4));
  } else {
    // Hash the top 64 bits.  Assumption: most leaf networks will be /64s.
    ip_hash = HashTo32(absl::string_view(
        reinterpret_cast<const char*>(ip6.ipv6_address().s6_addr), 8));
  }

  // s_addr is always taken to be big endian; however, for a random string
  // of bits like a hash endianness doesn't matter as long as we're
  // consistent. Due to historical reasons (the code was originally
  // written on x86, and used native byte order), we have to write the hash to
  // s_addr as if it were a uint32_t in little endian.
  LittleEndian::Store32(&coerced.s_addr, ip_hash);

  // Squash into 224/4 Multicast and 240/4 Reserved space (i.e. 224/3).
  coerced.s_addr |= ghtonl(0xe0000000);

  // Fixup to avoid some "illegal" values.  Currently the only potential
  // illegal value is 255.255.255.255.
  if (coerced.s_addr == 0xffffffff) {
    coerced.s_addr &= ghtonl(0xfffffffe);
  }

  return IPAddress(coerced);
}

IPAddress NormalizeIPAddress(const IPAddress& ip) {
  if (ip.address_family() != AF_INET6) {
    return ip;
  }

  IPAddress normalized_ip;
  if (GetMappedIPv4Address(ip, &normalized_ip)) {
    return normalized_ip;
  }

  // Not an IPv4 address stored in an IPv6 address; just return it
  // unchanged.
  return ip;
}

IPAddress DualstackIPAddress(const IPAddress& ip) {
  if (ip.address_family() == AF_INET6) {
    return ip;
  }

  CHECK_EQ(AF_INET, ip.address_family());
  struct in6_addr v4mapped = {};
  v4mapped.s6_addr16[5] = htons(0xffff);
  UNALIGNED_STORE32(v4mapped.s6_addr16 + 6, ip.ipv4_address().s_addr);
  DCHECK(IN6_IS_ADDR_V4MAPPED(&v4mapped))
      << "Conversion of " << ip << " to a dualstack IP address failed.";

  return IPAddress(v4mapped);
}

// This factory function exists to implement the legacy semantics of the crashy
// constructor that accepts sockaddr. If you want to expose something like this
// in the header, do not use this one—it should be an absl::StatusOr-returning
// function that does proper error handling, like
// MakeSocketAddressFromSockaddrIn6.
static SocketAddress MakeSocketAddressFromSockaddr(const sockaddr& sa) {
  switch (sa.sa_family) {
    case AF_INET: {
      const auto sin =
          base::UnalignedLoad<sockaddr_in>(reinterpret_cast<const char*>(&sa));

      // Our type punning should result in this condition.
      ABSL_ASSUME(sin.sin_family == AF_INET);

      return SocketAddress(sin);
    }

    case AF_INET6: {
      const auto sin6 =
          base::UnalignedLoad<sockaddr_in6>(reinterpret_cast<const char*>(&sa));
      const uint16_t port = ntohs(sin6.sin6_port);

      // Our type punning should result in this condition.
      ABSL_ASSUME(sin6.sin6_family == AF_INET6);

      ABSL_ASSIGN_OR_RETURN(
          IPAddress fully_specified_ip_address,
          MakeIPAddressWithScopeId(sin6.sin6_addr, sin6.sin6_scope_id),
          _.With([&](const absl::Status& error) {
            // In order to not induce several thousand failures for
            // uninitialized sockaddrs, simply log this failure and silently
            // carry on. Note that the fix for this may be as simple as:
            //
            //     sockaddr_storage foo;    // uninitialized
            //     sockaddr_storage foo{};  // generally much happier
            //
            LOG(WARNING) << error << "; possibly uninitialized sockaddr?";

            return SocketAddress{
                IPAddress(sin6.sin6_addr),
                port,
            };
          }));

      return SocketAddress{
          std::move(fully_specified_ip_address),
          port,
      };
    }

    default:
      LOG(DFATAL) << "Unknown address family "
                  << static_cast<int>(sa.sa_family);

      ABSL_FALLTHROUGH_INTENDED;

    case AF_UNSPEC:
      return SocketAddress();
  }
}

SocketAddress::SocketAddress(const struct sockaddr& saddr)
    : SocketAddress{
          MakeSocketAddressFromSockaddr(saddr),
      } {}

std::string SocketAddress::ToString() const {
  if (ABSL_PREDICT_FALSE(!IsInitializedAddress(host()))) {
    LOG(DFATAL) << "Calling ToString() on an empty SocketAddress";
    return "";
  }
  // IPv6 literal plus []: plus a possible UINT16_MAX as ASCII, ie. 5
  char buf[INET6_ADDRSTRLEN + 3 + 5];
  char* s = buf;
  switch (host_.address_family()) {
    case AF_INET:
      s = host_.ToCharBuf(s);
      break;
    case AF_INET6:
      *s++ = '[';
      s = host_.ToCharBuf(s);
      *s++ = ']';
      break;
  }
  *s++ = ':';
  s = strings::FastIntToBufferLeft(port_, s);
  return std::string(buf, s - buf);
}

std::string SocketAddress::ToStringWithScopeId() const {
  if (ABSL_PREDICT_FALSE(!IsInitializedAddress(host()))) {
    LOG(DFATAL) << "Calling ToStringWithScopeId() on an empty SocketAddress";
    return "";
  }
  // IPv6 literal plus []: plus a possible UINT16_MAX as ASCII, ie. 5
  // Also a possible %UINT32_MAX as ASCII inside the brackets.
  char buf[INET6_ADDRSTRLEN + 12 + 3 + 5];
  char* s = buf;
  switch (host_.address_family()) {
    case AF_INET:
      s = host_.ToCharBuf(s);
      break;
    case AF_INET6:
      *s++ = '[';
      s = host_.ToCharBuf(s);
      if (host_.scope_id() > 0) {
        *s++ = '%';
        s = strings::FastIntToBufferLeft(host_.scope_id(), s);
      }
      *s++ = ']';
      break;
  }
  *s++ = ':';
  s = strings::FastIntToBufferLeft(port_, s);
  return std::string(buf, s - buf);
}

std::string SocketAddress::ToPackedString() const {
  const uint16_t port = htons(port_);
  return absl::StrCat(
      host_.ToPackedString(),
      absl::string_view(reinterpret_cast<const char*>(&port), sizeof(port)));
}

IPAddress IPRange::network_address() const {
  switch (host_.address_family()) {
    case AF_INET:
    case AF_INET6:
      DCHECK_EQ(host(), TruncateIPAddress(host(), length()));
      return host_;

    default:
      LOG(DFATAL) << "Unknown address family " << host_.address_family();
      return IPAddress();
  }
}

IPAddress IPRange::broadcast_address() const {
  switch (host_.address_family()) {
    case AF_INET: {
      if (length() == 0) {
        return HostUInt32ToIPAddress(~0U);
      }

      // OR the address with a mask of "length_" leading zeroes and the
      // remainder of the bits set to one.
      const uint32_t addr32 = IPAddressToHostUInt32(host_);
      return HostUInt32ToIPAddress(addr32 | (~(~0U << (32 - length()))));
    }

    case AF_INET6: {
      if (length() == 0) {
        return UInt128ToIPAddress(~absl::uint128(0));
      }

      // OR the address with a mask of "length_" leading zeroes and the
      // remainder of the bits set to one.
      const absl::uint128 addr128 = IPAddressToUInt128(host_);
      return MakeIPAddressWithOptionalScopeId(
          addr128 | ~(~absl::uint128(0) << (128 - length())), host_.scope_id());
    }

    default: {
      LOG(DFATAL) << "Unknown address family " << host_.address_family();
      return IPAddress();
    }
  }
}

bool StringToSocketAddress(absl::string_view str, SocketAddress* out) {
  absl::string_view host_str;
  uint16_t port;
  if (!strings::ParseHostOptionalPort(str, -1, &host_str, &port)) {
    return false;
  }

  IPAddress host;
  if (!StringToIPAddress(host_str, &host)) {
    return false;
  }
  if ((str[0] == '[') != (host.address_family() == AF_INET6)) {
    return false;
  }

  if (out) {
    *out = SocketAddress(host, port);
  }
  return true;
}

absl::StatusOr<SocketAddress> StringToSocketAddressWithOptionalScope(
    absl::string_view str) {
  absl::string_view host_str;
  uint16_t port;
  if (!strings::ParseHostOptionalPort(str, -1, &host_str, &port)) {
    return absl::InvalidArgumentError(
        "bad port specification in socket string literal");
  }

  const auto ipaddr_or = StringToIPAddressWithOptionalScope(host_str);
  if (!ipaddr_or.ok()) {
    return ipaddr_or.status();
  }
  const auto& host = ipaddr_or.value();

  if ((str[0] == '[') != (host.address_family() == AF_INET6)) {
    return absl::InvalidArgumentError(
        "bad IP string literal: braces can only be used for IPv6 addresses");
  }

  return SocketAddress(host, port);
}

bool StringToSocketAddressWithDefaultPort(absl::string_view str,
                                          uint16_t default_port,
                                          SocketAddress* out) {
  absl::string_view host_str;
  uint16_t port;
  if (!strings::ParseHostOptionalPort(str, static_cast<int>(default_port),
                                      &host_str, &port)) {
    return false;
  }

  IPAddress host;
  if (!StringToIPAddress(host_str, &host)) {
    return false;
  }
  if ((str[0] == '[') != (host.address_family() == AF_INET6)) {
    return false;
  }

  if (out) {
    *out = SocketAddress(host, port);
  }
  return true;
}

sockaddr_storage SocketAddress::generic_address() const {
  sockaddr_storage ret;
  socklen_t size;
  CHECK(SocketAddressToFamily(AF_UNSPEC, *this, &ret, &size))
      << "Called generic_address() on " << *this;
  return ret;
}

bool SocketAddressToFamily(int output_family, const SocketAddress& sa,
                           sockaddr_storage* addr_out, socklen_t* size_out) {
  memset(addr_out, 0, sizeof(sockaddr_storage));
  const IPAddress host = sa.host();
  if (output_family == AF_UNSPEC) {
    output_family = host.address_family();
  }
  switch (output_family) {
    case AF_INET: {
      sockaddr_in* addr = reinterpret_cast<sockaddr_in*>(addr_out);
      if (size_out != nullptr) {
        *size_out = sizeof(*addr);
      }
      if (host.address_family() == AF_INET) {
        addr->sin_family = AF_INET;
        addr->sin_addr = host.ipv4_address();
        addr->sin_port = htons(sa.port());
        return true;
      } else if (host == IPAddress::Any6()) {
        // Binding to :: can be useful regardless of the socket family.
        addr->sin_family = AF_INET;
        DCHECK_EQ(IPAddress(addr->sin_addr), IPAddress::Any4());
        addr->sin_port = htons(sa.port());
        return true;
      }
      break;
    }
    case AF_INET6: {
      sockaddr_in6* addr = reinterpret_cast<sockaddr_in6*>(addr_out);
      if (size_out != nullptr) {
        *size_out = sizeof(*addr);
      }
      if (host.address_family() == AF_INET6) {
        addr->sin6_family = AF_INET6;
        addr->sin6_addr = host.ipv6_address();
        addr->sin6_port = htons(sa.port());
        addr->sin6_scope_id = host.scope_id();
        return true;
      } else if (host.address_family() == AF_INET) {
        // Convert IPv4 to IPv6, for use in dualstack sockets.
        addr->sin6_family = AF_INET6;
        addr->sin6_addr = DualstackIPAddress(host).ipv6_address();
        addr->sin6_port = htons(sa.port());
        return true;
      }
      break;
    }
  }
  // Generate an invalid sockaddr, to prevent accidental use.
  LOG(WARNING) << "Can't convert address family " << host.address_family()
               << " to " << output_family;
  addr_out->ss_family =
      std::numeric_limits<decltype(addr_out->ss_family)>::max();
  if (size_out != nullptr) {
    *size_out = 0;
  }
  return false;
}

bool SocketAddressToFamilyForBind(int output_family, const SocketAddress& sa,
                                  sockaddr_storage* addr_out,
                                  socklen_t* size_out) {
  SocketAddress sa_copy(sa);
  if (output_family == AF_INET6 && sa.host() == IPAddress::Any4()) {
    // Convert 0.0.0.0:port to [::]:port.
    sa_copy = SocketAddress(IPAddress::Any6(), sa.port());
  }
  return SocketAddressToFamily(output_family, sa_copy, addr_out, size_out);
}

namespace {

bool InternalStringToNetmaskLength(absl::string_view str,
                                   int host_address_family, int* out) {
  DCHECK(out);

  // Explicitly check that the first and last characters are digits, because
  // safe_strto32() will accept whitespace, +, -, etc.
  if (str.empty() || !absl::ascii_isdigit(*str.begin()) ||
      !absl::ascii_isdigit(*str.rbegin())) {
    return false;
  }

  // Check for a decimal number.
  if (absl::SimpleAtoi(str, out)) {
    DCHECK_GE(*out, 0);
    const int max_length =
        host_address_family == AF_INET6 ? kMaxNetmaskIPv6 : kMaxNetmaskIPv4;
    return *out <= max_length;
  }

  // Check for a netmask in dotted quad form, e.g. "255.255.0.0".
  in_addr mask;
  if (host_address_family == AF_INET && StringToInAddr(str, &mask)) {
    if (mask.s_addr == 0) {
      *out = 0;
    } else {
      // Now we check to make sure we have a sane netmask.
      // The inverted mask in native byte order (+1) will have to be a
      // power of two, if it's valid.
      uint32_t inv_mask = (~gntohl(mask.s_addr)) + 1;
      // Power of two iff x & (x - 1) == 0.
      if ((inv_mask & (inv_mask - 1)) != 0) {
        return false;
      }
      *out = 32 - __builtin_ffs(gntohl(mask.s_addr)) + 1;
    }
    return true;
  }

  return false;
}

//
// The "meat" of StringToIPRange{,AndTruncate}. Does no checking of correct
// prefix length, nor any automatic truncation.
//
bool InternalStringToIPRange(absl::string_view str,
                             std::pair<IPAddress, int>* out) {
  DCHECK(out);

  // Try to parse everything before the slash as an IP address.
  // If there is no slash, then substr(0, npos) yields the full string.
  const size_t slash_pos = str.find('/');
  if (!StringToIPAddress(str.substr(0, slash_pos), &out->first)) {
    return false;
  }

  // Try to parse everything after the slash as a prefix length.
  if (slash_pos != absl::string_view::npos) {
    return InternalStringToNetmaskLength(
        absl::ClippedSubstr(str, slash_pos + 1), out->first.address_family(),
        &out->second);
  }

  // There was no slash, so the range covers a single address.
  out->second = IPAddressLength(out->first);
  return true;
}

}  // namespace

bool StringToIPRange(absl::string_view str, IPRange* out) {
  std::pair<IPAddress, int> parsed;
  if (!InternalStringToIPRange(str, &parsed)) {
    return false;
  }
  const IPRange result(parsed.first, parsed.second);
  if (result.host() != parsed.first) {
    // Some bits were truncated.
    return false;
  }
  if (out) {
    *out = result;
  }
  return true;
}

bool StringToIPRangeAndTruncate(absl::string_view str, IPRange* out) {
  std::pair<IPAddress, int> parsed;
  if (!InternalStringToIPRange(str, &parsed)) {
    return false;
  }
  if (out) {
    *out = IPRange(parsed.first, parsed.second);
  }
  return true;
}

namespace ipaddress_internal {

IPAddress TruncateIPAndLength(const IPAddress& addr, int* length_io) {
  const int length = *length_io;
  switch (addr.address_family()) {
    case AF_INET: {
      if (length >= kMaxNetmaskIPv4) {
        *length_io = kMaxNetmaskIPv4;
        return addr;
      } else if (length > 0) {
        uint32_t ip4 = IPAddressToHostUInt32(addr);
        ip4 &= ~0U << (32 - length);
        return HostUInt32ToIPAddress(ip4);
      } else if (length == 0) {
        return IPAddress::Any4();
      }
      break;
    }
    case AF_INET6: {
      if (length >= kMaxNetmaskIPv6) {
        *length_io = kMaxNetmaskIPv6;
        return addr;
      } else if (length > 0) {
        absl::uint128 ip6 = IPAddressToUInt128(addr);
        ip6 &= ~absl::uint128(0) << (128 - length);
        return MakeIPAddressWithOptionalScopeId(ip6, addr.scope_id());
      } else if (length == 0) {
        return IPAddress::Any6();
      }
      break;
    }
    case AF_UNSPEC:
      *length_io = -1;
      return addr;
  }
  LOG(DFATAL) << "Invalid truncation: " << addr << "/" << length;
  *length_io = -1;
  return IPAddress();
}

}  // namespace ipaddress_internal

namespace {
// Constant needed to differentiate between IPv4 range and IPv6 range in
// IPRange::ToPackedString(). The prefix length and address family are stored
// in one byte, with values [0..128] assigned to IPv6 and [200..232] assigned
// to IPv4.
const uint8_t kPackedIPRangeIPv4LengthOffset = 200;
}  // namespace

std::string IPRange::ToPackedString() const {
  if (!(host_.address_family() == AF_INET ||
        host_.address_family() == AF_INET6)) {
    LOG(DFATAL) << "Uninitialized address in IPRange.";
    return "";
  }

  // Get the host part, with unwanted suffix bits zeroed.
  const std::string packed_host = host_.ToPackedString();

  // Retain only the portion of the string that is within the mask.
  uint8_t packed_host_len = (length() + 7) / 8;
  // Further compress by removing trailing 0s.
  while (packed_host_len > 0 && packed_host.at(packed_host_len - 1) == 0) {
    --packed_host_len;
  }
  // Encode the address family and prefix length into a 1-byte header.
  uint8_t header = length();
  if (host().address_family() == AF_INET) {
    header += kPackedIPRangeIPv4LengthOffset;
  }
  // Put it all together.
  std::string out;
  out.reserve(1 + packed_host_len);
  out.push_back(static_cast<char>(header));
  out.append(packed_host.data(), packed_host_len);
  return out;
}

bool PackedStringToIPRange(absl::string_view str, IPRange* out) {
  if (str.empty()) {
    return false;
  }
  const uint8_t header = static_cast<uint8_t>(str[0]);
  const size_t available_host_bytes = str.size() - 1;
  // If we have a packed IPv6 IPRange, then the header will represent the mask
  // length. If it is IPv4 range, than the mask length is obtained from header
  // by subtracting kPackedIPRangeIPv4LengthOffset.
  int prefix_len;
  size_t sizeof_addr;
  if (0 <= header && header <= kMaxNetmaskIPv6) {
    prefix_len = header;
    sizeof_addr = sizeof(in6_addr);
  } else if (kPackedIPRangeIPv4LengthOffset <= header &&
             header <= kMaxNetmaskIPv4 + kPackedIPRangeIPv4LengthOffset) {
    prefix_len = header - kPackedIPRangeIPv4LengthOffset;
    sizeof_addr = sizeof(in_addr);
  } else {
    LOG(ERROR) << "Invalid netmask " << static_cast<int>(header)
               << " passed to PackedStringToIPRange. Valid ranges are: 0-"
               << kMaxNetmaskIPv6 << " and "
               << static_cast<int>(kPackedIPRangeIPv4LengthOffset) << "-"
               << kPackedIPRangeIPv4LengthOffset + kMaxNetmaskIPv4 << ".";
    return false;
  }

  // Verify that the input doesn't overflow the address width.
  if (available_host_bytes > sizeof_addr) {
    return false;
  }

  // Drop the address into a zero-padded buffer, and convert to IPAddress.
  std::string packed_host(sizeof_addr, '\0');
  packed_host.replace(0, available_host_bytes, str.data() + 1,
                      available_host_bytes);
  const IPAddress host = PackedStringToIPAddressOrDie(packed_host);

  // Verify that the input has no bits set beyond the prefix length.
  const IPRange truncated(host, prefix_len);
  if (truncated.host() != host) {
    return false;
  }
  if (out) {
    *out = truncated;
  }
  return true;
}

bool IPAddressIntervalToSubnets(const IPAddress& first_addr,
                                const IPAddress& last_addr,
                                std::vector<IPRange>* covering_subnets) {
  covering_subnets->clear();

  // Fail if parameters do not belong to the same valid address family.
  if (first_addr.address_family() != last_addr.address_family() ||
      first_addr.address_family() == AF_UNSPEC) {
    return false;
  }

  for (IPAddress cur_addr = first_addr; !(last_addr < cur_addr);) {
    // Find the least specific IP subnet of cur_addr whose endpoints are still
    // covered by the interval [cur_addr, last_addr].
    IPRange cur_subnet(cur_addr);
    for (int len = IPAddressLength(cur_addr) - 1; len >= 0; --len) {
      const IPRange candidate_subnet(cur_addr, len);
      if (candidate_subnet.host() != cur_addr ||
          last_addr < candidate_subnet.broadcast_address()) {
        break;
      }
      cur_subnet = candidate_subnet;
    }

    covering_subnets->push_back(cur_subnet);

    // Find the first address not yet covered by covering_subnets.
    // As a special case, if we covered the max address (e.g. 255.255.255.255),
    // IPAddressPlusN returns false and we are done.
    const IPAddress last_covered_addr = cur_subnet.broadcast_address();
    if (!IPAddressPlusN(last_covered_addr, 1, &cur_addr)) {
      break;
    }
  }

  return !covering_subnets->empty();
}

bool IsRangeIndexValid(const IPRange& range, absl::uint128 index) {
  // Check for potential uint128 >> 128, which is undefined.
  return IPAddressLength(range.host()) - range.length() == 128 ||
         (index >> (IPAddressLength(range.host()) - range.length())) == 0;
}

IPAddress NthAddressInRange(const IPRange& range, absl::uint128 index) {
  if (!IsRangeIndexValid(range, index)) {
    LOG(DFATAL) << range << " does not contain index " << index;
    return IPAddress();
  }
  switch (range.host().address_family()) {
    case AF_INET: {
      const uint32_t addr = IPAddressToHostUInt32(range.host());
      return HostUInt32ToIPAddress(addr + absl::Uint128Low64(index));
    }
    case AF_INET6: {
      const absl::uint128 addr = IPAddressToUInt128(range.host());
      return MakeIPAddressWithOptionalScopeId(addr + index,
                                              range.host().scope_id());
    }
    default: {
      LOG(DFATAL)
          << "NthAddressInRange of IPRange with invalid address family: "
          << range.host().address_family();
      return IPAddress();
    }
  }
}

absl::uint128 IndexInRange(const IPRange& range, const IPAddress& ip) {
  if (!IsWithinSubnet(range, ip)) {
    LOG(DFATAL) << ip << " is not within " << range;
    return ~absl::uint128(0);
  }
  switch (range.host().address_family()) {
    case AF_INET: {
      const uint32_t addr = IPAddressToHostUInt32(range.host());
      return IPAddressToHostUInt32(ip) - addr;
    }
    case AF_INET6: {
      const absl::uint128 addr = IPAddressToUInt128(range.host());
      return IPAddressToUInt128(ip) - addr;
    }
    default: {
      LOG(DFATAL) << "IPRange with invalid address family: "
                  << range.host().address_family();
      return ~absl::uint128(0);
    }
  }
}

bool IPAddressPlusN(const IPAddress& addr, absl::int128 n, IPAddress* result) {
  if (n == 0) {
    *result = addr;
    return true;
  }
  IPAddress addr_copy = addr;
  constexpr absl::int128 kUint32Max = std::numeric_limits<uint32_t>::max();
  switch (addr.address_family()) {
    case AF_INET:
      if (n > kUint32Max || n < -kUint32Max) {
        return false;
      }
      *result = HostUInt32ToIPAddress(IPAddressToHostUInt32(addr) +
                                      static_cast<int32_t>(n));
      break;
    case AF_INET6:
      *result = MakeIPAddressWithOptionalScopeId(IPAddressToUInt128(addr) + n,
                                                 addr.scope_id());
      break;
    default:
      LOG(DFATAL) << "Invalid address family " << addr.address_family();
      return false;
  }
  // Return false iff the result crosses the IP address space.
  return (n > 0) == (addr_copy < *result);
}

bool SubtractIPRange(const IPRange& range, const IPRange& sub_range,
                     std::vector<IPRange>* diff_range) {
  diff_range->clear();

  // Subtract is undefined if "sub_range" is not a more specific of "range".
  if (!IsProperSubRange(range, sub_range)) {
    return false;
  }
  DCHECK_GE(sub_range.length(), 1);

  // An illustrative example using 8-bit IP addressing:
  //   range:      b7  b6  b5  b4  --  --  --  --  /4
  //   sub_range:  b7  b6  b5  b4  b3  b2  b1  b0  /8
  //
  //   diff_range: b7  b6  b5  b4  b3  b2  b1 ~b0  /8
  //               b7  b6  b5  b4  b3  b2 ~b1  --  /7
  //               b7  b6  b5  b4  b3 ~b2  --  --  /6
  //               b7  b6  b5  b4 ~b3  --  --  --  /5
  //
  int address_family = sub_range.host().address_family();
  switch (address_family) {
    case AF_INET: {
      in_addr addr4 = sub_range.network_address().ipv4_address();
      uint32_t flip_mask = 1U << (32 - sub_range.length());
      uint32_t subnet_mask = ~1U << (32 - sub_range.length());
      for (int len = sub_range.length(); len > range.length(); --len) {
        addr4.s_addr ^= ghtonl(flip_mask);
        diff_range->push_back(IPRange::UnsafeConstruct(IPAddress(addr4), len));
        addr4.s_addr &= ghtonl(subnet_mask);
        flip_mask <<= 1;
        subnet_mask <<= 1;
      }
      break;
    }
    case AF_INET6: {
      absl::uint128 addr128 = IPAddressToUInt128(sub_range.network_address());
      absl::uint128 flip_mask = absl::uint128(1) << (128 - sub_range.length());
      absl::uint128 subnet_mask = ~absl::uint128(1)
                                  << (128 - sub_range.length());
      for (int len = sub_range.length(); len > range.length(); --len) {
        addr128 ^= flip_mask;
        diff_range->push_back(
            IPRange::UnsafeConstruct(MakeIPAddressWithOptionalScopeId(
                                         addr128, sub_range.host().scope_id()),
                                     len));
        addr128 &= subnet_mask;
        flip_mask <<= 1;
        subnet_mask <<= 1;
      }
      break;
    }
    default: {
      LOG(FATAL) << "Unknown address family " << address_family;
      return false;
    }
  }
  return true;
}

bool NetMaskToMaskLength(const IPAddress& address, int* length) {
  int result;
  switch (address.address_family()) {
    case AF_INET: {
      uint32_t ipv4 = IPAddressToHostUInt32(address);
      result = ipv4 != 0 ? 32 - Bits::FindLSBSetNonZero(ipv4) : 0;
      // Verify this is a valid netmask.
      if ((~ipv4 & (~ipv4 + 1)) != 0) return false;
      break;
    }

    case AF_INET6: {
      absl::uint128 ipv6 = IPAddressToUInt128(address);
      result = ipv6 != 0 ? 128 - Bits::FindLSBSetNonZero128(ipv6) : 0;
      // Verify this is a valid netmask.
      if ((~ipv6 & (~ipv6 + 1)) != 0) return false;
      break;
    }

    default:
      return false;
  }

  if (length != nullptr) *length = result;
  return true;
}

bool MaskLengthToIPAddress(int family, int length, IPAddress* address) {
  switch (family) {
    case AF_INET: {
      if (length < 0 || length > 32) return false;

      // <<32 on an uint32_t is undefined, LL is important.
      uint32_t mask = 0xffffffffLL << (32 - length);
      if (address) *address = HostUInt32ToIPAddress(mask);
      return true;
    }

    case AF_INET6: {
      if (length < 0 || length > 128) return false;

      // uint128_t << 128 is undefined, so case on length.
      absl::uint128 mask =
          length == 0 ? 0 : absl::Uint128Max() << (128 - length);
      if (address) *address = UInt128ToIPAddress(mask);
      return true;
    }
  }
  return false;
}

std::string AddressFamilyToString(int family) {
  switch (family) {
    case AF_UNSPEC:
      return "unspecified";
    case AF_INET:
      return "IPv4";
    case AF_INET6:
      return "IPv6";
  }
  return "unknown";
}

bool AbslParseFlag(absl::string_view text, IPAddress* dst,
                   std::string* /* err */) {
  if (text.empty()) {
    *dst = IPAddress();
    return true;
  }
  return StringToIPAddress(text, dst);
}

std::string AbslUnparseFlag(IPAddress ip) {
  if (!IsInitializedAddress(ip)) {
    return "";
  }
  return ip.ToString();
}

bool AbslParseFlag(absl::string_view text, IPRange* dst,
                   std::string* /* err */) {
  if (text.empty()) {
    *dst = IPRange();
    return true;
  }
  return StringToIPRange(text, dst);
}

std::string AbslUnparseFlag(IPRange range) {
  if (!IsInitializedRange(range)) {
    return "";
  }
  return range.ToString();
}

bool AbslParseFlag(absl::string_view text, SocketAddress* dst,
                   std::string* /* err */) {
  if (text.empty()) {
    *dst = SocketAddress();
    return true;
  }
  absl::StatusOr<SocketAddress> status_or_sa =
      StringToSocketAddressWithOptionalScope(text);
  *dst = status_or_sa.value_or(SocketAddress());
  return status_or_sa.ok();
}

std::string AbslUnparseFlag(SocketAddress sa) {
  if (!IsInitializedSocketAddress(sa)) {
    return "";
  }
  return sa.ToStringWithScopeId();
}

bool AbslParseFlag(absl::string_view text, IPAddressList* dst,
                   std::string* error) {
  // An empty flag value corresponds to an empty vector, not a vector
  // with a single, uninitialized IP address.
  dst->clear();
  if (text.empty()) {
    return true;
  }
  for (const absl::string_view ip_string :
       absl::StrSplit(text, ',', absl::AllowEmpty())) {
    IPAddress ip;
    if (!AbslParseFlag(ip_string, &ip, error)) {
      return false;
    }
    dst->push_back(ip);
  }
  return true;
}

std::string AbslUnparseFlag(IPAddressList ips) {
  std::vector<std::string> ip_address_strings;
  for (const IPAddress& ip : ips) {
    ip_address_strings.push_back(AbslUnparseFlag(ip));
  }
  return absl::StrJoin(ip_address_strings, ",");
}

}  // namespace net_base

size_t HASH_NAMESPACE::hash<net_base::IPAddress>::operator()(
    const net_base::IPAddress& address) const {
  switch (address.address_family()) {
    case AF_INET: {
      in_addr addr4 = address.ipv4_address();
      return Hash32NumWithSeed(addr4.s_addr, AF_INET);
    }
    case AF_INET6: {
      const in6_addr addr6 = address.ipv6_address();
      const size_t value =
          Hash32NumWithSeed(UNALIGNED_LOAD32(addr6.s6_addr16),
                            UNALIGNED_LOAD32(addr6.s6_addr16 + 2)) ^
          Hash32NumWithSeed(UNALIGNED_LOAD32(addr6.s6_addr16 + 4),
                            UNALIGNED_LOAD32(addr6.s6_addr16 + 6));
      const uint32_t scope_id = address.scope_id();
      if (scope_id == 0) return value;
      return value ^ Hash32NumWithSeed(scope_id, AF_INET6);
    }
    case AF_UNSPEC: {
      return hash<int>()(address.address_family());
    }
    default: {
      LOG(FATAL) << "Unknown address family " << address.address_family();
    }
  }
}

size_t HASH_NAMESPACE::hash<net_base::SocketAddress>::operator()(
    const net_base::SocketAddress& address) const {
  if (!IsInitializedSocketAddress(address)) {
    return hash<net_base::IPAddress>()(address.host());
  }
  return Hash32NumWithSeed(hash<net_base::IPAddress>()(address.host()),
                           address.port());
}

size_t HASH_NAMESPACE::hash<net_base::IPRange>::operator()(
    const net_base::IPRange& range) const {
  if (!IsInitializedRange(range)) {
    return hash<net_base::IPAddress>()(range.host());
  }
  return Hash32NumWithSeed(hash<net_base::IPAddress>()(range.network_address()),
                           range.length());
}
