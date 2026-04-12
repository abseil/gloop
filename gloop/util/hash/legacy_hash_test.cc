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

#include "gloop/util/hash/legacy_hash.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-W#warnings"
#include <ext/hash_map>
#pragma clang diagnostic pop

#include <cstdint>
#include <string>
#include <utility>

#include "gloop/gloop_test.h"
#include "gloop/util/hash/builtin_type_hash.h"
#include "gloop/util/random/acmrandom.h"

using __gnu_cxx::hash;

TEST(LegacyHash, IntegerHashing) {
  EXPECT_NE(LegacyHash<int>()(0), LegacyHash<int>()(1));
  EXPECT_NE(LegacyHash<int>()(1), LegacyHash<int>()(2));
  EXPECT_NE(LegacyHash<uint64_t>()(0xaaaaaaaaaaaaaaaa),
            LegacyHash<uint64_t>()(0xbaaaaaaaaaaaaaab));
  EXPECT_EQ(LegacyHash<int>()(17), hash<int>()(17));
  EXPECT_EQ(LegacyHash<int64_t>()(-1), hash<int64_t>()(-1));
}

TEST(LegacyHash, Pair) {
  EXPECT_NE((LegacyHash<std::pair<int, std::string> >()(
                std::pair<int, std::string>(0, "xyzzy"))),
            (LegacyHash<std::pair<int, std::string> >()(
                std::pair<int, std::string>(1, "xyzzy"))));
  EXPECT_NE((LegacyHash<std::pair<int, std::string> >()(
                std::pair<int, std::string>(0, "xyzzy"))),
            (LegacyHash<std::pair<int, std::string> >()(
                std::pair<int, std::string>(0, "foobar"))));
  EXPECT_NE((LegacyHash<std::pair<std::string, int> >()(
                std::pair<std::string, int>("xyzzy", 2))),
            (LegacyHash<std::pair<std::string, int> >()(
                std::pair<std::string, int>("xyzzy", 3))));
  EXPECT_NE((LegacyHash<std::pair<std::string, int> >()(
                std::pair<std::string, int>("xyzzy", 2))),
            (LegacyHash<std::pair<std::string, int> >()(
                std::pair<std::string, int>("foobar", 2))));
}

// Confirm that HashTo32 for integer types is unchanging forever.
TEST(LegacyHash, HashTo32IntegerIsUnchanging) {
  const int kIters = 100;                         // Must be repeatable.
  ACMRandom rng(ACMRandom::DeterministicSeed());  // Must be repeatable.
  uint64_t u = 0;
  for (int i = 0; i < kIters; ++i) {
    int64_t data = rng.Next64();
    u = Hash64NumWithSeed(HashTo32(static_cast<char>(data)), u);
    u = Hash64NumWithSeed(HashTo32(static_cast<signed char>(data)), u);
    u = Hash64NumWithSeed(HashTo32(static_cast<unsigned char>(data)), u);
    u = Hash64NumWithSeed(HashTo32(static_cast<short>(data)), u);
    u = Hash64NumWithSeed(HashTo32(static_cast<unsigned short>(data)), u);
    u = Hash64NumWithSeed(HashTo32(static_cast<int>(data)), u);
    u = Hash64NumWithSeed(HashTo32(static_cast<unsigned int>(data)), u);
    u = Hash64NumWithSeed(HashTo32(static_cast<long>(data)), u);
    u = Hash64NumWithSeed(HashTo32(static_cast<unsigned long>(data)), u);
    u = Hash64NumWithSeed(HashTo32(static_cast<long long>(data)), u);
    u = Hash64NumWithSeed(HashTo32(static_cast<unsigned long long>(data)), u);
  }
  // The procedure above must generate this value.
  EXPECT_EQ(uint64_t{uint64_t{522075374766263304}}, u);
}
