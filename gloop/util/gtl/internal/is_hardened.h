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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_INTERNAL_IS_HARDENED_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_INTERNAL_IS_HARDENED_H_

#include "absl/base/macros.h"

namespace gtl::internal {

// Returns true if `ABSL_HARDENING_ASSERT()` evaluates (and therefore enforces)
// its condition in the current build mode, and false if it compiles to a no-op.
//
// This is intended for tests which verify that hardening assertions fire, e.g.
//
//   if (gtl::internal::IsHardened()) {
//     EXPECT_DEATH(FunctionThatViolatesAPrecondition(), "");
//   }

inline bool IsHardened() {
  bool hardened = false;
  ABSL_HARDENING_ASSERT([&hardened]() {
    hardened = true;
    return true;
  }());
  return hardened;
}

}  // namespace gtl::internal

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_INTERNAL_IS_HARDENED_H_
