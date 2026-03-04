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

// Copyright 2007 Google Inc.
// All rights reserved.

#include "gloop/util/random/global_id.h"

#include <math.h>
#include <stdio.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <thread>  // NOLINT
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "benchmark/benchmark.h"
#include "gtest/gtest.h"

TEST(GlobalID, MinimalStdThread) {
  const int kNumThreads = 4;
  const int kNumIDsPerThread = 1000;
  std::vector<std::thread> threads;
  std::vector<std::vector<uint64_t>> generated_ids(kNumThreads);

  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&, i] {
      for (int j = 0; j < kNumIDsPerThread; ++j) {
        generated_ids[i].push_back(util::random::NewGlobalID());
      }
    });
  }

  absl::flat_hash_set<uint64_t> all_ids;
  all_ids.reserve(kNumIDsPerThread * kNumThreads * 2);

  for (int i = 0; i < kNumThreads; ++i) {
    threads[i].join();
    for (const auto& id : generated_ids[i]) {
      EXPECT_TRUE(all_ids.insert(id).second)
          << "Duplicate ID generated: " << id;
    }
  }
}

TEST(GlobalID, ExhaustiveAlgorithm) {
  // Mimic the algorithm in global_id.cc.
  //
  // This test demonstrates that the algorithm exhaustively generates all
  // possible lower bits for any odd increment value.
  constexpr int kLog2NumIDsInBatch = 12;
  constexpr int kNumIDsInBatch = 1 << kLog2NumIDsInBatch;
  constexpr int kBatchMask = kNumIDsInBatch - 1;

  std::vector<uint32_t> ids;
  // Repeat the test with *all* possible increments.
  for (int increment = 1; increment < kBatchMask; increment += 2) {
    ids.clear();
    ids.reserve(kNumIDsInBatch);
    uint32_t lower_bits = 0;
    for (int i = 0; i < kNumIDsInBatch; ++i) {
      lower_bits = (lower_bits + increment) & kBatchMask;
      ids.push_back(lower_bits);
    }
    std::sort(ids.begin(), ids.end());
    auto it = std::adjacent_find(ids.begin(), ids.end());
    EXPECT_EQ(it, ids.end()) << "For increment=" << increment;
  }
}

static void BenchGlobalID(benchmark::State& state) {
  for (auto s : state) {
    (void)util::random::NewGlobalID();
  }
}

BENCHMARK(BenchGlobalID);
