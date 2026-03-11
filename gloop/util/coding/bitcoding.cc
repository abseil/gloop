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

#include "gloop/util/coding/bitcoding.h"

#include <string.h>

#include <cstdint>

#include "absl/log/check.h"
#include "gloop/util/bits/bits.h"

const int BitEncoder::kMaxGammaBits;
const int BitEncoder::kMaxVarIntBits;
const int BitEncoder::kMaxVarInt64Bits;

// mask_[k] has the low k bits set to 1, other bits to 0.
// Uses for extracting the low "k" bits from a word.
// (It is hard to do this with just a shift because
// shift(kBitsPerUwordT) is not defined.)
const uint64_t BitEncoder::mask_[64 + 1] = {
    0x00000000u,          0x00000001u,          0x00000003u,
    0x00000007u,          0x0000000fu,          0x0000001fu,
    0x0000003fu,          0x0000007fu,          0x000000ffu,
    0x000001ffu,          0x000003ffu,          0x000007ffu,
    0x00000fffu,          0x00001fffu,          0x00003fffu,
    0x00007fffu,          0x0000ffffu,          0x0001ffffu,
    0x0003ffffu,          0x0007ffffu,          0x000fffffu,
    0x001fffffu,          0x003fffffu,          0x007fffffu,
    0x00ffffffu,          0x01ffffffu,          0x03ffffffu,
    0x07ffffffu,          0x0fffffffu,          0x1fffffffu,
    0x3fffffffu,          0x7fffffffu,          0xffffffffu,
    0x00000001fffffffful, 0x00000003fffffffful, 0x00000007fffffffful,
    0x0000000ffffffffful, 0x0000001ffffffffful, 0x0000003ffffffffful,
    0x0000007ffffffffful, 0x000000fffffffffful, 0x000001fffffffffful,
    0x000003fffffffffful, 0x000007fffffffffful, 0x00000ffffffffffful,
    0x00001ffffffffffful, 0x00003ffffffffffful, 0x00007ffffffffffful,
    0x0000fffffffffffful, 0x0001fffffffffffful, 0x0003fffffffffffful,
    0x0007fffffffffffful, 0x000ffffffffffffful, 0x001ffffffffffffful,
    0x003ffffffffffffful, 0x007ffffffffffffful, 0x00fffffffffffffful,
    0x01fffffffffffffful, 0x03fffffffffffffful, 0x07fffffffffffffful,
    0x0ffffffffffffffful, 0x1ffffffffffffffful, 0x3ffffffffffffffful,
    0x7ffffffffffffffful, 0xfffffffffffffffful};

unsigned char BitDecoder::unary_decode_table[256] = {
    /*  0 */ 1, 2, 1, 3, 1, 2, 1, 4, 1, 2, 1, 3, 1, 2, 1, 5,
    /* 10 */ 1, 2, 1, 3, 1, 2, 1, 4, 1, 2, 1, 3, 1, 2, 1, 6,
    /* 20 */ 1, 2, 1, 3, 1, 2, 1, 4, 1, 2, 1, 3, 1, 2, 1, 5,
    /* 30 */ 1, 2, 1, 3, 1, 2, 1, 4, 1, 2, 1, 3, 1, 2, 1, 7,
    /* 40 */ 1, 2, 1, 3, 1, 2, 1, 4, 1, 2, 1, 3, 1, 2, 1, 5,
    /* 50 */ 1, 2, 1, 3, 1, 2, 1, 4, 1, 2, 1, 3, 1, 2, 1, 6,
    /* 60 */ 1, 2, 1, 3, 1, 2, 1, 4, 1, 2, 1, 3, 1, 2, 1, 5,
    /* 70 */ 1, 2, 1, 3, 1, 2, 1, 4, 1, 2, 1, 3, 1, 2, 1, 8,
    /* 80 */ 1, 2, 1, 3, 1, 2, 1, 4, 1, 2, 1, 3, 1, 2, 1, 5,
    /* 90 */ 1, 2, 1, 3, 1, 2, 1, 4, 1, 2, 1, 3, 1, 2, 1, 6,
    /* a0 */ 1, 2, 1, 3, 1, 2, 1, 4, 1, 2, 1, 3, 1, 2, 1, 5,
    /* b0 */ 1, 2, 1, 3, 1, 2, 1, 4, 1, 2, 1, 3, 1, 2, 1, 7,
    /* c0 */ 1, 2, 1, 3, 1, 2, 1, 4, 1, 2, 1, 3, 1, 2, 1, 5,
    /* d0 */ 1, 2, 1, 3, 1, 2, 1, 4, 1, 2, 1, 3, 1, 2, 1, 6,
    /* e0 */ 1, 2, 1, 3, 1, 2, 1, 4, 1, 2, 1, 3, 1, 2, 1, 5,
    /* f0 */ 1, 2, 1, 3, 1, 2, 1, 4, 1, 2, 1, 3, 1, 2, 1, 9};

uint32_t BitEncoder::gamma_[BitEncoder::kGammaTableLength] = {};

// Map from x to floor(log2(x))
const unsigned char BitEncoder::log2_table_[256] = {
    0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};

// For initializing various tables
class BitCoderInitializer {
 public:
  BitCoderInitializer() { BitEncoder::Initialize(); }
};
static BitCoderInitializer bcinit;

void BitEncoder::Initialize() {
  // gamma_
  for (int i = 1; i < kGammaTableLength; i++) {
    char buf[8] = {0};
    BitEncoder be(buf, sizeof(buf));
    be.InternalPutGamma(i);
    int bits = be.Bits();
    be.Flush(0);  // Flush stuff out into byte buffer
    BitDecoder bd(buf, sizeof(buf));
    uint32_t value = 0;
    bd.GetBits(bits, &value);
#if 0
    printf("%4d: ", i);
    for (int b = 0; b < bits; b++) {
      printf("%d", (value>>b) & 1);
      if (((b + 1) % 4) == 0) {
        printf(" ");
      }
    }
    printf("\n");
    fflush(stdout);
#endif
    CHECK_EQ((value & 0xffffff), value);
    gamma_[i] = (bits << 24) | value;
  }

  // Check that gamma encoding works
  char buf[kGammaTableLength * 8];
  // Clear buf so that valgrind doesn't warn about reading uninitialized memory.
  memset(buf, 0, sizeof(buf));
  BitEncoder be(buf, sizeof(buf));
  for (int i = 1; i < kGammaTableLength; i++) {
    be.PutGamma(i);
  }
  be.Flush(0);

  BitDecoder bd(buf, sizeof(buf));
  for (uint32_t i = 1; i < kGammaTableLength; i++) {
    uint32_t v = 0;
    CHECK(bd.GetGamma(&v));
    CHECK_EQ(v, i);
  }
}

uint32_t BitEncoder::ReverseBits(int n, uint32_t x) {
  // Special case 0 since ">> 32" is not defined.
  return (n == 0) ? 0 : (Bits::ReverseBits32(x) >> (32 - n));
}

const char* BitEncoder::UnparseLSBFirst(char* str, uint32_t x, int k) {
  for (int i = 0; i < k; i++) {
    str[i] = (x & (1 << i)) ? '1' : '0';
  }
  str[k] = '\0';
  return str;
}

void BitDecoder::SkipBitsNoInline(int64_t k) { return SkipBits(k); }

bool BitDecoder::GetBitsNoInline(int nbits, uint32_t* x) {
  return GetBits(nbits, x);
}

bool BitDecoder::GetBits64NoInline(int nbits, uint64_t* x) {
  return GetBits64(nbits, x);
}
