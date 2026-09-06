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

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iterator>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "absl/base/internal/cycleclock.h"
#include "absl/log/check.h"
#include "absl/numeric/int128.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "gloop/strings/numbers.h"
#include "gloop/strings/numbers_test_common.h"
#include "gtest/gtest.h"

namespace strings {
namespace {

using RandomEngine = std::minstd_rand0;
using absl::base_internal::CycleClock;

template <typename int_type>
void BM_fast_bufleft(benchmark::State& state) {
  const int inc = state.range(0);
  char buf[kFastToBufferSize];
  int_type x = 0;
  for (auto _ : state) {
    FastIntToBufferLeft(x, buf);
    x += inc;
  }
}
BENCHMARK_TEMPLATE(BM_fast_bufleft, int32_t)->Range(0, 1 << 15);
BENCHMARK_TEMPLATE(BM_fast_bufleft, uint32_t)->Range(0, 1 << 15);
BENCHMARK_TEMPLATE(BM_fast_bufleft, int64_t)->Range(0, 1 << 30);
BENCHMARK_TEMPLATE(BM_fast_bufleft, uint64_t)->Range(0, 1 << 30);

// This benchmark is probably not using data that are representative of
// real-life usage.  It's better than nothing, though.
void BM_AutoDigitStrCmp(benchmark::State& state) {
  static const int kNumStrings = 1000;
  const std::string kPrefix = "abc";
  static const char* parts[] = {"future", "past", ":",  "0",   "1",
                                "00",     "01",   "12", "012", "0012"};
  RandomEngine rng(CycleClock::Now());
  std::uniform_int_distribution<int> random_parts_index(0,
                                                        std::size(parts) - 1);
  std::vector<std::string> v1(kNumStrings), v2(kNumStrings);
  for (int i = 0; i < kNumStrings; i++) {
    v1[i] = absl::StrCat(kPrefix, parts[random_parts_index(rng)],
                         parts[random_parts_index(rng)],
                         parts[random_parts_index(rng)]);
    v2[i] = absl::StrCat(kPrefix, parts[random_parts_index(rng)],
                         parts[random_parts_index(rng)],
                         parts[random_parts_index(rng)]);
  }
  int count = 0, positive = 0;
  while (state.KeepRunningBatch(kNumStrings)) {
    for (size_t i = 0; i < kNumStrings; i++) {
      // Alternate 8 with strict false and 8 with it true.
      bool strict = (count / 8) & 1;
      positive += AutoDigitStrCmp(v1[i], v2[i], strict) > 0;
      ++count;
    }
  }
  CHECK((state.iterations() < 5000) || (positive > 0.4 * state.iterations()))
      << positive << '/' << state.iterations();
}
BENCHMARK(BM_AutoDigitStrCmp);

void BM_AutoDigitStrCmpZ(benchmark::State& state) {
  static const int kNumStrings = 1000;
  const std::string kPrefix = "abc";
  static const char* parts[] = {"future", "past", ":",  "0",   "1",
                                "00",     "01",   "12", "012", "0012"};
  RandomEngine rng(CycleClock::Now());
  std::uniform_int_distribution<int> random_parts_index(0,
                                                        std::size(parts) - 1);
  std::vector<std::string> v1(kNumStrings), v2(kNumStrings);
  for (int i = 0; i < kNumStrings; i++) {
    v1[i] = absl::StrCat(kPrefix, parts[random_parts_index(rng)],
                         parts[random_parts_index(rng)],
                         parts[random_parts_index(rng)]);
    v2[i] = absl::StrCat(kPrefix, parts[random_parts_index(rng)],
                         parts[random_parts_index(rng)],
                         parts[random_parts_index(rng)]);
  }
  int count = 0, positive = 0;
  while (state.KeepRunningBatch(kNumStrings)) {
    for (size_t i = 0; i < kNumStrings; i++) {
      // Alternate 8 with strict false and 8 with it true.
      bool strict = (count / 8) & 1;
      positive += AutoDigitStrCmpZ(v1[i].c_str(), v2[i].c_str(), strict) > 0;
      ++count;
    }
  }
  CHECK((state.iterations() < 5000) || (positive > 0.4 * state.iterations()))
      << positive << '/' << state.iterations();
}
BENCHMARK(BM_AutoDigitStrCmpZ);

void BM_ParseLeading_i32_string(benchmark::State& state) {
  const int digits = state.range(0);
  const int base = state.range(1);
  std::string str1(digits, '7');  // valid in octal, decimal and hex
  std::string parse_string = str1;
  parse_string.append("x");  // Explicitly add non-digit character at end.
  int32_t value = 0;
  int32_t (*test_function)(absl::string_view, int32_t) = nullptr;
  switch (base) {
    case 8:
      test_function = &ParseLeadingInt32Value;
      parse_string.insert(0, "0");  // force octal interpretation
      break;
    case 10:
      test_function = &ParseLeadingDec32Value;
      break;
  }
  for (auto _ : state) {
    value = test_function(parse_string, 0);
  }
  std::string str2;
  ASSERT_TRUE(Itoa(value, base, &str2));
  ASSERT_EQ(str1, str2);
}
BENCHMARK(BM_ParseLeading_i32_string)
    ->ArgPair(1, 8)
    ->ArgPair(1, 10)
    ->ArgPair(2, 8)
    ->ArgPair(2, 10)
    ->ArgPair(4, 8)
    ->ArgPair(4, 10)
    ->ArgPair(8, 8)
    ->ArgPair(8, 10)
    ->ArgPair(10, 8)
    ->ArgPair(9, 10);

void BM_ParseLeading_unsigned32_string(benchmark::State& state) {
  const int digits = state.range(0);
  const int base = state.range(1);
  std::string str1(digits, '7');  // valid in octal, decimal and hex
  std::string parse_string = str1;
  parse_string.append("x");  // Explicitly add non-digit character at end.
  uint32_t value = 0;
  uint32_t (*test_function)(absl::string_view, uint32_t) = nullptr;
  switch (base) {
    case 8:
      test_function = &ParseLeadingUInt32Value;
      parse_string.insert(0, "0");  // force octal interpretation
      break;
    case 10:
      test_function = &ParseLeadingUDec32Value;
      break;
  }
  for (auto _ : state) {
    value = test_function(parse_string, 0);
  }
  std::string str2;
  ASSERT_TRUE(Itoa(value, base, &str2));
  ASSERT_EQ(str1, str2);
}
BENCHMARK(BM_ParseLeading_unsigned32_string)
    ->ArgPair(1, 8)
    ->ArgPair(1, 10)
    ->ArgPair(2, 8)
    ->ArgPair(2, 10)
    ->ArgPair(4, 8)
    ->ArgPair(4, 10)
    ->ArgPair(8, 8)
    ->ArgPair(8, 10)
    ->ArgPair(10, 8)
    ->ArgPair(9, 10);

void BM_ParseLeading_i64_string(benchmark::State& state) {
  const int digits = state.range(0);
  const int base = state.range(1);
  std::string str1(digits, '7');  // valid in octal, decimal and hex
  std::string parse_string = str1;
  parse_string.append("x");  // Explicitly add non-digit character at end.
  int64_t value = 0;
  int64_t (*test_function)(absl::string_view, int64_t) = nullptr;
  switch (base) {
    case 8:
      test_function = &ParseLeadingInt64Value;
      parse_string.insert(0, "0");  // force octal interpretation
      break;
    case 10:
      test_function = &ParseLeadingDec64Value;
      break;
  }
  for (auto _ : state) {
    value = test_function(parse_string, 0);
  }
  std::string str2;
  ASSERT_TRUE(Itoa(value, base, &str2));
  ASSERT_EQ(str1, str2);
}
BENCHMARK(BM_ParseLeading_i64_string)
    ->ArgPair(1, 8)
    ->ArgPair(1, 10)
    ->ArgPair(2, 8)
    ->ArgPair(2, 10)
    ->ArgPair(4, 8)
    ->ArgPair(4, 10)
    ->ArgPair(8, 8)
    ->ArgPair(8, 10)
    ->ArgPair(16, 8)
    ->ArgPair(16, 10);

void BM_ParseLeading_unsigned64_string(benchmark::State& state) {
  const int digits = state.range(0);
  const int base = state.range(1);
  std::string str1(digits, '7');  // valid in octal, decimal and hex
  std::string parse_string = str1;
  parse_string.append("x");  // Explicitly add non-digit character at end.
  uint64_t value = 0;
  uint64_t (*test_function)(absl::string_view, uint64_t) = nullptr;
  switch (base) {
    case 8:
      test_function = &ParseLeadingUInt64Value;
      parse_string.insert(0, "0");  // force octal interpretation
      break;
    case 10:
      test_function = &ParseLeadingUDec64Value;
      break;
    case 16:
      test_function = &ParseLeadingHex64Value;
      break;
  }
  for (auto _ : state) {
    value = test_function(parse_string, 0);
  }
  std::string str2;
  ASSERT_TRUE(Itoa(value, base, &str2));
  ASSERT_EQ(str1, str2);
}
BENCHMARK(BM_ParseLeading_unsigned64_string)
    ->ArgPair(1, 8)
    ->ArgPair(1, 10)
    ->ArgPair(1, 16)
    ->ArgPair(2, 8)
    ->ArgPair(2, 10)
    ->ArgPair(2, 16)
    ->ArgPair(4, 8)
    ->ArgPair(4, 10)
    ->ArgPair(4, 16)
    ->ArgPair(8, 8)
    ->ArgPair(8, 10)
    ->ArgPair(8, 16)
    ->ArgPair(16, 8)
    ->ArgPair(16, 10)
    ->ArgPair(16, 16);

void BM_float32_buf(benchmark::State& state) {
  char buffer[32];
  float float_value = 0.01f;
  for (auto _ : state) {
    absl::numbers_internal::RoundTripFloatToBuffer(float_value, buffer);
    // Grow `float_value` geometrically, wrapping around to small exponents
    // after hitting a threshold.
    float_value = float_value * 1.00001;
    if (float_value > 1e9) float_value *= 1e-18;
  }
}
BENCHMARK(BM_float32_buf);

void BM_HexStringToUint128(benchmark::State& state) {
  RandomEngine rng(0);
  std::uniform_int_distribution<uint64_t> random_uint64(
      0, std::numeric_limits<uint64_t>::max());

  constexpr int kNumStrings = 1000;
  std::vector<std::string> test_strings;
  test_strings.reserve(kNumStrings);
  for (int i = 0; i < kNumStrings; ++i) {
    test_strings.push_back(Uint128ToHexString(
        absl::MakeUint128(random_uint64(rng), random_uint64(rng))));
  }
  for (auto _ : state) {
    for (const std::string& s : test_strings) {
      const auto value = HexStringToUint128(s);
      benchmark::DoNotOptimize(value);
    }
  }
}
BENCHMARK(BM_HexStringToUint128);

static const int kFastTimeToBufferSize = 30;

static void BM_FastTimeToBuffer(benchmark::State& state) {
  char buffer[kFastTimeToBufferSize];
  time_t t = 1000000000;
  int i = 0;
  for (auto _ : state) {
    FastTimeToBuffer(t + i, buffer);
    ++i;
  }
}
BENCHMARK(BM_FastTimeToBuffer);

}  // namespace
}  // namespace strings
