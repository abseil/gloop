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

// Include linux_syscall_support.h as the first file, so we will compilation
// errors if it has unexpected dependencies on other header files.
#include "gloop/base/linux_syscall_support.h"

// Define kernel data structures as known to glibc.
// Warning: do NOT include <asm/stat.h> here;
// it's incompatible with <sys/stat.h>.
// Instead, use accessors from "base/linux_syscall_support_unittest_helper.h".
#include <asm/poll.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <linux/capability.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdint>
#include <ctime>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/linux_syscall_support_unittest_helper.h"
#include "gloop/base/log_file_flags.h"
#include "gloop/thread/thread_control.h"
#include "gtest/gtest.h"

// Set by the signal handler to show that we received a signal
static int signaled;

template <typename UserType, typename KernelType,
          size_t UserSize = sizeof(UserType),
          size_t KernelSize = sizeof(KernelType)>
static void check_size() {
  static_assert(UserSize == KernelSize, "mismatched user and kernel size");
}

TEST(LinuxSyscallSupport, CheckStructures) {
  puts("CheckStructures...");
  // Compare sizes of the kernel structures. This will allow us to
  // catch cases where linux_syscall_support.h defined structures that
  // are obviously different from the ones the kernel expects. This is
  // a little complicated, because glibc often deliberately defines
  // incompatible versions. We address this issue on a case-by-case
  // basis by including the appropriate linux-specific header files
  // within our own namespace, instead of within the global
  // namespace. Occasionally, this requires careful sorting of header
  // files, too (e.g. in the case of "stat.h"). And we provide cleanup
  // where necessary (e.g. in the case of "struct statfs").  This is
  // far from perfect, but in the worst case, it'll lead to false
  // error messages that need to be fixed manually.  Unfortunately,
  // there are a small number of data structures (e.g "struct
  // kernel_old_sigaction") that we cannot test at all, as glibc does
  // not have any definitions for them.

#define CHECK_STRUCT(name) check_size<struct name, struct kernel_##name>()

  CHECK_STRUCT(iovec);
  CHECK_STRUCT(msghdr);
  CHECK_STRUCT(pollfd);
  check_size<struct rusage, struct kernel_rusage, sizeof(struct rusage)>();
  check_size<
      struct sigaction, struct kernel_sigaction, sizeof(struct sigaction),
      sizeof(struct kernel_sigaction)
          // glibc defines an excessively large sigset_t. Compensate for it:
          + sizeof((static_cast<struct sigaction*>(nullptr))->sa_mask) -
          KERNEL_NSIG / 8>();
  CHECK_STRUCT(sockaddr);
  EXPECT_EQ(asm_stat_size(), sizeof(struct kernel_stat));
  check_size<struct statfs, struct kernel_statfs, sizeof(struct statfs),
#if !defined(_LP64) && (defined(__ANDROID__))
             sizeof(struct kernel_statfs64)
#else
             sizeof(struct kernel_statfs)
#if defined(__USE_FILE_OFFSET64)
                 // glibc sometimes defines 64-bit wide fields in "struct
                 // statfs" even though this is just the 32-bit version of the
                 // structure.
                 +
                 5 * (sizeof((static_cast<struct statfs*>(nullptr))->f_blocks) -
                      sizeof(unsigned))
#endif
#endif
             >();
  CHECK_STRUCT(timespec);
#if !defined(__x86_64__) && !defined(__PPC64__) && !defined(__aarch64__) && \
    (!defined(__riscv) || __riscv_xlen != 64)
  EXPECT_EQ(asm_stat64_size(), sizeof(struct kernel_stat64));
  CHECK_STRUCT(statfs64);
#endif
#undef CHECK_STRUCT
}

#define ZERO_SIGACT {{0}}

static void SigHandler(int signum) {
  if (signaled) {
    // Caller will report an error, as we cannot do so from a signal handler
    signaled = -1;
  } else {
    signaled = signum;
  }
}

static void SigAction(int signum, siginfo_t* si, void* arg) {
  SigHandler(signum);
}

static void LssExit(int status, int* error) { lss_exit_group(status, error); }

TEST(LinuxSyscallSupport, Sigaction) {
#if defined(__ANDROID__) && defined(__i386__)
  LOG(WARNING) << "Android x86 only supports old_sigaction.";
#else
  puts("Sigaction...");
  int signum = SIGPWR;
  for (int info = 0; info < 2; info++) {
    signaled = 0;
    struct kernel_sigaction sa = ZERO_SIGACT, old, orig;

    // MSan can't intercept direct syscall below. Tell MSan this memory
    // has been initialized.
    memset(&orig, 0, sizeof(orig));

    ASSERT_EQ(0, lss_sigaction(signum, nullptr, &orig, &errno));
    if (info) {
      sa.sa_sigaction_ = SigAction;
    } else {
      sa.sa_handler_ = SigHandler;
    }
    sa.sa_flags = SA_RESETHAND | SA_RESTART | (info ? SA_SIGINFO : 0);
    EXPECT_EQ(0, lss_sigemptyset(&sa.sa_mask));
    EXPECT_EQ(0, lss_sigaction(signum, &sa, &old, &errno));
    EXPECT_EQ(0, memcmp(&old, &orig, sizeof(struct kernel_sigaction)));
    EXPECT_EQ(0, lss_sigaction(signum, nullptr, &old, &errno));
#if defined(__i386__) || defined(__x86_64__)
    old.sa_restorer = sa.sa_restorer;
    old.sa_flags &= ~SA_RESTORER;
#endif
    EXPECT_EQ(0, memcmp(&old, &sa, sizeof(struct kernel_sigaction)));
    struct kernel_sigset_t pending;
    EXPECT_EQ(0, lss_sigpending(&pending, &errno));
    EXPECT_EQ(0, lss_sigismember(&pending, signum, &errno));
    struct kernel_sigset_t mask, oldmask;
    EXPECT_EQ(0, lss_sigemptyset(&mask));
    EXPECT_EQ(0, lss_sigaddset(&mask, signum, &errno));
    EXPECT_EQ(0, lss_sigprocmask(SIG_BLOCK, &mask, &oldmask, &errno));
    EXPECT_EQ(0, lss_kill(lss_getpid(&errno), signum, &errno));
    EXPECT_EQ(0, lss_sigpending(&pending, &errno));
    EXPECT_EQ(1, lss_sigismember(&pending, signum, &errno));
    EXPECT_EQ(0, signaled);
    EXPECT_EQ(0, lss_sigfillset(&mask));
    EXPECT_EQ(0, lss_sigdelset(&mask, signum, &errno));
    EXPECT_EQ(-1, lss_sigsuspend(&mask, &errno));
    EXPECT_EQ(signaled, signum);
    EXPECT_EQ(0, lss_sigaction(signum, &orig, nullptr, &errno));
    EXPECT_EQ(0, lss_sigprocmask(SIG_SETMASK, &oldmask, nullptr, &errno));
  }
#endif
}

TEST(LinuxSyscallSupport, OldSigaction) {
#if defined(__i386__) || defined(__arm__) || \
    (defined(__PPC__) && !defined(__PPC64__))
  puts("OldSigaction...");
  int signum = SIGPWR;
  for (int info = 0; info < 2; info++) {
    signaled = 0;
    struct kernel_old_sigaction sa = ZERO_SIGACT, old, orig;
    EXPECT_EQ(0, lss__sigaction(signum, nullptr, &orig, &errno));
    if (info) {
      sa.sa_sigaction_ = SigAction;
    } else {
      sa.sa_handler_ = SigHandler;
    }
    sa.sa_flags = SA_RESETHAND | SA_RESTART | (info ? SA_SIGINFO : 0);
    memset(&sa.sa_mask, 0, sizeof(sa.sa_mask));
    EXPECT_EQ(0, lss__sigaction(signum, &sa, &old, &errno));
    EXPECT_EQ(0, memcmp(&old, &orig, sizeof(struct kernel_old_sigaction)));
    EXPECT_EQ(0, lss__sigaction(signum, nullptr, &old, &errno));
    old.sa_restorer = sa.sa_restorer;
    EXPECT_EQ(0, memcmp(&old, &sa, sizeof(struct kernel_old_sigaction)));
    unsigned long pending;
    EXPECT_EQ(0, lss__sigpending(&pending, &errno));
    EXPECT_EQ(0, (pending & (1UL << (signum - 1))));
    unsigned long mask, oldmask;
    mask = 1 << (signum - 1);
    EXPECT_EQ(0, lss__sigprocmask(SIG_BLOCK, &mask, &oldmask, &errno));
    EXPECT_EQ(0, lss_kill(lss_getpid(&errno), signum, &errno));
    EXPECT_EQ(0, lss__sigpending(&pending, &errno));
    EXPECT_EQ(pending, 1UL << (signum - 1));
    EXPECT_EQ(0, signaled);
    mask = ~mask;
    EXPECT_EQ(-1, lss__sigsuspend(
#ifndef __PPC__
                      &mask, 0,
#endif
                      mask, &errno));
    EXPECT_EQ(signaled, signum);
    EXPECT_EQ(0, lss__sigaction(signum, &orig, nullptr, &errno));
    EXPECT_EQ(0, lss__sigprocmask(SIG_SETMASK, &oldmask, nullptr, &errno));
  }
#else
  LOG(WARNING) << "OldSigaction not supported on this architecture";
#endif
}

template <class A, class B>
static void AlmostEquals(A a, B b) {
  double d = 0.0 + a - b;
  if (d < 0) {
    d = -d;
  }
  double avg = a / 2.0 + b / 2.0;
  if (avg < 4096) {
    // Round up to a minimum size. Otherwise, even minute changes could
    // trigger a false positive.
    avg = 4096;
  }
  // Check that a and b are within one percent of each other.
  EXPECT_LT(d / avg, 0.01);
}

TEST(LinuxSyscallSupport, StatFs) {
#if defined(__ANDROID__) && __ANDROID_API__ < __ANDROID_API_L__
  LOG(WARNING) << "statfs64 is only supported after Android API 21. "
                  "Test was compiled with API "
               << __ANDROID_API__;
#else
  puts("StatFs...");
  struct statfs64 libc_statfs;
  struct kernel_statfs kernel_statfs;
  EXPECT_EQ(0, statfs64("/", &libc_statfs));
  EXPECT_EQ(0, lss_statfs("/", &kernel_statfs, &errno));
  EXPECT_EQ(libc_statfs.f_type, kernel_statfs.f_type);
  EXPECT_EQ(libc_statfs.f_bsize, kernel_statfs.f_bsize);
  EXPECT_EQ(libc_statfs.f_blocks, kernel_statfs.f_blocks);
  AlmostEquals(libc_statfs.f_bfree, kernel_statfs.f_bfree);
  AlmostEquals(libc_statfs.f_bavail, kernel_statfs.f_bavail);
  EXPECT_EQ(libc_statfs.f_files, kernel_statfs.f_files);
  AlmostEquals(libc_statfs.f_ffree, kernel_statfs.f_ffree);
  EXPECT_EQ(libc_statfs.f_fsid.__val[0], kernel_statfs.f_fsid.val[0]);
  EXPECT_EQ(libc_statfs.f_fsid.__val[1], kernel_statfs.f_fsid.val[1]);
  EXPECT_EQ(libc_statfs.f_namelen, kernel_statfs.f_namelen);
#endif
}

TEST(LinuxSyscallSupport, StatFs64) {
#if defined(__ANDROID__) && __ANDROID_API__ < __ANDROID_API_L__
  LOG(WARNING) << "statfs64 is only supported after Android API 21. "
                  "Test was compiled with API "
               << __ANDROID_API__;
#elif (_LP64)
  LOG(INFO) << "64-bit: statfs64 == statfs";
#else
  puts("StatFs64...");
  struct statfs64 libc_statfs;
  struct kernel_statfs64 kernel_statfs;
  EXPECT_EQ(0, statfs64("/", &libc_statfs));
  EXPECT_EQ(0, lss_statfs64("/", &kernel_statfs, &errno));
  EXPECT_EQ(libc_statfs.f_type, kernel_statfs.f_type);
  EXPECT_EQ(libc_statfs.f_bsize, kernel_statfs.f_bsize);
  EXPECT_EQ(libc_statfs.f_blocks, kernel_statfs.f_blocks);
  AlmostEquals(libc_statfs.f_bfree, kernel_statfs.f_bfree);
  AlmostEquals(libc_statfs.f_bavail, kernel_statfs.f_bavail);
  EXPECT_EQ(libc_statfs.f_files, kernel_statfs.f_files);
  AlmostEquals(libc_statfs.f_ffree, kernel_statfs.f_ffree);
  EXPECT_EQ(libc_statfs.f_fsid.__val[0], kernel_statfs.f_fsid.val[0]);
  EXPECT_EQ(libc_statfs.f_fsid.__val[1], kernel_statfs.f_fsid.val[1]);
  EXPECT_EQ(libc_statfs.f_namelen, kernel_statfs.f_namelen);
#endif
}

class LssTestWithFilePaths : public ::testing::TestWithParam<const char*> {};

INSTANTIATE_TEST_SUITE_P(All, LssTestWithFilePaths,
                         testing::Values("/dev/null",
#if !defined(__ANDROID__)
                                         "/bin/sh",
#else
                                         "/proc/self/exe",
#endif
                                         "/"));

TEST_P(LssTestWithFilePaths, Stat) {
  puts("Stat...");
  struct ::stat64 libc_stat;
  struct kernel_stat kernel_stat;
  ASSERT_EQ(0, ::stat64(GetParam(), &libc_stat)) << GetParam();

  // MSan can't intercept direct syscall below. Tell MSan this memory
  // has been initialized.
  memset(&kernel_stat, 0, sizeof(kernel_stat));

  EXPECT_EQ(0, lss_stat(GetParam(), &kernel_stat, &errno));
  //  EXPECT_EQ(libc_stat.st_dev, kernel_stat.st_dev);
  EXPECT_EQ(libc_stat.st_ino, kernel_stat.st_ino);
  EXPECT_EQ(libc_stat.st_mode, kernel_stat.st_mode);
  EXPECT_EQ(libc_stat.st_nlink, kernel_stat.st_nlink);
  EXPECT_EQ(libc_stat.st_uid, kernel_stat.st_uid);
  EXPECT_EQ(libc_stat.st_gid, kernel_stat.st_gid);
  EXPECT_EQ(libc_stat.st_rdev, kernel_stat.st_rdev);
  EXPECT_EQ(libc_stat.st_size, kernel_stat.st_size);
#if !defined(__i386__) && !defined(__arm__) && \
    !(defined(__PPC__) && !defined(__PPC64__))
  EXPECT_EQ(libc_stat.st_blksize, kernel_stat.st_blksize);
  EXPECT_EQ(libc_stat.st_blocks, kernel_stat.st_blocks);
#endif
  EXPECT_EQ(libc_stat.st_atime, kernel_stat.st_atime_);
  EXPECT_EQ(libc_stat.st_mtime, kernel_stat.st_mtime_);
  EXPECT_EQ(libc_stat.st_ctime, kernel_stat.st_ctime_);
}

TEST_P(LssTestWithFilePaths, Stat64) {
#if defined(__i386__) || defined(__arm__) || \
    (defined(__PPC__) && !defined(__PPC64__))
  puts("Stat64...");
  struct ::stat64 libc_stat;
  struct kernel_stat64 kernel_stat;
  ASSERT_EQ(0, ::stat64(GetParam(), &libc_stat));
  EXPECT_EQ(0, lss_stat64(GetParam(), &kernel_stat, &errno));
  EXPECT_EQ(libc_stat.st_dev, kernel_stat.st_dev);
  EXPECT_EQ(libc_stat.st_ino, kernel_stat.st_ino);
  EXPECT_EQ(libc_stat.st_mode, kernel_stat.st_mode);
  EXPECT_EQ(libc_stat.st_nlink, kernel_stat.st_nlink);
  EXPECT_EQ(libc_stat.st_uid, kernel_stat.st_uid);
  EXPECT_EQ(libc_stat.st_gid, kernel_stat.st_gid);
  EXPECT_EQ(libc_stat.st_rdev, kernel_stat.st_rdev);
  EXPECT_EQ(libc_stat.st_size, kernel_stat.st_size);
  EXPECT_EQ(libc_stat.st_blksize, kernel_stat.st_blksize);
  EXPECT_EQ(libc_stat.st_blocks, kernel_stat.st_blocks);
  EXPECT_EQ(libc_stat.st_atime, kernel_stat.st_atime_);
  EXPECT_EQ(libc_stat.st_mtime, kernel_stat.st_mtime_);
  EXPECT_EQ(libc_stat.st_ctime, kernel_stat.st_ctime_);
#else
  LOG(INFO) << "64-bit: stat64 == stat";
#endif
}

static void get_kernel_version(int* major, int* minor, int* teeny) {
  struct utsname u;
  *major = *minor = *teeny = 0;
  CHECK_EQ(0, uname(&u));
  CHECK_EQ(3, sscanf(u.release, "%d.%d.%d", major, minor, teeny));
}

static bool kernel_version_is_at_least(int major, int minor, int teeny) {
  int this_major, this_minor, this_teeny;
  get_kernel_version(&this_major, &this_minor, &this_teeny);
  return ((this_major > major) || (this_major == major && this_minor > minor) ||
          (this_major == major && this_minor == minor && this_teeny >= teeny));
}

// Test clock_getres kernel call.
//
// We check for the current process and thread, accessed via pid/tid '0',
// and also accessed via the actual pid and tid.
//
// We verify that the clock resolutions reported by lss_clock_getres
// are non-zero, and assume they they are less than 1s.
TEST(LinuxSyscallSupport, ClockGetRes) {
  struct kernel_timespec t;
  pid_t mypid = getpid();
  pid_t mytid = lss_gettid(&errno);

  puts("ClockGetRes...");

  // Prior to 2.6.12, these clocks were not supported.
  if (!kernel_version_is_at_least(2, 6, 12)) {
    puts("Not testing (tested clocks need at least kernel 2.6.12)");
    return;
  }

  // Initialize t to have a value that may trip the first result
  // assertion if the syscall didn't return an error but didn't work
  // as intended either (e.g., we got the wrong syscall number).
  t.tv_sec = -1;
  t.tv_nsec = 0;

  EXPECT_EQ(
      0, lss_clock_getres(MAKE_PROCESS_CPUCLOCK(0, CPUCLOCK_PROF), &t, &errno));
  EXPECT_EQ(0, t.tv_sec);
  EXPECT_NE(0, t.tv_nsec);
  EXPECT_EQ(
      0, lss_clock_getres(MAKE_PROCESS_CPUCLOCK(0, CPUCLOCK_VIRT), &t, &errno));
  EXPECT_EQ(0, t.tv_sec);
  EXPECT_NE(0, t.tv_nsec != 0);
  EXPECT_EQ(0, lss_clock_getres(MAKE_PROCESS_CPUCLOCK(0, CPUCLOCK_SCHED), &t,
                                &errno));
  EXPECT_EQ(0, t.tv_sec);
  EXPECT_NE(0, t.tv_nsec);

  EXPECT_EQ(0, lss_clock_getres(MAKE_PROCESS_CPUCLOCK(mypid, CPUCLOCK_PROF), &t,
                                &errno));
  EXPECT_EQ(0, t.tv_sec);
  EXPECT_NE(0, t.tv_nsec);
  EXPECT_EQ(0, lss_clock_getres(MAKE_PROCESS_CPUCLOCK(mypid, CPUCLOCK_VIRT), &t,
                                &errno));
  EXPECT_EQ(0, t.tv_sec);
  EXPECT_NE(0, t.tv_nsec);
  EXPECT_EQ(0, lss_clock_getres(MAKE_PROCESS_CPUCLOCK(mypid, CPUCLOCK_SCHED),
                                &t, &errno));
  EXPECT_EQ(0, t.tv_sec);
  EXPECT_NE(0, t.tv_nsec);

  EXPECT_EQ(
      0, lss_clock_getres(MAKE_THREAD_CPUCLOCK(0, CPUCLOCK_PROF), &t, &errno));
  EXPECT_EQ(0, t.tv_sec);
  EXPECT_NE(0, t.tv_nsec);
  EXPECT_EQ(
      0, lss_clock_getres(MAKE_THREAD_CPUCLOCK(0, CPUCLOCK_VIRT), &t, &errno));
  EXPECT_EQ(0, t.tv_sec);
  EXPECT_NE(0, t.tv_nsec);
  EXPECT_EQ(
      0, lss_clock_getres(MAKE_THREAD_CPUCLOCK(0, CPUCLOCK_SCHED), &t, &errno));
  EXPECT_EQ(0, t.tv_sec);
  EXPECT_NE(0, t.tv_nsec);

  EXPECT_EQ(0, lss_clock_getres(MAKE_THREAD_CPUCLOCK(mytid, CPUCLOCK_PROF), &t,
                                &errno));
  EXPECT_EQ(0, t.tv_sec);
  EXPECT_NE(0, t.tv_nsec);
  EXPECT_EQ(0, lss_clock_getres(MAKE_THREAD_CPUCLOCK(mytid, CPUCLOCK_VIRT), &t,
                                &errno));
  EXPECT_EQ(0, t.tv_sec);
  EXPECT_NE(0, t.tv_nsec);
  EXPECT_EQ(0, lss_clock_getres(MAKE_THREAD_CPUCLOCK(mytid, CPUCLOCK_SCHED), &t,
                                &errno));
  EXPECT_EQ(0, t.tv_sec);
  EXPECT_NE(0, t.tv_nsec);
}

// Consume some measurable amount of system time.  We do this by
// mmap'ing a bunch of memory, and causing the kernel to zero-fill it.
//
// This code (causing the kernel to zero 512MiB of memory) consumes
// approximately 0.42s of CPU time on a Intel Core2 Duo system @ 2.4GHz.
//
// We're assuming that it'll take "some measurable amount of time" (>
// 0.01s, at least) on the fastest system we care about, and won't run
// for an oppressively long time on the slowest.
static void consume_system_time() {
  const int size = 32 * 1024 * 1024;  // 32MiB at a time.
  const int num_iters = 16;           // 32MiB * 16 = 512MiB total.
  const int page_size = getpagesize();

  for (int iter = 0; iter < num_iters; iter++) {
#if defined(__i386__) || defined(__arm__) || \
    (defined(__PPC__) && !defined(__PPC64__))
    char* addr = reinterpret_cast<char*>(
        lss_mmap2(nullptr, size, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0, &errno));
#else
    char* addr = reinterpret_cast<char*>(
        lss_mmap(nullptr, size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0, &errno));
#endif
    CHECK(addr != MAP_FAILED);

    for (int offset = 0; offset < size; offset += page_size) {
      addr[offset] = 1;  // Forces kernel to zero-fill the whole page.
    }

    EXPECT_EQ(0, lss_munmap(addr, size, &errno));
  }
}

// Test clock_gettime kernel call.  This test will only succeed on
// Linux 2.6.12 and later.
//
// We check for the current process and thread, accessed via pid/tid
// '0', and also accessed via the actual pid and tid.
//
// Before starting to test the timers, we attempt to consume a
// measurable amount of system time, then We verify that CPUCLOCK_PROF
// (which is user + system time) is always greater than CPUCLOCK_VIRT
// (user time only).  We don't try to verify anything about
// CPUCLOCK_SCHED, since it seems to be collected in a different way
// than the other two clocks.
TEST(LinuxSyscallSupport, ClockGetTime) {
  struct kernel_timespec tp, tv, ts;
  pid_t mypid = getpid();
  pid_t mytid = lss_gettid(&errno);

  puts("ClockGetTime...");

  // Prior to 2.6.12, these clocks were not supported.
  if (!kernel_version_is_at_least(2, 6, 12)) {
    puts("Not testing (tested clocks need at least kernel 2.6.12)");
    return;
  }

  consume_system_time();

  // Initialize tp and tv to have values that may trip the first
  // result assertion if the syscall didn't return an error but didn't
  // work as intended either (e.g., we got the wrong syscall number).
  tp.tv_sec = -12345;
  tp.tv_nsec = -2000000000;
  tv.tv_sec = 12345;
  tv.tv_nsec = 2000000000;

  EXPECT_EQ(0, lss_clock_gettime(MAKE_PROCESS_CPUCLOCK(0, CPUCLOCK_PROF), &tp,
                                 &errno));
  EXPECT_EQ(0, lss_clock_gettime(MAKE_PROCESS_CPUCLOCK(0, CPUCLOCK_VIRT), &tv,
                                 &errno));
  EXPECT_EQ(0, lss_clock_gettime(MAKE_PROCESS_CPUCLOCK(0, CPUCLOCK_SCHED), &ts,
                                 &errno));
  EXPECT_GT((int64_t)tp.tv_sec * 1000000000 + tp.tv_nsec,
            (int64_t)tv.tv_sec * 1000000000 + tv.tv_nsec);

  EXPECT_EQ(0, lss_clock_gettime(MAKE_PROCESS_CPUCLOCK(mypid, CPUCLOCK_PROF),
                                 &tp, &errno));
  EXPECT_EQ(0, lss_clock_gettime(MAKE_PROCESS_CPUCLOCK(mypid, CPUCLOCK_VIRT),
                                 &tv, &errno));
  EXPECT_EQ(0, lss_clock_gettime(MAKE_PROCESS_CPUCLOCK(mypid, CPUCLOCK_SCHED),
                                 &ts, &errno));
  EXPECT_GT((int64_t)tp.tv_sec * 1000000000 + tp.tv_nsec,
            (int64_t)tv.tv_sec * 1000000000 + tv.tv_nsec);

  EXPECT_EQ(0, lss_clock_gettime(MAKE_THREAD_CPUCLOCK(0, CPUCLOCK_PROF), &tp,
                                 &errno));
  EXPECT_EQ(0, lss_clock_gettime(MAKE_THREAD_CPUCLOCK(0, CPUCLOCK_VIRT), &tv,
                                 &errno));
  EXPECT_EQ(0, lss_clock_gettime(MAKE_THREAD_CPUCLOCK(0, CPUCLOCK_SCHED), &ts,
                                 &errno));
  EXPECT_GT((int64_t)tp.tv_sec * 1000000000 + tp.tv_nsec,
            (int64_t)tv.tv_sec * 1000000000 + tv.tv_nsec);

  EXPECT_EQ(0, lss_clock_gettime(MAKE_THREAD_CPUCLOCK(mytid, CPUCLOCK_PROF),
                                 &tp, &errno));
  EXPECT_EQ(0, lss_clock_gettime(MAKE_THREAD_CPUCLOCK(mytid, CPUCLOCK_VIRT),
                                 &tv, &errno));
  EXPECT_EQ(0, lss_clock_gettime(MAKE_THREAD_CPUCLOCK(mytid, CPUCLOCK_SCHED),
                                 &ts, &errno));
  EXPECT_GT((int64_t)tp.tv_sec * 1000000000 + tp.tv_nsec,
            (int64_t)tv.tv_sec * 1000000000 + tv.tv_nsec);
}

TEST(LinuxSyscallSupport, GetCPU) {
#if !defined(__NR_getcpu)
  LOG(WARNING) << "getcpu syscall not defined";
#else
  unsigned int cpu = -1;
  // lss_getcpu(&errno) added in 2.6.24.
  if (!kernel_version_is_at_least(2, 6, 24)) {
    puts("Not testing (lss_getcpu needs at least kernel 2.6.24)");
    return;
  }
  EXPECT_EQ(0, lss_getcpu(&cpu, nullptr, nullptr, &errno));
  EXPECT_GE(cpu, 0);
  EXPECT_LT(cpu, 2048);  // Bump when we deploy machines with more CPUs to prod.
#endif
}

// Write p to stdout without calling into libc. Note that it's not safe to call
// into libc after a manual call to 'clone', as libc's bookkeeping will not be
// correct for the new process.
static int local_putstr(const char* p) {
  // n = strlen(p).
  size_t n = 0;
  for (const char* q = p; *q != '\0'; ++q) {
    ++n;
  }

  while (n > 0) {
    int rc = lss_write(1, p, n, &errno);
    if (rc == -1) {
      if (errno == EINTR) continue;
      return EOF;
    }
    n -= rc;
  }
  return 0;
}

#if defined(__i386__) || defined(__x86_64__)
static int sse_callback(void* unused) {
#if defined(__ANDROID__) && (__ANDROID_API__ <= __ANDROID_API_O__)
  local_putstr(
      "Android pre-P may have unaligned stacks, "
      "alignment not tested see b/31809417.\n");
  return 0;
#endif

  /* If the stack is not aligned properly, the movaps instruction below
   * shall raise SIGSEGV.
   */
  local_putstr("Using SSE (stack must be aligned on 0x10)\n");
  char local[16];
  __asm__ __volatile__("movaps %0, %%xmm1\n" : "=m"(local));
  return 0;
}
#endif

// Used by no_sse_callback below to check that argument
// passing works in lss_clone.
static int no_sse_callback_arg;

static int no_sse_callback(void* arg) {
  local_putstr("Not using SSE\n");
  return (arg == &no_sse_callback_arg) ? 0 : 1;
}

static void run_callback(int (*callback)(void*), char* stack, void* arg) {
  pid_t child =
      lss_clone(callback, (void*)stack, CLONE_VM | CLONE_FS | CLONE_FILES, arg,
                nullptr, nullptr, nullptr, &errno);
  ASSERT_NE(child, -1);
  int rc, status;
  while ((rc = lss_waitpid(child, &status, __WALL, &errno)) < 0 &&
         errno == EINTR) {
    /* Keep waiting */
  }
  ASSERT_GE(rc, 0) << strerror(errno);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(0, WEXITSTATUS(status));
}

TEST(LinuxSyscallSupport, Clone) {
  const unsigned int kSize = 4096;
#ifndef MAP_STACK
// On some platform like ARM port of eglibc-2.11.1, there is no MAP_STACK.
#define MAP_STACK 0
#endif
#if defined(__i386__) || defined(__arm__) || \
    (defined(__PPC__) && !defined(__PPC64__))
  char* mapped_memory = reinterpret_cast<char*>(
      lss_mmap2(nullptr, kSize, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0, &errno));
#else
  char* mapped_memory = reinterpret_cast<char*>(
      lss_mmap(nullptr, kSize, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0, &errno));
#endif
  ASSERT_TRUE(mapped_memory != MAP_FAILED);
  char local_array[kSize];
  char* local_stack = local_array + kSize;
  char* mapped_stack = mapped_memory + kSize;
  run_callback(no_sse_callback, local_stack, &no_sse_callback_arg);
  run_callback(no_sse_callback, mapped_stack, &no_sse_callback_arg);
  run_callback(no_sse_callback, local_stack - 8, &no_sse_callback_arg);
  run_callback(no_sse_callback, mapped_stack - 8, &no_sse_callback_arg);
#ifdef __SSE2__
  run_callback(sse_callback, local_stack, nullptr);
  run_callback(sse_callback, mapped_stack, nullptr);
  run_callback(sse_callback, local_stack - 8, nullptr);
  run_callback(sse_callback, mapped_stack - 8, nullptr);
#endif
  EXPECT_EQ(0, lss_munmap(mapped_memory, kSize, &errno));
}

static void run_callback_with_fork(int (*callback)(void*), void* arg) {
  pid_t child = lss_fork(&errno);
  CHECK_NE(-1, child);
  if (child == 0) {
    lss__exit((*callback)(arg), &errno);
  }

  int rc, status;
  while ((rc = lss_waitpid(child, &status, __WALL, &errno)) < 0 &&
         errno == EINTR) {
    /* Keep waiting */
  }
  CHECK_LE(0, rc);
  CHECK(WIFEXITED(status));
  CHECK_EQ(0, WEXITSTATUS(status));
}

TEST(LinuxSyscallSupport, Fork) {
#if defined(THREAD_SANITIZER)
  LOG(WARNING) << "Fork test disabled on tsan";
  return;
#endif

  run_callback_with_fork(no_sse_callback, &no_sse_callback_arg);
#ifdef __SSE2__
  run_callback_with_fork(sse_callback, nullptr);
#endif
}

// Test for setgroups.  This test only runs when run as root.
// First get the groups for the process.  Find the gid for the operator
// group.  Do the test by adding operator to the groups and check it.
TEST(LinuxSyscallSupport, SetGroups) {
#if defined(__ANDROID__) && __ANDROID_API__ < __ANDROID_API_N__
  LOG(WARNING) << "Android getgrnam_r is only supported after API 24. "
                  "Test was compiled with API "
               << __ANDROID_API__;
#else
  puts("SetGroups...");
  uid_t uid = getuid();
  if (uid == 0) {
    gid_t group_ids[26];
    int count;
    EXPECT_GE((count = getgroups(25, group_ids)), 0);
    char buf[256];
    struct group grp, *operator_group = nullptr;
    EXPECT_EQ(0,
              getgrnam_r("operator", &grp, buf, sizeof(buf), &operator_group));
    EXPECT_NE(operator_group, nullptr);
    group_ids[count] = operator_group->gr_gid;
    EXPECT_EQ(0, lss_setgroups(count + 1, group_ids, &errno));
    gid_t new_group_ids[25];
    int new_count;
    EXPECT_GT((new_count = getgroups(25, new_group_ids)), 0);
    EXPECT_EQ(new_count, count + 1);
    bool found = false;
    for (int j = 0; j < new_count; ++j) {
      if (new_group_ids[j] == operator_group->gr_gid) {
        found = true;
        break;
      }
    }
    EXPECT_TRUE(found);
    EXPECT_EQ(0, lss_setgroups(count, group_ids, &errno));
  } else {
    puts("Not testing setgroups: needs test run as root");
    return;
  }
#endif
}

// Partial test runs as an unprivileged user. The full test only runs
// when invoked as root.  Something like the following works:
//
//   cp bazel-bin/base/linux_syscall_support_unittest .
//   sudo ./linux_syscall_support_unittest --uid=root
//
// The test always gets the capabilities for the process.  If root, it
// turns on and off some capabilities via cap_set_proc and verifies
// they were set as expected. In all PASSing cases, it restores the
// initial settings for the capabilities in the current process with
// another cap_set_proc call.
//
// Note: this test turns off CAP_SYS_TIME in the EFFECTIVE set and
// turns on CAP_NET_RAW in the INHERITABLE set, it then restores the
// original set - which is possible because the PERMITTED set is not
// changed.

TEST(LinuxSyscallSupport, Sendto) {
  puts("Sendto...");
  const char message[] = "42";
  const int len = sizeof(message);
  char buffer[100];
  int sockets[2], status, c;

  // MSan can't intercept direct syscall below. Tell MSan this memory
  // has been initialized.
  memset(sockets, 0, sizeof(sockets));

  ASSERT_EQ(0, lss_socketpair(AF_UNIX, SOCK_STREAM, 0, sockets, &errno));

  // Send a very short message.  We assume this is short enough that we
  // can eventually send the whole message without the receiver reading
  // it.  If this is not true, we need to use threads.
  for (c = 0; c < len;) {
    status =
        lss_sendto(sockets[0], message + c, len - c, 0, nullptr, 0, &errno);
    if (status < 0) {
      EXPECT_EQ(errno, EINTR);
    } else {
      c += status;
    }
  }
  EXPECT_EQ(c, len);

  // Receive the previous short message.  Note that we don't provide either
  // lss_recv or lss_recvfrom, so we use recv() in the C library.
  for (c = 0; c < len;) {
    status = recv(sockets[1], buffer + c, sizeof(buffer) - c, 0);
    if (status < 0) {
      EXPECT_EQ(errno, EINTR);
    } else {
      c += status;
    }
  }
  EXPECT_EQ(c, len);
  EXPECT_EQ(memcmp(message, buffer, len), 0);

  EXPECT_EQ(0, lss_close(sockets[0], &errno));
  EXPECT_EQ(0, lss_close(sockets[1], &errno));
}

// Helper for dup test.
static bool PipeTest(int read_fd, int write_fd) {
  const char kMessage[] = "hello";
  const size_t kMessageSize = sizeof(kMessage);

  const int byte_written =
      TEMP_FAILURE_RETRY(lss_write(write_fd, kMessage, kMessageSize, &errno));
  if (byte_written != kMessageSize) return false;

  char buffer[kMessageSize];
  const int byte_read =
      TEMP_FAILURE_RETRY(lss_read(read_fd, buffer, kMessageSize, &errno));
  return byte_read == kMessageSize && !memcmp(buffer, kMessage, kMessageSize);
}

TEST(LinuxSyscallSupport, Dup) {
  int pipefd[2], status;

  status = TEMP_FAILURE_RETRY(lss_dup(-42, &errno));
  EXPECT_EQ(-1, status);
  EXPECT_EQ(EBADF, errno);

  status = lss_pipe(pipefd, &errno); /* create a pipe to test dup */
  EXPECT_NE(-1, status);
  EXPECT_TRUE(PipeTest(pipefd[0], pipefd[1]));

  const int dup_read_fd = TEMP_FAILURE_RETRY(lss_dup(pipefd[0], &errno));
  EXPECT_NE(-1, dup_read_fd);
  EXPECT_TRUE(PipeTest(dup_read_fd, pipefd[1]));

  EXPECT_EQ(0, lss_close(dup_read_fd, &errno));
  EXPECT_EQ(0, lss_close(pipefd[0], &errno));
  EXPECT_EQ(0, lss_close(pipefd[1], &errno));
}

TEST(LinuxSyscallSupport, Dup2) {
  int pipefd[2], pipe2fd[2], status;

  status = lss_pipe(pipefd, &errno);
  EXPECT_NE(-1, status);
  status = lss_pipe(pipe2fd, &errno);
  EXPECT_NE(-1, status);

  // Check lss_dup2 with invalid file descriptor. */
  status = TEMP_FAILURE_RETRY(lss_dup2(pipefd[0], -42, &errno));
  EXPECT_EQ(-1, status);
  EXPECT_EQ(EBADF, errno);

  status = TEMP_FAILURE_RETRY(lss_dup2(-1, pipefd[0], &errno));
  EXPECT_EQ(-1, status);
  EXPECT_EQ(EBADF, errno);
  EXPECT_TRUE(
      PipeTest(pipefd[0], pipefd[1])); /* pipefd[0] should not be closed */

  // Check that lss_dup2(x, x) is an NOP */
  status = TEMP_FAILURE_RETRY(lss_dup2(pipefd[0], pipefd[0], &errno));
  EXPECT_EQ(pipefd[0], status);
  EXPECT_TRUE(PipeTest(pipefd[0], pipefd[1])); /* should still be open */

  status = TEMP_FAILURE_RETRY(lss_dup2(pipefd[0], pipe2fd[0], &errno));
  EXPECT_EQ(pipe2fd[0], status);
  EXPECT_TRUE(PipeTest(pipe2fd[0], pipefd[1]));

  EXPECT_EQ(0, lss_close(pipefd[0], &errno));
  EXPECT_EQ(0, lss_close(pipefd[1], &errno));
  EXPECT_EQ(0, lss_close(pipe2fd[0], &errno));
  EXPECT_EQ(0, lss_close(pipe2fd[1], &errno));
}

TEST(LinuxSyscallSupport, Dup3) {
  int pipefd[2], pipe2fd[2], status;

  status = lss_pipe(pipefd, &errno);
  EXPECT_NE(-1, status);
  status = lss_pipe(pipe2fd, &errno);
  EXPECT_NE(-1, status);

  // Check lss_dup2 with invalid file descriptor. */
  status = TEMP_FAILURE_RETRY(lss_dup3(pipefd[0], -42, 0, &errno));
  EXPECT_EQ(-1, status);
  EXPECT_EQ(EBADF, errno);

  status = TEMP_FAILURE_RETRY(lss_dup3(-1, pipefd[0], 0, &errno));
  EXPECT_EQ(-1, status);
  EXPECT_EQ(EBADF, errno);
  EXPECT_TRUE(
      PipeTest(pipefd[0], pipefd[1])); /* pipefd[0] should not be closed */

  status = TEMP_FAILURE_RETRY(lss_dup3(pipefd[0], pipefd[0], 0, &errno));
  EXPECT_EQ(-1, status);
  EXPECT_EQ(EINVAL, errno);
  EXPECT_TRUE(
      PipeTest(pipefd[0], pipefd[1])); /* pipefd[0] should not be closed */

  status = TEMP_FAILURE_RETRY(lss_dup3(pipefd[0], pipe2fd[0], 0, &errno));
  EXPECT_EQ(pipe2fd[0], status);
  EXPECT_TRUE(PipeTest(pipe2fd[0], pipefd[1]));
  status = TEMP_FAILURE_RETRY(lss_fcntl(pipe2fd[0], F_GETFD, 0, &errno));
  EXPECT_EQ(0, status);

  // Check that setting close-on-exec flag works.
  status =
      TEMP_FAILURE_RETRY(lss_dup3(pipefd[1], pipe2fd[1], O_CLOEXEC, &errno));
  EXPECT_EQ(pipe2fd[1], status);
  status = TEMP_FAILURE_RETRY(lss_fcntl(pipe2fd[1], F_GETFD, 0, &errno));
  EXPECT_EQ(1, status);

  EXPECT_EQ(0, lss_close(pipefd[0], &errno));
  EXPECT_EQ(0, lss_close(pipefd[1], &errno));
  EXPECT_EQ(0, lss_close(pipe2fd[0], &errno));
  EXPECT_EQ(0, lss_close(pipe2fd[1], &errno));
}

TEST(LinuxSyscallSupport, poll) {
  int status, pipefd[2];
  status = lss_pipe(pipefd, &errno);
  ASSERT_NE(-1, status);

  struct kernel_pollfd pollfd;
  pollfd.fd = pipefd[0];
  pollfd.events = POLLIN;

  // This should time out immediately.
  status = TEMP_FAILURE_RETRY(lss_poll(&pollfd, 1, 1, &errno));
  EXPECT_EQ(0, status);

  pid_t child = lss_fork(&errno);
  ASSERT_NE(-1, child);
  if (child != 0) {
    int answer;

    status = TEMP_FAILURE_RETRY(lss_poll(&pollfd, 1, 10000, &errno));
    EXPECT_EQ(1, status);
    EXPECT_EQ(POLLIN, pollfd.revents & POLLIN);
    status = TEMP_FAILURE_RETRY(
        lss_read(pipefd[0], &answer, sizeof(answer), &errno));
    EXPECT_EQ(42, answer);

    pid_t pid = lss_waitpid(child, &status, 0, &errno);
    EXPECT_EQ(child, pid);
    EXPECT_EQ(0, status);
  } else {
    const int test_data = 42;
    status = TEMP_FAILURE_RETRY(
        lss_write(pipefd[1], &test_data, sizeof(test_data), &errno));
    if (status != sizeof(test_data)) {
      LssExit(1, &errno);
    }
    LssExit(0, &errno);
  }

  EXPECT_EQ(0, lss_close(pipefd[0], &errno));
  EXPECT_EQ(0, lss_close(pipefd[1], &errno));
}

static volatile bool GotSigusr1 = false;
static void Sigusr1Handler(int sig) { GotSigusr1 = true; }

TEST(LinuxSyscallSupport, ppoll) {
#if defined(__ANDROID__) && defined(__i386__)
  LOG(WARNING) << "Android x86 doesn't support ppoll.";
#else
  int status, pipefd[2], pipe2fd[2];
  status = lss_pipe(pipefd, &errno);
  EXPECT_NE(-1, status);
  status = lss_pipe(pipe2fd, &errno);
  EXPECT_NE(-1, status);

  // This should time out immediately.
  struct kernel_pollfd pollfd;
  pollfd.fd = pipefd[0];
  pollfd.events = POLLIN;

  struct kernel_timespec one_ns, ten_s;  // time constants
  one_ns.tv_sec = 0;
  one_ns.tv_nsec = 1;
  ten_s.tv_sec = 10;
  ten_s.tv_nsec = 0;

  // Test time out.
  status =
      TEMP_FAILURE_RETRY(lss_ppoll(&pollfd, 1, &one_ns, nullptr, 0, &errno));
  EXPECT_EQ(0, status);

  pid_t child = fork();
  EXPECT_NE(child, -1);

  // Set up pipes so that parent and child and talk to each other.
  int read_fd, write_fd;
  bool is_parent = child != 0;
  read_fd = is_parent ? pipefd[0] : pipe2fd[0];
  write_fd = is_parent ? pipe2fd[1] : pipefd[1];
  EXPECT_EQ(0, lss_close(is_parent ? pipe2fd[0] : pipefd[0], &errno));
  EXPECT_EQ(0, lss_close(is_parent ? pipefd[1] : pipe2fd[1], &errno));

  if (is_parent) {
    // Wait for pid from child.
    pollfd.fd = read_fd;
    pollfd.events = POLLIN;
    status =
        TEMP_FAILURE_RETRY(lss_ppoll(&pollfd, 1, &ten_s, nullptr, 0, &errno));
    EXPECT_EQ(1, status);

    pid_t pid;
    status = TEMP_FAILURE_RETRY(lss_read(read_fd, &pid, sizeof(pid), &errno));
    EXPECT_EQ(sizeof(pid), status);
    EXPECT_EQ(child, pid);

    // Send SIGUSR1 to child, then write sync byte 'S'.
    lss_kill(child, SIGUSR1, &errno);
    char sync_byte = 'S';
    status = TEMP_FAILURE_RETRY(lss_write(write_fd, &sync_byte, 1, &errno));
    EXPECT_EQ(1, status);

    // Wait for child to be ready for the answer.
    char ready_byte;
    status = TEMP_FAILURE_RETRY(lss_read(read_fd, &ready_byte, 1, &errno));
    EXPECT_EQ(1, status);
    EXPECT_EQ('R', ready_byte);

    // Native Handshake: Wait for child to acknowledge receipt of SIGUSR1
    char ack_byte;
    status = TEMP_FAILURE_RETRY(lss_read(read_fd, &ack_byte, 1, &errno));
    EXPECT_EQ(1, status);
    EXPECT_EQ('A', ack_byte);

    const char answer = 42;
    status = TEMP_FAILURE_RETRY(
        lss_write(write_fd, &answer, sizeof(answer), &errno));
    EXPECT_EQ(sizeof(answer), status);

    // Check that child passes all tests.
    pid = lss_waitpid(child, &status, 0, &errno);
    EXPECT_EQ(child, pid);
    EXPECT_EQ(0, status);

    // Now that child has exited, we should get POLLERR */
    pollfd.fd = write_fd;
    pollfd.events = 0;
    status =
        TEMP_FAILURE_RETRY(lss_ppoll(&pollfd, 1, &one_ns, nullptr, 0, &errno));
    EXPECT_EQ(1, status);
    EXPECT_EQ(POLLERR, pollfd.revents & POLLERR);

  } else {
    // Set up signal handler in child and block signal.
    struct kernel_sigaction sa;
    sa.sa_handler_ = Sigusr1Handler;
    lss_sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (lss_sigaction(SIGUSR1, &sa, nullptr, &errno) != 0) {
      LssExit(1, &errno);
    }

    kernel_sigset_t blocked;
    lss_sigemptyset(&blocked);
    lss_sigaddset(&blocked, SIGUSR1, &errno);
    if (lss_sigprocmask(SIG_BLOCK, &blocked, nullptr, &errno) != 0) {
      LssExit(1, &errno);
    }

    // Write pid to signal parent that child is ready.
    pid_t self = lss_getpid(&errno);
    status =
        TEMP_FAILURE_RETRY(lss_write(write_fd, &self, sizeof(self), &errno));
    if (status != sizeof(self)) {
      LssExit(1, &errno);
    }

    // Block on reading sync byte 'S' from parent. Consumes 0% CPU.
    char sync_byte;
    status = TEMP_FAILURE_RETRY(lss_read(read_fd, &sync_byte, 1, &errno));
    if (status != 1 || sync_byte != 'S') {
      LssExit(1, &errno);
    }

    // Signal parent that we are ready for the answer and about to ppoll.
    char ready_byte = 'R';
    status = TEMP_FAILURE_RETRY(lss_write(write_fd, &ready_byte, 1, &errno));
    if (status != 1) {
      LssExit(1, &errno);
    }

    // Unblock SIGUSR1 in ppoll and poll 10 seconds for input.
    struct kernel_sigset_t signals;
    if (lss_sigemptyset(&signals) != 0 ||
        lss_sigdelset(&signals, SIGUSR1, &errno) != 0) {
      LssExit(1, &errno);
    }
    pollfd.fd = read_fd;
    pollfd.events = POLLIN;

    // Native Handshake: Call ppoll first time, expecting EINTR signal
    // interruption.
    status = lss_ppoll(&pollfd, 1, &ten_s, &signals, sizeof(signals), &errno);
    if (status != -1 || errno != EINTR || !GotSigusr1) {
      LssExit(1, &errno);
    }
    // Write 'A' (Ack) to parent to signal we received the signal.
    char ack_byte = 'A';
    status = TEMP_FAILURE_RETRY(lss_write(write_fd, &ack_byte, 1, &errno));
    if (status != 1) {
      LssExit(1, &errno);
    }

    status = TEMP_FAILURE_RETRY(
        lss_ppoll(&pollfd, 1, &ten_s, &signals, sizeof(signals), &errno));
    if (status != 1 || (pollfd.revents & POLLIN) != POLLIN) {
      LssExit(1, &errno);
    }
    LssExit(GotSigusr1 ? 0 : 1, &errno);
  }

  EXPECT_EQ(0, lss_close(read_fd, &errno));
  EXPECT_EQ(0, lss_close(write_fd, &errno));
#endif
}

TEST(LinuxSyscallSupport, SendRecvMsg) {
  puts("SendRecvMsg...");

  int sockets[2];
  ASSERT_EQ(lss_socketpair(AF_UNIX, SOCK_STREAM, 0, sockets, &errno), 0);

  // Send a very short message.  We assume this is short enough that we
  // can eventually send the whole message without the receiver reading
  // it.  If this is not true, we need to use threads.
  char message1[] = "1234";
  char message2[] = "abcd";
  struct kernel_iovec src_iov[2];
  src_iov[0].iov_base = &message1;
  src_iov[0].iov_len = sizeof(message1);
  src_iov[1].iov_base = &message2;
  src_iov[1].iov_len = sizeof(message2);

  struct kernel_msghdr src_msg;
  src_msg.msg_name = nullptr;
  src_msg.msg_namelen = 0;
  src_msg.msg_iov = src_iov;
  src_msg.msg_iovlen = 2;
  src_msg.msg_control = nullptr;
  src_msg.msg_controllen = 0;
  src_msg.msg_flags = 0;
  const ssize_t bytes_sent = lss_sendmsg(sockets[0], &src_msg, 0, &errno);
  EXPECT_EQ(sizeof(message1) + sizeof(message2), bytes_sent);

  // Receive it as a single message.
  struct kernel_iovec dst_iov;
  char buffer[sizeof(message1) + sizeof(message2)];
  dst_iov.iov_base = buffer;
  dst_iov.iov_len = sizeof(buffer);

  struct kernel_msghdr dst_msg;

  // There is a hole in kernel_msghdr between .msg_namelen and .msg_iov.
  // Tell MSan it has been initialized.
  memset(&dst_msg, 0, sizeof(dst_msg));

  dst_msg.msg_name = nullptr;
  dst_msg.msg_namelen = 0;
  dst_msg.msg_iov = &dst_iov;
  dst_msg.msg_iovlen = 1;
  dst_msg.msg_control = nullptr;
  dst_msg.msg_controllen = 0;
  dst_msg.msg_flags = 0;
  const ssize_t bytes_received = lss_recvmsg(sockets[1], &dst_msg, 0, &errno);
  EXPECT_EQ(sizeof(message1) + sizeof(message2), bytes_received);
  EXPECT_EQ(0, memcmp(message1, buffer, sizeof(message1)));
  EXPECT_EQ(0, memcmp(message2, buffer + sizeof(message1), sizeof(message2)));

  EXPECT_EQ(0, lss_close(sockets[0], &errno));
  EXPECT_EQ(0, lss_close(sockets[1], &errno));
}

TEST(LinuxSyscallSupport, Shutdown) {
  puts("Shutdown...");
  int sockets[2];
  ASSERT_EQ(0, lss_socketpair(AF_UNIX, SOCK_STREAM, 0, sockets, &errno));
  EXPECT_EQ(0, lss_shutdown(sockets[0], SHUT_RD, &errno));
  const int value = 42;
  ssize_t bytes_sent = send(sockets[1], &value, sizeof(value), MSG_NOSIGNAL);
  EXPECT_EQ(-1, bytes_sent);
  EXPECT_EQ(0, lss_close(sockets[0], &errno));
  EXPECT_EQ(0, lss_close(sockets[1], &errno));
}

TEST(LinuxSyscallSupport, Nanosleep) {
  puts("Nanosleep...");
  const struct kernel_timespec req = {0, 10000000};  // 10ms
  struct kernel_timespec rem;

  const auto k10ms = absl::Milliseconds(10);
  const auto k1000ms = absl::Milliseconds(1000);

  const auto t0 = absl::Now();
  EXPECT_EQ(0, lss_nanosleep(&req, nullptr, &errno));

  const auto t1 = absl::Now();
  EXPECT_GE(t1 - t0, k10ms);
  EXPECT_LE(t1 - t0, k1000ms);  // Delay on Forge can be long.

  EXPECT_EQ(0, lss_nanosleep(&req, &rem, &errno));

  const auto t2 = absl::Now();
  EXPECT_GE(t2 - t1, k10ms);
  EXPECT_LE(t2 - t1, k1000ms);  // Delay on Forge can be long.
}

TEST(LinuxSyscallSupport, brk) {
  void* const kFailedBrk = reinterpret_cast<void*>(-1);
  void* cur_brk = sbrk(0);
  ASSERT_TRUE(cur_brk != kFailedBrk);
  // Use lss_brk to keep brk where it is - a no op.
  void* changed_brk;
  while (true) {
    int local_errno;
    changed_brk = lss_brk(cur_brk, &local_errno);
    if (changed_brk != kFailedBrk || local_errno != EINTR) {
      break;
    }
  }
  EXPECT_EQ(changed_brk, cur_brk);
}

TEST(LinuxSyscallSupport, itimer) {
  struct kernel_itimerval itimer;

  EXPECT_EQ(0, lss_getitimer(ITIMER_REAL, &itimer, &errno));
  EXPECT_EQ(0, itimer.it_value.tv_sec);
  EXPECT_EQ(0, itimer.it_value.tv_usec);
  EXPECT_EQ(0, itimer.it_interval.tv_sec);
  EXPECT_EQ(0, itimer.it_interval.tv_usec);

  // Setup handler for our itimier signal.
  struct kernel_sigaction sa = ZERO_SIGACT;
  sa.sa_handler_ = SigHandler;
  EXPECT_EQ(0, lss_sigemptyset(&sa.sa_mask));
  EXPECT_EQ(0, lss_sigaction(SIGALRM, &sa, nullptr, &errno));
  signaled = 0;

  itimer.it_value.tv_usec = 990000;  // 990ms
  EXPECT_EQ(0, lss_setitimer(ITIMER_REAL, &itimer, nullptr, &errno) != 0);

  struct kernel_timespec timespec = {0, 10000000};  // 10ms
  EXPECT_EQ(0, lss_nanosleep(&timespec, nullptr, &errno));

  EXPECT_EQ(0, signaled);
  EXPECT_EQ(0, lss_getitimer(ITIMER_REAL, &itimer, &errno));
  EXPECT_EQ(0, itimer.it_value.tv_sec);
  EXPECT_EQ(0, itimer.it_interval.tv_sec);
  EXPECT_EQ(0, itimer.it_interval.tv_usec);
  // About 980ms remains in the timer (we use generous bounds
  // -- delay on Forge can be long):
  EXPECT_LT(itimer.it_value.tv_usec, 981000);  // 981ms
  EXPECT_GT(itimer.it_value.tv_usec, 10000);   // 10ms

  timespec.tv_sec = 3;  // itimer will interrupt it anyways
  timespec.tv_nsec = 0;
  int local_errno;
  EXPECT_EQ(-1, lss_nanosleep(&timespec, nullptr, &local_errno));
  EXPECT_EQ(EINTR, local_errno);

  EXPECT_EQ(SIGALRM, signaled);
  EXPECT_EQ(0, lss_getitimer(ITIMER_REAL, &itimer, &errno));
  EXPECT_EQ(0, itimer.it_value.tv_sec);
  EXPECT_EQ(0, itimer.it_value.tv_usec);
  EXPECT_EQ(0, itimer.it_interval.tv_sec);
  EXPECT_EQ(0, itimer.it_interval.tv_usec);
}

static int ForkExecWait(int fd, const char* pathname, const char* const argv[],
                        const char* const envp[], int flags) {
  int local_errno;
  pid_t child = lss_fork(&local_errno);
  if (child == -1) return -1;
  if (child != 0) {
    int wstatus;
    pid_t pid =
        TEMP_FAILURE_RETRY(lss_waitpid(child, &wstatus, __WALL, &local_errno));
    if (child != pid) return -1;
    if (!(WIFEXITED(wstatus))) return -1;
    return WEXITSTATUS(wstatus);
  }

  lss_execveat(fd, pathname, argv, envp, flags, &local_errno);
  for (;;) lss__exit(42, &local_errno);
}

TEST(LinuxSyscallSupport, execveat) {
  int local_errno;
  int dirfd = lss_open("/bin/true", O_RDONLY | O_CLOEXEC, 0, &local_errno);
  ASSERT_NE(-1, dirfd);
  EXPECT_EQ(
      0, ForkExecWait(dirfd, "",
                      (const char* const[]){"/path/does/not/matter", nullptr},
                      (const char* const[]){nullptr}, AT_EMPTY_PATH));
  close(dirfd);
  dirfd = lss_open("/bin", O_RDONLY | O_CLOEXEC | O_DIRECTORY, 0, &local_errno);
  ASSERT_NE(-1, dirfd);
  EXPECT_EQ(
      0, ForkExecWait(dirfd, "true",
                      (const char* const[]){"/path/does/not/matter", nullptr},
                      (const char* const[]){nullptr}, 0));
  EXPECT_EQ(42, ForkExecWait(dirfd, "nonexistent",
                             (const char* const[]){"nonexistent", nullptr},
                             (const char* const[]){nullptr}, 0));
  close(dirfd);
}

int main(int argc, char* argv[]) {
  // This test case (specifically: Sigaction) will intermittently fail if
  // there are any threads other than the main one.
  //
  // TODO: The real fix here is to stop using gUnit, and
  // to avoid the call to InitGoogle entirely.  See
  // <link>.
  ::thread::DeprecatedThreadControl::AvoidBackgroundThreads();

  absl::SetFlag(&FLAGS_logtostderr, true);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
