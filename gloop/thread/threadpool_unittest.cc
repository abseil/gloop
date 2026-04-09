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

#include "gloop/thread/threadpool.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_set.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/log/vlog_is_on.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/atomic_stats_counter.h"
#include "gloop/concurrent/barrier/barrier.h"
#include "gloop/thread/executor.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"
#include "gloop/util/refcount/blocking_refcount.h"
#include "gtest/gtest.h"
#include "tcmalloc/malloc_extension.h"

// Test the interaction of threadpools and closures.

struct MyCall {
  int calls_;
  absl::Mutex mutex_;

  MyCall() : calls_(0) {}

  void IncCalls(AbstractThreadPool* p) {
    CHECK_EQ(thread::Executor::CurrentExecutor(), p);
    absl::MutexLock m(mutex_);
    ++calls_;
  }

  int GetCalls() {
    absl::MutexLock m(mutex_);
    return calls_;
  }
};

TEST(ThreadPoolTest, Closure) {
  for (int trial = 0; trial < 10; ++trial) {
    MyCall m;
    {
      ThreadPool p(5);
      auto c = concurrent::NewBarrier(5, [&] { m.IncCalls(&p); });
      for (int i = 0; i < 5; ++i) {
        CHECK_EQ(m.GetCalls(), 0);
        p.Schedule(c);
      }
    }  // threadpool must terminate
    ASSERT_EQ(m.GetCalls(), 1);
  }
  CHECK(thread::Executor::CurrentExecutor() == nullptr);
}

TEST(ThreadPoolTest, Try) {
  MyCall m;
  std::optional<ThreadPool> p;
  p.emplace(5, ThreadPool::Options{.queue_capacity = 10});
  AbstractThreadPool* abstract_p = &*p;  // Used to check CurrentExecutor
  util::BlockingRefcount initial_running;
  initial_running.IncN(4);
  absl::Notification initial_continue;
  for (int i = 0; i < 4; ++i) {
    p->Schedule([&] {
      initial_running.Dec();
      initial_continue.WaitForNotification();
      m.IncCalls(abstract_p);
    });
  }
  initial_running.WaitForZero();
  absl::Notification second_running, second_continue;
  p->Schedule([&] {
    second_running.Notify();
    second_continue.WaitForNotification();
    m.IncCalls(abstract_p);
  });
  second_running.WaitForNotification();

  // Make sure we can't add any calls with this method, since all
  // threads are busy
  CHECK(p->ScheduleIfReadyToRun([&] { m.IncCalls(abstract_p); }) == false);
  // Make sure we can add calls with this method
  for (int i = 0; i < 10; ++i) {
    CHECK(p->TrySchedule([&] { m.IncCalls(abstract_p); }));
  }
  // Make sure we can't add any more now that the queue is full
  CHECK(p->TrySchedule([&] { m.IncCalls(abstract_p); }) == false);
  // Run initial closures and leave some time for the TrySchedule calls to run
  initial_continue.Notify();
  ::absl::SleepFor(::absl::Seconds(2));
  // Now we can add a few to the queue
  absl::Notification final_continue;
  for (int i = 0; i < 4; ++i) {
    CHECK(p->ScheduleIfReadyToRun([&] {
      final_continue.WaitForNotification();
      m.IncCalls(abstract_p);
    }));
  }
  // There should still be one more long job on the queue, so we
  // can't add any more again.  This should work whether or not the
  // jobs have been taken by the threads
  CHECK(p->ScheduleIfReadyToRun([&] { m.IncCalls(abstract_p); }) == false);
  second_continue.Notify();
  final_continue.Notify();
  p.reset();  // wait for threadpool to terminate
  CHECK_EQ(m.GetCalls(), 19);
}

// Code to run in a thread to check that it can allocate/deallocate properly
static void TestAllocation() {
  static const int kNum = 100;
  void* ptr[kNum];
  for (int size = 8; size <= 65536; size *= 2) {
    for (int i = 0; i < kNum; i++) {
      ptr[i] = malloc(size);
    }
    for (int i = 0; i < kNum; i++) {
      free(ptr[i]);
    }
  }
  ::absl::SleepFor(::absl::Milliseconds(50));
}

static void DumpMallocInfo() {
  if (VLOG_IS_ON(1)) {
    VLOG(1) << "\n" << tcmalloc::MallocExtension::GetStats();
  }
}

TEST(ThreadPoolTest, Idleness) {
  fprintf(stderr, "===== Testing that threads go idle after a while\n");
  const int kThreads = 10;
  ThreadPool* pool = new ThreadPool(kThreads);

  // Force every thread to do some work
  for (int i = 0; i < kThreads; i++) {
    pool->Schedule(TestAllocation);
  }
  ::absl::SleepFor(::absl::Milliseconds(200));
  DumpMallocInfo();

  // Make all threads go idle
  ::absl::SleepFor(::absl::Milliseconds(1100));
  DumpMallocInfo();

  // Force some of the threads to do some more work
  for (int i = 0; i < kThreads / 2; i++) {
    pool->Schedule(TestAllocation);
  }
  ::absl::SleepFor(::absl::Milliseconds(200));
  DumpMallocInfo();

  delete pool;
  DumpMallocInfo();
}

TEST(ThreadPoolTest, CanBeDeletedImmediatelyAfterConstruction) {
  ThreadPool pool(7);
}

typedef absl::flat_hash_set<std::pair<pid_t, std::string> > ThreadNameSet;

static bool ThreadNameRecorder(void* arg, const LiveThread* thread) {
  ThreadNameSet* names = static_cast<ThreadNameSet*>(arg);
  CHECK(names
            ->insert(std::make_pair(LiveThread_OS_TID(thread),
                                    LiveThread_Name(thread)))
            .second);
  return false;
}

static std::string ExpectedThreadName(absl::string_view prefix, pid_t pid) {
  return absl::StrFormat("%s/%d", prefix, static_cast<int64_t>(pid));
}

TEST(ThreadPoolTest, SetName) {
  ThreadPool pool(3, {.name_prefix = "foo"});

  // Start threads, and wait until all are running.
  util::BlockingRefcount workers_started;
  workers_started.IncN(3);
  util::BlockingRefcount workers_running;
  workers_running.IncN(3);
  for (int i = 0; i < 3; ++i) {
    pool.Schedule([&] {
      workers_started.Dec();
      workers_started.WaitForZero();
      workers_running.Dec();
    });
  }
  workers_running.WaitForZero();

  ThreadNameSet names;
  Thread_ForEach(ThreadNameRecorder, &names, nullptr, nullptr, 0);

  int foo_threads_seen = 0;
  for (ThreadNameSet::const_iterator i = names.begin(); i != names.end(); ++i) {
    if (absl::StartsWith(i->second, "foo/")) {
      foo_threads_seen++;
      CHECK_STREQ(ExpectedThreadName("foo", i->first).c_str(),
                  i->second.c_str());
    }
  }
  CHECK_EQ(3, foo_threads_seen);
}

TEST(ThreadPoolTest, OptionsConstructor) {
  thread::Options options;
  options.set_stack_size(192 * 1024);  // just a non-default size
  ThreadPool pool(5, ThreadPool::Options{.name_prefix = "TestPool",
                                         .thread_options = options});
  CHECK_EQ(pool.thread_options_.stack_size(), options.stack_size());
}

// If num_threads is 0, ThreadPool should not CHECK-fail or crash.
TEST(ThreadPoolTest, NumThreadsZero) { ThreadPool pool(0); }

// If num_threads is 1, the closures are run in FIFO order.
TEST(ThreadPoolTest, OneThreadRunsClosuresFIFO) {
  int count = 0;  // Declare first so that it outlives the thread pool.
  ThreadPool pool(1);
  EXPECT_EQ(pool.num_threads(), 1);
  for (int i = 0; i < 1000; ++i) {
    pool.Schedule([&count, i]() {
      EXPECT_EQ(count, i);
      count++;
    });
  }
}
