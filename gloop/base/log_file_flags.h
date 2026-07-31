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

#ifndef THIRD_PARTY_GLOOP_BASE_LOG_FILE_FLAGS_H_
#define THIRD_PARTY_GLOOP_BASE_LOG_FILE_FLAGS_H_

#include <string>

#include "absl/flags/declare.h"
#include "gloop/base/config.h"

// If specified, or if the `GOOGLE_LOG_DIR` environment variable is set,
// logfiles are written into this directory instead of the default logging
// directory.
ABSL_DECLARE_FLAG(std::string, log_dir);

#if GLOOP_INTERNAL_PROD_LOGGING

// When logging a new message, force a flush if any buffered messages are older
// than this. NOTE: this does *not* imply buffered log messages get flushed
// after this many seconds, or ever -- they are not flushed unless a new log
// message arrives.
ABSL_DECLARE_FLAG(int, logbufsecs);

// Sets the maximum log file size in MB.  Defaults to 200 MB unless the
// `GOOGLE_MAX_LOG_MB` environment variable is set.
ABSL_DECLARE_FLAG(int, max_log_size);

// If true, logging will cease if the disk appears to be full.  Defaults to
// false.
ABSL_DECLARE_FLAG(bool, stop_logging_if_full_disk);

#endif  // GLOOP_INTERNAL_PROD_LOGGING

// If specified, a symbolic link to each logfile is put in this directory.
ABSL_DECLARE_FLAG(std::string, log_link);

// If true, log messages go to stderr *instead* of `LogSink`s.  Defaults to
// false unless the `GOOGLE_LOGTOSTDERR` environment variable is set.
ABSL_DECLARE_FLAG(bool, logtostderr);

// If true, log messages go to stderr in *addition* to `LogSink`s.  Defaults to
// false unless the `GOOGLE_ALSOLOGTOSTDERR` environment variable is set.
ABSL_DECLARE_FLAG(bool, alsologtostderr);

// Log messages at or below this severity level are buffered.  Other messages
// are flushed immediately.  Defaults to `INFO`.  See log_severity.h for numeric
// values of severity levels.
ABSL_DECLARE_FLAG(int, logbuflevel);

// If true and //thread is linked into the binary (true for most google3
// builds), low-severity log messages are written to disk lazily by a background
// thread to avoid blocking.  The severity threshold is specified by
// --logbuflevel. Defaults to true.
ABSL_DECLARE_FLAG(bool, threaded_logging);

// Internal flag-related functionality needed by Abseil in google3-only code.
namespace base_logging {
namespace internal {

#if GLOOP_INTERNAL_PROD_LOGGING
constexpr bool kDefaultLogtostderr = false;
#else
constexpr bool kDefaultLogtostderr = true;
#endif

bool LogtostderrDefault();
bool AlsologtostderrDefault();

}  // namespace internal
}  // namespace base_logging

#endif  // THIRD_PARTY_GLOOP_BASE_LOG_FILE_FLAGS_H_
