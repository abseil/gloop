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

#ifndef THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_STRING_REGISTRY_H_
#define THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_STRING_REGISTRY_H_

#include <cstdint>
#include <iosfwd>
#include <type_traits>

namespace perftools::tracing {

// Well known string registries.
// See `StringId` and `StringLabel` for more information.
enum class StringRegistry : uint16_t {
  kNoRegistry = 0,    // Not a well known string / persisted registry
  kTestRegistry = 1,  // Reserved exclusively for testing purposes
  kTempus = 2,        // Tempus `TempusId` string values
};

template <typename R, typename = void>
struct StringRegistryTraits {
  static inline constexpr auto kRegistry = StringRegistry::kNoRegistry;
};

template <typename R>
struct StringRegistryTraits<R, std::void_t<decltype(R::kRegistry)>> {
  static inline constexpr StringRegistry kRegistry = R::kRegistry;
};

std::ostream& operator<<(std::ostream& stream, StringRegistry registry);

}  // namespace perftools::tracing

#endif  // THIRD_PARTY_GLOOP_PERFTOOLS_TRACING_STRING_REGISTRY_H_
