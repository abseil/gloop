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

#include "gloop/base/log_file_flags.h"

#include <algorithm>
#include <cstdlib>
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/log_severity.h"
#include "absl/flags/flag.h"
#include "absl/flags/marshalling.h"
#include "absl/log/globals.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"

namespace {

template <typename T>
T GetFromEnv(const char* varname, T dflt) {
  const char* val = ::getenv(varname);
  if (val != nullptr) {
    std::string err;
    ABSL_INTERNAL_CHECK(absl::ParseFlag(val, &dflt, &err), err.c_str());
  }
  return dflt;
}

// Deduces the value of stderr threshold, based on the value of flags
// FLAGS_logtostderr
// FLAGS_alsologtostderr
// `turning_on_off` indicates that we are deducing the threshold, while turning
// above flags on or off. The deduction logic differs in these cases, since
// flags may start to contradict each other.
void DeduceStderrThreshold(bool turning_on_off) {
  // Turning on case
  // set threshold to INFO
  if (turning_on_off) {
    absl::log_internal::RawSetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
    return;
  }
  // Turning off case
  // if flags contradict each other, keep current threshold
  // otherwise set threshold to at least ERROR.
  if (!absl::GetFlag(FLAGS_logtostderr) &&
      !absl::GetFlag(FLAGS_alsologtostderr)) {
    absl::log_internal::RawSetStderrThreshold(
        (std::max)(absl::LogSeverityAtLeast::kError, absl::StderrThreshold()));
  }
}

const char* DefaultLogDir() {
  const char* env;
  env = ::getenv("GOOGLE_LOG_DIR");
  if (env != nullptr && env[0] != '\0') {
    return env;
  }
  env = ::getenv("TEST_TMPDIR");
  if (env != nullptr && env[0] != '\0') {
    return env;
  }
  // If log_dir is still empty here,
  // see base_logging::logging_internal::InitLoggingDirectories,
  // which will ultimately select a directory from
  // base_internal::TempDirectories().
  return "";
}
}  // namespace

namespace base_logging {
namespace internal {

bool LogtostderrDefault() {
  static bool value = GetFromEnv("GOOGLE_LOGTOSTDERR", kDefaultLogtostderr);
  return value;
}

bool AlsologtostderrDefault() {
  static bool value = GetFromEnv("GOOGLE_ALSOLOGTOSTDERR", kDefaultLogtostderr);
  return value;
}

}  // namespace internal
}  // namespace base_logging

ABSL_FLAG(std::string, log_dir, DefaultLogDir(),
          "If specified, logfiles are written into this directory instead of "
          "the default logging directory.");

ABSL_FLAG(int, logbufsecs, 30,
          "When logging a new message, force a flush if any buffered messages "
          "are older than this. NOTE: this does *not* imply buffered log "
          "messages get flushed after this many seconds, or ever -- they "
          "don't, unless a new log message arrives.");
ABSL_FLAG(int, max_log_size, GetFromEnv("GOOGLE_MAX_LOG_MB", 200),
          "Approximate maximum log file size (in MB). A value of 0 will be "
          "silently overridden to 1.");
ABSL_FLAG(bool, stop_logging_if_full_disk, false,
          "Stop attempting to log to disk if the disk is full.");

ABSL_FLAG(std::string, log_link, "",
          "Put additional links to the log files in this directory. Has "
          "limited applicability on non-prod platforms.");

// Define a weak reference to the function in log_file.h to avoid cyclic
// dependencies.  If for whatever reason this code is linked in and that one
// isn't, we simply won't update the underlying log files.
namespace base_logging {
extern ABSL_ATTRIBUTE_WEAK void EnableLogToFiles(bool on_off);
}  // namespace base_logging

ABSL_FLAG(bool, logtostderr, base_logging::internal::LogtostderrDefault(),
          "log messages go to stderr instead of logfiles")
    .OnUpdate([] {
      bool turning_on_off = absl::GetFlag(FLAGS_logtostderr);
      DeduceStderrThreshold(turning_on_off);
      if (base_logging::EnableLogToFiles != nullptr) {
        base_logging::EnableLogToFiles(!turning_on_off);
      }
    });

ABSL_FLAG(bool, alsologtostderr,
          base_logging::internal::AlsologtostderrDefault(),
          "log messages go to stderr in addition to logfiles")
    .OnUpdate([] {
      bool turning_on_off = absl::GetFlag(FLAGS_alsologtostderr);
      DeduceStderrThreshold(turning_on_off);
    });

ABSL_FLAG(int, logbuflevel, static_cast<int>(absl::LogSeverityAtLeast::kInfo),
          "Buffer log messages logged at this level or lower (-1 means don't "
          "buffer; 0 means buffer INFO only; ...). Has limited applicability "
          "on non-prod platforms.")
    .OnUpdate([] {});

ABSL_FLAG(bool, threaded_logging, true,
          "Move logging into separate thread so that application threads do "
          "not get stuck on slow or busy disks. By default this does not "
          "enable threaded logging for severities above WARNING level. The "
          "flag -logbuflevel can be used to enable it for those levels as "
          "well. Has no effect unless //thread has been linked into binary. "
          "Has limited applicability on non-prod platforms.");
