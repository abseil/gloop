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

#include "gloop/util/process/subprocess.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/macros.h"
#include "absl/cleanup/cleanup.h"
#include "absl/flags/flag.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/file_toc.h"
#include "gloop/base/linux_syscall_support.h"
#include "gloop/base/logging_extensions.h"
#include "gloop/base/strerror.h"
#include "gloop/base/sysinfo.h"
#include "gloop/thread/executor.h"
#include "gloop/thread/fiber/bundle.h"
#include "gloop/thread/fiber/fiber-options.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/fiber/fifolifo-schedulers.h"
#include "gloop/thread/fiber/init-domain.h"
#include "gloop/thread/fiber/select.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"
#include "gloop/util/functional/from_callback.h"
#include "gloop/util/functional/to_callback.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

ABSL_FLAG(bool, test_overcommit_avoidance, false,
          "Attempt to check whether "
          "SubProcess can successfully avoid overcomitting memory when "
          "starting a new process. This is dangerous and could make your "
          "machine unusable. Do not run on the automated test cluster.");

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::AnyOf;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

// Linux write, with retry on EINTR.
inline ssize_t Write(int fd, const void* buffer, size_t count) {
  return TEMP_FAILURE_RETRY(write(fd, buffer, count));
}

// Helper method to generate command lines for ParentDeathSignal tests.
static std::string GetParentDeathSignalHelperCommand(
    int signum, absl::string_view pid_file, bool trigger_race_condition) {
  return absl::StrFormat(
      "%s/_main/gloop/util/process/"
      "subprocess_unittest_helper "
      "--parent_death_signal=%d --pid_file=%s --trigger_race_condition=%d",
      ::testing::SrcDir(), signum, pid_file, trigger_race_condition);
}

class SubProcessTest : public ::testing::Test {
 protected:
  void SetUp() override {
    start_time_ = absl::Now();
    finish_time_ = absl::UnixEpoch();
    outcount_ = -1;
    *outdata_ = '\0';
    last_status_ = 0;
  }

  // This is just reads whatever data is available on stdout and stores
  // it in "outdata_".
  void ExitHandler(SubProcess* sp) {
    LOG(INFO) << "Exit Handler!";
    int outfd = sp->GetFD(CHAN_STDOUT);
    if (outfd >= 0) {
      outcount_ = read(outfd, outdata_, sizeof(outdata_) - 1);
      CHECK_GE(outcount_, 0);
      outdata_[outcount_] = '\0';
    } else {
      outcount_ = 0;
      *outdata_ = '\0';
    }
  }

  std::function<void(SubProcess*)> ExitHandlerCallback() {
    return absl::bind_front(&SubProcessTest::ExitHandler, this);
  }

  void ChangeHandler(SubProcess* sp, int status) {
    LOG(INFO) << "Change Handler, status = " << status;
    last_status_ = status;
  }

  ::util::functional::CallbackFunctor<SubProcess*, int>
  ChangeHandlerCallback() {
    return absl::bind_front(&SubProcessTest::ChangeHandler, this);
  }

  void CheckRusage(const char* tag, SubProcess* sp) {
    struct rusage usage;
    sp->GetResourceUsage(&usage);
    double utime = usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1000000.0;
    double stime = usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1000000.0;
    char buf[150];
    snprintf(buf, sizeof(buf),
             "%s: maxrss=%ld utime=%f stime=%f minflt=%lu majflt=%lu", tag,
             usage.ru_maxrss, utime, stime, usage.ru_minflt, usage.ru_majflt);
    LOG(INFO) << buf;

    CHECK_LE(start_time_, sp->start_time());
    CHECK_LE(sp->start_time(), sp->finish_time());
    CHECK_LE(sp->finish_time(), finish_time_);
    CHECK_GT(usage.ru_maxrss, 0);
    CHECK_GE(utime, 0.0);
    CHECK_GE(stime, 0.0);
    CHECK_GE(usage.ru_minflt, 0);
    CHECK_GE(usage.ru_majflt, 0);
  }

  char outdata_[5000];
  int outcount_ = -1;
  int last_status_ = 0;
  absl::Time start_time_;
  absl::Time finish_time_;
};

TEST_F(SubProcessTest, OverCommit) {
  if (!absl::GetFlag(FLAGS_test_overcommit_avoidance)) {
    return;
  }

  LOG(INFO) << "Trying to hog all available memory";
  std::vector<void*> mappings;
  size_t chunk_size = 100 << 20;
  for (;;) {
    void* addr = malloc(chunk_size);
    if (addr == nullptr) {
      break;
    }
    mappings.push_back(addr);
  }
  if (!mappings.empty()) {
    free(mappings.back());
    mappings.pop_back();
  }
  LOG(INFO) << "Allocated " << (mappings.size() * (chunk_size >> 20)) << "MB";

  pid_t pids[100];
  int nproc = 0;
  for (nproc = 0; nproc < sizeof(pids) / sizeof(pid_t);) {
    pids[nproc] = sys_fork();
    if (pids[nproc] < 0) {
      if (errno == ENOMEM) {
        LOG(INFO) << "Managed to exhaust all memory available to fork()";
      } else {
        PLOG(INFO) << "fork() failed with an unexpected error: ";
      }
      break;
    } else if (pids[nproc] > 0) {
      VLOG(2) << "Created dummy process pid=" << pids[nproc];
      nproc++;
    } else {
      poll(nullptr, 0, 30 * 1000);
    }
  }
  LOG(INFO) << "Forked " << nproc << " dummy child processes";

  ASSERT_NE(nproc, sizeof(pids) / sizeof(pid_t))
      << "Cannot test overcomitting; fork() still works"
      << "Try setting /proc/sys/vm/overcommit_memory to 2"
      << "and rerun this test";
  SubProcess sp;
  sp.SetChannelAction(CHAN_STDIN, ACTION_CLOSE);
  sp.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  sp.SetChannelAction(CHAN_STDERR, ACTION_CLOSE);
  sp.SetCommand({"echo", "-n", "Google"});
  sp.SetExitCallback(ExitHandlerCallback());
  sp.Start();
  CHECK(sp.Wait());

  LOG(INFO) << "Calling DoWait to get count";
  int count = SubProcess::DoWait();

  EXPECT_EQ(count, 0) << "Not all processes died - " << count
                      << " still running";
  ASSERT_GE(outcount_, 0) << "Exit callback didn't happen.";
  EXPECT_EQ(std::string(outdata_), "Google")
      << "Process did not write expected output: '" << outdata_ << "'";

  while (nproc > 0) {
    int status;
    VLOG(2) << "Killing pid=" << pids[nproc - 1];
    CHECK(!kill(pids[--nproc], SIGKILL));
    CHECK_EQ(waitpid(pids[nproc], &status, 0), pids[nproc]);
    CHECK(WIFSIGNALED(status));
    CHECK_EQ(WTERMSIG(status), SIGKILL);
  }
  LOG(INFO) << "Killed all dummy child processes";

  while (!mappings.empty()) {
    free(mappings.back());
    mappings.pop_back();
  }
  LOG(INFO) << "Released all memory";
}

TEST_F(SubProcessTest, MissingDir) {
  SubProcess sp;
  sp.SetChannelAction(CHAN_STDIN, ACTION_DUPPARENT);
  sp.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  sp.SetChannelAction(CHAN_STDERR, ACTION_CLOSE);
  sp.SetShellCommand(":");
  sp.SetDirectory("/No Such Directory!");
  sp.Start();
  CHECK(sp.Wait());
  CHECK(sp.exit_status());
}

TEST_F(SubProcessTest, NoFileHandles) {
  SubProcess sp;
  sp.SetChannelAction(CHAN_STDIN, ACTION_CLOSE);
  sp.SetChannelAction(CHAN_STDOUT, ACTION_CLOSE);
  sp.SetChannelAction(CHAN_STDERR, ACTION_CLOSE);
  sp.SetCommand({"cat"});
  sp.SetExitCallback(ExitHandlerCallback());
  sp.Start();
  CHECK(sp.Wait());
  finish_time_ = absl::Now();

  LOG(INFO) << "Calling DoWait to get count";
  int count = SubProcess::DoWait();

  EXPECT_EQ(count, 0) << "Not all processes died - " << count
                      << " still running";
  EXPECT_GE(outcount_, 0) << "Exit callback didn't happen.";
}

TEST_F(SubProcessTest, Pipe) {
  SubProcess sp1, sp2;
  sp1.SetChannelAction(CHAN_STDIN, ACTION_PIPE);
  sp1.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  sp1.SetChannelFD(CHAN_STDERR, dup(2));  //  View stderr.
  sp1.SetCommand({"cat"});
  sp1.Start();
  int infd = sp1.GetFD(CHAN_STDIN);
  const char* indata = "two words\n";
  PCHECK(Write(infd, indata, strlen(indata)) != -1);

  //  Connect the output of the first to the input of the second.
  sp2.SetChannelFD(CHAN_STDIN, &sp1, CHAN_STDOUT);
  sp2.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  sp2.SetChannelFD(CHAN_STDERR, dup(2));  //  View stderr.
  sp2.SetExitCallback(ExitHandlerCallback());
  const char* args[] = {"wc", nullptr};
  sp2.SetProgram("/usr/bin/wc", args);
  sp2.Start();

  //  Closing this will make both processes exit.
  sp1.Close(CHAN_STDIN);
  for (int i = 0; i < 5; i++) {
    if (SubProcess::DoWait() <= 0) break;
    absl::SleepFor(absl::Seconds(1));
  }

  LOG(INFO) << "Calling DoWait to get count";
  int count = SubProcess::DoWait();

  finish_time_ = absl::Now();

  EXPECT_EQ(count, 0) << "Not all processes died - " << count
                      << " still running";
  EXPECT_GE(outcount_, 0) << "Exit callback didn't happen.";
  EXPECT_EQ(std::string(outdata_), "      1       2      10\n")
      << "Process output doesn't match.";

  CheckRusage("cat", &sp1);
  CheckRusage("wc", &sp2);

  CHECK_LE(sp1.start_time(), sp2.start_time());
  //  This check isn't reliable, because it depends on the order in which
  //  they are reaped by DoWait(), which is non-deterministic.
  //  CHECK_LE(sp1.finish_time(), sp2.finish_time());
}

TEST_F(SubProcessTest, DupParent) {
  // Testing that ACTION_DUPPARENT works for file descriptors > 2.
  // The setup itself is silly and not how you should do it with
  // SubProcess.

  int pipefd[2];
  PCHECK(pipe(pipefd) != -1);
  PCHECK(fcntl(pipefd[1], F_SETFD, FD_CLOEXEC) != -1);

  SubProcess sp(pipefd[0] + 1);
  sp.SetChannelAction(CHAN_STDIN, ACTION_CLOSE);
  sp.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  sp.SetChannelAction(CHAN_STDERR, ACTION_DUPPARENT);  //  View stderr.
  sp.SetChannelAction(static_cast<Channel>(pipefd[0]), ACTION_DUPPARENT);

  sp.SetShellCommand(absl::StrFormat("cat <&%d", pipefd[0]));
  sp.SetExitCallback(ExitHandlerCallback());
  sp.Start();

  int infd = pipefd[1];
  const char* indata = "two words\n";
  PCHECK(Write(infd, indata, strlen(indata)) != -1);
  PCHECK(close(infd) != -1);
  PCHECK(close(pipefd[0]) != -1);

  for (int i = 0; i < 5; i++) {
    if (SubProcess::DoWait() <= 0) break;
    absl::SleepFor(absl::Seconds(1));
  }

  LOG(INFO) << "Calling DoWait to get count";
  int count = SubProcess::DoWait();

  EXPECT_EQ(0, count) << "Not all processes died - " << count
                      << " still running";
  EXPECT_GE(outcount_, 0) << "Exit callback didn't happen.";
  EXPECT_STREQ(indata, outdata_) << "Process output doesn't match.";
}

TEST_F(SubProcessTest, DupParentManyTimes) {
  // Testing that ACTION_DUPPARENT works for a lot of fds.
  const int kNumPipes = 100;
  int pipefd[kNumPipes][2];
  int max_pipe_fd = 0;
  for (int i = 0; i < kNumPipes; ++i) {
    PCHECK(pipe(pipefd[i]) != -1);
    PCHECK(fcntl(pipefd[i][1], F_SETFD, FD_CLOEXEC) != -1);
    if (pipefd[i][0] > max_pipe_fd) {
      max_pipe_fd = pipefd[i][0];
    }
  }

  SubProcess sp(max_pipe_fd + 1);

  sp.SetChannelAction(CHAN_STDIN, ACTION_CLOSE);
  sp.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  sp.SetChannelAction(CHAN_STDERR, ACTION_DUPPARENT);  //  View stderr.
  for (int i = 0; i < kNumPipes; ++i) {
    sp.SetChannelAction(static_cast<Channel>(pipefd[i][0]), ACTION_DUPPARENT);
  }

  for (int i = 0; i < kNumPipes; ++i) {
    const std::string shell_command = absl::StrCat("cat <&", pipefd[i][0]);
    sp.SetShellCommand(shell_command);
    sp.SetExitCallback(ExitHandlerCallback());
    sp.Start();

    const int infd = pipefd[i][1];
    const std::string indata = absl::StrCat(i);
    PCHECK(Write(infd, indata.c_str(), indata.size()) != -1);
    PCHECK(close(infd) != -1);
    for (int j = 0; j < 5; ++j) {
      if (SubProcess::DoWait() <= 0) break;
      absl::SleepFor(absl::Seconds(1));
    }

    LOG(INFO) << "Iteration " << i << ": Calling DoWait to get count";
    int count = SubProcess::DoWait();

    EXPECT_EQ(0, count) << "Not all processes died - " << count
                        << " still running";
    EXPECT_GE(outcount_, 0) << "Exit callback didn't happen.";

    EXPECT_EQ(indata, outdata_) << "Process output doesn't match.";
  }

  for (int i = 0; i < kNumPipes; ++i) {
    PCHECK(close(pipefd[i][0]) != -1);
  }
}

//  This is the same test as for TestPipe, except that we call
//  ::Wait() to wait for the processes to finish.
//  Sorry for the repetition.
TEST_F(SubProcessTest, Wait) {
  SubProcess sp1, sp2;
  sp1.SetChannelAction(CHAN_STDIN, ACTION_PIPE);
  sp1.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  sp1.SetChannelFD(CHAN_STDERR, dup(2));  //  View stderr.
  sp1.SetCommand({"cat"});
  sp1.Start();
  int infd = sp1.GetFD(CHAN_STDIN);
  const char* indata = "two words\n";
  PCHECK(Write(infd, indata, strlen(indata)) != -1);

  //  Connect the output of the first to the input of the second.
  sp2.SetChannelFD(CHAN_STDIN, &sp1, CHAN_STDOUT);
  sp2.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  sp2.SetChannelFD(CHAN_STDERR, dup(2));  //  View stderr.
  sp2.SetExitCallback(ExitHandlerCallback());
  const char* args[] = {"wc", nullptr};
  sp2.SetProgram("/usr/bin/wc", args);
  sp2.Start();

  //  Closing this will make both processes exit.
  sp1.Close(CHAN_STDIN);
  CHECK(sp1.Wait());
  CHECK(sp2.Wait());
  finish_time_ = absl::Now();

  LOG(INFO) << "Calling DoWait to get count";
  int count = SubProcess::DoWait();

  EXPECT_EQ(count, 0) << "Not all processes died - " << count
                      << " still running";
  EXPECT_GE(outcount_, 0) << "Exit callback didn't happen.";
  EXPECT_EQ(std::string(outdata_), "      1       2      10\n")
      << "Process output doesn't match.";

  CheckRusage("cat", &sp1);
  CheckRusage("wc", &sp2);

  CHECK_LE(sp1.start_time(), sp2.start_time());
  CHECK_LE(sp1.start_time(), sp1.finish_time());
  CHECK_LE(sp2.start_time(), sp2.finish_time());
  // We cannot make any assertion about the relative order of finish times.
  // The processes may end out of order, and we may notice out of order
  // because any thread may call DoWait() at any time.
}

TEST_F(SubProcessTest, WaitDoesntDeadlockFibers) {
  SubProcess sp;
  sp.SetChannelAction(CHAN_STDIN, ACTION_PIPE);
  sp.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  sp.SetChannelFD(CHAN_STDERR, dup(2));
  sp.SetCommand({"cat"});
  sp.Start();

  // Start two fibers. The first waits on sp and the second closes STDIN,
  // which causes sp to exit. The scheduler is FIFO to ensure the wait fiber
  // executes first, and has one slot to ensure the fibers execute serially.
  // If wait is not properly annotated as a blocking operation (with SPBR/FPBR),
  // the first fiber will block the second fiber from making progress and the
  // test will deadlock.
  auto sched = thread::NewChildFIFOScheduler(
      thread::DefaultDomain()->root_scheduler(), 1);
  thread::TreeOptions opt;
  opt.set_scheduler(sched);
  auto root = thread::NewTree(opt, [&sp] {
    thread::Bundle b;
    b.Add([&sp] { sp.Wait(); });
    b.Add([&sp] { sp.Close(CHAN_STDIN); });
    b.JoinAll();
  });

  auto deadline = absl::Now() + absl::Seconds(5);
  CHECK_EQ(0, thread::SelectUntil(deadline, {root->OnJoinable()}));
  root->Join();
  sched->Orphan();
}

// -----------------------------------------------------------
// Regression test: the SubProcess destructor must wait for
// pending callbacks, rather than crashing if there is one.

// SubProcess allows any thread to call DoWait() at any time.
// It also allows the destructor to the called without Wait()
// having been called.   Therefore, SubProcess must tolerate
// the destructor being called while DoWait() is running an
// exit callback.

// This test uses synchronization and sleeps in the exit handler to adjust the
// timing of process cleanup, and another thread (the default Executor, in
// fact), to call DoWait() asynchronously so that it is extremely likely that
// the thread that created the process is destroying the SubProcess while the
// exit handler runs.  This sequence used to reliably cause a crash until the
// Await() was added to the destructor.

ABSL_CONST_INIT static absl::Mutex delay_mu(absl::kConstInit);
static bool delay_exit_start = false;
static bool delay_delete_done = false;

static void DelayExitHandler(SubProcess* sp) {
  delay_mu.lock();
  delay_exit_start = true;  // tell Start()ing thread of exit
  delay_mu.unlock();

  absl::SleepFor(absl::Seconds(1));  // give a chance for delete to occur

  delay_mu.lock();
  CHECK(!delay_delete_done);  // delete should not have happened yet
  delay_mu.unlock();
}

static void VoidDoWait() { SubProcess::DoWait(); }

static absl::StatusOr<char> ReadProcState(const char* spec, pid_t pid) {
  absl::StatusOr<base::ParsedProcessStat> parsed = ParseProcessStat(spec, pid);
  if (!parsed.ok()) {
    return parsed.status();
  }

  return parsed->GetState();
}

TEST_F(SubProcessTest, WaitDelayed) {
  delay_exit_start = false;
  delay_delete_done = false;
  SubProcess* sp = new SubProcess;
  sp->SetCommand({"true"});
  sp->SetExitCallback(&DelayExitHandler);
  sp->Start();

  while (sp->running()) {
    thread::Executor::DefaultExecutor()->Schedule(&VoidDoWait);
    absl::SleepFor(absl::Milliseconds(100));
  }

  // wait for exit handler to start
  delay_mu.LockWhen(absl::Condition(&delay_exit_start));
  delay_mu.unlock();

  // It is legal to delete the SubProcess without calling Wait().
  delete sp;  // deletion; should be delayed until exit handler finishes

  delay_mu.lock();
  delay_delete_done = true;  // mark the delete as done
  delay_mu.unlock();
}

// -----------------------------------------------------------

//  Some kernels have problems with wait, if the child process is being
//  traced at the same time. Verify that SubProcess::Wait() can work around
//  this problem.

#if 0
//  Unfortunately, it appears that when trying hard enough, it is still
//  possible to trigger the kernel bug. This causes flaky test behavior.
//  As there is really not much we can do, this test is not actually
//  providing any more data. We might as well remove it.
//  See http://b/issue?id=2749071 for a more detailed discussion

TEST_F(SubProcessTest, BuggyWait) {
  SubProcess sp;
  sp.SetChannelAction(CHAN_STDIN, ACTION_CLOSE);
  sp.SetChannelAction(CHAN_STDOUT, ACTION_CLOSE);
  sp.SetChannelAction(CHAN_STDERR, ACTION_CLOSE);
  sp.SetExitCallback(ExitHandlerCallback());
  const char * args[] = { "sleep", "3", nullptr };
  sp.SetProgram("/bin/sleep", args);
  sp.Start();

  int fd[2];
  CHECK(!pipe(fd));
  int data = sp.pid();

  pid_t pid = sys_fork();
  CHECK_GE(pid, 0) << "fork() failed";

  if (pid == 0) {
    if (sys_ptrace(PTRACE_ATTACH, data, 0, 0) < 0 ||
        sys_waitpid(pid, nullptr, __WALL) < 0) {
      data = -1;
      while (sys_write(fd[1], &data, sizeof(data)) < 0 && errno == EINTR) {}
      sys__exit(1);
    }
    while (sys_write(fd[1], &data, sizeof(data)) < 0 && errno == EINTR) {}
    sys_poll(0, 0, 3000);
    sys_ptrace_detach(data);
    sys__exit(0);
  } else {
    while (read(fd[0], &data, sizeof(data)) < 0 && errno == EINTR) {}
    CHECK(sp.CheckRunning());
    CHECK(sp.Wait());
    CHECK(!sp.CheckRunning());
    CHECK(sp.exit_normal());
    waitpid(pid, nullptr, 0);
  }
}
#endif

TEST_F(SubProcessTest, BadFile) {
  SubProcess sp1;
  sp1.SetChannelAction(CHAN_STDIN, ACTION_PIPE);
  sp1.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  sp1.SetChannelAction(CHAN_STDERR, ACTION_MAPTOSTDOUT);
  sp1.SetExitCallback(ExitHandlerCallback());
  const char* argv[] = {"/bin/atbatcat", nullptr};
  sp1.SetProgram(argv[0], argv);
  CHECK(!sp1.Start());
  CHECK_EQ(errno, ENOENT);
  CHECK_EQ(sp1.exit_status(), SubProcess::kExecFailed);
  CHECK_EQ(sp1.exit_code(), SubProcess::kExecFailed);
  const std::string err = sp1.error_text();
  EXPECT_THAT(err, HasSubstr(argv[0]));
  EXPECT_THAT(
      err, HasSubstr("exec() failed (errno = 2): No such file or directory"));

  sp1.Close(CHAN_STDIN);

  // Technically, we should not be checking exit_signum, if Start() failed,
  // but the old API always set the exit status to SIGABRT when exec()
  // fails. SubProcess maintains these semantics for backwards
  // compatibility, and we should be checking them here.
  EXPECT_EQ(sp1.exit_signum(), SIGABRT)
      << "Process didn't exit on signal SIGABRT.  status=" << sp1.exit_status();

  CHECK_EQ(sp1.running(), false);
}

// The boolean parameter determines whether SubProcess::System() is used (true)
// or system() (false)
class SubProcessSystemTest : public SubProcessTest,
                             public ::testing::WithParamInterface<bool> {
 protected:
  // dup() the fd passed in and restore the original value to that fd when the
  // class goes out of scope.  CHECKs that this has worked since failure will
  // break unrelated tests.
  class AutoRestoringDupdFd {
   public:
    explicit AutoRestoringDupdFd(int fd) : fd_(fd) {
      PCHECK((saved_fd_ = dup(fd_)) != -1);
    }

    ~AutoRestoringDupdFd() {
      CHECK_EQ(dup2(saved_fd_, fd_), fd_);
      PCHECK(close(saved_fd_) != -1);
    }

   private:
    int fd_;  // What fd we saved from and will restore to (usually 0, 1, or 2)
    int saved_fd_;
  };

  // Save the current fd, create a pipe(2) pair, set the appropriate end (reader
  // for fd=0, writer for others) on the fd, and restore everything on
  // destruction.  CHECK fails that this has worked since failure will break
  // unrelated tests.
  class AutoRestoringPipeFd {
   public:
    explicit AutoRestoringPipeFd(int fd)
        : save_fd_(fd), is_stdin_(fd == STDIN_FILENO) {
      PCHECK(pipe(pipes_) != -1);
      // Put the reader end into fd0 or the writer end into others.
      CHECK_EQ(dup2(pipes_[is_stdin_ ? 0 : 1], fd), fd);
    }

    ~AutoRestoringPipeFd() {
      // Do not check for errors, in case the test has already closed one end of
      // the pipes.
      close(pipes_[0]);
      close(pipes_[1]);
    }

    int GetParentEnd() {
      // Get the writer end for fd0, or the reader end for others.
      return pipes_[is_stdin_ ? 1 : 0];
    }

   private:
    // This will make sure that we restore fd back to what it was before.
    AutoRestoringDupdFd save_fd_;
    int pipes_[2];
    bool is_stdin_;
  };

  int CallSystem(const std::string& command) {
    if (GetParam()) {
      return SubProcess::System(command);
    }
    return system(command.c_str());
  }
};

TEST_P(SubProcessSystemTest, FilesClosed) {
  int fd, fd99;
  PCHECK((fd = open("/dev/null", O_RDONLY)) != -1);
  ASSERT_NE(fd, 99);
  PCHECK((fd99 = dup2(fd, 99)) != -1);

  ASSERT_EQ(CallSystem("[ ! -e /proc/self/fd/99 ]"), 0);

  PCHECK(close(fd) != -1);
  PCHECK(close(fd99) != -1);
}

TEST_P(SubProcessSystemTest, SystemStdinIsHookedUp) {
  std::string actual_out;
  int system_ret = 0;
  absl::Notification system_done;

  {
    // Put these in the smallest scope possible to allow e.g. EXPECT to
    // write to stdout.
    AutoRestoringPipeFd stdin_pipe(STDIN_FILENO), stdout_pipe(STDOUT_FILENO);

    // Start the system call in another thread.
    thread::Executor::DefaultExecutor()->Schedule(
        [this, &system_done, &system_ret] {
          CHECK_EQ(system_ret = CallSystem("rev"), 0);
          system_done.Notify();
        });

    const std::string test_text = "no palindromes, please\n";
    int stdin_sink_fd = stdin_pipe.GetParentEnd();
    PCHECK(write(stdin_sink_fd, test_text.data(), test_text.size()) ==
           test_text.size());
    PCHECK(close(stdin_sink_fd) != -1);

    std::vector<char> buf(test_text.size());
    while (actual_out.size() < test_text.size()) {
      int read_size = read(stdout_pipe.GetParentEnd(), buf.data(),
                           test_text.size() - actual_out.size());
      if (read_size <= 0) break;  // EOF or error
      actual_out.append(buf.data(), read_size);
    }
  }
  system_done.WaitForNotification();

  const std::string expected_out = "esaelp ,semordnilap on\n";
  EXPECT_EQ(expected_out, actual_out);
}

TEST_P(SubProcessSystemTest, SystemStdinReopensToDevNull) {
  AutoRestoringDupdFd saved_stdin(STDIN_FILENO);

  // Note: When running this on forge, it looks like stdin is already reopened
  // /dev/null.
  PCHECK(close(0) != -1);

  // Check that stdin is open and is /dev/null, even though the parent's stdin
  // is closed.  This is the default behavior for SubProcess::POpen, and we need
  // to make sure it happens for SubProcess::System() as well.
  CHECK_EQ(CallSystem("[[ -e /proc/self/fd/0 && "
                      "$(readlink /proc/self/fd/0) == /dev/null ]]"),
           0);
}

INSTANTIATE_TEST_SUITE_P(UseSubProcessSystem, SubProcessSystemTest,
                         ::testing::Values(false, true));

TEST_F(SubProcessTest, MeasureMemoryUsage) {
  SubProcess sp1;
  sp1.SetChannelAction(CHAN_STDIN, ACTION_CLOSE);
  sp1.SetChannelAction(CHAN_STDOUT, ACTION_CLOSE);
  sp1.SetChannelAction(CHAN_STDERR, ACTION_CLOSE);
  sp1.SetShellCommand("while true; do ps -ejH; done");

  SubProcess sp2;
  sp2.SetChannelAction(CHAN_STDIN, ACTION_CLOSE);
  sp2.SetChannelAction(CHAN_STDOUT, ACTION_CLOSE);
  sp2.SetChannelAction(CHAN_STDERR, ACTION_CLOSE);
  sp2.SetShellCommand("while true; do ps -ejH; done");

  // Start the children.
  ASSERT_TRUE(sp1.Start());
  ASSERT_TRUE(sp2.Start());
  ASSERT_TRUE(sp1.CheckRunning());
  ASSERT_TRUE(sp2.CheckRunning());
  // give some time to accumulate resource usage
  absl::SleepFor(absl::Seconds(2));
  int64_t rss1;
  EXPECT_EQ(2, SubProcess::ProcessListMemoryUsage(&rss1));
  EXPECT_LT(0, rss1);

  // Kill one process.
  ASSERT_TRUE(sp1.Kill(SIGKILL));
  for (int i = 0; i < 5; i++) {
    if (SubProcess::DoWait() <= 1) break;
    absl::SleepFor(absl::Seconds(1));
  }

  ASSERT_TRUE(!sp1.CheckRunning());

  // Total usage should drop since only 1 process running.
  int64_t rss2;
  EXPECT_EQ(SubProcess::ProcessListMemoryUsage(&rss2), 1);
  EXPECT_GT(rss1, rss2);

  // Kill second process.
  ASSERT_TRUE(sp2.Kill(SIGKILL));
  for (int i = 0; i < 5; i++) {
    if (SubProcess::DoWait() <= 0) break;
    absl::SleepFor(absl::Seconds(1));
  }
  ASSERT_TRUE(!sp2.CheckRunning());

  int64_t rss3;
  EXPECT_EQ(SubProcess::ProcessListMemoryUsage(&rss3), 0);
  EXPECT_EQ(rss3, 0);
}

TEST_F(SubProcessTest, ProcessGroups) {
  SubProcess sp1;
  pid_t child_pid;

  sp1.SetCommand({"sleep", "90"});

  // Start the child.
  CHECK(sp1.Start());
  child_pid = sp1.pid();
  CHECK(sp1.CheckRunning());
  CHECK_EQ(ProcessGroup(0), ProcessGroup(child_pid));
  // Try to kill a process group (should not exist).
  CHECK_LT(killpg(child_pid, SIGKILL), 0);
  for (int i = 0; i < 5; i++) {
    if (SubProcess::DoWait() <= 0) break;
    absl::SleepFor(absl::Seconds(1));
  }
  CHECK(sp1.CheckRunning());

  // Kill the process.
  CHECK(sp1.Kill(SIGKILL));
  for (int i = 0; i < 5; i++) {
    if (SubProcess::DoWait() <= 0) break;
    absl::SleepFor(absl::Seconds(1));
  }
  CHECK(!sp1.CheckRunning());

  // Now first set subprocess to get a new process group before exec.
  sp1.SetUseProcessGroup();
  CHECK(sp1.Start());
  child_pid = sp1.pid();
  CHECK(sp1.CheckRunning());
  CHECK_EQ(ProcessGroup(child_pid), child_pid);

  // Now we can kill the child's process group.
  CHECK_EQ(killpg(child_pid, SIGKILL), 0);
  for (int i = 0; i < 5; i++) {
    if (SubProcess::DoWait() <= 0) break;
    absl::SleepFor(absl::Seconds(1));
  }

  CHECK(!sp1.CheckRunning());
}

TEST_F(SubProcessTest, Sessions) {
  SubProcess sp1;
  pid_t child_pid;

  sp1.SetCommand({"sleep", "90"});

  // Start the child.
  CHECK(sp1.Start());
  child_pid = sp1.pid();
  CHECK(sp1.CheckRunning());

  CHECK_EQ(getsid(0), getsid(child_pid));

  // Kill the process.
  CHECK(sp1.Kill(SIGKILL));
  for (int i = 0; i < 5; i++) {
    if (SubProcess::DoWait() <= 0) break;
    absl::SleepFor(absl::Seconds(1));
  }
  CHECK(!sp1.CheckRunning());

  // Now first set subprocess to get a new process group before exec.
  sp1.SetUseSession();
  CHECK(sp1.Start());
  child_pid = sp1.pid();
  CHECK(sp1.CheckRunning());
  CHECK_EQ(getsid(child_pid), child_pid);

  sp1.Kill(SIGKILL);
  sp1.Wait();

  CHECK(!sp1.CheckRunning());
}

TEST_F(SubProcessTest, Priority) {
  SubProcess sp1;
  for (int priority = 0; priority < 15; priority += 3) {
    sp1.SetPriority(SubProcess::RELATIVE, priority);
    sp1.SetShellCommand(absl::StrFormat("test $(nice) = %d", priority));
    // Start the child.
    CHECK(sp1.Start());
    CHECK(sp1.Wait());
    CHECK_EQ(sp1.exit_status(), 0);
  }
  for (int priority = 0; priority < 15; priority += 3) {
    sp1.SetPriority(SubProcess::ABSOLUTE, priority);
    sp1.SetShellCommand(absl::StrFormat("test $(nice) = %d", priority));
    // Start the child.
    CHECK(sp1.Start());
    CHECK(sp1.Wait());
    CHECK_EQ(sp1.exit_status(), 0);
  }

  sp1.SetPriority(SubProcess::OFF, 8);
  sp1.SetShellCommand("test $(nice) = 0");
  // Start the child.
  CHECK(sp1.Start());
  CHECK(sp1.Wait());
  CHECK_EQ(sp1.exit_status(), 0);
}

TEST_F(SubProcessTest, RLimit) {
  SubProcess sp1;

  const int limit = 30 << 20;
  sp1.SetRLimit(RLIMIT_AS, limit, limit);
  // ulimit -v prints out KB not Bytes:
  sp1.SetShellCommand(absl::StrFormat("test $(ulimit -v) = %d", limit >> 10));
  // Start the child.
  CHECK(sp1.Start());
  CHECK(sp1.Wait());
  CHECK_EQ(sp1.exit_status(), 0);

  // test multiple rlimits:
  sp1.ClearRLimits();
  sp1.SetRLimit(RLIMIT_AS, 29 << 20, 29 << 20);
  sp1.SetRLimit(RLIMIT_AS, 20 << 20, 20 << 20);
  sp1.SetShellCommand(absl::StrFormat("test $(ulimit -v) = %d", 20 << 10));
  CHECK(sp1.Start());
  CHECK(sp1.Wait());
  CHECK_EQ(sp1.exit_status(), 0);

  // test no rlimits
  sp1.ClearRLimits();
  sp1.SetShellCommand("test $(ulimit -v) = unlimited");
  CHECK(sp1.Start());
  CHECK(sp1.Wait());
  CHECK_EQ(sp1.exit_status(), 0);

  // test broken glibc.
  sp1.ClearRLimits();
  LOG(INFO) << "infinity is " << RLIM_INFINITY;
  sp1.SetRLimit(RLIMIT_AS, RLIM_INFINITY, RLIM_INFINITY);
  // this should succeed on 2.4 kernels if glibc is working correctly.
  // NB: This shouldn't ever fail on 2.2 kernels.
  sp1.SetShellCommand("test $(ulimit -v) = unlimited");
  CHECK(sp1.Start());
  CHECK(sp1.Wait());
  CHECK_EQ(sp1.exit_status(), 0);
}

TEST_F(SubProcessTest, Umask) {
  int saved_umask = umask(022);
  umask(saved_umask);

  // Check that we can pass all possible umask values to a child.
  SubProcess sp;
  for (int mask = 0; mask <= 0777; ++mask) {
    sp.SetShellCommand(absl::StrFormat("test $(umask) = %04o", mask));
    sp.SetUmask(mask);
    CHECK(sp.Start());
    CHECK(sp.Wait());
    CHECK_EQ(sp.exit_status(), 0) << ": failed to set umask: " << mask;
    // Check that umask is unchanged in the parent.
    CHECK_EQ(saved_umask, umask(saved_umask));
  }

  SubProcess sp2;
  for (int i = 0; i < 2; ++i) {
    // Check that a fresh SubProcess starts out inheriting our umask.
    sp2.SetShellCommand(absl::StrFormat("test $(umask) = %04o", saved_umask));
    CHECK(sp2.Start());
    CHECK(sp2.Wait());
    CHECK_EQ(sp2.exit_status(), 0);
    // Check that we can set the mask, then reset it to inherit.
    sp2.SetUmask(0333);
    sp2.SetUmask(SubProcess::kInheritUmask);
  }
}

TEST_F(SubProcessTest, GetArgv) {
  // Test SetProgram with GetArgv.
  SubProcess sp1;
  CHECK(sp1.GetArgv() == nullptr);
  std::vector<std::string> input_argv;
  input_argv.push_back("/bin/cat");
  input_argv.push_back("-");
  sp1.SetProgram(input_argv[0], input_argv);
  const char* const* result_argv = sp1.GetArgv();
  CHECK_NE(result_argv, static_cast<void*>(nullptr));
  for (int i = 0; i < input_argv.size(); i++) {
    CHECK_EQ(input_argv[i], result_argv[i]);
  }

  // result_argv ends with a nullptr
  CHECK(result_argv[input_argv.size()] == nullptr);

  // Test SetCommand and SetShellCommand with GetArgv.  We make no guarantees
  // about the form of the argv we generate, only that it exists.
  sp1.SetCommand({"/usr/bin/diff", "x", "y"});
  CHECK(sp1.GetArgv() != nullptr);
  sp1.SetShellCommand("/usr/bin/diff x y");
  CHECK(sp1.GetArgv() != nullptr);
}

TEST_F(SubProcessTest, Abandoned) {
  pid_t pid1, pid2;

  // Start two child processes; mark one abandoned, and destroy both
  // SubProcess objects.
  // This test assumes that no other thread is calling DoWait(),
  // which is normally a bogus assumption.     We try to make it true in
  // this context by zapping SIGCHLD, and delaying so previous calls
  // to DoWait() have finished.
  // Stop SIGCHLD to ensure the zombies don't get cleaned up.
  signal(SIGCHLD, SIG_DFL);
  absl::SleepFor(absl::Seconds(
      2));  // wait until any previous handler invocation has finished
  {
    SubProcess sp1, sp2;
    sp1.SetCommand({"/bin/true"});
    sp2.SetCommand({"/bin/true"});
    CHECK(sp1.Start());
    pid1 = sp1.pid();
    CHECK(sp2.Start());
    pid2 = sp2.pid();
    sp1.SetAbandoned();
  }

  absl::SleepFor(absl::Seconds(1));

  // Both should be zombies now since no-one has waited for them
  EXPECT_THAT(ReadProcState("/proc/%d/stat", pid1), IsOkAndHolds('Z'));
  EXPECT_THAT(ReadProcState("/proc/%d/stat", pid2), IsOkAndHolds('Z'));

  // SubProcess::DoWait() should reap the first but not the second
  SubProcess::DoWait();
  EXPECT_THAT(ReadProcState("/proc/%d/stat", pid1),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(ReadProcState("/proc/%d/stat", pid2), IsOkAndHolds('Z'));
}

// SubProcess::SetParentDeathSignal() is only defined for Linux.
#ifdef PR_SET_PDEATHSIG
namespace {

void RemovePidFile(absl::string_view pid_file) {
  int rm_res = unlink(pid_file.data());
  PCHECK(rm_res == 0 || errno == ENOENT) << "Can't remove pid file";
}

int32_t ReadChildPid(absl::string_view pid_file) {
  static constexpr absl::Duration kSleepIncrement = absl::Milliseconds(100);
  static constexpr absl::Duration kMaxSleep = absl::Seconds(10);

  // Read the child process's PID.
  FILE* fp;
  for (absl::Duration slept = absl::ZeroDuration(); slept < kMaxSleep;
       slept += kSleepIncrement) {
    fp = fopen(pid_file.data(), "r");
    if (fp != nullptr) {
      break;
    }
    absl::SleepFor(kSleepIncrement);
  }
  CHECK(fp != nullptr);
  int32_t child_pid;
  CHECK_EQ(1, fscanf(fp, "%d", &child_pid));
  PCHECK(0 == fclose(fp));
  return child_pid;
}

}  // namespace

TEST_F(SubProcessTest, NoParentDeathSignal) {
  static const absl::Duration kWaitForDeath = absl::Seconds(1);
  std::string pid_file = absl::StrFormat("%s/parent_death_signal_helper.pid",
                                         ::testing::TempDir());
  RemovePidFile(pid_file);

  // Start a helper subprocess that will start its own child subprocess.  Don't
  // set a signal to get passed to the child when the helper is killed.
  SubProcess sp;
  sp.SetShellCommand(GetParentDeathSignalHelperCommand(0, pid_file, false));
  CHECK(sp.Start());
  pid_t helper_pid = sp.pid();

  const auto child_pid = ReadChildPid(pid_file);

  // Now kill the helper process.  The child process should still be around.
  sp.Kill(SIGTERM);
  sp.Wait();
  absl::SleepFor(kWaitForDeath);
  EXPECT_THAT(ReadProcState("/proc/%d/stat", helper_pid),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(ReadProcState("/proc/%d/stat", child_pid),
              IsOkAndHolds(AnyOf('S', 'R')));

  // Clean up.
  kill(child_pid, SIGTERM);
}
#endif

// SubProcess::SetParentDeathSignal() is only defined for Linux.
#ifdef PR_SET_PDEATHSIG
TEST_F(SubProcessTest, ParentDeathSignal) {
  static const absl::Duration kWaitForDeath = absl::Seconds(1);
  std::string pid_file = absl::StrFormat("%s/parent_death_signal_helper.pid",
                                         ::testing::TempDir());
  RemovePidFile(pid_file);

  // Now run the helper and tell it to set up the child to receive SIGTERM when
  // the helper is killed.
  SubProcess sp;
  sp.SetShellCommand(
      GetParentDeathSignalHelperCommand(SIGKILL, pid_file, false));
  CHECK(sp.Start());
  pid_t helper_pid = sp.pid();

  const auto child_pid = ReadChildPid(pid_file);

  // Both processes should be running initially.
  EXPECT_THAT(ReadProcState("/proc/%d/stat", helper_pid),
              IsOkAndHolds(AnyOf('S', 'R')));
  EXPECT_THAT(ReadProcState("/proc/%d/stat", child_pid),
              IsOkAndHolds(AnyOf('S', 'R')));

  // After we kill the helper and wait a bit, the child should also be dead.
  sp.Kill(SIGTERM);
  sp.Wait();
  absl::SleepFor(kWaitForDeath);
  EXPECT_THAT(ReadProcState("/proc/%d/stat", helper_pid),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(ReadProcState("/proc/%d/stat", child_pid),
              StatusIs(absl::StatusCode::kInternal));
}
#endif

// SubProcess::SetParentDeathSignal() is only defined for Linux.
#ifdef PR_SET_PDEATHSIG
TEST_F(SubProcessTest, ParentDeathSignalRaceCondition) {
  static const absl::Duration kWaitForDeath = absl::Seconds(1);
  std::string pid_file = absl::StrFormat("%s/parent_death_signal_helper.pid",
                                         ::testing::TempDir());
  RemovePidFile(pid_file);

  // Run the helper, telling its child to wait for the helper to die before
  // installing the parent death signal.
  SubProcess sp;
  sp.SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  sp.SetShellCommand(
      GetParentDeathSignalHelperCommand(SIGKILL, pid_file, true));
  CHECK(sp.Start());
  pid_t helper_pid = sp.pid();

  const auto child_pid = ReadChildPid(pid_file);

  // Both processes should be running initially.
  EXPECT_THAT(ReadProcState("/proc/%d/stat", helper_pid),
              IsOkAndHolds(AnyOf('S', 'R')));
  EXPECT_THAT(ReadProcState("/proc/%d/stat", child_pid),
              IsOkAndHolds(AnyOf('S', 'R')));

  // After we kill the helper and wait a bit, the child should also be dead.
  sp.Kill(SIGTERM);
  sp.Wait();
  absl::SleepFor(kWaitForDeath);
  EXPECT_THAT(ReadProcState("/proc/%d/stat", helper_pid),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(ReadProcState("/proc/%d/stat", child_pid),
              StatusIs(absl::StatusCode::kInternal));
}
#endif

#ifdef PR_SET_THP_DISABLE
// Naively tests SetDisableThp does the right thing and does
// not break.
TEST_F(SubProcessTest, DisableThp) {
  int status;
  if (!ReadProcKeyword("/proc/self/status", 0, "THP_enabled:", "%d", &status) ||
      prctl(PR_GET_THP_DISABLE, 0, 0, 0, 0)) {
    // If THP is not enabled in the kernel, or has been explicitly disabled
    // before test, skip it.
    GTEST_SKIP();
  }

  auto cmd = absl::StrCat(::testing::SrcDir(),
                          "/_main/gloop/util/process/"
                          "subprocess_unittest_helper "
                          "--test_thp_disabled");

  for (const bool thp : {true, false}) {
    SubProcess sp;
    sp.SetShellCommand(cmd);
    if (!thp) sp.SetDisableThp();
    CHECK(sp.Start());
    sp.Wait();
    EXPECT_EQ(sp.exit_code(), (int)thp);
    // Ensure we never disable thp for caller, e.g. in the case where
    // fork was not enforced, and clone has been used.
    EXPECT_EQ(0, prctl(PR_GET_THP_DISABLE, 0, 0, 0, 0));
  }
}
#endif

// ExecChild() uses execv{e,} to launch the child's binary.  We do not
// use execvp or any other function that searches the PATH for the
// reasons set out in the function header comment for ExecChild.  It's
// also possible that some user of this library depends on PATH not
// being searched for security reasons.  Anyway, this test case exists
// to make sure that the behaviour doesn't get _accidentally_ changed.
TEST_F(SubProcessTest, PathNotSearched) {
  const char* args[] = {"true", nullptr};
  SubProcess sp;
  sp.SetProgram("true", args);
  sp.Start();
  sp.Wait();
  CHECK_EQ(errno, ENOENT);
  CHECK_EQ(sp.exit_status(), SubProcess::kExecFailed);
  CHECK_EQ(sp.exit_code(), SubProcess::kExecFailed);
  EXPECT_THAT(
      sp.error_text(),
      HasSubstr("exec() failed (errno = 2): No such file or directory"));
  CHECK_EQ(sp.running(), false);

  sp.SetProgram("/bin/true", args);
  sp.Start();
  sp.Wait();
  CHECK_EQ(sp.exit_code(), 0);
  CHECK_EQ(sp.running(), false);
}

TEST_F(SubProcessTest, ExecveViaFd) {
  const char* args[] = {"true", nullptr};
  int fd = open("/bin/true", O_RDONLY);
  ASSERT_NE(fd, -1);

  SubProcess sp;
  sp.SetProgram(fd, args);
  sp.Start();
  sp.Wait();
  CHECK_EQ(sp.exit_code(), 0);
  CHECK_EQ(sp.running(), false);
  close(fd);

  // Find an un-opened file descriptor.
  fd = -1;
  for (int i = 42; fd == -1 && i < std::numeric_limits<int>::max(); ++i) {
    int rc = TEMP_FAILURE_RETRY(fcntl(i, F_GETFD));
    if (rc == -1 && errno == EBADF) fd = i;
  }
  ASSERT_GE(fd, 0);

  sp.SetProgram(fd, args);
  sp.Start();
  sp.Wait();
  CHECK_EQ(sp.exit_status(), SubProcess::kExecFailed);
  CHECK_EQ(sp.exit_code(), SubProcess::kExecFailed);
  EXPECT_THAT(sp.error_text(),
              HasSubstr("fcntl(execve_fd_, F_SETFD, FD_CLOEXEC) "
                        "failed (errno = 9): Bad file descriptor"));
  CHECK_EQ(sp.running(), false);

  ASSERT_DEATH(sp.SetProgram(-1, args), "is not a valid file descriptor");
}

TEST_F(SubProcessTest, POpen) {
  char buf[256];
  // Check the "r" version of POpen()
  FILE* fp = SubProcess::POpen("echo hi", "r");
  int line = 0;
  while (fgets(buf, sizeof(buf), fp) != nullptr) {
    CHECK_EQ(strcmp(buf, "hi\n"), 0);
    CHECK_EQ(line, 0);
    line++;
  }
  CHECK_EQ(SubProcess::PClose(fp), 0);
  CHECK_EQ(line, 1);

  // Check both the "w" version of POpen(), and the fact that
  // stdout will remain un-redirected. The latter is tested by us
  // temporarily redirecting it to a pipe, and checking that the
  // child can write to it.
  int fd[2];
  CHECK_EQ(pipe(fd), 0);
  int orig_stdout = dup(1);
  CHECK_EQ(dup2(fd[1], 1), 1);
  CHECK_EQ(close(fd[1]), 0);
  fp = SubProcess::POpen("read x; echo -n $x; exit $x", "w");
  CHECK_EQ(dup2(orig_stdout, 1), 1);
  CHECK_EQ(close(orig_stdout), 0);
  fputs("17\n", fp);
  int status = SubProcess::PClose(fp);
  CHECK_EQ(WEXITSTATUS(status), 17);
  CHECK_EQ(read(fd[0], buf, sizeof(buf)), 2);
  CHECK(!strncmp(buf, "17", 2));
  CHECK_EQ(close(fd[0]), 0);
}

TEST_F(SubProcessTest, ChangedCallback) {
  SubProcess sp;
  sp.SetShellCommand("sleep 1; true");
  sp.SetCallbackOnChange(WUNTRACED, ChangeHandlerCallback());
  sp.Start();
  CHECK_EQ(true, sp.CheckRunning());
  CHECK_EQ(0, last_status_);
  // Stop and check for status.
  kill(sp.pid(), SIGSTOP);
  // TODO: This test relies on timing so it may be flakey.
  // If you can think of a better way to test this functionality
  // please do.
  for (int i = 0; i < 10; i++) {
    CHECK_EQ(true, sp.CheckRunning());
    if (WIFSTOPPED(last_status_)) {
      break;
    }
    absl::SleepFor(absl::Milliseconds(100));
  }
  CHECK(WIFSTOPPED(last_status_));
  // Continue and wait for exit.
  kill(sp.pid(), SIGCONT);
  sp.Wait();
  CHECK_EQ(sp.exit_code(), 0);
  CHECK_EQ(sp.running(), false);
}

TEST_F(SubProcessTest, NoShellInjection) {
  SubProcess sp;
  sp.SetCommand({"echo", "; exit 10"});
  CHECK(sp.Start());
  CHECK(sp.Wait());
  CHECK_EQ(sp.exit_status(), 0);
}

TEST_F(SubProcessTest, GetEnvironInheritsInitially) {
  SubProcess::EnvMap actual;
  SubProcess p;
  p.GetEnviron(&actual);
  EXPECT_TRUE(actual.contains("TEST_TMPDIR"));
}

TEST_F(SubProcessTest, GetEnvironOne) {
  SubProcess::EnvMap actual, expected;
  expected["FOO"] = "BAR";
  SubProcess p;
  p.SetEnviron(expected);
  p.GetEnviron(&actual);
  EXPECT_TRUE(expected == actual);
}

TEST_F(SubProcessTest, GetEnvironSeveral) {
  SubProcess::EnvMap actual, expected;
  expected["FOO"] = "Bar";
  expected["CAT"] = "Dog";
  expected["BANANAS"] = "Canteloupes";
  SubProcess p;
  p.SetEnviron(expected);
  p.GetEnviron(&actual);
  EXPECT_TRUE(expected == actual);
}

TEST_F(SubProcessTest, GetEnvironSeveralWithExisting) {
  SubProcess::EnvMap actual, expected;
  actual["EXISTING"] = "Already here";
  expected["FOO"] = "Bar";
  expected["CAT"] = "Dog";
  expected["BANANAS"] = "Canteloupes";
  SubProcess p;
  p.SetEnviron(expected);
  p.GetEnviron(&actual);
  EXPECT_EQ("Already here", actual["EXISTING"]);
  EXPECT_EQ("Bar", actual["FOO"]);
  EXPECT_EQ("Dog", actual["CAT"]);
  EXPECT_EQ("Canteloupes", actual["BANANAS"]);
}

TEST_F(SubProcessTest, GetDefaultEnviron) {
  // Get the current environment directly
  extern char** environ;
  int i = 0;
  for (char** env = environ; *env; env++, i++) {
  }  // find nullptr
  std::set<std::string> orig(environ, environ + i);

  // Get the current environment from SubProcess.
  SubProcess::EnvMap default_env = SubProcess::GetThisProcessEnviron();
  EXPECT_EQ(default_env.size(), i);

  // Round-trip default_env though SubProcess
  SubProcess p;
  p.SetEnviron(default_env);
  const char* const* round_trip = p.GetEnviron();

  // Check the set of environment values match the origonal.
  i = 0;
  for (const char* const* env = round_trip; *env; env++, i++) {
  }  // find nullptr
  std::set<std::string> ret(round_trip, round_trip + i);
  EXPECT_EQ(orig, ret);
}

class InheritedSubProcess : public SubProcess {
 public:
  InheritedSubProcess() = default;
  // This type is neither copyable nor movable.
  InheritedSubProcess(const InheritedSubProcess&) = delete;
  InheritedSubProcess& operator=(const InheritedSubProcess&) = delete;

  ~InheritedSubProcess() override = default;

  int exit_status() const override { return exit_status_; }

  void set_exit_status(int status) { exit_status_ = status; }

 private:
  int exit_status_ = 0;
};

TEST_F(SubProcessTest, OverrideExitStatus) {
  InheritedSubProcess sp;
  sp.set_exit_status(SubProcess::kExecFailed);
  EXPECT_EQ(SubProcess::kExecFailed, sp.exit_status());
  EXPECT_FALSE(sp.exit_normal());
  EXPECT_EQ(SubProcess::kExecFailed, sp.exit_code());
  EXPECT_EQ(6, sp.exit_signum());

  // Simulate an exit code (88).
  sp.set_exit_status(55 << 8);
  EXPECT_FALSE(sp.exit_normal());
  EXPECT_EQ(55, sp.exit_code());
  EXPECT_EQ(0, sp.exit_signum());

  // Simulate a SIGKILL signal.
  sp.set_exit_status(SIGKILL);
  EXPECT_FALSE(sp.exit_normal());
  EXPECT_EQ(SubProcess::kWasKilled, sp.exit_code());
  EXPECT_EQ(SIGKILL, sp.exit_signum());

  // Simulate exit code 0 (normal and good).
  sp.set_exit_status(0);
  EXPECT_TRUE(sp.exit_normal());
  EXPECT_EQ(0, sp.exit_code());
  EXPECT_EQ(0, sp.exit_signum());
}

TEST_F(SubProcessTest, FdRace) {
  // This test tries to find bugs where the part of Subprocess that runs
  // before forking/cloning makes assumptions about which numbers will be
  // assigned to FDs it opens.
  const size_t kNumFds = 64;
  int fds[kNumFds];

  for (size_t i = 0; i < std::size(fds); ++i) {
    fds[i] = TEMP_FAILURE_RETRY(open("/dev/null", O_RDONLY));
  }

  absl::Notification done;
  std::vector<std::unique_ptr<ClosureThread>> cts;

  const size_t kNumThreads = 4;
  thread::Options thread_options;
  thread_options.set_joinable(true);
  for (size_t i = 0; i < kNumThreads; ++i) {
    auto* t =
        new ClosureThread(thread_options, "OpenCloser", [&done, &fds, i]() {
          while (!done.HasBeenNotified()) {
            close(fds[i]);
            fds[i] = open("/dev/null", O_RDONLY);
          }
        });
    t->Start();
    cts.emplace_back(t);
  }

  for (int j = 0; j < 1000; j++) {
    SubProcess proc(kNumFds + 1);
    proc.SetChannelAction(CHAN_STDIN, ACTION_PIPE);
    proc.SetProgram("/bin/true", {"true"});
    proc.EnableChildSetupLogs(true);
    ASSERT_TRUE(proc.Start());
    ASSERT_TRUE(proc.Wait());
  }

  done.Notify();

  for (const auto& t : cts) {
    t->Join();
  }

  for (size_t i = 0; i < std::size(fds); ++i) {
    close(fds[i]);
  }
}

enum class FailWhen { NEVER, EARLY, LATE };

enum class FailHow {
  EXIT,
  KILL,
  TERM,
  CLOSE,
};

class DoomedSubProcess : public SubProcess {
 public:
  DoomedSubProcess() = default;
  // This type is neither copyable nor movable.
  DoomedSubProcess(const DoomedSubProcess&) = delete;
  DoomedSubProcess& operator=(const DoomedSubProcess&) = delete;

  FailWhen fail_when_ = FailWhen::NEVER;
  FailHow fail_how_ = FailHow::EXIT;

 protected:
  void TEST_AfterForkBeforeExecEarly(int child_to_parent_fd) override {
    if (fail_when_ == FailWhen::EARLY) {
      Fail(child_to_parent_fd);
    }
  }

  void TEST_AfterForkBeforeExecLate(int child_to_parent_fd) override {
    if (fail_when_ == FailWhen::LATE) {
      Fail(child_to_parent_fd);
    }
  }

 private:
  void Fail(int child_to_parent_fd) {
    if (fail_how_ == FailHow::EXIT) {
      lss__exit(2, &child_errno_);
    } else if (fail_how_ == FailHow::KILL) {
      lss_kill(lss_getpid(&child_errno_), SIGKILL, &child_errno_);
    } else if (fail_how_ == FailHow::TERM) {
      lss_kill(lss_getpid(&child_errno_), SIGTERM, &child_errno_);
    } else {
      CHECK(fail_how_ == FailHow::CLOSE);
      // Close child_to_parent_fd, so the parent gets EOF when reading from it.
      // This simulates a mythical kernel bug where the socket returns spurious
      // EOFs even though it is still open. We dup the fd to another socket so
      // that the remaining setup code in the child has a valid fd to write to,
      // even though it goes nowhere.
      int pair[2];
      lss_socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair,
                     &child_errno_);
      lss_dup3(pair[0], child_to_parent_fd, O_CLOEXEC, &child_errno_);
    }
  }
};
