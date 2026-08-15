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

#ifndef THIRD_PARTY_GLOOP_BASE_INTERNAL_LOGGING_DIRECTORIES_H_
#define THIRD_PARTY_GLOOP_BASE_INTERNAL_LOGGING_DIRECTORIES_H_

#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/types/span.h"
#include "gloop/base/config.h"

namespace base_logging {
namespace logging_internal {

// Return the set of directories to try writing a log file into.  Do not call
// before `InitGoogle` or the returned value will not honor flags (e.g.
// --log_dir), and furthermore it will be cached and the flags will not take
// effect when `InitGoogle` is eventually called.
std::vector<std::string> LoggingDirectories();

#if GLOOP_INTERNAL_PROD_LOGGING

// For tests only: clear the internal (cached) list of logging directories to
// force a refresh the next time `GetLoggingDirectories` is called.
void ClearLoggingDirectories();

// For tests only: set the internal (cached) list of logging directories to the
// specified value.
void SetLoggingDirectories(std::vector<std::string> vec);

// Returns the current (cached) list of logging directories.
// This is for signal handlers that can't safely take the underlying lock or
// allocate to return-by-copy, so it's thread-hostile.
absl::Span<const std::string> LoggingDirectoriesUnsafe();

#endif  // GLOOP_INTERNAL_PROD_LOGGING

}  // namespace logging_internal
}  // namespace base_logging

#endif  // THIRD_PARTY_GLOOP_BASE_INTERNAL_LOGGING_DIRECTORIES_H_
