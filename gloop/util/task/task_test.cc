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

#include "gloop/util/task/task.h"

#include <stdio.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/functional/any_invocable.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "benchmark/benchmark.h"
#include "gloop/base/context.h"
#include "gloop/base/tracecontext.h"
#include "gloop/perftools/tracing/mock_trace_event_listener.h"
#include "gloop/perftools/tracing/string_label.h"
#include "gloop/perftools/tracing/with_trace_event_listener.h"
#include "gloop/thread/executor.h"
#include "gloop/thread/threadpool.h"
#include "gloop/util/functional/with_context.h"
#include "gloop/util/gtl/unique_array.h"
#include "gloop/util/refcount/reference_counted.h"
#include "gloop/util/status/error_space.h"
#include "gloop/util/status/status.h"
#include "gmock/gmock.h"
#include "google/protobuf/arena.h"
#include "gtest/gtest.h"

ABSL_FLAG(int32_t, task_test_timeout, 100,
          "Timeouts in ms for util/task/task_test");

namespace {

using ::perftools::tracing::MockTraceEventListener;
using ::perftools::tracing::StringRef;
using ::perftools::tracing::WithTraceEventListener;
using ::testing::_;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::WithArgs;

TEST(Task, PrintSize) {
  absl::FPrintF(stderr, "Task size is %d\n", sizeof(util::Task));
}
}  // namespace

// Error space for testing Return()
class MyErrorSpace : public util::ErrorSpaceImpl<MyErrorSpace> {
 public:
  static absl::string_view space_name() { return "myerrors"; }

  static std::string code_to_string(int code) {
    return absl::StrFormat("error(%d)", code);
  }

  static absl::StatusCode canonical_code(int code) {
    return absl::StatusCode::kUnknown;
  }
};

// Wait for one "tick"
static void Delay() {
  absl::SleepFor(absl::Milliseconds(absl::GetFlag(FLAGS_task_test_timeout)));
}

namespace {
// Class that sets a variable when destroyed
class DeleteMarker {
 private:
  absl::Notification* n_;

 public:
  explicit DeleteMarker(absl::Notification* n) : n_(n) {}
  ~DeleteMarker() { n_->Notify(); }
};

// ReferenceCounted Class that sets a variable when destroyed
class UnrefMarker : public util::ReferenceCounted {
 private:
  absl::Notification* n_;

 public:
  explicit UnrefMarker(absl::Notification* n) : n_(n) {}
  ~UnrefMarker() override { n_->Notify(); }
};

// Executor which verifies that Add() is not invoked
// after a barrier method has been called.  Used to
// test that a task does not access its executor after
// the done callback has started running.
class CheckedExecutor : public thread::Executor {
 public:
  explicit CheckedExecutor(thread::Executor* delegate)
      : delegate_(delegate), disabled_(false) {}

  // This type is neither copyable nor movable.
  CheckedExecutor(const CheckedExecutor&) = delete;
  CheckedExecutor& operator=(const CheckedExecutor&) = delete;

  // Causes future calls to Add() to crash.
  void Disable() {
    absl::MutexLock l(mu_);
    disabled_ = true;
  }

  void Schedule(absl::AnyInvocable<void() &&> callback) override {
    {
      absl::MutexLock l(mu_);
      CHECK(!disabled_);
    }
    delegate_->Schedule(std::move(callback));
  }

  bool TrySchedule(absl::AnyInvocable<void() &&> callback) override {
    LOG(FATAL);
  }
  void ScheduleAt(absl::Time when,
                  absl::AnyInvocable<void() &&> callback) override {
    LOG(FATAL);
  }
  int num_pending_closures() const override { LOG(FATAL); }

 private:
  thread::Executor* const delegate_;
  absl::Mutex mu_;
  bool disabled_;
};

// A wrapper that forces a task to block inside its done callback
// until explicitly released.
class TaskTester {
 private:
  CheckedExecutor checked_exec_;
  util::Task task_;
  bool continued_closure_;
  absl::Notification n0_;
  absl::Notification n1_;
  absl::Notification n2_;

  void Callback(util::Task* task) {
    DCHECK(!task->IsActive());
    DCHECK(task->IsDone());
    checked_exec_.Disable();
    n0_.Notify();
    n1_.WaitForNotification();
    n2_.Notify();
  }

 public:
  TaskTester()
      : checked_exec_(nullptr),
        task_([this](util::Task* task) { Callback(task); }) {
    continued_closure_ = false;
  }

  explicit TaskTester(thread::Executor* executor)
      : checked_exec_(executor),
        task_(absl::bind_front(&TaskTester::Callback, this), &checked_exec_) {
    continued_closure_ = false;
  }

  enum ConstructFunctionType { kConstructWithCallback };
  TaskTester(ConstructFunctionType /* unused */, thread::Executor* executor)
      : checked_exec_(nullptr),
        task_(absl::bind_front(&TaskTester::Callback, this), executor) {
    continued_closure_ = false;
  }

  virtual ~TaskTester() {
    if (!continued_closure_) n1_.Notify();
    n2_.WaitForNotification();
  }

  util::Task* task() { return &task_; }

  void WaitForClosureToStart() { n0_.WaitForNotification(); }

  void ContinueClosure() {
    if (!continued_closure_) {
      continued_closure_ = true;
      n1_.Notify();
    }
  }

  void WaitForClosureToFinish() { n2_.WaitForNotification(); }

  const absl::Status& status() const { return task_.status(); }
};

class TraceDeletion {
  uint64_t* rpc_id_ptr_;
  absl::Time* deadline_ptr_;

 public:
  TraceDeletion(uint64_t* rpc_id_ptr, absl::Time* deadline_ptr)
      : rpc_id_ptr_(rpc_id_ptr), deadline_ptr_(deadline_ptr) {}
  ~TraceDeletion() {
    *rpc_id_ptr_ = base::CurrentContext().trace_context().rpc_id();
    *deadline_ptr_ = base::CurrentContext().deadline();
  }
};

}  // namespace

// Helper method
static void Notify(absl::Notification* n, util::Task* task) { n->Notify(); }

// Run all the tests three times:
// - with the default executor (a thread pool)
// - with an inline executor, which has different deadlock scenarios.
// - with an inline done callback
// - with an inline done callback and a thread pool, so we can check
//   executor usage on inline_done_callback code paths

class InlineTaskTester : public TaskTester {
 public:
  InlineTaskTester() : TaskTester(thread::SingletonInlineExecutor()) {
    ContinueClosure();
  }
};

// We run a pass through the test cases using TaskTester::Callback to define the
// initial task.
class InlineFunctionTaskTester : public TaskTester {
 public:
  InlineFunctionTaskTester()
      : TaskTester(TaskTester::kConstructWithCallback,
                   thread::SingletonInlineExecutor()) {
    ContinueClosure();
  }
};

// Exercises the constructor and paths used by SyncTask.
class InlineDoneCallbackTaskTester : public TaskTester {
 public:
  InlineDoneCallbackTaskTester() {
    task()->set_inline_done_callback(true);
    ContinueClosure();
  }
};

class InlineDoneCallbackThreadPoolTaskTester : public TaskTester {
 public:
  InlineDoneCallbackThreadPoolTaskTester()
      : TaskTester(thread::Executor::DefaultExecutor()) {
    task()->set_inline_done_callback(true);
    ContinueClosure();
  }
};

template <class Tester>
class TaskTest : public testing::Test {
 protected:
  TaskTest() : pool_(1) {}

  void SetUp() override {}
  void TearDown() override {
    // Make sure trace context was restored.
  }

  ThreadPool pool_;
};

typedef testing::Types<TaskTester, InlineTaskTester, InlineFunctionTaskTester,
                       InlineDoneCallbackTaskTester,
                       InlineDoneCallbackThreadPoolTaskTester>
    TesterTypes;
TYPED_TEST_SUITE(TaskTest, TesterTypes);

template <class TypeParam>
void WaitForReturnCallback(TypeParam* s, absl::Notification* n) {
  s->WaitForClosureToStart();
  ASSERT_TRUE(!s->task()->IsActive());
  ASSERT_TRUE(s->task()->IsDone());
  ASSERT_TRUE(!s->task()->CancelRequested());

  s->ContinueClosure();

  n->Notify();
}

TYPED_TEST(TaskTest, StateChanges) {
  TypeParam s;
  ASSERT_TRUE(s.task()->IsActive());
  ASSERT_TRUE(!s.task()->IsDone());
  ASSERT_TRUE(!s.task()->CancelRequested());

  absl::Notification n;
  this->pool_.Schedule(
      absl::bind_front(WaitForReturnCallback<TypeParam>, &s, &n));

  s.task()->Return();
  ASSERT_TRUE(!s.task()->IsActive());

  n.WaitForNotification();
}

TYPED_TEST(TaskTest, DeleteWhenDone) {
  absl::Notification n1;
  absl::Notification n2;
  TypeParam s;
  s.task()->DeleteWhenDone(new int(10));
  s.task()->DeleteWhenDone(new std::string("foo"));
  s.task()->DeleteWhenDone(new DeleteMarker(&n1));
  s.task()->UnrefWhenDone(new UnrefMarker(&n2));

  ASSERT_FALSE(n1.HasBeenNotified());
  ASSERT_FALSE(n2.HasBeenNotified());
  s.task()->Return();
  s.WaitForClosureToStart();
  ASSERT_TRUE(n1.WaitForNotificationWithTimeout(
      absl::Milliseconds(absl::GetFlag(FLAGS_task_test_timeout))));
  ASSERT_TRUE(n2.WaitForNotificationWithTimeout(
      absl::Milliseconds(absl::GetFlag(FLAGS_task_test_timeout))));
}

TYPED_TEST(TaskTest, DeleteWhenDoneReturnsPtr) {
  TypeParam s;
  int* ptr = new int(10);
  EXPECT_EQ(ptr, s.task()->DeleteWhenDone(ptr));

  s.task()->Return();
}

TYPED_TEST(TaskTest, DeleteWhenDoneUniquePtr) {
  absl::Notification n1;
  TypeParam s;
  s.task()->DeleteWhenDone(std::make_unique<int>(10));
  s.task()->DeleteWhenDone(std::make_unique<std::string>("foo"));
  s.task()->DeleteWhenDone(std::make_unique<DeleteMarker>(&n1));

  ASSERT_FALSE(n1.HasBeenNotified());
  s.task()->Return();
  s.WaitForClosureToStart();
  ASSERT_TRUE(n1.WaitForNotificationWithTimeout(
      absl::Milliseconds(absl::GetFlag(FLAGS_task_test_timeout))));
}

TYPED_TEST(TaskTest, DeleteWhenDoneUniquePtrReturnsPtr) {
  TypeParam s;
  auto unique = std::make_unique<int>(10);
  int* ptr = unique.get();
  EXPECT_EQ(ptr, s.task()->DeleteWhenDone(std::move(unique)));

  s.task()->Return();
}

TYPED_TEST(TaskTest, Return) {
  TypeParam s;
  s.task()->Return(::util::MakeStatus(MyErrorSpace::Get(), 1, "message"));
  ASSERT_TRUE(!s.task()->IsActive());
  s.WaitForClosureToStart();
  ASSERT_EQ(::util::RetrieveErrorCode(s.status()), 1);
  ASSERT_EQ(s.status().message(), std::string("message"));
}

TYPED_TEST(TaskTest, ReturnFunctor) {
  TypeParam s;
  util::TaskReturn(s.task())(
      ::util::MakeStatus(MyErrorSpace::Get(), 1, "message"));
  ASSERT_TRUE(!s.task()->IsActive());
  s.WaitForClosureToStart();
  ASSERT_EQ(::util::RetrieveErrorCode(s.status()), 1);
  ASSERT_EQ(s.status().message(), std::string("message"));
}

TYPED_TEST(TaskTest, MultipleReturn) {
  TypeParam s;
  s.task()->Return(::util::MakeStatus(MyErrorSpace::Get(), 1, "message"));
  s.task()->Return(::util::MakeStatus(MyErrorSpace::Get(), 2, "msg2"));
  ASSERT_TRUE(!s.task()->IsActive());
  s.WaitForClosureToStart();
  ASSERT_EQ(::util::RetrieveErrorCode(s.status()), 1);
  ASSERT_EQ(s.status().message(), std::string("message"));
}

TYPED_TEST(TaskTest, Callback) {
  TypeParam s;
  s.task()->Return();
  s.WaitForClosureToStart();
}

TYPED_TEST(TaskTest, ReturnBlocksPrepare) {
  TypeParam s;
  Delay();
  ASSERT_TRUE(s.task()->IsActive());
  s.task()->Return();
  ASSERT_TRUE(!s.task()->IsActive());
}

TYPED_TEST(TaskTest, DoneCallbackBlocksExit) {
  absl::Notification n;
  TypeParam s;
  s.task()->DeleteWhenDone(new DeleteMarker(&n));
  ASSERT_FALSE(n.HasBeenNotified());
  s.task()->Return();
  s.WaitForClosureToStart();
  Delay();
  s.ContinueClosure();
  s.WaitForClosureToFinish();
  ASSERT_TRUE(n.WaitForNotificationWithTimeout(
      absl::Milliseconds(absl::GetFlag(FLAGS_task_test_timeout))));
}

// ------------------------------------------------------------
// Cancellation

TYPED_TEST(TaskTest, Cancel) {
  TypeParam s;
  ASSERT_TRUE(!s.task()->CancelRequested());
  s.task()->Cancel();
  ASSERT_TRUE(s.task()->CancelRequested());
  s.task()->Return();
}

TYPED_TEST(TaskTest, CancelAfterReturn) {
  TypeParam s;
  ASSERT_TRUE(!s.task()->CancelRequested());
  s.task()->Return();
  s.task()->Cancel();  // Should have no effect
  ASSERT_TRUE(!s.task()->CancelRequested());
}

TYPED_TEST(TaskTest, MultipleCancels) {
  TypeParam s;
  ASSERT_TRUE(!s.task()->CancelRequested());
  s.task()->Cancel();
  s.task()->Cancel();  // Should have no effect
  ASSERT_TRUE(s.task()->CancelRequested());
  s.task()->Return();
}

TYPED_TEST(TaskTest, CancelFunctionCallback) {
  int v = 0;
  absl::Notification n;
  TypeParam s;
  s.task()->WhenCancelled([&v, &n]() {
    v = 1;
    n.Notify();
  });
  Delay();
  ASSERT_EQ(0, v);
  s.task()->Cancel();
  n.WaitForNotification();
  ASSERT_EQ(1, v);
  s.task()->Return();
}

// We used to have a bug where the CancelCallback() was called
// even if the client did not call Cancel().
TYPED_TEST(TaskTest, CancelCallbackNotCalled) {
  int v = 0;
  TypeParam s;
  s.task()->WhenCancelled([&v]() { v = 2; });

  s.task()->AddHold();
  s.task()->Return();
  s.task()->RemoveHold();

  Delay();
  ASSERT_EQ(0, v);
}

template <class TypeParam>
void WaitForCancelAndReturn(absl::Notification* n1, TypeParam* s,
                            absl::Notification* w) {
  n1->WaitForNotification();
  s->task()->Return();
  Delay();
  ASSERT_TRUE(!s->task()->IsDone());
  w->Notify();
}

// We run these as separate tests since an inline executor may have been used.
TYPED_TEST(TaskTest, CancelFunctionBlocksDone) {
  absl::Notification n, w;
  TypeParam s;
  s.task()->WhenCancelled([&n, &w]() {
    n.Notify();
    w.WaitForNotification();
  });
  this->pool_.Schedule(
      absl::bind_front(WaitForCancelAndReturn<TypeParam>, &n, &s, &w));
  s.task()->Cancel();
}

TYPED_TEST(TaskTest, CancelCallbackDeleteOnReturn) {
  int v = 0;
  TypeParam s;
  s.task()->WhenCancelled([&v]() { v = 2; });
  s.task()->Return();
  Delay();
  ASSERT_EQ(0, v);
}

TYPED_TEST(TaskTest, CancelCallbackDeletedAfterReturn) {
  int v = 0;
  TypeParam s;
  s.task()->Return();
  s.task()->WhenCancelled([&v]() { v = 2; });
  Delay();
  ASSERT_EQ(0, v);
}

TYPED_TEST(TaskTest, CancelCallbackAfterCancel) {
  absl::Notification n2;
  TypeParam s;
  s.task()->Cancel();
  s.task()->WhenCancelled([&n2]() { n2.Notify(); });
  Delay();
  s.task()->Return();
  ASSERT_TRUE(n2.WaitForNotificationWithTimeout(
      absl::Milliseconds(absl::GetFlag(FLAGS_task_test_timeout))));
}

TYPED_TEST(TaskTest, ReturnStillRunsCancelCallbacks) {
  absl::Notification n2;
  TypeParam s;
  s.task()->WhenCancelled([&n2]() { n2.Notify(); });
  s.task()->Cancel();
  s.task()->Return();
  ASSERT_TRUE(n2.WaitForNotificationWithTimeout(
      absl::Milliseconds(absl::GetFlag(FLAGS_task_test_timeout))));
}

static void Increment(std::atomic<int32_t>* x) {
  // Multiple WhenCancelled callbacks are allowed to execute in parallel.
  x->fetch_add(1, std::memory_order_relaxed);
}

TYPED_TEST(TaskTest, MultipleCancelCallbacksCancelFunctionCallback) {
  std::atomic<int32_t> v{0};
  TypeParam s;
  const int kRuns = 3;
  for (int i = 0; i < kRuns; ++i) {
    s.task()->WhenCancelled(std::bind(Increment, &v));
  }
  s.task()->Cancel();
  s.task()->Return();
  Delay();
  s.ContinueClosure();
  s.WaitForClosureToFinish();
  ASSERT_EQ(kRuns, v.load(std::memory_order_relaxed));
}

void CheckExecutorAndNotify(ThreadPool* tp, absl::Notification* n,
                            util::Task* task) {
  CHECK(thread::Executor::CurrentExecutor() == tp);
  // So we can use this for a cancel callback
  task->Return();
  if (n != nullptr) {
    n->Notify();
  }
}

TYPED_TEST(TaskTest, CancelExecutor) {
  ThreadPool* tp = new ThreadPool(1);
  absl::Notification notify;

  util::Task* task =
      new util::Task(absl::bind_front(CheckExecutorAndNotify, tp, &notify), tp);
  task->WhenCancelled(
      absl::bind_front(CheckExecutorAndNotify, tp, nullptr, task));
  ASSERT_EQ(tp, task->executor());
  task->Cancel();
  notify.WaitForNotification();
  delete task;
}

TYPED_TEST(TaskTest, MultiCancel) {
  static const int N = 100;
  int value[N];
  absl::Notification notify[N];
  TypeParam s;
  for (int i = 0; i < N; i++) {
    s.task()->WhenCancelled([&notify, &value, i] {
      value[i] = i + 1;
      notify[i].Notify();
    });
  }
  s.task()->Cancel();
  for (int i = 0; i < N; i++) {
    notify[i].WaitForNotification();
    EXPECT_EQ(i + 1, value[i]);
  }
  s.task()->Return();
}

TYPED_TEST(TaskTest, CancelCancels) {
  TypeParam s;
  s.task()->WhenCancelled(absl::bind_front(&util::Task::Cancel, s.task()));
  s.task()->Cancel();
  s.task()->Return();
}

// -----------------------------------------------------------------
// WhenPrepared()

TYPED_TEST(TaskTest, PreparedCallback) {
  absl::Notification n;
  TypeParam s;
  s.task()->WhenPrepared([&n] { n.Notify(); });
  s.task()->Return();
  ASSERT_TRUE(n.WaitForNotificationWithTimeout(
      absl::Milliseconds(absl::GetFlag(FLAGS_task_test_timeout))));
}

TYPED_TEST(TaskTest, MultiplePreparedCallback) {
  absl::Notification n2;
  TypeParam s;
  s.task()->WhenPrepared([&n2]() { n2.Notify(); });
  s.task()->Return();
  ASSERT_TRUE(n2.WaitForNotificationWithTimeout(
      absl::Milliseconds(absl::GetFlag(FLAGS_task_test_timeout))));
}

TYPED_TEST(TaskTest, PreparedCallbacksWithHold) {
  absl::Notification n2;
  TypeParam s;
  s.task()->WhenPrepared([&n2]() { n2.Notify(); });

  s.task()->AddHold();
  s.task()->Return();
  s.task()->RemoveHold();
  ASSERT_TRUE(n2.WaitForNotificationWithTimeout(
      absl::Milliseconds(absl::GetFlag(FLAGS_task_test_timeout))));
}

// The prototypical use for TaskForce.
TYPED_TEST(TaskTest, PreparedCallbackReleasingHold) {
  TypeParam s;
  s.task()->WhenPrepared(std::bind(&util::Task::RemoveHold, s.task()));

  s.task()->AddHold();
  s.task()->Return();
}

TYPED_TEST(TaskTest, PreparedCallbackWithCancel) {
  absl::Notification n2;
  TypeParam s;
  s.task()->WhenPrepared([&n2]() { n2.Notify(); });

  s.task()->Cancel();
  s.task()->Return();
  ASSERT_TRUE(n2.WaitForNotificationWithTimeout(
      absl::Milliseconds(absl::GetFlag(FLAGS_task_test_timeout))));
}

// NewCallback(task, &util::Task::Return) cannot resolve the
// overloaded Return(), so we need another way to name the method.
static void ReturnTask(util::Task* task) { task->Return(); }

TYPED_TEST(TaskTest, PreparedCallbackWithCancelCausingReturn) {
  absl::Notification n;
  TypeParam s;
  s.task()->WhenPrepared([&n] { n.Notify(); });
  s.task()->WhenCancelled(absl::bind_front(&ReturnTask, s.task()));

  s.task()->Cancel();
  ASSERT_TRUE(n.WaitForNotificationWithTimeout(
      absl::Milliseconds(absl::GetFlag(FLAGS_task_test_timeout))));
}

// Resolve overloaded functions.
static void WhenPreparedCallback(util::Task* task,
                                 absl::AnyInvocable<void() &&> func) {
  task->WhenPrepared(std::move(func));
}

static void WhenCancelledCallback(util::Task* task,
                                  absl::AnyInvocable<void() &&> callback) {
  task->WhenCancelled(std::move(callback));
}

TYPED_TEST(TaskTest, AddPreparedCallbackDuringPrepare) {
  absl::Notification n2;
  TypeParam s;
  s.task()->WhenPrepared(
      std::bind(WhenPreparedCallback, s.task(), [&n2]() { n2.Notify(); }));

  s.task()->Return();
  ASSERT_TRUE(n2.WaitForNotificationWithTimeout(
      absl::Milliseconds(absl::GetFlag(FLAGS_task_test_timeout))));
}

TYPED_TEST(TaskTest, AddCancelledCallbackDuringPrepare) {
  int v = 0;
  TypeParam s;
  s.task()->WhenPrepared(
      std::bind(WhenCancelledCallback, s.task(), [&v]() { v = 2; }));

  s.task()->Return();
  ASSERT_EQ(0, v);
}

TYPED_TEST(TaskTest, AddPreparedCallbackDuringCancel) {
  absl::Notification n2;
  TypeParam s;
  s.task()->WhenCancelled(
      std::bind(WhenPreparedCallback, s.task(), [&n2]() { n2.Notify(); }));
  s.task()->Cancel();

  // This delay is necessary to ensure that WhenPrepared() happens
  // before s.task() is deleted.
  Delay();
  s.task()->Return();
  ASSERT_TRUE(n2.WaitForNotificationWithTimeout(
      absl::Milliseconds(absl::GetFlag(FLAGS_task_test_timeout))));
}

TYPED_TEST(TaskTest, AddPreparedBeforeDone) {
  absl::Notification n2, n4;
  TypeParam s;
  s.task()->WhenPrepared([&n2]() { n2.Notify(); });

  s.task()->AddHold();
  s.task()->Return();

  Delay();

  s.task()->WhenPrepared([&n4]() { n4.Notify(); });
  s.task()->RemoveHold();
  ASSERT_TRUE(n2.WaitForNotificationWithTimeout(
      absl::Milliseconds(absl::GetFlag(FLAGS_task_test_timeout))));
  ASSERT_TRUE(n4.WaitForNotificationWithTimeout(
      absl::Milliseconds(absl::GetFlag(FLAGS_task_test_timeout))));
}

// -----------------------------------------------------------------
// Context

TYPED_TEST(TaskTest, DeletionHappensInSameContext) {
  absl::Time deadline = absl::Now() + absl::Minutes(1);
  TraceContext new_trace(base::CurrentContext().trace_context());
  new_trace.set_rpc_id(5);
  base::WithContext wc(base::ContextBuilder(base::CurrentContext())
                           .set_trace_context(new_trace)
                           .set_deadline(deadline)
                           .BuildValue());

  absl::Notification n;
  uint64_t rpc_id_in_delete = 1;
  absl::Time deadline_in_delete;

  util::Task task_function(absl::bind_front(Notify, &n),
                           thread::SingletonInlineExecutor());
  task_function.DeleteWhenDone(
      new TraceDeletion(&rpc_id_in_delete, &deadline_in_delete));

  task_function.Return();
  n.WaitForNotification();
  Delay();
  ASSERT_EQ(5, rpc_id_in_delete);
  ASSERT_EQ(deadline, deadline_in_delete);
}

void Nop(util::Task*) {}

TYPED_TEST(TaskTest, ConstructorCapturesContext) {
  // The Task constructor accepting AnyInvocable captures the current context.
  absl::Time expected_deadline = absl::Now() + absl::Minutes(1);
  absl::Time actual_deadline;
  auto cb = [&actual_deadline](::util::Task* task) {
    actual_deadline = base::CurrentContext().deadline();
  };
  std::unique_ptr<util::Task> task;
  {
    base::WithContext w(base::ContextBuilder(base::BackgroundContext())
                            .set_deadline(expected_deadline)
                            .BuildValue());
    task = std::make_unique<util::Task>(cb, thread::SingletonInlineExecutor());
  }
  task->Return();
  EXPECT_EQ(expected_deadline, actual_deadline);
}

TYPED_TEST(TaskTest, AddChildCapturesContext) {
  // The AddChild (and AddChildWithExecutor) variant accepting AnyInvocable
  // captures the current context.
  util::Task task(&Nop, thread::SingletonInlineExecutor());
  absl::Time expected_child1_deadline = absl::Now() + absl::Minutes(1);
  absl::Time expected_child2_deadline = absl::Now() + absl::Minutes(2);
  absl::Time actual_child1_deadline;
  absl::Time actual_child2_deadline;
  auto child1_cb = [&actual_child1_deadline](::util::Task* child) {
    actual_child1_deadline = base::CurrentContext().deadline();
  };
  auto child2_cb = [&actual_child2_deadline](::util::Task* child) {
    actual_child2_deadline = base::CurrentContext().deadline();
  };

  util::Task* child1;
  {
    base::WithContext w(base::ContextBuilder(base::BackgroundContext())
                            .set_deadline(expected_child1_deadline)
                            .BuildValue());
    child1 = task.AddChild(child1_cb);
  }
  util::Task* child2;
  {
    base::WithContext w(base::ContextBuilder(base::BackgroundContext())
                            .set_deadline(expected_child2_deadline)
                            .BuildValue());
    child2 =
        task.AddChildWithExecutor(child2_cb, thread::SingletonInlineExecutor());
  }

  child1->Return();
  child2->Return();
  task.Return();
  EXPECT_EQ(expected_child1_deadline, actual_child1_deadline);
  EXPECT_EQ(expected_child2_deadline, actual_child2_deadline);
}

TEST(TaskTest, ChildChainSharesArena) {
  std::vector<util::Task*> to_return;
  google::protobuf::Arena arena;
  EXPECT_EQ(0, arena.SpaceUsed());
  int counter = 0;
  auto on_return = [&counter](::util::Task* child) { ++counter; };
  util::Task task(on_return, thread::SingletonInlineExecutor(), &arena);
  EXPECT_EQ(0, arena.SpaceUsed());

  auto* child1 = task.AddChild(on_return);
  const auto space_used_after_child1 = arena.SpaceUsed();
  EXPECT_GT(space_used_after_child1, 0);

  auto* child11 = child1->AddChild(on_return);
  const auto space_used_after_child2 = arena.SpaceUsed();
  EXPECT_GT(space_used_after_child2, space_used_after_child1);

  to_return.push_back(&task);
  to_return.push_back(child1);
  to_return.push_back(child11);

  for (auto I = to_return.rbegin(); I != to_return.rend(); ++I) {
    (*I)->Return();
  }
  EXPECT_EQ(3, counter);
}

TEST(TaskHold, TaskHold) {
  util::Task task(&Nop, thread::SingletonInlineExecutor());

  std::unique_ptr<util::TaskHold> outer_hold;
  {
    util::TaskHold hold(&task);
    task.Return();
    ASSERT_FALSE(task.IsDone());
    outer_hold = std::make_unique<util::TaskHold>(std::move(hold));
    // Tests destruction of hold with a nullptr task.
  }
  // outer_hold is still holding onto task.
  ASSERT_FALSE(task.IsDone());
  {
    util::TaskHold hold(&task);
    ASSERT_FALSE(task.IsDone());
    // Tests move-assignment with a non-nullptr task on hold.
    hold = std::move(*outer_hold);
  }
  // At the end of the block the remaining hold is released and the Nop callback
  // is run.
  ASSERT_TRUE(task.IsDone());
}

// --------------------------------------------------------------
// Death tests

TYPED_TEST(TaskTest, WhenPreparedDone) {
  TypeParam s;

  s.task()->Return();
  EXPECT_DEATH_IF_SUPPORTED({ s.task()->WhenPrepared([] {}); }, ".*");
}

// --------------------------------------------------------------
// Benchmarks
static void ExecutorAdd(thread::Executor* exec, benchmark::State* state,
                        absl::Notification* notify) {
  if (!state->KeepRunning()) {
    notify->Notify();
  } else {
    auto cb = absl::bind_front(ExecutorAdd, exec, state, notify);
    exec->Schedule(cb);
  }
}

static void BM_DefaultExecutorCallback(benchmark::State& state) {
  absl::Notification notify;

  thread::Executor* exec = thread::Executor::DefaultExecutor();
  exec->Schedule(absl::bind_front(ExecutorAdd, exec, &state, &notify));
  notify.WaitForNotification();
}
BENCHMARK(BM_DefaultExecutorCallback);

static void BM_SingleThreadedCallback(benchmark::State& state) {
  absl::Notification notify;
  ThreadPool* tp = new ThreadPool(1);
  tp->Schedule(absl::bind_front(ExecutorAdd, tp, &state, &notify));
  notify.WaitForNotification();
  delete tp;
}
BENCHMARK(BM_SingleThreadedCallback);

static void NextTask(benchmark::State* state, util::Task* task,
                     util::Task* child) {
  delete child;
  if (!state->KeepRunning()) {
    task->Return();
  } else {
    child = new util::Task(absl::bind_front(NextTask, state, task),
                           task->executor());
    child->Return();
  }
}
static void BM_Task(benchmark::State& state) {
  ThreadPool* tp = new ThreadPool(1);

  TaskTester s;
  s.task()->set_executor(tp);
  NextTask(&state, s.task(), nullptr);
}
BENCHMARK(BM_Task);

static void NextTaskWithDeletions(benchmark::State* state, util::Task* task,
                                  util::Task* child) {
  delete child;
  if (!state->KeepRunning()) {
    task->Return();
  } else {
    child = new util::Task(absl::bind_front(NextTaskWithDeletions, state, task),
                           task->executor());
    for (int i = 0; i < state->range(0); i++) {
      child->DeleteWhenDone(new int);
    }
    child->Return();
  }
}
static void BM_TaskWithDeletions(benchmark::State& state) {
  ThreadPool* tp = new ThreadPool(1);

  TaskTester s;
  s.task()->set_executor(tp);
  NextTaskWithDeletions(&state, s.task(), nullptr);
}
BENCHMARK(BM_TaskWithDeletions)->Arg(0)->Arg(1)->Arg(4)->Arg(16);

static void NextTaskWhenCancelled(benchmark::State* state, bool cancel_child,
                                  util::Task* task, util::Task* child) {
  delete child;
  if (!state->KeepRunning()) {
    task->Return();
  } else {
    child = new util::Task(
        absl::bind_front(NextTaskWhenCancelled, state, cancel_child, task),
        task->executor());
    for (int i = 0; i < state->range(0); i++) {
      child->WhenCancelled([] {});
    }
    if (cancel_child) child->Cancel();
    child->Return();
  }
}
static void BM_TaskWhenCancelledCancel(benchmark::State& state) {
  ThreadPool* tp = new ThreadPool(1);

  TaskTester s;
  s.task()->set_executor(tp);
  NextTaskWhenCancelled(&state, true, s.task(), nullptr);
}
BENCHMARK(BM_TaskWhenCancelledCancel)->Arg(0)->Arg(1)->Arg(4)->Arg(16);

static void BM_TaskWhenCancelledNoCancel(benchmark::State& state) {
  ThreadPool* tp = new ThreadPool(1);

  TaskTester s;
  s.task()->set_executor(tp);
  NextTaskWhenCancelled(&state, false, s.task(), nullptr);
}
BENCHMARK(BM_TaskWhenCancelledNoCancel)->Arg(0)->Arg(1)->Arg(4)->Arg(16);

static void NextTaskWhenPrepared(benchmark::State* state, util::Task* task,
                                 util::Task* child) {
  delete child;
  if (!state->KeepRunning()) {
    task->Return();
  } else {
    child = new util::Task(absl::bind_front(NextTaskWhenPrepared, state, task),
                           task->executor());
    for (int i = 0; i < state->range(0); i++) {
      child->WhenPrepared([] {});
    }
    child->Return();
  }
}
static void BM_TaskWhenPrepared(benchmark::State& state) {
  ThreadPool* tp = new ThreadPool(1);

  TaskTester s;
  s.task()->set_executor(tp);
  NextTaskWhenPrepared(&state, s.task(), nullptr);
}
BENCHMARK(BM_TaskWhenPrepared)->Arg(0)->Arg(1)->Arg(4)->Arg(16);

static void BM_Hold(benchmark::State& state) {
  util::Task task(&Nop, thread::SingletonInlineExecutor());
  while (state.KeepRunning()) {
    util::TaskHold hold(&task);
    benchmark::DoNotOptimize(hold);
  }
  task.Return();
}
BENCHMARK(BM_Hold);

static void BM_Arena(benchmark::State& state) {
  static const size_t kInitialBufferSize = 4096;
  auto sufficient_buffer =
      gtl::MakeUniqueArrayForOverwrite<char>(kInitialBufferSize);
  google::protobuf::Arena arena(sufficient_buffer.data(), kInitialBufferSize);

  // Meant to be run with --benchmark_memory_usage. Some allocations should
  // still be present because of the executor, but disabling arena-based
  // allocation by passing nullptr for it in the Task ctor would result in 2
  // extra allocations.
  while (state.KeepRunning()) {
    util::Task task([](::util::Task* child) {},
                    thread::SingletonInlineExecutor(), &arena);
    auto* child = task.AddChild([](::util::Task* child) {});
    auto* child2 = child->AddChild([](::util::Task* child) {});
    child2->Return();
    child->Return();
    task.Return();
  }
}

BENCHMARK(BM_Arena);
