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

#include "gloop/util/hash/legacy_hash_cord.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/strings/cord.h"
#include "absl/strings/cord_test_helpers.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "fuzztest/fuzztest.h"
#include "gloop/util/hash/hash.h"
#include "gloop/util/hash/murmur.h"
#include "gtest/gtest.h"

namespace {

TEST(LegacyHashCord, CordFunctions) {
  static constexpr absl::string_view str = "123456";
  absl::Cord flat_cord("123456");
  absl::Cord fragmented_cord = absl::MakeFragmentedCord({"123", "456"});

  EXPECT_EQ(Fingerprint(str), util_hash::FingerprintCord(flat_cord));
  EXPECT_EQ(Fingerprint(str), util_hash::FingerprintCord(fragmented_cord));
  EXPECT_EQ(Fingerprint(""), util_hash::FingerprintCord(absl::Cord()));

  EXPECT_EQ(HashTo32(str), util_hash::HashCordTo32(flat_cord));
  EXPECT_EQ(HashTo32(str), util_hash::HashCordTo32(fragmented_cord));
  EXPECT_EQ(HashTo32(""), util_hash::HashCordTo32(absl::Cord()));
}

void StringHashEquivalentToCordHash(const std::vector<std::string>& fragments) {
  const std::string str = absl::StrJoin(fragments, "");
  const absl::Cord cord = absl::MakeFragmentedCord(fragments);
  EXPECT_EQ(HashTo32(str), util_hash::HashCordTo32(cord));
  EXPECT_EQ(Fingerprint(str), util_hash::FingerprintCord(cord));
  EXPECT_EQ(util_hash::MurmurHash64(str), util_hash::MurmurHash64(cord));
}
FUZZ_TEST(LegacyHashCord, StringHashEquivalentToCordHash);

static void GrowCord(absl::Cord* c, int n) {
  for (int i = 0; i < n; i++) {
    c->Append("x");
  }
}

static void BM_Fingerprint(benchmark::State& state) {
  const int arg = state.range(0);
  absl::Cord c;
  GrowCord(&c, arg);
  const size_t n = c.size();
  for (auto _ : state) {
    benchmark::DoNotOptimize(util_hash::FingerprintCord(c));
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * n);
}
BENCHMARK(BM_Fingerprint)->Range(0, 512);

void BM_CordHash(benchmark::State& state) {
  const int arg = state.range(0);
  absl::Cord c;
  GrowCord(&c, arg);
  const size_t n = c.size();
  for (auto _ : state) {
    benchmark::DoNotOptimize(util_hash::HashCordTo32(c));
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * n);
}
BENCHMARK(BM_CordHash)->Range(1, 512);

static void BM_StringHash(benchmark::State& state) {
  const int arg = state.range(0);
  std::string s(arg, 'x');
  const size_t n = s.size();
  for (auto _ : state) {
    benchmark::DoNotOptimize(HashTo32(s));
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * n);
}
BENCHMARK(BM_StringHash)->Range(1, 512);

void BM_CordMurmurHash(benchmark::State& state) {
  const int arg = state.range(0);
  absl::Cord c;
  GrowCord(&c, arg);
  const size_t n = c.size();
  for (auto _ : state) {
    benchmark::DoNotOptimize(util_hash::MurmurHash64(c));
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * n);
}
BENCHMARK(BM_CordMurmurHash)->Range(1, 512);

static void BM_StringMurmurHash(benchmark::State& state) {
  const int arg = state.range(0);
  std::string s(arg, 'x');
  const size_t n = s.size();
  for (auto _ : state) {
    benchmark::DoNotOptimize(util_hash::MurmurHash64(s));
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * n);
}
BENCHMARK(BM_StringMurmurHash)->Range(1, 512);

}  // namespace
