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

/* Failing system calls return a negative result in the range of
 * -1..-4095. These are "errno" values with the sign inverted.
 */
#define LSS_SET_ERRNO_AND_RETURN(type, res, out_errno) \
  if ((uint64_t)(res) >= (uint64_t)(-4095)) {          \
    *out_errno = (int)-res;                            \
    res = -1;                                          \
  }                                                    \
  return (type)res;

#define __NR_setxattr 5
#define __NR_lsetxattr 6
#define __NR_getxattr 8
#define __NR_lgetxattr 9
#define __NR_listxattr 11
#define __NR_llistxattr 12
#define __NR_dup 23
#define __NR_dup3 24
#ifndef __NR_fcntl
#define __NR_fcntl 25
#endif
#define __NR_ioctl 29
#define __NR_ioprio_set 30
#define __NR_ioprio_get 31
#define __NR_mount 40
#ifndef __NR_statfs
#define __NR_statfs 43
#endif
#ifndef __NR_fstatfs
#define __NR_fstatfs 44
#endif
#ifndef __NR_ftruncate
#define __NR_ftruncate 46
#endif
#define __NR_fallocate 47
#define __NR_chdir 49
#define __NR_chroot 51
#define __NR_openat 56
#define __NR_close 57
#define __NR_pipe2 59
#define __NR_getdents64 61
#ifndef __NR_lseek
#define __NR_lseek 62
#endif
#define __NR_read 63
#define __NR_write 64
#define __NR_writev 66
#define __NR_pread64 67
#define __NR_pwrite64 68
#define __NR_ppoll 73
#define __NR_readlinkat 78
#ifndef __NR_newfstatat
#define __NR_newfstatat 79
#endif
#define __NR_fdatasync 83
#define __NR_capset 91
#define __NR_exit 93
#define __NR_exit_group 94
#define __NR_set_tid_address 96
#define __NR_unshare 97
#define __NR_futex 98
#define __NR_nanosleep 101
#define __NR_getitimer 102
#define __NR_setitimer 103
#define __NR_clock_gettime 113
#define __NR_clock_getres 114
#define __NR_ptrace 117
#define __NR_sched_setaffinity 122
#define __NR_sched_getaffinity 123
#define __NR_sched_yield 124
#define __NR_kill 129
#define __NR_tkill 130
#define __NR_tgkill 131
#define __NR_sigaltstack 132
#define __NR_rt_sigsuspend 133
#define __NR_rt_sigaction 134
#define __NR_rt_sigprocmask 135
#define __NR_rt_sigpending 136
#define __NR_rt_sigreturn 139
#define __NR_setpriority 140
#define __NR_getpriority 141
#define __NR_setgid 144
#define __NR_setuid 146
#define __NR_setresuid 147
#define __NR_getresuid 148
#define __NR_setresgid 149
#define __NR_getresgid 150
#define __NR_setfsuid 151
#define __NR_setfsgid 152
#define __NR_setpgid 154
#define __NR_getpgid 155
#define __NR_getsid 156
#define __NR_setsid 157
#define __NR_getrlimit 163
#define __NR_setrlimit 164
#define __NR_umask 166
#define __NR_prctl 167
#define __NR_gettimeofday 169
#define __NR_getpid 172
#define __NR_getppid 173
#define __NR_geteuid 175
#define __NR_getegid 177
#define __NR_gettid 178
#define __NR_socket 198
#define __NR_socketpair 199
#define __NR_sendto 206
#define __NR_shutdown 210
#define __NR_sendmsg 211
#define __NR_recvmsg 212
#define __NR_readahead 213
#define __NR_brk 214
#define __NR_munmap 215
#define __NR_mremap 216
#define __NR_clone 220
#define __NR_execve 221
#ifndef __NR_mmap
#define __NR_mmap 222
#endif
#define __NR_mprotect 226
#define __NR_move_pages 239
#define __NR_wait4 260
#define __NR_setns 268

#ifndef __syscall
#define __SYS_REG(name) register int64_t __sysreg __asm__("a7") = __NR_##name;
#define __SYS_REG_LIST(regs...) [sysreg] "r"(__sysreg), ##regs
#define __syscall(name) "scall\n"
#endif

#define LSS_REG(r, a) register int64_t __r##r __asm__("a" #r) = (int64_t)a

#define LSS_SAVE_ARG(r, a) int64_t __tmp##r = (int64_t)a
#define LSS_LOAD_ARG(r) register int64_t __r##r __asm__("a" #r) = __tmp##r

#define LSS_BODY(type, name, args...)         \
  register int64_t __res_a0 __asm__("a0");    \
  int64_t __res;                              \
  __SYS_REG(name)                             \
  __asm__ __volatile__(__syscall(name)        \
                       : "=r"(__res_a0)       \
                       : __SYS_REG_LIST(args) \
                       : "memory");           \
  __res = __res_a0;
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
    LSS_BODY(type, name, "r"(__r0));                          \
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
    LSS_BODY(type, name, "r"(__r0), "r"(__r1));                           \
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
    LSS_BODY(type, name, "r"(__r0), "r"(__r1), "r"(__r2));              \
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
    LSS_BODY(type, name, "r"(__r0), "r"(__r1), "r"(__r2), "r"(__r3));          \
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
    LSS_BODY(type, name, "r"(__r0), "r"(__r1), "r"(__r2), "r"(__r3),           \
             "r"(__r4));                                                       \
    LSS_POST(name, __res, arg1, arg2, arg3, arg4, arg5);                       \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);                          \
  }
#define LSS_syscall6(type, name, type1, arg1, type2, arg2, type3, arg3, type4, \
                     arg4, type5, arg5, type6, arg6)                           \
  static inline type lss_##name(type1 arg1, type2 arg2, type3 arg3,            \
                                type4 arg4, type5 arg5, type6 arg6,            \
                                int* out_errno) {                              \
    LSS_PRE(name, arg1, arg2, arg3, arg4, arg5);                               \
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
    LSS_BODY(type, name, "r"(__r0), "r"(__r1), "r"(__r2), "r"(__r3),           \
             "r"(__r4), "r"(__r5));                                            \
    LSS_POST(name, __res, arg1, arg2, arg3, arg4, arg5, arg6);                 \
    LSS_SET_ERRNO_AND_RETURN(type, __res, out_errno);                          \
  }

static inline int lss_clone(int (*fn)(void*), void* child_stack, int flags,
                            void* arg, int* parent_tidptr, void* newtls,
                            int* child_tidptr, int* out_errno) {
  int64_t __res;
  {
    if (fn == NULL || child_stack == NULL) {
      *out_errno = -EINVAL;
      return -1;
    }

    register int64_t __res_a0 __asm__("a0");

    register uint64_t __flags __asm__("a0") = flags;
    register void* __stack __asm__("a1") = child_stack;
    register void* __ptid __asm__("a2") = parent_tidptr;
    register void* __tls __asm__("a3") = newtls;
    register void* __ctid __asm__("a4") = child_tidptr;

    __asm__ __volatile__(/* Set up alignment of the child stack:
                          * child_stack = (child_stack & 0~0xf) - 15
                          */
                         "addi %2, %2, -16\n"

                         /* Push "arg" and "fn" onto the stack that will be used
                          * by the child.
                          */
                         "sd   %1, 0(%2)\n"
                         "sd   %4, 8(%2)\n"

                         /* %a0 = syscall(%a0 = flags,
                          *               %a1 = child_stack,
                          *               %a2 = parent_tidptr,
                          *               %a3 = newtls,
                          *               %a4 = child_tidptr)
                          */
                         "li   a7, %8\n"
                         "scall\n"

                         /* if(%a0 != 0)
                          *   return %a0;
                          */
                         "bnez %0, 1f\n"

                         /* In the child, now. Call "fn(arg)".
                          */
                         "ld   a1, 0(sp)\n"
                         "ld   a0, 8(sp)\n"
                         "jalr a1\n"

                         /* Call _exit(%a0, which never returns.  */
                         "li   a7, %9\n"
                         "scall\n"

                         /* Return to the parent.  */
                         "1:\n"
                         : "=r"(__res_a0)
                         : "r"(fn), "r"(__stack), "r"(__flags), "r"(arg),
                           "r"(__ptid), "r"(__tls), "r"(__ctid),
                           "i"(__NR_clone), "i"(__NR_exit)
                         : "cc", "memory");
    __res = __res_a0;
  }
  LSS_SET_ERRNO_AND_RETURN(int, __res, out_errno);
}

/*
 * riscv does not have fork, we need to use clone to emulate it.  Unfortunately,
 * this is very platform specific and cannot be done in a generic compatibility
 * wrapper.
 */
static inline int lss_fork(int* out_errno) {
  LSS_PRE(fork);
  LSS_REG(0, SIGCHLD); /* clone flags */
  LSS_REG(1, 0);       /* newsp */
  LSS_REG(2, NULL);    /* parent_tidptr */
  LSS_REG(3, 0);       /* tls_val */
  LSS_REG(4, NULL);    /* child_tidptr */
  LSS_BODY(int, clone, "r"(__r0), "r"(__r1), "r"(__r2), "r"(__r3), "r"(__r4));
  LSS_POST(fork, __res);
  LSS_SET_ERRNO_AND_RETURN(int, __res, out_errno);
}
SYS_WRAP_LSS0(int, fork);
