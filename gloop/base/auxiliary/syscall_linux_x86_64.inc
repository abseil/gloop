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

struct kernel_stat {
  unsigned long      st_dev;
  unsigned long      st_ino;
  unsigned long      st_nlink;
  unsigned           st_mode;
  unsigned           st_uid;
  unsigned           st_gid;
  unsigned           __pad0;
  unsigned long      st_rdev;
  long               st_size;
  long               st_blksize;
  long               st_blocks;
  unsigned long      st_atime_;
  unsigned long      st_atime_nsec_;
  unsigned long      st_mtime_;
  unsigned long      st_mtime_nsec_;
  unsigned long      st_ctime_;
  unsigned long      st_ctime_nsec_;
  long ___unused[3];
};

#ifndef ARCH_SET_GS
#define ARCH_SET_GS             0x1001
#endif
#ifndef ARCH_GET_GS
#define ARCH_GET_GS             0x1004
#endif

#ifndef __NR_pread64
#define __NR_pread64             17
#endif
#ifndef __NR_pwrite64
#define __NR_pwrite64            18
#endif
#ifndef __NR_setresuid
#define __NR_setresuid          117
#define __NR_getresuid          118
#define __NR_setresgid          119
#define __NR_getresgid          120
#endif
#ifndef __NR_mount
#define __NR_mount              165
#endif
#ifndef __NR_quotactl
#define __NR_quotactl           179
#endif
#ifndef __NR_gettid
#define __NR_gettid             186
#endif
#ifndef __NR_readahead
#define __NR_readahead          187
#endif
#ifndef __NR_setxattr
#define __NR_setxattr           188
#endif
#ifndef __NR_lsetxattr
#define __NR_lsetxattr          189
#endif
#ifndef __NR_getxattr
#define __NR_getxattr           191
#endif
#ifndef __NR_lgetxattr
#define __NR_lgetxattr          192
#endif
#ifndef __NR_listxattr
#define __NR_listxattr          194
#endif
#ifndef __NR_llistxattr
#define __NR_llistxattr         195
#endif
#ifndef __NR_tkill
#define __NR_tkill              200
#endif
#ifndef __NR_futex
#define __NR_futex              202
#endif
#ifndef __NR_sched_setaffinity
#define __NR_sched_setaffinity  203
#define __NR_sched_getaffinity  204
#endif
#ifndef __NR_io_setup
#define __NR_io_setup           206
#define __NR_io_destroy         207
#define __NR_io_getevents       208
#define __NR_io_submit          209
#define __NR_io_cancel          210
#endif
#ifndef __NR_getdents64
#define __NR_getdents64         217
#endif
#ifndef __NR_set_tid_address
#define __NR_set_tid_address    218
#endif
#ifndef __NR_fadvise64
#define __NR_fadvise64          221
#endif
#ifndef __NR_clock_gettime
#define __NR_clock_gettime      228
#endif
#ifndef __NR_clock_getres
#define __NR_clock_getres       229
#endif
#ifndef __NR_ioprio_set
#define __NR_ioprio_set         251
#endif
#ifndef __NR_ioprio_get
#define __NR_ioprio_get         252
#endif
#ifndef __NR_openat
#define __NR_openat             257
#endif
#ifndef __NR_newfstatat
#define __NR_newfstatat         262
#endif
#ifndef __NR_unlinkat
#define __NR_unlinkat           263
#endif
#ifndef __NR_unshare
#define __NR_unshare            272
#endif
#ifndef __NR_move_pages
#define __NR_move_pages         279
#endif
#ifndef __NR_fallocate
#define __NR_fallocate          285
#endif
#ifndef __NR_preadv
#define __NR_preadv             295
#endif
#ifndef __NR_pwritev
#define __NR_pwritev            296
#endif
#ifndef __NR_setns
#define __NR_setns              308
#endif

/* Failing system calls return a negative result in the range of
 * -1..-4095. These are "errno" values with the sign inverted.
 */
#define LSS_SET_ERRNO_AND_RETURN(type, res, out_errno)  \
  if ((unsigned long)(res) >= (unsigned long)(-4095)) { \
    *out_errno = (int)-res;                             \
    res = -1;                                           \
  }                                                     \
  return (type)res;

/* There are no known problems with any of the _syscallX() macros
 * currently shipping for x86_64, but we still need to be able to define
 * our own version so that we can override the location of the errno
 * location (e.g. when using the clone() system call with the CLONE_VM
 * option).
 */
#define LSS_BODY(res, name, ...)                                           \
  __asm__ __volatile__("syscall"                                           \
                       : "=a"(res)                                         \
                       : "0"(__NR_##name), ##__VA_ARGS__                   \
                       : "r11", "rcx", "memory");
#define LSS_syscall0(type, name)                      \
  static inline type lss_##name(int *out_errno) {     \
    long __res;                                       \
    LSS_PRE(name);                                    \
    LSS_BODY(__res, name);                            \
    LSS_POST(name, __res);                            \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno); \
  }
#define LSS_syscall1(type, name, type1, arg1)                 \
  static inline type lss_##name(type1 arg1, int *out_errno) { \
    long __res;                                               \
    LSS_PRE(name, arg1);                                      \
    LSS_BODY(__res, name, "D"((long)(arg1)));                 \
    LSS_POST(name, __res, arg1);                              \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);         \
  }
#define LSS_syscall2(type, name, type1, arg1, type2, arg2)                \
  static inline type lss_##name(type1 arg1, type2 arg2, int *out_errno) { \
    long __res;                                                           \
    LSS_PRE(name, arg1, arg2);                                            \
    LSS_BODY(__res, name, "D"((long)(arg1)), "S"((long)(arg2)));          \
    LSS_POST(name, __res, arg1, arg2);                                    \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);                     \
  }
#define LSS_syscall3(type, name, type1, arg1, type2, arg2, type3, arg3) \
  static inline type lss_##name(type1 arg1, type2 arg2, type3 arg3,     \
                                int *out_errno) {                       \
    long __res;                                                         \
    LSS_PRE(name, arg1, arg2, arg3);                                    \
    LSS_BODY(__res, name, "D"((long)(arg1)), "S"((long)(arg2)),         \
             "d"((long)(arg3)));                                        \
    LSS_POST(name, __res, arg1, arg2, arg3);                            \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);                   \
  }
#define LSS_syscall4(type, name, type1, arg1, type2, arg2, type3, arg3, type4, \
                     arg4)                                                     \
  static inline type lss_##name(type1 arg1, type2 arg2, type3 arg3,            \
                                type4 arg4, int *out_errno) {                  \
    long __res;                                                                \
    LSS_PRE(name, arg1, arg2, arg3, arg4);                                     \
    __asm__ __volatile__("movq %5,%%r10; syscall"                              \
                         : "=a"(__res)                                         \
                         : "0"(__NR_##name), "D"((long)(arg1)),                \
                           "S"((long)(arg2)), "d"((long)(arg3)),               \
                           "r"((long)(arg4))                                   \
                         : "r10", "r11", "rcx", "memory");                     \
    LSS_POST(name, __res, arg1, arg2, arg3, arg4);                             \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);                          \
  }
#define LSS_syscall5(type, name, type1, arg1, type2, arg2, type3, arg3, type4, \
                     arg4, type5, arg5)                                        \
  static inline type lss_##name(type1 arg1, type2 arg2, type3 arg3,            \
                                type4 arg4, type5 arg5, int *out_errno) {      \
    long __res;                                                                \
    LSS_PRE(name, arg1, arg2, arg3, arg4, arg5);                               \
    __asm__ __volatile__("movq %5,%%r10; movq %6,%%r8; syscall"                \
                         : "=a"(__res)                                         \
                         : "0"(__NR_##name), "D"((long)(arg1)),                \
                           "S"((long)(arg2)), "d"((long)(arg3)),               \
                           "r"((long)(arg4)), "r"((long)(arg5))                \
                         : "r8", "r10", "r11", "rcx", "memory");               \
    LSS_POST(name, __res, arg1, arg2, arg3, arg4, arg5);                       \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);                          \
  }
#define LSS_syscall6(type, name, type1, arg1, type2, arg2, type3, arg3, type4, \
                     arg4, type5, arg5, type6, arg6)                           \
  static inline type lss_##name(type1 arg1, type2 arg2, type3 arg3,            \
                                type4 arg4, type5 arg5, type6 arg6,            \
                                int *out_errno) {                              \
    long __res;                                                                \
    LSS_PRE(name, arg1, arg2, arg3, arg4, arg5, arg6);                         \
    __asm__ __volatile__(                                                      \
        "movq %5,%%r10; movq %6,%%r8; movq %7,%%r9;"                           \
        "syscall"                                                              \
        : "=a"(__res)                                                          \
        : "0"(__NR_##name), "D"((long)(arg1)), "S"((long)(arg2)),              \
          "d"((long)(arg3)), "r"((long)(arg4)), "r"((long)(arg5)),             \
          "r"((long)(arg6))                                                    \
        : "r8", "r9", "r10", "r11", "rcx", "memory");                          \
    LSS_POST(name, __res, arg1, arg2, arg3, arg4, arg5, arg6);                 \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);                          \
  }

static inline int lss_clone(int (*fn)(void *), void *child_stack, int flags,
                            void *arg, int *parent_tidptr, void *newtls,
                            int *child_tidptr, int *out_errno) {
  long __res;
  {
    if (fn == NULL || child_stack == NULL) {
      *out_errno = -EINVAL;
      return -1;
    }
    __asm__ __volatile__(/* Set up alignment of the child stack:
                          * child_stack = (child_stack & ~0xF) - 16;
                          */
                         "andq   $-16,%5\n"
                         "subq   $16,%5\n"

                         /* Push "arg" and "fn" onto the stack that will be
                          * used by the child.
                          */
                         "movq   %7,8(%5)\n"
                         "movq   %4,0(%5)\n"

                         /* %rax = syscall(%rax = __NR_clone,
                          *                %rdi = flags,
                          *                %rsi = child_stack,
                          *                %rdx = parent_tidptr,
                          *                %r8  = new_tls,
                          *                %r10 = child_tidptr)
                          */
                         "movq   %2,%%rax\n"
                         "movq   %9,%%r8\n"
                         "movq   %10,%%r10\n"
                         "syscall\n"

                         /* if (%rax != 0)
                          *   return;
                          */
                         "testq  %%rax,%%rax\n"
                         "jnz    1f\n"

                         /* In the child. Terminate frame pointer chain.
                          */
                         "xorq   %%rbp,%%rbp\n"

                         /* Clearing frame pointer is not sufficient to stop
                          * unwinding. http://b/18777254
                          */
#ifdef __GCC_HAVE_DWARF2_CFI_ASM
                        //  ".cfi_undefined %%rip\n"
#endif

                         /* Call "fn(arg)".
                          */
                         "popq   %%rax\n"
                         "popq   %%rdi\n"
                         "call   *%%rax\n"

                         /* Call _exit(%ebx).
                          */
                         "movq   %%rax,%%rdi\n"
                         "movq   %3,%%rax\n"
                         "syscall\n"

                         /* Return to parent.
                          */
                         "1:\n"
                         : "=a"(__res)
                         : "0"(-EINVAL), "i"(__NR_clone), "i"(__NR_exit),
                           "r"(fn), "S"(child_stack), "D"(flags), "r"(arg),
                           "d"(parent_tidptr), "g"(newtls), "g"(child_tidptr)
                         : "rsp", "memory", "r8", "r10", "r11", "rcx");
  }
  LSS_SET_ERRNO_AND_RETURN(int, __res, out_errno);
}

static inline void (*sys_restore_rt(void))(void) {
  /* On x86-64, the kernel does not know how to return from
   * a signal handler. Instead, it relies on user space to provide a
   * restorer function that calls the rt_sigreturn() system call.
   * Unfortunately, we cannot just reference the glibc version of this
   * function, as glibc goes out of its way to make it inaccessible.
   */
  void (*res)(void);
  __asm__ __volatile__(
      "jmp   2f\n"
      ".align 16\n"
      "1: movq %[NR_SIGRETURN],%%rax\n"
      "syscall\n"
      "2: leaq 1b(%%rip),%[RES]\n"
      : [RES] "=r"(res)
      : [NR_SIGRETURN] "i"(__NR_rt_sigreturn));
  return res;
}
