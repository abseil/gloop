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

#ifndef THIRD_PARTY_GLOOP_BASE_PHASED_FLAG_H_
#define THIRD_PARTY_GLOOP_BASE_PHASED_FLAG_H_

#include <string>

#include "absl/strings/string_view.h"

namespace base {

enum class FlagPhase { kDisabled, kDebugMode, kEarlyAdopter, kEnabled };

std::string AbslUnparseFlag(FlagPhase phase);

bool AbslParseFlag(absl::string_view text, FlagPhase* phase,
                   std::string* error);

// When libraries opt behavior changes into phased flags, this function will be
// used to determine if the change is enabled given the flag value and the
// binary-wide phase.
//
// Flags should be defined as:
//   ABSL_FLAG(bool, flag_name,
//             base::IsPhaseEnabled(base::FlagPhase::kDisabled), "helptext");
//
// And moved from phase to phase by changing the default value after the
// previous phase has been vetted.
bool IsPhaseEnabled(FlagPhase phase);

}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_PHASED_FLAG_H_
