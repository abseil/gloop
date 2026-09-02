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

#ifndef THIRD_PARTY_GLOOP_BASE_FPRINT_H_
#define THIRD_PARTY_GLOOP_BASE_FPRINT_H_

#include <stdint.h>

// TODO: remove this eventually.
// No object has kIllegalFprint as its Fingerprint.
typedef uint64_t Fprint;
#if defined(__cplusplus)
static constexpr Fprint kIllegalFprint = 0;
static constexpr Fprint kMaxFprint{0xFFFFFFFFFFFFFFFF};
#else   // not __cplusplus, this branch exists only for C-compat
static const Fprint kIllegalFprint = 0;
static const Fprint kMaxFprint = UINT64_C(0xFFFFFFFFFFFFFFFF);
#endif  // __cplusplus

#endif  // THIRD_PARTY_GLOOP_BASE_FPRINT_H_
