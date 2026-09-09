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

#include "gloop/util/hash/crc.h"

#include <stdio.h>
#include <stdlib.h>

#include <cstdint>
#include <ios>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "absl/container/fixed_array.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"

namespace {

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
                   uint64_t* hi, absl::string_view str) {
  for (char c : str) {
    *lo ^= (static_cast<uint8_t>(c) & 0xff);
    ExtendBy8ZeroBits(poly_lo, poly_hi, lo, hi);
  }
}

// Same as CRC::Extend, but one byte at a time.
void MyExtend(CRC* crc, uint64_t* lo, uint64_t* hi, absl::string_view str) {
  for (char c : str) {
    crc->Extend(lo, hi, &c, 1);
  }
}

testing::AssertionResult VerifyCrcImplementations(CRC* crc,
                                                  absl::string_view str,
                                                  uint64_t* plo = nullptr,
                                                  uint64_t* phi = nullptr,
                                                  uint64_t* pmlo = nullptr,
                                                  uint64_t* pmhi = nullptr) {
  uint64_t lo, mlo;
  uint64_t hi, mhi;

  VLOG(1) << "string " << str << " len " << str.size();
  crc->Empty(&lo, &hi);
  VLOG(1) << absl::StreamFormat("poly   %016x%016x", hi, lo);
  crc->Extend(&lo, &hi, str.data(), str.size());
  VLOG(1) << absl::StreamFormat("Extend %016x%016x", hi, lo);

  crc->Empty(&mlo, &mhi);
  MyExtend(crc, &mlo, &mhi, str);
  VLOG(1) << absl::StreamFormat("bybyte %016x%016x", mhi, mlo);

  if (lo != mlo) {
    return testing::AssertionFailure()
           << "Standard Extend (low bits) did not match "
              "reference Extend by byte: "
           << "Expected (reference): 0x" << std::hex << mlo
           << ", Got (standard): 0x" << lo;
  }
  if (hi != mhi) {
    return testing::AssertionFailure()
           << "Standard Extend (high bits) did not match "
              "reference Extend by byte: "
           << "Expected (reference): 0x" << std::hex << mhi
           << ", Got (standard): 0x" << hi;
  }

  uint64_t blo;
  uint64_t bhi;
  crc->Empty(&blo, &bhi);
  MyExtendBasic(blo, bhi, &blo, &bhi, str);
  VLOG(1) << absl::StreamFormat("by_bit %016x%016x", bhi, blo);

  if (lo != blo) {
    return testing::AssertionFailure()
           << "Standard Extend (low bits) did not match "
              "reference Extend by bit: "
           << "Expected (reference): 0x" << std::hex << blo
           << ", Got (standard): 0x" << lo;
  }
  if (hi != bhi) {
    return testing::AssertionFailure()
           << "Standard Extend (high bits) did not match "
              "reference Extend by bit: "
           << "Expected (reference): 0x" << std::hex << bhi
           << ", Got (standard): 0x" << hi;
  }

  if (plo) *plo = lo;
  if (phi) *phi = hi;
  if (pmlo) *pmlo = mlo;
  if (pmhi) *pmhi = mhi;
  return testing::AssertionSuccess();
}

uint32_t CalculateCrc32(absl::string_view data) {
  CRC* crc = CRC::Standard(CRC::CRC_32, 0);
  uint64_t lo = ~0;  // note that this is 64 bits of 1, not 32.
  crc->Extend(&lo, nullptr, data.data(), data.size());
  return ~lo;
}

uint32_t MyCalculateCrc32(absl::string_view data) {
  CRC* crc = CRC::Standard(CRC::CRC_32, 0);
  uint64_t lo = ~0;  // 64 bits of 1, not 32 bits of 1.
  MyExtend(crc, &lo, nullptr, data);
  return ~lo;
}

// For a given crc, for roll length r, for all test strings,
// we check that Extend() and ExtendByZeroes() get the same answer
// as doing the same thing one byte at a time.
// We also check that the rolling CRC gets the same answer as simply
// calculating the CRC on each successive string.
struct CrcTestParam {
  enum Type { kStandard, kCustom };
  Type type;
  int index;
  int roll_length;
};

std::vector<CrcTestParam> GetCrcTestParams() {
  std::vector<CrcTestParam> params;
  params.reserve(CRC::CRC_64_ECMA * 16 + CRC::N_POLYS * 16);
  for (int i = 0; i <= CRC::CRC_64_ECMA; i++) {
    for (int r = 1; r < 16; r += 13) {
      params.push_back({CrcTestParam::kStandard, i, r});
    }
  }
  for (int i = 0; i != CRC::N_POLYS; i++) {
    for (int r = 1; r < 16; r += 13) {
      params.push_back({CrcTestParam::kCustom, i, r});
    }
  }
  return params;
}

std::string PrintCrcTestParam(
    const ::testing::TestParamInfo<CrcTestParam>& info) {
  return absl::StrFormat(
      "%s_idx%d_r%d",
      info.param.type == CrcTestParam::kStandard ? "Standard" : "Custom",
      info.param.index, info.param.roll_length);
}

class CrcParamTest : public ::testing::TestWithParam<CrcTestParam> {};

TEST_P(CrcParamTest, TestCrc) {
  const CrcTestParam& param = GetParam();
  CRC* crc = nullptr;
  std::unique_ptr<CRC> custom_crc;
  if (param.type == CrcTestParam::kStandard) {
    crc = CRC::Standard(static_cast<CRC::CRCName>(param.index),
                        param.roll_length);
  } else {
    custom_crc.reset(
        CRC::New(CRC::POLYS[param.index].lo, CRC::POLYS[param.index].hi,
                 CRC::POLYS[param.index].degree, param.roll_length));
    crc = custom_crc.get();
  }
  ASSERT_NE(crc, nullptr);

  static constexpr absl::string_view strings[] = {
      absl::string_view(""),
      absl::string_view("\0", 1),
      absl::string_view("\1", 1),
      absl::string_view("\0\1", 2),
      absl::string_view("hello"),
      absl::string_view("the quick brown fox jumps over the lazy dog"),
      absl::string_view("To be, or not to be: that is the question:\n"
                        "Whether 'tis nobler in the mind to suffer\n"
                        "The slings and arrows of outrageous fortune,\n"
                        "Or to take arms against a sea of troubles,\n"
                        "And by opposing end them?\n")};

  std::mt19937_64 bitgen(1);
  // Try all test strings
  for (int j = 0; j != std::size(strings); j++) {
    SCOPED_TRACE(absl::StrFormat("j = %d, len = %d, str = %s", j,
                                 strings[j].size(), strings[j]));
    uint64_t lo, hi, mlo, mhi;

    EXPECT_TRUE(
        VerifyCrcImplementations(crc, strings[j], &lo, &hi, &mlo, &mhi));

    // Because the SIMD-accelerated CRC attempts to align loads on
    // some platforms, sweep through starting offsets and sizes in case that
    // provokes any bugs.
    if (strings[j].size() > 64) {
      int l = strings[j].size();
      absl::string_view s = strings[j];

      for (int m = 1; m <= 64; m++) {
        for (int k = 1; k < l - m; k++) {
          SCOPED_TRACE(absl::StrFormat("align: m = %d, k = %d", m, k));
          EXPECT_TRUE(VerifyCrcImplementations(crc, s.substr(k, m)));
        }
      }
    }

    int z = absl::Uniform<int>(bitgen, 0, 0x10000);
    crc->ExtendByZeroes(&lo, &hi, z);
    for (int k = 0; k != z; k++) {
      MyExtend(crc, &mlo, &mhi, absl::string_view("\0", 1));
    }
    EXPECT_EQ(lo, mlo);
    EXPECT_EQ(hi, mhi);

    if (strings[j].size() > param.roll_length) {
      crc->Empty(&lo, &hi);
      crc->Extend(&lo, &hi, strings[j].data(), param.roll_length);
      crc->Empty(&mlo, &mhi);
      MyExtend(crc, &mlo, &mhi, strings[j].substr(0, param.roll_length));
      EXPECT_EQ(lo, mlo);
      EXPECT_EQ(hi, mhi);
      for (size_t k = param.roll_length; k != strings[j].size(); k++) {
        SCOPED_TRACE(absl::StrFormat("roll: k = %d", k));
        crc->Roll(&lo, &hi, strings[j][k - param.roll_length], strings[j][k]);
        crc->Empty(&mlo, &mhi);
        MyExtend(
            crc, &mlo, &mhi,
            strings[j].substr(k - param.roll_length + 1, param.roll_length));
        EXPECT_EQ(lo, mlo);
        EXPECT_EQ(hi, mhi);
      }
    }
    VLOG(1) << "";
  }
}

INSTANTIATE_TEST_SUITE_P(CrcTests, CrcParamTest,
                         ::testing::ValuesIn(GetCrcTestParams()),
                         PrintCrcTestParam);

TEST(CrcTest, LargeExtendByZeroes) {
  uint64_t lo = 0x5766cc56f872daafLL;
  uint64_t hi = 0x42a470899c3c1cabLL;
  uint64_t size = 115455819776LL;
  uint64_t zero = 0;
  CRC* crc_gen = CRC::Default(128, 0);
  crc_gen->ExtendByZeroes(&lo, &hi, size - zero);
  EXPECT_EQ(hi, uint64_t{0xaf9e0b922a11b8bbu});
  EXPECT_EQ(lo, uint64_t{0xf2b9427e289f3091u});
}

// Return a 64-bit random number
static uint64_t Random64(std::mt19937_64& bitgen) {
  return absl::Uniform<uint64_t>(bitgen);
}

// These are precomputed patterns for Scramble()
// used to ensure that the function never changes.
// element i represents an (i+8)-bit value.
// If the pre-scramble value is (pre_hi, pre_lo)
// then the post-scramble value is (post_hi, post_lo)
struct ScrambleTestPattern {
  uint64_t pre_hi;
  uint64_t pre_lo;
  uint64_t post_hi;
  uint64_t post_lo;
};

inline constexpr ScrambleTestPattern kScrambleTestPatterns[] = {
    {0x0ULL, 0xffULL, 0x0ULL, 0xd0ULL},
    {0x0ULL, 0x16fULL, 0x0ULL, 0xacULL},
    {0x0ULL, 0x3f6ULL, 0x0ULL, 0x110ULL},
    {0x0ULL, 0x68ULL, 0x0ULL, 0x72ULL},
    {0x0ULL, 0x8deULL, 0x0ULL, 0xf39ULL},
    {0x0ULL, 0x1bdbULL, 0x0ULL, 0x1dbfULL},
    {0x0ULL, 0x55ULL, 0x0ULL, 0x1c24ULL},
    {0x0ULL, 0x206aULL, 0x0ULL, 0x4284ULL},
    {0x0ULL, 0xe1cdULL, 0x0ULL, 0xf462ULL},
    {0x0ULL, 0xad00ULL, 0x0ULL, 0x11bc8ULL},
    {0x0ULL, 0x39eadULL, 0x0ULL, 0x2c8a0ULL},
    {0x0ULL, 0x3db21ULL, 0x0ULL, 0x678afULL},
    {0x0ULL, 0x7f66dULL, 0x0ULL, 0x511dbULL},
    {0x0ULL, 0x18683eULL, 0x0ULL, 0x1165e9ULL},
    {0x0ULL, 0x1e10bcULL, 0x0ULL, 0x135d4fULL},
    {0x0ULL, 0x2a61c5ULL, 0x0ULL, 0x2f0214ULL},
    {0x0ULL, 0x4e1c31ULL, 0x0ULL, 0x26628ULL},
    {0x0ULL, 0xaa5596ULL, 0x0ULL, 0x19b1909ULL},
    {0x0ULL, 0x6f2142ULL, 0x0ULL, 0x55db98ULL},
    {0x0ULL, 0x2faa870ULL, 0x0ULL, 0x19165c6ULL},
    {0x0ULL, 0xfe8e714ULL, 0x0ULL, 0x965fcbfULL},
    {0x0ULL, 0x147f38deULL, 0x0ULL, 0x73e45ecULL},
    {0x0ULL, 0x2a9e1fa5ULL, 0x0ULL, 0xf01915ULL},
    {0x0ULL, 0x4dfbe558ULL, 0x0ULL, 0x64b9fc72ULL},
    {0x0ULL, 0xca9eca0cULL, 0x0ULL, 0xd713fc8aULL},
    {0x0ULL, 0x1a6fca739ULL, 0x0ULL, 0x18b54eab9ULL},
    {0x0ULL, 0x153bf3918ULL, 0x0ULL, 0x21d33608dULL},
    {0x0ULL, 0x47de6cd37ULL, 0x0ULL, 0x362a52b17ULL},
    {0x0ULL, 0x4b90f9904ULL, 0x0ULL, 0xcfa3e9cf0ULL},
    {0x0ULL, 0xc87ccf7b1ULL, 0x0ULL, 0xf6f3396c8ULL},
    {0x0ULL, 0xfdf50d91eULL, 0x0ULL, 0x1ef4e400dcULL},
    {0x0ULL, 0x4cabac4550ULL, 0x0ULL, 0x194b5fcda2ULL},
    {0x0ULL, 0x61e5d56101ULL, 0x0ULL, 0x6228e490a2ULL},
    {0x0ULL, 0x2257086374ULL, 0x0ULL, 0x1f478f2942bULL},
    {0x0ULL, 0x3214bcecc5eULL, 0x0ULL, 0x5b079d45e9ULL},
    {0x0ULL, 0x31f026d6009ULL, 0x0ULL, 0x488848d3cc3ULL},
    {0x0ULL, 0x1b2a6b8aa3aULL, 0x0ULL, 0x5f1caa3c5aaULL},
    {0x0ULL, 0x18bc656f6bf5ULL, 0x0ULL, 0x19940431d927ULL},
    {0x0ULL, 0x2da8d406d1dULL, 0x0ULL, 0x2dd44e230abbULL},
    {0x0ULL, 0x513398a35c47ULL, 0x0ULL, 0xd20313163c7ULL},
    {0x0ULL, 0xa82169ee4e41ULL, 0x0ULL, 0x32992e0428ccULL},
    {0x0ULL, 0x397cba501546ULL, 0x0ULL, 0xc6f9614cd674ULL},
    {0x0ULL, 0x259c8052d1ccdULL, 0x0ULL, 0x3a400e82e7e0cULL},
    {0x0ULL, 0x31e1bba78b3c8ULL, 0x0ULL, 0x1df2fc65f92faULL},
    {0x0ULL, 0x58e193fa18640ULL, 0x0ULL, 0xc30d4b67dc92dULL},
    {0x0ULL, 0x32cb24d5302a5ULL, 0x0ULL, 0xf279b02319c4fULL},
    {0x0ULL, 0x135316b251fdc6ULL, 0x0ULL, 0x32387861b346eULL},
    {0x0ULL, 0x74319b33041583ULL, 0x0ULL, 0xbd7ccf4291cb6ULL},
    {0x0ULL, 0x32b03992f9b35cULL, 0x0ULL, 0xb84bbb808834eULL},
    {0x0ULL, 0xc7a03f9c408914ULL, 0x0ULL, 0xab76d2f4b0037eULL},
    {0x0ULL, 0x39d4acc13da0b59ULL, 0x0ULL, 0x250ef745aeabf1ULL},
    {0x0ULL, 0x49d4bba950f5fd9ULL, 0x0ULL, 0x70c87e99aeafabULL},
    {0x0ULL, 0xb29e7a0ce46fe6eULL, 0x0ULL, 0xf97bc5129f08fa1ULL},
    {0x0ULL, 0x10e83e044ff12dfcULL, 0x0ULL, 0x1f9a0485d56d3c68ULL},
    {0x0ULL, 0x300315dc72a8d742ULL, 0x0ULL, 0x2847eed769d1760cULL},
    {0x0ULL, 0x77fd3fb34ac57c6fULL, 0x0ULL, 0x3c9e304571cb9fe3ULL},
    {0x0ULL, 0xda2827e64f5252bfULL, 0x0ULL, 0x3ee49b6d69fb440bULL},
    {0x0ULL, 0xe9430a9850abc7e1ULL, 0x0ULL, 0x7f22abfcf188b564ULL},
    {0x3ULL, 0x853e8091d1d79f39ULL, 0x2ULL, 0x4e83549fc33830ULL},
    {0x0ULL, 0x65e4669e23c4258aULL, 0x4ULL, 0xa476134ad7ecb1b3ULL},
    {0x2ULL, 0x5ac6a81a32fc9708ULL, 0x4ULL, 0xc2e6f647aa92a109ULL},
    {0x7ULL, 0xb036e96636f39bafULL, 0x19ULL, 0x95a9ff2a5540a932ULL},
    {0x8ULL, 0xe48bf8ca7f96d5d0ULL, 0x2aULL, 0xb836e7ad3de5a58fULL},
    {0x40ULL, 0x99dacac8cb9611b3ULL, 0x47ULL, 0xd067ae74b93a92afULL},
    {0xc3ULL, 0xfde3f083983d54f4ULL, 0x9eULL, 0x35a1c87e77bd9285ULL},
    {0xbaULL, 0x6ca0a43649575f09ULL, 0x67ULL, 0x7ce432462b337823ULL},
    {0x35fULL, 0xf5c7e5e007d68d51ULL, 0x103ULL, 0x64d716c5abbe5918ULL},
    {0x68dULL, 0xf639da2a57bcf966ULL, 0x350ULL, 0xc67bb03263c020e9ULL},
    {0x6dULL, 0x8c2f8538dc0e9b0fULL, 0xd21ULL, 0x50afe55cf10bfbcaULL},
    {0x153eULL, 0xba72ec266b0b073eULL, 0x15a6ULL, 0x607ad66693688298ULL},
    {0x3285ULL, 0x35297029e92afe9dULL, 0x1685ULL, 0xe878ae30902ef7d0ULL},
    {0x2e31ULL, 0xd885e76cbbf06f59ULL, 0x4e75ULL, 0x33a9ba6c3cd25447ULL},
    {0x60c2ULL, 0xeba83c9408f49e1ULL, 0xfcb7ULL, 0x8316fe4f66844471ULL},
    {0x19b53ULL, 0x185535ef78d08f9cULL, 0x1fa7ULL, 0x4773b76caf0911cbULL},
    {0x5709ULL, 0x7115f0ba399a1c15ULL, 0xea68ULL, 0x11003065451ab914ULL},
    {0x6ecb7ULL, 0x1435b920cd66491fULL, 0x2a1f7ULL, 0xba5a750ab0838106ULL},
    {0xd8c1fULL, 0xf1e28bb24b5024b9ULL, 0x7c4f3ULL, 0x8e11a859455d761dULL},
    {0x1b8addULL, 0x80bb3917af82019aULL, 0x51f77ULL, 0xe396d4191d0f5133ULL},
    {0xbb03bULL, 0xa5dbafbb10746bbeULL, 0x3facfbULL, 0xad3f670ee469faa0ULL},
    {0x25dca5ULL, 0x932d0e4a3bf00cbdULL, 0x73d353ULL, 0x3786c4b1ab08cfb6ULL},
    {0x6ce559ULL, 0x54cdf81fc11c7397ULL, 0xc27f7cULL, 0x9abd90911b2274e2ULL},
    {0x1206965ULL, 0x282e8f1f30192b87ULL, 0xf4f5e9ULL, 0xfa262d3b810fe7ULL},
    {0xc17ef5ULL, 0x938e1d781947bc78ULL, 0x3da847bULL, 0xea09319af4023571ULL},
    {0x4b5ae88ULL, 0xbdaeaa045d75191dULL, 0x146917dULL, 0x7fa7116bb24eddf4ULL},
    {0xfb00d5ULL, 0x563489dc2645bceeULL, 0xd418a97ULL, 0x942121407dc0a005ULL},
    {0x112faa34ULL, 0x5ebb83c073f56fe5ULL, 0x18fc289bULL,
     0x150021a9d07eb113ULL},
    {0x135a9cadULL, 0x59b02d009b4dd569ULL, 0x234c3271ULL,
     0x2e6112ffdab8537eULL},
    {0x169d41b0ULL, 0x6be466e712089fdeULL, 0x638ba03fULL,
     0xc1fcd6427fbb65b2ULL},
    {0x75ecfafdULL, 0xd6da3ebdb286cf69ULL, 0x4f76f07eULL,
     0xd9c21ac91c846854ULL},
    {0xe97e47b8ULL, 0x96d6ca4968861af1ULL, 0x12a7996fcULL,
     0xff0cd491c2e1c852ULL},
    {0x22625735ULL, 0xa2c6850d7399f0bcULL, 0xe53da210ULL,
     0xd4d73881e5502725ULL},
    {0x5752087faULL, 0x45394f777976da6fULL, 0x75f4f4fdbULL,
     0x7d148d3171814fc1ULL},
    {0x57676387fULL, 0xf93679891b46c15aULL, 0x9b372937bULL,
     0x4aea46c36ed15e60ULL},
    {0x7d2168148ULL, 0xec715b41d073e95ULL, 0x15d7912df8ULL,
     0x8ac0923777ea6112ULL},
    {0x3c56d2adddULL, 0xa385f51879c27b76ULL, 0x115522a0e5ULL,
     0x7e4591677ebe89d5ULL},
    {0x3208e14dbcULL, 0x233ba87943e59fd3ULL, 0x50454b92e4ULL,
     0x1f70ec8868bc71d0ULL},
    {0xfe86cf49acULL, 0x215304b4beb4c50fULL, 0xb2727695dULL,
     0x4956da33a43db8d9ULL},
    {0x8c6392134cULL, 0x6cb7f3137918dbb1ULL, 0x65343a78fbULL,
     0xfccb4919ba8abb34ULL},
    {0x183e0eb71fcULL, 0x710d58a7cff3e92ULL, 0x1f35baab762ULL,
     0x2ad9828242c01c03ULL},
    {0x5ebb4abe751ULL, 0xadb5c1ab7040449ULL, 0x537895caf5dULL,
     0xc8b21d144957012ULL},
    {0x1e4f25ab01fULL, 0xc561abc58e874a0bULL, 0x6017eb79fc5ULL,
     0xc4dd8363ffdc557eULL},
    {0xe1bdaf8d55cULL, 0x69720ec914c138a9ULL, 0x101bbe50ce07ULL,
     0x3115f1353c26cec6ULL},
    {0x31b060904184ULL, 0x74cc0583b5527b13ULL, 0x26996cf8f257ULL,
     0xcbae8d20357f8f6eULL},
    {0x4feb29ab07a9ULL, 0x32905fd8979160b2ULL, 0x2f6004630422ULL,
     0x668cc7e95045b42cULL},
    {0x5ab7ed94ca6dULL, 0x40fdc65f093a4b9fULL, 0x6613479bd897ULL,
     0xdd0bca569d043c1dULL},
    {0x1f042e935a228ULL, 0xe10f2a1fccc85ca8ULL, 0xdd8a4ffb3f40ULL,
     0xc3d68fd46d7019edULL},
    {0x220aee27ff24aULL, 0x4562086bf2498d15ULL, 0x330689c20c071ULL,
     0x307762e8894c154fULL},
    {0x397f251194553ULL, 0xe10216312c0a7857ULL, 0x5a0ecc2b502b8ULL,
     0xe5d533c42fa0d7b6ULL},
    {0x8edba3a833f4aULL, 0x3a42ce74afabe8b4ULL, 0x8225d49bc4599ULL,
     0x9e9552df450faaa6ULL},
    {0x1a842b5d89f40eULL, 0x23f34570d09fd22fULL, 0x17069683fc5ad9ULL,
     0x2ac82103a5e64323ULL},
    {0x355a98843d6d03ULL, 0x94a79782aea338d9ULL, 0x39d7decb746873ULL,
     0xd11177543e2ab0e8ULL},
    {0xa15471bf8795dULL, 0x739f4f199a9071e8ULL, 0x6b6d7a4e483ab0ULL,
     0x1a5d223db9db7686ULL},
    {0xe14d38bc5977baULL, 0xe4eff80a3d3cccceULL, 0xf5f2c1d35d9d87ULL,
     0x4fe850abdff5ae2eULL},
    {0xba1dd98e136b16ULL, 0xf5604dbc14bffac7ULL, 0xf2eadec4336deeULL,
     0x2eaed5b26dc5490fULL},
    {0x23968d7f7746feULL, 0x945ad07feafd9db4ULL, 0xe2930b0197481cULL,
     0xfcfd4da8fc721426ULL},
    {0x119a7749dda07d4ULL, 0x318f7ad6b5bbf9a5ULL, 0x56bbb60dc865bb8ULL,
     0xd5914575fd177cULL},
    {0xa686e79fda3bb0ULL, 0xb717ca81ea397206ULL, 0x61cc55643160ac4ULL,
     0x3f848788befef377ULL},
    {0xd07682e17f856afULL, 0x2ff220d58b105a39ULL, 0xa7020416e61cf95ULL,
     0x38464a15af3b2974ULL},
    {0xd3170cde60a75fcULL, 0xa3bebae7754b90e7ULL, 0x276346c5e8f09d40ULL,
     0x9c4d2daaa5afb407ULL},
    {0x116f49267968181ULL, 0xc46ddb6c0764542ULL, 0x30a9ef3777694aeULL,
     0xd032b16f273bbf8cULL},
    {0xb6d33fde1beac72aULL, 0x5ab041dd384a533ULL, 0x7fbcb22700fdc4a7ULL,
     0x2f77e5d6dc8029aULL},
};

class CrcScrambleTest : public ::testing::TestWithParam<int> {};

TEST_P(CrcScrambleTest, VerifyScrambleProperties) {
  int degree = GetParam();
  struct LoHi {
    uint64_t lo;
    uint64_t hi;
  };
  ASSERT_EQ(std::size(kScrambleTestPatterns), 121);

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

  CRC* crc = CRC::Default(degree, 0);
  std::mt19937_64 bitgen(1);

  int cycle_count = 0;
  int tries = 100000;
  enum { kCycleAttempts = 10 };
  for (int iter = 0; iter != tries; iter++) {
    // Verify that Scramble^i() is not its own inverse,
    // that Unscramble(Scramble(x)) == x,
    // and that Scramble()'s results always fit in degree bits.
    LoHi s[kCycleAttempts + 1];
    s[0].lo = Random64(bitgen) & mask.lo;
    s[0].hi = Random64(bitgen) & mask.hi;
    ASSERT_EQ((s[0].lo & ~mask.lo), 0);
    ASSERT_EQ((s[0].hi & ~mask.hi), 0);
    for (int i = 1; i != kCycleAttempts + 1; i++) {
      s[i] = s[i - 1];
      crc->Scramble(&s[i].lo, &s[i].hi);
      EXPECT_EQ((s[i].lo & ~mask.lo), 0)
          << "degree " << degree << " Scramble() left low bits out of range "
          << std::hex << s[i].lo;
      EXPECT_EQ((s[i].hi & ~mask.hi), 0)
          << "degree " << degree << " Scramble() left high bits out of range "
          << std::hex << s[i].hi;
      LoHi tmp = s[i];
      crc->Unscramble(&tmp.lo, &tmp.hi);
      EXPECT_EQ(tmp.lo, s[i - 1].lo)
          << "degree " << degree << " Unscramble()!=Scramble^-1()";
      EXPECT_EQ(tmp.hi, s[i - 1].hi)
          << "degree " << degree << " Unscramble()!=Scramble^-1()";
      if (s[i].lo == s[0].lo && s[i].hi == s[0].hi) {
        cycle_count++;
        VLOG(1) << std::hex << "Scramble^" << i << "(" << s[i].lo << ", "
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
  EXPECT_LT(cycle_count, limit)
      << "degree " << degree << " Scramble^N() cycled too quickly too often; "
      << cycle_count << " out of " << tries * kCycleAttempts << "  limit is "
      << limit;

  // Check a known value of length "degree" to ensure the function does not
  // change.
  int i = degree - 8;
  uint64_t pre_lo = kScrambleTestPatterns[i].pre_lo;
  uint64_t pre_hi = kScrambleTestPatterns[i].pre_hi;
  uint64_t post_lo = kScrambleTestPatterns[i].post_lo;
  uint64_t post_hi = kScrambleTestPatterns[i].post_hi;
  crc->Scramble(&pre_lo, &pre_hi);
  EXPECT_EQ(pre_lo, post_lo);
  EXPECT_EQ(pre_hi, post_hi);
}

std::vector<int> GetScrambleDegrees() {
  std::vector<int> degrees;
  degrees.reserve(121);
  for (int d = 8; d <= 128; ++d) {
    degrees.push_back(d);
  }
  return degrees;
}

INSTANTIATE_TEST_SUITE_P(CrcScrambleTests, CrcScrambleTest,
                         ::testing::ValuesIn(GetScrambleDegrees()),
                         [](const ::testing::TestParamInfo<int>& info) {
                           return absl::StrFormat("Degree_%d", info.param);
                         });

TEST(CrcTest, TestRegression) {
  const char* regression =
      "\000\000\022\253\000\000\000\000\003\001\000\006\332$QG\000\000\000"
      "\000\220/P\t\000\000\000\000\000\000\024\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
      "\000\000\000\000\000\000\000\000\000x";  // 65 characters

  EXPECT_TRUE(VerifyCrcImplementations(CRC::Standard(CRC::CRC_32, 0),
                                       absl::string_view(regression, 64)));

  for (int i = 0; i < 64; i++) {
    uint32_t got = CalculateCrc32(absl::string_view(regression, i));
    uint32_t want = MyCalculateCrc32(absl::string_view(regression, i));
    EXPECT_EQ(got, want) << "i=" << i << std::hex << ", want=" << want
                         << ", got=" << got;
  }
}

TEST(CrcTest, NonPositiveLength) {
  CRC* crcs[] = {
      CRC::Default(8, 0),  CRC::Default(16, 0),  CRC::Default(32, 0),
      CRC::Default(64, 0), CRC::Default(128, 0), CRC::Standard(CRC::CRC_32C, 0),
  };

  const char data[] = "hello";

  for (CRC* crc : crcs) {
    uint64_t lo, hi;
    crc->Empty(&lo, &hi);
    uint64_t expected_lo = lo;
    uint64_t expected_hi = hi;

    // Extend should not change the CRC and should not crash/hang.
    crc->Extend(&lo, &hi, data, 0);
    EXPECT_EQ(lo, expected_lo);
    EXPECT_EQ(hi, expected_hi);

#ifdef NDEBUG
    crc->Extend(&lo, &hi, data, -89);
    EXPECT_EQ(lo, expected_lo);
    EXPECT_EQ(hi, expected_hi);
#else
    EXPECT_DEATH_IF_SUPPORTED(crc->Extend(&lo, &hi, data, -89), "length >= 0");
#endif

    // ExtendByZeroes should not change the CRC and should not crash/hang.
    crc->ExtendByZeroes(&lo, &hi, 0);
    EXPECT_EQ(lo, expected_lo);
    EXPECT_EQ(hi, expected_hi);

#ifdef NDEBUG
    crc->ExtendByZeroes(&lo, &hi, -89);
    EXPECT_EQ(lo, expected_lo);
    EXPECT_EQ(hi, expected_hi);
#else
    EXPECT_DEATH_IF_SUPPORTED(crc->ExtendByZeroes(&lo, &hi, -89),
                              "length >= 0");
#endif

    // Concat should not change the CRC and should not crash/hang when
    // ylen <= 0.
    crc->Concat(&lo, &hi, 12345, 67890, 0);
    EXPECT_EQ(lo, expected_lo);
    EXPECT_EQ(hi, expected_hi);

#ifdef NDEBUG
    crc->Concat(&lo, &hi, 12345, 67890, -89);
    EXPECT_EQ(lo, expected_lo);
    EXPECT_EQ(hi, expected_hi);
#else
    EXPECT_DEATH_IF_SUPPORTED(crc->Concat(&lo, &hi, 12345, 67890, -89),
                              "ylen >= 0");
#endif
  }
}

TEST(CrcTest, StandardReturnsSameResult) {
  EXPECT_EQ(CRC::Standard(CRC::CRC_32, 0), CRC::Standard(CRC::CRC_32, 0));
}

TEST(CrcTest, EthernetCrc32GetsKnownGoodAnswer) {
  uint64_t lo = 0xffffffffUL;
  uint64_t hi = 0;
  CRC::Standard(CRC::CRC_32, 0)->Extend(&lo, &hi, "hello", 5);
  EXPECT_EQ(lo ^ 0xffffffffUL, 0x3610a686);
}

TEST(CrcTest, DefaultPolynomialsWork) {
  CRC* crc13_8_1 = CRC::Default(13, 8);
  CRC* crc13_8_2 = CRC::Default(13, 8);
  CRC* crc13_9_1 = CRC::Default(13, 9);
  CRC* crc13_9_2 = CRC::Default(13, 9);
  EXPECT_EQ(crc13_8_1, crc13_8_2);
  EXPECT_EQ(crc13_9_1, crc13_9_2);
  EXPECT_NE(crc13_8_1, crc13_9_1);
  uint64_t lo0;
  uint64_t hi0;
  crc13_8_1->Empty(&lo0, &hi0);
  crc13_8_1->Extend(&lo0, &hi0, "hello", 5);
  std::unique_ptr<CRC> crc(
      CRC::New(CRC::POLYS[13].lo, CRC::POLYS[13].hi, CRC::POLYS[13].degree, 8));
  EXPECT_NE(crc13_8_1, crc.get());
  uint64_t lo1;
  uint64_t hi1;
  crc->Empty(&lo1, &hi1);
  crc->Extend(&lo1, &hi1, "hello", 5);
  EXPECT_EQ(lo0, lo1);
  EXPECT_EQ(hi0, hi1);
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
// Find the size where SIMD begins to win
BENCHMARK(BM_Crc64)->DenseRange(16, 40, 4);
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

// ---------------------------------------------------------------------------
// Fuzz tests for the Gloop CRC library.
// ---------------------------------------------------------------------------

void ExtendNeverCrashes32(const std::string& data) {
  CRC* crc = CRC::Default(32, 0);
  uint64_t lo, hi;
  crc->Empty(&lo, &hi);
  crc->Extend(&lo, &hi, data.data(), static_cast<int64_t>(data.size()));
}
FUZZ_TEST(CrcFuzzTest, ExtendNeverCrashes32);

void ExtendNeverCrashes64(const std::string& data) {
  CRC* crc = CRC::Default(64, 0);
  uint64_t lo, hi;
  crc->Empty(&lo, &hi);
  crc->Extend(&lo, &hi, data.data(), static_cast<int64_t>(data.size()));
}
FUZZ_TEST(CrcFuzzTest, ExtendNeverCrashes64);

void ConcatAlgebraicProperty(const std::string& a, const std::string& b) {
  CRC* crc = CRC::Default(32, 0);

  uint64_t ab_lo, ab_hi;
  crc->Empty(&ab_lo, &ab_hi);
  crc->Extend(&ab_lo, &ab_hi, a.data(), static_cast<int64_t>(a.size()));
  crc->Extend(&ab_lo, &ab_hi, b.data(), static_cast<int64_t>(b.size()));

  uint64_t a_lo, a_hi;
  crc->Empty(&a_lo, &a_hi);
  crc->Extend(&a_lo, &a_hi, a.data(), static_cast<int64_t>(a.size()));

  uint64_t b_lo, b_hi;
  crc->Empty(&b_lo, &b_hi);
  crc->Extend(&b_lo, &b_hi, b.data(), static_cast<int64_t>(b.size()));

  crc->Concat(&a_lo, &a_hi, b_lo, b_hi, static_cast<int64_t>(b.size()));

  EXPECT_EQ(ab_lo, a_lo);
  EXPECT_EQ(ab_hi, a_hi);
}
FUZZ_TEST(CrcFuzzTest, ConcatAlgebraicProperty);

static constexpr size_t kRollLength = 8;

void RollSlidingWindowCorrectness(const std::string& data) {
  if (data.size() <= kRollLength) return;

  CRC* crc = CRC::Default(32, kRollLength);

  uint64_t roll_lo, roll_hi;
  crc->Empty(&roll_lo, &roll_hi);
  crc->Extend(&roll_lo, &roll_hi, data.data(),
              static_cast<int64_t>(kRollLength));

  for (size_t i = kRollLength; i < data.size(); ++i) {
    uint8_t o_byte = static_cast<uint8_t>(data[i - kRollLength]);
    uint8_t i_byte = static_cast<uint8_t>(data[i]);
    crc->Roll(&roll_lo, &roll_hi, o_byte, i_byte);

    uint64_t expected_lo, expected_hi;
    crc->Empty(&expected_lo, &expected_hi);
    crc->Extend(&expected_lo, &expected_hi, data.data() + i - kRollLength + 1,
                static_cast<int64_t>(kRollLength));

    EXPECT_EQ(roll_lo, expected_lo);
    EXPECT_EQ(roll_hi, expected_hi);
  }
}
FUZZ_TEST(CrcFuzzTest, RollSlidingWindowCorrectness);

void ScrambleUnscrambleRoundtrip(uint64_t lo, uint64_t hi) {
  CRC* crc = CRC::Default(64, 0);
  hi = 0;

  uint64_t orig_lo = lo;
  uint64_t orig_hi = hi;

  crc->Scramble(&lo, &hi);
  crc->Unscramble(&lo, &hi);

  EXPECT_EQ(lo, orig_lo);
  EXPECT_EQ(hi, orig_hi);
}
FUZZ_TEST(CrcFuzzTest, ScrambleUnscrambleRoundtrip);

void ScrambleUnscrambleRoundtrip32(uint64_t lo) {
  CRC* crc = CRC::Default(32, 0);
  lo &= 0xFFFFFFFF;
  uint64_t hi = 0;

  uint64_t orig_lo = lo;

  crc->Scramble(&lo, &hi);
  crc->Unscramble(&lo, &hi);

  EXPECT_EQ(lo, orig_lo);
  EXPECT_EQ(hi, uint64_t{0});
}
FUZZ_TEST(CrcFuzzTest, ScrambleUnscrambleRoundtrip32);

void ExtendByZeroesEquivalence(const std::string& prefix, uint16_t num_zeroes) {
  CRC* crc = CRC::Default(32, 0);

  uint64_t ext_lo, ext_hi;
  crc->Empty(&ext_lo, &ext_hi);
  crc->Extend(&ext_lo, &ext_hi, prefix.data(),
              static_cast<int64_t>(prefix.size()));
  const std::string zeroes(num_zeroes, '\0');
  crc->Extend(&ext_lo, &ext_hi, zeroes.data(),
              static_cast<int64_t>(zeroes.size()));

  uint64_t ebz_lo, ebz_hi;
  crc->Empty(&ebz_lo, &ebz_hi);
  crc->Extend(&ebz_lo, &ebz_hi, prefix.data(),
              static_cast<int64_t>(prefix.size()));
  crc->ExtendByZeroes(&ebz_lo, &ebz_hi, static_cast<int64_t>(num_zeroes));

  EXPECT_EQ(ebz_lo, ext_lo);
  EXPECT_EQ(ebz_hi, ext_hi);
}
FUZZ_TEST(CrcFuzzTest, ExtendByZeroesEquivalence);

}  // namespace
