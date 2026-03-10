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

// Copyright 2009 Google Inc. All Rights Reserved.

//
// Tests for the fast hashing algorithm based on Austin Appleby's
// MurmurHash 2.0 algorithm. See http://murmurhash.googlepages.com/

#include "gloop/util/hash/murmur.h"

#include <string.h>

#include <cstdint>
#include <string>

#include "absl/log/log.h"
#include "absl/numeric/int128.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "gloop/util/random/acmrandom.h"
#include "gtest/gtest.h"

namespace util_hash {

inline void InitializeRandomString(int len, char* data, RandomBase* rnd) {
  for (int k = 0; k < len; ++k) {
    data[k] = rnd->Rand8();
  }
}

TEST(Murmur, EmptyData64) {
  EXPECT_EQ(uint64_t{0},
            util_hash::MurmurHash64(absl::string_view(nullptr, uint64_t{0})));
  EXPECT_EQ(uint64_t{0}, util_hash::MurmurHash64WithSeed(
                             absl::string_view(nullptr, uint64_t{0}), 0));
}

TEST(MurmurCat, EmptyData64) {
  MurmurCat cat;
  cat.Init(uint64_t{0}, 0ULL);
  cat.Append(absl::string_view(nullptr, 0ULL));
  EXPECT_EQ(uint64_t{0}, cat.GetHash());
  EXPECT_EQ(uint64_t{0}, util_hash::MurmurHash64WithSeed(
                             absl::string_view(nullptr, uint64_t{0}), 0));
}

TEST(Murmur, VaryWithDifferentSeeds) {
  // While in theory different seeds could return the same
  // hash for the same data this is unlikely.
  char data1 = 'x';
  EXPECT_NE(util_hash::MurmurHash64WithSeed(absl::string_view(&data1, 1), 100),
            util_hash::MurmurHash64WithSeed(absl::string_view(&data1, 1), 101));
}

TEST(MurmurCat, VaryWithDifferentSeeds) {
  MurmurCat cat1;
  cat1.Init(uint64_t{100}, 1ULL);
  cat1.Append(absl::string_view("x", 1));

  MurmurCat cat2;
  cat2.Init(uint64_t{101}, 1ULL);
  cat2.Append(absl::string_view("x", 1));

  EXPECT_NE(cat1.GetHash(), cat2.GetHash());
}

TEST(MurmurCat_Aligned, VaryWithDifferentSeeds) {
  MurmurCat cat1;
  cat1.Init(uint64_t{100}, 8ULL);
  cat1.AppendAligned(12345);

  MurmurCat cat2;
  cat2.Init(uint64_t{101}, 8ULL);
  cat2.AppendAligned(12345);

  EXPECT_NE(cat1.GetHash(), cat2.GetHash());
}

// Hashes don't change.
TEST(Murmur, Idempotence) {
  const char data[] = "deadbeef";
  const size_t dlen = strlen(data);
  const uint64_t orig64 =
      util_hash::MurmurHash64(absl::string_view(data, dlen));

  EXPECT_EQ(util_hash::MurmurHash64(absl::string_view(data, dlen)),
            util_hash::MurmurHash64(absl::string_view(data, dlen)));

  for (int i = 0; i < 10; i++) {
    EXPECT_EQ(
        util_hash::MurmurHash64WithSeed(absl::string_view(data, dlen), i),
        util_hash::MurmurHash64WithSeed(absl::string_view(data, dlen), i));
  }

  const char next_data[] = "deadbeef000---";
  const size_t next_dlen = strlen(next_data);

  EXPECT_EQ(util_hash::MurmurHash64(absl::string_view(next_data, next_dlen)),
            util_hash::MurmurHash64(absl::string_view(next_data, next_dlen)));

  for (int i = 0; i < 10; i++) {
    EXPECT_EQ(util_hash::MurmurHash64WithSeed(
                  absl::string_view(next_data, next_dlen), i),
              util_hash::MurmurHash64WithSeed(
                  absl::string_view(next_data, next_dlen), i));
  }

  // Go back to the first test data ('data') and make sure it hasn't changed.
  EXPECT_EQ(util_hash::MurmurHash64(absl::string_view(data, dlen)), orig64);
}

TEST(Murmur, ConcatenationOrderDoesNotAffectHash) {
  MurmurCat murmur_a;
  murmur_a.Append(absl::string_view("0123456789abcdef", 16));

  MurmurCat murmur_b;
  murmur_b.Append(absl::string_view("01234567", 8));
  murmur_b.Append(absl::string_view("89abcdef", 8));

  MurmurCat murmur_c;
  murmur_c.Append(absl::string_view("0", 1));
  murmur_c.Append(absl::string_view("123456789abcdef", 15));

  MurmurCat murmur_d;
  murmur_d.Append(absl::string_view("0123456789abcde", 15));
  murmur_d.Append(absl::string_view("f", 1));

  EXPECT_EQ(murmur_a.GetHash(), murmur_b.GetHash());
  EXPECT_EQ(murmur_a.GetHash(), murmur_c.GetHash());
  EXPECT_EQ(murmur_a.GetHash(), murmur_d.GetHash());
}

TEST(MurmurCat, CompatibilityWithMurmurHash) {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  for (int i = 0; i < 1000; ++i) {
    std::string data(i + 1, 0);
    InitializeRandomString(data.size(), &data[0], &rnd);
    uint64_t expected =
        util_hash::MurmurHash64(absl::string_view(data.data(), data.size()));

    MurmurCat cat;
    cat.Init(uint64_t{0}, data.size());
    cat.Append(absl::string_view(data.data(), data.size()));
    EXPECT_EQ(expected, cat.GetHash());
  }
}

TEST(AlignedMurmurCat, CompatibilityWithMurmurHash) {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  uint64_t data[1000];
  for (int i = 0; i < 1000; ++i) {
    size_t elements = i + 1;
    const size_t size = elements * sizeof(uint64_t);
    InitializeRandomString(size, reinterpret_cast<char*>(data), &rnd);
    uint64_t expected = util_hash::MurmurHash64(
        absl::string_view(reinterpret_cast<char*>(data), size));

    MurmurCat cat;
    cat.Init(uint64_t{0}, elements * 8);
    for (size_t j = 0; j < elements; j++) {
      cat.AppendAligned(data[j]);
    }
    EXPECT_EQ(expected, cat.GetHash());
  }
}

TEST(MurmurCat, CompatibilityWithMurmurHashSplit2) {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  for (int i = 0; i < 1000; ++i) {
    std::string data(i + 9, 0);
    InitializeRandomString(data.size(), &data[0], &rnd);
    uint64_t expected =
        util_hash::MurmurHash64(absl::string_view(data.data(), data.size()));

    MurmurCat split_cat;
    split_cat.Init(uint64_t{0}, data.size());
    int split = 1 + (i % (data.size() - 2));
    split_cat.Append(absl::string_view(data.data(), split));
    split_cat.Append(
        absl::string_view(data.data() + split, data.size() - split));
    EXPECT_EQ(expected, split_cat.GetHash());
  }
}

TEST(MurmurCat, CompatibilityWithMurmurHashSplit3) {
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  for (int i = 0; i < 1000; ++i) {
    std::string data(i + 9, 0);
    InitializeRandomString(data.size(), &data[0], &rnd);
    uint64_t expected =
        util_hash::MurmurHash64(absl::string_view(data.data(), data.size()));

    MurmurCat triple_cat;
    triple_cat.Init(uint64_t{0}, data.size());
    int split0 = i & 7;
    int split1 = 8 + (i % (data.size() - 2));
    triple_cat.Append(absl::string_view(data.data(), split0));
    triple_cat.Append(absl::string_view(data.data() + split0, split1 - split0));
    triple_cat.Append(
        absl::string_view(data.data() + split1, data.size() - split1));
    EXPECT_EQ(expected, triple_cat.GetHash());
  }
}

static void BM_MurmurCatHash64(benchmark::State& state) {
  const int size_shift = state.range(0);
  std::string data(1 << size_shift, 0);
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  InitializeRandomString(data.size(), &data[0], &rnd);
  state.SetBytesProcessed(static_cast<int64_t>(state.max_iterations) *
                          static_cast<int64_t>(data.size()));

  MurmurCat cat;
  cat.Init(uint64_t{0}, data.size() * state.max_iterations);
  uint64_t hash = 0;
  int i = 0;
  for (auto _ : state) {  // NOLINT
    data[0] = i & 127;
    cat.Append(data);
    hash ^= cat.GetHash();
    ++i;
  }
  VLOG(1) << hash;
}

static void BM_MurmurHash64(benchmark::State& state) {
  const int size_shift = state.range(0);
  std::string data(1 << size_shift, 0);
  ACMRandom rnd(ACMRandom::DeterministicSeed());
  InitializeRandomString(data.size(), &data[0], &rnd);
  state.SetBytesProcessed(static_cast<int64_t>(state.max_iterations) *
                          static_cast<int64_t>(data.size()));
  uint64_t hash = 0;
  int i = 0;
  for (auto _ : state) {  // NOLINT
    data[0] = i & 127;
    hash ^= util_hash::MurmurHash64(data);
    ++i;
  }
  VLOG(1) << hash;
}

BENCHMARK(BM_MurmurCatHash64)->Arg(1)->Arg(2)->Arg(4)->Arg(7)->Arg(10)->Arg(25);
BENCHMARK(BM_MurmurHash64)->Arg(1)->Arg(2)->Arg(4)->Arg(7)->Arg(10)->Arg(25);
}  // namespace util_hash
