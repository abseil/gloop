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

#include "gloop/util/hash/crc.h"

#include <stdio.h>
#include <stdlib.h>

#include <cstdint>
#include <ios>

#include "absl/base/macros.h"
#include "absl/container/fixed_array.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/flags.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "benchmark/benchmark.h"

ABSL_FLAG(bool, print_output, false, "print CRC debug information");

// Extend a CRC by 8 bits of zero using the polynomial
// poly_lo,poly_hi using long division.
void ExtendBy8ZeroBits(uint64_t poly_lo, uint64_t poly_hi, uint64_t* lo,
                       uint64_t* hi) {
  for (int i = 0; i != 8; i++) {
    bool carry = ((*lo & 1) != 0);  // carry out of word
    *lo >>= 1;
    *lo |= (*hi & 1) << 63;  // carry between halfwords
    *hi >>= 1;
    if (carry) {  // xor polynomial if carry out
      *lo ^= poly_lo;
      *hi ^= poly_hi;
    }
  }
}

// Same as CRC::Extend, but one byte at a time, and done by long division
// using the polynomial poly_lo,poly_hi.
void MyExtendBasic(uint64_t poly_lo, uint64_t poly_hi, uint64_t* lo,
                   uint64_t* hi, const char* str, size_t len) {
  for (int i = 0; i != len; i++) {
    *lo ^= (str[i] & 0xff);
    ExtendBy8ZeroBits(poly_lo, poly_hi, lo, hi);
  }
}

// Same as CRC::Extend, but one byte at a time.
void MyExtend(CRC* crc, uint64_t* lo, uint64_t* hi, const char* str,
              size_t len) {
  for (int i = 0; i != len; i++) {
    crc->Extend(lo, hi, &str[i], 1);
  }
}

#define STRING_AND_LEN(_s) {_s, sizeof(_s) - 1}

// For a given crc, for roll length r, for all test strings,
// we check that Extend() and ExtendByZeroes() get the same answer
// as doing the same thing one byte at a time.
// We also check that the rolling CRC gets the same answer as simply
// calculating the CRC on each successive string.
static void TestCRC(CRC* crc, int r) {
  static const struct {  // a list of test strings to try
    const char* str;
    int len;
  } strings[] = {STRING_AND_LEN(""),
                 STRING_AND_LEN("\0"),
                 STRING_AND_LEN("\1"),
                 STRING_AND_LEN("\0\1"),
                 STRING_AND_LEN("hello"),
                 STRING_AND_LEN("the quick brown fox jumps over the lazy dog"),
                 STRING_AND_LEN("To be, or not to be: that is the question:\n"
                                "Whether 'tis nobler in the mind to suffer\n"
                                "The slings and arrows of outrageous fortune,\n"
                                "Or to take arms against a sea of troubles,\n"
                                "And by opposing end them?\n")};

  // Try all test strings
  for (int j = 0; j != sizeof(strings) / sizeof(strings[0]); j++) {
    uint64_t lo;
    uint64_t hi;

    if (absl::GetFlag(FLAGS_print_output)) {
      printf("string %s %d\n", strings[j].str, strings[j].len);
    }
    crc->Empty(&lo, &hi);
    if (absl::GetFlag(FLAGS_print_output)) {
      absl::PrintF("poly   %016x%016x\n", hi, lo);
    }
    crc->Extend(&lo, &hi, strings[j].str, strings[j].len);
    if (absl::GetFlag(FLAGS_print_output)) {
      absl::PrintF("Extend %016x%016x\n", hi, lo);
    }

    uint64_t mlo;
    uint64_t mhi;
    crc->Empty(&mlo, &mhi);
    MyExtend(crc, &mlo, &mhi, strings[j].str, strings[j].len);
    if (absl::GetFlag(FLAGS_print_output)) {
      absl::PrintF("bybyte %016x%016x\n", mhi, mlo);
    }
    CHECK_EQ(mlo, lo);
    CHECK_EQ(mhi, hi);

    uint64_t blo;
    uint64_t bhi;
    crc->Empty(&blo, &bhi);
    MyExtendBasic(blo, bhi, &blo, &bhi, strings[j].str, strings[j].len);
    if (absl::GetFlag(FLAGS_print_output)) {
      absl::PrintF("by_bit %016x%016x\n", bhi, blo);
    }
    CHECK_EQ(blo, lo);
    CHECK_EQ(bhi, hi);

    int z = random() & 0xffff;
    crc->ExtendByZeroes(&lo, &hi, z);
    for (int k = 0; k != z; k++) {
      MyExtend(crc, &mlo, &mhi, "\0", 1);
    }
    CHECK_EQ(mlo, lo);
    CHECK_EQ(mhi, hi);

    if (strings[j].len > r) {
      crc->Empty(&lo, &hi);
      crc->Extend(&lo, &hi, strings[j].str, r);
      crc->Empty(&mlo, &mhi);
      MyExtend(crc, &mlo, &mhi, strings[j].str, r);
      CHECK_EQ(mlo, lo);
      CHECK_EQ(mhi, hi);
      for (int k = r; k != strings[j].len; k++) {
        crc->Roll(&lo, &hi, strings[j].str[k - r], strings[j].str[k]);
        crc->Empty(&mlo, &mhi);
        MyExtend(crc, &mlo, &mhi, &strings[j].str[k - r + 1], r);
        CHECK_EQ(mlo, lo);
        CHECK_EQ(mhi, hi);
      }
    }
    if (absl::GetFlag(FLAGS_print_output)) {
      printf("\n");
    }
  }
}

static void TestLargeExtendByZeroes() {
  uint64_t hi = 0x5766cc56f872daafLL;
  uint64_t lo = 0x42a470899c3c1cabLL;
  uint64_t size = 115455819776LL;
  uint64_t zero = 0;
  CRC* crc_gen = CRC::Default(128, 0);
  crc_gen->ExtendByZeroes(&hi, &lo, size - zero);
  CHECK_EQ(lo, uint64_t{0xaf9e0b922a11b8bbu});
  CHECK_EQ(hi, uint64_t{0xf2b9427e289f3091u});
}

// Return a 64-bit random number
static uint64_t Random64() {
  uint64_t r = random();
  r <<= 25;
  r ^= random();
  r <<= 25;
  return r ^ random();
}

// These are precomputed patterns for Scramble()
// used to ensure that the function never changes.
// element i represents an (i+8)-bit value.
// If the pre-scramble value is (pre_127_96,pre_95_64,pre_63_32,pre_31_0)
// then the post-scramble value is
// (post_127_96,post_95_64,post_63_32,post_31_0)
static const struct {
  uint32_t pre_127_96;
  uint32_t pre_95_64;
  uint32_t pre_63_32;
  uint32_t pre_31_0;
  uint32_t post_127_96;
  uint32_t post_95_64;
  uint32_t post_63_32;
  uint32_t post_31_0;
} scramble_test_patterns[] = {
    {0x0U, 0x0U, 0x0U, 0xffU, 0x0U, 0x0U, 0x0U, 0xd0U},
    {0x0U, 0x0U, 0x0U, 0x16fU, 0x0U, 0x0U, 0x0U, 0xacU},
    {0x0U, 0x0U, 0x0U, 0x3f6U, 0x0U, 0x0U, 0x0U, 0x110U},
    {0x0U, 0x0U, 0x0U, 0x68U, 0x0U, 0x0U, 0x0U, 0x72U},
    {0x0U, 0x0U, 0x0U, 0x8deU, 0x0U, 0x0U, 0x0U, 0xf39U},
    {0x0U, 0x0U, 0x0U, 0x1bdbU, 0x0U, 0x0U, 0x0U, 0x1dbfU},
    {0x0U, 0x0U, 0x0U, 0x55U, 0x0U, 0x0U, 0x0U, 0x1c24U},
    {0x0U, 0x0U, 0x0U, 0x206aU, 0x0U, 0x0U, 0x0U, 0x4284U},
    {0x0U, 0x0U, 0x0U, 0xe1cdU, 0x0U, 0x0U, 0x0U, 0xf462U},
    {0x0U, 0x0U, 0x0U, 0xad00U, 0x0U, 0x0U, 0x0U, 0x11bc8U},
    {0x0U, 0x0U, 0x0U, 0x39eadU, 0x0U, 0x0U, 0x0U, 0x2c8a0U},
    {0x0U, 0x0U, 0x0U, 0x3db21U, 0x0U, 0x0U, 0x0U, 0x678afU},
    {0x0U, 0x0U, 0x0U, 0x7f66dU, 0x0U, 0x0U, 0x0U, 0x511dbU},
    {0x0U, 0x0U, 0x0U, 0x18683eU, 0x0U, 0x0U, 0x0U, 0x1165e9U},
    {0x0U, 0x0U, 0x0U, 0x1e10bcU, 0x0U, 0x0U, 0x0U, 0x135d4fU},
    {0x0U, 0x0U, 0x0U, 0x2a61c5U, 0x0U, 0x0U, 0x0U, 0x2f0214U},
    {0x0U, 0x0U, 0x0U, 0x4e1c31U, 0x0U, 0x0U, 0x0U, 0x26628U},
    {0x0U, 0x0U, 0x0U, 0xaa5596U, 0x0U, 0x0U, 0x0U, 0x19b1909U},
    {0x0U, 0x0U, 0x0U, 0x6f2142U, 0x0U, 0x0U, 0x0U, 0x55db98U},
    {0x0U, 0x0U, 0x0U, 0x2faa870U, 0x0U, 0x0U, 0x0U, 0x19165c6U},
    {0x0U, 0x0U, 0x0U, 0xfe8e714U, 0x0U, 0x0U, 0x0U, 0x965fcbfU},
    {0x0U, 0x0U, 0x0U, 0x147f38deU, 0x0U, 0x0U, 0x0U, 0x73e45ecU},
    {0x0U, 0x0U, 0x0U, 0x2a9e1fa5U, 0x0U, 0x0U, 0x0U, 0xf01915U},
    {0x0U, 0x0U, 0x0U, 0x4dfbe558U, 0x0U, 0x0U, 0x0U, 0x64b9fc72U},
    {0x0U, 0x0U, 0x0U, 0xca9eca0cU, 0x0U, 0x0U, 0x0U, 0xd713fc8aU},
    {0x0U, 0x0U, 0x1U, 0xa6fca739U, 0x0U, 0x0U, 0x1U, 0x8b54eab9U},
    {0x0U, 0x0U, 0x1U, 0x53bf3918U, 0x0U, 0x0U, 0x2U, 0x1d33608dU},
    {0x0U, 0x0U, 0x4U, 0x7de6cd37U, 0x0U, 0x0U, 0x3U, 0x62a52b17U},
    {0x0U, 0x0U, 0x4U, 0xb90f9904U, 0x0U, 0x0U, 0xcU, 0xfa3e9cf0U},
    {0x0U, 0x0U, 0xcU, 0x87ccf7b1U, 0x0U, 0x0U, 0xfU, 0x6f3396c8U},
    {0x0U, 0x0U, 0xfU, 0xdf50d91eU, 0x0U, 0x0U, 0x1eU, 0xf4e400dcU},
    {0x0U, 0x0U, 0x4cU, 0xabac4550U, 0x0U, 0x0U, 0x19U, 0x4b5fcda2U},
    {0x0U, 0x0U, 0x61U, 0xe5d56101U, 0x0U, 0x0U, 0x62U, 0x28e490a2U},
    {0x0U, 0x0U, 0x22U, 0x57086374U, 0x0U, 0x0U, 0x1f4U, 0x78f2942bU},
    {0x0U, 0x0U, 0x321U, 0x4bcecc5eU, 0x0U, 0x0U, 0x5bU, 0x79d45e9U},
    {0x0U, 0x0U, 0x31fU, 0x26d6009U, 0x0U, 0x0U, 0x488U, 0x848d3cc3U},
    {0x0U, 0x0U, 0x1b2U, 0xa6b8aa3aU, 0x0U, 0x0U, 0x5f1U, 0xcaa3c5aaU},
    {0x0U, 0x0U, 0x18bcU, 0x656f6bf5U, 0x0U, 0x0U, 0x1994U, 0x431d927U},
    {0x0U, 0x0U, 0x2daU, 0x8d406d1dU, 0x0U, 0x0U, 0x2dd4U, 0x4e230abbU},
    {0x0U, 0x0U, 0x5133U, 0x98a35c47U, 0x0U, 0x0U, 0xd20U, 0x313163c7U},
    {0x0U, 0x0U, 0xa821U, 0x69ee4e41U, 0x0U, 0x0U, 0x3299U, 0x2e0428ccU},
    {0x0U, 0x0U, 0x397cU, 0xba501546U, 0x0U, 0x0U, 0xc6f9U, 0x614cd674U},
    {0x0U, 0x0U, 0x259c8U, 0x52d1ccdU, 0x0U, 0x0U, 0x3a400U, 0xe82e7e0cU},
    {0x0U, 0x0U, 0x31e1bU, 0xba78b3c8U, 0x0U, 0x0U, 0x1df2fU, 0xc65f92faU},
    {0x0U, 0x0U, 0x58e19U, 0x3fa18640U, 0x0U, 0x0U, 0xc30d4U, 0xb67dc92dU},
    {0x0U, 0x0U, 0x32cb2U, 0x4d5302a5U, 0x0U, 0x0U, 0xf279bU, 0x2319c4fU},
    {0x0U, 0x0U, 0x135316U, 0xb251fdc6U, 0x0U, 0x0U, 0x32387U, 0x861b346eU},
    {0x0U, 0x0U, 0x74319bU, 0x33041583U, 0x0U, 0x0U, 0xbd7ccU, 0xf4291cb6U},
    {0x0U, 0x0U, 0x32b039U, 0x92f9b35cU, 0x0U, 0x0U, 0xb84bbU, 0xb808834eU},
    {0x0U, 0x0U, 0xc7a03fU, 0x9c408914U, 0x0U, 0x0U, 0xab76d2U, 0xf4b0037eU},
    {0x0U, 0x0U, 0x39d4accU, 0x13da0b59U, 0x0U, 0x0U, 0x250ef7U, 0x45aeabf1U},
    {0x0U, 0x0U, 0x49d4bbaU, 0x950f5fd9U, 0x0U, 0x0U, 0x70c87eU, 0x99aeafabU},
    {0x0U, 0x0U, 0xb29e7a0U, 0xce46fe6eU, 0x0U, 0x0U, 0xf97bc51U, 0x29f08fa1U},
    {0x0U, 0x0U, 0x10e83e04U, 0x4ff12dfcU, 0x0U, 0x0U, 0x1f9a0485U,
     0xd56d3c68U},
    {0x0U, 0x0U, 0x300315dcU, 0x72a8d742U, 0x0U, 0x0U, 0x2847eed7U,
     0x69d1760cU},
    {0x0U, 0x0U, 0x77fd3fb3U, 0x4ac57c6fU, 0x0U, 0x0U, 0x3c9e3045U,
     0x71cb9fe3U},
    {0x0U, 0x0U, 0xda2827e6U, 0x4f5252bfU, 0x0U, 0x0U, 0x3ee49b6dU,
     0x69fb440bU},
    {0x0U, 0x0U, 0xe9430a98U, 0x50abc7e1U, 0x0U, 0x0U, 0x7f22abfcU,
     0xf188b564U},
    {0x0U, 0x3U, 0x853e8091U, 0xd1d79f39U, 0x0U, 0x2U, 0x4e8354U, 0x9fc33830U},
    {0x0U, 0x0U, 0x65e4669eU, 0x23c4258aU, 0x0U, 0x4U, 0xa476134aU,
     0xd7ecb1b3U},
    {0x0U, 0x2U, 0x5ac6a81aU, 0x32fc9708U, 0x0U, 0x4U, 0xc2e6f647U,
     0xaa92a109U},
    {0x0U, 0x7U, 0xb036e966U, 0x36f39bafU, 0x0U, 0x19U, 0x95a9ff2aU,
     0x5540a932U},
    {0x0U, 0x8U, 0xe48bf8caU, 0x7f96d5d0U, 0x0U, 0x2aU, 0xb836e7adU,
     0x3de5a58fU},
    {0x0U, 0x40U, 0x99dacac8U, 0xcb9611b3U, 0x0U, 0x47U, 0xd067ae74U,
     0xb93a92afU},
    {0x0U, 0xc3U, 0xfde3f083U, 0x983d54f4U, 0x0U, 0x9eU, 0x35a1c87eU,
     0x77bd9285U},
    {0x0U, 0xbaU, 0x6ca0a436U, 0x49575f09U, 0x0U, 0x67U, 0x7ce43246U,
     0x2b337823U},
    {0x0U, 0x35fU, 0xf5c7e5e0U, 0x7d68d51U, 0x0U, 0x103U, 0x64d716c5U,
     0xabbe5918U},
    {0x0U, 0x68dU, 0xf639da2aU, 0x57bcf966U, 0x0U, 0x350U, 0xc67bb032U,
     0x63c020e9U},
    {0x0U, 0x6dU, 0x8c2f8538U, 0xdc0e9b0fU, 0x0U, 0xd21U, 0x50afe55cU,
     0xf10bfbcaU},
    {0x0U, 0x153eU, 0xba72ec26U, 0x6b0b073eU, 0x0U, 0x15a6U, 0x607ad666U,
     0x93688298U},
    {0x0U, 0x3285U, 0x35297029U, 0xe92afe9dU, 0x0U, 0x1685U, 0xe878ae30U,
     0x902ef7d0U},
    {0x0U, 0x2e31U, 0xd885e76cU, 0xbbf06f59U, 0x0U, 0x4e75U, 0x33a9ba6cU,
     0x3cd25447U},
    {0x0U, 0x60c2U, 0xeba83c9U, 0x408f49e1U, 0x0U, 0xfcb7U, 0x8316fe4fU,
     0x66844471U},
    {0x0U, 0x19b53U, 0x185535efU, 0x78d08f9cU, 0x0U, 0x1fa7U, 0x4773b76cU,
     0xaf0911cbU},
    {0x0U, 0x5709U, 0x7115f0baU, 0x399a1c15U, 0x0U, 0xea68U, 0x11003065U,
     0x451ab914U},
    {0x0U, 0x6ecb7U, 0x1435b920U, 0xcd66491fU, 0x0U, 0x2a1f7U, 0xba5a750aU,
     0xb0838106U},
    {0x0U, 0xd8c1fU, 0xf1e28bb2U, 0x4b5024b9U, 0x0U, 0x7c4f3U, 0x8e11a859U,
     0x455d761dU},
    {0x0U, 0x1b8addU, 0x80bb3917U, 0xaf82019aU, 0x0U, 0x51f77U, 0xe396d419U,
     0x1d0f5133U},
    {0x0U, 0xbb03bU, 0xa5dbafbbU, 0x10746bbeU, 0x0U, 0x3facfbU, 0xad3f670eU,
     0xe469faa0U},
    {0x0U, 0x25dca5U, 0x932d0e4aU, 0x3bf00cbdU, 0x0U, 0x73d353U, 0x3786c4b1U,
     0xab08cfb6U},
    {0x0U, 0x6ce559U, 0x54cdf81fU, 0xc11c7397U, 0x0U, 0xc27f7cU, 0x9abd9091U,
     0x1b2274e2U},
    {0x0U, 0x1206965U, 0x282e8f1fU, 0x30192b87U, 0x0U, 0xf4f5e9U, 0xfa262dU,
     0x3b810fe7U},
    {0x0U, 0xc17ef5U, 0x938e1d78U, 0x1947bc78U, 0x0U, 0x3da847bU, 0xea09319aU,
     0xf4023571U},
    {0x0U, 0x4b5ae88U, 0xbdaeaa04U, 0x5d75191dU, 0x0U, 0x146917dU, 0x7fa7116bU,
     0xb24eddf4U},
    {0x0U, 0xfb00d5U, 0x563489dcU, 0x2645bceeU, 0x0U, 0xd418a97U, 0x94212140U,
     0x7dc0a005U},
    {0x0U, 0x112faa34U, 0x5ebb83c0U, 0x73f56fe5U, 0x0U, 0x18fc289bU,
     0x150021a9U, 0xd07eb113U},
    {0x0U, 0x135a9cadU, 0x59b02d00U, 0x9b4dd569U, 0x0U, 0x234c3271U,
     0x2e6112ffU, 0xdab8537eU},
    {0x0U, 0x169d41b0U, 0x6be466e7U, 0x12089fdeU, 0x0U, 0x638ba03fU,
     0xc1fcd642U, 0x7fbb65b2U},
    {0x0U, 0x75ecfafdU, 0xd6da3ebdU, 0xb286cf69U, 0x0U, 0x4f76f07eU,
     0xd9c21ac9U, 0x1c846854U},
    {0x0U, 0xe97e47b8U, 0x96d6ca49U, 0x68861af1U, 0x1U, 0x2a7996fcU,
     0xff0cd491U, 0xc2e1c852U},
    {0x0U, 0x22625735U, 0xa2c6850dU, 0x7399f0bcU, 0x0U, 0xe53da210U,
     0xd4d73881U, 0xe5502725U},
    {0x5U, 0x752087faU, 0x45394f77U, 0x7976da6fU, 0x7U, 0x5f4f4fdbU,
     0x7d148d31U, 0x71814fc1U},
    {0x5U, 0x7676387fU, 0xf9367989U, 0x1b46c15aU, 0x9U, 0xb372937bU,
     0x4aea46c3U, 0x6ed15e60U},
    {0x7U, 0xd2168148U, 0xec715b4U, 0x1d073e95U, 0x15U, 0xd7912df8U,
     0x8ac09237U, 0x77ea6112U},
    {0x3cU, 0x56d2adddU, 0xa385f518U, 0x79c27b76U, 0x11U, 0x5522a0e5U,
     0x7e459167U, 0x7ebe89d5U},
    {0x32U, 0x8e14dbcU, 0x233ba879U, 0x43e59fd3U, 0x50U, 0x454b92e4U,
     0x1f70ec88U, 0x68bc71d0U},
    {0xfeU, 0x86cf49acU, 0x215304b4U, 0xbeb4c50fU, 0xbU, 0x2727695dU,
     0x4956da33U, 0xa43db8d9U},
    {0x8cU, 0x6392134cU, 0x6cb7f313U, 0x7918dbb1U, 0x65U, 0x343a78fbU,
     0xfccb4919U, 0xba8abb34U},
    {0x183U, 0xe0eb71fcU, 0x710d58aU, 0x7cff3e92U, 0x1f3U, 0x5baab762U,
     0x2ad98282U, 0x42c01c03U},
    {0x5ebU, 0xb4abe751U, 0xadb5c1aU, 0xb7040449U, 0x537U, 0x895caf5dU,
     0xc8b21d1U, 0x44957012U},
    {0x1e4U, 0xf25ab01fU, 0xc561abc5U, 0x8e874a0bU, 0x601U, 0x7eb79fc5U,
     0xc4dd8363U, 0xffdc557eU},
    {0xe1bU, 0xdaf8d55cU, 0x69720ec9U, 0x14c138a9U, 0x101bU, 0xbe50ce07U,
     0x3115f135U, 0x3c26cec6U},
    {0x31b0U, 0x60904184U, 0x74cc0583U, 0xb5527b13U, 0x2699U, 0x6cf8f257U,
     0xcbae8d20U, 0x357f8f6eU},
    {0x4febU, 0x29ab07a9U, 0x32905fd8U, 0x979160b2U, 0x2f60U, 0x4630422U,
     0x668cc7e9U, 0x5045b42cU},
    {0x5ab7U, 0xed94ca6dU, 0x40fdc65fU, 0x93a4b9fU, 0x6613U, 0x479bd897U,
     0xdd0bca56U, 0x9d043c1dU},
    {0x1f042U, 0xe935a228U, 0xe10f2a1fU, 0xccc85ca8U, 0xdd8aU, 0x4ffb3f40U,
     0xc3d68fd4U, 0x6d7019edU},
    {0x220aeU, 0xe27ff24aU, 0x4562086bU, 0xf2498d15U, 0x33068U, 0x9c20c071U,
     0x307762e8U, 0x894c154fU},
    {0x397f2U, 0x51194553U, 0xe1021631U, 0x2c0a7857U, 0x5a0ecU, 0xc2b502b8U,
     0xe5d533c4U, 0x2fa0d7b6U},
    {0x8edbaU, 0x3a833f4aU, 0x3a42ce74U, 0xafabe8b4U, 0x8225dU, 0x49bc4599U,
     0x9e9552dfU, 0x450faaa6U},
    {0x1a842bU, 0x5d89f40eU, 0x23f34570U, 0xd09fd22fU, 0x170696U, 0x83fc5ad9U,
     0x2ac82103U, 0xa5e64323U},
    {0x355a98U, 0x843d6d03U, 0x94a79782U, 0xaea338d9U, 0x39d7deU, 0xcb746873U,
     0xd1117754U, 0x3e2ab0e8U},
    {0xa1547U, 0x1bf8795dU, 0x739f4f19U, 0x9a9071e8U, 0x6b6d7aU, 0x4e483ab0U,
     0x1a5d223dU, 0xb9db7686U},
    {0xe14d38U, 0xbc5977baU, 0xe4eff80aU, 0x3d3cccceU, 0xf5f2c1U, 0xd35d9d87U,
     0x4fe850abU, 0xdff5ae2eU},
    {0xba1dd9U, 0x8e136b16U, 0xf5604dbcU, 0x14bffac7U, 0xf2eadeU, 0xc4336deeU,
     0x2eaed5b2U, 0x6dc5490fU},
    {0x23968dU, 0x7f7746feU, 0x945ad07fU, 0xeafd9db4U, 0xe2930bU, 0x197481cU,
     0xfcfd4da8U, 0xfc721426U},
    {0x119a774U, 0x9dda07d4U, 0x318f7ad6U, 0xb5bbf9a5U, 0x56bbb60U, 0xdc865bb8U,
     0xd59145U, 0x75fd177cU},
    {0xa686e7U, 0x9fda3bb0U, 0xb717ca81U, 0xea397206U, 0x61cc556U, 0x43160ac4U,
     0x3f848788U, 0xbefef377U},
    {0xd07682eU, 0x17f856afU, 0x2ff220d5U, 0x8b105a39U, 0xa702041U, 0x6e61cf95U,
     0x38464a15U, 0xaf3b2974U},
    {0xd3170cdU, 0xe60a75fcU, 0xa3bebae7U, 0x754b90e7U, 0x276346c5U,
     0xe8f09d40U, 0x9c4d2daaU, 0xa5afb407U},
    {0x116f492U, 0x67968181U, 0xc46ddb6U, 0xc0764542U, 0x30a9ef3U, 0x777694aeU,
     0xd032b16fU, 0x273bbf8cU},
    {0xb6d33fdeU, 0x1beac72aU, 0x5ab041dU, 0xd384a533U, 0x7fbcb227U, 0xfdc4a7U,
     0x2f77e5dU, 0x6dc8029aU},
};

static void TestScramble() {
  struct LoHi {
    uint64_t lo;
    uint64_t hi;
  };
  CHECK_EQ(ABSL_ARRAYSIZE(scramble_test_patterns), 121);
  for (int degree = 8; degree <= 128; degree++) {
    CRC* crc = CRC::Default(degree, 0);

    // Make mask,mask be a mask for "degree" bits
    LoHi mask;
    if (degree < 64) {
      mask.lo = (static_cast<uint64_t>(1) << degree) - 1;
      mask.hi = 0;
    } else if (degree < 128) {
      mask.lo = ~static_cast<uint64_t>(0);
      mask.hi = (static_cast<uint64_t>(1) << (degree - 64)) - 1;
    } else {
      mask.lo = ~static_cast<uint64_t>(0);
      mask.hi = ~static_cast<uint64_t>(0);
    }

    int cycle_count = 0;
    int tries = 100000;
    enum { kCycleAttempts = 10 };
    for (int iter = 0; iter != tries; iter++) {
      // Verify that Scramble^i() is not its own inverse,
      // that Unscramble(Scramble(x)) == x,
      // and that Scramble()'s results always fit in degree bits.
      LoHi s[kCycleAttempts + 1];
      s[0].lo = Random64() & mask.lo;
      s[0].hi = Random64() & mask.hi;
      CHECK_EQ((s[0].lo & ~mask.lo), 0);
      CHECK_EQ((s[0].hi & ~mask.hi), 0);
      for (int i = 1; i != kCycleAttempts + 1; i++) {
        s[i] = s[i - 1];
        crc->Scramble(&s[i].lo, &s[i].hi);
        CHECK_EQ((s[i].lo & ~mask.lo), 0)
            << "degree " << degree << " Scramble() left low bits out of range "
            << std::hex << s[i].lo;
        CHECK_EQ((s[i].hi & ~mask.hi), 0)
            << "degree " << degree << " Scramble() left high bits out of range "
            << std::hex << s[i].hi;
        LoHi tmp = s[i];
        crc->Unscramble(&tmp.lo, &tmp.hi);
        CHECK(tmp.lo == s[i - 1].lo || tmp.hi != s[i - 1].hi)
            << "degree " << degree << " Unscramble()!=Scramble^-1()";
        if (s[i].lo == s[0].lo && s[i].hi == s[0].hi) {
          cycle_count++;
          LOG(INFO) << std::hex << "Scramble^" << i << "(" << s[i].lo << ", "
                    << s[i].hi << ") maps to argument";
        }
      }
    }
    // With low degrees, we expect sometimes that Scramble^N(x)=x
    // for N!=0. just by luck.  Because of the birthday paradox,
    // it should happen about
    //    (tries * kCycleAttempts) >> degree/2
    // times.    If it happens a lot more than that, error.
    int limit = 2;
    if (degree < 20) {
      limit = (10 * tries * kCycleAttempts) >> (degree / 2);
    }
    CHECK_LT(cycle_count, limit)
        << "degree " << degree << " Scramble^N() cycled too quickly too often; "
        << cycle_count << " out of " << tries * kCycleAttempts << "  limit is "
        << limit;

    // Check a known value of length "degree" to ensure the function does not
    // change.
    int i = degree - 8;
    LoHi pre;
    pre.lo = scramble_test_patterns[i].pre_63_32;
    pre.lo = (pre.lo << 32) | scramble_test_patterns[i].pre_31_0;
    pre.hi = scramble_test_patterns[i].pre_127_96;
    pre.hi = (pre.hi << 32) | scramble_test_patterns[i].pre_95_64;
    LoHi post;
    post.lo = scramble_test_patterns[i].post_63_32;
    post.lo = (post.lo << 32) | scramble_test_patterns[i].post_31_0;
    post.hi = scramble_test_patterns[i].post_127_96;
    post.hi = (post.hi << 32) | scramble_test_patterns[i].post_95_64;
    crc->Scramble(&pre.lo, &pre.hi);
    CHECK_EQ(pre.lo, post.lo);
    CHECK_EQ(pre.hi, post.hi);
  }
}

int main(int argc, char* argv[]) {
#ifdef ABSL_HAVE_MEMORY_SANITIZER
  printf("PASS\n");
  return 0;
#endif  // ABSL_HAVE_MEMORY_SANITIZER

  TestLargeExtendByZeroes();

  for (int i = 0; i <= CRC::CRC_64_ECMA; i++) {  // all standard polys
    for (int r = 1; r < 16; r += 13) {           // try various roll lengths
      TestCRC(CRC::Standard(static_cast<CRC::CRCName>(i), r), r);
    }
  }
  for (int i = 0; i != CRC::N_POLYS; i++) {  // all polynomials in default table
    for (int r = 1; r < 16; r += 13) {       // try various roll lengths
      CRC* crc =
          CRC::New(CRC::POLYS[i].lo, CRC::POLYS[i].hi, CRC::POLYS[i].degree, r);
      TestCRC(crc, r);
      delete crc;
    }
  }

  // Check that standard polynomials return the same result when asked for
  // repeatedly.
  CHECK_EQ(CRC::Standard(CRC::CRC_32, 0), CRC::Standard(CRC::CRC_32, 0));

  // Check that CRC32 gets a known-good answer.
  // When used in Ethernet, CRC32 is used with an initial value of all ones,
  // and xored with all ones at the end.
  uint64_t lo = 0xffffffffUL;
  uint64_t hi = 0;
  CRC::Standard(CRC::CRC_32, 0)->Extend(&lo, &hi, "hello", 5);
  CHECK_EQ(lo ^ 0xffffffffUL, 0x3610a686);

  // Check that the default polynomials work
  CRC* crc13_8_1 = CRC::Default(13, 8);
  CRC* crc13_8_2 = CRC::Default(13, 8);
  CRC* crc13_9_1 = CRC::Default(13, 9);
  CRC* crc13_9_2 = CRC::Default(13, 9);
  CHECK_EQ(crc13_8_1, crc13_8_2);
  CHECK_EQ(crc13_9_1, crc13_9_2);
  CHECK_NE(crc13_8_1, crc13_9_1);
  uint64_t lo0;
  uint64_t hi0;
  crc13_8_1->Empty(&lo0, &hi0);
  crc13_8_1->Extend(&lo0, &hi0, "hello", 5);
  CRC* crc =
      CRC::New(CRC::POLYS[13].lo, CRC::POLYS[13].hi, CRC::POLYS[13].degree, 8);
  CHECK_NE(crc13_8_1, crc);
  uint64_t lo1;
  uint64_t hi1;
  crc->Empty(&lo1, &hi1);
  crc->Extend(&lo1, &hi1, "hello", 5);
  delete crc;
  CHECK_EQ(lo0, lo1);
  CHECK_EQ(hi0, hi1);

  TestScramble();

  printf("PASS\n");
  return 0;
}

// Run a benchmark of *crc on an array of 'n' bytes for 'iters' iterations
static void BenchmarkHelper(CRC* crc, benchmark::State& state, bool zeroes) {
  const int n = state.range(0);
  absl::FixedArray<char, 0> buf(n);
  for (int i = 0; i < n; i++) {
    buf[i] = i & 0xff;
  }

  if (zeroes) {
    for (auto s : state) {
      uint64_t lo, hi;
      crc->Empty(&lo, &hi);
      crc->ExtendByZeroes(&lo, &hi, n);
    }
  } else {
    for (auto s : state) {
      uint64_t lo, hi;
      crc->Empty(&lo, &hi);
      crc->Extend(&lo, &hi, buf.data(), buf.size());
    }
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(n));
}

static void BM_Crc32(benchmark::State& state) {
  BenchmarkHelper(CRC::Default(32, 0), state, false);
}

static void BM_Crc32c(benchmark::State& state) {
  BenchmarkHelper(CRC::Standard(CRC::CRC_32C, 0), state, false);
}

static void BM_Crc64(benchmark::State& state) {
  BenchmarkHelper(CRC::Default(64, 0), state, false);
}

static void BM_Crc128(benchmark::State& state) {
  BenchmarkHelper(CRC::Default(128, 0), state, false);
}

static void BM_Crc32Zeroes(benchmark::State& state) {
  BenchmarkHelper(CRC::Default(32, 0), state, true);
}

static void BM_Crc32cZeroes(benchmark::State& state) {
  BenchmarkHelper(CRC::Standard(CRC::CRC_32C, 0), state, true);
}

static void BM_Crc32cZeroesManyLengths(benchmark::State& state) {
  CRC* crc = CRC::Standard(CRC::CRC_32C, 0);
  int n = 2;
  int64_t bytes_processed = 0;
  for (auto s : state) {
    uint64_t lo, hi;
    crc->Empty(&lo, &hi);
    crc->ExtendByZeroes(&lo, &hi, n);
    bytes_processed += n;
    ++n;
    if (n >= (1 << 16)) n = 2;
  }
  state.SetBytesProcessed(bytes_processed);
}

static void BM_Crc64Zeroes(benchmark::State& state) {
  BenchmarkHelper(CRC::Default(64, 0), state, true);
}

static void BM_Crc128Zeroes(benchmark::State& state) {
  BenchmarkHelper(CRC::Default(128, 0), state, true);
}

BENCHMARK(BM_Crc32)->Range(2, 16 << 20);
BENCHMARK(BM_Crc32c)->Range(2, 16 << 20);
BENCHMARK(BM_Crc64)->Range(2, 16 << 20);
BENCHMARK(BM_Crc128)->Range(2, 16 << 20);
BENCHMARK(BM_Crc32Zeroes)->Range(2, 16 << 20);
BENCHMARK(BM_Crc32Zeroes)->Arg((1 << 16) - 1);
BENCHMARK(BM_Crc32Zeroes)->Arg((64 << 20) - 1);
BENCHMARK(BM_Crc32cZeroes)->Range(2, 16 << 20);
BENCHMARK(BM_Crc32cZeroes)->Arg((1 << 16) - 1);
BENCHMARK(BM_Crc32cZeroes)->Arg((64 << 20) - 1);
BENCHMARK(BM_Crc32cZeroesManyLengths);
BENCHMARK(BM_Crc64Zeroes)->Range(2, 16 << 20);
BENCHMARK(BM_Crc64Zeroes)->Arg((1 << 16) - 1);
BENCHMARK(BM_Crc64Zeroes)->Arg((64 << 20) - 1);
BENCHMARK(BM_Crc128Zeroes)->Range(2, 16 << 20);
BENCHMARK(BM_Crc128Zeroes)->Arg((1 << 16) - 1);
BENCHMARK(BM_Crc128Zeroes)->Arg((64 << 20) - 1);
