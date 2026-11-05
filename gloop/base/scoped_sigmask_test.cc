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

#include "gloop/base/scoped_sigmask.h"

#include <cstdint>
#include <thread>  // NOLINT(build/c++11)

#ifdef __linux__
#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "gtest/gtest.h"

namespace base {
namespace {

TEST(ScopedSigmaskTest, DefaultConstructsAndDestructs) { ScopedSigmask mask; }

#ifdef __linux__

bool IsBlocked(int sig) {
  sigset_t current;
  pthread_sigmask(SIG_BLOCK, nullptr, &current);
  return sigismember(&current, sig) == 1;
}

sigset_t SignalSet(int sig) {
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, sig);
  return set;
}

TEST(ScopedSigmaskTest, DefaultRestoresMask) {
  std::thread([] {
    sigset_t usr1 = SignalSet(SIGUSR1);
    ASSERT_EQ(pthread_sigmask(SIG_SETMASK, &usr1, nullptr), 0);
    EXPECT_TRUE(IsBlocked(SIGUSR1));
    {
      ScopedSigmask mask;
      EXPECT_TRUE(IsBlocked(SIGUSR1));
      EXPECT_TRUE(IsBlocked(SIGUSR2));
    }
    EXPECT_TRUE(IsBlocked(SIGUSR1));
    EXPECT_FALSE(IsBlocked(SIGUSR2));
  }).join();
}

TEST(ScopedSigmaskTest, SetmaskRestoresMask) {
  std::thread([] {
    sigset_t usr1 = SignalSet(SIGUSR1);
    ASSERT_EQ(pthread_sigmask(SIG_SETMASK, &usr1, nullptr), 0);
    EXPECT_TRUE(IsBlocked(SIGUSR1));
    sigset_t usr2 = SignalSet(SIGUSR2);
    {
      ScopedSigmask mask(&usr2);
      EXPECT_FALSE(IsBlocked(SIGUSR1));
      EXPECT_TRUE(IsBlocked(SIGUSR2));
    }
    EXPECT_TRUE(IsBlocked(SIGUSR1));
    EXPECT_FALSE(IsBlocked(SIGUSR2));
  }).join();
}

#if defined(__GLIBC__) && defined(SYS_rt_sigprocmask)
uint64_t KernelSigmask() {
  uint64_t mask = 0;  // MSan does not see the syscall's write.
  syscall(SYS_rt_sigprocmask, SIG_BLOCK, nullptr, &mask, sizeof(mask));
  return mask;
}

// Returns `sig`'s bit in the kernel's signal mask.
uint64_t SigmaskBit(int sig) { return uint64_t{1} << (sig - 1); }

// glibc's SIGEV_THREAD timer helper thread blocks SIGTIMER with a raw syscall;
// pthread_sigmask(SIG_SETMASK) cannot restore it.
TEST(ScopedSigmaskTest, PreservesSigtimer) {
  std::thread([] {
    // glibc uses the first real-time signal as SIGTIMER (and SIGCANCEL).
    const uint64_t sigtimer = SigmaskBit(__SIGRTMIN);
    ASSERT_EQ(syscall(SYS_rt_sigprocmask, SIG_BLOCK, &sigtimer, nullptr,
                      sizeof(sigtimer)),
              0);
    const uint64_t before = KernelSigmask();
    {
      ScopedSigmask mask;
      EXPECT_NE(KernelSigmask() & SigmaskBit(SIGUSR1), 0u);
    }
    EXPECT_EQ(KernelSigmask(), before);

    sigset_t empty;
    sigemptyset(&empty);
    {
      ScopedSigmask mask(&empty);
      EXPECT_NE(KernelSigmask() & sigtimer, 0u);
    }
    EXPECT_EQ(KernelSigmask(), before);
  }).join();
}
#endif  // __GLIBC__ && SYS_rt_sigprocmask

#endif  // __linux__

}  // namespace
}  // namespace base
