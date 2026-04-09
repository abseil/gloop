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

// Defines preprocessor macros describing the presence of "features" available.
// This facilitates writing portable code by parameterizing the compilation
// based on the presence or lack of a feature.
//
// See <link> for guidance on defining and using these macros.
//
// This header defines feature macros for the //thread package, declaring
// platform level support for various aspects of the implementation.
//
// As a special note, using feature macros from config.h to determine whether
// to include a particular header requires violating the style guide's required
// ordering for headers: this is permitted.

#ifndef THIRD_PARTY_GLOOP_THREAD_CONFIG_H_
#define THIRD_PARTY_GLOOP_THREAD_CONFIG_H_

#ifndef _WIN32
#include <unistd.h>
#endif

#define THREAD_HAVE_CPU_SUBCONTAINERS 0
#define THREAD_HAVE_THREAD_CONTROL 0

// THREAD_HAVE_THREAD_CLASS:
// Whether //thread offers the ::Thread class. If this macro is not set, then it
// is not safe to include thread/thread.h.
//
// THREAD_HAVE_FIBER:
// Whether the //thread/fiber/... package is usable on this platform.
// If this macro is not set, then it is not safe to depend on or include
// any headers under //thread/fiber/...
//
// THREAD_HAVE_ALTERNATE_THREAD_POOL:
// Indicates ThreadPool is a limited capability substitute.
//
// THREAD_HAVE_ALTERNATE_THREAD_LOCAL:
// Indicates ThreadLocal is a limited capability substitute.
#ifdef THREAD_HAVE_THREAD_CLASS
#error THREAD_HAVE_THREAD_CLASS cannot be set directly
#elif THREAD_HAVE_ALTERNATE_THREAD_POOL
#error THREAD_HAVE_ALTERNATE_THREAD_POOL cannot be set directly
#elif defined(THREAD_HAVE_ALTERNATE_THREAD_LOCAL)
#error THREAD_HAVE_ALTERNATE_THREAD_LOCAL cannot be set directly
#elif defined(__linux__) || defined(__APPLE__)
#define THREAD_HAVE_THREAD_CLASS 1
#else
#define THREAD_HAVE_ALTERNATE_THREAD_POOL 1
#define THREAD_HAVE_ALTERNATE_THREAD_LOCAL 1
#endif

// Indicates that this platform supports/uses pthreads and can both include the
// associated headers and use the associated symbols.
#ifdef THREAD_HAVE_POSIX_THREADS
#error THREAD_HAVE_POSIX_THREADS cannot be set directly
#elif _POSIX_THREADS >= 0 || defined(__Fuchsia__)
#define THREAD_HAVE_POSIX_THREADS 1
#endif

// Indicates that the io_priority_level and io_class fields in thread::Options
// take effect on this platform.
#ifdef THREAD_HAVE_IOPRIORITY
#error THREAD_HAVE_IOPRIORITY cannot be set directly
#elif defined(__linux__) && !defined(__ANDROID__)
#define THREAD_HAVE_IOPRIORITY 1
#endif

#endif  // THIRD_PARTY_GLOOP_THREAD_CONFIG_H_
