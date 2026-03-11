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

// Copyright 2002 Google Inc. All Rights Reserved.
//

#include "gloop/util/coding/coder.h"

#include <stddef.h>
#include <string.h>

#include <cstdint>
#include <iosfwd>
#include <iostream>
#include <limits>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/macros.h"
#include "absl/log/check.h"
#include "absl/numeric/int128.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "gloop/util/coding/varint.h"
#include "gloop/util/gtl/unique_array.h"
#include "gloop/util/random/acmrandom.h"
#include "gtest/gtest.h"

// Global data.
static char encoding_buffer[1000];
static char encoding_buffer2[1000];

TEST(Encoder, Moves) {
  Encoder a;
  a.Ensure(1);
  unsigned char buf[1];
  Encoder b(buf, 1);
  a = std::move(b);
  b = std::move(a);
  b.reset(buf, 1);
  b.put8('a');
  Decoder dec;
  dec.reset(buf, 1);
  EXPECT_EQ(dec.get8(), 'a');
}

TEST(CoderUnitTEst, TestEncodeDecode) {
  Encoder enc;
  static const size_t kBufSize = 500000;
  auto buf = gtl::MakeUniqueArrayForOverwrite<char>(kBufSize);
  std::string msg = "this is the first line\nthis is the second";

  enc.reset(buf.data(), kBufSize);
  enc.put8('a');
  enc.put16(0xFFFF);
  enc.put32(0xFFFFFFFF);
  enc.put64(uint64_t{0xFFFFFFFFFFFFFFFFu});
  enc.put128(absl::Uint128Max());
  enc.putn(msg.c_str(), msg.length());
  enc.putcn(msg.c_str(), '\n', msg.length());
  enc.puts(msg.c_str());
  enc.puts_without_null(msg.c_str());
  enc.putfloat(0.1);
  enc.putdouble(0.1);
  for (int p = 0; p < 31; p++) {
    enc.put_varint32(1 << p);
    enc.put_varint32_inline(1 << p);
  }
  for (int p = 0; p < 64; p++) {
    enc.put_varint64(uint64_t{1} << p);
    for (int s = 0; s < p - 2; s++) {
      enc.put_varint64((uint64_t{1} << p) + (uint64_t{1} << s));
    }
  }
  enc.put_varint64(int64_t{0xfde4837dad32956});
  enc.put_varint64(int64_t{0x45e44536da332956});
  enc.put_varint64(int64_t{0x245e44536da32956});
  enc.put_varint64(uint64_t{0xc245e44536da3256u});
  /* What can we really assert about varint encoding?
  CHECK_EQ(enc.varint32_length(1), 1);
  CHECK_EQ(enc.varint64_length(2ull), 1);
  */

  CHECK_EQ(enc.length() + enc.avail(), kBufSize);

  // See if we can read everything back now!
  Decoder dec;
  char decmsg[100];

  dec.reset(buf.data(), kBufSize);
  CHECK_EQ(dec.get8(), 'a');
  CHECK_EQ(dec.get16(), 0xFFFF);
  CHECK_EQ(dec.get32(), 0xFFFFFFFF);
  CHECK_EQ(dec.get64(), uint64_t{0xFFFFFFFFFFFFFFFFu});
  CHECK_EQ(dec.get128(), absl::Uint128Max());

  memset(decmsg, 0, sizeof(decmsg));
  dec.getn(decmsg, msg.length());
  CHECK_STREQ(decmsg, msg.c_str());

  std::string shortmsg = "this is the first line\n";
  memset(decmsg, 0, sizeof(decmsg));
  dec.getcn(decmsg, '\n', sizeof(decmsg));
  CHECK_STREQ(decmsg, shortmsg.c_str());

  memset(decmsg, 0, sizeof(decmsg));
  dec.gets(decmsg, sizeof(decmsg));
  CHECK_STREQ(decmsg, msg.c_str());

  // written with puts_without_null
  CHECK_EQ(absl::string_view(dec.skip(msg.length()), msg.length()), msg);
  CHECK_EQ(dec.getfloat(), (float)0.1);

  CHECK_EQ(dec.getdouble(), (double)0.1);
  for (int p = 0; p < 31; p++) {
    uint32_t u32;
    dec.get_varint32(&u32);
    CHECK_EQ(u32, 1 << p);
    dec.get_varint32(&u32);
    CHECK_EQ(u32, 1 << p);
  }
  for (int p = 0; p < 64; p++) {
    uint64_t u64;
    dec.get_varint64(&u64);
    CHECK_EQ(u64, uint64_t{1} << p);
    for (int s = 0; s < p - 2; s++) {
      dec.get_varint64(&u64);
      CHECK_EQ(u64, (uint64_t{1} << p) + (uint64_t{1} << s));
    }
  }
  uint64_t u64;
  dec.get_varint64(&u64);
  CHECK_EQ(u64, int64_t{0xfde4837dad32956});
  dec.get_varint64(&u64);
  CHECK_EQ(u64, int64_t{0x45e44536da332956});
  dec.get_varint64(&u64);
  CHECK_EQ(u64, int64_t{0x245e44536da32956});
  dec.get_varint64(&u64);
  CHECK_EQ(u64, uint64_t{0xc245e44536da3256u});

  CHECK_EQ(dec.pos() + dec.avail(), kBufSize);
  CHECK_EQ(dec.pos(), enc.length());

  // Test put_varint64_from_decoder
  enc.reset(buf.data(), kBufSize);
  enc.put_varint64(0);
  for (int p = 0; p < 64; p++) {
    enc.put_varint64(uint64_t{1} << p);
    for (int s = 0; s < p - 2; s++) {
      enc.put_varint64((uint64_t{1} << p) + (uint64_t{1} << s));
    }
  }
  enc.put_varint64(int64_t{0xfde4837dad32956});
  enc.put_varint64(int64_t{0x45e44536da332956});
  enc.put_varint64(int64_t{0x245e44536da32956});
  enc.put_varint64(uint64_t{0xc245e44536da3256u});
  // Decode from buf and encode into buf2 using put_varint64_from_decoder.
  dec.reset(buf.data(), kBufSize);
  auto buf2 = gtl::MakeUniqueArrayForOverwrite<char>(kBufSize);
  enc.reset(buf2.data(), kBufSize);
  CHECK(enc.put_varint64_from_decoder(&dec));
  for (int p = 0; p < 64; p++) {
    CHECK(enc.put_varint64_from_decoder(&dec));
    for (int s = 0; s < p - 2; s++) {
      CHECK(enc.put_varint64_from_decoder(&dec));
    }
  }
  for (int i = 0; i < 4; i++) {
    enc.put_varint64_from_decoder(&dec);
  }
  // Compare buf and buf2 directly.
  CHECK_EQ(dec.pos(), enc.length());
  CHECK_EQ(memcmp(buf.data(), buf2.data(), enc.length()), 0);
  // Check each value via decoding.
  dec.reset(buf2.data(), kBufSize);
  dec.get_varint64(&u64);
  CHECK_EQ(u64, 0);
  for (int p = 0; p < 64; p++) {
    uint64_t u64;
    dec.get_varint64(&u64);
    CHECK_EQ(u64, uint64_t{1} << p);
    for (int s = 0; s < p - 2; s++) {
      dec.get_varint64(&u64);
      CHECK_EQ(u64, (uint64_t{1} << p) + (uint64_t{1} << s));
    }
  }
  dec.get_varint64(&u64);
  CHECK_EQ(u64, int64_t{0xfde4837dad32956});
  dec.get_varint64(&u64);
  CHECK_EQ(u64, int64_t{0x45e44536da332956});
  dec.get_varint64(&u64);
  CHECK_EQ(u64, int64_t{0x245e44536da32956});
  dec.get_varint64(&u64);
  CHECK_EQ(u64, uint64_t{0xc245e44536da3256u});
  // Test invalid long (Encoder::kVarintMax64 + 1) encoded data.
  memset(buf.data(), 0xff, Encoder::kVarintMax64);
  memset(buf.data() + Encoder::kVarintMax64, 1, 1);
  dec.reset(buf.data(), kBufSize);
  enc.reset(buf2.data(), kBufSize);
  CHECK(!enc.put_varint64_from_decoder(&dec));
  CHECK(!dec.get_varint64(&u64));
  // Test short decoder limit.
  enc.reset(buf.data(), kBufSize);
  enc.put_varint64(-1);
  for (size_t size = 0; size < Encoder::kVarintMax64; ++size) {
    dec.reset(buf.data(), size);
    enc.reset(buf2.data(), kBufSize);
    CHECK(!enc.put_varint64_from_decoder(&dec));
    CHECK(!dec.get_varint64(&u64));
  }
  // Test short encoder limit.
  for (size_t size = 0; size < Encoder::kVarintMax64; ++size) {
    dec.reset(buf.data(), kBufSize);
    enc.reset(buf2.data(), size);
    CHECK(!enc.put_varint64_from_decoder(&dec));
    CHECK(dec.get_varint64(&u64));
  }
}

TEST(CoderUnitTest, CreateDecoder) {
  absl::string_view buf = "foo";
  Decoder d(buf);

  EXPECT_EQ(d.pos(), 0);
  EXPECT_EQ(d.avail(), buf.size());
  EXPECT_EQ(d.skip(0), "foo");
}

TEST(CoderUnitTest, TestGrowSpaceIfNeeded) {
  Encoder e;
  for (int t = 0; t < 2; ++t) {
    for (int i = 0; i < 1000; i++) {
      e.Ensure(26);
      e.put32(i);
      e.put64(i);
      for (int j = 0; j < (i % 14); j++) {
        e.put8(j);
      }
    }
    e.Ensure(100);

    Decoder d(e.base(), e.length());
    for (int i = 0; i < 1000; i++) {
      CHECK_EQ(d.get32(), i);
      CHECK_EQ(d.get64(), i);
      for (int j = 0; j < (i % 14); j++) {
        CHECK_EQ(d.get8(), j);
      }
    }
    CHECK_EQ(d.avail(), 0);
    // Reset and run the same test again.
    e.reset();
  }
}

void TestFillArray() {
  const char kData[] = "abcdef";

  constexpr const int kNumDecoders = 3;
  Decoder decoders[kNumDecoders] = {Decoder(kData, 2), Decoder(kData, 3),
                                    Decoder(kData, 4)};
  DecoderExtensions::FillArray(decoders, kNumDecoders);
  for (const Decoder& decoder : decoders) {
    CHECK_EQ(decoder.ptr(), nullptr);
    CHECK_EQ(decoder.pos(), 0);
    CHECK_EQ(decoder.avail(), 0);
  }
}

TEST(CoderUnitTest, TestFillArray) { TestFillArray(); }

#if GTEST_HAS_DEATH_TEST
TEST(CoderDeathTest, Hardening) {
  // Use a condition with a side effect to see if ABSL_HARDENING_ASSERT is
  // enabled.
  bool is_hardened = false;
  ABSL_HARDENING_ASSERT([&is_hardened]() {
    is_hardened = true;
    return true;
  }());
  if (!is_hardened) {
    GTEST_SKIP() << "This test requires ABSL_HARDENING_ASSERT is enabled";
  }

  const std::string str = "abcdefghijklmnopqrstuvwxyz";
  Encoder e(encoding_buffer, 0);
  EXPECT_DEATH(e.put8(0), "");
  EXPECT_DEATH(e.put16(0), "");
  EXPECT_DEATH(e.put32(0), "");
  EXPECT_DEATH(e.put64(0), "");
  EXPECT_DEATH(e.put128(0), "");
  EXPECT_DEATH(e.putcn(str.data(), '\0', str.size()), "");
  EXPECT_DEATH(e.putfloat(0.0), "");
  EXPECT_DEATH(e.putdouble(0.0), "");
  EXPECT_DEATH(e.put_varint32(0.0), "");
  EXPECT_DEATH(e.put_varint32_inline(0.0), "");
  EXPECT_DEATH(e.put_varint64(0.0), "");
  EXPECT_DEATH(e.skip(1), "");
  EXPECT_DEATH(e.skip(-1), "");
  EXPECT_DEATH(e.RemoveLast(-1), "");

  Decoder d(encoding_buffer, 0);
  EXPECT_DEATH(d.get8(), "");
  EXPECT_DEATH(d.get16(), "");
  EXPECT_DEATH(d.get32(), "");
  EXPECT_DEATH(d.get64(), "");
  EXPECT_DEATH(d.get128(), "");
  EXPECT_DEATH(d.getfloat(), "");
  EXPECT_DEATH(d.getdouble(), "");
  EXPECT_DEATH(d.skip(1), "");
  EXPECT_DEATH(d.skip(-1), "");
}
#endif  // GTEST_HAS_DEATH_TEST

static void BM_putvarint32(benchmark::State& state) {
  for (auto _ : state) {
    Encoder e(encoding_buffer, sizeof(encoding_buffer));
    e.put_varint32(1ULL << (state.range(0) * 7));
  }
}
BENCHMARK(BM_putvarint32)->DenseRange(0, 4);

static void BM_putvarint32_2(benchmark::State& state) {
  const int shift_bits = state.range(0) * 7;
  for (auto _ : state) {
    Encoder e(encoding_buffer, sizeof(encoding_buffer));
    e.put_varint32(1ULL << shift_bits);
    e.put_varint32((1ULL << shift_bits) + 1);
    e.put_varint32((1ULL << shift_bits) + 2);
    e.put_varint32((1ULL << shift_bits) + 3);
    e.put_varint32((1ULL << shift_bits) + 4);
    e.put_varint32((1ULL << shift_bits) + 5);
    e.put_varint32((1ULL << shift_bits) + 6);
    e.put_varint32((1ULL << shift_bits) + 7);
    e.put_varint32((1ULL << shift_bits) + 8);
    e.put_varint32((1ULL << shift_bits) + 9);
  }
}
BENCHMARK(BM_putvarint32_2)->DenseRange(0, 4);

static void BM_putvarint32_inline(benchmark::State& state) {
  const int shift_bits = state.range(0) * 7;
  for (auto _ : state) {
    Encoder e(encoding_buffer, sizeof(encoding_buffer));
    e.put_varint32_inline(1ULL << shift_bits);
    e.put_varint32_inline((1ULL << shift_bits) + 1);
    e.put_varint32_inline((1ULL << shift_bits) + 2);
    e.put_varint32_inline((1ULL << shift_bits) + 3);
    e.put_varint32_inline((1ULL << shift_bits) + 4);
    e.put_varint32_inline((1ULL << shift_bits) + 5);
    e.put_varint32_inline((1ULL << shift_bits) + 6);
    e.put_varint32_inline((1ULL << shift_bits) + 7);
    e.put_varint32_inline((1ULL << shift_bits) + 8);
    e.put_varint32_inline((1ULL << shift_bits) + 9);
  }
}
BENCHMARK(BM_putvarint32_inline)->DenseRange(0, 4);

static void BM_putvarint64(benchmark::State& state) {
  for (auto _ : state) {
    Encoder e(encoding_buffer, sizeof(encoding_buffer));
    e.put_varint64(uint64_t{1} << (state.range(0) * 7));
  }
}
BENCHMARK(BM_putvarint64)->DenseRange(0, 9);

static void BM_putvarfromdecoder(benchmark::State& state) {
  Encoder e(encoding_buffer, sizeof(encoding_buffer));
  for (uint64_t i = 0; i < 100; ++i) {
    e.put_varint64(uint64_t{1} << (state.range(0) * 7));
  }

  for (auto _ : state) {
    Decoder d(encoding_buffer, sizeof(encoding_buffer));
    e.reset(encoding_buffer2, sizeof(encoding_buffer2));
    for (int j = 0; j < 100; ++j) {
      e.put_varint64_from_decoder(&d);
    }
    CHECK_EQ(e.avail(), d.avail());
  }
}
BENCHMARK(BM_putvarfromdecoder)->DenseRange(0, 9);

/* Similar to BM_putvarfromdecoder, but use get8/put8 instead of
 * put_varint64_from_decoder.
 * We use this benchmark to measure the alternative implmentation of
 * put_varint64_from_decoder.
 */
static void BM_putvarfromget8(benchmark::State& state) {
  Encoder e(encoding_buffer, sizeof(encoding_buffer));
  for (uint64_t i = 0; i < 100; ++i) {
    e.put_varint64(uint64_t{1} << (state.range(0) * 7));
  }

  for (auto _ : state) {
    Decoder d(encoding_buffer, sizeof(encoding_buffer));
    e.reset(encoding_buffer2, sizeof(encoding_buffer2));
    for (int j = 0; j < 100; ++j) {
      unsigned char c;
      // simple get8 and put8 instead of put_varint64_from_decoder.
      do {
        if (d.avail() <= 0) break;
        if (e.avail() <= 0) break;
        c = d.get8();
        e.put8(c);
      } while (c >= 128);
    }
    CHECK_EQ(e.avail(), d.avail());
  }
}
BENCHMARK(BM_putvarfromget8)->DenseRange(0, 9);

/* Similar to BM_putvarfromdecoder, but use get_varint64 and put_varint64
 * instead of put_varint64_from_decoder.
 * We use this benchmark to measure the alternative implementation of
 * put_varint64_from_decoder.
 */
static void BM_putvarfromgetvar(benchmark::State& state) {
  Encoder e(encoding_buffer, sizeof(encoding_buffer));
  for (uint64_t i = 0; i < 100; ++i) {
    e.put_varint64(uint64_t{1} << (state.range(0) * 7));
  }

  for (auto _ : state) {
    Decoder d(encoding_buffer, sizeof(encoding_buffer));
    e.reset(encoding_buffer2, sizeof(encoding_buffer2));
    for (int j = 0; j < 100; ++j) {
      uint64_t v = 0;
      d.get_varint64(&v);
      e.put_varint64(v);
    }
    CHECK_EQ(e.avail(), d.avail());
  }
}
BENCHMARK(BM_putvarfromgetvar)->DenseRange(0, 9);

// Keyword is either `constexpr` or `volatile` to simulate whether the buffer
// size is known at compile-time. In hardened builds, when the size is known at
// compile-time, the optimizer can eliminate some bounds checks.
#define BENCHMARK_getX(keyword, x)                                    \
  static void BM_get##x##_##keyword##_size(benchmark::State& state) { \
    constexpr size_t kBatch = 100;                                    \
    keyword size_t size = (x / 8) * kBatch;                           \
    std::vector<char> v(size);                                        \
    Encoder e(v.data(), size);                                        \
    for (size_t i = 0; i < kBatch; i++) e.put##x(42);                 \
    while (state.KeepRunningBatch(kBatch)) {                          \
      Decoder d(v.data(), size);                                      \
      for (size_t j = 0; j < kBatch; j++) {                           \
        benchmark::DoNotOptimize(d.get##x());                         \
      }                                                               \
    }                                                                 \
  }                                                                   \
  BENCHMARK(BM_get##x##_##keyword##_size)

BENCHMARK_getX(constexpr, 8);
BENCHMARK_getX(volatile, 8);
BENCHMARK_getX(constexpr, 16);
BENCHMARK_getX(volatile, 16);
BENCHMARK_getX(constexpr, 32);
BENCHMARK_getX(volatile, 32);
BENCHMARK_getX(constexpr, 64);
BENCHMARK_getX(volatile, 64);
BENCHMARK_getX(constexpr, 128);
BENCHMARK_getX(volatile, 128);

// Keyword is either `constexpr` or `volatile` to simulate whether the buffer
// size is known at compile-time. In hardened builds, when the size is known at
// compile-time, the optimizer can eliminate some bounds checks.
#define BENCHMARK_putX(keyword, x)                                    \
  static void BM_put##x##_##keyword##_size(benchmark::State& state) { \
    constexpr size_t kBatch = 100;                                    \
    keyword size_t size = (x / 8) * kBatch;                           \
    std::vector<char> v(size);                                        \
    uint8_t put_val = 0;                                              \
    while (state.KeepRunningBatch(kBatch)) {                          \
      Encoder e(v.data(), size);                                      \
      for (size_t i = 0; i < kBatch; ++i) {                           \
        e.put##x(put_val++);                                          \
      }                                                               \
      CHECK_EQ(e.avail(), 0u); /* Prevent optimization */             \
    }                                                                 \
  }                                                                   \
  BENCHMARK(BM_put##x##_##keyword##_size)

BENCHMARK_putX(constexpr, 8);
BENCHMARK_putX(volatile, 8);
BENCHMARK_putX(constexpr, 16);
BENCHMARK_putX(volatile, 16);
BENCHMARK_putX(constexpr, 32);
BENCHMARK_putX(volatile, 32);
BENCHMARK_putX(constexpr, 64);
BENCHMARK_putX(volatile, 64);
BENCHMARK_putX(constexpr, 128);
BENCHMARK_putX(volatile, 128);

static void BM_getvarint64fast(benchmark::State& state) {
  Encoder e(encoding_buffer, sizeof(encoding_buffer));
  for (uint64_t i = 0; i < 100; ++i) {
    e.put_varint64(uint64_t{1} << (state.range(0) * 7));
  }

  for (auto _ : state) {
    Decoder d(encoding_buffer, sizeof(encoding_buffer));
    uint64_t v = 0;
    for (int j = 0; j < 100; ++j) {
      d.get_varint64(&v);
    }
    CHECK_EQ(e.avail(), d.avail());
  }
}
BENCHMARK(BM_getvarint64fast)->DenseRange(0, 9);

static void BM_getvarint64slow(benchmark::State& state) {
  Encoder e(encoding_buffer, sizeof(encoding_buffer));
  e.put_varint64(uint64_t{1} << (state.range(0) * 7));

  uint64_t v = 0;
  Decoder d;
  for (auto _ : state) {
    d.reset(encoding_buffer, e.length());
    d.get_varint64(&v);
  }
  CHECK_EQ(d.avail(), 0);
}
BENCHMARK(BM_getvarint64slow)->DenseRange(0, 9);

static void BM_getvarint32fast(benchmark::State& state) {
  Encoder e(encoding_buffer, sizeof(encoding_buffer));
  for (uint64_t i = 0; i < 100; ++i) {
    e.put_varint64(1U << (state.range(0) * 7));
  }

  for (auto _ : state) {
    Decoder d(encoding_buffer, sizeof(encoding_buffer));
    uint32_t v = 0;
    for (int j = 0; j < 100; ++j) {
      d.get_varint32(&v);
    }
    CHECK_EQ(e.avail(), d.avail());
  }
}
BENCHMARK(BM_getvarint32fast)->DenseRange(0, 4);

static void BM_getvarint32slow(benchmark::State& state) {
  Encoder e(encoding_buffer, sizeof(encoding_buffer));
  e.put_varint64(1U << (state.range(0) * 7));

  uint32_t v = 0;
  Decoder d;
  for (auto _ : state) {
    for (int j = 0; j < 100; ++j) {
      d.reset(encoding_buffer, e.length());
      d.get_varint32(&v);
    }
  }
  CHECK_EQ(d.avail(), 0);
}
BENCHMARK(BM_getvarint32slow)->DenseRange(0, 4);

static void BM_length(benchmark::State& state) {
  Encoder e(encoding_buffer, ABSL_ARRAYSIZE(encoding_buffer));
  for (auto _ : state) {
    benchmark::DoNotOptimize(e.length());
  }
}
BENCHMARK(BM_length);

static void BM_avail(benchmark::State& state) {
  Encoder e(encoding_buffer, ABSL_ARRAYSIZE(encoding_buffer));
  for (auto _ : state) {
    benchmark::DoNotOptimize(e.avail());
  }
}
BENCHMARK(BM_avail);
