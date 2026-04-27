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

#include <cstddef>

#ifdef __linux__
#include <linux/version.h>
#endif

/*
 * Test compiler support.
 */

#if !defined(__clang__)
#error "Gloop requires Clang."
#endif

#if !defined(__clang_major__) || __clang_major__ < 21
#error "Gloop requires Clang 21 or later."
#endif

#if !defined(__cplusplus) || __cplusplus < 202002L
#error "Gloop requires C++20 or later."
#endif

// Explicitly use some C++20 syntax, to prevent teams from adding
// flags like `-Wc++17-compat`, which are not supported and which will
// lead to breakages as more C++20 syntax gets added to common
// library headers.
namespace gloop::requires_cpp20_or_later {
consteval void requires_cpp20() {}
}  // namespace gloop::requires_cpp20_or_later

/*
 * Test platform support.
 */

#if !defined(__linux__)
#error "Gloop only supports Linux."
#endif  // !defined(__linux__)

#if defined(__ANDROID__)
#error "Gloop doesn't support Android."
#endif  // !defined(__ANDROID__)

static_assert(sizeof(void*) == 8, "Gloop only supports 64-bit platforms.");

#if !defined(__x86_64__)
#error "Gloop only supports x86-64."
#endif  // !defined(__x86_64__)

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0)
#error "Gloop requires Linux kernel 6.6 or later."
#endif

/*
 * Test toolchain support.
 */

#if defined(__GLIBCXX__) && \
    !defined(GLOOP_I_ACKNOWLEDGE_THAT_GLOOP_DOES_NOT_SUPPORT_LIBSTDCXX)
#error "Gloop does not support libstdc++."
#elif defined(__GLIBCXX__)
// TODO Remove this once we decide on whether to support libstdc++.
#define GLOOP_UNSUPPORTED_LIBSTDCXX 1
#endif

#ifndef GLOOP_I_ACKNOWLEDGE_THAT_GLOOP_DOES_NOT_SUPPORT_LIBSTDCXX
#if !defined(_LIBCPP_VERSION) || _LIBCPP_VERSION < 210000
#error "Gloop requires libc++ 21 or later."
#endif
#endif

#if !defined(__GLIBC__) || !defined(__GLIBC_MINOR__) || __GLIBC__ < 2 || \
    (__GLIBC__ == 2 && __GLIBC_MINOR__ < 39)
#error "Gloop requires glibc 2.39 or later."
#endif

/*
 * Test compiler flags.
 */

static_assert((char)-1 > 0,
              "Gloop does not support signed chars (use -funsigned-char)");

#if defined(__EXCEPTIONS) || defined(__cpp_exceptions)
#error "Gloop does not support exceptions (use -fno-exceptions)"
#endif

#endif  // THIRD_PARTY_GLOOP_ENFORCE_GLOOP_SUPPORT_H_
