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

#include "gloop/base/raw_logging.h"

#include <atomic>
#include <cstring>
#include <ctime>
#include <iterator>

#include "absl/base/attributes.h"
#include "absl/base/call_once.h"
#include "absl/base/log_severity.h"
#include "absl/debugging/stacktrace.h"
#include "absl/log/globals.h"
#include "absl/log/internal/globals.h"
#include "absl/log/internal/log_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "gloop/base/config.h"
#include "gloop/base/crash.h"
#include "gloop/base/examine_stack.h"
#include "gloop/base/sysinfo.h"  // For GetTID()

namespace base_raw_log {
namespace raw_log_internal {
namespace {

bool Google3LogFilterAndPrefixHook(absl::LogSeverity severity, const char* file,
                                   int line, char** buf, int* buf_size) {
  if (severity < absl::StderrThreshold() &&
      absl::log_internal::IsInitialized() &&
      severity < absl::LogSeverityAtLeast::kError) {
    return false;
  }

  if (severity < absl::MinLogLevel()) {
    return false;
  }

#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || \
    defined(__Fuchsia__) || defined(__native_client__) ||               \
    defined(__OpenBSD__) || defined(__EMSCRIPTEN__)
  // `clock_gettime` is async-signal-safe per POSIX.  Unlike `time()`, its API
  // supports sub-second resolution.
  struct timespec ts;
  const absl::Time timestamp = clock_gettime(CLOCK_REALTIME, &ts)
                                   ? absl::UnixEpoch()
                                   : absl::TimeFromTimespec(ts);
#else
  const absl::Time timestamp = absl::Now();
#endif

  absl::string_view file_basename =
      absl::raw_log_internal::Basename(file, strlen(file));

  absl::Span<char> buf_remaining(*buf, *buf_size);
  absl::log_internal::FormatLogPrefix(
      severity, timestamp, ::GetTID(), file_basename, line,
      absl::log_internal::PrefixFormat::kRaw, buf_remaining);

  *buf = buf_remaining.data();
  *buf_size = buf_remaining.size();
  return true;
}

void Google3AbortHook(const char* file, int line, const char* buf_start,
                      const char* prefix_end, const char* buf_end) {
#if BASE_HAVE_CRASHREASON
  // Note: RAW_CHECK may be used in contexts like a fiber scheduler,
  // where taking locks is prohibited.  Therefore, assert that atomic<bool>
  // is lock-free. Also, use ABSL_CONST_INIT to make sure that crash_reason
  // doesn't use the on-first-use initialization of function-local statics,
  // because that would take a lock.
  ABSL_CONST_INIT static std::atomic_flag crashed = ATOMIC_FLAG_INIT;
  ABSL_CONST_INIT static base::CrashReason crash_reason;

  if (!crashed.test_and_set(std::memory_order_relaxed)) {
    // `buf_end` points at the end of the caller's buffer; the nul-terminated
    // message is likely shorter than that, and we don't have a pointer to it.
    crash_reason.filename = file;
    crash_reason.line_number = line;
    // Don't include prefix
    crash_reason.message = absl::string_view(prefix_end);
    crash_reason.depth = absl::GetStackTrace(crash_reason.stack,
                                             std::size(crash_reason.stack), 2);
    base::SetCrashReason(&crash_reason);
  }
#endif  // BASE_HAVE_CRASHREASON
  // It's harder to recover a full stack trace from inside
  // `FailureSignalHandler()`, so we capture one here and direct it not to
  // capture a second one.
  DumpStackTrace(1, DebugWriteToStderr, nullptr);
  absl::log_internal::SetSuppressSigabortTrace(true);
  abort();
}

struct InstallHooksOnStartup final {
  InstallHooksOnStartup() { InstallGoogle3Hooks(); }
} install_hooks_on_startup;

void InstallGoogle3HooksOnce() {
  absl::raw_log_internal::RegisterLogFilterAndPrefixHook(
      Google3LogFilterAndPrefixHook);
  absl::raw_log_internal::RegisterAbortHook(Google3AbortHook);
}

}  // namespace

void InstallGoogle3Hooks() {
  ABSL_CONST_INIT static absl::once_flag once;
  absl::base_internal::LowLevelCallOnce(&once, InstallGoogle3HooksOnce);
}

}  // namespace raw_log_internal
}  // namespace base_raw_log
