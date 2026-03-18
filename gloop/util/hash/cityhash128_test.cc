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

#include <cstdint>
#include <ios>
#include <string>

#include "absl/base/casts.h"
#include "absl/log/log.h"
#include "absl/numeric/int128.h"
#include "absl/strings/string_view.h"
#include "gloop/util/hash/city.h"
#include "gloop/util/random/acmrandom.h"
#include "gtest/gtest.h"

using util_hash::CityHash128;
using util_hash::CityHash128WithSeed;

static const int kMaxSize = 1000;
static char buf[kMaxSize];

#define E(lo, hi, h)                          \
  do {                                        \
    absl::uint128 hash = (h);                 \
    uint64_t ll = (lo), hh = (hi);            \
    EXPECT_EQ(ll, absl::Uint128Low64(hash));  \
    EXPECT_EQ(hh, absl::Uint128High64(hash)); \
  } while (0)

TEST(CityHash128Test, Unchanging0) {
  ACMRandom r(9);
  for (int i = 0; i < kMaxSize; i++) {
    buf[i] = absl::bit_cast<char>(r.Rand8());
  }

  E(0x6f298d7d228962afull, 0x79c6f6a53c1b8c03ull, CityHash128(buf, 1000));
  E(0x8c07d623ba3bc060ull, 0x7c7573970b7aecadull, CityHash128(buf, 800));
  E(0x6df80aca6ecccad8ull, 0x84f3ec8b264fd8feull, CityHash128(buf, 640));
  E(0x253b32c9e0f5b5d8ull, 0x070a484e76ae6deeull, CityHash128(buf, 512));
  E(0x1574aaf2d8d86145ull, 0x5b71d439dd67524dull, CityHash128(buf, 409));
  E(0x4e99bfb3b72c86c0ull, 0x7aaf8194e256fa37ull, CityHash128(buf, 327));
  E(0x3d7f7eb30b0fb6bbull, 0xf00b831722097f37ull, CityHash128(buf, 261));
  E(0xeeaa009af4e35969ull, 0x25701d1fd052e4aaull, CityHash128(buf, 208));
  E(0x287b6bd11e2e5060ull, 0x15076d342093f883ull, CityHash128(buf, 166));
  E(0x98b4946252825086ull, 0x9a5f2b91a4ca0485ull, CityHash128(buf, 132));
  E(0xab16a4b9f728b2ffull, 0x277840ff90b5d4feull, CityHash128(buf, 105));
  E(0x58d9054df25db4c5ull, 0x1d6abc7580e6c895ull, CityHash128(buf, 84));
  E(0xe53369728b7d7d69ull, 0xd6dd6f3cb82555c3ull, CityHash128(buf, 67));
  E(0xad0d331520da1144ull, 0x1aad65d82930e550ull, CityHash128(buf, 53));
  E(0xf75c13a96d3cda22ull, 0x8cf35d95c76636efull, CityHash128(buf, 42));
  E(0x527d378e50bcb906ull, 0x94fbf5b0f30c46d8ull, CityHash128(buf, 33));
  E(0xee09029d9f3e2201ull, 0x366b539073250081ull, CityHash128(buf, 26));
  E(0x9e3bbaf9568cc46aull, 0x06936a70e6e90a5cull, CityHash128(buf, 20));
  E(0x2d251b77e45397acull, 0x056ab8041ee2c4d3ull, CityHash128(buf, 16));
  E(0xc4b76142983f0d1full, 0xe4f78a75f40163c4ull, CityHash128(buf, 12));
  E(0x2dea708ef8311e3full, 0x98194fd4c0d4cabdull, CityHash128(buf, 9));
  E(0xd523d7bcc888f838ull, 0x027536e83208bc3aull, CityHash128(buf, 7));
  E(0x3fa722373a0a9fccull, 0x291f4ea194a8e1feull, CityHash128(buf, 5));
  E(0xb50e02ddbd14790eull, 0x5ace1fd360147cabull, CityHash128(buf, 4));
  E(0x1e8e0da87d5b7467ull, 0xe410491d7cd70040ull, CityHash128(buf, 3));
  E(0xe70394c3e2d323a3ull, 0x7bfc3ac85edd7a6full, CityHash128(buf, 2));
  E(0xcd47923f21b6e88dull, 0x4c410d5fa38fbf69ull, CityHash128(buf, 1));
  E(0x70e3427355f5fa2eull, 0xebea917381a3f828ull, CityHash128(buf, 0));

  uint64_t tmp;
#define H(len)         \
  ((tmp = r.Rand64()), \
   CityHash128WithSeed(buf, (len), absl::MakeUint128(tmp, r.Rand64())))

  E(0x05861822c674be4aull, 0x64d0604cb1bc8f37ull, H(1000));
  E(0xf8aadf8b4885a00bull, 0x3eeb21f041835f6eull, H(800));
  E(0x15a1fe42ce26cacbull, 0xc0c29e43f5a5eff2ull, H(640));
  E(0xac0ed166ae390ba4ull, 0x6fb98b3fbe54c31aull, H(512));
  E(0xad7b5ee61bc8ca6dull, 0xdeb392c15c443f7dull, H(409));
  E(0xf635c5f83e115d3bull, 0x7475b957662792dbull, H(327));
  E(0x46b98f8836b27d51ull, 0x0b6020d268349b90bll, H(261));
  E(0x01b95d43123e243aull, 0x008e03d78aa1d8e1ull, H(208));
  E(0x8b1214b34a29e082ull, 0x84c5341f03410498ull, H(166));
  E(0x5a828a2947bab3adull, 0x79cd8983b1087d2cull, H(132));
  E(0xe01188d853a2e446ull, 0x62a48d2393629e86ull, H(105));
  E(0xbf0a085da6d9509cull, 0x8affd6aa011798e6ull, H(84));
  E(0x6e44ba6d8e0178acull, 0x9bde93cde40f91bcull, H(67));
  E(0x5f69fe137757b0e9ull, 0x3ab64cf647c1600cull, H(53));
  E(0x6212cc933eb49137ull, 0x3931a989881e7b53ull, H(42));
  E(0xdea83ce59f7ae773ull, 0x835175f2c2b17202ull, H(33));
  E(0xa6c4340125ec39a1ull, 0x8a55e9476a3c58caull, H(26));
  E(0x620a778ac7d5ce13ull, 0xd9c366275ecc944aull, H(20));
  E(0x65497f02b4ce093full, 0x3198e2e10eadf77cull, H(16));
  E(0xed6d2fa4ebd93002ull, 0x70d912a3ee4d891aull, H(12));
  E(0xc04e0933a1bc2823ull, 0x2ae12682e6e66cabull, H(9));
  E(0x01f1cfc7bb5d140b6ll, 0x0b949b5c2c71386dull, H(7));
  E(0x734688a24f58f898ull, 0xcc1bc54bf4717fc1ull, H(5));
  E(0xa15c8f14743341bbull, 0x584220cbfa39e0ddull, H(4));
  E(0x3b84a606c340d73aull, 0x84b4bb8cb5a78f70ull, H(3));
  E(0x19f4f2605da6a939ull, 0x7096bf89b2e00f75ull, H(2));
  E(0x73e89955fe8cca4aull, 0x979274a5c101eb26ull, H(1));
  E(0x4d278cc18bf503bcull, 0x752e232e509d95ebull, H(0));
}

// Helper for Unchanging1.  Replace *h with a hash of itself and return a
// char that is also a hash of *h.  Neither hash needs to be particularly good.
static char Remix(absl::uint128* h) {
  uint64_t lo = absl::Uint128Low64(*h);
  uint64_t hi = absl::Uint128High64(*h);
  lo += hi;
  hi += lo;
  lo ^= lo >> 41;
  *h = absl::MakeUint128(hi, lo);
  return 'a' + ((hi & 0xfffff) % 26);
}

// This is more thorough, but if something is wrong the output will be even
// less illuminating, because it just checks one uint128 at the end.
TEST(CityHash128Test, Unchanging1) {
  const int kIters = 800;
  std::string s;
  absl::uint128 h(0);
  for (int i = 0; i < kIters; i++) {
    h ^= CityHash128(absl::string_view(s.data(), s.size()));
    s.push_back(Remix(&h));
    h = CityHash128WithSeed(absl::string_view(s.data(), i), h);
    s.push_back(Remix(&h));
    h = CityHash128WithSeed(absl::string_view(s.data(), s.size()), h);
    s.push_back(Remix(&h));
    h ^= CityHash128(absl::string_view(s.data(), i));
    s.push_back(Remix(&h));
    uint32_t x0 = static_cast<uint8_t>(s[s.size() - 1]);
    uint32_t x1 = static_cast<uint8_t>(s[s.size() - 2]);
    uint32_t x2 = static_cast<uint8_t>(s[s.size() - 3]);
    uint32_t x3 = static_cast<uint8_t>(s[s.size() - 4]);
    s[((x0 << 16) + (x1 << 8) + x2) % s.size()] ^= x3;
    s[((x1 << 16) + (x2 << 8) + x3) % s.size()] ^= i % 256;
    VLOG(2) << "At end of i=" << i << " iteration, h=" << std::hex
            << absl::Uint128High64(h) << absl::Uint128Low64(h);
  }
  E(0x5c1052b9185fdcb3ull, 0x3fcafa3d5e22a540ull, h);
}
