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

// Explicitly use some C++20 syntax, to prevent teams from adding
// flags like `-Wc++17-compat`, which are not supported and which will
// lead to breakages as more C++20 syntax gets added to common
// library headers.
namespace gloop::requires_cpp20_or_later {
consteval void requires_cpp20() {}
}  // namespace gloop::requires_cpp20_or_later

#if !defined(__linux__)
#error "Gloop only supports Linux."
#endif  // !defined(__linux__)

#if defined(__ANDROID__)
#error "Gloop doesn't support Android."
#endif  // !defined(__ANDROID__)

static_assert(sizeof(void*) == 8);

#if !defined(__x86_64__) && !defined(__aarch64__)
#error "Gloop only supports x86-64 and AArch64."
#endif  // !defined(__x86_64__) && !defined(__aarch64__)

#endif  // THIRD_PARTY_GLOOP_ENFORCE_GLOOP_SUPPORT_H_
