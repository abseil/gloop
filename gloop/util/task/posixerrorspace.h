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

// This package defines the POSIX error space (think 'errno' values).
// NOTE: Despite its name, this error space can capture non-posix errno values
// and as such is actually Linux specific. It should not be used to handle errno
// values which originate from non-Linux systems.
//
// PosixErrorSpace: returns the ErrorSpace*
// PosixErrorToStatus: returns a Status given a code and message.
//
// Prefer //gloop/util/task/errno_mapping.h in new code, unless your code needs
// to discriminate specific `errno` values held in Status values.
//
// Avoid this error space in portable code. Serialized status values in this
// error space are not portable across platforms. POSIX specifies only the
// names of the symbolic constants, not the error numbers themselves.
//
#ifndef THIRD_PARTY_GLOOP_UTIL_TASK_POSIXERRORSPACE_H_
#define THIRD_PARTY_GLOOP_UTIL_TASK_POSIXERRORSPACE_H_

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/source_location.h"
#include "gloop/util/task/status.h"

namespace util {

// Returns the POSIX error space.
//
// Note: for converting errno values to strings, prefer `StrError(errno)`
// over the following incantation: `PosixErrorSpace()->String(errno)`.
// See //gloop/base/strerror.h
const ErrorSpace* PosixErrorSpace();

// Returns a Status in the POSIX error space holding `code` and `message`,
// unless `code == 0`, in which case returns `::absl::OkStatus()`.  The `code`
// should be one of the error codes defined in either <errno.h> or <cerrno>.
//
//  e.g.: return PosixErrorToStatus(ENOSYS, "Not Implemented");
//
// Prefer ErrnoToCanonicalStatus() from //gloop/util/task/errno_mapping.h in
// new code, unless your code needs to discriminate specific `errno` values
// held in Status values.
inline absl::Status PosixErrorToStatus(
    int code, absl::string_view message,
    absl::SourceLocation loc = absl::SourceLocation::current()) {
  return ::util::MakeStatus(PosixErrorSpace(), code, message, loc);
}

}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_TASK_POSIXERRORSPACE_H_
