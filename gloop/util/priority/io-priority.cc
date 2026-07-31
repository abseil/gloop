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

#include "gloop/util/priority/io-priority.h"

#include <unistd.h>

#include <string>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"
#include "gloop/base/linux_syscall_support.h"
#include "gloop/base/sysinfo.h"

ABSL_FLAG(bool, use_io_priority, true, "DEPRECATED: this flag has no effect.");

namespace util {

ABSL_CONST_INIT static absl::Mutex logging_mutex(absl::kConstInit);

// Returns a string representation of the given priority class.
std::string IOPriorityClassToString(IOPriorityClass priority_class) {
  switch (priority_class) {
    case IOPRIO_CLASS_NONE:
      return "IOPRIO_CLASS_NONE";
    case IOPRIO_CLASS_RT:
      return "IOPRIO_CLASS_RT";
    case IOPRIO_CLASS_BE:
      return "IOPRIO_CLASS_BE";
    case IOPRIO_CLASS_IDLE:
      return "IOPRIO_CLASS_IDLE";
    case IOPRIO_CLASS_INVALID:
      return "IOPRIO_CLASS_INVALID";
  }
  LOG(ERROR) << "Unknown IOPriorityClass: " << priority_class;
  return "IOPRIO_CLASS_UNKNOWN";
}

bool SetProcessIOPriority(IOPriorityClass priority_class, int level, int hint) {
  return SetIOPriority(getpid(), priority_class, level, hint);
}

bool SetIOPriority(pid_t id, IOPriorityClass priority_class, int level,
                   int hint) {
  if ((level < 0) || (level >= IOPRIO_NR_LEVELS)) {
    LOG(ERROR) << "Requested I/O priority " << level << " is out of range [0-"
               << (IOPRIO_NR_LEVELS - 1) << "]";
    return false;
  }

  if (priority_class < IOPRIO_CLASS_NONE ||
      priority_class > IOPRIO_CLASS_IDLE) {
    LOG(ERROR) << "Requested I/O priority class " << priority_class
               << " cannot be used for setting io-priority";
    return false;
  }

  if (hint < 0 || hint >= IOPRIO_NR_HINTS) {
    LOG(ERROR) << "Requested I/O priority hint " << hint
               << " is out of range [0-" << (IOPRIO_NR_HINTS - 1) << "]";
    return false;
  }

  if (sys_ioprio_set(IOPRIO_WHO_PROCESS, id,
                     IOPRIO_PRIO_VALUE_HINT(priority_class, level, hint)) < 0) {
    PLOG(WARNING) << "Failed to set process I/O priority to class "
                  << priority_class << " priority " << level << " hint "
                  << hint;
    if (hint != IOPRIO_HINT_NONE && priority_class == IOPRIO_CLASS_BE) {
      // Linux kernels before v6.5 used all 13 bits of DATA to check for
      // the allowed level to be in the [0..7] range. Since Linux v6.5
      // 13 bits of DATA were split into 10 bits of HINT + 3 bits of level.
      //
      // Setting a hint for IOPRIO_CLASS_BE in pre-v6.5 kernels is invalid:
      //
      //     #define IOPRIO_BE_NR    (8)
      //     ...
      //     int data = IOPRIO_PRIO_DATA(ioprio);
      //     ...
      //        case IOPRIO_CLASS_BE:
      //            if (data >= IOPRIO_BE_NR || data < 0)
      //                return -EINVAL;
      //
      LOG(WARNING) << "Kernel might not have a support for I/O priority hints.";
    }
    return false;
  }

  absl::MutexLock mlock(logging_mutex);
  LOG_FIRST_N(INFO, 32) << "Process " << id << " I/O priority set: class "
                        << priority_class << " level " << level << " hint "
                        << hint;
  return true;
}

bool GetIOPriority(pid_t pid, IOPriorityClass* io_priority_class, int* level,
                   int* hint) {
  if (!io_priority_class && !level && !hint) {
    return true;  // don't need to call the kernel
  }

  const int priority_bits = sys_ioprio_get(IOPRIO_WHO_PROCESS, pid);
  CHECK_NE(priority_bits, -1);

  if (io_priority_class) {
    const int int_class = IOPRIO_PRIO_CLASS(priority_bits);
    // If we were to perform this cast inside the macro above, we might have
    // more work to do when glibc support arrives.  So we'll do it here for now.
    *io_priority_class = static_cast<IOPriorityClass>(int_class);
  }
  if (level) {
    *level = IOPRIO_PRIO_LEVEL(priority_bits);
  }
  if (hint) {
    *hint = IOPRIO_PRIO_HINT(priority_bits);
  }
  return true;
}

void SetThreadIOPriority(int io_class, int level, int hint) {
  SetIOPriority(GetTID(), (IOPriorityClass)io_class, level, hint);
}

int MakeSystemIOPriority(IOPriorityClass io_priority_class, int level,
                         int hint) {
  return IOPRIO_PRIO_VALUE_HINT(io_priority_class, level, hint);
}

int ExtractIOPriorityHint(int system_io_priority) {
  return IOPRIO_PRIO_HINT(system_io_priority);
}

}  // namespace util
