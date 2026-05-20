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

#include "gloop/thread/exit_timeout_seconds.h"

#include <cstdint>
#include <cstdlib>

#include "absl/flags/flag.h"
#include "gloop/base/commandlineflags.h"

namespace {
// Compute a default timeout to use after `exit()` before `SIGTRAP` is sent.
//
// Processes that get stuck during `exit()` are killed by this module with
// `SIGTRAP` after a configurable timeout.  The default value chosen here should
// ensure that stuck processes will exit after a reasonable time without causing
// flakes under typical usage.
//
// Specifically:
//
// 2.  Sanitizers like ASAN, MSAN, TSAN, and UBSAN can take minutes to complete
//     leak checking and other cleanup activities, so we allow users to override
//     this default for their particular use-case, both by environment variable
//     `GOOGLE_EXIT_TIMEOUT_SECONDS` and by flag `--exit_timeout_seconds`.
int32_t GetDefaultExitTimeoutSeconds() {
  int32_t default_timeout_seconds = 30;

  // Allow users to override the default with an environment variable.
  return Int32FromEnv("GOOGLE_EXIT_TIMEOUT_SECONDS", default_timeout_seconds);
}

}  // namespace

ABSL_FLAG(int32_t, exit_timeout_seconds, GetDefaultExitTimeoutSeconds(),
          "Send a `SIGTRAP` to the main thread if `exit()` takes longer "
          "than this duration in seconds.");
