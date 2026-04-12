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

#include "gloop/util/random/shared_bit_gen.h"

#include <cstdint>
#include <random>

#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "gloop/gloop_test.h"

namespace util_random {
namespace {

TEST(SharedBitGenTest, CompatibleWithDistributionUtils) {
  SharedBitGen bitgen;

  absl::Uniform(bitgen, 0, 100);
  absl::Uniform(bitgen, 0.5, 0.7);
  absl::Poisson<uint32_t>(bitgen);
  absl::Exponential<float>(bitgen);
}

TEST(SharedBitGenTest, CompatibleWithStdDistributions) {
  SharedBitGen bitgen;

  // Cast to void to suppress [[nodiscard]] warnings
  static_cast<void>(std::uniform_int_distribution<uint32_t>(0, 100)(bitgen));
  static_cast<void>(std::uniform_real_distribution<float>()(bitgen));
  static_cast<void>(std::bernoulli_distribution(0.2)(bitgen));
}

template <typename Engine, typename Dist, typename... Args>
void BM_Distribution(benchmark::State& state, Args&&... args) {
  using value_type = typename Dist::result_type;
  Engine rng;
  Dist dis{std::forward<Args>(args)...};
  // Compare the following loop performance:
  for (auto _ : state) {
    auto x = dis(rng);
    benchmark::DoNotOptimize(x);
  }
  state.SetBytesProcessed(sizeof(value_type) * state.iterations());
}

template <typename Engine, typename... Args>
void BM_ConstructGenerate(benchmark::State& state, Args&&... args) {
  // Compare the following loop performance:
  for (auto _ : state) {
    Engine rng;
    benchmark::DoNotOptimize(rng());
  }
}

BENCHMARK_TEMPLATE(BM_Distribution, SharedBitGen,
                   absl::uniform_real_distribution<double>)
    ->ThreadRange(1, 32);
BENCHMARK_TEMPLATE(BM_Distribution, SharedBitGen,
                   absl::uniform_int_distribution<uint64_t>)
    ->ThreadRange(1, 32);

BENCHMARK_TEMPLATE(BM_Distribution, absl::BitGen,
                   absl::uniform_real_distribution<double>);
BENCHMARK_TEMPLATE(BM_Distribution, absl::BitGen,
                   absl::uniform_int_distribution<uint64_t>);

BENCHMARK_TEMPLATE(BM_Distribution, util_random::SharedBitGen,
                   absl::uniform_real_distribution<double>)
    ->ThreadRange(1, 32);
BENCHMARK_TEMPLATE(BM_Distribution, util_random::SharedBitGen,
                   absl::uniform_int_distribution<uint64_t>)
    ->ThreadRange(1, 32);

BENCHMARK_TEMPLATE(BM_ConstructGenerate, SharedBitGen)->ThreadRange(1, 32);
BENCHMARK_TEMPLATE(BM_ConstructGenerate, absl::BitGen)->ThreadRange(1, 32);
BENCHMARK_TEMPLATE(BM_ConstructGenerate, util_random::SharedBitGen)
    ->ThreadRange(1, 32);

}  // namespace
}  // namespace util_random
