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

// Single file to include target specific implementations for percpu.

#include "gloop/base/internal/percpu.inc"

#if PERCPU_USE_RSEQ

#ifdef __GCC_HAVE_DWARF2_CFI_ASM
#define CFI(...) __VA_ARGS__
#else
#define CFI(...)
#endif

#if defined(__x86_64__)
#include "gloop/base/percpu_rseq_x86_64.S"
#elif defined(__aarch64__)
#include "gloop/base/percpu_rseq_aarch64.S"
#else
#error "RSEQ support expected, but not found."
#endif

// We do not need an executable stack. Disable it using the same syntax that
// gcc does (https://stackoverflow.com/a/7863222/1505451).
//
// We make sure we have a path that does this for every architecture so that we
// don't receive "missing .note.GNU-stack section implies executable stack"
// errors.
#if defined(__arm__) || defined(__aarch64__) || defined(__ppc__) || \
    defined(__ppc64__)
.section .note.GNU-stack, "", %progbits
#else
.section .note.GNU-stack, "", @progbits
#endif

#endif // PERCPU_USE_RSEQ
