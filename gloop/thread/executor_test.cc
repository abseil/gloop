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
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/functional/any_invocable.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "benchmark/benchmark.h"
#include "gloop/base/callback.h"
#include "gloop/base/context.h"
#include "gloop/thread/sync_queue.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/threadpool.h"
#include "gloop/util/functional/from_callback.h"
#include "gloop/util/functional/to_callback.h"
#include "gtest/gtest.h"

ABSL_DECLARE_FLAG(bool, thread_executor_checkfail_on_permanent_callbacks);

namespace thread_internal {
thread::Executor* DefaultThreadPoolExecutor();
}

namespace thread {

class AddCancellableTest : public ::testing::TestWithParam<bool> {
 public:
  static void SetUpTestSuite() {
    dummy_closure_ = util::functional::ToPermanentCallback([] {});
    executor_ = new ThreadPool(1);
  }

 protected:
  void AddCancellableHelper(Executor* const executor,
                            const absl::Duration delay, Closure* const closure,
                            ExecutorHandle* const h) {
    if (use_absolute_time_) {
      return AddCancellableAt(executor, absl::Now() + delay, closure, h);
    }

    return AddCancellable(executor, delay, closure, h);
  }

 protected:
  static ThreadPool* executor_;
  static Closure* dummy_closure_;

 private:
  const bool use_absolute_time_ = GetParam();
};

ThreadPool* AddCancellableTest::executor_ = nullptr;
Closure* AddCancellableTest::dummy_closure_ = nullptr;

TEST_P(AddCancellableTest, NoCancellationNoDelay) {
  ExecutorHandle h;
  absl::Notification n;
  AddCancellableHelper(executor_, absl::ZeroDuration(),
                       ::util::functional::ToCallback([&n] { n.Notify(); }),
                       &h);
  n.WaitForNotification();
}

TEST_P(AddCancellableTest, NoCancellationWithDelay) {
  ExecutorHandle h;
  absl::Notification n;
  absl::Time before = absl::Now();
  AddCancellableHelper(executor_, absl::Milliseconds(20),
                       ::util::functional::ToCallback([&n] { n.Notify(); }),
                       &h);
  n.WaitForNotification();
  absl::Time after = absl::Now();
  EXPECT_GE(after - before, absl::Milliseconds(20));
}

TEST_P(AddCancellableTest, CancelledDuringDelay) {
  ExecutorHandle h;
  absl::Notification n;
  // 3 minutes so the unit test times out before this call.
  AddCancellableHelper(executor_, absl::Minutes(3),
                       ::util::functional::ToCallback([&n] { n.Notify(); }),
                       &h);
  Closure* cb = nullptr;
  bool cancelled = Cancel(h, absl::ZeroDuration(), &cb);
  EXPECT_TRUE(cancelled);
  EXPECT_NE(cb, nullptr);
  EXPECT_FALSE(n.HasBeenNotified());
  delete cb;
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
        executor_, absl::ZeroDuration(),
        ::util::functional::ToCallback([&n] { StartWaitFinish(&n); }), &h);
    n.start.WaitForNotification();

    Closure* cb = dummy_closure_;
    bool cancelled = Cancel(h, absl::Milliseconds(i), &cb);
    EXPECT_FALSE(cancelled);
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
        executor_, absl::ZeroDuration(),
        ::util::functional::ToCallback([&n] { StartWaitFinish(&n); }), &h);
    n.start.WaitForNotification();

    // We are not guaranteed to get the interleaving we want (cancel before the
    // end thread calls notify), but we are fairly likely too.  Regardless, they
    // look the same from the outside.
    ClosureThread let_it_end(
        absl::bind_front(&absl::Notification::Notify, &n.wait));
    let_it_end.SetJoinable(true);
    let_it_end.Start();

    Closure* cb = dummy_closure_;
    bool cancelled = Cancel(h, absl::InfiniteDuration(), &cb);
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(cb, nullptr);
    let_it_end.Join();
    n.finish.WaitForNotification();
  }
}

TEST_P(AddCancellableTest, CancelAfterFinish) {
  for (int i = 0; i < 20; ++i) {
    ExecutorHandle h;
    absl::Notification n;
    AddCancellableHelper(executor_, absl::ZeroDuration(),
                         ::util::functional::ToCallback([&n] { n.Notify(); }),
                         &h);
    n.WaitForNotification();

    // Sleep a tiny bit to make it more likely we get the interleave we want.
    absl::SleepFor(absl::Milliseconds(1));

    // If the notification is descheduled before the destructor finishes (but
    // after it notifies), cancel with a timeout of zero would fail and return
    // false.  We pass kBlock to avoid that tiny race.
    Closure* cb;
    bool cancelled = Cancel(h, absl::InfiniteDuration(), &cb);
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(cb, nullptr);
  }
}

TEST_P(AddCancellableTest, MultipleCancellations) {
  // Multiple cancellations with same handle
  ExecutorHandle h;
  absl::Notification n;
  AddCancellableHelper(executor_, absl::Minutes(3),
                       ::util::functional::ToCallback([&n] { n.Notify(); }),
                       &h);

  Closure* cb;
  EXPECT_TRUE(Cancel(h, absl::ZeroDuration(), &cb));
  EXPECT_NE(cb, nullptr);
  delete cb;
  EXPECT_FALSE(n.HasBeenNotified());

  EXPECT_TRUE(Cancel(h, absl::ZeroDuration(), &cb));
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
  ASSERT_TRUE(Cancel(h, absl::ZeroDuration()));

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
  ASSERT_TRUE(Cancel(h, absl::ZeroDuration()));

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
  ASSERT_TRUE(Cancel(h, absl::ZeroDuration(), &cb));

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
  ASSERT_TRUE(Cancel(h, absl::ZeroDuration()));

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
  ASSERT_TRUE(Cancel(h, absl::ZeroDuration(), &cb));

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

  ASSERT_TRUE(Cancel(h, absl::InfiniteDuration()));
  EXPECT_TRUE(was_deleted);
}

TEST(Executor, CancelWithInvalidHandle) {
  ExecutorHandle handle;  // an invalid/default handle

  Closure* cb1;
  EXPECT_TRUE(Cancel(handle, absl::ZeroDuration(), &cb1));
  EXPECT_TRUE(cb1 == nullptr);

  // The timeout value is irrelevant with an invalid handle.
  Closure* cb2;
  EXPECT_TRUE(Cancel(handle, absl::InfiniteDuration(), &cb2));
  EXPECT_TRUE(cb2 == nullptr);
}

TEST_P(AddCancellableTest, EmptyHandle) {
  ExecutorHandle handle;  // an invalid/default handle
  EXPECT_TRUE(handle.empty());

  Closure* cb1 = util::functional::ToCallback([] {});
  AddCancellableHelper(executor_, absl::Milliseconds(100), cb1, &handle);
  EXPECT_FALSE(handle.empty());
  EXPECT_TRUE(Cancel(handle, absl::InfiniteDuration(), &cb1));
  EXPECT_FALSE(handle.empty());
  delete cb1;

  handle = ExecutorHandle();
  EXPECT_TRUE(handle.empty());
  absl::Notification n;
  Closure* cb2 = util::functional::ToCallback([&n] { n.Notify(); });
  AddCancellableHelper(executor_, absl::Milliseconds(100), cb2, &handle);
  EXPECT_FALSE(handle.empty());
  n.WaitForNotification();
  EXPECT_TRUE(Cancel(handle, absl::InfiniteDuration(), &cb2));
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
  AddCancellableHelper(&pool, absl::ZeroDuration(),
                       util::functional::ToCallback([] {}), &handle);

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
  bool destructor_called;
  ExecutorHandle handle;
  AddCancellableHelper(&executor, absl::ZeroDuration(),
                       new TestClosure(&destructor_called, false), &handle);
  EXPECT_TRUE(destructor_called);
}

TEST_P(AddCancellableTest,
       PermanentCallbackIsNotDeletedIfExecutorDropsScheduledInvocable) {
  // TODO: replace the permanent callback in this test with a
  // non-permanent one or delete this once we remove the flag
  absl::SetFlag(&FLAGS_thread_executor_checkfail_on_permanent_callbacks, false);

  DoNothingExecutor executor;
  bool destructor_called;
  auto callback = std::make_unique<TestClosure>(&destructor_called, true);
  ExecutorHandle handle;
  AddCancellableHelper(&executor, absl::ZeroDuration(), callback.get(),
                       &handle);
  EXPECT_FALSE(destructor_called);
}

TEST_P(AddCancellableTest, ExecutorDeletesWithoutRunning) {
  // TODO: replace the permanent callback in this test with a
  // non-permanent one or delete this once we remove the flag
  absl::SetFlag(&FLAGS_thread_executor_checkfail_on_permanent_callbacks, false);

  // This test uses an executor that captures callbacks but doesn't execute
  // them. It adds cancellable, permanent callbacks to the executor and then
  // tests from the main thread that you can delete the permanent callbacks
  // after cancelling them. The executor is destroyed on a different thread half
  // way through the cancellations, to verify in tsan that no references to the
  // permanent callbacks are kept and any accesses are thread safe.
  auto executor = std::make_unique<NonExecutingExecutor>();

  static constexpr int kNumCallbacks = 1000;
  std::vector<std::unique_ptr<bool>> destructors_called;
  std::vector<std::unique_ptr<TestClosure>> callbacks;
  for (int i = 0; i < kNumCallbacks; ++i) {
    destructors_called.emplace_back(std::make_unique<bool>(false));
    callbacks.emplace_back(
        std::make_unique<TestClosure>(destructors_called[i].get(), true));
  }
  std::vector<ExecutorHandle> handles(kNumCallbacks);
  for (int i = 0; i < kNumCallbacks; ++i) {
    AddCancellableHelper(executor.get(), absl::ZeroDuration(),
                         callbacks[i].get(), &handles[i]);
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
    Closure* cb = nullptr;
    thread::Cancel(handles[i], absl::InfiniteDuration(), &cb);
    callbacks[i].reset();
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

// Tests that a closure passed to AddCancellable will execute in the Context
// captured at its creation.
//
// There are three threads: the test's main thread, and two threads in the
// ThreadPool. The main thread does set up and verification: it builds a
// context, sets a deadline, creates a closure, passes the closure to
// AddCancellable, then waits for it to execute. The closure executes on the
// ThreadPool, and saves the deadline of its Context. The main thread then
// verifies that the saved deadline and the deadline from the set-up phase are
// the same.
TEST_P(AddCancellableTest, ContextPropagation) {
  ThreadPool e(2);

  const absl::Time deadline = absl::Now() + absl::Seconds(2);
  absl::Time saved_deadline = absl::InfiniteFuture();

  absl::Notification notification;
  // This closure will be created with a context whose deadline is set to
  // "deadline". When it executes, it should run in the same context as that of
  // its creation.
  Closure* closure;
  {
    std::unique_ptr<base::Context> target_context(
        new base::Context(base::ContextBuilder(base::BackgroundContext())
                              .set_deadline(deadline)
                              .BuildValue()));
    base::WithContext with(*target_context);
    closure = util::functional::ToCallback(
        [&notification, &saved_deadline]() mutable {
          saved_deadline = base::CurrentContext().deadline();
          notification.Notify();
        });
  }

  EXPECT_NE(deadline, base::CurrentContext().deadline());
  EXPECT_EQ(deadline, closure->context_ptr()->deadline());
  EXPECT_EQ(absl::InfiniteFuture(), saved_deadline);

  ExecutorHandle handle;
  AddCancellableHelper(&e, absl::ZeroDuration(), closure, &handle);

  // Wait for closure to execute and delete itself.
  CHECK(notification.WaitForNotificationWithTimeout(absl::Seconds(5)));

  EXPECT_EQ(deadline, saved_deadline);
}

INSTANTIATE_TEST_SUITE_P(AddCancellableTest, AddCancellableTest,
                         ::testing::Bool());

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
  // TODO: replace the permanent callback in this test with a
  // non-permanent one or delete this once we remove the flag
  absl::SetFlag(&FLAGS_thread_executor_checkfail_on_permanent_callbacks, false);

  std::vector<Closure*> add_after_closures;
  add_after_closures.reserve(kBatchSize);
  BenchmarkExecutor executor(&add_after_closures);
  Closure* cb = util::functional::ToPermanentCallback(Nothing);
  while (state.KeepRunningBatch(kBatchSize)) {
    for (int i = 0; i < kBatchSize; i++) {
      ExecutorHandle handle;
      AddCancellable(&executor, absl::ZeroDuration(), cb, &handle);
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
  delete cb;
}
BENCHMARK(BM_AddCancellable)->ThreadRange(1, 16);

static void BM_Cancel(benchmark::State& state) {
  // TODO: replace the permanent callback in this test with a
  // non-permanent one or delete this once we remove the flag
  absl::SetFlag(&FLAGS_thread_executor_checkfail_on_permanent_callbacks, false);

  std::vector<Closure*> add_after_closures;
  add_after_closures.reserve(kBatchSize);
  BenchmarkExecutor executor(&add_after_closures);
  Closure* cb = util::functional::ToPermanentCallback(Nothing);
  while (state.KeepRunningBatch(kBatchSize)) {
    for (int i = 0; i < kBatchSize; i++) {
      // Prepare a closure to cancel (do not time it).
      state.PauseTiming();
      ExecutorHandle handle;
      AddCancellable(&executor, absl::ZeroDuration(), cb, &handle);
      state.ResumeTiming();
      Closure* cb;
      Cancel(handle, absl::ZeroDuration(), &cb);
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
  delete cb;
}
BENCHMARK(BM_Cancel)->ThreadRange(1, 16);

static void BM_RunUncancelledClosures(benchmark::State& state) {
  // TODO: replace the permanent callback in this test with a
  // non-permanent one or delete this once we remove the flag
  absl::SetFlag(&FLAGS_thread_executor_checkfail_on_permanent_callbacks, false);

  std::vector<Closure*> add_after_closures;
  add_after_closures.reserve(kBatchSize);
  BenchmarkExecutor executor(&add_after_closures);
  Closure* cb = util::functional::ToPermanentCallback(Nothing);
  while (state.KeepRunningBatch(kBatchSize)) {
    // Prepare a batch of cancellable closures to run (do not time it).
    state.PauseTiming();
    for (int i = 0; i < kBatchSize; i++) {
      ExecutorHandle handle;
      AddCancellable(&executor, absl::ZeroDuration(), cb, &handle);
    }
    state.ResumeTiming();
    for (int i = 0; i < kBatchSize; ++i) {
      add_after_closures[i]->Run();
    }
    CHECK_EQ(kBatchSize, add_after_closures.size());
    add_after_closures.clear();
  }

  state.SetItemsProcessed(state.iterations());
  delete cb;
}
BENCHMARK(BM_RunUncancelledClosures)->ThreadRange(1, 16);

static void BM_RunCancelledClosures(benchmark::State& state) {
  // TODO: replace the permanent callback in this test with a
  // non-permanent one or delete this once we remove the flag
  absl::SetFlag(&FLAGS_thread_executor_checkfail_on_permanent_callbacks, false);

  std::vector<Closure*> add_after_closures;
  add_after_closures.reserve(kBatchSize);
  BenchmarkExecutor executor(&add_after_closures);
  Closure* cb = util::functional::ToPermanentCallback(Nothing);
  while (state.KeepRunningBatch(kBatchSize)) {
    // Prepare a batch of cancellable closures to run (do not time it).
    state.PauseTiming();
    for (int i = 0; i < kBatchSize; i++) {
      ExecutorHandle handle;
      AddCancellable(&executor, absl::ZeroDuration(), cb, &handle);
      Closure* cb;
      Cancel(handle, absl::ZeroDuration(), &cb);
    }
    CHECK_EQ(kBatchSize, add_after_closures.size());
    state.ResumeTiming();
    for (int i = 0; i < add_after_closures.size(); ++i) {
      add_after_closures[i]->Run();
    }
    add_after_closures.clear();
  }
  state.SetItemsProcessed(state.iterations());
  delete cb;
}
BENCHMARK(BM_RunCancelledClosures)->ThreadRange(1, 16);

}  // namespace thread
