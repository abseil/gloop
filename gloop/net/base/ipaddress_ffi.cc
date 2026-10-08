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

#include <compare>

#include "gloop/net/base/ipaddress.h"

namespace net_base::ipaddress_internal {

// A version of IPAddress::operator<=> that returns -1, 0, or 1 in a way that
// corresponds to the std::weak_ordering returned by that operator.
//
// TODO: remove this once crubit generates bindings for non-default
// operator<=>.
int CompareForRust(const IPAddress& a, const IPAddress& b) {
  const std::weak_ordering cmp = a <=> b;
  return cmp < 0 ? -1 : (cmp > 0 ? 1 : 0);
}

}  // namespace net_base::ipaddress_internal
