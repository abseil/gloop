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

#include "gloop/thread/add_after_helper.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/clock_interface.h"
#include "absl/time/simulated_clock.h"
#include "absl/time/time.h"
#include "benchmark/benchmark.h"
#include "gloop/base/callback.h"
#include "gloop/base/config.h"
#include "gloop/base/context.h"
#include "gloop/base/mock_tracer.h"
#include "gloop/base/tracecontext.h"
#include "gloop/base/walltime.h"
#include "gloop/thread/mock_executor.h"
#include "gloop/thread/threadpool.h"
#include "gloop/thread/timedcall.h"
#include "gloop/util/functional/to_callback.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using testing::InSequence;
using testing::MockFunction;
using thread::AddAfterHelper;

#if !PORTABLE_BASE  // We lack callback matchers because callbacks are limited
                    // in portable base (Ironically due to lack of ::Thread).
TEST(AddAfterHelperTest_NullUnderlyingExecutor, UsesTimedCall) {
  MockFunction<void(absl::AnyInvocable<void() &&>)> complete_add_after;
  AddAfterHelper timed_call_helper(nullptr, complete_add_after.AsStdFunction());

  absl::Notification done;
  EXPECT_CALL(complete_add_after, Call).WillOnce([&done] { done.Notify(); });

  absl::Time start_time = absl::Now();
  timed_call_helper.ScheduleAddAfterAt(absl::Now() + absl::Milliseconds(20),
                                       [] {});
  done.WaitForNotification();
  // Only 19ms to handle clock jitter.
  EXPECT_LT(base::ToWallTime(start_time + absl::Seconds(0.019)),
            base::ToWallTime(absl::Now()));
}
#endif  // !PORTABLE_BASE

class CheckPoint {
 public:
  MOCK_METHOD(void, Check, (const std::string& id));
};

class MockExecutor : public thread::MockExecutor {
 public:
  explicit MockExecutor(absl::Clock& clock) : clock_(clock) {}

  MOCK_METHOD(void, ScheduleAtCalled, (absl::Time when));

  // Saves the added closure into a vector and then records the call
  // by forwarding to ScheduleAtCalled
  void ScheduleAt(absl::Time when,
                  absl::AnyInvocable<void() &&> callback) override {
    add_afters_.push_back(std::move(callback));
    ScheduleAtCalled(when);
  }
  int num_add_afters() const { return add_afters_.size(); }
  void RunPendingAddAfters() {
    for (size_t i = 0; i < add_afters_.size(); ++i) {
      std::move(add_afters_[i])();
    }
    add_afters_.clear();
  }

  absl::Clock* clock() override { return &clock_; }

 private:
  std::vector<absl::AnyInvocable<void() &&>> add_afters_;
  absl::Clock& clock_;
};

class AddAfterHelperTest : public testing::Test {
 protected:
  AddAfterHelperTest()
      : mock_executor_(test_clock_),
        helper_(new AddAfterHelper(
            &mock_executor_,
            std::bind_front(&AddAfterHelperTest::CompleteAddAfter, this))),
        dummy_task_(util::functional::ToPermanentCallback([] {})) {}

  void CompleteAddAfter(absl::AnyInvocable<void() &&> task) {
    const char* thread_status = base::CurrentThreadStatus();
    if (thread_status != nullptr) {
      complete_add_after_thread_status_ = thread_status;
    }
    std::move(task)();
  }

  CheckPoint check_;
  absl::SimulatedClock test_clock_;
  MockExecutor mock_executor_;
  // unique_ptr<> so I can delete the instance and check what happens afterward.
  std::unique_ptr<AddAfterHelper> helper_;
  const ::util::functional::CallbackFunctor<> dummy_task_;
  std::string complete_add_after_thread_status_;
};

TEST_F(AddAfterHelperTest, AddAfterForwardsToUnderlyingExecutorAddAfter) {
  InSequence s;
  absl::Notification n;
  Closure* task = util::functional::ToCallback([&n] { n.Notify(); });
  EXPECT_CALL(mock_executor_,
              ScheduleAtCalled(test_clock_.TimeNow() + absl::Milliseconds(15)));
  EXPECT_CALL(check_, Check("1"));

  helper_->ScheduleAddAfter(absl::Milliseconds(15), task);
  check_.Check("1");

  EXPECT_EQ(1, mock_executor_.num_add_afters());
  // The MockExecutor doesn't run anything on its own, so we do it
  // explicitly here.  This will call the CancellableClosure that the
  // AddAfterHelper passed to the underlying Executor's AddAfter()
  // method, and that CancellableClosure will call
  // complete_add_after->Run(dummy_task_).
  mock_executor_.RunPendingAddAfters();
  EXPECT_TRUE(n.HasBeenNotified());
}

TEST_F(AddAfterHelperTest, ScheduleAtForwardsToUnderlyingExecutorScheduleAt) {
  InSequence s;
  absl::Time now = absl::Now();
  absl::Notification n;
  std::function<void()> task = [&n] { n.Notify(); };
  EXPECT_CALL(mock_executor_, ScheduleAtCalled(now));
  EXPECT_CALL(check_, Check("1"));

  helper_->ScheduleAddAfterAt(now, std::move(task));
  check_.Check("1");

  EXPECT_EQ(1, mock_executor_.num_add_afters());
  // The MockExecutor doesn't run anything on its own, so we do it
  // explicitly here.  This will call the CancellableClosure that the
  // AddAfterHelper passed to the underlying Executor's AddAfter()
  // method, and that CancellableClosure will call
  // complete_add_after->Run(dummy_task_).
  mock_executor_.RunPendingAddAfters();
  EXPECT_TRUE(n.HasBeenNotified());
}

void ExpectNotCalled() { ADD_FAILURE() << "Shouldn't be called."; }

TEST_F(AddAfterHelperTest, ShutdownExecutesUnrunClosures) {
  InSequence s;
  absl::Notification n1, n2;
  EXPECT_CALL(mock_executor_,
              ScheduleAtCalled(test_clock_.TimeNow() + absl::Milliseconds(20)))
      .Times(2);
  EXPECT_CALL(check_, Check("1"));
  // We may not run the closures in order, but we should run them both
  // before Shutdown returns.
  EXPECT_CALL(check_, Check("2"));

  helper_->ScheduleAddAfter(
      absl::Milliseconds(20),
      util::functional::ToCallback([&n1] { n1.Notify(); }));
  helper_->ScheduleAddAfter(
      absl::Milliseconds(20),
      util::functional::ToCallback([&n2] { n2.Notify(); }));
  check_.Check("1");
  // This should run complete_add_after_ on both Closure*s.
  helper_->ShutdownAndRunPendingImmediately();
  check_.Check("2");
  // After shutdown, ScheduleAddAfter() should not schedule or call its
  // argument, and should clean up non-permanent callbacks.
  helper_->ScheduleAddAfter(absl::ZeroDuration(),
                            ::util::functional::ToCallback(&ExpectNotCalled));
  ::util::functional::CallbackFunctor<> permanent_cb(
      ::util::functional::ToPermanentCallback(&ExpectNotCalled));
  helper_->ScheduleAddAfter(absl::ZeroDuration(), permanent_cb.get());
  // This should run complete_add_after_ on the two non-permanent Closure*s.
  helper_.reset();
  EXPECT_TRUE(n1.HasBeenNotified());
  EXPECT_TRUE(n2.HasBeenNotified());

  // Some time later, a real underlying Executor would get around to
  // calling the Closures that the AddAfterHelper scheduled on it.
  // The MockExecutor doesn't call anything on its own, so we simulate
  // that manually.  This will call the scheduled CancellableClosures
  // (allowing them to delete themselves), but since they were
  // cancelled in the Shutdown...() call, they won't call
  // complete_add_after_->Run(anything).
  EXPECT_EQ(2, mock_executor_.num_add_afters());
  mock_executor_.RunPendingAddAfters();
}

// It's possible we'll want to change exactly how deletion gets rid of
// its un-run closures, but here's what it does today.
TEST_F(AddAfterHelperTest, DeletionExecutesUnrunClosures) {
  InSequence s;
  absl::Notification n1, n2;
  EXPECT_CALL(mock_executor_,
              ScheduleAtCalled(test_clock_.TimeNow() + absl::Milliseconds(20)))
      .Times(2);
  EXPECT_CALL(check_, Check("1"));
  EXPECT_CALL(check_, Check("2"));

  helper_->ScheduleAddAfter(
      absl::Milliseconds(20),
      util::functional::ToCallback([&n1] { n1.Notify(); }));
  helper_->ScheduleAddAfter(
      absl::Milliseconds(20),
      util::functional::ToCallback([&n2] { n2.Notify(); }));
  check_.Check("1");
  // This should run complete_add_after_ on both Closure*s.
  helper_.reset();
  EXPECT_TRUE(n1.HasBeenNotified());
  EXPECT_TRUE(n2.HasBeenNotified());
  check_.Check("2");

  // Some time later, a real underlying Executor would get around to
  // calling the Closures that the AddAfterHelper scheduled on it.
  // The MockExecutor doesn't call anything on its own, so we simulate
  // that manually.  This will call the scheduled CancellableClosures
  // (allowing them to delete themselves), but since they were
  // cancelled in the destructor call, they won't call
  // complete_add_after_->Run(anything).
  EXPECT_EQ(2, mock_executor_.num_add_afters());
  mock_executor_.RunPendingAddAfters();
}

TEST_F(AddAfterHelperTest, PropagatesContextToDelayedTask) {
  const absl::Time now = absl::Now();

  std::string task_thread_status = "uninitialized";
  auto task = [&] { task_thread_status = base::CurrentThreadStatus(); };
  EXPECT_CALL(mock_executor_, ScheduleAtCalled(now));

  {
    base::WithThreadStatus ts("expected_task_thread_status");
    helper_->ScheduleAddAfterAt(now, std::move(task));
  }

  ASSERT_EQ(1, mock_executor_.num_add_afters());
  // The MockExecutor doesn't run anything on its own, so we do it
  // explicitly here.  This will call the CancellableClosure that the
  // AddAfterHelper passed to the underlying Executor's AddAfter()
  // method, and that CancellableClosure will call
  // complete_add_after->Run(dummy_task_).
  mock_executor_.RunPendingAddAfters();
  EXPECT_EQ(task_thread_status, "expected_task_thread_status");
}

TEST_F(AddAfterHelperTest, PropagatesContextToCompleteAddAfter) {
  const absl::Time now = absl::Now();

  EXPECT_CALL(mock_executor_, ScheduleAtCalled(now));

  {
    base::WithThreadStatus ts("expected_task_thread_status");
    helper_->ScheduleAddAfterAt(now, [] {});
  }

  ASSERT_EQ(1, mock_executor_.num_add_afters());
  // The MockExecutor doesn't run anything on its own, so we do it
  // explicitly here.
  mock_executor_.RunPendingAddAfters();
  EXPECT_EQ(complete_add_after_thread_status_, "expected_task_thread_status");
}

void RunClosureArg(absl::AnyInvocable<void() &&> task) { std::move(task)(); }

void BM_ObjectOverhead(benchmark::State& state) {
  for (auto _ : state) {
    AddAfterHelper helper(nullptr, &RunClosureArg);
  }
}
BENCHMARK(BM_ObjectOverhead);

void BM_ExplicitShutdownOverhead(benchmark::State& state) {
  for (auto _ : state) {
    AddAfterHelper helper(nullptr, &RunClosureArg);
    helper.ShutdownAndRunPendingImmediately();
  }
}
BENCHMARK(BM_ExplicitShutdownOverhead);

void BM_ThreadPoolAddCost(benchmark::State& state) {
  ThreadPool pool(1);
  for (auto _ : state) {
    pool.Schedule([] {});
  }
}
BENCHMARK(BM_ThreadPoolAddCost);

void BM_TimedCallRunCost(benchmark::State& state) {
  for (auto _ : state) {
    TimedCall::RunAt(base::ToWallTime(absl::Now()), [] {});
  }
}
BENCHMARK(BM_TimedCallRunCost);

void BM_AddAfterOverhead(benchmark::State& state) {
  AddAfterHelper helper(nullptr, &RunClosureArg);
  for (auto _ : state) {
    helper.ScheduleAddAfter(absl::Milliseconds(1),
                            util::functional::ToCallback([] {}));
  }
}
BENCHMARK(BM_AddAfterOverhead);

}  // namespace
