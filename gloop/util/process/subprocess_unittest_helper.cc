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

#include <fcntl.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "gloop/base/init_google.h"
#include "gloop/util/process/subprocess.h"

ABSL_FLAG(int32_t, parent_death_signal, 0,
          "Signal that should be delivered to child process when parent is "
          "killed");

std::string* pid_file_str = new std::string;

ABSL_FLAG(std::string, pid_file, "",
          "File where child process's PID should be written")
    .OnUpdate([]() { *pid_file_str = absl::GetFlag(FLAGS_pid_file); });

ABSL_FLAG(bool, trigger_race_condition, false,
          "If set, the parent death signal is set after the parent has died");

ABSL_FLAG(bool, test_thp_disabled, false,
          "If set, exit(0) if THP is disabled, otherwise exit(1)");

namespace {

void MaybeWritePid(pid_t pid) {
  if (pid_file_str->empty()) {
    return;
  }

  // This may execute in the child before exec.
  int fd = open(pid_file_str->c_str(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
  if (fd < 0) {
    // The PID file won't be written and the unittest will time out.
    return;
  }

  char buf[32];
  size_t n = snprintf(buf, sizeof(buf), "%d", static_cast<int>(pid));
  (void)write(fd, buf, n);
  close(fd);
}

class TestSubProcess : public SubProcess {
 public:
  explicit TestSubProcess(bool block_until_parent_death)
      : block_until_parent_death_(block_until_parent_death), ppid_(getpid()) {}

  void TEST_AfterForkBeforeSetParentDeathSignal() override {
    if (block_until_parent_death_) {
      // Write the PID file from the child, as the parent may never return from
      // sp.Start().
      MaybeWritePid(getpid());

      // stdin is closed when the parent shuts down. Wait until that before
      // busy-looping.
      char c;
      while (read(STDIN_FILENO, &c, 1) < 0) {
      }

      // Busy-loop until the parent has been reaped.
      while (ppid_ == getppid()) {
      }
    }
  }

 private:
  bool block_until_parent_death_;
  pid_t ppid_;
};

}  // namespace

// Helper program used by subprocess_unittest.  Starts a long-running subprocess
// (optionally setting a signal to be delivered if the parent process is killed)
// and writes its PID to a file.

int main(int argc, char** argv) {
  InitGoogle(argv[0], &argc, &argv, true);

  if (absl::GetFlag(FLAGS_test_thp_disabled)) {
    if (prctl(PR_GET_THP_DISABLE, 0, 0, 0, 0)) {
      std::cout << "THP is disabled" << '\n';
      exit(0);
    }
    exit(1);
  }

  TestSubProcess sp(absl::GetFlag(FLAGS_trigger_race_condition));
#ifdef PR_SET_PDEATHSIG
  if (absl::GetFlag(FLAGS_parent_death_signal) > 0) {
    sp.SetParentDeathSignal(absl::GetFlag(FLAGS_parent_death_signal));
  }
  // Set STDIN to pipe for the child to block on.
  // This also causes the FlushInfoMessages after the prctl call to be skipped,
  // as the buffer is flushed to send the fds to the parent. If the buffer is
  // flushed after the parent has died, the subprocess dies, which hides the
  // effect of the race.
  sp.SetChannelAction(CHAN_STDIN, ACTION_PIPE);
#endif
  sp.SetShellCommand("sleep 90");
  sp.SetUseProcessGroup();
  CHECK(sp.Start());

  if (!absl::GetFlag(FLAGS_trigger_race_condition)) {
    MaybeWritePid(sp.pid());
  }

  sp.Wait();
  return 0;
}
