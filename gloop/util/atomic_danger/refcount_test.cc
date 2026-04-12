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

#include "gloop/util/atomic_danger/refcount.h"

#include <cstdint>

#include "gloop/gloop_test.h"

namespace {

TEST(RefCount, Create) {
  atomic_danger::RefCount<int32_t> rc;
  EXPECT_TRUE(rc.IsUnique());
  EXPECT_TRUE(rc.Dec());
}

TEST(RefCount, IncAndIsUnique) {
  atomic_danger::RefCount<int32_t> rc;
  EXPECT_TRUE(rc.IsUnique());

  rc.Inc();
  EXPECT_FALSE(rc.IsUnique());

  EXPECT_FALSE(rc.Dec());
  EXPECT_TRUE(rc.IsUnique());

  EXPECT_TRUE(rc.Dec());
  EXPECT_FALSE(rc.IsUnique());
}

TEST(RefCount, IncAfterLastDec) {
  atomic_danger::RefCount<int32_t> rc;
  EXPECT_TRUE(rc.Dec());

  // Calling Inc() in a debug build should crash, since Dec() has returned
  // true.
  GLOOP_EXPECT_DEBUG_DEATH(rc.Inc(), "");
}

TEST(RefCount, IncIntWithLong) {
  atomic_danger::RefCount<intptr_t> rc;
  rc.Inc(5);
  EXPECT_TRUE(rc.Dec(6));
}

TEST(RefCount, IsZeroAndDec) {
  atomic_danger::RefCount<int> rc;
  EXPECT_FALSE(rc.IsZero());
  EXPECT_TRUE(rc.Dec());
  EXPECT_TRUE(rc.IsZero());
}

TEST(RefCount, DebugStringReturnsString) {
  atomic_danger::RefCount<int> rc;
  // NOTE: Do not write program logic against the return value of DebugString.
  EXPECT_EQ(rc.DebugString(), "1");
}
}  // namespace
