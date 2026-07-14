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

/*
 * Implementation of LSS_PRE and LSS_POST pre- and post- syscall hooks
 * using sanitizer-provided syscall handlers.
 */

#ifndef LINUX_SYSCALL_ENABLE_HOOKS
#error "This header must not be included w/o LINUX_SYSCALL_ENABLE_HOOKS macro."
#endif

// If SYS_CPLUSPLUS is defined, user must include
// <sanitizer/linux_syscall_hooks.h> manually.
#ifndef SYS_CPLUSPLUS
#include <sanitizer/linux_syscall_hooks.h>

/* linux_syscall_hooks.h defines __sanitizer_syscall_post_mmap and
 * __sanitizer_syscall_post_mmap2 as empty macros because SYS_mmap_pgoff is the
 * standard syscall for mmap hooks in sanitizer runtime. Map mmap and mmap2 to
 * the mmap_pgoff implementation so that lss_mmap updates sanitizer shadow
 * memory correctly.
 */
#undef __sanitizer_syscall_pre_mmap
#define __sanitizer_syscall_pre_mmap(addr, len, prot, flags, fd, offset) \
  __sanitizer_syscall_pre_impl_mmap_pgoff((long)(addr), (long)(len),     \
                                          (long)(prot), (long)(flags),   \
                                          (long)(fd), (long)(offset))
#undef __sanitizer_syscall_post_mmap
#define __sanitizer_syscall_post_mmap(res, addr, len, prot, flags, fd, offset) \
  __sanitizer_syscall_post_impl_mmap_pgoff(res, (long)(addr), (long)(len),     \
                                           (long)(prot), (long)(flags),        \
                                           (long)(fd), (long)(offset))
#undef __sanitizer_syscall_pre_mmap2
#define __sanitizer_syscall_pre_mmap2(addr, len, prot, flags, fd, offset) \
  __sanitizer_syscall_pre_impl_mmap_pgoff((long)(addr), (long)(len),      \
                                          (long)(prot), (long)(flags),    \
                                          (long)(fd), (long)(offset))
#undef __sanitizer_syscall_post_mmap2
#define __sanitizer_syscall_post_mmap2(res, addr, len, prot, flags, fd,    \
                                       offset)                             \
  __sanitizer_syscall_post_impl_mmap_pgoff(res, (long)(addr), (long)(len), \
                                           (long)(prot), (long)(flags),    \
                                           (long)(fd), (long)(offset))
#endif

/* linux_syscall_support.h defines a bunch of non-standard NR_* constants and
 * syscall-like functions (ex.: _pread64 and NR__pread64). Syscall hook names
 * constructed with LSS_PRE and LSS_POST macros below contain an extra
 * underscore as a result. Map them back to the standard syscall names.
 */
#define __sanitizer_syscall_pre__fadvise64_64 __sanitizer_syscall_pre_fadvise64_64
#define __sanitizer_syscall_pre__fallocate __sanitizer_syscall_pre_fallocate
#define __sanitizer_syscall_pre__exit __sanitizer_syscall_pre_exit
#define __sanitizer_syscall_pre__gettid __sanitizer_syscall_pre_gettid
#define __sanitizer_syscall_pre__mremap __sanitizer_syscall_pre_mremap
#define __sanitizer_syscall_pre__getresgid32 __sanitizer_syscall_pre_getresgid32
#define __sanitizer_syscall_pre__getresuid32 __sanitizer_syscall_pre_getresuid32
#define __sanitizer_syscall_pre__setfsgid32 __sanitizer_syscall_pre_setfsgid32
#define __sanitizer_syscall_pre__setfsuid32 __sanitizer_syscall_pre_setfsuid32
#define __sanitizer_syscall_pre__setgroups32 __sanitizer_syscall_pre_setgroups32
#define __sanitizer_syscall_pre__setgroups __sanitizer_syscall_pre_setgroups
#define __sanitizer_syscall_pre__setresgid32 __sanitizer_syscall_pre_setresgid32
#define __sanitizer_syscall_pre__setresuid32 __sanitizer_syscall_pre_setresuid32
#define __sanitizer_syscall_pre__sigaction __sanitizer_syscall_pre_sigaction
#define __sanitizer_syscall_pre__sigpending __sanitizer_syscall_pre_sigpending
#define __sanitizer_syscall_pre__sigprocmask __sanitizer_syscall_pre_sigprocmask
#define __sanitizer_syscall_pre__sigsuspend __sanitizer_syscall_pre_sigsuspend
#define __sanitizer_syscall_pre__statfs64 __sanitizer_syscall_pre_statfs64
#define __sanitizer_syscall_pre__fstatfs64 __sanitizer_syscall_pre_fstatfs64
#define __sanitizer_syscall_pre__pread64 __sanitizer_syscall_pre_pread64
#define __sanitizer_syscall_pre__pwrite64 __sanitizer_syscall_pre_pwrite64
#define __sanitizer_syscall_pre__readahead __sanitizer_syscall_pre_readahead

#define __sanitizer_syscall_post__fadvise64_64 __sanitizer_syscall_post_fadvise64_64
#define __sanitizer_syscall_post__fallocate __sanitizer_syscall_post_fallocate
#define __sanitizer_syscall_post__exit __sanitizer_syscall_post_exit
#define __sanitizer_syscall_post__gettid __sanitizer_syscall_post_gettid
#define __sanitizer_syscall_post__mremap __sanitizer_syscall_post_mremap
#define __sanitizer_syscall_post__getresgid32 __sanitizer_syscall_post_getresgid32
#define __sanitizer_syscall_post__getresuid32 __sanitizer_syscall_post_getresuid32
#define __sanitizer_syscall_post__setfsgid32 __sanitizer_syscall_post_setfsgid32
#define __sanitizer_syscall_post__setfsuid32 __sanitizer_syscall_post_setfsuid32
#define __sanitizer_syscall_post__setgroups32 __sanitizer_syscall_post_setgroups32
#define __sanitizer_syscall_post__setgroups __sanitizer_syscall_post_setgroups
#define __sanitizer_syscall_post__setresgid32 __sanitizer_syscall_post_setresgid32
#define __sanitizer_syscall_post__setresuid32 __sanitizer_syscall_post_setresuid32
#define __sanitizer_syscall_post__sigaction __sanitizer_syscall_post_sigaction
#define __sanitizer_syscall_post__sigpending __sanitizer_syscall_post_sigpending
#define __sanitizer_syscall_post__sigprocmask __sanitizer_syscall_post_sigprocmask
#define __sanitizer_syscall_post__sigsuspend __sanitizer_syscall_post_sigsuspend
#define __sanitizer_syscall_post__statfs64 __sanitizer_syscall_post_statfs64
#define __sanitizer_syscall_post__fstatfs64 __sanitizer_syscall_post_fstatfs64
#define __sanitizer_syscall_post__pread64 __sanitizer_syscall_post_pread64
#define __sanitizer_syscall_post__pwrite64 __sanitizer_syscall_post_pwrite64
#define __sanitizer_syscall_post__readahead __sanitizer_syscall_post_readahead

/* linux_syscall_hooks.h does not presently have hooks for close_range; stub
 * them here. */
#ifndef __sanitizer_syscall_pre_close_range
#define __sanitizer_syscall_pre_close_range(...)
#endif
#ifndef __sanitizer_syscall_post_close_range
#define __sanitizer_syscall_post_close_range(...)
#endif

#ifndef __sanitizer_syscall_pre_execveat
#define __sanitizer_syscall_pre_execveat(...)
#endif
#ifndef __sanitizer_syscall_post_execveat
#define __sanitizer_syscall_post_execveat(...)
#endif

#define LSS_PRE(name, ...) __sanitizer_syscall_pre_##name(__VA_ARGS__)
#define LSS_POST(name, ...) __sanitizer_syscall_post_##name(__VA_ARGS__)
