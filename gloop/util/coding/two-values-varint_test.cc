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

#include "gloop/util/coding/two-values-varint.h"

#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "absl/random/distributions.h"
#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"
#include "fuzztest/fuzztest.h"
#include "gloop/util/random/distributions.h"
#include "gtest/gtest.h"

namespace util {
namespace coding {
namespace {

#if (!defined(__EMSCRIPTEN__) && !defined(__wasm__) && \
     !defined(__wasm32__)) ||                          \
    defined(__EMSCRIPTEN_PTHREADS__)
#define TWO_VALUES_VARINT_TEST_HAS_THREADS 1
#else
#define TWO_VALUES_VARINT_TEST_HAS_THREADS 0
#endif

using ::testing::AssertionFailure;
using ::testing::AssertionResult;
using ::testing::AssertionSuccess;

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
AssertionResult VerifyOnePair(T a, T b, std::string* s_out = nullptr) {
  std::string s;
  TwoValuesEncode(&s, a, b);
  if (s_out != nullptr) {
    *s_out = s;
  }
  T a_decode = 0;
  T b_decode = 0;
  const char* limit = s.data() + s.size();

  const char* p = TwoValuesDecode(s.data(), &a_decode, &b_decode);
  if (p != limit) {
    return AssertionFailure() << "TwoValuesDecode pointer mismatch: got "
                              << static_cast<const void*>(p) << ", expected "
                              << static_cast<const void*>(limit);
  }
  if (a_decode != a) {
    return AssertionFailure() << "TwoValuesDecode 'a' mismatch: got "
                              << a_decode << ", expected " << a;
  }
  if (b_decode != b) {
    return AssertionFailure() << "TwoValuesDecode 'b' mismatch: got "
                              << b_decode << ", expected " << b;
  }

  // Decode*WithLimit with limit right at the end, success.
  p = TwoValuesDecodeWithLimit(s.data(), limit, &a_decode, &b_decode);
  if (p != limit) {
    return AssertionFailure()
           << "TwoValuesDecodeWithLimit (at end) pointer mismatch: got "
           << static_cast<const void*>(p) << ", expected "
           << static_cast<const void*>(limit);
  }
  if (a_decode != a) {
    return AssertionFailure()
           << "TwoValuesDecodeWithLimit (at end) 'a' mismatch: got " << a_decode
           << ", expected " << a;
  }
  if (b_decode != b) {
    return AssertionFailure()
           << "TwoValuesDecodeWithLimit (at end) 'b' mismatch: got " << b_decode
           << ", expected " << b;
  }

  // Decode*WithLimit with limit at beginning, failure.
  p = TwoValuesDecodeWithLimit(s.data(), s.data(), &a_decode, &b_decode);
  if (p != nullptr) {
    return AssertionFailure()
           << "TwoValuesDecodeWithLimit (at start) expected nullptr, got "
           << static_cast<const void*>(p);
  }

  // Decode*WithLimit with limit before finishing, failure.
  p = TwoValuesDecodeWithLimit(s.data(), limit - 1, &a_decode, &b_decode);
  if (p != nullptr) {
    return AssertionFailure()
           << "TwoValuesDecodeWithLimit (before finish) expected nullptr, got "
           << static_cast<const void*>(p);
  }

  // Decode*WithLimit with limit past finishing, success.
  p = TwoValuesDecodeWithLimit(s.data(), limit + 1, &a_decode, &b_decode);
  if (p != limit) {
    return AssertionFailure()
           << "TwoValuesDecodeWithLimit (past finish) pointer mismatch: got "
           << static_cast<const void*>(p) << ", expected "
           << static_cast<const void*>(limit);
  }
  if (a_decode != a) {
    return AssertionFailure()
           << "TwoValuesDecodeWithLimit (past finish) 'a' mismatch: got "
           << a_decode << ", expected " << a;
  }
  if (b_decode != b) {
    return AssertionFailure()
           << "TwoValuesDecodeWithLimit (past finish) 'b' mismatch: got "
           << b_decode << ", expected " << b;
  }
  return AssertionSuccess();
}

struct Pair32 {
  uint32_t a;
  uint32_t b;
};

struct Pair64 {
  uint64_t a;
  uint64_t b;
};

std::vector<Pair32> Get32TestPairs() {
  std::vector<Pair32> pairs = {
      {0, 0},
      {1, 0},
      {1, 1},
      {2, 1},
      {17, 1},
      {17, 17},
      {253, 17},
      {1u << 30, 17},
      {253, 255},
      {256, 255},
      {std::numeric_limits<uint32_t>::max(),
       std::numeric_limits<uint32_t>::max()},
  };
  for (int i = 0; i < 31; i += 4) {
    for (int j = 0; j < 31; j += 4) {
      pairs.push_back({1u << i, 1u << j});
    }
  }
  return pairs;
}

std::vector<Pair64> Get64TestPairs() {
  std::vector<Pair64> pairs = {
      {uint64_t{1} << 61, uint64_t{1} << 35},
      {uint64_t{1} << 60, uint64_t{1} << 61},
      {std::numeric_limits<uint64_t>::max(),
       std::numeric_limits<uint64_t>::max()},
      {uint64_t{1}, std::numeric_limits<uint64_t>::max()},
  };
  for (uint64_t i = 0; i < 63; i += 8) {
    for (uint64_t j = 0; j < 63; j += 8) {
      pairs.push_back({(uint64_t{1} << i) - 1, (uint64_t{1} << j) - 1});
    }
  }
  return pairs;
}

class TwoValueVarint32Test : public testing::TestWithParam<Pair32> {};

TEST_P(TwoValueVarint32Test, Encoding32Test) {
  const Pair32& param = GetParam();
  uint32_t a = param.a;
  uint32_t b = param.b;

  std::string s32;
  std::string s64;
  EXPECT_TRUE(VerifyOnePair<uint32_t>(a, b, &s32));

  // Validates the 64 encoder would generate the same output.
  EXPECT_TRUE(VerifyOnePair<uint64_t>(static_cast<uint64_t>(a),
                                      static_cast<uint64_t>(b), &s64));
  EXPECT_EQ(s32, s64);

  // Check the reverse case.
  std::string rev_s32;
  std::string rev_s64;
  EXPECT_TRUE(VerifyOnePair<uint32_t>(b, a, &rev_s32));
  EXPECT_TRUE(VerifyOnePair<uint64_t>(static_cast<uint64_t>(b),
                                      static_cast<uint64_t>(a), &rev_s64));
  EXPECT_EQ(rev_s32, rev_s64);
}

INSTANTIATE_TEST_SUITE_P(TwoValueVarint32, TwoValueVarint32Test,
                         testing::ValuesIn(Get32TestPairs()),
                         [](const testing::TestParamInfo<Pair32>& info) {
                           return absl::StrCat("idx_", info.index, "_a_",
                                               info.param.a, "_b_",
                                               info.param.b);
                         });

class TwoValueVarint64Test : public testing::TestWithParam<Pair64> {};

TEST_P(TwoValueVarint64Test, Encoding64Test) {
  const Pair64& param = GetParam();
  uint64_t a = param.a;
  uint64_t b = param.b;

  EXPECT_TRUE(VerifyOnePair<uint64_t>(a, b));
  EXPECT_TRUE(VerifyOnePair<uint64_t>(b, a));
}

INSTANTIATE_TEST_SUITE_P(TwoValueVarint64, TwoValueVarint64Test,
                         testing::ValuesIn(Get64TestPairs()),
                         [](const testing::TestParamInfo<Pair64>& info) {
                           return absl::StrCat("idx_", info.index, "_a_",
                                               info.param.a, "_b_",
                                               info.param.b);
                         });

void Encoding32FuzzTest(uint32_t a, uint32_t b) {
  std::string s32;
  std::string s64;
  EXPECT_TRUE(VerifyOnePair<uint32_t>(a, b, &s32));
  EXPECT_TRUE(VerifyOnePair<uint64_t>(static_cast<uint64_t>(a),
                                      static_cast<uint64_t>(b), &s64));
  EXPECT_EQ(s32, s64);

  std::string rev_s32;
  std::string rev_s64;
  EXPECT_TRUE(VerifyOnePair<uint32_t>(b, a, &rev_s32));
  EXPECT_TRUE(VerifyOnePair<uint64_t>(static_cast<uint64_t>(b),
                                      static_cast<uint64_t>(a), &rev_s64));
  EXPECT_EQ(rev_s32, rev_s64);
}

#if TWO_VALUES_VARINT_TEST_HAS_THREADS
FUZZ_TEST(TwoValueVarint32FuzzTest, Encoding32FuzzTest);
#endif

void Encoding64FuzzTest(uint64_t a, uint64_t b) {
  EXPECT_TRUE(VerifyOnePair<uint64_t>(a, b));
  EXPECT_TRUE(VerifyOnePair<uint64_t>(b, a));
}

#if TWO_VALUES_VARINT_TEST_HAS_THREADS
FUZZ_TEST(TwoValueVarint64FuzzTest, Encoding64FuzzTest);
#endif

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
  return sum_len / vals.size();
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
    for (int i = 0; i < kNumValues; ++i) {
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
