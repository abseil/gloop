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

// Helper macros and methods to return and propagate errors with `absl::Status`.
//
// See https://abseil.io/tips/121 for guidance on the use of these macros.

#ifndef THIRD_PARTY_GLOOP_UTIL_STATUS_STATUS_MACROS_H_
#define THIRD_PARTY_GLOOP_UTIL_STATUS_STATUS_MACROS_H_

#include <cstddef>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/status/status_builder.h"  // IWYU pragma: export
#include "absl/status/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/types/source_location.h"
#include "gloop/util/status/status.h"
#include "gloop/util/status/status_builder.h"  // IWYU pragma: export

namespace util::status_macro_internal {

using StatusAdaptorForMacros ABSL_DEPRECATE_AND_INLINE() =
    // NOLINTNEXTLINE(abseil-no-internal-dependencies)
    absl::status_macro_internal::StatusAdaptorForMacros;
}

#endif  // THIRD_PARTY_GLOOP_UTIL_STATUS_STATUS_MACROS_H_
