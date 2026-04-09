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

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/optimization.h"
#include "absl/debugging/stacktrace.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/functional/any_invocable.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/barrier.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "gloop/base/callback.h"
#include "gloop/base/context.h"
#include "gloop/base/init_google.h"
#include "gloop/base/port.h"
#include "gloop/base/signal-handler.h"
#include "gloop/base/sysinfo.h"
#include "gloop/base/timer.h"
#include "gloop/base/tracecontext.h"
#include "gloop/thread/config.h"
#include "gloop/thread/thread-internal.h"
#include "gloop/thread/thread_control.h"
#include "gloop/thread/thread_options.h"
#include "gloop/thread/threadpool.h"
#include "gloop/util/functional/to_callback.h"
#include "gloop/util/gtl/container_logging.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "re2/re2.h"

#if !PORTABLE_BASE
#define AVOID_TRACECONTEXT 1
#endif

ABSL_FLAG(bool, crashme, false, "Crash so you can see stack trace output");

// friend that is allowed to use thread::DeprecatedThreadControl.
class DeprecatedSingleThreadedTest {
 public:
  static void AvoidBackgroundThreads() {
    ::thread::DeprecatedThreadControl::AvoidBackgroundThreads();
  }
};

class PythonGilHolderLookupForTest : public PythonGilHolderLookup {
 public:
  using PythonGilHolderLookup::Register;
};

namespace {

// The timeout used for Thread_ForEach() and Thread_ProcessStackTraces()
// invocations.  We choose a conservative value, higher than the default
// timeouts, to increase reliability on heavily loaded forge machines.
constexpr int kPerThreadTimeoutMs = 100;

void TestCallback(Thread* thd) {
  sleep(2);
  printf("[2sec sleep done] ");
}

void TestClosure() {
  sleep(2);
  printf("[2sec sleep done] ");
}

static void IncrementMe(int* x) { ++*x; }

static void DieHorribly() {
#if AVOID_TRACECONTEXT
  static const char* status_string = "Hi, mom!";
  base::WithThreadStatus trace_status_switcher(status_string);
#endif
  char* null = nullptr;
  strcpy(null, "Hi!");  // NOLINT
}

TEST(ThreadTest, CheckClosureThread) {
  int y(3);
  ClosureThread t([&y] { IncrementMe(&y); });
  t.SetJoinable(true);
  CHECK_EQ(y, 3);
  t.Start();

  if (absl::GetFlag(FLAGS_crashme)) {
    // Launch a thread that crashes, to see what it looks like.
    ClosureThread u(DieHorribly);
    u.SetJoinable(true);
    u.Start();
    u.Join();
  }

  t.Join();
  CHECK_EQ(y, 4);

  // now with options and name:
  static const char* kNamePrefix = "ClosureTest";
  thread::Options options;
  ClosureThread t1(options, kNamePrefix, [&y] { IncrementMe(&y); });
  t1.SetJoinable(true);
  CHECK_EQ(y, 4);
  t1.Start();
  CHECK_EQ(t1.name_prefix(), kNamePrefix);
  t1.Join();
  CHECK_EQ(y, 5);
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
  CHECK_EQ(checker.Counter(), 0);
  t1.Start();
  t1.Join();
  CHECK_EQ(checker.Counter(), 1);

  // now with options and name:
  static const char* kNamePrefix = "MemberTest";
  thread::Options options;
  MemberThread<MemberThreadChecker> t2(options, kNamePrefix, &checker,
                                       &MemberThreadChecker::IncrementCounter);
  t2.SetJoinable(true);
  CHECK_EQ(checker.Counter(), 1);
  t2.Start();
  CHECK_EQ(t2.name_prefix(), kNamePrefix);
  t2.Join();
  CHECK_EQ(checker.Counter(), 2);
}

TEST(ThreadTest, CheckDetachableThread) {
  absl::Notification notification;
  StartDetachedThread(thread::Options(), "testing_thread",
                      [&notification] { notification.Notify(); });
  notification.WaitForNotification();
}

class TidTestThread : public Thread {
 public:
  void Run() override { CHECK_EQ(tid(), pthread_self()); }
};

TEST(ThreadTest, CheckChildTid) {
  for (int i = 0; i < 5000; ++i) {
    TidTestThread* thread = new TidTestThread();
    thread->SetJoinable(true);
    thread->Start();
    thread->Join();
    delete thread;
    //  Yield whenever i+1 is a power of 2.
    if ((i & (i + 1)) == 0) sched_yield();
  }
}

static bool ShouldRunSignalTest(const char* testname) {
  if (!absl::debugging_internal::StackTraceWorksForTest()) {
    LOG(WARNING) << "Skipping " << testname
                 << " because stack traces are not expected to work in tests.";
    return false;
  }
#ifdef ABSL_HAVE_THREAD_SANITIZER
  // TODO: Re-enable after fixing signal handler problems with test.
  LOG(WARNING) << "Skipping " << testname
               << " due to unreliable signal delivery (see b/62138055).";
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
      << testname
      << " requires \"-install_signal_handlers=true\" and "
         "\"-install_named_signal_handlers=stackdump\"; skipping";
  return ok;
#endif  // ABSL_HAVE_THREAD_SANITIZER
}

TEST(ThreadTest, CheckOptions) {
  thread::Options opt;

  opt.set_joinable(false);
  opt.set_stack_size(17);
  CHECK_EQ(opt.joinable(), false);
  CHECK_EQ(opt.stack_size(), 17);

  opt.set_joinable(true).set_stack_size(18);
  CHECK_EQ(opt.joinable(), true);
  CHECK_EQ(opt.stack_size(), 18);

  opt.set_io_priority(2, 6);
  CHECK_EQ(opt.io_class(), 2);
  CHECK_EQ(opt.io_priority_level(), 6);
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
static void Wait(absl::Barrier* started, absl::Barrier* done) {
  started->Block();
  done->Block();
}

TEST(ThreadTest, CheckThreadSignalSafeDumpStacks) {
  // Fire up a set number of threads which wait
  // to indicate they're started and then wait again
  // before exiting.
  std::list<std::unique_ptr<Thread>> li;
  absl::Barrier started(kThreads + 1);
  absl::Barrier done(kThreads + 1);
  for (int i = 0; i < kThreads; ++i) {
    thread::Options options;
    options.set_joinable(true);
    std::unique_ptr<Thread> thr(
        new ClosureThread(options, absl::StrFormat("dumpthread-%d", i),
                          absl::bind_front(Wait, &started, &done)));
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
    CHECK_NE(std::string::npos, dump.find(name))
        << "Looking for thread signature: " << name;
  }

  CHECK_NE(std::string::npos, dump.find("--- Memory map: ---"))
      << "Looking for '--- Memory map: ---' in output";

  // Unblock all the threads and then join each for cleanup.
  done.Block();
  for (int i = 0; i < kThreads; ++i, li.pop_front()) {
    std::unique_ptr<Thread> thr(std::move(li.front()));
    thr->Join();
  }
}

#endif  // !__APPLE__ && !__ANDROID__

TEST(ThreadTest, CheckPeriodicThreads) {
  bool signaled;
  WallTimer timer;

  class MyTest {
   public:
    MyTest() : t_(this) {}
    void RunInThread(PeriodicThread<MyTest>* thd) {
      sleep(2);
      printf("[2sec sleep done] ");
    }

    bool Signal(bool wait) { return t_.Signal(wait); }
    void Exit() { t_.Exit(); }

   private:
    PeriodicThread<MyTest> t_;
  };
  MyTest tester;

  timer.Restart();
  printf("Sending first thread off (should be 1): ");
  fflush(stdout);
  signaled = tester.Signal(true);
  printf("%d\n", signaled);
  fflush(stdout);
  CHECK(signaled);
  CHECK_LT(timer.Get(), 0.2);  // shouldn't have had to wait at all

  printf("Sending second thread off (should be 0): ");
  fflush(stdout);
  signaled = tester.Signal(false);
  printf("%d\n", signaled);
  fflush(stdout);
  CHECK(!signaled);
  CHECK_LT(timer.Get(), 0.2);  // shouldn't have had to wait at all

  printf("Sending third thread off (should be 1, after a wait): ");
  fflush(stdout);
  signaled = tester.Signal(true);
  printf("%d\n", signaled);
  fflush(stdout);
  CHECK(signaled);
  CHECK_GT(timer.Get(), 1.8);  // should have to wait about 2 seconds

  printf("Telling thread to exit (should have to wait again): ");
  fflush(stdout);
  tester.Exit();
  CHECK_GT(timer.Get(), 1.8);  // should have to wait about 2 seconds

  ::util::functional::CallbackFunctor<Thread*> callback(
      ::util::functional::ToPermanentCallback(&TestCallback));
  PeriodicThread<::util::functional::CallbackFunctor<Thread*>> callback_thread(
      callback.get());

  timer.Restart();
  printf("Sending first thread off (should be 1): ");
  fflush(stdout);
  signaled = callback_thread.Signal(true);
  printf("%d\n", signaled);
  fflush(stdout);
  CHECK(signaled);
  CHECK_LT(timer.Get(), 0.2);  // shouldn't have had to wait at all

  printf("Sending second thread off (should be 0): ");
  fflush(stdout);
  signaled = callback_thread.Signal(false);
  printf("%d\n", signaled);
  fflush(stdout);
  CHECK(!signaled);
  CHECK_LT(timer.Get(), 0.2);  // shouldn't have had to wait at all

  printf("Sending third thread off (should be 1, after a wait): ");
  fflush(stdout);
  signaled = callback_thread.Signal(true);
  printf("%d\n", signaled);
  fflush(stdout);
  CHECK(signaled);
  CHECK_GT(timer.Get(), 1.8);  // should have to wait about 2 seconds

  printf("Telling thread to exit (should have to wait again): ");
  fflush(stdout);
  callback_thread.Exit();
  CHECK_GT(timer.Get(), 1.8);  // should have to wait about 2 seconds

  std::unique_ptr<Closure> closure(
      ::util::functional::ToPermanentCallback(&TestClosure));
  PeriodicThread<Closure> closure_thread(closure.get());

  timer.Restart();
  printf("Sending first thread off (should be 1): ");
  fflush(stdout);
  signaled = closure_thread.Signal(true);
  printf("%d\n", signaled);
  fflush(stdout);
  CHECK(signaled);
  CHECK_LT(timer.Get(), 0.2);  // shouldn't have had to wait at all

  printf("Sending second thread off (should be 0): ");
  fflush(stdout);
  signaled = closure_thread.Signal(false);
  printf("%d\n", signaled);
  fflush(stdout);
  CHECK(!signaled);
  CHECK_LT(timer.Get(), 0.2);  // shouldn't have had to wait at all

  printf("Sending third thread off (should be 1, after a wait): ");
  fflush(stdout);
  signaled = closure_thread.Signal(true);
  printf("%d\n", signaled);
  fflush(stdout);
  CHECK(signaled);
  CHECK_GT(timer.Get(), 1.8);  // should have to wait about 2 seconds

  printf("Telling thread to exit (should have to wait again): ");
  fflush(stdout);
  closure_thread.Exit();
  CHECK_GT(timer.Get(), 1.8);  // should have to wait about 2 seconds
  printf("\n");
  fflush(stdout);
}

// Collects a list of thread IDs using Thread_ForEach.  Assumes that
// insertion of a pthread_t into a vector is safe from an async signal
// handler, so long as the vector has the required capacity and
// multiple threads don't try to do it concurrently.
class ThreadCollector {
 public:
  struct ThreadTids {
    pid_t os_tid;
    pthread_t pthread_tid;

    ThreadTids(pid_t os_tid, pthread_t pthread_tid)
        : os_tid(os_tid), pthread_tid(pthread_tid) {}
    bool operator==(const ThreadTids& rhs) const {
      // Can't use =default for C++17 users.
      return os_tid == rhs.os_tid && pthread_tid == rhs.pthread_tid;
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
    CHECK_EQ(0, StackTrace_GetPCs(trace, -1, nullptr));
    CHECK_EQ(0, StackTrace_GetPCs(trace, 0, nullptr));
    // There should always be at least one frame in the trace!
    CHECK_EQ(1, StackTrace_GetPCs(trace, 1, pc_buffer));
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

  bool ContainsThread(const ThreadTids& tids) const {
    // Linear search, but we don't have large numbers of threads.
    for (const ThreadTids& my_tids : tids_) {
      if (tids == my_tids) return true;
    }
    return false;
  }

  bool ContainsNamePrefix(absl::string_view name_prefix) const {
    for (absl::string_view my_name_prefix : name_prefixes_) {
      if (name_prefix == my_name_prefix) return true;
    }
    return false;
  }

  bool ContainsNamedThread(const ThreadTids& tids,
                           absl::string_view name) const {
    for (size_t i = 0; i < tids_.size(); ++i) {
      if (tids_[i] == tids && names_[i] == name) return true;
    }
    return false;
  }

 private:
  std::vector<ThreadTids> tids_;
  std::vector<std::string> name_prefixes_;
  std::vector<std::string> names_;
};

// Class that implements a simple state machine for use in testing the
// LiveThread interfaces and registration of external threads.
class LiveThreadTestController {
 public:
  LiveThreadTestController()
      : started_(false),
        registered_(false),
        stop_(false),
        register_(false),
        thread_tids_(0, 0) {}

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
  //
  // All require L < mu_
  void MarkAsStarted() {
    absl::MutexLock l(mu_);
    thread_tids_.os_tid = GetTID();
    thread_tids_.pthread_tid = pthread_self();
    started_ = true;
  }
  void WaitForRegisterCommand() { WaitForVar(&register_); }
  void MarkAsRegistered() { SetVar(&registered_); }
  void WaitForStopCommand() { WaitForVar(&stop_); }

  // Steps in the parent thread:
  //   <pre>
  //   (start)
  //   WaitUntilStarted
  //   if external thread:
  //     Register / WaitUntilRegistered
  //   Stop
  //   (join)
  //   </pre>
  //
  // All require L < mu_
  void WaitUntilStarted() { WaitForVar(&started_); }
  void Register() { SetVar(&register_); }
  void WaitUntilRegistered() { WaitForVar(&registered_); }
  void Stop() { SetVar(&stop_); }

  // Get this thread's TID (same value ::GetTID()'s return when called
  // in this thread).
  //
  // L < mu_
  ThreadCollector::ThreadTids GetThreadTids() {
    absl::MutexLock l(mu_);
    CHECK(started_);
    return thread_tids_;
  }

 private:
  absl::Mutex mu_;   // protects all of the variables below.
  bool started_;     // has started?           (under mu_)
  bool registered_;  // has been registered?   (under mu_)
  bool stop_;        // should stop.           (under mu_)
  bool register_;    // should register.       (under mu_)

  // Thread's TIDs.  Set at startup.  (under mu_)
  ThreadCollector::ThreadTids thread_tids_;

  // L < mu_
  void SetVar(bool* var) {
    absl::MutexLock lock(mu_);
    *var = true;
  }

  // L < mu_
  void WaitForVar(bool* var) {
    absl::MutexLock lock(mu_);
    mu_.Await(absl::Condition(var));
  }
};

class LiveThreadTestThread : public Thread {
 public:
  LiveThreadTestController controller_;

  LiveThreadTestThread() : tids_(GetTID(), pthread_self()) {
    SetJoinable(true);
  }

  void Run() override {
    CHECK_EQ(GetTID(), LiveThread_OS_TID(Thread_GetMyLiveThread()));
    CHECK_EQ(pthread_self(), LiveThread_Pthread_TID(Thread_GetMyLiveThread()));
    // Reset to the real values now that we're inside the thread.
    tids_ = ThreadCollector::ThreadTids(GetTID(), pthread_self());
    controller_.MarkAsStarted();
    controller_.WaitForStopCommand();
  }

  ThreadCollector::ThreadTids ThreadId() const { return tids_; }

 private:
  ThreadCollector::ThreadTids tids_;
};

TEST(ThreadTest, CheckForEach) {
  if (!ShouldRunSignalTest("CheckForEach")) {
    return;
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
          LOG(INFO) << "Dropped " << dropped << " threads";
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
#if THREAD_HAVE_THREAD_CONTROL
  const int kBaselineThreads = 1;
#else
  const int kBaselineThreads = 3;
#endif
  CHECK_EQ(kBaselineThreads, for_each_collector.Count());
  CHECK(for_each_collector.ContainsThread(
      ThreadCollector::ThreadTids(GetTID(), pthread_self())));

  LiveThreadTestThread t1, t2, t3;
  t1.Start();
  t2.Start();
  t3.Start();

  t1.controller_.WaitUntilStarted();
  t2.controller_.WaitUntilStarted();
  t3.controller_.WaitUntilStarted();

  // Verify that all threads are now running, without running anything
  // in their contexts.
  for_each_collector.Reset();
  in_each_collector.Reset();
  thread_foreach(&for_each_collector.FalseAdd, &for_each_collector,
                 &in_each_collector.AddInTarget, &in_each_collector,
                 kPerThreadTimeoutMs /* should be unused */);
  CHECK_EQ(kBaselineThreads + 3, for_each_collector.Count());
  CHECK(for_each_collector.ContainsThread(
      ThreadCollector::ThreadTids(GetTID(), pthread_self())));
  CHECK(for_each_collector.ContainsThread(t1.controller_.GetThreadTids()));
  CHECK(for_each_collector.ContainsThread(t2.controller_.GetThreadTids()));
  CHECK(for_each_collector.ContainsThread(t3.controller_.GetThreadTids()));
  CHECK_EQ(0, in_each_collector.Count());

  // Verify that all threads are now running, and run a function in
  // their contexts.
  for_each_collector.Reset();
  in_each_collector.Reset();
  thread_foreach(&for_each_collector.TrueAdd, &for_each_collector,
                 &in_each_collector.AddInTarget, &in_each_collector,
                 kPerThreadTimeoutMs);
  CHECK_EQ(kBaselineThreads + 3, for_each_collector.Count());
  CHECK(for_each_collector.ContainsThread(
      ThreadCollector::ThreadTids(GetTID(), pthread_self())));
  CHECK(for_each_collector.ContainsThread(t1.controller_.GetThreadTids()));
  CHECK(for_each_collector.ContainsThread(t2.controller_.GetThreadTids()));
  CHECK(for_each_collector.ContainsThread(t3.controller_.GetThreadTids()));
  EXPECT_THAT(for_each_collector.ThreadIds(),
              testing::ContainerEq(in_each_collector.ThreadIds()));

  // Same, but run a function by using a NULL for_each function.
  // Reuses contents of for_each_collector from above.
  in_each_collector.Reset();
  thread_foreach(nullptr, nullptr, &in_each_collector.AddInTarget,
                 &in_each_collector, kPerThreadTimeoutMs);
  EXPECT_THAT(in_each_collector.ThreadIds(),
              testing::ContainerEq(for_each_collector.ThreadIds()));

  t1.controller_.Stop();
  t2.controller_.Stop();
  t1.Join();
  t2.Join();

  // Verify that only two threads are now running
  for_each_collector.Reset();
  thread_foreach(&for_each_collector.FalseAdd, &for_each_collector, nullptr,
                 nullptr, kPerThreadTimeoutMs);
  CHECK_EQ(kBaselineThreads + 1, for_each_collector.Count());
  CHECK(for_each_collector.ContainsThread(
      ThreadCollector::ThreadTids(GetTID(), pthread_self())));
  CHECK(for_each_collector.ContainsThread(t3.controller_.GetThreadTids()));

  t3.controller_.Stop();
  t3.Join();

  // We should be back to one thread active.
  for_each_collector.Reset();
  thread_foreach(&for_each_collector.FalseAdd, &for_each_collector, nullptr,
                 nullptr, kPerThreadTimeoutMs);
  CHECK_EQ(kBaselineThreads, for_each_collector.Count());
  CHECK(for_each_collector.ContainsThread(
      ThreadCollector::ThreadTids(GetTID(), pthread_self())));
}

// Special Thread_ForEach for_each function used by
// CheckInEachWhileExiting.  Returns true (indicating to run the
// in_each function) for all threads, and also causes the
// LiveThreadTestThread pointed to by testthreadv to start to exit.
// This violates the interface spec for Thread_ForEach, but is safe in
// the current implementation and is needed to make this test
// deterministic.
static bool StopTestThreadFromForEach(void* testthreadv,
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
  if (!ShouldRunSignalTest("CheckInEachWhileExiting")) {
    return;
  }

  LiveThreadTestThread t;
  t.Start();
  t.controller_.WaitUntilStarted();

  ThreadCollector collector(4);
  collector.Reset();

  int num_fails =
      Thread_ForEach(StopTestThreadFromForEach, &t, &collector.AddInTarget,
                     &collector, kPerThreadTimeoutMs);
  // Could not collect from 't', since it has started to exit.
  CHECK_EQ(1, num_fails);

  // Verify that only non-exiting threads were collected.
#if THREAD_HAVE_THREAD_CONTROL
  const int kExpectedThreads = 1;
#else
  const int kExpectedThreads = 3;
#endif
  CHECK_EQ(kExpectedThreads, collector.Count());
  CHECK(collector.ContainsThread(
      ThreadCollector::ThreadTids(GetTID(), pthread_self())));

  t.Join();
}

static void* ExternalThreadMain(void* arg) {
  LiveThreadTestController* controller =
      static_cast<LiveThreadTestController*>(arg);

  controller->MarkAsStarted();
  CHECK_EQ(static_cast<LiveThread*>(nullptr), Thread_GetMyLiveThread());

  controller->WaitForRegisterCommand();
  Thread_RegisterExternalThread("example");
  CHECK_NE(static_cast<LiveThread*>(nullptr), Thread_GetMyLiveThread());
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
#if THREAD_HAVE_THREAD_CONTROL
  const int kBaselineThreads = 1;
#else
  const int kBaselineThreads = 3;
#endif
  CHECK_EQ(kBaselineThreads, for_each_collector.Count());
  CHECK(for_each_collector.ContainsThread(
      ThreadCollector::ThreadTids(GetTID(), pthread_self())));

  LiveThreadTestController controller;
  pthread_t thread;
  CHECK_EQ(0,
           pthread_create(&thread, nullptr, ExternalThreadMain, &controller));

  controller.WaitUntilStarted();

  // Still only one thread (external threads aren't registered
  // automatically).
  for_each_collector.Reset();
  Thread_ForEach(&for_each_collector.FalseAdd, &for_each_collector, nullptr,
                 nullptr, kPerThreadTimeoutMs);
  CHECK_EQ(kBaselineThreads, for_each_collector.Count());
  CHECK(for_each_collector.ContainsThread(
      ThreadCollector::ThreadTids(GetTID(), pthread_self())));

  controller.Register();
  controller.WaitUntilRegistered();

  // Expect to see both threads.
  for_each_collector.Reset();
  Thread_ForEach(&for_each_collector.FalseAdd, &for_each_collector, nullptr,
                 nullptr, kPerThreadTimeoutMs);
  CHECK_EQ(kBaselineThreads + 1, for_each_collector.Count());
  CHECK(for_each_collector.ContainsThread(
      ThreadCollector::ThreadTids(GetTID(), pthread_self())));
  CHECK(for_each_collector.ContainsThread(controller.GetThreadTids()));

  controller.Stop();
  CHECK_EQ(0, pthread_join(thread, nullptr));

  // Back to only one thread (external thread unregistered on exit).
  for_each_collector.Reset();
  Thread_ForEach(&for_each_collector.FalseAdd, &for_each_collector, nullptr,
                 nullptr, kPerThreadTimeoutMs);
  CHECK_EQ(kBaselineThreads, for_each_collector.Count());
  CHECK(for_each_collector.ContainsThread(
      ThreadCollector::ThreadTids(GetTID(), pthread_self())));
}

struct CheckProcessStackTracesData {
  ThreadCollector* filter_collector = nullptr;
  ThreadCollector* process_trace_collector = nullptr;
  ThreadCollector* process_thread_collector = nullptr;
  ThreadCollector::ThreadTids* self = nullptr;
  LiveThreadTestThread* t1 = nullptr;
  LiveThreadTestThread* t2 = nullptr;
  LiveThreadTestThread* t3 = nullptr;
};

// Helper to retry stack extraction since it can time out under heavy load.
static void RetryOnDrop(CheckProcessStackTracesData* data,
                        std::function<int()> fn) {
  auto reset = [](ThreadCollector* c) {
    if (c != nullptr) {
      c->Reset();
    }
  };

  const int kMaxAttempts = 5;
  for (int i = 0; i < kMaxAttempts; i++) {
    reset(data->filter_collector);
    reset(data->process_trace_collector);
    reset(data->process_thread_collector);

    const int dropped = fn();
    if (dropped == 0) {
      return;
    }
    LOG(INFO) << "Dropped " << dropped << " threads";
    // Wait a little in case we are competing with some bursty cpu load.
    absl::SleepFor(absl::Milliseconds(100));
  }
  LOG(WARNING) << "Could not avoid dropped stack trace in " << kMaxAttempts
               << " attempts";
}

// CheckSingleThreadActive verifies that Thread_ProcessStackTraces sees
// only one active thread at this point in time.
static void CheckSingleThreadActive(CheckProcessStackTracesData* data) {
  RetryOnDrop(data, [data] {
    Thread_ProcessStackTracesArg arg;
    arg.filter = &data->filter_collector->FalseAdd;
    arg.filter_arg = data->filter_collector;
    arg.per_thread_timeout_ms = kPerThreadTimeoutMs;

    return Thread_ProcessStackTraces(arg);
  });

#if THREAD_HAVE_THREAD_CONTROL
  const int kBaselineThreads = 1;
#else
  const int kBaselineThreads = 3;
#endif
  EXPECT_EQ(kBaselineThreads, data->filter_collector->Count());
  EXPECT_THAT(data->filter_collector->ThreadIds(),
              testing::Contains(*data->self));
}

// CheckFilterDropsAll verifies that Thread_ProcessStackTracesArg called with
// a filter that excludes all threads doesn't execute any other callbacks.
static void CheckFilterDropsAll(CheckProcessStackTracesData* data) {
  RetryOnDrop(data, [data] {
    Thread_ProcessStackTracesArg arg;
    arg.filter = &data->filter_collector->FalseAdd;
    arg.filter_arg = data->filter_collector;
    arg.process_trace = &data->process_thread_collector->AddStackTrace;
    arg.process_trace_arg = data->process_thread_collector;
    arg.process_thread =
        &data->process_trace_collector->AddStackTraceLiveThreadState;
    arg.process_thread_arg = data->process_trace_collector;
    arg.per_thread_timeout_ms = kPerThreadTimeoutMs;

    return Thread_ProcessStackTraces(arg);
  });

  EXPECT_EQ(0, data->process_trace_collector->Count());
  EXPECT_EQ(0, data->process_thread_collector->Count());
#if THREAD_HAVE_THREAD_CONTROL
  const int kExpectedThreads = 4;
#else
  const int kExpectedThreads = 6;
#endif
  EXPECT_EQ(kExpectedThreads, data->filter_collector->Count());
  EXPECT_THAT(
      data->filter_collector->ThreadIds(),
      testing::IsSupersetOf({*data->self, data->t1->ThreadId(),
                             data->t2->ThreadId(), data->t3->ThreadId()}));
}

// CheckAllProcessStackTracesExecute verifies that all 3 possible
// callbacks for Thread_ProcessStackTraces execute: filter, process_trace and
// process_thread.
static void CheckAllProcessStackTracesExecute(
    CheckProcessStackTracesData* data) {
  RetryOnDrop(data, [data] {
    Thread_ProcessStackTracesArg arg;
    arg.filter = &data->filter_collector->TrueAdd;
    arg.filter_arg = data->filter_collector;
    arg.process_trace = &data->process_thread_collector->AddStackTrace;
    arg.process_trace_arg = data->process_thread_collector;
    arg.process_thread =
        &data->process_trace_collector->AddStackTraceLiveThreadState;
    arg.process_thread_arg = data->process_trace_collector;
    arg.per_thread_timeout_ms = kPerThreadTimeoutMs;

    return Thread_ProcessStackTraces(arg);
  });

#if THREAD_HAVE_THREAD_CONTROL
  const int kExpectedThreads = 4;
#else
  const int kExpectedThreads = 6;
#endif
  EXPECT_EQ(kExpectedThreads, data->process_trace_collector->Count());
  EXPECT_EQ(kExpectedThreads, data->process_thread_collector->Count());
  EXPECT_EQ(kExpectedThreads, data->filter_collector->Count());
  EXPECT_THAT(
      data->filter_collector->ThreadIds(),
      testing::IsSupersetOf({*data->self, data->t1->ThreadId(),
                             data->t2->ThreadId(), data->t3->ThreadId()}));
  EXPECT_THAT(
      data->process_trace_collector->ThreadIds(),
      testing::IsSupersetOf({*data->self, data->t1->ThreadId(),
                             data->t2->ThreadId(), data->t3->ThreadId()}));
  EXPECT_THAT(
      data->process_thread_collector->ThreadIds(),
      testing::IsSupersetOf({*data->self, data->t1->ThreadId(),
                             data->t2->ThreadId(), data->t3->ThreadId()}));
}

// CheckNoFilterProcessStackTracesExecute verifies that with no filter
// both process_trace and process_thread execute as expected.
void CheckNoFilterProcessStackTracesExecute(CheckProcessStackTracesData* data) {
  RetryOnDrop(data, [data] {
    Thread_ProcessStackTracesArg arg;
    arg.process_trace = &data->process_thread_collector->AddStackTrace;
    arg.process_trace_arg = data->process_thread_collector;
    arg.process_thread =
        &data->process_trace_collector->AddStackTraceLiveThreadState;
    arg.process_thread_arg = data->process_trace_collector;
    arg.per_thread_timeout_ms = kPerThreadTimeoutMs;

    return Thread_ProcessStackTraces(arg);
  });

#if THREAD_HAVE_THREAD_CONTROL
  const int kExpectedThreads = 4;
#else
  const int kExpectedThreads = 6;
#endif
  EXPECT_EQ(kExpectedThreads, data->process_trace_collector->Count());
  EXPECT_EQ(kExpectedThreads, data->process_thread_collector->Count());
  EXPECT_EQ(0, data->filter_collector->Count());
  EXPECT_THAT(
      data->process_trace_collector->ThreadIds(),
      testing::IsSupersetOf({*data->self, data->t1->ThreadId(),
                             data->t2->ThreadId(), data->t3->ThreadId()}));
  EXPECT_THAT(
      data->process_thread_collector->ThreadIds(),
      testing::IsSupersetOf({*data->self, data->t1->ThreadId(),
                             data->t2->ThreadId(), data->t3->ThreadId()}));
}

TEST(ThreadTest, CheckProcessStackTraces) {
  if (!ShouldRunSignalTest("CheckProcessStackTraces")) {
    return;
  }

  // 6 threads max in this test.
  ThreadCollector filter_collector(6);
  ThreadCollector process_trace_collector(6);
  ThreadCollector process_thread_collector(6);
  ThreadCollector::ThreadTids self(GetTID(), pthread_self());

  CheckProcessStackTracesData data;
  data.self = &self;
  data.filter_collector = &filter_collector;
  data.process_trace_collector = &process_trace_collector;
  data.process_thread_collector = &process_thread_collector;

  // Basic sanity: we believe we start out with one thread active, try
  // to process it but filter it out.
  CheckSingleThreadActive(&data);

  // Add the threads we'll use for this test.
  LiveThreadTestThread t1, t2, t3;
  data.t1 = &t1;
  data.t2 = &t2;
  data.t3 = &t3;
  t1.Start();
  t2.Start();
  t3.Start();

  t1.controller_.WaitUntilStarted();
  t2.controller_.WaitUntilStarted();
  t3.controller_.WaitUntilStarted();

  // Next verify filter drops all threads.
  CheckFilterDropsAll(&data);

  // Now check all 3 (filter, process_trace, process_thread) run.
  CheckAllProcessStackTracesExecute(&data);

  // Finally verify with no filter set both process_trace and process_thread
  // execute.
  CheckNoFilterProcessStackTracesExecute(&data);

  t1.controller_.Stop();
  t2.controller_.Stop();
  t3.controller_.Stop();
  t1.Join();
  t2.Join();
  t3.Join();

  // We should be back to one thread active.
  CheckSingleThreadActive(&data);
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

  EXPECT_EQ(sum, 0);
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
  uint64_t trace_id;
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
    if (stack.thread_name.find(thread_name_prefix) == 0) {
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
          "is in function " + testing::PrintToString(address)) {
  return FrameInFunction(arg, reinterpret_cast<void*>(address));
}

TEST(ThreadTest, ProcessStackTracesCorrectStacks) {
  if (!ShouldRunSignalTest("ProcessStackTracesCorrectStacks")) {
    return;
  }
#if defined(ABSL_HAVE_MEMORY_SANITIZER)
  // TODO - Fix this test to work with MSAN.
  GTEST_SKIP() << "Skipping test because it doesn't work MSAN.";
#endif

  thread::Options options;
  options.set_joinable(true);
  StackTraceTestThread t(options, "cpu_busy");
  t.Start();
  absl::SleepFor(absl::Milliseconds(50));
  std::vector<CollectedStack> stacks = CollectStacks();
  t.Join();

  const CollectedStack* main_stack = FindThreadStack(stacks, "main");
  ASSERT_NE(main_stack, nullptr);
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

static std::string ExpectedThreadName(absl::string_view prefix, pid_t pid) {
  return absl::StrFormat("%s/%d", prefix, static_cast<int64_t>(pid));
}

TEST(ThreadTest, CheckThreadNameMain) {
  CHECK_STREQ("main", LiveThread_NamePrefix(Thread_GetMyLiveThread()));
  CHECK_STREQ(ExpectedThreadName("main", GetTID()).c_str(),
              LiveThread_Name(Thread_GetMyLiveThread()));
}

TEST(ThreadTest, CheckThreadNameUnnamed) {
  LiveThreadTestThread t1;
  // We do not set the thread's name before starting it.

  t1.Start();
  t1.controller_.WaitUntilStarted();

  ThreadCollector collector(4);

  Thread_ProcessStackTracesArg arg;
  arg.filter = &collector.FalseAdd;
  arg.filter_arg = &collector;
  arg.per_thread_timeout_ms = kPerThreadTimeoutMs;
  Thread_ProcessStackTraces(arg);

#if THREAD_HAVE_THREAD_CONTROL
  const int kExpectedThreads = 2;
#else
  const int kExpectedThreads = 4;
#endif
  CHECK_EQ(kExpectedThreads, collector.Count());
  CHECK(collector.ContainsNamePrefix("main"));
  CHECK(collector.ContainsNamedThread(
      ThreadCollector::ThreadTids(GetTID(), pthread_self()),
      ExpectedThreadName("main", GetTID())));
  constexpr absl::string_view kThreadNamePrefix =
      "gloop_thread_thread_unittest";
  CHECK(collector.ContainsNamePrefix(kThreadNamePrefix));
  CHECK(collector.ContainsNamedThread(
      t1.controller_.GetThreadTids(),
      ExpectedThreadName(kThreadNamePrefix,
                         t1.controller_.GetThreadTids().os_tid)));

  t1.controller_.Stop();
  t1.Join();
}

TEST(ThreadTest, SanitizeThreadNamePrefix) {
  std::vector<std::pair<std::string, std::string>> before_after{
      {"", ""},
      {"2abc", "_abc"},
      {"!@#$%^&*()", "__________"},
      {"2foo☺*&^%$#(@)!", "_foo_____________"}};
  for (const auto& [before, after] : before_after) {
    EXPECT_EQ(thread::SanitizeThreadNamePrefix(before), after);
  }
}

TEST(ThreadTest, CheckThreadNameNamed) {
  LiveThreadTestThread t1;
  t1.SetNamePrefix("B0_b");

  t1.Start();
  t1.controller_.WaitUntilStarted();

  ThreadCollector collector(4);

  Thread_ProcessStackTracesArg arg;
  arg.filter = &collector.FalseAdd;
  arg.filter_arg = &collector;
  arg.per_thread_timeout_ms = kPerThreadTimeoutMs;
  Thread_ProcessStackTraces(arg);
#if THREAD_HAVE_THREAD_CONTROL
  const int kExpectedThreads = 2;
#else
  const int kExpectedThreads = 4;
#endif
  CHECK_EQ(kExpectedThreads, collector.Count());
  CHECK(collector.ContainsNamePrefix("main"));
  CHECK(collector.ContainsNamedThread(
      ThreadCollector::ThreadTids(GetTID(), pthread_self()),
      ExpectedThreadName("main", GetTID())));
  CHECK(collector.ContainsNamePrefix("B0_b"));
  CHECK(collector.ContainsNamedThread(
      t1.controller_.GetThreadTids(),
      ExpectedThreadName("B0_b", t1.controller_.GetThreadTids().os_tid)));

  t1.controller_.Stop();
  t1.Join();
}

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
  if (!ShouldRunSignalTest("CheckTraceContextThreadStatusRace")) {
    return;
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
  helper_thread.controller_.WaitUntilStarted();

  NullThreadStackWriter writer;
  Thread_ExtractStacks(&writer);

  helper_thread.controller_.Stop();
  helper_thread.Join();
}

TEST(ThreadTest, CheckStatusFilledForThreadProcessStackTraces) {
  if (!ShouldRunSignalTest("CheckStatusFilledForThread_ProcessStackTraces")) {
    return;
  }

  // Reuse ThreadStatusRaceHelper since it sets a status so can verify
  // access.
  ThreadStatusRaceHelper helper_thread;
  helper_thread.Start();
  helper_thread.controller_.WaitUntilStarted();

  bool got_status = false;
  auto verify_status = +[](void* arg, const LiveThreadState& state) {
    if (state.thread_status != nullptr) {
      *static_cast<bool*>(arg) = true;  // Sets got_status
      // Make sure it's a valid and accessible pointer.
      LOG(INFO) << "Thread status: " << state.thread_status;
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

  helper_thread.controller_.Stop();
  helper_thread.Join();
}

class SigIgnoreTestThread : public Thread {
 public:
  LiveThreadTestController controller_;

  SigIgnoreTestThread() { SetJoinable(true); }

  void Run() override {
    CHECK_EQ(GetTID(), LiveThread_OS_TID(Thread_GetMyLiveThread()));
    CHECK_EQ(pthread_self(), LiveThread_Pthread_TID(Thread_GetMyLiveThread()));
    // Disable the signals so this can't receive.
    sigset_t mask_to_block, orig_mask;
    CHECK_EQ(0, sigemptyset(&mask_to_block));
    CHECK_EQ(0, sigaddset(&mask_to_block, GOOGLE_OBSCURE_SIGNAL));
    CHECK_EQ(0, pthread_sigmask(SIG_BLOCK, &mask_to_block, &orig_mask));
    CHECK_EQ(0, sigismember(&orig_mask, GOOGLE_OBSCURE_SIGNAL));

    controller_.MarkAsStarted();

    controller_.WaitForStopCommand();
    CHECK_EQ(0, pthread_sigmask(SIG_UNBLOCK, &mask_to_block, &orig_mask));
  }
};

static void DoNothing(void* arg, const LiveThreadState& state) {}

TEST(ThreadTest, CheckThreadProcessStackTracesSigSafe) {
  LiveThreadTestThread t1, t2, t3;
  t1.Start();
  t2.Start();
  t3.Start();

  t1.controller_.WaitUntilStarted();
  t2.controller_.WaitUntilStarted();
  t3.controller_.WaitUntilStarted();

  // Create a new thread that blocks the signal
  SigIgnoreTestThread t4;
  t4.Start();
  t4.controller_.WaitUntilStarted();

  Thread_ProcessStackTracesArg arg;
  arg.process_thread = &DoNothing;
  arg.process_thread_arg = nullptr;
  arg.sigsafe = true;
  arg.per_thread_timeout_ms = kPerThreadTimeoutMs;

  // At least one always fails
  CHECK_GE(Thread_ProcessStackTraces(arg), 1);

  t4.controller_.Stop();
  t4.Join();

  // Should be 0 but could be higher under load so avoid flakes.
  CHECK_GE(Thread_ProcessStackTraces(arg), 0);

  t1.controller_.Stop();
  t2.controller_.Stop();
  t3.controller_.Stop();
  t1.Join();
  t2.Join();
  t3.Join();
}

static void NullFuncInTarget(void* arg, ucontext_t* uc,
                             const LiveThread* thread) {}

TEST(ThreadTest, CheckInEachSignalDisabled) {
  if (!ShouldRunSignalTest("CheckInEachSignalDisabled")) {
    return;
  }

  sigset_t mask_to_block, orig_mask;
  CHECK_EQ(0, sigemptyset(&mask_to_block));
  CHECK_EQ(0, sigaddset(&mask_to_block, GOOGLE_OBSCURE_SIGNAL));
  CHECK_EQ(0, pthread_sigmask(SIG_BLOCK, &mask_to_block, &orig_mask));
  CHECK_EQ(0, sigismember(&orig_mask, GOOGLE_OBSCURE_SIGNAL));

  // We assume that there is only one Thread running -- the main
  // thread.
  CHECK_EQ(1, Thread_ForEach(nullptr, nullptr, NullFuncInTarget, nullptr,
                             kPerThreadTimeoutMs));

  CHECK_EQ(0, pthread_sigmask(SIG_UNBLOCK, &mask_to_block, &orig_mask));

  // Make sure Thread_ForEach works again, after unblocking the signal.
  CHECK_EQ(0, Thread_ForEach(nullptr, nullptr, NullFuncInTarget, nullptr,
                             kPerThreadTimeoutMs));
}

static void BenchmarkNullInEach() {
  if (!ShouldRunSignalTest("BenchmarkNullInEach")) {
    return;
  }

  const int num_threads = 50;
  std::unique_ptr<LiveThreadTestThread[]> threads(
      new LiveThreadTestThread[num_threads]);
  const int num_iterations = 100;

  // Thread_ForEach is best-effort, so tolerate 5% error.
  const int allowed_failures = (num_threads * num_iterations) / 20;
  int failures = 0;

  LOG(INFO) << "Benchmarking NullInEach with " << num_threads << " threads";

  for (int i = 0; i < num_threads; ++i) {
    threads[i].Start();
    threads[i].controller_.WaitUntilStarted();
  }

  // This is just intended to be a rough number.
  failures += Thread_ForEach(nullptr, nullptr, NullFuncInTarget, nullptr,
                             kPerThreadTimeoutMs);
  absl::Time start_time = absl::Now();
  for (int i = 0; i < num_iterations; ++i) {
    failures += Thread_ForEach(nullptr, nullptr, NullFuncInTarget, nullptr,
                               kPerThreadTimeoutMs);
  }
  absl::Time end_time = absl::Now();

  CHECK_LE(failures, allowed_failures)
      << "from " << num_iterations << " iterations with " << num_threads
      << " threads live";

  for (int i = 0; i < num_threads; ++i) {
    threads[i].controller_.Stop();
    threads[i].Join();
  }

  LOG(INFO) << "NullInEach: "
            << (uint64_t)(1e9 *
                          ((absl::ToDoubleSeconds(end_time - start_time)) /
                           (num_threads * num_iterations)))
            << " ns per thread\n";
}

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
    LOG(WARNING) << "Skip because stack traces are not expected to work.";
    return;
  }
  void* pc = RecursiveCallPC();
  std::string dump;
  Recurse(100, [&dump] { SaveStackTraceDump(&dump); });

  // Since we line-wrap the pcs, there must be some line that
  // contains the recursive call pc more than once.
  CHECK(RE2::PartialMatch(dump, absl::StrFormat(" %p +%p", pc, pc)))
      << pc << " in " << dump;
}

TEST(ThreadTest, CheckOtherStack) {
  if (!absl::debugging_internal::StackTraceWorksForTest()) {
    LOG(WARNING) << "Skip because stack traces are not expected to work.";
    return;
  }
  std::string dump;

  // Fetch stack trace while recursing to an arbitrary depth in another thread
  {
    ThreadPool pool(1);
    pool.Schedule(
        absl::bind_front(Recurse, 20, [&dump] { SaveStackTraceDump(&dump); }));
  }

  void* pc = RecursiveCallPC();
  CHECK(RE2::PartialMatch(dump, absl::StrFormat(" %p +%p", pc, pc)))
      << pc << " in " << dump;

  // Must contain a creator: entry
  CHECK(RE2::PartialMatch(dump, "creator:")) << dump;
}

TEST(ThreadTest, CheckStackLineWrap) {
  if (!absl::debugging_internal::StackTraceWorksForTest()) {
    LOG(WARNING) << "Skip because stack traces are not expected to work.";
    return;
  }
  void* pc = RecursiveCallPC();
  std::string dump;
  Recurse(100, [&dump] { SaveStackTraceDump(&dump); });
  std::vector<std::string> lines =
      absl::StrSplit(dump, '\n', absl::SkipEmpty());

  // Every line that contains "pc" must be at most 80 chars in length
  for (size_t i = 0; i < lines.size(); i++) {
    if (RE2::PartialMatch(lines[i], absl::StrFormat(" %p ", pc))) {
      CHECK_LE(lines[i].size(), 80) << lines[i];
    }
  }
}

TEST(ThreadTest, CheckStackTop) {
  if (!absl::debugging_internal::StackTraceWorksForTest()) {
    LOG(WARNING) << "Skip because stack traces are not expected to work.";
    return;
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
    LOG(WARNING) << "Skip because stack traces are not expected to work.";
    return;
  }

  std::string dump;
  {
    thread::Note note("test_note");
    Recurse(100, [&dump] { SaveStackTraceDump(&dump); });
  }

  EXPECT_THAT(dump, testing::HasSubstr("note: test_note\n"));
}

TEST(ThreadTest, DumpGilHolder) {
  std::string dump;

  PythonGilHolderLookupForTest::Register(nullptr);
  SaveStackTraceDump(&dump);
  EXPECT_THAT(dump, Not(testing::HasSubstr("--- Python GIL")));

  auto return_minus_one = []() { return static_cast<int64_t>(-1); };
  PythonGilHolderLookupForTest::Register(return_minus_one);
  SaveStackTraceDump(&dump);
  EXPECT_THAT(dump, Not(testing::HasSubstr("--- Python GIL")));

  auto return_valid = []() { return static_cast<int64_t>(-42); };
  PythonGilHolderLookupForTest::Register(return_valid);
  SaveStackTraceDump(&dump);
  EXPECT_THAT(dump,
              testing::HasSubstr(
                  "--- Python GIL held by thread ffffffffffffffd6 ---\n"));

  PythonGilHolderLookupForTest::Register(nullptr);
}

void ListOneNote(void* arg, absl::string_view note) {
  auto* v = static_cast<std::vector<std::string>*>(arg);
  v->push_back(std::string(note));
}

void ExpectNotes(const LiveThread* thread,
                 absl::Span<const std::string> expectation) {
  EXPECT_THAT(LiveThread_GetNotes(thread),
              testing::UnorderedElementsAreArray(expectation));

  std::vector<std::string> notes;
  EXPECT_TRUE(LiveThread_ForEachNoteAsyncSignalSafe(thread, nullptr,
                                                    ListOneNote, &notes));
  EXPECT_THAT(notes, testing::UnorderedElementsAreArray(expectation));
}

void ReetrantNoteReader(void* arg, absl::string_view note) {
  // Mainly we're just testing that this doesn't hang.
  const LiveThread* thread = Thread_GetMyLiveThread();
  EXPECT_FALSE(LiveThread_ForEachNoteAsyncSignalSafe(
      thread, nullptr, ReetrantNoteReader, nullptr));
}

TEST(ThreadTest, Notes) {
  const LiveThread* thread = Thread_GetMyLiveThread();

  ExpectNotes(thread, {});

  {
    thread::Note note1("Note1");
    ExpectNotes(thread, {"Note1"});
  }

  {
    thread::Note note2("Note2");
    thread::Note note3("Note3");
    ExpectNotes(thread, {"Note2", "Note3"});
  }

  {
    std::string s("Note4");
    thread::Note note4(std::move(s));
    ExpectNotes(thread, {"Note4"});
  }

  {
    absl::string_view s = "Note5";
    thread::Note note5(s);
    ExpectNotes(thread, {"Note5"});
  }

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
    CHECK_EQ(thread, tester->self_);

    if (trace != nullptr) {
      // Make sure we do get the notes in this case.
      auto notes_for_trace = LiveThread_GetNotesForTrace(thread, trace);
      EXPECT_FALSE(notes_for_trace.notes_changed_since_stack_trace);
      EXPECT_THAT(notes_for_trace.notes,
                  testing::UnorderedElementsAreArray({"Note1"}));

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
      EXPECT_THAT(notes_for_trace.notes, testing::IsEmpty());

      auto fn = [](void* arg, absl::string_view note) {};
      EXPECT_FALSE(
          LiveThread_ForEachNoteAsyncSignalSafe(thread, trace, fn, nullptr));
    }
  };

  Thread_ProcessStackTraces(arg);

  tester.stop_.Notify();
  tester.Join();
}

}  // namespace

// We want to test thread watchers and watchdogs for mobile platforms regardless
// of default value. So forward-declaring here in order to be able to override.
ABSL_DECLARE_FLAG(bool, watch_pthread_manager);
ABSL_DECLARE_FLAG(bool, watch_thread_liveness);

int main(int argc, char** argv) {
  // Disable background threads so we know how many threads exist.
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
  InitGoogle(argv[0], &argc, &argv, true);

  BenchmarkNullInEach();

  return RUN_ALL_TESTS();
}
