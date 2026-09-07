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

#include "gloop/util/status/posixerrorspace.h"

#include <string>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "gloop/base/strerror.h"
#include "gloop/util/status/errno_mapping.h"
#include "gloop/util/status/status.h"

namespace util {

class PosixErrorSpaceImpl : public ErrorSpaceImpl<PosixErrorSpaceImpl> {
 public:
  static absl::string_view space_name();

  // Returns the message associated with the given code in this error
  // space. This is basically a call to strerror_r.
  static std::string code_to_string(int code);

  static absl::StatusCode canonical_code(int code);
};

const ErrorSpace* PosixErrorSpace() { return PosixErrorSpaceImpl::Get(); }

absl::string_view PosixErrorSpaceImpl::space_name() {
  return "util::PosixErrorSpace";
}

std::string PosixErrorSpaceImpl::code_to_string(int code) {
  return base::StrError(code);
}

absl::StatusCode PosixErrorSpaceImpl::canonical_code(int code) {
  return static_cast<absl::StatusCode>(ErrnoToCanonicalCode(code));
}

}  // namespace util
