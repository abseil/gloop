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
// We define a feature as some interface we wish to program to: for example,
// some library function or system call.
//
// For example, suppose a programmer wants to write a program that uses the
// 'mmap' system call. Then one might write:
//
// #include "gloop/base/config.h"
//
// #ifdef ABSL_HAVE_MMAP
// #include "sys/mman.h"
// #endif  //ABSL_HAVE_MMAP
//
// ...
// #ifdef ABSL_HAVE_MMAP
// void *ptr = mmap(...);
// ...
// #endif  // ABSL_HAVE_MMAP
//
// As a special note, using feature macros from config.h to determine whether
// to include a particular header requires violating the style guide's required
// ordering for headers: this is permitted.
#ifndef THIRD_PARTY_GLOOP_BASE_CONFIG_H_
#define THIRD_PARTY_GLOOP_BASE_CONFIG_H_

#include "absl/base/config.h"  // IWYU pragma: keep

#ifdef SWIG
%include "absl/base/config.h"
#endif

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif  // defined(__APPLE__)

#if defined(__ANDROID__)
#include <android/api-level.h>
#endif  // defined(__ANDROID__)

// Operating system-specific features.
//
// Currently supported operating systems and associated preprocessor
// symbols:
//
//   Linux and Linux-derived           __linux__
//   Android                           __ANDROID__ (implies __linux__)
//   Linux (non-Android)               __linux__ && !__ANDROID__
//   Darwin (Mac OS X and iOS)         __APPLE__
//   Akaros (http://akaros.org)        __ros__
//   Windows                           _WIN32
//   NaCL                              __native_client__
//   AsmJS                             __asmjs__
//   WebAssembly                       __wasm__
//   Fuchsia                           __Fuchsia__
//
// Note that since Android defines both __ANDROID__ and __linux__, one
// may probe for either Linux or Android by simply testing for __linux__.
//

// GOOGLE_HAVE_FADVISE is defined when the system provides the posix_fadvise(2)
// system call as defined in POSIX.1-2001.
#if defined(__linux__) || defined(__ros__)
#define GOOGLE_HAVE_FADVISE 1
#else
#undef GOOGLE_HAVE_FADVISE
#endif

// GOOGLE_HAVE_FORK is defined when the system provides the fork(2) function
// to create a new process.  fork() has existed on every version of Unix since
// 1970, and many other systems as well.
#if defined(__linux__) || (defined(__APPLE__) && TARGET_OS_OSX) || \
    defined(__ros__) || defined(__native_client__)
#define GOOGLE_HAVE_FORK 1
#else
#undef GOOGLE_HAVE_FORK
#endif
//
// GOOGLE_HAVE_GETPAGESIZE is defined when the system has a getpagesize(2)
// implementation.  Note: getpagesize(2) was removed in POSIX.1-2001.  New
// code should use `sysconf(_SC_PAGESIZE)` instead.
#if defined(__linux__) || defined(__APPLE__) || defined(__ros__) || \
    defined(__native_client__)
#define GOOGLE_HAVE_GETPAGESIZE 1
#else
#undef GOOGLE_HAVE_GETPAGESIZE
#endif

// GOOGLE_HAVE_MLOCK is defined when the system has an mlock(2) implementation
// as defined in POSIX.1-2001.
#if defined(__linux__) || defined(__APPLE__) || defined(__ros__) || \
    defined(__Fuchsia__)
#define GOOGLE_HAVE_MLOCK 1
#else
#undef GOOGLE_HAVE_MLOCK
#endif

// GOOGLE_HAVE_POSIX_MEMALIGN is defined when the system provides the
// posix_memalign(3) function to allocate memory aligned on a boundary,
// as defined in POSIX.1-2001.
#if (defined(__linux__) && !defined(__ANDROID__)) || defined(__APPLE__) || \
    defined(__ros__) || defined(__native_client__)
#define GOOGLE_HAVE_POSIX_MEMALIGN 1
#else
#undef GOOGLE_HAVE_POSIX_MEMALIGN
#endif

// GOOGLE_HAVE_POSIX_SIGNAL_STACK is defined on systems that provide
// support for separate signals stacks via the sigaltstack(2) call,
// as defined by POSIX.1-2008.  Note that "sigaltstack" looks like a
// typo, but is not: it is "Sig Alt Stack" not "signal stack".
#if defined(__linux__) || (defined(__APPLE__) && TARGET_OS_OSX)
#define GOOGLE_HAVE_POSIX_SIGNAL_ALT_STACK 1
#else
#undef GOOGLE_HAVE_POSIX_SIGNAL_ALT_STACK
#endif

// GOOGLE_HAVE_POSIX_SPAWN is defined when the system provides the
// posix_spawn(3) call, as defined by the POSIX advanced realtime support
// supplement and version 3 of the Single UNIX Specification (SUSv3).
//
// See also Austin T. Clements et al, "The Scalable Commutativity
// Rule: Designing Scalable Software for Multicore Processors"
// (https://people.csail.mit.edu/nickolai/papers/clements-sc.pdf).
#if defined(__linux__) || defined(__APPLE__)
#define GOOGLE_HAVE_POSIX_SPAWN 1
#else
#undef GOOGLE_HAVE_POSIX_SPAWN
#endif

// GOOGLE_HAVE_PTHREAD_SETNAME_NP is defined when the system provides the
// pthread_setname_np(3) function as defined by the behavior of the
// implementation in glibc.  Note that this is a non-standard but common
// extension to the pthreads interface.
#if (defined(__linux__) && !defined(__ANDROID__)) || defined(__APPLE__) || \
    defined(__native_client__) ||                                          \
    (defined(__ANDROID__) && defined(__ANDROID_API__) &&                   \
     __ANDROID_API__ >= 10)
#define GOOGLE_HAVE_PTHREAD_SETNAME_NP 1
#else
#undef GOOGLE_HAVE_PTHREAD_SETNAME_NP
#endif

// GOOGLE_HAVE_SCHED_GETCPU is defined when the system implements
// sched_getcpu(3) as by glibc and it's imitators.
#if defined(__linux__) || defined(__ros__)
#define GOOGLE_HAVE_SCHED_GETCPU 1
#else
#undef GOOGLE_HAVE_SCHED_GETCPU
#endif

// GOOGLE_ENABLE_SETGID is defined on systems that provide the setgid(2) system
// call as defined by POSIX.1-2001, and which desire to use it.
#ifdef GOOGLE_ENABLE_SETGID
#error GOOGLE_ENABLE_SETGID cannot be directly set
#elif defined(__linux__) && !defined(__ANDROID__)
#define GOOGLE_ENABLE_SETGID 1
#elif defined(__APPLE__)
#if TARGET_OS_OSX
#define GOOGLE_ENABLE_SETGID 1
#endif
#endif

// GOOGLE_ENABLE_SETUID is defined on systems that provide the setuid(2) system
// call as defined by POSIX.1-2001, and which desire to use it.
#ifdef GOOGLE_ENABLE_SETUID
#error GOOGLE_ENABLE_SETUID cannot be directly set
#elif defined(__linux__) && !defined(__ANDROID__)
#define GOOGLE_ENABLE_SETUID 1
#elif defined(__APPLE__)
#if TARGET_OS_OSX
#define GOOGLE_ENABLE_SETUID 1
#endif
#endif

// GOOGLE_ENABLE_CHROOT is defined on systems that provide the chroot(2) system
// call as defined by POSIX.1-2001, and which desire to use it.
#ifdef GOOGLE_ENABLE_CHROOT
#error GOOGLE_ENABLE_CHROOT cannot be directly set
#elif defined(__linux__) && !defined(__ANDROID__)
#define GOOGLE_ENABLE_CHROOT 1
#elif defined(__APPLE__)
#if TARGET_OS_OSX
#define GOOGLE_ENABLE_CHROOT 1
#endif
#endif

// GOOGLE_ENABLE_NICE is defined on systems that provide the nice(2) system
// call, and which desire to use it.
#ifdef GOOGLE_ENABLE_NICE
#error GOOGLE_ENABLE_NICE cannot be directly set
#elif defined(__linux__)
#define GOOGLE_ENABLE_NICE 1
#elif defined(__APPLE__)
#if TARGET_OS_OSX
#define GOOGLE_ENABLE_NICE 1
#endif
#endif

// GOOGLE_ENABLE_SYSLOG is defined on systems which desire to use syslog(3).
// TODO: See about enabling syslog on macOS.
#ifdef GOOGLE_ENABLE_SYSLOG
#error GOOGLE_ENABLE_SYSLOG cannot be directly set
#elif defined(__linux__) && !defined(__ANDROID__)
#define GOOGLE_ENABLE_SYSLOG 1
#endif

// GOOGLE_HAVE_SIGINFO_T is defined when the implementation provides the
// siginfo_t for the sigaction(2) interface, as standardized in POSIX.1-2001.
#if defined(__linux__) || defined(__APPLE__) || defined(__ros__)
#define GOOGLE_HAVE_SIGINFO_T 1
#else
#undef GOOGLE_HAVE_SIGINFO_T
#endif

// POSIX library support (not part of the C/C++ standard).
#if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 2
#define GOOGLE_HAVE_FNMATCH 1
#endif

// GOOGLE_ENABLE_SIGNAL_HANDLERS is defined on systems which support posix
// signals and desire to register the default Google signal handlers.
#ifdef GOOGLE_ENABLE_SIGNAL_HANDLERS
#error GOOGLE_ENABLE_SIGNAL_HANDLERS cannot be directly set
#elif defined(__linux__) && !defined(__ANDROID__) && \
    !defined(GOOGLE_UNSUPPORTED_OS_GGP)
#define GOOGLE_ENABLE_SIGNAL_HANDLERS 1
#elif defined(__APPLE__)
#if TARGET_OS_OSX
#define GOOGLE_ENABLE_SIGNAL_HANDLERS 1
#endif
#endif

#ifdef BASE_HAVE_CRASHREASON
#error BASE_HAVE_CRASHREASON cannot be directly set
#elif defined(__linux__) && !defined(__ANDROID__)
#endif

#ifdef BASE_HAVE_PROCESS_STATE
#error BASE_HAVE_PROCESS_STATE cannot be directly set
#elif defined(__linux__) || defined(__APPLE__) || defined(__ros__)
#define BASE_HAVE_PROCESS_STATE 1
#endif

#ifdef BASE_HAVE_CPU_PROFILER
#error BASE_HAVE_CPU_PROFILER cannot be directly set
#elif defined(__linux__) || defined(__APPLE__)
#define BASE_HAVE_CPU_PROFILER 1
#endif

#ifdef BASE_USE_SIGNAL_H
#error "BASE_USE_SIGNAL_H must not be set externally"
#elif defined(__linux__) || defined(__APPLE__) || defined(__Fuchsia__) || \
    defined(__EMSCRIPTEN__) || defined(__wasm__)
#define BASE_USE_SIGNAL_H 1
#endif

#if defined(__linux__) || defined(__APPLE__) || defined(_WIN32) || \
    defined(__EMSCRIPTEN__) || defined(__Fuchsia__)
// Defined if this build includes an implementation of the above fns.
#define GOOGLE_BASE_HAS_INITGOOGLE 1
#endif

// GOOGLE_HAVE_BASE_PERCPU is defined on systems which support optimized per-cpu
// operations.
#ifdef GOOGLE_HAVE_BASE_PERCPU
#error GOOGLE_HAVE_BASE_PERCPU cannot be set directly
#elif defined(__linux__) && !defined(__ANDROID__)
#define GOOGLE_HAVE_BASE_PERCPU 1
#endif

#endif  // THIRD_PARTY_GLOOP_BASE_CONFIG_H_
