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
#include "gloop/thread/thread.h"
#include "gtest/gtest.h"

static void SampleThreadBody(int period, uint64_t* num_generated,
                             uint64_t* num_sampled) {
  uint64_t g = 0;
  uint64_t s = 0;

  for (int i = 0; i < 65536 * 16; ++i) {
    ++g;
    util::random::ResetPerThreadGlobalIDGenerator();
    uint64_t id = util::random::NewGlobalID();
    if (id % period == 0) {
      ++s;
    }
  }
  *num_generated = g;
  *num_sampled = s;
}

static void RunSampling(int period) {
  const int kNumThreads = 20;
  std::vector<uint64_t> num_generated;
  std::vector<uint64_t> num_sampled;
  std::vector<std::unique_ptr<Thread>> threads;
  num_generated.resize(kNumThreads);
  num_sampled.resize(kNumThreads);

  for (int i = 0; i < kNumThreads; ++i) {
    Thread* thread = new ClosureThread([&, i, period] {
      SampleThreadBody(period, &num_generated[i], &num_sampled[i]);
    });
    thread->SetJoinable(true);
    thread->Start();
    threads.emplace_back(thread);
  }

  uint64_t total_generated = 0, total_sampled = 0;
  for (int i = 0; i < kNumThreads; ++i) {
    threads[i]->Join();
    threads[i].reset();

    total_generated += num_generated[i];
    total_sampled += num_sampled[i];
  }
  absl::PrintF("period=%d (%f %%), sampled=%u/%u %f %%\n", period,
               (1.0 / period) * 100, total_sampled, total_generated,
               static_cast<double>(total_sampled) * 100 / total_generated);

  // Make sure that the actual sample rate is within 5 standard deviations from
  // the expected rate. 5 standard deviations means the test should fail with
  // probability less than 0.00003%. The standard deviation is n*p*(1-p)
  // according to <http://en.wikipedia.org/wiki/Binomial_distribution>.
  double p = 1.0 / period;
  double std_dev = sqrt(total_generated * p * (1 - p));
  EXPECT_GT(total_generated * p + 5 * std_dev, total_sampled);
  EXPECT_LT(total_generated * p - 5 * std_dev, total_sampled);
}

TEST(GlobalID, Sampling) {
  RunSampling(1024);
  RunSampling(128);
  RunSampling(100);
  RunSampling(32);
  RunSampling(10);
}

static void StressThreadBody(int num_ids,
                             std::vector<uint64_t>* generated_ids) {
  for (int i = 0; i < num_ids; ++i) {
    generated_ids->push_back(util::random::NewGlobalID());
  }
}

TEST(GlobalID, Stress) {
  const int kNumIDsPerThread = 65536 * 32;
  const int kNumThreads = 20;
  std::vector<std::vector<uint64_t>> generated_ids;
  std::vector<std::unique_ptr<Thread>> threads;
  generated_ids.resize(kNumThreads);

  for (int i = 0; i < kNumThreads; ++i) {
    Thread* thread = new ClosureThread(
        [&, i] { StressThreadBody(kNumIDsPerThread, &generated_ids[i]); });
    thread->SetJoinable(true);
    thread->Start();
    threads.emplace_back(thread);
  }

  absl::flat_hash_set<uint64_t> all_ids;
  all_ids.reserve(kNumIDsPerThread * kNumThreads * 2);

  for (int i = 0; i < kNumThreads; ++i) {
    threads[i]->Join();
    putchar('.');
    fflush(stdout);
    threads[i].reset();

    for (const auto& id : generated_ids[i]) {
      bool ok = all_ids.insert(id).second;
      CHECK(ok) << id;
    }
    generated_ids[i].clear();
  }
}

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
