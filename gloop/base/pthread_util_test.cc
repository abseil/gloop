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

#include "gloop/base/pthread_util.h"

#include <pthread.h>

#include "absl/container/flat_hash_set.h"
#include "gtest/gtest.h"

namespace base {
namespace {

TEST(PthreadUtilTest, GetPthreadNumericId) {
  EXPECT_GT(GetPthreadNumericId(pthread_self()), 0);
  EXPECT_EQ(GetPthreadNumericId(pthread_t{}), 0);
}

TEST(PthreadUtilTest, PthreadHashAndEqual) {
  const pthread_t self = pthread_self();
  const pthread_t zero{};
  EXPECT_TRUE(PthreadEqual{}(self, self));
  EXPECT_FALSE(PthreadEqual{}(self, zero));
  EXPECT_EQ(PthreadHash{}(self), PthreadHash{}(self));

  absl::flat_hash_set<pthread_t, PthreadHash, PthreadEqual> set;
  EXPECT_TRUE(set.insert(self).second);
  EXPECT_FALSE(set.insert(self).second);
  EXPECT_TRUE(set.contains(self));
  EXPECT_FALSE(set.contains(zero));
}

}  // namespace
}  // namespace base
