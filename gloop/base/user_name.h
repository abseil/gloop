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

#ifndef THIRD_PARTY_GLOOP_BASE_USER_NAME_H_
#define THIRD_PARTY_GLOOP_BASE_USER_NAME_H_

// Functions that provide the user name for the current process or a
// given uid.

#ifndef _WIN32
#include <sys/types.h>
#endif

#include <string>

#include "gloop/base/port.h"  // IWYU pragma: keep

#ifndef _WIN32
// Looks up the name of the user specified by 'uid'.  On error,
// returns the empty string.  Unlike getpwuid(), this is thread-safe.
// Is guaranteed to return "root" for uid 0.
//
// Not available on Windows.
std::string UserName(uid_t uid);
#endif

// Returns the name of the effective user for the current process.
// Equivalent to UserName(geteuid()), but typically much faster.  Note
// that this may not be the LOAS username, which is available from
// LOASUtil::GetDefaultUserNameNonBlocking().
std::string MyUserName();

#endif  // THIRD_PARTY_GLOOP_BASE_USER_NAME_H_
