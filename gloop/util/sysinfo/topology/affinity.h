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

#ifndef THIRD_PARTY_GLOOP_UTIL_SYSINFO_TOPOLOGY_AFFINITY_H_
#define THIRD_PARTY_GLOOP_UTIL_SYSINFO_TOPOLOGY_AFFINITY_H_

#include <sched.h>

namespace util {
namespace sysinfo {
namespace topology {

// Restrict the current thread to the set of CPUs given by cpu_set
bool SetCPUSet(const cpu_set_t* cpu_set);
// Get the set of CPUs the current thread is allowed to run on
bool GetCPUSet(cpu_set_t* result_set);

// Restrict a thread or process to the set of CPUs given by cpu_set;
// (a pid of zero affects the current thread).
// returns a true if successful.
bool SetCPUSet(pid_t pid, const cpu_set_t* cpu_set);
// Get the set of CPUs a thread or process is allowed to run on.
// (a pid of zero returns the current thread).
bool GetCPUSet(pid_t pid, cpu_set_t* result_set);

}  // namespace topology
}  // namespace sysinfo
}  // namespace util

#endif  // THIRD_PARTY_GLOOP_UTIL_SYSINFO_TOPOLOGY_AFFINITY_H_
