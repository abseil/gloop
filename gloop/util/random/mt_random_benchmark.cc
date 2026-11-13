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

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "gloop/util/random/mt_random.h"

namespace {

constexpr int kNumStringSeeds = 64;
constexpr int kNumRawSeeds = 4;

std::vector<std::string> MakeStringSeeds(const size_t length) {
  std::vector<std::string> seeds;
  seeds.reserve(kNumStringSeeds);
  for (int i = 0; i < kNumStringSeeds; ++i) {
    std::string seed(length, static_cast<char>('a' + (i % 26)));
    for (size_t pos = 0; pos < length; ++pos) {
      seed[pos] = static_cast<char>((i * 31 + pos * 17 + 1) & 0xff);
    }
    seeds.push_back(std::move(seed));
  }
  return seeds;
}

std::array<std::array<uint32_t, MTRandom::kMTNumWords>, kNumRawSeeds>
MakeRawSeeds() {
  std::array<std::array<uint32_t, MTRandom::kMTNumWords>, kNumRawSeeds> seeds{};
  for (int i = 0; i < kNumRawSeeds; ++i) {
    uint32_t x = 0x9e3779b9u * static_cast<uint32_t>(i + 1);
    for (int j = 0; j < MTRandom::kMTNumWords; ++j) {
      x = x * 1664525u + 1013904223u;
      seeds[i][j] = x;
    }
  }
  return seeds;
}

void BM_ConstructUint32(benchmark::State& state) {
  uint32_t seed = 1;
  for (auto _ : state) {
    seed += 0x9e3779b9u;
    benchmark::DoNotOptimize(seed);
    MTRandom rng(seed);
    benchmark::DoNotOptimize(rng);
  }
}
BENCHMARK(BM_ConstructUint32);

void BM_ConstructUint32AndDraw(benchmark::State& state) {
  const int num_draws = static_cast<int>(state.range(0));
  uint32_t seed = 1;
  for (auto _ : state) {
    seed += 0x9e3779b9u;
    benchmark::DoNotOptimize(seed);
    MTRandom rng(seed);
    uint32_t acc = 0;
    for (int i = 0; i < num_draws; ++i) {
      acc ^= rng.Rand32();
    }
    benchmark::DoNotOptimize(acc);
  }
}
BENCHMARK(BM_ConstructUint32AndDraw)
    ->Arg(1)
    ->Arg(8)
    ->Arg(64)
    ->Arg(624)
    ->Arg(1000);

void BM_ConstructDefault(benchmark::State& state) {
  for (auto _ : state) {
    MTRandom rng;
    benchmark::DoNotOptimize(rng);
  }
}
BENCHMARK(BM_ConstructDefault);

void BM_ConstructDefaultAndDraw1(benchmark::State& state) {
  for (auto _ : state) {
    MTRandom rng;
    const uint32_t value = rng.Rand32();
    benchmark::DoNotOptimize(value);
  }
}
BENCHMARK(BM_ConstructDefaultAndDraw1);

void BM_ConstructString(benchmark::State& state) {
  const size_t length = static_cast<size_t>(state.range(0));
  const std::vector<std::string> seeds = MakeStringSeeds(length);
  size_t idx = 0;
  for (auto _ : state) {
    const absl::string_view seed = seeds[idx];
    idx = (idx + 1) & (kNumStringSeeds - 1);
    MTRandom rng(seed);
    benchmark::DoNotOptimize(rng);
  }
}
BENCHMARK(BM_ConstructString)->Arg(16)->Arg(64)->Arg(4096);

void BM_ConstructStringAndDraw1(benchmark::State& state) {
  const size_t length = static_cast<size_t>(state.range(0));
  const std::vector<std::string> seeds = MakeStringSeeds(length);
  size_t idx = 0;
  for (auto _ : state) {
    const absl::string_view seed = seeds[idx];
    idx = (idx + 1) & (kNumStringSeeds - 1);
    MTRandom rng(seed);
    const uint32_t value = rng.Rand32();
    benchmark::DoNotOptimize(value);
  }
}
BENCHMARK(BM_ConstructStringAndDraw1)->Arg(16)->Arg(64)->Arg(4096);

void BM_ConstructRawArray(benchmark::State& state) {
  const auto seeds = MakeRawSeeds();
  size_t idx = 0;
  for (auto _ : state) {
    const uint32_t* const seed = seeds[idx].data();
    idx = (idx + 1) & (kNumRawSeeds - 1);
    MTRandom rng(seed, MTRandom::kMTNumWords);
    benchmark::DoNotOptimize(rng);
  }
}
BENCHMARK(BM_ConstructRawArray);

void BM_ConstructRawArrayAndDraw1(benchmark::State& state) {
  const auto seeds = MakeRawSeeds();
  size_t idx = 0;
  for (auto _ : state) {
    const uint32_t* const seed = seeds[idx].data();
    idx = (idx + 1) & (kNumRawSeeds - 1);
    MTRandom rng(seed, MTRandom::kMTNumWords);
    const uint32_t value = rng.Rand32();
    benchmark::DoNotOptimize(value);
  }
}
BENCHMARK(BM_ConstructRawArrayAndDraw1);

void BM_ResetUint32(benchmark::State& state) {
  MTRandom rng(1u);
  benchmark::DoNotOptimize(rng.Rand8());
  uint32_t seed = 1;
  for (auto _ : state) {
    seed += 0x9e3779b9u;
    benchmark::DoNotOptimize(seed);
    rng.Reset(seed);
    benchmark::DoNotOptimize(rng);
  }
}
BENCHMARK(BM_ResetUint32);

void BM_ResetUint32AndDraw(benchmark::State& state) {
  const int num_draws = static_cast<int>(state.range(0));
  MTRandom rng(1u);
  uint32_t seed = 1;
  for (auto _ : state) {
    seed += 0x9e3779b9u;
    benchmark::DoNotOptimize(seed);
    rng.Reset(seed);
    uint32_t acc = 0;
    for (int i = 0; i < num_draws; ++i) {
      acc ^= rng.Rand32();
    }
    benchmark::DoNotOptimize(acc);
  }
}
BENCHMARK(BM_ResetUint32AndDraw)->Arg(1)->Arg(8)->Arg(64)->Arg(624)->Arg(1000);

void BM_Rand32(benchmark::State& state) {
  MTRandom rng(12345u);
  for (auto _ : state) {
    const uint32_t value = rng.Rand32();
    benchmark::DoNotOptimize(value);
  }
}
BENCHMARK(BM_Rand32);

void BM_Rand64(benchmark::State& state) {
  MTRandom rng(12345u);
  for (auto _ : state) {
    const uint64_t value = rng.Rand64();
    benchmark::DoNotOptimize(value);
  }
}
BENCHMARK(BM_Rand64);

void BM_Rand8(benchmark::State& state) {
  MTRandom rng(12345u);
  for (auto _ : state) {
    const uint8_t value = rng.Rand8();
    benchmark::DoNotOptimize(value);
  }
}
BENCHMARK(BM_Rand8);

void BM_Clone(benchmark::State& state) {
  MTRandom rng(12345u);
  for (auto _ : state) {
    std::unique_ptr<MTRandom> clone(rng.Clone());
    benchmark::DoNotOptimize(clone);
  }
}
BENCHMARK(BM_Clone);

}  // namespace
