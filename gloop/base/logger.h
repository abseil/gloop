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

// Defines the `base_logging::Logger` interface and a corresponding registration
// mechanism.

#ifndef THIRD_PARTY_GLOOP_BASE_LOGGER_H_
#define THIRD_PARTY_GLOOP_BASE_LOGGER_H_

#include <stddef.h>
#include <time.h>

#include "absl/base/log_severity.h"
#include "gloop/base/config.h"
#include "gloop/base/log_severity.h"

#if GLOOP_INTERNAL_PROD_LOGGING
namespace base_logging {
// `Logger` is an interface used to write logged messages out (e.g. to disk
// files).  It can be extended and registered to override the default behavior.
// While `absl::LogSink` is a supplemental mechanism for doing additional things
// with logged messages, there is only one `Logger` for each severity, and
// registering a new one replaces the existing one.  If in doubt, use a
// `absl::LogSink`.  Implementations must be thread-safe because the logging
// system will write to them from multiple threads.
class Logger {
 public:
  virtual ~Logger() = default;

  // Writes `message[0, message_len - 1]` corresponding to an event that
  // occurred at `timestamp`.  If `force_flush` is true, the log file is flushed
  // immediately.
  // `message` has already been formatted as deemed appropriate by the higher
  // level logging facility.  For example, textual log messages already contain
  // timestamps and the file:linenumber header.
  virtual void Write(bool force_flush, time_t timestamp, const char* message,
                     size_t message_len) = 0;

  // `Flush` any buffered messages
  virtual void Flush() = 0;

  // Get the current log file size.  The returned value is approximate due to
  // various race conditions.
  virtual size_t LogSize() = 0;

 private:
  virtual void UnusedKeyMethod();  // <link>
};

// Get the `Logger` for the specified severity level.  The logger remains the
// property of the logging module and should not be deleted by the caller.
// Thread-safe.
namespace logging_internal {
Logger* GetLogger(absl::LogSeverity severity, bool create = true);
}  // namespace logging_internal
inline Logger* GetLogger(absl::LogSeverity severity) {
  return logging_internal::GetLogger(severity, true);
}
inline Logger* GetLogger(base_logging::LogSeverity severity) {
  return GetLogger(static_cast<absl::LogSeverity>(severity));
}
// Set the `Logger` for the specified severity level.  The logger becomes the
// property of the logging module and should not be deleted by the caller.
// Thread-safe.
void SetLogger(absl::LogSeverity severity, Logger* logger);
inline void SetLogger(base_logging::LogSeverity severity, Logger* logger) {
  SetLogger(static_cast<absl::LogSeverity>(severity), logger);
}
}  // namespace base_logging

// deprecated aliases (to be deleted)
namespace base {
using Logger = base_logging::Logger;
inline Logger* GetLogger(absl::LogSeverity severity) {
  return base_logging::GetLogger(severity);
}
inline Logger* GetLogger(base_logging::LogSeverity severity) {
  return base_logging::GetLogger(static_cast<absl::LogSeverity>(severity));
}
inline void SetLogger(absl::LogSeverity severity, Logger* logger) {
  base_logging::SetLogger(severity, logger);
}
inline void SetLogger(base_logging::LogSeverity severity, Logger* logger) {
  base_logging::SetLogger(static_cast<absl::LogSeverity>(severity), logger);
}

}  // namespace base

#endif  // GLOOP_INTERNAL_PROD_LOGGING

#endif  // THIRD_PARTY_GLOOP_BASE_LOGGER_H_
