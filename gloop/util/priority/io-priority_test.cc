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

#include "gloop/util/priority/io-priority.h"

#include <ctime>
#include <limits>
#include <thread>  // NOLINT

#include "absl/log/check.h"
#include "gloop/base/sysinfo.h"
#include "gloop/gloop_test.h"
#include "gloop/thread/threadpool.h"

namespace util {

TEST(IOPriorityTest, SetIOPriorityInvalidInputs) {
  // This can't be represented in IOPRIO_CLASS_SHIFT (13) bits.
  int bogus_priority = std::numeric_limits<int32_t>::max();
  EXPECT_FALSE(SetProcessIOPriority(IOPRIO_CLASS_BE, bogus_priority));

  // If the kernel we're running on doesn't support IO Priority,
  // SetProcessIOPriority will return false anyways, as the syscall
  // will return a "Function not implemented" error.
  EXPECT_FALSE(SetProcessIOPriority(IOPRIO_CLASS_INVALID, 0));

  int bogus_hint = IOPRIO_NR_HINTS;
  EXPECT_FALSE(SetProcessIOPriority(IOPRIO_CLASS_IDLE, 0, bogus_hint));
}

TEST(IOPriorityTest, SetPriorityLevels) {
  const pid_t tid = GetTID();
  IOPriorityClass iop_class = IOPRIO_CLASS_BE;
  for (int level = 0; level < IOPRIO_NR_LEVELS; level++) {
    EXPECT_TRUE(SetIOPriority(tid, iop_class, level));

    IOPriorityClass new_iop_class = IOPRIO_CLASS_NONE;
    int new_level = -1;
    EXPECT_TRUE(GetIOPriority(tid, &new_iop_class, &new_level));
    EXPECT_EQ(new_iop_class, iop_class);
    EXPECT_EQ(new_level, level);
  }
}

TEST(IOPriorityTest, SetPriorityHints) {
  const pid_t tid = GetTID();
  // Linux kernel does not check the level for IOPRIO_CLASS_IDLE.
  // Using IOPRIO_CLASS_BE fails on pre-v6.5 kernels because all 13 bits
  // of DATA are used to check for the level to be in the [0..7] range.
  IOPriorityClass iop_class = IOPRIO_CLASS_IDLE;
  int level = 4;
  for (int hint = 0; hint < IOPRIO_NR_HINTS; hint++) {
    EXPECT_TRUE(SetIOPriority(tid, iop_class, level, hint));

    IOPriorityClass new_iop_class = IOPRIO_CLASS_NONE;
    int new_level = -1;
    int new_hint = -1;
    EXPECT_TRUE(GetIOPriority(tid, &new_iop_class, &new_level, &new_hint));
    EXPECT_EQ(new_iop_class, iop_class);
    EXPECT_EQ(new_level, level);
    EXPECT_EQ(new_hint, hint);
  }
}

// Sets priority to class BE and 'level' and then verifies it was set.
// Since this code gets executed by a thread, CHECK is used here rather
// than EXPECT.
static void SetAndCheckThreadPriority(int level) {
  const pid_t tid = GetTID();
  CHECK(SetIOPriority(tid, IOPRIO_CLASS_BE, level));
  IOPriorityClass new_iop_class = IOPRIO_CLASS_NONE;
  int new_level = -1;
  EXPECT_TRUE(GetIOPriority(tid, &new_iop_class, &new_level));
  CHECK_EQ(new_iop_class, IOPRIO_CLASS_BE);
  CHECK_EQ(new_level, level);
}

TEST(IOPriorityTest, TestThreads) {
  const int main_thread_level = 4;
  SetAndCheckThreadPriority(main_thread_level);

  {
    // Fire up a thread and set & check a different priority level
    ThreadPool p(1);

    p.Schedule([] { SetAndCheckThreadPriority(5); });
  }

  // main thread's priority level is unaffected by thread setting
  int new_level = -1;
  EXPECT_TRUE(GetIOPriority(GetTID(), nullptr, &new_level));
  EXPECT_EQ(new_level, main_thread_level);
}

TEST(IOPriorityTest, TestStdThreads) {
  const int main_thread_level = 4;
  SetAndCheckThreadPriority(main_thread_level);

  {
    // Fire up a thread and set & check a different priority level
    std::thread thread([] { SetAndCheckThreadPriority(5); });
    thread.join();
  }

  // main thread's priority level is unaffected by thread setting
  int new_level = -1;
  ASSERT_TRUE(GetIOPriority(GetTID(), nullptr, &new_level));
  EXPECT_EQ(new_level, main_thread_level);
}

TEST(IOPriorityTest, TestMakeSystemIOPriority) {
  // Make sure the default hint is IOPRIO_HINT_NONE
  int system_priority = MakeSystemIOPriority(IOPRIO_CLASS_RT, 0);
  int system_priority_hint =
      MakeSystemIOPriority(IOPRIO_CLASS_RT, 0, IOPRIO_HINT_NONE);

  EXPECT_EQ(system_priority, system_priority_hint);
}

TEST(IOPriorityTest, TestExtractIOPriorityHint) {
  for (int io_priority_class = IOPRIO_CLASS_NONE;
       io_priority_class < IOPRIO_CLASS_INVALID; ++io_priority_class) {
    for (int hint = 0; hint < IOPRIO_NR_HINTS; ++hint) {
      const int system_priority = MakeSystemIOPriority(
          static_cast<IOPriorityClass>(io_priority_class), 0, hint);
      EXPECT_EQ(ExtractIOPriorityHint(system_priority), hint);
    }
  }
}

}  // namespace util
