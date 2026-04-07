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

#include "gloop/base/hostname.h"

#include <cstddef>
#include <string>

#if defined(_WIN32)
#include <winsock.h>
#else
#include <unistd.h>

#include <cerrno>
#endif

#include "absl/base/internal/raw_logging.h"
#include "absl/strings/string_view.h"

namespace base {
namespace hostname_internal {
namespace {
std::string* InitHostname() {
  // Per RFC1035, a DNS domain name is made dot-delimited labels of up to 63
  // octets and has a total length not exceeding 253 octets.
  auto hostname = new std::string(256, '\0');
  do {
    const int ret = gethostname(&(*hostname)[0], hostname->size());
    if (ret == 0) break;
#ifdef _WIN32
    const int code = WSAGetLastError();
    const bool too_long = code == WSAENAMETOOLONG;
#else
    const int code = errno;
    const bool too_long = errno == ENAMETOOLONG;
#endif
    if (!too_long)
      ABSL_RAW_LOG(FATAL, "gethostname() failed with error %d", errno);
    if (hostname->size() > 1048576)
      ABSL_RAW_LOG(FATAL,
                   "gethostname() failed with error %d and the buffer was "
                   "already quite big",
                   code);
    hostname->resize(hostname->size() * 2);
  } while (true);
  const size_t nul = hostname->find('\0');
  if (nul != hostname->npos) hostname->resize(nul);
  return hostname;
}

const std::string& HostnameString() {
  static const std::string* hostname = InitHostname();
  return *hostname;
}
}  // namespace
}  // namespace hostname_internal
absl::string_view Hostname() { return hostname_internal::HostnameString(); }
}  // namespace base

const char* Hostname() {
  return base::hostname_internal::HostnameString().c_str();
}
