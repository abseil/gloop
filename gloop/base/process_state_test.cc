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

#include "gloop/base/process_state.h"

#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/config.h"  // IWYU pragma: keep
#include "absl/base/const_init.h"
#include "absl/base/macros.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "gloop/base/config.h"
#include "gloop/base/internal/logging_directories.h"
#include "gloop/base/signal-handler.h"
#include "gloop/thread/thread.h"
#include "gloop/thread/thread_options.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AllOf;
using testing::ContainsRegex;
using testing::HasSubstr;
using testing::Not;

namespace thread::internal {
size_t MinValidStackSizeWithTlsAndSanitizers(size_t, size_t*);
}  // namespace thread::internal
using ::thread::internal::MinValidStackSizeWithTlsAndSanitizers;

TEST(ProcessState, GetKernelVersionString) {
#if !defined(OS_WINDOWS) && !defined(__APPLE__)
  const char* p = GetKernelVersionString();
  EXPECT_LT(0, strlen(p));
  EXPECT_NE('<', GetKernelVersionString()[0]);
#endif
}

TEST(ProcessState, GetKernelVersion) {
#if !defined(OS_WINDOWS) && !defined(__APPLE__)
  struct KernelVersion kv;
  GetKernelVersion(&kv);
  EXPECT_LE(2, kv.major);  // We will not be using 1.x series linux any more.
#endif
}

TEST(ProcessState, FailureSignalHandler) {
  ASSERT_FALSE(InFailureSignalHandler());
  ASSERT_FALSE(IsFailureSignalHandlerRunning());
}

TEST(ProcessState, ProcessIsDying) { ASSERT_FALSE(base::ProcessIsDying()); }

TEST(ProcessState, ShouldInstallDefaultSignalHandlers) {
  bool original_install_signal_handlers =
      absl::GetFlag(FLAGS_install_signal_handlers);
  std::string original_install_named_signal_handlers =
      absl::GetFlag(FLAGS_install_named_signal_handlers);
  bool original_use_alternate_stack_for_signal_handlers =
      absl::GetFlag(FLAGS_use_alternate_stack_for_signal_handlers);

  // In the following:
  // 10 is not present; 11 is asserted, 12 is negated;
  // 13 is asserted then negated; 17 is negated then asserted.
  // A repeated comma tests the "empty item" case.
  static const char flag_setting0[] =
      "asserted,-negated,-negated_then_asserted,asserted_then_negated,,"
      "negated_then_asserted,-asserted_then_negated,11,-12,-17,13,17,-13";
  // Same but with the signal numbers first.
  static const char flag_setting1[] =
      "11,-12,-17,13,17,-13,asserted,-negated,-negated_then_asserted,,"
      "asserted_then_negated,negated_then_asserted,-asserted_then_negated";

  static const struct {
    int line;
    const char* install_named_signal_handlers;
    bool use_alternative_signal_handler_stack_result;
    const char* signal_group;
    int signal;
    bool should_install_signal_handler_result;
  } test[] = {
      {__LINE__, flag_setting0, true, "not_present", 10, false},
      {__LINE__, flag_setting0, true, "not_present", 11, true},
      {__LINE__, flag_setting0, true, "not_present", 12, false},
      {__LINE__, flag_setting0, true, "not_present", 13, false},
      {__LINE__, flag_setting0, true, "not_present", 17, true},

      {__LINE__, flag_setting0, true, "asserted", 10, true},
      {__LINE__, flag_setting0, true, "asserted", 11, true},
      {__LINE__, flag_setting0, true, "asserted", 12, false},
      {__LINE__, flag_setting0, true, "asserted", 13, false},
      {__LINE__, flag_setting0, true, "asserted", 17, true},

      {__LINE__, flag_setting0, true, "negated", 10, false},
      {__LINE__, flag_setting0, true, "negated", 11, true},
      {__LINE__, flag_setting0, true, "negated", 12, false},
      {__LINE__, flag_setting0, true, "negated", 13, false},
      {__LINE__, flag_setting0, true, "negated", 17, true},

      {__LINE__, flag_setting0, true, "asserted_then_negated", 10, false},
      {__LINE__, flag_setting0, true, "asserted_then_negated", 11, true},
      {__LINE__, flag_setting0, true, "asserted_then_negated", 12, false},
      {__LINE__, flag_setting0, true, "asserted_then_negated", 13, false},
      {__LINE__, flag_setting0, true, "asserted_then_negated", 17, true},

      {__LINE__, flag_setting0, true, "negated_then_asserted", 10, true},
      {__LINE__, flag_setting0, true, "negated_then_asserted", 11, true},
      {__LINE__, flag_setting0, true, "negated_then_asserted", 12, false},
      {__LINE__, flag_setting0, true, "negated_then_asserted", 13, false},
      {__LINE__, flag_setting0, true, "negated_then_asserted", 17, true},

      {__LINE__, flag_setting1, true, "not_present", 10, false},
      {__LINE__, flag_setting1, true, "not_present", 11, true},
      {__LINE__, flag_setting1, true, "not_present", 12, false},
      {__LINE__, flag_setting1, true, "not_present", 13, false},
      {__LINE__, flag_setting1, true, "not_present", 17, true},

      {__LINE__, flag_setting1, true, "asserted", 10, true},
      {__LINE__, flag_setting1, true, "asserted", 11, true},
      {__LINE__, flag_setting1, true, "asserted", 12, true},
      {__LINE__, flag_setting1, true, "asserted", 13, true},
      {__LINE__, flag_setting1, true, "asserted", 17, true},

      {__LINE__, flag_setting1, true, "negated", 10, false},
      {__LINE__, flag_setting1, true, "negated", 11, false},
      {__LINE__, flag_setting1, true, "negated", 12, false},
      {__LINE__, flag_setting1, true, "negated", 13, false},
      {__LINE__, flag_setting1, true, "negated", 17, false},

      {__LINE__, flag_setting1, true, "asserted_then_negated", 10, false},
      {__LINE__, flag_setting1, true, "asserted_then_negated", 11, false},
      {__LINE__, flag_setting1, true, "asserted_then_negated", 12, false},
      {__LINE__, flag_setting1, true, "asserted_then_negated", 13, false},
      {__LINE__, flag_setting1, true, "asserted_then_negated", 17, false},

      {__LINE__, flag_setting1, true, "negated_then_asserted", 10, true},
      {__LINE__, flag_setting1, true, "negated_then_asserted", 11, true},
      {__LINE__, flag_setting1, true, "negated_then_asserted", 12, true},
      {__LINE__, flag_setting1, true, "negated_then_asserted", 13, true},
      {__LINE__, flag_setting1, true, "negated_then_asserted", 17, true},

      {__LINE__, ",", true, "anything", 10, false},

      {__LINE__, "", false, "not_present", 10, false},
  };

  // ShouldInstallDefaultSignalHandler always answers false if there is already
  // a handler. Remove and restore existing handlers for each signal under test.
  struct sigaction dfl_sa = {}, prev_sa;
  dfl_sa.sa_handler = SIG_DFL;

  for (int i = 0; i != ABSL_ARRAYSIZE(test); i++) {
    absl::SetFlag(&FLAGS_install_named_signal_handlers,
                  test[i].install_named_signal_handlers);
    sigaction(test[i].signal, &dfl_sa, &prev_sa);

    for (int j = 0; j != 2; j++) {
      absl::SetFlag(&FLAGS_install_signal_handlers, (j == 1));
      for (int k = 0; k != 2; k++) {
        absl::SetFlag(&FLAGS_use_alternate_stack_for_signal_handlers, (k == 1));
        bool result =
            test[i].use_alternative_signal_handler_stack_result &&
            absl::GetFlag(FLAGS_install_signal_handlers) &&
            absl::GetFlag(FLAGS_use_alternate_stack_for_signal_handlers);
        EXPECT_EQ(result, UseAlternateSignalHandlerStack())
            << "   line=" << test[i].line << "   install_signal_handlers="
            << absl::GetFlag(FLAGS_install_signal_handlers)
            << "   use_alternate_stack_for_signal_handlers="
            << absl::GetFlag(FLAGS_use_alternate_stack_for_signal_handlers)
            << "   install_named_signal_handlers="
            << test[i].install_named_signal_handlers;

        result = test[i].should_install_signal_handler_result &&
                 absl::GetFlag(FLAGS_install_signal_handlers);
        EXPECT_EQ(result, ShouldInstallDefaultSignalHandler(
                              test[i].signal_group, test[i].signal))
            << "   line=" << test[i].line << "   install_signal_handlers="
            << absl::GetFlag(FLAGS_install_signal_handlers)
            << "   use_alternate_stack_for_signal_handlers="
            << absl::GetFlag(FLAGS_use_alternate_stack_for_signal_handlers)
            << "   install_named_signal_handlers="
            << test[i].install_named_signal_handlers
            << "   signal_group=" << test[i].signal_group
            << "   signal=" << test[i].signal;
      }
    }

    sigaction(test[i].signal, &prev_sa, nullptr);
  }

  absl::SetFlag(&FLAGS_install_signal_handlers,
                original_install_signal_handlers);
  absl::SetFlag(&FLAGS_install_named_signal_handlers,
                original_install_named_signal_handlers);
  absl::SetFlag(&FLAGS_use_alternate_stack_for_signal_handlers,
                original_use_alternate_stack_for_signal_handlers);
}

TEST(process_state, ShouldInstallSighupSigpipe) {
  static const struct {
    int line;
    bool install_signal_handlers;
    const char* install_named_signal_handlers;
    const char* signal_group;
    bool should_install_signal_handler_result;
  } test[] = {
      {__LINE__, true, "", "hup", true},
      {__LINE__, false, "", "hup", true},
      {__LINE__, true, "", "pipe", true},
      {__LINE__, false, "", "pipe", true},

      {__LINE__, true, "-hup", "hup", false},
      {__LINE__, false, "-hup", "hup", false},
      {__LINE__, true, "-hup", "pipe", true},
      {__LINE__, false, "-hup", "pipe", true},

      {__LINE__, true, "-pipe", "hup", true},
      {__LINE__, false, "-pipe", "hup", true},
      {__LINE__, true, "-pipe", "pipe", false},
      {__LINE__, false, "-pipe", "pipe", false},

      {__LINE__, true, "-hup,-pipe", "hup", false},
      {__LINE__, false, "-hup,-pipe", "hup", false},
      {__LINE__, true, "-hup,-pipe", "pipe", false},
      {__LINE__, false, "-hup,-pipe", "pipe", false},

      {__LINE__, true, "sig1,-hup,-pipe,sig2", "hup", false},
      {__LINE__, false, "sig1,-hup,-pipe,sig2", "hup", false},
      {__LINE__, true, "sig1,-hup,-pipe,sig2", "pipe", false},
      {__LINE__, false, "sig1,-hup,-pipe,sig2", "pipe", false},
  };

  for (int i = 0; i != ABSL_ARRAYSIZE(test); i++) {
    absl::SetFlag(&FLAGS_install_signal_handlers,
                  test[i].install_signal_handlers);
    absl::SetFlag(&FLAGS_install_named_signal_handlers,
                  test[i].install_named_signal_handlers);
    EXPECT_EQ(test[i].should_install_signal_handler_result,
              ShouldInstallDefaultSignalHandler(test[i].signal_group, 0))
        << "   line=" << test[i].line
        << "   install_signal_handlers=" << test[i].install_signal_handlers
        << "   install_named_signal_handlers="
        << test[i].install_named_signal_handlers
        << "   signal_group=" << test[i].signal_group;
  }
}

TEST(process_state, GetRequiredAlternateSignalStackSize) {
  int32_t stack_size = GetRequiredAlternateSignalStackSize();
  ASSERT_LT(0, stack_size);
  EXPECT_EQ(0, stack_size % getpagesize());
}

// This enum duplicated from process_state.cc.
enum IsSafeType { kUnsafe, kSafe };

namespace base {
namespace internal {
extern void ExecuteFailureCallbacks(int, siginfo_t*, void*, IsSafeType);
extern void ExecuteFailure(int, void*, IsSafeType);
extern void ExecuteFailureClosures(int, IsSafeType);
extern void ExecuteCrashDataCallback(int signo, siginfo_t* si, void* uc,
                                     int cpu);
}  // namespace internal
}  // namespace base

namespace crash_analysis {
namespace reporting {
namespace remote_coredumper {
class SetUpHook;
}  // namespace remote_coredumper
}  // namespace reporting
}  // namespace crash_analysis

using crash_analysis::reporting::remote_coredumper::SetUpHook;

class FailureSignalHandlers : public testing::Test {
 public:
  void Callback() { callback_called_ = true; }
  void Callback2(int signo, siginfo_t* info, void* uctx) {
    EXPECT_FALSE(callback_called_);
    callback_called_ = true;
    signo_ = signo;
    si_ = info;
    uctx_ = uctx;
  }

 protected:
  FailureSignalHandlers() = default;

  void SetUp() override {
    callback_called_ = false;
    signo_ = 0;
    si_ = nullptr;
    uctx_ = nullptr;
    crash_data_callback_called_ = false;
  }

  friend class SetUpHook;
  bool callback_called_;
  int signo_;
  siginfo_t* si_;
  void* uctx_;
  bool crash_data_callback_called_;
  FailureSignalHandlers(const FailureSignalHandlers&) = delete;
  FailureSignalHandlers& operator=(const FailureSignalHandlers&) = delete;
};

class crash_analysis::reporting::remote_coredumper::SetUpHook {
 public:
  static void RegisterCallback() {
    base::CrashData::SetCallback(SetUpHook::CrashDataCallback);
  }
  static void RegisterCallbackNull() { base::CrashData::SetCallback(nullptr); }
  static void CrashDataCallback(const base::CrashData* cd) {
    FailureSignalHandlers* f =
        reinterpret_cast<FailureSignalHandlers*>(cd->context_);
    EXPECT_FALSE(f->callback_called_);
    f->callback_called_ = true;
    f->signo_ = cd->signal_;
    f->si_ = cd->si_;
    f->uctx_ = cd->context_;
    f->crash_data_callback_called_ = true;
  }
};

void CallbackWrapper(FailureContext fc) {
  FailureSignalHandlers* p = static_cast<FailureSignalHandlers*>(fc.args);
  p->Callback();
}

void Callback2Wrapper(FailureContext fc) {
  FailureSignalHandlers* p = static_cast<FailureSignalHandlers*>(fc.args);
  p->Callback2(fc.signo, fc.info, fc.context);
}

TEST_F(FailureSignalHandlers, RunOnFailure) {
  RunOnFailure(*CallbackWrapper, this);
  int uctx;
  siginfo_t info;
  base::internal::ExecuteFailureCallbacks(9, &info, &uctx, kUnsafe);
  EXPECT_TRUE(callback_called_);
}

TEST_F(FailureSignalHandlers, RunSignalSafeOnFailure) {
  RunSignalSafeOnFailure(*CallbackWrapper, this);
  int uctx;
  siginfo_t info;
  base::internal::ExecuteFailureCallbacks(9, &info, &uctx, kSafe);
  EXPECT_TRUE(callback_called_);
}

TEST_F(FailureSignalHandlers, RunOnFailureCallback2) {
  RunOnFailure(*Callback2Wrapper, this);
  int uctx;
  siginfo_t info;
  base::internal::ExecuteFailureCallbacks(9, &info, &uctx, kUnsafe);
  EXPECT_TRUE(callback_called_);
  EXPECT_EQ(9, signo_);
  EXPECT_EQ(&info, si_);
  EXPECT_EQ(&uctx, uctx_);
}

TEST_F(FailureSignalHandlers, RunSignalSafeOnFailureCallback2) {
  RunSignalSafeOnFailure(*Callback2Wrapper, this);
  int uctx;
  siginfo_t info;
  base::internal::ExecuteFailureCallbacks(10, &info, &uctx, kSafe);
  EXPECT_TRUE(callback_called_);
  EXPECT_EQ(10, signo_);
  EXPECT_EQ(&info, si_);
  EXPECT_EQ(&uctx, uctx_);
}

TEST_F(FailureSignalHandlers, RunCrashDataCallback) {
  // Can register N times but only calls once
  SetUpHook::RegisterCallback();
  SetUpHook::RegisterCallback();
  siginfo_t si;
  base::internal::ExecuteCrashDataCallback(6, &si, this, -1);
  EXPECT_TRUE(callback_called_);
  EXPECT_EQ(6, signo_);
  EXPECT_EQ(&si, si_);
  EXPECT_EQ(this, uctx_);
  EXPECT_TRUE(crash_data_callback_called_);
  // Reset and verify only one execution occurs.
  SetUp();
  base::internal::ExecuteCrashDataCallback(6, &si, this, -1);
  EXPECT_FALSE(callback_called_);
  EXPECT_EQ(0, signo_);
  EXPECT_EQ(nullptr, si_);
  EXPECT_EQ(nullptr, uctx_);
  EXPECT_FALSE(crash_data_callback_called_);
}

TEST_F(FailureSignalHandlers, CrashDataCallbackNoAction) {
  // Verify no registration means nothing runs.
  siginfo_t si;
  base::internal::ExecuteCrashDataCallback(6, &si, this, -1);
  EXPECT_FALSE(callback_called_);
  EXPECT_EQ(0, signo_);
  EXPECT_EQ(nullptr, si_);
  EXPECT_EQ(nullptr, uctx_);
  EXPECT_FALSE(crash_data_callback_called_);

  // Now verify if we register a nullptr the same occurs
  SetUpHook::RegisterCallbackNull();
  base::internal::ExecuteCrashDataCallback(6, &si, this, -1);
  EXPECT_FALSE(callback_called_);
  EXPECT_EQ(0, signo_);
  EXPECT_EQ(nullptr, si_);
  EXPECT_EQ(nullptr, uctx_);
  EXPECT_FALSE(crash_data_callback_called_);
}

TEST_F(FailureSignalHandlers, CancelRunOnFailure) {
  int ticket = RunOnFailure(*CallbackWrapper, this);
  EXPECT_TRUE(CancelRunOnFailure(ticket));
  EXPECT_FALSE(CancelRunOnFailure(ticket));
  base::internal::ExecuteFailureCallbacks(9, nullptr, nullptr, kUnsafe);
  EXPECT_FALSE(callback_called_);
}

TEST_F(FailureSignalHandlers, CancelRunSignalSafeOnFailure) {
  int ticket = RunSignalSafeOnFailure(*CallbackWrapper, this);
  EXPECT_TRUE(CancelRunSignalSafeOnFailure(ticket));
  EXPECT_FALSE(CancelRunSignalSafeOnFailure(ticket));
  base::internal::ExecuteFailureCallbacks(9, nullptr, nullptr, kSafe);
  EXPECT_FALSE(callback_called_);
}

TEST_F(FailureSignalHandlers, CancelRunOnFailureCallback2) {
  int ticket = RunOnFailure(*Callback2Wrapper, this);
  EXPECT_TRUE(CancelRunOnFailure(ticket));
  EXPECT_FALSE(CancelRunOnFailure(ticket));
  base::internal::ExecuteFailureCallbacks(9, nullptr, nullptr, kUnsafe);
  EXPECT_FALSE(callback_called_);
}

TEST_F(FailureSignalHandlers, CancelRunSignalSafeOnFailureCallback2) {
  int ticket = RunSignalSafeOnFailure(*Callback2Wrapper, this);
  EXPECT_TRUE(CancelRunSignalSafeOnFailure(ticket));
  EXPECT_FALSE(CancelRunSignalSafeOnFailure(ticket));
  base::internal::ExecuteFailureCallbacks(9, nullptr, nullptr, kSafe);
  EXPECT_FALSE(callback_called_);
}

#if ABSL_HAVE_ADDRESS_SANITIZER || ABSL_HAVE_MEMORY_SANITIZER || \
    ABSL_HAVE_THREAD_SANITIZER
// Sanitizers don't allow reading of "random" stack regions.
#else
// From process_state.cc
extern void ImmediateAbortSignalHandlerForTesting(int signo);

// Verify that "See <link>" is *not* emitted when we are
// merely executing on alternate stack and don't have actual stack overflow.
// See b/77328589.
TEST(ProcessState, FailureSignalHandlerNoStackOverflowMessageOnAlternateStack) {
  auto crash_on_alternate_stack = [] {
    // gUnit suppresses failure output in DEATH tests by default.
    // Re-enable it.
    absl::SetFlag(&FLAGS_suppress_failure_output, false);

    // Must not be on stack. Also need to be properly aligned.
    // 16-byte stack alignment is required on x86_64. I assume 64-byte
    // alignment is sufficient everywhere.
    alignas(64) static char altstack[128 << 10];
    ucontext_t main_ctx, crash_ctx;

    getcontext(&crash_ctx);
    crash_ctx.uc_stack.ss_sp = altstack;
    crash_ctx.uc_stack.ss_size = sizeof(altstack);
    crash_ctx.uc_link = &main_ctx;
    makecontext(&crash_ctx, +[] { kill(getpid(), SIGSEGV); }, 0);
    swapcontext(&main_ctx, &crash_ctx);
    LOG(FATAL) << "Unreachable code reached.";
  };
  EXPECT_DEATH(crash_on_alternate_stack(), Not(HasSubstr("STACK OVERFLOW")));
}

TEST(ProcessState, ImmediateAbortSignalHandlerUnblocksSignal) {
  // Block `SIGABRT`.
  sigset_t sa_mask;
  sigemptyset(&sa_mask);
  sigaddset(&sa_mask, SIGABRT);
  pthread_sigmask(SIG_BLOCK, &sa_mask, nullptr);
  // Even though the signal is blocked, `SIGABRT` should still be raised to kill
  // the process within this last resort crash handler.
  EXPECT_DEATH(ImmediateAbortSignalHandlerForTesting(SIGABRT), "");
}
#endif  // Sanitizers

namespace {

// Tests of crash messages from FailureSignalHandler.

void ABSL_ATTRIBUTE_NOINLINE Crash() {
  volatile int* ip = nullptr;
  LOG(INFO) << "Crash and burn!";
  ip[1] = 42;  // Generate SIGSEGV.
  LOG(FATAL) << "Unreachable code reached.";
}

ABSL_ATTRIBUTE_NO_SANITIZE_ADDRESS
static void FunctionWhichCausesStackOverflow() {
  volatile int x = 0;
  if (++x) {  // prevent the compiler from proving infinite recursion
    FunctionWhichCausesStackOverflow();
  }
  ++x;  // prevent tail-call optimization
}

// Verify that a crash unwinds all the way to the test method.
TEST(StackTrace, FromFailureSignalHandlerOnMainThread) {
// TODO: Re-enable under TSAN once we can fix the test in OSS
#ifdef ABSL_HAVE_THREAD_SANITIZER
  GTEST_SKIP() << "Message from TSAN interferes with the test.";
#endif
  const char* regex =
      "anonymous namespace.*::StackTrace_FromFailureSignalHandlerOnMainThread";
  EXPECT_DEATH(Crash(), regex);
}

// Verify that a stack overflow shows the function involved.
TEST(StackTrace, FromStackOverflowOnMainThread) {
#ifdef ABSL_HAVE_THREAD_SANITIZER
  // Tsan doesn't give a nice stack trace on stack overflow =(
  const char* regex = "";
#else
  const char* regex = "FunctionWhichCausesStackOverflow";
#endif
  EXPECT_DEATH(FunctionWhichCausesStackOverflow(), regex);
}

struct MyThread : public Thread {
  void Run() override {
    std::string s("Tail-call blocker: test case needs this frame.");
    // Prevent the string from being optimized away by observing its data.
    const char* volatile vdata = s.data();
    CHECK(vdata) << "Unused variable blocker should never fail.";
    Crash();
  }
};

void CreateThreadAndCrash() {
  MyThread t;
  t.SetJoinable(true);
  t.Start();
  t.Join();
  LOG(FATAL) << "Unreachable code reached.";
}

// Verify that a crash unwinds all the way to the test method.
TEST(StackTrace, FromFailureSignalHandlerOnOtherThread) {
// TODO: Re-enable under TSAN once we can fix the test in OSS
#ifdef ABSL_HAVE_THREAD_SANITIZER
  GTEST_SKIP() << "Message from TSAN interferes with the test.";
#endif
  const char* regex = "anonymous namespace.*::MyThread::Run";
  EXPECT_DEATH(CreateThreadAndCrash(), regex);
}

struct OverFlowThread : public Thread {
  OverFlowThread(const thread::Options& options, absl::string_view name_prefix)
      : Thread(options, name_prefix) {}
  void Run() override { FunctionWhichCausesStackOverflow(); }
};

ABSL_ATTRIBUTE_NOINLINE ABSL_ATTRIBUTE_NO_TAIL_CALL static void TrapLeaf() {
// Clang defines a __has_builtin test macro, which we can use to test for
// __builtin_trap.  Otherwise, define __has_builtin ourselves so the
// preprocessor won't choke on it.
#if ABSL_HAVE_BUILTIN(__builtin_trap) || \
    (defined(__GNUC__) && !defined(__clang__))
  __builtin_trap();
#else
  raise(SIGILL);
#endif
}
ABSL_ATTRIBUTE_NOINLINE ABSL_ATTRIBUTE_NO_TAIL_CALL static void Trap2() {
  TrapLeaf();
}
ABSL_ATTRIBUTE_NOINLINE ABSL_ATTRIBUTE_NO_TAIL_CALL static void Trap1() {
  Trap2();
}

// re-enable once b/232749344 is fixed
TEST(StackTraceDeathTest, GetStackTraceWithContext_FramelessLeafFunction) {
  // If the compiler optimizes away the frame pointer in the leaf function (as
  // we expect under Clang), the stack trace will always include TrapLeaf (which
  // can be found using the saved instruction pointer) and Trap1 (which has a
  // proper stack frame).
  //
  // TODO: Check for a Trap2 frame here.
  EXPECT_DEATH(Trap1(), "TrapLeaf(.|\\n)*Trap1");
}

// Verify that protection key violations result in appropriate message.
TEST(StackTraceDeathTest, ProtectionKeyViolationDiagnosed) {
  int pkey = pkey_alloc(0, PKEY_DISABLE_WRITE);
  if (pkey == -1) GTEST_SKIP() << "Unable to allocate pkey";
  const int pagesize = getpagesize();
  void* mem = mmap(nullptr, pagesize, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ASSERT_TRUE(mem != MAP_FAILED);

  PCHECK(pkey_mprotect(mem, pagesize, PROT_READ, pkey) == 0);
  EXPECT_DEATH(
      { *(int*)mem = 42; },
      "SIGSEGV \\(@0x[[:xdigit:]]+\\), pkey=[[:digit:]]+, see "
      "<link>");

  munmap(mem, pagesize);
  pkey_free(pkey);
}
}  // namespace
