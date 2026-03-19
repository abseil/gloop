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

#ifndef THIRD_PARTY_GLOOP_UTIL_STATUS_STATUS_OSS_SHIM_H_
#define THIRD_PARTY_GLOOP_UTIL_STATUS_STATUS_OSS_SHIM_H_

// TODO: Remove this once codes.proto has been released to Gloop.
// This is copied verbatim from protobuf gencode as a temporary measure.
namespace util {
namespace error {
enum Code : int {
  OK = 0,
  CANCELLED = 1,
  UNKNOWN = 2,
  INVALID_ARGUMENT = 3,
  DEADLINE_EXCEEDED = 4,
  NOT_FOUND = 5,
  ALREADY_EXISTS = 6,
  PERMISSION_DENIED = 7,
  UNAUTHENTICATED = 16,
  RESOURCE_EXHAUSTED = 8,
  FAILED_PRECONDITION = 9,
  ABORTED = 10,
  OUT_OF_RANGE = 11,
  UNIMPLEMENTED = 12,
  INTERNAL = 13,
  UNAVAILABLE = 14,
  DATA_LOSS = 15,
  DO_NOT_USE_RESERVED_FOR_FUTURE_EXPANSION_USE_DEFAULT_IN_SWITCH_INSTEAD_ = 20,
};

inline constexpr Code Code_MIN =
    static_cast<Code>(0);
inline constexpr Code Code_MAX =
    static_cast<Code>(20);
inline bool Code_IsValid(int value) {
  return 0 <= value && value <= 20 && ((1179647u >> value) & 1) != 0;
}

}  // namespace error
}  // namespace util
namespace google::protobuf {
template <>
struct is_proto_enum<::util::error::Code> : std::true_type {};

}  // namespace google::protobuf

// TODO Remove this once base_logging has been moved to Gloop.
namespace base_logging {

// Variables of type `LogSeverity` are widely taken to be one of the values in
// `LogSeverities()`.  Be careful to preserve this assumption if you ever need
// to change their values.
typedef int LogSeverity;

// Severity-level definitions.  These are C++ constants representing the four
// severity levels `INFO` through `FATAL`.
//
// These severity constants can be used outside of a `LOG` statement, or in a
// variable-severity statement such as:
//
//   LOG(LEVEL(is_error ? base_logging::ERROR : base_logging::WARNING)) << ...;
//
// Another common use of these symbols is:
//
//   using base_logging::INFO;
//   testing::ScopedMockLog log;
//   EXPECT_CALL(log, Log(INFO, testing::_, "..."));
constexpr LogSeverity INFO = static_cast<LogSeverity>(absl::LogSeverity::kInfo);
constexpr LogSeverity WARNING =
    static_cast<LogSeverity>(absl::LogSeverity::kWarning);
constexpr LogSeverity ERROR =
    static_cast<LogSeverity>(absl::LogSeverity::kError);
constexpr LogSeverity FATAL =
    static_cast<LogSeverity>(absl::LogSeverity::kFatal);

// Returns an iterable of all standard `LogSeverity` values, ordered from least
// to most severe.
constexpr std::array<LogSeverity, 4> LogSeverities() {
  return {{INFO, WARNING, ERROR, FATAL}};
}

// Deprecated; prefer `LogSeverities().size()`.
constexpr int NUM_SEVERITIES = static_cast<int>(LogSeverities().size());

// Use `base_logging::DFATAL` to refer to the `LogSeverity` level `DFATAL`
// outside of a `LOG` statement.  Avoid ODR-using this variable as it has
// internal linkage and thus distinct storage in different TUs.
#ifdef NDEBUG
static constexpr LogSeverity DFATAL =
    static_cast<LogSeverity>(absl::LogSeverity::kError);
#else
static constexpr LogSeverity DFATAL =
    static_cast<LogSeverity>(absl::LogSeverity::kFatal);
#endif

// Values less than `INFO` normalize to `INFO`; values greater than `FATAL`
// normalize to `ERROR` (**NOT** `FATAL`).
constexpr LogSeverity NormalizeSeverity(LogSeverity s) {
  return static_cast<LogSeverity>(
      absl::NormalizeLogSeverity(static_cast<absl::LogSeverity>(s)));
}
constexpr LogSeverity NormalizeSeverity(absl::LogSeverity s) {
  return static_cast<LogSeverity>(absl::NormalizeLogSeverity(s));
}

}  // namespace base_logging

#endif  // THIRD_PARTY_GLOOP_UTIL_STATUS_STATUS_OSS_SHIM_H_
