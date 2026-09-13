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

// Stub implementation of io-priority.h, used to make it compile but be a no-op
// on non-Linux systems.

#include <string>

#include "absl/flags/flag.h"
#include "gloop/util/priority/io-priority.h"

ABSL_FLAG(bool, use_io_priority, false, "DEPRECATED: this flag has no effect.");

namespace util {

bool SetIOPriority(pid_t pid, IOPriorityClass io_priority_class, int level,
                   int hint) {
  return true;
}

bool SetProcessIOPriority(IOPriorityClass priority_class, int level, int hint) {
  return true;
}

void SetThreadIOPriority(int io_class, int io_priority_level, int hint) {}

bool GetIOPriority(pid_t pid, IOPriorityClass* io_priority_class, int* level,
                   int* hint) {
  return true;
}

int MakeSystemIOPriority(IOPriorityClass io_priority_class, int level,
                         int hint) {
  return 0;
}

int ExtractIOPriorityHint(int system_io_priority) { return 0; }

std::string IOPriorityClassToString(IOPriorityClass priority_class) {
  return "";
}

}  // namespace util
