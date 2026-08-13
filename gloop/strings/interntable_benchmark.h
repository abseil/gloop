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

#ifndef THIRD_PARTY_GLOOP_STRINGS_INTERNTABLE_BENCHMARK_H_
#define THIRD_PARTY_GLOOP_STRINGS_INTERNTABLE_BENCHMARK_H_

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace strings {

using RandomEngine = std::mt19937;

// Set each element of strings to a non-empty random string.  Also, set each
// element of indices to a random index into strings.  The indices are chosen
// so that XX% of the strings get (100 - XX)% of the activity.
struct InternTableBenchmark {
  std::vector<std::string> strings;
  std::vector<int> indices;

  InternTableBenchmark(int32_t random_seed, int num_strings, int num_indices,
                       double hot_fraction)
      : strings(num_strings), indices(num_indices) {
    RandomEngine rng(random_seed);
    const int kMinStringLength = 10;
    const int kMaxStringLength = 80;
    std::uniform_int_distribution<int> random_to_1001(0, 1000);
    // Avoid embedded '\0' to ensure c_str() does nothing surprising.
    std::uniform_int_distribution<int> random_uint8(0x1, 0xFF);
    // Construct some strings.  Favor the middle of the range of allowed
    // lengths.
    for (size_t i = 0; i < strings.size(); ++i) {
      int length = random_to_1001(rng) + random_to_1001(rng);
      length = length / 2000.0 * (kMaxStringLength - kMinStringLength) +
               kMinStringLength;
      while (strings[i].size() < length) {
        strings[i].push_back(static_cast<char>(random_uint8(rng)));
      }
    }
    // Construct an array that indicates which strings to use.
    int first_cold_index = strings.size() * (1.0 - hot_fraction);
    std::uniform_real_distribution<double> random_fraction(0.0, 1.0);
    for (size_t i = 0; i < indices.size(); i++) {
      int lo = first_cold_index;
      int hi = strings.size();
      if (random_fraction(rng) < hot_fraction) {
        lo = 0;
        hi = first_cold_index;
      }
      indices[i] = std::uniform_int_distribution<int>(lo, hi - 1)(rng);
    }
  }
};

// The "ManyTableSizes" benchmarks use many table sizes in an attempt to test
// different hash table load factors.  Benchmarks that use one size can be
// unrealistic in a sense: e.g., they might only exercise the load=0.5 case if
// the size is a power of two and the underlying hash table forces the bucket
// count to be a power of two.
extern const int kMinForManyTableSizes;
extern const float kMultiplierForManyTableSizes;
extern const int kMaxForManyTableSizes;

}  // namespace strings

#endif  // THIRD_PARTY_GLOOP_STRINGS_INTERNTABLE_BENCHMARK_H_
