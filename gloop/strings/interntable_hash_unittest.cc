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

// Simple test cases for InternTable.  The
// majority of the work is done by the Arena,
// so there is not much to these.

#include <set>
#include <string>
#include <vector>

#include "absl/container/node_hash_set.h"
#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"
#include "gloop/strings/interntable_benchmark.h"
#include "gtest/gtest.h"

namespace strings {
namespace {

// Fill t with n different strings, and return the set of all
// bucket counts seen before and during that process.
template <typename T>
std::set<int> GetBucketCounts(int n, T* t) {
  CHECK(t->empty());
  std::set<int> result;
  result.insert(t->bucket_count());
  for (int i = 0; i < n; i++) {
    t->insert(absl::StrCat(i));
    result.insert(t->bucket_count());
  }
  CHECK_EQ(t->size(), n);
  return result;
}

// If we add elements to a T (e.g., unordered_set) until it has size n, what
// are the bucket counts?  E.g., for dense_hash_set, n=20, we want something
// like "8, 16, 32."  Use square brackets to indicate a bucket count that would
// only occur starting from the minimal, rather than default, bucket count.
template <typename T>
void SetLabel(int n, benchmark::State& state) {
  T t;
  T t1(1);
  std::set<int> bucket_counts_starting_from_default = GetBucketCounts(n, &t);
  std::set<int> both = GetBucketCounts(n, &t1);
  both.insert(bucket_counts_starting_from_default.begin(),
              bucket_counts_starting_from_default.end());
  std::string label;
  for (int i : both) {
    std::string text = bucket_counts_starting_from_default.count(i) == 0
                           ? absl::StrCat("[", i, "]")
                           : absl::StrCat(i);
    if (label.empty()) {
      absl::StrAppend(&label, text);
    } else {
      absl::StrAppend(&label, ", ", text);
    }
  }
  state.SetLabel(absl::StrCat("bucket counts: ", label));
}

// Similar to InternStringPiece, but with T (e.g., unordered_set) instead.  The
// APIs are different enough that InternTable and T aren't drop-in replacements
// for each other, but the performance comparison is still interesting.
template <typename T>
void BenchmarkInsertString(benchmark::State& state, int num_strings,
                           double hot_fraction) {
  const int kMeanInternsPerString = 10;
  InternTableBenchmark bench(testing::UnitTest::GetInstance()->random_seed(),
                             num_strings, kMeanInternsPerString * num_strings,
                             hot_fraction);
  SetLabel<T>(num_strings, state);
  for (auto _ : state) {
    T s;
    // We could resize() now to improve performance, but for
    // consistency with the InternTable test above, let's not.
    // This is slightly unrealistic, since some real-life code
    // uses resize() up front.  (But perhaps that's uncommon?)
    for (int index : bench.indices) {
      s.insert(bench.strings[index]);
    }
    CHECK_LE(s.size(), num_strings);
  }
}

void HSInsertString(benchmark::State& state, int num_strings,
                    double hot_fraction) {
  BenchmarkInsertString<absl::node_hash_set<std::string>>(state, num_strings,
                                                          hot_fraction);
}

void BM_HSInsertString_90_10(benchmark::State& state) {
  HSInsertString(state, state.range(0), .9);
}
BENCHMARK(BM_HSInsertString_90_10)->Range(1 << 8, 1 << 18);
void BM_HSInsertString_80_20(benchmark::State& state) {
  HSInsertString(state, state.range(0), .8);
}
BENCHMARK(BM_HSInsertString_80_20)->Range(1 << 8, 1 << 18);

}  // namespace
}  // namespace strings
