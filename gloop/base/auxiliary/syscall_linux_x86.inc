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
#define __NR_mount 21
#endif
#ifndef __NR_setgroups32
#define __NR_setgroups32 81
#endif
#ifndef __NR_quotactl
#define __NR_quotactl 131
#endif
#ifndef __NR_setresuid
#define __NR_setresuid 164
#define __NR_getresuid 165
#define __NR_setresgid 170
#define __NR_getresgid 171
#endif
#ifndef __NR_rt_sigaction
#define __NR_rt_sigreturn 173
#define __NR_rt_sigaction 174
#define __NR_rt_sigprocmask 175
#define __NR_rt_sigpending 176
#define __NR_rt_sigsuspend 179
#endif
#ifndef __NR_pread64
#define __NR_pread64 180
#endif
#ifndef __NR_pwrite64
#define __NR_pwrite64 181
#endif
#ifndef __NR_ugetrlimit
#define __NR_ugetrlimit 191
#endif
#ifndef __NR_stat64
#define __NR_stat64 195
#endif
#ifndef __NR_fstat64
#define __NR_fstat64 197
#endif
#ifndef __NR_setresuid32
#define __NR_setresuid32 208
#define __NR_getresuid32 209
#define __NR_setresgid32 210
#define __NR_getresgid32 211
#endif
#ifndef __NR_setfsuid32
#define __NR_setfsuid32 215
#define __NR_setfsgid32 216
#endif
#ifndef __NR_getdents64
#define __NR_getdents64 220
#endif
#ifndef __NR_gettid
#define __NR_gettid 224
#endif
#ifndef __NR_readahead
#define __NR_readahead 225
#endif
#ifndef __NR_setxattr
#define __NR_setxattr 226
#endif
#ifndef __NR_lsetxattr
#define __NR_lsetxattr 227
#endif
#ifndef __NR_getxattr
#define __NR_getxattr 229
#endif
#ifndef __NR_lgetxattr
#define __NR_lgetxattr 230
#endif
#ifndef __NR_listxattr
#define __NR_listxattr 232
#endif
#ifndef __NR_llistxattr
#define __NR_llistxattr 233
#endif
#ifndef __NR_tkill
#define __NR_tkill 238
#endif
#ifndef __NR_futex
#define __NR_futex 240
#endif
#ifndef __NR_sched_setaffinity
#define __NR_sched_setaffinity 241
#define __NR_sched_getaffinity 242
#endif
#ifndef __NR_io_setup
#define __NR_io_setup 245
#define __NR_io_destroy 246
#define __NR_io_getevents 247
#define __NR_io_submit 248
#define __NR_io_cancel 249
#endif
#ifndef __NR_set_tid_address
#define __NR_set_tid_address 258
#endif
#ifndef __NR_clock_gettime
#define __NR_clock_gettime 265
#endif
#ifndef __NR_clock_getres
#define __NR_clock_getres 266
#endif
#ifndef __NR_statfs64
#define __NR_statfs64 268
#endif
#ifndef __NR_fstatfs64
#define __NR_fstatfs64 269
#endif
#ifndef __NR_fadvise64_64
#define __NR_fadvise64_64 272
#endif
#ifndef __NR_ioprio_set
#define __NR_ioprio_set 289
#endif
#ifndef __NR_ioprio_get
#define __NR_ioprio_get 290
#endif
#ifndef __NR_openat
#define __NR_openat 295
#endif
#ifndef __NR_fstatat64
#define __NR_fstatat64 300
#endif
#ifndef __NR_unlinkat
#define __NR_unlinkat 301
#endif
#ifndef __NR_unshare
#define __NR_unshare 310
#endif
#ifndef __NR_move_pages
#define __NR_move_pages 317
#endif
#ifndef __NR_getcpu
#define __NR_getcpu 318
#endif
#ifndef __NR_fallocate
#define __NR_fallocate 324
#endif
#ifndef __NR_preadv
#define __NR_preadv 333
#endif
#ifndef __NR_pwritev
#define __NR_pwritev 334
#endif
#ifndef __NR_setns
#define __NR_setns 346
#endif

/* In PIC mode (e.g. when building shared libraries), gcc for i386
 * reserves ebx. Unfortunately, most distribution ship with implementations
 * of _syscallX() which clobber ebx.
 * Also, most definitions of _syscallX() neglect to mark "memory" as being
 * clobbered. This causes problems with compilers, that do a better job
 * at optimizing across __asm__ calls.
 * So, we just have to redefine all of the _syscallX() macros.
 */
#define LSS_BODY(type, args...) \
  type __res;                   \
  __asm__ __volatile__("int $0x80\n" args : "memory");

#define LSS_syscall0(type, name)                      \
  static inline type lss_##name(int* out_errno) {     \
    LSS_PRE(name);                                    \
    LSS_BODY(type, : "=a"(__res) : "0"(__NR_##name)); \
    LSS_POST(name, __res);                            \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno); \
  }

#define LSS_syscall1(type, name, type1, arg1)                    \
  static inline type lss_##name(type1 arg1, int* out_errno) {    \
    LSS_PRE(name, arg1);                                         \
    LSS_BODY(type, : "=a"(__res) : "0"(__NR_##name), "b"(arg1)); \
    LSS_POST(name, __res, arg1);                                 \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);            \
  }

#define LSS_syscall2(type, name, type1, arg1, type2, arg2)                  \
  static inline type lss_##name(type1 arg1, type2 arg2, int* out_errno) {   \
    LSS_PRE(name, arg1, arg2);                                              \
    LSS_BODY(type, : "=a"(__res) : "0"(__NR_##name), "b"(arg1), "c"(arg2)); \
    LSS_POST(name, __res, arg1, arg2);                                      \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);                       \
  }

#define LSS_syscall3(type, name, type1, arg1, type2, arg2, type3, arg3)    \
  static inline type lss_##name(type1 arg1, type2 arg2, type3 arg3,        \
                                int* out_errno) {                          \
    LSS_PRE(name, arg1, arg2, arg3);                                       \
    LSS_BODY(type, : "=a"(__res) : "0"(__NR_##name), "b"(arg1), "c"(arg2), \
             "d"(arg3));                                                   \
    LSS_POST(name, __res, arg1, arg2, arg3);                               \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);                      \
  }

#define LSS_syscall4(type, name, type1, arg1, type2, arg2, type3, arg3, type4, \
                     arg4)                                                     \
  static inline type lss_##name(type1 arg1, type2 arg2, type3 arg3,            \
                                type4 arg4, int* out_errno) {                  \
    LSS_PRE(name, arg1, arg2, arg3, arg4);                                     \
    LSS_BODY(type, : "=a"(__res) : "0"(__NR_##name), "b"(arg1), "c"(arg2),     \
             "d"(arg3), "S"(arg4));                                            \
    LSS_POST(name, __res, arg1, arg2, arg3, arg4);                             \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);                          \
  }

#define LSS_syscall5(type, name, type1, arg1, type2, arg2, type3, arg3, type4, \
                     arg4, type5, arg5)                                        \
  static inline type lss_##name(type1 arg1, type2 arg2, type3 arg3,            \
                                type4 arg4, type5 arg5, int* out_errno) {      \
    LSS_PRE(name, arg1, arg2, arg3, arg4, arg5);                               \
    /* For PIC we rely on the compiler to save/restore ebx */                  \
    LSS_BODY(type, : "=a"(__res) : "0"(__NR_##name), "b"(arg1), "c"(arg2),     \
             "d"(arg3), "S"(arg4), "D"(arg5));                                 \
    LSS_POST(name, __res, arg1, arg2, arg3, arg4, arg5);                       \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);                          \
  }

#define LSS_syscall6(type, name, type1, arg1, type2, arg2, type3, arg3, type4, \
                     arg4, type5, arg5, type6, arg6)                           \
  static inline type lss_##name(type1 arg1, type2 arg2, type3 arg3,            \
                                type4 arg4, type5 arg5, type6 arg6,            \
                                int* out_errno) {                              \
    LSS_PRE(name, arg1, arg2, arg3, arg4, arg5, arg6);                         \
    type __res;                                                                \
    /* Perform a register shuffle as using ebp as a constraint breaks LLVM. */ \
    __asm__ __volatile__(                                                      \
        "pushl %[M_ARG6]\n"                                                    \
        ".cfi_adjust_cfa_offset 4\n"                                           \
        "xchgl %%ebp, 0(%%esp)\n"                                              \
        "int $0x80\n"                                                          \
        "popl  %%ebp\n"                                                        \
        ".cfi_adjust_cfa_offset -4\n"                                          \
        : "=a"(__res)                                                          \
        : "0"(__NR_##name), "b"(arg1), "c"(arg2), "d"(arg3), "S"(arg4),        \
          "D"(arg5), [M_ARG6] "m"(arg6)                                        \
        : "memory");                                                           \
    LSS_POST(name, __res, arg1, arg2, arg3, arg4, arg5, arg6);                 \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);                          \
  }

static inline int lss_clone(int (*fn)(void*), void* child_stack, int flags,
                            void* arg, int* parent_tidptr, void* newtls,
                            int* child_tidptr, int* out_errno) {
  long __res;
  if (fn == NULL || child_stack == NULL) {
    *out_errno = -EINVAL;
    return -1;
  }
  /* set up alignment of child stack (16-byte aligned) */
  const intptr_t mask = ~((intptr_t)15);
  child_stack = (void*)(((intptr_t)child_stack) & mask);

  /* Push "arg" and "fn" onto the stack that will be
   * used by the child.
   */
  long* tmp_stack = (long*)child_stack;
  --tmp_stack;  // Pad.
  --tmp_stack;  // Pad.
  --tmp_stack;  // Pad.
  *(--tmp_stack) = (long)arg;
  *(--tmp_stack) = (long)fn;

  __asm__ __volatile__(
  /* %eax = syscall(%eax = __NR_clone,
   *                %ebx = flags,
   *                %ecx = child_stack,
   *                %edx = parent_tidptr,
   *                %esi = newtls,
   *                %edi = child_tidptr)
   */
#if defined(__PIC__)
      /* Shuffle flags into ebx while preserving ebp in the parent and the GOT
       * that is in ebx, as we want to have ebx contain the GOT in the child. */
      "pushl %[M_FLAGS]\n" /* stack[0] = flags */
      ".cfi_adjust_cfa_offset 4\n"
      "xchgl %%ebx, 0(%%esp)\n" /* ebx = flags, stack[0] = GOT */
      "xchgl %%ebp, 0(%%esp)\n" /* ebx = flags, ebp = GOT, stack[0] = ebp */
      "int $0x80\n"
      "movl  %%ebp, %%ebx\n" /* ebx = GOT */
      "popl  %%ebp\n"        /* restore ebp or in child ebp = fn */
      ".cfi_adjust_cfa_offset -4\n"
#else
      "int $0x80\n"
#endif
      /* if (%eax != 0)
       *   return %eax;
       */
      "test   %%eax,%%eax\n"
      "jnz    1f\n"
  /* In the child. %ecx = fn */
#if defined(__PIC__)
      "movl  %%ebp, %%ecx\n"
#else
      "popl  %%ecx\n"
#endif
      /* Terminate the call chain. */
      "xorl   %%ebp,%%ebp\n"
      /* Call "fn". "arg" is already on the stack. */
      "call   *%%ecx\n"
      /* Call _exit(%ebx). */
      "movl   %%eax,%%ebx\n"
      "movl   %[NR_EXIT],%%eax\n"
      "int    $0x80\n"
      /* Return to parent. */
      "1:"
      : "=a"(__res)
      : "0"(__NR_clone), [NR_EXIT] "i"(__NR_exit), "c"(tmp_stack),
        "d"(parent_tidptr), "S"(newtls), "D"(child_tidptr),
#if !defined(__PIC__)
        "b"(flags)
#else
        [M_FLAGS] "m"(flags)
#endif
  );
  LSS_SET_ERRNO_AND_RETURN(int, __res, out_errno);
}

static inline void (*sys_restore_rt(void))(void) {
  /* On i386, the kernel does not know how to return from a signal
   * handler. Instead, it relies on user space to provide a
   * restorer function that calls the {rt_,}sigreturn() system call.
   * Unfortunately, we cannot just reference the glibc version of this
   * function, as glibc goes out of its way to make it inaccessible.
   */
  void (*res)(void);
  __asm__ __volatile__(
      "call   2f\n"
      "0:.align 16\n"
      "1:movl   %1,%%eax\n"
      "int    $0x80\n"
      "2:popl   %0\n"
      "addl   $(1b-0b),%0\n"
      : "=a"(res)
      : "i"(__NR_rt_sigreturn));
  return res;
}

static inline void (*sys_restore(void))(void) {
  /* On i386, the kernel does not know how to return from a signal
   * handler. Instead, it relies on user space to provide a
   * restorer function that calls the {rt_,}sigreturn() system call.
   * Unfortunately, we cannot just reference the glibc version of this
   * function, as glibc goes out of its way to make it inaccessible.
   */
  void (*res)(void);
  __asm__ __volatile__(
      "call   2f\n"
      "0:.align 16\n"
      "1:pop    %%eax\n"
      "movl   %1,%%eax\n"
      "int    $0x80\n"
      "2:popl   %0\n"
      "addl   $(1b-0b),%0\n"
      : "=a"(res)
      : "i"(__NR_sigreturn));
  return res;
}
