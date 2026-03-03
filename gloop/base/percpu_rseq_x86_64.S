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

#ifndef __x86_64__
#error "percpu_rseq_x86_64.inc should only be included for x86-64 builds"
#endif

#include "gloop/base/internal/percpu.inc"

/*
 * Upstream API Exposition:
 *
 *   METHOD_abort:  // Emitted as part of START_RSEQ()
 *     START_RSEQ() // Starts critical section between [start,commit)
 *   METHOD_start:  // Emitted as part of START_RSEQ()
 *     FETCH_CPU()  // Reads current CPU
 *     ...
 *     single store // Commits sequence
 *   METHOD_commit:
 *     ...return...
 *
 * This process is assisted by the DEFINE_UPSTREAM_CS macro, which encodes a
 * (rodata) constant table, whose address is used to start the critical
 * section, and the abort trampoline.
 *
 * The trampoline is used because:
 * 1.  Restarts are expected to be rare, so the extra jump when restarting is
 *     expected to be infrequent.
 * 2.  The upstream restartable sequence implementation expects the trailing 4
 *     bytes of the abort PC to be "signed" (to prevent manipulation of the PC
 *     to an arbitrary choice).  For us, this is PERCPU_RSEQ_SIGNATURE.  This
 *     value is passed to the kernel during configuration of the rseq syscall.
 *     This would either need to be encoded as a nop (SIGN_ABORT) at the start
 *     of every restartable sequence, increasing instruction cache pressure, or
 *     placed directly before the entry point.
 *
 * The trampoline returns us to METHOD_abort, which is the normal entry point
 * for the restartable sequence.  Upon restart, the (upstream) kernel API
 * clears the per-thread restartable sequence state. We return to METHOD_abort
 * (rather than METHOD_start), as we need to reinitialize this value.
 */

/* Place the code into the google_malloc section. This section is the heaviest
 * user of Rseq code, so it makes sense to co-locate it.
 */

.section google_malloc, "ax"

/* ---------------- start helper macros ----------------  */

// This macro defines:
// * the rseq_cs instance that we'll use for label's critical section.
// * a trampoline to return to when we abort.  This label_trampoline is
//   distinct from label_start, as the return IP must be "signed" (see
//   SIGN_ABORT()).
//
// TODO:  __rseq_cs only needs to be writeable to allow for
// relocations, but could be read-only for non-PIE builds.
#define DEFINE_UPSTREAM_CS(label)                   \
  .pushsection __rseq_cs, "aw";                     \
  .balign 32;                                       \
  .protected __rseq_cs_##label;                     \
  .type __rseq_cs_##label,@object;                  \
  .size __rseq_cs_##label,32;                       \
  __rseq_cs_##label:                                \
  .long PERCPU_RSEQ_VERSION, PERCPU_RSEQ_FLAGS;     \
  .quad .L##label##_start;                          \
  .quad .L##label##_commit - .L##label##_start;     \
  .quad label##_trampoline;                         \
  .reloc 0, R_X86_64_NONE, .L##label##array;        \
  .popsection;                                      \
  .pushsection __rseq_cs_ptr_array, "aw";           \
  .L##label##array:                                 \
  .quad __rseq_cs_##label;                          \
  .popsection;                                      \
  SIGN_ABORT();                                     \
  .globl label##_trampoline;                        \
  .type  label##_trampoline, @function;             \
label##_trampoline:                                 \
  CFI(.cfi_startproc);                              \
  jmp .L##label##_abort;                 	    \
  CFI(.cfi_endproc);                                \
  .size label##_trampoline, . - label##_trampoline;

// This is part of the upstream rseq ABI.  The 4 bytes prior to the abort IP
// must match PERCPU_RSEQ_SIGNATURE (as configured by our rseq syscall's
// signature parameter).  This signature is used to annotate valid abort IPs
// (since rseq_cs could live in a user-writable segment).
//
// To allow this to be safely executed as a valid instruction, we encode the
// value with a nop.  This is decoded as:
//
//   nopl 0xSIGNATURE(%rip)
//
#define SIGN_ABORT()           \
  .byte 0x0f, 0x1f, 0x05;      \
  .long PERCPU_RSEQ_SIGNATURE;

/*
 * Provide a directive to specify the size of symbol "label", relative to the
 * current location and its start.
 */
#define ENCODE_SIZE(label) .size label, . - label;

/*
 * Fetches the per-cpu data addressed by (%base) for cpu %cpu into %dest.
 *
 *  CAUTION: %base must be distinct from %dest since the actual address is
 *  calculated in place using %cpu
 */
#define FETCH_PERCPU_DATA(cpu, base, dest) \
  movslq cpu, dest; \
  shlq $PERCPU_BYTES_PER_REGION_SHIFT, dest; \
  addq base, dest;

/*
 *  Combines FETCH_CPU and FETCH_PERCPU_DATA above for the local cpu (which is
 *  also returned in %cpu).
 */
#define FETCH_LOCAL_PERCPU_DATA(cpu, base, dest) \
  FETCH_CPU(cpu); \
  FETCH_PERCPU_DATA(cpu, base, dest);

/* In all non-position independent cases we need to use RIP-relative label
   addresses */
#if !defined(__PIC__)
#define LABEL_ADDR(label) $label
#else
#define LABEL_ADDR(label) label@GOTPCREL(%rip)
#endif /* !defined(__PIC__) */

#define FETCH_CPU(dest) movl 4(%rdi), dest;
#define START_RSEQ(src)                     \
   .L##src##_abort:                             \
   leaq __rseq_cs_##src(%rip), %rax;            \
   movq %rax, 8(%rdi);                          \
   .L##src##_start:

/* ---------------- end helper macros ---------------- */

/* int RseqFunction_PerCpuTryLock(volatile kernel_rseq *rseq_abi,
 *                                long *ptr, int64_t lock_value); */
  .p2align 6; /* aligns to 2^6 with NOP filling */
  .globl RseqFunction_PerCpuTryLock
  .type  RseqFunction_PerCpuTryLock, @function
RseqFunction_PerCpuTryLock:
  CFI(.cfi_startproc)
.LRseqFunction_PerCpuTryLock_region0:
  START_RSEQ(RseqFunction_PerCpuTryLock);
  FETCH_LOCAL_PERCPU_DATA(%eax, %rsi, %r10);
  cmpq $0, (%r10);
  jne .LRseqFunction_PerCpuTryLock_region2;
  movq %rdx, (%r10); /* %rsi has the lock value */
.LRseqFunction_PerCpuTryLock_region1:
.LRseqFunction_PerCpuTryLock_commit:
  ret;
.LRseqFunction_PerCpuTryLock_region2:
  movq $-1, %rax;
  ret;
.LRseqFunction_PerCpuTryLock_region3:
  CFI(.cfi_endproc)
ENCODE_SIZE(RseqFunction_PerCpuTryLock);
DEFINE_UPSTREAM_CS(RseqFunction_PerCpuTryLock)

/*
 * NOTE:  We don't use cmpxchgq in the following functions since this would
   make checking the success of our commit operation dependent on flags (which
 * are in turn clobbered by the restart region) -- furthermore we can't just
 * retry to fill in the flags since the restarted cmpxchg may have actually
 * succeeded; spuriously failing subsequent attempts.
 */

/*
 * int RseqFunction_PerCpuCmpxchg64(volatile kernel_rseq *rseq_abi,
 *                                  int target_cpu, long *p,
 *                                  long old_val, long new_val)
 */
  .p2align 6; /* aligns to 2^6 with NOP filling */
  .globl RseqFunction_PerCpuCmpxchg64
  .type  RseqFunction_PerCpuCmpxchg64, @function
RseqFunction_PerCpuCmpxchg64:
  CFI(.cfi_startproc)
.LRseqFunction_PerCpuCmpxchg64_region0:
  START_RSEQ(RseqFunction_PerCpuCmpxchg64);
  FETCH_CPU(%eax);
  cmp %eax, %esi; /* check cpu vs current_cpu */
  jne .LRseqFunction_PerCpuCmpxchg64_region1;
  cmp %rcx, (%rdx); /* verify old == *p */
  jne .LRseqFunction_PerCpuCmpxchg64_region2;
  mov %r8, (%rdx);
.LRseqFunction_PerCpuCmpxchg64_region1:
.LRseqFunction_PerCpuCmpxchg64_commit:
  ret;  /* return current cpu, indicating mismatch OR success */
.LRseqFunction_PerCpuCmpxchg64_region2:
  mov $-1, %eax;  /* mismatch versus "old" or "check", return -1 */
  ret;
.LRseqFunction_PerCpuCmpxchg64_region3:
  CFI(.cfi_endproc)
ENCODE_SIZE(RseqFunction_PerCpuCmpxchg64)
DEFINE_UPSTREAM_CS(RseqFunction_PerCpuCmpxchg64)

/*
 * int RseqFunction_PerCpuCmpxchgCheck64(volatile kernel_rseq *rseq_abi,
 *                                       int cpu, int64_t *p,
 *                                       int64_t old, int64_t new.
 *                                       int64_t check_ptr*, inptr_t check_val)
 */
  .p2align 6; /* aligns to 2^6 with NOP filling */
  .globl RseqFunction_PerCpuCmpxchgCheck64
  .type  RseqFunction_PerCpuCmpxchgCheck64, @function
RseqFunction_PerCpuCmpxchgCheck64:
  CFI(.cfi_startproc)
.LRseqFunction_PerCpuCmpxchgCheck64_region0:
  START_RSEQ(RseqFunction_PerCpuCmpxchgCheck64);
  FETCH_CPU(%eax);
  cmp %eax, %esi;  /* check cpu vs current_cpu */
  jne .LRseqFunction_PerCpuCmpxchgCheck64_region1;
  cmp %rcx, (%rdx);  /* verify old == *p */
  jne .LRseqFunction_PerCpuCmpxchgCheck64_region2;
  movq 8(%rsp), %r10;
  cmpq %r10, (%r9);  /* verify check_val == *check_ptr */
  jne .LRseqFunction_PerCpuCmpxchgCheck64_region2;
  mov %r8, (%rdx);
.LRseqFunction_PerCpuCmpxchgCheck64_region1:
.LRseqFunction_PerCpuCmpxchgCheck64_commit:
  ret;  /* return current cpu, indicating mismatch OR success */
.LRseqFunction_PerCpuCmpxchgCheck64_region2:
  mov $-1, %eax;  /* mismatch versus "old" or "check", return -1 */
  ret;
.LRseqFunction_PerCpuCmpxchgCheck64_region3:
  CFI(.cfi_endproc)
ENCODE_SIZE(RseqFunction_PerCpuCmpxchgCheck64)
DEFINE_UPSTREAM_CS(RseqFunction_PerCpuCmpxchgCheck64)

/* RseqCycleCounterResult RseqFunction_PerCpuReadCycleCounter(
 *     volatile kernel_rseq* rseq_abi) */
  .p2align 6; /* aligns to 2^6 with NOP filling */
  .globl RseqFunction_PerCpuReadCycleCounter
  .type  RseqFunction_PerCpuReadCycleCounter, @function
RseqFunction_PerCpuReadCycleCounter:
  CFI(.cfi_startproc)
.LRseqFunction_PerCpuReadCycleCounter_region0:
  START_RSEQ(RseqFunction_PerCpuReadCycleCounter);
  FETCH_CPU(%ecx);

  /* Fetch the cycle count into edx:eax.  */
  rdtsc;

  /*
   * We must return the cycle count in rdx, which currently has the high bits
   * of the result, but in low position. Shift them up and then merge in the low
   * bits. (Note that rdtsc guarantees it clears the high bits of rax.)
   */
  shlq $32, %rdx;
  orq %rax, %rdx;

  /* Return the CPU number in %rax. */
  movl %ecx, %eax;

.LRseqFunction_PerCpuReadCycleCounter_region1:
.LRseqFunction_PerCpuReadCycleCounter_commit:
  ret;
.LRseqFunction_PerCpuReadCycleCounter_region2:
  CFI(.cfi_endproc)
ENCODE_SIZE(RseqFunction_PerCpuReadCycleCounter)
DEFINE_UPSTREAM_CS(RseqFunction_PerCpuReadCycleCounter)
