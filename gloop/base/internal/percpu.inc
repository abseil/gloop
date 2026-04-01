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

// This file contains internals that need to be shared between C++ and assembly
// code. It shouldn't be referenced directly; instead use
// "gloop/base/percpu.h".
//
// IWYU pragma: private, include "gloop/base/percpu.h"

#ifndef THIRD_PARTY_GLOOP_BASE_INTERNAL_PERCPU_INC_
#define THIRD_PARTY_GLOOP_BASE_INTERNAL_PERCPU_INC_

#include "absl/base/config.h"

// PERCPU_USE_RSEQ defines whether or not we have an implementation for the
// target OS and architecture. We currently only support RSEQ on 64-bit x86
// and ARM binaries running on Linux platforms.
#ifdef PERCPU_USE_RSEQ
#error PERCPU_USE_RSEQ cannot be directly set
#endif
#if defined(OS_LINUX) && (defined(__x86_64__) || defined(__aarch64__)) && \
    !defined(__ppc__)
#define PERCPU_USE_RSEQ 1
#else
#define PERCPU_USE_RSEQ 0
#endif

// PERCPU_USE_RSEQ_GOTO is true when both RSEQ support is available and asm goto
// support is available. This is useful for applications requiring goto support
// or having optimized implementations if goto labels are supported.
#ifdef PERCPU_USE_RSEQ_GOTO
#error PERCPU_USE_RSEQ_GOTO cannot be directly set
#endif
#if PERCPU_USE_RSEQ && !ABSL_HAVE_FEATURE(speculative_load_hardening) && \
    (ABSL_INTERNAL_HAVE_MIN_CLANG_VERSION(11, 0) ||                      \
     ABSL_INTERNAL_HAVE_MIN_GNUC_VERSION(4, 5))
#define PERCPU_USE_RSEQ_GOTO 1
#else
#define PERCPU_USE_RSEQ_GOTO 0
#endif

#define PERCPU_RSEQ_VERSION 0x0
#define PERCPU_RSEQ_FLAGS 0x0

// Define the "signature" for the abort handler, verified by the kernel (see
// http://b/173825338#comment14). In order to allow interoperability with the
// rseq functions defined by tcmalloc, these values must match the ones it
// uses.
#if defined(__x86_64__)
#define PERCPU_RSEQ_SIGNATURE 0x53053053
#elif defined(__aarch64__)
#define PERCPU_RSEQ_SIGNATURE 0xd428bc00
#else
#define PERCPU_RSEQ_SIGNATURE 0x0
#endif

// This defines the number of bytes in each per-cpu memory region.  It is the
// number of adjacent objects (by bytes) that we will place next to each other
// behind a Handle.  See the comments surrounding StaticPerCpuObjects allocator
// initialization in percpu_handle.cc for more background.
#define PERCPU_BYTES_PER_REGION_SHIFT 12
#define PERCPU_BYTES_PER_REGION (1 << PERCPU_BYTES_PER_REGION_SHIFT)

#endif  // THIRD_PARTY_GLOOP_BASE_INTERNAL_PERCPU_INC_
