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

#include <signal.h>

#include "absl/log/check.h"
#include "gloop/util/process/subprocess.h"
#include "gtest/gtest.h"

namespace {

const char kShellCmd[] = "kill -TERM $$; true";

TEST(SubprocessSignalTest, TermIgnored) {
  SubProcess sp;
  sp.SetShellCommand(kShellCmd);
  PCHECK(SIG_ERR != signal(SIGTERM, SIG_DFL));
  sp.SetSignalAction(SIGTERM, SubProcess::SIGACTION_IGNORE);
  ASSERT_TRUE(sp.Start());
  sp.Wait();
  EXPECT_TRUE(sp.exit_normal());
}

TEST(SubprocessSignalTest, TermIgnoreInherited) {
  SubProcess sp;
  sp.SetShellCommand(kShellCmd);

  sp.SetSignalAction(SIGTERM, SubProcess::SIGACTION_INHERIT);
  PCHECK(SIG_ERR != signal(SIGTERM, SIG_IGN));
  ASSERT_TRUE(sp.Start());
  sp.Wait();
  EXPECT_TRUE(sp.exit_normal());
  PCHECK(SIG_ERR != signal(SIGTERM, SIG_DFL));
}

TEST(SubprocessSignalTest, TermBlocked) {
  SubProcess sp;
  sp.SetShellCommand(kShellCmd);

  sp.SetSignalAction(SIGTERM, SubProcess::SIGACTION_DEFAULT);
  sigset_t sset;
  sigfillset(&sset);
  sp.SetSignalMask(&sset);

  ASSERT_TRUE(sp.Start());
  sp.Wait();
  EXPECT_TRUE(sp.exit_normal());
}

TEST(SubprocessSignalTest, TermSetDefault) {
  SubProcess sp;
  sp.SetShellCommand(kShellCmd);

  sigset_t sset;
  sigemptyset(&sset);
  sp.SetSignalMask(&sset);
  sp.SetSignalAction(SIGTERM, SubProcess::SIGACTION_DEFAULT);

  PCHECK(SIG_ERR != signal(SIGTERM, SIG_IGN));

  ASSERT_TRUE(sp.Start());
  sp.Wait();
  EXPECT_EQ(SIGTERM, sp.exit_signum());

  PCHECK(SIG_ERR != signal(SIGTERM, SIG_DFL));
}

}  // namespace
