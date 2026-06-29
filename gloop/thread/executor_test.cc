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

#include "gloop/thread/executor.h"

#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/functional/any_invocable.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "benchmark/benchmark.h"
#include "gloop/base/callback.h"
#include "gloop/base/context.h"
#include "gloop/base/mock_tracer.h"
#include "gloop/base/tracecontext.h"
#include "gloop/thread/sync_queue.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/threadpool.h"
#include "gloop/util/functional/from_callback.h"
#include "gloop/util/functional/to_callback.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace thread_internal {
thread::Executor* DefaultThreadPoolExecutor();
}

namespace thread {

struct AddCancellableTestCase {
  bool use_absolute_time = false;
  bool use_legacy_callback = false;
};

class AddCancellableTest
    : public ::testing::TestWithParam<AddCancellableTestCase> {
 public:
  static void SetUpTestSuite() {
    dummy_closure_ = util::functional::ToPermanentCallback([] {});
    executor_ = new ThreadPool(1);
  }

 protected:
  void AddCancellableHelper(Executor* const executor,
                            const absl::Duration delay,
                            absl::AnyInvocable<void() &&> callback,
                            ExecutorHandle* const h) {
    if (GetParam().use_absolute_time) {
      if (GetParam().use_legacy_callback) {
        AddCancellableAt(executor, absl::Now() + delay,
                         util::functional::ToCallback(std::move(callback)), h);
      } else {
        AddCancellableAt(executor, absl::Now() + delay, std::move(callback), h);
      }
    } else {
      if (GetParam().use_legacy_callback) {
        AddCancellable(executor, delay,
                       util::functional::ToCallback(std::move(callback)), h);
      } else {
        AddCancellable(executor, delay, std::move(callback), h);
      }
    }
  }

  void AddCancellableHelperWithRepeatableCallback(Executor* const executor,
                                                  const absl::Duration delay,
                                                  Closure* const closure,
                                                  ExecutorHandle* const h) {
    CHECK(closure->IsRepeatable())
        << "AddCancellableHelperWithRepeatableCallback only accepts "
           "repeatable callbacks.";
    CHECK(GetParam().use_legacy_callback)
        << "AddCancellableHelperWithRepeatableCallback does not support "
           "AnyInvocable";
    if (GetParam().use_absolute_time) {
      AddCancellableAt(executor, absl::Now() + delay, closure, h);
    } else {
      AddCancellable(executor, delay, closure, h);
    }
  }

 protected:
  static ThreadPool* executor_;
  static Closure* dummy_closure_;
};

ThreadPool* AddCancellableTest::executor_ = nullptr;
Closure* AddCancellableTest::dummy_closure_ = nullptr;

TEST_P(AddCancellableTest, NoCancellationNoDelay) {
  ExecutorHandle h;
  absl::Notification n;
  AddCancellableHelper(
      executor_, absl::ZeroDuration(), [&n] { n.Notify(); }, &h);
  n.WaitForNotification();
}

TEST_P(AddCancellableTest, NoCancellationWithDelay) {
  ExecutorHandle h;
  absl::Notification n;
  absl::Time before = absl::Now();
  AddCancellableHelper(
      executor_, absl::Milliseconds(20), [&n] { n.Notify(); }, &h);
  n.WaitForNotification();
  absl::Time after = absl::Now();
  EXPECT_GE(after - before, absl::Milliseconds(20));
}

TEST_P(AddCancellableTest, CancelledDuringDelay) {
  ExecutorHandle h;
  absl::Notification n;
  // 3 minutes so the unit test times out before this call.
  AddCancellableHelper(executor_, absl::Minutes(3), [&n] { n.Notify(); }, &h);
  Closure* cb = nullptr;
  EXPECT_EQ(Cancel(h, absl::ZeroDuration(), &cb), CancelResult::kCancelled);
  EXPECT_NE(cb, nullptr);
  EXPECT_FALSE(n.HasBeenNotified());
  delete cb;
}

TEST_P(AddCancellableTest, PropagatesContextToTask) {
  ExecutorHandle h;
  absl::Notification n;
  std::string task_thread_status = "uninitialized";
  {
    base::WithThreadStatus ts("expected_task_thread_status");
    AddCancellableHelper(
        executor_, absl::Milliseconds(20),
        [&n, &task_thread_status] {
          task_thread_status = base::CurrentThreadStatus();
          n.Notify();
        },
        &h);
  }
  n.WaitForNotification();
  EXPECT_EQ(task_thread_status, "expected_task_thread_status");
}

namespace {

struct Notifications {
  absl::Notification start;
  absl::Notification wait;
  absl::Notification finish;
};

void StartWaitFinish(Notifications* n) {
  n->start.Notify();
  n->wait.WaitForNotification();
  n->finish.Notify();
}

}  // namespace

TEST_P(AddCancellableTest, CancelWillTimeout) {
  for (int i = 0; i < 20; ++i) {
    ExecutorHandle h;
    Notifications n;
    AddCancellableHelper(
        executor_, absl::ZeroDuration(), [&n] { StartWaitFinish(&n); }, &h);
    n.start.WaitForNotification();

    Closure* cb = dummy_closure_;
    EXPECT_EQ(Cancel(h, absl::Milliseconds(i), &cb), CancelResult::kRunning);
    EXPECT_EQ(cb, nullptr);
    n.wait.Notify();
    n.finish.WaitForNotification();
  }
}

TEST_P(AddCancellableTest, CancelWaitsForFinish) {
  for (int i = 0; i < 20; ++i) {
    ExecutorHandle h;
    Notifications n;
    AddCancellableHelper(
        executor_, absl::ZeroDuration(), [&n] { StartWaitFinish(&n); }, &h);
    n.start.WaitForNotification();

    // We are not guaranteed to get the interleaving we want (cancel before the
    // end thread calls notify), but we are fairly likely too.  Regardless, they
    // look the same from the outside.
    ClosureThread let_it_end(
        absl::bind_front(&absl::Notification::Notify, &n.wait));
    let_it_end.SetJoinable(true);
    let_it_end.Start();

    Closure* cb = dummy_closure_;
    EXPECT_EQ(Cancel(h, absl::InfiniteDuration(), &cb),
              CancelResult::kNotScheduled);
    EXPECT_EQ(cb, nullptr);
    let_it_end.Join();
    n.finish.WaitForNotification();
  }
}

TEST_P(AddCancellableTest, CancelAfterFinish) {
  for (int i = 0; i < 20; ++i) {
    ExecutorHandle h;
    absl::Notification n;
    AddCancellableHelper(
        executor_, absl::ZeroDuration(), [&n] { n.Notify(); }, &h);
    n.WaitForNotification();

    // Sleep a tiny bit to make it more likely we get the interleave we want.
    absl::SleepFor(absl::Milliseconds(1));

    // If the notification is descheduled before the destructor finishes (but
    // after it notifies), cancel with a timeout of zero would fail and return
    // false.  We pass kBlock to avoid that tiny race.
    Closure* cb;
    EXPECT_EQ(Cancel(h, absl::InfiniteDuration(), &cb),
              CancelResult::kNotScheduled);
    EXPECT_EQ(cb, nullptr);
  }
}

TEST_P(AddCancellableTest, MultipleCancellations) {
  // Multiple cancellations with same handle
  ExecutorHandle h;
  absl::Notification n;
  AddCancellableHelper(executor_, absl::Minutes(3), [&n] { n.Notify(); }, &h);

  Closure* cb;
  EXPECT_EQ(Cancel(h, absl::ZeroDuration(), &cb), CancelResult::kCancelled);
  EXPECT_NE(cb, nullptr);
  delete cb;
  EXPECT_FALSE(n.HasBeenNotified());

  EXPECT_EQ(Cancel(h, absl::ZeroDuration(), &cb), CancelResult::kNotScheduled);
  EXPECT_EQ(cb, nullptr);
  EXPECT_FALSE(n.HasBeenNotified());
}

// Verify that a scheduled AnyInvocable can run to completion.
TEST(AddCancellableAt, AnyInvocableRuns) {
  ThreadPool executor{/*num_threads=*/1};
  ExecutorHandle h;
  absl::Notification done;

  AddCancellableAt(&executor, absl::Now(), [&] { done.Notify(); }, &h);

  done.WaitForNotification();
}

// Verify that a scheduled AnyInvocable can get cancelled.
TEST(AddCancellableAt, AnyInvocableCancels) {
  ThreadPool executor{/*num_threads=*/1};
  ExecutorHandle h;
  absl::Notification done;

  // Schedule the callback to run in a while so we get a chance to cancel it,
  // and cancel it immediately.
  AddCancellableAt(
      &executor, absl::Now() + absl::Hours(10), [&] { done.Notify(); }, &h);
  EXPECT_EQ(Cancel(h, absl::ZeroDuration()), CancelResult::kCancelled);

  EXPECT_FALSE(done.HasBeenNotified());
}

// Verify that a scheduled AnyInvocable scheduled immediately can get cancelled.
TEST(AddCancellableAt, AnyInvocableImmediateCancels) {
  absl::Notification hogger_done;
  ThreadPool executor{/*num_threads=*/1};
  executor.Schedule([&] { hogger_done.WaitForNotification(); });

  ExecutorHandle h;
  absl::Notification done;

  AddCancellable(&executor, [&] { done.Notify(); }, &h);
  EXPECT_EQ(Cancel(h, absl::ZeroDuration()), CancelResult::kCancelled);

  EXPECT_FALSE(done.HasBeenNotified());
  hogger_done.Notify();
}

// Verify that the result of the Closure-returning overload of Cancel is a
// wrapper around the AnyInvocable.
TEST(AddCancellableAt, ClosureWrapsAnyInvocable) {
  ThreadPool executor{/*num_threads=*/1};
  ExecutorHandle h;
  absl::Notification done;

  // Schedule the callback to run in a while so we get a chance to cancel it,
  // and cancel it immediately.
  Closure* cb = nullptr;
  AddCancellableAt(
      &executor, absl::Now() + absl::Hours(10), [&] { done.Notify(); }, &h);
  EXPECT_EQ(Cancel(h, absl::ZeroDuration(), &cb), CancelResult::kCancelled);

  ASSERT_FALSE(done.HasBeenNotified());
  ASSERT_NE(cb, nullptr);

  // Now run the closure and verify that it wraps our original callback.
  cb->Run();
  EXPECT_TRUE(done.HasBeenNotified());
}

// Verify that a scheduled AnyInvocable can run to completion.
TEST(AddCancellable, AnyInvocableRuns) {
  ThreadPool executor{/*num_threads=*/1};
  ExecutorHandle h;
  absl::Notification done;

  AddCancellable(&executor, [&] { done.Notify(); }, &h);

  done.WaitForNotification();
}

// Verify that a scheduled AnyInvocable can get cancelled.
TEST(AddCancellable, AnyInvocableCancels) {
  ThreadPool executor{/*num_threads=*/1};
  ExecutorHandle h;
  absl::Notification done;

  // Schedule the callback to run in a while so we get a chance to cancel it,
  // and cancel it immediately.
  AddCancellable(&executor, absl::Hours(10), [&] { done.Notify(); }, &h);
  EXPECT_EQ(Cancel(h, absl::ZeroDuration()), CancelResult::kCancelled);

  EXPECT_FALSE(done.HasBeenNotified());
}

// Verify that the result of the Closure-returning overload of Cancel is a
// wrapper around the AnyInvocable.
TEST(AddCancellable, ClosureWrapsAnyInvocable) {
  ThreadPool executor{/*num_threads=*/1};
  ExecutorHandle h;
  absl::Notification done;

  // Schedule the callback to run in a while so we get a chance to cancel it,
  // and cancel it immediately.
  Closure* cb = nullptr;
  AddCancellable(&executor, absl::Hours(10), [&] { done.Notify(); }, &h);
  EXPECT_EQ(Cancel(h, absl::ZeroDuration(), &cb), CancelResult::kCancelled);

  ASSERT_FALSE(done.HasBeenNotified());
  ASSERT_NE(cb, nullptr);

  // Now run the closure and verify that it wraps our original callback.
  cb->Run();
  EXPECT_TRUE(done.HasBeenNotified());
}

// Verify that the overload of Cancel that doesn't return a callback via pointer
// deletes it.
TEST(AddCancellable, CancelDeletesCallbackWhenNonReturning) {
  ThreadPool executor(/*num_threads=*/1);
  bool was_deleted = false;

  // Schedule a callback to run in a distant future so we get enough time to
  // cancel it.
  ExecutorHandle h;
  AddCancellable(&executor, absl::Hours(200),
                 util::functional::ToCallback(
                     [on_delete = absl::Cleanup{[&] {
                        was_deleted = true;
                      }}]() mutable { std::move(on_delete).Cancel(); }),
                 &h);

  EXPECT_EQ(Cancel(h, absl::InfiniteDuration()), CancelResult::kCancelled);
  EXPECT_TRUE(was_deleted);
}

TEST(Executor, CancelWithInvalidHandle) {
  ExecutorHandle handle;  // an invalid/default handle

  Closure* cb1;
  EXPECT_EQ(Cancel(handle, absl::ZeroDuration(), &cb1),
            CancelResult::kNotScheduled);
  EXPECT_TRUE(cb1 == nullptr);

  // The timeout value is irrelevant with an invalid handle.
  Closure* cb2;
  EXPECT_EQ(Cancel(handle, absl::InfiniteDuration(), &cb2),
            CancelResult::kNotScheduled);
  EXPECT_TRUE(cb2 == nullptr);
}

TEST_P(AddCancellableTest, EmptyHandle) {
  ExecutorHandle handle;  // an invalid/default handle
  EXPECT_TRUE(handle.empty());

  AddCancellableHelper(executor_, absl::Milliseconds(100), [] {}, &handle);
  EXPECT_FALSE(handle.empty());
  Closure* cb1;
  EXPECT_EQ(Cancel(handle, absl::InfiniteDuration(), &cb1),
            CancelResult::kCancelled);
  EXPECT_FALSE(handle.empty());
  delete cb1;

  handle = ExecutorHandle();
  EXPECT_TRUE(handle.empty());
  absl::Notification n;
  AddCancellableHelper(
      executor_, absl::Milliseconds(100), [&n] { n.Notify(); }, &handle);
  EXPECT_FALSE(handle.empty());
  n.WaitForNotification();
  Closure* cb2;
  EXPECT_EQ(Cancel(handle, absl::InfiniteDuration(), &cb2),
            CancelResult::kNotScheduled);
  EXPECT_EQ(cb2, nullptr);
  EXPECT_FALSE(handle.empty());
}

TEST_P(AddCancellableTest, AddCancellableWithFullQueue) {
  ThreadPool pool(1, {.queue_capacity = 5});

  // Add an initial "slow" callback.  Wait for it to start, so we know
  // it's blocking the one (and only) thread in the pool.
  Notifications n;
  pool.Schedule(absl::bind_front(&StartWaitFinish, &n));  // NOLINT
  n.start.WaitForNotification();

  // Pad the queue with no-ops until full.
  while (true) {
    if (!pool.TrySchedule([] {})) {
      break;
    }
  }

  // Test that AddCancellable() doesn't wait for the slow callback to finish,
  // despite the full queue.
  ExecutorHandle handle;
  AddCancellableHelper(&pool, absl::ZeroDuration(), [] {}, &handle);

  EXPECT_FALSE(n.finish.HasBeenNotified());
  n.wait.Notify();
  n.finish.WaitForNotification();
}

// Simple mock-like closure records the states it goes through.
class RecordingClosure : public Closure {
 public:
  RecordingClosure(bool is_repeatable, bool* ran, bool* destroyed)
      : is_repeatable_(is_repeatable), ran_(ran), destroyed_(destroyed) {}

  bool IsRepeatable() const override { return is_repeatable_; }
  void Run() override {
    *ran_ = true;
    if (!IsRepeatable()) {
      delete this;
    }
  }

  ~RecordingClosure() override { *destroyed_ = true; }

 private:
  const bool is_repeatable_;
  bool* const ran_;
  bool* const destroyed_;
};

// SaveAddAfterExecutor is used to record the AddAfter closure scheduled by
// thread::AddCancellable.
class SaveAddAfterExecutor : public Executor {
 public:
  explicit SaveAddAfterExecutor(absl::AnyInvocable<void() &&>* save_add_after)
      : save_add_after_(save_add_after) {}

  void Schedule(absl::AnyInvocable<void() &&> callback) override {
    std::move(callback)();
  }
  bool TrySchedule(absl::AnyInvocable<void() &&> callback) override {
    return false;
  }

  void ScheduleAt(absl::Time when,
                  absl::AnyInvocable<void() &&> callback) override {
    *save_add_after_ = std::move(callback);
  }

  int num_pending_closures() const override { return 0; }

 private:
  absl::AnyInvocable<void() &&>* const save_add_after_;
};

namespace internal {
extern bool IsActiveExecutorHandle(ExecutorHandle handle);
}

namespace {

class TestClosure : public Closure {
 public:
  explicit TestClosure(bool* destructor_called, bool is_permanent)
      : destructor_called_(*destructor_called), is_permanent_(is_permanent) {
    destructor_called_ = false;
  }

  bool IsRepeatable() const override { return is_permanent_; }

  ~TestClosure() override { destructor_called_ = true; }

  void Run() override { LOG(FATAL) << "should not be reached"; }

 private:
  bool& destructor_called_;
  bool is_permanent_;
};

// An executor that does nothing when any methods are called.
class DoNothingExecutor : public ::thread::Executor {
 public:
  void Schedule(absl::AnyInvocable<void() &&> callback) override {}
  bool TrySchedule(absl::AnyInvocable<void() &&> callback) override {
    return true;
  }
  void ScheduleAt(absl::Time when,
                  absl::AnyInvocable<void() &&> callback) override {}
  int num_pending_closures() const override { return 0; }
};

// An executor that captures callbacks but doesn't execute them.
class NonExecutingExecutor : public ::thread::Executor {
 public:
  void Schedule(absl::AnyInvocable<void() &&> callback) override {
    callbacks_.push(std::move(callback));
  }
  bool TrySchedule(absl::AnyInvocable<void() &&> callback) override {
    Schedule(std::move(callback));
    return true;
  }
  void ScheduleAt(absl::Time when,
                  absl::AnyInvocable<void() &&> callback) override {
    Schedule(std::move(callback));
  }
  int num_pending_closures() const override { return callbacks_.size(); }

 private:
  SyncQueue<absl::AnyInvocable<void() &&>> callbacks_;
};

}  // namespace

TEST(Executor,
     AddedPermanentCallbackIsNotDeletedIfExecutorDropsScheduledInvocable) {
  DoNothingExecutor executor;
  bool destructor_called;
  auto callback = std::make_unique<TestClosure>(&destructor_called, true);
  executor.Schedule(util::functional::FromCallback(callback.get()));
  EXPECT_FALSE(destructor_called);
}

TEST_P(AddCancellableTest, CallbackIsDeletedIfExecutorDropsScheduledInvocable) {
  DoNothingExecutor executor;
  ExecutorHandle handle;
  bool destructor_called;
  AddCancellableHelper(
      &executor, absl::ZeroDuration(),
      [c = absl::MakeCleanup(
           [&destructor_called] { destructor_called = true; })] {},
      &handle);
  EXPECT_TRUE(destructor_called);
}

TEST_P(AddCancellableTest, ExecutorDeletesWithoutRunning) {
  // This test uses an executor that captures callbacks but doesn't execute
  // them. It adds cancellable callbacks to the executor and then/ tests from
  // the main thread that you can cancel the callbacks. The executor is
  // destroyed on a different thread half way through the cancellations, to
  // verify in tsan that no references to the callbacks are kept and
  // any accesses are thread safe.
  auto executor = std::make_unique<NonExecutingExecutor>();

  static constexpr int kNumCallbacks = 1000;
  std::vector<std::unique_ptr<bool>> destructors_called;
  std::vector<ExecutorHandle> handles(kNumCallbacks);
  for (int i = 0; i < kNumCallbacks; ++i) {
    AddCancellableHelper(
        executor.get(), absl::ZeroDuration(), [] {}, &handles[i]);
  }

  absl::Notification cancelled_half;
  absl::Notification executor_deleted;
  absl::Notification deletion_started;
  thread::Executor::DefaultExecutor()->Schedule([&]() {
    deletion_started.Notify();
    cancelled_half.WaitForNotification();
    executor = nullptr;
    executor_deleted.Notify();
  });
  deletion_started.WaitForNotification();

  for (int i = 0; i < kNumCallbacks; ++i) {
    if (i == kNumCallbacks / 2) {
      cancelled_half.Notify();
    }
    thread::Cancel(handles[i], absl::InfiniteDuration());
  }

  executor_deleted.WaitForNotification();
}

// Tests for the AnyInvocable wrappers in thread::Executor.  These can be
// removed when the AnyInvocable API becomes pure virtual.
TEST(Executor, ScheduleAnyInvocable) {
  Executor* e = thread::Executor::DefaultExecutor();
  absl::Notification notify;
  e->Schedule([&notify] { notify.Notify(); });
  notify.WaitForNotification();
}

TEST(Executor, ScheduleManyAnyInvocable) {
  Executor* e = thread::Executor::DefaultExecutor();
  absl::Notification notify1;
  absl::Notification notify2;
  std::vector<absl::AnyInvocable<void() &&>> callbacks;
  callbacks.emplace_back([&notify1] { notify1.Notify(); });
  callbacks.emplace_back([&notify2] { notify2.Notify(); });
  e->ScheduleMany(absl::MakeSpan(callbacks));
  notify1.WaitForNotification();
  notify2.WaitForNotification();
}

TEST(Executor, TryScheduleAnyInvocable) {
  Executor* e = thread::Executor::DefaultExecutor();
  absl::Notification notify;
  if (e->TrySchedule([&notify] { notify.Notify(); })) {
    notify.WaitForNotification();
  }
}

TEST(Executor, ScheduleAtAnyInvocable) {
  Executor* e = thread::Executor::DefaultExecutor();
  absl::Notification notify;
  e->ScheduleAt(absl::Now() + absl::Milliseconds(100),
                [&notify] { notify.Notify(); });
  notify.WaitForNotification();
}

INSTANTIATE_TEST_SUITE_P(
    AddCancellableTest, AddCancellableTest,
    testing::ConvertGenerator(testing::Combine(testing::Bool(),
                                               testing::Bool()),
                              [](const std::tuple<bool, bool>& t) {
                                AddCancellableTestCase tc;
                                tc.use_absolute_time = std::get<0>(t);
                                tc.use_legacy_callback = std::get<1>(t);
                                return tc;
                              }),
    [](const testing::TestParamInfo<AddCancellableTest::ParamType>& info) {
      return absl::StrCat(
          info.param.use_absolute_time ? "AbsoluteTime_" : "RelativeTime_",
          info.param.use_legacy_callback ? "LegacyCallback" : "AnyInvocable");
    });

class ManyTest : public ::testing::Test {
 public:
  ManyTest() { executor_ = new ThreadPool(1); }

  void DecrementCounter(absl::BlockingCounter* c) { c->DecrementCount(); }

 protected:
  static constexpr int kManySize = 1024;
  ThreadPool* executor_;
};

TEST_F(ManyTest, ScheduleMany) {
  absl::BlockingCounter bcounter(kManySize);
  std::vector<absl::AnyInvocable<void() &&>> funcs;
  funcs.reserve(kManySize);
  for (int i = 0; i < kManySize; ++i) {
    funcs.push_back([&bcounter] { bcounter.DecrementCount(); });
  }
  executor_->ScheduleMany(absl::MakeSpan(funcs));
  bcounter.Wait();
}

void ValidateCurrentExecutorAndNotify(Executor* expected_executor,
                                      absl::Notification* notification,
                                      absl::Time* deadline) {
  EXPECT_EQ(Executor::CurrentExecutor(), expected_executor);
  notification->Notify();
  *deadline = base::CurrentContext().deadline();
}

TEST(InlineExecutorTest, Add) {
  auto executor = absl::WrapUnique(NewInlineExecutor());
  auto* current_executor = Executor::CurrentExecutor();
  const absl::Time deadline = absl::Now() + absl::Seconds(2);
  absl::Time saved_deadline = absl::InfiniteFuture();
  absl::Notification notification;
  {
    std::unique_ptr<base::Context> target_context(
        new base::Context(base::ContextBuilder(base::BackgroundContext())
                              .set_deadline(deadline)
                              .BuildValue()));
    base::WithContext with(*target_context);
    executor->Schedule(absl::bind_front(ValidateCurrentExecutorAndNotify,
                                        executor.get(), &notification,
                                        &saved_deadline));
  }
  ASSERT_TRUE(notification.HasBeenNotified());
  EXPECT_EQ(Executor::CurrentExecutor(), current_executor);
  EXPECT_EQ(saved_deadline, deadline);
}

TEST(InlineExecutorTest, Schedule) {
  auto executor = absl::WrapUnique(NewInlineExecutor());
  auto* current_executor = Executor::CurrentExecutor();
  const absl::Time deadline = absl::Now() + absl::Seconds(2);
  absl::Time saved_deadline = absl::InfiniteFuture();
  absl::Notification notification;
  {
    std::unique_ptr<base::Context> target_context(
        new base::Context(base::ContextBuilder(base::BackgroundContext())
                              .set_deadline(deadline)
                              .BuildValue()));
    base::WithContext with(*target_context);
    executor->Schedule([&notification, &executor, &saved_deadline] {
      ValidateCurrentExecutorAndNotify(executor.get(), &notification,
                                       &saved_deadline);
    });
  }
  ASSERT_TRUE(notification.HasBeenNotified());
  EXPECT_EQ(Executor::CurrentExecutor(), current_executor);
  EXPECT_EQ(saved_deadline, deadline);
}

TEST(InlineExecutorTest, TryAdd) {
  auto executor = absl::WrapUnique(NewInlineExecutor());
  auto* current_executor = Executor::CurrentExecutor();
  const absl::Time deadline = absl::Now() + absl::Seconds(2);
  absl::Time saved_deadline = absl::InfiniteFuture();
  absl::Notification notification;
  {
    std::unique_ptr<base::Context> target_context(
        new base::Context(base::ContextBuilder(base::BackgroundContext())
                              .set_deadline(deadline)
                              .BuildValue()));
    base::WithContext with(*target_context);

    EXPECT_TRUE(executor->TrySchedule(
        absl::bind_front(ValidateCurrentExecutorAndNotify, executor.get(),
                         &notification, &saved_deadline)));
  }
  ASSERT_TRUE(notification.HasBeenNotified());
  EXPECT_EQ(Executor::CurrentExecutor(), current_executor);
  EXPECT_EQ(saved_deadline, deadline);
}

TEST(InlineExecutorTest, TrySchedule) {
  auto executor = absl::WrapUnique(NewInlineExecutor());
  auto* current_executor = Executor::CurrentExecutor();
  const absl::Time deadline = absl::Now() + absl::Seconds(2);
  absl::Time saved_deadline = absl::InfiniteFuture();
  absl::Notification notification;
  {
    std::unique_ptr<base::Context> target_context(
        new base::Context(base::ContextBuilder(base::BackgroundContext())
                              .set_deadline(deadline)
                              .BuildValue()));
    base::WithContext with(*target_context);
    EXPECT_TRUE(
        executor->TrySchedule([&notification, &executor, &saved_deadline] {
          ValidateCurrentExecutorAndNotify(executor.get(), &notification,
                                           &saved_deadline);
        }));
  }
  ASSERT_TRUE(notification.HasBeenNotified());
  EXPECT_EQ(Executor::CurrentExecutor(), current_executor);
  EXPECT_EQ(saved_deadline, deadline);
}

TEST(Executor, DefaultExecutor) {
  EXPECT_EQ(Executor::DefaultExecutor(),
            thread_internal::DefaultThreadPoolExecutor());
}

static void Nothing() {}

class BenchmarkExecutor : public Executor {
 public:
  explicit BenchmarkExecutor(std::vector<Closure*>* add_after_closures)
      : add_after_closures_(add_after_closures) {}

  void Schedule(absl::AnyInvocable<void() &&> callback) override {
    std::move(callback)();
  }
  bool TrySchedule(absl::AnyInvocable<void() &&> callback) override {
    return false;
  }

  void ScheduleAt(absl::Time when,
                  absl::AnyInvocable<void() &&> callback) override {
    add_after_closures_->push_back(
        util::functional::ToCallback(std::move(callback)));
  }

  int num_pending_closures() const override { return 0; }

 private:
  std::vector<Closure*>* const add_after_closures_;
};

constexpr int kBatchSize = 100;

static void BM_AddCancellable(benchmark::State& state) {
  std::vector<Closure*> add_after_closures;
  add_after_closures.reserve(kBatchSize);
  BenchmarkExecutor executor(&add_after_closures);
  while (state.KeepRunningBatch(kBatchSize)) {
    for (int i = 0; i < kBatchSize; i++) {
      // Add the cancellable callback with a non-zero delay to avoid the
      // optimized absl::ZeroDuration() overload (http://shortn/_GPkyw8iANb).
      //
      // TODO: revisit this and consider benchmarking both
      // overloads.
      ExecutorHandle handle;
      AddCancellable(&executor, absl::Milliseconds(1), [] {}, &handle);
    }
    // Run all the closures to clean up (do not time it).
    state.PauseTiming();
    CHECK_EQ(kBatchSize, add_after_closures.size());
    for (int i = 0; i < add_after_closures.size(); ++i) {
      add_after_closures[i]->Run();
    }
    add_after_closures.clear();
    state.ResumeTiming();
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AddCancellable)->ThreadRange(1, 16);

static void BM_Cancel(benchmark::State& state) {
  std::vector<Closure*> add_after_closures;
  add_after_closures.reserve(kBatchSize);
  BenchmarkExecutor executor(&add_after_closures);
  while (state.KeepRunningBatch(kBatchSize)) {
    for (int i = 0; i < kBatchSize; i++) {
      // Prepare a closure to cancel (do not time it).
      state.PauseTiming();

      // Add the cancellable callback with a non-zero delay to avoid the
      // optimized absl::ZeroDuration() overload (http://shortn/_GPkyw8iANb).
      //
      // TODO: revisit this and consider benchmarking both
      // overloads.
      ExecutorHandle handle;
      AddCancellable(&executor, absl::Milliseconds(1), [] {}, &handle);

      state.ResumeTiming();
      TryCancel(handle);
    }
    // Run all the closures to clean up (do not time it).
    state.PauseTiming();
    CHECK_EQ(kBatchSize, add_after_closures.size());
    for (int i = 0; i < add_after_closures.size(); ++i) {
      add_after_closures[i]->Run();
    }
    add_after_closures.clear();
    state.ResumeTiming();
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Cancel)->ThreadRange(1, 16);

static void BM_RunUncancelledClosures(benchmark::State& state) {
  std::vector<Closure*> add_after_closures;
  add_after_closures.reserve(kBatchSize);
  BenchmarkExecutor executor(&add_after_closures);
  while (state.KeepRunningBatch(kBatchSize)) {
    // Prepare a batch of cancellable closures to run (do not time it).
    state.PauseTiming();
    for (int i = 0; i < kBatchSize; i++) {
      // Add the cancellable callback with a non-zero delay to avoid the
      // optimized absl::ZeroDuration() overload (http://shortn/_GPkyw8iANb).
      //
      // TODO: revisit this and consider benchmarking both
      // overloads.
      ExecutorHandle handle;
      AddCancellable(&executor, absl::Milliseconds(1), [] {}, &handle);
    }
    state.ResumeTiming();
    for (int i = 0; i < kBatchSize; ++i) {
      add_after_closures[i]->Run();
    }
    CHECK_EQ(kBatchSize, add_after_closures.size());
    add_after_closures.clear();
  }

  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_RunUncancelledClosures)->ThreadRange(1, 16);

static void BM_RunCancelledClosures(benchmark::State& state) {
  std::vector<Closure*> add_after_closures;
  add_after_closures.reserve(kBatchSize);
  BenchmarkExecutor executor(&add_after_closures);
  while (state.KeepRunningBatch(kBatchSize)) {
    // Prepare a batch of cancellable closures to run (do not time it).
    state.PauseTiming();
    for (int i = 0; i < kBatchSize; i++) {
      // Add the cancellable callback with a non-zero delay to avoid the
      // optimized absl::ZeroDuration() overload (http://shortn/_GPkyw8iANb).
      //
      // TODO: revisit this and consider benchmarking both
      // overloads.
      ExecutorHandle handle;
      AddCancellable(&executor, absl::Milliseconds(1), [] {}, &handle);

      TryCancel(handle);
    }
    CHECK_EQ(kBatchSize, add_after_closures.size());
    state.ResumeTiming();
    for (int i = 0; i < add_after_closures.size(); ++i) {
      add_after_closures[i]->Run();
    }
    add_after_closures.clear();
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_RunCancelledClosures)->ThreadRange(1, 16);

}  // namespace thread
