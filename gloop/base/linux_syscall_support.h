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

/* This file provides Linux-specific direct system call access and a couple
 * symbols missing from standard headers. Functions prefixed sys_ set errno in
 * the event of errors while those prefixed lss_ take an out argument that is
 * assigned in the event of an error. As the same functionality for the sys_
 * functionality is provided by syscall, then it is expected use of this code
 * will diminish with time.
 */
#ifndef THIRD_PARTY_GLOOP_BASE_LINUX_SYSCALL_SUPPORT_H_
#define THIRD_PARTY_GLOOP_BASE_LINUX_SYSCALL_SUPPORT_H_

/* We currently only support x86-32, x86-64, ARM, and PPC on Linux.
 * Porting to other related platforms should not be difficult.
 */
#if (defined(__i386__) || defined(__x86_64__) || defined(__arm__) ||  \
     defined(__PPC__) || defined(__aarch64__) || defined(__riscv)) && \
    defined(__linux)

#include <endian.h>
#include <errno.h>
#include <linux/futex.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/time.h>  // IWYU pragma: keep
#include <sys/types.h>
#include <syscall.h>
#include <unistd.h>

#if __BOUNDED_POINTERS__
#error "Need to port invocations of syscalls for bounded ptrs"
#endif

/* Sanitizer hooks. When built with (Address|Thread|Memory)Sanitizer, calls to
 *   __sanitizer_syscall_pre_<name>(args), and
 *   __sanitizer_syscall_post_<name>(retval, args)
 * are inserted at the beginning and end of each syscall stub. These functions
 * are implemented in the sanitizer runtime library, and are used to avoid false
 * positives and/or detect additional bugs, depending on the tool.
 * Since these hooks may be called in any context where this raw syscall
 * interface is used, they must be async signal safe, which the implementation
 * in sanitizer runtime is.
 * Hook arguments match system call arguments one-to-one. For example, in cases
 * where an off64_t argument is split in low and high halves on 32-bit
 * platforms, sanitizer hook accepts it as a pair of arguments as well.
 * Return value is passed to post-syscall hook as a single "int" argument.
 * Platforms where error code is returned in a separate register must convert
 * (error code, return value) to a single X86-style word.
 * Sanitizer hooks do not modify arguments and return values of the system call.
 *
 * If used with SYS_CPLUSPLUS, additional sanitizer header has to be included by
 * the user of this file: sanitizer/linux_syscall_hooks.h.
 * Define SYS_FORCE_SANITIZER_HOOKS_INTERNAL to insert pre- and post- sanitizer
 * syscall hooks when building without any sanitizer. This should be used for
 * testing purposes only.
 */
#ifdef LINUX_SYSCALL_ENABLE_HOOKS
#include "gloop/base/auxiliary/linux_syscall_support_sanitizer.inc"
#else
#define LSS_PRE(name, ...)
#define LSS_POST(name, res, ...)
#endif

#if defined(__cplusplus)
extern "C" {
#endif

/* As glibc often provides subtly incompatible data structures (and implicit
 * wrapper functions that convert them), we provide our own kernel data
 * structures for use by the system calls.  These structures have been developed
 * by using Linux 2.6.23 headers for reference. Note though, we do not care
 * about exact API compatibility with the kernel, and in fact the kernel often
 * does not have a single API that works across architectures. Instead, we try
 * to mimic the glibc API where reasonable, and only guarantee ABI compatibility
 * with the kernel headers.  Most notably, here are a few changes that were made
 * to the structures defined by kernel headers:
 *
 * - we only define structures, but not symbolic names for kernel data
 *   types. For the latter, we directly use the native C datatype
 *   (i.e. "unsigned" instead of "mode_t").
 * - in a few cases, it is possible to define identical structures for
 *   both 32bit (e.g. i386) and 64bit (e.g. x86-64) platforms by
 *   standardizing on the 64bit version of the data types. In particular,
 *   this means that we use "unsigned" where the 32bit headers say
 *   "unsigned long".
 * - overall, we try to minimize the number of cases where we need to
 *   conditionally define different structures.
 * - the "struct kernel_sigaction" class of structures have been
 *   modified to more closely mimic glibc's API by introducing an
 *   anonymous union for the function pointer.
 * - a small number of field names had to have an underscore appended to
 *   them, because glibc defines a global macro by the same name.
 */

/* include/linux/dirent.h                                                    */
struct kernel_dirent64 {
  unsigned long long d_ino;
  long long d_off;
  unsigned short d_reclen;
  unsigned char d_type;
  char d_name[256];
};

/* include/linux/uio.h                                                       */
struct kernel_iovec {
  void* iov_base;
  unsigned long iov_len;
};

/* include/linux/socket.h                                                    */
struct kernel_msghdr {
  void* msg_name;
  int msg_namelen;
  struct kernel_iovec* msg_iov;
  unsigned long msg_iovlen;
  void* msg_control;
  unsigned long msg_controllen;
  unsigned msg_flags;
};

/* include/asm-generic/poll.h                                                */
struct kernel_pollfd {
  int fd;
  short events;
  short revents;
};

/* include/linux/resource.h                                                  */
struct kernel_rlimit {
  unsigned long rlim_cur;
  unsigned long rlim_max;
};

/* include/linux/time.h                                                      */
struct kernel_timespec {
  long tv_sec;
  long tv_nsec;
};

/* include/linux/time.h                                                      */
struct kernel_timeval {
  long tv_sec;
  long tv_usec;
};

/* include/linux/time.h                                                      */
struct kernel_itimerval {
  struct kernel_timeval it_interval;
  struct kernel_timeval it_value;
};

/* include/linux/resource.h                                                  */
struct kernel_rusage {
  struct kernel_timeval ru_utime;
  struct kernel_timeval ru_stime;
  long ru_maxrss;
  long ru_ixrss;
  long ru_idrss;
  long ru_isrss;
  long ru_minflt;
  long ru_majflt;
  long ru_nswap;
  long ru_inblock;
  long ru_oublock;
  long ru_msgsnd;
  long ru_msgrcv;
  long ru_nsignals;
  long ru_nvcsw;
  long ru_nivcsw;
};

/* include/linux/capablilty.h                                                */
struct kernel_cap_user_header {
  unsigned int version;
  int pid;
};

struct kernel_cap_user_data {
  unsigned int effective;
  unsigned int permitted;
  unsigned int inheritable;
};

#if defined(__i386__) || defined(__arm__) || defined(__PPC__)
/* include/asm-{arm,i386,ppc}/signal.h                                  */
struct kernel_old_sigaction {
  union {
    void (*sa_handler_)(int);
    void (*sa_sigaction_)(int, siginfo_t*, void*);
  };
  unsigned long sa_mask;
  unsigned long sa_flags;
  void (*sa_restorer)(void);
} __attribute__((packed, aligned(4)));
#endif  // defined(__i386__) || defined(__arm__) || defined(__PPC__)

/* Some kernel functions (e.g. sigaction() in 2.6.23) require that the
 * exactly match the size of the signal set, even though the API was
 * intended to be extensible. We define our own KERNEL_NSIG to deal with
 * this.
 * Please note that glibc provides signals [1.._NSIG-1], whereas the
 * kernel (and this header) provides the range [1..KERNEL_NSIG]. The
 * actual number of signals is obviously the same, but the constants
 * differ by one.
 */
#define KERNEL_NSIG 64

/* include/asm-{arm,i386,x86_64}/signal.h                               */
struct kernel_sigset_t {
  unsigned long sig[(KERNEL_NSIG + 8 * sizeof(unsigned long) - 1) /
                    (8 * sizeof(unsigned long))];
};

/* include/asm-{arm,i386,x86_64,ppc}/signal.h                           */
struct kernel_sigaction {
  union {
    void (*sa_handler_)(int);
    void (*sa_sigaction_)(int, siginfo_t*, void*);
  };
  unsigned long sa_flags;
  void (*sa_restorer)(void);
  struct kernel_sigset_t sa_mask;
};

/* include/linux/socket.h                                                    */
struct kernel_sockaddr {
  unsigned short sa_family;
  char sa_data[14];
};

/* include/asm-{arm,i386,ppc}/stat.h                                    */
#if !defined(_LP64)
struct kernel_stat64 {
  unsigned long long st_dev;
  unsigned char __pad0[4];
  unsigned __st_ino;
  unsigned st_mode;
  unsigned st_nlink;
  unsigned st_uid;
  unsigned st_gid;
  unsigned long long st_rdev;
  unsigned char __pad3[4];
  long long st_size;
  unsigned st_blksize;
  unsigned long long st_blocks;
  unsigned st_atime_;
  unsigned st_atime_nsec_;
  unsigned st_mtime_;
  unsigned st_mtime_nsec_;
  unsigned st_ctime_;
  unsigned st_ctime_nsec_;
  unsigned long long st_ino;
};
#endif  // !defined(_LP64)

/* include/asm-{arm,i386,x86_64,ppc}/stat.h                             */
#if defined(__i386__) || defined(__arm__)
struct kernel_stat {
  /* The kernel headers suggest that st_dev and st_rdev should be 32bit
   * quantities encoding 12bit major and 20bit minor numbers in an interleaved
   * format. In reality, we do not see useful data in the top bits. So,
   * we'll leave the padding in here, until we find a better solution.
   */
  unsigned short st_dev;
  short pad1;
  unsigned st_ino;
  unsigned short st_mode;
  unsigned short st_nlink;
  unsigned short st_uid;
  unsigned short st_gid;
  unsigned short st_rdev;
  short pad2;
  unsigned st_size;
  unsigned st_blksize;
  unsigned st_blocks;
  unsigned st_atime_;
  unsigned st_atime_nsec_;
  unsigned st_mtime_;
  unsigned st_mtime_nsec_;
  unsigned st_ctime_;
  unsigned st_ctime_nsec_;
  unsigned __unused4;
  unsigned __unused5;
};
#endif  // defined(__i386__) || defined(__arm__)

// include/uapi/asm-generic/stat.h
#if defined(__riscv)
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
  unsigned long st_atime_nsec;
  long st_mtime_;
  unsigned long st_mtime_nsec;
  long st_ctime_;
  unsigned long st_ctime_nsec;
  unsigned int __unused4;
  unsigned int __unused5;
};
#endif  // defined(__riscv)

/* include/asm-{arm,i386,x86_64,ppc}/statfs.h                           */
#if !defined(_LP64)
struct kernel_statfs64 {
  unsigned long f_type;
  unsigned long f_bsize;
  unsigned long long f_blocks;
  unsigned long long f_bfree;
  unsigned long long f_bavail;
  unsigned long long f_files;
  unsigned long long f_ffree;
  struct {
    int val[2];
  } f_fsid;
  unsigned long f_namelen;
  unsigned long f_frsize;
  unsigned long f_spare[5];
};
#endif  // !defined(_LP64)

/* include/asm-{arm,i386,x86_64,ppc,generic}/statfs.h                   */
struct kernel_statfs {
  /* x86_64 actually defines all these fields as signed, whereas all other  */
  /* platforms define them as unsigned. Leaving them at unsigned should not */
  /* cause any problems.                                                    */
  unsigned long f_type;
  unsigned long f_bsize;
  unsigned long f_blocks;
  unsigned long f_bfree;
  unsigned long f_bavail;
  unsigned long f_files;
  unsigned long f_ffree;
  struct {
    int val[2];
  } f_fsid;
  unsigned long f_namelen;
  unsigned long f_frsize;
  unsigned long f_spare[5];
};

/* include/linux/aio_abi.h                                                   */
/* Layout depends on big/little endian.                                      */
struct kernel_iocb {
  unsigned long long aio_data;
#if __BYTE_ORDER == __LITTLE_ENDIAN
  unsigned int aio_key;
  unsigned int aio_reserved;
#else
  unsigned int aio_reserved;
  unsigned int aio_key;
#endif
  unsigned short aio_lio_opcode;
  short aio_reqprio;
  unsigned int aio_fildes;
  unsigned long long aio_buf;
  unsigned long long aio_nbytes;
  long long aio_offset;
  unsigned long long aio_reserved2;
  unsigned int aio_flags;
  unsigned int aio_resfd;
};

/* include/linux/aio_abi.h                                                   */
struct kernel_io_event {
  unsigned long long data;
  unsigned long long obj;
  long long res;
  long long res2;
};

/* Definitions missing from the standard header files                        */
#ifndef NT_PRXFPREG
#define NT_PRXFPREG 0x46e62b7f
#endif

#ifndef PTRACE_GETFPXREGS
#define PTRACE_GETFPXREGS ((enum __ptrace_request)18)
#endif

#ifndef PR_GET_DUMPABLE
#define PR_GET_DUMPABLE 3
#endif

#ifndef PR_SET_DUMPABLE
#define PR_SET_DUMPABLE 4
#endif

#ifndef PR_GET_SECCOMP
#define PR_GET_SECCOMP 21
#endif

#ifndef PR_SET_SECCOMP
#define PR_SET_SECCOMP 22
#endif

#ifndef AT_FDCWD
#define AT_FDCWD -100
#endif

#ifndef AT_SYMLINK_NOFOLLOW
#define AT_SYMLINK_NOFOLLOW 0x100
#endif

#ifndef AT_REMOVEDIR
#define AT_REMOVEDIR 0x200
#endif

#ifndef AT_EMPTY_PATH
#define AT_EMPTY_PATH 0x1000
#endif

#ifndef MREMAP_FIXED
#define MREMAP_FIXED 2
#endif

#ifndef SA_RESTORER
#define SA_RESTORER 0x04000000
#endif

#ifndef CPUCLOCK_PROF
#define CPUCLOCK_PROF 0
#endif

#ifndef CPUCLOCK_VIRT
#define CPUCLOCK_VIRT 1
#endif

#ifndef CPUCLOCK_SCHED
#define CPUCLOCK_SCHED 2
#endif

#ifndef CPUCLOCK_PERTHREAD_MASK
#define CPUCLOCK_PERTHREAD_MASK 4
#endif

#ifndef MAKE_PROCESS_CPUCLOCK
#define MAKE_PROCESS_CPUCLOCK(pid, clock) \
  ((~(unsigned)(pid) << 3) | (int)(clock))
#endif

#ifndef MAKE_THREAD_CPUCLOCK
#define MAKE_THREAD_CPUCLOCK(tid, clock) \
  ((~(unsigned)(tid) << 3) | (int)((clock) | CPUCLOCK_PERTHREAD_MASK))
#endif

#ifndef FUTEX_WAIT
#define FUTEX_WAIT 0
#endif

#ifndef FUTEX_WAKE
#define FUTEX_WAKE 1
#endif

#ifndef FUTEX_FD
#define FUTEX_FD 2
#endif

#ifndef FUTEX_REQUEUE
#define FUTEX_REQUEUE 3
#endif

#ifndef FUTEX_CMP_REQUEUE
#define FUTEX_CMP_REQUEUE 4
#endif

#ifndef FUTEX_WAKE_OP
#define FUTEX_WAKE_OP 5
#endif

#ifndef FUTEX_LOCK_PI
#define FUTEX_LOCK_PI 6
#endif

#ifndef FUTEX_UNLOCK_PI
#define FUTEX_UNLOCK_PI 7
#endif

#ifndef FUTEX_TRYLOCK_PI
#define FUTEX_TRYLOCK_PI 8
#endif

#ifndef FUTEX_PRIVATE_FLAG
#define FUTEX_PRIVATE_FLAG 128
#endif

#ifndef FUTEX_CMD_MASK
#define FUTEX_CMD_MASK ~FUTEX_PRIVATE_FLAG
#endif

#ifndef FUTEX_WAIT_PRIVATE
#define FUTEX_WAIT_PRIVATE (FUTEX_WAIT | FUTEX_PRIVATE_FLAG)
#endif

#ifndef FUTEX_WAKE_PRIVATE
#define FUTEX_WAKE_PRIVATE (FUTEX_WAKE | FUTEX_PRIVATE_FLAG)
#endif

#ifndef FUTEX_REQUEUE_PRIVATE
#define FUTEX_REQUEUE_PRIVATE (FUTEX_REQUEUE | FUTEX_PRIVATE_FLAG)
#endif

#ifndef FUTEX_CMP_REQUEUE_PRIVATE
#define FUTEX_CMP_REQUEUE_PRIVATE (FUTEX_CMP_REQUEUE | FUTEX_PRIVATE_FLAG)
#endif

#ifndef FUTEX_WAKE_OP_PRIVATE
#define FUTEX_WAKE_OP_PRIVATE (FUTEX_WAKE_OP | FUTEX_PRIVATE_FLAG)
#endif

#ifndef FUTEX_LOCK_PI_PRIVATE
#define FUTEX_LOCK_PI_PRIVATE (FUTEX_LOCK_PI | FUTEX_PRIVATE_FLAG)
#endif

#ifndef FUTEX_UNLOCK_PI_PRIVATE
#define FUTEX_UNLOCK_PI_PRIVATE (FUTEX_UNLOCK_PI | FUTEX_PRIVATE_FLAG)
#endif

#ifndef FUTEX_TRYLOCK_PI_PRIVATE
#define FUTEX_TRYLOCK_PI_PRIVATE (FUTEX_TRYLOCK_PI | FUTEX_PRIVATE_FLAG)
#endif

#ifndef __NR_close_range
#define __NR_close_range 436
#endif

#ifndef __NR_epoll_pwait2
#define __NR_epoll_pwait2 441
#endif

/* Wrap the given lss syscall with a sys version that assigns to errno. */
#define SYS_WRAP_LSS0(type, name)       \
  static inline type sys_##name() {     \
    int local_errno;                    \
    type rc = lss_##name(&local_errno); \
    if (rc == (type) - 1) {             \
      errno = local_errno;              \
    }                                   \
    return rc;                          \
  }

#define SYS_WRAP_LSS1(type, name, type1, arg1) \
  static inline type sys_##name(type1 arg1) {  \
    int local_errno;                           \
    type rc = lss_##name(arg1, &local_errno);  \
    if (rc == (type) - 1) {                    \
      errno = local_errno;                     \
    }                                          \
    return rc;                                 \
  }

#define SYS_WRAP_LSS2(type, name, type1, arg1, type2, arg2) \
  static inline type sys_##name(type1 arg1, type2 arg2) {   \
    int local_errno;                                        \
    type rc = lss_##name(arg1, arg2, &local_errno);         \
    if (rc == (type) - 1) {                                 \
      errno = local_errno;                                  \
    }                                                       \
    return rc;                                              \
  }

#define SYS_WRAP_LSS3(type, name, type1, arg1, type2, arg2, type3, arg3) \
  static inline type sys_##name(type1 arg1, type2 arg2, type3 arg3) {    \
    int local_errno;                                                     \
    type rc = lss_##name(arg1, arg2, arg3, &local_errno);                \
    if (rc == (type) - 1) {                                              \
      errno = local_errno;                                               \
    }                                                                    \
    return rc;                                                           \
  }

#define SYS_WRAP_LSS4(type, name, type1, arg1, type2, arg2, type3, arg3, \
                      type4, arg4)                                       \
  static inline type sys_##name(type1 arg1, type2 arg2, type3 arg3,      \
                                type4 arg4) {                            \
    int local_errno;                                                     \
    type rc = lss_##name(arg1, arg2, arg3, arg4, &local_errno);          \
    if (rc == (type) - 1) {                                              \
      errno = local_errno;                                               \
    }                                                                    \
    return rc;                                                           \
  }

#define SYS_WRAP_LSS5(type, name, type1, arg1, type2, arg2, type3, arg3, \
                      type4, arg4, type5, arg5)                          \
  static inline type sys_##name(type1 arg1, type2 arg2, type3 arg3,      \
                                type4 arg4, type5 arg5) {                \
    int local_errno;                                                     \
    type rc = lss_##name(arg1, arg2, arg3, arg4, arg5, &local_errno);    \
    if (rc == (type) - 1) {                                              \
      errno = local_errno;                                               \
    }                                                                    \
    return rc;                                                           \
  }

#define SYS_WRAP_LSS6(type, name, type1, arg1, type2, arg2, type3, arg3,    \
                      type4, arg4, type5, arg5, type6, arg6)                \
  static inline type sys_##name(type1 arg1, type2 arg2, type3 arg3,         \
                                type4 arg4, type5 arg5, type6 arg6) {       \
    int local_errno;                                                        \
    type rc = lss_##name(arg1, arg2, arg3, arg4, arg5, arg6, &local_errno); \
    if (rc == (type) - 1) {                                                 \
      errno = local_errno;                                                  \
    }                                                                       \
    return rc;                                                              \
  }

#if defined(__arm__) && !defined(__NR_fstatat) && defined(__linux__) && \
    !defined(__GLIBC__)
#if defined(__NR_newfstatat)
#define __NR_fstatat __NR_newfstatat
#elif defined(__NR_fstatat64)
#define __NR_fstatat __NR_fstatat64
#endif
#endif

#if defined(__aarch64__)
#include "gloop/base/auxiliary/syscall_linux_aarch64.inc"
#elif defined(__x86_64__)
#include "gloop/base/auxiliary/syscall_linux_x86_64.inc"
#elif defined(__i386__)
#include "gloop/base/auxiliary/syscall_linux_x86.inc"
#elif defined(__arm__)
#include "gloop/base/auxiliary/syscall_linux_arm.inc"
#elif defined(__riscv)
#include "gloop/base/auxiliary/syscall_linux_riscv.inc"
#endif

#define SYS_syscall0(type, name) \
  LSS_syscall0(type, name);      \
  SYS_WRAP_LSS0(type, name);

#define SYS_syscall1(type, name, type1, arg1) \
  LSS_syscall1(type, name, type1, arg1);      \
  SYS_WRAP_LSS1(type, name, type1, arg1);

#define SYS_syscall2(type, name, type1, arg1, type2, arg2) \
  LSS_syscall2(type, name, type1, arg1, type2, arg2);      \
  SYS_WRAP_LSS2(type, name, type1, arg1, type2, arg2);

#define SYS_syscall3(type, name, type1, arg1, type2, arg2, type3, arg3) \
  LSS_syscall3(type, name, type1, arg1, type2, arg2, type3, arg3);      \
  SYS_WRAP_LSS3(type, name, type1, arg1, type2, arg2, type3, arg3);

#define SYS_syscall4(type, name, type1, arg1, type2, arg2, type3, arg3, type4, \
                     arg4)                                                     \
  LSS_syscall4(type, name, type1, arg1, type2, arg2, type3, arg3, type4,       \
               arg4);                                                          \
  SYS_WRAP_LSS4(type, name, type1, arg1, type2, arg2, type3, arg3, type4, arg4);

#define SYS_syscall5(type, name, type1, arg1, type2, arg2, type3, arg3, type4, \
                     arg4, type5, arg5)                                        \
  LSS_syscall5(type, name, type1, arg1, type2, arg2, type3, arg3, type4, arg4, \
               type5, arg5);                                                   \
  SYS_WRAP_LSS5(type, name, type1, arg1, type2, arg2, type3, arg3, type4,      \
                arg4, type5, arg5);

#define SYS_syscall6(type, name, type1, arg1, type2, arg2, type3, arg3, type4, \
                     arg4, type5, arg5, type6, arg6)                           \
  LSS_syscall6(type, name, type1, arg1, type2, arg2, type3, arg3, type4, arg4, \
               type5, arg5, type6, arg6);                                      \
  SYS_WRAP_LSS6(type, name, type1, arg1, type2, arg2, type3, arg3, type4,      \
                arg4, type5, arg5, type6, arg6);

static inline int sys_clone(int (*fn)(void*), void* child_stack, int flags,
                            void* arg, int* parent_tidptr, void* newtls,
                            int* child_tidptr) {
  int local_errno;
  int rc = lss_clone(fn, child_stack, flags, arg, parent_tidptr, newtls,
                     child_tidptr, &local_errno);
  if (rc == -1) {
    errno = local_errno;
  }
  return rc;
}

#define __NR__exit __NR_exit
#define __NR__gettid __NR_gettid
#define __NR__mremap __NR_mremap

/* Note: brk is a little unusual in that it may fail with an errno or it may
 * fail during unmapping and return a brk value that is unchanged. Users should
 * check the result against the input value, rather than just -1, to indicate an
 * error. This is different from the libc version of brk/sbrk. */
SYS_syscall1(void*, brk, void*, e);

SYS_syscall2(int, capset, struct kernel_cap_user_header*, h,
             struct kernel_cap_user_data*, d);
SYS_syscall1(int, chdir, const char*, p);
SYS_syscall1(int, chroot, const char*, p);
SYS_syscall1(int, close, int, f);
SYS_syscall2(int, clock_getres, int, c, struct kernel_timespec*, t);
SYS_syscall2(int, clock_gettime, int, c, struct kernel_timespec*, t);
SYS_syscall1(int, dup, int, f);
#if defined(__NR_dup2)
/* dup2 isn't provided by certain architectures like Aarch64. */
SYS_syscall2(int, dup2, int, s, int, d);
#endif
SYS_syscall3(int, dup3, int, s, int, d, int, f);
SYS_syscall3(int, execve, const char*, f, const char* const*, a,
             const char* const*, e);
#if defined(__NR_execveat)
/* execveat isn't provided by old systems, e.g. 1st-gen Nestcam QV1. */
SYS_syscall5(int, execveat, int, dirfd, const char*, path,  // NOLINT
             const char* const*, argv, const char* const*, envp, int, flags);
#endif
SYS_syscall1(int, _exit, int, e);
SYS_syscall1(int, exit_group, int, e);
#if defined(__NR_fadvise64) && defined(_LP64)
/* Use this only for 64-bit ABIs.  32-bit targets use fadvise64_64 */
SYS_syscall4(int, fadvise64, int, fd, loff_t, offset, loff_t, len, int, advice);
#endif
SYS_syscall3(int, fcntl, int, f, int, c, long, a);
SYS_syscall1(int, fdatasync, int, f);
#if defined(__NR_fork)
SYS_syscall0(pid_t, fork);
#endif
#if defined(__NR_fstat)
SYS_syscall2(int, fstat, int, f, struct kernel_stat*, b);
#endif
SYS_syscall2(int, fstatfs, int, f, struct kernel_statfs*, b);
SYS_syscall2(int, ftruncate, int, f, off_t, l);
SYS_syscall6(int, futex, int*, a, int, o, int, v, struct kernel_timespec*, t,
             uint32_t*, u2, uint32_t, v3);
SYS_syscall3(int, getdents64, int, f, struct kernel_dirent64*, d, int, c);
SYS_syscall0(gid_t, getegid);
SYS_syscall0(uid_t, geteuid);
SYS_syscall2(int, getitimer, int, w, struct kernel_itimerval*, c);
#if defined(__NR_getpgid)
SYS_syscall1(pid_t, getpgid, pid_t, p);
#endif
#if defined(__NR_getpgrp)
SYS_syscall0(pid_t, getpgrp);
#endif
SYS_syscall0(pid_t, getpid);
SYS_syscall0(pid_t, getppid);
SYS_syscall2(int, getpriority, int, a, int, b);
SYS_syscall3(int, getresgid, gid_t*, r, gid_t*, e, gid_t*, s);
SYS_syscall3(int, getresuid, uid_t*, r, uid_t*, e, uid_t*, s);
#if defined(__NR_getrlimit)
SYS_syscall2(int, getrlimit, int, r, struct kernel_rlimit*, l);
#endif
SYS_syscall1(pid_t, getsid, pid_t, p);
SYS_syscall0(pid_t, _gettid);
SYS_syscall2(int, gettimeofday, struct timeval*, v, struct timezone*, z);
SYS_syscall5(int, setxattr, const char*, p, const char*, n, const void*, v,
             size_t, s, int, f);
SYS_syscall5(int, lsetxattr, const char*, p, const char*, n, const void*, v,
             size_t, s, int, f);
SYS_syscall4(ssize_t, getxattr, const char*, p, const char*, n, void*, v,
             size_t, s);
SYS_syscall4(ssize_t, lgetxattr, const char*, p, const char*, n, void*, v,
             size_t, s);
SYS_syscall3(ssize_t, listxattr, const char*, p, char*, l, size_t, s);
SYS_syscall3(ssize_t, llistxattr, const char*, p, char*, l, size_t, s);
SYS_syscall3(int, ioctl, int, d, int, r, void*, a);
SYS_syscall2(int, ioprio_get, int, which, int, who);
SYS_syscall3(int, ioprio_set, int, which, int, who, int, ioprio);
SYS_syscall2(int, kill, pid_t, p, int, s);
SYS_syscall3(off_t, lseek, int, f, off_t, o, int, w);
SYS_syscall2(int, munmap, void*, s, size_t, l);
SYS_syscall6(long, move_pages, pid_t, p, unsigned long, n, void**, g, int*, d,
             int*, s, int, f);
SYS_syscall3(int, mprotect, const void*, a, size_t, l, int, p);
SYS_syscall5(void*, _mremap, void*, o, size_t, os, size_t, ns, unsigned long, f,
             void*, a);
#if defined(__NR_nanosleep)
SYS_syscall2(int, nanosleep, const struct kernel_timespec*, req,
             struct kernel_timespec*, rem);
#endif
#if defined(__NR_openat)
SYS_syscall4(int, openat, int, d, const char*, p, int, f, int, m);
#endif
#if defined(__NR_open)
SYS_syscall3(int, open, const char*, p, int, f, int, m);
#endif
#if defined(__NR_poll)
SYS_syscall3(int, poll, struct kernel_pollfd*, u, unsigned int, n, int, t);
#endif
#if defined(__NR_ppoll)
SYS_syscall5(int, ppoll, struct kernel_pollfd*, u, unsigned int, n,
             const struct kernel_timespec*, t, const struct kernel_sigset_t*,
             sigmask, size_t, sigsetsize);
#endif
SYS_syscall5(int, prctl, int, o, unsigned long, a1, unsigned long, a2,
             unsigned long, a3, unsigned long, a4);
#if defined(__NR_arch_prctl)
SYS_syscall2(int, arch_prctl, int, c, void*, a);
#endif
SYS_syscall5(int, mount, const char*, source, const char*, target, const char*,
             filesystemtype, unsigned long, mountflags, const void*, data);
SYS_syscall1(int, unshare, int, flags);
SYS_syscall2(int, setns, int, fd, int, nstype);
#if defined(__NR_preadv)
// Defined on x86_64 / i386 only
SYS_syscall5(ssize_t, preadv, unsigned long, fd, const struct kernel_iovec*,
             iovec, unsigned long, vlen, unsigned long, pos_l, unsigned long,
             pos_h);
#endif
SYS_syscall4(long, ptrace, int, r, pid_t, p, void*, a, void*, d);
#if defined(__NR_pwritev)
// Defined on x86_64 / i386 only
SYS_syscall5(ssize_t, pwritev, unsigned long, fd, const struct kernel_iovec*,
             iovec, unsigned long, vlen, unsigned long, pos_l, unsigned long,
             pos_h);
#endif
#if defined(__NR_quotactl)
// Defined on x86_64 / i386 only
SYS_syscall4(int, quotactl, int, cmd, const char*, special, int, id, caddr_t,
             addr);
#endif
SYS_syscall3(ssize_t, read, int, f, void*, b, size_t, c);
#if defined(__NR_readlink)
SYS_syscall3(int, readlink, const char*, p, char*, b, size_t, s);
#endif
#if defined(__NR_readlinkat)
SYS_syscall4(int, readlinkat, int, f, const char*, p, char*, b, size_t, s);
#endif
SYS_syscall4(int, rt_sigaction, int, s, const struct kernel_sigaction*, a,
             struct kernel_sigaction*, o, size_t, c);
SYS_syscall2(int, rt_sigpending, struct kernel_sigset_t*, s, size_t, c);
SYS_syscall4(int, rt_sigprocmask, int, h, const struct kernel_sigset_t*, s,
             struct kernel_sigset_t*, o, size_t, c);
SYS_syscall1(int, rt_sigreturn, unsigned long, u);
SYS_syscall2(int, rt_sigsuspend, const struct kernel_sigset_t*, s, size_t, c);
SYS_syscall3(int, sched_getaffinity, pid_t, p, unsigned int, l, unsigned long*,
             m);
SYS_syscall3(int, sched_setaffinity, pid_t, p, unsigned int, l, unsigned long*,
             m);
SYS_syscall0(int, sched_yield);
SYS_syscall1(long, set_tid_address, int*, t);
SYS_syscall3(int, setitimer, int, w, const struct kernel_itimerval*, n,
             struct kernel_itimerval*, o);
SYS_syscall1(int, setfsgid, gid_t, g);
SYS_syscall1(int, setfsuid, uid_t, u);
SYS_syscall1(int, setuid, uid_t, u);
SYS_syscall1(int, setgid, gid_t, g);
SYS_syscall2(int, setpgid, pid_t, p, pid_t, g);
SYS_syscall3(int, setpriority, int, a, int, b, int, p);
SYS_syscall3(int, setresgid, gid_t, r, gid_t, e, gid_t, s);
SYS_syscall3(int, setresuid, uid_t, r, uid_t, e, uid_t, s);
SYS_syscall2(int, setrlimit, int, r, const struct kernel_rlimit*, l);
SYS_syscall0(pid_t, setsid);
SYS_syscall2(int, sigaltstack, const stack_t*, s, const stack_t*, o);
#if defined(__NR_sigreturn)
SYS_syscall1(int, sigreturn, unsigned long, u);
#endif
#if defined(__NR_stat)
SYS_syscall2(int, stat, const char*, f, struct kernel_stat*, b);
#endif
SYS_syscall2(int, statfs, const char*, f, struct kernel_statfs*, b);
SYS_syscall3(int, tgkill, pid_t, p, pid_t, t, int, s);
SYS_syscall2(int, tkill, pid_t, p, int, s);
SYS_syscall3(ssize_t, write, int, f, const void*, b, size_t, c);
SYS_syscall3(ssize_t, writev, int, f, const struct kernel_iovec*, v, size_t, c);
SYS_syscall1(int, umask, unsigned, m);
#if defined(__NR_unlink)
SYS_syscall1(int, unlink, const char*, f);
#endif
#if defined(__NR_unlinkat)
SYS_syscall3(int, unlinkat, int, d, const char*, p, int, f);
#endif
#if defined(__NR_getcpu)
SYS_syscall3(long, getcpu, unsigned*, cpu, unsigned*, node, void*, unused);
#endif
#if defined(__x86_64__) || defined(__aarch64__) || defined(__PPC__) || \
    defined(__riscv)
SYS_syscall3(int, recvmsg, int, s, struct kernel_msghdr*, m, int, f);
SYS_syscall3(int, sendmsg, int, s, const struct kernel_msghdr*, m, int, f);
SYS_syscall6(int, sendto, int, s, const void*, m, size_t, l, int, f,
             const struct kernel_sockaddr*, a, int, t);
SYS_syscall2(int, shutdown, int, s, int, h);
SYS_syscall3(int, socket, int, d, int, t, int, p);
SYS_syscall4(int, socketpair, int, d, int, t, int, p, int*, s);
#endif
SYS_syscall6(int, epoll_pwait2, int, epfd, struct epoll_event*, events, int,
             maxevents, const struct kernel_timespec*, timeout,
             const struct kernel_sigset_t*, sigmask, size_t, sigsetsize);

#if defined(__NR_newfstatat)
SYS_syscall4(int, newfstatat, int, d, const char*, p, struct kernel_stat*, b,
             int, f);
#endif  // defined(__NR_newfstatat)

#if defined(__x86_64__) || defined(__PPC__) || defined(__aarch64__) || \
    defined(__riscv)
static inline int sys_getresgid32(gid_t* rgid, gid_t* egid, gid_t* sgid) {
  return sys_getresgid(rgid, egid, sgid);
}

static inline int lss_getresgid32(gid_t* rgid, gid_t* egid, gid_t* sgid,
                                  int* out_errno) {
  return lss_getresgid(rgid, egid, sgid, out_errno);
}

static inline int sys_getresuid32(uid_t* ruid, uid_t* euid, uid_t* suid) {
  return sys_getresuid(ruid, euid, suid);
}

static inline int lss_getresuid32(uid_t* ruid, uid_t* euid, uid_t* suid,
                                  int* out_errno) {
  return lss_getresuid(ruid, euid, suid, out_errno);
}

static inline int sys_setfsgid32(gid_t gid) { return sys_setfsgid(gid); }

static inline int lss_setfsgid32(gid_t gid, int* out_errno) {
  return lss_setfsgid(gid, out_errno);
}

static inline int sys_setfsuid32(uid_t uid) { return sys_setfsuid(uid); }

static inline int lss_setfsuid32(uid_t uid, int* out_errno) {
  return lss_setfsuid(uid, out_errno);
}

static inline int sys_setresgid32(gid_t rgid, gid_t egid, gid_t sgid) {
  return sys_setresgid(rgid, egid, sgid);
}

static inline int lss_setresgid32(gid_t rgid, gid_t egid, gid_t sgid,
                                  int* out_errno) {
  return lss_setresgid(rgid, egid, sgid, out_errno);
}

static inline int sys_setresuid32(uid_t ruid, uid_t euid, uid_t suid) {
  return sys_setresuid(ruid, euid, suid);
}

static inline int lss_setresuid32(uid_t ruid, uid_t euid, uid_t suid,
                                  int* out_errno) {
  return lss_setresuid(ruid, euid, suid, out_errno);
}
#endif  // defined(__x86_64__) || defined(__PPC__) || defined(__aarch64__) ||
        // defined(__riscv)

#if defined(__x86_64__) || defined(__PPC64__) || defined(__aarch64__) || \
    defined(__riscv)
SYS_syscall4(int, fallocate, int, fd, int, mode, loff_t, offset, loff_t, len);
SYS_syscall6(void*, mmap, void*, s, size_t, l, int, p, int, f, int, d, off_t,
             o);

static inline int lss_sigaction(int signum, const struct kernel_sigaction* act,
                                struct kernel_sigaction* oldact,
                                int* out_errno) {
#if defined(__x86_64__)
  /* On x86_64, the kernel requires us to always set our own
   * SA_RESTORER in order to be able to return from a signal handler.
   * This function must have a "magic" signature that the "gdb"
   * (and maybe the kernel?) can recognize.
   */
  if (act != NULL && !(act->sa_flags & SA_RESTORER)) {
    struct kernel_sigaction a = *act;
    a.sa_flags |= SA_RESTORER;
    a.sa_restorer = sys_restore_rt();
    return lss_rt_sigaction(signum, &a, oldact, (KERNEL_NSIG + 7) / 8,
                            out_errno);
  } else {
    return lss_rt_sigaction(signum, act, oldact, (KERNEL_NSIG + 7) / 8,
                            out_errno);
  }
#else
  return lss_rt_sigaction(signum, act, oldact, (KERNEL_NSIG + 7) / 8,
                          out_errno);
#endif
}
SYS_WRAP_LSS3(int, sigaction, int, signum, const struct kernel_sigaction*, act,
              struct kernel_sigaction*, oldact);

static inline int sys_sigpending(struct kernel_sigset_t* set) {
  return sys_rt_sigpending(set, (KERNEL_NSIG + 7) / 8);
}

static inline int lss_sigpending(struct kernel_sigset_t* set, int* out_errno) {
  return lss_rt_sigpending(set, (KERNEL_NSIG + 7) / 8, out_errno);
}

static inline int sys_sigprocmask(int how, const struct kernel_sigset_t* set,
                                  struct kernel_sigset_t* oldset) {
  return sys_rt_sigprocmask(how, set, oldset, (KERNEL_NSIG + 7) / 8);
}

static inline int lss_sigprocmask(int how, const struct kernel_sigset_t* set,
                                  struct kernel_sigset_t* oldset,
                                  int* out_errno) {
  return lss_rt_sigprocmask(how, set, oldset, (KERNEL_NSIG + 7) / 8, out_errno);
}

static inline int sys_sigsuspend(const struct kernel_sigset_t* set) {
  return sys_rt_sigsuspend(set, (KERNEL_NSIG + 7) / 8);
}

static inline int lss_sigsuspend(const struct kernel_sigset_t* set,
                                 int* out_errno) {
  return lss_rt_sigsuspend(set, (KERNEL_NSIG + 7) / 8, out_errno);
}
#endif /* defined(__x86_64__) || defined(__PPC64__) || defined(__arch64__) || \
          defined(__riscv) */

#if defined(__x86_64__) || defined(__arm__) || defined(__aarch64__) || \
    defined(__riscv)
SYS_syscall4(pid_t, wait4, pid_t, p, int*, s, int, o, struct kernel_rusage*, r);

static inline pid_t lss_waitpid(pid_t pid, int* status, int options,
                                int* out_errno) {
  return lss_wait4(pid, status, options, 0, out_errno);
}
SYS_WRAP_LSS3(pid_t, waitpid, pid_t, pid, int*, status, int, options);

#endif  // defined(__x86_64__) || defined(__arm__) || defined(__aarch64__)

#if defined(__x86_64__) || defined(__ARM_ARCH_3__) ||                       \
    defined(__ARM_ARCH_5T__) || defined(__PPC__) || defined(__aarch64__) || \
    defined(__riscv)
SYS_syscall2(int, setgroups, size_t, c, const gid_t*, g);
#endif

#if defined(__i386__) || defined(__arm__)
#define __NR__getresgid32 __NR_getresgid32
#define __NR__getresuid32 __NR_getresuid32
#define __NR__setfsgid32 __NR_setfsgid32
#define __NR__setfsuid32 __NR_setfsuid32
#define __NR__setgroups32 __NR_setgroups32
#define __NR__setgroups __NR_setgroups
#define __NR__setresgid32 __NR_setresgid32
#define __NR__setresuid32 __NR_setresuid32

SYS_syscall2(int, ugetrlimit, int, r, struct kernel_rlimit*, l);
SYS_syscall3(int, _getresgid32, gid_t*, r, gid_t*, e, gid_t*, s);
SYS_syscall3(int, _getresuid32, uid_t*, r, uid_t*, e, uid_t*, s);
SYS_syscall1(int, _setfsgid32, gid_t, f);
SYS_syscall1(int, _setfsuid32, uid_t, f);
SYS_syscall2(int, _setgroups32, int, s, const unsigned int*, l);
SYS_syscall2(int, _setgroups, size_t, c, const unsigned short*, g);
SYS_syscall3(int, _setresgid32, gid_t, r, gid_t, e, gid_t, s);
SYS_syscall3(int, _setresuid32, uid_t, r, uid_t, e, uid_t, s);

static inline int lss_getresgid32(gid_t* rgid, gid_t* egid, gid_t* sgid,
                                  int* out_errno) {
  int rc;
  if ((rc = lss__getresgid32(rgid, egid, sgid, out_errno)) < 0 &&
      *out_errno == ENOSYS) {
    if ((rgid == NULL) || (egid == NULL) || (sgid == NULL)) {
      return EFAULT;
    }
    // Clear the high bits first, since getresgid only sets 16 bits
    *rgid = *egid = *sgid = 0;
    rc = lss_getresgid(rgid, egid, sgid, out_errno);
  }
  return rc;
}
SYS_WRAP_LSS3(int, getresgid32, gid_t*, rgid, gid_t*, egid, gid_t*, sgid);

static inline int lss_getresuid32(uid_t* ruid, uid_t* euid, uid_t* suid,
                                  int* out_errno) {
  int rc;
  if ((rc = lss__getresuid32(ruid, euid, suid, out_errno)) < 0 &&
      *out_errno == ENOSYS) {
    if ((ruid == NULL) || (euid == NULL) || (suid == NULL)) {
      *out_errno = EFAULT;
      return -1;
    }
    // Clear the high bits first, since getresuid only sets 16 bits
    *ruid = *euid = *suid = 0;
    rc = lss_getresuid(ruid, euid, suid, out_errno);
  }
  return rc;
}
SYS_WRAP_LSS3(int, getresuid32, uid_t*, ruid, uid_t*, euid, uid_t*, suid);

static inline int lss_setfsgid32(gid_t gid, int* out_errno) {
  int rc = lss__setfsgid32(gid, out_errno);
  if (rc < 0 && *out_errno == ENOSYS) {
    if ((unsigned int)gid & ~0xFFFFu) {
      *out_errno = EINVAL;
    } else {
      rc = lss_setfsgid(gid, out_errno);
    }
  }
  return rc;
}
SYS_WRAP_LSS1(int, setfsgid32, gid_t, gid);

static inline int lss_setfsuid32(uid_t uid, int* out_errno) {
  int rc;
  if ((rc = lss__setfsuid32(uid, out_errno)) < 0 && *out_errno == ENOSYS) {
    if ((unsigned int)uid & ~0xFFFFu) {
      *out_errno = EINVAL;
    } else {
      rc = lss_setfsuid(uid, out_errno);
    }
  }
  return rc;
}
SYS_WRAP_LSS1(int, setfsuid32, uid_t, uid);

// We cannot allocate memory so there is a problem with building the
// list of groups with the proper datatype.  Older kernels have limits
// on the number of groups that can be set at one time of up to 32.
// So we have an array on the stack of size 32 where to put the groups.
#define LSS_SET_GROUPS_SIZE 32
static inline int lss_setgroups(size_t size, const unsigned int* list,
                                int* out_errno) {
  int rc = 0;
  if ((rc = lss__setgroups32(size, list, out_errno)) < 0 &&
      *out_errno == ENOSYS) {
    if (size > LSS_SET_GROUPS_SIZE) {
      *out_errno = EINVAL;
    } else {
      unsigned short gid_list[LSS_SET_GROUPS_SIZE];
      size_t i;
      for (i = 0; i < size; ++i) {
        if (list[i] & ~0xFFFFu) {
          *out_errno = EINVAL;
          break;
        }
        gid_list[i] = list[i];
      }
      if (*out_errno != EINVAL) {
        rc = lss__setgroups(size, gid_list, out_errno);
      }
    }
  }
  return rc;
}
#undef LSS_SET_GROUPS_SIZE
SYS_WRAP_LSS2(int, setgroups, size_t, size, const unsigned int*, list);

static inline int lss_setresgid32(gid_t rgid, gid_t egid, gid_t sgid,
                                  int* out_errno) {
  int rc;
  if ((rc = lss__setresgid32(rgid, egid, sgid, out_errno)) < 0 &&
      *out_errno == ENOSYS) {
    if ((unsigned int)rgid & ~0xFFFFu || (unsigned int)egid & ~0xFFFFu ||
        (unsigned int)sgid & ~0xFFFFu) {
      *out_errno = EINVAL;
    } else {
      rc = lss_setresgid(rgid, egid, sgid, out_errno);
    }
  }
  return rc;
}
SYS_WRAP_LSS3(int, setresgid32, gid_t, rgid, gid_t, egid, gid_t, sgid);

static inline int lss_setresuid32(uid_t ruid, uid_t euid, uid_t suid,
                                  int* out_errno) {
  int rc;
  if ((rc = lss__setresuid32(ruid, euid, suid, out_errno)) < 0 &&
      *out_errno == ENOSYS) {
    if ((unsigned int)ruid & ~0xFFFFu || (unsigned int)euid & ~0xFFFFu ||
        (unsigned int)suid & ~0xFFFFu) {
      *out_errno = EINVAL;
    } else {
      rc = lss_setresuid(ruid, euid, suid, out_errno);
    }
  }
  return rc;
}
SYS_WRAP_LSS3(int, setresuid32, uid_t, ruid, uid_t, euid, uid_t, suid);
#endif  // defined(__i386__) || defined(__arm__)

static inline int sys_sigemptyset(struct kernel_sigset_t* set) {
  memset(&set->sig, 0, sizeof(set->sig));
  return 0;
}
static inline int lss_sigemptyset(struct kernel_sigset_t* set) {
  memset(&set->sig, 0, sizeof(set->sig));
  return 0;
}

static inline int sys_sigfillset(struct kernel_sigset_t* set) {
  memset(&set->sig, -1, sizeof(set->sig));
  return 0;
}
static inline int lss_sigfillset(struct kernel_sigset_t* set) {
  memset(&set->sig, -1, sizeof(set->sig));
  return 0;
}

static inline int lss_sigaddset(struct kernel_sigset_t* set, int signum,
                                int* out_errno) {
  if (signum < 1 || signum > (int)(8 * sizeof(set->sig))) {
    *out_errno = EINVAL;
    return -1;
  } else {
    set->sig[(signum - 1) / (8 * sizeof(set->sig[0]))] |=
        1UL << ((signum - 1) % (8 * sizeof(set->sig[0])));
    return 0;
  }
}
SYS_WRAP_LSS2(int, sigaddset, struct kernel_sigset_t*, set, int, signum);

static inline int lss_sigdelset(struct kernel_sigset_t* set, int signum,
                                int* out_errno) {
  if (signum < 1 || signum > (int)(8 * sizeof(set->sig))) {
    *out_errno = EINVAL;
    return -1;
  } else {
    set->sig[(signum - 1) / (8 * sizeof(set->sig[0]))] &=
        ~(1UL << ((signum - 1) % (8 * sizeof(set->sig[0]))));
    return 0;
  }
}
SYS_WRAP_LSS2(int, sigdelset, struct kernel_sigset_t*, set, int, signum);

static inline int lss_sigismember(struct kernel_sigset_t* set, int signum,
                                  int* out_errno) {
  if (signum < 1 || signum > (int)(8 * sizeof(set->sig))) {
    *out_errno = EINVAL;
    return -1;
  } else {
    return !!(set->sig[(signum - 1) / (8 * sizeof(set->sig[0]))] &
              (1UL << ((signum - 1) % (8 * sizeof(set->sig[0])))));
  }
}
SYS_WRAP_LSS2(int, sigismember, struct kernel_sigset_t*, set, int, signum);

#if defined(__i386__) || defined(__arm__) || \
    (defined(__PPC__) && !defined(__PPC64__))
#define __NR__sigaction __NR_sigaction
#define __NR__sigpending __NR_sigpending
#define __NR__sigprocmask __NR_sigprocmask
#define __NR__sigsuspend __NR_sigsuspend

SYS_syscall2(int, fstat64, int, f, struct kernel_stat64*, b);
SYS_syscall5(int, _llseek, uint, fd, unsigned long, hi, unsigned long, lo,
             loff_t*, res, uint, wh);

SYS_syscall6(void*, mmap2, void*, s, size_t, l, int, p, int, f, int, d, off_t,
             o);
SYS_syscall3(int, _sigaction, int, s, const struct kernel_old_sigaction*, a,
             struct kernel_old_sigaction*, o);
SYS_syscall1(int, _sigpending, unsigned long*, s);
SYS_syscall3(int, _sigprocmask, int, h, const unsigned long*, s, unsigned long*,
             o);
#ifdef __PPC__
SYS_syscall1(int, _sigsuspend, unsigned long, s);
#else
SYS_syscall3(int, _sigsuspend, const void*, a, int, b, unsigned long, s);
#endif
SYS_syscall2(int, stat64, const char*, p, struct kernel_stat64*, b);

static inline int lss_sigaction(int signum, const struct kernel_sigaction* act,
                                struct kernel_sigaction* oldact,
                                int* out_errno) {
  int rc;
  struct kernel_sigaction a;
  if (act != NULL) {
    a = *act;
#ifdef __i386__
    /* On i386, the kernel requires us to always set our own
     * SA_RESTORER when using realtime signals. Otherwise, it does not
     * know how to return from a signal handler. This function must have
     * a "magic" signature that the "gdb" (and maybe the kernel?) can
     * recognize.
     * Apparently, a SA_RESTORER is implicitly set by the kernel, when
     * using non-realtime signals.
     *
     * TODO: Test whether ARM needs a restorer
     */
    if (!(a.sa_flags & SA_RESTORER)) {
      a.sa_flags |= SA_RESTORER;
      a.sa_restorer =
          (a.sa_flags & SA_SIGINFO) ? sys_restore_rt() : sys_restore();
    }
#endif
  }
  rc = lss_rt_sigaction(signum, act ? &a : act, oldact, (KERNEL_NSIG + 7) / 8,
                        out_errno);
  if (rc < 0 && *out_errno == ENOSYS) {
    struct kernel_old_sigaction oa, ooa, *ptr_a = &oa, *ptr_oa = &ooa;
    if (!act) {
      ptr_a = NULL;
    } else {
      oa.sa_handler_ = act->sa_handler_;
      memcpy(&oa.sa_mask, &act->sa_mask, sizeof(oa.sa_mask));
      oa.sa_restorer = act->sa_restorer;
      oa.sa_flags = act->sa_flags;
    }
    if (!oldact) {
      ptr_oa = NULL;
    }
    rc = lss__sigaction(signum, ptr_a, ptr_oa, out_errno);
    if (rc == 0 && oldact) {
      if (act) {
        memcpy(oldact, act, sizeof(*act));
      } else {
        memset(oldact, 0, sizeof(*oldact));
      }
      oldact->sa_handler_ = ptr_oa->sa_handler_;
      oldact->sa_flags = ptr_oa->sa_flags;
      memcpy(&oldact->sa_mask, &ptr_oa->sa_mask, sizeof(ptr_oa->sa_mask));
      oldact->sa_restorer = ptr_oa->sa_restorer;
    }
  }
  return rc;
}
SYS_WRAP_LSS3(int, sigaction, int, signum, const struct kernel_sigaction*, act,
              struct kernel_sigaction*, oldact);

static inline int lss_sigpending(struct kernel_sigset_t* set, int* out_errno) {
  int rc = lss_rt_sigpending(set, (KERNEL_NSIG + 7) / 8, out_errno);
  if (rc < 0 && *out_errno == ENOSYS) {
    lss_sigemptyset(set);
    rc = lss__sigpending(&set->sig[0], out_errno);
  }
  return rc;
}
SYS_WRAP_LSS1(int, sigpending, struct kernel_sigset_t*, set);

static inline int lss_sigprocmask(int how, const struct kernel_sigset_t* set,
                                  struct kernel_sigset_t* oldset,
                                  int* out_errno) {
  int rc =
      lss_rt_sigprocmask(how, set, oldset, (KERNEL_NSIG + 7) / 8, out_errno);
  if (rc < 0 && *out_errno == ENOSYS) {
    if (oldset) {
      lss_sigemptyset(oldset);
    }
    rc = lss__sigprocmask(how, set ? &set->sig[0] : NULL,
                          oldset ? &oldset->sig[0] : NULL, out_errno);
  }
  return rc;
}
SYS_WRAP_LSS3(int, sigprocmask, int, how, const struct kernel_sigset_t*, set,
              struct kernel_sigset_t*, oldset);

static inline int lss_sigsuspend(const struct kernel_sigset_t* set,
                                 int* out_errno) {
  int rc = lss_rt_sigsuspend(set, (KERNEL_NSIG + 7) / 8, out_errno);
  if (rc < 0 && *out_errno == ENOSYS) {
    rc = lss__sigsuspend(
#ifndef __PPC__
        set, 0,
#endif
        set->sig[0], out_errno);
  }
  return rc;
}
SYS_WRAP_LSS1(int, sigsuspend, const struct kernel_sigset_t*, set);
#endif  // defined(__i386__) || defined(__arm__) || (defined(__PPC__) &&
        // !defined(__PPC64__))

#if defined(__i386__)
/* See sys_socketcall in net/socket.c in kernel source.
 * It de-multiplexes on its first arg and unpacks the arglist
 * array in its second arg.
 */
SYS_syscall2(long, socketcall, int, c, unsigned long*, a);

static inline ssize_t lss_recvmsg(int s, struct kernel_msghdr* msg, int flags,
                                  int* out_errno) {
  unsigned long args[3] = {(unsigned long)s, (unsigned long)msg,
                           (unsigned long)flags};
  return (ssize_t)lss_socketcall(17, args, out_errno);
}
SYS_WRAP_LSS3(ssize_t, recvmsg, int, s, struct kernel_msghdr*, msg, int, flags);

static inline ssize_t lss_sendmsg(int s, const struct kernel_msghdr* msg,
                                  int flags, int* out_errno) {
  unsigned long args[3] = {(unsigned long)s, (unsigned long)msg,
                           (unsigned long)flags};
  return (ssize_t)lss_socketcall(16, args, out_errno);
}
SYS_WRAP_LSS3(ssize_t, sendmsg, int, s, const struct kernel_msghdr*, msg, int,
              flags);

static inline ssize_t lss_sendto(int s, const void* buf, size_t len, int flags,
                                 const struct kernel_sockaddr* to,
                                 unsigned int tolen, int* out_errno) {
  unsigned long args[6] = {(unsigned long)s,   (unsigned long)buf,
                           (unsigned long)len, (unsigned long)flags,
                           (unsigned long)to,  (unsigned long)tolen};
  return (ssize_t)lss_socketcall(11, args, out_errno);
}
SYS_WRAP_LSS6(ssize_t, sendto, int, s, const void*, buf, size_t, len, int,
              flags, const struct kernel_sockaddr*, to, unsigned int, tolen);

static inline int lss_shutdown(int s, int how, int* out_errno) {
  unsigned long args[2] = {(unsigned long)s, (unsigned long)how};
  return lss_socketcall(13, args, out_errno);
}
SYS_WRAP_LSS2(int, shutdown, int, s, int, how);

static inline int lss_socket(int domain, int type, int protocol,
                             int* out_errno) {
  unsigned long args[3] = {(unsigned long)domain, (unsigned long)type,
                           (unsigned long)protocol};
  return lss_socketcall(1, args, out_errno);
}
SYS_WRAP_LSS3(int, socket, int, domain, int, type, int, protocol);

static inline int lss_socketpair(int d, int type, int protocol, int sv[2],
                                 int* out_errno) {
  unsigned long args[4] = {(unsigned long)d, (unsigned long)type,
                           (unsigned long)protocol, (unsigned long)sv};
  return lss_socketcall(8, args, out_errno);
}
static inline int sys_socketpair(int d, int type, int protocol, int sv[2],
                                 int* out_errno) {
  int local_errno;
  int rc = lss_socketpair(d, type, protocol, sv, &local_errno);
  if (rc == -1) {
    errno = local_errno;
  }
  return rc;
}
#elif defined(__arm__)
SYS_syscall3(ssize_t, recvmsg, int, s, struct kernel_msghdr*, m, int, f);
SYS_syscall3(ssize_t, sendmsg, int, s, const struct kernel_msghdr*, m, int, f);
SYS_syscall6(ssize_t, sendto, int, s, const void*, b, size_t, l, int, f,
             const struct kernel_sockaddr*, to, unsigned int, tl);
SYS_syscall2(int, shutdown, int, s, int, h);
SYS_syscall3(int, socket, int, d, int, t, int, p);
SYS_syscall4(int, socketpair, int, d, int, t, int, p, int*, s);
#endif

#if defined(__i386__) || (defined(__PPC__) && !defined(__PPC64__)) || \
    defined(__arm__)
SYS_syscall4(int, fstatat64, int, d, const char*, p, struct kernel_stat64*, b,
             int, f);
#endif

#if defined(__i386__) || defined(__PPC__)
SYS_syscall3(pid_t, waitpid, pid_t, p, int*, s, int, o);
#endif
#if defined(__NR_pipe)
SYS_syscall1(int, pipe, int*, p);
#endif

#if defined(__NR_pipe2)
SYS_syscall2(int, pipe2, int*, p, int, f);
#endif

/* TODO: see if ppc can/should support this as well */
#if defined(__i386__) || defined(__arm__)
#define __NR__statfs64 __NR_statfs64
#define __NR__fstatfs64 __NR_fstatfs64

SYS_syscall3(int, _statfs64, const char*, p, size_t, s, struct kernel_statfs64*,
             b);
SYS_syscall3(int, _fstatfs64, int, f, size_t, s, struct kernel_statfs64*, b);

static inline int sys_statfs64(const char* p, struct kernel_statfs64* b) {
  return sys__statfs64(p, sizeof(*b), b);
}

static inline int lss_statfs64(const char* p, struct kernel_statfs64* b,
                               int* out_errno) {
  return lss__statfs64(p, sizeof(*b), b, out_errno);
}

static inline int sys_fstatfs64(int f, struct kernel_statfs64* b) {
  return sys__fstatfs64(f, sizeof(*b), b);
}

static inline int lss_fstatfs64(int f, struct kernel_statfs64* b,
                                int* out_errno) {
  return lss__fstatfs64(f, sizeof(*b), b, out_errno);
}
#endif  // defined(__i386__) || defined(__arm__)

static inline int sys_execv(const char* path, const char* const argv[]) {
  extern char** environ;
  return sys_execve(path, argv, (const char* const*)environ);
}

static inline int lss_execv(const char* path, const char* const argv[],
                            int* out_errno) {
  extern char** environ;
  return lss_execve(path, argv, (const char* const*)environ, out_errno);
}

static inline pid_t lss_gettid(int* out_errno) {
  pid_t tid = lss__gettid(out_errno);
  if (tid != -1) {
    return tid;
  }
  return lss_getpid(out_errno);
}
SYS_WRAP_LSS0(pid_t, gettid);

static inline void* sys_mremap(void* old_address, size_t old_size,
                               size_t new_size, int flags, ...) {
  va_list ap;
  void *new_address, *rc;
  va_start(ap, flags);
  new_address = va_arg(ap, void*);
  rc = sys__mremap(old_address, old_size, new_size, flags, new_address);
  va_end(ap);
  return rc;
}

static inline long sys_ptrace_detach(pid_t pid) {
  return sys_ptrace(PTRACE_DETACH, pid, (void*)0, (void*)0);
}

static inline long lss_ptrace_detach(pid_t pid, int* out_errno) {
  return lss_ptrace(PTRACE_DETACH, pid, (void*)0, (void*)0, out_errno);
}

static inline int sys_raise(int sig) { return sys_kill(sys_gettid(), sig); }

static inline int lss_raise(int sig, int* out_errno) {
  return lss_kill(lss_gettid(out_errno), sig, out_errno);
}

static inline int sys_setpgrp() { return sys_setpgid(0, 0); }

static inline int lss_setpgrp(int* out_errno) {
  return lss_setpgid(0, 0, out_errno);
}

static inline long lss_sysconf(int name, int* out_errno) {
  extern int __getpagesize(void)
#ifdef __cplusplus
      noexcept
#endif
      ;
  switch (name) {
    case _SC_OPEN_MAX: {
      struct kernel_rlimit limit;

      /* On some systems getrlimit is obsolete, use ugetrlimit instead. */
#ifndef __NR_getrlimit
      return lss_ugetrlimit(RLIMIT_NOFILE, &limit, out_errno) < 0
                 ? 8192
                 : limit.rlim_cur;
#else
      return lss_getrlimit(RLIMIT_NOFILE, &limit, out_errno) < 0
                 ? 8192
                 : limit.rlim_cur;
#endif
    }
    case _SC_PAGESIZE:
      return __getpagesize();
    default:
      *out_errno = ENOSYS;
      return -1;
  }
}
SYS_WRAP_LSS1(long, sysconf, int, name);

#if defined(_LP64)
#undef pread64
SYS_syscall4(ssize_t, pread64, int, f, void*, b, size_t, c, loff_t, o);
#undef pwrite64
SYS_syscall4(ssize_t, pwrite64, int, f, const void*, b, size_t, c, loff_t, o);
SYS_syscall3(int, readahead, int, f, loff_t, o, unsigned, c);
#else
#define __NR__pread64 __NR_pread64
#define __NR__pwrite64 __NR_pwrite64
#define __NR__readahead __NR_readahead

SYS_syscall5(ssize_t, _pread64, int, f, void*, b, size_t, c, unsigned, o1,
             unsigned, o2);
SYS_syscall5(ssize_t, _pwrite64, int, f, const void*, b, size_t, c, unsigned,
             o1, long, o2);
SYS_syscall4(int, _readahead, int, f, unsigned, o1, unsigned, o2, size_t, c);
/* We force 64bit-wide parameters onto the stack, then access each
 * 32-bit component individually. This guarantees that we build the
 * correct parameters independent of the native byte-order of the
 * underlying architecture.
 */
static inline ssize_t lss_pread64(int fd, void* buf, size_t count, loff_t off,
                                  int* out_errno) {
  union {
    loff_t off;
    unsigned arg[2];
  } o = {off};
  return lss__pread64(fd, buf, count, o.arg[0], o.arg[1], out_errno);
}
SYS_WRAP_LSS4(ssize_t, pread64, int, fd, void*, buf, size_t, count, loff_t,
              off);

static inline ssize_t lss_pwrite64(int fd, const void* buf, size_t count,
                                   loff_t off, int* out_errno) {
  union {
    loff_t off;
    unsigned arg[2];
  } o = {off};
  return lss__pwrite64(fd, buf, count, o.arg[0], o.arg[1], out_errno);
}
SYS_WRAP_LSS4(ssize_t, pwrite64, int, fd, const void*, buf, size_t, count,
              loff_t, off);

static inline int lss_readahead(int fd, loff_t off, int len, int* out_errno) {
  union {
    loff_t off;
    unsigned arg[2];
  } o = {off};
  return lss__readahead(fd, o.arg[0], o.arg[1], len, out_errno);
}
SYS_WRAP_LSS3(int, readahead, int, fd, loff_t, off, int, len);
#endif  // defined(_LP64)

#if defined(__NR_io_setup)
SYS_syscall2(int, io_setup, int, maxevents, unsigned long*, ctxp);
SYS_syscall3(int, io_submit, unsigned long, ctx_id, long, nr,
             struct kernel_iocb**, ios);
SYS_syscall5(int, io_getevents, unsigned long, ctx_id, long, min_nr, long, nr,
             struct kernel_io_event*, events, struct kernel_timespec*, timeout);
SYS_syscall1(int, io_destroy, unsigned long, ctx);
SYS_syscall3(int, io_cancel, unsigned long, ctx_id, struct kernel_iocb*, iocb,
             struct kernel_io_event*, result);
#endif  // defined(__NR_io_setup)

#if defined(__i386__)
#define __NR__fadvise64_64 __NR_fadvise64_64
SYS_syscall6(int, _fadvise64_64, int, fd, unsigned, offset_lo, unsigned,
             offset_hi, unsigned, len_lo, unsigned, len_hi, int, advice);

static inline int lss_fadvise64(int fd, loff_t offset, loff_t len, int advice,
                                int* out_errno) {
  return lss__fadvise64_64(fd, (unsigned)offset, (unsigned)(offset >> 32),
                           (unsigned)len, (unsigned)(len >> 32), advice,
                           out_errno);
}
SYS_WRAP_LSS4(int, fadvise64, int, fd, loff_t, offset, loff_t, len, int,
              advice);

#define __NR__fallocate __NR_fallocate
SYS_syscall6(int, _fallocate, int, fd, int, mode, unsigned, offset_lo, unsigned,
             offset_hi, unsigned, len_lo, unsigned, len_hi);

static inline int lss_fallocate(int fd, int mode, loff_t offset, loff_t len,
                                int* out_errno) {
  union {
    loff_t off;
    unsigned w[2];
  } o = {offset}, l = {len};
  return lss__fallocate(fd, mode, o.w[0], o.w[1], l.w[0], l.w[1], out_errno);
}
SYS_WRAP_LSS4(int, fallocate, int, fd, int, mode, loff_t, offset, loff_t, len);

SYS_syscall1(int, set_thread_area, void*, u);
SYS_syscall1(int, get_thread_area, void*, u);
#endif  // defined(__i386__)

SYS_syscall3(int, close_range, unsigned int, first, unsigned int, last,
             unsigned int, flags);

#undef SYS_syscall0
#undef SYS_syscall1
#undef SYS_syscall2
#undef SYS_syscall3
#undef SYS_syscall4
#undef SYS_syscall5
#undef SYS_syscall6
#undef LSS_syscall0
#undef LSS_syscall1
#undef LSS_syscall2
#undef LSS_syscall3
#undef LSS_syscall4
#undef LSS_syscall5
#undef LSS_syscall6

// Compatibility wrappers defined at end of file so that they can refer
// to other syscalls defined earlier.

#if !defined(__NR_dup2) && defined(__NR_dup3)
/* dup2 isn't provided on Aarch64 but dup3 is. */
static inline int lss_dup2(int oldfd, int newfd, int* out_errno) {
  if (oldfd == newfd) return oldfd;
  return lss_dup3(oldfd, newfd, 0, out_errno);
}
SYS_WRAP_LSS2(int, dup2, int, oldfd, int, newfd)
#endif  // !defined(__NR_dup2) && defined(__NR_dup3)

#if !defined(__NR_getpgrp) && defined(__NR_getpgid)
static inline int sys_getpgrp() { return sys_getpgid(0); }
static inline int lss_getpgrp(int* out_errno) {
  return lss_getpgid(0, out_errno);
}
#endif  // !defined(__NR_getpgrp) && defined(__NR_getpgid)

#if !defined(__NR_open) && defined(__NR_openat)
static inline int sys_open(const char* path, int flags, int mode) {
  return sys_openat(AT_FDCWD, path, flags, mode);
}
static inline int lss_open(const char* path, int flags, int mode,
                           int* out_errno) {
  return lss_openat(AT_FDCWD, path, flags, mode, out_errno);
}
#endif  // !defined(__NR_open) && defined(__NR_openat)

#if !defined(__NR_pipe) && defined(__NR_pipe2)
static inline int sys_pipe(int pipefd[2]) { return sys_pipe2(pipefd, 0); }
static inline int lss_pipe(int pipefd[2], int* out_errno) {
  return lss_pipe2(pipefd, 0, out_errno);
}
#endif  // !defined(__NR_pipe) && defined(__NR_pipe2)

#if !defined(__NR_poll) && defined(__NR_ppoll)
static inline int lss_poll(struct kernel_pollfd* fds, unsigned int nfds,
                           int timeout_in_ms, int* out_errno) {
  struct kernel_timespec timeout_ts, *ts_ptr;
  if (timeout_in_ms >= 0) {
    timeout_ts.tv_nsec = (timeout_in_ms % 1000) * 1000000;
    timeout_ts.tv_sec = timeout_in_ms / 1000;
    ts_ptr = &timeout_ts;
  } else {
    ts_ptr = NULL;
  }
  return lss_ppoll(fds, nfds, ts_ptr, NULL, 0, out_errno);
}
SYS_WRAP_LSS3(int, poll, struct kernel_pollfd*, fds, unsigned int, nfds, int,
              timeout_in_ms);
#endif  // !defined(__NR_poll) && defined(__NR_ppoll)

#if !defined(__NR_readlink) && defined(__NR_readlinkat)
static inline int lss_readlink(const char* pathname, char* buf, size_t bufsize,
                               int* out_errno) {
  return lss_readlinkat(AT_FDCWD, pathname, buf, bufsize, out_errno);
}
SYS_WRAP_LSS3(int, readlink, const char*, pathname, char*, buf, size_t,
              bufsize);
#endif  // !defined(__NR_readlink) && defined(__NR_readlinkat)

#if !defined(__NR_stat) && defined(__NR_newfstatat)
static inline int lss_stat(const char* pathname, struct kernel_stat* buf,
                           int* out_errno) {
  return lss_newfstatat(AT_FDCWD, pathname, buf, 0, out_errno);
}
SYS_WRAP_LSS2(int, stat, const char*, pathname, struct kernel_stat*, buf);
#endif  // !defined(__NR_stat) && defined(__NR_newfstatat)

#if !defined(__NR_unlink) && defined(__NR_unlinkat)
static inline int lss_unlink(const char* pathname, int* out_errno) {
  return lss_unlinkat(AT_FDCWD, pathname, 0, out_errno);
}
SYS_WRAP_LSS1(int, unlink, const char*, pathname);
#endif  // !defined(__NR_unlink) && defined(__NR_unlinkat)

#if !defined(__NR_execveat)
static inline int lss_execveat(int dirfd, const char* path,
                               const char* const* argv, const char* const* envp,
                               int flags, int* out_errno) {
  *out_errno = ENOSYS;
  return -1;
}
#endif  // !defined(__NR_execveat)

#undef SYS_WRAP_LSS0
#undef SYS_WRAP_LSS1
#undef SYS_WRAP_LSS2
#undef SYS_WRAP_LSS3
#undef SYS_WRAP_LSS4
#undef SYS_WRAP_LSS5
#undef SYS_WRAP_LSS6

#if defined(__cplusplus)
}
#endif

#endif  // Supported architectures.
#endif  // THIRD_PARTY_GLOOP_BASE_LINUX_SYSCALL_SUPPORT_H_
