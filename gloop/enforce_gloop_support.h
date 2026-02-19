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

// This header enforces our support matrix at build time, with unsupported
// configurations reported with `#error`.

#ifndef THIRD_PARTY_GLOOP_ENFORCE_GLOOP_SUPPORT_H_
#define THIRD_PARTY_GLOOP_ENFORCE_GLOOP_SUPPORT_H_

#if !defined(__clang__)
#error "Gloop requires Clang."
#endif  // !defined(__clang__)

#if !defined(__clang_major__) || __clang_major__ < 21
#error "Gloop requires Clang 21 or later."
#endif  // !defined(__clang_major__) || __clang_major__ < 21

#if !defined(__cplusplus) || __cplusplus < 202002L
#error "Gloop requires C++20 or later."
#endif  // !defined(__cplusplus) || __cplusplus < 202002L

#if !defined(__linux__)
#error "Gloop only supports Linux."
#endif  // !defined(__linux__)

#if defined(__ANDROID__)
#error "Gloop doesn't support Android."
#endif  // !defined(__ANDROID__)

#include "gloop_distro.h"
#if GLOOP_ON_DEBIAN != 1
#error "Gloop only supports Debian."
#endif
#endif  // THIRD_PARTY_GLOOP_ENFORCE_GLOOP_SUPPORT_H_
