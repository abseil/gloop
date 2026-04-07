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

// LogFileObject encapsulates all filesystem-related state for a single logfile.

#ifndef THIRD_PARTY_GLOOP_BASE_LOG_FILE_OBJECT_H_
#define THIRD_PARTY_GLOOP_BASE_LOG_FILE_OBJECT_H_

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

#include "absl/base/log_severity.h"
#include "absl/log/internal/config.h"
#include "absl/time/time.h"

namespace base_logging {
namespace logging_internal {

// Thread-compatible
class LogFileObject {
 public:
  LogFileObject(absl::LogSeverity severity, const char* base_filename,
                absl::TimeZone tz);
  ~LogFileObject();

  void Write(bool force_flush, time_t timestamp, const char* message,
             int message_len);

  // Configuration options
  void SetBasename(const char* basename);
  void SetExtension(const char* ext);
  void SetPermissions(int permissions);
  void SetSymlinkBasename(const char* symlink_basename);

  // Returns the current log filename, or an empty string if none.
  std::string GetLogPath() const { return log_path_; }
  std::string GetSymlinkPath() const { return symlink_path_; }

  // Normal flushing routine
  void Flush();

  // It is the actual file length for the system loggers,
  // i.e., INFO, ERROR, etc.
  size_t LogSize() const { return file_length_; }

  // Flush without updating stats so as to not require holding locks.  Use this
  // only when the program is crashing (e.g. in a signal handler) and locks
  // can't be taken safely.
  void FlushUnsafe();

  std::string TestOnlyFileHeaderString(absl::Time timestamp) const;

 private:
  // Actually create a logfile using the value of base_filename_ and the
  // supplied argument time_pid_string
  bool CreateLogfile(const std::string& time_pid_string);

  // Closes the log file and clears any associated settings. If delete_file is
  // true, it also tries to delete the old log file.
  void CloseLogfile(bool delete_file = false);

  void FlushUnlocked();

  bool base_filename_selected_;
  std::string base_filename_;
  std::string symlink_basename_;
  // The current path to the log.
  std::string log_path_;
  // The current path to the symlink.
  std::string symlink_path_;
  // Used for lazy initialization
  bool symlink_basename_initialized_;
  // option users can specify (eg to add port#)
  std::string filename_extension_;
  int permissions_;
  FILE* file_;
  absl::LogSeverity severity_;
  uint32_t bytes_since_flush_;
  uint32_t file_length_;
  // TimeZone used for civil times in logfile names/headers.
  absl::TimeZone tz_;
  // Time at which to flush log
  absl::Time next_flush_time_;
  // Time of log file creation
  absl::Time log_creation_time_;
  bool disk_full_;
};

}  // namespace logging_internal
}  // namespace base_logging

#endif  // THIRD_PARTY_GLOOP_BASE_LOG_FILE_OBJECT_H_
