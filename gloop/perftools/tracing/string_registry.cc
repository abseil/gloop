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

#include "gloop/perftools/tracing/string_registry.h"

#include <iostream>

namespace perftools::tracing {

std::ostream& operator<<(std::ostream& stream, StringRegistry registry) {
  switch (registry) {
    case StringRegistry::kNoRegistry:
      return stream << "NoRegistry";
    case StringRegistry::kTestRegistry:
      return stream << "TestRegistry";
    case StringRegistry::kTempus:
      return stream << "Tempus";
  }
  return stream << "UnknownRegistry";
}

}  // namespace perftools::tracing
