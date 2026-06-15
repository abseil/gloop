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

// -----------------------------------------------------------------------------
// File: logging.h
// -----------------------------------------------------------------------------
//
// NOTE: To get the `LOG` family of macros, you should now include
// absl/log/log.h.
// NOTE: To get the `CHECK` family of macros, you should now include
// absl/log/check.h.
//
//
// See Also:
//
// * `class LogSink` in absl/log/log_sink.h
// * `class LogEntry` in absl/log/log_entry.h
// * `class Logger` in logger.h
// *  Miscellaneous google3 extensions in logging_extensions.h
//
// TODO: provide guidance for defaults and configuration of sinks for
// Abseil.
//
// logging_extensions.h documents the default behavior of logging in production
// and provides some public functions and flags for changing that behavior.  It
// also contains some google3-only extensions that apply to both production and
// non-production platforms.

#ifndef THIRD_PARTY_GLOOP_BASE_LOGGING_H_
#define THIRD_PARTY_GLOOP_BASE_LOGGING_H_

// IWYU pragma: begin_exports
// IWYU pragma: begin_keep
#include "absl/base/log_severity.h"
#include "absl/log/check.h"
#include "absl/log/die_if_null.h"
#include "absl/log/flags.h"
#include "absl/log/globals.h"
#include "absl/log/log.h"
// IWYU pragma: end_keep
// IWYU pragma: end_exports

namespace base_logging {

// Initializes the logging library.
//
// `base_logging::Initialize()` must be called only once, and only after flags
// have been parsed.
//
// Ordinarily `base_logging::Initialize()` is called from `InitGoogle()`,
// so programs that call `InitGoogle()` do not need to call it directly.
//
// Programs that do not call `InitGoogle()` can call this to initialize
// the logging library. If the returned function pointer is non-null, the
// caller must call it to complete the initialization of the logging library:
//
// auto callback = base_logging::Initialize();
// if (callback != nullptr) {
//   (*callback)();
// }
typedef void (*InitializeCallback)();
InitializeCallback Initialize();

}  // namespace base_logging

#endif  // THIRD_PARTY_GLOOP_BASE_LOGGING_H_
