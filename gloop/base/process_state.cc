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

#include "gloop/base/process_state.h"

#if defined(BASE_PROCESS_STATE_USE_SYS_RESOURCE_H)
#error "BASE_PROCESS_STATE_USE_SYS_RESOURCE_H must not be set externally"
#elif !defined(__Fuchsia__)
#define BASE_PROCESS_STATE_USE_SYS_RESOURCE_H
#endif  // !defined(__Fuchsia__)

#include <errno.h>
#include <inttypes.h>  // for PRIxPTR
#include <limits.h>    // for INT_MAX
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#if defined(BASE_PROCESS_STATE_USE_SYS_RESOURCE_H)
#include <sys/resource.h>
#endif  // defined(BASE_PROCESS_STATE_USE_SYS_RESOURCE_H)

#include <algorithm>
#include <atomic>
#include <iterator>
#include <limits>
#include <string>
#include <tuple>
#include <vector>

// You're not allowed to use anything outside of base/ or absl/ here (otherwise
// the library you use might as well be part of base!).  So, no strutil
// or other handy things -- you get to do that sort of thing "by hand".

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/dynamic_annotations.h"
#include "absl/base/log_severity.h"
#include "absl/base/thread_annotations.h"
#include "absl/debugging/internal/examine_stack.h"
#include "absl/debugging/leak_check.h"
#include "absl/debugging/stacktrace.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/log/internal/globals.h"
#include "absl/log/log.h"
#include "absl/log/log_streamer.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/source_location.h"
#include "gloop/base/address_is_readable.h"
#include "gloop/base/config.h"
#include "gloop/base/coredump_flags.h"
#include "gloop/base/examine_stack.h"
#include "gloop/base/init_google_flags.h"
#include "gloop/base/logging_extensions.h"
#include "gloop/base/on_fatal_log_message.h"
#include "gloop/base/port.h"
#include "gloop/base/raw_logging.h"
#include "gloop/base/sysinfo.h"
#include "tcmalloc/malloc_extension.h"

#if GOOGLE_ENABLE_SIGNAL_HANDLERS
#include "gloop/base/signal-handler.h"
#endif  // GOOGLE_ENABLE_SIGNAL_HANDLERS

#if !PORTABLE_BASE
#include <sys/ucontext.h>  // ucontext_t

#endif  // PORTABLE_BASE

ABSL_FLAG(bool, infinite_loop_on_signal, false,
          "Go into an infinite loop on SEGV and some other signals");
ABSL_FLAG(bool, suppress_failure_output, false,
          "Do not log messages about stack "
          "traces or register state if a crash occurs.");

ABSL_RETIRED_FLAG(std::string, debugger_command, "", "ignored");

// When alarm_on_failure is set, stacktrace_timeout in this file controls the
// duration of the alarm(). The core dumper libraries (including coreutil and
// remote_coredumper) also manipulate the alarm.

// It can take a long time to dump stacks for 1000s of threads.
ABSL_FLAG(int32_t, stacktrace_timeout, 300,
          "Max number of seconds we expect to take while writing the stack "
          "trace");

ABSL_FLAG(bool, test_indicate_sighandler_done, false,
          "Print a message when signal handler processing is "
          "successfully completed (for testing)");

#ifndef WNOHANG
// Without WNOHANG, you'll have to interrupt waitpid in OS-specific manner
// if you want to continue past waitpid in InvokeDebugger().
#define WNOHANG 0
#endif

int32_t GetMainThreadPid() { return getpid(); }

// Set the status message. It is only set in the
// case of a crash, not a CHECK or assertion failure.
static void SetKillSignalStatusMessage(int signo) {
  // Don't overwrite a status message that could have
  // been set by LOG(FATAL) or equivalent.
  if (signo == SIGABRT) return;

  char signal_exit_message[64];
  absl::SNPrintF(signal_exit_message, sizeof(signal_exit_message),
                 "Killed by signal %d!", signo);
}

/***** Callbacks to run on failure *****/

ABSL_CONST_INIT static absl::Mutex failure_mutex(absl::kConstInit);
using FailureFunctionVector =
    std::vector<std::tuple<FailureFunction, void*, int>>;
ABSL_CONST_INIT static std::atomic<FailureFunctionVector*> failure_functions{
    nullptr};
ABSL_CONST_INIT static std::atomic<FailureFunctionVector*>
    safe_failure_functions{nullptr};

#if defined(BASE_USE_SIGNAL_H)
ABSL_CONST_INIT static std::atomic<void (*)(const base::CrashData*)>
    crash_data_callback{nullptr};
#endif  // defined(BASE_USE_SIGNAL_H)

static int ticket_number ABSL_GUARDED_BY(failure_mutex) = 0;

enum IsSafeType { kUnsafe, kSafe };

static int RunOnFailureInternal(FailureFunction callback, void* args,
                                IsSafeType safe_mode) {
  // All modifications to the failure-handler sets are guarded by mutex, to
  // prevent write-races across multiple threads. This mutex is NOT acquired
  // when these callbacks are executed in the event of a failure (see
  // ExecuteFailureCallbacks() below). This technically presents a race
  // condition between one thread Adding/Canceling a callback while a separate
  // thread Fails; the atomic-exchange operations ensure that an
  // Adding/Canceling thread does not modify a callback vector while a Failing
  // thread is in the process of iterating over its contents. See comments in
  // process_state.h for the potential effects of this race.
  absl::MutexLock l(failure_mutex);
  std::atomic<FailureFunctionVector*>* list_ptr =
      safe_mode == kSafe ? &safe_failure_functions : &failure_functions;
  FailureFunctionVector* list =
      list_ptr->exchange(nullptr, std::memory_order_relaxed);
  if (list == nullptr) {
    // Ignore the leaks of failure_functions allocation and extensions;
    // we need it to live in case we crash.
    list = absl::IgnoreLeak(new FailureFunctionVector);
  }
  int callback_ticket = ++ticket_number;
  list->push_back(std::make_tuple(callback, args, callback_ticket));
  list_ptr->store(list, std::memory_order_release);
  return callback_ticket;
}

static bool CancelRunOnFailureInternal(int ticket, IsSafeType safe_mode) {
  absl::MutexLock l(failure_mutex);
  std::atomic<FailureFunctionVector*>* list_ptr =
      safe_mode == kSafe ? &safe_failure_functions : &failure_functions;
  FailureFunctionVector* list =
      list_ptr->exchange(nullptr, std::memory_order_relaxed);
  bool res = false;
  if (list) {
    FailureFunctionVector::iterator pos = std::remove_if(
        list->begin(), list->end(),
        [ticket](const std::tuple<FailureFunction, void*, int>& x) {
          return std::get<2>(x) == ticket;
        });
    if (pos != list->end()) {
      list->erase(pos, list->end());
      res = true;
    }
  }
  list_ptr->store(list, std::memory_order_release);
  return res;
}

int RunOnFailure(FailureFunction callback, void* args) {
  return RunOnFailureInternal(callback, args, kUnsafe);
}

int RunSignalSafeOnFailure(FailureFunction callback, void* args) {
  return RunOnFailureInternal(callback, args, kSafe);
}

bool CancelRunOnFailure(int ticket) {
  return CancelRunOnFailureInternal(ticket, kUnsafe);
}

bool CancelRunSignalSafeOnFailure(int ticket) {
  return CancelRunOnFailureInternal(ticket, kSafe);
}

namespace base {
namespace internal {

#if defined(BASE_PROCESS_STATE_USE_SYS_RESOURCE_H)
// Execute callbacks registered with RunOnFailure functions. This
// function is called from inside signal handler, possibly multiple
// times.
// If safe_mode is kSafe, execute the safe callbacks.  Otherwise execute
// the unsafe callbacks.
void ExecuteFailureCallbacks(int signo, siginfo_t* info, void* uc,
                             IsSafeType safe_mode) {
  // Call failure callbacks: we could try to grab the failure_mutex
  // here, but it seems that we will be less likely to hang if we
  // just live with the race condition.
  FailureFunctionVector* local_failure_functions;
  FailureContext fc;
  fc.signo = signo;
#if defined(BASE_USE_SIGNAL_H)
  fc.info = info;
#endif  // defined(BASE_USE_SIGNAL_H)
  fc.context = uc;
  if (safe_mode == kSafe) {
    ABSL_RAW_LOG(INFO, "ExecuteFailureCallbacks() safe");
    local_failure_functions =
        safe_failure_functions.exchange(nullptr, std::memory_order_acquire);
  } else {
    ABSL_RAW_LOG(INFO, "ExecuteFailureCallbacks() unsafe");
    local_failure_functions =
        failure_functions.exchange(nullptr, std::memory_order_acquire);
  }
  if (local_failure_functions != nullptr) {
    for (std::tuple<FailureFunction, void*, int> p : *local_failure_functions) {
      fc.args = std::get<1>(p);
      (*std::get<0>(p))(fc);
    }
  }
}
#endif  // defined(BASE_PROCESS_STATE_USE_SYS_RESOURCE_H)

#if defined(BASE_USE_SIGNAL_H)
// Execute crash data callback (if registered). This function is called
// from inside a signal handler.
void ExecuteCrashDataCallback(int signo, siginfo_t* si, void* uc, int cpu) {
  base::CrashData cd(signo, si, uc, cpu);
  void (*cb)(const base::CrashData*) =
      crash_data_callback.exchange(nullptr, std::memory_order_acquire);
  if (cb != nullptr) {
    cb(&cd);
  }
}
#endif  // defined(BASE_USE_SIGNAL_H)

}  // namespace internal
}  // namespace base

// InvokeDebugger helper routine: expand '%p', '%f' etc. tokens.
// Returns false in case of difficulties (e.g. not enough space in the
// buffer).
static bool ExpandTokens(char* cmd_buffer, size_t buffer_size, const char* pid,
                         const char* invoker) {
  for (char* p = cmd_buffer; *p; ++p) {
    if (*p == '%') {
      switch (p[1]) {
        case '%': {  // '%%' => '%'
          memmove(p, p + 1, strlen(p + 1) + 1);
          break;
        }
        case 'w':    // '%w' => <invoker>
        case 'p': {  // '%p' => <pid>
          const char* arg = (p[1] == 'w') ? invoker : pid;
          size_t arg_len = strlen(arg);
          size_t tail_len = strlen(p + 2);
          if ((p - cmd_buffer) + arg_len + tail_len >= buffer_size) {
            return false;
          }
          memmove(p + arg_len, p + 2, tail_len + 1);
          memcpy(p, arg, arg_len);
          p += arg_len - 1;
          break;
        }
        case 'f': {  // '%f' => "/proc/<pid>/exe"
          // TODO: is there any advantage to expanding
          // "/proc/<pid>/exe" into real executable name via readlink?
          size_t pid_len = strlen(pid);
          size_t arg_len = pid_len + 10;
          size_t tail_len = strlen(p + 2);
          if ((p - cmd_buffer) + arg_len + tail_len >= buffer_size) {
            return false;
          }
          memmove(p + arg_len, p + 2, tail_len + 1);
          memcpy(p, "/proc/", 6);
          memcpy(p + 6, pid, pid_len);
          memcpy(p + 6 + pid_len, "/exe", 4);
          p += arg_len - 1;
          break;
        }
        case '\0': {
          fprintf(stderr, "Unexpected trailing '%%'\n");
          return false;
        }
        default: {
          fprintf(stderr, "Unknown token '%%%c'\n", p[1]);
          return false;
        }
      }
    }
  }
  return true;
}

static const char kFailureSignalHandlerName[] = "FailureSignalHandler";

// Invoke a debugger.
extern "C" void InvokeDebugger(const char* invoker_name) {
  CHECK(invoker_name != nullptr);
  char* dbg = getenv("GOOGLE_DEBUG_ON_FAILURE");
  if ((dbg == nullptr) || (dbg[0] == '\0')) {
    // The usual case. No need to be verbose here.
    return;
  }
  if (getuid() != geteuid() || getgid() != getegid()) {
    ABSL_RAW_LOG(INFO, "Running setuid or setgid binary.");
    return;
  }
  if (!isatty(STDIN_FILENO)) {
    // Not an interactive session. We don't want to accept arbitrary debugger
    // commands from STDIN -- who knows where they are coming from.
    ABSL_RAW_LOG(INFO, "STDIN is not a TTY");
    return;
  }
  if (invoker_name != kFailureSignalHandlerName) {
    // GOOGLE_DEBUG_ON_FAILURE is not intended to invoke debugger except
    // on failure.
    ABSL_RAW_LOG(ERROR, "Unexpected invoker");
    return;
  }
  char cmd[1024] = "\0";
  absl::SNPrintF(cmd, sizeof(cmd), "%s -p %%p -- %%f", dbg);
  InvokeDebuggerWithCommand(invoker_name, cmd);
}

// The process will be stopped until a debugger is attached, unless thedebugger
// command contains the substring "INVOKE_DEBUGGER_WAIT_FOR_ATTACH=0".
void InvokeDebuggerWithCommand(const char* invoker_name,
                               const char* debugger_command) {
#ifndef GOOGLE_HAVE_FORK
  LOG(ERROR) << __func__ << " called but not supported (fork not available).";
  return;
#else   // GOOGLE_HAVE_FORK
  ABSL_RAW_LOG(INFO, "Starting debugger with command: %s", debugger_command);
  int pid = getpid();
  int child = fork();
  if (child == -1) {
    perror("fork (debugger)");
    return;
  }
  if (child == 0) {
    const char* shell = "/bin/sh";
    char expanded_debugger_command[1024];
    char pid_buf[10];
    absl::SNPrintF(pid_buf, sizeof(pid_buf), "%d", pid);
    absl::SNPrintF(expanded_debugger_command, sizeof(expanded_debugger_command),
                   "%s", debugger_command);
    if (ExpandTokens(expanded_debugger_command,
                     sizeof(expanded_debugger_command), pid_buf,
                     invoker_name)) {
      ABSL_RAW_LOG(INFO, "Invoking %s -c '%s'", shell,
                   expanded_debugger_command);
      execl(shell, shell, "-c", expanded_debugger_command, nullptr);
      perror("exec (debugger)");
    }
    exit(1);
  }
  if (strstr(debugger_command, "INVOKE_DEBUGGER_WAIT_FOR_ATTACH=0") !=
      nullptr) {
    ABSL_RAW_LOG(INFO, "Not waiting for debugger to attach");
    return;
  }
  if (!isatty(STDIN_FILENO)) {
    // Suspend ourselves. This allows GDB to take its time loading symbols,
    // etc, and doesn't allow other threads in this process to keep going
    // (and possibly crash) before GDB had a chance to attach them.
    // We only do this when STDIN is not a terminal, because if it is,
    // the SIGSTOP may be interpreted by the interactive shell and may
    // interfere with its job control logic (e.g. unintentionally putting
    // current process into the background).
    kill(pid, SIGSTOP);
  }
  volatile bool done = false;  // May be set to true from debugger.
  do {
    int child_exit_status;
    // We don't wait for debugger to exit (with waitpid(..., 0))
    // because the debugger may well want us to return from this routine
    // and to continue regular execution. Debugger would have to set
    // done = false for that to happen. On Linux, IsDebuggerAttached()
    // will do that automagically.
    int child_id = waitpid(child, &child_exit_status, WNOHANG);
    VLOG(1) << "child_id " << child_id << ", exit_status " << child_exit_status;
    if (child_id != 0) {
      // Only pause for re-attach if the debugger exited with error.
      // If the debugger quit normally, the delay is only an annoyance.
      if (child_exit_status != 0) {
        const int kReattachDelay = 60;  // 1 minute.
        fprintf(stderr,
                "Debugger (pid=%d) exited with status=%d\n"
                "You have %d seconds to re-attach to pid=%d\n",
                child_id, child_exit_status, kReattachDelay, pid);
        // Our debugger has exited, or couldn't be invoked.
        // Wait kReatachDelay seconds to give someone a chance to re-attach.
        poll(nullptr, 0, kReattachDelay * 1000);
      }
      break;
    }
    // While the debugger exists, and have not told us to return (by
    // setting done = true), continue sleeping.
    const int kRetryDelayInMs = 100;  // 100 ms between retries.
    // poll() is allegedly more debuggable than sleep:
    // for sleep() call gdb sometimes can't climb out of the stack.
    poll(nullptr, 0, kRetryDelayInMs);  // sleep(kRetryDelay ms);
    done = (done || IsDebuggerAttached());
    VLOG(1) << "done " << done;
  } while (!done);
  ABSL_RAW_LOG(INFO, "Debugger attached");
  // Continue regularly scheduled programming.
  // If we came here from FailureSignalHandler, death will follow shortly.
#endif  // GOOGLE_HAVE_FORK
}

// Default stack dumping routine from signal handler.
// uc is a ucontext_t *.  We use void* to avoid the use
// of ucontext_t on non-POSIX systems.
static void DefaultStackDumper(void* uc) {
  // FailureSignalHandler has already dumped the relevant stack to
  // both stderr and the logs, so there's not much to do here.

  // &LOG() fails to compile under NDK 9 and Exoblaze.
#if !defined(__ANDROID__) && !defined(__APPLE__)
  // FailureSignalHandler didn't write the address map to stderr;
  if (!absl::GetFlag(FLAGS_skip_address_map)) {
    DumpAddressMap(
        base::DebugWriteToStream,
        &absl::LogInfoStreamer(::absl::SourceLocation::current()).stream());
  }
#endif
}

// Pointer to thread-stack dumping routine.  The pointer starts out
// pointing to DefaultStackDumper, but may be changed by the threading
// code when a new thread is created.
void (*thread_stack_dumper)(void* uc) = &DefaultStackDumper;

static absl::string_view SigName(int signo) {
  switch (signo) {
    case SIGSEGV:
      return "SIGSEGV";
    case SIGILL:
      return "SIGILL";
    case SIGFPE:
      return "SIGFPE";
    case SIGABRT:
      return "SIGABRT";
    case SIGBUS:
      return "SIGBUS";
    case SIGTERM:
      return "SIGTERM";
    case SIGTRAP:
      return "SIGTRAP";
    default:
      return strsignal(signo);
  }
}

#if !PORTABLE_BASE
#if defined(__x86_64__)
constexpr int kRedZoneSize = 128;
#elif defined(__i386__)
constexpr int kRedZoneSize = 0;
#elif defined(__arm__)
constexpr int kRedZoneSize = 0;
#elif defined(__aarch64__)
constexpr int kRedZoneSize = 0;
#elif defined(__riscv)
constexpr int kRedZoneSize = 0;
#else
#error Implement me.
#endif

static uintptr_t StackPointerFromUcontext(const void* uc) {
  const ucontext_t* const ucp = reinterpret_cast<const ucontext_t*>(uc);
#if defined(__x86_64__)
  return ucp->uc_mcontext.gregs[REG_RSP];
#elif defined(__i386__)
  return ucp->uc_mcontext.gregs[REG_ESP];
#elif defined(__arm__)
  return ucp->uc_mcontext.arm_sp;
#elif defined(__aarch64__)
  return ucp->uc_mcontext.sp;
#elif defined(__riscv)
  return ucp->uc_mcontext.__gregs[REG_SP];
#else
#error Implement me.
#endif
}
#endif  // PORTABLE_BASE

static void FormatSignalMessage(char* buf, int bufsize, int signo,
                                const void* uc, siginfo_t* si, bool dump_trace,
                                int cpu) {
  char on_cpu[32] = {0};
  char signal_desc[128] = {0};
  char signal_sender[32] = {0};

  if (cpu != -1) {
    absl::SNPrintF(on_cpu, sizeof(on_cpu), " on cpu %d", cpu);
  }
  absl::SNPrintF(signal_desc, sizeof(signal_desc), "%s", SigName(signo));
  int len = strlen(signal_desc);
  // We trust (here and elsewhere) absl::SNPrintF to be safe.
  if (si) {
    // SIGILL, SIGFPE, SIGSEGV, SIGBUS, SIGPOLL don't have si_pid.
    // See /usr/include/bits/siginfo.h.
    switch (signo) {
      case SIGSEGV:
        if (si->si_addr == nullptr && si->si_code != SEGV_MAPERR) {
          // This very likely means that we've hit a general protection fault,
          // and not a page fault. See b/27134779 for details.
          absl::SNPrintF(signal_desc + len, sizeof(signal_desc) - len,
                         ", si_code=%d, see <link>", si->si_code);
          break;
        }
#if defined(SEGV_PKUERR)
        if (si->si_code == SEGV_PKUERR) {
          absl::SNPrintF(signal_desc + len, sizeof(signal_desc) - len,
                         " (@%p), pkey=%d, see <link>", si->si_addr,
                         si->si_pkey);
          break;
        }
#endif  // SEGV_PKUERR
#if !PORTABLE_BASE
#endif

        [[fallthrough]];
      case SIGILL:
#if defined(__x86_64__)
        if (signo == SIGILL && si->si_code == ILL_ILLOPN) {
          const unsigned char* pc =
              static_cast<const unsigned char*>(si->si_addr);
          // Clang ubsan generates ud1l ubsan_type(%eax),%eax in
          // -fsanitize-trap= mode.
          if (pc && pc[0] == 0x67 && pc[1] == 0x0f && pc[2] == 0xb9 &&
              pc[3] == 0x40) {
            absl::SNPrintF(signal_desc + len, sizeof(signal_desc) - len,
                           " (UD1"
                           "@%p), see <link>\n",
                           pc);
            break;
          }
        }
        [[fallthrough]];
#endif
      case SIGTRAP:
#if defined(__aarch64__)
        if (signo == SIGTRAP && si->si_addr != nullptr) {
          const char* pc = (const char*)si->si_addr;
          uint32_t insn;
          memcpy(&insn, pc, sizeof(insn));
          // Clang ubsan generates brk #(ubsan_type | 'U'<<8) in
          // -fsanitize-trap= mode.
          if ((insn & 0xffe0001f) == 0xd4200000 &&
              ((insn >> 13) & 255) == 'U') {
            absl::SNPrintF(signal_desc + len, sizeof(signal_desc) - len,
                           " (TRAP"
                           "@ %p), see <link>\n",
                           pc);
            break;
          }
        }
        [[fallthrough]];
#endif
      case SIGFPE:  // fall through
      case SIGBUS: {
        absl::SNPrintF(signal_desc + len, sizeof(signal_desc) - len,
                       " (@%p), si_code=%d, see <link>", si->si_addr,
                       si->si_code);
        break;
      }
#ifndef __APPLE__
      case SIGPOLL:
        break;
#endif
      default:
        absl::SNPrintF(signal_sender, sizeof(signal_sender),
                       ", si_code=%d, from PID %d", si->si_code, si->si_pid);
        break;
    }
  }

  absl::SNPrintF(buf, bufsize,
                 "*** %s received by PID %d (TID %d)%s%s; %s***\n", signal_desc,
                 getpid(), GetTID(), on_cpu, signal_sender,
                 dump_trace ? "stack trace: " : "");
}

namespace base {
namespace internal {

void EmitSymbolizerURL(void* uc) {
  auto debug_stack_trace_hook =
      absl::debugging_internal::GetDebugStackTraceHook();
  if (debug_stack_trace_hook == nullptr) return;

  // Gather the crashing stack PC values so we can emit via the debug
  // hook (if it exists) first since that involves no symbolization
  // and is safer.
  void* pc = GetPC(uc);
  void* stack[33];
  int min_dropped_frames;
  int depth = absl::GetStackTraceWithContext(
      stack + 1,  // Reserve stack[0] for pc.
      std::size(stack) - 1,
      1,  // Do not include this function in stack trace.
      uc, &min_dropped_frames);
  int start = 1;
  if (pc != nullptr) {
    stack[0] = pc;
    start = 0;
    depth++;
  }
  debug_stack_trace_hook(stack + start, depth, pc, DebugWriteToStderr, nullptr);
}

}  // namespace internal

#if defined(BASE_USE_SIGNAL_H)
void CrashData::SetCallback(void (*callback)(const CrashData*)) {
  crash_data_callback.store(callback, std::memory_order_release);
}
#endif  // defined(BASE_USE_SIGNAL_H)

namespace process_state {

struct NotificationMessage {
  const char* const msg_string;
};

// String representation of notification messages.  If there is a guaranteed
// order between two messages A and B such that A can never occur after B,
// then A must come before B in this array.  Thus, these are approximately
// listed in the order they may occur during execution.
static NotificationMessage notification_messages[] = {
    {"InitGoogleDone"}, {"AppInitDone"},  {"TestSuiteStart"}, {"TestStart"},
    {"TestEnd"},        {"TestSuiteEnd"}, {"ShutdownBegin"}};

const NotificationMessage* const kInitGoogleDone = &notification_messages[0];
const NotificationMessage* const kAppInitDone = &notification_messages[1];
const NotificationMessage* const kTestSuiteStart = &notification_messages[2];
const NotificationMessage* const kTestStart = &notification_messages[3];
const NotificationMessage* const kTestEnd = &notification_messages[4];
const NotificationMessage* const kTestSuiteEnd = &notification_messages[5];
const NotificationMessage* const kShutdownBegin = &notification_messages[6];

// TODO: investigate other process-state related functionality in
// base that could be merged into this interface (e.g.,
// SignalThatProcessIsDying, InFailureSignalHandler, etc.)
bool ReportChange(const NotificationMessage* const notification_message,
                  const char* notification_data) {
  // Verify the notification message is not nullptr
  if (notification_message == nullptr) return false;

  // Write the notification message to the log.  The message string can never
  // be nullptr, since only the above messages are valid for the type signature.
  VLOG(1) << "Process state change notification: "
          << notification_message->msg_string << " "
          << ((notification_data != nullptr) ? notification_data : "");
  return true;
}

}  // namespace process_state
}  // namespace base

#if GOOGLE_ENABLE_SIGNAL_HANDLERS
static void RaiseSignalToDefaultHandler(int signo) {
  // Block the current signal until the handler returns.  This makes the stack
  // nice in the core dump by taking the signal handlers off the call stack.
  sigset_t block_set;
  sigemptyset(&block_set);
  sigaddset(&block_set, signo);
  sigprocmask(SIG_BLOCK, &block_set, nullptr);

  struct sigaction sa = {};
  sa.sa_flags = UseAlternateStackForSignal(signo) ? SA_ONSTACK : 0;
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = SIG_DFL;
  sigaction(signo, &sa, nullptr);

  ABSL_RAW_LOG(ERROR, "Raising signal %d with default behavior", signo);

  raise(signo);
}

// Block GOOGLE_OBSCURE_SIGNAL to avoid lock inversion between the lock used in
// libunwind and the spinlock used in RunInThread::SignalHandler (see
// http://b/19830650).
//
// Store the previous signal mask in *prev. The caller should restore it after
// concluding any potential libunwind calls.
//
// Stack tracing within a signal handler uses libunwind to trace up to the
// nearest frame pointer, and libunwind acquires a spin-lock. The signal
// handlers here, RunInThread::SignalHandler, and ProfileHandler::SignalHandler
// may all trace stacks from within the handler, so if one of the corresponding
// signals arrives while one of the other handlers is in libunwind a deadlock
// may occur.
//
// Note that blocking GOOGLE_OBSCURE_SIGNAL makes deadlock less likely, but does
// not prevent it (see http:////gloop/thread/thread.cc?l=1593&rcl=148376820).
// For example, the deadlock appears to be possible if the user sends SIGTERM to
// the process while RunInThread::SignalHandler happens to be in libunwind on
// the receiving thread.
static void BlockGoogleObscureSignal(sigset_t* prev) {
  sigset_t obscure;
  sigemptyset(&obscure);
  sigaddset(&obscure, GOOGLE_OBSCURE_SIGNAL);
  sigprocmask(SIG_BLOCK, &obscure, prev);
}

// A couple of functions to install signal handlers to trap common
// signals so as to kill the whole program rather than a single thread
// in it.

// The actual signal handler function
static void LoopingSignalHandler(int signo, siginfo_t* si, void* uc) {
  // *** WARNING ***
  //
  // This code is called from a signal handler, and must be fully
  // signal-safe.  It should not invoke LOG() or allocate any memory
  // or use stdio or iostreams or do anything that uses those things.
  // Since we want to let the user attach to us, we can't call the
  // full-blown stack dumper, but we do dump a basic stack to stderr.

  // Block GOOGLE_OBSCURE_SIGNAL while writing tracebacks to avoid lock
  // inversion in libunwind.
  sigset_t prev_sigset;
  BlockGoogleObscureSignal(&prev_sigset);

  // Emit a symbolizer URL now (if possible) in case PC dumping below
  // fails during symbolization.
  base::internal::EmitSymbolizerURL(uc);

  char buf[250];
  FormatSignalMessage(buf, sizeof(buf), signo, uc, si, true, -1);
  (void)write(STDERR_FILENO, buf, strlen(buf));

  DumpPCAndStackTraceForSignalHandler(uc, DebugWriteToStderr, nullptr);

  // Restore the previous mask to unblock GOOGLE_OBSCURE_SIGNAL while we loop.
  sigprocmask(SIG_SETMASK, &prev_sigset, nullptr);

  while (1) {
    absl::SNPrintF(buf, sizeof(buf), "%s",
                   "Sleeping for you to attach to me.\n");
    (void)write(STDERR_FILENO, buf, strlen(buf));

    poll(nullptr, 0, std::numeric_limits<int32_t>::max());
    // This is more debuggable than sleep:
    // for sleep() call gdb sometimes can't climb out of the stack.
  }

  /* If we wanted to die, we'd have to call
     pthreads_kill_other_threads_np to get a core.
     But this core is not useful since all the relevant
     threads are dead! There are some contexts in
     which the static data may still be interesting
  */
}

// This signal handler is the handler of last resort.
// It is called if the regular failure signal handler
// is hung or blocked.

static void ImmediateAbortSignalHandler(int signo) {
  struct sigaction sa;

  // polled by the manager watcher in thread/thread.cc
  // and ~ThreadLocalInternal in thread/threadlocal.cc.
  base::SignalThatProcessIsDying();

  sa.sa_flags = UseAlternateStackForSignal(SIGABRT) ? SA_ONSTACK : 0;
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = SIG_DFL;
  sigaction(SIGABRT, &sa, nullptr);

  // `SIGABRT` might be blocked, so unblock it to ensure the process is aborted.
  sigset_t sa_mask;
  sigemptyset(&sa_mask);
  sigaddset(&sa_mask, SIGABRT);
  pthread_sigmask(SIG_UNBLOCK, &sa_mask, nullptr);

  raise(SIGABRT);
}

// Used only for testing.
void ImmediateAbortSignalHandlerForTesting(int signo) {
  ImmediateAbortSignalHandler(signo);
}

enum class StackDumpMode {
  // Print only important warnings (misaligned, overflow), no stack contents.
  kImportant,
  // Print warnings and the contents of the stack.
  kAll,
};

namespace {

uintptr_t AlignUp(uintptr_t value, size_t alignment) {
  return (value + (alignment - 1)) & ~(alignment - 1);
}

size_t GetPageSize() { return getpagesize(); }

}  // namespace

// Note: this routine assumes that stack grows down, which is true on all
// Linux systems we currently care about.
//
// TSan may complain on data race with another thread.
ABSL_ATTRIBUTE_NO_SANITIZE_THREAD
// ASan doesn't allow reading "random" stack.
ABSL_ATTRIBUTE_NO_SANITIZE_ADDRESS
// MSan may complain on reading "random" uninitialized stack.
ABSL_ATTRIBUTE_NO_SANITIZE_MEMORY
static void DumpStackContents(StackDumpMode mode, void* uc, int signo,
                              siginfo_t* si, DebugWriter* writerfn,
                              void* writerfn_arg) {
#if !PORTABLE_BASE
  char buf[250];
  uintptr_t usp = StackPointerFromUcontext(uc);
  uintptr_t crash_addr = usp;

  // If the signal is one for which si->si_addr (the address that caused
  // the crash) is set, record that address in crash_addr. It could be
  // below the SP at crash due to e.g. red zone access, or it could be
  // completely unrelated to stack at all (when crash is not caused by the
  // stack overflow in the first place).
  switch (signo) {
    case SIGBUS:
    case SIGSEGV:
    case SIGILL:
    case SIGFPE: {
      if (si != nullptr) crash_addr = reinterpret_cast<uintptr_t>(si->si_addr);
      break;
    }
  }

  constexpr int kFailureStackContentWords = 40;
  // Some systems have a stack "red zone" on the "unallocated" side of the
  // stack pointer; we print it since it may contain variables from leaf
  // routines.
  int content_words = kFailureStackContentWords + kRedZoneSize / sizeof(usp);
  usp -= kRedZoneSize;
  void** sp = reinterpret_cast<void**>(usp);

  // If SP is mis-aligned, re-align it.
  if ((usp & (sizeof(sp) - 1)) != 0) {
    absl::SNPrintF(buf, sizeof(buf), "WARNING: mis-aligned stack: %p\n", sp);
    (*writerfn)(buf, writerfn_arg);
    usp &= ~(sizeof(usp) - 1);  // Round down.
    sp = reinterpret_cast<void**>(usp);
  }

  const int width = 2 * sizeof(sp);
  size_t stack_low, stack_high;

  // Fetching stack boundaries failed. Therefore assume the stack covers
  // everything to try to print as much useful information as possible.
  stack_low = 0;
  stack_high = std::numeric_limits<size_t>::max();

  (void)crash_addr;

  // Return if we only wanted the warnings above to be printed.
  if (mode == StackDumpMode::kImportant) {
    return;
  }

  if (usp < stack_low) {
    // Don't try to print inaccessible (below stack boundary) memory.
    sp = reinterpret_cast<void**>(stack_low);
  }

  (*writerfn)("--- Stack contents: ---\n", writerfn_arg);
  // Note: stack_high is the last readable word
  // (i.e. 1 word below actual end of stack).
  absl::SNPrintF(buf, sizeof(buf),
                 "  --- Stack boundaries: [%p, %p) --- (%zuKiB)\n",
                 reinterpret_cast<void*>(stack_low),
                 reinterpret_cast<void*>(stack_high + sizeof(sp)),
                 (stack_high + sizeof(sp) - stack_low) / 1024);
  (*writerfn)(buf, writerfn_arg);

  // The JVM likes to install its own guard pages on some threads. If it has,
  // attempting to access sp may trigger an additional SIGSEGV. If this happens
  // in an unfortunate place, we may spill over the alternative signal stack
  // entirely and be force killed by the kernel. Try to avoid that. Doing so is
  // a bit complicated. The JVM has a few zones for its guard pages. If we are
  // unlucky enough, we may have jumped past the yellow zone without the JVM
  // unguarding them. So we clamp the pages we will actually dump to the ones we
  // can probably read. This is done by iterating up the stack pages until we
  // find the end of the stack or the first unreadable page, then back up to the
  // end of the last readable page.
  {
    if (!base::AddressIsReadable(reinterpret_cast<void*>(sp))) {
      absl::SNPrintF(buf, sizeof(buf), "WARNING: stack unreadable: %p\n", sp);
      (*writerfn)(buf, writerfn_arg);
      return;
    }
    const size_t page_size = GetPageSize();
    uintptr_t readable_stack_high =
        AlignUp(reinterpret_cast<uintptr_t>(sp), page_size);
    while (readable_stack_high <= stack_high) {
      if (!base::AddressIsReadable(
              reinterpret_cast<void*>(readable_stack_high))) {
        // Back to the inclusive end of the previous page.
        --readable_stack_high;
        break;
      }
      readable_stack_high += page_size;
    }
    stack_high = std::max(std::min(stack_high, readable_stack_high),
                          reinterpret_cast<uintptr_t>(sp));
  }

  const int words_per_line = sizeof(sp) == 4 ? 8 : 4;
  bool reached_end_of_stack = false;
  for (; content_words > 0 && !reached_end_of_stack;
       content_words -= words_per_line, sp += words_per_line) {
    int n = absl::SNPrintF(buf, sizeof(buf), "  0x%0*" PRIxPTR ":", width,
                           reinterpret_cast<intptr_t>(sp));
    for (int j = 0; j < std::min(words_per_line, content_words); ++j) {
      if (reinterpret_cast<size_t>(sp + j) < stack_low ||
          stack_high < reinterpret_cast<size_t>(sp + j)) {
        // Outside of stack.
        reached_end_of_stack = true;
        break;
      }
      n += absl::SNPrintF(buf + n, sizeof(buf) - n, " 0x%0*" PRIxPTR, width,
                          reinterpret_cast<intptr_t>(sp[j]));
    }
    absl::SNPrintF(buf + n, sizeof(buf) - n, "\n");
    (*writerfn)(buf, writerfn_arg);
  }
#endif  // !PORTABLE_BASE
}

// This signal handler is caused in event of a "failure"
// of some type. We try and do all the application independent
// cleanup so that our failure will not have been in vain.
// This is essentially flushing all our global resources.

static void FailureSignalHandler(int signo, siginfo_t* si, void* uc) {
  // *** WARNING ***
  //
  // This code is called from a signal handler when the program crashes!
  // The program may be in an arbitrarily bad state at this point.
  // The allocation mutex may be held, the heap may be corrupt, we could
  // be in the middle of an arbitrarily complicated action.
  // The code here should be async-termination-safe; that is, it must not
  // rely on the progress of the interrupted flow of control, nor that
  // of any other thread.   We do not require full async-signal-safety
  // here, so it is legal to use, for example, the write() system call.
  //
  // Most notably, it is NOT SAFE to allocate memory or to call anything
  // which allocates memory.  That means you cannot use LOG() macros,
  // you cannot use stdio, you cannot use iostreams.  If you want to do
  // I/O here, you should use raw Unix file descriptors.
  //
  // At a certain point in this function (marked below with TRANSITION)
  // we stop trying to be safe and start writing to LOG() and doing other
  // unsafe things.  At that point, the program can crash (again!), or
  // hang forever.
  //
  // Be VERY CAREFUL modifying code in this function (or anything it calls).

  // *** NOTE *** This must be the first code in FailureSignalHandler!
  pid_t my_tid = GetTID();
  pid_t old_tid = -1;

  base::internal::failed_tid.compare_exchange_strong(
      old_tid, my_tid, std::memory_order_relaxed, std::memory_order_relaxed);

  if (old_tid != -1) {
    // If this code does crash, we don't want to start it all over again.
    // That would create an infinite loop.  So, we set a flag on entry;
    // if the flag is already set, we invoke the default signal handler.

    void* pc = GetPC(uc);

    // Block GOOGLE_OBSCURE_SIGNAL while writing tracebacks to avoid lock
    // inversion in libunwind.
    sigset_t prev_sigset;
    BlockGoogleObscureSignal(&prev_sigset);

    // Emit a symbolizer URL now (if possible) in case PC dumping below
    // fails during symbolization.
    base::internal::EmitSymbolizerURL(uc);

    ABSL_RAW_LOG(
        ERROR,
        "Signal %d raised at PC: %p while already in FailureSignalHandler!",
        signo, pc);

    if (my_tid != old_tid) {
      ABSL_RAW_LOG(ERROR, "tid: %d raised new signal (old_tid: %d)", my_tid,
                   old_tid);
      // another thread is already killing us off, so wait a bit for it
      // to finish.  Ultimately, if the other thread doesn't kill us
      // off, we'll do so below after the sleep.

#if defined(BASE_PROCESS_STATE_USE_SYS_RESOURCE_H)
      // while waiting, execute safe failure callbacks and closures
      base::internal::ExecuteFailureCallbacks(signo, si, uc, kSafe);
#endif  // defined(BASE_PROCESS_STATE_USE_SYS_RESOURCE_H)
      struct timespec ts, rem;
      // enough time to write STDERR output, dump the core file, etc.
      rem.tv_sec = absl::GetFlag(FLAGS_coredump_timeout) +
                   absl::GetFlag(FLAGS_stacktrace_timeout);
      rem.tv_nsec = 0;
      do {
        ts.tv_sec = rem.tv_sec;
        ts.tv_nsec = rem.tv_nsec;
      } while (nanosleep(&ts, &rem) == -1 && errno == EINTR);
    }

    // Now that we're done with everything that might call libunwind, restore
    // the previous signal mask.
    sigprocmask(SIG_SETMASK, &prev_sigset, nullptr);

    struct sigaction sa;

    sa.sa_flags = UseAlternateStackForSignal(signo) ? SA_ONSTACK : 0;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = SIG_DFL;
    sigaction(signo, &sa, nullptr);

    ABSL_RAW_LOG(ERROR, "Raising %d signal with default behavior", signo);
    raise(signo);

    return;  // Recursively raised signal may be blocked until we return
  }

  // Increase the chance that the CPU we report was the same CPU on which the
  // signal was received by doing this as early as possible, i.e. after
  // verifying that this is not a recursive signal handler invocation.
  int my_cpu = -1;
#ifdef GOOGLE_HAVE_SCHED_GETCPU
  my_cpu = sched_getcpu();
#endif

  // Try to start a debugger on this code, if the user asked for it.
  // Do this before alarm(), or else the program being debugged is interrupted.
  InvokeDebugger(kFailureSignalHandlerName);

  // Number of frames we can unwind in 1s.
  // We allow 100ms per frame, which has been observed on large binaries.
  const int kNumFramesPerSec = 10;
  const int num_frames = absl::GetFlag(FLAGS_dump_stack_frames_limit);

  // alarm_on_failure_initial_secs covers initial tasks like dumping the stack
  // of the faulting thread. If it's too long, we'll have poor time-to-recover.
  // Too short and we'll alarm() before we print out debugging info.
  // The current value is comfortably longer than the worst-case time to dump a
  // symbolized stack for a single thread (~100ms/frame for a very large binary
  // times dump_stack_frames_limit frames in
  // DumpPCAndStackTraceForSignalHandler).
  // We add 3 seconds for other (independent of stack depth) overhead.
  //
  // As of cr/416652490 with default dump_stack_frames_limit==64 we set alarm
  // for 10 seconds.
  const int alarm_on_failure_initial_secs =
      3 + (num_frames + kNumFramesPerSec - 1) / kNumFramesPerSec;

  // *** NOTE *** This should be as early as possible in FailureSignalHandler!
  //
  // Once we go into "unsafe" mode, we use unsafe glibc library calls,
  // and use code which will try to acquire mutexes (e.g. allocation
  // mutexes, the Google logging mutex).  If the mutex is already held,
  // the program will hang forever.
  //
  // Set an alarm to kill the program if that happens.
  if (absl::GetFlag(FLAGS_alarm_on_failure)) {
    // First, destroy any existing alarm
    alarm(0);

    // Set alarm handler to instantly abort
    struct sigaction sa;

    sa.sa_flags = UseAlternateStackForSignal(SIGALRM) ? SA_ONSTACK : 0;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = ImmediateAbortSignalHandler;
    sigaction(SIGALRM, &sa, nullptr);

    // Set an alarm to go off in a little while. If the process is
    // still alive when it goes off, assume we are hung.
    alarm(alarm_on_failure_initial_secs);
  }

  // Describe what happened (safely!) on stdout.
  // We trust absl::SNPrintF to be async-termination-safe, here and elsewhere.

  // Block GOOGLE_OBSCURE_SIGNAL while writing tracebacks to avoid lock
  // inversion between lock used in libunwind and the spinlock used in
  // RunInThread::SignalHandler (http://b/19830650).
  sigset_t prev_sigset;
  BlockGoogleObscureSignal(&prev_sigset);

#if GOOGLE_BASE_HAS_INITGOOGLE
  // Do not dump stack trace if LOG(FATAL) already did so.
  // Suppression can affect only the immediately next signal handler invocation:
  const bool prior_suppress_sigabort_trace =
      absl::log_internal::SetSuppressSigabortTrace(false);
  bool dump_trace = !(signo == SIGABRT && prior_suppress_sigabort_trace);
#else
  bool dump_trace = !(signo == SIGABRT);
#endif

  // Emit a symbolizer URL now (if possible) in case PC dumping below
  // fails during symbolization.
  base::internal::EmitSymbolizerURL(uc);

  char sigmsg[250];
  FormatSignalMessage(sigmsg, sizeof(sigmsg), signo, uc, si, dump_trace,
                      my_cpu);
  (void)write(STDERR_FILENO, sigmsg, strlen(sigmsg));

  if (dump_trace) {
    // Now, we write a stack trace, for the current thread, ONLY to stderr.
    // That way, even if nothing else works, at least we get a basic stack.
    // This should be async-termination-safe.
    DumpPCAndStackTraceForSignalHandler(uc, DebugWriteToStderr, nullptr);
  }

#if defined(BASE_PROCESS_STATE_USE_SYS_RESOURCE_H)
  // Execute known-safe callbacks and closures
  base::internal::ExecuteFailureCallbacks(signo, si, uc, kSafe);
#endif  // defined(BASE_PROCESS_STATE_USE_SYS_RESOURCE_H)

#if defined(BASE_USE_SIGNAL_H)
  base::internal::ExecuteCrashDataCallback(signo, si, uc, my_cpu);
#endif  // defined(BASE_USE_SIGNAL_H)

  // *** TRANSITION ***
  //
  // BEFORE this point, all code must be async-termination-safe!
  // (See WARNING above.)
  //
  // AFTER this point, we do unsafe things, like using LOG()!
  // The process could be terminated or hung at any time.  We try to
  // do more useful things first and riskier things later.

  ABSL_RAW_LOG(INFO, "FailureSignalHandler(): starting unsafe phase");

  // The signalled thread may have been waiting on a Mutex, so re-entry into
  // the Mutex code (e.g., in the logging code below) may corrupt Mutex's
  // per-thread data structures.  The following call prepares the Mutex
  // implementation so that our future Mutex operations are somewhat more
  // likely to succeed.  They can still deadlock of course.
  absl::Mutex::InternalAttemptToUseMutexInFatalSignalHandler();

  // Flush the logs before we do anything in case 'anything'
  // causes problems.
#if !PORTABLE_BASE
  FlushLogFilesUnsafe(static_cast<absl::LogSeverity>(0));

  if (absl::StderrThreshold() > absl::LogSeverityAtLeast::kInfo) {
    // We already wrote this to stderr; don't write it again.
    absl::ScopedStderrThreshold scoped_stderr_threshold(
        absl::LogSeverityAtLeast::kInfinity);

    if (!dump_trace) {
      FormatSignalMessage(sigmsg, sizeof(sigmsg), signo, uc, si, true, my_cpu);
    }

    if (signo == SIGTERM) {
      // Use a unique stack trace for <link> muting as SIGTERM errors are
      // typically unactionable.
      LOG(ERROR) << sigmsg;
    } else {
      LOG(ERROR) << sigmsg;
    }
    DumpPCAndStackTraceForSignalHandler(
        uc, base::DebugWriteToStream,
        &absl::LogErrorStreamer(::absl::SourceLocation::current()).stream());
  }
#endif  // !PORTABLE_BASE

  if (absl::GetFlag(FLAGS_suppress_failure_output)) {
    // Print important stack warnings (stack overflow) even if failure output is
    // suppressed.
    if (dump_trace) {
      DumpStackContents(
          StackDumpMode::kImportant, uc, signo, si, base::DebugWriteToStream,
          &absl::LogWarningStreamer(::absl::SourceLocation::current())
               .stream());
    }
  } else {
    // Dump the registers when the program crashed.
    DumpRegisterContext(
        uc, base::DebugWriteToStream,
        &absl::LogWarningStreamer(::absl::SourceLocation::current()).stream());
    if (dump_trace) {
      DumpStackContents(
          StackDumpMode::kAll, uc, signo, si, base::DebugWriteToStream,
          &absl::LogWarningStreamer(::absl::SourceLocation::current())
               .stream());
    }

    // Lengthen the "we're hung" alarm
    if (absl::GetFlag(FLAGS_alarm_on_failure))
      alarm(alarm_on_failure_initial_secs +
            absl::GetFlag(FLAGS_stacktrace_timeout));

    // Dump a stack trace.  By default, this calls DefaultStackDumper(),
    // which dumps the address map.
    // Code in //gloop/thread/thread.cc replaces it which a function
    // to dump *every* thread's stack.
    (*thread_stack_dumper)(uc);

    // reset the alarm timeout
#if !PORTABLE_BASE
    if (absl::GetFlag(FLAGS_alarm_on_failure))
      alarm(alarm_on_failure_initial_secs);

    // Try to make sure we at least get the stack trace
    FlushLogFilesUnsafe(static_cast<absl::LogSeverity>(0));
#endif  // !PORTABLE_BASE
  }

#if defined(BASE_PROCESS_STATE_USE_SYS_RESOURCE_H)
  base::internal::ExecuteFailureCallbacks(signo, si, uc, kUnsafe);
#endif  // defined(BASE_PROCESS_STATE_USE_SYS_RESOURCE_H)
  // Flush the log files one last time, before we die.
  base_logging::logging_internal::ReprintFatalMessage();
#if !PORTABLE_BASE
  FlushLogFilesUnsafe(static_cast<absl::LogSeverity>(0));
#endif  // !PORTABLE_BASE

  SetKillSignalStatusMessage(signo);

  // Now that we're done with everything that might call libunwind, restore the
  // previous signal mask.
  sigprocmask(SIG_SETMASK, &prev_sigset, nullptr);

  if (absl::GetFlag(FLAGS_test_indicate_sighandler_done)) {
    // Used in the unittests to determine that we successfully
    // completed the signal handler.
    // This can't be done after we raise the signal: if some other thread
    // gets the raised signal, the process will evaporate, and we'll never
    // get the message.
    const char* done_msg = "Signal handler completed\n";
    (void)write(2, done_msg, strlen(done_msg));
  }

  // Re-raise the signal to terminate the process with the appropriate core file
  // and exit status.
  //
  // The recursively raise()'d signal is blocked until we return.
  RaiseSignalToDefaultHandler(signo);

  // recursively raise()'d signal is blocked until we return. This
  // makes the stack nice in the core dump by taking
  // FailureSignalHandler off the call stack.
  ABSL_RAW_LOG(INFO, "FailureSignalHandler() exiting");
}

namespace base {

void HandleOrRaiseFailureSignal(int signo, siginfo_t* info, void* context) {
  // Use memory_order_acquire here to ensure that the subsequent read to
  // FLAGS_infinite_loop_on_signal will not race on flag parsing.
  bool handle_signo = base::internal::handle_failure_signal[signo].load(
      std::memory_order_acquire);
  if (!handle_signo) {
    RaiseSignalToDefaultHandler(signo);
  } else if (absl::GetFlag(FLAGS_infinite_loop_on_signal)) {
    LoopingSignalHandler(signo, info, context);
  } else {
    FailureSignalHandler(signo, info, context);
  }
}

}  // namespace base

extern "C" void CoreDumpSanitization_SetupAlternateSignalHandlerStack()
    ABSL_ATTRIBUTE_WEAK;

#ifndef MAP_STACK
// Linux specific, available since 2.6.27 and glibc-2.19
// Currently a a no-op according to man pages
#define MAP_STACK (0)
#endif

#ifdef GOOGLE_HAVE_POSIX_SIGNAL_ALT_STACK
static void SetupAlternateStack() {
#if !PORTABLE_BASE
  // CoreDumpSanitization is only available in google3 and might not be linked
  // in. If it is linked in it takes up ownership of the altstacks.
  if (&CoreDumpSanitization_SetupAlternateSignalHandlerStack != nullptr) {
    CoreDumpSanitization_SetupAlternateSignalHandlerStack();
    return;
  }
#endif  // !PORTABLE_BASE
  // Create an alternate stack to execute signal handlers. This is
  // useful in detecting stack overflow errors (in which case
  // there is no room to run the signal handler on that stack).
  // This stack is for the thread that calls InitGoogle() (typically the
  // main thread, but not always, e.g. in the case where we're loaded
  // into a JVM).  Other threads get their own alternate signal stacks.
  // Note that SA_ONSTACK is only set for the "failure signals".
  // Handlers for other signals like SIGPROF run on the default stack
  // of the receiving thread.
  stack_t sigstk = {};
  sigstk.ss_size = GetRequiredAlternateSignalStackSize();
  sigstk.ss_sp = mmap(nullptr, sigstk.ss_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
  if (sigstk.ss_sp == MAP_FAILED) {
    PLOG(FATAL) << "mmap for alternate signal stack";
  }
  PCHECK(sigaltstack(&sigstk, nullptr) != -1);
}
#endif  // GOOGLE_HAVE_POSIX_SIGNAL_ALT_STACK

// TODO: perhaps set an unexpected() handler too.
// For now, the most common problem seems to be terminate()s
// due to STL exceptions.

void InstallSignalHandlers() {
  struct sigaction sa = {};
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = SIG_IGN;

  // Only ignore SIGHUP and SIGPIPE if there isn't some other handler
  // registered.

  if (ShouldInstallDefaultSignalHandler("hup", SIGHUP)) {
#if GOOGLE_BASE_HAS_INITGOOGLE
    if (!absl::GetFlag(FLAGS_silent_init)) {
      VLOG(1) << "Ignoring SIGHUPs";
    }
#endif
    sigaction(SIGHUP, &sa, nullptr);  // better redirect stdout!
  }
  if (ShouldInstallDefaultSignalHandler("pipe", SIGPIPE)) {
#if GOOGLE_BASE_HAS_INITGOOGLE
    if (!absl::GetFlag(FLAGS_silent_init)) {
      VLOG(1) << "Ignoring SIGPIPEs";
    }
#endif
    sigaction(SIGPIPE, &sa, nullptr);  // functions will return EPIPE instead
  }
#ifdef GOOGLE_HAVE_POSIX_SIGNAL_ALT_STACK
  if (UseAlternateSignalHandlerStack()) {
    SetupAlternateStack();
  }
#endif
  const bool handler_loops = absl::GetFlag(FLAGS_infinite_loop_on_signal);
  const auto handler =
      handler_loops ? LoopingSignalHandler : FailureSignalHandler;

  if (handler == FailureSignalHandler) {
    // This call is here in order to make sure that whatever system calls
    // are used by the implementation of AddressIsReadable are allowed by
    // various sandbox policies. See b/206786528 for details.
    CHECK(base::AddressIsReadable(&sa));
  }

  base::internal::InstallSignalHandler(handler_loops, handler);
}
#endif  // GOOGLE_ENABLE_SIGNAL_HANDLERS

// Sanitized binaries require more stack space. See http://b/7982846.
static size_t GetAlternateSignalStackMultiplier() {
  return tcmalloc::MallocExtension::GetNumericProperty(
             "dynamic_tool.stack_size_multiplier")
      .value_or(1);
}

int32_t GetRequiredAlternateSignalStackSize() {
  // FailureSignalHandler() itself can run on a 16KB stack, but closures
  // given to RunOnFailure() may also run on the alternate signal stack,
  // so make it a comfortable size.
  const int page_size = getpagesize();
  const int page_mask = page_size - 1;
  int required_stack =
      (std::max<size_t>(SIGSTKSZ, 65536) + page_mask) & ~page_mask;

#ifdef STACKTRACE_USES_LIBUNWIND
  // Libunwind requires a bit more stack still.
  required_stack += page_size;
#endif

  static const size_t stack_multiplier = GetAlternateSignalStackMultiplier();
  required_stack *= stack_multiplier;

  return required_stack;
}

bool UseAlternateSignalHandlerStack() {
#if defined(GOOGLE_HAVE_POSIX_SIGNAL_ALT_STACK) && \
    defined(GOOGLE_ENABLE_SIGNAL_HANDLERS)
  return absl::GetFlag(FLAGS_install_signal_handlers) &&
         !absl::GetFlag(FLAGS_install_named_signal_handlers).empty() &&
         absl::GetFlag(FLAGS_use_alternate_stack_for_signal_handlers);
#else
  return false;
#endif
}

#if defined(BASE_PROCESS_STATE_USE_SYS_RESOURCE_H)
void LimitPhysicalMemory(double fraction, bool randomize) {
  // sanity check
  CHECK(fraction >= 0.01);

  int64_t total_memory = PhysicalMem();

  pid_t pid = getpid();

  double rnd_factor = 1.0;
  if (randomize) {
    switch (pid % 4) {
      case 0:
        break;
      case 1:
        rnd_factor = 0.95;
        break;
      case 2:
        rnd_factor = 0.9;
        break;
      case 3:
        rnd_factor = 0.85;
        break;
    }
  }

  int64_t limit_memory =
      static_cast<int64_t>(total_memory * fraction * rnd_factor);

  // Ensure the limit fits into rlim_t.
  if (limit_memory > std::numeric_limits<rlim_t>::max()) {
    LOG(WARNING) << "Memory limit requested (" << limit_memory
                 << ") exceed the maximum possible (due rlimit structure). ";
    limit_memory =
        static_cast<int64_t>(std::numeric_limits<rlim_t>::max() * rnd_factor);
    LOG(WARNING) << "Enforcing limit of " << limit_memory << " instead.";
  }

  struct rlimit r;

  r.rlim_max = r.rlim_cur = limit_memory;

  setrlimit(RLIMIT_AS, &r);  // apparently RLIMIT_DATA is broken on linux
}

uint64_t GetPhysicalMemoryLimit() {
  struct rlimit r;
  int ok = getrlimit(RLIMIT_AS, &r);
  return (ok != 0 ? 0 : r.rlim_cur);
}
#endif  // defined(BASE_PROCESS_STATE_USE_SYS_RESOURCE_H)

#if !defined(__Fuchsia__)
namespace base {
namespace internal {

const char KernelVersionInfo::kDefaultKernelVersionString[] =
    "<The kernel version is not available on this platform>";
const KernelVersion KernelVersionInfo::kDefaultKernelVersion =  //
    {-1, -1, -1, -1, -1};

KernelVersionInfo* ReadAndParseKernelVersionString(const char* filename) {
  auto* info = new KernelVersionInfo;
  bool uname_fallback = false;
  static const size_t kBufferSize = 1024;
  char buffer[kBufferSize];
  FILE* fp = fopen(filename, "r");
  if (fp != nullptr) {
    PCHECK(fgets(buffer, kBufferSize, fp) != nullptr)
        << "Failed to read kernel version string from " << filename;
    fclose(fp);
    info->raw_version_string = buffer;  // fgets zero-terminates buffer.
  } else {
    // Fallback to uname call if /proc/version unavailable.
    uname_fallback = true;
    struct utsname u;
    PCHECK(uname(&u) != -1) << "Failed to read kernel version from uname";
    info->raw_version_string =
        absl::StrFormat("%s version %s %s", u.sysname, u.release, u.version);
  }
  if (!ParseKernelVersionString(info->raw_version_string,
                                &info->parsed_version)) {
    // Invalidate the kernel version on parse failure.
    info->parsed_version = KernelVersionInfo::kDefaultKernelVersion;
    LOG(ERROR) << "Failed to parse version string read from "
               << (uname_fallback ? "uname()" : filename) << ": "
               << info->raw_version_string;
  }
  return info;
}

bool ParseKernelVersionString(absl::string_view raw_string,
                              KernelVersion* const parsed_version) {
  // We assume that g_kernel_version (read from /proc/version) is of the form
  // "Linux version x.y.(z?) ...  #p ..."
  size_t pos = raw_string.find('.');
  if (pos == absl::string_view::npos) return false;
  pos = raw_string.rfind(' ', pos);
  if (pos == absl::string_view::npos) return false;

  std::string raw_str_copy(raw_string);
  switch (sscanf(raw_str_copy.c_str() + pos + 1, "%d.%d.%d",
                 &parsed_version->major, &parsed_version->minor,
                 &parsed_version->micro)) {
    case 0:
    case 1:
      return false;
      break;
    case 2:
      if (parsed_version->major < 0) return false;
      if (parsed_version->minor < 0) return false;
      parsed_version->micro = 0;
      break;
    case 3:
      if (parsed_version->major < 0) return false;
      if (parsed_version->minor < 0) return false;
      if (parsed_version->micro < 0) return false;
      break;
    default:
      LOG(FATAL) << "unexpected parse result ";
  }
  pos = raw_str_copy.find('#', pos);
  if (pos != std::string::npos) {
    const char* patchpos = raw_str_copy.c_str() + pos + 1;
    if (!strncmp("DEV", patchpos, 3) || !strncmp("gg", patchpos, 2)) {
      // Handle e.g. 2.4.18-smp #DEV (not from a release branch) or
      // 2.6.32-gg129 (gg129 = google build #)
      parsed_version->patch = 0;
    } else {
      // See if revision number is present e.g. #175.13
      if (sscanf(patchpos, "%d.%d", &parsed_version->patch,
                 &parsed_version->revision) != 2) {
        // Revision number not present, just use zero
        parsed_version->revision = 0;
        if (sscanf(patchpos, "%d", &parsed_version->patch) != 1) {
          LOG(ERROR) << "Unknown kernel patch version " << patchpos;
          return false;
        }
      }
    }
  }
  return true;
}

bool GetKernelVersionIfValid(const KernelVersionInfo& info,
                             KernelVersion* const output_version) {
  if (info.parsed_version.major >= 0) {
    *output_version = info.parsed_version;
    return true;
  } else {
    return false;
  }
}

const KernelVersionInfo& GetKernelVersionInfo() {
  static KernelVersionInfo* const info =
      ReadAndParseKernelVersionString("/proc/version");
  return *info;
}

}  // namespace internal
}  // namespace base

const char* GetKernelVersionString() {
  return base::internal::GetKernelVersionInfo().raw_version_string.c_str();
}

bool GetKernelVersion(KernelVersion* const kv) {
  return base::internal::GetKernelVersionIfValid(
      base::internal::GetKernelVersionInfo(), kv);
}

#endif
