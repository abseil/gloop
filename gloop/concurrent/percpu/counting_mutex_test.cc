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

#include "gloop/concurrent/percpu/counting_mutex.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>  // NOLINT
#include <typeinfo>
#include <utility>
#include <variant>
#include <vector>

#include "absl/base/log_severity.h"
#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "benchmark/benchmark.h"
#include "gloop/base/sysinfo.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::Eq;

namespace {

// See comment on the kRegionSize constant being 128 >= ABSL_CACHELINE_SIZE
static_assert(ABSL_CACHELINE_SIZE <= 128, "Adjust kRegionSize");

// Possible thread modes supported by the TestThread class
enum class ThreadMode { kDefault, kThread, kLegacy };

// Parses a ThreadMode from the command line flag value `text`.
// Returns `true` and sets `*mode` on success.
// Returns `false` and sets `*error` on failure.
bool AbslParseFlag(absl::string_view text, ThreadMode* mode,
                   std::string* error) {
  if (text == "default") {
    *mode = ThreadMode::kDefault;
  } else if (text == "thread") {
    *mode = ThreadMode::kThread;
  } else if (text == "legacy") {
    *mode = ThreadMode::kLegacy;
  } else {
    *error =
        "Use one of 'default', 'thread', 'fiber', 'root', 'one_cpu', "
        "'detached', 'legacy'";
    return false;
  }
  return true;
}

// Returns the text representation of `mode`
std::string AbslUnparseFlag(ThreadMode mode) {
  switch (mode) {
    case ThreadMode::kDefault:
      return "default";
    case ThreadMode::kThread:
      return "thread";
    case ThreadMode::kLegacy:
      return "legacy";
  }
  return absl::StrCat(mode);
}

}  // namespace

ABSL_FLAG(ThreadMode, thread_mode, ThreadMode::kDefault, "Thread mode");
ABSL_FLAG(int, threads, 4, "Thread count for test");
ABSL_FLAG(int, wait_micros, 0, "Number of microseconds to wait");
ABSL_FLAG(int, max_threads, -1, "Max thread count for benchmarks");
ABSL_FLAG(int, write_interval, 1000,
          "Number of reads across threads before a write");

namespace concurrent {
namespace {

// Returns a random ThreadMode value, kDefault excluded.
ThreadMode RandomThreadMode() {
  absl::BitGen gen;
  switch (absl::Uniform(gen, 1, 5)) {
    case 1:
      return ThreadMode::kLegacy;
    default:
      return ThreadMode::kThread;
  }
}

// Returns the provided mode if the '--thread_mode` flag value is 'default',
// else returns the explicit value parsed from the `--thread_mode=<mode>` flag.
ThreadMode ThreadModeIfDefault(ThreadMode mode) {
  ThreadMode flag_mode = absl::GetFlag(FLAGS_thread_mode);
  return flag_mode == ThreadMode::kDefault ? mode : flag_mode;
}

// `TestThread` runs a specified functor on a separate thread or fiber. The type
// of thread or fiber is defined by the 'thread_mode` flag, and can be one of:
//   - std::thread
//   - thread::Fiber:
//     - child fiber
//     - root fiber
//     - root fiber      (parallelism = 1)
//     - detached fiber
//   - Thread
//
// Detached threads are joined using a shared Notification instance.
class TestThread {
 public:
  TestThread() = default;

  TestThread(TestThread&& rhs) noexcept : impl_(std::move(rhs.impl_)) {
    rhs.impl_.emplace<std::monostate>();
  }

  template <typename FN>
  explicit TestThread(FN&& fn)
      : TestThread(absl::GetFlag(FLAGS_thread_mode), std::forward<FN>(fn)) {}

  template <typename FN>
  explicit TestThread(ThreadMode mode, FN&& fn) {
    switch (mode) {
      case ThreadMode::kDefault:
      case ThreadMode::kThread:
        impl_.emplace<std::thread>(std::forward<FN>(fn));
        break;
      case ThreadMode::kLegacy:
        impl_ = std::make_unique<LegacyThread>(std::forward<FN>(fn));
        break;
    }
  }

  ~TestThread() { Join(); }

  void Join() {
    if (auto* thread = std::get_if<std::thread>(&impl_)) {
      thread->join();
    } else if (auto* legacy_thread = std::get_if<LegacyThreadPtr>(&impl_)) {
      (*legacy_thread)->Join();
    } else if (auto* notification = std::get_if<NotificationPtr>(&impl_)) {
      (*notification)->WaitForNotification();
    }
    impl_.emplace<std::monostate>();
  }

 private:
  struct LegacyThread : public Thread {
    explicit LegacyThread(std::function<void()> fn)
        : Thread(thread::Options().set_joinable(true), "TestThread"),
          fn(std::move(fn)) {
      Start();
    }
    void Run() final { fn(); }
    std::function<void()> fn;
  };

  using LegacyThreadPtr = std::unique_ptr<LegacyThread>;
  using NotificationPtr = std::shared_ptr<absl::Notification>;

  std::variant<std::monostate, std::thread, LegacyThreadPtr, NotificationPtr>
      impl_;
};

TEST(CountingMutexTest, ConcurrentlyAllocFreeManyMutexes) {
  // This test makes sure that we hit all code paths for allocating,
  // free-listing and re-using free-listed handles concurrently. We use some
  // arbitrary 256 threads which should lead to enough waits and running /
  // runnable threads to create contended Alloc / Free and re-use scenarios.
  std::vector<TestThread> threads;
  for (int i = 0; i < 256; ++i) {
    threads.emplace_back([] {
      std::deque<CountingMutex> mutexes;
      // Typical entries per region is 128 / 4 = 32, aim substantially higher.
      for (int i = 0; i < 1024; ++i) {
        mutexes.emplace_back();
      }
    });
  }

  for (auto& thread : threads) {
    thread.Join();
  }
}

TEST(CountingMutexTest, ReaderLockUnlock) {
  CountingMutex mutex;
  mutex.lock_shared();
  mutex.unlock_shared();
}

TEST(CountingMutexTest, ReaderTryLockUnlock) {
  CountingMutex mutex;
  bool ret = mutex.try_lock_shared();
  EXPECT_TRUE(ret);
  if (ret) mutex.unlock_shared();
}

TEST(CountingMutexTest, WriterLockUnlock) {
  CountingMutex mutex;
  mutex.lock();
  mutex.unlock();
}

bool WaitFor(absl::Notification& notification,
             absl::Duration duration = absl::Minutes(1)) {
  return notification.WaitForNotificationWithTimeout(duration);
}

TEST(CountingMutexTest, ConcurrentReaderLocks) {
  absl::Notification notification;
  CountingMutex mutex;
  mutex.lock_shared();
  TestThread thread([&] {
    mutex.lock_shared();
    notification.Notify();
    mutex.unlock_shared();
  });
  EXPECT_TRUE(WaitFor(notification));
  mutex.unlock_shared();
  thread.Join();
}

TEST(CountingMutexTest, ConcurrentReaderTryLocks) {
  absl::Notification notification;
  CountingMutex mutex;
  bool ret = mutex.try_lock_shared();
  ASSERT_TRUE(ret);
  TestThread thread([&] {
    bool ret = mutex.try_lock_shared();
    EXPECT_TRUE(ret);
    notification.Notify();
    if (ret) mutex.unlock_shared();
  });
  EXPECT_TRUE(WaitFor(notification));
  if (ret) mutex.unlock_shared();
  thread.Join();
}

TEST(CountingMutexTest, ReaderTryLockWithConcurrentWriterLock) {
  absl::Notification notification;
  CountingMutex mutex;
  bool ret = mutex.try_lock_shared();
  EXPECT_TRUE(ret);
  TestThread thread([&] {
    mutex.lock();
    notification.Notify();
    mutex.unlock();
  });
  EXPECT_FALSE(WaitFor(notification, absl::Seconds(0.5)));
  if (ret) mutex.unlock_shared();
  EXPECT_TRUE(WaitFor(notification));
  thread.Join();
}

TEST(CountingMutexTest, ReaderWithConcurrentWriterLock) {
  absl::Notification notification;
  CountingMutex mutex;
  mutex.lock_shared();
  TestThread thread([&] {
    mutex.lock();
    notification.Notify();
    mutex.unlock();
  });
  EXPECT_FALSE(WaitFor(notification, absl::Seconds(0.5)));
  mutex.unlock_shared();
  EXPECT_TRUE(WaitFor(notification));
  thread.Join();
}

TEST(CountingMutexTest, WriterWithConcurrentReaderLock) {
  absl::Notification notification;
  CountingMutex mutex;
  mutex.lock();
  TestThread thread([&] {
    mutex.lock_shared();
    notification.Notify();
    mutex.unlock_shared();
  });
  EXPECT_FALSE(WaitFor(notification, absl::Seconds(1)));
  mutex.unlock();
  EXPECT_TRUE(WaitFor(notification));
  thread.Join();
}

TEST(CountingMutexTest, WriterWithConcurrentReaderTryLock) {
  absl::Notification notification;
  CountingMutex mutex;
  mutex.lock();
  TestThread thread([&] {
    bool ret = mutex.try_lock_shared();
    EXPECT_FALSE(ret);
    if (ret) mutex.unlock_shared();
    notification.Notify();
  });
  EXPECT_TRUE(WaitFor(notification));
  mutex.unlock();
  thread.Join();
}

TEST(CountingMutexTest, WriterAwaitTrueCondition) {
  CountingMutex mutex;
  CountingMutexLock lock(mutex);
  auto cond = [&] { return mutex.AssertReaderHeld(), true; };

  mutex.Await(absl::Condition(&cond));

  EXPECT_TRUE(mutex.AwaitWithTimeout(absl::Condition(&cond), absl::Seconds(1)));
  EXPECT_TRUE(
      mutex.AwaitWithTimeout(absl::Condition(&cond), absl::Seconds(-1)));

  EXPECT_TRUE(mutex.AwaitWithDeadline(absl::Condition(&cond),
                                      absl::Now() + absl::Seconds(1)));
  EXPECT_TRUE(mutex.AwaitWithDeadline(absl::Condition(&cond),
                                      absl::Now() - absl::Seconds(1)));
}

TEST(CountingMutexTest, WriterAwaitFalseCondition) {
  CountingMutex mutex;
  CountingMutexLock lock(mutex);
  auto cond = [&] { return mutex.AssertReaderHeld(), false; };

  EXPECT_FALSE(
      mutex.AwaitWithTimeout(absl::Condition(&cond), absl::Seconds(1)));
  EXPECT_FALSE(
      mutex.AwaitWithTimeout(absl::Condition(&cond), absl::Seconds(-1)));

  EXPECT_FALSE(mutex.AwaitWithDeadline(absl::Condition(&cond),
                                       absl::Now() + absl::Seconds(1)));
  EXPECT_FALSE(mutex.AwaitWithDeadline(absl::Condition(&cond),
                                       absl::Now() - absl::Seconds(1)));
}

TEST(CountingMutexTest, WriterAwaitTimeoutFailureHoldsLock) {
  absl::Notification awaiter_can_unlock;
  absl::Notification awaiter_re_locked;
  CountingMutex mutex;

  TestThread thread([&] {
    CountingMutexLock lock(mutex);
    auto cond = [&] { return mutex.AssertReaderHeld(), false; };
    EXPECT_FALSE(
        mutex.AwaitWithTimeout(absl::Condition(&cond), absl::Seconds(1)));
    mutex.AssertHeld();
    awaiter_re_locked.Notify();
    EXPECT_TRUE(WaitFor(awaiter_can_unlock));
  });
  EXPECT_TRUE(WaitFor(awaiter_re_locked));

  // Ensure we have a legitimate writer lock after awaiting, meaning no other
  // readers or writers are allowed until the awaiter is unlocked.
  absl::Notification other_reader_locked;
  TestThread other_reader_thread([&] {
    CountingMutexReaderLock lock(mutex);
    other_reader_locked.Notify();
  });
  EXPECT_FALSE(WaitFor(other_reader_locked, absl::Seconds(1)));

  absl::Notification other_writer_locked;
  TestThread other_writer_thread([&] {
    CountingMutexLock lock(mutex);
    other_writer_locked.Notify();
  });
  EXPECT_FALSE(WaitFor(other_writer_locked, absl::Seconds(1)));

  // Now let the awaiter thread unlock and ensure we can get the other locks.
  awaiter_can_unlock.Notify();
  EXPECT_TRUE(WaitFor(other_reader_locked));
  EXPECT_TRUE(WaitFor(other_writer_locked));
}

TEST(CountingMutexTest, WriterAwaitChange) {
  int state = 0;
  CountingMutex mutex;
  TestThread thread([&] {
    CountingMutexLock lock(mutex);
    state = 1;
    auto cond = [&] { return mutex.AssertReaderHeld(), state == 2; };
    mutex.Await(absl::Condition(&cond));
    mutex.AssertHeld();
  });

  CountingMutexLock lock(mutex);
  auto cond = [&] { return mutex.AssertReaderHeld(), state == 1; };
  mutex.Await(absl::Condition(&cond));
  state = 2;
}

TEST(CountingMutexTest, WriterAwaitChangeWithTimeout) {
  int state = 0;
  CountingMutex mutex;
  TestThread thread([&] {
    CountingMutexLock lock(mutex);
    state = 1;
    auto cond = [&] { return mutex.AssertReaderHeld(), state == 2; };
    EXPECT_TRUE(
        mutex.AwaitWithTimeout(absl::Condition(&cond), absl::Seconds(10)));
    mutex.AssertHeld();
  });

  CountingMutexLock lock(mutex);
  auto cond = [&] { return mutex.AssertReaderHeld(), state == 1; };
  EXPECT_TRUE(
      mutex.AwaitWithTimeout(absl::Condition(&cond), absl::Seconds(10)));
  state = 2;
}

TEST(CountingMutexTest, ReaderLockThenWriterAwaitChange) {
  // Ensure any state on the CountingMutex set by reader locking/unlocking does
  // not interfere with writer lock awaiting.
  int state = 0;

  CountingMutex mutex;
  mutex.lock_shared();
  mutex.unlock_shared();

  TestThread thread([&] {
    CountingMutexLock lock(mutex);
    state = 1;
    auto cond = [&] { return mutex.AssertReaderHeld(), state == 2; };
    mutex.Await(absl::Condition(&cond));
    mutex.AssertHeld();
  });

  CountingMutexLock lock(mutex);
  auto cond = [&] { return mutex.AssertReaderHeld(), state == 1; };
  mutex.Await(absl::Condition(&cond));
  state = 2;
}

TEST(CountingMutexTest, WriterRegainsWriterLockAfterAwait) {
  absl::Notification awaiter_can_unlock;
  absl::Notification awaiter_re_locked;

  // Make sure we await in the core logic, not the short-circuit when cond is
  // already true.
  int state = 0;
  CountingMutex mutex;
  TestThread thread([&] {
    CountingMutexLock lock(mutex);
    state = 1;
    auto cond = [&] { return mutex.AssertReaderHeld(), state == 2; };
    mutex.Await(absl::Condition(&cond));
    mutex.AssertHeld();
    awaiter_re_locked.Notify();
    EXPECT_TRUE(WaitFor(awaiter_can_unlock));
  });

  {
    CountingMutexLock lock(mutex);
    auto cond = [&] { return mutex.AssertReaderHeld(), state == 1; };
    mutex.Await(absl::Condition(&cond));
    state = 2;
  }
  EXPECT_TRUE(WaitFor(awaiter_re_locked));

  // Ensure we have a legitimate writer lock after awaiting, meaning no other
  // readers or writers are allowed until the awaiter is unlocked.
  absl::Notification other_reader_locked;
  TestThread other_reader_thread([&] {
    CountingMutexReaderLock lock(mutex);
    other_reader_locked.Notify();
  });
  EXPECT_FALSE(WaitFor(other_reader_locked, absl::Seconds(1)));

  absl::Notification other_writer_locked;
  TestThread other_writer_thread([&] {
    CountingMutexLock lock(mutex);
    other_writer_locked.Notify();
  });
  EXPECT_FALSE(WaitFor(other_writer_locked, absl::Seconds(1)));

  // Now let the awaiter thread unlock and ensure we can get the other locks.
  awaiter_can_unlock.Notify();
  EXPECT_TRUE(WaitFor(other_reader_locked));
  EXPECT_TRUE(WaitFor(other_writer_locked));
}

TEST(CountingMutexTest, ReaderAwaitTrueCondition) {
  CountingMutex mutex;
  CountingMutexReaderLock lock(mutex);
  auto cond = [&] { return mutex.AssertReaderHeld(), true; };

  mutex.Await(absl::Condition(&cond));

  EXPECT_TRUE(mutex.AwaitWithTimeout(absl::Condition(&cond), absl::Seconds(1)));
  EXPECT_TRUE(
      mutex.AwaitWithTimeout(absl::Condition(&cond), absl::Seconds(-1)));

  EXPECT_TRUE(mutex.AwaitWithDeadline(absl::Condition(&cond),
                                      absl::Now() + absl::Seconds(1)));
  EXPECT_TRUE(mutex.AwaitWithDeadline(absl::Condition(&cond),
                                      absl::Now() - absl::Seconds(1)));
}

TEST(CountingMutexTest, ReaderAwaitFalseCondition) {
  CountingMutex mutex;
  CountingMutexReaderLock lock(mutex);
  auto cond = [&] { return mutex.AssertReaderHeld(), false; };

  EXPECT_FALSE(
      mutex.AwaitWithTimeout(absl::Condition(&cond), absl::Seconds(1)));
  EXPECT_FALSE(
      mutex.AwaitWithTimeout(absl::Condition(&cond), absl::Seconds(-1)));

  EXPECT_FALSE(mutex.AwaitWithDeadline(absl::Condition(&cond),
                                       absl::Now() + absl::Seconds(1)));
  EXPECT_FALSE(mutex.AwaitWithDeadline(absl::Condition(&cond),
                                       absl::Now() - absl::Seconds(1)));
}

TEST(CountingMutexTest, ReaderAwaitTimeoutFailureHoldsLock) {
  absl::Notification awaiter_re_locked;
  absl::Notification awaiter_can_unlock;
  CountingMutex mutex;

  TestThread thread([&] {
    CountingMutexReaderLock lock(mutex);
    auto cond = [&] { return mutex.AssertReaderHeld(), false; };
    EXPECT_FALSE(
        mutex.AwaitWithTimeout(absl::Condition(&cond), absl::Seconds(1)));
    mutex.AssertReaderHeld();
    awaiter_re_locked.Notify();
    EXPECT_TRUE(WaitFor(awaiter_can_unlock));
  });
  EXPECT_TRUE(WaitFor(awaiter_re_locked));

  // Ensure we have a legitimate reader lock after awaiting, meaning no other
  // writers are allowed until the awaiter is unlocked, but other readers are
  // allowed.
  absl::Notification other_reader_locked;
  TestThread other_reader_thread([&] {
    CountingMutexReaderLock lock(mutex);
    other_reader_locked.Notify();
  });
  EXPECT_TRUE(WaitFor(other_reader_locked));

  absl::Notification other_writer_locked;
  TestThread other_writer_thread([&] {
    CountingMutexLock lock(mutex);
    other_writer_locked.Notify();
  });
  EXPECT_FALSE(WaitFor(other_writer_locked, absl::Seconds(1)));

  // Now let the awaiter thread unlock and ensure we can get the other writer
  // lock.
  awaiter_can_unlock.Notify();
  EXPECT_TRUE(WaitFor(other_writer_locked));
}

TEST(CountingMutexTest, ReaderAwaitChange) {
  absl::Notification awaiter_locked;
  absl::Notification await_completed;
  int state = 0;
  CountingMutex mutex;
  TestThread thread([&] {
    CountingMutexReaderLock lock(mutex);
    awaiter_locked.Notify();
    auto cond = [&] { return mutex.AssertReaderHeld(), state == 1; };
    mutex.Await(absl::Condition(&cond));
    mutex.AssertReaderHeld();
    await_completed.Notify();
  });

  {
    EXPECT_TRUE(WaitFor(awaiter_locked));
    CountingMutexLock lock(mutex);
    EXPECT_FALSE(WaitFor(await_completed, absl::Seconds(1)));
    state = 1;
    EXPECT_FALSE(WaitFor(await_completed, absl::Seconds(1)));
  }
  EXPECT_TRUE(WaitFor(await_completed));
}

TEST(CountingMutexTest, ReaderAwaitChangeWithTimeout) {
  absl::Notification awaiter_locked;
  absl::Notification await_completed;
  int state = 0;
  CountingMutex mutex;
  TestThread thread([&] {
    CountingMutexReaderLock lock(mutex);
    awaiter_locked.Notify();
    auto cond = [&] { return mutex.AssertReaderHeld(), state == 1; };
    EXPECT_TRUE(
        mutex.AwaitWithTimeout(absl::Condition(&cond), absl::Seconds(10)));
    mutex.AssertReaderHeld();
    await_completed.Notify();
  });

  {
    EXPECT_TRUE(WaitFor(awaiter_locked));
    CountingMutexLock lock(mutex);
    EXPECT_FALSE(WaitFor(await_completed, absl::Seconds(1)));
    state = 1;
    EXPECT_FALSE(WaitFor(await_completed, absl::Seconds(1)));
  }
  EXPECT_TRUE(WaitFor(await_completed));
}

TEST(CountingMutexTest, WriterLockThenReaderAwaitChange) {
  // Ensure any state on the CountingMutex set by writer locking/unlocking does
  // not interfere with reader lock awaiting.
  absl::Notification awaiter_locked;
  absl::Notification await_completed;
  int state = 0;

  CountingMutex mutex;
  mutex.lock();
  mutex.unlock();

  TestThread thread([&] {
    CountingMutexReaderLock lock(mutex);
    awaiter_locked.Notify();
    auto cond = [&] { return mutex.AssertReaderHeld(), state == 1; };
    mutex.Await(absl::Condition(&cond));
    mutex.AssertReaderHeld();
    await_completed.Notify();
  });

  {
    EXPECT_TRUE(WaitFor(awaiter_locked));
    CountingMutexLock lock(mutex);
    EXPECT_FALSE(WaitFor(await_completed, absl::Seconds(1)));
    state = 1;
    EXPECT_FALSE(WaitFor(await_completed, absl::Seconds(1)));
  }
  EXPECT_TRUE(WaitFor(await_completed));
}

TEST(CountingMutexTest, ReaderRegainsReaderLockAfterAwait) {
  absl::Notification awaiter_locked;
  absl::Notification awaiter_re_locked;
  absl::Notification awaiter_can_unlock;
  int state = 0;
  CountingMutex mutex;

  // Make sure we await in the core logic, not the short-circuit when cond is
  // already true.
  TestThread thread([&] {
    CountingMutexReaderLock lock(mutex);
    awaiter_locked.Notify();
    auto cond = [&] { return mutex.AssertReaderHeld(), state == 1; };
    mutex.Await(absl::Condition(&cond));
    mutex.AssertReaderHeld();
    awaiter_re_locked.Notify();
    EXPECT_TRUE(WaitFor(awaiter_can_unlock));
  });

  {
    EXPECT_TRUE(WaitFor(awaiter_locked));
    CountingMutexLock lock(mutex);
    state = 1;
  }
  EXPECT_TRUE(WaitFor(awaiter_re_locked));

  // Ensure we have a legitimate reader lock after awaiting, meaning no other
  // writers are allowed until the awaiter is unlocked, but other readers are
  // allowed.
  absl::Notification other_reader_locked;
  TestThread other_reader_thread([&] {
    CountingMutexReaderLock lock(mutex);
    other_reader_locked.Notify();
  });
  EXPECT_TRUE(WaitFor(other_reader_locked));

  absl::Notification other_writer_locked;
  TestThread other_writer_thread([&] {
    CountingMutexLock lock(mutex);
    other_writer_locked.Notify();
  });
  EXPECT_FALSE(WaitFor(other_writer_locked, absl::Seconds(1)));

  // Now let the awaiter thread unlock and ensure we can get the other writer
  // lock.
  awaiter_can_unlock.Notify();
  EXPECT_TRUE(WaitFor(other_writer_locked));
}

TEST(CountingMutexTest, MultipleAwaitsOnSameCondition) {
  CountingMutex mutex;
  bool b = false;
  auto c = [&] { return mutex.AssertReaderHeld(), b; };
  absl::Condition cond(&c);
  int expected_locks = 0;
  std::atomic<int> locks(0);

  std::vector<TestThread> threads;

  for (int i = 0; i < 2; ++i) {
    expected_locks++;
    threads.emplace_back([&] {
      CountingMutexLock lock(mutex);
      locks++;
      mutex.Await(cond);
    });
  }
  for (int i = 0; i < NumCPUs() * 2 - 1; ++i) {
    expected_locks++;
    threads.emplace_back([&] {
      CountingMutexReaderLock lock(mutex);
      locks++;
      mutex.Await(cond);
    });
  }

  while (locks != expected_locks) {
    absl::SleepFor(absl::Milliseconds(1));
  }

  CountingMutexLock lock(mutex);
  b = true;
}

// Reader/WriterLockWhen() are implemented in terms of Await(): perform
// basic tests with a high probability to hit the fast and slow paths.
TEST(CountingMutexTest, ReaderWriterLockWhen) {
  int state = 0;
  CountingMutex mutex;
  auto cond0 = [&] { return mutex.AssertReaderHeld(), state == 0; };
  auto cond1 = [&] { return mutex.AssertReaderHeld(), state == 1; };
  auto cond2 = [&] { return mutex.AssertReaderHeld(), state == 2; };
  auto cond3 = [&] { return mutex.AssertReaderHeld(), state == 3; };
  TestThread thread([&] {
    mutex.ReaderLockWhen(absl::Condition(&cond1));
    EXPECT_THAT(state, Eq(1));
    mutex.unlock_shared();

    mutex.WriterLockWhen(absl::Condition(&cond1));
    EXPECT_THAT(state, Eq(1));
    state = 2;
    mutex.unlock();

    mutex.ReaderLockWhen(absl::Condition(&cond3));
    EXPECT_THAT(state, Eq(3));
    mutex.unlock_shared();
  });

  mutex.WriterLockWhen(absl::Condition(&cond0));
  EXPECT_THAT(state, Eq(0));
  state = 1;
  mutex.unlock();

  mutex.ReaderLockWhen(absl::Condition(&cond2));
  EXPECT_THAT(state, Eq(2));
  mutex.unlock_shared();

  mutex.WriterLockWhen(absl::Condition(&cond2));
  EXPECT_THAT(state, Eq(2));
  state = 3;
  mutex.unlock();
}

template <typename LockType, bool longlived = false, bool alllonglived = false>
void ReaderWriterMutexTest_Exhaustive() {
  int threads = absl::GetFlag(FLAGS_threads) - 1;
  if (threads < 0) threads = NumCPUs() - 1;
  int wait_micros = absl::GetFlag(FLAGS_wait_micros);
  if (wait_micros == 0) wait_micros = 1;

  LockType lock;
  int64_t value1 = 0;
  int64_t value2 = 0;
  std::atomic<bool> done(false);
  std::atomic<int64_t> reads(0);
  std::atomic<int64_t> try_reads(0);
  std::atomic<int64_t> missed_reads(0);
  std::atomic<int64_t> writes(0);

  auto make_task = [&]() {
    return [&] {
      int64_t r = 0, tr = 0, mr = 0, w = 0;
      absl::Time end_time = absl::Now() + absl::Seconds(2);
      int wi = absl::GetFlag(FLAGS_write_interval);
      int wr = wi;
      while (absl::Now() < end_time) {
        if ((++r & 1) == 0) {
          lock.ReaderLock();
        } else {
          ++tr;
          if (!lock.ReaderTryLock()) {
            ++mr;
            continue;
          }
        }
        int v1 = value1, v2 = value2;
        bool same = v1 == v2;
        for (int j = 0; same && j < 100; ++j) {
          v2 = value2;
          same = (v1 + j) == (v2 + j);
        }
        lock.ReaderUnlock();
        if (v1 != v2) {
          FAIL() << "v1 != v2, " << v1 << " versus " << v2;
          return;
        }

        if (wr && --wr == 0) {
          ++w;
          lock.WriterLock();
          value1 = value1 + 1;
          for (int64_t j = value1 + 100; j != value1; --j) {
            value2 = j;
          }
          value2 = value1;
          lock.WriterUnlock();
          wr = wi;
        }
      }
      reads += r - tr;
      try_reads += tr;
      missed_reads += mr;
      writes += w;
      done = true;
    };
  };

  std::vector<TestThread> tasks;
  for (int i = 1; i < threads; ++i) {
    tasks.emplace_back(make_task());
  }

  for (auto& task : tasks) {
    task.Join();
  }
}

TEST(CountingMutexTest, Exhaustive) {
  ReaderWriterMutexTest_Exhaustive<CountingMutex>();
}

TEST(CountingMutexTest, ExhaustiveWithLongLivedReaders) {
  ReaderWriterMutexTest_Exhaustive<CountingMutex, true>();
}

TEST(CountingMutexTest, ExhaustiveWithAllLongLivedReaders) {
  ReaderWriterMutexTest_Exhaustive<CountingMutex, true, true>();
}

TEST(CountingMutexTest, FuzzTest) {
  // On each turn, each thread either obtains a lock, releases a lock, or sleeps
  // blocking others. The 'guarded' values are always either checked or written.
  // We use a boolean atomic stop instead of an end time inside the threads to
  // avoid ordering side effects of absl::Now() on the non sleeping turns.
  enum class State { kEmpty, kExclusive, kShared };
  int64_t value1 = 0;
  int64_t value2 = 0;
  CountingMutex mutex;
  std::vector<TestThread> threads;
  std::atomic<int> awaiter_count(0);
  std::atomic<bool> stop{false};
  std::atomic<int> done_count(0);

  const int kThreadCount = NumCPUs() * 2 - 1;
  for (int i = 0; i < kThreadCount; ++i) {
    ThreadMode mode = ThreadModeIfDefault(RandomThreadMode());
    threads.emplace_back(mode, [&]() ABSL_NO_THREAD_SAFETY_ANALYSIS {
      absl::BitGen gen;
      State state = State::kEmpty;
      while (!stop.load(std::memory_order_acquire) || state != State::kEmpty) {
        if (state == State::kShared) {
          ASSERT_THAT(value1, Eq(value2));
        } else if (state == State::kExclusive) {
          // Throw value out of balance
          ++value1;
        }

        // 1 in  5000 : sleep 1ms
        // 1 in  1000 : (and not locked) lock exclusive
        // 1 in   100 : sleep 1us
        // 1 in    50 : (and has reader lock) await value change
        // 1 in     2 : (and has writer lock) await value change
        int choice = absl::Uniform(gen, 0, 10000);
        if (choice < 2) {
          absl::SleepFor(absl::Milliseconds(1));
        } else if (choice < 10 && state == State::kEmpty) {
          mutex.lock();
          state = State::kExclusive;
        } else if (choice < 100) {
          absl::SleepFor(absl::Microseconds(1));
        } else {
          switch (state) {
            case State::kEmpty:
              mutex.lock_shared();
              state = State::kShared;
              break;
            case State::kShared:
              if (choice < 200) {
                awaiter_count.fetch_add(1, std::memory_order_relaxed);
                auto cond = [&, original_value = value1]() {
                  mutex.AssertReaderHeld();
                  return value1 != original_value && value1 == value2;
                };
                mutex.Await(absl::Condition(&cond));
                awaiter_count.fetch_sub(1, std::memory_order_relaxed);
              }
              mutex.unlock_shared();
              state = State::kEmpty;
              break;
            case State::kExclusive:
              // Restore value1 / value2 invariant before unlock
              value2 = value1;
              if (choice < 5000) {
                awaiter_count.fetch_add(1, std::memory_order_relaxed);
                auto cond = [&, original_value = value1]() {
                  mutex.AssertReaderHeld();
                  return value1 != original_value && value1 == value2;
                };
                mutex.Await(absl::Condition(&cond));
                awaiter_count.fetch_sub(1, std::memory_order_relaxed);
              }
              mutex.unlock();
              state = State::kEmpty;
              break;
          }
        }
      }
      done_count++;
    });
  }

  absl::SleepFor(absl::Seconds(2));
  stop.store(true, std::memory_order_release);

  // Make sure awaiters do not get stuck if the final active threads are all
  // awaiitng.
  while (done_count < kThreadCount) {
    if (awaiter_count.load(std::memory_order_relaxed) != 0) {
      CountingMutexLock lock(mutex);
      value2 = ++value1;
    }
    absl::SleepFor(absl::Milliseconds(100));
  }

  for (auto& thread : threads) {
    thread.Join();
  }
}

TEST(CountingMutexTest, ThreadAnnotations) {
  static CountingMutex& mu = *new CountingMutex;
  static int x ABSL_GUARDED_BY(mu) = 0;
  {
    mu.lock_shared();
    EXPECT_THAT(x, Eq(0));
    mu.unlock_shared();
  }
  {
    CountingMutexReaderLock l(mu);
    EXPECT_THAT(x, Eq(0));
  }
  {
    mu.lock();
    x = 0;
    mu.unlock();
  }
  {
    CountingMutexWriterLock l(mu);
    x = 0;
  }
}

TEST(CountingMutexTest, ReleasableCountingMutexLock) {
  CountingMutex mutex;
  {
    ReleasableCountingMutexLock lock(mutex);
    mutex.AssertHeld();
  }
  mutex.AssertNotHeld();
  {
    ReleasableCountingMutexLock lock(mutex);
    mutex.AssertHeld();
    lock.Release();
    mutex.AssertNotHeld();
    EXPECT_DEBUG_DEATH(mutex.AssertHeld(),
                       "thread should hold an exclusive lock");
  }
}

// `RunAtThreadExit` exercises the use case where a (global) CountingMutex is
// invoked from an exit handler which means after the thread local data has been
// destroyed. This test guarantees that sanitizers (MSAN / ASAN) don't trigger
// on accessing 'destroyed thread local data'.
// See also the comments on the 'ThreadContext' class definition.
TEST(CountingMutexTest, RunAtExit) {
  auto exit_fn = +[] {
    static auto* mutex1 = new CountingMutex;
    static auto* mutex2 = new CountingMutex;
    mutex1->lock_shared();
    mutex2->lock_shared();
    mutex2->unlock_shared();
    mutex1->unlock_shared();
  };
  atexit(exit_fn);
}

// `RunAtThreadExit` is similar to `AtExit` except that we invoke a
// CountingMutex here from inside a thread local destructor.
TEST(CountingMutexTest, RunAtThreadExit) {
  static auto* mutex1 = new CountingMutex;
  static auto* mutex2 = new CountingMutex;
  static thread_local struct PerThread {
    ~PerThread() { Run(); }

    void Run() {
      mutex1->lock();
      mutex1->unlock();
      mutex1->lock_shared();
      mutex2->lock_shared();
      mutex2->unlock_shared();
      mutex1->unlock_shared();
      mutex2->lock();
      mutex2->unlock();
    }
  } per_thread;

  per_thread.Run();
  std::vector<TestThread> threads;
  for (int i = 0; i < 16; ++i) {
    threads.emplace_back([&] { per_thread.Run(); });
  }
  for (auto& thread : threads) {
    thread.Join();
  }
}

TEST(CountingMutexTest, CountingMutexLockMaybe) {
  using MockFn = ::testing::MockFunction<bool()>;
  CountingMutex mu;

  {
    CountingMutexLockMaybe maybe_null1(nullptr);
  }

  {
    MockFn cond_fn;
    absl::Condition cond(&cond_fn, &MockFn::Call);
    EXPECT_CALL(cond_fn, Call()).Times(0);
    CountingMutexLockMaybe maybe_null2(nullptr, cond);
  }

  {
    CountingMutexLockMaybe maybe(&mu);
    mu.AssertHeld();
  }
  mu.AssertNotHeld();

  {
    MockFn cond_fn;
    absl::Condition cond(&cond_fn, &MockFn::Call);
    EXPECT_CALL(cond_fn, Call()).WillOnce(::testing::Return(true));
    CountingMutexLockMaybe maybe(&mu, cond);
    mu.AssertHeld();
  }
  mu.AssertNotHeld();
}

enum LockContention { kSingle, kShared };

template <typename RWL, LockContention contention>
void BM_ReaderWriter_ReaderLock(benchmark::State& state) {
  constexpr int kBatchSize = 100000;

  RWL private_lock;
  std::atomic<int64_t> total_locks(0);
  static RWL* shared_lock = new RWL;
  auto& lock = (contention == kSingle) ? private_lock : *shared_lock;

  while (state.KeepRunningBatch(kBatchSize)) {
    for (int i = 0; i < kBatchSize; ++i) {
      lock.ReaderLock();
      lock.ReaderUnlock();
    }
    total_locks += kBatchSize;
  }
  state.SetItemsProcessed(total_locks);
  state.SetBytesProcessed(total_locks);
}

template <typename RWL>
void BM_ReaderWriter_WriterLock(benchmark::State& state) {
  constexpr int kBatchSize = 100000;

  RWL lock;
  std::atomic<int64_t> total_locks(0);

  while (state.KeepRunningBatch(kBatchSize)) {
    for (int i = 0; i < kBatchSize; ++i) {
      lock.WriterLock();
      lock.WriterUnlock();
    }
    total_locks += kBatchSize;
  }
  state.SetItemsProcessed(total_locks);
  state.SetBytesProcessed(total_locks);
}

template <typename Lock>
void BM_ReaderLock_Contended(benchmark::State& state) {
  int64_t locks = 0;
  int64_t value1 = 0;
  int64_t value2 = 0;
  int wait_micros = absl::GetFlag(FLAGS_wait_micros);
  static auto& lock = *new Lock;

  int wi = absl::GetFlag(FLAGS_write_interval);
  int wr = wi;
  while (state.KeepRunning()) {
    lock.ReaderLock();
    int v1 = value1, v2 = value2;
    bool same = v1 == v2;
    for (int j = 0; same && j < 100; ++j) {
      v2 = value2;
      same = (v1 + j) == (v2 + j);
    }
    if (wait_micros && (v1 & 0x1000) == 0x1000) {
      absl::SleepFor(absl::Microseconds(wait_micros));
    }
    lock.ReaderUnlock();
    CHECK_EQ(v1, v2) << typeid(Lock).name();
    ++locks;

    if (wr && --wr == 0) {
      lock.WriterLock();
      int64_t i = value1 + 1;
      value1 = 7;
      value2 = -1;
      value1 = i;
      value2 = i;
      lock.WriterUnlock();
      wr = wi;
      ++locks;
    }
  }

  state.SetItemsProcessed(locks);
  state.SetBytesProcessed(locks);
}

// darwin_x86_64 (or any Mac/IOS x64) is a problem, but also a dying breed
#if !(defined(__APPLE__) && defined(__x86_64__))

BENCHMARK(BM_ReaderWriter_WriterLock<absl::Mutex>);
BENCHMARK(BM_ReaderWriter_WriterLock<CountingMutex>);

BENCHMARK(BM_ReaderWriter_ReaderLock<absl::Mutex, kSingle>);
BENCHMARK(BM_ReaderWriter_ReaderLock<CountingMutex, kSingle>);

#endif  // !(defined(__APPLE__) && defined(__x86_64__))

}  // namespace
}  // namespace concurrent

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
