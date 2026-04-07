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

#include <cstdlib>
#include <string>

#include "absl/base/internal/raw_logging.h"
#include "absl/flags/flag.h"
#include "absl/flags/marshalling.h"
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
