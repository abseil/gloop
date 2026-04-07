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

#include "gloop/base/internal/logging_directories.h"

#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/thread_annotations.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"
#include "gloop/base/internal/temp_directories.h"
#include "gloop/base/log_file_flags.h"

namespace base_logging {
namespace logging_internal {
namespace {
ABSL_CONST_INIT absl::Mutex directories_mutex(absl::kConstInit);
ABSL_CONST_INIT std::vector<std::string>* directories ABSL_GUARDED_BY(
    directories_mutex) ABSL_PT_GUARDED_BY(directories_mutex) = nullptr;

void ResetDirectories(std::vector<std::string>* new_directories = nullptr)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(directories_mutex) {
  std::swap(directories, new_directories);
  delete new_directories;
}

void InitLoggingDirectories() ABSL_EXCLUSIVE_LOCKS_REQUIRED(directories_mutex) {
  ResetDirectories(new std::vector<std::string>);

  std::string log_dir = absl::GetFlag(FLAGS_log_dir);
  if (!log_dir.empty()) {
    // A directory was specified, we should use it
    directories->push_back(std::move(log_dir));
  } else {
    *directories = base::internal::TempDirectories();
  }
}
}  // namespace

void ClearLoggingDirectories() ABSL_LOCKS_EXCLUDED(directories_mutex) {
  absl::MutexLock l(directories_mutex);
  ResetDirectories();
}

void SetLoggingDirectories(std::vector<std::string> vec)
    ABSL_LOCKS_EXCLUDED(directories_mutex) {
  absl::MutexLock l(directories_mutex);
  ResetDirectories(new std::vector<std::string>(std::move(vec)));
}

const std::vector<std::string>& LoggingDirectoriesUnsafe() {
  return *ABSL_TS_UNCHECKED_READ(directories);
}

std::vector<std::string> LoggingDirectories()
    ABSL_LOCKS_EXCLUDED(directories_mutex) {
  absl::MutexLock l(directories_mutex);
  if (!directories) InitLoggingDirectories();
  return *directories;
}

}  // namespace logging_internal
}  // namespace base_logging
