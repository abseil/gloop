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

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gloop/net/base/ipaddress.h"

namespace net_base::ipaddress_internal {

// A version of IPRange::operator<=> that returns -1, 0, or 1 in a way that
// corresponds to the std::weak_ordering returned by that operator.
//
// TODO: remove this once crubit generates bindings for non-default
// operator<=>.
int CompareIPRangeForRust(const IPRange& a, const IPRange& b) {
  const std::weak_ordering cmp = a <=> b;
  return cmp < 0 ? -1 : (cmp > 0 ? 1 : 0);
}

// TODO: Replace with a StatusOr-returning C++ overload of
// StringToIPRange in ipaddress.h (similar to cl/995171107).
absl::StatusOr<IPRange> StringToIPRangeForRust(absl::string_view str) {
  IPRange out;
  if (StringToIPRange(str, &out)) {
    return out;
  }
  return absl::InvalidArgumentError(
      absl::StrCat("Invalid IP range: '", str, "'"));
}

// TODO: Replace with a StatusOr-returning C++ overload of
// StringToIPRangeAndTruncate in ipaddress.h (similar to cl/995171107).
absl::StatusOr<IPRange> StringToIPRangeAndTruncateForRust(
    absl::string_view str) {
  IPRange out;
  if (StringToIPRangeAndTruncate(str, &out)) {
    return out;
  }
  return absl::InvalidArgumentError(
      absl::StrCat("Invalid IP range: '", str, "'"));
}

// TODO: Replace with a StatusOr-returning C++ overload of
// PackedStringToIPRange in ipaddress.h (similar to cl/994557396).
absl::StatusOr<IPRange> PackedStringToIPRangeForRust(absl::string_view str) {
  IPRange out;
  if (PackedStringToIPRange(str, &out)) {
    return out;
  }
  return absl::InvalidArgumentError("Invalid packed IP range");
}

}  // namespace net_base::ipaddress_internal
