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

// This defines some convenience functions and operator overloading to make it
// easier to write more natural unit test code for cpu_set_t.

#ifndef THIRD_PARTY_GLOOP_UTIL_OS_CORE_CPU_SET_TEST_UTIL_H_
#define THIRD_PARTY_GLOOP_UTIL_OS_CORE_CPU_SET_TEST_UTIL_H_

#include <cstdint>
#define _GNU_SOURCE 1  // Required as per the man page for CPU_SET.
#include <sched.h>

#include <string>

#include "gloop/util/os/core/cpu_set.h"

// Equality operator. This is the minimum set of interfaces to support:
//   EXPECT_EQ(expected_set, set);
//   EXPECT_EQ(expected_int, set);
// Other interfaces are not provided in order to discourage abuse.
bool operator==(const cpu_set_t& lhs, const cpu_set_t& rhs);
bool operator==(uint64_t lhs, const cpu_set_t& rhs);

// Non equality operator. This is the minimum set of interfaces to support:
//   EXPECT_NE(expected_set, set);
//   EXPECT_NE(expected_int, set);
// Other interfaces are not provided in order to discourage abuse.
bool operator!=(const cpu_set_t& lhs, const cpu_set_t& rhs);
bool operator!=(uint64_t lhs, const cpu_set_t& rhs);

#endif  // THIRD_PARTY_GLOOP_UTIL_OS_CORE_CPU_SET_TEST_UTIL_H_
