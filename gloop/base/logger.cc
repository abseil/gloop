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

#include "gloop/base/logger.h"

#include <string_view>

#include "absl/base/const_init.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/log_sink.h"
#include "gloop/base/config.h"

#if GLOOP_INTERNAL_PROD_LOGGING

#include <cassert>
#include <ctime>
#include <memory>

#include "absl/base/attributes.h"
#include "absl/base/log_severity.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/globals.h"
#include "absl/log/log_entry.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gloop/base/log_file.h"

namespace base_logging {
void Logger::UnusedKeyMethod() {}  // <link>

namespace {

ABSL_CONST_INIT absl::Mutex borrowed_references_guard(absl::kConstInit);
using BorrowedSinkReferences =
    absl::flat_hash_map<absl::LogSink*, std::shared_ptr<absl::LogSink>>;
ABSL_CONST_INIT BorrowedSinkReferences* borrowed_sink_references
    ABSL_GUARDED_BY(borrowed_references_guard)
        ABSL_PT_GUARDED_BY(borrowed_references_guard) = nullptr;

void SaveBorrowedReference(std::shared_ptr<absl::LogSink> sink)
    ABSL_LOCKS_EXCLUDED(borrowed_references_guard) {
  absl::MutexLock lock(borrowed_references_guard);
  if (!borrowed_sink_references) {
    borrowed_sink_references = new BorrowedSinkReferences;
  }
  (*borrowed_sink_references)[sink.get()] = sink;
}

using BorrowedLoggerReferences = absl::flat_hash_set<Logger*>;
ABSL_CONST_INIT BorrowedLoggerReferences* borrowed_logger_references
    ABSL_GUARDED_BY(borrowed_references_guard)
        ABSL_PT_GUARDED_BY(borrowed_references_guard) = nullptr;

void SaveBorrowedReference(Logger* logger)
    ABSL_LOCKS_EXCLUDED(borrowed_references_guard) {
  absl::MutexLock lock(borrowed_references_guard);
  if (!borrowed_logger_references) {
    borrowed_logger_references = new BorrowedLoggerReferences;
  }
  borrowed_logger_references->insert(logger);
}

// `GetLogger()` below has an unfortunate interface from an ownership management
// perspective. `GetLogFileSink()` returns a shared pointer, but we need to
// return a raw pointer. If we do nothing and user code calls `GetLogger()`
// followed by `SetLogger()`, the `Logger` pointer returned by `GetLogger()`
// might became dangling, since `SetLogger()` will invoke `ReplaceLogFileSink()`
// and this routine releases the `LogFileSink` object it ownerd before if no
// other references remains.
// To prevent this we use "borrowed references" every time
// `GetLogger()` is called. To avoid unbounded multiplication of references to
// the same LogFileSink object in case of repeated GetLogger calls, we use a
// hash map to maintain a single "borrowed reference" per LogFileSink object.
// We never return this reference, so once we call GetLogger at least once
// we hold on to the shared pointer forever, thus keeping the object allocated
// forever. Long-term it might become problematic if users start to register
// multiple custom `LogFileSink`s intermixed with the calls to `GetLogger()`.
// We expect all the `Logger` instances (ang thus calls to `GetLogger()`) to
// disappear before this problem might materialize.
std::shared_ptr<absl::LogSink> GetBorrowedReference(absl::LogSink* sink)
    ABSL_LOCKS_EXCLUDED(borrowed_references_guard) {
  absl::MutexLock lock(borrowed_references_guard);
  if (!borrowed_sink_references) {
    borrowed_sink_references = new BorrowedSinkReferences;
  }
  return (*borrowed_sink_references)[sink];
}

// This class is a "bridge" LogSink that points to, but does not own a
// Logger, to which it forwards all entries at or above `severity`. We create
// an instance of this class every time we want to use instance of Logger as a
// LogFileSink for the specified `severity`.
class LogFileSinkToLoggerBridge final : public absl::LogSink {
 public:
  explicit LogFileSinkToLoggerBridge(absl::LogSeverity severity, Logger* logger)
      : severity_(severity), logger_(logger) {}
  ~LogFileSinkToLoggerBridge() override {
    absl::MutexLock lock(borrowed_references_guard);
    if (borrowed_logger_references == nullptr ||
        borrowed_logger_references->find(logger_) ==
            borrowed_logger_references->end()) {
      delete logger_;
    }
  }

  Logger* Logger() const { return logger_; }

  // LogSink interface
  void Send(const absl::LogEntry& entry) override {
    if (entry.log_severity() < severity_) return;

    time_t time_secs = absl::ToTimeT(entry.timestamp());
    auto message = entry.text_message_with_prefix_and_newline();
    logger_->Write(entry.log_severity() > absl::LogSeverity::kInfo, time_secs,
                   message.data(), message.size());
  }
  void Flush() override { logger_->Flush(); }

 private:
  absl::LogSeverity severity_;
  base_logging::Logger* logger_;
};

}  // namespace

// Returns currently active Logger or LogFileSink for specified `severity`. If
// create is false, it may return nullptr, if no Logger/LogFileSink were
// registered before by calls to SetLogger or InitializeLogFileSinks.
// If current LogFileSink is an instance of the "bridge" to the Logger, which
// was created via the call to SetLogger, we return underlying Logger pointer.
// Otherwise we return LogFileSink, which is expected to implement the Logger
// interface.
Logger* GetLogger(absl::LogSeverity severity) {
  std::shared_ptr<absl::LogSink> log_file =
      logging_internal::GetLogFileSink(severity);
  if (!log_file) return nullptr;
  auto* bridge = dynamic_cast<LogFileSinkToLoggerBridge*>(log_file.get());
  if (bridge != nullptr) {
    SaveBorrowedReference(bridge->Logger());
    return bridge->Logger();
  }

  SaveBorrowedReference(log_file);

  auto* logger = dynamic_cast<Logger*>(log_file.get());
  assert(logger != nullptr);
  return logger;
}

namespace logging_internal {

// Sets `logger` to represent a currently active LogFileSink
// If `logger` is nullptr, we replace the current LogFileSink with nullptr,
// effectively preventing logging. If `logger` is an  instance of LogSink,
// which also implements the Logger interface, we use the borrowed reference
// for it and use it to set the current LogFileSink. Otherwise we create a new
// LogFileSinkToLoggerBridge, which will refer to the specified `logger`.
// Note that bridge does not own the logger instance, but only points to it.
// A logger instance may only be registered once per severity.
void SetLogger(absl::LogSeverity severity, Logger* logger) {
  if (logger == nullptr) {
    base_logging::ReplaceLogFileSink(severity,
                                     std::shared_ptr<absl::LogSink>{});
    return;
  }

  auto* sink = dynamic_cast<absl::LogSink*>(logger);
  if (sink != nullptr) {
    ReplaceLogFileSink(severity, GetBorrowedReference(sink));
    return;
  }

  ReplaceLogFileSink(
      severity, std::make_shared<LogFileSinkToLoggerBridge>(severity, logger));
}

}  // namespace logging_internal
}  // namespace base_logging
#endif  // GLOOP_INTERNAL_PROD_LOGGING
