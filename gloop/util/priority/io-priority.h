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

#ifndef THIRD_PARTY_GLOOP_UTIL_PRIORITY_IO_PRIORITY_H_
#define THIRD_PARTY_GLOOP_UTIL_PRIORITY_IO_PRIORITY_H_

#include <sys/types.h>

#include <string>

#include "absl/flags/declare.h"
#include "gloop/base/port.h"

using IOPriorityClass = int;

// DEPRECATED: this flag has no effect
ABSL_DECLARE_FLAG(bool, use_io_priority);

namespace util {

// Convenience function to set I/O priority on process pid.  Valid values for
// 'level' in classes RT and BE are 0-7, 0 is highest.  Only level 0
// is valid for class IDLE.  It is not legal to set to class NONE.
// Returns false on error.
bool SetIOPriority(pid_t pid, IOPriorityClass io_priority_class, int level,
                   int hint = 0 /*IOPRIO_HINT_NONE*/);

// Same as above, but applies to current pid.
bool SetProcessIOPriority(IOPriorityClass priority_class, int level,
                          int hint = 0 /*IOPRIO_HINT_NONE*/);

// Convenience function to set I/O priority on thread tid. If io_class or
// io_priority_level is < 0, this function has no effect.
// The return value is void so it can be used as a closure.
void SetThreadIOPriority(int io_class, int io_priority_level,
                         int hint = 0 /*IOPRIO_HINT_NONE*/);

// Returns I/O priority settings for the given process.  Either output
// parameter may be NULL if you aren't interested in it. Always returns true.
bool GetIOPriority(pid_t pid, IOPriorityClass* io_priority_class, int* level,
                   int* hint = nullptr);

// Returns the OS-level priority corresponding to the given priority class
// and level. Should only be used by low-level code that must directly set
// IO priority via system calls.
int MakeSystemIOPriority(IOPriorityClass io_priority_class, int level,
                         int hint = 0 /*IOPRIO_HINT_NONE*/);

// Extracts the CDL hint from the system IO priority (reverse of
// `MakeSystemIOPriority`).
int ExtractIOPriorityHint(int system_io_priority);

// Converts an IOPriorityClass to a string.
std::string IOPriorityClassToString(IOPriorityClass priority_class);

}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_PRIORITY_IO_PRIORITY_H_
