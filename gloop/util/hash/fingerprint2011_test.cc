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

#include "gloop/util/hash/fingerprint2011.h"

#include <cstdint>
#include <ios>
#include <string>

#include "absl/base/casts.h"
#include "absl/log/log.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "gloop/util/hash/hash.h"
#include "gloop/util/random/acmrandom.h"
#include "gtest/gtest.h"

class Fingerprint2011Test : public ::testing::Test {
 public:
  static void SetUpTestSuite() {
    ACMRandom r(9);
    for (int i = 0; i < kMaxSize; ++i) {
      buf[i] = absl::bit_cast<char>(r.Rand8());
    }
  }
  static const int kMaxSize;
  static char buf[];
};

const int Fingerprint2011Test::kMaxSize = 1000;
char Fingerprint2011Test::buf[Fingerprint2011Test::kMaxSize];

TEST_F(Fingerprint2011Test, Unchanging0) {
  EXPECT_EQ(uint64_t{0x433109b33e13e6ed},
            Fingerprint2011(absl::string_view(buf, 1000)));
  EXPECT_EQ(uint64_t{0x5f2f123bfc815f81},
            Fingerprint2011(absl::string_view(buf, 800)));
  EXPECT_EQ(uint64_t{0x6396fc6a67293cf4},
            Fingerprint2011(absl::string_view(buf, 640)));
  EXPECT_EQ(uint64_t{0x45c01b4934ddbbbe},
            Fingerprint2011(absl::string_view(buf, 512)));
  EXPECT_EQ(uint64_t{0xfcd19b617551db45u},
            Fingerprint2011(absl::string_view(buf, 409)));
  EXPECT_EQ(uint64_t{0x4eee69e12854871e},
            Fingerprint2011(absl::string_view(buf, 327)));
  EXPECT_EQ(uint64_t{0xab753446a3bbd532u},
            Fingerprint2011(absl::string_view(buf, 261)));
  EXPECT_EQ(uint64_t{0x54242fe06a291c3f},
            Fingerprint2011(absl::string_view(buf, 208)));
  EXPECT_EQ(uint64_t{0x4f7acff7703a635b},
            Fingerprint2011(absl::string_view(buf, 166)));
  EXPECT_EQ(uint64_t{0xa784bd0a1f22cc7fu},
            Fingerprint2011(absl::string_view(buf, 132)));
  EXPECT_EQ(uint64_t{0xf19118e187456638u},
            Fingerprint2011(absl::string_view(buf, 105)));
  EXPECT_EQ(uint64_t{0x3e2e58f9196abfe5},
            Fingerprint2011(absl::string_view(buf, 84)));
  EXPECT_EQ(uint64_t{0xd38ae3dec0107aeau},
            Fingerprint2011(absl::string_view(buf, 67)));
  EXPECT_EQ(uint64_t{0xea3033885868e10eu},
            Fingerprint2011(absl::string_view(buf, 53)));
  EXPECT_EQ(uint64_t{0x1394a146d0d7e04b},
            Fingerprint2011(absl::string_view(buf, 42)));
  EXPECT_EQ(uint64_t{0x9962499315d2e8dau},
            Fingerprint2011(absl::string_view(buf, 33)));
  EXPECT_EQ(uint64_t{0x0849f5cfa85489b5},
            Fingerprint2011(absl::string_view(buf, 26)));
  EXPECT_EQ(uint64_t{0x83b395ff19bf2171u},
            Fingerprint2011(absl::string_view(buf, 20)));
  EXPECT_EQ(uint64_t{0x9d33dd141bd55d9au},
            Fingerprint2011(absl::string_view(buf, 16)));
  EXPECT_EQ(uint64_t{0x196248eb0b02466a},
            Fingerprint2011(absl::string_view(buf, 12)));
  EXPECT_EQ(uint64_t{0x1cf73a50ff120336},
            Fingerprint2011(absl::string_view(buf, 9)));
  EXPECT_EQ(uint64_t{0xb451c339457dbf51u},
            Fingerprint2011(absl::string_view(buf, 7)));
  EXPECT_EQ(uint64_t{0x681982c5e7b74064},
            Fingerprint2011(absl::string_view(buf, 5)));
  EXPECT_EQ(uint64_t{0xc5ce47450ca6c021u},
            Fingerprint2011(absl::string_view(buf, 4)));
  EXPECT_EQ(uint64_t{0x9fcc3c3fde4d5ff7u},
            Fingerprint2011(absl::string_view(buf, 3)));
  EXPECT_EQ(uint64_t{0x090966a836e5fa4b},
            Fingerprint2011(absl::string_view(buf, 2)));
  EXPECT_EQ(uint64_t{0x8199675ecaa6fe64u},
            Fingerprint2011(absl::string_view(buf, 1)));
  EXPECT_EQ(uint64_t{0x23ad7c904aa665e3},
            Fingerprint2011(absl::string_view(buf, 0)));
}

// Helper for Unchanging1.  Replace *h with a hash of itself and return a
// char that is also a hash of *h.  Neither hash needs to be particularly good.
static char Remix(uint64_t* h) {
  *h ^= *h >> 41;
  *h *= 949921979;
  return 'a' + ((*h & 0xfffff) % 26);
}

// This is more thorough, but if something is wrong the output will be even
// less illuminating, because it just checks one uint64 at the end.
TEST_F(Fingerprint2011Test, Unchanging1) {
  const int kIters = 800;
  std::string s;
  uint64_t h = 0;
  for (int i = 0; i < kIters; i++) {
    h ^= Fingerprint2011(absl::string_view(s.data(), i));
    s.push_back(Remix(&h));
    h ^= Fingerprint2011(absl::string_view(s.data(), i * i % s.size()));
    s.push_back(Remix(&h));
    h ^= Fingerprint2011(absl::string_view(s.data(), i * i * i % s.size()));
    s.push_back(Remix(&h));
    h ^= Fingerprint2011(absl::string_view(s.data(), s.size()));
    s.push_back(Remix(&h));
    uint32_t x0 = static_cast<uint8_t>(s[s.size() - 1]);
    uint32_t x1 = static_cast<uint8_t>(s[s.size() - 2]);
    uint32_t x2 = static_cast<uint8_t>(s[s.size() - 3]);
    uint32_t x3 = static_cast<uint8_t>(s[s.size() / 2]);
    s[((x0 << 16) + (x1 << 8) + x2) % s.size()] ^= x3;
    s[((x1 << 16) + (x2 << 8) + x3) % s.size()] ^= i % 256;
    VLOG(2) << "At end of i=" << i << " iteration, h=" << std::hex << h;
  }
  EXPECT_EQ(uint64_t{0xeaa3b1c985261632u}, h);
}

TEST_F(Fingerprint2011Test, StringPiece) {
  EXPECT_EQ(Fingerprint2011(absl::string_view("test", 4)),
            Fingerprint2011("test"));
}

TEST_F(Fingerprint2011Test, Overloaded) {
  for (int len :
       {1000, 800, 640, 512, 409, 327, 261, 208, 166, 132, 105, 84, 67, 53,
        42,   33,  26,  20,  16,  12,  9,   7,   5,   4,   3,   2,  1,  0}) {
    EXPECT_EQ(Fingerprint2011(absl::string_view(buf, len)),
              Fingerprint2011(absl::string_view(buf, len)))
        << "StringPiece " << len;
    EXPECT_EQ(Fingerprint2011(absl::string_view(buf, len)),
              Fingerprint2011(absl::Cord(absl::string_view(buf, len))))
        << "Cord " << len;
  }
}

TEST_F(Fingerprint2011Test, CatUnchanging) {
  const int num_indexes = 8;
  int indexes[num_indexes];
  ACMRandom r(11);
  for (int i = 0; i < num_indexes; ++i) {
    indexes[i] = (r.Rand16() * kMaxSize) >> 16;
  }

  EXPECT_EQ(
      uint64_t{0x14262e3a192d9c0b},
      FingerprintCat2011(Fingerprint2011(absl::string_view(buf, indexes[0])),
                         Fingerprint2011(absl::string_view(buf, indexes[1]))));
  EXPECT_EQ(
      uint64_t{0x37c7978268e0881},
      FingerprintCat2011(Fingerprint2011(absl::string_view(buf, indexes[2])),
                         Fingerprint2011(absl::string_view(buf, indexes[3]))));
  EXPECT_EQ(
      uint64_t{0x2065a6666eff954e},
      FingerprintCat2011(Fingerprint2011(absl::string_view(buf, indexes[4])),
                         Fingerprint2011(absl::string_view(buf, indexes[5]))));
  EXPECT_EQ(
      uint64_t{0xab7eff18994628},
      FingerprintCat2011(Fingerprint2011(absl::string_view(buf, indexes[6])),
                         Fingerprint2011(absl::string_view(buf, indexes[7]))));
}

TEST_F(Fingerprint2011Test, CatEqualInputs) {
  EXPECT_EQ(
      uint64_t{0x19ef53cf9c129dc9},
      FingerprintCat2011(Fingerprint2011("hello"), Fingerprint2011("hello")));
  EXPECT_EQ(
      uint64_t{0xed782bb3b6766637u},
      FingerprintCat2011(Fingerprint2011("world"), Fingerprint2011("world")));
}

TEST_F(Fingerprint2011Test, CatIsNotCommutative) {
  EXPECT_NE(
      FingerprintCat2011(Fingerprint2011("hello"), Fingerprint2011("world")),
      FingerprintCat2011(Fingerprint2011("world"), Fingerprint2011("hello")));
}

static void BM_FingerprintCat(benchmark::State& state) {
  uint64_t res = 0;
  int i = 0;
  for (auto _ : state) {  // NOLINT
    res = FingerprintCat(res, i++);
  }
  VLOG(99) << res;
}

BENCHMARK(BM_FingerprintCat);

static void BM_FingerprintCat2011(benchmark::State& state) {
  uint64_t res = 0;
  int i = 0;
  for (auto _ : state) {  // NOLINT
    res = FingerprintCat2011(res, i++);
  }
  VLOG(99) << res;
}

BENCHMARK(BM_FingerprintCat2011);

static void BM_Fingerprint2011StringPiece(benchmark::State& state) {
  Fingerprint2011Test::SetUpTestSuite();
  for (auto _ : state) {  // NOLINT
    Fingerprint2011(absl::string_view(Fingerprint2011Test::buf, 1000));
  }
}
BENCHMARK(BM_Fingerprint2011StringPiece);

static void BM_Fingerprint2011Cord(benchmark::State& state) {
  Fingerprint2011Test::SetUpTestSuite();
  absl::Cord buffer(absl::string_view(Fingerprint2011Test::buf, 1000));
  for (auto _ : state) {  // NOLINT
    Fingerprint2011(buffer);
  }
}
BENCHMARK(BM_Fingerprint2011Cord);
