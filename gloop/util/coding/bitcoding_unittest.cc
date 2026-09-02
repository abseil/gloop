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

#include "gloop/util/coding/bitcoding.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <array>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/fixed_array.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/numeric/int128.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/types/span.h"
#include "benchmark/benchmark.h"
#include "gloop/util/coding/coder.h"
#include "gloop/util/gtl/unique_array.h"
#include "gloop/util/random/acmrandom.h"
#include "gloop/util/random/distributions.h"
#include "gtest/gtest.h"

ABSL_FLAG(int32_t, value_range, 256,
          "Maximum end of range of values for random tests");

void PrintBuf(char* buf, int bit_len) {
  int len = (bit_len + 7) / 8;
  for (int i = 0; i < len; i++) {
    for (int j = 0; j < 8; j++) {
      if (i * 8 + j < bit_len) {
        printf("%d", (buf[i] >> j) & 0x1);
      }
    }
  }
}

static uint32_t global_value = 0;

static const int N = 16384;  // Number of random values for BitsRequired.*
                             // Must be power of 2

// The number of ops that are performed per iter for each benchmark.  This
// increases the resolution of the benchmark output, so you can detect smaller
// changes in cost.  However, as is standard with benchmarks, beware of noise.
// You may need to run multiple trials to get statistically reliable deltas.
static const int kOpsPerIter = 1000;

static void BM_BitsRequiredRandom(benchmark::State& state) {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  std::vector<uint32_t> numbers;
  for (int i = 0; i < N; ++i) {
    numbers.push_back(util_random::SkewedLow<uint32_t>(rnd, 0, (1u << 31) - 1));
  }

  while (state.KeepRunning()) {
    for (int i = 0; i < kOpsPerIter; ++i) {
      ::benchmark::DoNotOptimize(
          BitEncoder::BitsRequired(numbers[i & (N - 1)]));
    }
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          kOpsPerIter * sizeof(uint32_t));
}

static void BM_BitsRequiredRandom64(benchmark::State& state) {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  std::vector<uint64_t> numbers;
  for (int i = 0; i < N; ++i) {
    numbers.push_back(
        util_random::SkewedLow<uint64_t>(rnd, 0, (uint64_t{1} << 63) - 1));
  }

  while (state.KeepRunning()) {
    for (int i = 0; i < kOpsPerIter; ++i) {
      ::benchmark::DoNotOptimize(
          BitEncoder::BitsRequired(numbers[i & (N - 1)]));
    }
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          kOpsPerIter * sizeof(uint64_t));
}

static void BM_BitsRequiredTableDrivenRandom(benchmark::State& state) {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  std::vector<uint32_t> numbers;
  for (int i = 0; i < N; ++i) {
    numbers.push_back(util_random::SkewedLow<uint32_t>(rnd, 0, (1u << 31) - 1));
  }
  uint32_t sum = 0;
  for (auto _ : state) {
    for (int i = 0; i < kOpsPerIter; ++i) {
      sum += BitEncoder::BitsRequiredTableDriven(numbers[i & (N - 1)]);
    }
  }

  global_value += sum;  // Prevent optimizing away everything
}

static void BM_BitsRequiredSameValue(benchmark::State& state) {
  std::vector<uint32_t> numbers;
  numbers.reserve(N);
  for (int i = 0; i < N; ++i) {
    numbers.push_back(state.range(0));
  }
  uint32_t sum = 0;

  for (auto _ : state) {
    for (int i = 0; i < kOpsPerIter; ++i) {
      sum += BitEncoder::BitsRequired(numbers[i & (N - 1)]);
    }
  }
  global_value += sum;  // Prevent optimizing away everything
}

static void BM_BitsRequiredTableDrivenSameValue(benchmark::State& state) {
  std::vector<uint32_t> numbers;
  numbers.reserve(N);
  for (int i = 0; i < N; ++i) {
    numbers.push_back(state.range(0));
  }
  uint32_t sum = 0;

  for (auto _ : state) {
    for (int i = 0; i < kOpsPerIter; ++i) {
      sum += BitEncoder::BitsRequiredTableDriven(numbers[i & (N - 1)]);
    }
  }
  global_value += sum;  // Prevent optimizing away everything
}

static void BM_BitsRequiredWithRiceSameValue(benchmark::State& state) {
  std::vector<uint32_t> numbers;
  numbers.reserve(N);
  for (int i = 0; i < N; ++i) {
    numbers.push_back(BitEncoder::FloorLogBase2(state.range(0)));
  }
  uint32_t sum = 0;
  for (auto _ : state) {
    for (int i = 0; i < kOpsPerIter; ++i) {
      sum += BitEncoder::BitsRequiredWithRice(numbers[i & (N - 1)], i);
    }
  }
  global_value += sum;  // Prevent optimizing away everything
}

static void BM_BitsRequiredWithRiceRandom(benchmark::State& state) {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  std::vector<uint32_t> numbers;
  numbers.reserve(N);
  for (int i = 0; i < N; ++i) {
    numbers.push_back(BitEncoder::FloorLogBase2(
        util_random::SkewedLow<uint32_t>(rnd, 0, (1u << 31) - 1)));
  }
  uint32_t sum = 0;
  for (auto _ : state) {
    for (int i = 0; i < kOpsPerIter; ++i) {
      sum += BitEncoder::BitsRequiredWithRice(numbers[i & (N - 1)], i);
    }
  }
  global_value += sum;  // Prevent optimizing away everything
}

BENCHMARK(BM_BitsRequiredRandom);
BENCHMARK(BM_BitsRequiredRandom64);
BENCHMARK(BM_BitsRequiredTableDrivenRandom);
BENCHMARK(BM_BitsRequiredWithRiceRandom);
BENCHMARK(BM_BitsRequiredSameValue)->Arg(0xffffff);
BENCHMARK(BM_BitsRequiredTableDrivenSameValue)->Arg(0xffffff);
BENCHMARK(BM_BitsRequiredWithRiceSameValue)->Arg(0xffffff);

// This function verifies that nbits number of bits are required to
// encode the supplied number.
bool VerifyBitsRequired(uint64_t x, uint32_t nbits) {
  uint64_t lower_bound = 1ULL << (nbits - 1);
  uint64_t upper_bound = 1ULL << nbits;
  return ((x >= lower_bound) && (x < upper_bound));
}

// Test will fail under asan if buf overflows.
template <class F>
void TestOutputFits(int maxbits, const F& f) {
  int maxbytes = (maxbits + 7) / 8;
  int padbits = maxbytes * 8 - maxbits;
  auto buf = gtl::MakeUniqueArray<char>(maxbytes);
  BitEncoder e(&buf[0], maxbytes);
  e.PutBits(0, padbits);
  uint64_t nbits = e.Bits();
  f(e);
  e.Flush(0);
  CHECK_LE(e.Bits() - nbits, maxbits);
}

TEST(MaxBitsRequired, VarInt) {
  TestOutputFits(BitEncoder::kMaxGammaBits, [](BitEncoder& e) {
    e.PutGamma(std::numeric_limits<uint32_t>::max());
  });
  TestOutputFits(BitEncoder::kMaxVarIntBits, [](BitEncoder& e) {
    e.PutVarInt(1, std::numeric_limits<uint32_t>::max());
  });
  TestOutputFits(BitEncoder::kMaxVarInt64Bits, [](BitEncoder& e) {
    e.PutVarInt64(1, std::numeric_limits<uint64_t>::max());
  });
}

TEST(BitsRequired64, BasicTests) {
  // Test boundary conditions.
  EXPECT_EQ(1, BitEncoder::BitsRequired64(0));
  EXPECT_EQ(32,
            BitEncoder::BitsRequired64(std::numeric_limits<uint32_t>::max()));
  EXPECT_EQ(33, BitEncoder::BitsRequired64(uint64_t{1} << 32));
  EXPECT_EQ(64,
            BitEncoder::BitsRequired64(std::numeric_limits<uint64_t>::max()));

  // For values < kuint32max, the output of BitsRequired64() should
  // agree with BitsRequired().
  //
  // Test powers of two that fit 32 bits, +/- displacements
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  for (uint32_t i = 0; i < 32; ++i) {
    uint32_t base = 1 << i;
    uint32_t randvalue = absl::Uniform<int32_t>(rnd, 0, base - 1);

    EXPECT_EQ(BitEncoder::BitsRequired(base - 1),
              BitEncoder::BitsRequired64(base - 1));
    EXPECT_EQ(BitEncoder::BitsRequired(base), BitEncoder::BitsRequired64(base));
    EXPECT_EQ(BitEncoder::BitsRequired(base + 1),
              BitEncoder::BitsRequired64(base + 1));
    EXPECT_EQ(BitEncoder::BitsRequired(base + randvalue),
              BitEncoder::BitsRequired64(base + randvalue));
  }

  // Test for numbers > kuint32max.
  for (uint32_t i = 0; i < 32; ++i) {
    uint32_t base32 = 1ULL << i;
    uint32_t randvalue = absl::Uniform<int32_t>(rnd, 0, base32 - 1);
    uint64_t base64 = (1ULL << 32) + static_cast<uint64_t>(base32);

    EXPECT_TRUE(
        VerifyBitsRequired(base64 - 1, BitEncoder::BitsRequired64(base64 - 1)));
    EXPECT_TRUE(VerifyBitsRequired(base64, BitEncoder::BitsRequired64(base64)));
    EXPECT_TRUE(
        VerifyBitsRequired(base64 + 1, BitEncoder::BitsRequired64(base64 + 1)));
    EXPECT_TRUE(VerifyBitsRequired(
        base64 + randvalue, BitEncoder::BitsRequired64(base64 + randvalue)));
  }
}

TEST(BitsRequiredWithRice, BasicTest) {
  // Verify boundary cases.
  EXPECT_EQ(1, BitEncoder::BitsRequiredWithRice(0, 0));
  EXPECT_EQ(0, BitEncoder::BitsRequiredWithRice(
                   0, std::numeric_limits<uint32_t>::max()));
  EXPECT_EQ(33, BitEncoder::BitsRequiredWithRice(
                    31, std::numeric_limits<uint32_t>::max()));
  EXPECT_EQ(4294967295, BitEncoder::BitsRequiredWithRice(
                            0, std::numeric_limits<uint32_t>::max() - 1));
  EXPECT_EQ(33, BitEncoder::BitsRequiredWithRice(
                    31, std::numeric_limits<uint32_t>::max() - 1));
  EXPECT_EQ(2147483649, BitEncoder::BitsRequiredWithRice(0, 1ULL << 31));
  EXPECT_EQ(33, BitEncoder::BitsRequiredWithRice(31, 1ULL << 31));

  // Verify reserved values for some random numbers and random rice.
  BitEncoder be;
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  uint64_t encoded_bits = 0;
  for (uint32_t i = 0; i < 32; ++i) {
    uint32_t numbase = 1ULL << i;
    uint32_t randnum = absl::Uniform<int32_t>(rnd, 0, numbase - 1);
    for (uint32_t j = 0; j < i; ++j) {
      int bits_required = BitEncoder::BitsRequiredWithRice(j, randnum);

      EXPECT_EQ(bits_required, j + (1 + (randnum >> j)));
      be.EnsureBits(bits_required);
      be.PutRice(j, randnum);

      encoded_bits += bits_required;
      EXPECT_EQ(be.Bits(), encoded_bits);
    }
  }
}

TEST(EncodeDecode, Rice) {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  const int kSmallKRepetitions = 100;
  const int kLargeKRepetitions = 1000;
  for (int k = 1; k < 32; ++k) {
    BitEncoder be;
    std::vector<uint32_t> inputs;
    const int repetitions = (k < 10) ? kSmallKRepetitions : kLargeKRepetitions;
    for (int i = 0; i < repetitions; ++i) {
      uint32_t numbase = 1ULL << (i % 32);
      uint32_t randnum = absl::Uniform<int32_t>(rnd, 0, numbase - 1);
      inputs.push_back(randnum);
      int bits_required = BitEncoder::BitsRequiredWithRice(k, randnum);
      be.EnsureBits(bits_required);
      be.PutRice(k, randnum);
    }
    be.Flush(0);
    BitDecoder d(be.base(), be.Bits() / 8);
    for (const auto input : inputs) {
      uint32_t v;
      d.GetRice(k, &v);
      EXPECT_EQ(input, v);
    }
  }
}

TEST(EncodeDecode, Rice64) {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  const int kSmallKRepetitions = 100;
  const int kLargeKRepetitions = 1000;
  for (int k = 1; k < 32; ++k) {
    BitEncoder be;
    std::vector<uint64_t> inputs;
    const int repetitions = (k < 10) ? kSmallKRepetitions : kLargeKRepetitions;
    for (int i = 0; i < repetitions; ++i) {
      uint64_t numbase = 1ULL << (i % 64);
      uint64_t randnum = absl::Uniform<int32_t>(rnd, 0, numbase - 1);
      inputs.push_back(randnum);
      int bits_required = BitEncoder::BitsRequiredWithRice(k, randnum);
      be.EnsureBits(bits_required);
      be.PutRice64(k, randnum);
    }
    be.Flush(0);
    BitDecoder d(be.base(), be.Bits() / 8);
    for (const auto input : inputs) {
      uint64_t v;
      d.GetRice64(k, &v);
      EXPECT_EQ(input, v);
    }
  }
}

TEST(EncodeDecode, ProgressiveRice) {
  // This test verifies round trip encode / decode with 32-bit progressive Rice
  // coding for multiple values of k, u_max, and m, for random symbols and
  // power-of-two and power-of-two-minus-one edge cases.
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  const int kSmallKRepetitions = 100;
  const int kLargeKRepetitions = 1000;
  for (int k = 0; k < 32; ++k) {
    for (int u_max = 1; u_max < 32; u_max *= 2) {
      for (int m = 1; m < 32; m *= 2) {
        BitEncoder be;
        std::vector<uint32_t> inputs;
        // Add random symbols to inputs.
        const int repetitions =
            (k < 10) ? kSmallKRepetitions : kLargeKRepetitions;
        for (int i = 0; i < repetitions; ++i) {
          uint32_t numbase = 1ULL << (i % 32);
          uint32_t randnum = absl::Uniform<int32_t>(rnd, 0, numbase - 1);
          inputs.push_back(randnum);
        }
        // Add edge cases to inputs.
        inputs.push_back(uint32_t{0});
        inputs.push_back(~uint32_t{0});
        for (int shift = 0; shift < 32; ++shift) {
          inputs.push_back(uint32_t{1} << shift);
          inputs.push_back((uint32_t{1} << shift) - 1);
        }
        // Encode inputs.
        for (const auto input : inputs) {
          int bits_required = BitEncoder::MaxBitsRequiredWithProgressiveRice(
              k, u_max, m, input);
          be.EnsureBits(bits_required);
          be.PutProgressiveRice(k, u_max, m, input);
        }
        be.Flush(0);
        // Decode and verify.
        BitDecoder d(be.base(), be.Bits() / 8);
        for (const auto input : inputs) {
          uint32_t v;
          d.GetProgressiveRice(k, u_max, m, &v);
          EXPECT_EQ(input, v);
        }
      }
    }
  }
}

TEST(EncodeDecode, ProgressiveRice64) {
  // This test verifies round trip encode / decode with 64-bit progressive Rice
  // coding for multiple values of k, u_max, and m, for random symbols and
  // power-of-two and power-of-two-minus-one edge cases.
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  const int kSmallKRepetitions = 100;
  const int kLargeKRepetitions = 1000;
  for (int k = 0; k < 64; ++k) {
    for (int u_max = 1; u_max < 64; u_max *= 4) {
      for (int m = 1; m < 64; m *= 4) {
        BitEncoder be;
        std::vector<uint64_t> inputs;
        // Add random symbols to inputs.
        const int repetitions =
            (k < 10) ? kSmallKRepetitions : kLargeKRepetitions;
        for (int i = 0; i < repetitions; ++i) {
          uint64_t numbase = 1ULL << (i % 64);
          uint64_t randnum = absl::Uniform<int64_t>(rnd, 0, numbase - 1);
          inputs.push_back(randnum);
        }
        // Add edge cases to inputs.
        inputs.push_back(uint64_t{0});
        inputs.push_back(~uint64_t{0});
        for (int shift = 0; shift < 64; ++shift) {
          inputs.push_back(uint64_t{1} << shift);
          inputs.push_back((uint64_t{1} << shift) - 1);
        }
        // Encode inputs.
        for (const auto input : inputs) {
          int bits_required = BitEncoder::MaxBitsRequiredWithProgressiveRice64(
              k, u_max, m, input);
          be.EnsureBits(bits_required);
          be.PutProgressiveRice64(k, u_max, m, input);
        }
        be.Flush(0);
        // Decode and verify.
        BitDecoder d(be.base(), be.Bits() / 8);
        for (const auto input : inputs) {
          uint64_t v;
          d.GetProgressiveRice64(k, u_max, m, &v);
          EXPECT_EQ(input, v);
        }
      }
    }
  }
}

TEST(EncodeDecode, Unary) {
  BitEncoder be;
  EXPECT_TRUE(be.ensure_allowed());
  for (int i = 1; i < 100; ++i) {
    be.EnsureBits(i + 1);
    be.PutUnary(i);
  }
  be.Flush(1);
  BitDecoder bd(be.base(), be.Bits() / 8);
  uint32_t val;
  for (int i = 1; i < 100; ++i) {
    ASSERT_TRUE(bd.GetUnary(&val));
    EXPECT_EQ(val, i);
  }
  uint32_t dummy;
  // 1 bit padding should make this fail
  EXPECT_FALSE(bd.GetUnary(&dummy));
}

TEST(EncodeDecode, InvertedUnary) {
  BitEncoder be;
  for (int i = 1; i < 1000; ++i) {
    be.EnsureBits(i + 1);
    be.PutInvertedUnary(i);
  }
  be.Flush(0);
  BitDecoder bd(be.base(), be.Bits() / 8);
  uint32_t val;
  for (int i = 1; i < 1000; ++i) {
    ASSERT_TRUE(bd.GetInvertedUnary(&val));
    EXPECT_EQ(val, i);
  }
  uint32_t dummy;
  // 0 bit padding should make this fail
  EXPECT_FALSE(bd.GetInvertedUnary(&dummy));
}

TEST(EncodeDecode, VarInt) {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  const int kRepetitions = 1000;
  // PutVarInt() warns that it's unsafe on certain inputs (log_base not a
  // divisor of 32, with a large enough remainder). Avoid this by keeping
  // log_base no larger than 16 and the encoded numbers less than 23 bits.
  for (int k = 1; k < 17; ++k) {
    LOG(INFO) << "Testing k = " << k;
    BitEncoder be;
    std::vector<uint32_t> inputs;
    for (uint32_t i = 0; i < kRepetitions; ++i) {
      uint32_t numbase = 1ULL << (i % 22);
      uint32_t randnum = absl::Uniform<int32_t>(rnd, 0, numbase - 1);
      inputs.push_back(randnum);
      int bits_required = BitEncoder::BitsRequiredWithRice(k, randnum);
      be.EnsureBits(bits_required);
      be.PutVarInt(k, randnum);
    }
    be.Flush(0);
    BitDecoder d(be.base(), be.Bits() / 8);
    for (const auto input : inputs) {
      uint32_t v;
      d.GetVarInt(k, &v);
      EXPECT_EQ(input, v);
    }
  }
}

TEST(EncodeDecode, VarIntOversizedShift) {
  char buf[32] = {};
  buf[0] = 0xFF;
  buf[1] = 0xC0;
  BitDecoder decoder(buf, sizeof(buf));
  uint32_t val = 0;
  EXPECT_FALSE(decoder.GetVarInt(4, &val));
}

TEST(EncodeDecode, VarInt64OversizedShift) {
  char buf[32] = {};
  buf[0] = 0xFF;
  buf[1] = 0xFF;
  buf[2] = 0xC0;
  BitDecoder decoder(buf, sizeof(buf));
  uint64_t val = 0;
  EXPECT_FALSE(decoder.GetVarInt64(4, &val));
}

TEST(EncodeDecode, VarInt32B) {
  for (uint32_t i = 0; i < 4; i++) {
    for (uint32_t j = 0; j < 4; j++) {
      char buf[16];
      Encoder e(buf, 16);
      e.put_varint32(i);
      e.put_varint32(j);

      Decoder d(buf, e.length());
      uint32_t val_i, val_j;
      ASSERT_TRUE(d.get_varint32(&val_i));
      EXPECT_EQ(val_i, i);
      ASSERT_TRUE(d.get_varint32(&val_j));
      EXPECT_EQ(val_j, j);

      d.reset(buf, e.length() - 1);
      ASSERT_TRUE(d.get_varint32(&val_i));
      EXPECT_EQ(val_i, i);
      ASSERT_FALSE(d.get_varint32(&val_j));

      d.reset(buf, e.length() + 1);
      ASSERT_TRUE(d.get_varint32(&val_i));
      EXPECT_EQ(val_i, i);
      ASSERT_TRUE(d.get_varint32(&val_j));
      EXPECT_EQ(val_j, j);
    }
  }
}

TEST(EncodeDecode, VarInt32) {
  const int N = 1000000;
  absl::FixedArray<char> b(N);
  Encoder e(b.data(), N);
  ACMRandom rnd(5);
  uint32_t checksum = 0;
  while ((e.length() + Encoder::kVarintMax32) < N) {
    uint32_t v;
    if (rnd.OneIn(3)) {
      v = rnd.OneIn(2) ? absl::Uniform<int32_t>(rnd, 0, 1 << 16)
                       : absl::Uniform<int32_t>(rnd, 0, 1 << 30);
    } else {
      v = absl::Uniform<int32_t>(rnd, 0,
                                 128);  // 2/3rds of the values fit in one byte
    }
    e.put_varint32(v);
    checksum ^= v;
  }

  Decoder d(b.data(), e.length());
  uint32_t v;
  uint32_t checksum2 = 0;
  while (d.get_varint32(&v)) {
    checksum2 ^= v;
  }
  EXPECT_EQ(checksum, checksum2);
}

TEST(BitDecoder, GetUnaryNoZeros) {
  char buf[72];
  memset(buf, 0xff, 72);
  BitDecoder d(buf, 72);
  uint32_t v;
  EXPECT_FALSE(d.GetUnary(&v)) << "Unexpectedly extracted " << v;
}

TEST(BitDecoder, GetBits64NotEnoughBits) {
  char buf[8] = {};  // vlues don't matter
  BitDecoder d(buf, std::size(buf));
  uint32_t v;
  uint64_t u = 0xff;
  // Only 40 bits left after first extraction.
  ASSERT_TRUE(d.GetBits(32, &v));
  EXPECT_FALSE(d.GetBits64(64, &u)) << "Unexpectedly extracted" << u;
}

TEST(BitDecoder, GetGammaNoZeros) {
  unsigned char buf[1] = {0xff};
  BitDecoder d(buf, std::size(buf));
  uint32_t v;
  EXPECT_FALSE(d.GetGamma(&v)) << "Unexpectedly extracted " << v;
}

TEST(BitDecoder, GetGammaNoValue) {
  // Buf is used entirely for the unary encoding.
  char buf[1] = {0x7f};
  BitDecoder d(buf, std::size(buf));
  uint32_t v;
  EXPECT_FALSE(d.GetGamma(&v)) << "Unexpectedly extracted " << v;
}

TEST(BitDecoder, GetGamma32BitUnaryValue) {
  // Encode a 32-bit unary value and remainder. This is being tested because it
  // is a boundary condition that should pass.
  std::array<unsigned char, 9> buf = {0xff, 0xff, 0xff, 0x7f, 0x00,
                                      0x00, 0x00, 0x00, 0x00};
  BitDecoder d(buf.data(), buf.size());
  uint32_t v;
  EXPECT_TRUE(d.GetGamma(&v));
}

TEST(BitDecoder, GetGamma33BitUnaryValue) {
  // The decoder should fail if the we read a very long unary value as that
  // would cause buffer overflow and produce an invalid value.
  std::array<unsigned char, 9> buf = {0xff, 0xff, 0xff, 0xff, 0x00,
                                      0x00, 0x00, 0x00, 0x00};
  BitDecoder d(buf.data(), buf.size());
  uint32_t v;
  EXPECT_FALSE(d.GetGamma(&v)) << "Unexpectedly extracted " << v;
}

TEST(BitDecoder, GetRiceNoZeros) {
  unsigned char buf[1] = {static_cast<unsigned char>(0xff)};
  BitDecoder d(buf, std::size(buf));
  uint32_t v;
  EXPECT_FALSE(d.GetRice(5, &v)) << "Unexpectedly extracted " << v;
}

TEST(BitDecoder, GetRice64NoZeros) {
  unsigned char buf[1] = {static_cast<unsigned char>(0xff)};
  BitDecoder d(buf, std::size(buf));
  uint64_t v;
  EXPECT_FALSE(d.GetRice64(5, &v)) << "Unexpectedly extracted " << v;
}

TEST(BitEncoder, Moves) {
  BitEncoder a;
  a.EnsureBits(1);
  unsigned char buf[1];
  BitEncoder b(buf, 1);
  a = std::move(b);
  b = std::move(a);
  b.Clear();
  b.PutBits(12, 4);
  b.Flush(0);
  BitDecoder dec(buf, 1);
  uint32_t x;
  EXPECT_TRUE(dec.GetBits(4, &x));
  EXPECT_EQ(x, 12);
}

TEST(BitEncoder, BitsRequired) {
  // Test extremes
  EXPECT_EQ(BitEncoder::BitsRequired(0),
            BitEncoder::BitsRequiredTableDriven(0));
  EXPECT_EQ(BitEncoder::BitsRequired(static_cast<uint32_t>(-1)),
            BitEncoder::BitsRequiredTableDriven(static_cast<uint32_t>(-1)));

  // Test powers of two that fit 32 bits, +/- displacements
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  for (uint32_t i = 0; i < 32; ++i) {
    uint32_t base = 1 << i;
    uint32_t randvalue = absl::Uniform<int32_t>(rnd, 0, base - 1);

    EXPECT_EQ(BitEncoder::BitsRequired(base - 1),
              BitEncoder::BitsRequiredTableDriven(base - 1));
    EXPECT_EQ(BitEncoder::BitsRequired(base),
              BitEncoder::BitsRequiredTableDriven(base));
    EXPECT_EQ(BitEncoder::BitsRequired(base + 1),
              BitEncoder::BitsRequiredTableDriven(base + 1));
    EXPECT_EQ(BitEncoder::BitsRequired(base + randvalue),
              BitEncoder::BitsRequiredTableDriven(base + randvalue));
  }
}

TEST(BitEncoder, ReverseBits) {
  for (int i = 0; i < 10000; i++) {
    for (int l = 0; l <= 10; l++) {
      EXPECT_EQ(i & ((1 << l) - 1),
                BitEncoder::ReverseBits(l, BitEncoder::ReverseBits(l, i)))
          << ": i = " << i << ", l = " << l;
    }
  }
}

TEST(EncodeDecode, VarInt64) {
  for (int base = 1; base < 10; base++) {
    char buf[1024];
    for (int i = 0; i < 10000; i++) {
      for (int j = 0; j < 10; j++) {
        BitEncoder be(buf, sizeof(buf));
        EXPECT_FALSE(be.ensure_allowed());
        uint64_t val1 = i + (static_cast<uint64_t>(j) << 15);
        uint64_t val2 = i + (static_cast<uint64_t>(j) << 32);
        be.PutVarInt64(base, val1);
        int pos1 = be.Bits();
        be.PutVarInt64(base, val2);
        int pos2 = be.Bits();
        be.PutVarInt(base, 0);
        be.Flush(1);

        BitDecoder bd(buf, be.Bits() / 8);
        uint64_t x = 0;
        EXPECT_TRUE(bd.GetVarInt64(base, &x));
        EXPECT_EQ(x, val1);
        EXPECT_EQ(bd.BitsDecoded(), pos1);

        EXPECT_TRUE(bd.GetVarInt64(base, &x));
        EXPECT_EQ(x, val2);
        EXPECT_EQ(bd.BitsDecoded(), pos2);

        EXPECT_TRUE(bd.GetVarInt64(base, &x));
        EXPECT_EQ(0, x);
        EXPECT_FALSE(bd.GetVarInt64(base, &x));

        // Make sure the representation of a small 64-bit number is the
        // same as what we would have emitted for a 32-bit number, as
        // long as it is guaranteed to be representable with a 32-bit
        // remainder
        bd.reset(buf, be.Bits() / 8);
        uint32_t x32 = 0;
        ASSERT_TRUE(bd.GetUnary(&x32));
        // See whether the number is representable with a 32-bit remainder
        if (x32 * base <= 32) {
          bd.reset(buf, be.Bits() / 8);
          ASSERT_TRUE(bd.GetVarInt(base, &x32));
          EXPECT_EQ(x32, val1);
        }
      }
    }
  }
}

#if 0
// generates a unary decoding table that was used when writing a faster
// version of GetUnary in bitcoding.h
void Test3() {
  unsigned char buf[2];
  for (int i = 0; i < 256; i++) {
    buf[0] = i;
    buf[1] = 0;
    BitDecoder bd(buf, sizeof(buf));
    uint32 val;
    bd.GetUnary(&val);
    CHECK_EQ(val, bd.Pos());
    if (i % 16 == 0) {
      printf("\n/* %2x */ ", i);
    }
    printf(" %d,", val);
  }
}
#endif

TEST(EncodeDecode, MixedOperations) {
  for (int log_bufsize = 10; log_bufsize < 24; ++log_bufsize) {
    int bufsize = 1 << log_bufsize;
    auto buf = gtl::MakeUniqueArray<char>(bufsize);
    BitEncoder be(buf.data(), bufsize);
    ACMRandom acm_rand(122);
    uint32_t cnt = 0;
    while (true) {
      uint32_t N = (absl::Uniform<uint32_t>(acm_rand) %
                    absl::GetFlag(FLAGS_value_range)) +
                   1;
      be.PutGamma(N);
      be.PutVarInt(2, N);
      be.PutVarInt(4, N);
      be.PutVarInt(8, N);
      if (N <= 160) {
        be.PutUnary(N);
        be.PutRice(2, N);
        be.PutRice(3, N);
      }
      cnt++;
      if (be.Bits() > static_cast<uint64_t>((bufsize - 100) * 8)) break;
    }
    be.Flush(1);

    acm_rand.Reset(122);
    BitDecoder bd(buf.data(), be.Bits() / 8);
    uint32_t x = 0;
    uint32_t i = 0;
    uint32_t expected;
    while (i < cnt) {
      expected = (absl::Uniform<uint32_t>(acm_rand) %
                  absl::GetFlag(FLAGS_value_range)) +
                 1;
      ASSERT_TRUE(bd.GetGamma(&x));
      EXPECT_EQ(x, expected);
      EXPECT_TRUE(bd.GetVarInt(2, &x));
      EXPECT_EQ(x, expected);
      EXPECT_TRUE(bd.GetVarInt(4, &x));
      EXPECT_EQ(x, expected);
      EXPECT_TRUE(bd.GetVarInt(8, &x));
      EXPECT_EQ(x, expected);
      if (expected <= 160) {
        EXPECT_TRUE(bd.GetUnary(&x));
        EXPECT_EQ(x, expected);
        EXPECT_TRUE(bd.GetRice(2, &x));
        EXPECT_EQ(x, expected);
        EXPECT_TRUE(bd.GetRice(3, &x));
        EXPECT_EQ(x, expected);
      }
      i++;
    }
    // Shouldn't be able to get a unary value at end
    EXPECT_FALSE(bd.GetUnary(&x));
  }
}

static void BM_MixedOperationsEncode(benchmark::State& state) {
  int bufsize = 1 << 24;
  auto buf = gtl::MakeUniqueArray<char>(bufsize);
  BitEncoder be(buf.data(), bufsize);
  ACMRandom acm_rand(122);
  std::vector<uint32_t> inputs;
  int total_items = 0;
  for (int i = 0; i < kOpsPerIter; ++i) {
    uint32_t N = (absl::Uniform<uint32_t>(acm_rand) % 32) + 1;
    inputs.push_back(N);
    total_items += 5;
    if (N <= 160) {
      total_items += 3;
    }
  }
  for (auto _ : state) {
    for (int N : inputs) {
      be.PutGamma(N);
      be.PutVarInt(2, N);
      be.PutVarInt(4, N);
      be.PutVarInt(8, N);
      be.PutBits(16, N);
      if (N <= 160) {
        be.PutUnary(N);
        be.PutRice(2, N);
        be.PutRice(3, N);
      }
    }
    ::benchmark::DoNotOptimize(be.base());
    be.Clear();
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                          total_items);
}

static void BM_MixedOperationsDecode(benchmark::State& state) {
  int bufsize = 1 << 24;
  auto buf = gtl::MakeUniqueArray<char>(bufsize);
  BitEncoder be(buf.data(), bufsize);
  ACMRandom acm_rand(122);
  int total_items = 0;
  for (int i = 0; i < kOpsPerIter; ++i) {
    int N = (absl::Uniform<uint32_t>(acm_rand) % 32) + 1;
    be.PutGamma(N);
    be.PutVarInt(2, N);
    be.PutVarInt(4, N);
    be.PutVarInt(8, N);
    be.PutBits(16, N);
    total_items += 5;
    if (N <= 160) {
      be.PutUnary(N);
      be.PutRice(2, N);
      be.PutRice(3, N);
      total_items += 3;
    }
  }

  for (auto _ : state) {
    BitDecoder bd(buf.data(), be.Bits() / 8);
    for (int i = 0; i < kOpsPerIter; ++i) {
      uint32_t garbage;
      bd.GetGamma(&garbage);
      ::benchmark::DoNotOptimize(garbage);
      bd.GetVarInt(2, &garbage);
      ::benchmark::DoNotOptimize(garbage);
      bd.GetVarInt(4, &garbage);
      ::benchmark::DoNotOptimize(garbage);
      bd.GetVarInt(8, &garbage);
      ::benchmark::DoNotOptimize(garbage);
      bd.GetBits(16, &garbage);
      ::benchmark::DoNotOptimize(garbage);
      if (N <= 160) {
        bd.GetUnary(&garbage);
        ::benchmark::DoNotOptimize(garbage);
        bd.GetRice(2, &garbage);
        ::benchmark::DoNotOptimize(garbage);
        bd.GetRice(3, &garbage);
        ::benchmark::DoNotOptimize(garbage);
      }
    }
  }
  state.SetItemsProcessed(static_cast<uint64_t>(state.iterations()) *
                          total_items);
}
BENCHMARK(BM_MixedOperationsEncode);
BENCHMARK(BM_MixedOperationsDecode);

TEST(BitEncoder, GrowSpaceIfNeeded) {
  BitEncoder e;
  for (int i = 0; i < 1000; i++) {
    e.EnsureBits(128);
    e.PutBits(i, 32);
    e.PutBits(i * 2, 32);
    e.PutBits(i * 3, 32);
    e.PutBits(i, 17);
    e.PutBits(i, 15);
  }
  int num_bits = e.Bits();
  e.Flush(1);
  e.EnsureBits(100);

  BitDecoder d(e.base(), e.Bits() / 8);
  for (int i = 0; i < 1000; i++) {
    uint32_t val;
    EXPECT_TRUE(d.GetBits(32, &val));
    EXPECT_EQ(val, i);
    EXPECT_TRUE(d.GetBits(32, &val));
    EXPECT_EQ(val, i * 2);
    EXPECT_TRUE(d.GetBits(32, &val));
    EXPECT_EQ(val, i * 3);
    EXPECT_TRUE(d.GetBits(17, &val));
    EXPECT_EQ(val, i);
    EXPECT_TRUE(d.GetBits(15, &val));
    EXPECT_EQ(val, i);
  }
  EXPECT_EQ(d.AvailBits(), e.Bits() - num_bits);
}

TEST(BitDecoder, SkipBits) {
  BitEncoder e;
  for (int i = 0; i < 1000; ++i) {
    e.EnsureBits(32);
    e.PutBits(i, 32);
  }
  e.Flush(1);

  BitDecoder d(e.base(), e.Bits() / 8);
  for (int i = 0; i < 200; ++i) {
    d.SkipBits(4 * 32);
    uint32_t x = 0;
    d.GetBits(32, &x);
    EXPECT_EQ(x, 5 * i + 4);
  }
}

TEST(BitDecoder, SkipBitsLarge) {
  // Test that skipping by more than std::numeric_limits<int32_t>::max() works.
  const int64_t big_skip = (static_cast<int64_t>(1) << 31) + 128;
  ASSERT_GE(big_skip, std::numeric_limits<int32_t>::max());
  ASSERT_EQ(big_skip % 8, 0);

  std::string encoding(big_skip / 8, 0);
  encoding.push_back('x');
  BitDecoder d(encoding.data(), encoding.size());
  d.SkipBits(big_skip);

  uint32_t result;
  ASSERT_TRUE(d.GetBits(8, &result));
  ASSERT_EQ(result, 'x');
}

TEST(SkipBits, Assert) {
  BitEncoder e;
  e.EnsureBits(64);
  e.PutBits(0x12345678, 32);
  e.PutBits(0x08172635, 32);
  e.Flush(0);
  BitDecoder d(e.base(), e.Bits() / 8);
  uint32_t x;
  d.GetBits(1, &x);
  EXPECT_EQ(0, x);
  d.SkipBits(35);
  EXPECT_TRUE(d.GetBits(28, &x));
  EXPECT_EQ(0x0817263, x);
  EXPECT_FALSE(d.GetBits(1, &x));
}

TEST(Bits, Put128) {
  BitEncoder e;
  e.EnsureBits(93);
  e.PutBits128(absl::MakeUint128(~0x1234567890ABCDEF, 0x1234567890ABCDEF), 93);
  e.Flush(0);
  EXPECT_EQ(e.Bits(), 96);
  BitDecoder d(e.base(), e.Bits() / 8);
  uint32_t x;
  EXPECT_TRUE(d.GetBits(32, &x));
  EXPECT_EQ(x, 0x90ABCDEF);
  EXPECT_TRUE(d.GetBits(32, &x));
  EXPECT_EQ(x, 0x12345678);
  EXPECT_TRUE(d.GetBits(32, &x));
  EXPECT_EQ(x, ~0xF0ABCDEF);
}

TEST(Bits, Flush64) {
  BitEncoder e;
  e.EnsureBits(37);
  e.PutBits(1, 25);
  e.PutBits(2, 12);
  e.Flush64(1);
  EXPECT_EQ(e.Bits(), 64);
  BitDecoder d(e.base(), e.Bits() / 8);
  uint32_t x;
  EXPECT_TRUE(d.GetBits(25, &x));
  EXPECT_EQ(x, 1);
  EXPECT_TRUE(d.GetBits(12, &x));
  EXPECT_EQ(x, 2);
  EXPECT_TRUE(d.GetBits(27, &x));  // pad
  EXPECT_EQ(x, 0x7FFFFFF);         // pad
  EXPECT_FALSE(d.GetBits(1, &x));
}

TEST(TransferBits, Basic) {
  BitEncoder enc, enc1, enc2;
  uint64_t val1 = 0x5555555555555555ULL;
  uint64_t val2 = 0xAAAAAAAAAAAAAAAAULL;

  enc1.EnsureBits(50);
  enc1.PutBits64(val1, 50);
  CHECK_EQ(enc1.Bits(), 50);

  enc2.EnsureBits(30);
  enc2.PutBits64(val2, 30);
  CHECK_EQ(enc2.Bits(), 30);

  enc.EnsureBits(60);
  enc.PutBits64(val2, 60);
  CHECK_EQ(enc.Bits(), 60);

  enc.EnsureBits(enc1.Bits());
  enc1.TransferBitsTo(&enc);
  CHECK_EQ(enc.Bits(), 60 + 50);
  CHECK_EQ(enc1.Bits(), 0);

  enc.EnsureBits(enc2.Bits());
  enc2.TransferBitsTo(&enc);
  CHECK_EQ(enc.Bits(), 60 + 50 + 30);
  CHECK_EQ(enc2.Bits(), 0);

  enc.Flush(0);
  BitDecoder decoder(enc.base(), enc.Bits() / 8);
  uint64_t val = 0;
  CHECK(decoder.GetBits64(64, &val));
  CHECK_EQ(val, uint64_t{0x5AAAAAAAAAAAAAAA});
  CHECK(decoder.GetBits64(64, &val));
  CHECK_EQ(val, uint64_t{0xAAAA955555555555u});
  CHECK(decoder.GetBits64(60 + 50 + 30 - 2 * 64, &val));
  CHECK_EQ(val, uint64_t{0xAAA});
}

TEST(EncodeDecode, PutTransferAndGet) {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  BitEncoder be;
  std::vector<uint64_t> inputs;
  const int kRepetitions = 1000;
  for (int i = 0; i < kRepetitions; ++i) {
    uint64_t numbase = 1ULL << (i % 64);
    uint64_t randnum = absl::Uniform<int32_t>(rnd, 0, numbase - 1);
    inputs.push_back(randnum);
    be.EnsureBits(BitEncoder::BitsRequired64(randnum));
    int num_bits = i % 64;
    be.PutBits64(randnum, num_bits);
  }
  be.Flush(0);

  int64_t num_bits = be.Bits();
  BitEncoder recipient;
  recipient.EnsureBits(be.Bits());
  be.TransferBitsTo(&recipient);
  // Make sure to write bits to underlying encoder, or decoder will use
  // uninitialized memory.
  recipient.Flush(0);
  BitDecoder d(recipient.base(), recipient.Bits() / 8);
  ASSERT_EQ(num_bits, recipient.Bits());
  for (int i = 0; i < kRepetitions; ++i) {
    uint64_t v;
    d.GetBits64(i % 64, &v);
    EXPECT_EQ(inputs[i], v);
  }
}

static void FillNumbers(int N, int skewed_base,
                        std::vector<uint32_t>* numbers) {
  ACMRandom rnd(301);  // Deterministic
  numbers->clear();
  for (int i = 0; i < N; i++) {
    numbers->push_back(absl::Uniform<int32_t>(rnd, 0, 1u << skewed_base));
  }
}

static void BM_GetRice(benchmark::State& state) {
  std::vector<uint32_t> numbers;
  const int k = state.range(0);
  FillNumbers(kOpsPerIter, k + 1, &numbers);
  BitEncoder e;
  for (int i = 0; i < kOpsPerIter; i++) {
    e.EnsureBits(4096);
    e.PutRice(k, numbers[i]);
  }
  e.Flush(0);
  int bytes = e.Bits() / 8;
  uint32_t A = 0;
  uint32_t B = 0;
  uint32_t C = 0;
  uint32_t D = 0;
  uint32_t sum = 0;
  for (auto _ : state) {
    BitDecoder d(e.base(), bytes);
    sum = 0;
    for (int i = 0; i < kOpsPerIter; i += 4) {
      d.GetRice(k, &A);
      d.GetRice(k, &B);
      d.GetRice(k, &C);
      d.GetRice(k, &D);
      sum += A + B + C + D;
    }
  }
  global_value += sum;  // Prevent optimizing away everything
}
BENCHMARK(BM_GetRice)->Arg(2)->Arg(6)->Arg(8)->Arg(10)->Arg(12);

static void BM_GetProgressiveRice(benchmark::State& state) {
  // Use typical values of u_max, m for benchmarking.
  constexpr int u_max = 12;
  constexpr int m = 4;

  const int k = state.range(0);
  std::vector<uint32_t> numbers;
  FillNumbers(kOpsPerIter, k + 1, &numbers);
  BitEncoder e;
  for (int i = 0; i < kOpsPerIter; i++) {
    e.EnsureBits(4096);
    e.PutProgressiveRice(k, u_max, m, numbers[i]);
  }
  e.Flush(0);

  int bytes = e.Bits() / 8;
  uint32_t A = 0;
  uint32_t B = 0;
  uint32_t C = 0;
  uint32_t D = 0;
  uint32_t sum = 0;
  for (auto _ : state) {
    BitDecoder d(e.base(), bytes);
    sum = 0;
    for (int i = 0; i < kOpsPerIter; i += 4) {
      d.GetProgressiveRice(k, u_max, m, &A);
      d.GetProgressiveRice(k, u_max, m, &B);
      d.GetProgressiveRice(k, u_max, m, &C);
      d.GetProgressiveRice(k, u_max, m, &D);
      sum += A + B + C + D;
    }
  }
  global_value += sum;  // Prevent optimizing away everything
}
BENCHMARK(BM_GetProgressiveRice)->Arg(2)->Arg(6)->Arg(8)->Arg(10)->Arg(12);

static void EncodeGroupedRice(absl::Span<const uint32_t> numbers, int k,
                              BitEncoder* e_unary, BitEncoder* e_k) {
  for (auto A : numbers) {
    e_unary->EnsureBits(4096);
    e_k->EnsureBits(4096);
    uint32_t A_prefix = 1 + (A >> k);
    e_unary->PutUnary(A_prefix);
    e_k->PutBits(A, k);
  }
  e_unary->Flush(0);
  e_k->Flush(0);
}

static void BM_GetGroupedRice(benchmark::State& state) {
  const int k = state.range(0);
  std::vector<uint32_t> numbers;
  FillNumbers(kOpsPerIter, k + 1, &numbers);
  BitEncoder e_unary, e_k;
  EncodeGroupedRice(numbers, k, &e_unary, &e_k);

  int bytes_unary = e_unary.Bits() / 8;
  int bytes_K = e_k.Bits() / 8;
  uint32_t A_unary = 0, B_unary = 0, C_unary = 0, D_unary = 0;
  uint32_t A_bits = 0, B_bits = 0, C_bits = 0, D_bits = 0;
  uint32_t sum = 0;
  for (auto _ : state) {
    BitDecoder d_unary(e_unary.base(), bytes_unary);
    BitDecoder d_k(e_k.base(), bytes_K);
    sum = 0;
    for (int i = 0; i < kOpsPerIter; i += 4) {
      d_unary.GetUnary(&A_unary);
      d_unary.GetUnary(&B_unary);
      d_unary.GetUnary(&C_unary);
      d_unary.GetUnary(&D_unary);
      d_k.GetBits(k, &A_bits);
      d_k.GetBits(k, &B_bits);
      d_k.GetBits(k, &C_bits);
      d_k.GetBits(k, &D_bits);
      uint32_t A = ((A_unary - 1) << k) + A_bits;
      uint32_t B = ((B_unary - 1) << k) + B_bits;
      uint32_t C = ((C_unary - 1) << k) + C_bits;
      uint32_t D = ((D_unary - 1) << k) + D_bits;
      sum += A + B + C + D;
    }
  }
  global_value += sum;  // Prevent optimizing away everything
}
BENCHMARK(BM_GetGroupedRice)->Arg(2)->Arg(6)->Arg(8)->Arg(10)->Arg(12);

static void BM_GetBits10(benchmark::State& state) {
  BitEncoder e;
  for (int i = 0; i < kOpsPerIter; i++) {
    e.EnsureBits(256);
    e.PutBits(i, 10);  // Values don't matter
  }
  e.Flush(0);
  uint32_t sum = 0;
  for (auto _ : state) {
    BitDecoder d(e.base(), e.Bits() / 8);
    uint32_t A = 0, B = 0, C = 0, D = 0;
    if (state.range(0) == 0) {
      for (int i = 0; i < kOpsPerIter; i += 4) {
        d.GetBits(10, &A);
        d.GetBits(10, &B);
        d.GetBits(10, &C);
        d.GetBits(10, &D);
        sum += A + B + C + D;
      }
    } else {
      // Experimental approach where we encode 4 10-bit values as
      // 4 8-bit values, and the remaining 4 2-bit portions of the
      // 4 numbers in a fifth byte.
      const char* p = e.base();
      for (int i = 0; i < kOpsPerIter; i += 4) {
        A = p[0];
        B = p[1];
        C = p[2];
        D = p[3];
        uint32_t extra = p[4];
        A += ((extra & 0x3) << 8);
        B += (((extra >> 2) & 0x3) << 8);
        C += (((extra >> 4) & 0x3) << 8);
        D += (((extra >> 6) & 0x3) << 8);
        p += 5;
        sum += A + B + C + D;
      }
    }
  }
  global_value += sum;  // Prevent optimizing away everything
}
BENCHMARK(BM_GetBits10)->Arg(0)->Arg(1);

// These templated benchmarks behave similarly to the macro-defined benchmarks
// (e.g. DEFINE_PUT_BENCHMARK), but implemented with templates. The values are
// chosen to be the same as the benchmarks for Put...() functions.
template <class DecodeType, int max_value_bits, int reserved_encoding_bits,
          void (BitEncoder::*encode_method)(DecodeType, int),
          bool (BitDecoder::*method)(int, DecodeType*)>
static void BM_GetBits(benchmark::State& state) {
  BitEncoder e;
  e.EnsureBits(reserved_encoding_bits * kOpsPerIter);
  std::vector<int> bit_counts;
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  for (int i = 0; i < kOpsPerIter; i++) {
    int num_bits =
        util_random::SkewedLow<int>(rnd, 0, (1 << max_value_bits) - 1);
    (e.*encode_method)(i, num_bits);  // Values don't matter
    bit_counts.push_back(num_bits);
  }
  e.Flush(0);
  DecodeType a = 0;
  for (auto _ : state) {
    BitDecoder d(e.base(), e.Bits() / 8);
    for (const auto i : bit_counts) {
      (d.*method)(i, &a);
      ::benchmark::DoNotOptimize(a);
    }
  }
  state.SetItemsProcessed(static_cast<int64_t>(kOpsPerIter) *
                          state.iterations());
}

BENCHMARK_TEMPLATE(BM_GetBits, uint32_t, 5, 8, &BitEncoder::PutBits,
                   &BitDecoder::GetBits);
BENCHMARK_TEMPLATE(BM_GetBits, uint64_t, 6, 16, &BitEncoder::PutBits64,
                   &BitDecoder::GetBits64);

template <int max_value_bits, int reserved_encoding_bits,
          void (BitEncoder::*encode_method)(uint32_t),
          bool (BitDecoder::*method)(uint32_t*)>
static void BM_GetBitsUnary(benchmark::State& state) {
  BitEncoder e;
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  e.EnsureBits(reserved_encoding_bits * kOpsPerIter);
  for (int i = 0; i < kOpsPerIter; i++) {
    int unary_val = util_random::SkewedLow<int>(rnd, 1, 1 << max_value_bits);
    (e.*encode_method)(unary_val);
  }
  e.Flush(0);
  uint32_t a = 0;
  for (auto _ : state) {
    BitDecoder d(e.base(), e.Bits() / 8);
    for (int i = 0; i < kOpsPerIter; ++i) {
      (d.*method)(&a);
      ::benchmark::DoNotOptimize(a);
    }
  }
  state.SetItemsProcessed(static_cast<int64_t>(kOpsPerIter) *
                          state.iterations());
}

BENCHMARK_TEMPLATE(BM_GetBitsUnary, 6, 16, &BitEncoder::PutUnary,
                   &BitDecoder::GetUnary);
BENCHMARK_TEMPLATE(BM_GetBitsUnary, 6, 16, &BitEncoder::PutInvertedUnary,
                   &BitDecoder::GetInvertedUnary);
BENCHMARK_TEMPLATE(BM_GetBitsUnary, 6, 16, &BitEncoder::PutGamma,
                   &BitDecoder::GetGamma);

template <class DecodeType, int max_value_bits, int reserved_encoding_bits,
          int log_base, void (BitEncoder::*encode_method)(int, DecodeType),
          bool (BitDecoder::*method)(int, DecodeType*)>
static void BM_GetBitsLogbase(benchmark::State& state) {
  const DecodeType upper_bound =
      max_value_bits < std::numeric_limits<DecodeType>::digits
          ? (1ull << (max_value_bits - 1)) |
                ((1ull << (max_value_bits - 1)) - 1)
          : std::numeric_limits<DecodeType>::max();

  BitEncoder e;
  e.EnsureBits(reserved_encoding_bits * kOpsPerIter);
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  for (int i = 0; i < kOpsPerIter; i++) {
    DecodeType value = util_random::SkewedLow<DecodeType>(rnd, 0, upper_bound);
    (e.*encode_method)(log_base, value);
  }
  e.Flush(0);
  DecodeType a = 0;
  for (auto _ : state) {
    BitDecoder d(e.base(), e.Bits() / 8);
    for (int i = 0; i < kOpsPerIter; ++i) {
      (d.*method)(log_base, &a);
      ::benchmark::DoNotOptimize(a);
    }
  }
  state.SetItemsProcessed(static_cast<int64_t>(kOpsPerIter) *
                          state.iterations());
}

BENCHMARK_TEMPLATE(BM_GetBitsLogbase, uint32_t, 32, 32, 8,
                   &BitEncoder::PutVarInt, &BitDecoder::GetVarInt);
BENCHMARK_TEMPLATE(BM_GetBitsLogbase, uint64_t, 64, 64, 8,
                   &BitEncoder::PutVarInt64, &BitDecoder::GetVarInt64);
BENCHMARK_TEMPLATE(BM_GetBitsLogbase, uint32_t, 22, 32, 16,
                   &BitEncoder::PutRice, &BitDecoder::GetRice);
BENCHMARK_TEMPLATE(BM_GetBitsLogbase, uint64_t, 38, 64, 32,
                   &BitEncoder::PutRice64, &BitDecoder::GetRice64);

static void BM_ReverseBits(benchmark::State& state) {
  uint32_t x = 0xaaaaaaaa;
  for (auto _ : state) {
    for (int i = 0; i < kOpsPerIter; ++i) {
      x = BitEncoder::ReverseBits(state.range(0), x);
    }
    // Ensure that the compiler does not make all this looping a no-op
    CHECK_NE(x + 1, x);
  }
}

BENCHMARK(BM_ReverseBits)
    ->Arg(0)
    ->Arg(2)
    ->Arg(4)
    ->Arg(5)
    ->Arg(8)
    ->Arg(16)
    ->Arg(20)
    ->Arg(21)
    ->Arg(32);

static void BM_GetUnary(benchmark::State& state) {
  std::vector<uint32_t> numbers;
  FillNumbers(kOpsPerIter, 8, &numbers);

  BitEncoder e;
  for (int i = 0; i < kOpsPerIter; i++) {
    e.EnsureBits(256);
    e.PutUnary(numbers[i] + 1);
  }
  e.Flush(state.range(0));
  BitDecoder d(e.base(), e.Bits() / 8);

  for (auto _ : state) {
    uint32_t x = 0;
    uint32_t sum = 0;

    for (int i = 0; i < kOpsPerIter; i++) {
      if (state.range(0)) {
        d.GetInvertedUnary(&x);
      } else {
        d.GetUnary(&x);
      }
      sum += x;
    }
    global_value += sum;  // Dummy use
  }
}

BENCHMARK(BM_GetUnary)->Arg(0)->Arg(1);

TEST(InvertedUnary, LargeValueSpanningPartialWord) {
  BitEncoder bitenc;
  uint32_t values[] = {5, 4, 128};

  bitenc.EnsureBits(1024);

  for (auto A : values) {
    bitenc.PutInvertedUnary(A);
  }
  bitenc.Flush(0);

  BitDecoder bitdec;

  const char* ptr;
  int len;

  ptr = bitenc.base();
  len = bitenc.Bits() / 8;
  if (bitenc.Bits() % 8 != 0) len++;

  bitdec.reset(ptr, len);

  uint32_t val = 0;
  for (auto A : values) {
    ASSERT_TRUE(bitdec.GetInvertedUnary(&val));
    ASSERT_EQ(A, val);
  }
  ASSERT_FALSE(bitdec.GetInvertedUnary(&val));
}

#define DEFINE_PUT_BENCHMARK(method, value_type, max_value_bits,          \
                             reserved_encoding_bits)                      \
  static void BM_##method(benchmark::State& state) {                      \
    ACMRandom rnd(ACMRandom::DeterministicSeed());                        \
    std::vector<uint32_t> num_bits;                                       \
    for (int n = 0; n < kOpsPerIter; ++n) {                               \
      num_bits.push_back(                                                 \
          util_random::SkewedLow<uint32_t>(rnd, 1, 1 << max_value_bits)); \
    }                                                                     \
    const value_type val = static_cast<value_type>(0x12b9b0a112b9b0a1UL); \
    BitEncoder bitenc;                                                    \
    bitenc.EnsureBits(reserved_encoding_bits * kOpsPerIter);              \
    for (auto _ : state) {                                                \
      for (int n = 0; n < kOpsPerIter; ++n) {                             \
        bitenc.method(val, num_bits[n]);                                  \
      }                                                                   \
      bitenc.Flush(0);                                                    \
      global_value += bitenc.Bits();                                      \
      bitenc.Clear();                                                     \
    }                                                                     \
  }                                                                       \
  BENCHMARK(BM_##method);

#define DEFINE_PUT_UNARY_BENCHMARK(method, max_value_bits,                \
                                   reserved_encoding_bits)                \
  static void BM_##method(benchmark::State& state) {                      \
    ACMRandom rnd(ACMRandom::DeterministicSeed());                        \
    std::vector<uint32_t> unary_val;                                      \
    for (int i = 0; i < kOpsPerIter; ++i) {                               \
      unary_val.push_back(                                                \
          util_random::SkewedLow<uint32_t>(rnd, 1, 1 << max_value_bits)); \
    }                                                                     \
    BitEncoder bitenc;                                                    \
    bitenc.EnsureBits(reserved_encoding_bits * kOpsPerIter);              \
    for (auto _ : state) {                                                \
      for (int n = 0; n < kOpsPerIter; ++n) {                             \
        bitenc.method(unary_val[n]);                                      \
      }                                                                   \
      bitenc.Flush(0);                                                    \
      global_value += bitenc.Bits();                                      \
      bitenc.Clear();                                                     \
    }                                                                     \
  }                                                                       \
  BENCHMARK(BM_##method);

#define DEFINE_PUT_LOGBASE_BENCHMARK(method, value_type, log_base,           \
                                     max_value_bits, reserved_encoding_bits) \
  static void BM_##method(benchmark::State& state) {                         \
    constexpr value_type upper_bound =                                       \
        max_value_bits < std::numeric_limits<value_type>::digits             \
            ? ((1ull << (max_value_bits - 1)) |                              \
               ((1ull << (max_value_bits - 2)) - 1)) +                       \
                  1                                                          \
            : std::numeric_limits<value_type>::max();                        \
    ACMRandom rnd(ACMRandom::DeterministicSeed());                           \
    std::vector<value_type> unary_val;                                       \
    for (int i = 0; i < kOpsPerIter; ++i) {                                  \
      unary_val.push_back(                                                   \
          util_random::SkewedLow<value_type>(rnd, 1, upper_bound));          \
    }                                                                        \
    BitEncoder bitenc;                                                       \
    bitenc.EnsureBits(reserved_encoding_bits * kOpsPerIter);                 \
    for (auto _ : state) {                                                   \
      for (int n = 0; n < kOpsPerIter; ++n) {                                \
        bitenc.method(log_base, unary_val[n]);                               \
      }                                                                      \
      bitenc.Flush(0);                                                       \
      global_value += bitenc.Bits();                                         \
      bitenc.Clear();                                                        \
    }                                                                        \
  }                                                                          \
  BENCHMARK(BM_##method);

DEFINE_PUT_BENCHMARK(PutBits, uint32_t, 5, 8);
DEFINE_PUT_BENCHMARK(PutBits64, uint64_t, 6, 16);
DEFINE_PUT_UNARY_BENCHMARK(PutUnary, 6, 16);
DEFINE_PUT_UNARY_BENCHMARK(PutInvertedUnary, 6, 16);
DEFINE_PUT_UNARY_BENCHMARK(PutGamma, 16, 16);
DEFINE_PUT_LOGBASE_BENCHMARK(PutVarInt, uint32_t, 8, 32, 32);
DEFINE_PUT_LOGBASE_BENCHMARK(PutVarInt64, uint64_t, 8, 64, 64);
DEFINE_PUT_LOGBASE_BENCHMARK(PutRice, uint32_t, 16, 22, 32);
DEFINE_PUT_LOGBASE_BENCHMARK(PutRice64, uint64_t, 32, 38, 64);

#define DEFINE_PUT_LOGBASE3_BENCHMARK(method, value_type, log_base, param_2, \
                                      param_3, max_value_bits,               \
                                      reserved_encoding_bits)                \
  static void BM_##method(benchmark::State& state) {                         \
    ACMRandom rnd(ACMRandom::DeterministicSeed());                           \
    std::vector<value_type> unary_val;                                       \
    for (int i = 0; i < kOpsPerIter; ++i) {                                  \
      constexpr value_type upper_bound =                                     \
          max_value_bits < std::numeric_limits<value_type>::digits           \
              ? 1ull << max_value_bits                                       \
              : std::numeric_limits<value_type>::max();                      \
      unary_val.push_back(                                                   \
          util_random::SkewedLow<value_type>(rnd, 1, upper_bound));          \
    }                                                                        \
    BitEncoder bitenc;                                                       \
    bitenc.EnsureBits(reserved_encoding_bits * kOpsPerIter);                 \
    for (auto _ : state) {                                                   \
      for (int n = 0; n < kOpsPerIter; ++n) {                                \
        bitenc.method(log_base, param_2, param_3, unary_val[n]);             \
      }                                                                      \
      bitenc.Flush(0);                                                       \
      global_value += bitenc.Bits();                                         \
      bitenc.Clear();                                                        \
    }                                                                        \
  }                                                                          \
  BENCHMARK(BM_##method);

DEFINE_PUT_LOGBASE3_BENCHMARK(PutProgressiveRice, uint32_t, 16, 12, 4, 22, 32);
DEFINE_PUT_LOGBASE3_BENCHMARK(PutProgressiveRice64, uint64_t, 32, 12, 4, 38,
                              64);

template <class ArrayClass>
class FixedBitWidthArrayTest : public testing::Test {
 public:
  // Generates a k-bit test value with i as a seed.
  static uint32_t TestValue(int i, int k) {
    if (k <= 1) return i % 2;
    return (i + (3 << (k - 2))) % ((1ul << k) - 1);
  }
  static void TestKBits(int k) {
    if (k > ArrayClass::MaxElementWidthInBits()) return;
    auto data = gtl::MakeUniqueArray<char>(ArrayClass::SizeInBytes(k, 200) +
                                           ArrayClass::kSlopBytes);
    ArrayClass array(data.data(), k, 200);
    for (int i = 0; i < 200; ++i) {
      array.Set(i, TestValue(i, k));
      EXPECT_EQ(TestValue(i, k), array.Get(i));
    }
    for (int i = 0; i < 200; ++i) {
      EXPECT_EQ(TestValue(i, k), array.Get(i));
    }
  }
};

typedef ::testing::Types<FixedBitWidthArrayBase<false>,
                         FixedBitWidthArrayBase<true> >
    FixedBitWidthArrayTypes;
TYPED_TEST_SUITE(FixedBitWidthArrayTest, FixedBitWidthArrayTypes);

TYPED_TEST(FixedBitWidthArrayTest, Simple) {
  for (int num_bits = 1; num_bits <= 32; ++num_bits) {
    FixedBitWidthArrayTest<TypeParam>::TestKBits(num_bits);
  }
}

TEST(FixedBitWidthArrayBinaryCompatibility, Main) {
  auto buf =
      gtl::MakeUniqueArray<char>(FixedBitWidthArray::SizeInBytes(100, 22) +
                                 FixedBitWidthArray::kSlopBytes);

  // Write with safe array, read with unsafe array.
  {
    SafeFixedBitWidthArray writer(buf.data(), 22, 100);
    for (int i = 0; i < 100; ++i) writer.Set(i, i + (1 << 21));
    FixedBitWidthArray reader(buf.data(), 22);
    for (int i = 0; i < 100; ++i) EXPECT_EQ(i + (1 << 21), reader.Get(i));
  }
  // Write with unsafe array, read with safe array.
  {
    FixedBitWidthArray writer(buf.data(), 22);
    for (int i = 0; i < 100; ++i) writer.Set(i, i + (1 << 21));
    SafeFixedBitWidthArray reader(buf.data(), 22, 100);
    for (int i = 0; i < 100; ++i) EXPECT_EQ(i + (1 << 21), reader.Get(i));
  }
}

#if GTEST_HAS_DEATH_TEST
TEST(SafeFixedBitWidthArrayTest, BoundsChecks) {
  auto buf =
      gtl::MakeUniqueArray<char>(SafeFixedBitWidthArray::SizeInBytes(10, 22));
  SafeFixedBitWidthArray a(buf.data(), 22, 10);
  EXPECT_EQ(a.Get(11), 0);
  EXPECT_DEATH(a.Set(11, 0), "");
  EXPECT_EQ(a.Get(-1), 0);
  EXPECT_DEATH(a.Set(-1, 0), "");
}
#endif

template <class ArrayClass>
static void BM_FixedBitWidthArray(benchmark::State& state) {
  ArrayClass* arrays[21];
  auto data =
      gtl::MakeUniqueArray<char>(10240);  // this is enough for our needs.
  char* ptr = data.data();
  for (int i = 0; i < 21; ++i) {
    arrays[i] = new ArrayClass(ptr, i + 4, 100);
    for (int j = 0; j < 100; ++j) arrays[i]->Set(j, j);
    ptr += ArrayClass::SizeInBytes(100, i + 4);
  }

  int sum = 0;

  for (auto _ : state) {
    for (int iter = 0; iter < kOpsPerIter / (21 * 5); ++iter) {
      for (int i = 0; i < 21; ++i) {
        // We access the first element, a few middle element and the last
        // element in the hope of modelling a reasonable access pattern.
        sum += arrays[i]->Get(0);   // first element
        sum += arrays[i]->Get(11);  // a few middle elements
        sum += arrays[i]->Get(39);
        sum += arrays[i]->Get(89);
        sum += arrays[i]->Get(99);  // last element
      }
    }
  }
  for (int i = 0; i < 21; ++i) {
    delete arrays[i];
  }
  VLOG(1) << sum;
}
BENCHMARK_TEMPLATE(BM_FixedBitWidthArray, FixedBitWidthArrayBase<false>);
BENCHMARK_TEMPLATE(BM_FixedBitWidthArray, FixedBitWidthArrayBase<true>);
