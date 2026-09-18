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

#if !defined(__GLIBC__)
#define loff_t off_t
#endif

struct kernel_stat {
  unsigned long st_dev;
  unsigned long st_ino;
  unsigned int st_mode;
  unsigned int st_nlink;
  unsigned int st_uid;
  unsigned int st_gid;
  unsigned long st_rdev;
  unsigned long __pad1;
  long st_size;
  int st_blksize;
  int __pad2;
  long st_blocks;
  long st_atime_;
  unsigned long st_atime_nsec_;
  long st_mtime_;
  unsigned long st_mtime_nsec_;
  long st_ctime_;
  unsigned long st_ctime_nsec_;
  unsigned int __unused4;
  unsigned int __unused5;
};

/* Failing system calls return a negative result in the range of
 * -1..-4095. These are "errno" values with the sign inverted.
 */
#define LSS_SET_ERRNO_AND_RETURN(type, res, out_errno)  \
  if ((unsigned long)(res) >= (unsigned long)(-4095)) { \
    *out_errno = (int)-res;                             \
    res = -1;                                           \
  }                                                     \
  return (type)res;

#ifndef __syscall
#define __SYS_REG(name) register long __sysreg __asm__("x8") = __NR_##name;
#define __SYS_REG_LIST(regs...) [sysreg] "r"(__sysreg), ##regs
#define __syscall(name) "svc\t0\n"
#endif

/* Most definitions of _syscallX() neglect to mark "memory" as being
 * clobbered. This causes problems with compilers, that do a better job
 * at optimizing across __asm__ calls.
 * So, we just have to redefine all of the _syscallX() macros.
 */
#define LSS_REG(r, a) register long __x##r __asm__("x" #r) = (long)a

#define LSS_SAVE_ARG(r, a) long __tmp##r = (long)a
#define LSS_LOAD_ARG(r) register long __x##r __asm__("x" #r) = __tmp##r

#define LSS_BODY(type, name, args...)         \
  register long __res_r0 __asm__("x0");       \
  long __res;                                 \
  __SYS_REG(name)                             \
  __asm__ __volatile__(__syscall(name)        \
                       : "=r"(__res_r0)       \
                       : __SYS_REG_LIST(args) \
                       : "memory");           \
  __res = __res_r0;
#define LSS_syscall0(type, name)                      \
  static inline type lss_##name(int* out_errno) {     \
    LSS_PRE(name);                                    \
    LSS_BODY(type, name);                             \
    LSS_POST(name, __res);                            \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno); \
  }
#define LSS_syscall1(type, name, type1, arg1)                 \
  static inline type lss_##name(type1 arg1, int* out_errno) { \
    LSS_PRE(name, arg1);                                      \
    /* There is no need for using a volatile temp.  */        \
    LSS_REG(0, arg1);                                         \
    LSS_BODY(type, name, "r"(__x0));                          \
    LSS_POST(name, __res, arg1);                              \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);         \
  }
#define LSS_syscall2(type, name, type1, arg1, type2, arg2)                \
  static inline type lss_##name(type1 arg1, type2 arg2, int* out_errno) { \
    LSS_PRE(name, arg1, arg2);                                            \
    LSS_SAVE_ARG(0, arg1);                                                \
    LSS_SAVE_ARG(1, arg2);                                                \
    LSS_LOAD_ARG(0);                                                      \
    LSS_LOAD_ARG(1);                                                      \
    LSS_BODY(type, name, "r"(__x0), "r"(__x1));                           \
    LSS_POST(name, __res, arg1, arg2);                                    \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);                     \
  }
#define LSS_syscall3(type, name, type1, arg1, type2, arg2, type3, arg3) \
  static inline type lss_##name(type1 arg1, type2 arg2, type3 arg3,     \
                                int* out_errno) {                       \
    LSS_PRE(name, arg1, arg2, arg3);                                    \
    LSS_SAVE_ARG(0, arg1);                                              \
    LSS_SAVE_ARG(1, arg2);                                              \
    LSS_SAVE_ARG(2, arg3);                                              \
    LSS_LOAD_ARG(0);                                                    \
    LSS_LOAD_ARG(1);                                                    \
    LSS_LOAD_ARG(2);                                                    \
    LSS_BODY(type, name, "r"(__x0), "r"(__x1), "r"(__x2));              \
    LSS_POST(name, __res, arg1, arg2, arg3);                            \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);                   \
  }
#define LSS_syscall4(type, name, type1, arg1, type2, arg2, type3, arg3, type4, \
                     arg4)                                                     \
  static inline type lss_##name(type1 arg1, type2 arg2, type3 arg3,            \
                                type4 arg4, int* out_errno) {                  \
    LSS_PRE(name, arg1, arg2, arg3, arg4);                                     \
    LSS_SAVE_ARG(0, arg1);                                                     \
    LSS_SAVE_ARG(1, arg2);                                                     \
    LSS_SAVE_ARG(2, arg3);                                                     \
    LSS_SAVE_ARG(3, arg4);                                                     \
    LSS_LOAD_ARG(0);                                                           \
    LSS_LOAD_ARG(1);                                                           \
    LSS_LOAD_ARG(2);                                                           \
    LSS_LOAD_ARG(3);                                                           \
    LSS_BODY(type, name, "r"(__x0), "r"(__x1), "r"(__x2), "r"(__x3));          \
    LSS_POST(name, __res, arg1, arg2, arg3, arg4);                             \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);                          \
  }
#define LSS_syscall5(type, name, type1, arg1, type2, arg2, type3, arg3, type4, \
                     arg4, type5, arg5)                                        \
  static inline type lss_##name(type1 arg1, type2 arg2, type3 arg3,            \
                                type4 arg4, type5 arg5, int* out_errno) {      \
    LSS_PRE(name, arg1, arg2, arg3, arg4, arg5);                               \
    LSS_SAVE_ARG(0, arg1);                                                     \
    LSS_SAVE_ARG(1, arg2);                                                     \
    LSS_SAVE_ARG(2, arg3);                                                     \
    LSS_SAVE_ARG(3, arg4);                                                     \
    LSS_SAVE_ARG(4, arg5);                                                     \
    LSS_LOAD_ARG(0);                                                           \
    LSS_LOAD_ARG(1);                                                           \
    LSS_LOAD_ARG(2);                                                           \
    LSS_LOAD_ARG(3);                                                           \
    LSS_LOAD_ARG(4);                                                           \
    LSS_BODY(type, name, "r"(__x0), "r"(__x1), "r"(__x2), "r"(__x3),           \
             "r"(__x4));                                                       \
    LSS_POST(name, __res, arg1, arg2, arg3, arg4, arg5);                       \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);                          \
  }
#define LSS_syscall6(type, name, type1, arg1, type2, arg2, type3, arg3, type4, \
                     arg4, type5, arg5, type6, arg6)                           \
  static inline type lss_##name(type1 arg1, type2 arg2, type3 arg3,            \
                                type4 arg4, type5 arg5, type6 arg6,            \
                                int* out_errno) {                              \
    LSS_PRE(name, arg1, arg2, arg3, arg4, arg5, arg6);                         \
    LSS_SAVE_ARG(0, arg1);                                                     \
    LSS_SAVE_ARG(1, arg2);                                                     \
    LSS_SAVE_ARG(2, arg3);                                                     \
    LSS_SAVE_ARG(3, arg4);                                                     \
    LSS_SAVE_ARG(4, arg5);                                                     \
    LSS_SAVE_ARG(5, arg6);                                                     \
    LSS_LOAD_ARG(0);                                                           \
    LSS_LOAD_ARG(1);                                                           \
    LSS_LOAD_ARG(2);                                                           \
    LSS_LOAD_ARG(3);                                                           \
    LSS_LOAD_ARG(4);                                                           \
    LSS_LOAD_ARG(5);                                                           \
    LSS_BODY(type, name, "r"(__x0), "r"(__x1), "r"(__x2), "r"(__x3),           \
             "r"(__x4), "r"(__x5));                                            \
    LSS_POST(name, __res, arg1, arg2, arg3, arg4, arg5, arg6);                 \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);                          \
  }

static inline int lss_clone(int (*fn)(void*), void* child_stack, int flags,
                            void* arg, int* parent_tidptr, void* newtls,
                            int* child_tidptr, int* out_errno) {
  long __res;
  {
    if (fn == NULL || child_stack == NULL) {
      *out_errno = -EINVAL;
      return -1;
    }

    /* set up alignment of child stack (16-byte aligned) */
    const intptr_t mask = ~((intptr_t)15);
    child_stack = (void*)(((intptr_t)child_stack) & mask);

    /* stash first 4 arguments on stack first because we can only load
     * them after all function calls.
     */
    long tmp_flags = flags;
    long* tmp_stack = (long*)child_stack;
    void* tmp_ptid = parent_tidptr;
    void* tmp_tls = newtls;

    register int* __ctid __asm__("x4") = child_tidptr;

    /* Push "arg" and "fn" onto the stack that will be
     * used by the child.
     */
    *(--tmp_stack) = (long)arg;
    *(--tmp_stack) = (long)fn;

    /* We must load r0..r3 last after all possible function calls.  */
    register long __flags __asm__("x0") = tmp_flags;
    register void* __stack __asm__("x1") = tmp_stack;
    register void* __ptid __asm__("x2") = tmp_ptid;
    register void* __tls __asm__("x3") = tmp_tls;

    /* %r0 = syscall(%x0 = flags,
     *               %x1 = child_stack,
     *               %x2 = parent_tidptr,
     *               %x3 = newtls,
     *               %x4 = child_tidptr)
     */
    __SYS_REG(clone)
    __asm__ __volatile__(/* %x0 = syscall(%x0 = flags,
                          *               %x1 = child_stack,
                          *               %x2 = parent_tidptr,
                          *               %x3 = newtls,
                          *               %x4 = child_tidptr)
                          */
                         __syscall(clone)"\n"

                         /* if (%r0 != 0)
                          *   return %r0;
                          */
                         "cbnz  w0, 1f\n"

                         /* In the child, now. Call "fn(arg)".
                          */
                         "ldr x1, [sp]\n"
                         "ldr x0, [sp, 8]\n"
                         "blr x1\n"

                         /* Call _exit(%x0), which never returns.  */
                         "mov   x8, %2\n"
                         __syscall(exit)"\n"

                       "1:\n"
                         "mov  %0, x0\n"
                         : "=r" (__res)
                         : "r"(__sysreg),
                           "i"(__NR_exit), "r"(__stack), "r"(__flags),
                           "r"(__ptid), "r"(__tls), "r"(__ctid)
                         : "cc", "memory");
  }
  LSS_SET_ERRNO_AND_RETURN(int, __res, out_errno);
}

/* aarch64 does not have fork, we need to use clone to emulate
 * it.  Unfortunately, this is very platform specific and cannot be
 * done in a generic compatibility wrapper.
 */
static inline int lss_fork(int* out_errno) {
  LSS_PRE(fork);
  LSS_REG(0, SIGCHLD); /* clone_flags   */
  LSS_REG(1, 0);       /* newsp         */
  LSS_REG(2, NULL);    /* parent_tidptr */
  LSS_REG(3, 0);       /* tls_val       */
  LSS_REG(4, NULL);    /* child_tidptr  */
  LSS_BODY(int, clone, "r"(__x0), "r"(__x1), "r"(__x2), "r"(__x3), "r"(__x4));
  LSS_POST(fork, __res);
  LSS_SET_ERRNO_AND_RETURN(int, __res, out_errno);
}
SYS_WRAP_LSS0(int, fork);
