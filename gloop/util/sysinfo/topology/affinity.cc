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

#include "gloop/util/sysinfo/topology/affinity.h"

#include <sched.h>

#include "absl/log/log.h"
#include "gloop/util/os/core/cpu_set.h"  // IWYU pragma: keep

namespace util {
namespace sysinfo {
namespace topology {

bool SetCPUSet(const cpu_set_t* cpu_set) { return SetCPUSet(0, cpu_set); }

bool SetCPUSet(pid_t pid, const cpu_set_t* cpu_set) {
  if (sched_setaffinity(pid, sizeof(*cpu_set), cpu_set) < 0) {
    PLOG(WARNING) << "sched_setaffinity(" << pid << "), "
                  << "cpu_set: " << *cpu_set;
    return false;
  }
  return true;
}

bool GetCPUSet(cpu_set_t* result_set) { return GetCPUSet(0, result_set); }

bool GetCPUSet(pid_t pid, cpu_set_t* result_set) {
  if (sched_getaffinity(pid, sizeof(*result_set), result_set) < 0) {
    PLOG(WARNING) << "Could not get the cpuset for pid " << pid;
    return false;
  }
  return true;
}

}  // namespace topology
}  // namespace sysinfo
}  // namespace util
