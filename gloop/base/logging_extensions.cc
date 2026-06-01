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

#include "gloop/base/logging_extensions.h"

#include <stddef.h>
#include <stdint.h>

#include <array>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/log_severity.h"
#include "absl/base/thread_annotations.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/log/log_entry.h"
#include "absl/log/log_sink_registry.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "gloop/base/config.h"
#include "gloop/base/sysinfo.h"

namespace base_logging {

namespace {

ABSL_CONST_INIT absl::Mutex stats_mutex(absl::kConstInit);
ABSL_CONST_INIT std::array<size_t, absl::LogSeverities().size()> logged_messages
    ABSL_GUARDED_BY(stats_mutex) = {{0}};
// `logged_bytes` includes the prefix (if any), the message (if any), and the
// newline (if any), but not any nul terminator.
ABSL_CONST_INIT std::array<size_t, absl::LogSeverities().size()> logged_bytes
    ABSL_GUARDED_BY(stats_mutex) = {{0}};

}  // namespace

namespace logging_internal {

// Simple LogSink, which maintains counter of logged messages.
void MessageCounterSink::Send(const absl::LogEntry& entry) {
  const size_t bytes_logged =
      entry.text_message_with_prefix_and_newline().size();

  absl::MutexLock l(stats_mutex);
  const int severity_index = static_cast<int>(entry.log_severity());
  logged_messages[severity_index] += 1;
  logged_bytes[severity_index] += bytes_logged;
}

}  // namespace logging_internal

size_t LoggedMessages(absl::LogSeverity severity)
    ABSL_LOCKS_EXCLUDED(stats_mutex) {
  absl::MutexLock l(stats_mutex);
  const int severity_index =
      static_cast<int>(absl::NormalizeLogSeverity(severity));
  return logged_messages[severity_index];
}

size_t LoggedBytes(absl::LogSeverity severity)
    ABSL_LOCKS_EXCLUDED(stats_mutex) {
  absl::MutexLock l(stats_mutex);
  const int severity_index =
      static_cast<int>(absl::NormalizeLogSeverity(severity));
  return logged_bytes[severity_index];
}

}  // namespace base_logging

#if !PORTABLE_BASE

void StatusMessage(int64_t done, int64_t total) {
  LOG(INFO) << "STATUS: " << done << " of " << total;
  LOG(INFO) << "Memory Footprint: " << (VirtualProcessSize() >> 10) << "K.";
  absl::FlushLogSinks();
}
#endif
