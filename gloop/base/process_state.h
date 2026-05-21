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

// Contains functions commonly used to measure and manipulate the state
// and characteristics of the process and the machine the process is running
// on. This includes some typical Unix characteristics (like hostname), and
// others that are more typical to Google servers (like if flags have been
// parsed yet).
//
// The functions in here are thread-safe unless specified otherwise,
// but they must be called after InitGoogle() (or the
// InitGoogleExceptChangeRootAndUser/ChangeRootAndUser pair).
//
// When adding functions in the future, please put them into the 'base'
// namespace. (Bottom of file)

#ifndef THIRD_PARTY_GLOOP_BASE_PROCESS_STATE_H_
#define THIRD_PARTY_GLOOP_BASE_PROCESS_STATE_H_

#include <csignal>  // IWYU pragma: keep
#include <cstdint>
#include <string>

#include "absl/flags/declare.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "gloop/base/config.h"  // For BASE_USE_SIGNAL_H

// Go into an infinite loop on SEGV and some other signals
ABSL_DECLARE_FLAG(bool, infinite_loop_on_signal);

// Max number of seconds we expect to take while writing the stack trace
ABSL_DECLARE_FLAG(int32_t, stacktrace_timeout);

// Do not log messages about stack traces or register state if a crash occurs
ABSL_DECLARE_FLAG(bool, suppress_failure_output);

// Print a message when signal handler processing is successfully completed (for
// testing)
ABSL_DECLARE_FLAG(bool, test_indicate_sighandler_done);

#if defined(GOOGLE_ENABLE_SIGNAL_HANDLERS)
// Exposed for InitGoogle.
void InstallSignalHandlers();
#endif  // defined(GOOGLE_ENABLE_SIGNAL_HANDLERS)

// Return kernel version string in /proc/version
const char* GetKernelVersionString();

// Return true and set kernel version (x.y.z), patch level and revision (#p.r),
// false, if not known.
// Typical version string:
//  Linux version 2.4.18-smp-175.13 (<email address>)
//  (gcc version 3.3.3 20040201 (prerelease)) #175.13 SMP
//  Thu Aug 25 12:57:08 PDT 2005
struct KernelVersion {
  int major;     // Major release
  int minor;     // Minor release
  int micro;     // Whatever the third no. is called ...
  int patch;     // Patch level
  int revision;  // Patch revision
};

bool GetKernelVersion(KernelVersion* kv);

namespace base {
namespace internal {

// KernelVersionInfo holds kernel version information read and parsed
// from /proc/info, if available.
struct KernelVersionInfo {
  std::string raw_version_string = kDefaultKernelVersionString;
  KernelVersion parsed_version = kDefaultKernelVersion;

  static const char kDefaultKernelVersionString[];
  static const KernelVersion kDefaultKernelVersion;
};

// Parses `raw_string` into `parsed_version`.
// Returns true on success.
bool ParseKernelVersionString(const std::string& raw_string,
                              KernelVersion* parsed_version);

// If `filename` is empty, returns a pointer to a default-constructed
// KernelVersionInfo, whose ownership is acquired by the caller.
// Else, reads the kernel version string from `filename` (storing it
// in the 'raw_version_string' member variable of `info` variable),
// then parses the string into the 'parsed_version' member variable,
// using the function ParseKernelVersionString.  If the parse fails,
// the 'parsed_version' member variable will be the default value.
// `filename` may not be null.
KernelVersionInfo* ReadAndParseKernelVersionString(const char* filename);

// If the KernelVersion stored in `info` is valid, copies it to
// *`output_version` and returns true. Else does not modify
// *`output_version` and returns false.
bool GetKernelVersionIfValid(const KernelVersionInfo& info,
                             KernelVersion* output_version);

const KernelVersionInfo& GetKernelVersionInfo();

}  // namespace internal
}  // namespace base

// A string saying when InitGoogle() was called -- probably program start
const char* GetStartTime();

// the pid for the startup thread, multithread-safe.
int32_t GetMainThreadPid();

// limit the amount of physical memory used by this process to a
// fraction of the available physical memory. The process is killed if
// it tries to go beyond this limit. If randomize is set, we reduce
// the fraction a little in a sort-of-random way. randomize is meant
// to be used for applications which run many copies -- by randomizing
// the limit, we can avoid having all copies of the application hit
// the limit (and die) at the same time.
void LimitPhysicalMemory(double fraction, bool randomize);

// Return the limit set on physical memory, zero if error or no limit set.
uint64_t GetPhysicalMemoryLimit();

// Note: The RunOnFailure() and CancelRunOnFailure() family of functions
// can behave unexpectedly, if a thread fails concurrently with these
// functions' execution. Either:
//
// - The callback may never be executed, if ExecuteFailureCallbacks() has
//   already begun in the failing thread.
// - All callbacks may fail to be executed by ExecuteFailureCallbacks(), if
//   the underlying callback-vector is currently being referenced by a
//   Run/Cancel function.
//
// This race is intentionally tolerated, to prevent the Failing thread from
// acquiring locks. In practice this means that Failure callbacks should be
// registered at process start and left alone - repeated addition/removal of
// Failure callbacks increases the chance that no callbacks are executed.

struct FailureContext {
  // The signal causing the failure.
  int signo;

#if defined(BASE_USE_SIGNAL_H)
  // The siginfo_t object describing the failure.
  siginfo_t* info;
#endif

  // A ucontext_t on POSIX systems, as per sigaction(2)
  void* context;

  // A user-provided opaque pointer (as specified by the call to RunOnFailure).
  void* args = nullptr;
};

using FailureFunction = void (*)(FailureContext);

// Add specified callback to the set of callbacks which are executed
// when the program dies a horrible death (signal, etc.)
//
// RunOnFailure() returns a positive ticket number which can be used by
// CancelRunOnFailure() to remove the callback.
//
// Unlike RunSignalSafeOnFailure, which requires async-signal-safe code,
// RunOnFailure is allowed to do unsafe things, like using LOG macros. However,
// because of this unsafe behavior, the process could be terminated at any time
// and there is no guarantee the callback will run.
//
// Note: These are not particularly efficient.  Use sparingly.
// Note: you can't just use atexit() because functions registered with
// atexit() are supposedly only called on normal program exit, and we
// want to do things like flush logs on failures.
int RunOnFailure(FailureFunction callback, void* args = nullptr);

// Remove callback with specified ticket number that was returned by
// RunOnFailure.
//
// The function returns false if the callback is currently being executed
// by the failure handler, in such case the caller must not delete the args
// passed into the callback.
bool CancelRunOnFailure(int ticket);

class ScopedRunOnFailure {
 public:
  ScopedRunOnFailure(
      FailureFunction callback, void* args = nullptr,
      absl::AnyInvocable<void(bool)> deletion_callback = [](bool success) {})
      : deletion_callback_(std::move(deletion_callback)),
        ticket_(RunOnFailure(callback, args)) {}
  ~ScopedRunOnFailure() { deletion_callback_(CancelRunOnFailure(ticket_)); }

  // Neither copyable nor movable.
  ScopedRunOnFailure(const ScopedRunOnFailure&) = delete;
  ScopedRunOnFailure& operator=(const ScopedRunOnFailure&) = delete;
  ScopedRunOnFailure(ScopedRunOnFailure&&) = delete;
  ScopedRunOnFailure& operator=(ScopedRunOnFailure&&) = delete;

 private:
  absl::AnyInvocable<void(bool)> deletion_callback_;
  const int ticket_;
};

// Call this ONLY with callbacks known not to allocate or free memory,
// use iostreams, LOG macros, or anything else that is unsafe in a
// signal handler. These callbacks will run before those registered
// with RunOnFailure(). These functions are not run in any
// guaranteed order and are assumed to be fast (sub 100ms)
// per execution so they should be used sparingly.
//
// RunSignalSafeOnFailure() returns a positive ticket number which can be used
// by CancelRunSignalSafeOnFailure() to remove the callback.
int RunSignalSafeOnFailure(FailureFunction callback, void* args = nullptr);

// Remove callback with specified ticket number that was set by
// RunSignalSafeOnFailure.
//
// The function returns false if the callback is currently being executed
// by the failure handler, in such case the caller must not delete the args
// passed into the callback.
bool CancelRunSignalSafeOnFailure(int ticket);

// Return whether alternate signal stacks should be set up.
// (True iff the flag has been set and any signal handler could be installed.)
bool UseAlternateSignalHandlerStack();

// Return the alternate signal stack size (in bytes) needed in order to
// safely run the failure signal handlers.  The returned value will
// always be a multiple of the system page size.
int32_t GetRequiredAlternateSignalStackSize();

// Attempt to invoke debugger.
// The invoker_name should be the name of the function which is calling
// InvokeDebugger. Since that name is (potentially) passed to the shell,
// it is best to avoid any shell metacharacters (including parenths) in it.
// See <link>
extern "C" void InvokeDebugger(const char* invoker_name);

// Invoke debugger with given command. See InvokeDebugger description above.
void InvokeDebuggerWithCommand(const char* invoker_name, const char* command);

namespace crash_analysis {
namespace reporting {
namespace remote_coredumper {
class SetUpHook;
}  // namespace remote_coredumper
}  // namespace reporting
}  // namespace crash_analysis

namespace car {
class CoreDumpCaptureValidator;
}  // namespace car

namespace base {

#if defined(BASE_USE_SIGNAL_H)
// If the handler for the given signal is enabled (by the configuration of
// FLAGS_install_signal_handlers and FLAGS_install_named_signal_handlers), write
// tracebacks and then either execute failure callbacks and terminate the
// process or enter an infinite loop for debugging (depending on the value of
// FLAGS_infinite_loop_on_signal).
//
// Otherwise, set the handler to SIG_DFL, re-raise the signal, and return.
//
// This function is async-signal-safe and safe to call during library
// initialization (including before InitGoogle).
//
// This implements the default signal-handling behavior of this library. Its
// intended use is to allow programs to safely install handlers before
// InitGoogle (for example, to avoid conflicts with language runtimes that may
// convert an otherwise-fatal signal into an exception or panic).
void HandleOrRaiseFailureSignal(int signo, siginfo_t* info, void* context);

class CrashData {
 public:
  CrashData(int signal, siginfo_t* si, void* context, int cpu)
      : si_(si), context_(context), signal_(signal), cpu_(cpu) {}

 private:
  friend class ::crash_analysis::reporting::remote_coredumper::SetUpHook;
  friend class ::car::CoreDumpCaptureValidator;

  // Adds a single callback to run during a program crash for
  // the purpose of gathering crash data beyond the default
  // behavior of printing into the log files. The provided CrashData will
  // contain all relevant data necessary for gathering data.
  //
  // This callback runs before unsafe mode begins.
  //
  // NOTE: This callback must be signal safe and not allocate or free memory,
  // use iostreams, LOG macros, or anything else that is unsafe in a signal
  // handler.
  static void SetCallback(void (*callback)(const CrashData*));

  siginfo_t* si_;
  void* context_;  // Avoid ucontext_t* for benefit of non-POSIX systems
  int signal_;
  // The CPU on which the signal handler was running, or a negative number if
  // there was an error finding the information; note that this may or may
  // not be the CPU on which the thread was running when the signal was received
  // depending on rescheduling effects.
  int cpu_;
};
#endif  // defined(BASE_USE_SIGNAL_H)

namespace process_state {

// The following interface allows processes to send notifications about
// important events in their execution to interested parties such as profilers
// or execution monitors.  Developers can add these notifications to their code
// if they wish to cooperate with such tools in order to allow time-based
// profiling, e.g., pre- and post-initialization. These functions are safe to
// call even if the application is not currently being monitored.
//
// Notification messages are written to LOG(INFO) and may include as data an
// optional null-terminated char* string that is also written to the same log
// line. The format of these log messages is:
//
// Process state change notification: <notification_type> [<notification_data>]
//
// To receive notifications, monitors can either intercept the notification
// function (e.g., using a breakpoint), or inspect the log stream for messages
// of this format. Other notification mechanisms may be implemented in the
// future, for example based on network messages or system calls.
//
// No notifications should be sent before InitGoogle is done, since the logging
// subsystem is set up within InitGoogle.

// The different types of notification messages, and the corresponding data sent
// for each type (i.e., the second argument to ReportProcessStateChange).
// Developers wishing to add new notification messages should document them
// here.
struct NotificationMessage;

// Notify that InitGoogle() has completed.  This notification is issued
// automatically by base/init_google.cc.
// notification_data: none
extern const NotificationMessage* const kInitGoogleDone;

// Notify that the process has completed all initialization tasks. Used to
// distinguish between initialization and "steady state" execution, for example
// in long-running processes.
// notification_data: none
extern const NotificationMessage* const kAppInitDone;

// Notify that the process will begin shutting down.  Used to detect the start
// of orderly process shutdown, as well as the start of abnormal shutdowns and
// failures where the process still has a chance to execute code.
// notification_data: none
extern const NotificationMessage* const kShutdownBegin;

// Test-related notifications.  These are issued automatically by testing
// frameworks such as GUnit and should not be used by code outside those those
// testing libraries and frameworks.

// Notifies that a test case is starting.  Used to detect the start of
// initialization code that may be executed once, and only once, before any test
// cases or tests are performed.
// notification_data: test case name
extern const NotificationMessage* const kTestSuiteStart;

// Notifies that a test case has ended.  Used to detect the end of cleanup code
// that may be executed after all tests have been performed.
// notification_data: test case name
extern const NotificationMessage* const kTestSuiteEnd;

// Notifies that a test (within a test case) is starting.
// notification_data: test name
extern const NotificationMessage* const kTestStart;

// Notifies that a test (within a test case) has ended.
// notification_data: test name
extern const NotificationMessage* const kTestEnd;

// Public interface for process state change notification.  Writes notifications
// of changes in process state to LOG(INFO).  The notification message must be
// one of the message types declared above.  The caller can optionally include
// data about the notification.  If notification_data is null, no information
// is included with the notification.  Returns false if notification_message is
// null, otherwise writes to the log and returns true.
bool ReportChange(const NotificationMessage* const notification_message,
                  const char* notification_data);

}  // namespace process_state
}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_PROCESS_STATE_H_
