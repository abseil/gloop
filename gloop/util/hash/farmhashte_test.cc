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

#include "gloop/util/hash/farmhashte.h"

#include <stddef.h>

#include <cstdint>
#include <ios>
#include <string>

#include "absl/log/log.h"
// Include farmhash.h just so that can_use_sse41 will be set if we have SSE4.1.
#include "gloop/util/hash/farmhash.h"

// The current implementation of farmhashte requires SSE4.1.  Do not try to test
// unless we have that.
#if can_use_sse41 || can_use_neon

#include "absl/base/casts.h"
#include "gloop/util/random/acmrandom.h"
#include "gtest/gtest.h"

#define E64(expected0, expected1, expected2, n)                               \
  do {                                                                        \
    const size_t len = (n);                                                   \
    const uint64_t seed0{0x5c63b66443990bec};                                 \
    const uint64_t seed1{0xa69f3400e6c98357u};                                \
    const uint64_t seed2{0xdd731dfee500aa3cu};                                \
    EXPECT_EQ(expected0, farmhashte::Fingerprint64(buf, len));                \
    EXPECT_EQ(expected1, farmhashte::Fingerprint64WithSeed(buf, len, seed0)); \
    EXPECT_EQ(expected2,                                                      \
              farmhashte::Fingerprint64WithSeeds(buf, len, seed1, seed2));    \
  } while (0)

TEST(FarmHashteFingerprint64Test, Unchanging0) {
  static const int kMaxSize = 1000;
  static char buf[kMaxSize];
  ACMRandom r(10);
  for (int i = 0; i < kMaxSize; i++) {
    buf[i] = absl::bit_cast<char>(r.Rand8());
  }
  E64(uint64_t{0xdae207de9b87d359}, uint64_t{0xa19443d8784e8249},
      uint64_t{0x654167918b40a438}, 1000);
  E64(uint64_t{0x476564b168db5e91}, uint64_t{0x2cd2a3ee4f8bce00},
      uint64_t{0x51a75b1c38d50a05}, 800);
  E64(uint64_t{0x35bef4056e0ab51d}, uint64_t{0xa24672152cf6e58f},
      uint64_t{0xad61a0bef285710f}, 640);
  E64(uint64_t{0xdeb6e34ed8462dd}, uint64_t{0x1e93f344c3886348},
      uint64_t{0x575bda3660bf33fe}, 512);
  E64(uint64_t{0xa68c6a3f6e8ff8aa}, uint64_t{0x6bba9a64fee35f7d},
      uint64_t{0x35e4b96a2c28d510}, 409);
  E64(uint64_t{0xc960ed75d3134ef9}, uint64_t{0x49aa5d0a74675fc5},
      uint64_t{0xb12b1190fdb61188}, 327);
  E64(uint64_t{0xc8535cfbbade6630}, uint64_t{0x66bcfc74e564ca86},
      uint64_t{0x7d301a6bee50a4d2}, 261);
  E64(uint64_t{0x1fa618c494927ff6}, uint64_t{0x35498b5d900f2d16},
      uint64_t{0xf18eb2facd19b6bd}, 208);
  E64(uint64_t{0x5cc24f9da7ec05cb}, uint64_t{0xc34a6d520b0e6e3f},
      uint64_t{0xbfc3622065581281}, 166);
  E64(uint64_t{0x61ff1b56642fccfd}, uint64_t{0x7abc321478c5ae6a},
      uint64_t{0xec80a0c13746c675}, 132);
  E64(uint64_t{0x358e7ec85c0cab0b}, uint64_t{0x390d661aadcccf9d},
      uint64_t{0x18cc5136b977977e}, 105);
  E64(uint64_t{0x3985a7b4d9f93dbc}, uint64_t{0xecacca329f842857},
      uint64_t{0xd7c72f84a5bba4b0}, 84);
  E64(uint64_t{0x19c33ce1ede00c1b}, uint64_t{0x8e6a607658dd4844},
      uint64_t{0xb20a6072c4fe4eb9}, 67);
  E64(uint64_t{0xeeb75a64e69f3b5c}, uint64_t{0xed9cef47e88b78ed},
      uint64_t{0xa529a233e2049df3}, 53);
  E64(uint64_t{0xf98533798f417353}, uint64_t{0xfdb555057b652ddb},
      uint64_t{0xe64c71a095d233c1}, 42);
  E64(uint64_t{0xf0e0971bc27777dd}, uint64_t{0xafa0f657a766fe27},
      uint64_t{0x66fa9010e65816f3}, 33);
  E64(uint64_t{0x3dfa033c954b4941}, uint64_t{0x8d1c6474013a669a},
      uint64_t{0xb7558f90c73235ad}, 26);
  E64(uint64_t{0x24c4630c0d80e821}, uint64_t{0x6f49f7317d4a99a5},
      uint64_t{0xed86c5da29c5824}, 20);
  E64(uint64_t{0xddd59210f940d91c}, uint64_t{0xdbb8b45ffc590d98},
      uint64_t{0x33565b0c254ca77f}, 16);
  E64(uint64_t{0x9366281b936e3ee2}, uint64_t{0x8074ab2db631d16e},
      uint64_t{0x5671236bebb7690}, 12);
  E64(uint64_t{0x26a1240b6240f27}, uint64_t{0x8c2a397b456b4a70},
      uint64_t{0xa81815f0103eb197}, 9);
  E64(uint64_t{0x309f9a56b2706c8e}, uint64_t{0x85b98db959f56e93},
      uint64_t{0xb349434dc5817a22}, 7);
  E64(uint64_t{0x6d128bc4607f612f}, uint64_t{0x3fe0c5acf9ff8097},
      uint64_t{0xc0e305cb96e556ec}, 5);
  E64(uint64_t{0xc7be6bb4e214b0f0}, uint64_t{0xd7bdd09d6b38b22c},
      uint64_t{0x33edb11cd7dc2a31}, 4);
  E64(uint64_t{0x1b45ca08d6f1cee0}, uint64_t{0x5fd270c14de42771},
      uint64_t{0xd4f1cf5198530925}, 3);
  E64(uint64_t{0x318ef19ba3459f19}, uint64_t{0x7d8ea6446885fa95},
      uint64_t{0xb94247fc1c71b03b}, 2);
  E64(uint64_t{0x187c54cd1ba9dbc8}, uint64_t{0xc872215e732c9c7},
      uint64_t{0x70ff824f491ffe4c}, 1);
  E64(uint64_t{0xbf2f0332578dc3dc}, uint64_t{0x8557fab59bdcd75},
      uint64_t{0xccf324f93c693a9b}, 0);
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
TEST(FarmHashteFingerprint64Test, Unchanging1) {
  const int kIters = 800;
  std::string s;
  uint64_t h = 0;
  for (int i = 0; i < kIters; i++) {
    h ^= farmhashte::Fingerprint64(s.data(), i);
    s.push_back(Remix(&h));
    h ^= farmhashte::Fingerprint64(s.data(), i * i % s.size());
    s.push_back(Remix(&h));
    h ^= farmhashte::Fingerprint64WithSeed(s.data(), i * i * i % s.size(), i);
    s.push_back(Remix(&h));
    h ^= farmhashte::Fingerprint64WithSeeds(s.data(), s.size(), h, h * 999);
    s.push_back(Remix(&h));
    uint32_t x0 = static_cast<uint8_t>(s[s.size() - 1]);
    uint32_t x1 = static_cast<uint8_t>(s[s.size() - 2]);
    uint32_t x2 = static_cast<uint8_t>(s[s.size() - 3]);
    uint32_t x3 = static_cast<uint8_t>(s[s.size() / 2]);
    s[((x0 << 16) + (x1 << 8) + x2) % s.size()] ^= x3;
    s[((x1 << 16) + (x2 << 8) + x3) % s.size()] ^= i % 256;
    VLOG(2) << "At end of i=" << i << " iteration, h=" << std::hex << h;
  }
  EXPECT_EQ(uint64_t{0x6b5c772d842e8c11}, h);
}
#else
#endif  // can_use_sse41 || can_use_neon
