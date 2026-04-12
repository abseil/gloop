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

#include <memory>

#include "absl/functional/bind_front.h"
#include "gloop/gloop_test.h"
#include "gloop/thread/executor.h"

using thread::Executor;

namespace {

class InlineExecutorTest : public testing::Test {
 protected:
  InlineExecutorTest()
      : e1_(thread::NewInlineExecutor()),
        e2_(thread::NewInlineExecutor()),
        total_calls_(0) {}

  virtual ~InlineExecutorTest() {}

 public:
  void Counter() { total_calls_++; }

  //  Verify that the current executor is what we expect it to be.
  //  If we're expecting e1_, make a recursive Add() to e2_.
  void CurrentChecker(Executor* e) {
    total_calls_++;
    EXPECT_EQ(e, Executor::CurrentExecutor());
    if (e == e1())
      e2()->Schedule(
          absl::bind_front(&InlineExecutorTest::CurrentChecker, this, e2()));
  }

  Executor* e1() { return e1_.get(); }
  Executor* e2() { return e2_.get(); }
  std::unique_ptr<Executor> e1_, e2_;
  int total_calls_;
};

//  Not much to test - just that the callback was run.
TEST_F(InlineExecutorTest, OneExecutor) {
  auto cb = [this] { Counter(); };
  e1_->Schedule(cb);
  EXPECT_TRUE(e1()->TrySchedule(cb));
  EXPECT_TRUE(e1()->TrySchedule(cb));
  e1()->Schedule(cb);
  EXPECT_EQ(4, total_calls_);
}

//  Verify that InlineExecutor updates Executor::CurrentExecutor()
//  properly.
TEST_F(InlineExecutorTest, CurrentExecutor) {
  e1()->Schedule(
      absl::bind_front(&InlineExecutorTest::CurrentChecker, this, e1()));
  e1()->Schedule(
      absl::bind_front(&InlineExecutorTest::CurrentChecker, this, e1()));
  e2()->Schedule(
      absl::bind_front(&InlineExecutorTest::CurrentChecker, this, e2()));
  e1()->Schedule(
      absl::bind_front(&InlineExecutorTest::CurrentChecker, this, e1()));
  e2()->Schedule(
      absl::bind_front(&InlineExecutorTest::CurrentChecker, this, e2()));

  //  For each Add on e1_, we also ran a closure on e2_.
  EXPECT_EQ(8, total_calls_);
}

}  // namespace
