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

#include "gloop/util/coding/two-values-varint.h"

#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "gloop/gloop_test.h"

namespace util {
namespace coding {
namespace {

template <typename T>
void TwoValuesEncode(std::string* s, T a, T b) {}

template <>
void TwoValuesEncode(std::string* s, uint32_t a, uint32_t b) {
  TwoValuesVarint::Encode32(s, a, b);
}

template <>
void TwoValuesEncode(std::string* s, uint64_t a, uint64_t b) {
  TwoValuesVarint::Encode64(s, a, b);
}

template <typename T>
const char* TwoValuesDecode(const char* p, T* a, T* b) {
  return nullptr;
}

template <>
const char* TwoValuesDecode(const char* p, uint32_t* a, uint32_t* b) {
  return TwoValuesVarint::Decode32(p, a, b);
}

template <>
const char* TwoValuesDecode(const char* p, uint64_t* a, uint64_t* b) {
  return TwoValuesVarint::Decode64(p, a, b);
}

template <typename T>
const char* TwoValuesDecodeWithLimit(const char* p, const char* limit, T* a,
                                     T* b) {
  return nullptr;
}

template <>
const char* TwoValuesDecodeWithLimit(const char* p, const char* limit,
                                     uint32_t* a, uint32_t* b) {
  return TwoValuesVarint::Decode32WithLimit(p, limit, a, b);
}

template <>
const char* TwoValuesDecodeWithLimit(const char* p, const char* limit,
                                     uint64_t* a, uint64_t* b) {
  return TwoValuesVarint::Decode64WithLimit(p, limit, a, b);
}

template <typename T>
std::string TestOnePair(T a, T b) {
  std::string s;
  TwoValuesEncode(&s, a, b);
  T a_decode, b_decode;
  const char* limit = s.data() + s.size();

  const char* p = TwoValuesDecode(s.data(), &a_decode, &b_decode);
  CHECK_EQ(p, limit);
  CHECK_EQ(a, a_decode);
  CHECK_EQ(b, b_decode);

  // Decode*WithLimit with limit right at the end, success.
  p = TwoValuesDecodeWithLimit(s.data(), limit, &a_decode, &b_decode);
  CHECK_EQ(p, limit);
  CHECK_EQ(a, a_decode);
  CHECK_EQ(b, b_decode);

  // Decode*WithLimit with limit at beginning, failure.
  CHECK_EQ(nullptr,
           TwoValuesDecodeWithLimit(s.data(), s.data(), &a_decode, &b_decode));

  // Decode*WithLimit with limit before finishing, failure.
  CHECK_EQ(nullptr,
           TwoValuesDecodeWithLimit(s.data(), limit - 1, &a_decode, &b_decode));

  // Decode*WithLimit with limit past finishing, success.
  p = TwoValuesDecodeWithLimit(s.data(), limit + 1, &a_decode, &b_decode);
  CHECK_EQ(p, limit);
  CHECK_EQ(a, a_decode);
  CHECK_EQ(b, b_decode);
  return s;
}

template <typename T>
void TestPair(T a, T b) {}

template <>
void TestPair(uint32_t a, uint32_t b) {
  // Validates the 64 encoder would generate the same output.
  CHECK_EQ(TestOnePair<uint32_t>(a, b),
           TestOnePair<uint64_t>(static_cast<uint64_t>(a),
                                 static_cast<uint64_t>(b)));
  if (a != b) {
    // Check the reverse case.
    CHECK_EQ(TestOnePair<uint32_t>(b, a),
             TestOnePair<uint64_t>(static_cast<uint64_t>(b),
                                   static_cast<uint64_t>(a)));
  }
}

template <>
void TestPair(uint64_t a, uint64_t b) {
  TestOnePair(a, b);
  if (a != b) {
    // Check the reverse case.
    TestOnePair(b, a);
  }
}

TEST(TwoValueVarint, Encoding32Test) {
  TestPair<uint32_t>(0, 0);
  TestPair<uint32_t>(1, 0);
  TestPair<uint32_t>(1, 1);
  TestPair<uint32_t>(2, 1);
  TestPair<uint32_t>(17, 1);
  TestPair<uint32_t>(17, 17);
  TestPair<uint32_t>(253, 17);
  TestPair<uint32_t>(1u << 30, 17);
  TestPair<uint32_t>(253, 255);
  TestPair<uint32_t>(256, 255);
  TestPair<uint32_t>(1u << 30, 17);
  TestPair<uint32_t>(std::numeric_limits<uint32_t>::max(),
                     std::numeric_limits<uint32_t>::max());
  for (int i = 0; i < 31; ++i) {
    for (int j = 0; j < 31; ++j) {
      TestPair<uint32_t>(1 << i, 1 << j);
    }
  }
  absl::BitGen gen;
  for (int i = 0; i < 10000; i++) {
    TestPair<uint32_t>(absl::Uniform<uint32_t>(gen, 0, 1 << 30),
                       absl::Uniform<uint32_t>(gen, 0, 1 << 30));
  }
}

TEST(TwoValueVarint, Encoding64Teset) {
  // Note: Encoding32Test already covers Encoding64 for small values.
  TestPair<uint64_t>(uint64_t{1} << 61, uint64_t{1} << 35);
  TestPair<uint64_t>(uint64_t{1} << 60, uint64_t{1} << 61);
  TestPair<uint64_t>(std::numeric_limits<uint64_t>::max(),
                     std::numeric_limits<uint64_t>::max());
  TestPair<uint64_t>(uint64_t{1}, std::numeric_limits<uint64_t>::max());
  for (uint64_t i = 0; i < 63; ++i) {
    for (uint64_t j = 0; j < 63; ++j) {
      TestPair<uint64_t>((uint64_t{1} << i) - 1, (uint64_t{1} << j) - 1);
    }
  }
  std::seed_seq seed_seq({1, 2, 3});
  absl::BitGen gen(seed_seq);
  for (uint64_t i = 0; i < 10000; i++) {
    TestPair<uint64_t>(
        absl::Uniform<uint64_t>(gen, 0, std::numeric_limits<uint64_t>::max()),
        absl::Uniform<uint64_t>(gen, 0, std::numeric_limits<uint64_t>::max()));
  }
}

// Helper routine to initialize an array of N values based on "bit_shift".
// If "bit_shift" is -1, random values are chosen. Otherwise, the values are all
// the same constant value of "1ull << bit_shift".
template <typename T>
std::vector<T> GetValueArray(int n, int bit_shift) {
  std::vector<T> vals(n, 1ull << (bit_shift == -1 ? 0 : bit_shift));
  std::seed_seq seed_seq({1, 2, 3});
  absl::BitGen gen{seed_seq};
  if (bit_shift == -1) {
    for (auto& x : vals) x = absl::Uniform<T>(gen);
  }
  return vals;
}

// Gets the length of the encoded result from EncodeTwo32Values().
template <typename T>
int GetEncodedLength(const T a) {
  std::string buf;
  TwoValuesEncode<T>(&buf, a, a);
  return buf.size();
}

template <typename T>
int GetAverageEncodedLength(const std::vector<T>& vals) {
  int64_t sum_len = 0;
  for (const T val : vals) sum_len += GetEncodedLength(val);
  return sum_len /= vals.size();
}

template <typename T>
void BM_Encode(benchmark::State& state) {
  const int bit_shift = state.range(0);
  const int kNumValues = 8192;
  std::vector<T> vals = GetValueArray<T>(kNumValues, bit_shift);
  if (bit_shift == -1) {
    state.SetLabel(absl::StrCat("random: average ",
                                GetAverageEncodedLength<T>(vals), " bytes"));
  } else {
    state.SetLabel(absl::StrCat("len: ", GetEncodedLength(vals[0]), " bytes"));
  }

  std::string buf;
  while (state.KeepRunningBatch(kNumValues)) {
    for (int i = 0; i < kNumValues; i++) {
      TwoValuesEncode<T>(&buf, vals[i], vals[i]);
    }
  }
}

// The following arguments were chosen to generate encodings of all possible
// lengths [1,max_length], as well as random.
// Concretely,
// [ (i-1)*7/2 for i in [1,2,3,4,5,6,7,8,9,10 (encode32 max),
//                          11,12,13,14,15,16,17,18,19 (encode64 max) ]]
BENCHMARK_TEMPLATE(BM_Encode, uint32_t)
    ->Arg(-1)
    ->Arg(0)
    ->Arg(3)
    ->Arg(7)
    ->Arg(10)
    ->Arg(14)
    ->Arg(17)
    ->Arg(21)
    ->Arg(24)
    ->Arg(28)
    ->Arg(31);

BENCHMARK_TEMPLATE(BM_Encode, uint64_t)
    ->Arg(-1)
    ->Arg(0)
    ->Arg(3)
    ->Arg(7)
    ->Arg(10)
    ->Arg(14)
    ->Arg(17)
    ->Arg(21)
    ->Arg(24)
    ->Arg(28)
    ->Arg(31)
    ->Arg(35)
    ->Arg(38)
    ->Arg(42)
    ->Arg(45)
    ->Arg(49)
    ->Arg(52)
    ->Arg(56)
    ->Arg(59)
    ->Arg(63);

template <typename T>
void BM_Decode(benchmark::State& state) {
  const int bit_shift = state.range(0);
  const int kNumValues = 8192;
  std::vector<T> vals = GetValueArray<T>(kNumValues, bit_shift);
  if (bit_shift == -1) {
    state.SetLabel(absl::StrCat("random: average ",
                                GetAverageEncodedLength(vals), " bytes"));
  } else {
    state.SetLabel(absl::StrCat("len: ", GetEncodedLength(vals[0]), " bytes"));
  }
  std::string buf;
  for (const T v : vals) {
    TwoValuesEncode(&buf, v, v);
  }
  while (state.KeepRunningBatch(kNumValues)) {
    const char* p = buf.data();
    for (int i = 0; i < kNumValues; ++i) {
      T low, high;
      p = TwoValuesDecode<T>(p, &low, &high);
    }
  }
}

BENCHMARK_TEMPLATE(BM_Decode, uint32_t)
    ->Arg(-1)
    ->Arg(0)
    ->Arg(3)
    ->Arg(7)
    ->Arg(10)
    ->Arg(14)
    ->Arg(17)
    ->Arg(21)
    ->Arg(24)
    ->Arg(28)
    ->Arg(31);

BENCHMARK_TEMPLATE(BM_Decode, uint64_t)
    ->Arg(-1)
    ->Arg(0)
    ->Arg(3)
    ->Arg(7)
    ->Arg(10)
    ->Arg(14)
    ->Arg(17)
    ->Arg(21)
    ->Arg(24)
    ->Arg(28)
    ->Arg(31)
    ->Arg(35)
    ->Arg(38)
    ->Arg(42)
    ->Arg(45)
    ->Arg(49)
    ->Arg(52)
    ->Arg(56)
    ->Arg(59)
    ->Arg(63);

}  // namespace
}  // namespace coding
}  // namespace util
