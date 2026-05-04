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

#include <cstdint>

#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/log/initialize.h"
#include "gloop/base/log_file_flags.h"
#include "gloop/thread/thread.h"
#include "gtest/gtest.h"

const int kMaxRecursionDepth = 100;
const int kOnStackSize = 256;

ABSL_DECLARE_FLAG(int32_t, default_thread_stack_size);

class TestThread : public Thread {
 public:
  explicit TestThread(int32_t max_recursion_depth) {
    Init(max_recursion_depth);
  };

  TestThread() { Init(kMaxRecursionDepth); }

  virtual void Run() {
    int32_t onstack[kOnStackSize];
    for (int i = 0; i < kOnStackSize; i++) onstack[i] = 0;
    result_ = 0;
    RecursionTest(0, onstack);
    result_ = onstack[0];
  }

  int32_t result() { return result_; }

 protected:
  void Init(int32_t max_recursion_depth) {
    max_recursion_depth_ = max_recursion_depth;
    SetJoinable(true);
  }

  void RecursionTest(int depth, int32_t* from_parent) {
    int32_t onstack[kOnStackSize];  // use 1K of stack space
    if (depth >= max_recursion_depth_) {
      for (int i = 0; i < kOnStackSize; i++) from_parent[i] = 0;
      return;
    }
    RecursionTest(depth + 1, onstack);
    for (int i = 0; i < kOnStackSize; i++) from_parent[i] = onstack[i] + 1;
  }

 private:
  int32_t result_;
  int32_t max_recursion_depth_;
};

bool RunThread(Thread* t) {
  t->Start();
  t->Join();
  return true;
}

TEST(DefaultStackSizeTest, TooLittleStackDeathTest) {
  absl::SetFlag(&FLAGS_default_thread_stack_size, PTHREAD_STACK_MIN);
  // Some platform like PowerPC64 can have a large minimum stack
  // size (128K).  The ensure that this fail, we need to
  // bump up the level of recursions.
  //
  // max_recursion_depth must have the same size as int32.  I should
  // be sizeof(int32) below but then lint would complain.
  int32_t max_recursion_depth =
      (PTHREAD_STACK_MIN / sizeof(max_recursion_depth) * kOnStackSize) + 1;
  TestThread* test_thread = new TestThread(max_recursion_depth);
  EXPECT_DEATH(RunThread(test_thread), "");
  delete test_thread;
}

TEST(DefaultStackSizeTest, EnoughStack) {
  absl::SetFlag(&FLAGS_default_thread_stack_size,
                2 * 1024 * kMaxRecursionDepth);
  TestThread* test_thread = new TestThread();
  EXPECT_EQ(true, RunThread(test_thread));
  EXPECT_EQ(kMaxRecursionDepth, test_thread->result());
  delete test_thread;
}

TEST(DefaultStackSizeTest, EnoughStackManual) {
  absl::SetFlag(&FLAGS_default_thread_stack_size, PTHREAD_STACK_MIN);
  // Thread stack size should override flag
  TestThread* test_thread = new TestThread();
  test_thread->SetStackSize(2 * 1024 * kMaxRecursionDepth);
  EXPECT_EQ(true, RunThread(test_thread));
  EXPECT_EQ(kMaxRecursionDepth, test_thread->result());
  delete test_thread;
}

int main(int argc, char** argv) {
  absl::SetFlag(&FLAGS_logtostderr, true);
  absl::InitializeLog();

  return RUN_ALL_TESTS();
}
