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

// Various classes for storing Internet addresses:
//
//  * IPAddress:      An IPv4 or IPv6 address. Fundamentally represents a host
//                    (or more precisely, an network interface).
//                    Roughly analogous to a struct in_addr.
//  * SocketAddress:  A socket endpoint address (IPAddress, plus a port).
//                    Roughly analogous to a struct sockaddr_in.
//  * IPRange:        A subnet address, ie. a range of IPv4 or IPv6 addresses
//                    (IPAddress, plus a prefix length). (Would have been named
//                    IPSubnet, but that was already taken several other
//                    places.)
//  * IPAddressList:  A vector of `IPAddress`, mainly used to define a custom
//                    flag type per
//                    <link>.
//                    The actual flag takes a comma-separated list of IP
//                    addresses in string format parseable by
//                    `StringToIPAddress()`, e.g. "1.1.1.1,2::2". Empty list is
//                    parsed into an empty vector. Any error encountered in the
//                    course of parsing the list fails parsing the flag and the
//                    result is undefined.
//
// The IPAddress class explicitly does not handle mapped or compatible IPv4
// addresses specially. In particular, operator== will treat 1.2.3.4 (IPv4),
// ::1.2.3.4 (compatible IPv4 embedded in IPv6) and
// ::ffff:1.2.3.4 (mapped IPv4 embedded in IPv6) as all distinct.

#ifndef THIRD_PARTY_GLOOP_NET_BASE_IPADDRESS_H_
#define THIRD_PARTY_GLOOP_NET_BASE_IPADDRESS_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <iterator>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-W#warnings"
#include <ext/hash_map>
#pragma clang diagnostic pop

#include <algorithm>
#include <compare>
#include <ostream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/base/optimization.h"
#include "absl/base/port.h"
#include "absl/hash/hash.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/numeric/int128.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/compare.h"
#include "absl/types/span.h"
#include "gloop/base/port.h"
#include "gloop/util/endian/endian.h"
#include "gloop/util/gtl/value_or_die.h"
#include "gloop/util/tuple/components/dump_vars.h"

#ifdef _WIN32

#include <winsock2.h>

// winsock2.h must come before windows.h.

#include <windows.h>
#include <ws2ipdef.h>
#include <ws2tcpip.h>

#else  // !defined(_WIN32)

#include <netinet/in.h>
#include <sys/socket.h>

#endif  // !defined(_WIN32)

#ifdef __KAME__
#define s6_addr16 __u6_addr.__u6_addr16
#define s6_addr32 __u6_addr.__u6_addr32
#elif defined(_WIN32)
#define s6_addr16 u.Word
#endif

namespace net_base {

class IPAddress;
class SocketAddress;
class IPRange;

absl::StatusOr<IPAddress> MakeIPAddressWithScopeId(const in6_addr&, uint32_t);
absl::StatusOr<SocketAddress> MakeSocketAddressFromSockaddrIn6(
    const sockaddr_in6& sin6);
bool IsInitializedRange(const IPRange& range);

class IPAddress {
 public:
  // Default constructor. Leaves the object in an empty state.
  // The empty state is analogous to a NULL pointer; the only operations
  // that are allowed on the object are:
  //
  //   * Assignment and copy construction.
  //   * Checking for the empty state (address_family() will return AF_UNSPEC,
  //     or equivalently, the helper function IsInitializedAddress() will
  //     return false).
  //   * Comparison (operator== and operator!=).
  //   * Logging (AbslStringify(), operator<<)
  //   * Ordering
  //   * Hashing
  //
  // In particular, no guarantees are made about the behavior of the
  // IPv4/IPv6 conversion accessors, of string conversions and
  // serialization, of IsAnyIPAddress() and friends.
  //
  // Also see the default constructor for SocketAddress, below.
  IPAddress() = default;

  // Constructors from standard BSD socket address structures.
  // For conversions from uint32_t (for legacy Google3 code) and from strings,
  // see under "Free utility functions", below.
  constexpr explicit IPAddress(const in_addr& addr) : address_(addr) {}
  explicit IPAddress(const in6_addr& addr) : IPAddress(addr, 0U) {}

  // Accessors. Note that these return their value directly even when it
  // is not a primitive type, as this was deemed more logical, and with
  // recent (>= 4.1) gcc is just as fast as using an output parameter.

  // The address family; either AF_UNSPEC, AF_INET or AF_INET6.
  int address_family() const {
    switch (address_.type()) {
      case Variant::Type::kUninitialized:
        return AF_UNSPEC;

      case Variant::Type::kIpv4:
        return AF_INET;

      case Variant::Type::kIpv6:
        return AF_INET6;
    }

    ABSL_UNREACHABLE();
  }

  // The address as an in_addr structure; CHECK-fails if address_family() is
  // not AF_INET (ie. the held address is not an IPv4 address).
  in_addr ipv4_address() const {
    return ABSL_PREDICT_TRUE(is_ipv4()) ? address_.get_ipv4()
                                        : ipv4_address_slowpath();
  }

  // The address as an in6_addr structure; CHECK-fails if address_family() is
  // not AF_INET6 (ie. the held address is not an IPv6 address).
  in6_addr ipv6_address() const {
    return ABSL_PREDICT_TRUE(address_.type() == Variant::Type::kIpv6 &&
                             !address_.has_scope())
               ? address_.get_ipv6()
               : ipv6_address_slowpath();
  }

  // Convenient helpers that return true if IP address is v4 or v6 respectively.
  bool is_ipv4() const { return address_.type() == Variant::Type::kIpv4; }
  bool is_ipv6() const { return address_.type() == Variant::Type::kIpv6; }

  // Returns the scope_id if this is an IPv6 link-local address with a
  // compactly stored scope_id; 0U otherwise. An IPv6 link-local address
  // may not have had a scope_id assigned; in this case 0U is also returned.
  uint32_t scope_id() const {
    return address_.has_scope()
               ? BigEndian::Load32(address_.get_ipv6().s6_addr16 + 2)
               : 0U;
  }

  // The address in string form, as returned by inet_ntop(). In particular,
  // this means that IPv6 addresses may be subject to zero compression
  // (e.g. "2001:700:300:1800::f" instead of "2001:700:300:1800:0:0:0:f").
  //
  // NOTE: Before you send a CL asking that ToString() on an
  // uninitialized IPAddress should not CHECK-fail in debug mode,
  // please consider:
  //
  //   1. There's a guarantee that StringToIPAddress can parse anything
  //      ToString() outputs, and one might not want to make
  //      StringToIPAddress("") return true.
  //   2. If you just want to log an IPAddress, do not use ToString();
  //      simply do LOG(INFO) << addr, which can handle empty IPAddress
  //      objects just fine.
  std::string ToString() const;

  // Returns the same as ToString().
  // <buffer> must have room for at least INET6_ADDRSTRLEN bytes,
  // including the final NUL.
  // Returns the pointer to the terminating NUL.
  char* ToCharBuf(char* buffer) const;

  // Returns the address as a sequence of bytes in network-byte-order.
  // This is suitable for writing onto the wire or into a protocol buffer
  // as a string (proto1 syntax) or bytes (proto2 syntax).
  // IPv4 will be 4 bytes. IPv6 will be 16 bytes.
  // Can be parsed using PackedStringToIPAddress().
  std::string ToPackedString() const;

  // Static, constant IPAddresses for convenience.
  static IPAddress Any4();       // 0.0.0.0
  static IPAddress Loopback4();  // 127.0.0.1

  static IPAddress Any6();       // ::
  static IPAddress Loopback6();  // ::1

  bool operator==(const IPAddress& other) const {
    const IPAddress::Variant& a = address_;
    const IPAddress::Variant& b = other.address_;

    if (a.type() != b.type()) {
      return false;
    }

    switch (a.type()) {
      case IPAddress::Variant::Type::kIpv4:
        return a.get_ipv4().s_addr == b.get_ipv4().s_addr;

      case IPAddress::Variant::Type::kIpv6:
        return a.has_scope() == b.has_scope() &&
               std::equal(a.get_ipv6().s6_addr16,
                          std::end(a.get_ipv6().s6_addr16),
                          b.get_ipv6().s6_addr16);

      case IPAddress::Variant::Type::kUninitialized:
        // We've already verified that they've got the same address family, and
        // the only possibility at this point is AF_UNSPEC, which are all equal.
        return true;
    }

    ABSL_UNREACHABLE();
  }

#if __cplusplus >= 202002L
  // The ordering defined by this operator is: First uninitialized
  // addresses, then all IPv4 addresses, then all IPv6
  // addresses. Internally, the addresses are ordered lexically by
  // network byte order (so they go 0.0.0.0, 0.0.0.1, 0.0.0.2, etc.).
  //
  // IPv6 link-local addresses with non-zero scope_id are sorted by scope_id
  // after their scope_id-zero cousins, but by address when compared to other
  // IPv6 addresses. For example,
  //
  //   ::0 < ::1 < 2001:db8:: < fe80:: < fe80::%1 < fe80::1 < ff02:: < ff02::%1
  friend std::weak_ordering operator<=>(const IPAddress& lhs,
                                        const IPAddress& rhs) {
    return ThreeWayCompare(lhs, rhs);
  }
#else
  // Before C++20 we can't provide a <=> operator.
  friend bool operator<(const IPAddress& lhs, const IPAddress& rhs) {
    return ThreeWayCompare(lhs, rhs) == absl::weak_ordering::less;
  }
#endif

  // Although in C++20 the compiler can use == to implement !=, we need to
  // teach CLIF, which doesn't support the C++20 feature correctly as of 2025-08
  // (http://yaqs/3372005349807620096).
  bool operator!=(const IPAddress& other) const { return !(*this == other); }

  // This is a hot function, so give it access to internals.
  friend bool IsAnyIPAddress(const IPAddress& ip);

  // POD type, so no DISALLOW_COPY_AND_ASSIGN:
  IPAddress(const IPAddress&) = default;
  IPAddress(IPAddress&&) = default;
  IPAddress& operator=(const IPAddress&) = default;
  IPAddress& operator=(IPAddress&&) = default;

  template <typename H>
  friend H AbslHashValue(H h, const IPAddress& ip) {
    switch (ip.address_.type()) {
      case IPAddress::Variant::Type::kIpv4:
        return H::combine(std::move(h), ip.address_.get_ipv4().s_addr, AF_INET);

      case IPAddress::Variant::Type::kIpv6:
        return H::combine(H::combine_contiguous(
                              std::move(h), ip.address_.get_ipv6().s6_addr16,
                              std::size(ip.address_.get_ipv6().s6_addr16)),
                          AF_INET6, ip.address_.has_scope());

      case IPAddress::Variant::Type::kUninitialized:
        // NOTE; added a zero to ensure the hashing unit tests pass.
        return H::combine(std::move(h), 0, AF_UNSPEC);
    }

    ABSL_UNREACHABLE();
  }

  // Expose a Hash() member method to make this type hashable in pyclif.
  size_t Hash() const { return absl::Hash<IPAddress>()(*this); }

  // For debugging/logging. Note that as a special case, you can log an
  // uninitialized IP address, although you cannot use ToString() on it.
  template <typename Sink>
  friend void AbslStringify(Sink& sink, const IPAddress& address) {
    switch (address.address_.type()) {
      case IPAddress::Variant::Type::kIpv4:
      case IPAddress::Variant::Type::kIpv6:
        sink.Append(address.ToString());
        return;

      case IPAddress::Variant::Type::kUninitialized:
        sink.Append("<uninitialized IPAddress>");
        return;
    }

    ABSL_UNREACHABLE();
  }

  // For debugging/logging. Note that as a special case, you can log an
  // uninitialized IP address, although you cannot use ToString() on it.
  friend std::ostream& operator<<(std::ostream& stream,
                                  const IPAddress& address) {
    switch (address.address_family()) {
      case AF_INET:
      case AF_INET6:
        return stream << address.ToString();
      case AF_UNSPEC:
        return stream << "<uninitialized IPAddress>";
      default:
        return stream << "<corrupt IPAddress with family="
                      << address.address_family() << ">";
    }
  }

  friend absl::StatusOr<IPAddress> MakeIPAddressWithScopeId(const in6_addr&,
                                                            uint32_t);

 private:
  friend absl::weak_ordering ThreeWayCompare(const IPAddress& lhs,
                                             const IPAddress& rhs);

  // Returns true if the given address is one that can make use of scope_ids;
  // false otherwise.
  //
  // Currently only IPv6 link-local unicast and link-local multicast addresses
  // fit this description. In the future, though, the IPv4 link-local unicast
  // range (169.254.0.0/16; RFC 3927) could be considered to use scope_ids.
  static bool MayUseScopeIds(const in6_addr& in6) {
    return (IN6_IS_ADDR_LINKLOCAL(&in6) || IN6_IS_ADDR_MC_LINKLOCAL(&in6));
  }

  // A much stricter test for whether in6 is a candidate for the kind of
  // scope_id compaction implemented here (cf. IPAddressMayUseScopeIds()).
  static bool MayUseCompactScopeIds(const in6_addr& in6) {
    return (in6.s6_addr16[0] == htons(0xfe80) ||
            in6.s6_addr16[0] == htons(0xff02)) &&
           in6.s6_addr16[1] == 0;
  }

  // Test for whether in6 may safely, compactly store a scope_id.
  static bool MayStoreCompactScopeId(const in6_addr& in6) {
    return MayUseCompactScopeIds(in6) && in6.s6_addr16[2] == 0 &&
           in6.s6_addr16[3] == 0;
  }

  // Test for whether in6 appears to have a compact scope_id stored.
  static bool HasCompactScopeId(const in6_addr& in6) {
    return MayUseCompactScopeIds(in6) &&
           (in6.s6_addr16[2] != 0 || in6.s6_addr16[3] != 0);
  }

  // Constructor that also supports an IPv6 link-local address with a scope_id.
  IPAddress(const in6_addr& addr, const uint32_t scope_id)
      : address_(addr, scope_id != 0 && MayUseCompactScopeIds(addr)) {
    if (ABSL_PREDICT_FALSE(scope_id != 0)) {
      if (MayUseCompactScopeIds(address_.get_ipv6())) {
        BigEndian::Store32(address_.get_ipv6().s6_addr16 + 2, scope_id);
      } else if (MayUseScopeIds(address_.get_ipv6())) {
        LOG(WARNING) << "Discarding scope_id; cannot be compactly stored.";
      }
    }
  }

  in_addr ipv4_address_slowpath() const;
  in6_addr ipv6_address_slowpath() const;

  // A type-safe variant that contains nothing, an in_addr, or an in6_addr.
  //
  // We can't use std::variant for this because it doesn't allow us to reuse its
  // tail padding in SocketAddress and IPRange. See also
  // https://stackoverflow.com/q/79318542/1505451.
  class Variant final {
   public:
    enum class Type : uint8_t {
      kUninitialized,
      kIpv4,
      kIpv6,
    };

    Variant() = default;

    constexpr explicit Variant(const in_addr& a)
        : addr_(a), type_(Type::kIpv4) {}

    constexpr explicit Variant(const in6_addr& a)
        : addr_(a), type_(Type::kIpv6) {}

    constexpr Variant(const in6_addr& a, bool has_scope)
        : addr_(a), type_(Type::kIpv6), has_scope_(has_scope) {}

    Type type() const { return type_; }
    bool has_scope() const { return has_scope_; }

    // REQUIRES: type() == kIpv4
    const in_addr& get_ipv4() const;

    // REQUIRES: type() == kIpv6
    in6_addr& get_ipv6();
    const in6_addr& get_ipv6() const;

   private:
    union Addr {
      Addr() {}
      constexpr explicit Addr(const in_addr& a4) : addr4(a4) {}
      constexpr explicit Addr(const in6_addr& a6) : addr6(a6) {}

      in_addr addr4;
      in6_addr addr6;
    } addr_;

    Type type_ = Type::kUninitialized;
    bool has_scope_ = false;
  };

  // Allow other classes containing an IPAddress, like SocketAddress, to use the
  // IP address tail padding for other members.
  ABSL_ATTRIBUTE_NO_UNIQUE_ADDRESS Variant address_;
};

class SocketAddress {
 public:
  // Default constructor. Leaves the object in an empty state.
  // The empty state is analogous to a NULL pointer; the only operations
  // that are allowed on the object are:
  //
  //   * Assignment and copy construction.
  //   * Checking for the empty state (host() will return an
  //     empty IPAddress object, or equivalently, the helper function
  //     IsInitializedSocketAddress() will return false).
  //   * Comparison (operator== and operator!=).
  //   * Logging (AbslStringify(), operator<<)
  //   * Ordering
  //   * Hashing
  //
  // In particular, no guarantees are made about the behavior of the IPv4/IPv6
  // conversion accessors, of string conversions and serialization, of ordering,
  // of hashing, or of the port() accessor.
  //
  // Note that you can also arrive at the empty state by construction
  // from a struct sockaddr, below.
  SocketAddress() = default;

  // Constructor with IP address and port (in host byte order).
  SocketAddress(const IPAddress& host, const uint16_t port)
      : host_(host),
        port_{
            // Ensure that the port is always zero for uninitialized socket
            // addresses, so that operator== correctly treats all uninitialized
            // addresses as equal just like ThreeWayCompare.
            //
            // The user can't tell the difference because the port accessor is
            // not allowed to be called in this case.
            host_.address_family() != AF_UNSPEC ? port
                                                : static_cast<uint16_t>(0),
        } {}

  // Constructors from standard BSD socket address structures.
  explicit SocketAddress(const struct sockaddr_in& sin)
      : SocketAddress{
            IPAddress(sin.sin_addr),
            gntohs(sin.sin_port),
        } {
    CHECK_EQ(AF_INET, sin.sin_family);
  }

  // Like MakeSocketAddressFromSockaddrIn6, but crashes on invalid input. Do not
  // use this in new code.
  ABSL_DEPRECATE_AND_INLINE()
  explicit SocketAddress(const struct sockaddr_in6& sin6)
      : SocketAddress{gtl::ValueOrDie(MakeSocketAddressFromSockaddrIn6(sin6))} {
  }

  // The following constructors are not recommended for most users, because
  // storing raw ::ffff:0.0.0.0/96 addresses may lead to cumbersome interactions
  // with common google3 libraries.  Instead, consider passing sockaddr_* types
  // directly to NormalizeSocketAddress(), where IPv4-mapped IPv6 addresses will
  // be converted to a plain IPv4 format.
  explicit SocketAddress(const struct sockaddr& saddr);

  // Delegate to the above constructor.
  explicit SocketAddress(const struct sockaddr_storage& saddr)
      : SocketAddress(reinterpret_cast<const struct sockaddr&>(saddr)) {}

  // For conversion from a string, see under "Free utility functions",
  // below.

  // Accessors. Note that these return their value directly even when it
  // is not a primitive type, as this was deemed more logical, and with
  // recent (>= 4.1) gcc is just as fast as using an output parameter.

  // The individual parts of the subnet.
  const IPAddress& host() const { return host_; }

  // Port in host byte order.
  uint16_t port() const {
    if (ABSL_PREDICT_FALSE(host().address_family() == AF_UNSPEC)) {
      LOG(DFATAL) << "Trying to take port() of an empty SocketAddress";
    }

    return port_;
  }

  // A string representation of the address.
  //
  // For IPv4 addresses, this is "host:port" (e.g. "127.0.0.1:80").
  // For IPv6 addresses, this is "[host]:port" (e.g. "[::1]:80").
  //
  // In an attempt to avoid changing existing API behavior and assumptions,
  // IPv6 addresses that include a scope id (e.g. "%iface0") will NOT emit that
  // scope id in the string returned by this method.
  //
  // NOTE: Before you send a CL asking that ToString() on an
  // uninitialized SocketAddress should not CHECK-fail in debug mode,
  // please consider:
  //
  //   1. There's a guarantee that StringToSocketAddress can parse anything
  //      ToString() outputs, and one might not want to make
  //      StringToSocketAddress("") return true.
  //   2. If you just want to log a SocketAddress, do not use ToString();
  //      simply do LOG(INFO) << addr, which can handle empty SocketAddress
  //      objects just fine.
  std::string ToString() const;

  // A string representation of the address.
  //
  // For IPv4 addresses, this is "host:port" (e.g. "127.0.0.1:80").
  // For most IPv6 addresses, this is "[host]:port" (e.g. "[::1]:80").
  // For link-local IPv6 addresses with a non-zero scope ID, this is
  // "[host%scope_id]:port" (e.g. "[fe80::1%3]:80").
  std::string ToStringWithScopeId() const;

  // Returns the address as a sequence of bytes in network-byte-order,
  // IPAddress first, then port. This is suitable for writing onto the
  // wire or into a protocol buffer as a string (proto1 syntax) or
  // bytes (proto2 syntax). IPv4 will be 6 bytes. IPv6 will be 18
  // bytes. Can be parsed using PackedStringToSocketAddress().
  std::string ToPackedString() const;

  // The socket address as a sockaddr_in structure; CHECK-fails if
  // host().address_family() is not AF_INET (ie. the held address is
  // not an IPv4 address).
  sockaddr_in ipv4_address() const {
    sockaddr_in ret;
    memset(&ret, 0, sizeof(ret));
    ret.sin_family = AF_INET;
    ret.sin_addr = host_.ipv4_address();
    ret.sin_port = htons(port_);
    return ret;
  }

  // The socket address as a sockaddr_in6 structure; CHECK-fails if
  // host().address_family() is not AF_INET6 (ie. the held address is
  // not an IPv6 address).
  sockaddr_in6 ipv6_address() const {
    sockaddr_in6 ret;
    memset(&ret, 0, sizeof(ret));
    ret.sin6_family = AF_INET6;
    ret.sin6_addr = host_.ipv6_address();
    ret.sin6_port = htons(port_);
    ret.sin6_scope_id = host_.scope_id();
    return ret;
  }

  // Returns the socket address as a sockaddr_storage structure, with sa_family
  // matching the held address_family.  CHECK-fails if the family is not AF_INET
  // or AF_INET6.
  //
  // When interfacing with the kernel, you should typically prefer
  // SocketAddressToFamily(), as it helps avoid address family mismatches.
  sockaddr_storage generic_address() const;

  bool operator==(const SocketAddress& other) const {
    // Compare the port first, as that is cheaper and if we can short-circuit
    // due to port mismatch our overall cost will be lower.
    return std::forward_as_tuple(port_, host_) ==
           std::forward_as_tuple(other.port_, other.host_);
  }

// Before C++20 we have to teach the compiler how to use == to implement !=.
#if __cplusplus < 202002L
  bool operator!=(const SocketAddress& other) const {
    return !(*this == other);
  }
#endif

#if __cplusplus >= 202002L
  // Socket addresses are lexicographically ordered on (host, port), except that
  // the uninitialized address comes before all others.
  friend std::weak_ordering operator<=>(const SocketAddress& lhs,
                                        const SocketAddress& rhs) {
    return ThreeWayCompare(lhs, rhs);
  }
#else
  // Before C++20 we can't provide a <=> operator.
  friend bool operator<(const SocketAddress& lhs, const SocketAddress& rhs) {
    return ThreeWayCompare(lhs, rhs) == absl::weak_ordering::less;
  }
#endif

  // POD type, so no DISALLOW_COPY_AND_ASSIGN:
  SocketAddress(const SocketAddress&) = default;
  SocketAddress(SocketAddress&&) = default;
  SocketAddress& operator=(const SocketAddress&) = default;
  SocketAddress& operator=(SocketAddress&&) = default;

  template <typename H>
  friend H AbslHashValue(H h, const SocketAddress& address) {
    return H::combine(std::move(h), address.host_, address.port_);
  }

  // Expose a Hash() member method to make this type hashable in pyclif.
  size_t Hash() const { return absl::Hash<SocketAddress>()(*this); }

  // For debugging/logging. Note that as a special case, you can log an
  // uninitialized socket address, although you cannot use ToString() on it.
  template <typename Sink>
  friend void AbslStringify(Sink& sink, const SocketAddress& address) {
    if (address.host().address_family() == AF_UNSPEC) {
      sink.Append("<uninitialized SocketAddress>");
      return;
    }
    sink.Append(address.ToString());
  }

  // For debugging/logging. Note that as a special case, you can log an
  // uninitialized socket address, although you cannot use ToString() on it.
  friend std::ostream& operator<<(std::ostream& stream,
                                  const SocketAddress& address) {
    if (address.host().address_family() == AF_UNSPEC) {
      return stream << "<uninitialized SocketAddress>";
    }
    return stream << address.ToString();
  }

 private:
  friend absl::weak_ordering ThreeWayCompare(const SocketAddress& lhs,
                                             const SocketAddress& rhs);

  // Use [[no_unique_address]] to allow the port to occupy the tail padding of
  // the host.
  ABSL_ATTRIBUTE_NO_UNIQUE_ADDRESS IPAddress host_;
  uint16_t port_ = 0;
};

namespace ipaddress_internal {

// Truncate any IPv4, IPv6, or empty IPAddress to the specified length.
// If *length_io exceeds the number of bits in the address family, then it
// will be overwritten with the correct value.  Normal addresses will
// CHECK-fail if the length is negative, but empty addresses ignore the
// length and write -1.
//
// This is not for external use; see TruncateIPAddress() instead.
IPAddress TruncateIPAndLength(const IPAddress& addr, int* length_io);

// A templated Formatter for use with the strings::Join API to print
// collections of IPAddresses, SocketAddresses, or IPRanges (or anything
// with a suitable ToString() method).  See also //gloop/strings/join.h.
// TODO: Replace with calls to something better in //gloop/strings:join,
// once something better is available.
template <typename T>
struct ToStringJoinFormatter {
  void operator()(std::string* out, const T& t) const {
    out->append(t.ToString());
  }
};

}  // namespace ipaddress_internal

// Forward declaration. See definition below.
int IPAddressLength(const IPAddress& ip);

class IPRange {
 public:
  // Default constructor. Leaves the object in an empty state.
  // The empty state is analogous to a NULL pointer; the only operations
  // that are allowed on the object are:
  //
  //   * Assignment and copy construction.
  //   * Checking for the empty state (IsInitializedRange() will return false).
  //   * Comparison (operator== and operator!=).
  //   * Logging (AbslStringify(), operator<<)
  //   * Ordering
  //   * Hashing
  //
  // In particular, no guarantees are made about the behavior of the
  // of string conversions and serialization, or any other accessors.
  IPRange() = default;

  // Constructs an IPRange from an address and a length. Properly zeroes out
  // bits and adjusts length as required, but CHECK-fails on negative lengths
  // (since that is inherently nonsensical). Typical examples:
  //
  //   129.240.2.3/10 => 129.192.0.0/10
  //   2001:700:300:1800::/48 => 2001:700:300::/48
  //
  //   127.0.0.1/33 => 127.0.0.1/32
  //   ::1/129 => ::1/128
  //
  //   IPAddress()/* => empty IPRange()
  //
  //   127.0.0.1/-1 => undefined (currently CHECK-fail)
  //   ::1/-1 => undefined (currently CHECK-fail)
  //
  IPRange(const IPAddress& host, int length)
      : host_(ipaddress_internal::TruncateIPAndLength(host, &length)),
        length_(length) {}

  // Unsafe constructor from a host and prefix length.
  //
  // This is the fastest way to construct an IPRange, but the caller must
  // ensure that all inputs are strictly validated:
  //   - IPv4 host must have length 0..32
  //   - IPv6 host must have length 0..128
  //   - The host must be cleanly truncated, i.e. there must not be any bits
  //     set beyond the prefix length.
  //   - Uninitialized IPAddress() must have length -1
  //
  // For performance reasons, these constraints are only checked in debug mode.
  // Any violations will result in undefined behavior.  Callers who cannot
  // guarantee correctness should use IPRange(host, length) instead.
  static IPRange UnsafeConstruct(const IPAddress& host, int length) {
    return IPRange(host, length, /* dummy = */ 0);
  }

  // Construct an IPRange from just an IPAddress, applying the
  // address-family-specific maximum netmask length.
  explicit IPRange(const IPAddress& host)
      : IPRange{
            // If the host is initialized we use the constructor above.
            // Otherwise we are also uninitialized.
            host.address_family() != AF_UNSPEC
                ? IPRange(host, IPAddressLength(host))
                : IPRange(),
        } {}

  // For conversion from a string, see under "Free utility functions",
  // below.

  // Accessors. Note that these return their value directly even when it
  // is not a primitive type, as this was deemed more logical, and with
  // recent (>= 4.1) gcc is just as fast as using an output parameter.

  // The individual parts of the subnet.
  IPAddress host() const { return host_; }
  int length() const { return length_; }

  // The bounding IPAddresses in this IPRange.  The "network address"
  // is the "all zeroes" address, or lower bound.  The "broadcast address"
  // is the "all ones" address, or upper bound.
  //
  // Simple example:
  //   IPRange range;
  //   CHECK(StringToIPRangeAndTruncate("10.1.1.0/24", &range));
  //   range.network_address().ToString();    // returns "10.1.1.0"
  //   range.broadcast_address().ToString();  // returns "10.1.1.255"
  IPAddress network_address() const;
  IPAddress broadcast_address() const;

  bool operator==(const IPRange& other) const {
    // Compare the length first, as that is cheaper.
    return std::forward_as_tuple(length_, host_) ==
           std::forward_as_tuple(other.length_, other.host_);
  }

  // Although in C++20 the compiler can use == to implement !=, we need to
  // teach CLIF, which doesn't support the C++20 feature correctly as of 2025-08
  // (http://yaqs/3372005349807620096).
  bool operator!=(const IPRange& other) const { return !(*this == other); }

#if __cplusplus >= 202002L
  // IP ranges are lexicographically ordered on (host, length), except that the
  // uninitialized range comes before all others.
  friend std::weak_ordering operator<=>(const IPRange& lhs,
                                        const IPRange& rhs) {
    return ThreeWayCompare(lhs, rhs);
  }
#else
  // Before C++20 we can't provide a <=> operator.
  friend bool operator<(const IPRange& lhs, const IPRange& rhs) {
    return ThreeWayCompare(lhs, rhs) == absl::weak_ordering::less;
  }
#endif

  // A string representation of the subnet, in "host/length" format.
  // Examples would be "127.0.0.0/8" or "2001:700:300:1800::/64".
  std::string ToString() const {
    return absl::StrCat(host_.ToString(), "/", length());
  }

  // Convert an IPRange into a sequence of bytes suitable for writing to a
  // protocol buffer as a string (proto1 syntax) or bytes (proto2 syntax).
  // Any bits beyond the prefix length will be truncated.
  //
  // This will crash with LOG(FATAL) if the IPRange is uninitialized.
  //
  // The address family and prefix length are stored in the first byte, with
  // values [0..128] assigned to IPv6 and [200..232] assigned to IPv4.  The
  // remaining bytes contain the address, with all trailing zeroes omitted.
  //
  // This table shows the maximum encoded size for some example prefix lengths.
  // IPv4 and IPv6 follow the same formula.
  //
  //   Prefix length (bits)  Maximum output length (bytes)
  //   --------------------  -----------------------------
  //   /0                    1 + 0 = 1
  //   /17 .. /24            1 + 3 = 4
  //   /25 .. /32            1 + 4 = 5
  //   /41 .. /48            1 + 6 = 7
  //   /57 .. /64            1 + 8 = 9
  //   /N                    1 + ceil(N/8)
  std::string ToPackedString() const;

  // Convenience ranges, representing every IPv4 or IPv6 address.
  static IPRange Any4() {
    return IPRange::UnsafeConstruct(IPAddress::Any4(), 0);  // 0.0.0.0/0
  }
  static IPRange Any6() {
    return IPRange::UnsafeConstruct(IPAddress::Any6(), 0);  // ::/0
  }

  // POD type, so no DISALLOW_COPY_AND_ASSIGN:
  IPRange(const IPRange&) = default;
  IPRange(IPRange&&) = default;
  IPRange& operator=(const IPRange&) = default;
  IPRange& operator=(IPRange&&) = default;

  template <typename H>
  friend H AbslHashValue(H h, const IPRange& address) {
    return H::combine(std::move(h), address.host_, address.length_);
  }

  // Expose a Hash() member method to make this type hashable in pyclif.
  size_t Hash() const { return absl::Hash<IPRange>()(*this); }

  // For debugging/logging.
  template <typename Sink>
  friend void AbslStringify(Sink& sink, const IPRange& range) {
    if (!IsInitializedRange(range)) {
      sink.Append("<uninitialized IPRange>");
      return;
    }
    sink.Append(range.ToString());
  }

  // For debugging/logging.
  friend std::ostream& operator<<(std::ostream& stream, const IPRange& range) {
    if (!IsInitializedRange(range)) {
      return stream << "<uninitialized IPRange>";
    }
    return stream << range.ToString();
  }

 private:
  friend absl::weak_ordering ThreeWayCompare(const IPRange& lhs,
                                             const IPRange& rhs);

  // Internal implementation of UnsafeConstruct().
  IPRange(const IPAddress& host, int length, int)
      : host_(host), length_(length) {
    DCHECK_EQ(this->host(),
              ipaddress_internal::TruncateIPAndLength(host, &length))
        << "Host has bits set beyond the prefix length.";
    DCHECK_EQ(this->length(), length)
        << "Length is inconsistent with address family.";
  }

  // Use [[no_unique_address]] to allow the length to occupy the tail padding of
  // the host.
  ABSL_ATTRIBUTE_NO_UNIQUE_ADDRESS IPAddress host_;
  int16_t length_ = -1;
};

// Define the redundant comparison operators for pre-C++20 toolchains.
#if __cplusplus < 202002L
inline bool operator<=(const IPAddress& a, const IPAddress& b) {
  return !(b < a);
}
inline bool operator>=(const IPAddress& a, const IPAddress& b) {
  return b <= a;
}
inline bool operator>(const IPAddress& a, const IPAddress& b) { return b < a; }

inline bool operator<=(const SocketAddress& a, const SocketAddress& b) {
  return !(b < a);
}
inline bool operator>=(const SocketAddress& a, const SocketAddress& b) {
  return b <= a;
}
inline bool operator>(const SocketAddress& a, const SocketAddress& b) {
  return b < a;
}

inline bool operator<=(const IPRange& a, const IPRange& b) { return !(b < a); }
inline bool operator>=(const IPRange& a, const IPRange& b) { return b <= a; }
inline bool operator>(const IPRange& a, const IPRange& b) { return b < a; }
#endif

// Free utility functions for IPAddress.

// Convert a network byte order uint32_t into an IPv4 IPAddress.
//
// Warning! The input |bytes| must be in network byte order (big endian), so use
// carefully. Maybe you really want HostUInt32ToIPAddress(bytes) or should use
// IPAddress(in_addr{bytes}) directly. Do NOT use this function with protobufs.
// If you are not dealing with raw network data, your value is likely in host
// byte order and not suitable for this function.
//
// Example usage:
//   const iphdr* ipv4 = reinterpret_cast<iphdr*>(raw_data);
//   UInt32ToIPAddress(ipv4->daddr);
//
//   UInt32ToIPAddress(htonl(0x01020304)).ToString();  // Yields "1.2.3.4"
//
// To invert this function, read the ipv4_address().s_addr field directly.
ABSL_DEPRECATED("Use ::net_base::IPAddress(in_addr{bytes})")
inline IPAddress UInt32ToIPAddress(uint32_t bytes) {
  in_addr addr;
  addr.s_addr = bytes;
  return IPAddress(addr);
}

// Convert a host byte order uint32_t into an IPv4 IPAddress.
//
// This is the less-evil cousin of UInt32ToIPAddress.  It can be used with
// protobufs, mathematical/bitwise operations, or any other case where the
// address is represented as an ordinary number.
//
// Example usage:
//   HostUInt32ToIPAddress(0x01020304).ToString();  // Yields "1.2.3.4"
//
IPAddress HostUInt32ToIPAddress(uint32_t address);

// Convert an IPv4 IPAddress to a uint32_t in host byte order.
// This is the inverse of HostUInt32ToIPAddress().
//
// Example usage:
//   const IPAddress addr(StringToIPAddressOrDie("1.2.3.4"));
//   IPAddressToHostUInt32(addr);  // Yields 0x01020304
//
// Will CHECK-fail if addr does not contain an IPv4 address.
inline uint32_t IPAddressToHostUInt32(const IPAddress& addr) {
  return ntohl(addr.ipv4_address().s_addr);
}

// Convert a uint128 in host byte order to an IPv6 IPAddress
// (e.g., uint128(0, 1) will become "::1").
// Not a constructor, to make it easier to grep for the ugliness later.
IPAddress UInt128ToIPAddress(absl::uint128 bigint);

// Convert an IPv6 IPAddress to a uint128 in host byte order
// (e.g., "::1" will become uint128(0, 1)).
// Will CHECK-fail if addr does not contain an IPv6 address,
// so use with care, and only in low-level code.
inline absl::uint128 IPAddressToUInt128(const IPAddress& addr) {
  struct in6_addr addr6 = addr.ipv6_address();
  return absl::MakeUint128(BigEndian::Load64(addr6.s6_addr16),
                           BigEndian::Load64(addr6.s6_addr16 + 4));
}

// Factory function to try making an IPAddress with the given scope_id.
//
// This is useful when working with IPv6 link-local unicast addresses (e.g.
// "fe80::9036:ba50:bed2:b3cc%enp2s0") or link-local multicast addresses (e.g.
// "ff02::fb%wlan0"). For more info on IPv6 addresses that use scope_ids, see:
//
//     <link>
//
// If the scope_id is zero, this is the same as calling the constructor for
// IPAddress(const in6_addr&).
//
// If the scope_id is non-zero then safety checks are performed to make sure
// that the given address uses scope_ids (see above) and that the address can
// be used with the current compaction scheme. An error is returned if any of
// these checks fails.
absl::StatusOr<IPAddress> MakeIPAddressWithScopeId(const in6_addr& addr,
                                                   uint32_t scope_id);
absl::StatusOr<IPAddress> MakeIPAddressWithScopeId(const in6_addr& addr,
                                                   absl::string_view ifname);

// Attempt to build a socket address from the supplied struct. This may fail if
// the struct specifies an invalid scope ID.
absl::StatusOr<SocketAddress> MakeSocketAddressFromSockaddrIn6(
    const sockaddr_in6& sin6);

// Parse an IPv4 or IPv6 address in textual form to an IPAddress.
// Not a constructor since it can fail (in which case it returns false,
// and the contents of "out" is undefined). If only validation is required,
// "out" can be set to nullptr.
//
// The input argument can be in whatever form inet_pton(AF_INET, ...) or
// inet_pton(AF_INET6, ...) accepts (ie. typically something like "127.0.0.1"
// or "2001:700:300:1800::f").
//
// Note that in particular, this function does not do DNS lookup.
//
ABSL_MUST_USE_RESULT bool StringToIPAddress(absl::string_view str,
                                            IPAddress* out);

// Parse an IPv4 or IPv6 address in textual form to an IPAddress.
//
// This difference between this function and others is that this function
// additionally understands IPv6 addresses with scope identifiers and can return
// properly scoped IP addresses (see MakeIPAddressWithScopeId() above).
//
// Most platforms can use either numerical interface indices or interface names
// as scope identifiers, but Windows supports only numerical indices.
absl::StatusOr<IPAddress> StringToIPAddressWithOptionalScope(
    absl::string_view str);

// StringToIPAddress conversion methods that CHECK()-fail on invalid input.
// Not a good idea to use on user-provided input.
inline IPAddress StringToIPAddressOrDie(const char* absl_nonnull str) {
  IPAddress ip;
  CHECK(StringToIPAddress(str, &ip)) << "Invalid IP " << str;
  return ip;
}

inline IPAddress StringToIPAddressOrDie(absl::string_view str) {
  return StringToIPAddressOrDie(std::string(str).c_str());
}

// Parse a "binary" or packed string containing an IPv4 or IPv6 address in
// non-textual, network-byte-order form to an IPAddress.  Not a constructor
// since it can fail (in which case it returns false, and the contents of
// "out" is undefined). If only validation is required, "out" can be set to
// nullptr.
ABSL_MUST_USE_RESULT bool PackedStringToIPAddress(absl::string_view str,
                                                  IPAddress* out);
ABSL_MUST_USE_RESULT inline bool PackedStringToIPAddress(const char* str,
                                                         size_t len,
                                                         IPAddress* out) {
  return PackedStringToIPAddress(absl::string_view(str, len), out);
}

// Binary packed string conversion methods that CHECK()-fail on invalid input.
inline IPAddress PackedStringToIPAddressOrDie(absl::string_view str) {
  IPAddress ip;
  CHECK(PackedStringToIPAddress(str, &ip))
      << "Invalid packed IP address of length " << str.length();
  return ip;
}
inline IPAddress PackedStringToIPAddressOrDie(const char* str, size_t len) {
  return PackedStringToIPAddressOrDie(absl::string_view(str, len));
}

// Parse a "binary" or packed string containing an IPv4 or IPv6
// address and port in non-textual, network-byte-order form to a
// SocketAddress. Not a constructor since it can fail (in which case
// it returns false, and the contents of "out" is undefined). If only
// validation is required, "out" can be set to nullptr.
ABSL_MUST_USE_RESULT bool PackedStringToSocketAddress(absl::string_view str,
                                                      SocketAddress* out);

// Binary packed string conversion method that CHECK()-fails on invalid input.
inline SocketAddress PackedStringToSocketAddressOrDie(absl::string_view str) {
  SocketAddress addr;
  CHECK(PackedStringToSocketAddress(str, &addr))
      << "Invalid packed socket address of length " << str.length();
  return addr;
}

// A free function to parse hexadecimal strings like
// "fe80000000000000000573fffea00065" in to an IPv6 IPAddress.  @ip6 may be
// NULL, in which case the function still behaves as a boolean test for
// the validity of a given string being converted to an IPv6 IPAddress.
bool ColonlessHexToIPv6Address(absl::string_view hex_str, IPAddress* ip6);

// Same as IPAddress::ToString(), unless the address is an IPv6 link-local
// address with a non-zero scope_id in which case "%scope_id" is appended
// (a la https://tools.ietf.org/html/rfc4007#section-11).
std::string IPAddressToStringWithScopeId(const IPAddress& ip);

// Same as IPAddressToStringWithScopeId() except scope_id-to-interface-name
// resolution is attempted. If no interface name can be found for the given
// scope_id the value of IPAddressToStringWithScopeId() is returned.
std::string IPAddressToStringWithInterfaceName(const IPAddress& ip);

// Free function to return a version of the address in string form
// (a la IPAddress::ToString()) which additionally conforms to URI
// guidelines, e.g. RFC 3986 section 3.2.2.  Specifically, IPv4
// addresses remain unchanged while IPv6 addresses are encapsulated
// within '[' and ']'.
std::string IPAddressToURIString(const IPAddress& ip);

// Free function to output the PTR representation of an IPAddress.
// IPv4 and IPv6 addresses are output in their in-addr.arpa and
// ip6.arpa formats respectively.
std::string IPAddressToPTRString(const IPAddress& ip);

// Free function to return an IPAddress from the corresponding PTR
// representation, e.g. the inverse of IPAddressToPtrString.
ABSL_MUST_USE_RESULT bool PTRStringToIPAddress(absl::string_view ptr_address,
                                               IPAddress* out);

// Specific JoinFormatters for IPAddress, SocketAddress, and IPRange.
typedef ipaddress_internal::ToStringJoinFormatter<IPAddress>
    IPAddressJoinFormatter;
typedef ipaddress_internal::ToStringJoinFormatter<SocketAddress>
    SocketAddressJoinFormatter;
typedef ipaddress_internal::ToStringJoinFormatter<IPRange> IPRangeJoinFormatter;

// Boolean convenience checks.

// Return true if `ip` is 0.0.0.0 or ::. Returns false if `ip` is any other
// IPv4 or IPv6 address. Otherwise the behavior is undefined.  The "`Any`" in
// the function name refers to `INADDR_ANY` (0.0.0.0) and `in6addr_any` (::).
bool IsAnyIPAddress(const IPAddress& ip);

// Return true if the address is ipv4 and in the multicast range according to
// https://www.rfc-editor.org/rfc/rfc1112.html#section-4.
bool IsV4MulticastIPAddress(const IPAddress& ip);

// Return true if `ip` is 127.0.0.1 or ::1. Returns false if `ip` is any other
// IPv4 or IPv6 address. Otherwise the behavior is undefined.
//
// Note that this function returns false for all addresses in the 127.0.0.0/8
// subnet except for 127.0.0.1, even though every address in that subnet is a
// loopback address. For this reason, most people should probably be calling
// IsLoopbackIPAddress() instead.
bool IsCanonicalLoopbackIPAddress(const IPAddress& ip);

// Return true if `ip` is in the 127.0.0.0/8 range, if it is ::1, or if it
// is in the fd14:988a:50ee:1006::/64 range (this range is meant to be the
// Google equivalent of 127/8 but for IPv6).
// Returns false if `ip` is any other IPv4 or IPv6 address. Otherwise the
// behavior is undefined.
bool IsLoopbackIPAddress(const IPAddress& ip);

// NOTE: Before adding more overloads to ChooseRandomIPAddress(), consider
// gtl::AnySpan.

// Choose a random IPAddress from a span of same.  Requires that `ips` is
// non-empty.
IPAddress ChooseRandomIPAddress(absl::Span<const IPAddress> ips);

// Choose a random IPAddress from the given IPRange.
IPAddress ChooseRandomIPAddress(const IPRange& range);

// Returns whether the address is initialized or not.
inline bool IsInitializedAddress(const IPAddress& addr) {
  return addr.address_family() != AF_UNSPEC;
}

// Return the family-dependent length (in bits) of an IP address given an
// IPAddress object.  A debug-fatal error is logged if the address family
// is not of the Internet variety, i.e. not one of set(AF_INET, AF_INET6);
// the caller is responsible for verifying IsInitializedAddress(ip).
inline int IPAddressLength(const IPAddress& ip) {
  switch (ip.address_family()) {
    case AF_INET:
      return 32;
    case AF_INET6:
      return 128;
    default:
      LOG(DFATAL) << "IPAddressLength() of object with invalid address family: "
                  << ip.address_family();
      return -1;
  }
}

// IPv6-specific functions for different classes of addresses. These
// can optionally populate an IPAddress with the source IPv4 address.  In
// the case of a Teredo address much more information can be extracted.
// In the below examples X.Y.Z.Q is assumed to represent an IPv4 address.

// Compatible IPv4 addresses are of the form "::X.Y.Z.Q/96".
bool GetCompatIPv4Address(const IPAddress& ip6, IPAddress* ip4);

// Mapped IPv4 addresses are of the form "::ffff:X.Y.Z.Q/96".
bool GetMappedIPv4Address(const IPAddress& ip6, IPAddress* ip4);

// 6to4 addresses are of the form:
//
//     2002:UpperV4Hex:LowerV4Hex::/48
//
// i.e. 2002::/16 plus the 32 bit IPv4 address totaling 48 bits.
// So for the IPv4 address 1.2.3.4, the 6to4 /48 block routed to it
// is 2002::0102:0304::/48.

// Extracts the embedded IPv4 address from a 6to4 address, if possible.
// For example, 2002:c000:201:: yields 192.0.2.1.
bool Get6to4IPv4Address(const IPAddress& ip6, IPAddress* ip4);

// Converts any IPv4 range into its 6to4 equivalent.
// For example, 192.0.2.4/31 yields 2002:c000:204::/47.
bool Get6to4IPv6Range(const IPRange& iprange4, IPRange* iprange6);

// ISATAP addresses have a lower 64 bits of the form:
//
//     <...>:0[0-3]00:5efe:ClientUpperV4Hex:ClientLowerV4Hex
//
// See:
//     http://tools.ietf.org/html/rfc5214#section-6.1
//
// NOTE: ISATAP is one of the transition mechanisms that does NOT require
// verifiably valid IPv4 routing to work.  For such protocols, e.g., 6to4
// and Teredo, _some_ level of trust can be established because Google's
// view of global routing would need to be spoofed in order for the TCP
// connection to complete.  (Aside: For Teredo, a malicious server/relay
// could be used, but there are other ways to mitigate this particular
// risk, specifically: having a Google owned and operated server/relay.)
//
// Because the client address is in the lower 64 bits and this is already
// considered to under the control of leaf networks/nodes all that's
// required is a valid IPv6 route and the rest is trivially spoofable.
// Hence ISATAP addresses SHOULD NOT be considered within a security
// context.  This function is here to make unverifiable ISATAP detection
// possible.
bool GetIsatapIPv4Address(const IPAddress& ip6, IPAddress* ip4);

// Teredo addresses are of the form:
//
//     2001:0:ServerUpperV4Hex:ServerLowerV4Hex:
//       flags:~ClientUDPPort:~ClientUpperV4Hex:~ClientLowerV4Hex
//
// Yes, it's complicated.  For eye-bleeding details see:
//     http://tools.ietf.org/html/rfc4380#section-4
bool GetTeredoInfo(const IPAddress& ip6, IPAddress* server, uint16_t* flags,
                   uint16_t* port, IPAddress* client);

// Extract the embedded IPv4 client address, if present.  This only
// returns true if the address is one of [compat, mapped, 6to4, teredo],
// false otherwise.  Due to the spoof-ability of these addresses on the
// wire this should NEVER be used in a security context (e.g. to evaluate
// or elevate privileges).  ISATAP addresses are explicitly excluded.
bool GetEmbeddedIPv4ClientAddress(const IPAddress& ip6, IPAddress* ip4);

// HACK: As long as applications continue to use IPv4 addresses for
// indexing into tables, accounting, et cetera, it may be necessary to
// *coerce* IPv6 addresses into IPv4 addresses. This function does so
// by hashing the upper 64 bits into 224.0.0.0/3 (64 bits into 29 bits).
// If the input IPAddress is already an IPv4 address it is simply returned.
//
// NOTE: Please do not rely on particular outputs from this function.
// Different languages and/or platforms might produce different hashes, and
// there is a risk that we might need to change it at a later point. If you
// really require long-term stable identifiers for IPv6 addresses, convert your
// code to understand IPv6 natively instead.
//
// NOTE: This function is failsafe for security purposes: ALL IPv6 addresses
// (except localhost (::1)) are hashed to avoid the security risk associated
// with extracting an embedded IPv4 address that might permit elevated
// privileges.
IPAddress GetCoercedIPv4Address(const IPAddress& ip6);

// Normalizes the address representation with respect to IPv4 addresses -- that
// is, mapped IPv4 addresses ("::ffff:X.Y.Z.Q") are converted to pure IPv4
// addresses.  All other IPv4, IPv6, and empty values are left unchanged.
//
// For more discussion of mapped addresses and how/when an application
// can receive them from the OS see:
//
//     http://tools.ietf.org/html/rfc4038#section-4.2
//
// NOTE: IPv4-Compatible IPv6 Addresses (a.k.a. "compat" addresses) are
// not normalized here.  A "compat" address will not be passed to an
// application by the OS unless such traffic actually arrives at the node.
// If an application is seeing "compat" addresses then an investigation
// into the origin of the traffic should be initiated.  See also:
//
//     http://tools.ietf.org/html/rfc4291#section-2.5.5.1
IPAddress NormalizeIPAddress(const IPAddress& ip);

// Returns an address suitable for use in IPv6-aware contexts.  This is the
// opposite of NormalizeIPAddress() above.  IPv4 addresses are converted into
// their IPv4-mapped address equivalents (e.g. 192.0.2.1 becomes
// ::ffff:192.0.2.1, 0.0.0.0 becomes ::ffff:0.0.0.0).  IPv6 addresses are a noop
// (they are returned unchanged). Another type of address (e.g. AF_UNSPEC) will
// result in a CHECK-failure.
//
// Historically, using const sockaddr_in* with a dualstack socket (i.e.
// an AF_INET6 socket without the IPV6_V6ONLY socket option set) mostly
// "just worked".  However, per:
//
//     http://tools.ietf.org/html/rfc3493#section-3.7
//
// (discussion of the use of IPv4-mapped address with dualstack sockets),
// this behaviour is not technically required.  Nowadays EINVAL is returned.
//
// Most applications should avoid storing ::ffff-mapped addresses in IPAddress
// or SocketAddress objects, because they're inconvenient for logging and UIs.
// Instead, prefer SocketAddressToFamily() so that mapped addresses may be
// confined to the Berkeley/POSIX "struct sockaddr" types.
IPAddress DualstackIPAddress(const IPAddress& ip);

// Free utility functions for SocketAddress.

// Parses an IPv4 or IPv6 address with included port number to a SocketAddress.
// Not a constructor since it can fail (in which case it returns false,
// and the contents of "out" is undefined). If only validation is required,
// "out" can be set to nullptr.
//
// The format accepted is the same that is output by SocketAddress::ToString().
// The port section of the string is mandatory.
ABSL_MUST_USE_RESULT bool StringToSocketAddress(absl::string_view str,
                                                SocketAddress* out);

// Parses an IPv4 or IPv6 address with included port number and optional scope
// identifier to a SocketAddress. Not a constructor since it can fail.
//
// The format accepted is the same that is output by SocketAddress::ToString(),
// with the optional inclusion of a scope identifier. The port section of the
// string is mandatory. The scope identifier, if included, may be an interface
// name or numerical interface ID, but must be valid at the time of the call (at
// runtime).
//
// Note that the serialized form of this SocketAddress will not include a scope
// identifier, regardless of whether one was included when it was created.  See
// IpAddress::ToPackedstring() for the motivation for this behavior.
absl::StatusOr<SocketAddress> StringToSocketAddressWithOptionalScope(
    absl::string_view str);

// StringToSocketAddress conversion method that CHECK()-fails on invalid input.
// Not a good idea to use on user provided input. The port section of the string
// is mandatory.
inline SocketAddress StringToSocketAddressOrDie(absl::string_view str) {
  SocketAddress addr;
  CHECK(StringToSocketAddress(str, &addr)) << "Invalid SocketAddress " << str;
  return addr;
}

// Similar to StringToSocketAddress, but allows the port be omitted, in which
// case the passed-in default port value is used.
ABSL_MUST_USE_RESULT bool StringToSocketAddressWithDefaultPort(
    absl::string_view str, uint16_t default_port, SocketAddress* out);

// Normalizes the host part of an IPv6 SocketAddress.  All other values are left
// unchanged.  See NormalizeIPAddress for more information.
inline SocketAddress NormalizeSocketAddress(const SocketAddress& addr) {
  return addr.host().address_family() == AF_INET6
             ? SocketAddress(NormalizeIPAddress(addr.host()), addr.port())
             : addr;
}

// Normalize and construct a SocketAddress from a sockaddr_* in one step.
// This is essentially the inverse of SocketAddressToFamily().
inline SocketAddress NormalizeSocketAddress(const sockaddr& addr) {
  return NormalizeSocketAddress(SocketAddress(addr));
}
inline SocketAddress NormalizeSocketAddress(const sockaddr_in6& addr) {
  return NormalizeSocketAddress(
      gtl::ValueOrDie(MakeSocketAddressFromSockaddrIn6(addr)));
}
inline SocketAddress NormalizeSocketAddress(const sockaddr_storage& addr) {
  return NormalizeSocketAddress(SocketAddress(addr));
}

inline SocketAddress DualstackSocketAddress(const SocketAddress& addr) {
  return SocketAddress(DualstackIPAddress(addr.host()), addr.port());
}

// Converts a SocketAddress into a sockaddr_storage of the desired address
// family, suitable for sending to kernel syscalls like connect(), bind(),
// etc.  Returns true if successful.
//
// This is effectively the inverse of NormalizeSocketAddress().
//
// Args:
//   output_family should be one of three values:
//     AF_INET: Builds a sockaddr_in structure.  The held address_family must
//         be AF_INET to avoid an error result.  As a special case, this also
//         maps :: to 0.0.0.0.
//     AF_INET6: Builds a sockaddr_in6 structure.  The held address_family
//         must either be AF_INET or AF_INET6 to avoid an error result.
//         For compatibility with dualstack sockets, any IPv4 address will be
//         mapped to IPv6 using DualstackIPAddress().
//     AF_UNSPEC: Automatically select either AF_INET or AF_INET6, depending
//         on the held address_family.  Use of this mode is discouraged,
//         because it doesn't interact conveniently with dualstack sockets.
//   sa: The SocketAddress to convert.
//   addr_out: Stores the resulting address.  Must not be NULL.
//   size_out: Stores the number of bytes produced.  NULL is permitted.
//
// This function will never CHECK-fail; errors are signaled by returning false.
// To prevent accidental misuse, this also writes a nonsensical address family
// with a size of zero.
//
// Example usage:
//   int fd = socket(...);
//   SocketAddress addr = ...;
//   sockaddr_storage kaddr;
//   socklen_t kaddr_size;
//   // Note: If the address family is already known from context, then
//   //       there's no need to call GetSocketFamily().
//   CHECK(SocketAddressToFamily(GetSocketFamily(fd), addr,
//                               &kaddr, &kaddr_size));
//   if (connect(fd, sockaddr_cast(&kaddr), kaddr_size) == -1) {
//     // Handle the failure.
//   }
//
ABSL_MUST_USE_RESULT bool SocketAddressToFamily(int output_family,
                                                const SocketAddress& sa,
                                                sockaddr_storage* addr_out,
                                                socklen_t* size_out);

// This behaves like SocketAddressToFamily, with the exception that converting
// 0.0.0.0 to AF_INET6 yields ::, rather than ::ffff:0.0.0.0.  This should be
// used with the bind() syscall, in cases where it's desirable to interpret
// 0.0.0.0 as "all IP addresses".
//
// However, this is not suitable for sending packets, because :: behaves like
// an IPv6 loopback destination, and IPv6 packets cannot be received by AF_INET
// sockets bound to 0.0.0.0.
ABSL_MUST_USE_RESULT bool SocketAddressToFamilyForBind(
    int output_family, const SocketAddress& sa, sockaddr_storage* addr_out,
    socklen_t* size_out);

// Returns whether the socket address is initialized or not.
inline bool IsInitializedSocketAddress(const SocketAddress& addr) {
  return IsInitializedAddress(addr.host());
}

// Free utility functions for IPRange.

//
// Parse an IPv4 or IPv6 subnet mask in textual form into an IPRange.
// Not a constructor since it can fail (in which case it returns false,
// and the contents of "out" is undefined). If only validation is required,
// "out" can be set to nullptr.
//
// Note that an improperly zeroed out mask (say, 192.168.0.0/8) will be
// rejected as invalid by this function. If you instead want the excess bits to
// be zeroed out silently, see StringToIPRangeAndTruncate(), below.
//
// The format accepted is the same that is output by IPRange::ToString().
// Any IP addresses without a "/netmask" will be given an implicit
// CIDR netmask length equal to the number of bits in the address
// family (e.g. /32 or /128).  Additionally, IPv4 ranges may have a netmask
// specifier in the older dotted quad format, e.g. "/255.255.0.0".
//
ABSL_MUST_USE_RESULT bool StringToIPRange(absl::string_view str, IPRange* out);

// StringToIPRange conversion methods that CHECK()-fail on invalid input.
// Not a good idea to use on user-provided input.
inline IPRange StringToIPRangeOrDie(absl::string_view str) {
  IPRange ipr;
  CHECK(StringToIPRange(str, &ipr)) << "Invalid IP range " << str;
  return ipr;
}

//
// The same as StringToIPRange and StringToIPRangeOrDie, but truncating instead
// of returning an error in the event of an improperly zeroed out mask (ie.,
// 192.168.0.0/8 will automatically be changed to 192.0.0.0/8).
//
ABSL_MUST_USE_RESULT bool StringToIPRangeAndTruncate(absl::string_view str,
                                                     IPRange* out);
inline IPRange StringToIPRangeAndTruncateOrDie(absl::string_view str) {
  IPRange ipr;
  CHECK(StringToIPRangeAndTruncate(str, &ipr)) << "Invalid IP range " << str;
  return ipr;
}

// Truncate any IPv4 or IPv6 address to the specified number of bits.
// Large lengths become no-ops, but negative lengths are DFATAL.
inline IPAddress TruncateIPAddress(const IPAddress& addr, int length) {
  if (!IsInitializedAddress(addr)) {
    LOG(DFATAL) << "Can't truncate " << addr;
  }
  return ipaddress_internal::TruncateIPAndLength(addr, &length);
}

// This function is deprecated.  Use the constructor instead.
inline IPRange TruncatedAddressToIPRange(const IPAddress& host, int length) {
  return IPRange(host, length);
}

// Parse a "binary" or packed string containing a *truncated* IPRange of IPv4
// or IPv6 addresses. If this string contains an IPRange that is not truncated
// then it will return false. See IPRange::ToPackedString for more information.
ABSL_MUST_USE_RESULT bool PackedStringToIPRange(absl::string_view str,
                                                IPRange* out);

// Binary packed string conversion method that CHECK()-fails on invalid input.
inline IPRange PackedStringToIPRangeOrDie(absl::string_view str) {
  IPRange ip_range;
  CHECK(PackedStringToIPRange(str, &ip_range))
      << "Invalid packed IP range of length " << str.length();
  return ip_range;
}

// Returns true if and only if the IP range is initialized, i.e. its
// address is initialized.
inline bool IsInitializedRange(const IPRange& range) {
  return IsInitializedAddress(range.host());
}

// Checks whether the given IP address "needle" is within the IP range
// "haystack".  Note that an IPv4 address is never considered to be within an
// IPv6 range, and vice versa.
inline bool IsWithinSubnet(const IPRange& haystack, const IPAddress& needle) {
  return haystack.host().address_family() == needle.address_family() &&
         haystack == IPRange(needle, haystack.length());
}

// Checks whether the given IP range "needle" is properly contained within
// the IP range "haystack", i.e. whether "needle" is a more specific of
// "haystack".  Note that an IPv4 range is never considered to be contained
// within an IPv6 range, and vice versa.
inline bool IsProperSubRange(const IPRange& haystack, const IPRange& needle) {
  return haystack.length() < needle.length() &&
         IsWithinSubnet(haystack, needle.host());
}

inline bool IPRangesOverlap(const IPRange& ip_range1,
                            const IPRange& ip_range2) {
  return ip_range1 == ip_range2 || IsProperSubRange(ip_range1, ip_range2) ||
         IsProperSubRange(ip_range2, ip_range1);
}

// Returns true if and only if the IP range is initialized and valid, i.e. its
// address is initialized, its prefix length is valid and the host bits of the
// address are properly zeroed out.
inline bool IsValidRange(const IPRange& range) {
  if (!IsInitializedAddress(range.host())) {
    return false;
  }
  // This branch is arguably unnecessary.  It should only fail in the event of
  // memory corruption, or improper use of UnsafeConstruct().
  const int max_len = IPAddressLength(range.host());
  return 0 <= range.length() && range.length() <= max_len &&
         range == IPRange(range.host(), range.length());
}

// Returns true if and only if the IPRange refers to a single IP.
// That is, the prefix length is 32 for IPv4 or 128 for IPv6.
// If the range is not initialized, false is returned.
inline bool IsSingleIPRange(const IPRange& range) {
  return IsValidRange(range) ? range.length() == IPAddressLength(range.host())
                             : false;
}

// Computes the non-overlapping adjacent IP subnet ranges that cover the IP
// address interval [first_addr, last_addr] and does not cover any other IP
// addresses.  The resulting IP subnet ranges are ordered according to
// IPRangeOrdering and returned in covering_subnets.
//
// The method clears the covering_subnets and returns false if:
//   - first_addr and last_addr does not belong to the same address family, or
//   - first_addr > last_addr according to IPRangeOrdering.
bool IPAddressIntervalToSubnets(const IPAddress& first_addr,
                                const IPAddress& last_addr,
                                std::vector<IPRange>* covering_subnets);

// Return true if the size of the range is greater than the given number.
// CHECK-fail if the IPRange has not been initialized.
bool IsRangeIndexValid(const IPRange& range, absl::uint128 index);

// Return the nth IPAddress in the range. 0 indexes the first one.
// CHECK-fail if the index is out of range or if the range hasn't been
// initialized.  This can effectively be used as an iterator over an
// IPRange with the following pattern:
//
//   for (int i = 0; IsRangeIndexValid(range, i); ++i) {
//     IPAddress addr = NthAddressInRange(range, i);
//     ...
//   }
//
IPAddress NthAddressInRange(const IPRange& range, absl::uint128 index);

// Finds the index of the IP address in the given range.
// CHECK-fails if the IP address does not sit in the given range or if range is
// an invalid IPRange.
//
// This function is the inverse of NthAddressInRange:
//
//   IndexInRange(range, NthAddressInRange(range, i)) == i
absl::uint128 IndexInRange(const IPRange& range, const IPAddress& ip);

// Converts a mask length to an IPAddress. For example, 24 for
// AF_INET is converted to 255.255.255.0, same for IPv6 addresses.
// Useful to convert back and forth between CIDR notation.
// Returns false if the family is unknown or if the length is
// invalid for the family specified. In both cases, address is
// not modified.
ABSL_MUST_USE_RESULT bool MaskLengthToIPAddress(int family, int length,
                                                IPAddress* address);

// Computes the length of a netmask. For example, '255.255.255.0'
// is converted to 24, and returned in the length parameter.
// Useful to convert back and forth between CIDR notation.
//
// Returns false if the address family is not supported, or the
// supplied address does not look like a valid netmask.
// length can be null, in which case the address is still verified.
ABSL_MUST_USE_RESULT bool NetMaskToMaskLength(const IPAddress& address,
                                              int* length);

// If (n > 0), it returns the nth IP address after the given addr.
// If (n < 0), it returns the nth IP address before the given addr.
// For example:
//   "10.1.1.150" + 1 returns "10.1.1.151"
//   "10.1.1.150" + 150 returns "10.1.2.44"
//   "10.1.1.1" - 2 returns "10.1.0.255"
// Supports adding and subtracting 128-bit integers from IPv6 addresses.
// For example:
//   "1::" + absl::MakeInt128(0xabcd, 0) returns "1:0:0:abcd::"
//   "::2" + absl::MakeInt128(0xffff, 0x12) returns "::ffff:0:0:0:14"
// The method CHECK-fails if the given addr has not been initialized.
// It returns false iff the result crosses the IP address space, in which case
// the contents of "result" is undefined.
// For example, "192.0.0.0" + 0x40000000.
ABSL_MUST_USE_RESULT bool IPAddressPlusN(const IPAddress& addr, absl::int128 n,
                                         IPAddress* result);

// Subtract the IP range "sub_range" from the less specific IP range "range"
// and return the resulting collection of disjoint IP ranges in "diff_range".
//
// Collectively, the IP ranges in "diff_range" contain the same set of IP
// addresses that "range" does except the ones that are also contained by
// "sub_range". Note that all IP ranges in "diff_range" are more specific
// than "range".
//
// The subtract operation is undefined if "sub_range" is not a more specific
// of "range", in which case "diff_range" is cleared and the method returns
// false. Otherwise, the method returns true and "diff_range" is set to the
// result.
//
// An illustrative example using 8-bit IP addressing:
//   range:      b7  b6  b5  b4  --  --  --  -- /4
//   sub_range:  b7  b6  b5  b4  b3  b2  b1  b0 /8
//
//   diff_range: b7  b6  b5  b4  b3  b2  b1 ~b0 /8
//               b7  b6  b5  b4  b3  b2 ~b1  -- /7
//               b7  b6  b5  b4  b3 ~b2  --  -- /6
//               b7  b6  b5  b4 ~b3  --  --  -- /5
//
bool SubtractIPRange(const IPRange& range, const IPRange& sub_range,
                     std::vector<IPRange>* diff_range);

// Returns a human-readable representation of the address family.
// Use only for human consumption (e.g. debugging).
std::string AddressFamilyToString(int family);

// Flag support.
//
// Usage:
// DEFINE_FLAG(IPAddress, my_address, IPAddress(), "...");
// DEFINE_FLAG(IPRange, my_range, IPRange(), "...");
// DEFINE_FLAG(SocketAddress, my_socket_address, SocketAddress(), "...");
// DEFINE_FLAG(IPAddressList, my_addresses, IPAddressList(),
//             "...");
//
// Passing an empty string is legally parsed as an uninitialized object.
// Passing an illegal string is considered an error.
// For example:
// ./my_app --my_address=1.2.3.4 --my_range= --my_socket_address=[1234::]:5678 \
//   --my_addresses=5.6.7.8,90::ab
// (FLAGS_my_range == IPRange()).

bool AbslParseFlag(absl::string_view text, IPAddress* dst, std::string* err);
std::string AbslUnparseFlag(IPAddress ip);
bool AbslParseFlag(absl::string_view text, IPRange* dst, std::string* err);
std::string AbslUnparseFlag(IPRange range);
bool AbslParseFlag(absl::string_view text, SocketAddress* dst,
                   std::string* err);
std::string AbslUnparseFlag(SocketAddress sa);

class IPAddressList : public std::vector<IPAddress> {
 public:
  using std::vector<IPAddress>::vector;
};

bool AbslParseFlag(absl::string_view text, IPAddressList* dst,
                   std::string* err);
std::string AbslUnparseFlag(IPAddressList ips);

// sockaddr_cast() converts a variety of sockaddr_foo pointers to sockaddr*,
// which the socket API uses as a generic base type.  We only define overloads
// for IP-related types here, because others (e.g. sockaddr_un) would be out
// of scope for the ipaddress library.
inline sockaddr* sockaddr_cast(sockaddr_storage* s) {
  return reinterpret_cast<sockaddr*>(s);
}
inline sockaddr* sockaddr_cast(sockaddr_in* s) {
  return reinterpret_cast<sockaddr*>(s);
}
inline sockaddr* sockaddr_cast(sockaddr_in6* s) {
  return reinterpret_cast<sockaddr*>(s);
}
inline const sockaddr* sockaddr_cast(const sockaddr_storage* s) {
  return reinterpret_cast<const sockaddr*>(s);
}
inline const sockaddr* sockaddr_cast(const sockaddr_in* s) {
  return reinterpret_cast<const sockaddr*>(s);
}
inline const sockaddr* sockaddr_cast(const sockaddr_in6* s) {
  return reinterpret_cast<const sockaddr*>(s);
}

static_assert(sizeof(IPAddress) == 20, "IPAddress should be 20 bytes");
#ifdef _WIN32
static_assert(sizeof(SocketAddress) == 24, "SocketAddress should be 24 bytes");
static_assert(sizeof(IPRange) == 24, "IPRange should be 24 bytes");
#else
static_assert(sizeof(SocketAddress) == 20, "SocketAddress should be 20 bytes");
static_assert(sizeof(IPRange) == 20, "IPRange should be 20 bytes");
#endif

}  // namespace net_base

#ifndef SWIG

// Hash functions, for use in hash_set<> etc.
HASH_NAMESPACE_DECLARATION_START
template <>
struct hash<net_base::IPAddress> {
  size_t operator()(const net_base::IPAddress& address) const;
};

template <>
struct hash<net_base::SocketAddress> {
  size_t operator()(const net_base::SocketAddress& address) const;
};

template <>
struct hash<net_base::IPRange> {
  size_t operator()(const net_base::IPRange& range) const;
};
HASH_NAMESPACE_DECLARATION_END
#endif  // SWIG

////////////////////////////////////////////////////////////////////////
// IPAddress::Variant implementation details
////////////////////////////////////////////////////////////////////////

namespace net_base {

inline const in_addr& IPAddress::Variant::get_ipv4() const {
  DCHECK(type_ == Type::kIpv4) << DUMP_VARS(type_);
  return addr_.addr4;
}

inline in6_addr& IPAddress::Variant::get_ipv6() {
  DCHECK(type_ == Type::kIpv6) << DUMP_VARS(type_);
  return addr_.addr6;
}

inline const in6_addr& IPAddress::Variant::get_ipv6() const {
  DCHECK(type_ == Type::kIpv6) << DUMP_VARS(type_);
  return addr_.addr6;
}

}  // namespace net_base

#ifdef _WIN32
#undef s6_addr16
#endif

#endif  // THIRD_PARTY_GLOOP_NET_BASE_IPADDRESS_H_
