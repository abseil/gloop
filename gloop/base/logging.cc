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

#include "gloop/base/logging.h"

#include <stddef.h>
#include <stdint.h>

#include <string>

#include "absl/base/attributes.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/base/internal/strerror.h"
#include "absl/base/log_severity.h"
#include "absl/flags/flag.h"
#include "absl/log/flags.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/internal/flags.h"
#include "absl/log/log.h"
#include "absl/log/log_sink_registry.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "gloop/base/commandlineflags.h"
#include "gloop/base/config.h"
#include "gloop/base/internal/logging.h"
#include "gloop/base/internal/logging_directories.h"
#include "gloop/base/internal/logging_globals.h"
#include "gloop/base/log_file.h"
#include "gloop/base/logging_extensions.h"

extern "C" {
ABSL_ATTRIBUTE_WEAK base_logging::InitializeCallback
InitializeRemoteDebugLogging() {
  return nullptr;
}
}  // extern "C"

namespace base_logging {

InitializeCallback Initialize() {
  // First do google3 specific initialization.

#if BASE_HAVE_PROCESS_STATE
  ABSL_RAW_CHECK(
      logging_internal::LoggingFlagsParsed(),
      "base_logging::Initialize() must be called after flag parsing");
#endif

  // Initialize remote debug logging, if it is linked in and enabled (otherwise
  // this does nothing). The return value is returned to our caller and will
  // be called at the end of InitGoogle, unless it is null.
  InitializeCallback end_of_init_google_init = InitializeRemoteDebugLogging();

#if GLOOP_INTERNAL_PROD_LOGGING
  base_logging::InitializeLogFileSinks();
#endif  // GLOOP_INTERNAL_PROD_LOGGING

  if ((base::WasPresentOnCommandLine("logtostderr") ||
       base::WasPresentOnCommandLine("alsologtostderr")) &&
      base::WasPresentOnCommandLine("stderrthreshold")) {
    absl::SetStderrThreshold(static_cast<absl::LogSeverityAtLeast>(
        absl::GetFlag(FLAGS_stderrthreshold)));
  }

  // Finally initialize OSS Abseil Logging library.
  absl::InitializeLog();

  static auto* counter_log_sink = new logging_internal::MessageCounterSink;
  absl::AddLogSink(counter_log_sink);

  return end_of_init_google_init;
}

}  // namespace base_logging

// Register a logging callback for ABSL_INTERNAL logging.
static struct Google3AbseilInternalLog final {
  Google3AbseilInternalLog() {
    absl::raw_log_internal::RegisterInternalLogFunction(
        Google3AbseilInternalLog::Hook);
  }

  static void Hook(absl::LogSeverity severity, const char* file, int line,
                   const std::string& message) {
    LOG(LEVEL(severity)).AtLocation(file, line) << message;
  }
} abseil_internal_log_hook;
