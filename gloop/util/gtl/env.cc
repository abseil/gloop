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

#include "gloop/util/gtl/env.h"

#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/thread_annotations.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"

// Unfortunately there is no cross platform header file that declares environ.
extern char** environ;

namespace gtl {

namespace {

ABSL_CONST_INIT absl::Mutex global_env_mutex(absl::kConstInit);

}

std::optional<std::string> GetEnv(absl::string_view name)
    ABSL_LOCKS_EXCLUDED(global_env_mutex) {
  absl::MutexLock l(global_env_mutex);
  // getenv needs a null-terminated parameter.
  const char* value = getenv(std::string(name).c_str());
  return value == nullptr ? std::nullopt
                          : std::make_optional<std::string>(value);
}

bool SetEnv(absl::string_view name, absl::string_view value)
    ABSL_LOCKS_EXCLUDED(global_env_mutex) {
  absl::MutexLock l(global_env_mutex);
  // setenv needs null-terminated parameters.
  return setenv(std::string(name).c_str(), std::string(value).c_str(),
                /* rewrite= */ 1) == 0;
}

bool SetEnvIfUnset(absl::string_view name, absl::string_view value)
    ABSL_LOCKS_EXCLUDED(global_env_mutex) {
  absl::MutexLock l(global_env_mutex);
  // setenv needs null-terminated parameters.
  return setenv(std::string(name).c_str(), std::string(value).c_str(),
                /* rewrite= */ 0) == 0;
}

bool UnsetEnv(absl::string_view name) ABSL_LOCKS_EXCLUDED(global_env_mutex) {
  absl::MutexLock l(global_env_mutex);
  // unsetenv needs a null-terminated parameter.
  return unsetenv(std::string(name).c_str()) == 0;
}

std::vector<std::string> Environ() {
  std::vector<std::string> env;
  absl::MutexLock l(global_env_mutex);
  if (environ != nullptr) {
    for (char** p = environ; *p; p++) {
      env.push_back(*p);
    }
  }
  return env;
}

}  // namespace gtl
