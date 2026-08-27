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

#include "gloop/util/task/child_task_barrier.h"

#include <functional>
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "gloop/thread/executor.h"
#include "gloop/util/status/status.h"
#include "gloop/util/task/sync_task.h"
#include "gloop/util/task/task.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace util {
namespace {

using ::testing::Bool;
using ::testing::Combine;
using ::testing::Values;

std::unique_ptr<absl::Notification> CreateCancelNotification(util::Task* t) {
  auto n_ptr = std::make_unique<absl::Notification>();
  t->WhenCancelled([n = n_ptr.get()]() { n->Notify(); });
  return n_ptr;
}

bool AwaitNotificationOrFail(absl::Notification* n) {
  return n->WaitForNotificationWithTimeout(absl::Minutes(1));
}

// Check that a SyncTask* has completed with a status matching 'm'.
MATCHER_P(BecomesDone, m, "") {
  if (!arg->WaitIgnoresCancelWithTimeout(absl::Milliseconds(1000))) {
    *result_listener << "Parent task is not yet done.";
    // If we finish a test the task is unexpectedly incomplete, it may crash,
    // but it might not, in which case we can get some useful test output other
    // than having the whole test time out.
    return false;
  } else if (!testing::Value(arg->status(), m)) {
    *result_listener << "Parent task completed with unexpected result: "
                     << ::util::StatusToString(arg->status());
    return false;
  } else {
    return true;
  }
}

MATCHER_P(WithError, message, "") { return arg.message() == message; }

// Check that a SyncTask* has not yet completed.
MATCHER(StillDoesNotBecomeDone, "") {
  // Don't wait long; just long enough for any threads trying to incorrectly
  // complete the task to run.
  if (!arg->WaitIgnoresCancelWithTimeout(absl::Milliseconds(150))) {
    return true;
  } else {
    *result_listener << "Parent task already done: "
                     << ::util::StatusToString(arg->status());
    return false;
  }
}

// An error with the given message.
static absl::Status Failure(std::string message) {
  return ::util::MakeStatus(CanonicalErrorSpace(), error::UNKNOWN, message);
}

template <typename BarrierT>
class ChildTaskBarrierTestBase
    : public ::testing::TestWithParam<
          ::testing::tuple<thread::Executor*, bool>> {
 protected:
  ChildTaskBarrierTestBase() : barrier_(new BarrierT(sync_.task())) {
    sync_.task()->set_executor(::testing::get<0>(GetParam()));
    sync_.task()->set_inline_done_callback(::testing::get<1>(GetParam()));
  }

  SyncTask sync_;
  std::unique_ptr<BarrierT> barrier_;
};

// On a multi-threaded executor, if Return is called on multiple tasks is quick
// succession, they might return to the parent task out of order;
// StillDoesNotBecomeDone() waits long enough to prevent this.
class ChildTaskBarrierTest : public ChildTaskBarrierTestBase<ChildTaskBarrier> {
};
class NeglectedChildTaskBarrierTest
    : public ChildTaskBarrierTestBase<NeglectedChildTaskBarrier> {};
class RedundantChildTaskBarrierTest
    : public ChildTaskBarrierTestBase<RedundantChildTaskBarrier> {};

TEST_P(ChildTaskBarrierTest, EmptyGroup) {
  barrier_.reset();
  EXPECT_THAT(&sync_, BecomesDone(absl::OkStatus()));
}

TEST_P(NeglectedChildTaskBarrierTest, EmptyGroup) {
  barrier_.reset();
  EXPECT_THAT(&sync_, BecomesDone(absl::OkStatus()));
}

TEST_P(RedundantChildTaskBarrierTest, EmptyGroup) {
  barrier_.reset();
  EXPECT_THAT(&sync_, BecomesDone(absl::UnknownError("")));
}

template <typename T>
static void TestAllGoodCloseFirst(T* barrier, SyncTask* sync) {
  std::unique_ptr<T> barrier_scope(barrier);
  Task* child1 = barrier->AddChildTask();
  Task* child2 = barrier->AddChildTask();
  EXPECT_THAT(sync, StillDoesNotBecomeDone());
  barrier_scope.reset();
  child1->Return(absl::OkStatus());
  EXPECT_THAT(sync, StillDoesNotBecomeDone());
  child2->Return(absl::OkStatus());
  EXPECT_THAT(sync, BecomesDone(absl::OkStatus()));
}

TEST_P(ChildTaskBarrierTest, AllGoodCloseFirst) {
  TestAllGoodCloseFirst(barrier_.release(), &sync_);
}

TEST_P(NeglectedChildTaskBarrierTest, AllGoodCloseFirst) {
  TestAllGoodCloseFirst(barrier_.release(), &sync_);
}

TEST_P(RedundantChildTaskBarrierTest, AllGoodCloseFirst) {
  TestAllGoodCloseFirst(barrier_.release(), &sync_);
}

template <typename T>
static void TestAllGoodCloseLast(T* barrier, SyncTask* sync) {
  std::unique_ptr<T> barrier_scope(barrier);
  Task* child1 = barrier->AddChildTask();
  Task* child2 = barrier->AddChildTask();
  child1->Return(absl::OkStatus());
  child2->Return(absl::OkStatus());
  EXPECT_THAT(sync, StillDoesNotBecomeDone());
  barrier_scope.reset();
  EXPECT_THAT(sync, BecomesDone(absl::OkStatus()));
}

TEST_P(ChildTaskBarrierTest, AllGoodCloseLast) {
  TestAllGoodCloseLast(barrier_.release(), &sync_);
}

TEST_P(NeglectedChildTaskBarrierTest, AllGoodCloseLast) {
  TestAllGoodCloseLast(barrier_.release(), &sync_);
}

TEST_P(RedundantChildTaskBarrierTest, AllGoodCloseLast) {
  TestAllGoodCloseLast(barrier_.release(), &sync_);
}

TEST_P(ChildTaskBarrierTest, CloseThenKeepsFirstError) {
  Task* child1 = barrier_->AddChildTask();
  auto child1_cancelled = CreateCancelNotification(child1);
  Task* child2 = barrier_->AddChildTask();
  barrier_.reset();
  child2->Return(Failure("bar"));  // Fail second child before first.
  EXPECT_THAT(&sync_, StillDoesNotBecomeDone());
  ASSERT_TRUE(AwaitNotificationOrFail(child1_cancelled.get()));
  child1->Return(Failure("foo"));
  EXPECT_THAT(&sync_, BecomesDone(WithError("bar")));
}

TEST_P(NeglectedChildTaskBarrierTest, ChildFailureIgnored) {
  Task* child = barrier_->AddChildTask();
  barrier_.reset();
  child->Return(Failure("injected"));
  EXPECT_THAT(&sync_, BecomesDone(absl::OkStatus()));
}

TEST_P(NeglectedChildTaskBarrierTest, FirstChildFailureIgnored) {
  Task* child1 = barrier_->AddChildTask();
  Task* child2 = barrier_->AddChildTask();
  barrier_.reset();
  child1->Return(Failure("injected"));
  EXPECT_THAT(&sync_, StillDoesNotBecomeDone());
  EXPECT_FALSE(child2->CancelRequested());
  child2->Return();
  EXPECT_THAT(&sync_, BecomesDone(absl::OkStatus()));
}

TEST_P(NeglectedChildTaskBarrierTest, SecondChildFailureIgnored) {
  Task* child1 = barrier_->AddChildTask();
  Task* child2 = barrier_->AddChildTask();
  barrier_.reset();
  child2->Return(Failure("injected"));
  EXPECT_THAT(&sync_, StillDoesNotBecomeDone());
  EXPECT_FALSE(child1->CancelRequested());
  child1->Return();
  EXPECT_THAT(&sync_, BecomesDone(absl::OkStatus()));
}

TEST_P(RedundantChildTaskBarrierTest, IgnoreFailuresIfEventualSuccess) {
  Task* child1 = barrier_->AddChildTask();
  Task* child2 = barrier_->AddChildTask();
  Task* child3 = barrier_->AddChildTask();
  auto child3_cancelled = CreateCancelNotification(child3);
  barrier_.reset();
  child1->Return(Failure("injected"));
  EXPECT_THAT(&sync_, StillDoesNotBecomeDone());
  EXPECT_FALSE(child2->CancelRequested());
  EXPECT_FALSE(child3->CancelRequested());
  child2->Return();
  EXPECT_THAT(&sync_, StillDoesNotBecomeDone());
  ASSERT_TRUE(AwaitNotificationOrFail(child3_cancelled.get()));
  child3->Return(Failure("injected"));
  EXPECT_THAT(&sync_, BecomesDone(absl::OkStatus()));
}

TEST_P(RedundantChildTaskBarrierTest,
       ChildTaskErrorDetailsTakenFromLastFailure) {
  Task* child1 = barrier_->AddChildTask();
  Task* child2 = barrier_->AddChildTask();
  barrier_.reset();
  child1->Return(Failure("foo"));
  EXPECT_THAT(&sync_, StillDoesNotBecomeDone());
  EXPECT_FALSE(child2->CancelRequested());
  child2->Return(Failure("bar"));
  EXPECT_THAT(&sync_, BecomesDone(WithError("bar")));
}

TEST_P(ChildTaskBarrierTest, FirstErrorKeptWithLateClose) {
  Task* child1 = barrier_->AddChildTask();
  auto child1_cancelled = CreateCancelNotification(child1);
  Task* child2 = barrier_->AddChildTask();
  child2->Return(Failure("bar"));
  EXPECT_THAT(&sync_, StillDoesNotBecomeDone());
  ASSERT_TRUE(AwaitNotificationOrFail(child1_cancelled.get()));
  child1->Return(Failure("foo"));
  EXPECT_THAT(&sync_, StillDoesNotBecomeDone());
  barrier_.reset();
  EXPECT_THAT(&sync_, BecomesDone(WithError("bar")));
}

template <typename T>
static void TestParentErrorPropagatesToChildren(T* barrier, SyncTask* sync) {
  std::unique_ptr<T> barrier_scope(barrier);
  Task* child1 = barrier->AddChildTask();
  auto child1_cancelled = CreateCancelNotification(child1);
  Task* child2 = barrier->AddChildTask();
  auto child2_cancelled = CreateCancelNotification(child2);

  const absl::Status parent_error = Failure("parent failed");
  sync->task()->Return(parent_error);
  EXPECT_THAT(sync, StillDoesNotBecomeDone());
  ASSERT_TRUE(AwaitNotificationOrFail(child1_cancelled.get()));
  ASSERT_TRUE(AwaitNotificationOrFail(child2_cancelled.get()));
  child1->Return();
  child2->Return();

  barrier_scope.reset();
  EXPECT_THAT(sync, BecomesDone(parent_error));
}

TEST_P(ChildTaskBarrierTest, ParentErrorPropagatesToChildren) {
  TestParentErrorPropagatesToChildren(barrier_.release(), &sync_);
}

TEST_P(NeglectedChildTaskBarrierTest, ParentErrorPropagatesToChildren) {
  TestParentErrorPropagatesToChildren(barrier_.release(), &sync_);
}

TEST_P(RedundantChildTaskBarrierTest, ParentErrorPropagatesToChildren) {
  TestParentErrorPropagatesToChildren(barrier_.release(), &sync_);
}

MATCHER_P2(ChildWithStatus, child_task, status, "") {
  return (arg == child_task &&
          testing::ExplainMatchResult(status, arg->status(), result_listener));
}

template <typename T>
class BarrierMock {
 public:
  MOCK_METHOD(void, Mark, ());
  MOCK_METHOD(void, ChildDone, (util::Task * child_task));

  Task* AddChildWithCallback(T* barrier) {
    Task* child = barrier->AddChildTaskWithCallback(
        std::bind(&BarrierMock::ChildDone, this, std::placeholders::_1));
    child->set_inline_done_callback(true);
    return child;
  }

  Task* AddChildWithCallbackAndExecutor(T* barrier,
                                        thread::Executor* executor) {
    Task* child = barrier->AddChildTaskWithCallbackAndExecutor(
        std::bind(&BarrierMock::ChildDone, this, std::placeholders::_1),
        executor);
    child->set_inline_done_callback(true);
    return child;
  }
};

TEST_P(ChildTaskBarrierTest, AddChildTaskWithCallback) {
  BarrierMock<ChildTaskBarrier> mock;
  Task* child1 = mock.AddChildWithCallback(barrier_.get());
  Task* child2 = mock.AddChildWithCallback(barrier_.get());
  Task* child3 = mock.AddChildWithCallback(barrier_.get());
  auto child3_cancelled = CreateCancelNotification(child3);
  barrier_.reset();

  // Each child sees the appropriate result immediately.
  testing::InSequence seq;
  EXPECT_CALL(mock, Mark());
  EXPECT_CALL(mock, ChildDone(ChildWithStatus(child2, absl::OkStatus())));
  EXPECT_CALL(mock, Mark());
  EXPECT_CALL(mock, ChildDone(ChildWithStatus(child1, WithError("foo"))));
  EXPECT_CALL(mock, Mark());
  EXPECT_CALL(mock, ChildDone(ChildWithStatus(child3, WithError("bar"))));
  EXPECT_CALL(mock, Mark());

  mock.Mark();
  child2->Return(absl::OkStatus());  // ChildDone seen immediately.
  EXPECT_THAT(&sync_, StillDoesNotBecomeDone());
  mock.Mark();
  child1->Return(Failure("foo"));  // First child failure fails parent.
  EXPECT_THAT(&sync_, StillDoesNotBecomeDone())
      << "Parent should wait for child3";
  EXPECT_FALSE(sync_.task()->IsActive()) << "Parent should have failed.";
  ASSERT_TRUE(AwaitNotificationOrFail(child3_cancelled.get()));
  mock.Mark();
  child3->Return(Failure("bar"));
  mock.Mark();
  EXPECT_THAT(&sync_, BecomesDone(WithError("foo")));
}

TEST_P(NeglectedChildTaskBarrierTest, AddChildTaskWithCallback) {
  BarrierMock<NeglectedChildTaskBarrier> mock;
  Task* child1 = mock.AddChildWithCallback(barrier_.get());
  Task* child2 = mock.AddChildWithCallback(barrier_.get());
  Task* child3 = mock.AddChildWithCallback(barrier_.get());
  barrier_.reset();

  // Each child sees the appropriate result immediately.
  testing::InSequence seq;
  EXPECT_CALL(mock, Mark());
  EXPECT_CALL(mock, ChildDone(ChildWithStatus(child2, absl::OkStatus())));
  EXPECT_CALL(mock, Mark());
  EXPECT_CALL(mock, ChildDone(ChildWithStatus(child1, WithError("foo"))));
  EXPECT_CALL(mock, Mark());
  EXPECT_CALL(mock, ChildDone(ChildWithStatus(child3, WithError("bar"))));
  EXPECT_CALL(mock, Mark());

  mock.Mark();
  child2->Return(absl::OkStatus());  // ChildDone seen immediately.
  EXPECT_THAT(&sync_, StillDoesNotBecomeDone());
  mock.Mark();
  child1->Return(Failure("foo"));
  EXPECT_THAT(&sync_, StillDoesNotBecomeDone())
      << "Parent should wait for child3";
  EXPECT_TRUE(sync_.task()->IsActive()) << "Parent should not have failed.";
  EXPECT_FALSE(child3->CancelRequested());
  mock.Mark();
  child3->Return(Failure("bar"));
  mock.Mark();
  EXPECT_THAT(&sync_, BecomesDone(absl::OkStatus()));
}

TEST_P(RedundantChildTaskBarrierTest, AddChildTaskWithCallback) {
  BarrierMock<RedundantChildTaskBarrier> mock;
  Task* child1 = mock.AddChildWithCallback(barrier_.get());
  Task* child2 = mock.AddChildWithCallback(barrier_.get());
  Task* child3 = mock.AddChildWithCallback(barrier_.get());
  auto child3_cancelled = CreateCancelNotification(child3);
  barrier_.reset();

  // Each child sees the appropriate result immediately.
  testing::InSequence seq;
  EXPECT_CALL(mock, Mark());
  EXPECT_CALL(mock, ChildDone(ChildWithStatus(child1, WithError("foo"))));
  EXPECT_CALL(mock, Mark());
  EXPECT_CALL(mock, ChildDone(ChildWithStatus(child2, absl::OkStatus())));
  EXPECT_CALL(mock, Mark());
  EXPECT_CALL(mock, ChildDone(ChildWithStatus(child3, WithError("bar"))));
  EXPECT_CALL(mock, Mark());

  mock.Mark();
  child1->Return(Failure("foo"));  // First child failure doesn't fail parent.
  EXPECT_THAT(&sync_, StillDoesNotBecomeDone());
  EXPECT_TRUE(sync_.task()->IsActive()) << "Parent should not have failed.";
  mock.Mark();
  child2->Return(absl::OkStatus());  // ChildDone seen immediately.
  // We should be waiting for all child tasks to complete, but still have
  // called Return(OK) on parent.
  EXPECT_FALSE(sync_.task()->IsActive()) << "Parent should have succeeded.";
  EXPECT_THAT(&sync_, StillDoesNotBecomeDone())
      << "Parent should wait for child3";

  ASSERT_TRUE(AwaitNotificationOrFail(child3_cancelled.get()))
      << "First success should cancel remaining children.";
  mock.Mark();
  child3->Return(Failure("bar"));
  mock.Mark();
  EXPECT_THAT(&sync_, BecomesDone(absl::OkStatus()));
}

TEST_P(ChildTaskBarrierTest, AddChildTaskWithExecutor) {
  std::unique_ptr<thread::Executor> executor(thread::NewInlineExecutor());
  Task* task = barrier_->AddChildTaskWithExecutor(executor.get());
  barrier_.reset();

  ASSERT_EQ(executor.get(), task->executor());
  task->Return();
  EXPECT_THAT(&sync_, BecomesDone(absl::OkStatus()));
}

TEST_P(ChildTaskBarrierTest, AddChildTaskWithCallbackAndExecutor) {
  std::unique_ptr<thread::Executor> executor(thread::NewInlineExecutor());
  BarrierMock<ChildTaskBarrier> mock;
  Task* child1 =
      mock.AddChildWithCallbackAndExecutor(barrier_.get(), executor.get());
  Task* child2 =
      mock.AddChildWithCallbackAndExecutor(barrier_.get(), executor.get());
  barrier_.reset();

  EXPECT_CALL(mock, ChildDone(ChildWithStatus(child1, absl::OkStatus())));
  EXPECT_CALL(mock, ChildDone(ChildWithStatus(child2, absl::OkStatus())));

  child1->Return();
  child2->Return();
  EXPECT_THAT(&sync_, BecomesDone(absl::OkStatus()));
}

TEST_P(ChildTaskBarrierTest, ReturnOnSiblings) {
  Task* child1 = barrier_->AddChildTask();
  Task* child2 = barrier_->AddChildTaskWithCallback(
      [=](Task* child2) { child1->Return(absl::OkStatus()); });

  barrier_.reset();
  child2->Return(absl::OkStatus());
  EXPECT_THAT(&sync_, BecomesDone(absl::OkStatus()));
}

TEST_P(NeglectedChildTaskBarrierTest, AddChildTaskWithExecutor) {
  std::unique_ptr<thread::Executor> executor(thread::NewInlineExecutor());
  Task* task = barrier_->AddChildTaskWithExecutor(executor.get());
  barrier_.reset();

  ASSERT_EQ(executor.get(), task->executor());
  task->Return();
  EXPECT_THAT(&sync_, BecomesDone(absl::OkStatus()));
}

TEST_P(NeglectedChildTaskBarrierTest, AddChildTaskWithCallbackAndExecutor) {
  std::unique_ptr<thread::Executor> executor(thread::NewInlineExecutor());
  BarrierMock<NeglectedChildTaskBarrier> mock;
  Task* child1 =
      mock.AddChildWithCallbackAndExecutor(barrier_.get(), executor.get());
  Task* child2 =
      mock.AddChildWithCallbackAndExecutor(barrier_.get(), executor.get());
  barrier_.reset();

  EXPECT_CALL(mock, ChildDone(ChildWithStatus(child1, absl::OkStatus())));
  EXPECT_CALL(mock, ChildDone(ChildWithStatus(child2, absl::OkStatus())));

  child1->Return();
  child2->Return();
  EXPECT_THAT(&sync_, BecomesDone(absl::OkStatus()));
}

TEST_P(RedundantChildTaskBarrierTest, AddChildTaskWithExecutor) {
  std::unique_ptr<thread::Executor> executor(thread::NewInlineExecutor());
  Task* task = barrier_->AddChildTaskWithExecutor(executor.get());
  barrier_.reset();

  ASSERT_EQ(executor.get(), task->executor());
  task->Return();
  EXPECT_THAT(&sync_, BecomesDone(absl::OkStatus()));
}

TEST_P(RedundantChildTaskBarrierTest, AddChildTaskWithCallbackAndExecutor) {
  std::unique_ptr<thread::Executor> executor(thread::NewInlineExecutor());
  BarrierMock<RedundantChildTaskBarrier> mock;
  Task* child1 =
      mock.AddChildWithCallbackAndExecutor(barrier_.get(), executor.get());
  Task* child2 =
      mock.AddChildWithCallbackAndExecutor(barrier_.get(), executor.get());
  barrier_.reset();

  EXPECT_CALL(mock, ChildDone(ChildWithStatus(child1, absl::OkStatus())));
  EXPECT_CALL(mock, ChildDone(ChildWithStatus(child2, absl::OkStatus())));

  child1->Return();
  child2->Return();
  EXPECT_THAT(&sync_, BecomesDone(absl::OkStatus()));
}

#define INSTANTIATE_CHILD_TASK_BARRIER_TEST(name)                              \
  INSTANTIATE_TEST_SUITE_P(name##ForExecutors, name##Test,                     \
                           Combine(Values(thread::Executor::DefaultExecutor(), \
                                          thread::SingletonInlineExecutor()),  \
                                   Bool()));

INSTANTIATE_CHILD_TASK_BARRIER_TEST(ChildTaskBarrier);
INSTANTIATE_CHILD_TASK_BARRIER_TEST(NeglectedChildTaskBarrier);
INSTANTIATE_CHILD_TASK_BARRIER_TEST(RedundantChildTaskBarrier);

#undef INSTANTIATE_CHILD_TASK_BARRIER_TEST

}  // namespace
}  // namespace util
