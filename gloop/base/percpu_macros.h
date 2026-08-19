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

#ifndef THIRD_PARTY_GLOOP_BASE_PERCPU_MACROS_H_
#define THIRD_PARTY_GLOOP_BASE_PERCPU_MACROS_H_

// This header file defines percpu macros removing platform specific
// boilerplate from application code implementing inlined RSEQ assembly.
//
// The minimum inlined RSEQ assembly code looks as follows:
//
//  #if PERCPU_USE_RSEQ
//
//    int64_t scratch;
//    asm volatile(
//        PERCPU_RSEQ_PROLOGUE(MyPercpuFunction, scratch)
//
//        "4:\n"
//        PERCPU_RSEQ_LOAD_CPU_ID(scratch)
//
//        "5:\n"
//
//        : [scratch] "=&r"(scratch)
//        : PERCPU_RSEQ_INPUTS
//        : PERCPU_RSEQ_CLOBBERS);
//
//  #endif  // PERCPU_USE_RSEQ
//
// PERCPU_RSEQ_PROLOGUE includes all required RSEQ sections, vars tables and
// prologue code. The 1st argument is the (unique) symbol name to be
// associated with the inline assembly code. The 2nd argument defines a scratch
// variable that the prologue code can use during initialization. The scratch
// variable does not need to be preserved: the application code can freely
// use the scratch variable for its own purposes.
//
// The application must provide the non empty `PERCPU_RSEQ_INPUTS` inputs, and
// non empty `PERCPU_RSEQ_CLOBBERS` clobbers. Applications can add additional
// inputs and clobbers. PERCPU_RSEQ_CLOBBERS automatically includes the flags
// ("cc") clobber. `PERCPU_RSEQ_INPUTS` allocates one register bound input for
// holding a pointer to the rseq ABI structure. Applications should not use any
// variable names starting with 'percpu_rseq'.
//
// The percpu RSEQ logic uses 5 labels of which labels 1, 2 and 3 are defined
// inside the prologue code, and labels `4:` and `5:` must be provided by the
// inlined application assembly code: `4:` designates the entry or restart
// location and `5:` designates the `commit` label marking the end of the
// restartable sequence. I.e.: once the code executes beyond label `5:`, the
// critical section is considered complete / committed (and won't restart).
// Typically the last instruction before the commit label is a single store
// operation, committing whatever the purpose of your code is.
//
// Applications typically start with loading the current cpu or vcpu id value.
// The PERCPU_RSEQ_LOAD_CPU_ID always loads the actual cpu id. In our above
// example, `scratch` will always receive the actual cpu id value. Applications
// can also use the virtual cpu id value if supported by the kernel.
//
// One option is to duplicate the code for virtual cpu id support, e.g.:
//
//    int64_t scratch;
//    if (base::subtle::percpu::UsingRseqVirtualCpus()) {
//      asm volatile(
//          PERCPU_RSEQ_PROLOGUE(MyPercpuFunction, scratch)
//
//          "4:\n"
//          PERCPU_RSEQ_LOAD_CPU_ID(scratch)
//
//          "5:\n"
//      : ... );
//    } else {
//      asm volatile(
//          PERCPU_RSEQ_PROLOGUE(MyPercpuFunction, scratch)
//
//          "4:\n"
//          PERCPU_RSEQ_LOAD_VCPU_ID(scratch)
//
//          "5:\n"
//      : ... );
//    }
//
// A more dynamic approach is to use the PERCPU_RSEQ_LOAD_VIRTUAL_FLAT_CPU_ID
// macro which automatically loads the virtual flat cpu id value if available,
// or the cpu id otherwise:
//
//    int64_t scratch;
//    asm volatile(
//        PERCPU_RSEQ_PROLOGUE(MyPercpuFunction, scratch)
//
//        "4:\n"
//        PERCPU_RSEQ_LOAD_VIRTUAL_FLAT_CPU_ID(cpu_id_offset, scratch)
//
//        "5:\n"
//
//        : [scratch] "=&r"(scratch)
//        : PERCPU_RSEQ_INPUTS,
//        : PERCPU_RSEQ_CLOBBERS);
//
// See `percpu_macros_test` for more concrete examples for x86 and ARM.

#include <stddef.h>

#include "absl/base/attributes.h"
#include "gloop/base/internal/percpu.inc"

#if PERCPU_USE_RSEQ
#include "tcmalloc/internal/linux_syscall_support.h"

extern "C" ABSL_CONST_INIT thread_local volatile kernel_rseq __rseq_abi;
#endif

namespace base {
namespace subtle {
namespace percpu {

#if PERCPU_USE_RSEQ

#if ABSL_IS_BIG_ENDIAN
#error "Big Endian rseq is not (yet) supported"
#endif

#define PERCPU_RSEQ_STRINGIZE(expr) #expr
#define PERCPU_RSEQ_STRINGIZE_VALUE(expr) PERCPU_RSEQ_STRINGIZE(expr)

// TODO: clang-format bug
// clang-format off

#if defined(__x86_64__)

#define PERCPU_RSEQ_JMP "jmp"
#define PERCPU_RSEQ_RELOC_ARCH "R_X86_64_NONE"
#define PERCPU_RSEQ_TRAMPOLINE_BYTES ".byte 0x0f, 0x1f, 0x05\n"

#define PERCPU_RSEQ_CLOBBERS "cc"

#define PERCPU_RSEQ_PREPARE(fn, var)                                    \
  "mov __rseq_cs_" PERCPU_RSEQ_STRINGIZE(fn) "_%=@GOTPCREL(%%rip), %["  \
  PERCPU_RSEQ_STRINGIZE(var) "]\n"                                      \
  "mov %[" PERCPU_RSEQ_STRINGIZE(var) "]"                               \
  ", %c[percpu_rseq_cs](%[percpu_rseq_abi])\n"

#define PERCPU_RSEQ_LOAD_CPU_ID(var)                           \
  "movslq %c[percpu_cpu_id](%[percpu_rseq_abi]), %["           \
  PERCPU_RSEQ_STRINGIZE(var) "]\n"

#define PERCPU_RSEQ_LOAD_VCPU_ID(var)                          \
  "movswq %c[percpu_vcpu_id](%[percpu_rseq_abi]), %["          \
  PERCPU_RSEQ_STRINGIZE(var) "]\n"

#define PERCPU_RSEQ_LOAD_VIRTUAL_FLAT_CPU_ID(var)              \
  "mov __rseq_virtual_flat_cpu_id_offset@GOTPCREL(%%rip),"     \
  "%[" PERCPU_RSEQ_STRINGIZE(var) "]\n"                        \
  "mov (%[" PERCPU_RSEQ_STRINGIZE(var) "]),"                   \
  "%[" PERCPU_RSEQ_STRINGIZE(var) "]\n"                        \
  "movswq (%[percpu_rseq_abi], %["                             \
  PERCPU_RSEQ_STRINGIZE(var) "]), %["                          \
  PERCPU_RSEQ_STRINGIZE(var) "]\n"

#elif defined(__aarch64__)

#define PERCPU_RSEQ_JMP "b"
#define PERCPU_RSEQ_TRAMPOLINE_BYTES
#define PERCPU_RSEQ_RELOC_ARCH "R_AARCH64_NONE"

#define PERCPU_RSEQ_CLOBBERS "x16", "x17", "cc"

#define PERCPU_RSEQ_PREPARE(fn, var)                                    \
  "adrp %[" PERCPU_RSEQ_STRINGIZE(var) "], __rseq_cs_"                  \
  PERCPU_RSEQ_STRINGIZE(fn) "_%=\n"                                     \
  "add  %[" PERCPU_RSEQ_STRINGIZE(var) "], %["                          \
  PERCPU_RSEQ_STRINGIZE(var) "], :lo12:__rseq_cs_"                      \
  PERCPU_RSEQ_STRINGIZE(fn) "_%=\n"                                     \
  "str %[" PERCPU_RSEQ_STRINGIZE(var) "]"                               \
  ", [%[percpu_rseq_abi], %c[percpu_rseq_cs]]\n"

#define PERCPU_RSEQ_LOAD_CPU_ID(var)                                   \
  "ldrsw %[" PERCPU_RSEQ_STRINGIZE(var) "]"                            \
  ", [%[percpu_rseq_abi], %c[percpu_cpu_id]]\n"

#define PERCPU_RSEQ_LOAD_VCPU_ID(var)                                  \
  "ldrsh %[" PERCPU_RSEQ_STRINGIZE(var) "]"                            \
  ", [%[percpu_rseq_abi], %c[percpu_vcpu_id]]\n"

#define PERCPU_RSEQ_LOAD_VIRTUAL_FLAT_CPU_ID(var)               \
  "adrp   %[" PERCPU_RSEQ_STRINGIZE(var) "],"                   \
  " :got:__rseq_virtual_flat_cpu_id_offset\n"                   \
  "ldr    %[" PERCPU_RSEQ_STRINGIZE(var) "],"                   \
  " [%[" PERCPU_RSEQ_STRINGIZE(var) "],"                        \
  " :got_lo12:__rseq_virtual_flat_cpu_id_offset]\n"             \
  "ldr    %[" PERCPU_RSEQ_STRINGIZE(var) "],"                   \
  " [%[" PERCPU_RSEQ_STRINGIZE(var) "]]\n"                      \
  "ldrsh  %[" PERCPU_RSEQ_STRINGIZE(var) "],"                  \
  " [%[percpu_rseq_abi], %[" PERCPU_RSEQ_STRINGIZE(var) "]]\n"

#endif

// clang-format on

#define PERCPU_RSEQ_INPUTS_P(rseq_abi)                      \
  [percpu_rseq_abi] "r"(rseq_abi),                          \
      [percpu_rseq_cs] "n"(offsetof(kernel_rseq, rseq_cs)), \
      [percpu_cpu_id] "n"(offsetof(kernel_rseq, cpu_id)),   \
      [percpu_vcpu_id] "n"(offsetof(kernel_rseq, vcpu_id))

#define PERCPU_RSEQ_INPUTS \
  PERCPU_RSEQ_INPUTS_P(&::base::subtle::percpu::__rseq_abi)

#if !defined(__clang_major__) || __clang_major__ >= 9
#define PERCPU_RSEQ_RELOC ".reloc 0, " PERCPU_RSEQ_RELOC_ARCH ", 1f\n"
#else
#define PERCPU_RSEQ_RELOC
#endif

#define PERCPU_RSEQ_PROLOGUE(fn, var) \
  ".pushsection __rseq_cs, \"aw?\"\n"                                   \
  ".balign 32\n"                                                        \
  ".local __rseq_cs_" PERCPU_RSEQ_STRINGIZE(fn) "_%=\n"                 \
  ".type __rseq_cs_" PERCPU_RSEQ_STRINGIZE(fn) "_%=,@object\n"          \
  ".size __rseq_cs_" PERCPU_RSEQ_STRINGIZE(fn) "_%=,32\n"               \
  "__rseq_cs_" PERCPU_RSEQ_STRINGIZE(fn) "_%=:\n"                       \
  ".long 0x0\n"                                                         \
  ".long 0x0\n"                                                         \
  ".quad 4f\n"                                                          \
  ".quad 5f - 4f\n"                                                     \
  ".quad 2f\n"                                                          \
  ".popsection\n"                                                       \
  PERCPU_RSEQ_RELOC                                                     \
  ".pushsection __rseq_cs_ptr_array, \"aw?\"\n"                         \
  "1:\n"                                                                \
  ".balign 8;"                                                          \
  ".quad __rseq_cs_" PERCPU_RSEQ_STRINGIZE(fn) "_%=\n"                  \
  ".popsection\n"                                                       \
  ""                                                                    \
  ".pushsection .text.unlikely, \"ax?\"\n"                              \
  PERCPU_RSEQ_TRAMPOLINE_BYTES                                          \
  ".long " PERCPU_RSEQ_STRINGIZE_VALUE(PERCPU_RSEQ_SIGNATURE) "\n"      \
  ".local " PERCPU_RSEQ_STRINGIZE(fn) "_trampoline_%=\n"                \
  ".type " PERCPU_RSEQ_STRINGIZE(fn) "_trampoline_%=,@function\n"       \
  "" PERCPU_RSEQ_STRINGIZE(fn) "_trampoline_%=:\n"                      \
  "2:\n"                                                                \
  PERCPU_RSEQ_JMP " 3f\n"                                               \
  ".popsection\n"                                                       \
  "3:\n"                                                                \
  PERCPU_RSEQ_PREPARE(fn, var)

#else  // PERCPU_USE_RSEQ

#define PERCPU_RSEQ_PROLOGUE(fn, var) "N/A"
#define PERCPU_RSEQ_LOAD_CPU_ID(var) "N/A"
#define PERCPU_RSEQ_LOAD_VCPU_ID(var) "N/A"

#endif  // PERCPU_USE_RSEQ

}  // namespace percpu
}  // namespace subtle
}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_PERCPU_MACROS_H_
