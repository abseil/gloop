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

//
// Fully inlined syscalls.  Provides a family of calls which should
// inline to just the <syscall> instruction (and minimal register
// munging.) This saves multiple calls and attendant stackspilling.
//
// We also explictly do NOT set the <errno> variable that syscall(3) does.
// Instead, we just return the (negated) error number that the kernel returns.
//
// We also provide a family "FPUSafe_InlinedSyscallx" -- these work
// even for a syscall that (breaking the x86-64 ABI!) does not restore
// floating-point state, by explicitly clobbering the proper set of
// registers.  I hope.
#ifndef THIRD_PARTY_GLOOP_BASE_INLINED_SYSCALL_H_
#define THIRD_PARTY_GLOOP_BASE_INLINED_SYSCALL_H_

#include <errno.h>  // IWYU pragma: keep
#include <unistd.h>

#include <cstdint>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"  // IWYU pragma: keep

namespace base {

// this is the full state of FPU that GCC tracks; if we can't trust a syscall
// to restore them, clobber them.
#if defined(__x86_64__)
#define INLINED_SYSCALL_FPU_CLOBBER                                           \
  "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", "xmm8",     \
      "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15", "mm0",    \
      "mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm7", "fpsr", "st", "st(1)", \
      "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"
#else
// Manifest destiny arch or similar; I don't know what we need to clobber,
// but thankfully no such arch actually violates the rules (yet.)
#define INLINED_SYSCALL_FPU_CLOBBER
#endif

#if defined(__x86_64__)
// The register magic used in this file triggers GCC warnings
// for unused values.  We disable this warning (just in this file.)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"

// Quoth the x86-64 ABI for the kernel (www.x86-64.org/documentation/abi.pdf):
//
// "1. User-level applications use as integer registers for passing the sequence
// %rdi, %rsi, %rdx, %rcx, %r8 and %r9. The kernel interface uses %rdi,
// %rsi, %rdx, %r10, %r8 and %r9.
//
// 2. A system-call is done via the syscall instruction. The kernel destroys
// registers %rcx and %r11.
//
// 3. The number of the syscall has to be passed in register %rax.
//
// 4. System-calls are limited to six arguments, no argument is passed
// directly on the stack.
//
// 5. Returning from the syscall, register %rax contains the result of
// the system-call. A value in the range between -4095 and -1
// indicates an error, it is -errno."
//
// 6. All FPU registers are caller-save; in addition, the x87 stack
// must be empty on function calls and returns (with the possible
// exception of st0 and st1 storing function return values.)
//
// Note that GCC does not allow us to constrain an input to r8,r9,r10.
// So instead, we bind a local variable to that register and use a
// generic input register bound to that variable (this hack is the
// explicitly supported way to do this, alas.)

// All syscalls need these registers clobbered
#define CLOBBERED_BY_SYSCALL "rcx", "memory", "r11"

ABSL_ATTRIBUTE_ALWAYS_INLINE inline intptr_t InlinedSyscall0(intptr_t num) {
  asm volatile("syscall\n"
               : [Num] "+a"(num)
               :  // no other inputs
               : CLOBBERED_BY_SYSCALL);
  return num;
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline intptr_t InlinedSyscall1(intptr_t num,
                                                             intptr_t arg1) {
  asm volatile("syscall\n"
               : [Num] "+a"(num)
               : [Arg1] "D"(arg1)
               : CLOBBERED_BY_SYSCALL);
  return num;
}
ABSL_ATTRIBUTE_ALWAYS_INLINE inline intptr_t InlinedSyscall2(intptr_t num,
                                                             intptr_t arg1,
                                                             intptr_t arg2) {
  asm volatile("syscall\n"
               : [Num] "+a"(num)
               : [Arg1] "D"(arg1), [Arg2] "S"(arg2)
               : CLOBBERED_BY_SYSCALL);
  return num;
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline intptr_t InlinedSyscall3(intptr_t num,
                                                             intptr_t arg1,
                                                             intptr_t arg2,
                                                             intptr_t arg3) {
  asm volatile("syscall\n"
               : [Num] "+a"(num)
               : [Arg1] "D"(arg1), [Arg2] "S"(arg2), [Arg3] "d"(arg3)
               : CLOBBERED_BY_SYSCALL);
  return num;
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline intptr_t InlinedSyscall4(
    intptr_t num, intptr_t arg1, intptr_t arg2, intptr_t arg3, intptr_t arg4) {
  register intptr_t arg4_r10 asm("r10") = arg4;
  asm volatile("syscall\n"
               : [Num] "+a"(num)
               : [Arg1] "D"(arg1), [Arg2] "S"(arg2), [Arg3] "d"(arg3),
                 [Arg4] "r"(arg4_r10)
               : CLOBBERED_BY_SYSCALL);
  return num;
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline intptr_t InlinedSyscall5(
    intptr_t num, intptr_t arg1, intptr_t arg2, intptr_t arg3, intptr_t arg4,
    intptr_t arg5) {
  register intptr_t arg4_r10 asm("r10") = arg4;
  register intptr_t arg5_r8 asm("r8") = arg5;
  asm volatile("syscall\n"
               : [Num] "+a"(num)
               : [Arg1] "D"(arg1), [Arg2] "S"(arg2), [Arg3] "d"(arg3),
                 [Arg4] "r"(arg4_r10), [Arg5] "r"(arg5_r8)
               : CLOBBERED_BY_SYSCALL);
  return num;
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline intptr_t InlinedSyscall6(
    intptr_t num, intptr_t arg1, intptr_t arg2, intptr_t arg3, intptr_t arg4,
    intptr_t arg5, intptr_t arg6) {
  register intptr_t arg4_r10 asm("r10") = arg4;
  register intptr_t arg5_r8 asm("r8") = arg5;
  register intptr_t arg6_r9 asm("r9") = arg6;
  asm volatile("syscall\n"
               : [Num] "+a"(num)
               : [Arg1] "D"(arg1), [Arg2] "S"(arg2), [Arg3] "d"(arg3),
                 [Arg4] "r"(arg4_r10), [Arg5] "r"(arg5_r8), [Arg6] "r"(arg6_r9)
               : CLOBBERED_BY_SYSCALL);
  return num;
}

// In addition to the clobber specifications, we also emit an emms to
// clear the x87 stack.  Our clobber means (I hope) that we enter the
// syscall with an empty stack: we need to return with one as well and
// the kernel might give us a partially full one.
//
// This is not ideal; emms isn't free and it's getting more expensive (!)
// in Sandy Bridge and newer processors. We should look into better kernel
// fixes.
ABSL_ATTRIBUTE_ALWAYS_INLINE inline intptr_t FPUSafe_InlinedSyscall0(
    intptr_t num) {
  asm volatile(
      "syscall\n"
      "emms\n"
#ifdef __AVX__
      "vzeroupper"
#endif  // __AVX__
      : [Num] "+a"(num)
      :  // no other inputs
      : CLOBBERED_BY_SYSCALL, INLINED_SYSCALL_FPU_CLOBBER);
  return num;
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline intptr_t FPUSafe_InlinedSyscall1(
    intptr_t num, intptr_t arg1) {
  asm volatile(
      "syscall\n"
      "emms\n"
#ifdef __AVX__
      "vzeroupper"
#endif  // __AVX__
      : [Num] "+a"(num)
      : [Arg1] "D"(arg1)
      : CLOBBERED_BY_SYSCALL, INLINED_SYSCALL_FPU_CLOBBER);
  return num;
}
ABSL_ATTRIBUTE_ALWAYS_INLINE inline intptr_t FPUSafe_InlinedSyscall2(
    intptr_t num, intptr_t arg1, intptr_t arg2) {
  asm volatile(
      "syscall\n"
      "emms\n"
#ifdef __AVX__
      "vzeroupper"
#endif  // __AVX__
      : [Num] "+a"(num)
      : [Arg1] "D"(arg1), [Arg2] "S"(arg2)
      : CLOBBERED_BY_SYSCALL, INLINED_SYSCALL_FPU_CLOBBER);
  return num;
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline intptr_t FPUSafe_InlinedSyscall3(
    intptr_t num, intptr_t arg1, intptr_t arg2, intptr_t arg3) {
  asm volatile(
      "syscall\n"
      "emms\n"
#ifdef __AVX__
      "vzeroupper"
#endif  // __AVX__
      : [Num] "+a"(num)
      : [Arg1] "D"(arg1), [Arg2] "S"(arg2), [Arg3] "d"(arg3)
      : CLOBBERED_BY_SYSCALL, INLINED_SYSCALL_FPU_CLOBBER);
  return num;
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline intptr_t FPUSafe_InlinedSyscall4(
    intptr_t num, intptr_t arg1, intptr_t arg2, intptr_t arg3, intptr_t arg4) {
  register intptr_t arg4_r10 asm("r10") = arg4;  // NOLINT
  asm volatile(
      "syscall\n"
      "emms\n"
#ifdef __AVX__
      "vzeroupper"
#endif  // __AVX__
      : [Num] "+a"(num)
      :
      [Arg1] "D"(arg1), [Arg2] "S"(arg2), [Arg3] "d"(arg3), [Arg4] "r"(arg4_r10)
      : CLOBBERED_BY_SYSCALL, INLINED_SYSCALL_FPU_CLOBBER);
  return num;
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline intptr_t FPUSafe_InlinedSyscall5(
    intptr_t num, intptr_t arg1, intptr_t arg2, intptr_t arg3, intptr_t arg4,
    intptr_t arg5) {
  register intptr_t arg4_r10 asm("r10") = arg4;  // NOLINT
  register intptr_t arg5_r8 asm("r8") = arg5;    // NOLINT
  asm volatile(
      "syscall\n"
      "emms\n"
#ifdef __AVX__
      "vzeroupper"
#endif  // __AVX__
      : [Num] "+a"(num)
      : [Arg1] "D"(arg1), [Arg2] "S"(arg2), [Arg3] "d"(arg3),
        [Arg4] "r"(arg4_r10), [Arg5] "r"(arg5_r8)
      : CLOBBERED_BY_SYSCALL, INLINED_SYSCALL_FPU_CLOBBER);
  return num;
}

ABSL_ATTRIBUTE_ALWAYS_INLINE inline intptr_t FPUSafe_InlinedSyscall6(
    intptr_t num, intptr_t arg1, intptr_t arg2, intptr_t arg3, intptr_t arg4,
    intptr_t arg5, intptr_t arg6) {
  register intptr_t arg4_r10 asm("r10") = arg4;  // NOLINT
  register intptr_t arg5_r8 asm("r8") = arg5;    // NOLINT
  register intptr_t arg6_r9 asm("r9") = arg6;    // NOLINT
  asm volatile(
      "syscall\n"
      "emms\n"
#ifdef __AVX__
      "vzeroupper"
#endif  // __AVX__
      : [Num] "+a"(num)
      : [Arg1] "D"(arg1), [Arg2] "S"(arg2), [Arg3] "d"(arg3),
        [Arg4] "r"(arg4_r10), [Arg5] "r"(arg5_r8), [Arg6] "r"(arg6_r9)
      : CLOBBERED_BY_SYSCALL, INLINED_SYSCALL_FPU_CLOBBER);
  return num;
}

#undef CLOBBERED_BY_SYSCALL
#pragma GCC diagnostic pop

#else  // !__x86_64__

// Nothing else cheats, so no need to do anything special.  Probably
// worth figuring out the actual inlines for manifest-destiny.

inline intptr_t InlinedSyscall0(intptr_t num) {
  intptr_t rc = syscall(num);
  if (ABSL_PREDICT_FALSE(rc < 0)) {
    rc = -errno;
  }
  return rc;
}

inline intptr_t InlinedSyscall1(intptr_t num, intptr_t arg1) {
  intptr_t rc = syscall(num, arg1);
  if (ABSL_PREDICT_FALSE(rc < 0)) {
    rc = -errno;
  }
  return rc;
}

inline intptr_t InlinedSyscall2(intptr_t num, intptr_t arg1, intptr_t arg2) {
  intptr_t rc = syscall(num, arg1, arg2);
  if (ABSL_PREDICT_FALSE(rc < 0)) {
    rc = -errno;
  }
  return rc;
}

inline intptr_t InlinedSyscall3(intptr_t num, intptr_t arg1, intptr_t arg2,
                                intptr_t arg3) {
  intptr_t rc = syscall(num, arg1, arg2, arg3);
  if (ABSL_PREDICT_FALSE(rc < 0)) {
    rc = -errno;
  }
  return rc;
}

inline intptr_t InlinedSyscall4(intptr_t num, intptr_t arg1, intptr_t arg2,
                                intptr_t arg3, intptr_t arg4) {
  intptr_t rc = syscall(num, arg1, arg2, arg3, arg4);
  if (ABSL_PREDICT_FALSE(rc < 0)) {
    rc = -errno;
  }
  return rc;
}

inline intptr_t InlinedSyscall5(intptr_t num, intptr_t arg1, intptr_t arg2,
                                intptr_t arg3, intptr_t arg4, intptr_t arg5) {
  intptr_t rc = syscall(num, arg1, arg2, arg3, arg4, arg5);
  if (ABSL_PREDICT_FALSE(rc < 0)) {
    rc = -errno;
  }
  return rc;
}

inline intptr_t InlinedSyscall6(intptr_t num, intptr_t arg1, intptr_t arg2,
                                intptr_t arg3, intptr_t arg4, intptr_t arg5,
                                intptr_t arg6) {
  intptr_t rc = syscall(num, arg1, arg2, arg3, arg4, arg5, arg6);
  if (ABSL_PREDICT_FALSE(rc < 0)) {
    rc = -errno;
  }
  return rc;
}

inline intptr_t FPUSafe_InlinedSyscall0(intptr_t num) {
  intptr_t ret = InlinedSyscall0(num);
  return ret;
}

inline intptr_t FPUSafe_InlinedSyscall1(intptr_t num, intptr_t arg1) {
  intptr_t ret = InlinedSyscall1(num, arg1);
  return ret;
}

inline intptr_t FPUSafe_InlinedSyscall2(intptr_t num, intptr_t arg1,
                                        intptr_t arg2) {
  intptr_t ret = InlinedSyscall2(num, arg1, arg2);
  return ret;
}

inline intptr_t FPUSafe_InlinedSyscall3(intptr_t num, intptr_t arg1,
                                        intptr_t arg2, intptr_t arg3) {
  intptr_t ret = InlinedSyscall3(num, arg1, arg2, arg3);
  return ret;
}

inline intptr_t FPUSafe_InlinedSyscall4(intptr_t num, intptr_t arg1,
                                        intptr_t arg2, intptr_t arg3,
                                        intptr_t arg4) {
  intptr_t ret = InlinedSyscall4(num, arg1, arg2, arg3, arg4);
  return ret;
}

inline intptr_t FPUSafe_InlinedSyscall5(intptr_t num, intptr_t arg1,
                                        intptr_t arg2, intptr_t arg3,
                                        intptr_t arg4, intptr_t arg5) {
  intptr_t ret = InlinedSyscall5(num, arg1, arg2, arg3, arg4, arg5);
  return ret;
}

inline intptr_t FPUSafe_InlinedSyscall6(intptr_t num, intptr_t arg1,
                                        intptr_t arg2, intptr_t arg3,
                                        intptr_t arg4, intptr_t arg5,
                                        intptr_t arg6) {
  intptr_t ret = InlinedSyscall6(num, arg1, arg2, arg3, arg4, arg5, arg6);
  return ret;
}
#endif
}  // namespace base

#undef INLINED_SYSCALL_FPU_CLOBBER
#endif  // THIRD_PARTY_GLOOP_BASE_INLINED_SYSCALL_H_
