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

// A test for thread_manager.cc

#include "gloop/thread/thread_manager.h"

#include <stdio.h>
#include <unistd.h>

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "absl/base/config.h"
#include "absl/base/dynamic_annotations.h"
#include "absl/base/macros.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "benchmark/benchmark.h"
#include "gloop/base/callback.h"
#include "gloop/base/per_thread.h"
#include "gloop/thread/executor.h"
#include "gloop/thread/thread_manager_policy.h"
#include "gloop/thread/threadlocal.h"
#include "gloop/thread/threadpool.h"
#include "gloop/thread/watchdog.h"
#include "gloop/util/functional/from_callback.h"
#include "gloop/util/functional/to_callback.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AllOf;
using testing::ElementsAre;
using testing::Eq;
using testing::Field;
using testing::IsEmpty;

TEST(ThreadManagerTest, DeleteWithoutWork) {
  thread::ThreadManager* tm =
      new thread::ThreadManager("test0", thread::ManagerOptions());
  delete tm;
}

static void NotifyThenWait(absl::Notification* to_notify,
                           absl::Notification* to_wait) {
  to_notify->Notify();
  to_wait->WaitForNotification();
}

static void WaitThenNotify(absl::Notification* to_wait,
                           absl::Notification* to_notify) {
  to_wait->WaitForNotification();
  to_notify->Notify();
}

TEST(ThreadManagerTest, Add) {
  thread::ThreadManager* tm =
      new thread::ThreadManager("test1", thread::ManagerOptions());
  thread::ManagedQueue* queue =
      tm->NewQueue("default", thread::ManagedQueueOptions());
  CHECK_EQ(queue->name(), "default");
  absl::Notification started;
  absl::Notification done;
  queue->Schedule(absl::bind_front(&NotifyThenWait, &started, &done));
  WaitThenNotify(&started, &done);
  delete queue;
  delete tm;
}

TEST(ThreadManagerTest, TrySchedule) {
  {
    thread::ManagedQueue* queue = thread::DefaultQueue();
    absl::Notification started[10];
    absl::Notification done[ABSL_ARRAYSIZE(started)];
    // TrySchedule on default queue always succeeds
    for (int i = 0; i != ABSL_ARRAYSIZE(started); i++) {
      CHECK(queue->TrySchedule(
          absl::bind_front(&NotifyThenWait, &started[i], &done[i])));
    }
    for (int i = 0; i != ABSL_ARRAYSIZE(started); i++) {
      WaitThenNotify(&started[i], &done[i]);
    }
    delete queue;
    ::absl::SleepFor(
        ::absl::Milliseconds(500));  // want all closures to finish, but can't
                                     // delete the default ThreadManager
  }
  {
    // Now try a limited queue with one thread and one queue entry.
    thread::ManagedQueueOptions q_options;
    q_options.thread_limit = 1;  // at most one thread
    q_options.queue_limit = 0;   // queue must be zero length
    thread::ManagedQueue* queue =
        thread::DefaultManager()->NewQueue("limited", q_options);
    absl::Notification started[2];
    absl::Notification done[2];
    Closure* cb = ::util::functional::ToCallback(
        absl::bind_front(&NotifyThenWait, &started[0], &done[0]));
    CHECK(queue->TrySchedule(::util::functional::FromCallback(
        cb)));  // first attempt should succeed.
    cb = ::util::functional::ToCallback(
        absl::bind_front(&NotifyThenWait, &started[1], &done[1]));
    CHECK(!queue->TrySchedule(
        ::util::functional::FromCallback(cb)));  // second attempt should fail.
    WaitThenNotify(&started[0], &done[0]);       // let first callback proceed
    ::absl::SleepFor(
        ::absl::Milliseconds(500));  // time for worker thread to become idle
    CHECK(queue->TrySchedule(::util::functional::FromCallback(
        cb)));  // second callback should be accepted
    WaitThenNotify(&started[1], &done[1]);
    delete queue;
    ::absl::SleepFor(
        ::absl::Milliseconds(500));  // want all closures to finish, but can't
                                     // delete the default ThreadManager
  }
}

TEST(ThreadManagerTest, AddIfReadyToRun) {
  thread::ThreadManager* tm =
      new thread::ThreadManager("test3", thread::ManagerOptions());
  thread::ManagedQueueOptions q_options;
  q_options.thread_limit = 1;  // at most one thread
  q_options.queue_limit = 0;   // queue must be zero length
  thread::ManagedQueue* queue = tm->NewQueue("limited", q_options);
  absl::Notification started[2];
  absl::Notification done[2];
  std::function<void()> cb =
      absl::bind_front(&NotifyThenWait, &started[0], &done[0]);
  CHECK(queue->TrySchedule(cb));  // first attempt should succeed.
  cb = absl::bind_front(&NotifyThenWait, &started[1], &done[1]);
  CHECK(!queue->TrySchedule(cb));         // second attempt should fail.
  WaitThenNotify(&started[0], &done[0]);  // let first callback proceed
  ::absl::SleepFor(
      ::absl::Milliseconds(500));  // time for worker thread to become idle
  CHECK(queue->TrySchedule(cb));   // second callback should be accepted
  WaitThenNotify(&started[1], &done[1]);
  delete queue;
  delete tm;
}

static void SleepThenNotify(absl::Notification* done) {
  ::absl::SleepFor(::absl::Milliseconds(100));
  done->Notify();
}

TEST(ThreadManagerTest, WaitUntilComplete) {
  // For various queue and thread limits, we start some closures that delay a
  // while on a a queue, and use WaitUntilComplete().  We check that all the
  // closures have indeed finished when WaitUntilComplete() returns.
  thread::ThreadManager* tm = thread::DefaultManager();
  static const int limit[] = {1, INT_MAX};
  for (int i = 0; i != ABSL_ARRAYSIZE(limit); i++) {
    for (int j = 0; j != ABSL_ARRAYSIZE(limit); j++) {
      thread::ManagedQueueOptions q_options;
      q_options.thread_limit = limit[i];
      q_options.queue_limit = limit[j];
      thread::ManagedQueue* queue = tm->NewQueue("test queue", q_options);
      absl::Notification done[10];
      for (int i = 0; i != ABSL_ARRAYSIZE(done); i++) {
        queue->Schedule(absl::bind_front(&SleepThenNotify, &done[i]));
      }
      queue->WaitUntilComplete();
      // All the Notifications should be notified
      for (int i = 0; i != ABSL_ARRAYSIZE(done); i++) {
        CHECK(done[i].HasBeenNotified())
            << "thread limit " << q_options.thread_limit << "queue limit "
            << q_options.queue_limit;
      }
      delete queue;
    }
  }
}

// Struct used to test named queues
struct QueueInfo {
  absl::Mutex mu;             // protects counter, thread_counter,
                              // max_threads_seen, max_pending_for_queue,
                              // max_pending_for_all_tags
  int expected_calls;         // expected calls; read-only after init
  int total_calls;            // total calls seen; under mu
  int concurrent_calls;       // concurrent calls; under mu
  int total_threads;          // threads seen; under mu
  int max_concurrent_calls;   // max concurrent calls value seen; under mu
  int max_pending_for_queue;  // max pending closures for this queue; under mu

  // The following fields are read-only after initialization
  thread::ManagedQueueOptions queue_options;
  Closure* cb;                  // Closure to be called for each event
  std::string name;             // unique name
  thread::ManagedQueue* queue;  // queue for this name
};

// Increment queue_info->count under queue_info->mu, wait sleep_ms milliseconds,
// then decrement it again, spin "spin" times, then return.  Check that
// queue_info->count never exceeds queue_info->limit.  Count the unique threads
// that run in queue_info->thread_counter, and note the maximum number of
// pending closures for the queue.
// L < queue_info->mu
static void ParameterizedTestClosure(PerThread::Key* key, int sleep_ms,
                                     int spin, QueueInfo* queue_info) {
  void** per_thread_data = PerThread::Data(*key);
  int pending_for_queue = queue_info->queue->num_pending_closures();

  queue_info->mu.lock();
  if (*per_thread_data == nullptr) {
    *per_thread_data = (void*)1;
    queue_info->total_threads++;
  }
  queue_info->concurrent_calls++;
  CHECK_LE(queue_info->concurrent_calls,
           queue_info->queue_options.thread_limit);
  if (queue_info->max_concurrent_calls < queue_info->concurrent_calls) {
    queue_info->max_concurrent_calls = queue_info->concurrent_calls;
  }
  if (queue_info->max_pending_for_queue < pending_for_queue) {
    queue_info->max_pending_for_queue = pending_for_queue;
  }
  queue_info->mu.unlock();
  if (sleep_ms != 0) {
    static bool always_false = false;
    absl::Mutex mu;
    mu.LockWhenWithTimeout(absl::Condition(&always_false),
                           absl::Milliseconds(sleep_ms));
    mu.unlock();
  }
  CHECK_EQ(thread::Executor::CurrentExecutor(),
           queue_info->queue->current_executor_for_testing());
  queue_info->mu.lock();
  queue_info->concurrent_calls--;
  queue_info->total_calls++;
  queue_info->mu.unlock();
  for (volatile int i = 0; i != spin; i++) {
  }
}

// Wait for the expected number of calls to complete.
static void WaitForCompletion(QueueInfo& queue_info) {
  (void)absl::MutexLock{queue_info.mu,
                        absl::Condition{
                            +[](const QueueInfo* const queue_info) {
                              return queue_info->total_calls ==
                                     queue_info->expected_calls;
                            },
                            &queue_info,
                        }};
}

TEST(ThreadManagerTest, SleepingClosures) {
  thread::ThreadManager* tm =
      new thread::ThreadManager("test4", thread::ManagerOptions());
  PerThread::Key key{PerThread::kInvalid};
  PerThread::Allocate(&key, nullptr);
  QueueInfo queue_info;
  queue_info.expected_calls = 100;
  queue_info.total_calls = 0;
  queue_info.concurrent_calls = 0;
  queue_info.total_threads = 0;
  queue_info.max_concurrent_calls = 0;
  queue_info.max_pending_for_queue = 0;
  queue_info.cb = ::util::functional::ToPermanentCallback(
      absl::bind_front(&ParameterizedTestClosure, &key, 1000, 0, &queue_info));
  queue_info.name = "sleeping";
  queue_info.queue = tm->NewQueue(queue_info.name, queue_info.queue_options);
  const absl::Time start_time = absl::Now();
  for (int i = 0; i != queue_info.expected_calls; i++) {
    queue_info.queue->Schedule(util::functional::FromCallback(queue_info.cb));
  }
  WaitForCompletion(queue_info);
  const absl::Duration elapsed = absl::Now() - start_time;
  delete queue_info.queue;
  delete tm;
  delete queue_info.cb;
  CHECK_GT(elapsed, absl::Milliseconds(500));
#if defined(ABSL_HAVE_THREAD_SANITIZER)
  CHECK_LT(elapsed, absl::Milliseconds(6000));
#else
  CHECK_LT(elapsed, absl::Milliseconds(3000));
#endif
}

TEST(ThreadManagerTest, CPUBoundClosures) {
  thread::ThreadManager* tm =
      new thread::ThreadManager("test5", thread::ManagerOptions());
  PerThread::Key key{PerThread::kInvalid};
  PerThread::Allocate(&key, nullptr);
  static const int kSpin = 10000000;
  QueueInfo queue_info;
  queue_info.expected_calls = 500;
  queue_info.total_calls = 0;
  queue_info.concurrent_calls = 0;
  queue_info.total_threads = 0;
  queue_info.max_concurrent_calls = 0;
  queue_info.max_pending_for_queue = 0;
  queue_info.cb = ::util::functional::ToPermanentCallback(
      absl::bind_front(&ParameterizedTestClosure, &key, 0, kSpin, &queue_info));
  queue_info.name = "cpu_bound";
  queue_info.queue = tm->NewQueue(queue_info.name, queue_info.queue_options);
  for (int i = 0; i != queue_info.expected_calls; i++) {
    queue_info.queue->Schedule(util::functional::FromCallback(queue_info.cb));
  }
  WaitForCompletion(queue_info);
  delete queue_info.queue;
  delete tm;
  delete queue_info.cb;
  CHECK_LT(queue_info.total_threads, queue_info.total_calls);
}

TEST(ThreadManagerTest, ContendingClosures) {
  thread::ThreadManager* tm =
      new thread::ThreadManager("test6", thread::ManagerOptions());
  PerThread::Key key{PerThread::kInvalid};
  PerThread::Allocate(&key, nullptr);
  QueueInfo queue_info;
#if defined(ABSL_HAVE_THREAD_SANITIZER)
  queue_info.expected_calls = 10000;
#else
  queue_info.expected_calls = 100000;
#endif
  queue_info.total_calls = 0;
  queue_info.concurrent_calls = 0;
  queue_info.total_threads = 0;
  queue_info.max_concurrent_calls = 0;
  queue_info.max_pending_for_queue = 0;
  queue_info.cb = ::util::functional::ToPermanentCallback(
      absl::bind_front(&ParameterizedTestClosure, &key, 0, 0, &queue_info));
  queue_info.name = "contending";
  queue_info.queue = tm->NewQueue(queue_info.name, queue_info.queue_options);
  for (int i = 0; i != queue_info.expected_calls; i++) {
    queue_info.queue->Schedule(util::functional::FromCallback(queue_info.cb));
    if (((i + 1) % 10000) == 0) {
      VLOG(0) << "Added " << (i + 1) << " of " << queue_info.expected_calls;
    }
  }
  WaitForCompletion(queue_info);
  delete queue_info.queue;
  delete tm;
  delete queue_info.cb;
#if defined(ABSL_HAVE_THREAD_SANITIZER)
  CHECK_LT(queue_info.total_threads, queue_info.total_calls / 2);
#else
  CHECK_LT(queue_info.total_threads, queue_info.total_calls / 5);
#endif
}

TEST(ThreadManagerTest, Queues) {
  PerThread::Key key{PerThread::kInvalid};
  PerThread::Allocate(&key, nullptr);
  thread::ThreadManager* tm =
      new thread::ThreadManager("test7", thread::ManagerOptions());
  static const int kSleepMS = 40;  // ms to sleep in closures
  // We use 2 queue names.  Entry 0 gets default (infinite) limits.
  // Entry 1 gets specific finite limits.
  QueueInfo queue_info[2];
  queue_info[1].queue_options.thread_limit = 2;
  queue_info[1].queue_options.queue_limit = 27;
  for (int i = 0; i != ABSL_ARRAYSIZE(queue_info); i++) {
    queue_info[i].expected_calls = 1000;
    queue_info[i].total_calls = 0;
    queue_info[i].concurrent_calls = 0;
    queue_info[i].total_threads = 0;
    queue_info[i].max_concurrent_calls = 0;
    queue_info[i].max_pending_for_queue = 0;
    queue_info[i].cb = ::util::functional::ToPermanentCallback(absl::bind_front(
        &ParameterizedTestClosure, &key, kSleepMS, 0, &queue_info[i]));
    queue_info[i].name = absl::StrFormat("queue %d", i);
    queue_info[i].queue =
        tm->NewQueue(queue_info[i].name, queue_info[i].queue_options);
    CHECK_EQ(queue_info[i].queue->num_pending_closures(), 0);
  }

  int64_t start_ms = absl::ToUnixMillis(absl::Now());
  for (int i = 0; i != queue_info[0].expected_calls; i++) {
    int r = rand();
    int entry = r % ABSL_ARRAYSIZE(queue_info);
    queue_info[entry].queue->Schedule(
        util::functional::FromCallback(queue_info[entry].cb));
  }
  int total_calls;
  do {
    ::absl::SleepFor(::absl::Milliseconds(40));
    total_calls = 0;
    for (int i = 0; i != ABSL_ARRAYSIZE(queue_info); i++) {
      queue_info[i].mu.lock();
      total_calls += queue_info[i].total_calls;
      queue_info[i].mu.unlock();
    }
  } while (total_calls != queue_info[0].expected_calls);
  for (int i = 0; i != ABSL_ARRAYSIZE(queue_info); i++) {
    delete queue_info[i].queue;
  }
  delete tm;
  int64_t end_ms = absl::ToUnixMillis(absl::Now());
  int threads = 0;
  for (int i = 0; i != ABSL_ARRAYSIZE(queue_info); i++) {
    threads += queue_info[i].total_threads;
    LOG(INFO) << "queue " << queue_info[i].name << "  thread_limit "
              << queue_info[i].queue_options.thread_limit << "  queue_limit "
              << queue_info[i].queue_options.queue_limit
              << " max_concurrent_calls " << queue_info[i].max_concurrent_calls
              << "  max_pending " << queue_info[i].max_pending_for_queue;
    CHECK_LE(queue_info[i].max_concurrent_calls,
             queue_info[i].queue_options.thread_limit);
    CHECK_EQ(queue_info[i].concurrent_calls, 0);
    if (queue_info[i].queue_options.thread_limit == INT_MAX) {
      // queues with infinite thread_limits should have less queuing
      CHECK_LE(8, queue_info[i].max_pending_for_queue);
      CHECK_LE(queue_info[i].max_pending_for_queue, 28);
    } else {  // others should see more queuing
      CHECK_LE(27, queue_info[i].max_pending_for_queue);
      CHECK_LE(queue_info[i].max_pending_for_queue, 40);
    }
    delete queue_info[i].cb;
  }
  LOG(INFO) << "ran " << queue_info[0].expected_calls << " " << kSleepMS
            << "ms closures in " << end_ms - start_ms << " ms with " << threads
            << " threads";
}

TEST(ThreadManagerTest, AddAfter) {
  thread::ThreadManager* tm =
      new thread::ThreadManager("test8", thread::ManagerOptions());
  thread::ManagedQueueOptions options;
  options.queue_limit = 1;
  options.thread_limit = 1;
  thread::ManagedQueue* q0 = tm->NewQueue("test8q0", options);
  thread::ManagedQueue* q1 = tm->NewQueue("test8q1", options);
  absl::Notification n[4];
  int64_t start_ms = absl::ToUnixMillis(absl::Now());
  q0->ScheduleAt(absl::Now() + absl::Milliseconds(1),
                 absl::bind_front(&absl::SleepFor, absl::Milliseconds(10000)));
  for (int i = 0; i != 3; i++) {
    q0->ScheduleAt(absl::Now() + absl::Milliseconds(1),
                   absl::bind_front(&absl::Notification::Notify, &n[i]));
  }
  q1->ScheduleAt(absl::Now() + absl::Milliseconds(1000),
                 absl::bind_front(&absl::Notification::Notify, &n[3]));
  n[3].WaitForNotification();
  int64_t end0_ms = absl::ToUnixMillis(absl::Now());
  // At least 1s should have passed ...
  CHECK_LE(start_ms + 1000, end0_ms);
  // but less than 10s because the TimedCall thread should not have been
  // blocked.
  CHECK_LT(end0_ms, start_ms + 10000);
  for (int i = 0; i != 3; i++) {
    n[i].WaitForNotification();
  }
  int64_t end1_ms = absl::ToUnixMillis(absl::Now());
  // now 10s should have passed despite the 1ms delay because q0 is
  // single-threaded.
  CHECK_LT(start_ms + 10000, end1_ms);
  delete q0;
  delete q1;
  // Check that WaitUntilComplete() waits even for work passed via
  // AddAfter(), both with bounded and unbounded threads.
  for (int i = 0; i != 2; i++) {
    thread::ManagedQueueOptions options_wait;
    if ((i & 1) == 0) {
      options_wait.thread_limit = 1;
    }
    absl::Notification add_after_note;
    thread::ManagedQueue* q = tm->NewQueue("test8q2", options_wait);
    q->ScheduleAt(absl::Now() + absl::Milliseconds(1000),
                  [&add_after_note] { add_after_note.Notify(); });
    q->WaitUntilComplete();
    CHECK(add_after_note.HasBeenNotified());
    delete q;
  }
  delete tm;

  // Check that deleting the thread_manager waits even for work passed
  // via AddAfter(), and even if WaitUntilComplete() is not called.
  for (int i = 0; i != 2; i++) {
    tm = new thread::ThreadManager("test8a", thread::ManagerOptions());
    thread::ManagedQueueOptions options_wait;
    if ((i & 1) == 0) {
      options_wait.thread_limit = 1;
    }
    absl::Notification add_after_note;
    thread::ManagedQueue* q = tm->NewQueue("test8q2", options_wait);
    q->ScheduleAt(absl::Now() + absl::Milliseconds(1000),
                  [&add_after_note] { add_after_note.Notify(); });
    delete q;
    delete tm;
    CHECK(add_after_note.HasBeenNotified());
  }
}

// Helper class for CurrentExecutorNotSetDuringThreadExit.
class VerifyNoCurrentExecutorAtDestruction {
 public:
  ~VerifyNoCurrentExecutorAtDestruction() {
    CHECK_EQ(thread::Executor::CurrentExecutor(), nullptr);
  }
};

TEST(ThreadManagerTest, CurrentExecutorNotSetDuringThreadExit) {
  thread::ThreadManager* tm =
      new thread::ThreadManager("test9", thread::ManagerOptions());
  thread::ManagedQueue* q =
      tm->NewQueue("test9q", thread::ManagedQueueOptions());

  // Create a threadlocal object on the ThreadManager thread that will verify
  // there's no CurrentExecutor visible when the ThreadManager thread exits and
  // runs threadlocal destructors.
  ThreadLocal<VerifyNoCurrentExecutorAtDestruction> threadlocal;
  q->Schedule([&threadlocal] {
    threadlocal.get();  // Force creation of the threadlocal on this thread.
  });

  delete q;
  delete tm;
}

// Helper class for ToleratesSlowThreadExit test; it's like a Notification but
// allows for waiting for at least one of several notifiers.
class AtLeastOneNotification {
 public:
  void Notify() {
    mu_.lock();
    notified_ = true;
    mu_.unlock();
  }

  void WaitForAtLeastOneNotification() const {
    mu_.LockWhen(absl::Condition(&notified_));
    mu_.unlock();
  }

 private:
  mutable absl::Mutex mu_;
  bool notified_ = false;
};

// Helper class for ToleratesSlowThreadExit test; it's a class that allows its
// destruction to be arbitrarily delayed.
class ClassWithSlowDestructor {
 public:
  ~ClassWithSlowDestructor() {
    started_destructor_->Notify();
    can_finish_destructor_->WaitForNotification();
  }

  void SetDestructorNotifications(
      AtLeastOneNotification* started_destructor,
      const absl::Notification* can_finish_destructor) {
    started_destructor_ = started_destructor;
    can_finish_destructor_ = can_finish_destructor;
  }

 private:
  AtLeastOneNotification* started_destructor_ = nullptr;
  const absl::Notification* can_finish_destructor_ = nullptr;
};

// This test verifies ThreadManager facilities remain available/uncontended even
// in the case of a slow exit of one of ThreadManager's threads, e.g., due to a
// destructor for a thread local object suffering from contention.
//
// The test works roughly as follows:
// - First, set up a ThreadManager thread with a threadlocal that's artificially
//   slow-to-destruct; this will result in the thread being slow to exit.
// - Then while the threadlocal destructor is running (i.e., the thread is
//   "exiting" from ThreadManager's perspective), verify ThreadManager is
//   available for use: new closures may be queued, etc. without contention.
//
// (The actual mechanics are of course a bit more complicated than the sketch
// above, mainly since we need to start enough threads so that ThreadManager
// will decide to kill some of them and since we inject Notifications in order
// to synchronize the test so that it covers the desired scenario.)
TEST(ThreadManagerTest, ToleratesSlowThreadExit) {
  // We must start enough threads that ThreadManager will decide to kill some
  // when they become idle.
  const int kNumThreads = 100;

  thread::ThreadManager* tm =
      new thread::ThreadManager("test10", thread::ManagerOptions());
  thread::ManagedQueue* q =
      tm->NewQueue("test10q", thread::ManagedQueueOptions());

  // Start several threads that have a slow-to-destruct threadlocal.
  ThreadLocal<ClassWithSlowDestructor> threadlocal;
  absl::BlockingCounter configured_threadlocal_destructors(kNumThreads);
  absl::Notification can_start_thread_exits;
  AtLeastOneNotification started_threadlocal_destructors;
  absl::Notification can_finish_threadlocal_destructors;
  for (int i = 0; i < kNumThreads; i++) {
    q->Schedule([&threadlocal, &configured_threadlocal_destructors,
                 &can_start_thread_exits, &started_threadlocal_destructors,
                 &can_finish_threadlocal_destructors] {
      threadlocal.pointer()->SetDestructorNotifications(
          &started_threadlocal_destructors,
          &can_finish_threadlocal_destructors);
      configured_threadlocal_destructors.DecrementCount();
      can_start_thread_exits.WaitForNotification();
    });
  }
  configured_threadlocal_destructors.Wait();
  LOG(INFO) << "Configured threads with slow-to-destroy threadlocals";

  // Allow the closures started above to finish; ThreadManager should tell at
  // least some of the idle threads to exit, and hit the artificially-blocked
  // threadlocal destructor.
  can_start_thread_exits.Notify();
  started_threadlocal_destructors.WaitForAtLeastOneNotification();
  LOG(INFO) << "Destructor for threadlocal started -- but completion blocked";

  // Sleep a bit so that it's likely the ThreadManager overseer has begun to
  // process the threads that have been instructed to exit. (If the overseer
  // doesn't wake in time, the test will still pass, but it might not actually
  // verify behavior of ThreadManager wrt. exited threads.)
  absl::SleepFor(absl::Seconds(1));

  // Despite the blocked thread exit, existing and other ThreadManagers should
  // still work as usual.
  const int kNumClosures = 100;  // Large enough to likely hit all pools.
  for (int i = 0; i < kNumClosures; i++) {
    // We can't actually guarantee closures get run (since that might require
    // waking the overseer), but enqueuing new closures should not block.
    q->Schedule([] {});
  }
  LOG(INFO) << "Ran closures on queue";

  thread::ManagedQueue* q2 =
      tm->NewQueue("test10q2", thread::ManagedQueueOptions());
  delete q2;
  LOG(INFO) << "Created/destroyed new queue";

  thread::ThreadManager* tm2 =
      new thread::ThreadManager("test10tm2", thread::ManagerOptions());
  delete tm2;
  LOG(INFO) << "Created/destroyed new threadmanager";

  // Clean up.
  can_finish_threadlocal_destructors.Notify();
  delete q;
  delete tm;
}

namespace thread {
TEST(ThreadManagerWatchdogTest, UsesCustomWatchDogCallback) {
  absl::Notification watchdog_fired;

  thread::ManagerOptions manager_options;
  manager_options.watchdog_callback = util::functional::ToPermanentCallback(
      [&](WatchDog* watchdog) { watchdog_fired.Notify(); });

  thread::ManagedQueueOptions queue_options;
  queue_options.time_limit_s = 1;

  thread::ThreadManager* tm = new thread::ThreadManager(
      "custom_watchdog_callback_test", manager_options);
  thread::ManagedQueue* q =
      tm->NewQueue("custom_watchdog_callback_test_queue", queue_options);

  q->Schedule([&]() {
    const absl::Time watchdog_wait_start = absl::Now();
    // WatchDog checks are not enabled by default on all platforms, so this
    // explicit call is necessary (and also makes the test faster by not waiting
    // for a periodic poll).
    while (!watchdog_fired.WaitForNotificationWithTimeout(
        absl::Milliseconds(100))) {
      ASSERT_LT(absl::Now() - watchdog_wait_start, absl::Seconds(10))
          << "Expected WatchDog to trigger after 1s, it did not trigger after "
             "10s";
      WatchDog::CheckAlive();
    }
  });

  q->WaitUntilComplete();
  delete q;
  delete tm;
}
}  // namespace thread

namespace thread {
void PrintTo(const thread::ManagedQueueStats& s, std::ostream* os) {
  *os << "queue_name=" << s.queue_name << " queue_running=" << s.queue_running
      << " num_pending_closures=" << s.num_pending_closures;
}
}  // namespace thread

TEST(QueueStatsTest, ZeroQueues) {
  std::vector<thread::ManagedQueueStats> stats =
      thread::ThreadManager::QueueStats();
  EXPECT_THAT(stats, IsEmpty());
}

TEST(QueueStatsTest, OneQueue) {
  thread::ThreadManager tm("QueueStatsTest", thread::ManagerOptions());
  {
    thread::ManagedQueueOptions qo;
    qo.thread_limit = 1;
    std::unique_ptr<thread::ManagedQueue> q(tm.NewQueue("OneQueue", qo));
    EXPECT_THAT(
        q->Stats(),
        AllOf(Field(&thread::ManagedQueueStats::queue_name, Eq("OneQueue")),
              Field(&thread::ManagedQueueStats::queue_running, Eq(0)),
              Field(&thread::ManagedQueueStats::num_pending_closures, Eq(0))));

    // Make one running closure.
    absl::Notification started, finish;
    q->Schedule([&started, &finish]() {
      started.Notify();
      finish.WaitForNotification();
    });
    started.WaitForNotification();

    // and two pending
    q->Schedule([]() {});
    q->Schedule([]() {});

    std::vector<thread::ManagedQueueStats> stats =
        thread::ThreadManager::QueueStats();
    EXPECT_THAT(
        stats,
        ElementsAre(AllOf(
            Field(&thread::ManagedQueueStats::queue_name, Eq("OneQueue")),
            Field(&thread::ManagedQueueStats::queue_running, Eq(1)),
            Field(&thread::ManagedQueueStats::num_pending_closures, Eq(2)))));
    EXPECT_THAT(
        q->Stats(),
        AllOf(Field(&thread::ManagedQueueStats::queue_name, Eq("OneQueue")),
              Field(&thread::ManagedQueueStats::queue_running, Eq(1)),
              Field(&thread::ManagedQueueStats::num_pending_closures, Eq(2))));

    finish.Notify();
    q->WaitUntilComplete();
  }
}

// Helper class for benchmarks.  It arranges to invoke Run() a specified
// number of times by recursively scheduling itself on an executor.
class BM_Runner {
 private:
  // No locks are needed since we ensure that a BM_Runner is present
  // at most once on any executor queue.
  absl::Notification done_;
  thread::Executor* q_;
  benchmark::State* const state_;

 public:
  BM_Runner(thread::Executor* q, benchmark::State* state)
      : q_(q), state_(state) {}

  void Run() {
    if (state_->KeepRunning()) {
      q_->Schedule([this] { Run(); });
    } else {
      done_.Notify();
    }
  }

  void Wait() { done_.WaitForNotification(); }
};

// For the purposes of some benchmarks, we want a thread manager that
// uses exactly one thread, even if using an unlimited queue.
class SingleThreadPolicy : public thread::ThreadManagerPolicy {
 public:
  virtual void Eval(const thread::ThreadManagerState& state,
                    thread::ThreadManagerAction* result) {
    result->create = (state.threads < 1);
    result->delay_ms = 100000;  // Long delay since no need to call again
  }
};

static void BM_ThreadManagerRun(benchmark::State& state) {
  // Make a single-thread manager
  thread::ManagerOptions options;
  options.n_pools = 1;
  options.policy = new SingleThreadPolicy;
  thread::ThreadManager* manager =
      new thread::ThreadManager("benchmark_manager", options);

  // Make a queue with the specified thread limit
  thread::ManagedQueueOptions qoptions;
  qoptions.thread_limit = state.range(0);
  thread::ManagedQueue* q = manager->NewQueue("benchmark_queue", qoptions);

  BM_Runner runner(q, &state);
  runner.Run();
  runner.Wait();
  delete q;
  delete manager;
}
BENCHMARK(BM_ThreadManagerRun)->Arg(1)->Arg(INT_MAX);

static void BM_ThreadManagerSchedule(benchmark::State& state) {
  // Make a single-thread manager
  thread::ManagerOptions options;
  options.n_pools = 1;
  options.policy = new SingleThreadPolicy;
  thread::ThreadManager* manager =
      new thread::ThreadManager("benchmark_manager", options);

  // Make a queue with the specified thread limit
  thread::ManagedQueueOptions qoptions;
  qoptions.thread_limit = state.range(0);
  thread::ManagedQueue* q = manager->NewQueue("benchmark_queue", qoptions);

  absl::Notification done;
  std::function<void()> fn = [&] {
    if (state.KeepRunning()) {
      q->Schedule(fn);  // Enqueue self.
    } else {
      done.Notify();
    }
  };

  fn();  // Kick things off
  done.WaitForNotification();
  delete q;
  delete manager;
}
BENCHMARK(BM_ThreadManagerSchedule)->Arg(1)->Arg(INT_MAX);

static void BM_ThreadManagerDefaultPolicyRun(benchmark::State& state) {
  // Make a thread manager with the default policy
  thread::ManagerOptions options;
  thread::ThreadManager* manager =
      new thread::ThreadManager("benchmark_manager_with_defaults", options);

  // Make a queue with the specified thread limit
  thread::ManagedQueueOptions qoptions;
  qoptions.thread_limit = state.range(0);
  thread::ManagedQueue* q = manager->NewQueue("benchmark_queue", qoptions);

  BM_Runner runner(q, &state);
  runner.Run();
  runner.Wait();
  delete q;
  delete manager;
}
BENCHMARK(BM_ThreadManagerDefaultPolicyRun)->Arg(1)->Arg(INT_MAX);

static void Nothing() {}

static void BM_ThreadManagerDefaultPolicyQueuedInAdvance(
    benchmark::State& state) {
  const int kBatchSize = 500;  // Arbitrary - could pick a different value.
  // Make a thread manager with the default policy
  thread::ManagerOptions options;
  options.n_pools = 1;
  options.policy = new SingleThreadPolicy;
  thread::ThreadManager* manager =
      new thread::ThreadManager("benchmark_manager_with_defaults", options);
  // Make a queue with the specified thread limit
  thread::ManagedQueueOptions qoptions;
  thread::ManagedQueue* q = manager->NewQueue("benchmark_queue", qoptions);

  while (state.KeepRunningBatch(kBatchSize)) {
    for (int i = 0; i < kBatchSize; ++i) {
      q->Schedule(Nothing);
    }
    q->WaitUntilComplete();
  }

  delete q;
  delete manager;
}
BENCHMARK(BM_ThreadManagerDefaultPolicyQueuedInAdvance);

static void BM_ThreadPoolRun(benchmark::State& state) {
  ThreadPool pool(1, ThreadPool::Options{.name_prefix = "benchmark_pool"});
  BM_Runner runner(&pool, &state);
  runner.Run();
  runner.Wait();
}
BENCHMARK(BM_ThreadPoolRun);

static void BM_ThreadPoolStartStop(benchmark::State& state) {
  for (auto _ : state) {
    ThreadPool pool(state.range(0),
                    ThreadPool::Options{.name_prefix = "benchmark_pool"});
  }
}
BENCHMARK(BM_ThreadPoolStartStop)->Arg(1)->Arg(10)->Arg(100)->Arg(1000);
