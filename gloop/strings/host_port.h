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

// based on contributions of various authors in strings/strutil.h
//
// These are functions for parsing host and port out of a string.

#ifndef THIRD_PARTY_GLOOP_STRINGS_HOST_PORT_H_
#define THIRD_PARTY_GLOOP_STRINGS_HOST_PORT_H_

#include <string.h>

#include <cstdint>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"

namespace strings {

// ----------------------------------------------------------------------
// HostPortPair
//   This type represents a host and port.  The host field may point to any
//   NUL-terminated string, but the caller is responsible for ensuring that
//   this string outlives the HostPortPair instance.
//
//   A function which accepts a HostPortPair must not keep a private copy
//   without also making a private copy of the host field.
// ----------------------------------------------------------------------
class HostPortPair {
 public:
  HostPortPair() : first(), second() {}
  HostPortPair(const char* host, uint16_t port) : first(host), second(port) {}

  HostPortPair(const HostPortPair& x) : first(x.first), second(x.second) {}

  HostPortPair& operator=(const HostPortPair& x) {
    first = x.first;
    second = x.second;
    return *this;
  }

  const char* host() const { return first; }
  uint16_t port() const { return second; }

  friend bool operator<(const HostPortPair& x, const HostPortPair& y) {
    int r = strcmp(x.first, y.first);
    return r != 0 ? r < 0 : x.second < y.second;
  }

  const char* first;
  uint16_t second;
};

// ----------------------------------------------------------------------
// ParseHostPortList()
//    Parse a string containing a sequence of "host:port" or "[host]:port"
//    pairs, delimited by commas and/or whitespace.  The host values are NOT
//    strictly validated.
//
//    If hosts_ports is non-NULL, then the hosts and ports are appended to
//    this vector.  If a parse error occurs, all elements in the vector will
//    be deallocated.  Otherwise, the caller must deallocate the vector later
//    using HostPortPairVectorClear().
//
//    Returns the number of host:ports parsed, or 0 if an error occurred.
//    The same is true when hosts_ports is NULL.
//
// HostPortPairVectorClear()
//    Deallocate a vector filled by ParseHostPortList.  This calls free() on
//    all the host strings, and then clears the vector.  If hosts_ports is NULL
//    or empty, this is a no-op.
// ----------------------------------------------------------------------
int ParseHostPortList(const char* const_full,
                      std::vector<HostPortPair>* hosts_ports);

void HostPortPairVectorClear(std::vector<HostPortPair>* hosts_ports);

// ----------------------------------------------------------------------
// HostOnlyString()
//    Given a string, returns it surrounded by [brackets] if it contains
//    colons like an IPv6 literal.  If the host is already bracketed,
//    then additional brackets will not be added.  This is useful when
//    dealing with port numbers that are implicit or non-numeric.
//
//    The behavior of HostOnlyString when given a NULL input is undefined,
//    except that we guarantee it will not crash.
// ----------------------------------------------------------------------
std::string HostOnlyString(absl::string_view host);

// ----------------------------------------------------------------------
// HostPortString()
//    Given a host and port, returns a string of the form "host:port" or
//    "[ho:st]:port", depending on whether host contains colons like
//    an IPv6 literal.  If the host is already bracketed, then additional
//    brackets will not be added.
//
//    The behavior of HostPortString when given a NULL host string
//    (including when hpp.first == NULL) is undefined, except that we
//    guarantee it will not crash.
// ----------------------------------------------------------------------
std::string HostPortString(absl::string_view host, uint16_t port);

inline std::string HostPortString(const HostPortPair& hpp) {
  return HostPortString(absl::NullSafeStringView(hpp.first), hpp.second);
}

// ----------------------------------------------------------------------
// ParseHostOptionalPort()
// ParseHostOptionalPortString() - wrapper which returns a string.
//
// Parse a freeform nul-terminated string into a host and port, without strict
// validation.  The following formats are recognized:
//
//   example.com
//   example.com:80
//   192.0.2.1
//   192.0.2.1:80
//   [2001:db8::1]     - strips brackets
//   [2001:db8::1]:80  - strips brackets
//   2001:db8::1       - requires a default port
//
// Args:
//   full: The input string to parse.
//   default_port: Use this port number if a port was omitted from the input.
//       To make the port mandatory, set default_port to -1.
//
// Returns:
//   If nothing meaningful could be parsed, returns false and leaves *host and
//   *port unmodified.
//
//   If parsing was successful, returns true.  Host points inside of "full"
//   to a hostname, IPv4/IPv6 literal, or possibly unvalidated nonsense.
//   Port is a validated number in the range 0..65535.
// ----------------------------------------------------------------------
bool ParseHostOptionalPort(absl::string_view full, int default_port,
                           absl::string_view* host, uint16_t* port);

bool ParseHostOptionalPortString(absl::string_view full, int default_port,
                                 std::string* host, uint16_t* port);

// ----------------------------------------------------------------------
// ParseHostPort()
// ParseHostPortString() - wrapper that returns a string.
//
// This is a wrapper around ParseHostOptionalPort(), which only accepts the
// formats containing an explicit port:
//
//   example.com:80
//   192.0.2.1:80
//   [2001:db8::1]:80  - strips brackets
//
// See the ParseHostOptionalPort() comments above for details.
// ----------------------------------------------------------------------
inline bool ParseHostPort(absl::string_view full, absl::string_view* host,
                          uint16_t* port) {
  return ParseHostOptionalPort(full, -1, host, port);
}

inline bool ParseHostPortString(absl::string_view full, std::string* host,
                                uint16_t* port) {
  return ParseHostOptionalPortString(full, -1, host, port);
}

//------------------------------------------------------------------------
// ParseIpRange
//  Parses a range of IP addresses in either wildcard, range, or CIDR
//  format. E.g.:
//   10.20.*.*
//   10.20.*.*-10.30.*.*
//   10.20.30.0-10.20.30.255
//   10.20.30.0/24
//  Returns the low and high addresses in the range (inclusive) through
//  lowip and highip, and returns false if it is unable to parse the
//  range.
//------------------------------------------------------------------------
bool ParseIpRange(absl::string_view range, uint32_t* lowip, uint32_t* highip);

}  // namespace strings

#endif  // THIRD_PARTY_GLOOP_STRINGS_HOST_PORT_H_
