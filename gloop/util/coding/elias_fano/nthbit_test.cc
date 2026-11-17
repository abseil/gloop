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
#include <utility>
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
    const int cnt = absl::popcount(x);
    const int n = absl::Uniform(rng, 0, cnt);
    const uint64_t expected = nthbit_internal::NthbitSlow(x, n);
    EXPECT_EQ(Nthbit(x, n), expected) << "x=" << x << " n=" << n;
    const auto [b, after] = NthbitAndAfter(x, n);
    EXPECT_EQ(b, expected);
    EXPECT_EQ(after, x & ((~uint64_t{1}) << expected));
    if (n + 1 < cnt) {
      ASSERT_NE(after, 0u);
      const uint64_t expected_next = nthbit_internal::NthbitSlow(x, n + 1);
      EXPECT_EQ(static_cast<uint64_t>(absl::countr_zero(after)), expected_next);
      EXPECT_EQ(NthbitNext(x, n + 1, after), expected_next);
    } else {
      EXPECT_EQ(after, 0u);
    }
    if (HasTryDepositNthbit()) {
      EXPECT_EQ(TryDepositNthbit(x, n), uint64_t{1} << expected);
      EXPECT_EQ(TryDepositNthbit(x, cnt), 0u);
    } else {
      EXPECT_EQ(TryDepositNthbit(x, n), 0u);
    }
  }
  // Boundary bit positions for NthbitAndAfter: b == 0, b == 62, and b == 63.
  EXPECT_EQ(NthbitAndAfter(uint64_t{1}, 0),
            (std::pair<uint64_t, uint64_t>{0u, 0u}));
  EXPECT_EQ(NthbitAndAfter(~uint64_t{0}, 0),
            (std::pair<uint64_t, uint64_t>{0u, ~uint64_t{1}}));
  EXPECT_EQ(NthbitAndAfter(uint64_t{1} << 63, 0),
            (std::pair<uint64_t, uint64_t>{63u, 0u}));
  EXPECT_EQ(NthbitAndAfter(uint64_t{3} << 62, 0),
            (std::pair<uint64_t, uint64_t>{62u, uint64_t{1} << 63}));
  EXPECT_EQ(NthbitAndAfter(~uint64_t{0}, 63),
            (std::pair<uint64_t, uint64_t>{63u, 0u}));

  // Boundary cases for TryDepositNthbit: empty mask, n == popcount(x), n >= 64.
  EXPECT_EQ(TryDepositNthbit(0, 0), 0u);
  EXPECT_EQ(TryDepositNthbit(0, 63), 0u);
  EXPECT_EQ(TryDepositNthbit(~uint64_t{1}, 63), 0u);  // 63 set bits, n == 63.
  EXPECT_EQ(TryDepositNthbit(~uint64_t{0}, 64), 0u);  // 64 set bits, n == 64.
  EXPECT_EQ(TryDepositNthbit(~uint64_t{0}, ~uint64_t{0}), 0u);
  if (HasTryDepositNthbit()) {
    EXPECT_EQ(TryDepositNthbit(~uint64_t{1}, 62), uint64_t{1} << 63);
    EXPECT_EQ(TryDepositNthbit(~uint64_t{0}, 63), uint64_t{1} << 63);
    EXPECT_EQ(TryDepositNthbit(uint64_t{1}, 0), uint64_t{1});
    EXPECT_EQ(TryDepositNthbit(uint64_t{1}, 1), 0u);
  }
}

void BM_NthbitSlow(benchmark::State& state) {
  const int size = 1 << 20;
  std::vector<uint64_t> xx(size);
  std::vector<int> nn(size);
  absl::InsecureBitGen rng;
  for (int i = 0; i < size; ++i) {
    xx[i] = absl::Uniform<uint64_t>(rng);
    if (xx[i] == 0) xx[i] = 1;
    nn[i] = absl::Uniform(rng, 0, absl::popcount(xx[i]));
  }

  while (state.KeepRunningBatch(size)) {
    for (int i = 0; i < size; ++i) {
      benchmark::DoNotOptimize(nthbit_internal::NthbitSlow(xx[i], nn[i]));
    }
  }
}
BENCHMARK(BM_NthbitSlow);

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
