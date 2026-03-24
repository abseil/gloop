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

#ifndef THIRD_PARTY_GLOOP_BASE_SIGNAL_HANDLER_H_
#define THIRD_PARTY_GLOOP_BASE_SIGNAL_HANDLER_H_

#include <atomic>
#include <string>

#include "absl/flags/declare.h"
#include "absl/strings/string_view.h"
#include "gloop/base/config.h"  // For GOOGLE_ENABLE_SIGNAL_HANDLERS

#if defined(GOOGLE_ENABLE_SIGNAL_HANDLERS)
#include <csignal>  // IWYU pragma: keep
#endif

// Set an alarm in the crash handler to avoid infinite loops
ABSL_DECLARE_FLAG(bool, alarm_on_failure);

// Comma-separated list of signal numbers/groups to install if
// install_signal_handlers==true
ABSL_DECLARE_FLAG(std::string, install_named_signal_handlers);

// TODO investigate usages of this flag and come up with a better
// comment / help string.
// If true, signal handlers are installed
ABSL_DECLARE_FLAG(bool, install_signal_handlers);

// Run signal handlers on a separate stack
ABSL_DECLARE_FLAG(bool, use_alternate_stack_for_signal_handlers);

// Return whether the default signal handler for "signal" in
// "signal_group" should be installed.  See default value of flag
// install_named_signal_handlers for list of valid signal group names.
bool ShouldInstallDefaultSignalHandler(absl::string_view signal_group,
                                       int signal);

// Return whether the handler for "signal" should be configured to use an
// alternate signal stack if present (e.g. by setting SA_ONSTACK in the sa_flags
// of a "struct sigaction").
bool UseAlternateStackForSignal(int signal);

// Return true if currently executing in the google failure signal
// handler. If this returns true you should:
//
// - avoid allocating anything via malloc/new
// - assume that your stack limit is SIGSTKSZ
// - assume that no other thread can be executing in the failure handler
bool InFailureSignalHandler();

// Return true if the google default signal handler is running, false
// otherwise.  Sometimes callbacks specified with
// RunOnFailure are not called because the process hangs
// or takes too long to symbolize callstacks. Users may want to
// augment the RunOnFailure mechanism with a dedicated thread which
// polls the below function periodically (say, every second) and runs
// their failure callbacks when it returns true.
bool IsFailureSignalHandlerRunning();

namespace base {

// This returns, in a thread safe way, the value of the process_is_dying
// variable, which is set to true at the start of exit() processing and at the
// start of signal handlers that are intended to kill the process.  It is
// never set back to false.
bool ProcessIsDying();

// This puts the process in dying mode (see above).
void SignalThatProcessIsDying();

namespace internal {

#if defined(GOOGLE_ENABLE_SIGNAL_HANDLERS)
void InstallSignalHandler(bool handler_loops,
                          void (*handler)(int, siginfo_t*, void*));

extern std::atomic<bool> handle_failure_signal[];
#endif  // GOOGLE_ENABLE_SIGNAL_HANDLERS

extern std::atomic<pid_t> failed_tid;

}  // namespace internal
}  // namespace base

#endif  // THIRD_PARTY_GLOOP_BASE_SIGNAL_HANDLER_H_
