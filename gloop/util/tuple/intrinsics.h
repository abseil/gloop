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

// This file exports the intrinsics.h header that is in a separate directory to
// play nicely with <link>, which has stricter dependency rules.

#ifndef THIRD_PARTY_GLOOP_UTIL_TUPLE_INTRINSICS_H_
#define THIRD_PARTY_GLOOP_UTIL_TUPLE_INTRINSICS_H_

// IWYU pragma: begin_exports
#include "gloop/util/tuple/components/intrinsics.h"
// IWYU pragma: end_exports

#endif  // THIRD_PARTY_GLOOP_UTIL_TUPLE_INTRINSICS_H_
