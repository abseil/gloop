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

#ifndef BASE_INTERNAL_INIT_GOOGLE_H_
#define BASE_INTERNAL_INIT_GOOGLE_H_

// Internal details of init_google.cc.
// To be included only by init_google.cc.

#include "absl/strings/string_view.h"

namespace base {
namespace internal {

int InitGoogleFindAllowlistEntryForTest(absl::string_view, absl::string_view);

bool IsValidMlockStyle(absl::string_view);

}  // namespace internal
}  // namespace base

#endif  // BASE_INTERNAL_INIT_GOOGLE_H_
