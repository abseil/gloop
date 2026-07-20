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

#include "gloop/strings/keyed_intern_table.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "gloop/strings/interntable_benchmark.h"
#include "gloop/util/gtl/stl_util.h"
#include "gloop/util/intops/safe_int.h"
#include "gloop/util/intops/strong_int.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace strings {
namespace {

using ::testing::Eq;
using ::testing::Gt;
using ::testing::Ne;
using ::testing::Test;
using ::testing::Types;

constexpr std::array<absl::string_view, 4> kNonEmptyStrings = {
    {"a", "this is a long string",
     absl::string_view("string with embedded\0null", 25),
     absl::string_view("string with null at end\0", 23)}};

constexpr std::array<absl::string_view, 3> kEmptyStrings = {
    {"", absl::string_view(), absl::string_view(nullptr, 0)}};

DEFINE_SAFE_INT_TYPE(SafeTestInt, uint8_t, util_intops::LogFatalOnError);
DEFINE_STRONG_INT_TYPE(StrongTestInt, uint8_t);
enum class EnumClassStrictInt : uint8_t {};

template <typename T>
class KeyedInternTableInsertTest : public Test {};

TYPED_TEST_SUITE_P(KeyedInternTableInsertTest);

TYPED_TEST_P(KeyedInternTableInsertTest, InsertLookupNonEmpty) {
  KeyedInternTable<TypeParam> x;
  size_t expected_size = 0;
  for (absl::string_view s : kNonEmptyStrings) {
    EXPECT_THAT(x.size(), Eq(expected_size));
    // Value not in table.
    EXPECT_THAT(x.ToKey(s), Eq(std::nullopt));
    const TypeParam result = x.Insert(s);
    EXPECT_THAT(result, Eq(static_cast<TypeParam>(expected_size)));
    ++expected_size;
    // Double insertion results in consistent identifier.
    EXPECT_THAT(x.Insert(s), Eq(result));
    // Check the stored value is consistent.
    EXPECT_THAT(x.ToStringView(result), Eq(s));
    // Value now in table; check identifier is consistent.
    EXPECT_THAT(x.ToKey(s), Eq(result));
    EXPECT_THAT(x.size(), Eq(expected_size));
    // Check that string data was copied into the table, i.e. the pointed-to
    // data are in different memory locations.
    // Cast the pointers to void* to prevent gTest from trying to print them as
    // C-strings. They are not null terminated.
    EXPECT_THAT(static_cast<const void*>(x.ToStringView(result).data()),
                Ne(static_cast<const void*>(s.data())));
  }
}

TYPED_TEST_P(KeyedInternTableInsertTest, InsertLookupEmpty) {
  KeyedInternTable<TypeParam> x;
  EXPECT_THAT(x.size(), Eq(0));
  // Value not initially in table.
  EXPECT_THAT(x.ToKey(kEmptyStrings[0]), Eq(std::nullopt));

  for (absl::string_view s : kEmptyStrings) {
    const TypeParam result = x.Insert(s);
    // Double insertion results in consistent identifier.
    EXPECT_THAT(x.Insert(s), Eq(result));
    // Check the stored value is consistent.
    EXPECT_THAT(x.ToStringView(result), Eq(s));
    // Value now in table; check identifier is consistent.
    EXPECT_THAT(x.ToKey(s), Eq(result));
    EXPECT_THAT(x.size(), Eq(1));
  }
}

// Verifies that Reserve() executes without crashing for valid sizes.
TYPED_TEST_P(KeyedInternTableInsertTest, ReservePositive) {
  KeyedInternTable<TypeParam> x;
  x.Reserve(0);
  x.Reserve(10);
  x.Reserve(0);
}

REGISTER_TYPED_TEST_SUITE_P(KeyedInternTableInsertTest, InsertLookupNonEmpty,
                            InsertLookupEmpty, ReservePositive);

using IntegralTypes =
    Types<uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t, uint64_t,
          int64_t, SafeTestInt, StrongTestInt, EnumClassStrictInt>;
INSTANTIATE_TYPED_TEST_SUITE_P(KeyedInternTableInsertTestInstantiation,
                               KeyedInternTableInsertTest, IntegralTypes);

template <typename T>
class KeyedInternTableOverflowDeathTest : public Test {};

TYPED_TEST_SUITE_P(KeyedInternTableOverflowDeathTest);

TYPED_TEST_P(KeyedInternTableOverflowDeathTest, Overflow) {
  // Can not use std::numeric_limits<TypeParam> since
  // std::is_integral_v<TypeParam> might be false.  However all the TypeParam
  // have an effective range of uint8.
  constexpr uint8_t uint8max = std::numeric_limits<uint8_t>::max();
  static_assert(sizeof(TypeParam) == 1, "TypeParam too large");
  static_assert(static_cast<int>(static_cast<TypeParam>(uint8max)) ==
                    static_cast<int>(uint8max),
                "Round-trip to a larger signed int should be lossless");

  KeyedInternTable<TypeParam> x;
  for (int i = 0; i <= uint8max; ++i) {
    x.Insert(absl::StrCat(i));
  }
  EXPECT_THAT(x.size() - 1, Eq(std::numeric_limits<uint8_t>::max()));
  EXPECT_DEATH(x.Insert("abc123"), "256");
}

TYPED_TEST_P(KeyedInternTableOverflowDeathTest, ReserveNegative) {
  KeyedInternTable<TypeParam> x;
  EXPECT_DEATH(x.Reserve(-1), "Reserve()");
}

TYPED_TEST_P(KeyedInternTableOverflowDeathTest, ReserveOverflow) {
  KeyedInternTable<TypeParam> x;
  constexpr size_t kLimit = std::numeric_limits<uint8_t>::max();
  EXPECT_DEATH(x.Reserve(1 + kLimit), "Reserve()");
}

REGISTER_TYPED_TEST_SUITE_P(KeyedInternTableOverflowDeathTest, Overflow,
                            ReserveNegative, ReserveOverflow);

using OverflowIntegralTypes = Types<uint8_t, SafeTestInt, EnumClassStrictInt>;
INSTANTIATE_TYPED_TEST_SUITE_P(KeyedInternTableOverflowDeathTestInstantiation,
                               KeyedInternTableOverflowDeathTest,
                               OverflowIntegralTypes);

template <typename RNG>
static std::string RandomString(RNG* rng) {
  std::string str;
  str.resize(std::uniform_int_distribution<size_t>(0, 20)(*rng));
  std::generate(str.begin(), str.end(), [rng] {
    return static_cast<char>(std::uniform_int_distribution<int>(0, 255)(*rng));
  });
  return str;
}

TEST(KeyedInternTableTest, StressTest) {
  std::mt19937 rng(GTEST_FLAG_GET(random_seed));
  std::vector<std::string> random_strings;
  constexpr size_t kBufferLength = 4096;
  for (int i = 0; i < kBufferLength; ++i) {
    random_strings.push_back(RandomString(&rng));
  }
  // Check that the total length of strings stored is greater than the length
  // of the initial Arena buffer.
  const size_t total_length = std::accumulate(
      random_strings.begin(), random_strings.end(), size_t{0},
      [](size_t length, absl::string_view s) { return length + s.size(); });
  ASSERT_THAT(total_length, Gt(kBufferLength));
  using MappedInt = int32_t;
  std::vector<MappedInt> mapped_values;
  KeyedInternTable<MappedInt> x(kBufferLength);
  for (absl::string_view s : random_strings) {
    mapped_values.push_back(x.Insert(s));
  }
  const size_t number_of_unique_strings = [&random_strings] {
    auto tmp = random_strings;
    gtl::STLSortAndRemoveDuplicates(&tmp);
    return tmp.size();
  }();
  EXPECT_THAT(x.size(), Eq(number_of_unique_strings));
  for (int i = 0; i < mapped_values.size(); ++i) {
    EXPECT_THAT(x.ToStringView(mapped_values[i]), Eq(random_strings[i]));
  }
}

// A benchmark, with XX% of the strings getting (100 - XX)% of the activity.
void InternWithKeyedInternTable(benchmark::State& state, int num_strings,
                                double hot_fraction) {
  const int kMeanInternsPerString = 10;
  InternTableBenchmark bench(testing::UnitTest::GetInstance()->random_seed(),
                             num_strings, kMeanInternsPerString * num_strings,
                             hot_fraction);
  for (auto s : state) {
    strings::KeyedInternTable<uint64_t> intbl;
    for (int index : bench.indices) {
      intbl.Insert(bench.strings[index]);
    }
    CHECK_LE(intbl.size(), num_strings);
  }
}

// A benchmark, with XX% of the strings getting (100 - XX)% of the activity.
// Iters * num_strings is always == kMaxForManyTableSizes for benchmarking.
void InternWithKeyedInternTableManyTableSizes(benchmark::State& state,
                                              int iters, int num_strings,
                                              double hot_fraction) {
  state.PauseTiming();
  const int kMeanInternsPerString = 10;
  InternTableBenchmark bench(testing::UnitTest::GetInstance()->random_seed(),
                             num_strings, kMeanInternsPerString * num_strings,
                             hot_fraction);
  state.ResumeTiming();
  while (iters-- > 0) {
    strings::KeyedInternTable<uint64_t> intbl;
    for (int index : bench.indices) {
      intbl.Insert(bench.strings[index]);
    }
    CHECK_LE(intbl.size(), num_strings);
  }
}

void BM_KeyedInternTableInsertString_90_10(benchmark::State& state) {
  const int n = state.range(0);

  InternWithKeyedInternTable(state, n, 0.9);
}
BENCHMARK(BM_KeyedInternTableInsertString_90_10)->Range(1 << 8, 1 << 18);

void BM_KeyedInternTableInsertString_80_20(benchmark::State& state) {
  const int n = state.range(0);

  InternWithKeyedInternTable(state, n, 0.8);
}
BENCHMARK(BM_KeyedInternTableInsertString_80_20)->Range(1 << 8, 1 << 18);

void BM_KeyedInternTableInsertString_80_20_ManyTableSizes(
    benchmark::State& state) {
  for (auto s : state) {
    for (int n = kMinForManyTableSizes; n < kMaxForManyTableSizes;
         n *= kMultiplierForManyTableSizes) {
      InternWithKeyedInternTableManyTableSizes(state, kMaxForManyTableSizes / n,
                                               n, 0.8);
    }
  }
}
BENCHMARK(BM_KeyedInternTableInsertString_80_20_ManyTableSizes);

}  // namespace
}  // namespace strings
