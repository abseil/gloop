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

#ifndef THIRD_PARTY_GLOOP_THREAD_LOGGER_H_
#define THIRD_PARTY_GLOOP_THREAD_LOGGER_H_

#include <stddef.h>

#include "absl/base/log_severity.h"
#include "absl/flags/declare.h"
#include "gloop/base/log_severity.h"

ABSL_DECLARE_FLAG(bool, disable_threaded_logging);

namespace threadlogger {

// Ensure all binary logging and all logging severity levels up to and including
//     max(FLAGS_logbuflevel, max_severity)
// are buffered via a logging thread. May be called more than once; subsequent
// calls have no effect on severity levels already using the logging thread.
extern void EnableThreadedLogging(base_logging::LogSeverity max_severity);

// Maximum log message length handled by threaded logging.
// Larger messages will be silently truncated to this length.
inline constexpr size_t kMaxLogLength = 512 * 1024;

}  // namespace threadlogger

#endif  // THIRD_PARTY_GLOOP_THREAD_LOGGER_H_
