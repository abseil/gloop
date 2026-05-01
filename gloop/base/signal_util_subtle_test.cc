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

#include "gloop/base/signal_util_subtle.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "benchmark/benchmark.h"
#include "gloop/base/commandlineflags.h"
#include "gloop/base/percpu.h"
#include "gloop/base/time_support.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

ABSL_FLAG(int32_t, num_ping_pongs, 50,
          "Number of safe read/write iterations to attempt");

// This is the interval at which we will deliver signals during read/write
// tests. It should be large enough that scheduling effects cannot perturb the
// test.
ABSL_FLAG(int32_t, signal_interval, 50, "Interval in ms between signals");

namespace {

using base::internal::LineReader;
using base::internal::signal_safe_close;
using base::internal::signal_safe_open;
using base::internal::signal_safe_poll;
using base::internal::signal_safe_read;
using base::internal::signal_safe_write;
using base::internal::thread_safe_getenv;

using base::internal::SleepForNanoseconds;

using base::internal::ReadVariableAlignMaskUnsigned;
using base::internal::ReadVariableSwitchUnsigned;
using base::internal::ReadVariableUnsigned;

using base::internal::ReadVariableAlignMaskSigned;
using base::internal::ReadVariableSigned;
using base::internal::ReadVariableSwitchSigned;

using base::internal::WriteVariableSigned;
using base::internal::WriteVariableUnsigned;

using base::internal::AllowedCpus;
using base::internal::ScopedAffinityMask;

// Internal variables and helpers to deal with signals

volatile int signal_iterations;
volatile int pipe_fd[2];
volatile int pipe_reads;
volatile int pipe_writes;
struct itimerval signal_interval;

const char* kPipeToken = "GOOGgoogGOOGgoogGOOGgoogGOOGgoog";
int kPipeTokensPerPage;

enum signal_modes {
  kInactive = 0,
  kPipeWrite,
  kPipeRead,
  kInterruptOnly,
};
std::atomic<signal_modes> g_signal_mode;
signal_modes get_signal_mode() {
  return g_signal_mode.load(std::memory_order_acquire);
}
void set_signal_mode(signal_modes new_mode) {
  g_signal_mode.store(new_mode, std::memory_order_release);
}

void __ProgramSignalCallBackMode(enum signal_modes new_mode, int64_t usec) {
  struct timeval timeval;

  timeval.tv_sec = usec / kNumMicrosPerSecond;
  timeval.tv_usec = usec % kNumMicrosPerSecond;
  signal_interval.it_value = timeval;
  signal_interval.it_interval = timeval;

  setitimer(ITIMER_REAL, &signal_interval, nullptr);
  set_signal_mode(new_mode);
}

// Disable the current SIG_ALRM call-back (if any)
void DisableSignalCallBack() {
  if (get_signal_mode() == kInactive) return;

  __ProgramSignalCallBackMode(kInactive, 0);
}

// Programs a SIG_ALRM to be repeated with a period of usec_interval
void SetSignalCallBackMode(enum signal_modes new_mode, int iterations,
                           int64_t usec_interval) {
  DisableSignalCallBack();
  signal_iterations = iterations;
  __ProgramSignalCallBackMode(new_mode, usec_interval);
}

bool EveryOther() {
  int i = signal_iterations;
  if (i == 0) return false;
  --signal_iterations;
  return (i % 2) == 0;
}

void sa_alrm(int sig) {
  int saved_errno = errno;
  signal_modes signal_mode = get_signal_mode();
  switch (signal_mode) {
    case kPipeWrite:  // write one token
      if (EveryOther()) {
        pipe_writes++;
        ABSL_RAW_CHECK(write(pipe_fd[1], kPipeToken, strlen(kPipeToken)) ==
                           strlen(kPipeToken),
                       "Write to pipe failed");
      }
      break;
    case kPipeRead:  // read one page of tokens
      if (EveryOther()) {
        char buf[128];  // must be larger than kPipeToken
        pipe_reads++;

        for (int i = 0; i < kPipeTokensPerPage; i++) {
          ABSL_RAW_CHECK(
              read(pipe_fd[0], buf, strlen(kPipeToken)) == strlen(kPipeToken),
              "Token read from pipe is too short");
          ABSL_RAW_CHECK(strncmp(buf, kPipeToken, strlen(kPipeToken)) == 0,
                         "Read non-matching token from pipe");
        }
      }
      break;
    case kInterruptOnly:
      break;
    case kInactive:
      // If we reach here already in a disabled state then our signal handler
      // has lost synchronization with the main loop.
      ABSL_RAW_LOG(FATAL, "invalid signal_mode state");
      break;
  }
  errno = saved_errno;
}

// Start of test implementation
//
class UtilInternalTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    struct sigaction sig;

    // Set up signal handler
    memset(&sig, 0, sizeof(sig));  // sa_flags == 0 => SA_RESTART not set
    sig.sa_handler = sa_alrm;
    CHECK_EQ(sigaction(SIGALRM, &sig, nullptr), 0);  // install signal handler
    set_signal_mode(kInactive);

    // Pipe blocking is about the page boundary so this must fit evenly
    kPipeTokensPerPage = getpagesize() / strlen(kPipeToken);
    CHECK_EQ(getpagesize() % strlen(kPipeToken), 0);
  }

  static void TearDownTestSuite() { signal(SIGALRM, SIG_DFL); }

  void SetUp() override {
    // Unblock SIGALRM.
    sigset_t unblock_signals;
    CHECK_EQ(sigemptyset(&unblock_signals), 0);
    CHECK_EQ(sigaddset(&unblock_signals, SIGALRM), 0);
    CHECK_EQ(sigprocmask(SIG_UNBLOCK, &unblock_signals, &blocked_signals_), 0);

    CHECK_EQ(pipe(const_cast<int*>(pipe_fd)), 0);
    signal_modes signal_mode = get_signal_mode();
    CHECK_EQ(signal_mode, kInactive);
  }

  void TearDown() override {
    close(pipe_fd[0]);
    close(pipe_fd[1]);

    DisableSignalCallBack();

    CHECK_EQ(sigprocmask(SIG_SETMASK, &blocked_signals_, nullptr), 0);
  }

 private:
  // Blocked signals at SetUp, SIGALRM will be removed.
  sigset_t blocked_signals_;
};

// Test that open/close work, there's not a nice way to make these block so we
// do this by brute force.  Ensuring they work consistently over many iterations
// while we're being barraged by signals.
TEST_F(UtilInternalTest, open_close) {
  int fd;
  const std::string tmp_file = ::testing::TempDir() + "/openclose.dat";

  // We need to able to pass the mode, so just use creat(2) here
  CHECK_GT(fd = creat(tmp_file.c_str(), S_IRWXU), 0);
  CHECK_EQ(close(fd), 0);

  SetSignalCallBackMode(kInterruptOnly, 10000000, kNumMicrosPerMilli);
  for (int i = 0; i < 1000000; i++) {
    ASSERT_GT(fd = signal_safe_open(tmp_file.c_str(), O_WRONLY | O_TRUNC), 0);

    ASSERT_EQ(signal_safe_close(fd), 0);
  }
}

// Test that signal_safe_poll ignores signals.
TEST_F(UtilInternalTest, poll) {
  struct pollfd pfd;
  const int timeout_ms = 2000;
  int64_t start, end;
  pfd.fd = pipe_fd[0];
  pfd.events = POLL_IN;
  pfd.revents = 0;

  SetSignalCallBackMode(kInterruptOnly, 10000000, kNumMicrosPerMilli);
  start = absl::ToUnixMillis(absl::Now());
  // Ensure we are not interrupted
  ASSERT_EQ(signal_safe_poll(&pfd, 1, timeout_ms), 0);
  end = absl::ToUnixMillis(absl::Now());
  EXPECT_GT(end - start, timeout_ms);  // Validate timeout
  CHECK_EQ(
      signal_safe_write(pipe_fd[1], kPipeToken, strlen(kPipeToken), nullptr),
      strlen(kPipeToken));
  ASSERT_EQ(signal_safe_poll(&pfd, 1, timeout_ms), 1);
}

// Test that signal_safe_read is never interrupted by performing blocking reads
// against a pipe.  The signal handler will write into the pipe every other time
// it fires, if signal_safe_was not interrupted by a signal that *did not* write
// then reads should equal writes after a chosen number of iterations.
TEST_F(UtilInternalTest, signal_safe_read) {
  char buf[128];

  pipe_reads = pipe_writes = 0;
  SetSignalCallBackMode(
      kPipeWrite, absl::GetFlag(FLAGS_num_ping_pongs) * 2,
      absl::GetFlag(FLAGS_signal_interval) * kNumMicrosPerMilli);
  while (pipe_reads < absl::GetFlag(FLAGS_num_ping_pongs)) {
    size_t bytes_read;
    ssize_t rc;
    buf[0] = '\0';  // signal_safe_read will over-write this
    rc = signal_safe_read(pipe_fd[0], buf, strlen(kPipeToken), &bytes_read);
    ASSERT_EQ(rc, strlen(kPipeToken));
    ASSERT_EQ(rc, bytes_read);
    ASSERT_EQ(strncmp(kPipeToken, buf, strlen(kPipeToken)), 0);
    pipe_reads++;
  }
  ASSERT_EQ(pipe_reads, pipe_writes);
  DisableSignalCallBack();
}

// Test that signal_safe_read is never interrupted by performing blocking write
// against a pipe.  This is very similar to the read test above except since the
// blocking boundary is a page we have to read/write a full page of tokens in
// each iteration.
TEST_F(UtilInternalTest, signal_safe_write) {
  int fd_flags, rc;

  signal_modes signal_mode = get_signal_mode();
  CHECK_EQ(signal_mode, kInactive);

  // first fill up the pipe
  fd_flags = fcntl(pipe_fd[1], F_GETFL);
  CHECK_EQ(fcntl(pipe_fd[1], F_SETFL, fd_flags | O_NONBLOCK), 0);
  do {
    rc = write(pipe_fd[1], kPipeToken, strlen(kPipeToken));
  } while ((rc > 0) || (rc == -1 && errno == EINTR));
  CHECK(errno == EAGAIN || errno == EINVAL);  // POSIX allows for either.
  CHECK_EQ(fcntl(pipe_fd[1], F_SETFL, fd_flags & ~O_NONBLOCK), 0);

  pipe_reads = pipe_writes = 0;
  SetSignalCallBackMode(
      kPipeRead, absl::GetFlag(FLAGS_num_ping_pongs) * 2,
      absl::GetFlag(FLAGS_signal_interval) * kNumMicrosPerMilli);

  while (pipe_writes < absl::GetFlag(FLAGS_num_ping_pongs)) {
    size_t bytes_written;
    ssize_t rc;
    // write a full page so that we block again
    for (int i = 0; i < kPipeTokensPerPage; i++) {
      rc = signal_safe_write(pipe_fd[1], kPipeToken, strlen(kPipeToken),
                             &bytes_written);
      ASSERT_EQ(rc, strlen(kPipeToken));
      ASSERT_EQ(rc, bytes_written);
    }
    pipe_writes++;
  }
  ASSERT_EQ(pipe_reads, pipe_writes);
  DisableSignalCallBack();

  CHECK_EQ(fcntl(pipe_fd[1], F_SETFL, fd_flags), 0);
}

TEST_F(UtilInternalTest, thread_safe_getenv) {
  // Should never be defined at test start
  const char *result, *undefined_env_var = "UTIL_TEST_UNDEFINED_ENV_VAR";

  // Check that we handle an undefined variable and then set it
  CHECK(getenv(undefined_env_var) == nullptr);
  ASSERT_TRUE(thread_safe_getenv(undefined_env_var) == nullptr);
  CHECK_EQ(setenv(undefined_env_var, "1234567890", 0), 0);
  CHECK(getenv(undefined_env_var) != nullptr);

  // Make sure we can find the new variable
  result = thread_safe_getenv(undefined_env_var);
  ASSERT_TRUE(result != nullptr);
  // ... and that it matches what was set
  EXPECT_EQ(strcmp(result, getenv(undefined_env_var)), 0);
}

TEST_F(UtilInternalTest, SleepTest) {
  static const absl::Duration kSleepLength = absl::Seconds(1.25);
  SetSignalCallBackMode(kInterruptOnly, 1250, kNumMicrosPerMilli);
  absl::Time before = absl::Now();
  SleepForNanoseconds(kSleepLength / absl::Nanoseconds(1));
  absl::Time after = absl::Now();
  absl::Duration slept = after - before;
  EXPECT_LE(kSleepLength, slept);
  EXPECT_GE(kSleepLength * 3 / 2, slept);
}

ABSL_ATTRIBUTE_NOINLINE uint64_t BaseHelper(benchmark::State& state, void* ptr,
                                            size_t w) {
  uint64_t sum = 0;
  for (auto s : state) {
    __asm__ volatile("" : : : "memory");
    sum += ReadVariableUnsigned(ptr, w);
  }
  return sum;
}
static void BM_ReadVariable(benchmark::State& state) {
  const int w = state.range(0);
  const int align = state.range(1);

  CHECK_EQ(0, align % w);
  uint64_t foo = state.max_iterations + w;
  char* base = reinterpret_cast<char*>(&foo);
  base += align;
  void* ptr = reinterpret_cast<void*>(base);
  uint64_t sum = BaseHelper(state, ptr, w);
  CHECK_NE(1, sum);
}

BENCHMARK(BM_ReadVariable)
    ->ArgPair(1, 0)
    ->ArgPair(1, 1)
    ->ArgPair(1, 2)
    ->ArgPair(1, 3)
    ->ArgPair(1, 4)
    ->ArgPair(1, 5)
    ->ArgPair(1, 6)
    ->ArgPair(1, 7)
    ->ArgPair(2, 0)
    ->ArgPair(2, 2)
    ->ArgPair(2, 4)
    ->ArgPair(2, 6)
    ->ArgPair(4, 0)
    ->ArgPair(4, 4)
    ->ArgPair(8, 0);

ABSL_ATTRIBUTE_NOINLINE uint64_t SwitchHelper(benchmark::State& state,
                                              void* ptr, size_t w) {
  uint64_t sum = 0;
  for (auto s : state) {
    __asm__ volatile("" : : : "memory");
    sum += ReadVariableSwitchUnsigned(ptr, w);
  }
  return sum;
}
void BM_ReadVariableSwitch(benchmark::State& state) {
  const int w = state.range(0);
  const int align = state.range(1);

  CHECK_EQ(0, align % w);
  uint64_t foo = state.max_iterations + w;
  char* base = reinterpret_cast<char*>(&foo);
  base += align;
  void* ptr = reinterpret_cast<void*>(base);
  uint64_t sum = SwitchHelper(state, ptr, w);
  CHECK_NE(1, sum);
}
BENCHMARK(BM_ReadVariableSwitch)
    ->ArgPair(1, 0)
    ->ArgPair(1, 1)
    ->ArgPair(1, 2)
    ->ArgPair(1, 3)
    ->ArgPair(1, 4)
    ->ArgPair(1, 5)
    ->ArgPair(1, 6)
    ->ArgPair(1, 7)
    ->ArgPair(2, 0)
    ->ArgPair(2, 2)
    ->ArgPair(2, 4)
    ->ArgPair(2, 6)
    ->ArgPair(4, 0)
    ->ArgPair(4, 4)
    ->ArgPair(8, 0);

ABSL_ATTRIBUTE_NOINLINE uint64_t AlignMaskHelper(benchmark::State& state,
                                                 void* ptr, size_t w) {
  uint64_t sum = 0;
  for (auto s : state) {
    __asm__ volatile("" : : : "memory");
    sum += ReadVariableAlignMaskUnsigned(ptr, w);
  }
  return sum;
}

void BM_ReadVariableAlignMask(benchmark::State& state) {
  const int w = state.range(0);
  const int align = state.range(1);

  CHECK_EQ(0, align % w);
  uint64_t foo = state.max_iterations + w;
  char* base = reinterpret_cast<char*>(&foo);
  base += align;
  void* ptr = reinterpret_cast<void*>(base);
  uint64_t sum = AlignMaskHelper(state, ptr, w);
  CHECK_NE(1, sum);
}
BENCHMARK(BM_ReadVariableAlignMask)
    ->ArgPair(1, 0)
    ->ArgPair(1, 1)
    ->ArgPair(1, 2)
    ->ArgPair(1, 3)
    ->ArgPair(1, 4)
    ->ArgPair(1, 5)
    ->ArgPair(1, 6)
    ->ArgPair(1, 7)
    ->ArgPair(2, 0)
    ->ArgPair(2, 2)
    ->ArgPair(2, 4)
    ->ArgPair(2, 6)
    ->ArgPair(4, 0)
    ->ArgPair(4, 4)
    ->ArgPair(8, 0);

ABSL_ATTRIBUTE_NOINLINE void WriteHelper(benchmark::State& state, void* ptr,
                                         size_t w) {
  int i = 0;
  for (auto s : state) {
    for (int reps = 0; reps < 1000; ++reps) {
      __asm__ volatile("" : : : "memory");
      WriteVariableUnsigned(ptr, w, i++);
    }
  }
}
static void BM_WriteVariable(benchmark::State& state) {
  const int w = state.range(0);
  const int align = state.range(1);

  CHECK_EQ(0, align % w);
  uint64_t foo = state.max_iterations + w;
  char* base = reinterpret_cast<char*>(&foo);
  base += align;
  void* ptr = reinterpret_cast<void*>(base);
  WriteHelper(state, ptr, w);
  CHECK_NE(1, foo);
}

BENCHMARK(BM_WriteVariable)
    ->ArgPair(1, 0)
    ->ArgPair(1, 1)
    ->ArgPair(1, 2)
    ->ArgPair(1, 3)
    ->ArgPair(1, 4)
    ->ArgPair(1, 5)
    ->ArgPair(1, 6)
    ->ArgPair(1, 7)
    ->ArgPair(2, 0)
    ->ArgPair(2, 2)
    ->ArgPair(2, 4)
    ->ArgPair(2, 6)
    ->ArgPair(4, 0)
    ->ArgPair(4, 4)
    ->ArgPair(8, 0);

static bool AffinityMatches(std::vector<int> expected_affinity) {
  cpu_set_t allowed_cpus;
  PCHECK(sched_getaffinity(0, sizeof(allowed_cpus), &allowed_cpus) == 0);
  for (int cpu : expected_affinity) {
    if (!CPU_ISSET(cpu, &allowed_cpus)) return false;

    CPU_CLR(cpu, &allowed_cpus);
  }

  // All cpus should now be accounted for.
  return CPU_COUNT(&allowed_cpus) == 0;
}

TEST_F(UtilInternalTest, AllowedCpus) {
  ASSERT_THAT(AllowedCpus(),
              testing::Contains(base::subtle::percpu::GetCurrentCpu()));
  ASSERT_TRUE(AffinityMatches(AllowedCpus()));
}

TEST_F(UtilInternalTest, ScopedAffinityTamper) {
  // It would be convenient to use a ScopedAffinityMask here also, however, the
  // tampering logic disables the destructor (this is intentional so as to leave
  // us with the most consistent masks).
  cpu_set_t original_cpus;
restart:
  PCHECK(sched_getaffinity(0, sizeof(original_cpus), &original_cpus) == 0);

  // We require at least 2 cpus to run this test.
  if (CPU_COUNT(&original_cpus) == 1) return;

  for (int i = 0; i < CPU_SETSIZE; i++) {
    if (CPU_ISSET(i, &original_cpus)) {
      ScopedAffinityMask mask({i});

      // Progressing past this point _requires_ a successful false return.
      if (mask.Tampered()) goto restart;

      EXPECT_FALSE(mask.Tampered());
      // Manually tampered.  Note that the only way this can fail (external
      // restriction away from "i", must in itself trigger tampering.
      sched_setaffinity(0, sizeof(original_cpus), &original_cpus);
      ASSERT_TRUE(mask.Tampered());
      break;
    }
  }
  // We already restored original_cpus above.
}

TEST_F(UtilInternalTest, ScopedAffinityMask) {
  auto original_cpus = AllowedCpus();

restart:
  std::vector<int> original_affinity = AllowedCpus(), temporary_affinity;

  for (int i = 0; i < original_affinity.size(); i++) {
    if (AllowedCpus() != original_affinity) goto restart;

    temporary_affinity.push_back(original_affinity[i]);
    ScopedAffinityMask mask(temporary_affinity);
    ASSERT_TRUE(AllowedCpus() == temporary_affinity || mask.Tampered());

    if (mask.Tampered()) {
      goto restart;
    }
  }

  EXPECT_EQ(original_affinity, AllowedCpus());
}

}  // namespace

int main(int argc, char** argv) {
  // Add SIGALRM to blocked signals so that threads inherit and we can unblock
  // on the main test thread. This avoids data races on variables shared between
  // the test and the signal handler.
  sigset_t blocked_signals;
  CHECK_EQ(sigemptyset(&blocked_signals), 0);
  CHECK_EQ(sigaddset(&blocked_signals, SIGALRM), 0);
  CHECK_EQ(sigprocmask(SIG_BLOCK, &blocked_signals, nullptr), 0);
  // Init and run tests.
  ::testing::InitGoogleTest(&argc, argv);
  if (!benchmark::GetBenchmarkFilter().empty()) {
    benchmark::RunSpecifiedBenchmarks();
    exit(0);
  }
  return RUN_ALL_TESTS();
}
