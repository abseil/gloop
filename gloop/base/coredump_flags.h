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

#ifndef THIRD_PARTY_GLOOP_BASE_COREDUMP_FLAGS_H_
#define THIRD_PARTY_GLOOP_BASE_COREDUMP_FLAGS_H_

#include <cstdint>

#include "absl/flags/declare.h"

// Max number of secs we expect to take while writing a core dump
ABSL_DECLARE_FLAG(int32_t, coredump_timeout);

#endif  // THIRD_PARTY_GLOOP_BASE_COREDUMP_FLAGS_H_
