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

// Based on contributions of various authors in strings/strutil_unittest.cc
//
// This file contains conversion functions from various data types to
// strings and back.

#include "gloop/strings/serialize.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/container/node_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/numeric/int128.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace strings {

TEST(Serialize, DoubleToKeyGolden) {
  const struct {
    double input;
    const char* output;
  } golden_data[] = {
      {0.0, "\200\000\000\000\000\000\000\000"},
      {-0.0, "\200\000\000\000\000\000\000\000"},
      {HUGE_VAL, "\377\360\000\000\000\000\000\000"},
      {-HUGE_VAL, "\000\020\000\000\000\000\000\000"},
      // Randomly generated, denormalized values:
      {-7.9573861743e-309, "\177\372G,\236\311\250\270"},
      {-2.0584061964e-308, "\177\3612\316\341p\370&"},
      {-5.3881954870e-310, "\177\377\234\317\343QmD"},
      {-1.4104201356e-308, "\177\365\333\2455-\206\206"},
      {-1.3122468475e-308, "\177\366\220]\316\371q\374"},
      // Randomly generated values:
      {-8.2208942774e-155, "`\016]7\312\247\307\272"},
      {-4.2289966290e+75, "0]L\362\t\331{;"},
      {-3.1295554093e-105, "U\303J\033\031b\3152"},
      {-4.4113819827e+169, "\034\330\237<T\006z\265"},
      {1.0993888931e-263, "\211V\'\342\331{u\006"},
      {2.8860242090e+253, "\364\217}\324#\312\267\253"},
      {7.2694793595e+190, "\347\220P\322\205\365\250c"},
      {-1.5532561277e+37, "8X\241\0234\321\353h"},
      {1.6112781500e+53, "\312\372\352\201\377pd\n"},
      {-3.1187891975e-236, "p\366\2355}K\2736"},
  };
  for (const auto& testcase : golden_data) {
    const std::string str(testcase.output, 8);
    EXPECT_EQ(str, DoubleToKey(testcase.input));
    EXPECT_EQ(testcase.input, KeyToDouble(str));
  }
}

TEST(Serialize, FloatToKeyGolden) {
  const struct {
    float input;
    const char* output;
  } golden_data[] = {{0.0, "\200\000\000\000"},
                     {-0.0, "\200\000\000\000"},
                     {HUGE_VALF, "\377\200\000\000"},
                     {-HUGE_VALF, "\000\200\000\000"},
                     // Randomly generated, denormalized values:
                     {6.6963891754e-39, "\200H\352\317"},
                     {1.0545966252e-38, "\200r\325\335"},
                     {-3.4626505443e-39, "\177\332K\212"},
                     {-5.4040893108e-39, "\177\305\'\231"},
                     {-1.1379884573e-38, "\177\204\025\203"},
                     {-1.0294981085e-38, "\177\217\345\310"},
                     // Randomly generated values:
                     {-3.2496018736e-26, "j\337\026g"},
                     {3.2495658532e-19, "\240\277\322\r"},
                     {5.6485808221e-17, "\244\202?V"},
                     {-1.4589976286e-15, "Y-\274}"},
                     {1.0130019490e-08, "\262.\010L"},
                     {1.3461539745e+00, "\277\254N\306"},
                     {1.6433030360e-22, "\233F\251\304"},
                     {5.8059166186e-03, "\273\276?\217"},
                     {-1.2451648512e+10, "/\306t\273"},
                     {1.8695803305e-07, "\264H\276\242"}};
  for (const auto& testcase : golden_data) {
    const std::string str(testcase.output, 4);
    EXPECT_EQ(str, FloatToKey(testcase.input));
    EXPECT_EQ(testcase.input, KeyToFloat(str));
  }
}

TEST(Serialize, FloatEncodings) {
  double d1 = 2.718;
  double d2 = 0.0;
  std::string double_str = EncodeDouble(d1);
  CHECK(DecodeDouble(double_str, &d2));
  ASSERT_EQ(0, memcmp(&d1, &d2, sizeof(d1)));
  d2 = 0.0;
  CHECK(!absl::StrContains(double_str, '\0'));  // doesn't have '\0'
  CHECK(DecodeDouble(double_str.c_str(), &d2));
  ASSERT_EQ(0, memcmp(&d1, &d2, sizeof(d1)));

  float f1 = 2.718;
  float f2 = 0.0;
  std::string float_str = EncodeFloat(f1);
  CHECK(DecodeFloat(float_str, &f2));
  ASSERT_EQ(0, memcmp(&f1, &f2, sizeof(f1)));
  f2 = 0.0;
  CHECK(!absl::StrContains(float_str, '\0'));  // doesn't have '\0'
  CHECK(DecodeFloat(float_str.c_str(), &f2));
  ASSERT_EQ(0, memcmp(&f1, &f2, sizeof(f1)));

  CHECK(!DecodeFloat(double_str, &f1));
  CHECK(!DecodeFloat(double_str.c_str(), &f1));
  CHECK(!DecodeDouble(float_str, &d1));
  CHECK(!DecodeDouble(float_str.c_str(), &d1));
}

TEST(Serialize, UintEncodings) {
  // Modeled on the float version: just convert from uint to string and back.

  uint32_t w1 = 0xDEADBEEF;
  uint32_t w2 = 0;
  std::string uint32_str = EncodeUint32(w1);
  EXPECT_EQ(4, uint32_str.size());
  CHECK(DecodeUint32(uint32_str, &w2));
  EXPECT_EQ(w1, w2);
  w2 = 0;
  CHECK(!absl::StrContains(uint32_str, '\0'));  // doesn't have '\0'
  CHECK(DecodeUint32(uint32_str.c_str(), &w2));
  EXPECT_EQ(w1, w2);

  uint64_t q1 = 0x021A098CABCD1234ULL;
  uint64_t q2 = 0;
  std::string uint64_str = EncodeUint64(q1);
  EXPECT_EQ(8, uint64_str.size());
  CHECK(DecodeUint64(uint64_str, &q2));
  EXPECT_EQ(q1, q2);
  q2 = 0;
  CHECK(!absl::StrContains(uint64_str, '\0'));  // doesn't have '\0'
  CHECK(DecodeUint64(uint64_str.c_str(), &q2));
  EXPECT_EQ(q1, q2);

  // Verify that DecodeUintNN() fails if the string is the wrong size.
  CHECK(!DecodeUint32(uint64_str, &w1));
  CHECK(!DecodeUint32(uint64_str.c_str(), &w1));
  CHECK(!DecodeUint64(uint32_str, &q1));
  CHECK(!DecodeUint64(uint32_str.c_str(), &q1));
}

struct st_pod {
  int32_t i;
  int64_t l;
  double d;
};

TEST(Serialize, PODEncodeDecode) {
  uint64_t q1 = 0x012345670f0f0f0fULL;
  uint64_t q2 = 0;
  std::string uint64_str = EncodePOD(q1);
  CHECK(DecodePOD(uint64_str, &q2));
  EXPECT_EQ(q1, q2);
  q2 = 0;
  CHECK(!absl::StrContains(uint64_str, '\0'));  // doesn't have '\0'
  CHECK(DecodePOD(uint64_str.c_str(), &q2));
  EXPECT_EQ(q1, q2);

  st_pod st1 = {10, 2000000000L, 5.55555555555};
  st_pod st2;
  std::string st_str = EncodePOD(st1);
  CHECK(DecodePOD(st_str, &st2));
  EXPECT_EQ(st1.i, st2.i);
  EXPECT_EQ(st1.l, st2.l);
  EXPECT_EQ(st1.d, st2.d);
  st2 = st_pod();
  CHECK(absl::StrContains(st_str, '\0'));  // has '\0'
  // This actually shouldn't work, because st_str contains a '\0' character
  // which will be construed as the end of its C string equivalent.
  CHECK(!DecodePOD(st_str.c_str(), &st2));

  CHECK(!DecodePOD(uint64_str, &st1));
  CHECK(!DecodePOD(uint64_str.c_str(), &st1));
  CHECK(!DecodePOD(st_str, &q1));
  CHECK(!DecodePOD(st_str.c_str(), &q1));
}

struct PaddedStruct {
  char c = 'a';
  int64_t l = 0;
  double d = 0.0;
};

TEST(Serialize, PODWithPaddingEncodeDecode) {
  PaddedStruct x = {'1', 2, 3.0};
  static_assert(sizeof(x) > sizeof(x.c) + sizeof(x.l) + sizeof(x.d));
  std::string encoded = EncodePOD(x);
  LOG(INFO) << encoded;  // Needed to ensure MSan "sees" the serialized bytes.

  PaddedStruct y;
  ASSERT_TRUE(DecodePOD(encoded, &y));
  EXPECT_EQ(std::tie(x.c, x.l, x.d), std::tie(y.c, y.l, y.d));
}

TEST(Serialize, EncodeManyPOD) {
  const std::string vecch =
      EncodeManyPOD(std::vector<char>{'a', 'b', 'y', 'z'});
  const std::string strch = EncodeManyPOD(std::string{"abyz"});
  const std::string arrch =
      EncodeManyPOD(std::array<char, 4>{'a', 'b', 'y', 'z'});
  EXPECT_EQ(vecch, strch);
  EXPECT_EQ(strch, arrch);
  EXPECT_EQ(arrch, vecch);

  std::vector<char> decodech;
  ASSERT_TRUE(DecodeVectorPOD(vecch, &decodech));
  EXPECT_THAT(decodech, testing::ElementsAre('a', 'b', 'y', 'z'));

  const std::string vecu16 = EncodeManyPOD(std::vector<uint16_t>{1, 2, 9, 10});
  uint16_t plainc[] = {1, 2, 9, 10};
  const std::string spanu16 = EncodeManyPOD(absl::Span<const uint16_t>(plainc));
  const std::string arru16 =
      EncodeManyPOD(std::array<uint16_t, 4>{1, 2, 9, 10});
  EXPECT_EQ(vecu16, spanu16);
  EXPECT_EQ(spanu16, arru16);
  EXPECT_EQ(arru16, vecu16);

  std::vector<uint16_t> decodeu16;
  ASSERT_TRUE(DecodeVectorPOD(vecu16, &decodeu16));
  EXPECT_THAT(decodeu16, testing::ElementsAre(1, 2, 9, 10));
}

TEST(Serialize, VectorPODEncodeDecode) {
  std::vector<int64_t> vi1;
  std::vector<int64_t> vi2;
  for (int64_t i = -5L; i < 5L; ++i) vi1.push_back(i * 100000000L);
  std::string vi_str = EncodeManyPOD(vi1);
  CHECK(DecodeVectorPOD(vi_str, &vi2));
  ASSERT_EQ(vi1.size(), vi2.size());
  for (std::vector<int64_t>::iterator it1 = vi1.begin(), it2 = vi2.begin();
       (it1 != vi1.end() && it2 != vi2.end()); ++it1, ++it2) {
    EXPECT_EQ(*it1, *it2);
  }

  std::vector<char> vc1;
  std::vector<char> vc2;
  for (signed char c = -5; c < 5; ++c) vc1.push_back(static_cast<char>(c));
  std::string vc_str = EncodeManyPOD(vc1);
  CHECK(DecodeVectorPOD(vc_str, &vc2));
  EXPECT_EQ(vc1.size(), vc2.size());
  for (std::vector<char>::iterator it1 = vc1.begin(), it2 = vc2.begin();
       (it1 != vc1.end() && it2 != vc2.end()); ++it1, ++it2) {
    EXPECT_EQ(*it1, *it2);
  }
}

TEST(Serialize, DictionaryParse) {
  std::string empty_string;
  std::vector<std::pair<std::string, std::string>> items;
  CHECK(DictionaryParse(empty_string, &items));
  std::string dic1 = "goog:1,msft:2,amzn:3";
  CHECK(DictionaryParse(dic1, &items));
  EXPECT_EQ(items[0].first, "goog");
  EXPECT_EQ(items[0].second, "1");
  EXPECT_EQ(items[1].first, "msft");
  EXPECT_EQ(items[1].second, "2");
  EXPECT_EQ(items[2].first, "amzn");
  EXPECT_EQ(items[2].second, "3");
}

TEST(Serialize, DictionaryEncodeDecodeUnordered) {
  const std::string google = "google";
  const std::string yahoo = "yahoo";
  const std::string cnn = "cnn";
  const std::string empty = "";

  LOG(INFO) << "Testing intmap encode/decode";
  std::unordered_map<std::string, int32_t> intmap;

  intmap[google] = 1;
  intmap[yahoo] = 2;
  intmap[empty] = 4;

  std::string encoded_intmap = DictionaryEncode(intmap);
  std::unordered_map<std::string, int32_t> intmap_copy;
  CHECK(DictionaryInt32Decode(&intmap_copy, encoded_intmap))
      << " decode failed for " << encoded_intmap;
  for (auto iter = intmap.cbegin(); iter != intmap.cend(); ++iter) {
    EXPECT_EQ(iter->second, intmap_copy[iter->first]);
  }
  for (auto iter = intmap_copy.cbegin(); iter != intmap_copy.cend(); ++iter) {
    EXPECT_EQ(iter->second, intmap[iter->first]);
  }

  LOG(INFO) << "Testing int64map encode/decode";
  absl::node_hash_map<std::string, int64_t> int64map;
  int64map[google] = 1;
  int64map[cnn] = 2;
  int64map[empty] = 4;

  std::string encoded_int64map = DictionaryEncode(int64map);
  absl::node_hash_map<std::string, int64_t> int64map_copy;
  CHECK(DictionaryInt64Decode(&int64map_copy, encoded_int64map))
      << " decode failed for " << encoded_int64map;
  for (auto iter = int64map.cbegin(); iter != int64map.cend(); ++iter) {
    EXPECT_EQ(iter->second, int64map_copy[iter->first]);
  }
  for (auto iter = int64map_copy.cbegin(); iter != int64map_copy.cend();
       ++iter) {
    EXPECT_EQ(iter->second, int64map[iter->first]);
  }

  LOG(INFO) << "Testing double encode/decode";
  std::unordered_map<std::string, double> doublemap;
  doublemap[google] = 1.0;
  doublemap[cnn] = 2.0;
  doublemap[yahoo] = 3.0;
  doublemap[empty] = 12.0;

  std::string encoded_doublemap = DictionaryEncode(doublemap);
  std::unordered_map<std::string, double> doublemap_copy;
  CHECK(DictionaryDoubleDecode(&doublemap_copy, encoded_doublemap))
      << " decode failed for " << encoded_doublemap;
  for (auto iter = doublemap.cbegin(); iter != doublemap.cend(); ++iter) {
    EXPECT_EQ(iter->second, doublemap_copy[iter->first]);
  }
  for (auto iter = doublemap_copy.cbegin(); iter != doublemap_copy.cend();
       ++iter) {
    EXPECT_EQ(iter->second, doublemap[iter->first]);
  }

  LOG(INFO) << "Testing bad input parse";
  std::string encoded_bad_input("google:2x,yahoo:1");  // "2x" should fail parse
  CHECK(!DictionaryDoubleDecode(&doublemap_copy, encoded_bad_input))
      << " decode succeeded for " << encoded_bad_input;
}

std::string MakeBenchmarkInput(const benchmark::State& state) {
  const int num_values = state.range(0);
  const int value_size = state.range(1);
  const auto entry = absl::StrCat("some_key:", std::string(value_size, 'v'));
  std::string input = entry;
  for (int i = 0; i < num_values; ++i) {
    input.push_back(',');
    input.append(entry);
  }
  return input;
}

void BM_DictionaryParse_Vector(benchmark::State& state) {
  std::string input = MakeBenchmarkInput(state);
  for (const auto _ : state) {
    std::vector<std::pair<std::string, std::string>> items;
    DictionaryParse(input, &items);
    benchmark::DoNotOptimize(items);
  }
}
BENCHMARK(BM_DictionaryParse_Vector)->RangePair(1, 100, 1, 100);

void BM_DictionaryParse_Callback(benchmark::State& state) {
  std::string input = MakeBenchmarkInput(state);
  for (const auto _ : state) {
    DictionaryParse(input, [](absl::string_view key, absl::string_view value) {
      benchmark::DoNotOptimize(key);
      benchmark::DoNotOptimize(value);
    });
  }
}
BENCHMARK(BM_DictionaryParse_Callback)->RangePair(1, 100, 1, 100);

template <typename T, auto Fn>
void BM_KeyFrom(benchmark::State& state) {
  T value = T{};
  std::string out;
  for (const auto s : state) {
    benchmark::DoNotOptimize(value);
    Fn(value, &out);
    benchmark::DoNotOptimize(out);
  }
}
BENCHMARK(BM_KeyFrom<uint32_t, KeyFromUint32>);
BENCHMARK(BM_KeyFrom<uint64_t, KeyFromUint64>);
BENCHMARK(BM_KeyFrom<absl::uint128, KeyFromUint128>);
BENCHMARK(BM_KeyFrom<int32_t, KeyFromInt32>);
BENCHMARK(BM_KeyFrom<int64_t, KeyFromInt64>);
BENCHMARK(BM_KeyFrom<absl::int128, KeyFromInt128>);
BENCHMARK(BM_KeyFrom<double, KeyFromDouble>);
BENCHMARK(BM_KeyFrom<float, KeyFromFloat>);

template <typename T, auto Fn>
void BM_ToKey(benchmark::State& state) {
  T value = T{};
  for (const auto s : state) {
    benchmark::DoNotOptimize(value);
    auto key = Fn(value);
    benchmark::DoNotOptimize(key);
  }
}
BENCHMARK(BM_ToKey<uint32_t, Uint32ToKey>);
BENCHMARK(BM_ToKey<uint64_t, Uint64ToKey>);
BENCHMARK(BM_ToKey<absl::uint128, Uint128ToKey>);
BENCHMARK(BM_ToKey<int32_t, Int32ToKey>);
BENCHMARK(BM_ToKey<int64_t, Int64ToKey>);
BENCHMARK(BM_ToKey<absl::int128, Int128ToKey>);
BENCHMARK(BM_ToKey<double, DoubleToKey>);
BENCHMARK(BM_ToKey<float, FloatToKey>);

template <typename IntT, auto Fn>
void BM_StrCatWithToKey(benchmark::State& state) {
  IntT value = IntT{};
  for (const auto s : state) {
    benchmark::DoNotOptimize(value);
    auto out = absl::StrCat("prefix", Fn(value), "suffix");
    benchmark::DoNotOptimize(out);
  }
}
BENCHMARK(BM_StrCatWithToKey<uint32_t, Uint32ToKey>);
BENCHMARK(BM_StrCatWithToKey<uint64_t, Uint64ToKey>);
BENCHMARK(BM_StrCatWithToKey<absl::uint128, Uint128ToKey>);
BENCHMARK(BM_StrCatWithToKey<int32_t, Int32ToKey>);
BENCHMARK(BM_StrCatWithToKey<int64_t, Int64ToKey>);
BENCHMARK(BM_StrCatWithToKey<absl::int128, Int128ToKey>);

void BM_OrderedStringFromInt32(benchmark::State& state) {
  int32_t i = INT32_MAX;
  for (auto _ : state) {
    std::string result = Int32ToOrderedString(i);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(BM_OrderedStringFromInt32);

void BM_OrderedStringFromInt64(benchmark::State& state) {
  int64_t i = INT64_MAX;
  for (auto _ : state) {
    std::string result = Int64ToOrderedString(i);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(BM_OrderedStringFromInt64);

}  // namespace strings
