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

#include "gloop/base/phased_flag.h"

#include <string>

#include "absl/flags/flag.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

ABSL_FLAG(base::FlagPhase, phased_flag_enabled_phase,
#ifdef NDEBUG
          base::FlagPhase::kEnabled,
#else
          base::FlagPhase::kDebugMode,
#endif
          "The phase value at which phased flags are enabled. To pre-adopt "
          "phased flag values, set this value to `EARLY_ADOPTER`");

namespace base {

std::string AbslUnparseFlag(FlagPhase phase) {
  switch (phase) {
    case FlagPhase::kDisabled:
      return "DISABLED";
    case FlagPhase::kDebugMode:
      return "DEBUG_MODE";
    case FlagPhase::kEarlyAdopter:
      return "EARLY_ADOPTER";
    case FlagPhase::kEnabled:
      return "ENABLED";
  }
}

bool AbslParseFlag(absl::string_view text, FlagPhase* phase,
                   std::string* error) {
  if (text == "DISABLED") {
    *phase = FlagPhase::kDisabled;
  } else if (text == "DEBUG_MODE") {
    *phase = FlagPhase::kDebugMode;
  } else if (text == "EARLY_ADOPTER") {
    *phase = FlagPhase::kEarlyAdopter;
  } else if (text == "ENABLED") {
    *phase = FlagPhase::kEnabled;
  } else {
    *error = absl::StrCat("unknown FlagPhase: ", text);
    return false;
  }
  return true;
}

bool IsPhaseEnabled(FlagPhase phase) {
  FlagPhase enabled_phase = absl::GetFlag(FLAGS_phased_flag_enabled_phase);
  return phase >= enabled_phase;
}

}  // namespace base
