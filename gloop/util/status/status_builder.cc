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

#include "gloop/util/status/status_builder.h"

#include <memory>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>

#include "absl/base/log_severity.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/internal/vlog_config.h"
#include "absl/log/log.h"
#include "absl/log/log_entry.h"
#include "absl/log/log_sink.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/source_location.h"
#include "gloop/base/examine_stack.h"
#include "gloop/util/status/status.h"

namespace util {

void StatusBuilder::Destroy(std::unique_ptr<Rep>) {
  // nothing to do. The unique_ptr will do the cleanup.
}

// These constructors are not-inlined and defined in the .cc file to reduce
// binary size. See cl/354351433 for a quantification.
StatusBuilder::StatusBuilder() {}

StatusBuilder::StatusBuilder(const absl::Status& original_status,
                             absl::SourceLocation location)
    : loc_(location), rep_(InitRep(original_status)) {}

StatusBuilder::operator absl::Status() const& {
  if (rep_ == nullptr) return absl::Status();
  return CreateStatusAndConditionallyLog(loc_, std::make_unique<Rep>(*rep_));
}

StatusBuilder::Rep::Rep(const absl::Status& s) : status(s) {}
StatusBuilder::Rep::Rep(absl::Status&& s) : status(std::move(s)) {}
StatusBuilder::Rep::~Rep() {}

StatusBuilder::Rep* StatusBuilder::InitRepImpl(absl::Status s) {
  if (s.ok()) {
    return nullptr;
  } else {
    return new Rep(std::move(s));
  }
}

StatusBuilder::Rep::Rep(const Rep& r)
    : status(r.status),
      logging_mode(r.logging_mode),
      log_severity(r.log_severity),
      verbose_level(r.verbose_level),
      n(r.n),
      period(r.period),
      stream_message(r.stream_message),
      sink(r.sink),
      message_join_style(r.message_join_style),
      should_log_stack_trace(r.should_log_stack_trace),
      also_send_to_log(r.also_send_to_log) {
  if (r.stream.has_value()) {
    InitStream();
  }
}

void StatusBuilder::Rep::InitStream() { stream.emplace(stream_message); }

absl::Status JoinMessageToStatus(absl::Status s, absl::string_view msg,
                                 MessageJoinStyle style) {
  if (msg.empty()) return s;
  if (style == MessageJoinStyle::kAnnotate) {
    return Annotate(std::move(s), msg);
  }
  std::string new_msg = style == MessageJoinStyle::kPrepend
                            ? absl::StrCat(msg, s.message())
                            : absl::StrCat(s.message(), msg);
  absl::Status result = util::SetMessage(s, new_msg);
  SetCanonicalCode(s.code(), &result);
  return result;
}

void StatusBuilder::ConditionallyLog(const absl::Status& status,
                                     absl::SourceLocation loc, const Rep& rep) {
  if (rep.logging_mode == Rep::LoggingMode::kDisabled) return;

  base_logging::LogSeverity severity = rep.log_severity;
  switch (rep.logging_mode) {
    case Rep::LoggingMode::kDisabled:
    case Rep::LoggingMode::kLog:
      break;
    case Rep::LoggingMode::kVLog: {
      // Combine these into a single struct so that we only have one atomic
      // access on each pass through the function (instead of one for the map
      // and one for the mutex).
      struct LogSites {
        absl::Mutex mutex;
        std::unordered_map<const void*, absl::log_internal::VLogSite>
            sites_by_file ABSL_GUARDED_BY(mutex);
      };
      static auto* vlog_sites = new LogSites();

      vlog_sites->mutex.lock();
      // This assumes that loc.file_name() is a compile time constant in order
      // to satisfy the lifetime constraints imposed by VLogSite. The
      // constructors of SourceLocation guarantee that for us.
      auto [iter, unused] = vlog_sites->sites_by_file.try_emplace(
          loc.file_name(), loc.file_name());
      auto& site = iter->second;
      vlog_sites->mutex.unlock();

      if (!site.IsEnabled(rep.verbose_level)) {
        return;
      }

      severity = base_logging::INFO;
      break;
    }
    case Rep::LoggingMode::kLogEveryN: {
      struct LogSites {
        absl::Mutex mutex;
        absl::flat_hash_map<std::pair<const void*, uint>, uint>
            counts_by_file_and_line ABSL_GUARDED_BY(mutex);
      };
      static auto* log_every_n_sites = new LogSites();

      log_every_n_sites->mutex.lock();
      const uint count =
          log_every_n_sites
              ->counts_by_file_and_line[{loc.file_name(), loc.line()}]++;
      log_every_n_sites->mutex.unlock();

      if (count % rep.n != 0) {
        return;
      }
      break;
    }
    case Rep::LoggingMode::kLogEveryPeriod: {
      struct LogSites {
        absl::Mutex mutex;
        absl::flat_hash_map<std::pair<const void*, uint>, absl::Time>
            next_log_by_file_and_line ABSL_GUARDED_BY(mutex);
      };
      static auto* log_every_sites = new LogSites();

      const auto now = absl::Now();
      absl::MutexLock lock(log_every_sites->mutex);
      absl::Time& next_log =
          log_every_sites
              ->next_log_by_file_and_line[{loc.file_name(), loc.line()}];
      if (now < next_log) {
        return;
      }
      next_log = now + rep.period;
      break;
    }
  }

  absl::LogSink* const sink = rep.sink;
  const bool also_send_to_log = rep.also_send_to_log;
  const int verbose_level = rep.logging_mode == Rep::LoggingMode::kVLog
                                ? rep.verbose_level
                                : absl::LogEntry::kNoVerbosityLevel;

  // Use a lambda instead of defining and moving a string to avoid a false
  // positive use-after-move finding (http://b/493875031).
  auto make_msg = [&] {
    return rep.should_log_stack_trace ? absl::StrCat("\n", CurrentStackTrace())
                                      : "";
  };

  // Avoid calling ToSinkAlso or ToSinkOnly if we don't have a sink, since their
  // arguments are non-nullable (See
  // https://github.com/abseil/abseil-cpp/log/internal/log_message.h;l=121-125;rcl=883256961).
  if (sink == nullptr) {
    // We can't log exclusively to a sink if we don't have one.
    if (!also_send_to_log) {
      return;
    }

    LOG(LEVEL(severity)).AtLocation(loc).WithVerbosity(verbose_level)
        << status.ToString(absl::StatusToStringMode::kWithEverything)
        << make_msg();
    return;
  }

  // If specified, log to the default global log sinks, in addition to the
  // supplied sink.
  if (also_send_to_log) {
    LOG(LEVEL(severity))
            .AtLocation(loc)
            .ToSinkAlso(sink)
            .WithVerbosity(verbose_level)
        << status.ToString(absl::StatusToStringMode::kWithEverything)
        << make_msg();
    return;
  }

  // Otherwise, log exclusively to the supplied sink.
  LOG(LEVEL(severity))
          .AtLocation(loc)
          .ToSinkOnly(sink)
          .WithVerbosity(verbose_level)
      << status.ToString(absl::StatusToStringMode::kWithEverything)
      << make_msg();
}

absl::Status StatusBuilder::CreateStatusAndConditionallyLog(
    absl::SourceLocation loc, std::unique_ptr<Rep> rep) {
  if (rep == nullptr) return absl::OkStatus();
  absl::Status result = util::JoinMessageToStatus(
      std::move(rep->status), rep->stream_message, rep->message_join_style);
  ConditionallyLog(result, loc, *rep);
  // Passing in the `loc` last to ensure the sequence of the source locations.
  result.AddSourceLocation(loc);
  return result;
}

std::string StatusBuilder::ToString() const {
  if (rep_ == nullptr) {
    return ::util::StatusToString(absl::OkStatus());
  }

  return util::StatusToString(JoinMessageToStatus(rep_->status,
                                                  rep_->stream_message,
                                                  rep_->message_join_style)
                                  .WithSourceLocation(loc_));
}

std::ostream& operator<<(std::ostream& os, const StatusBuilder& builder) {
  return os << ::util::StatusToString(static_cast<absl::Status>(builder));
}

std::ostream& operator<<(std::ostream& os, StatusBuilder&& builder) {
  return os << ::util::StatusToString(
             static_cast<absl::Status>(std::move(builder)));
}

StatusBuilder AbortedErrorBuilder(absl::SourceLocation location) {
  return util::MakeStatusBuilder(error::ABORTED, location);
}

StatusBuilder AlreadyExistsErrorBuilder(absl::SourceLocation location) {
  return util::MakeStatusBuilder(error::ALREADY_EXISTS, location);
}

StatusBuilder CancelledErrorBuilder(absl::SourceLocation location) {
  return util::MakeStatusBuilder(error::CANCELLED, location);
}

StatusBuilder DataLossErrorBuilder(absl::SourceLocation location) {
  return util::MakeStatusBuilder(error::DATA_LOSS, location);
}

StatusBuilder DeadlineExceededErrorBuilder(absl::SourceLocation location) {
  return util::MakeStatusBuilder(error::DEADLINE_EXCEEDED, location);
}

StatusBuilder FailedPreconditionErrorBuilder(absl::SourceLocation location) {
  return util::MakeStatusBuilder(error::FAILED_PRECONDITION, location);
}

StatusBuilder InternalErrorBuilder(absl::SourceLocation location) {
  return util::MakeStatusBuilder(error::INTERNAL, location);
}

StatusBuilder InvalidArgumentErrorBuilder(absl::SourceLocation location) {
  return util::MakeStatusBuilder(error::INVALID_ARGUMENT, location);
}

StatusBuilder NotFoundErrorBuilder(absl::SourceLocation location) {
  return util::MakeStatusBuilder(error::NOT_FOUND, location);
}

StatusBuilder OutOfRangeErrorBuilder(absl::SourceLocation location) {
  return util::MakeStatusBuilder(error::OUT_OF_RANGE, location);
}

StatusBuilder PermissionDeniedErrorBuilder(absl::SourceLocation location) {
  return util::MakeStatusBuilder(error::PERMISSION_DENIED, location);
}

StatusBuilder UnauthenticatedErrorBuilder(absl::SourceLocation location) {
  return util::MakeStatusBuilder(error::UNAUTHENTICATED, location);
}

StatusBuilder ResourceExhaustedErrorBuilder(absl::SourceLocation location) {
  return util::MakeStatusBuilder(error::RESOURCE_EXHAUSTED, location);
}

StatusBuilder UnavailableErrorBuilder(absl::SourceLocation location) {
  return util::MakeStatusBuilder(error::UNAVAILABLE, location);
}

StatusBuilder UnimplementedErrorBuilder(absl::SourceLocation location) {
  return util::MakeStatusBuilder(error::UNIMPLEMENTED, location);
}

StatusBuilder UnknownErrorBuilder(absl::SourceLocation location) {
  return util::MakeStatusBuilder(error::UNKNOWN, location);
}

}  // namespace util
