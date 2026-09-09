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

#include "gloop/strings/host_port.h"

#include <stdlib.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gloop/strings/numbers.h"
#include "gloop/strings/util.h"

namespace strings {

// ----------------------------------------------------------------------
// ParseHostPortList()
//    Parse a string containing a sequence of "host:port" or "[host]:port"
//    pairs, delimited by commas and/or whitespace.
// ----------------------------------------------------------------------
int ParseHostPortList(const char* const_full,
                      std::vector<HostPortPair>* hosts_ports) {
  if (const_full == nullptr) return 0;
  std::unique_ptr<char[]> private_full(strdup_with_new(const_full));
  char* full = private_full.get();
  char* next = full;
  int cnt = 0;
  while ((next = gstrsep(&full, " \t\n,")) != nullptr) {
    if (next[0] == '\0') continue;
    std::string host;
    uint16_t port;
    if (!ParseHostPortString(next, &host, &port)) {
      // Clean up the pieces I allocated.  Note: if somebody
      // passed in a non-empty vector, we will end up deleting
      // any pointers they may have stored in here, but that's
      // the best we can do with this poorly specified routine.
      HostPortPairVectorClear(hosts_ports);
      return 0;
    }
    cnt++;
    if (hosts_ports != nullptr) {
      hosts_ports->push_back(HostPortPair(strdup(host.c_str()), port));
    }
  }
  return cnt;
}

// ----------------------------------------------------------------------
// HostPortPairVectorClear()
//    Deallocate a vector filled by ParseHostPortList.
// ----------------------------------------------------------------------
void HostPortPairVectorClear(std::vector<HostPortPair>* hosts_ports) {
  if (hosts_ports) {
    for (const HostPortPair& p : *hosts_ports) free(const_cast<char*>(p.first));
    hosts_ports->clear();
  }
}

namespace {

// Hosts that look like IPv6 address need to be wrapped in square brackets.
inline bool NeedsBrackets(absl::string_view host) {
  return !host.empty() && host[0] != '[' && absl::StrContains(host, ':');
}

}  // namespace

std::string HostOnlyString(absl::string_view host) {
  if (NeedsBrackets(host)) {
    return absl::StrCat("[", host, "]");
  }
  return std::string(host);
}

std::string HostPortString(absl::string_view host, uint16_t port) {
  if (NeedsBrackets(host)) {
    return absl::StrCat("[", host, "]:", port);
  }
  return absl::StrCat(host, ":", port);
}

// ----------------------------------------------------------------------
// ParseHostOptionalPort()
// ParseHostOptionalPortString()
//
//   Parse a string of the form host:port, [host]:port, or 2001::
//   If default_port >= 0, the port in the string can be omitted.
// ----------------------------------------------------------------------
bool ParseHostOptionalPort(absl::string_view full, int default_port,
                           absl::string_view* host, uint16_t* port) {
  const absl::string_view::size_type npos = absl::string_view::npos;
  absl::string_view host_piece;
  absl::string_view port_piece;
  if (!full.empty() && full[0] == '[') {
    // Parse a bracketed host, typically an IPv6 literal.
    auto rbracket = full.rfind(']');
    if (rbracket == npos) {
      // Unmatched [
      return false;
    }
    if (rbracket + 1 < full.size()) {
      if (full[rbracket + 1] == ':') {
        // ]:<port?>
        port_piece = absl::ClippedSubstr(full, rbracket + 2);
        if (port_piece.empty()) {
          // ]:<end>
          return false;
        }
      } else {
        // ]<invalid>
        return false;
      }
    }
    host_piece = absl::ClippedSubstr(full, 1, rbracket - 1);
    // Require all bracketed hosts to contain a colon, because a hostname or
    // IPv4 address should never use brackets.
    if (host_piece.find(':') == npos) {
      return false;
    }
  } else {
    const auto colon = full.find(':');
    if (colon != npos && full.find(':', colon + 1) == npos) {
      // Exactly 1 colon.  Split into host:port.
      host_piece = full.substr(0, colon);
      port_piece = absl::ClippedSubstr(full, colon + 1);
      if (port_piece.empty()) {
        return false;
      }
    } else {
      // 0 or 2+ colons.  Bare hostname or IPv6 literal.
      host_piece = full;
    }
  }

  // Now, try to parse the port.
  int32_t port32;
  if (port_piece.empty()) {
    // Port omitted.  Try the default.
    port32 = default_port;
    if (port32 < 0 || port32 > 65535) {
      // Default port out of range.
      return false;
    }
  } else {
    // Try to parse the port as a decimal number.
    port32 = 0;
    for (const char c : port_piece) {
      if (!absl::ascii_isdigit(c)) {
        return false;
      }
      port32 = 10 * port32 + (c - '0');
      if (port32 > 65535) {
        // Port is getting too large.
        return false;
      }
    }
  }

  // Valid port.
  *host = host_piece;
  *port = static_cast<uint16_t>(port32);
  return true;
}

bool ParseHostOptionalPortString(absl::string_view full, int default_port,
                                 std::string* host, uint16_t* port) {
  absl::string_view host_p;
  if (!ParseHostOptionalPort(full, default_port, &host_p, port)) {
    return false;
  }
  // Don't use CopyToString(), because *host and full might intersect.
  *host = std::string(host_p);
  return true;
}

// Parse one of the dotted components of an IP address.
static bool ParseIpComponent(absl::string_view str, int32_t* comp) {
  if (!strings::safe_strto32_base(str, comp, 10)) {
    return false;
  } else if (*comp < 0) {
    return false;
  }
  *comp = std::min(*comp, 255);  // just in case
  return true;
}

// Parse four dotted components of an IP address.
static bool ParseIpQuarduple(absl::string_view str, uint32_t wild,
                             uint32_t* ip) {
  const char* begin = str.data();
  const char* const end = begin + str.size();
  int32_t b[4] = {0, 0, 0, 0};
  for (int i = 0; i < 4; i++) {
    const char* const tail = std::find(begin, end, '.');
    if (begin == tail) {  // no chars at all?
      return false;
    } else if (begin + 1 == tail && *begin == '*') {  // wildcard?
      b[i] = wild;
    } else if (!ParseIpComponent(absl::string_view(begin, tail - begin),
                                 &b[i])) {
      return false;
    }
    begin = std::min(tail + 1, end);
  }
  if (begin != end) {  // trailing garbage?
    return false;
  }
  *ip = (b[0] << 24) + (b[1] << 16) + (b[2] << 8) + b[3];
  return true;
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
bool ParseIpRange(absl::string_view range, uint32_t* lowip, uint32_t* highip) {
  int bits;
  const std::string::size_type dash = range.find('-');
  if (dash != std::string::npos) {
    return ParseIpQuarduple(range.substr(0, dash), 0, lowip) &&      // * => 0
           ParseIpQuarduple(range.substr(dash + 1), 255, highip) &&  // * => 255
           *lowip <= *highip;  // inclusive
  } else {
    const std::string::size_type slash = range.find('/');
    if (slash == std::string::npos) {  // single ip address, or wildcard A.B.*.*
      return ParseIpQuarduple(range, 0, lowip) &&   // * => 0
             ParseIpQuarduple(range, 255, highip);  // * => 255
    } else {                                        // cidr notation - ip/bits
      if (ParseIpQuarduple(range.substr(0, slash), 0, lowip) &&  // * => 0
          ParseIpComponent(range.substr(slash + 1), &bits) && 8 <= bits &&
          bits <= 32) {
        const uint32_t mask = (1 << (32 - bits)) - 1;  // e.g., 255 if bits=24
        *lowip = *lowip & ~mask;                       // clear low bits
        *highip = *lowip | mask;                       // set low bits
        return true;
      } else {
        return false;
      }
    }
  }
}

}  // namespace strings
