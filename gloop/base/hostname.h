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

#ifndef THIRD_PARTY_GLOOP_BASE_HOSTNAME_H_
#define THIRD_PARTY_GLOOP_BASE_HOSTNAME_H_

#include <string>

#include "absl/base/attributes.h"
#include "absl/strings/string_view.h"

namespace base {
// Returns the system hostname, as returned by `gethostname`.  The form of what
// is returned depends on the platform and on the system's configuration.  In
// particular, it might be a single alphanumeric label, a fully-qualified
// dot-delimited DNS name, or something else.
//
// The hostname is fetched at first access and saved for the lifetime of the
// program, meaning that the returned `absl::string_view` remains valid forever
// and that updates to the system hostname after the first call will not be
// visible to this function.
//
// This function is not async-signal-safe.
absl::string_view Hostname();

}  // namespace base

// TODO: remove this deprecated alias.
ABSL_DEPRECATED("Use base::Hostname().")
const char* Hostname();

#endif  // THIRD_PARTY_GLOOP_BASE_HOSTNAME_H_
