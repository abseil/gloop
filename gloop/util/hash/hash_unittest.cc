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

//
// Copyright (C) 1999 and onwards Google, Inc.
//

//
// This file tests hash.h
// For some hash functions it calls the function once and prints the output.
// For others it does a bit more.  There are few if any "serious" tests of
// hash quality in this file.

#include "gloop/util/hash/hash.h"

#include <math.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-W#warnings"
#include <ext/__hash>
#include <ext/hash_map>
#pragma clang diagnostic pop

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ios>
#include <iostream>
#include <iterator>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/casts.h"
#include "absl/base/macros.h"
#include "absl/base/port.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "absl/log/log.h"
#include "absl/numeric/int128.h"
#include "absl/strings/cord.h"
#include "absl/strings/cord_test_helpers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "gloop/util/hash/builtin_type_hash.h"
#include "gloop/util/hash/farmhash.h"
#include "gloop/util/hash/hasher.h"
#include "gloop/util/hash/jenkins.h"
#include "gloop/util/hash/jenkins_lookup2.h"
#include "gloop/util/hash/legacy_hash.h"
#include "gloop/util/hash/string_hash.h"
#include "gloop/util/random/acmrandom.h"
#include "gtest/gtest.h"

using __gnu_cxx::hash;

namespace {

constexpr char s[] = "Hello, world";
constexpr char s2[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

constexpr int64_t s64 = 0x0123456789ABCDEFLL;
constexpr uint64_t u64 = 0xFEDCBA9876543210ULL;
constexpr int32_t s32 = 0x12345678L;
constexpr uint32_t u32 = 0x87654321UL;
constexpr int16_t s16 = -12874;
constexpr uint16_t u16 = 12874;
constexpr signed char s8 = 'h';
constexpr char u8 = 'i';

}  // namespace

#define HASH(from, to, var, value, ...)                               \
  do {                                                                \
    std::cout << "Hashing " << (from) << " " << (var) << " to " << to \
              << "bits: " << HashTo##to(__VA_ARGS__) << "\n";         \
    EXPECT_EQ(value, HashTo##to(__VA_ARGS__));                        \
    EXPECT_NE(value, kIllegalHash##to);                               \
  } while (0)

#define FINGERPRINT(from, var, value, ...)                          \
  do {                                                              \
    std::cout << "Fingerprinting " << (from) << " " << (var) << " " \
              << Fingerprint(__VA_ARGS__) << "\n";                  \
    EXPECT_EQ(value, Fingerprint(__VA_ARGS__));                     \
  } while (0)

TEST(Hash, Simple) {
  HASH("string", 32, s, 3333966598UL, s, sizeof(s) - 1);
  HASH("string", 16, s, 50872, s, sizeof(s) - 1);
  FINGERPRINT("string", s, uint64_t{7002428594095431544u}, s, sizeof(s) - 1);
  std::cout << "\n";

  HASH("int64", 32, s64, 2905288307UL, s64);
  FINGERPRINT("int64", s64, uint64_t{12478118266929040348u}, s64);
  std::cout << "\n";

  HASH("uint64", 32, u64, 2270422886UL, u64);
  FINGERPRINT("int64", u64, uint64_t{9751392047640360519u}, u64);
  std::cout << "\n";

  HASH("int32", 32, s32, 898813988UL, s32);
  FINGERPRINT("int32", s32, uint64_t{3186554785951438647u}, s32);
  std::cout << "\n";

  HASH("uint32", 32, u32, 1904075167UL, u32);
  FINGERPRINT("int32", u32, uint64_t{496459378949975917u}, u32);
  std::cout << "\n";

  HASH("int16", 32, s16, 4061345062UL, s16);
  FINGERPRINT("int16", s16, uint64_t{6361105804315517388u}, s16);
  std::cout << "\n";

  HASH("uint16", 32, u16, 1017571297UL, u16);
  FINGERPRINT("int16", u16, uint64_t{3734383939737814843u}, u16);
  std::cout << "\n";

  HASH("int8", 32, s8, 1414988UL, s8);
  FINGERPRINT("int8", s8, uint64_t{2988008119595602476u}, s8);
  std::cout << "\n";

  HASH("uint8", 32, u8, 2868795942UL, u8);
  FINGERPRINT("int8", u8, uint64_t{11124244741422027502u}, u8);
  std::cout << "\n";

  // Check hashing of uint128 keys
  // (there is no HashToXXX function for int128s, so no HASH() macro here)
  __gnu_cxx::hash_map<absl::uint128, std::string, hash<absl::uint128>> m;
  absl::uint128 one(1);
  absl::uint128 two = absl::MakeUint128(2000, 2);
  m[one] = "one";
  m[two] = "two";
  absl::uint128 k(1);
  absl::uint128 t = absl::MakeUint128(2000, 2);
  EXPECT_EQ(m[k], "one");
  EXPECT_EQ(m[t], "two");
}

static inline uint32_t Google1AtReferenceImpl(const char* ptr2) {
  const signed char* signed_ptr = reinterpret_cast<const signed char*>(ptr2);
  return (static_cast<signed char>(signed_ptr[0]) +
          (static_cast<uint32_t>(signed_ptr[1]) << 8) +
          (static_cast<uint32_t>(signed_ptr[2]) << 16) +
          (static_cast<uint32_t>(signed_ptr[3]) << 24));
}

TEST(Hash, Google1AtSimple) {
  EXPECT_EQ(0, Google1At("\0\0\0\0"));
  EXPECT_EQ(0x7f7f7f7f, Google1At("\x7f\x7f\x7f\x7f"));
  EXPECT_EQ(0x807f7f7f, Google1At("\x7f\x7f\x7f\x80"));
  EXPECT_EQ(0xffffff80, Google1At("\x80\x00\x00\x00"));
}

TEST(Hash, Google1AtCombinations) {
  char v[10] = {0,
                1,
                33,
                127,
                static_cast<char>(128),
                static_cast<char>(129),
                static_cast<char>(254),
                static_cast<char>(255)};
  for (int i = 0; i < 4096; ++i) {
    char str[4] = {v[i & 7], v[(i >> 3) & 7], v[(i >> 6) & 7], v[(i >> 9) & 7]};
    EXPECT_EQ(Google1AtReferenceImpl(str), Google1At(str));
  }
}

// Check hashing of pairs. Make it recursive for fun.
TEST(Hash, Pair) {
  typedef std::pair<std::pair<std::string, int>, std::string> T;
  __gnu_cxx::hash_map<T, int, hash<T>> hm1;
  // explicit strings to avoid errors from pair<char*> vs const char*
  hm1[std::make_pair(std::make_pair(std::string("one"), -1),
                     std::string("ONE"))] = 1;
  hm1[std::make_pair(std::make_pair(std::string("two"), -2),
                     std::string("TWO"))] = 2;
  EXPECT_EQ(hm1[std::make_pair(std::make_pair(std::string("one"), -1),
                               std::string("ONE"))],
            1);
  EXPECT_EQ(hm1[std::make_pair(std::make_pair(std::string("two"), -2),
                               std::string("TWO"))],
            2);

  absl::node_hash_map<T, int> hm2;
  hm2[std::make_pair(std::make_pair(std::string("one"), -1),
                     std::string("ONE"))] = 1;
  hm2[std::make_pair(std::make_pair(std::string("two"), -2),
                     std::string("TWO"))] = 2;
  EXPECT_EQ(hm2[std::make_pair(std::make_pair(std::string("one"), -1),
                               std::string("ONE"))],
            1);
  EXPECT_EQ(hm2[std::make_pair(std::make_pair(std::string("two"), -2),
                               std::string("TWO"))],
            2);
}

TEST(Hash, PairStrip) {
  using PRaw = std::pair<std::string, std::string>;
  using PConst = std::pair<const std::string, const std::string>;
  using PRefs = std::pair<std::string&, std::string&>;
  using PCRefs = std::pair<const std::string&, const std::string&>;
  std::string k1("abc");
  std::string k2("abc");
  EXPECT_EQ(hash<PRaw>()(PRaw(k1, k2)), hash<PConst>()(PConst(k1, k2)));
  EXPECT_EQ(hash<PRaw>()(PRaw(k1, k2)), hash<PRefs>()(PRefs(k1, k2)));
  EXPECT_EQ(hash<PRaw>()(PRaw(k1, k2)), hash<PCRefs>()(PCRefs(k1, k2)));
}

// Check hashing of pairs with a bool member.
TEST(Hash, PairBool) {
  typedef std::pair<int, bool> T;
  absl::node_hash_map<T, int, hash<T>> hm1;
  hm1[std::make_pair(1, false)] = 1;
  hm1[std::make_pair(1, true)] = 2;
  EXPECT_EQ(hm1[std::make_pair(1, false)], 1);
  EXPECT_EQ(hm1[std::make_pair(1, true)], 2);

  __gnu_cxx::hash_map<T, int, GoodFastHash<T>> hm2;
  hm2[std::make_pair(1, false)] = 1;
  hm2[std::make_pair(1, true)] = 2;
  EXPECT_EQ(hm2[std::make_pair(1, false)], 1);
  EXPECT_EQ(hm2[std::make_pair(1, true)], 2);
}

// Check that GoodFastHash<pair<T, U>> recursively calls GoodFashHash<T> and
// GoodFastHash<U>, where these classes exist.  Also check that we compile OK if
// T or U only has a GoodFastHash.
//
// GoodFastHash<pair<T, U>> only invokes GoodFastHash<T/U> in C++11; this test
// doesn't work (and in fact won't compile) otherwise.

// Dummy1 has hash and GoodFastHash, while Dummy2 only has GoodFastHash.
struct Dummy1 {};
struct Dummy2 {};

struct NotCopyable {
  NotCopyable() {}

 private:
  NotCopyable(const NotCopyable&) = delete;
  NotCopyable& operator=(const NotCopyable&) = delete;
};

HASH_NAMESPACE_DECLARATION_START
template <>
struct hash<Dummy1> {
  static size_t num_calls;
  size_t operator()(const Dummy1& dummy) const {
    num_calls++;
    return 0;
  }
};
size_t hash<Dummy1>::num_calls = 0;
HASH_NAMESPACE_DECLARATION_END

template <>
struct GoodFastHash<Dummy1> {
  static size_t num_calls;
  size_t operator()(const Dummy1& dummy) const {
    num_calls++;
    return 0;
  }
};
size_t GoodFastHash<Dummy1>::num_calls = 0;

template <>
struct GoodFastHash<Dummy2> {
  static size_t num_calls;
  size_t operator()(const Dummy2& dummy) const {
    num_calls++;
    return 0;
  }
};
size_t GoodFastHash<Dummy2>::num_calls = 0;

template <>
struct GoodFastHash<NotCopyable> {
  size_t operator()(const NotCopyable& dummy) const { return 0; }
};

void ClearHashersNumCalls() {
  hash<Dummy1>::num_calls = 0;
  GoodFastHash<Dummy1>::num_calls = 0;
  GoodFastHash<Dummy2>::num_calls = 0;
}

TEST(Hash, GoodFastHashPair_Dummy1Dummy1) {
  ClearHashersNumCalls();
  GoodFastHash<std::pair<Dummy1, Dummy1>>()({Dummy1(), Dummy1()});
  EXPECT_EQ(2, GoodFastHash<Dummy1>::num_calls);
  EXPECT_EQ(0, hash<Dummy1>::num_calls);
  EXPECT_EQ(0, GoodFastHash<Dummy2>::num_calls);
}

TEST(Hash, GoodFastHashPair_Dummy1Dummy2) {
  ClearHashersNumCalls();
  GoodFastHash<std::pair<Dummy1, Dummy2>>()({Dummy1(), Dummy2()});
  EXPECT_EQ(1, GoodFastHash<Dummy1>::num_calls);
  EXPECT_EQ(0, hash<Dummy1>::num_calls);
  EXPECT_EQ(1, GoodFastHash<Dummy2>::num_calls);
}

TEST(Hash, GoodFastHashPair_Dummy2Dummy1) {
  ClearHashersNumCalls();
  GoodFastHash<std::pair<Dummy2, Dummy1>>()({Dummy2(), Dummy1()});
  EXPECT_EQ(1, GoodFastHash<Dummy1>::num_calls);
  EXPECT_EQ(0, hash<Dummy1>::num_calls);
  EXPECT_EQ(1, GoodFastHash<Dummy2>::num_calls);
}

TEST(Hash, GoodFastHashPair_Dummy2Dummy2) {
  ClearHashersNumCalls();
  GoodFastHash<std::pair<Dummy2, Dummy2>>()({Dummy2(), Dummy2()});
  EXPECT_EQ(0, GoodFastHash<Dummy1>::num_calls);
  EXPECT_EQ(0, hash<Dummy1>::num_calls);
  EXPECT_EQ(2, GoodFastHash<Dummy2>::num_calls);
}

// Check that util_hash::Hash calls GoodFastHash where appropriate.

TEST(Hash, HashTwo_Dummy1Dummy1) {
  ClearHashersNumCalls();
  ::util_hash::Hash(Dummy1(), Dummy1());
  EXPECT_EQ(2, GoodFastHash<Dummy1>::num_calls);
  EXPECT_EQ(0, hash<Dummy1>::num_calls);
  EXPECT_EQ(0, GoodFastHash<Dummy2>::num_calls);
}

TEST(Hash, HashTwo_Dummy1Dummy2) {
  ClearHashersNumCalls();
  ::util_hash::Hash(Dummy1(), Dummy2());
  EXPECT_EQ(1, GoodFastHash<Dummy1>::num_calls);
  EXPECT_EQ(0, hash<Dummy1>::num_calls);
  EXPECT_EQ(1, GoodFastHash<Dummy2>::num_calls);
}

TEST(Hash, HashTwo_Dummy2Dummy1) {
  ClearHashersNumCalls();
  ::util_hash::Hash(Dummy2(), Dummy1());
  EXPECT_EQ(1, GoodFastHash<Dummy1>::num_calls);
  EXPECT_EQ(0, hash<Dummy1>::num_calls);
  EXPECT_EQ(1, GoodFastHash<Dummy2>::num_calls);
}

TEST(Hash, HashTwo_Dummy2Dummy2) {
  ClearHashersNumCalls();
  ::util_hash::Hash(Dummy2(), Dummy2());
  EXPECT_EQ(0, GoodFastHash<Dummy1>::num_calls);
  EXPECT_EQ(0, hash<Dummy1>::num_calls);
  EXPECT_EQ(2, GoodFastHash<Dummy2>::num_calls);
}

namespace {

namespace best_avail {
template <size_t N>
struct HasHash {};
template <size_t N>
struct HasGood {};
template <size_t N>
struct HasBoth {};
}  // namespace best_avail

template <size_t N>
struct ToyHasher {
  template <typename T>
  size_t operator()(const T&) const {
    return N;
  }
};
}  // namespace

// ====================
HASH_NAMESPACE_DECLARATION_START
template <>
struct hash<best_avail::HasHash<0>> : ToyHasher<1> {};
template <>
struct hash<best_avail::HasBoth<0>> : ToyHasher<1> {};
HASH_NAMESPACE_DECLARATION_END
template <>
struct GoodFastHash<best_avail::HasGood<0>> : ToyHasher<2> {};
template <>
struct GoodFastHash<best_avail::HasBoth<0>> : ToyHasher<2> {};
TEST(BestAvailableHashTest, AllDeclared) {
  using util_hash::internal::ChooseHasher;
  EXPECT_EQ(1, ChooseHasher(best_avail::HasHash<0>{}));
  EXPECT_EQ(2, ChooseHasher(best_avail::HasGood<0>{}));
  EXPECT_EQ(2, ChooseHasher(best_avail::HasBoth<0>{}));
}

// ====================
HASH_NAMESPACE_DECLARATION_START
template <>
struct hash<best_avail::HasHash<1>> : ToyHasher<11> {};
template <>
struct hash<best_avail::HasBoth<1>> : ToyHasher<11> {};
HASH_NAMESPACE_DECLARATION_END
TEST(BestAvailableHashTest, HashDeclared) {
  using util_hash::internal::ChooseHasher;
  EXPECT_EQ(11, ChooseHasher(best_avail::HasHash<1>{}));
  EXPECT_EQ(12, ChooseHasher(best_avail::HasGood<1>{}));
  EXPECT_EQ(12, ChooseHasher(best_avail::HasBoth<1>{}));
}
template <>
struct GoodFastHash<best_avail::HasGood<1>> : ToyHasher<12> {};
template <>
struct GoodFastHash<best_avail::HasBoth<1>> : ToyHasher<12> {};

// ====================
template <>
struct GoodFastHash<best_avail::HasGood<2>> : ToyHasher<22> {};
template <>
struct GoodFastHash<best_avail::HasBoth<2>> : ToyHasher<22> {};
TEST(BestAvailableHashTest, GoodDeclared) {
  using util_hash::internal::ChooseHasher;
  EXPECT_EQ(21, ChooseHasher(best_avail::HasHash<2>{}));
  EXPECT_EQ(22, ChooseHasher(best_avail::HasGood<2>{}));
  EXPECT_EQ(22, ChooseHasher(best_avail::HasBoth<2>{}));
}
HASH_NAMESPACE_DECLARATION_START
template <>
struct hash<best_avail::HasHash<2>> : ToyHasher<21> {};
template <>
struct hash<best_avail::HasBoth<2>> : ToyHasher<21> {};
HASH_NAMESPACE_DECLARATION_END

// ====================
TEST(BestAvailableHashTest, NoneDeclared) {
  using util_hash::internal::ChooseHasher;
  EXPECT_EQ(31, ChooseHasher(best_avail::HasHash<3>{}));
  EXPECT_EQ(32, ChooseHasher(best_avail::HasGood<3>{}));
  EXPECT_EQ(32, ChooseHasher(best_avail::HasBoth<3>{}));
}
HASH_NAMESPACE_DECLARATION_START
template <>
struct hash<best_avail::HasHash<3>> : ToyHasher<31> {};
template <>
struct hash<best_avail::HasBoth<3>> : ToyHasher<31> {};
HASH_NAMESPACE_DECLARATION_END
template <>
struct GoodFastHash<best_avail::HasGood<3>> : ToyHasher<32> {};
template <>
struct GoodFastHash<best_avail::HasBoth<3>> : ToyHasher<32> {};

// ====================

// Confirm that HasherXX() is an incremental version of
// HashXXStringWithSeed().
TEST(Hash, HasherXXvsHashXXStringWithSeed) {
  const char s[] = "http://testurl.com/to/parse.html";

  uint32_t expected32 =
      Hash32StringWithSeed(absl::string_view(s, sizeof(s)), MIX32);
  uint64_t expected64 = Hash64StringWithSeed(s, sizeof(s), MIX64);
  for (int i = 0; i < sizeof(s); ++i)
    for (int j = i; j < sizeof(s); ++j)
      for (int k = j; k < sizeof(s); ++k) {
        SCOPED_TRACE(absl::StrCat("i=", i, ", j=", j, ", k=", k));
        // Use asserts in this test to avoid flooding the results with so
        // many failures that it can't be displayed in Sponge.
        Hasher32 h32(MIX32);
        h32.AddString(absl::string_view(s, i));
        h32.AddString(absl::string_view(s + i, j - i));
        h32.AddString(absl::string_view(s + j, k - j));
        h32.AddString(absl::string_view(s + k, sizeof(s) - k));
        ASSERT_EQ(h32.Result(), expected32);
        ASSERT_NE(h32.ResultNonReserved(), kIllegalHash32);
        Hasher64 h64(MIX64);
        h64.AddString(absl::string_view(s, i));
        h64.AddString(absl::string_view(s + i, j - i));
        h64.AddString(absl::string_view(s + j, k - j));
        h64.AddString(absl::string_view(s + k, sizeof(s) - k));
        ASSERT_EQ(h64.Result(), expected64);
      }
  std::cout << "Hasher{32,64} returns same results as "
            << "Hash{32,64}StringWithSeed(s,len,MIX{32,64})\n";
}

// Confirm that Fingerprinting integer types is unchanging forever.
TEST(Hash, FingerprintIntegerIsUnchanging) {
  const int kIters = 100;                         // Must be repeatable.
  ACMRandom rng(ACMRandom::DeterministicSeed());  // Must be repeatable.
  uint64_t u = 0;
  for (int i = 0; i < kIters; ++i) {
    int64_t data = rng.Next64();
    u = Hash64NumWithSeed(Fingerprint(static_cast<char>(data)), u);
    u = Hash64NumWithSeed(Fingerprint(static_cast<int8_t>(data)), u);
    u = Hash64NumWithSeed(Fingerprint(static_cast<uint8_t>(data)), u);
    u = Hash64NumWithSeed(Fingerprint(static_cast<int16_t>(data)), u);
    u = Hash64NumWithSeed(Fingerprint(static_cast<uint16_t>(data)), u);
    u = Hash64NumWithSeed(Fingerprint(static_cast<int32_t>(data)), u);
    u = Hash64NumWithSeed(Fingerprint(static_cast<uint32_t>(data)), u);
    u = Hash64NumWithSeed(Fingerprint(static_cast<int64_t>(data)), u);
    u = Hash64NumWithSeed(Fingerprint(static_cast<uint64_t>(data)), u);
    // Note: this test used to be written in terms of casting to
    // native integral types (e.g. `long long`).  This was inherently
    // non-portable because the casts would mask `data` differently on
    // different platforms.  When changing the test to its current
    // portable form, the "extra" calls to Fingerprint() were retained
    // so the expectation below could be retained.
    u = Hash64NumWithSeed(Fingerprint(static_cast<int64_t>(data)), u);
    u = Hash64NumWithSeed(Fingerprint(static_cast<uint64_t>(data)), u);
  }
  // The procedure above must generate this value.
  EXPECT_EQ(uint64_t{uint64_t{4832948539582754558}}, u);
}

// Be paranoid about subtle changes that may affect both the reference
// implementation and the non-reference implementation.  Verify that
// certain known strings generate certain known hashes.

constexpr uint32_t expected_hash[] = {
    0x0004cb2f, 0xf48b6a63, 0x127cf8f4, 0x19d6e07c, 0x593c013b,
    0xbd02a456, 0x41f49b7e, 0xdbb5422f, 0x8c840dc7, 0xa61a6869,
    0x86df370b, 0xd2fd8134, 0xa2fd3c12, 0x79c9af07, 0xb9194973,
    0xe823270e, 0x8619903d, 0x9c14eefd, 0xa4b0dccf, 0xae51bcf7,
    0x3172f051, 0x526602ca, 0x30689dac, 0xbe851fc4, 0x865a21d4,
};

TEST(Hash, CompareToKnownValues) {
  char buf[ABSL_ARRAYSIZE(expected_hash)];
  uint32_t i, h;

  LOG(INFO) << "CompareToKnownValues";
  for (i = 0; i < sizeof(buf); i++) {
    buf[i] = (i & 1) ? -i : i;
  }
  for (i = 0; i < sizeof(buf); i++) {
    SCOPED_TRACE(absl::StrCat("i=", i));
    h = Hash32StringWithSeed(absl::string_view(buf, i), 314159);
    EXPECT_EQ(expected_hash[i], h);
  }

  uint64_t value = 0xc18b4f51027f3b70ULL;
  EXPECT_EQ(uint64_t{0x66007fe9d1689e74}, Hash64NumWithSeed(0, 0));
  EXPECT_EQ(uint64_t{0xc9c918f01db1c114u}, Hash64NumWithSeed(0, MIX64));
  EXPECT_EQ(uint64_t{0xf31e78a6fe23df78u}, Hash64NumWithSeed(MIX64, 0));
  EXPECT_EQ(uint64_t{0xb7af56f1d1053fd3u}, Hash64NumWithSeed(MIX64, MIX64));
  EXPECT_EQ(uint64_t{0xfcb97a212c793dd4u}, Hash64NumWithSeed(0, value));
  EXPECT_EQ(uint64_t{0x9f698472ce319675u}, Hash64NumWithSeed(value, 0));
  EXPECT_EQ(uint64_t{0x5323a3995e7a090e}, Hash64NumWithSeed(value, value));
  EXPECT_EQ(uint64_t{0x550851514cbcc02f}, Hash64NumWithSeed(value, MIX64));
}

TEST(Hash, CombineFingerprintHalves) {
  EXPECT_EQ(uint64_t{uint64_t{0x123456789abcdef0}},
            CombineFingerprintHalves(0x12345678ul, 0x9abcdef0ul));
  EXPECT_EQ(uint64_t{uint64_t{0x130f9bef94a0a928}},
            CombineFingerprintHalves(0, 0));
  EXPECT_EQ(uint64_t{uint64_t{0x2}}, CombineFingerprintHalves(0, 2));
}

// Print basic stats and sample outputs for some of the hash functions.
// If you are concerned about whether a hash function has changed over time,
// you can compare the output over time.
TEST(Hash, BasicStats) {
  int a[256 * 256];
  char c[256];
  for (int i = 0; i < ABSL_ARRAYSIZE(c); i++) {
    c[i] = static_cast<char>(i);
  }
  for (int i = 0; i < ABSL_ARRAYSIZE(a) / 2; i++) {
    a[i] = i * i;
    a[i + ABSL_ARRAYSIZE(a) / 2] = -i * i;
  }
  std::string x1 = std::string(1, c[67]);
  std::string x1000 = std::string(1000, c[67]);

  // First, some of the functions that DO NOT CHANGE over time.

  size_t max_hash = 0;
  size_t min_hash = ~max_hash;
  uint32_t max_hash32 = 0;
  uint32_t min_hash32 = ~max_hash32;
  uint64_t max_hash64 = 0;
  uint64_t min_hash64 = ~max_hash64;
  LOG(INFO) << "HashTo32(char)";
  for (int i = 0; i < ABSL_ARRAYSIZE(c); i++) {
    size_t h = HashTo32(c[i]);
    EXPECT_EQ(h, static_cast<uint32_t>(h));
    min_hash32 = std::min<uint32_t>(min_hash32, h);
    max_hash32 = std::max<uint32_t>(max_hash32, h);
  }
  EXPECT_GT(log2(max_hash32 - min_hash32), 31.98);
  LOG(INFO) << " range(hash(0) ... hash(255)) = " << std::hex << min_hash32
            << " to " << max_hash32;
  LOG(INFO) << " sample hash: c[73] -> " << std::hex << HashTo32(c[73]);

  max_hash32 = 0;
  min_hash32 = ~max_hash32;
  LOG(INFO) << "HashTo32(uint32)";
  for (int i = 0; i < ABSL_ARRAYSIZE(a); i++) {
    size_t h = HashTo32(a[i]);
    EXPECT_EQ(h, static_cast<uint32_t>(h));
    min_hash32 = std::min<uint32_t>(min_hash32, h);
    max_hash32 = std::max<uint32_t>(max_hash32, h);
  }
  EXPECT_GT(log2(max_hash32 - min_hash32), 31.98);
  LOG(INFO) << " range(hash(0), hash(1), hash(4), hash(9), ...) = " << std::hex
            << min_hash32 << " to " << max_hash32;
  int k = ABSL_ARRAYSIZE(a) / 3;
  LOG(INFO) << " sample hash: a[" << k << "] -> " << std::hex << HashTo32(a[k]);

  max_hash32 = 0;
  min_hash32 = ~max_hash32;
  LOG(INFO) << "Hash32StringWithSeed()";
  for (int i = 0; i < strlen(s2); i++) {
    for (int j = i; j < strlen(s2); j++) {
      size_t h = Hash32StringWithSeed(absl::string_view(s2 + i, j - i), MIX32);
      EXPECT_EQ(h, static_cast<uint32_t>(h));
      min_hash32 = std::min<uint32_t>(min_hash32, h);
      max_hash32 = std::max<uint32_t>(max_hash32, h);
    }
  }
  EXPECT_GT(log2(max_hash32 - min_hash32), 31.99);
  LOG(INFO) << " range(...) = " << std::hex << min_hash32 << " to "
            << max_hash32;
  std::string url("http://www.google.com/");
  LOG(INFO) << " sample hash with seed 19: " << url << " -> " << std::hex
            << Hash32StringWithSeed(absl::string_view(url.data(), url.size()),
                                    19);

  max_hash64 = 0;
  min_hash64 = ~max_hash64;
  LOG(INFO) << "Hash64StringWithSeed()";
  for (int i = 0; i < strlen(s2); i++) {
    for (int j = i; j < strlen(s2); j++) {
      uint64_t h = Hash64StringWithSeed(s2 + i, j - i, MIX64);
      min_hash64 = std::min<uint64_t>(min_hash64, h);
      max_hash64 = std::max<uint64_t>(max_hash64, h);
    }
  }
  EXPECT_GT(log2(max_hash64 - min_hash64), 63.99);
  LOG(INFO) << " range(...) = " << std::hex << min_hash64 << " to "
            << max_hash64;
  LOG(INFO) << " sample hash with seed 19: " << url << " -> " << std::hex
            << Hash64StringWithSeed(url.data(), url.size(), 19);

  max_hash64 = 0;
  min_hash64 = ~max_hash64;
  LOG(INFO) << "Fingerprint(const char *, uint32)";
  for (int i = 0; i < strlen(s2); i++) {
    for (int j = i; j < strlen(s2); j++) {
      uint64_t h = Fingerprint(s2 + i, static_cast<uint32_t>(j - i));
      min_hash64 = std::min<uint64_t>(min_hash64, h);
      max_hash64 = std::max<uint64_t>(max_hash64, h);
    }
  }
  EXPECT_GT(log2(max_hash64 - min_hash64), 63.99);
  LOG(INFO) << " range(...) = " << std::hex << min_hash64 << " to "
            << max_hash64;
  LOG(INFO) << " sample Fingerprint: " << url << " -> " << std::hex
            << Fingerprint(url.data(), url.size());

  // Now, some of the functions that we've (1) left uncategorized, or
  // (2) explicitly labelled as CHANGEABLE over time.

  max_hash = 0;
  min_hash = ~max_hash;
  LOG(INFO) << "hash<string>";
  for (int i = 0; i < strlen(s2); i++) {
    for (int j = i; j < strlen(s2); j++) {
      size_t h = hash<std::string>()(std::string(s2).substr(i, j - i));
      min_hash = std::min<size_t>(min_hash, h);
      max_hash = std::max<size_t>(max_hash, h);
    }
  }

  // Sadly, our hash<string> is a 32-bit hash, even though it returns size_t.
  EXPECT_GT(log2(max_hash - min_hash), 31.99);

  LOG(INFO) << " range(...) = " << std::hex << min_hash << " to " << max_hash;
  LOG(INFO) << " sample hash: string(1, c[67]) -> " << std::hex
            << hash<std::string>()(x1);
  LOG(INFO) << " sample hash: string(1000, c[67]) -> " << std::hex
            << hash<std::string>()(x1000);

  max_hash = 0;
  min_hash = ~max_hash;
  LOG(INFO) << "HashStringThoroughly";
  for (int i = 0; i < strlen(s2); i++) {
    for (int j = i; j < strlen(s2); j++) {
      std::string s = std::string(s2).substr(i, j - i);
      size_t h = HashStringThoroughly(s.data(), s.size());
      min_hash = std::min<size_t>(min_hash, h);
      max_hash = std::max<size_t>(max_hash, h);
    }
  }
  EXPECT_GT(log2(max_hash - min_hash), sizeof(min_hash) * 8 - .01);
  LOG(INFO) << " range(...) = " << std::hex << min_hash << " to " << max_hash;
  LOG(INFO) << " sample hash: string(1, c[67]) -> " << std::hex
            << HashStringThoroughly(x1.data(), x1.size());
  LOG(INFO) << " sample hash: string(1000, c[67]) -> " << std::hex
            << HashStringThoroughly(x1000.data(), x1000.size());

  min_hash = ~0;
  max_hash = 0;
  LOG(INFO) << "hash<uint128>";
  for (int i = 0; i < ABSL_ARRAYSIZE(a); i++) {
    size_t h = hash<absl::uint128>()(absl::MakeUint128(i, i / 8));
    min_hash = std::min<size_t>(min_hash, h);
    max_hash = std::max<size_t>(max_hash, h);
  }
  EXPECT_GT(log2(max_hash - min_hash), sizeof(min_hash) * 8 - .02);
  LOG(INFO) << " range(...) = " << std::hex << min_hash << " to " << max_hash;
  absl::uint128 u = absl::MakeUint128(MIX64, ~MIX64);
  LOG(INFO) << " sample hash: " << std::hex << u << " -> "
            << hash<absl::uint128>()(u);
}

TEST(Hash, HashFloat) {
  // The only guarantee we make is that hash(0) == hash(-0)
  float zero_f = 0.0f;
  float neg_zero_f = -0.0f;
  // Ensure we're representing 0 and -0 correctly in the test
  EXPECT_NE(absl::bit_cast<uint32_t>(zero_f),
            absl::bit_cast<uint32_t>(neg_zero_f));

  uint64_t seed = 141421356;  // Arbitrary- some digits of sqrt(2)
  EXPECT_EQ(Hash64FloatWithSeed(zero_f, seed),
            Hash64FloatWithSeed(neg_zero_f, seed));

  double zero_l = 0.0l;
  double neg_zero_l = -0.0l;
  EXPECT_NE(absl::bit_cast<uint64_t>(zero_l),
            absl::bit_cast<uint32_t>(neg_zero_f));
  EXPECT_EQ(Hash64DoubleWithSeed(zero_l, seed),
            Hash64DoubleWithSeed(neg_zero_l, seed));

  // It is not technically guaranteed that hash(1.0) != hash(2.0), but it
  // is vanishingly unlikely. Just a sanity check, not a real verification
  // of the distribution.
  EXPECT_NE(Hash64FloatWithSeed(1, seed), Hash64FloatWithSeed(2, seed));
  EXPECT_NE(Hash64DoubleWithSeed(1, seed), Hash64DoubleWithSeed(2, seed));
}

template <typename T>
void Basic() {
  // This does little more than test for compile-time errors.
  absl::node_hash_map<std::string, int> a;
  T b;
#ifndef ABSL_HAVE_MEMORY_SANITIZER
  const int kIters = 1000;
#else
  const int kIters = 20;  // Avoids timeouts.
#endif  // !ABSL_HAVE_MEMORY_SANITIZER
  const std::string data(kIters, 'a');
  for (int i = 0; i < kIters; i++) {
    std::string s(i, 'a');
    a[s] = b[s] = i;
  }
  for (int i = 0; i < kIters; i++) {
    std::string s(i, 'a');
    EXPECT_EQ(i, a[s]);
    EXPECT_EQ(i, b[s]);
    s = "b" + s;
    EXPECT_EQ(0, a.count(s));
    EXPECT_EQ(0, b.count(s));
  }
}

TEST(FarmHash, Basic) {
  struct F {
    size_t operator()(absl::string_view v) const { return farmhash::Hash64(v); }
  };
  Basic<__gnu_cxx::hash_map<std::string, int, F>>();
}
TEST(FarmHashWithSeed, Basic) {
  static const uint64_t kSeed{0x7b4cce2dcf8c3cd7};
  struct F {
    size_t operator()(const std::string& v) const {
      return farmhash::Hash64WithSeed(v.data(), v.size(), kSeed);
    }
  };
  Basic<__gnu_cxx::hash_map<std::string, int, F>>();
}
TEST(FarmHashWithSeeds, Basic) {
  static const uint64_t kSeed0{0xedc68715a21b8d98};
  static const uint64_t kSeed1{0x649960b4a21677f0};
  struct F {
    size_t operator()(const std::string& v) const {
      return farmhash::Hash64WithSeeds(v.data(), v.size(), kSeed0, kSeed1);
    }
  };
  Basic<__gnu_cxx::hash_map<std::string, int, F>>();
}
TEST(GoodFastHash, Basic) {
  Basic<__gnu_cxx::hash_map<std::string, int, GoodFastHash<std::string>>>();
}

TEST(GoodFastHash, Enum) {
  enum class Enum { ONE, TWO };
  GoodFastHash<std::pair<std::string, Enum>> hasher;
  std::pair<std::string, Enum> one = {"ONE", Enum::ONE},
                               two = {"TWO", Enum::TWO};
  EXPECT_EQ(hasher(one), hasher({"ONE", Enum::ONE}));
  EXPECT_EQ(hasher(two), hasher({"TWO", Enum::TWO}));
}

enum class SpecializedEnum { ONE, TWO };

template <>
struct GoodFastHash<SpecializedEnum> {
  size_t operator()(const SpecializedEnum& v) const { return 123; }
};

TEST(GoodFastHash, EnumWithSpecialization) {
  // Test that explicit specialization is correctly chosen.
  EXPECT_EQ(GoodFastHash<SpecializedEnum>()(SpecializedEnum::ONE), 123);
  GoodFastHash<std::pair<std::string, SpecializedEnum>> hasher;
  EXPECT_EQ(hasher({"ONE", SpecializedEnum::ONE}),
            hasher({"ONE", SpecializedEnum::TWO}));
}

TEST(GoodFastHash, IsConsistentOnStrings) {
  GoodFastHash<std::string> hasher1;
  GoodFastHash<absl::string_view> hasher2;
  for (const char* const input :
       {"", "1", " ", "ab", "ba", "Hello World.",
        "This string is too long to fit into any reasonable SSO buffer."}) {
    EXPECT_EQ(hasher1(input), hasher2(input));
  }
}

// Helper for tuple tests.
template <typename T>
void Bump(T* t) {
  ++*t;
}
template <>
void Bump(std::string* t) {
  t->push_back(testing::UnitTest::GetInstance()->random_seed() & 255);
}
template <>
void Bump(void** t) {
  *t = reinterpret_cast<void*>(1);
}

// Sanity test dimension N of the tuple t.
template <typename T, int N>
void TupleTestOneDimension(T t) {
  T other = t;
  Bump(&std::get<N>(other));
  ASSERT_NE(hash<T>()(t), hash<T>()(other));
}

// Test dimentions 0 ... N-1.
template <typename T, int N>
void GenericTupleTestNDimensions(T t) {
  if (N > 0) {
    TupleTestOneDimension<T, (N > 0 ? N - 1 : 0)>(t);
    GenericTupleTestNDimensions<T, (N > 0 ? N - 1 : 0)>(t);
  }
}

template <typename T>
void ShowHash() {
  T t{};
  // Ensure at compile-time that all tuples we test can at least be hashed.
  LOG(INFO) << __PRETTY_FUNCTION__
            << " Hash of default-initialized tuple: " << hash<T>()(t);
}

template <typename T>
void GenericTupleTest() {
  ShowHash<T>();
  GenericTupleTestNDimensions<T, std::tuple_size<T>::value>(T());
}

TEST(Hash, EmptyTuple) { ShowHash<std::tuple<>>(); }
TEST(Hash, ITuple) { GenericTupleTest<std::tuple<int>>(); }
TEST(Hash, IITuple) { GenericTupleTest<std::tuple<int, int>>(); }
TEST(Hash, ICTuple) { GenericTupleTest<std::tuple<int, char>>(); }
TEST(Hash, IISTuple) { GenericTupleTest<std::tuple<int, int, std::string>>(); }
TEST(Hash, ISITuple) { GenericTupleTest<std::tuple<int, std::string, int>>(); }
TEST(Hash, SIITuple) { GenericTupleTest<std::tuple<std::string, int, int>>(); }
TEST(Hash, LongTuple) {
  GenericTupleTest<
      std::tuple<std::string, int32_t, int64_t, char, size_t, std::string, char,
                 void*, std::string, int, unsigned, char, char, char>>();
}
TEST(Hash, CStrCStrTuple) {
  char buf[4] = {'d', 'o', 'g', '\0'};
  std::tuple<char*, char*> x(buf, buf);
  GenericTupleTestNDimensions<std::tuple<char*, char*>, 2>(x);
}
TEST(Hash, NestedTuple) {
  using std::make_tuple;
  // Sanity
  ShowHash<std::tuple<std::tuple<int, char>, char>>();
  // Basic nested tuples test (thanks, Roman!)
  const std::tuple<int, int> t32{3, 2};
  const std::tuple<int, int> t23{2, 3};
  EXPECT_NE(util_hash::Hash(make_tuple(1, t32)),
            util_hash::Hash(make_tuple(1, t23)));
  EXPECT_NE(util_hash::Hash(make_tuple(t32, 1)),
            util_hash::Hash(make_tuple(t23, 1)));
  auto x = make_tuple(make_tuple(666, 42), 1337);
  auto y = make_tuple(make_tuple(666, 1337), 42);
  EXPECT_NE(util_hash::Hash(x), util_hash::Hash(y));

  // Slightly harder nested tuples test, times out on MSAN.
#ifndef ABSL_HAVE_MEMORY_SANITIZER
  absl::flat_hash_map<size_t, std::string> hashes;
  const int kMax = sizeof(decltype(util_hash::Hash(0))) == 4 ? 7 : 18;
  for (int i = 0; i < kMax; i++)
    for (int j = 0; j < kMax; j++)
      for (int k = 0; k < kMax; k++)
        for (int l = 0; l < kMax; l++) {
          size_t hash0 =
              util_hash::Hash(make_tuple(i, make_tuple(j, make_tuple(k, l))));
          size_t hash1 =
              util_hash::Hash(make_tuple(make_tuple(i, j), make_tuple(k, l)));
          size_t hash2 =
              util_hash::Hash(make_tuple(make_tuple(make_tuple(i, j), k), l));
          std::string s = absl::StrCat("[", i, " ", j, " ", k, " ", l, "]");
          VLOG(1) << s << " " << std::hex << hash0 << " " << hash1 << " "
                  << hash2;
          EXPECT_TRUE(hashes.emplace(hash0, s).second)
              << "hash collision " << hash0 << " between " << hashes[hash0]
              << " and " << s << " (hashes.size=" << hashes.size() << ")";
          EXPECT_TRUE(hashes.emplace(hash1, s).second)
              << "hash collision " << hash1 << " between " << hashes[hash1]
              << " and " << s << " (hashes.size=" << hashes.size() << ")";
          EXPECT_TRUE(hashes.emplace(hash2, s).second)
              << "hash collision " << hash2 << " between " << hashes[hash2]
              << " and " << s << " (hashes.size=" << hashes.size() << ")";
        }
#endif  // !ABSL_HAVE_MEMORY_SANITIZER
}

TEST(Hash, EmptyArray) { ShowHash<std::array<int, 0>>(); }
TEST(Hash, Array) {
  GenericTupleTestNDimensions<std::array<int, 1>, 1>(std::array<int, 1>());
  GenericTupleTestNDimensions<std::array<int, 2>, 2>(std::array<int, 2>());
  GenericTupleTestNDimensions<std::array<int, 30>, 30>(std::array<int, 30>());
  GenericTupleTestNDimensions<std::array<char, 7>, 7>(std::array<char, 7>());
  GenericTupleTestNDimensions<std::array<std::string, 3>, 3>(
      std::array<std::string, 3>());
  GenericTupleTestNDimensions<std::array<std::string, 5>, 5>(
      std::array<std::string, 5>());
}
TEST(Hash, NestedArray) { ShowHash<std::array<std::array<int, 3>, 4>>(); }

TEST(Hash, TupleWithArray) {
  ShowHash<std::tuple<std::array<std::array<int, 3>, 4>, int,
                      std::array<char, 0>, std::string>>();
}
TEST(Hash, ArrayWithTuples) {
  ShowHash<std::tuple<std::array<std::tuple<char, int, char>, 4>,
                      std::array<std::tuple<char, int, char>, 0>,
                      std::array<std::tuple<>, 4>, std::array<std::tuple<>, 0>,
                      std::string>>();
}

TEST(Hash, UtilHash_Hash_Simple) {
  using ::util_hash::Hash;
  ASSERT_NE(Hash(1, 0), Hash(0, 0));
  ASSERT_NE(Hash(1, std::array<int, 2>{{2, 3}}), Hash(0, 1, 2));
}

// Apply the given function to all permutations, including repeats, of v.
// The number of invocations of f will be the factorial of v.size().
// Using std::next_permutation directly "skips repeats," so we use a
// level of indirection to get the behavior we want.  Requires v.size() > 0.
template <typename F, typename Vec>
void ApplyToAllPermutations(const F& f, const Vec& v) {
  std::vector<typename Vec::const_pointer> w(v.size());
  for (int i = 0; i < v.size(); i++) {
    w[i] = &v[i];
  }
  do {
    Vec u(v.size());
    for (int i = 0; i < v.size(); i++) {
      u[i] = *w[i];
    }
    f(u);
  } while (std::next_permutation(std::begin(w), std::end(w)));
}

template <typename T, typename Hasher>
void TestPermutationWithHasher(const std::vector<T>& v, Hasher hasher) {
  VLOG(1) << __PRETTY_FUNCTION__ << " v = " << absl::StrJoin(v, " ");
  std::map<std::string, size_t> m;  // map from string to its hash
  std::set<size_t> hashes;          // the hash codes in m
  ApplyToAllPermutations(
      [&m, &hashes, hasher](const std::vector<T>& u) {
        size_t h = hasher(u);
        std::string s = absl::StrJoin(u, " ");
        VLOG(1) << s << " " << std::hex << h;
        auto p = m.insert(std::make_pair(s, h));
        if (p.second) {  // This permutation wasn't seen before.
          EXPECT_TRUE(hashes.insert(h).second);
        } else {  // This permutation was seen before.
          size_t old_hash_code_for_this_permutation = p.first->second;
          EXPECT_EQ(old_hash_code_for_this_permutation, h);
        }
      },
      v);
}

template <typename T>
void TestPermutation(const std::vector<T>& v) {
  auto hasher0 = [](absl::Span<const T> u) {
    auto combine = [](size_t a, T b) {
      // Note the order of the arguments!
      return util_hash::Hash(b, a);
    };
    return std::accumulate(u.begin(), u.end(), static_cast<size_t>(0), combine);
  };

  auto hasher1 = [](absl::Span<const T> u) {
    switch (u.size()) {
      case 2:
        return util_hash::Hash(u[0], u[1]);
      case 3:
        return util_hash::Hash(u[0], u[1], u[2]);
      case 4:
        return util_hash::Hash(u[0], u[1], u[2], u[3]);
      case 5:
        return util_hash::Hash(u[0], u[1], u[2], u[3], u[4]);
      case 6:
        return util_hash::Hash(u[0], u[1], u[2], u[3], u[4], u[5]);
      default:
        LOG_IF(FATAL, u.size() != 7) << "unsupported u.size()=" << u.size();
        return util_hash::Hash(u[0], u[1], u[2], u[3], u[4], u[5], u[6]);
    }
  };

  TestPermutationWithHasher(v, hasher0);
  TestPermutationWithHasher(v, hasher1);
}

TEST(Hash, UtilHash_Hash_Permutations) {
  TestPermutation(std::vector<int>{0, 1});
  TestPermutation(std::vector<int>{1, 2, 3});
  TestPermutation(std::vector<int>{0, 5, 10});
  TestPermutation(std::vector<int>{0, 1, 1});
  TestPermutation(std::vector<int>{0, 0, 1, 1});
  TestPermutation(std::vector<int>{1, 2, 3, 4, 5});
  TestPermutation(std::vector<int>{0, 1, 4, 9, 16, 25, 36});
  TestPermutation(std::vector<std::string>{"0", "1"});
  TestPermutation(std::vector<std::string>{"1", "2", "3"});
  TestPermutation(std::vector<std::string>{"0", "5", "10"});
  TestPermutation(std::vector<std::string>{"0", "1", "1"});
  TestPermutation(std::vector<std::string>{"0", "0", "1", "1"});
  TestPermutation(std::vector<std::string>{"1", "2", "3", "4", "5"});
  TestPermutation(
      std::vector<std::string>{"0", "1", "4", "9", "16", "25", "36"});
}

TEST(Hash, UtilHash_Hash_DoesntCopy) { ::util_hash::Hash(NotCopyable()); }

TEST(Hash, CordFunctions) {
  static constexpr absl::string_view str = "123456";
  absl::Cord flat_cord("123456");
  absl::Cord fragmented_cord = absl::MakeFragmentedCord({"123", "456"});

  EXPECT_EQ(Fingerprint(str), Fingerprint(flat_cord));
  EXPECT_EQ(Fingerprint(str), Fingerprint(fragmented_cord));
  EXPECT_EQ(Fingerprint(""), Fingerprint(absl::Cord()));

  EXPECT_EQ(HashTo32(str), HashTo32(flat_cord));
  EXPECT_EQ(HashTo32(str), HashTo32(fragmented_cord));
  EXPECT_EQ(HashTo32(""), HashTo32(absl::Cord()));

  EXPECT_EQ(util_hash::Hash(flat_cord), util_hash::Hash(fragmented_cord));
  EXPECT_EQ(util_hash::Hash(flat_cord, 42),
            util_hash::Hash(fragmented_cord, 42));
  EXPECT_EQ(util_hash::Hash(std::make_tuple(flat_cord, 42)),
            util_hash::Hash(std::make_tuple(fragmented_cord, 42)));
}
