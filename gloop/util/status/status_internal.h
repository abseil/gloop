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

// IWYU pragma: private, include "gloop/util/status/status.h"

#ifndef THIRD_PARTY_GLOOP_UTIL_STATUS_STATUS_INTERNAL_H_
#define THIRD_PARTY_GLOOP_UTIL_STATUS_STATUS_INTERNAL_H_

// This internal header contains some declarations shared by .cc files within
// //gloop/util/task.

#include <string>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "gloop/util/status/error_space.h"

namespace util {

namespace status_internal {

inline constexpr absl::string_view kErrorSpaceUrl =
    "type.googleapis.com/util.ErrorSpacePayload";
inline constexpr absl::string_view kGenericErrorSpaceName = "generic";

}  // namespace status_internal

// The canonical error space.
// GenericErrorSpace::Get() == Status::canonical_space();
class GenericErrorSpace : public ErrorSpaceImpl<GenericErrorSpace> {
 public:
  static absl::string_view space_name();
  static std::string code_to_string(int code);
  static absl::StatusCode canonical_code(int code);
};

}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_STATUS_STATUS_INTERNAL_H_
