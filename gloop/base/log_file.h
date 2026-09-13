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

// Interfaces for managing log files for specific severity levels
#ifndef THIRD_PARTY_GLOOP_BASE_LOG_FILE_H_
#define THIRD_PARTY_GLOOP_BASE_LOG_FILE_H_

#include "absl/log/log_entry.h"
#include "absl/log/log_sink.h"
#include "gloop/base/config.h"

#if GLOOP_INTERNAL_PROD_LOGGING

#include <functional>
#include <memory>
#include <string>

#include "absl/base/log_severity.h"
#include "gloop/base/log_severity.h"
#endif  // GLOOP_INTERNAL_PROD_LOGGING

namespace base_logging {
//-----------------------------------------------------------------------------
// Log To Files
// This option tells logging library that logging to local disk is
// enabled/disabled for all severity levels. By default this option is set to
// true.

// Returns the value of the Log To Files option.
// This function is async-signal-safe.
bool LogToFiles();

// Updates the value of the Log To Files option.
// This function is async-signal-safe.
void EnableLogToFiles(bool on_off);

#if GLOOP_INTERNAL_PROD_LOGGING
//-----------------------------------------------------------------------------
// Interfaces which operate on underlying log file objects.

// Log file parameters. This struct is only intended to be used as a parameter
// to ConfigureLogfile(s) interfaces below. Do NOT persist this structure.
struct LogFileConfig final {
  // Main part of the log file name. The log file will be created with the name
  // constructed as basename + extension + timestamp. If basename is empty
  // we use "<program name>.<hostname>.<user name>.log.<severity level>" by
  // default
  std::string basename;
  // an extension added to the log filename
  std::string extension;
  // Permissions for the log file. If this is set to 0, log files will
  // be set 0664 and restricted by `umask`. Otherwise, `umask` will be
  // ignored by calling `chmod` after the initial creation.
  int permissions = 0;
  // Basename of the symlink to the log file. If symlink is empty
  // we use basename(argv0) by default
  std::string symlink;
  // If false, we do not create a symlink.
  bool create_symlink = true;
};

// Creates new log file for specified severity level based on specified
// configuration. Old log file object is destroyed and file is closed.
void ConfigureLogFile(absl::LogSeverity severity, const LogFileConfig& config);

// -----------------------------------------------------------------------------
// Deprecated functions for operating/customizing log files

// Set the destination to which a particular severity level of log messages is
// sent.  If base_filename is "", it means "don't log this severity".
// Thread-safe.
void SetLogDestination(absl::LogSeverity severity, const char* base_filename);
inline void SetLogDestination(base_logging::LogSeverity severity,
                              const char* base_filename) {
  SetLogDestination(static_cast<absl::LogSeverity>(severity), base_filename);
}

// Set the basename of the symlink to the latest log file at a given severity.
// If symlink_basename is empty, do not make a symlink.  If you don't call this
// function, the symlink basename is the invocation name of the program.
// Thread-safe.
void SetLogSymlink(absl::LogSeverity severity, const char* symlink_basename);
inline void SetLogSymlink(base_logging::LogSeverity severity,
                          const char* symlink_basename) {
  SetLogSymlink(static_cast<absl::LogSeverity>(severity), symlink_basename);
}

// Specify an extension added to the filename specified via `SetLogDestination`.
// This applies to all severity levels.  It's often used to append the port
// we're listening on to the logfile name.  Thread-safe.
void SetLogFilenameExtension(const char* filename_extension);

// Specifies permissions for the log file.  By default (or if this is called
// with 0), log files will be set 0664 and restricted by `umask`.  When this
// function is called with a non-zero value, `umask` will be ignored by calling
// `chmod` after the initial creation.  Thread-safe.
void SetLogFilePermissions(int permissions);

// Flushes all log files that contain messages that are of at least the
// specified severity level.
void FlushLogFiles(absl::LogSeverity min_severity);
inline void FlushLogFiles(base_logging::LogSeverity min_severity) {
  FlushLogFiles(static_cast<absl::LogSeverity>(min_severity));
}

// Performs thread unsafe flush of log files.
// A thread-hostile. To be use only for catastrophic failures.
// TODO: eliminate parameter
void FlushLogFilesUnsafe(absl::LogSeverity /*min_severity - unused*/);
inline void FlushLogFilesUnsafe(base_logging::LogSeverity min_severity) {
  FlushLogFilesUnsafe(static_cast<absl::LogSeverity>(min_severity));
}

//-----------------------------------------------------------------------------
// Interfaces which operate on sinks, which correspond to log files with
// specified severity. Log file sink by default uses underlying log file, but
// can be replaced with custom sink.

namespace logging_internal {
// TODO: Delete this method. It is only used by the bridge.
// If new LogFile sink is created, it is automatically registered in the global
// sinks list.
std::shared_ptr<absl::LogSink> GetLogFileSink(absl::LogSeverity severity);
}  // namespace logging_internal

// Registers default log file sinks, if no custom registered before.
void InitializeLogFileSinks();

// Replace active log file sink for the specified severity with new one. Returns
// owned pointer to the old log file sink so that caller can decide if it can be
// released. New sink is added to the global log sinks collection and the old
// one is removed from the list.
std::shared_ptr<absl::LogSink> ReplaceLogFileSink(
    absl::LogSeverity severity, std::shared_ptr<absl::LogSink> new_sink);

// Filters are binary predicates operating on absl::LogEntry and returning true
// if message should be written or false if it should be skipped.
// The filter set is not ordered and you can't rely on particular order
// of filter invocations. Moreover we will short-circuit filters invocations, so
// you can't rely on the filter function being called for every message.
// Additionally, the following requirements apply to log filters:
//
// * must be fast (non-blocking)
// * must not have side effects (due to the undefined evaluation order and
//   short circuiting) - use LogSink instead if you want that
// * must be thread-safe
// * must not change LogToFiles() or filters configuration
// * it's okay to LOG from filter routine, but your messages won't appear in
//   log files, but will be directed to stderr instead.

// For the time being filter accepts a pointer argument. This will be updated
// to use a reference once the Logger API is eliminated.
using LogFileFilter = std::function<bool(const absl::LogEntry*)>;

// Attach log file filter to the sink for specified severity.
void AttachLogFileFilter(absl::LogSeverity severity, LogFileFilter filter);
#endif  // GLOOP_INTERNAL_PROD_LOGGING
}  // namespace base_logging

#endif  // THIRD_PARTY_GLOOP_BASE_LOG_FILE_H_
