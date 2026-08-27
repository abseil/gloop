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

// This tests common/thread.h/thread.cc/thread_options.h/thread_options.cc.
// I actually only test the periodic-thread stuff

#include "gloop/thread/thread.h"

#include <signal.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef __Fuchsia__
#include <sys/resource.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/log_severity.h"
#include "absl/base/no_destructor.h"
#include "absl/base/optimization.h"
#include "absl/base/thread_annotations.h"
#include "absl/cleanup/cleanup.h"
#include "absl/debugging/stacktrace.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/functional/any_invocable.h"
#include "absl/functional/bind_front.h"
#include "absl/functional/function_ref.h"
#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/barrier.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "benchmark/benchmark.h"
#include "gloop/base/callback.h"
#include "gloop/base/config.h"
#include "gloop/base/context.h"
#include "gloop/base/googleinit.h"
#include "gloop/base/init_google.h"
#include "gloop/base/port.h"
#include "gloop/base/raw_logging.h"
#include "gloop/base/scheduling/fiber_name.h"
#include "gloop/base/signal-handler.h"
#include "gloop/base/sysinfo.h"
#include "gloop/base/timer.h"
#include "gloop/base/tracecontext.h"
#include "gloop/strings/arena-string.h"
#include "gloop/thread/fiber/fiber.h"
#include "gloop/thread/thread-internal.h"
#include "gloop/thread/thread_control.h"
#include "gloop/thread/thread_options.h"
#include "gloop/thread/threadpool.h"
#include "gloop/util/functional/from_callback.h"
#include "gloop/util/functional/to_callback.h"
#include "gloop/util/gtl/container_logging.h"
#include "gloop/util/priority/io-priority.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#if !PORTABLE_BASE
#define AVOID_TRACECONTEXT 1
#endif

#if THREAD_HAVE_THREAD_CONTROL
constexpr int kBaselineThreads = 1;
#else
// This is coming from main, ExitTimeoutWatcher, and ThreadLivenessWatcher.
constexpr int kBaselineThreads = 3;
#endif

ABSL_FLAG(bool, crashme, false, "Crash so you can see stack trace output");

// friend that is allowed to use thread::DeprecatedThreadControl.
class DeprecatedSingleThreadedTest {
 public:
  static void AvoidBackgroundThreads() {
    ::thread::DeprecatedThreadControl::AvoidBackgroundThreads();
  }
};

namespace {

using ::testing::Contains;
using ::testing::ContainsRegex;
using ::testing::ExplainMatchResult;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsSupersetOf;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::UnorderedElementsAreArray;

// The timeout used for Thread_ForEach() and Thread_ProcessStackTraces()
// invocations.  We choose a conservative value, higher than the default
// timeouts, to increase reliability on heavily loaded forge machines.
constexpr int kPerThreadTimeoutMs = 100;

void TestCallback(Thread* thd) {
  absl::SleepFor(absl::Seconds(2));
  VLOG(1) << "[2sec sleep done]";
}

void TestClosure() {
  absl::SleepFor(absl::Seconds(2));
  VLOG(1) << "[2sec sleep done]";
}

void IncrementMe(int* x) { ++*x; }

void DieHorribly() {
#if AVOID_TRACECONTEXT
  static const char* status_string = "Hi, mom!";
  base::WithThreadStatus trace_status_switcher(status_string);
#endif
  char* null = nullptr;
  strcpy(null, "Hi!");  // NOLINT
}

// Helper to check if a thread is a background thread that is not relevant to
// the test. These background threads (such as the global "timedcall" timer
// executor and the fiber scheduling domain threads "-SDomainT" or "-PDomainT")
// are lazy-spawned on-demand during tests and remain active for the lifetime of
// the process by design. To prevent them from polluting active thread count
// assertions when tests are executed sequentially or shuffled (e.g. under
// --gunit_shuffle), we filter them out unconditionally.
bool IsBackgroundExecutorThread(const LiveThread* thread) {
  const char* prefix = LiveThread_NamePrefix(thread);
  if (prefix == nullptr) return false;
  absl::string_view prefix_sv(prefix);
  return prefix_sv == "timedcall" || absl::EndsWith(prefix_sv, "-SDomainT") ||
         absl::EndsWith(prefix_sv, "-PDomainT");
}

// Wait up to 60 seconds for detached threads to fully exit and unregister
// themselves, so they do not leak into subsequent tests.
void WaitForDetachedThreadsToExit() {
  auto count_thread_fn = [](void* arg, const LiveThread* thread) -> bool {
    if (IsBackgroundExecutorThread(thread)) {
      return false;
    }
    *static_cast<int*>(arg) += 1;
    return false;
  };

  for (int i = 0; i < 600; ++i) {
    int count = 0;
    Thread_ForEach(count_thread_fn, &count, nullptr, nullptr, 0);
    if (count <= kBaselineThreads) {
      return;
    }
    absl::SleepFor(absl::Milliseconds(100));
  }

  GTEST_FAIL() << "Detached threads did not exit in time";
}

TEST(ThreadTest, CheckClosureThread) {
  int y(3);
  ClosureThread t([&y] { IncrementMe(&y); });
  t.SetJoinable(true);
  EXPECT_EQ(y, 3);
  t.Start();

  if (absl::GetFlag(FLAGS_crashme)) {
    // Launch a thread that crashes, to see what it looks like.
    ClosureThread u(DieHorribly);
    u.SetJoinable(true);
    u.Start();
    u.Join();
  }

  t.Join();
  EXPECT_EQ(y, 4);

  // now with options and name:
  const char* kNamePrefix = "ClosureTest";
  ClosureThread t1(thread::Options(), kNamePrefix, [&y] { IncrementMe(&y); });
  t1.SetJoinable(true);
  EXPECT_EQ(y, 4);
  t1.Start();
  EXPECT_EQ(t1.name_prefix(), kNamePrefix);
  t1.Join();
  EXPECT_EQ(y, 5);
}

TEST(ThreadTest, ClosureThreadFunctor) {
  std::atomic<bool> val{false};
  ClosureThread t1([&val]() { val = true; });
  t1.SetJoinable(true);
  t1.Start();
  t1.Join();
  EXPECT_TRUE(val.load());

  ClosureThread t2(thread::Options(), "foo", [&val]() { val = false; });
  t2.SetJoinable(true);
  t2.Start();
  t2.Join();
  EXPECT_FALSE(val.load());
}

TEST(ThreadTest, StartDetachedThread) {
  absl::Notification notify;
  StartDetachedThread("test", [&notify] { notify.Notify(); });
  notify.WaitForNotification();
  // If the test doesn't hang, it has succeeded.
  WaitForDetachedThreadsToExit();
}

class MemberThreadChecker {
 public:
  MemberThreadChecker() : counter_(0) {}
  int Counter() {
    absl::MutexLock lock(mutex_);
    return counter_;
  }
  void IncrementCounter() {
    absl::MutexLock lock(mutex_);
    ++counter_;
  }

 private:
  absl::Mutex mutex_;
  int counter_;
};

TEST(ThreadTest, CheckMemberThread) {
  MemberThreadChecker checker;
  MemberThread<MemberThreadChecker> t1(&checker,
                                       &MemberThreadChecker::IncrementCounter);
  t1.SetJoinable(true);
  EXPECT_EQ(checker.Counter(), 0);
  t1.Start();
  t1.Join();
  EXPECT_EQ(checker.Counter(), 1);

  // now with options and name:
  const char* kNamePrefix = "MemberTest";
  MemberThread<MemberThreadChecker> t2(thread::Options(), kNamePrefix, &checker,
                                       &MemberThreadChecker::IncrementCounter);
  t2.SetJoinable(true);
  EXPECT_EQ(checker.Counter(), 1);
  t2.Start();
  EXPECT_EQ(t2.name_prefix(), kNamePrefix);
  t2.Join();
  EXPECT_EQ(checker.Counter(), 2);
}

TEST(ThreadTest, CheckDetachableThread) {
  absl::Notification notification;
  StartDetachedThread(thread::Options(), "testing_thread",
                      [&notification] { notification.Notify(); });
  notification.WaitForNotification();
  WaitForDetachedThreadsToExit();
}

TEST(ThreadTest, StartDetachedThreadWithCustomOptions) {
  absl::Notification notify;
  StartDetachedThread(
      // Arbitrary custom options.
      thread::Options().set_joinable(false).set_stack_size(1024 * 1024),
      "custom_detached", [&notify] { notify.Notify(); });
  notify.WaitForNotification();
  WaitForDetachedThreadsToExit();
}

TEST(ThreadTest, DetachedThreadPreservesContext) {
  absl::Notification notify;
  {
    base::WithThreadStatus wts("banana");
    StartDetachedThread("ctx_detached", [&notify] {
      EXPECT_STREQ(base::CurrentThreadStatus(), "banana");
      notify.Notify();
    });
  }
  notify.WaitForNotification();
  WaitForDetachedThreadsToExit();
}

TEST(ThreadTest, StackAndGuardSizeOptions) {
  thread::Options options;
  options.set_joinable(true);
  options.set_stack_size(65536);  // 64KB custom stack size
  options.set_guard_size(4096);   // 4KB custom guard size

  ClosureThread t(options, "CustomStackGuardThread", [] {});
  t.Start();
  t.Join();
}

class TidTestThread : public Thread {
 public:
  TidTestThread() : pthread_val_(0) {}

  void Run() override { pthread_val_ = pthread_self(); }

  pthread_t pthread_val() const { return pthread_val_; }

 private:
  pthread_t pthread_val_;
};

TEST(ThreadTest, CheckChildTid) {
  for (int i = 0; i < 5000; ++i) {
    TidTestThread thread;
    thread.SetJoinable(true);
    thread.Start();
    thread.Join();
    EXPECT_EQ(thread.tid(), thread.pthread_val())
        << "Mismatch occurred at iteration: " << i;
    //  Yield whenever i+1 is a power of 2.
    if ((i & (i + 1)) == 0) sched_yield();
  }
}

bool ShouldRunSignalTest() {
  if (!absl::debugging_internal::StackTraceWorksForTest()) {
    LOG(WARNING)
        << "Skipping because stack traces are not expected to work in tests.";
    return false;
  }
#ifdef ABSL_HAVE_THREAD_SANITIZER
  // TODO: Re-enable after fixing signal handler problems with test.
  LOG(WARNING)
      << "Skipping due to unreliable signal delivery (see b/62138055).";
  return false;
#else
  // ShouldInstallDefaultSignalHandler returns false if there is already a
  // handler installed, so remove any existing handler before calling it.
  struct sigaction ign = {};
  sigemptyset(&ign.sa_mask);
  ign.sa_handler = SIG_IGN;
  struct sigaction sa_prev = {};
  sigaction(GOOGLE_OBSCURE_SIGNAL, &ign, &sa_prev);

  bool ok =
      ShouldInstallDefaultSignalHandler("stackdump", GOOGLE_OBSCURE_SIGNAL);

  // Restore the existing handler (if present).
  sigaction(GOOGLE_OBSCURE_SIGNAL, &sa_prev, nullptr);

  LOG_IF(WARNING, !ok)
      << "Requires \"-install_signal_handlers=true\" and "
         "\"-install_named_signal_handlers=stackdump\"; skipping";
  return ok;
#endif  // ABSL_HAVE_THREAD_SANITIZER
}

TEST(ThreadTest, CheckOptions) {
  thread::Options opt;

  opt.set_joinable(false);
  opt.set_stack_size(17);
  EXPECT_FALSE(opt.joinable());
  EXPECT_EQ(opt.stack_size(), 17);

  opt.set_joinable(true).set_stack_size(18);
  EXPECT_TRUE(opt.joinable());
  EXPECT_EQ(opt.stack_size(), 18);

  opt.set_io_priority(2, 6);
  EXPECT_EQ(opt.io_class(), 2);
  EXPECT_EQ(opt.io_priority_level(), 6);
}

class TestThreadStackWriter : public ThreadStackWriter {
 public:
  void Write(const char* data, int data_length) override {
    buffer_.append(data, data_length);
  }

  std::string String() const { return buffer_; }

 private:
  std::string buffer_;
};

// TODO: This test is crashing on darwin, and flaky on Android.
#if !defined(__APPLE__) && !defined(__ANDROID__)

const int kThreads = 100;

// Wait ensures the threads started below in CheckThreadSignalSafeDumpStacks
// have predictable behavior. 2 Barriers are used to coordinate startup and
// shutdown.
void Wait(absl::Barrier* started, absl::Barrier* done) {
  started->Block();
  done->Block();
}

TEST(ThreadTest, CheckThreadSignalSafeDumpStacks) {
  // Fire up a set number of threads which wait
  // to indicate they're started and then wait again
  // before exiting.
  std::vector<std::unique_ptr<Thread>> li;
  li.reserve(kThreads);
  absl::Barrier started(kThreads + 1);
  absl::Barrier done(kThreads + 1);
  absl::Cleanup cleanup = [&] {
    // Unblock all the threads and then join each for cleanup.
    done.Block();
    for (auto& thr : li) {
      thr->Join();
    }
    li.clear();
  };
  for (int i = 0; i < kThreads; ++i) {
    auto thr = std::make_unique<ClosureThread>(
        thread::Options().set_joinable(true),
        absl::StrFormat("dumpthread-%d", i),
        absl::bind_front(Wait, &started, &done));
    thr->Start();
    li.push_back(std::move(thr));
  }

  // Make sure they're all running.
  started.Block();

  // Dump everything into the writer.
  TestThreadStackWriter writer;
  Thread_SignalSafe_DumpStacksTo(&writer);

  // Test output from writer by making sure all threads
  // started above are in the dump.
  std::string dump = writer.String();
  for (int i = 0; i < kThreads; ++i) {
    std::string name = absl::StrFormat("(name: dumpthread-%d", i);
    EXPECT_THAT(dump, HasSubstr(name))
        << "Looking for thread signature: " << name;
  }

  EXPECT_THAT(dump, HasSubstr("--- Memory map: ---"))
      << "Looking for '--- Memory map: ---' in output";
}

#endif  // !__APPLE__ && !__ANDROID__

template <typename T>
class PeriodicThreadHelper {
 public:
  PeriodicThreadHelper() = default;

  ~PeriodicThreadHelper() {
    EXPECT_TRUE(exited)
        << "PeriodicThreadHelper destroyed before Exit() was called";
    if (!exited) {
      target.thread.Exit();
    }
  }

  bool Signal(bool wait) { return target.thread.Signal(wait); }
  void Exit() {
    exited = true;
    target.thread.Exit();
  }

 private:
  T target;
  bool exited = false;
};

struct PeriodicThreadTestTarget {
 public:
  PeriodicThreadTestTarget() : thread(this) {}

  void RunInThread(PeriodicThread<PeriodicThreadTestTarget>*) {
    absl::SleepFor(absl::Seconds(2));
    VLOG(1) << "[2sec sleep done]";
  }

  PeriodicThread<PeriodicThreadTestTarget> thread;
};

struct CallbackTestTarget {
 public:
  CallbackTestTarget()
      : thread(::util::functional::CallbackFunctor<Thread*>(&TestCallback)) {}

  PeriodicThread<::util::functional::CallbackFunctor<Thread*>> thread;
};

struct ClosureTestTarget {
 public:
  ClosureTestTarget()
      : closure(::util::functional::ToPermanentCallback(&TestClosure)),
        thread(closure.get()) {}

  std::unique_ptr<Closure> closure;
  PeriodicThread<Closure> thread;
};

template <typename T>
class PeriodicThreadTest : public ::testing::Test {};

using PeriodicThreadTypes =
    ::testing::Types<PeriodicThreadHelper<PeriodicThreadTestTarget>,
                     PeriodicThreadHelper<CallbackTestTarget>,
                     PeriodicThreadHelper<ClosureTestTarget>>;
TYPED_TEST_SUITE(PeriodicThreadTest, PeriodicThreadTypes);

TYPED_TEST(PeriodicThreadTest, CheckPeriodicThreads) {
  TypeParam helper;
  WallTimer timer;
  bool signaled;
  double elapsed;

  VLOG(1) << "Sending first thread off (should be 1)...";
  timer.Restart();
  signaled = helper.Signal(true);
  elapsed = timer.Get();
  VLOG(1) << "First thread signal returned: " << signaled
          << " elapsed: " << elapsed;
  EXPECT_TRUE(signaled);
  EXPECT_LT(elapsed, 0.2);  // shouldn't have had to wait at all

  VLOG(1) << "Sending second thread off (should be 0)...";
  timer.Restart();
  signaled = helper.Signal(false);
  elapsed = timer.Get();
  VLOG(1) << "Second thread signal returned: " << signaled
          << " elapsed: " << elapsed;
  EXPECT_FALSE(signaled);
  EXPECT_LT(elapsed, 0.2);  // shouldn't have had to wait at all

  VLOG(1) << "Sending third thread off (should be 1, after a wait)...";
  timer.Restart();
  signaled = helper.Signal(true);
  elapsed = timer.Get();
  VLOG(1) << "Third thread signal returned: " << signaled
          << " elapsed: " << elapsed;
  EXPECT_TRUE(signaled);
  EXPECT_GT(elapsed, 1.8);  // should have to wait about 2 seconds

  VLOG(1) << "Telling thread to exit (should have to wait again)...";
  timer.Restart();
  helper.Exit();
  elapsed = timer.Get();
  VLOG(1) << "Exit completed in elapsed: " << elapsed;
  EXPECT_GT(elapsed, 1.8);  // should have to wait about 2 seconds
}

// Collects a list of thread IDs using Thread_ForEach. Assumes that
// insertion of a pthread_t into a vector is safe from an async signal
// handler, so long as the vector has the required capacity and
// multiple threads don't try to do it concurrently.
// Skips background threads. (See IsBackgroundExecutorThread.)
class ThreadCollector {
 public:
  struct ThreadTids {
    pid_t os_tid = 0;
    pthread_t pthread_tid = 0;

    bool operator==(const ThreadTids& rhs) const = default;

    [[maybe_unused]] friend void PrintTo(const ThreadTids& tids,
                                         std::ostream* os) {
      *os << "{os_tid: " << tids.os_tid << ", pthread_tid: " << tids.pthread_tid
          << "}";
    }
  };

  explicit ThreadCollector(int max_size) {
    // We reserve space for the maximum number of tids, so that "Add"
    // can be async-signal-safe when needed.  We don't bother to reserve
    // memory for name_prefixes_ and names_, since we can't collect them
    // in Add anyway (since it is not async-signal-safe to copy strings,
    // because they need to allocate memory).
    tids_.reserve(max_size);
  }

  // This type is neither copyable nor movable.
  ThreadCollector(const ThreadCollector&) = delete;
  ThreadCollector& operator=(const ThreadCollector&) = delete;

  // Must be async-signal-safe if signal_safety_required is true, and so
  // in that case will not collect a thread name.
  static void Add(void* arg, const LiveThread* thread,
                  bool signal_safety_required) {
    if (IsBackgroundExecutorThread(thread)) {
      return;
    }

    ThreadCollector* collector = static_cast<ThreadCollector*>(arg);
    ABSL_RAW_CHECK(collector->tids_.size() < collector->tids_.capacity(),
                   "ThreadCollector overflow");
    collector->tids_.push_back(
        ThreadTids(LiveThread_OS_TID(thread), LiveThread_Pthread_TID(thread)));
    if (!signal_safety_required) {
      collector->name_prefixes_.push_back(LiveThread_NamePrefix(thread));
      collector->names_.push_back(LiveThread_Name(thread));
    }
  }

  static bool TrueAdd(void* arg, const LiveThread* thread) {
    Add(arg, thread, false /* not async-signal-safe */);
    return true;
  }

  static bool FalseAdd(void* arg, const LiveThread* thread) {
    Add(arg, thread, false /* not async-signal-safe */);
    return false;
  }

  // Must be async-signal-safe, so cannot record thread name.
  static void AddInTarget(void* arg, ucontext_t* uc, const LiveThread* thread) {
    ABSL_RAW_CHECK(thread == Thread_GetMyLiveThread(), "LiveThread mismatch");
    Add(arg, thread, true /* async-signal-safe */);
  }

  static void AddStackTrace(void* arg, const LiveThread* thread,
                            const StackTrace* trace) {
    if (trace == nullptr) {
      // May happen if stack trace fetch timed out.
      return;
    }

    const void* pc_buffer[1];
    EXPECT_EQ(StackTrace_GetPCs(trace, -1, nullptr), 0);
    EXPECT_EQ(StackTrace_GetPCs(trace, 0, nullptr), 0);
    // There should always be at least one frame in the trace!
    EXPECT_EQ(StackTrace_GetPCs(trace, 1, pc_buffer), 1);
    Add(arg, thread, false /* not async-signal-safe */);
  }

  static void AddStackTraceLiveThreadState(void* arg,
                                           const LiveThreadState& state) {
    if (state.trace == nullptr) {
      // May happen if stack trace fetch timed out.
      return;
    }
    Add(arg, state.thread, false /* not async-signal-safe */);
  }

  void Reset() {
    tids_.clear();
    name_prefixes_.clear();
    names_.clear();
  }

  int Count() const { return tids_.size(); }

  const std::vector<ThreadTids>& ThreadIds() const { return tids_; }
  const std::vector<std::string>& NamePrefixes() const {
    return name_prefixes_;
  }
  const std::vector<std::string>& Names() const { return names_; }

 private:
  std::vector<ThreadTids> tids_;
  std::vector<std::string> name_prefixes_;
  std::vector<std::string> names_;
};

MATCHER_P2(ContainsNamedThread, tids, name, "") {
  const auto& tids_list = arg.ThreadIds();
  const auto& names_list = arg.Names();
  const size_t limit = std::min(tids_list.size(), names_list.size());
  for (size_t i = 0; i < limit; ++i) {
    if (tids_list[i] == tids && names_list[i] == name) {
      return true;
    }
  }
  return false;
}

// Class that implements a simple state machine for use in testing the
// LiveThread interfaces and registration of external threads.
class LiveThreadTestController {
 public:
  LiveThreadTestController() = default;

  // This type is neither copyable nor movable.
  LiveThreadTestController(const LiveThreadTestController&) = delete;
  LiveThreadTestController& operator=(const LiveThreadTestController&) = delete;

  // Steps in the created thread:
  //   <pre>
  //   MarkAsStarted
  //   if external thread:
  //     WaitForRegisterCommand / MarkAsRegistered
  //   WaitForStopCommand
  //   </pre>
  void MarkAsStarted() {
    thread_tids_.os_tid = GetTID();
    thread_tids_.pthread_tid = pthread_self();
    started_.Notify();
  }
  void WaitForRegisterCommand() { register_.WaitForNotification(); }
  void MarkAsRegistered() { registered_.Notify(); }
  void WaitForStopCommand() { stop_.WaitForNotification(); }

  // Steps in the parent thread:
  //   <pre>
  //   (start)
  //   WaitUntilStarted
  //   if external thread:
  //     Register / WaitUntilRegistered
  //   Stop
  //   (join)
  //   </pre>
  void WaitUntilStarted() { started_.WaitForNotification(); }
  void Register() { register_.Notify(); }
  void WaitUntilRegistered() { registered_.WaitForNotification(); }
  void Stop() {
    if (!stop_.HasBeenNotified()) {
      stop_.Notify();
    }
  }

  // Get this thread's TID (same value ::GetTID()'s return when called
  // in this thread).
  ThreadCollector::ThreadTids GetThreadTids() {
    CHECK(started_.HasBeenNotified());
    return thread_tids_;
  }

 private:
  absl::Notification started_;
  absl::Notification registered_;
  absl::Notification stop_;
  absl::Notification register_;

  // Thread's TIDs.  Set at startup.
  ThreadCollector::ThreadTids thread_tids_;
};

class LiveThreadTestThread : public Thread {
 public:
  LiveThreadTestController controller_;
  ThreadCollector::ThreadTids actual_;
  ThreadCollector::ThreadTids expected_;

  LiveThreadTestThread() { SetJoinable(true); }

  void Run() override {
    actual_ = ThreadCollector::ThreadTids(GetTID(), pthread_self());
    expected_ = ThreadCollector::ThreadTids(
        LiveThread_OS_TID(Thread_GetMyLiveThread()),
        LiveThread_Pthread_TID(Thread_GetMyLiveThread()));

    controller_.MarkAsStarted();
    controller_.WaitForStopCommand();
  }

  ThreadCollector::ThreadTids ThreadId() const { return actual_; }
};

TEST(ThreadTest, CheckForEach) {
  if (!ShouldRunSignalTest()) {
    GTEST_SKIP() << "Skipping signal test because preconditions are not met.";
  }

  // 6 threads max in this test.
  ThreadCollector for_each_collector(6);
  ThreadCollector in_each_collector(6);

  // Helper that retries a few times in case we timed out getting info.
  auto thread_foreach =
      [&](bool (*fn1)(void* arg, const LiveThread* thread), void* arg1,
          void (*fn2)(void* arg, ucontext_t* uc, const LiveThread* thread),
          void* arg2, int timeout) {
        const int kMaxAttempts = 5;
        for (int i = 0; i < kMaxAttempts; i++) {
          if (arg1 == &for_each_collector) for_each_collector.Reset();
          if (arg2 == &in_each_collector) in_each_collector.Reset();
          const int dropped = Thread_ForEach(fn1, arg1, fn2, arg2, timeout);
          if (dropped == 0) {
            return;
          }
          VLOG(1) << "Dropped " << dropped << " threads";
          // Wait a little in case we are competing with some bursty cpu load.
          absl::SleepFor(absl::Milliseconds(100));
        }
        LOG(WARNING) << "Could not avoid dropped stack trace in "
                     << kMaxAttempts << " attempts";
      };

  // Basic sanity: we believe we start out with one thread active.
  for_each_collector.Reset();
  thread_foreach(&for_each_collector.FalseAdd, &for_each_collector, nullptr,
                 nullptr, kPerThreadTimeoutMs);
  ASSERT_EQ(for_each_collector.Count(), kBaselineThreads);
  ASSERT_THAT(for_each_collector.ThreadIds(),
              Contains(ThreadCollector::ThreadTids(GetTID(), pthread_self())));

  LiveThreadTestThread t1, t2, t3;
  t1.Start();
  absl::Cleanup cleanup1 = [&t1] {
    t1.controller_.Stop();
    t1.Join();
  };
  t2.Start();
  absl::Cleanup cleanup2 = [&t2] {
    t2.controller_.Stop();
    t2.Join();
  };
  t3.Start();
  absl::Cleanup cleanup3 = [&t3] {
    t3.controller_.Stop();
    t3.Join();
  };

  t1.controller_.WaitUntilStarted();
  t2.controller_.WaitUntilStarted();
  t3.controller_.WaitUntilStarted();

  EXPECT_EQ(t1.actual_, t1.expected_);
  EXPECT_EQ(t2.actual_, t2.expected_);
  EXPECT_EQ(t3.actual_, t3.expected_);

  // Verify that all threads are now running, without running anything
  // in their contexts.
  for_each_collector.Reset();
  in_each_collector.Reset();
  thread_foreach(&for_each_collector.FalseAdd, &for_each_collector,
                 &in_each_collector.AddInTarget, &in_each_collector,
                 kPerThreadTimeoutMs /* should be unused */);
  EXPECT_EQ(for_each_collector.Count(), kBaselineThreads + 3);
  EXPECT_THAT(
      for_each_collector.ThreadIds(),
      IsSupersetOf({ThreadCollector::ThreadTids(GetTID(), pthread_self()),
                    t1.controller_.GetThreadTids(),
                    t2.controller_.GetThreadTids(),
                    t3.controller_.GetThreadTids()}));
  ASSERT_EQ(in_each_collector.Count(), 0);

  // Verify that all threads are now running, and run a function in
  // their contexts.
  for_each_collector.Reset();
  in_each_collector.Reset();
  thread_foreach(&for_each_collector.TrueAdd, &for_each_collector,
                 &in_each_collector.AddInTarget, &in_each_collector,
                 kPerThreadTimeoutMs);
  EXPECT_EQ(for_each_collector.Count(), kBaselineThreads + 3);
  EXPECT_THAT(
      for_each_collector.ThreadIds(),
      IsSupersetOf({ThreadCollector::ThreadTids(GetTID(), pthread_self()),
                    t1.controller_.GetThreadTids(),
                    t2.controller_.GetThreadTids(),
                    t3.controller_.GetThreadTids()}));
  EXPECT_EQ(for_each_collector.ThreadIds(), in_each_collector.ThreadIds());

  // Same, but run a function by using a NULL for_each function.
  // Reuses contents of for_each_collector from above.
  in_each_collector.Reset();
  thread_foreach(nullptr, nullptr, &in_each_collector.AddInTarget,
                 &in_each_collector, kPerThreadTimeoutMs);
  EXPECT_EQ(in_each_collector.ThreadIds(), for_each_collector.ThreadIds());

  std::move(cleanup1).Invoke();
  std::move(cleanup2).Invoke();

  // Verify that only two threads are now running
  for_each_collector.Reset();
  thread_foreach(&for_each_collector.FalseAdd, &for_each_collector, nullptr,
                 nullptr, kPerThreadTimeoutMs);
  EXPECT_EQ(for_each_collector.Count(), kBaselineThreads + 1);
  EXPECT_THAT(
      for_each_collector.ThreadIds(),
      IsSupersetOf({ThreadCollector::ThreadTids(GetTID(), pthread_self()),
                    t3.controller_.GetThreadTids()}));

  std::move(cleanup3).Invoke();

  // We should be back to one thread active.
  for_each_collector.Reset();
  thread_foreach(&for_each_collector.FalseAdd, &for_each_collector, nullptr,
                 nullptr, kPerThreadTimeoutMs);
  EXPECT_EQ(for_each_collector.Count(), kBaselineThreads);
  EXPECT_THAT(for_each_collector.ThreadIds(),
              Contains(ThreadCollector::ThreadTids(GetTID(), pthread_self())));
}

// Special Thread_ForEach for_each function used by
// CheckInEachWhileExiting.  Returns true (indicating to run the
// in_each function) for all threads, and also causes the
// LiveThreadTestThread pointed to by testthreadv to start to exit.
// This violates the interface spec for Thread_ForEach, but is safe in
// the current implementation and is needed to make this test
// deterministic.
bool StopTestThreadFromForEach(void* testthreadv,
                               const LiveThread* target_thread) {
  LiveThreadTestThread* testthread =
      static_cast<LiveThreadTestThread*>(testthreadv);

  if (pthread_equal(LiveThread_Pthread_TID(target_thread), testthread->tid())) {
    testthread->controller_.Stop();

    // We cannot call testthread->Join() here to make sure the thread
    // has exited, since the thread cannot actually exit yet (because
    // Thread_ForEach holds thread_starter_mutex).  We're just trying
    // to get it to start to exit, to destroy thread-local data.  We
    // delay a bit to give it a chance to wake up and start to exit.
    absl::SleepFor(absl::Milliseconds(500));
  }

  return true;  // Run in_each in all threads.
}

// This test verifies that Thread_ForEach works properly if a thread
// starts to exit while Thread_ForEach tries to run an in_each
// function in that thread.  (This is not a comprehensive test for all
// possible race conditions, but specifically tests for one race
// condition that was found in the code.)
//
// To test this case determinisically, we use the filter function
// (for_each) to cause a thread to (start to) exit at the same time it
// commits Thread_ForEach to try to run the in_each function in that
// thread's context.
TEST(ThreadTest, CheckInEachWhileExiting) {
  if (!ShouldRunSignalTest()) {
    GTEST_SKIP() << "Skipping signal test because preconditions are not met.";
  }

  LiveThreadTestThread t;
  t.Start();
  absl::Cleanup cleanup = [&t] {
    t.controller_.Stop();
    t.Join();
  };
  t.controller_.WaitUntilStarted();
  EXPECT_EQ(t.actual_, t.expected_);

  ThreadCollector collector(4);
  collector.Reset();

  int num_fails =
      Thread_ForEach(StopTestThreadFromForEach, &t, &collector.AddInTarget,
                     &collector, kPerThreadTimeoutMs);
  // Could not collect from 't', since it has started to exit.
  EXPECT_EQ(num_fails, 1);

  // Verify that only non-exiting threads were collected.
  EXPECT_EQ(collector.Count(), kBaselineThreads);
  EXPECT_THAT(collector.ThreadIds(),
              Contains(ThreadCollector::ThreadTids(GetTID(), pthread_self())));
}

void* ExternalThreadMain(void* arg) {
  LiveThreadTestController* controller =
      static_cast<LiveThreadTestController*>(arg);

  controller->MarkAsStarted();
  // Use CHECK to immediately abort on unexpected failures in background
  // threads.
  CHECK_EQ(Thread_GetMyLiveThread(), nullptr);

  controller->WaitForRegisterCommand();
  Thread_RegisterExternalThread("example");
  CHECK_NE(Thread_GetMyLiveThread(), nullptr);
  CHECK_EQ(GetTID(), LiveThread_OS_TID(Thread_GetMyLiveThread()));
  CHECK_EQ(pthread_self(), LiveThread_Pthread_TID(Thread_GetMyLiveThread()));
  controller->MarkAsRegistered();

  controller->WaitForStopCommand();
  return nullptr;
}

TEST(ThreadTest, CheckRegisterExternalThread) {
  // 4 threads max in this test.
  ThreadCollector for_each_collector(4);

  // Basic sanity: we believe we start out with one thread active.
  for_each_collector.Reset();
  Thread_ForEach(&for_each_collector.FalseAdd, &for_each_collector, nullptr,
                 nullptr, kPerThreadTimeoutMs);
  EXPECT_EQ(for_each_collector.Count(), kBaselineThreads);
  EXPECT_THAT(for_each_collector.ThreadIds(),
              Contains(ThreadCollector::ThreadTids(GetTID(), pthread_self())));

  LiveThreadTestController controller;
  pthread_t thread;
  ASSERT_EQ(pthread_create(&thread, nullptr, ExternalThreadMain, &controller),
            0);
  controller.WaitUntilStarted();

  // Still only one thread (external threads aren't registered
  // automatically).
  for_each_collector.Reset();
  Thread_ForEach(&for_each_collector.FalseAdd, &for_each_collector, nullptr,
                 nullptr, kPerThreadTimeoutMs);
  EXPECT_EQ(for_each_collector.Count(), kBaselineThreads);
  EXPECT_THAT(for_each_collector.ThreadIds(),
              Contains(ThreadCollector::ThreadTids(GetTID(), pthread_self())));

  controller.Register();
  controller.WaitUntilRegistered();

  // Expect to see both threads.
  for_each_collector.Reset();
  Thread_ForEach(&for_each_collector.FalseAdd, &for_each_collector, nullptr,
                 nullptr, kPerThreadTimeoutMs);
  EXPECT_EQ(for_each_collector.Count(), kBaselineThreads + 1);
  EXPECT_THAT(
      for_each_collector.ThreadIds(),
      IsSupersetOf({ThreadCollector::ThreadTids(GetTID(), pthread_self()),
                    controller.GetThreadTids()}));

  controller.Stop();
  EXPECT_EQ(pthread_join(thread, nullptr), 0);

  // Back to only one thread (external thread unregistered on exit).
  for_each_collector.Reset();
  Thread_ForEach(&for_each_collector.FalseAdd, &for_each_collector, nullptr,
                 nullptr, kPerThreadTimeoutMs);
  EXPECT_EQ(for_each_collector.Count(), kBaselineThreads);
  EXPECT_THAT(for_each_collector.ThreadIds(),
              Contains(ThreadCollector::ThreadTids(GetTID(), pthread_self())));
}

// RAII helper to make sure we clean up a test thread.
class ThreadScopedGuard final {
 public:
  ThreadScopedGuard() : thread_(nullptr) {}
  explicit ThreadScopedGuard(LiveThreadTestThread* thread) : thread_(thread) {}
  ThreadScopedGuard(const ThreadScopedGuard&) = delete;
  ThreadScopedGuard& operator=(const ThreadScopedGuard&) = delete;
  ThreadScopedGuard& operator=(ThreadScopedGuard&& other) noexcept {
    if (this != &other) {
      Reset();
      thread_ = std::exchange(other.thread_, nullptr);
    }
    return *this;
  }

  ~ThreadScopedGuard() { Reset(); }

  void Reset() {
    if (thread_ != nullptr) {
      thread_->controller_.Stop();
      thread_->Join();
      thread_ = nullptr;
    }
  }

 private:
  LiveThreadTestThread* thread_;
};

class ProcessStackTracesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!ShouldRunSignalTest()) {
      GTEST_SKIP() << "Skipping signal test; preconditions not met.";
    }

    t1_.Start();
    guard1_ = ThreadScopedGuard(&t1_);

    t2_.Start();
    guard2_ = ThreadScopedGuard(&t2_);

    t3_.Start();
    guard3_ = ThreadScopedGuard(&t3_);

    t1_.controller_.WaitUntilStarted();
    t2_.controller_.WaitUntilStarted();
    t3_.controller_.WaitUntilStarted();

    ASSERT_EQ(t1_.actual_, t1_.expected_);
    ASSERT_EQ(t2_.actual_, t2_.expected_);
    ASSERT_EQ(t3_.actual_, t3_.expected_);
  }

  void TearDown() override {
    if (!ShouldRunSignalTest()) {
      return;
    }

    // Explicitly trigger cleanups to stop and join the threads.
    guard3_.Reset();
    guard2_.Reset();
    guard1_.Reset();

    // Post-condition: Verify that only the main thread is active
    // after teardown.
    RetryOnDrop([this] {
      Thread_ProcessStackTracesArg arg;
      arg.filter = &filter_collector_.FalseAdd;
      arg.filter_arg = &filter_collector_;
      arg.per_thread_timeout_ms = kPerThreadTimeoutMs;

      return Thread_ProcessStackTraces(arg);
    });

    EXPECT_EQ(filter_collector_.Count(), kBaselineThreads);
    EXPECT_THAT(filter_collector_.ThreadIds(), Contains(self_));
  }

  ThreadCollector filter_collector_{16};
  ThreadCollector process_trace_collector_{16};
  ThreadCollector process_thread_collector_{16};
  ThreadCollector::ThreadTids self_{GetTID(), pthread_self()};

  // Note: The thread objects must be declared BEFORE the guard objects.
  // C++ destroys members in the reverse order of their declaration.
  // This ensures guards are destroyed first (stopping and joining the
  // threads) while the thread objects are still fully alive,
  // preventing use-after-free during fixture destruction.
  LiveThreadTestThread t1_;
  LiveThreadTestThread t2_;
  LiveThreadTestThread t3_;

  ThreadScopedGuard guard1_;
  ThreadScopedGuard guard2_;
  ThreadScopedGuard guard3_;

  void RetryOnDrop(absl::FunctionRef<int()> fn) {
    const int kMaxAttempts = 5;
    for (int i = 0; i < kMaxAttempts; i++) {
      filter_collector_.Reset();
      process_trace_collector_.Reset();
      process_thread_collector_.Reset();

      const int dropped = fn();
      if (dropped == 0) {
        return;
      }
      VLOG(1) << "Dropped " << dropped << " threads";
      // Wait a little in case we are competing with some bursty cpu load.
      absl::SleepFor(absl::Milliseconds(100));
    }
    LOG(WARNING) << "Could not avoid dropped stack trace in " << kMaxAttempts
                 << " attempts";
  }
};

TEST_F(ProcessStackTracesTest, SingleThreadActive) {
  // This test intentionally has an empty body to serve as an integration
  // canary, validating that the SetUp and TearDown lifecycle starts and
  // stops threads correctly in isolation without any extra test logic.
}

TEST_F(ProcessStackTracesTest, FilterDropsAll) {
  RetryOnDrop([this] {
    Thread_ProcessStackTracesArg arg;
    arg.filter = &filter_collector_.FalseAdd;
    arg.filter_arg = &filter_collector_;
    arg.process_trace = &process_trace_collector_.AddStackTrace;
    arg.process_trace_arg = &process_trace_collector_;
    arg.process_thread =
        &process_thread_collector_.AddStackTraceLiveThreadState;
    arg.process_thread_arg = &process_thread_collector_;
    arg.per_thread_timeout_ms = kPerThreadTimeoutMs;

    return Thread_ProcessStackTraces(arg);
  });

  EXPECT_EQ(process_trace_collector_.Count(), 0);
  EXPECT_EQ(process_thread_collector_.Count(), 0);
  EXPECT_EQ(filter_collector_.Count(), kBaselineThreads + 3);
  EXPECT_THAT(
      filter_collector_.ThreadIds(),
      IsSupersetOf({self_, t1_.ThreadId(), t2_.ThreadId(), t3_.ThreadId()}));
}

TEST_F(ProcessStackTracesTest, AllCallbacksExecute) {
  RetryOnDrop([this] {
    Thread_ProcessStackTracesArg arg;
    arg.filter = &filter_collector_.TrueAdd;
    arg.filter_arg = &filter_collector_;
    arg.process_trace = &process_trace_collector_.AddStackTrace;
    arg.process_trace_arg = &process_trace_collector_;
    arg.process_thread =
        &process_thread_collector_.AddStackTraceLiveThreadState;
    arg.process_thread_arg = &process_thread_collector_;
    arg.per_thread_timeout_ms = kPerThreadTimeoutMs;

    return Thread_ProcessStackTraces(arg);
  });

  constexpr int kExpectedThreads = kBaselineThreads + 3;
  EXPECT_EQ(process_trace_collector_.Count(), kExpectedThreads);
  EXPECT_EQ(process_thread_collector_.Count(), kExpectedThreads);
  EXPECT_EQ(filter_collector_.Count(), kExpectedThreads);
  EXPECT_THAT(
      filter_collector_.ThreadIds(),
      IsSupersetOf({self_, t1_.ThreadId(), t2_.ThreadId(), t3_.ThreadId()}));
  EXPECT_THAT(
      process_trace_collector_.ThreadIds(),
      IsSupersetOf({self_, t1_.ThreadId(), t2_.ThreadId(), t3_.ThreadId()}));
  EXPECT_THAT(
      process_thread_collector_.ThreadIds(),
      IsSupersetOf({self_, t1_.ThreadId(), t2_.ThreadId(), t3_.ThreadId()}));
}

TEST_F(ProcessStackTracesTest, NoFilterExecutesBoth) {
  RetryOnDrop([this] {
    Thread_ProcessStackTracesArg arg;
    arg.process_trace = &process_trace_collector_.AddStackTrace;
    arg.process_trace_arg = &process_trace_collector_;
    arg.process_thread =
        &process_thread_collector_.AddStackTraceLiveThreadState;
    arg.process_thread_arg = &process_thread_collector_;
    arg.per_thread_timeout_ms = kPerThreadTimeoutMs;

    return Thread_ProcessStackTraces(arg);
  });

  constexpr int kExpectedThreads = kBaselineThreads + 3;
  EXPECT_EQ(process_trace_collector_.Count(), kExpectedThreads);
  EXPECT_EQ(process_thread_collector_.Count(), kExpectedThreads);
  EXPECT_EQ(filter_collector_.Count(), 0);
  EXPECT_THAT(
      process_trace_collector_.ThreadIds(),
      IsSupersetOf({self_, t1_.ThreadId(), t2_.ThreadId(), t3_.ThreadId()}));
  EXPECT_THAT(
      process_thread_collector_.ThreadIds(),
      IsSupersetOf({self_, t1_.ThreadId(), t2_.ThreadId(), t3_.ThreadId()}));
}

constexpr absl::string_view kLongFiberName = "test-fiber-name";
constexpr absl::string_view kShortFiberName = "short-fiber";

// Flaps the current thread's fiber name between a non-empty name and
// nullptr in a tight loop to maximize the likelihood of signal
// interruption during fiber name updates.
class FiberNameFlappingThread : public Thread {
 public:
  FiberNameFlappingThread()
      : Thread(thread::Options().set_joinable(true), "flapping-fiber"),
        encoded_long_name_(thread::internal::EncodedFiberName::FromEncoded(
            strings::ArenaString::Encode(kLongFiberName, buf_long_))),
        encoded_short_name_(thread::internal::EncodedFiberName::FromEncoded(
            strings::ArenaString::Encode(kShortFiberName, buf_short_))) {}

  void Run() override {
    while (!stop_.load(std::memory_order_relaxed)) {
      thread::InternalSetCurrentFiberName(encoded_long_name_);
      thread::InternalSetCurrentFiberName(encoded_short_name_);
      thread::InternalSetCurrentFiberName(
          thread::internal::EncodedFiberName::None());
    }
    thread::InternalSetCurrentFiberName(
        thread::internal::EncodedFiberName::None());
  }

  void Stop() { stop_.store(true, std::memory_order_relaxed); }

 private:
  char buf_long_[32];
  char buf_short_[32];
  thread::internal::EncodedFiberName encoded_long_name_;
  thread::internal::EncodedFiberName encoded_short_name_;
  std::atomic<bool> stop_{false};
};

template <typename Fn>
void SetProcessThreadCallback(Thread_ProcessStackTracesArg& arg, Fn& fn) {
  arg.process_thread_arg = &fn;
  arg.process_thread = [](void* ctx, const LiveThreadState& state) {
    (*static_cast<Fn*>(ctx))(state);
  };
}

// Regression test for b/553104223.
// When fiber names were stored as a 16-byte absl::string_view in
// thread-local storage, updates were not atomic (requiring separate
// pointer and size stores). An asynchronous signal handler invoking
// FillStackTrace() could interrupt the thread mid-update and observe
// a torn read (e.g. empty string pointer with the previous name's
// non-zero length), leading to out-of-bounds reads and AddressSanitizer
// global-buffer-overflow crashes. This test stresses concurrent stack
// trace collection across multiple threads rapidly updating fiber names
// to ensure fiber name reads remain atomic and async-signal-safe.
TEST_F(ProcessStackTracesTest, FiberNameTornReadRepro) {
  constexpr int kNumThreads = 16;
  std::vector<std::unique_ptr<FiberNameFlappingThread>> threads;
  threads.reserve(kNumThreads);
  for (int i = 0; i < kNumThreads; ++i) {
    threads.push_back(std::make_unique<FiberNameFlappingThread>());
    threads.back()->Start();
  }

  std::atomic<int> non_empty_fiber_name_samples{0};
  auto on_thread = [&](const LiveThreadState& state) {
    if (state.fiber_name != nullptr && *state.fiber_name != '\0') {
      non_empty_fiber_name_samples.fetch_add(1, std::memory_order_relaxed);
      const absl::string_view fiber_name = state.fiber_name;
      EXPECT_TRUE(fiber_name == kLongFiberName || fiber_name == kShortFiberName)
          << "Observed torn or invalid fiber name: " << fiber_name;
    }
  };

  Thread_ProcessStackTracesArg arg;
  SetProcessThreadCallback(arg, on_thread);
  arg.per_thread_timeout_ms = kPerThreadTimeoutMs;

  absl::Time deadline = absl::Now() + absl::Seconds(1);
  while (absl::Now() < deadline) {
    Thread_ProcessStackTraces(arg);
  }

  EXPECT_GT(non_empty_fiber_name_samples.load(std::memory_order_relaxed), 0)
      << "Expected at least one non-empty fiber name sample during collection.";

  for (auto& t : threads) {
    t->Stop();
    t->Join();
  }
}

constexpr uint64_t kMaxFunctionSize = 0x80;

// Must be less than kMaxFunctionSize bytes
ABSL_ATTRIBUTE_NOINLINE void InnermostFrame() {
  volatile int64_t sum = 0;
  int itercount = 1024;
  for (int i = 0; i < itercount; i++) {
    for (int j = 0; j < itercount; j++) {
      for (int k = 0; k < itercount; k++) {
        sum += i * (j - k);
      }
    }
  }

  // Executed in background test thread, EXPECT_EQ is not safe.
  CHECK_EQ(sum, 0);
}

// Must be less than kMaxFunctionSize bytes
ABSL_ATTRIBUTE_NOINLINE ABSL_ATTRIBUTE_NO_TAIL_CALL void ProfilingWorkload() {
  InnermostFrame();
}

class StackTraceTestThread : public Thread {
 public:
  StackTraceTestThread(const thread::Options& options,
                       absl::string_view name_prefix)
      : Thread(options, name_prefix) {}
  void Run() override { ProfilingWorkload(); }
};

struct CollectedStack {
  std::string thread_name;
  std::vector<const void*> pcs;
  uint64_t trace_id = 0;
};

constexpr int kMaxCollectedFrames = 100;

ABSL_ATTRIBUTE_NOINLINE ABSL_ATTRIBUTE_NO_TAIL_CALL void CollectStack(
    void* arg, const LiveThread* thread, const StackTrace* trace) {
  std::vector<CollectedStack>* stacks =
      static_cast<std::vector<CollectedStack>*>(arg);
  stacks->push_back(CollectedStack());
  CollectedStack& stack = stacks->back();
  stack.pcs.resize(kMaxCollectedFrames);
  int depth = StackTrace_GetPCs(trace, stack.pcs.size(), stack.pcs.data());
  stack.pcs.resize(depth);
  stack.thread_name = LiveThread_Name(thread);
  stack.trace_id = StackTrace_GetTraceId(trace);
}

// Must be less than kMaxFunctionSize bytes
ABSL_ATTRIBUTE_NOINLINE ABSL_ATTRIBUTE_NO_TAIL_CALL void DoProcessStackTraces(
    Thread_ProcessStackTracesArg& arg) {
  Thread_ProcessStackTraces(arg);
}

std::vector<CollectedStack> CollectStacks() {
  std::vector<CollectedStack> stacks;
  Thread_ProcessStackTracesArg arg;
  arg.process_trace = &CollectStack;
  arg.process_trace_arg = &stacks;
  arg.per_thread_timeout_ms = kPerThreadTimeoutMs;
  DoProcessStackTraces(arg);
  return stacks;
}

const CollectedStack* FindThreadStack(absl::Span<const CollectedStack> stacks,
                                      absl::string_view thread_name_prefix) {
  const CollectedStack* thread_stack = nullptr;
  for (const auto& stack : stacks) {
    if (absl::StartsWith(stack.thread_name, thread_name_prefix)) {
      EXPECT_EQ(thread_stack, nullptr)
          << "Multiple threads with prefix " << thread_name_prefix << " found";
      thread_stack = &stack;
    }
  }
  return thread_stack;
}

bool FrameInFunction(const void* pc, const void* function) {
  uintptr_t function_address = reinterpret_cast<uintptr_t>(function);
  uintptr_t pc_address = reinterpret_cast<uintptr_t>(pc);
  return function_address <= pc_address &&
         pc_address < function_address + kMaxFunctionSize;
}

MATCHER_P(FrameInFunctionMatcher, address,
          absl::StrCat("is in function ", testing::PrintToString(address))) {
  return FrameInFunction(arg, reinterpret_cast<void*>(address));
}

TEST(ThreadTest, ProcessStackTracesCorrectStacks) {
  if (!ShouldRunSignalTest()) {
    GTEST_SKIP() << "Skipping signal test because preconditions are not met.";
  }
#if defined(ABSL_HAVE_MEMORY_SANITIZER)
  // TODO - Fix this test to work with MSAN.
  GTEST_SKIP() << "Skipping test because it doesn't work MSAN.";
#endif

  StackTraceTestThread t(thread::Options().set_joinable(true), "cpu_busy");
  t.Start();
  absl::SleepFor(absl::Milliseconds(50));
  std::vector<CollectedStack> stacks = CollectStacks();
  t.Join();

  const CollectedStack* main_stack = FindThreadStack(stacks, "main");
  ASSERT_THAT(main_stack, NotNull());
  ASSERT_GE(main_stack->pcs.size(), 1);
  EXPECT_THAT(main_stack->pcs[0], FrameInFunctionMatcher(&DoProcessStackTraces))
      << "Complete stack is " << gtl::LogContainer(main_stack->pcs);

  const CollectedStack* cpu_busy_stack = FindThreadStack(stacks, "cpu_busy");
  if (cpu_busy_stack != nullptr) {
    bool contains_innermost_frame = false;
    for (const void* pc : cpu_busy_stack->pcs) {
      if (FrameInFunction(pc, reinterpret_cast<void*>(&InnermostFrame))) {
        contains_innermost_frame = true;
      }
    }
    if (contains_innermost_frame) {
      // Test that signal-related frames have been correctly elided
      EXPECT_THAT(cpu_busy_stack->pcs[0],
                  FrameInFunctionMatcher(&InnermostFrame))
          << "Complete stack is " << gtl::LogContainer(cpu_busy_stack->pcs);
      EXPECT_THAT(cpu_busy_stack->pcs[1],
                  FrameInFunctionMatcher(&ProfilingWorkload))
          << "Complete stack is " << gtl::LogContainer(cpu_busy_stack->pcs);
    } else {
      // Must have grabbed the stack before or after InnermostFrame was running
      LOG(WARNING) << "Can't find InnermostFrame on stack";
    }
  } else {
    LOG(WARNING) << "Can't find cpu_busy stack";
  }
}

std::string ExpectedThreadName(absl::string_view prefix, pid_t pid) {
  return absl::StrFormat("%s/%d", prefix, static_cast<int64_t>(pid));
}

TEST(ThreadTest, CheckThreadNameMain) {
  EXPECT_STREQ(LiveThread_NamePrefix(Thread_GetMyLiveThread()), "main");
  EXPECT_EQ(LiveThread_Name(Thread_GetMyLiveThread()),
            ExpectedThreadName("main", GetTID()));
}

using ThreadNameTest = testing::TestWithParam<std::optional<std::string>>;

INSTANTIATE_TEST_SUITE_P(
    , ThreadNameTest, testing::Values(std::nullopt, "B0_b"),
    [](const testing::TestParamInfo<std::optional<std::string>>& info) {
      return info.param.value_or("nullopt");
    });

TEST_P(ThreadNameTest, CheckThreadName) {
  const std::optional<std::string>& custom_prefix = GetParam();
  LiveThreadTestThread t1;
  if (custom_prefix.has_value()) {
    t1.SetNamePrefix(*custom_prefix);
  }

  t1.Start();
  absl::Cleanup cleanup = [&t1] {
    t1.controller_.Stop();
    t1.Join();
  };
  t1.controller_.WaitUntilStarted();
  EXPECT_EQ(t1.actual_, t1.expected_);

  ThreadCollector collector(4);

  Thread_ProcessStackTracesArg arg;
  arg.filter = &collector.FalseAdd;
  arg.filter_arg = &collector;
  arg.per_thread_timeout_ms = kPerThreadTimeoutMs;
  Thread_ProcessStackTraces(arg);

  constexpr int kExpectedThreads = kBaselineThreads + 1;
  EXPECT_EQ(collector.Count(), kExpectedThreads);
  EXPECT_THAT(collector.NamePrefixes(), Contains("main"));

  ThreadCollector::ThreadTids main_tids(GetTID(), pthread_self());
  EXPECT_THAT(collector, ContainsNamedThread(
                             main_tids, ExpectedThreadName("main", GetTID())));

  constexpr absl::string_view kDefaultThreadNamePrefix =
      "gloop_thread_thread_unittest";

  const std::string expected_prefix =
      custom_prefix.value_or(std::string(kDefaultThreadNamePrefix));

  EXPECT_THAT(collector.NamePrefixes(), Contains(expected_prefix));
  EXPECT_THAT(collector,
              ContainsNamedThread(
                  t1.controller_.GetThreadTids(),
                  ExpectedThreadName(expected_prefix,
                                     t1.controller_.GetThreadTids().os_tid)));
}

using SanitizeThreadNamePrefixTest =
    testing::TestWithParam<std::tuple<std::string, std::string, std::string>>;

TEST_P(SanitizeThreadNamePrefixTest, SanitizesCorrectly) {
  const auto& [name, before, after] = GetParam();
  EXPECT_EQ(thread::SanitizeThreadNamePrefix(before), after);
}

INSTANTIATE_TEST_SUITE_P(
    , SanitizeThreadNamePrefixTest,
    testing::ValuesIn(
        std::vector<std::tuple<std::string, std::string, std::string>>{
            {"Empty", "", ""},
            {"Alphanumeric", "2abc", "_abc"},
            {"SpecialCharacters", "!@#$%^&*()", "__________"},
            {"MixedPadding", "2foo☺*&^%$#(@)!", "_foo_____________"},
            {"MixedGaps", "abc.def#ghi", "abc_def_ghi"},
            {"Dash", "123-abc", "_23-abc"},
        }),
    [](const testing::TestParamInfo<
        std::tuple<std::string, std::string, std::string>>& info) {
      return std::get<0>(info.param);
    });

using IsValidThreadNamePrefixTest =
    testing::TestWithParam<std::tuple<std::string, std::string, bool>>;

TEST_P(IsValidThreadNamePrefixTest, Test) {
  const auto& [name, prefix, expected] = GetParam();
  EXPECT_EQ(thread::IsValidThreadNamePrefix(prefix), expected);
}

INSTANTIATE_TEST_SUITE_P(
    , IsValidThreadNamePrefixTest,
    testing::ValuesIn(std::vector<std::tuple<std::string, std::string, bool>>{
        {"ValidPrefix", "ValidPrefix", true},
        {"ValidPrefixWithNumbers", "valid-prefix_123", true},
        {"EmptyPrefix", "", true},
        {"InvalidPrefix", "1Invalid", false},
        {"InvalidPrefixWithDot", "invalid.prefix", false},
        {"InvalidPrefixWithSpace", "invalid space", false},
    }),
    [](const testing::TestParamInfo<std::tuple<std::string, std::string, bool>>&
           info) { return std::get<0>(info.param); });

// Helper thread for CheckTraceContextThreadStatusRace.  See
// description in that function for information about what we're
// trying to test here.
class ThreadStatusRaceHelper : public Thread {
 public:
  LiveThreadTestController controller_;

  ThreadStatusRaceHelper() { SetJoinable(true); }

  void Run() override {
    const char* status_string = "foo";
    int page_size = getpagesize();
    // Background thread infrastructure verification: abort instantly if
    // allocations or syscalls fail.
    CHECK_GT(page_size, strlen(status_string));

    // Get a page, and put our status string in it.  We use mmap
    // rather than malloc because we want to unmap the page when we're
    // done with it (to cause a fault if it is accessed).
    void* status_addr = mmap(nullptr, page_size, PROT_READ | PROT_WRITE,
                             MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    CHECK_NE(MAP_FAILED, status_addr);
    snprintf(static_cast<char*>(status_addr), page_size, "%s", status_string);

    {
#if AVOID_TRACECONTEXT
      base::WithThreadStatus trace_status_switcher(
          static_cast<char*>(status_addr));
#endif

      sigset_t mask_to_block, orig_mask;
      CHECK_EQ(0, sigemptyset(&mask_to_block));
      CHECK_EQ(0, sigaddset(&mask_to_block, GOOGLE_OBSCURE_SIGNAL));
      CHECK_EQ(0, pthread_sigmask(SIG_BLOCK, &mask_to_block, &orig_mask));
      CHECK_EQ(0, sigismember(&orig_mask, GOOGLE_OBSCURE_SIGNAL));

      // We are now initialized enough that we want the main thread to
      // proceed.
      controller_.MarkAsStarted();

      int received_signal;
      CHECK_EQ(0, sigwait(&mask_to_block, &received_signal));
      CHECK_EQ(GOOGLE_OBSCURE_SIGNAL, received_signal);

      CHECK_EQ(0, pthread_sigmask(SIG_UNBLOCK, &mask_to_block, &orig_mask));
      CHECK_EQ(0, pthread_kill(pthread_self(), GOOGLE_OBSCURE_SIGNAL));

      // The thread status in the thread's TraceContext is reset here.
    }
    // Unmap the old status address to catch bogus uses.
    CHECK_EQ(0, munmap(status_addr, page_size));

    controller_.WaitForStopCommand();
  }
};

class NullThreadStackWriter : public ThreadStackWriter {
 public:
  void Write(const char* data, int data_length) override {}
};

TEST(ThreadTest, CheckTraceContextThreadStatusRace) {
  if (!ShouldRunSignalTest()) {
    GTEST_SKIP() << "Skipping signal test because preconditions are not met.";
  }

  // This code tests for a race condition that could occur with the
  // old implementation of TraceContext thread status gathering and
  // printing, which could cause garbage to be printed or even could
  // cause a crash.
  //
  // The sequence of events that can cause a problem:
  //   Thread A: signals thread B to get stack trace including
  //             trace context pointer.
  //   Thread B: fills in the trace, signals thread A to proceed.
  //   Thread B: unsets its TraceContext->thread_status() value,
  //             and clobbers/unmaps the old value.
  //   Thread A: attempts to dereference the thread status pointer.
  //             DOOM!
  //
  // To make this test have a remote hope of triggering the problem we
  // do something horrible.  In the target thread, we block the signal
  // used to gather the information, reissue it, then free the trace
  // status memory.  This is not guaranteed to be deterministic, but
  // should work pretty well with the NotifyThread implementation.
  // (If NotifyThread is changed to use a semaphore rather than
  // usleep, this will no longer be deterministic.)

  ThreadStatusRaceHelper helper_thread;
  helper_thread.Start();
  absl::Cleanup cleanup = [&helper_thread] {
    helper_thread.controller_.Stop();
    helper_thread.Join();
  };
  helper_thread.controller_.WaitUntilStarted();

  NullThreadStackWriter writer;
  Thread_ExtractStacks(&writer);
}

TEST(ThreadTest, CheckStatusFilledForThreadProcessStackTraces) {
  if (!ShouldRunSignalTest()) {
    GTEST_SKIP() << "Skipping signal test because preconditions are not met.";
  }

  // Reuse ThreadStatusRaceHelper since it sets a status so can verify
  // access.
  ThreadStatusRaceHelper helper_thread;
  helper_thread.Start();
  absl::Cleanup cleanup = [&helper_thread] {
    helper_thread.controller_.Stop();
    helper_thread.Join();
  };
  helper_thread.controller_.WaitUntilStarted();

  bool got_status = false;
  auto verify_status = +[](void* arg, const LiveThreadState& state) {
    if (state.thread_status != nullptr) {
      *static_cast<bool*>(arg) = true;  // Sets got_status
      // Make sure it's a valid and accessible pointer.
      VLOG(1) << "Thread status: " << state.thread_status;
    }
  };

  // Try a few times in case thread gets dropped.
  const int kMaxAttempts = 5;
  for (int i = 0; i < kMaxAttempts; i++) {
    Thread_ProcessStackTracesArg arg;
    arg.process_thread = verify_status;
    arg.process_thread_arg = &got_status;
    arg.per_thread_timeout_ms = kPerThreadTimeoutMs;
    int dropped = Thread_ProcessStackTraces(arg);
    if (dropped == 0) {
      break;
    }
    LOG(WARNING) << "Dropped " << dropped << " threads";
    // Wait a little in case we are competing with some bursty cpu load.
    absl::SleepFor(absl::Milliseconds(100));
  }
  EXPECT_TRUE(got_status) << "Did not get thread status after " << kMaxAttempts
                          << " attempts";
}

class SigIgnoreTestThread : public Thread {
 public:
  LiveThreadTestController controller_;
  ThreadCollector::ThreadTids actual_;
  ThreadCollector::ThreadTids expected_;
  int sigemptyset_res_ = -1;
  int sigaddset_res_ = -1;
  int pthread_sigmask_block_res_ = -1;
  int sigismember_res_ = -1;
  int pthread_sigmask_unblock_res_ = -1;

  SigIgnoreTestThread() { SetJoinable(true); }

  void Run() override {
    actual_ = ThreadCollector::ThreadTids(GetTID(), pthread_self());
    expected_ = ThreadCollector::ThreadTids(
        LiveThread_OS_TID(Thread_GetMyLiveThread()),
        LiveThread_Pthread_TID(Thread_GetMyLiveThread()));

    // Disable the signals so this can't receive.
    sigset_t mask_to_block, orig_mask;
    sigemptyset_res_ = sigemptyset(&mask_to_block);
    sigaddset_res_ = sigaddset(&mask_to_block, GOOGLE_OBSCURE_SIGNAL);
    pthread_sigmask_block_res_ =
        pthread_sigmask(SIG_BLOCK, &mask_to_block, &orig_mask);
    sigismember_res_ = sigismember(&orig_mask, GOOGLE_OBSCURE_SIGNAL);

    controller_.MarkAsStarted();

    controller_.WaitForStopCommand();
    pthread_sigmask_unblock_res_ =
        pthread_sigmask(SIG_UNBLOCK, &mask_to_block, &orig_mask);
  }
};

void DoNothing(void* arg, const LiveThreadState& state) {}

TEST(ThreadTest, CheckThreadProcessStackTracesSigSafe) {
  LiveThreadTestThread t1, t2, t3;
  t1.Start();
  absl::Cleanup cleanup1 = [&t1] {
    t1.controller_.Stop();
    t1.Join();
  };
  t2.Start();
  absl::Cleanup cleanup2 = [&t2] {
    t2.controller_.Stop();
    t2.Join();
  };
  t3.Start();
  absl::Cleanup cleanup3 = [&t3] {
    t3.controller_.Stop();
    t3.Join();
  };

  t1.controller_.WaitUntilStarted();
  t2.controller_.WaitUntilStarted();
  t3.controller_.WaitUntilStarted();

  EXPECT_EQ(t1.actual_, t1.expected_);
  EXPECT_EQ(t2.actual_, t2.expected_);
  EXPECT_EQ(t3.actual_, t3.expected_);

  // Create a new thread that blocks the signal
  SigIgnoreTestThread t4;
  t4.Start();
  absl::Cleanup cleanup4 = [&t4] {
    t4.controller_.Stop();
    t4.Join();
  };
  t4.controller_.WaitUntilStarted();

  Thread_ProcessStackTracesArg arg;
  arg.process_thread = &DoNothing;
  arg.process_thread_arg = nullptr;
  arg.sigsafe = true;
  arg.per_thread_timeout_ms = kPerThreadTimeoutMs;

  // At least one always fails
  EXPECT_GE(Thread_ProcessStackTraces(arg), 1);

  std::move(cleanup4).Invoke();

  EXPECT_EQ(t4.actual_, t4.expected_);
  EXPECT_EQ(t4.sigemptyset_res_, 0);
  EXPECT_EQ(t4.sigaddset_res_, 0);
  EXPECT_EQ(t4.pthread_sigmask_block_res_, 0);
  EXPECT_EQ(t4.sigismember_res_, 0);
  EXPECT_EQ(t4.pthread_sigmask_unblock_res_, 0);

  // Should be 0 but could be higher under load so avoid flakes.
  EXPECT_GE(Thread_ProcessStackTraces(arg), 0);
}

void NullFuncInTarget(void* arg, ucontext_t* uc, const LiveThread* thread) {}

TEST(ThreadTest, CheckInEachSignalDisabled) {
  if (!ShouldRunSignalTest()) {
    GTEST_SKIP() << "Skipping signal test because preconditions are not met.";
  }

  sigset_t mask_to_block, orig_mask;
  EXPECT_EQ(sigemptyset(&mask_to_block), 0);
  EXPECT_EQ(sigaddset(&mask_to_block, GOOGLE_OBSCURE_SIGNAL), 0);
  EXPECT_EQ(pthread_sigmask(SIG_BLOCK, &mask_to_block, &orig_mask), 0);
  EXPECT_EQ(sigismember(&orig_mask, GOOGLE_OBSCURE_SIGNAL), 0);

  // We assume that there is only one Thread running -- the main
  // thread.
  EXPECT_EQ(Thread_ForEach(nullptr, nullptr, NullFuncInTarget, nullptr,
                           kPerThreadTimeoutMs),
            1);

  EXPECT_EQ(pthread_sigmask(SIG_UNBLOCK, &mask_to_block, &orig_mask), 0);

  // Make sure Thread_ForEach works again, after unblocking the signal.
  EXPECT_EQ(Thread_ForEach(nullptr, nullptr, NullFuncInTarget, nullptr,
                           kPerThreadTimeoutMs),
            0);
}

constexpr int kNumNullInEachThreads = 50;

class NullInEachManager {
 public:
  NullInEachManager() : should_run_(ShouldRunSignalTest()) {}

  static void Setup(const benchmark::State& state) {
    Instance().SetupImpl(state);
  }

  static void Teardown(const benchmark::State& state) {
    Instance().TeardownImpl(state);
  }

  static bool ShouldRun() { return Instance().should_run_; }

 private:
  static NullInEachManager& Instance() {
    static absl::NoDestructor<NullInEachManager> instance;
    return *instance;
  }

  void SetupImpl(const benchmark::State& state) {
    absl::MutexLock lock(mu_);
    if (!should_run_) return;
    TeardownImplLocked();

    threads_.reserve(kNumNullInEachThreads);
    absl::Cleanup cleanup = [this] {
      mu_.AssertHeld();
      TeardownImplLocked();
    };

    for (int i = 0; i < kNumNullInEachThreads; ++i) {
      threads_.push_back(std::make_unique<LiveThreadTestThread>());
      threads_.back()->Start();
      threads_.back()->controller_.WaitUntilStarted();
    }
    std::move(cleanup).Cancel();

    Thread_ForEach(nullptr, nullptr, NullFuncInTarget, nullptr,
                   kPerThreadTimeoutMs);
  }

  void TeardownImpl(const benchmark::State& state) {
    absl::MutexLock lock(mu_);
    TeardownImplLocked();
  }

  void TeardownImplLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    for (auto& thread : threads_) {
      thread->controller_.Stop();
      thread->Join();
    }
    threads_.clear();
  }

  const bool should_run_;
  absl::Mutex mu_;
  std::vector<std::unique_ptr<LiveThreadTestThread>> threads_
      ABSL_GUARDED_BY(mu_);
};

static void BM_NullInEach(benchmark::State& state) {
  if (!NullInEachManager::ShouldRun()) {
    return;
  }
  int64_t failures = 0;
  for (auto _ : state) {
    failures += Thread_ForEach(nullptr, nullptr, NullFuncInTarget, nullptr,
                               kPerThreadTimeoutMs);
  }
  // Thread_ForEach is best-effort, so tolerate 5% error.
  EXPECT_LE(failures, (kNumNullInEachThreads * state.iterations()) / 20)
      << "from " << state.iterations() << " iterations with "
      << kNumNullInEachThreads << " threads live";
}

BENCHMARK(BM_NullInEach)
    ->Setup(NullInEachManager::Setup)
    ->Teardown(NullInEachManager::Teardown);

// -----------------------------------------------------------------
// Stack trace testing code

namespace {

// Extracted stack trace
struct StackContents {
  void* stack[kStackCount];
  int depth = 0;

  // We want to reliably skip this frame regardless of the optimization level.
  ABSL_ATTRIBUTE_NOINLINE
  void Fill() {
    depth = absl::GetStackTrace(stack, kStackCount, /*skip_count=*/1);
  }
};

// A ThreadStackWriter that appends to a string
struct StringWriter : public ThreadStackWriter {
  std::string str;
  void Write(const char* data, int data_length) override {
    str.append(data, data_length);
  }
};

// Recurse to "depth", and invoke closure.
ABSL_ATTRIBUTE_NOINLINE
void Recurse(int depth, absl::AnyInvocable<void() &&> closure) {
  if (depth <= 0) {
    std::move(closure)();
  } else {
    Recurse(depth - 1, std::move(closure));
    VLOG(1) << "Recursing...";  // Helps avoid tail-call optimizations
  }
  ABSL_BLOCK_TAIL_CALL_OPTIMIZATION();
}

// Return the PC of the recursive call inside Recurse()
void* RecursiveCallPC() {
  // Fill stack trace while recursing
  StackContents s;
  Recurse(10, [&s] { s.Fill(); });

  // A little hack: we assume that the PC of the recursive call in
  // "Recurse" occupies a few slots near the start of the stack trace.
  CHECK_GE(s.depth, 10);
  CHECK_EQ(s.stack[8], s.stack[9]);
  CHECK_EQ(s.stack[9], s.stack[10]);
  return s.stack[9];
}

// Store the current stack trace dump in *str
ABSL_ATTRIBUTE_NOINLINE
void SaveStackTraceDump(std::string* str) {
  StringWriter writer;
  Thread_ExtractStacks(&writer);
  *str = std::move(writer).str;
}

}  // namespace

TEST(ThreadTest, CheckSelfStack) {
  if (!absl::debugging_internal::StackTraceWorksForTest()) {
    GTEST_SKIP() << "Skipping because stack traces are not expected to work.";
  }
  void* pc = RecursiveCallPC();
  std::string dump;
  Recurse(100, [&dump] { SaveStackTraceDump(&dump); });

  // Since we line-wrap the pcs, there must be some line that
  // contains the recursive call pc more than once.
  EXPECT_THAT(dump, ContainsRegex(absl::StrFormat(" %p +%p", pc, pc)));
}

TEST(ThreadTest, CheckOtherStack) {
  if (!absl::debugging_internal::StackTraceWorksForTest()) {
    GTEST_SKIP() << "Skipping because stack traces are not expected to work.";
  }
  std::string dump;

  // Fetch stack trace while recursing to an arbitrary depth in another thread
  {
    ThreadPool pool(1);
    pool.Schedule(
        absl::bind_front(Recurse, 20, [&dump] { SaveStackTraceDump(&dump); }));
  }

  void* pc = RecursiveCallPC();
  EXPECT_THAT(dump, ContainsRegex(absl::StrFormat(" %p +%p", pc, pc)));

  // Must contain a creator: entry
  EXPECT_THAT(dump, HasSubstr("creator:"));
}

TEST(ThreadTest, CheckStackLineWrap) {
  if (!absl::debugging_internal::StackTraceWorksForTest()) {
    GTEST_SKIP() << "Skipping because stack traces are not expected to work.";
  }
  void* pc = RecursiveCallPC();
  std::string dump;
  Recurse(100, [&dump] { SaveStackTraceDump(&dump); });
  const std::string pc_pattern = absl::StrFormat(" %p ", pc);
  for (absl::string_view line : absl::StrSplit(dump, '\n', absl::SkipEmpty())) {
    if (absl::StrContains(line, pc_pattern)) {
      EXPECT_LE(line.size(), 80) << line;
    }
  }
}

TEST(ThreadTest, CheckStackTop) {
  if (!absl::debugging_internal::StackTraceWorksForTest()) {
    GTEST_SKIP() << "Skipping because stack traces are not expected to work.";
  }
  StackContents stack;
  // Construct a stack trace that will be filled with our known PC.
  Recurse(kStackCount + 20, [&stack] { stack.Fill(); });

  ASSERT_GT(stack.depth, 20) << "Got a very truncated stack trace.";

  const void* const pc = RecursiveCallPC();
  int level = 0;
  while (level < stack.depth) {
    if (stack.stack[level] == pc) break;
    ++level;
  }
  ASSERT_GT(level, 0) << "Did not find PC " << pc << " in stack trace.";
  EXPECT_LT(level, 8) << "Did not find PC " << pc
                      << " in the first 8 levels of stack trace.";
  // Remaining levels should all match our PC.
  for (; level < stack.depth; ++level) {
    EXPECT_EQ(stack.stack[level], pc)
        << "Found PC " << stack.stack[level] << " at level " << level
        << ",  expected " << pc;
  }
}

TEST(ThreadTest, CheckThreadNotesInStack) {
  if (!absl::debugging_internal::StackTraceWorksForTest()) {
    GTEST_SKIP() << "Skipping because stack traces are not expected to work.";
  }

  std::string dump;
  {
    thread::Note note("test_note");
    Recurse(100, [&dump] { SaveStackTraceDump(&dump); });
  }

  EXPECT_THAT(dump, HasSubstr("note: test_note\n"));
}

#if !PORTABLE_BASE

// Pretend to be a Python interpreter to test detection of threads
// holding the Python GIL.
std::atomic<bool> gil_held;
typedef struct _ts PyThreadState;
extern "C" PyThreadState* PyThreadState_GetUnchecked() {
  return gil_held.load() ? reinterpret_cast<PyThreadState*>(1) : nullptr;
}

TEST(ThreadTest, DumpGilHolder) {
  absl::Cleanup restore_gil_held = [] { gil_held.store(false); };
  std::string dump;

  gil_held.store(false);
  SaveStackTraceDump(&dump);
  EXPECT_THAT(dump, Not(HasSubstr("python_gil: held")));

  gil_held.store(true);
  SaveStackTraceDump(&dump);
  EXPECT_THAT(dump, HasSubstr("python_gil: held"));
}

#endif

TEST(ThreadTest, DebugNameNotInsideFiber) {
  if (thread::Fiber::IsFiber()) {
    GTEST_SKIP() << "Skipping test as precondition not met: test cannot "
                    "execute inside fiber.";
  }
  const LiveThread* thread = Thread_GetMyLiveThread();
  ASSERT_THAT(thread, NotNull());

  const char* thread_name = LiveThread_Name(thread);
  ASSERT_THAT(thread_name, NotNull());

  EXPECT_THAT(thread::DebugName(), HasSubstr(thread_name));
}

TEST(ThreadTest, DebugNameInsideFiber) {
  const char* kFiberName = "test_fiber_name";
  thread::Fiber f(thread::FiberOptions().SetInternedName(kFiberName),
                  [kFiberName] {
                    const LiveThread* thread = Thread_GetMyLiveThread();
                    ASSERT_THAT(thread, NotNull());

                    const char* thread_name = LiveThread_Name(thread);
                    ASSERT_THAT(thread_name, NotNull());

                    EXPECT_THAT(thread::DebugName(), HasSubstr(thread_name));
                    EXPECT_THAT(thread::DebugName(), HasSubstr(kFiberName));
                  });
  f.Join();
}

void ListOneNote(void* arg, absl::string_view note) {
  auto* v = static_cast<std::vector<std::string>*>(arg);
  v->push_back(std::string(note));
}

MATCHER_P(NotesAreImpl, expectation, "") {
  auto expected_matcher = UnorderedElementsAreArray(expectation);
  if (!ExplainMatchResult(expected_matcher, LiveThread_GetNotes(arg),
                          result_listener)) {
    *result_listener << " (GetNotes mismatch)";
    return false;
  }

  std::vector<std::string> notes;
  if (!LiveThread_ForEachNoteAsyncSignalSafe(arg, nullptr, ListOneNote,
                                             &notes)) {
    *result_listener << "LiveThread_ForEachNoteAsyncSignalSafe failed";
    return false;
  }
  if (!ExplainMatchResult(expected_matcher, notes, result_listener)) {
    *result_listener << " (ForEachNoteAsyncSignalSafe mismatch)";
    return false;
  }
  return true;
}

inline auto NotesAre(std::vector<std::string> expectation) {
  return NotesAreImpl(std::move(expectation));
}

TEST(ThreadTest, NotesEmpty) {
  const LiveThread* thread = Thread_GetMyLiveThread();
  EXPECT_THAT(thread, NotesAre({}));
}

TEST(ThreadTest, NotesSingle) {
  const LiveThread* thread = Thread_GetMyLiveThread();
  thread::Note note1("Note1");
  EXPECT_THAT(thread, NotesAre({"Note1"}));
}

TEST(ThreadTest, NotesMultiple) {
  const LiveThread* thread = Thread_GetMyLiveThread();
  thread::Note note2("Note2");
  thread::Note note3("Note3");
  EXPECT_THAT(thread, NotesAre({"Note2", "Note3"}));
}

TEST(ThreadTest, NotesFromMovedString) {
  const LiveThread* thread = Thread_GetMyLiveThread();
  std::string s("Note4");
  thread::Note note4(std::move(s));
  EXPECT_THAT(thread, NotesAre({"Note4"}));
}

TEST(ThreadTest, NotesFromStringView) {
  const LiveThread* thread = Thread_GetMyLiveThread();
  absl::string_view s = "Note5";
  thread::Note note5(s);
  EXPECT_THAT(thread, NotesAre({"Note5"}));
}

void ReetrantNoteReader(void* arg, absl::string_view note) {
  // Mainly we're just testing that this doesn't hang.
  const LiveThread* thread = Thread_GetMyLiveThread();
  EXPECT_FALSE(LiveThread_ForEachNoteAsyncSignalSafe(
      thread, nullptr, ReetrantNoteReader, nullptr));
}

TEST(ThreadTest, NotesReentrantReader) {
  const LiveThread* thread = Thread_GetMyLiveThread();
  EXPECT_TRUE(LiveThread_ForEachNoteAsyncSignalSafe(
      thread, nullptr, ReetrantNoteReader, nullptr));
}

// This thread, used by the SkipNotes test, adds a note and then removes it when
// requested by the test.
class ThreadSkipNotesTester : public Thread {
 public:
  absl::Notification started_;
  const LiveThread* self_;

  absl::Notification drop_note_;
  absl::Notification note_dropped_;
  absl::Notification stop_;

  ThreadSkipNotesTester() { SetJoinable(true); }

  void Run() final {
    {
      thread::Note note1("Note1");
      self_ = Thread_GetMyLiveThread();
      // Tell the test that the note has been added.
      started_.Notify();
      // Wait until it's time to drop the note.
      drop_note_.WaitForNotification();
    }

    // Tell the test that the note has been dropped.
    note_dropped_.Notify();
    // Wait until the test is over to exit.
    stop_.WaitForNotification();
  }
};

// Tests that we skip returning notes for a thread if they have changed since
// its stack trace was taken.
TEST(ThreadTest, SkipNotes) {
  ThreadSkipNotesTester tester;

  tester.Start();
  tester.started_.WaitForNotification();

  Thread_ProcessStackTracesArg arg;
  arg.filter_arg = &tester;
  arg.filter = [](void* arg, const LiveThread* thread) {
    return thread == static_cast<ThreadSkipNotesTester*>(arg)->self_;
  };
  arg.process_trace_arg = &tester;
  arg.process_trace = [](void* arg, const LiveThread* thread,
                         const StackTrace* trace) {
    auto* tester = static_cast<ThreadSkipNotesTester*>(arg);
    EXPECT_EQ(thread, tester->self_);

    if (trace != nullptr) {
      // Make sure we do get the notes in this case.
      auto notes_for_trace = LiveThread_GetNotesForTrace(thread, trace);
      EXPECT_FALSE(notes_for_trace.notes_changed_since_stack_trace);
      EXPECT_THAT(notes_for_trace.notes, UnorderedElementsAreArray({"Note1"}));

      auto fn = [](void* arg, absl::string_view note) {};
      EXPECT_TRUE(
          LiveThread_ForEachNoteAsyncSignalSafe(thread, trace, fn, nullptr));
    }

    // Trigger a change in the notes.
    tester->drop_note_.Notify();
    tester->note_dropped_.WaitForNotification();

    if (trace != nullptr) {
      // Now we should see the notes change.
      auto notes_for_trace = LiveThread_GetNotesForTrace(thread, trace);
      EXPECT_TRUE(notes_for_trace.notes_changed_since_stack_trace);
      EXPECT_THAT(notes_for_trace.notes, IsEmpty());

      auto fn = [](void* arg, absl::string_view note) {};
      EXPECT_FALSE(
          LiveThread_ForEachNoteAsyncSignalSafe(thread, trace, fn, nullptr));
    }
  };

  Thread_ProcessStackTraces(arg);

  tester.stop_.Notify();
  tester.Join();
}

#ifndef __Fuchsia__
TEST(ThreadTest, NicePriorityAppliedToOS) {
  const int parent_nice = getpriority(PRIO_PROCESS, 0);
  const int kNiceIncrement = 2;

  thread::Options options;
  options.set_nice_priority_level(kNiceIncrement);
  options.set_joinable(true);

  int child_nice = -1;
  ClosureThread t(options, "nice_thread",
                  [&child_nice] { child_nice = getpriority(PRIO_PROCESS, 0); });
  t.Start();
  t.Join();

  EXPECT_EQ(child_nice, std::min(19, parent_nice + kNiceIncrement));
}
#endif

#if defined(__linux__) && !defined(__ANDROID__)
TEST(ThreadTest, IOPriorityAppliedToThread) {
  thread::Options options;
  options.set_io_priority(2, 5);
  options.set_joinable(true);

  int child_io_class = -1;
  int child_io_level = -1;

  ClosureThread t(options, "io_prio_thread", [&] {
    util::GetIOPriority(GetTID(), &child_io_class, &child_io_level);
  });
  t.Start();
  t.Join();

  EXPECT_EQ(child_io_class, 2);
  EXPECT_EQ(child_io_level, 5);
}
#endif

TEST(ThreadTest, RegisterExitHandlerTest) {
  static std::atomic<bool> g_exit_handler_called{false};
  g_exit_handler_called.store(false);
  Thread::RegisterExitHandler([]() { g_exit_handler_called.store(true); });

  ClosureThread thread(thread::Options().set_joinable(true),
                       "ExitHandlerTestThread",
                       []() { pthread_exit(nullptr); });
  thread.Start();
  thread.Join();

  EXPECT_TRUE(g_exit_handler_called.load());
}

TEST(ThreadDeathTest, DoubleStartCrashes) {
  ClosureThread t([] {});
  t.SetJoinable(true);
  t.Start();
  EXPECT_DEATH_IF_SUPPORTED(t.Start(), "Thread is not restartable");
  t.Join();
}

TEST(ThreadDeathTest, SetNamePrefixAfterStartCrashes) {
  ClosureThread t([] {});
  t.SetJoinable(true);
  t.Start();
  EXPECT_DEATH_IF_SUPPORTED(t.SetNamePrefix("NewPrefix"),
                            "Only call SetNamePrefix");
  t.Join();
}

TEST(ThreadDeathTest, SetNamePrefixInvalidCharacterCrashes) {
  ClosureThread t([] {});
  EXPECT_DEATH_IF_SUPPORTED(t.SetNamePrefix("1Invalid"),
                            "contains a disallowed character");
}

TEST(ThreadDeathTest, SetJoinableAfterStartCrashes) {
  ClosureThread t([] {});
  t.SetJoinable(true);
  t.Start();
  EXPECT_DEATH_IF_SUPPORTED(t.SetJoinable(false), "Only call SetJoinable");
  t.Join();
}

}  // namespace

// We want to test thread watchers and watchdogs for mobile platforms regardless
// of default value. So forward-declaring here in order to be able to override.
ABSL_DECLARE_FLAG(bool, watch_pthread_manager);
ABSL_DECLARE_FLAG(bool, watch_thread_liveness);

REGISTER_MODULE_INITIALIZER(thread_unittest_pre_init, {
  DeprecatedSingleThreadedTest::AvoidBackgroundThreads();

#if defined(__ANDROID__) || defined(__APPLE__)
  // We want to test thread watchers and watchdogs for mobile platforms in case
  // anyone wants to use them regardless of the energy cost.
  // This block is within #ifdef because watchers also need to be disabled in
  // some cases by the AvoidBackgroundThreads call above. Meanwhile, using
  // `SetCommandLineOptionWithMode(name, value, SET_FLAG_IF_DEFAULT)` fails to
  // find the flags dynamically on Android.
  absl::SetFlag(&FLAGS_watch_pthread_manager, true);
  absl::SetFlag(&FLAGS_watch_thread_liveness, true);
#endif

  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
});

REGISTER_MODULE_INITIALIZER_SEQUENCE(thread_unittest_pre_init,
                                     command_line_flags_parsing);
