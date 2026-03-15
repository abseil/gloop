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

// Defines conversion from error numbers to canonical status codes and Status
// objects.
//
// Use of this API should be rare; most code should use higher level
// abstractions above platform specific functionality.
//
// Error numbers are held in the `errno` variable after calling certain platform
// APIs (typically those derived from the Unix/C/POSIX heritage), or by using
// the symbolic constants defined in <errno.h> and <cerrno>.
//
// See https://en.cppreference.com/w/cpp/error/errno and
// https://en.cppreference.com/w/cpp/error/errno_macros for the C++
// specifications. The "error number" API is also part of C and POSIX standards,
// and platforms extend the error set. Availability of the symbolic constants,
// and the APIs that set `errno`, will vary by platform.
//
// Given an error number, one can obtain a ::util::error::Code with the same or
// closely related semantics, such as retriability.
//
//  e.g.: ErrnoToCanonicalCode(EAGAIN) // => ::util::error::Unavailable
//
// For existing users of PosixErrorToStatus, a drop-in replacement is provided.
//
//  e.g.: ErrnoToCanonicalStatus(EAGAIN, "failed to connect")
//
// The differences between PosixErrorToStatus and ErrnoToCanonicalStatus are:
// - PosixErrorToStatus returns a Status with a custom error space, while
//   ErrnoToCanonicalStatus returns a Status in the canonical space.
// - PosixErrorToStatus only supports Linux-derived operating systems,
//   while ErrnoToCanonicalStatus also supports other UNIX, such as macOS.
// - PosixErrorToStatus preserves the original error number, even through
//   serialization, while ErrnoToCanonicalStatus discards the error number
//   and preserves only its canonical code.

#ifndef THIRD_PARTY_GLOOP_UTIL_STATUS_ERRNO_MAPPING_H_
#define THIRD_PARTY_GLOOP_UTIL_STATUS_ERRNO_MAPPING_H_

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/source_location.h"
#include "gloop/util/status/status.h"
#include "gloop/util/status/status_builder.h"

namespace util {

// Returns the canonical code for `error_number`, which should be an `errno`
// value. See https://en.cppreference.com/w/cpp/error/errno_macros and similar
// references.
::util::error::Code ErrnoToCanonicalCode(int error_number);

// Returns a Status in the canonical space, using a code of
// `ErrnoToCanonicalCode(error_number)`, and a `message` with the result of
// `StrError(error_number)` appended.
absl::Status ErrnoToCanonicalStatus(int error_number,
                                    absl::string_view message);

// Returns a StatusBuilder using a status of
// `ErrnoToCanonicalStatus(error_number, message)` and `location`.
StatusBuilder ErrnoToCanonicalStatusBuilder(
    int error_number, absl::string_view message,
    absl::SourceLocation location = absl::SourceLocation::current());

}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_STATUS_ERRNO_MAPPING_H_
