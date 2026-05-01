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

#ifndef THIRD_PARTY_GLOOP_BASE_RAW_LOGGING_H_
#define THIRD_PARTY_GLOOP_BASE_RAW_LOGGING_H_

// Google3 developers should still #include
// "gloop/base/raw_logging.h". Headers in Abseil internal
// directories are not intended for public use. Some APIs, including this one,
// are still considered public when used within google3, but are not promised to
// be stable in the public Abseil release.

#include "absl/base/internal/raw_logging.h"  // IWYU pragma: export

#ifdef SWIG
%include "absl/base/internal/raw_logging.h"
#endif

// Production google3 specific hook functions for the raw logging system:
namespace base_raw_log {
namespace raw_log_internal {

// Installs google3-specific hooks that modify the behavior of raw_logging.h.
// (This enables tracebacks on crashes, and makes raw logging match the standard
// log format.)  This is safe to call multiple times, doesn't alloc, and is
// async-signal safe.
//
// This function shouldn't be necessary since the hooks are installed in a
// static initializer, but MSVC strips the static initializer when this module
// exports no called functions, even with alwayslink=1. So we hack around this
// by calling it as a no-op in InitGoogle().
void InstallGoogle3Hooks();

}  // namespace raw_log_internal
}  // namespace base_raw_log

#endif  // THIRD_PARTY_GLOOP_BASE_RAW_LOGGING_H_
