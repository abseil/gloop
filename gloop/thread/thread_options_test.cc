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

#include "gloop/thread/thread_options.h"

#include <cstddef>
#include <cstdint>

#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "gtest/gtest.h"

ABSL_DECLARE_FLAG(int32_t, default_thread_stack_size);
ABSL_DECLARE_FLAG(int32_t, default_thread_stack_guard);

namespace thread {
namespace {

TEST(ThreadOptionsTest, DefaultConstructor) {
  Options options;
  EXPECT_FALSE(options.joinable());
  EXPECT_EQ(options.stack_size(), 0);
  EXPECT_EQ(options.guard_size(), 0);
  EXPECT_EQ(options.scheduling_policy(), SCHEDPOLICY_NORMAL);
  EXPECT_EQ(options.get_sched_priority(), -1);
  EXPECT_EQ(options.nice_priority_level(), 0);
  EXPECT_EQ(options.io_priority_level(), -1);
  EXPECT_EQ(options.io_class(), -1);
}

TEST(ThreadOptionsTest, SetAndGetJoinable) {
  Options options;
  options.set_joinable(true);
  EXPECT_TRUE(options.joinable());

  options.set_joinable(false);
  EXPECT_FALSE(options.joinable());
}

TEST(ThreadOptionsTest, SetAndGetStackSize) {
  Options options;
  options.set_stack_size(65536);
  EXPECT_EQ(options.stack_size(), 65536);

  options.set_stack_size(0);
  EXPECT_EQ(options.stack_size(), 0);
}

TEST(ThreadOptionsTest, SetAndGetGuardSize) {
  Options options;
  options.set_guard_size(8192);
  EXPECT_EQ(options.guard_size(), 8192);

  options.set_guard_size(0);
  EXPECT_EQ(options.guard_size(), 0);
}

TEST(ThreadOptionsTest, SetAndGetSchedulingPolicy) {
  Options options;

  options.set_scheduling_policy(SCHEDPOLICY_BESTEFFORT);
  EXPECT_EQ(options.scheduling_policy(), SCHEDPOLICY_BESTEFFORT);

  options.set_scheduling_policy(SCHEDPOLICY_NORMAL);
  EXPECT_EQ(options.scheduling_policy(), SCHEDPOLICY_NORMAL);

  options.set_scheduling_policy(SCHEDPOLICY_URGENT);
  EXPECT_EQ(options.scheduling_policy(), SCHEDPOLICY_URGENT);

  options.set_scheduling_policy(SCHEDPOLICY_FIFO);
  EXPECT_EQ(options.scheduling_policy(), SCHEDPOLICY_FIFO);
}

TEST(ThreadOptionsTest, SetAndGetSchedPriority) {
  Options options;
  options.set_sched_priority(10);
  EXPECT_EQ(options.get_sched_priority(), 10);

  options.set_sched_priority(-1);
  EXPECT_EQ(options.get_sched_priority(), -1);
}

TEST(ThreadOptionsTest, SetAndGetNicePriorityLevel) {
  Options options;
  options.set_nice_priority_level(-5);
  EXPECT_EQ(options.nice_priority_level(), -5);

  options.set_nice_priority_level(10);
  EXPECT_EQ(options.nice_priority_level(), 10);
}

TEST(ThreadOptionsTest, SetAndGetIoPriority) {
  Options options;
  options.set_io_priority(2, 4);
  EXPECT_EQ(options.io_class(), 2);
  EXPECT_EQ(options.io_priority_level(), 4);
}

TEST(ThreadOptionsTest, MethodChaining) {
  Options options;
  options.set_joinable(true)
      .set_stack_size(1024)
      .set_guard_size(2048)
      .set_scheduling_policy(SCHEDPOLICY_URGENT)
      .set_sched_priority(5)
      .set_nice_priority_level(-10)
      .set_io_priority(1, 3);

  EXPECT_TRUE(options.joinable());
  EXPECT_EQ(options.stack_size(), 1024);
  EXPECT_EQ(options.guard_size(), 2048);
  EXPECT_EQ(options.scheduling_policy(), SCHEDPOLICY_URGENT);
  EXPECT_EQ(options.get_sched_priority(), 5);
  EXPECT_EQ(options.nice_priority_level(), -10);
  EXPECT_EQ(options.io_class(), 1);
  EXPECT_EQ(options.io_priority_level(), 3);
}

TEST(ThreadOptionsTest, CopyConstructorAndAssignment) {
  Options options1;
  options1.set_joinable(true)
      .set_stack_size(4096)
      .set_guard_size(8192)
      .set_scheduling_policy(SCHEDPOLICY_FIFO)
      .set_sched_priority(20)
      .set_nice_priority_level(5)
      .set_io_priority(2, 6);

  Options options2(options1);
  EXPECT_TRUE(options2.joinable());
  EXPECT_EQ(options2.stack_size(), 4096);
  EXPECT_EQ(options2.guard_size(), 8192);
  EXPECT_EQ(options2.scheduling_policy(), SCHEDPOLICY_FIFO);
  EXPECT_EQ(options2.get_sched_priority(), 20);
  EXPECT_EQ(options2.nice_priority_level(), 5);
  EXPECT_EQ(options2.io_class(), 2);
  EXPECT_EQ(options2.io_priority_level(), 6);

  Options options3 = options1;
  EXPECT_TRUE(options3.joinable());
  EXPECT_EQ(options3.stack_size(), 4096);
  EXPECT_EQ(options3.guard_size(), 8192);
  EXPECT_EQ(options3.scheduling_policy(), SCHEDPOLICY_FIFO);
  EXPECT_EQ(options3.get_sched_priority(), 20);
  EXPECT_EQ(options3.nice_priority_level(), 5);
  EXPECT_EQ(options3.io_class(), 2);
  EXPECT_EQ(options3.io_priority_level(), 6);
}

TEST(ThreadOptionsTest, RespectsDefaultFlags) {
  absl::SetFlag(&FLAGS_default_thread_stack_size, 16384);
  absl::SetFlag(&FLAGS_default_thread_stack_guard, 4096);

  Options options;
  EXPECT_EQ(options.stack_size(), 16384);
  EXPECT_EQ(options.guard_size(), 4096);

  // Restore defaults
  absl::SetFlag(&FLAGS_default_thread_stack_size, 0);
  absl::SetFlag(&FLAGS_default_thread_stack_guard, 0);
}

}  // namespace
}  // namespace thread
