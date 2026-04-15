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

#include "gloop/thread/wait_state.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/no_destructor.h"
#include "absl/cleanup/cleanup.h"
#include "absl/debugging/stacktrace.h"
#include "absl/flags/flag.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/strings/cord.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/barrier.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/thread-identity.h"
#include "gloop/base/walltime.h"
#include "gloop/thread/periodicclosure.h"
#include "gloop/thread/thread-internal.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"
#include "gloop/thread/threadpool.h"
#include "gloop/thread/timedcall.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace thread {

using ::testing::HasSubstr;
using ::testing::Not;

TEST(WaitStateScopeTest, ScopeRestoresPriorState) {
  // NOLINTNEXTLINE(abseil-no-internal-dependencies)
  absl::base_internal::ThreadIdentity* ti =
      absl::base_internal::CurrentThreadIdentityIfPresent();
  ASSERT_NE(ti, nullptr);

  // The default state is kActive.
  EXPECT_EQ(ti->wait_state.load(), WaitStateScope::WaitState::kActive);
  {
    // Create the first scope and expect to see its effect.
    WaitStateScope scope1(WaitStateScope::WaitState::kWaitingForWork);
    EXPECT_EQ(ti->wait_state.load(),
              WaitStateScope::WaitState::kWaitingForWork);
    {
      // Create another scope.
      WaitStateScope scope1(WaitStateScope::WaitState::kActive);
      EXPECT_EQ(ti->wait_state.load(), WaitStateScope::WaitState::kActive);
    }
    // After popping the second scope, we should have our first scope back.
    EXPECT_EQ(ti->wait_state.load(),
              WaitStateScope::WaitState::kWaitingForWork);
  }

  // The default state should be restored.
  EXPECT_EQ(ti->wait_state.load(), WaitStateScope::WaitState::kActive);
}

TEST(WaitStateScopeTest, ConditionalScope) {
  // NOLINTNEXTLINE(abseil-no-internal-dependencies)
  absl::base_internal::ThreadIdentity* ti =
      absl::base_internal::CurrentThreadIdentityIfPresent();
  ASSERT_NE(ti, nullptr);

  // The default state is kActive.
  EXPECT_EQ(ti->wait_state.load(), WaitStateScope::WaitState::kActive);
  {
    // Create a scope object which is conditionally disabled. We should not see
    // a change in state.
    WaitStateScope scope1(WaitStateScope::WaitState::kWaitingForWork,
                          /*enabled=*/false);
    EXPECT_EQ(ti->wait_state.load(), WaitStateScope::WaitState::kActive);

    // Create another scope object which is conditionally enabled.
    WaitStateScope scope2(WaitStateScope::WaitState::kWaitingForWork,
                          /*enabled=*/true);
    EXPECT_EQ(ti->wait_state.load(),
              WaitStateScope::WaitState::kWaitingForWork);

    // Create another disabled scope object.
    WaitStateScope scope3(WaitStateScope::WaitState::kActive,
                          /*enabled=*/false);
    EXPECT_EQ(ti->wait_state.load(),
              WaitStateScope::WaitState::kWaitingForWork);
  }

  // Pop every object. The default state should be restored.
  EXPECT_EQ(ti->wait_state.load(), WaitStateScope::WaitState::kActive);
}

class CordStackWriter : public ThreadStackWriter {
 public:
  void Write(const char* data, int data_length) override {
    buffer_.Append(absl::string_view(data, data_length));
  }

  std::string ToString() const { return std::string(buffer_); }

 private:
  absl::Cord buffer_;
};

using PrintStackFunction = std::function<void(ThreadStackWriter*)>;
class WaitStateScopePrintTest
    : public ::testing::TestWithParam<PrintStackFunction> {
 public:
  WaitStateScopePrintTest() {
    absl::SetFlag(&FLAGS_stacktrace_skip_waiting_threads, true);
  }

 protected:
  // It's possible that we failed to fetch the userspace stack trace, in which
  // case we would not know to skip this thread. Detect that case and consider
  // it a pass.
  static bool ThreadSkipped(absl::string_view output, absl::string_view name) {
    return !absl::StrContains(output, name) ||
           absl::StrContains(output, "could not fetch");
  }

  std::string ExtractStacks() {
    CordStackWriter writer;
    GetParam()(&writer);
    return writer.ToString();
  }
};

TEST_P(WaitStateScopePrintTest, WaitingThreadSkippedInPrintStacktrace) {
  if (!absl::debugging_internal::StackTraceWorksForTest()) {
    LOG(WARNING) << "Skipping because stack traces are not expected to work.";
    return;
  }

  absl::Barrier start(3);
  absl::Barrier done(3);
  absl::Notification busy_started;

  // Start two threads, one is busy and one is waiting for work.
  ClosureThread busy_thread(
      thread::Options().set_joinable(true),
      /*name_prefix=*/"test_busy_thread", [&]() {
        WaitStateScope scope(WaitStateScope::WaitState::kActive);
        busy_started.Notify();
        start.Block();
        done.Block();
      });
  busy_thread.Start();
  ClosureThread waiting_thread(
      thread::Options().set_joinable(true),
      /*name_prefix=*/"test_waiting_thread", [&]() {
        WaitStateScope scope(WaitStateScope::WaitState::kWaitingForWork);
        start.Block();
        done.Block();
      });
  waiting_thread.Start();
  start.Block();
  auto cleanup = absl::MakeCleanup([&]() {
    done.Block();
    busy_thread.Join();
    waiting_thread.Join();
  });

  busy_started.WaitForNotification();
  EXPECT_THAT(ExtractStacks(), HasSubstr("test_busy_thread"));
  for (int i = 0; i < 20; ++i) {
    if (!absl::StrContains(ExtractStacks(), "test_waiting_thread")) {
      break;
    }
    absl::SleepFor(absl::Milliseconds(100 * i));
  }
  EXPECT_THAT(ExtractStacks(), Not(HasSubstr("test_waiting_thread")));
}

INSTANTIATE_TEST_SUITE_P(PrintstackImpls, WaitStateScopePrintTest,
                         ::testing::Values(&Thread_ExtractStacks,
                                           &Thread_SignalSafe_DumpStacksTo));

// API for spawning a worker thread to run some function and return the name of
// that thread.
using SpawnWorkerThread =
    std::function<std::string(absl::AnyInvocable<void() &&>)>;
class ActiveWorkerTest : public ::testing::TestWithParam<SpawnWorkerThread> {
 public:
  ActiveWorkerTest() : done_(std::make_shared<absl::Notification>()) {
    absl::SetFlag(&FLAGS_stacktrace_skip_waiting_threads, true);
  }
  ~ActiveWorkerTest() override { FinishWork(); }

 protected:
  static std::string ExtractStacks() {
    CordStackWriter writer;
    Thread_ExtractStacks(&writer);
    return writer.ToString();
  }

  void FinishWork() {
    if (done_ != nullptr) {
      done_->Notify();
      done_.reset();
    }
  }

  std::shared_ptr<absl::Notification> done_;
};

TEST_P(ActiveWorkerTest, OnlyWaitingThreadsSkipped) {
  // Start some work.
  absl::Notification started;
  const std::string thread_name = GetParam()([&started, done = done_]() {
    started.Notify();
    done->WaitForNotification();
  });
  started.WaitForNotification();

  // We should see this worker thread.
  CordStackWriter writer;
  Thread_ExtractStacks(&writer);
  LOG(INFO) << "Waiting for worker thread to appear...";
  EXPECT_THAT(ExtractStacks(), HasSubstr(thread_name));

  // Unblock the work and wait for the worker thread to go idle.
  FinishWork();

  // We should eventually not see its stacktrace.
  LOG(INFO) << "Waiting for idle thread to go away...";
  for (int i = 0; i < 20; ++i) {
    if (!absl::StrContains(ExtractStacks(), thread_name)) {
      break;
    }
    absl::SleepFor(absl::Milliseconds(100 * i));
  }
  EXPECT_THAT(ExtractStacks(), Not(HasSubstr(thread_name)));
}

// Various implementations of SpawnWorkerThread.
namespace {

std::string RunOnThreadPool(absl::AnyInvocable<void() &&> f) {
  const std::string kName = "active-worker-test-threadpool";
  static absl::NoDestructor<std::unique_ptr<ThreadPool>> pool([&]() {
    auto pool = std::make_unique<ThreadPool>(
        /*num_workers=*/1, ThreadPool::Options{.name_prefix = kName});

    return pool;
  }());
  (*pool)->Schedule(std::move(f));
  return kName;
}

std::string RunOnTimedCall(absl::AnyInvocable<void() &&> f) {
  // Create a new TimedCall that will delete itself when done.
  auto tc = std::make_unique<TimedCall>();
  tc->Set(base::ToWallTime(absl::Now()),
          [f = std::move(f), tc = std::move(tc)]() mutable { std::move(f)(); });
  // This is the name of the one thread that runs all TimedCalls.
  return "timedcall";
}
}  // namespace

INSTANTIATE_TEST_SUITE_P(ExecutorImpls, ActiveWorkerTest,
                         ::testing::Values(RunOnThreadPool, RunOnTimedCall));

}  // namespace thread
