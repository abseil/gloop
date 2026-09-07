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

#include "gloop/base/log_file.h"

#include "gloop/base/config.h"

#if GLOOP_INTERNAL_PROD_LOGGING

#include <cassert>
#include <cstddef>
#include <ctime>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/const_init.h"
#include "absl/base/log_severity.h"
#include "absl/log/globals.h"
#include "absl/log/internal/globals.h"
#include "absl/log/log_entry.h"
#include "absl/synchronization/mutex.h"
#include "gloop/base/log_file_object.h"
#include "gloop/base/logger.h"
#endif  // GLOOP_INTERNAL_PROD_LOGGING

#include <array>
#include <atomic>
#include <functional>

#include "absl/base/attributes.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/log_sink.h"
#include "absl/log/log_sink_registry.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace base_logging {
namespace {
ABSL_CONST_INIT std::atomic<bool> log_to_files{true};
}  // namespace

#if GLOOP_INTERNAL_PROD_LOGGING
namespace {
constexpr size_t ToIndex(absl::LogSeverity severity) {
  assert(severity == absl::NormalizeLogSeverity(severity));
  const size_t severity_index =
      static_cast<size_t>(absl::NormalizeLogSeverity(severity));
  return severity_index;
}

//-----------------------------------------------------------------------------
// Interfaces which operate on underlying log file objects.

// Each `LogFile` corresponds to a severity level. Messages are recorded to
// the file(s) that meet *or exceed* the corresponding severity level; typically
// a set of four `LogFile`s is used to create four files, one per severity
// level, such that the INFO file contains everything and the FATAL file
// contains only FATAL messages (usually just one, but it's often long due to
// stack dumps).
class LogFile final {
 public:
  explicit LogFile(absl::LogSeverity severity, absl::TimeZone tz)
      : severity_(severity), old_object_(severity_, nullptr, tz) {}

  // Log file configuration interfaces
  void Configure(const LogFileConfig& config) ABSL_LOCKS_EXCLUDED(guard_) {
    absl::MutexLock lock(guard_);
    if (!config.basename.empty()) {
      old_object_.SetBasename(config.basename.c_str());
    }
    old_object_.SetExtension(config.extension.c_str());
    assert(config.permissions >= 0);
    old_object_.SetPermissions(config.permissions);
    if (!config.create_symlink) {
      old_object_.SetSymlinkBasename("");
    } else if (!config.symlink.empty()) {
      old_object_.SetSymlinkBasename(config.symlink.c_str());
    }
  }

  void AttachFilter(LogFileFilter filter) ABSL_LOCKS_EXCLUDED(guard_) {
    absl::MutexLock lock(guard_);
    filters_.push_back(std::move(filter));
  }

  void SetLogDestination(const char* base_filename)
      ABSL_LOCKS_EXCLUDED(guard_) {
    absl::MutexLock lock(guard_);
    old_object_.SetBasename(base_filename);
  }

  void SetLogSymlink(const char* symlink_basename) ABSL_LOCKS_EXCLUDED(guard_) {
    absl::MutexLock lock(guard_);
    old_object_.SetSymlinkBasename(symlink_basename);
  }

  void SetLogFilenameExtension(const char* filename_extension)
      ABSL_LOCKS_EXCLUDED(guard_) {
    absl::MutexLock lock(guard_);
    old_object_.SetExtension(filename_extension);
  }

  void SetLogFilePermissions(int permissions) ABSL_LOCKS_EXCLUDED(guard_) {
    absl::MutexLock lock(guard_);
    old_object_.SetPermissions(permissions);
  }

  void Send(const absl::LogEntry& entry) ABSL_LOCKS_EXCLUDED(guard_) {
    if (entry.log_severity() < severity_) return;

    absl::MutexLock lock(guard_);
    for (const auto& filter : filters_) {
      if (!filter(&entry)) return;
    }
    time_t time_secs = absl::ToTimeT(entry.timestamp());
    auto message = entry.text_message_with_prefix_and_newline();

    old_object_.Write(
        /* force_flush = */ entry.log_severity() > absl::LogSeverity::kInfo,
        time_secs, message.data(), message.size());
  }
  void Flush() ABSL_LOCKS_EXCLUDED(guard_) {
    absl::MutexLock lock(guard_);
    old_object_.Flush();
  }
  void FlushUnsafe() ABSL_NO_THREAD_SAFETY_ANALYSIS {
    old_object_.FlushUnsafe();
  }
  void Write(bool force_flush, time_t timestamp, const char* message,
             size_t message_len) ABSL_LOCKS_EXCLUDED(guard_) {
    absl::MutexLock lock(guard_);
    for (const auto& filter : filters_) {
      if (!filter(nullptr)) return;
    }
    old_object_.Write(force_flush, timestamp, message, message_len);
  }

  std::string Path() ABSL_LOCKS_EXCLUDED(guard_) {
    absl::MutexLock lock(guard_);
    return old_object_.GetLogPath();
  }
  std::string SymlinkPath() ABSL_LOCKS_EXCLUDED(guard_) {
    absl::MutexLock lock(guard_);
    return old_object_.GetSymlinkPath();
  }
  size_t LogSize() ABSL_LOCKS_EXCLUDED(guard_) {
    absl::MutexLock lock(guard_);
    return old_object_.LogSize();
  }

 private:
  // The severity that this log file corresponds to.
  const absl::LogSeverity severity_;
  // Protects access to mutable data.
  absl::Mutex guard_;
  // Underlying log file object (to be inlined in this class).
  logging_internal::LogFileObject old_object_ ABSL_GUARDED_BY(guard_);
  // List of attached filters.
  std::vector<LogFileFilter> filters_ ABSL_GUARDED_BY(guard_);
};

ABSL_CONST_INIT absl::Mutex log_files_guard(absl::kConstInit);
ABSL_CONST_INIT LogFile*
    log_files[absl::LogSeverities().size()] ABSL_GUARDED_BY(log_files_guard) = {
        nullptr};

LogFile* GetLogFile(absl::LogSeverity severity)
    ABSL_LOCKS_EXCLUDED(log_files_guard) {
  absl::MutexLock lock(log_files_guard);
  auto* log_file = log_files[ToIndex(severity)];
  if (!log_file) {
    // The absl::TimeZone used to render civil times in logfile names/headers.
    // Note that we only call absl::LocalTimeZone() once, which means we only
    // read ${TZ} once. This is a feature.
    //
    // TODO: It would be preferable to just use the timezone returned
    // by absl::log_internal::TimeZone(), but it is at least unclear whether
    // base_logging::Initialize() has completed before GetLogFile() is called
    // for the first time. So, we get our own timezone instead (which should
    // be the same as that set by base_logging::Initialize()).
    static auto* tz = new absl::TimeZone(absl::LocalTimeZone());
    log_file = new LogFile(severity, *tz);
    log_files[ToIndex(severity)] = log_file;
  }
  return log_file;
}

}  // namespace

void ConfigureLogFile(absl::LogSeverity severity, const LogFileConfig& config) {
  GetLogFile(severity)->Configure(config);
}

void SetLogDestination(absl::LogSeverity severity, const char* base_filename) {
  GetLogFile(severity)->SetLogDestination(base_filename);
}

void SetLogSymlink(absl::LogSeverity severity, const char* symlink_basename) {
  GetLogFile(severity)->SetLogSymlink(symlink_basename);
}

void SetLogFilenameExtension(const char* filename_extension) {
  for (auto severity : absl::LogSeverities()) {
    GetLogFile(severity)->SetLogFilenameExtension(filename_extension);
  }
}

void SetLogFilePermissions(int permissions) {
  for (auto severity : absl::LogSeverities()) {
    GetLogFile(severity)->SetLogFilePermissions(permissions);
  }
}

void FlushLogFilesUnsafe(absl::LogSeverity) {
  if (!base_logging::LogToFiles()) return;

  for (auto severity : absl::LogSeverities()) {
    GetLogFile(severity)->FlushUnsafe();
  }
}

std::string GetLogPath(absl::LogSeverity severity) {
  if (!base_logging::LogToFiles()) return "";
  return GetLogFile(severity)->Path();
}

std::string GetSymlinkPath(absl::LogSeverity severity) {
  if (!base_logging::LogToFiles()) return "";
  return GetLogFile(severity)->SymlinkPath();
}

//-----------------------------------------------------------------------------
// Interfaces which operate on sinks, which correspond to log files with
// specified severity. By default we use sinks based on underlying log file
// object, but sinks can be customized with user defined ones.

namespace {
ABSL_CONST_INIT absl::Mutex log_file_sinks_guard(absl::kConstInit);
ABSL_CONST_INIT std::shared_ptr<absl::LogSink>*
    log_file_sinks[absl::LogSeverities().size()] ABSL_GUARDED_BY(
        log_file_sinks_guard) = {nullptr};

// Creation and registration of log file sinks are independent operations.
// A log file sink can be created but not registered with the logging system.
// is_log_file_sink_registered[i] is true if log_file_sinks[i] is currently
// registered with the logging system.
ABSL_CONST_INIT bool
    is_log_file_sink_registered[absl::LogSeverities().size()] ABSL_GUARDED_BY(
        log_file_sinks_guard) = {false};

// "Default" log file sink, which corresponds directly to an
// underlying log file object.
class LogFileSink final : public absl::LogSink, public base_logging::Logger {
 public:
  explicit LogFileSink(absl::LogSeverity severity)
      : log_file_(GetLogFile(severity)) {}
  ~LogFileSink() override {}

  // LogSink interface
  void Send(const absl::LogEntry& entry) override {
    if (entry.log_severity() == absl::LogSeverity::kFatal) {
      if (!entry.stacktrace().empty()) {
        log_file_->Write(true, 0, entry.stacktrace().data(),
                         entry.stacktrace().size());
      }
    }
    log_file_->Send(entry);
  }
  void Flush() override { log_file_->Flush(); }

  // Logger interface
  void Write(bool force_flush, time_t timestamp, const char* message,
             size_t message_len) override {
    log_file_->Write(force_flush, timestamp, message, message_len);
  }
  size_t LogSize() override { return log_file_->LogSize(); };

 private:
  // Non-owning pointer to underlying log file.
  LogFile* log_file_;
};

}  // namespace

namespace logging_internal {
static std::shared_ptr<absl::LogSink> GetLogFileSinkLocked(
    absl::LogSeverity severity, bool create)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(log_file_sinks_guard) {
  std::shared_ptr<absl::LogSink>* sink = log_file_sinks[ToIndex(severity)];
  if (!sink) {
    if (!create) return {};
    sink = log_file_sinks[ToIndex(severity)] =
        new std::shared_ptr<absl::LogSink>;
  }
  if (!*sink) {
    if (!create || !base_logging::LogToFiles()) return {};
    *sink = std::make_shared<LogFileSink>(severity);
  }
  return *sink;
}

std::shared_ptr<absl::LogSink> GetLogFileSink(absl::LogSeverity severity)
    ABSL_LOCKS_EXCLUDED(log_file_sinks_guard) {
  absl::MutexLock lock(log_file_sinks_guard);
  return GetLogFileSinkLocked(severity, /*create=*/true);
}
}  // namespace logging_internal

void InitializeLogFileSinks() ABSL_LOCKS_EXCLUDED(log_file_sinks_guard) {
  absl::MutexLock lock(log_file_sinks_guard);
  for (auto severity : absl::LogSeverities()) {
    auto sink = logging_internal::GetLogFileSinkLocked(severity, true);
    if (base_logging::LogToFiles() &&
        !is_log_file_sink_registered[ToIndex(severity)]) {
      absl::AddLogSink(sink.get());
      is_log_file_sink_registered[ToIndex(severity)] = true;
    }
  }
}

std::shared_ptr<absl::LogSink> ReplaceLogFileSink(
    absl::LogSeverity severity, std::shared_ptr<absl::LogSink> new_sink)
    ABSL_LOCKS_EXCLUDED(log_file_sinks_guard) {
  absl::MutexLock lock(log_file_sinks_guard);
  std::shared_ptr<absl::LogSink>* old_sink_ptr =
      log_file_sinks[ToIndex(severity)];
  std::shared_ptr<absl::LogSink> old_sink;

  if (old_sink_ptr != nullptr) {
    old_sink = *old_sink_ptr;
  } else {
    log_file_sinks[ToIndex(severity)] = new std::shared_ptr<absl::LogSink>;
  }

  *log_file_sinks[ToIndex(severity)] = new_sink;
  if (base_logging::LogToFiles()) {
    if (old_sink && is_log_file_sink_registered[ToIndex(severity)]) {
      absl::RemoveLogSink(old_sink.get());
      is_log_file_sink_registered[ToIndex(severity)] = false;
    }
    if (new_sink) {
      absl::AddLogSink(new_sink.get());
      is_log_file_sink_registered[ToIndex(severity)] = true;
    }
  }

  return old_sink;
}

void FlushLogFiles(absl::LogSeverity min_severity) {
  // TODO: this should really only flush underlying log files.
  absl::MutexLock lock(log_file_sinks_guard);
  for (auto severity : absl::LogSeverities()) {
    if (severity < min_severity) continue;
    auto sink = logging_internal::GetLogFileSinkLocked(severity, false);
    if (sink) sink->Flush();
  }
}

void AttachLogFileFilter(absl::LogSeverity severity, LogFileFilter filter) {
  LogFile* log_file = GetLogFile(severity);
  log_file->AttachFilter(std::move(filter));
}
#endif  // GLOOP_INTERNAL_PROD_LOGGING

bool LogToFiles() { return log_to_files.load(std::memory_order_relaxed); }

void EnableLogToFiles(bool on_off) {
  log_to_files.store(on_off, std::memory_order_relaxed);
#if GLOOP_INTERNAL_PROD_LOGGING
  absl::MutexLock lock(log_file_sinks_guard);
  // Do NOT register sinks before InitializeLogFileSinks() is called.
  if (!absl::log_internal::IsInitialized()) return;

  for (auto severity : absl::LogSeverities()) {
    auto sink = logging_internal::GetLogFileSinkLocked(severity, on_off);

    if (!on_off && is_log_file_sink_registered[ToIndex(severity)]) {
      if (sink) absl::RemoveLogSink(sink.get());
      is_log_file_sink_registered[ToIndex(severity)] = false;
    } else if (on_off && !is_log_file_sink_registered[ToIndex(severity)] &&
               sink) {
      absl::AddLogSink(sink.get());
      is_log_file_sink_registered[ToIndex(severity)] = true;
    }
  }
#endif  // GLOOP_INTERNAL_PROD_LOGGING
}

}  // namespace base_logging
