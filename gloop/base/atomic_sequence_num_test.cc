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

// Copyright 2008 Google Inc.
// All rights reserved.

// A test for the atomic counter operations in atomic_sequence_num.h

#include "gloop/base/atomic_sequence_num.h"

#include <cstdint>
#include <thread>  // NOLINT(build/c++11)
#include <vector>

#include "absl/log/check.h"
#include "gtest/gtest.h"

TEST(AtomicSequenceNumber, SingleThreaded) {
  base::SequenceNumber cnt;
  EXPECT_EQ(cnt.GetNext(), 0);
  EXPECT_EQ(cnt.GetNext(), 1);
  EXPECT_EQ(cnt.GetNext(), 2);
}

TEST(AtomicSequenceNumber, StdThreaded) {
  base::SequenceNumber cnt;
  std::vector<std::thread> threads;
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back([&cnt] {
      for (int j = 0; j < 10000; ++j) {
        cnt.GetNext();
      }
    });
  }
  for (auto& t : threads) {
    t.join();
  }
  EXPECT_EQ(cnt.GetNext(), 100000);
}
