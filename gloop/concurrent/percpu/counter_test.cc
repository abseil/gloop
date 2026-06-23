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

#include "gloop/concurrent/percpu/counter.h"

#include <cstddef>
#include <memory>
#include <vector>

#include "absl/base/call_once.h"
#include "absl/base/no_destructor.h"
#include "benchmark/benchmark.h"
#include "gloop/base/sysinfo.h"
#include "gloop/thread/threadpool.h"
#include "gtest/gtest.h"

using concurrent::percpu::Counter;

namespace {

TEST(PerCpuCounterTest, SimpleUse) {
  Counter counter;
  EXPECT_EQ(0, counter.value());

  counter.Add(12);
  EXPECT_EQ(12, counter.value());

  counter.Add(-112);
  EXPECT_EQ(-100, counter.value());

  counter.Clear();

  EXPECT_EQ(0, counter.value());
}

TEST(PerCpuCounterTest, CountsAccurately) {
  Counter counter;
  {
    ThreadPool pool(20);

    for (int i = 0; i < pool.num_threads(); ++i) {
      pool.Schedule([&counter]() {
        for (int i = 0; i < 5000; ++i) {
          counter.Add(1);
        }
      });
    }
  }
  EXPECT_EQ(100000, counter.value());
}

TEST(PerCpuCounterTest, Move) {
  static const size_t kNumCtrs = 128;
  std::vector<Counter> ctrs;
  for (int i = 0; i < kNumCtrs; ++i) {
    ctrs.push_back(Counter());
    ctrs.back() = Counter();
    for (auto& c : ctrs) {
      c.Add(1);
    }
  }

  for (int i = 0; i < kNumCtrs; ++i) {
    EXPECT_EQ(kNumCtrs - i, ctrs[i].value());
  }
}

absl::NoDestructor<Counter> c;

void BM_Counter_Add(benchmark::State& state) {
  int i = 0;
  for (auto s : state) {
    c->Add(i++);
    benchmark::DoNotOptimize(c);
  }
}

void BM_Counter_Value(benchmark::State& state) {
  for (auto s : state) {
    benchmark::DoNotOptimize(c->value());
  }
}
BENCHMARK(BM_Counter_Add)->ThreadRange(1, NumCPUs());
BENCHMARK(BM_Counter_Value);

void BM_CounterCreate(benchmark::State& state) {
  for (auto s : state) {
    Counter c;
    benchmark::DoNotOptimize(c);
  }
}
BENCHMARK(BM_CounterCreate);

}  // anonymous namespace
