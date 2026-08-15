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

#ifndef THIRD_PARTY_GLOOP_BASE_LOG_SEVERITY_H_
#define THIRD_PARTY_GLOOP_BASE_LOG_SEVERITY_H_

#include "absl/base/log_severity.h"

namespace base_logging {

typedef int LogSeverity;

constexpr LogSeverity INFO = static_cast<LogSeverity>(absl::LogSeverity::kInfo);
constexpr LogSeverity WARNING =
    static_cast<LogSeverity>(absl::LogSeverity::kWarning);
constexpr LogSeverity ERROR =
    static_cast<LogSeverity>(absl::LogSeverity::kError);
constexpr LogSeverity FATAL =
    static_cast<LogSeverity>(absl::LogSeverity::kFatal);
#ifdef NDEBUG
static constexpr LogSeverity DFATAL =
    static_cast<LogSeverity>(absl::LogSeverity::kError);
#else
static constexpr LogSeverity DFATAL =
    static_cast<LogSeverity>(absl::LogSeverity::kFatal);
#endif
}  // namespace base_logging

#endif  // THIRD_PARTY_GLOOP_BASE_LOG_SEVERITY_H_
