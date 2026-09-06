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

/* Only #included by base/linux_syscall_support.h */
// IWYU pragma: private, include "base/linux_syscall_support.h"

/* Failing system calls return a negative result in the range of
 * -1..-4095. These are "errno" values with the sign inverted.
 */
#define LSS_SET_ERRNO_AND_RETURN(type, res, out_errno)  \
  if ((unsigned long)(res) >= (unsigned long)(-4095)) { \
    *out_errno = -((int)res);                           \
    res = (type) - 1;                                   \
  }                                                     \
  return res;

#ifndef __NR_mount
#define __NR_mount (__NR_SYSCALL_BASE + 21)
#endif
#ifndef __NR_setresuid
#define __NR_setresuid (__NR_SYSCALL_BASE + 164)
#define __NR_getresuid (__NR_SYSCALL_BASE + 165)
#define __NR_setresgid (__NR_SYSCALL_BASE + 170)
#define __NR_getresgid (__NR_SYSCALL_BASE + 171)
#endif
#ifndef __NR_rt_sigaction
#define __NR_rt_sigreturn (__NR_SYSCALL_BASE + 173)
#define __NR_rt_sigaction (__NR_SYSCALL_BASE + 174)
#define __NR_rt_sigprocmask (__NR_SYSCALL_BASE + 175)
#define __NR_rt_sigpending (__NR_SYSCALL_BASE + 176)
#define __NR_rt_sigsuspend (__NR_SYSCALL_BASE + 179)
#endif
#ifndef __NR_pread64
#define __NR_pread64 (__NR_SYSCALL_BASE + 180)
#endif
#ifndef __NR_pwrite64
#define __NR_pwrite64 (__NR_SYSCALL_BASE + 181)
#endif
#ifndef __NR_capset
#define __NR_capset (__NR_SYSCALL_BASE + 185)
#endif
#ifndef __NR_ugetrlimit
#define __NR_ugetrlimit (__NR_SYSCALL_BASE + 191)
#endif
#ifndef __NR_stat64
#define __NR_stat64 (__NR_SYSCALL_BASE + 195)
#endif
#ifndef __NR_fstat64
#define __NR_fstat64 (__NR_SYSCALL_BASE + 197)
#endif
#ifndef __NR_setgroups32
#define __NR_setgroups32 (__NR_SYSCALL_BASE + 206)
#endif
#ifndef __NR_setresuid32
#define __NR_setresuid32 (__NR_SYSCALL_BASE + 208)
#define __NR_getresuid32 (__NR_SYSCALL_BASE + 209)
#define __NR_setresgid32 (__NR_SYSCALL_BASE + 210)
#define __NR_getresgid32 (__NR_SYSCALL_BASE + 211)
#endif
#ifndef __NR_setfsuid32
#define __NR_setfsuid32 (__NR_SYSCALL_BASE + 215)
#define __NR_setfsgid32 (__NR_SYSCALL_BASE + 216)
#endif
#ifndef __NR_getdents64
#define __NR_getdents64 (__NR_SYSCALL_BASE + 217)
#endif
#ifndef __NR_gettid
#define __NR_gettid (__NR_SYSCALL_BASE + 224)
#endif
#ifndef __NR_readahead
#define __NR_readahead (__NR_SYSCALL_BASE + 225)
#endif
#ifndef __NR_setxattr
#define __NR_setxattr (__NR_SYSCALL_BASE + 226)
#endif
#ifndef __NR_lsetxattr
#define __NR_lsetxattr (__NR_SYSCALL_BASE + 227)
#endif
#ifndef __NR_getxattr
#define __NR_getxattr (__NR_SYSCALL_BASE + 229)
#endif
#ifndef __NR_lgetxattr
#define __NR_lgetxattr (__NR_SYSCALL_BASE + 230)
#endif
#ifndef __NR_listxattr
#define __NR_listxattr (__NR_SYSCALL_BASE + 232)
#endif
#ifndef __NR_llistxattr
#define __NR_llistxattr (__NR_SYSCALL_BASE + 233)
#endif
#ifndef __NR_tkill
#define __NR_tkill (__NR_SYSCALL_BASE + 238)
#endif
#ifndef __NR_futex
#define __NR_futex (__NR_SYSCALL_BASE + 240)
#endif
#ifndef __NR_sched_setaffinity
#define __NR_sched_setaffinity (__NR_SYSCALL_BASE + 241)
#define __NR_sched_getaffinity (__NR_SYSCALL_BASE + 242)
#endif
#ifndef __NR_set_tid_address
#define __NR_set_tid_address (__NR_SYSCALL_BASE + 256)
#endif
#ifndef __NR_clock_gettime
#define __NR_clock_gettime (__NR_SYSCALL_BASE + 263)
#endif
#ifndef __NR_clock_getres
#define __NR_clock_getres (__NR_SYSCALL_BASE + 264)
#endif
#ifndef __NR_statfs64
#define __NR_statfs64 (__NR_SYSCALL_BASE + 266)
#endif
#ifndef __NR_fstatfs64
#define __NR_fstatfs64 (__NR_SYSCALL_BASE + 267)
#endif
#ifndef __NR_ioprio_set
#define __NR_ioprio_set (__NR_SYSCALL_BASE + 314)
#endif
#ifndef __NR_ioprio_get
#define __NR_ioprio_get (__NR_SYSCALL_BASE + 315)
#endif
#ifndef __NR_unshare
#define __NR_unshare (__NR_SYSCALL_BASE + 337)
#endif
#ifndef __NR_move_pages
#define __NR_move_pages (__NR_SYSCALL_BASE + 344)
#endif
#ifndef __NR_setns
#define __NR_setns (__NR_SYSCALL_BASE + 375)
#endif

/* TODO: find a means to determine when CFI generation won't fail a
 * build, such as #if optimized. */
#if defined(CFI_DIRECTIVES_ALLOWED)
#define CFI_REGISTER(reg1, reg2) ".cfi_register " #reg1 ", " #reg2 "\n"
#define CFI_RESTORE(reg) ".cfi_restore " #reg "\n"
#else
#define CFI_REGISTER(reg1, reg2)
#define CFI_RESTORE(reg)
#endif

#if (defined(__thumb__) || defined(__thumb2__)) && !defined(NO_FRAME_POINTER)
/* R7 may be the frame pointer with thumb, so it needs to be explicitly saved
 * and restored */
#define LSS_LOADARGS_0(name)
#else
#define LSS_LOADARGS_0(name) \
  register unsigned long __sc_7 __asm__("r7") = __NR_##name
#endif
#define LSS_LOADARGS_1(name, arg1) \
  LSS_LOADARGS_0(name);            \
  register unsigned long __sc_0 __asm__("r0") = (unsigned long)(arg1)
#define LSS_LOADARGS_2(name, arg1, arg2) \
  LSS_LOADARGS_1(name, arg1);            \
  register unsigned long __sc_1 __asm__("r1") = (unsigned long)(arg2)
#define LSS_LOADARGS_3(name, arg1, arg2, arg3) \
  LSS_LOADARGS_2(name, arg1, arg2);            \
  register unsigned long __sc_2 __asm__("r2") = (unsigned long)(arg3)
#define LSS_LOADARGS_4(name, arg1, arg2, arg3, arg4) \
  LSS_LOADARGS_3(name, arg1, arg2, arg3);            \
  register unsigned long __sc_3 __asm__("r3") = (unsigned long)(arg4)
#define LSS_LOADARGS_5(name, arg1, arg2, arg3, arg4, arg5) \
  LSS_LOADARGS_4(name, arg1, arg2, arg3, arg4);            \
  register unsigned long __sc_4 __asm__("r4") = (unsigned long)(arg5)
#define LSS_LOADARGS_6(name, arg1, arg2, arg3, arg4, arg5, arg6) \
  LSS_LOADARGS_5(name, arg1, arg2, arg3, arg4, arg5);            \
  register unsigned long __sc_5 __asm__("r5") = (unsigned long)(arg6)

/* Give a string version of the argument */
#define STRINGIFY(x) #x
/* Causes x to be evaluated and turned into its value, for example
 * STRINGIFY(__LINE__) yields "__LINE__" where as TOSTRING(__LINE__) yields
 * "169" */
#define TOSTRING(x) STRINGIFY(x)

#if (defined(__thumb__) || defined(__thumb2__)) && !defined(NO_FRAME_POINTER)
/* Thumb with frame pointer variant. Save restore frame pointer, so it may hold
 * the syscall number. */
#define LSS_syscall0(type, name)                        \
  static inline type lss_##name(int* out_errno) {       \
    LSS_PRE(name);                                      \
    LSS_LOADARGS_0(name);                               \
    register type __res __asm__("r0");                  \
    __asm__ __volatile__(                             \
        "mov ip, r7\n"                                \
        CFI_REGISTER(r7, ip)                          \
        "ldr r7, =" TOSTRING(__NR_##name) "\n"        \
        "swi #0\n"                                    \
        "mov r7, ip\n"                                \
        CFI_RESTORE(r7)                               \
        : "=r"(__res)                                 \
        :                                             \
        : "ip", "memory"); \
    LSS_POST(name, __res);                              \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);   \
  }
#else
#define LSS_syscall0(type, name)                                              \
  static inline type lss_##name(int* out_errno) {                             \
    LSS_PRE(name);                                                            \
    LSS_LOADARGS_0(name);                                                     \
    register type __res __asm__("r0");                                        \
    __asm__ __volatile__("swi\t#0\n" : "=r"(__res) : "r"(__sc_7) : "memory"); \
    LSS_POST(name, __res);                                                    \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);                         \
  }
#endif

#if (defined(__thumb__) || defined(__thumb2__)) && !defined(NO_FRAME_POINTER)
/* Thumb with frame pointer variant. Save restore frame pointer, so it may hold
 * the syscall number. */
#define LSS_BODY(type, name, args...) \
  register type __res __asm__("r0");  \
  __asm__ __volatile__(                      \
      "mov ip, r7\n"                         \
      CFI_REGISTER(r7, ip)                   \
      "ldr r7, =" TOSTRING(__NR_##name) "\n" \
      "swi #0\n"                             \
      "mov r7, ip\n"                         \
      CFI_RESTORE(r7)                        \
      : "=r"(__res)                          \
      : args                                 \
      : "ip", "memory")
#else
#define LSS_BODY(type, name, args...)      \
  register type __res __asm__("r0");       \
  __asm__ __volatile__("swi #0\n"          \
                       : "=r"(__res)       \
                       : "r"(__sc_7), args \
                       : "memor"           \
                         "y")
#endif

#define LSS_syscall1(type, name, type1, arg1)                 \
  static inline type lss_##name(type1 arg1, int* out_errno) { \
    LSS_PRE(name, arg1);                                      \
    LSS_LOADARGS_1(name, arg1);                               \
    LSS_BODY(type, name, "r"(__sc_0));                        \
    LSS_POST(name, __res, arg1);                              \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);         \
  }

#define LSS_syscall2(type, name, type1, arg1, type2, arg2)                \
  static inline type lss_##name(type1 arg1, type2 arg2, int* out_errno) { \
    LSS_PRE(name, arg1, arg2);                                            \
    LSS_LOADARGS_2(name, arg1, arg2);                                     \
    LSS_BODY(type, name, "r"(__sc_0), "r"(__sc_1));                       \
    LSS_POST(name, __res, arg1, arg2);                                    \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);                     \
  }

#define LSS_syscall3(type, name, type1, arg1, type2, arg2, type3, arg3) \
  static inline type lss_##name(type1 arg1, type2 arg2, type3 arg3,     \
                                int* out_errno) {                       \
    LSS_PRE(name, arg1, arg2, arg3);                                    \
    LSS_LOADARGS_3(name, arg1, arg2, arg3);                             \
    LSS_BODY(type, name, "r"(__sc_0), "r"(__sc_1), "r"(__sc_2));        \
    LSS_POST(name, __res, arg1, arg2, arg3);                            \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);                   \
  }

#define LSS_syscall4(type, name, type1, arg1, type2, arg2, type3, arg3, type4, \
                     arg4)                                                     \
  static inline type lss_##name(type1 arg1, type2 arg2, type3 arg3,            \
                                type4 arg4, int* out_errno) {                  \
    LSS_PRE(name, arg1, arg2, arg3, arg4);                                     \
    LSS_LOADARGS_4(name, arg1, arg2, arg3, arg4);                              \
    LSS_BODY(type, name, "r"(__sc_0), "r"(__sc_1), "r"(__sc_2), "r"(__sc_3));  \
    LSS_POST(name, __res, arg1, arg2, arg3, arg4);                             \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);                          \
  }

#define LSS_syscall5(type, name, type1, arg1, type2, arg2, type3, arg3, type4, \
                     arg4, type5, arg5)                                        \
  static inline type lss_##name(type1 arg1, type2 arg2, type3 arg3,            \
                                type4 arg4, type5 arg5, int* out_errno) {      \
    LSS_PRE(name, arg1, arg2, arg3, arg4, arg5);                               \
    LSS_LOADARGS_5(name, arg1, arg2, arg3, arg4, arg5);                        \
    LSS_BODY(type, name, "r"(__sc_0), "r"(__sc_1), "r"(__sc_2), "r"(__sc_3),   \
             "r"(__sc_4));                                                     \
    LSS_POST(name, __res, arg1, arg2, arg3, arg4, arg5);                       \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);                          \
  }

#define LSS_syscall6(type, name, type1, arg1, type2, arg2, type3, arg3, type4, \
                     arg4, type5, arg5, type6, arg6)                           \
  static inline type lss_##name(type1 arg1, type2 arg2, type3 arg3,            \
                                type4 arg4, type5 arg5, type6 arg6,            \
                                int* out_errno) {                              \
    LSS_PRE(name, arg1, arg2, arg3, arg4, arg5, arg6);                         \
    LSS_LOADARGS_6(name, arg1, arg2, arg3, arg4, arg5, arg6);                  \
    LSS_BODY(type, name, "r"(__sc_0), "r"(__sc_1), "r"(__sc_2), "r"(__sc_3),   \
             "r"(__sc_4), "r"(__sc_5));                                        \
    LSS_POST(name, __res, arg1, arg2, arg3, arg4, arg5, arg6);                 \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);                          \
  }

static inline int lss_clone(int (*fn)(void*), void* child_stack, int flags,
                            void* arg, int* parent_tidptr, void* newtls,
                            int* child_tidptr, int* out_errno) {
  if (fn == NULL || child_stack == NULL) {
    *out_errno = -EINVAL;
    return -1;
  }

  /* Push "arg" and "fn" onto the stack that will be
   * used by the child.
   */
  long* tmp_stack = (long*)child_stack;
  *(--tmp_stack) = (long)arg;
  *(--tmp_stack) = (long)fn;

  int __res;
#if !(defined(__thumb__) || defined(__thumb2__)) || defined(NO_FRAME_POINTER)
  register long __sc_7 __asm__("r7") = __NR_clone;
#endif
  register int __flags __asm__("r0") = flags;
  register void* __stack __asm__("r1") = tmp_stack;
  register void* __ptid __asm__("r2") = parent_tidptr;
  register void* __tls __asm__("r3") = newtls;
  register int* __ctid __asm__("r4") = child_tidptr;
  __asm__ __volatile__(
#if !(defined(__thumb__) || defined(__thumb2__)) || defined(NO_FRAME_POINTER)
      "swi #0\n"
#else
      /* Save restore frame pointer, so it may hold the syscall number. */
      "mov ip, r7\n"
      CFI_REGISTER(r7, ip)
      "ldr r7, =" TOSTRING(__NR_clone) "\n"
      "swi #0\n"
      "mov r7, ip\n"
      CFI_RESTORE(r7)
#endif
      /* if (%r0 != 0)
       *   return %r0;
       */
      "movs  %0,r0\n"
      "bne   1f\n"
      /* In the child, now. Call "fn(arg)". Clobbers ip(r12)
       * the intra-procedure scratch register which the child
       * won't mind and the parent will never see. */
      "ldr   r0,[sp, #4]\n"
      "ldr   ip,[sp]\n"
      "blx   ip\n"
      /* Call _exit(%r0), which never returns and clobbering r7 doesn't
       * matter. */
      "ldr r7, =" TOSTRING(__NR_exit) "\n"
      "swi #0\n"
      /* In the parent. */
      "1:"
      : "=r"(__res)
      : "r"(__flags), "r"(__stack), "r"(__ptid), "r"(__tls), "r"(__ctid)
#if !(defined(__thumb__) || defined(__thumb2__)) || defined(NO_FRAME_POINTER)
        , "r"(__sc_7)
      : "memory"
#else
      : "ip", "memory"
#endif
  );
  LSS_SET_ERRNO_AND_RETURN(int, __res, out_errno);
}
