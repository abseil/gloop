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

#ifndef THIRD_PARTY_GLOOP_UTIL_GTL_ENV_H_
#define THIRD_PARTY_GLOOP_UTIL_GTL_ENV_H_

// GTL environment functions.
//
// The `gtl::GetEnv()` / `SetEnv()` family of functions are the preferred way
// to read and manipulate the environment in Google.  They have three main
// benefits over the corresponding POSIX functions:
//
// * `GetEnv()` returns an `optional<string>` rather than a `const char*`.
//   This is a more modern coding style and also avoids string lifetime issues
//   (where a call to setenv() on another thread can invalidate the `char*`.)
//
// * All of the GTL `GetEnv()` and `SetEnv()` calls are guarded by a global
//   mutex.  This means that calls to `GetEnv()` and `SetEnv()` will never cause
//   a C++ race. (`GetEnv()` and POSIX `setenv()` can still race, which is one
//   reason we prefer `gtl::SetEnv()` in Google.)
//
// * We retain the ability to write platform-specific implementations of these
//   functions in the case where platforms provide APIs superior to the standard
//   POSIX versions.

#include <optional>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"

namespace gtl {

// Returns the value of environment variable `name`, or `nullopt` if it doesn't
// exist.
//
// Despite being internally synchronized, this function is not thread-safe in
// the presence of other calls to `setenv`.
std::optional<std::string> GetEnv(absl::string_view name);

// Sets environment variable `name` to `value`.  Returns `true` on success and
// `false` on failure (e.g., because `name` is an invalid environment variable
// name).
//
// Despite being internally synchronized, this function is not thread-safe in
// the presence of other calls to `setenv` and `getenv`, where the latter is
// widely used. In practice, setting an environment variable must happen in a
// single-threaded context.
bool SetEnv(absl::string_view name, absl::string_view value);

// Sets environment variable `name` to `value`, but only if an environment
// variable of the given name doesn't already exist.  Returns `false` if the
// system call failed (e.g., because `name` is an invalid environment variable
// name) or `true` otherwise.
//
// Note that this function will return `true` in the case that the environment
// variable was already set and this call was a no-op.
//
// See the note on thread safety in `SetEnv`.
bool SetEnvIfUnset(absl::string_view name, absl::string_view value);

// Removes the variable `name` from the environment.  Returns `true` on success
// and `false` if the system call failed.
//
// Note that this function will return `true` in the case that no environment
// variable with the given name exists.
//
// See the note on thread safety in `SetEnv`.
bool UnsetEnv(absl::string_view name);

// Return the entire environment. See 'man environ' for details as to the format
// of each string.
std::vector<std::string> Environ();

}  // namespace gtl

#endif  // THIRD_PARTY_GLOOP_UTIL_GTL_ENV_H_
