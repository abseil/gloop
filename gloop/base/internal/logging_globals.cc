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

#include "gloop/base/internal/logging_globals.h"

#include <atomic>

#include "absl/base/attributes.h"

namespace base_logging {
namespace logging_internal {
namespace {
// Keeps track of whether Logging flags are initialized.
ABSL_CONST_INIT std::atomic<bool> logging_flags_parsed{false};
}  // namespace

bool LoggingFlagsParsed() {
  return logging_flags_parsed.load(std::memory_order_acquire);
}

void SetLoggingFlagsParsed() {
  logging_flags_parsed.store(true, std::memory_order_release);
}

}  // namespace logging_internal
}  // namespace base_logging
