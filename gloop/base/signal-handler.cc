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

#include "gloop/base/signal-handler.h"

#include <stdio.h>

#include <atomic>
#include <csignal>
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/flags/flag.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gloop/base/config.h"  // For GOOGLE_HAVE_POSIX_SIGNAL_ALT_STACK
#include "gloop/base/sysinfo.h"

// Controls the default of the --install_signal_handlers flag.
#ifdef BASE_CONFIG_DEFAULT_INSTALL_SIGNAL_HANDLERS
#error BASE_CONFIG_DEFAULT_INSTALL_SIGNAL_HANDLERS cannot be directly set
#elif defined(__linux__) && !defined(__ANDROID__)
// Our signal handlers only work well on linux, and android does not use them.
#define BASE_CONFIG_DEFAULT_INSTALL_SIGNAL_HANDLERS true
#else
#define BASE_CONFIG_DEFAULT_INSTALL_SIGNAL_HANDLERS false
#endif

ABSL_FLAG(bool, install_signal_handlers,
          BASE_CONFIG_DEFAULT_INSTALL_SIGNAL_HANDLERS,
          "Should we install signal handlers?");

ABSL_FLAG(std::string, install_named_signal_handlers,
          "failure,profiling,stackdump",
          "comma-separated list of signal numbers/groups to install if "
          "install_signal_handlers==true.  Items can be negated with a "
          "leading hyphen. The last match applies. e.g. 'failure,-11' means "
          "all the failure signal handlers except signal 11.  You may use "
          "-hup and/or -pipe to disable the default behavior of ignoring "
          "SIGHUP and SIGPIPE, whether or not the install_signal_handlers "
          "flag is set.  Any existing handlers registered before InitGoogle "
          "will not be replaced. The \"profiling\" handler can be disabled "
          "even before InitGoogle by setting the environment variable "
          "CPUPROFILE_FREQUENCY=0.");

ABSL_FLAG(bool, use_alternate_stack_for_signal_handlers, true,
          "Run signal handlers on a separate stack.");

// Normally, FailureSignalHandler() (which handles SEGV, etc.) sets an
// alarm to make sure it doesn't get into an infinite loop.  This should
// normally be enabled, but you might want to disable it for debugging.
ABSL_FLAG(bool, alarm_on_failure, true,
          "Set an alarm in the crash handler to avoid infinite loops");

#if defined(GOOGLE_ENABLE_SIGNAL_HANDLERS)
namespace base {
namespace internal {

static bool SigactionHasHandler(const struct sigaction& sa) {
  if ((sa.sa_flags & SA_SIGINFO) == 0) {
    return sa.sa_handler != SIG_DFL && sa.sa_handler != SIG_IGN;
  }

  // SIG_DFL and SIG_IGN aren't valid sigactions, but are sometimes passed as
  // them anyway
  // (http://<path>?l=202&rcl=84481437).
  // Check for them explicitly by comparing as void*.
  auto sigaction = reinterpret_cast<void*>(sa.sa_sigaction);
  return sigaction != SIG_DFL && sigaction != SIG_IGN;
}

static bool SignalHasExistingHandler(int signo) {
  struct sigaction sa = {};
  if (sigaction(signo, nullptr, &sa) != 0) {
    return false;  // Not a valid signal.
  }
  return SigactionHasHandler(sa);
}

static bool DefaultSignalHandlerFlagEnabled(absl::string_view signal_group,
                                            int signal) {
  // For backward compatibility, default for SIGHUP/SIGPIPE is to
  // install, default for rest is to not install.
  const bool special = (signal_group == "hup" || signal_group == "pipe");
  bool result = special;
  if (special || absl::GetFlag(FLAGS_install_signal_handlers)) {
    char signal_buf[8];
    snprintf(signal_buf, sizeof signal_buf, "%d", signal);
    std::string flag = absl::GetFlag(FLAGS_install_named_signal_handlers);
    // Iterate over each comma-separated piece of flag.
    std::string::size_type end;
    for (std::string::size_type start = 0; start < flag.size();
         start = end + 1) {
      for (end = start; end != flag.size() && flag[end] != ','; end++) {
      }
      bool positive = true;
      if (start < end && flag[start] == '-') {
        positive = false;
        start++;
      }
      std::string part = flag.substr(start, end - start);
      if (part == signal_group || part == signal_buf) {
        result = positive;
      }
    }
  }
  return result;
}

// A table indicating which signals are enabled according to the
// --install_named_signal_handlers flag.
//
// After parsing flags, we set handle_failure_signal to true for the failure
// signals to be handled. HandleFailureSignal uses this table to decide whether
// to invoke a handler function or reset to SIG_DFL and re-raise the signal.
std::atomic<bool> handle_failure_signal[NSIG] = {};

static void InstallOneSignalHandler(int sig, struct sigaction* sa) {
  if (!DefaultSignalHandlerFlagEnabled("failure", sig)) {
    return;
  }
  // Use memory_order_release here to ensure that writes to parsed flag values
  // occur before HandlerOrRaiseFailureSignal attempts to read those flags.
  handle_failure_signal[sig].store(true, std::memory_order_release);

  // Our handler terminates the process, so if there is some existing handler
  // that may actually handle the signal (e.g. by raising an exception or
  // starting a recoverable panic) we need to leave it in place.
  if (SignalHasExistingHandler(sig)) {
    return;
  }

  if (UseAlternateStackForSignal(sig)) {
    sa->sa_flags |= SA_ONSTACK;
  } else {
    sa->sa_flags &= ~SA_ONSTACK;
  }

  // We shouldn't have to explicitly zero out old_sa here, but ASAN
  // sometimes returns 0 without filling it in.
  // (See http://b/34773319#comment42.)
  struct sigaction old_sa = {};
  if (sigaction(sig, sa, &old_sa) != 0) {
    ABSL_INTERNAL_LOG(
        FATAL, absl::StrCat("Could not set ", strsignal(sig), " handler"));
  }
  if (SigactionHasHandler(old_sa)) {
    ABSL_INTERNAL_LOG(
        FATAL,
        absl::StrCat("Registration of existing handler for ", strsignal(sig),
                     " races with InstallOneSignalHandler"));
  }
}

// Install signal handlers that crash (handler_loops==false)
// or loop (handler_loops==true).
void InstallSignalHandler(bool handler_loops,
                          void (*handler)(int, siginfo_t*, void*)) {
  struct sigaction sa = {};

  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);

#ifndef __myriad2__
  if (!handler_loops) {
    sa.sa_flags |= SA_NODEFER;  // Don't ignore recursive signals
  }
#endif  // __myriad2__

  sa.sa_flags |= SA_SIGINFO;
  sa.sa_sigaction = handler;

  // //gloop/base/<link>.cc registers handlers for these signals in an
  // early constructor. Please keep these files synchronized.
  //
  static const int failure_signals[] = {
      SIGSEGV, SIGILL, SIGFPE, SIGABRT, SIGBUS, SIGTERM, SIGTRAP,
  };

  for (int sig : failure_signals) {
    InstallOneSignalHandler(sig, &sa);
  }
}

}  // namespace internal
}  // namespace base
#endif  // defined(GOOGLE_ENABLE_SIGNAL_HANDLERS)

bool ShouldInstallDefaultSignalHandler(absl::string_view signal_group,
                                       int signal) {
#if defined(GOOGLE_ENABLE_SIGNAL_HANDLERS)
  return base::internal::DefaultSignalHandlerFlagEnabled(signal_group,
                                                         signal) &&
         !base::internal::SignalHasExistingHandler(signal);
#else
  return false;
#endif
}

// x_cgo_init is linked into the process if and only if it includes a Go runtime
// (i.e. a go_library dependency, or a cc_library dependency of a go_binary).
extern "C" ABSL_ATTRIBUTE_WEAK char x_cgo_init[];

bool UseAlternateStackForSignal(int signal) {
#ifndef GOOGLE_HAVE_POSIX_SIGNAL_ALT_STACK
  return false;
#else              // GOOGLE_HAVE_POSIX_SIGNAL_ALT_STACK
#ifndef __APPLE__  // Something is wrong with weak symbols on OSX.
  // Always use an alternate stack if Go code is linked into the binary.  We'd
  // ideally like to do this always (even without Go) but are concerned about
  // the memory footprint.  See discussion on CL 112820896.
  if (x_cgo_init != nullptr) return true;
#endif

  // TODO: If the flag is false, does it actually do any harm to set
  // SA_ONSTACK for these signals anyway?  (Is this check actually needed?)
  if (!absl::GetFlag(FLAGS_use_alternate_stack_for_signal_handlers)) {
    return false;
  }

  // In programs that do not link against Go, use an alternate stack only for
  // "failure" signals (for which the odds are higher that the regular stack is
  // full, corrupted, or otherwise unusable).
  switch (signal) {
    case SIGSEGV:
    case SIGILL:
    case SIGFPE:
    case SIGABRT:
    case SIGBUS:
    case SIGTERM:
    case SIGTRAP:
      return true;
    default:
      return false;
  }
#endif  // GOOGLE_HAVE_POSIX_SIGNAL_ALT_STACK
}

namespace base {
namespace internal {

std::atomic<pid_t> failed_tid{-1};

}  // namespace internal
}  // namespace base

bool InFailureSignalHandler() {
  // When a thread enters the signal handler, it sets failed_tid to its own
  // thread id.
  return base::internal::failed_tid.load(std::memory_order_relaxed) == GetTID();
}

bool IsFailureSignalHandlerRunning() {
  return base::internal::failed_tid.load(std::memory_order_relaxed) != -1;
}

namespace base {
// This variable is set to 1 at the start of exit() processing and at the
// start of signal handlers that are intended to kill the process.  It is never
// set back to 0.  The pthread manager watcher thread in thread.cc kills
// the process if it is still alive some threshold after the flag becomes 1.
// This is to kill the process in spite of deadlocks in
// exit()/abort()/signal handling.
static std::atomic<bool> process_is_dying{false};

// This is the preferred way to read the process_is_dying variable.
bool ProcessIsDying() {
  return process_is_dying.load(std::memory_order_acquire);
}

// This is the preferred way to turn on the process_is_dying variable.
void SignalThatProcessIsDying() {
  return process_is_dying.store(true, std::memory_order_release);
}

}  // namespace base
