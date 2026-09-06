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

#include <cstdint>

#include "gloop/base/process_state.h"
#include "gtest/gtest.h"

// Upper bound for available virtual memory set by LimitPhysicalMemory() is
// less than 2^31. This may break sanitizers, as they need at least 16T
// of virtual memory to operate, so we isolate this in its own test program.
TEST(ProcessState, MemoryLimit) {
  const uint64_t orig_limit = GetPhysicalMemoryLimit();
  EXPECT_EQ(orig_limit, GetPhysicalMemoryLimit());
  LimitPhysicalMemory(0.88, true);
  EXPECT_LT(0, GetPhysicalMemoryLimit());
  EXPECT_GT(orig_limit, GetPhysicalMemoryLimit());
}
