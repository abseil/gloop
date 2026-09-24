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

#include "gloop/util/coding/elias_fano/nthbit.h"

#include <cstdint>
#include <vector>

#include "absl/numeric/bits.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "benchmark/benchmark.h"
#include "gtest/gtest.h"

namespace elias_fano {
namespace {

TEST(Nthbit, Simple) {
  for (int i = 0; i != 64; ++i) {
    EXPECT_EQ(i, Nthbit(uint64_t{1} << i, 0)) << i;
  }
  for (int i = 0; i != 32; ++i) {
    EXPECT_EQ(i + 32, Nthbit(0xFFFFFFFF00000000, i)) << i;
  }
  for (int i = 0; i != 32; ++i) {
    EXPECT_EQ(i * 2, Nthbit(0x5555555555555555, i)) << i;
  }
  for (int i = 0; i != 32; ++i) {
    EXPECT_EQ(i * 2 + 1, Nthbit(0xAAAAAAAAAAAAAAAA, i)) << i;
  }
}

// Nthbit() dispatches to the pdep path on any CPU that has a usable one, so on
// those machines the case above never reaches NthbitSlow. Exercise the fallback
// directly so it is covered everywhere rather than only on pre-BMI2 and
// Zen1/Zen2 hardware.
TEST(NthbitSlow, Simple) {
  for (int i = 0; i != 64; ++i) {
    EXPECT_EQ(i, nthbit_internal::NthbitSlow(uint64_t{1} << i, 0)) << i;
  }
  for (int i = 0; i != 32; ++i) {
    EXPECT_EQ(i + 32, nthbit_internal::NthbitSlow(0xFFFFFFFF00000000, i)) << i;
  }
  for (int i = 0; i != 32; ++i) {
    EXPECT_EQ(i * 2, nthbit_internal::NthbitSlow(0x5555555555555555, i)) << i;
  }
  for (int i = 0; i != 32; ++i) {
    EXPECT_EQ(i * 2 + 1, nthbit_internal::NthbitSlow(0xAAAAAAAAAAAAAAAA, i))
        << i;
  }
}

// Pins the two implementations to each other across the whole input space, so
// that a change to either one cannot silently make them disagree.
TEST(Nthbit, AgreesWithNthbitSlow) {
  absl::InsecureBitGen rng;
  for (int trial = 0; trial != 20000; ++trial) {
    const uint64_t x = absl::Uniform<uint64_t>(rng);
    if (x == 0) continue;
    const int n = absl::Uniform(rng, 0, absl::popcount(x));
    EXPECT_EQ(Nthbit(x, n), nthbit_internal::NthbitSlow(x, n))
        << "x=" << x << " n=" << n;
  }
}

void BM_Nthbit(benchmark::State& state) {
  const int size = 1 << 20;
  std::vector<uint64_t> xx(size);
  std::vector<int> nn(size);
  absl::InsecureBitGen rng;
  for (int i = 0; i < size; ++i) {
    xx[i] = absl::Uniform<uint64_t>(rng);
    nn[i] = absl::Uniform(rng, 0, absl::popcount(xx[i]));
  }

  while (state.KeepRunningBatch(size)) {
    for (int i = 0; i < size; ++i) {
      benchmark::DoNotOptimize(Nthbit(xx[i], nn[i]));
    }
  }
}
BENCHMARK(BM_Nthbit);

}  // namespace
}  // namespace elias_fano
