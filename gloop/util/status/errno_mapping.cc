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

#include "gloop/util/status/errno_mapping.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/source_location.h"
#include "gloop/util/status/status.h"
#include "gloop/util/status/status_builder.h"

namespace util {

::util::error::Code ErrnoToCanonicalCode(int error_number) {
  return static_cast<::util::error::Code>(
      absl::ErrnoToStatusCode(error_number));
}

absl::Status ErrnoToCanonicalStatus(int error_number,
                                    absl::string_view message) {
  return absl::ErrnoToStatus(error_number, message);
}

absl::StatusBuilder ErrnoToCanonicalStatusBuilder(
    int error_number, absl::string_view message,
    absl::SourceLocation location) {
  return absl::StatusBuilder(absl::ErrnoToStatus(error_number, message),
                             location);
}

}  // namespace util
