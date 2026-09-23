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

// Measures the speed of small_map of various sizes and backing
// store types.

#include <stddef.h>

#include <cstdint>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-W#warnings"
#include <ext/hash_map>
#pragma clang diagnostic pop
#include <map>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "benchmark/benchmark.h"
#include "gloop/util/gtl/small_map.h"

static void BM_Map(benchmark::State& state) {
  gtl::small_map<std::map<int32_t, const char*>, 4> src;
  const int size = state.range(0);
  for (int i = 0; i < size; i++) {
    src[i] = "test";
  }

  for (auto _ : state) {
    gtl::small_map<std::map<int32_t, const char*>, 4> dest;
    dest.insert(src.begin(), src.end());
  }
}
BENCHMARK(BM_Map)->Range(1, 256 * 1024);

static void BM_HashMap(benchmark::State& state) {
  gtl::small_map<__gnu_cxx::hash_map<int32_t, const char*>, 4> src;
  const int size = state.range(0);
  for (int i = 0; i < size; i++) {
    src[i] = "test";
  }

  for (auto _ : state) {
    gtl::small_map<__gnu_cxx::hash_map<int32_t, const char*>, 4> dest;
    dest.insert(src.begin(), src.end());
  }
}
BENCHMARK(BM_HashMap)->Range(1, 256 * 1024);

static void BM_FlatHashMap(benchmark::State& state) {
  gtl::small_map<absl::flat_hash_map<int32_t, const char*>, 4> src;
  const int size = state.range(0);
  for (int i = 0; i < size; i++) {
    src[i] = "test";
  }

  for (auto _ : state) {
    gtl::small_map<absl::flat_hash_map<int32_t, const char*>, 4> dest;
    dest.insert(src.begin(), src.end());
  }
}
BENCHMARK(BM_FlatHashMap)->Range(1, 256 * 1024);

static void BM_Find(benchmark::State& state) {
  gtl::small_map<absl::flat_hash_map<int32_t, std::string>, 4> map;
  const int size = state.range(0);
  for (int i = 0; i < size; i++) {
    map[i] = "test";
  }

  while (state.KeepRunningBatch(size)) {
    for (int i = 0; i < size; i++) {
      ::benchmark::DoNotOptimize(i);
      ::benchmark::DoNotOptimize(map.find(i));
    }
  }
}
BENCHMARK(BM_Find)->Range(1, 256 * 1024);

static void BM_Extract(benchmark::State& state) {
  gtl::small_map<absl::flat_hash_map<int32_t, std::string>, 4> map;
  const int size = state.range(0);

  while (state.KeepRunningBatch(size)) {
    state.PauseTiming();
    for (int i = 0; i < size; i++) {
      map[i] = "test";
    }
    state.ResumeTiming();
    for (int i = 0; i < size; i++) {
      ::benchmark::DoNotOptimize(i);
      ::benchmark::DoNotOptimize(map.extract(i));
    }
  }
}
BENCHMARK(BM_Extract)->Range(1, 256 * 1024);

static void BM_ExtractFind(benchmark::State& state) {
  gtl::small_map<absl::flat_hash_map<int32_t, std::string>, 4> map;
  const int size = state.range(0);

  while (state.KeepRunningBatch(size)) {
    state.PauseTiming();
    for (int i = 0; i < size; i++) {
      map[i] = "test";
    }
    state.ResumeTiming();
    for (int i = 0; i < size; i++) {
      ::benchmark::DoNotOptimize(i);
      ::benchmark::DoNotOptimize(map.extract(map.find(i)));
    }
  }
}
BENCHMARK(BM_ExtractFind)->Range(1, 256 * 1024);
