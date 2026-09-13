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

// Defines some google3-only extensions to (what will become) the Abseil logging
// API (documented in logging.h).
//
// This header consists of the following:
//
// * Documentation for the default behavior of logging in production (and corp).
// * Structured logging helpers.
// * Miscellaneous
// * `absl::LogSink` implementations for some common operations.
// * A number of functions for customizing the built-in logging-to-files
//   behavior.

// For on-disk logs, each logged line is written into the file corresponding to
// its severity *and* the files for all lower severity levels such that the
// `INFO` file contains all messages logged at any severity.  By default, the
// `WARNING`, `ERROR`, and `FATAL` severity levels cause a synchronous fflush()
// call for all written log files.  Beware the impact on performance.
//
// Log lines are written to these files per this template:
//
//   Lmmdd hh:mm:ss.uuuuuu threadid file:line] msg...
//
// Where the fields are defined as follows:
//
//   L                One of [IWEF] corresponding to the severity level.
//   mm               The month (zero padded; ie May is '05')
//   dd               The day (zero padded)
//   hh:mm:ss.uuuuuu  Time in hours, minutes and fractional seconds
//   threadid         The space-padded thread ID as returned by GetTID()
//                    (this matches the PID on Linux)
//   file             The file name
//   line             The line number
//   msg              The user-supplied message
//
// Example:
//
//   I1103 11:57:31.739339 24395 google.cc:2341] Command line: ./some_prog
//   I1103 11:57:31.739403 24395 google.cc:2342] Process id 24395
//
// Structured logging is used in most google3 Linux prod and corp build
// configurations but not e.g. on mobile platforms.  It buffers logged data in
// protobuf wire-format in one buffer before rendering it as text into another
// buffer and making both buffers available to `LogSink`s.  Long messages and
// messages with long wire-format representations are truncated, but the maximum
// size is dependent on the structure of the message.

#ifndef THIRD_PARTY_GLOOP_BASE_LOGGING_EXTENSIONS_H_
#define THIRD_PARTY_GLOOP_BASE_LOGGING_EXTENSIONS_H_

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

#include "absl/base/log_severity.h"
#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/internal/log_message.h"
#include "absl/log/log_entry.h"
#include "absl/log/log_sink.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "gloop/base/config.h"
#include "gloop/base/internal/temp_directories.h"  // to be removed
#include "gloop/base/log_file.h"
#include "gloop/base/log_severity.h"

#if GLOOP_INTERNAL_PROD_LOGGING
#include "gloop/base/internal/logging_directories.h"  // to be removed
#endif  // GLOOP_INTERNAL_PROD_LOGGING

// -----------------------------------------------------------------------------
// Miscellaneous
// -----------------------------------------------------------------------------

namespace base_logging {
// Returns the number of messages logged at the specified severity level.
size_t LoggedMessages(absl::LogSeverity severity);
// Returns the number of bytes of text logged at the specified severity level,
// including prefixes.
size_t LoggedBytes(absl::LogSeverity severity);

// Returns the size of the buffers used by logging.  This is used in google3 to
// ballpark the maximum message size, however it is inexact (and no better
// estimate can be made) in the presence of structured logging (see file
// comment).  Rather than use this function, prefer to avoid logging large data,
// accept truncation of large data, or use a different API better suited to
// saving large data to disk.
constexpr size_t LogBufferSize() {
  return absl::log_internal::kLogMessageBufferSize;
}

// Returns the path to the log file of the indicated severity. An empty string
// indicates that no log file is currently in use.
std::string GetLogPath(absl::LogSeverity severity);
inline std::string GetLogPath(base_logging::LogSeverity severity) {
  return GetLogPath(static_cast<absl::LogSeverity>(severity));
}

// Returns the path to the symlink to the logfile of the indicated severity. An
// empty string indicates that no log file and/or symlink is currently in use.
std::string GetSymlinkPath(absl::LogSeverity severity);

}  // namespace base_logging

// -----------------------------------------------------------------------------
// `absl::LogSink` Implementations
// -----------------------------------------------------------------------------

namespace base_logging {
// Has no effect if constructed with a null `str`; otherwise each logged message
// will be copied into `*str` without its prefix or a trailing newline.
class CopyToStringSink final : public absl::LogSink {
 public:
  explicit CopyToStringSink(std::string* absl_nullable str) : str_(str) {}

  CopyToStringSink(const CopyToStringSink& other) = delete;
  CopyToStringSink& operator=(const CopyToStringSink& other) = delete;

  void Send(const absl::LogEntry& entry) override {
    if (str_) {
      absl::MutexLock lock(mutex_);
      str_->assign(std::string(entry.text_message()));
    }
  }

 private:
  absl::Mutex mutex_;
  std::string* absl_nullable str_ ABSL_PT_GUARDED_BY(mutex_);
};

// Has no effect if constructed with a null `vec`; otherwise each logged message
// will be pushed onto `*vec` without its prefix or a trailing newline.
class AppendToVectorSink final : public absl::LogSink {
 public:
  explicit AppendToVectorSink(std::vector<std::string>* absl_nullable vec)
      : vec_(vec) {}

  AppendToVectorSink(const AppendToVectorSink& other) = delete;
  AppendToVectorSink& operator=(const AppendToVectorSink& other) = delete;

  void Send(const absl::LogEntry& entry) override {
    if (vec_) {
      absl::MutexLock lock(mutex_);
      vec_->emplace_back(entry.text_message());
    }
  }

 private:
  absl::Mutex mutex_;
  std::vector<std::string>* absl_nullable vec_ ABSL_PT_GUARDED_BY(mutex_);
};

// If constructed with a null `sink`, does nothing; otherwise forwards to
// `sink`.
class NullSafeSinkWrapper final : public absl::LogSink {
 public:
  explicit NullSafeSinkWrapper(absl::LogSink* absl_nullable sink)
      : sink_(sink) {}

  NullSafeSinkWrapper(const NullSafeSinkWrapper& other) = default;
  // Reassignment is not thread-safe.  In particular, don't reassign an instance
  // while it is registered with `absl::AddLogSink`.
  NullSafeSinkWrapper& operator=(const NullSafeSinkWrapper& other) = default;

  void Send(const absl::LogEntry& entry) override {
    if (sink_) sink_->Send(entry);
  }
  void Flush() override {
    if (sink_) sink_->Flush();
  }

 private:
  absl::LogSink* absl_nullable sink_;
};
}  // namespace base_logging

// -----------------------------------------------------------------------------
// Functions for Customizing Production/Corp Logging Behavior
// -----------------------------------------------------------------------------
// TODO: all of these global namespace routines should be inlined.

// Flushes all log files that contain messages that are of at least the
// specified severity level.  Thread-safe. The portable implementation does
// nothing.
inline void FlushLogFiles(absl::LogSeverity min_severity) {
#if GLOOP_INTERNAL_PROD_LOGGING
  base_logging::FlushLogFiles(min_severity);
#endif  // GLOOP_INTERNAL_PROD_LOGGING
}
// A thread-hostile variant of FlushLogFiles for catastrophic failures.
inline void FlushLogFilesUnsafe(absl::LogSeverity min_severity) {
#if GLOOP_INTERNAL_PROD_LOGGING
  base_logging::FlushLogFilesUnsafe(min_severity);
#endif  // GLOOP_INTERNAL_PROD_LOGGING
}

#if !PORTABLE_BASE
// Generate a special status message describing the progress of a long-running
// program for use by monitoring scripts.  `done` specifies the amount of work
// already completed and `total` specifies the full amount.  Their units don't
// matter but must match.  Thread-hostile if
// FLAGS_status_messages_to_status_file and thread-safe otherwise.
void StatusMessage(int64_t done, int64_t total);
#endif

#if GLOOP_INTERNAL_PROD_LOGGING
// Return the set of directories to try writing a log file into.  Do not call
// before `InitGoogle` or the returned value will not honor flags (e.g.
// --log_dir), and furthermore it will be cached and the flags will not take
// effect when `InitGoogle` is eventually called.
inline std::vector<std::string> GetLoggingDirectories() {
  return base_logging::logging_internal::LoggingDirectories();
}
#endif  // GLOOP_INTERNAL_PROD_LOGGING

// Returns a set of existing temporary directories, which will be a subset of
// the directories returned by `GetLoggingDirectories`.  Thread-safe.
inline void GetExistingTempDirectories(
    std::vector<std::string>* absl_nonnull dirs) {
  *dirs = base::internal::ExistingTempDirectories();
}

namespace base_logging::logging_internal {

class MessageCounterSink final : public absl::LogSink {
 public:
  void Send(const absl::LogEntry& entry) override;
};

}  // namespace base_logging::logging_internal

#endif  // THIRD_PARTY_GLOOP_BASE_LOGGING_EXTENSIONS_H_
