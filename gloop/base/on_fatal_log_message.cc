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

#include "gloop/base/on_fatal_log_message.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/log_severity.h"
#include "absl/base/macros.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "gloop/base/config.h"
#include "gloop/base/tracecontext.h"

#if BASE_HAVE_CRASHREASON
#include "absl/debugging/stacktrace.h"
#include "gloop/base/context.h"
#include "gloop/base/crash.h"
#include "gloop/base/tracer.h"
#endif
#if BASE_HAVE_PROCESS_STATE
#include "gloop/base/signal-handler.h"
#endif
#include "absl/log/internal/globals.h"
#include "absl/log/log_entry.h"

namespace base_logging {
namespace logging_internal {
namespace {
// Copy of first FATAL log message so that we can print it out again after all
// the stack traces.  `fatal_message` mustn't be read until an acquire-load of
// `fatal_message_ready` returns true.
ABSL_CONST_INIT absl::string_view fatal_message;
ABSL_CONST_INIT std::atomic<bool> fatal_message_ready{false};

// Copies into `dst` as many bytes of `src` as will fit, then truncates the
// copied bytes from the front of `dst` and returns the number of bytes written.
size_t AppendTruncated(absl::string_view src, absl::Span<char>* dst) {
  if (src.size() > dst->size()) src = src.substr(0, dst->size());
  memcpy(dst->data(), src.data(), src.size());
  dst->remove_prefix(src.size());
  return src.size();
}

}  // namespace
}  // namespace logging_internal
}  // namespace base_logging

// Weak functions don't work well under Lexan (Windows / MSVC)
// See b/347686146 for some more background and original failure mode.
// Talking to the core library team, we would surprised if the windows
// Lexan folks would be interrested in this code at all, and QED, it
// doesn't work currently. We simply disable it.
#if ABSL_HAVE_ATTRIBUTE_WEAK && !defined(_MSC_VER)

// The library will call this function only once, even if multiple `FATAL`s
// race.  Another thread may concurrently run `FailureSignalHandler`.
extern "C" void ABSL_INTERNAL_C_SYMBOL(AbslInternalOnFatalLogMessage)(
    const absl::LogEntry& entry) {
#if BASE_HAVE_PROCESS_STATE
  // We're in a FATAL log message. Ensure that the process will die despite
  // any issues we may encounter from here on out.
  base::SignalThatProcessIsDying();
#endif

  // Store shortened fatal message for other logs and GWQ status.
  ABSL_CONST_INIT static std::array<char, 512> fatal_message_buf{{0}};
  auto fatal_message_remaining = absl::MakeSpan(fatal_message_buf);
  fatal_message_remaining.remove_suffix(1);  // Save space for a '\n'
  size_t chars_written = base_logging::logging_internal::AppendTruncated(
      entry.text_message_with_prefix(), &fatal_message_remaining);
  // Append a '\n' unless the message already ends with one.
  // TODO: append a newline unconditionally instead
  if (!chars_written || fatal_message_buf[chars_written - 1] != '\n') {
    fatal_message_buf[chars_written++] = '\n';
  }
  base_logging::logging_internal::fatal_message = {fatal_message_buf.data(),
                                                   chars_written};
  base_logging::logging_internal::fatal_message_ready.store(
      true, std::memory_order_release);

#if BASE_HAVE_CRASHREASON
  ABSL_CONST_INIT static std::atomic_flag crashed = ATOMIC_FLAG_INIT;
  ABSL_CONST_INIT static base::CrashReason reason;
  if (crashed.test_and_set(std::memory_order_relaxed)) return;
  // Record crash context information (for Deathrattle).
  reason.filename = entry.source_filename();
  reason.line_number = entry.source_line();
  reason.message = entry.text_message_with_newline();
  // Retrieve the stack trace, omitting this frame.
  reason.depth = absl::GetStackTrace(reason.stack, std::size(reason.stack), 1);
  // Grab the current TraceContext. We are not within a signal handler here,
  // but we'll use CurrentNoAlloc() instead of Current() anyway because we don't
  // need to create a new context object if one doesn't already exist.
  const TraceContext* tc = base::CurrentTraceContextNoAlloc();
  if (tc != nullptr && tc->CanRecordAnnotations()) {
    // We're not within a signal handler here, so using ToString() is safe.
    std::string info = tc->tracer()->ToString();
    const int tocopy =
        std::min<int>(std::size(reason.trace_info) - 1, info.length());
    memcpy(reason.trace_info, info.c_str(), tocopy);
    reason.trace_info[tocopy] = '\0';
  }
  base::SetCrashReason(&reason);

#endif
}

#endif  // ABSL_HAVE_ATTRIBUTE_WEAK && !_MSC_VER

namespace base_logging {
namespace logging_internal {

void ReprintFatalMessage() {
  // Perhaps this crash wasn't due to a fatal log and so no fatal message was
  // written, or perhaps there was another concurrent fatal log which won the
  // race to write out the `fatal_message` but hasn't yet finished doing so.
  // Either way, we write nothing.  If there was a message, it already got
  // printed and the reader will just have to scroll past the thread stacks to
  // find it.
  if (fatal_message_ready.load(std::memory_order_acquire)) {
    absl::log_internal::WriteToStderr(fatal_message, absl::LogSeverity::kFatal);
  }
}

}  // namespace logging_internal
}  // namespace base_logging
