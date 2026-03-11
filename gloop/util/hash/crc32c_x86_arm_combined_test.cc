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

#include <sys/types.h>

#include <cstdint>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log_streamer.h"
#include "absl/random/random.h"
#include "absl/strings/str_format.h"
#include "gloop/util/bits/bits.h"
#include "gloop/util/hash/crc.h"
#include "gloop/util/hash/crc_internal.h"
#include "gtest/gtest.h"

ABSL_FLAG(bool, crc32c_generate_powers, false,
          "If true print powers of crc32c polynomial");
ABSL_FLAG(bool, crc32c_generate_constants, false,
          "If true print PCLMULQDQ constants for crc32c polynomial");

namespace {

// Formulas are on page 22 of Intel crc paper:
// https://www.intel.com/content/dam/www/public/us/en/documents/white-papers/fast-crc-computation-generic-polynomials-pclmulqdq-paper.pdf
uint64_t computeU(int shift, uint32_t poly) {
  uint32_t r = 1 << 24;
  uint32_t d = 0;
  int len = shift / 8 + 1;
  while (len > 4) {
    for (int i = 7; i >= 0; i--) {
      d <<= 1;
      if (r & 0x80000000u) {
        r <<= 1;
        r ^= poly;
        d |= 1;
      } else {
        r <<= 1;
      }
    }
    len--;
  }
  return (static_cast<uint64_t>(Bits::ReverseBits32(d)) << 1) | 1;
}

// Also see
// https://stackoverflow.com/questions/21171733/calculating-constants-for-crc32-using-pclmulqdq
uint64_t computeK(int shift, uint32_t poly) {
  uint32_t r = 1 << 24;
  int len = shift / 8 + 1;
  while (len > 4) {
    for (int i = 7; i >= 0; i--) {
      if (r & 0x80000000u) {
        r <<= 1;
        r ^= poly;
      } else {
        r <<= 1;
      }
    }
    len--;
  }
  return static_cast<uint64_t>(Bits::ReverseBits32(r)) << 1;
}

uint64_t computeP(uint32_t poly) {
  return (static_cast<uint64_t>(Bits::ReverseBits32(poly)) << 1);
}

TEST(MagicNumbers, constants) {
  constexpr uint32_t kPoly = 0x1edc6f41;

  uint64_t k1 = computeK(4 * 128 + 32, kPoly);
  uint64_t k2 = computeK(4 * 128 - 32, kPoly);
  uint64_t k3 = computeK(2 * 128 + 32, kPoly);
  uint64_t k4 = computeK(2 * 128 - 32, kPoly);
  uint64_t k5 = computeK(128 + 32, kPoly);
  uint64_t k6 = computeK(128 - 32, kPoly);
  uint64_t k7 = computeK(64, kPoly);
  uint64_t p = computeP(kPoly);
  uint64_t u = computeU(64, kPoly);

  CHECK_EQ(k1, 0x740eef02);
  CHECK_EQ(k2, 0x9e4addf8);
  CHECK_EQ(k3, 0x1384aa63a);
  CHECK_EQ(k4, 0xba4fc28e);
  CHECK_EQ(k5, 0xf20c0dfe);
  CHECK_EQ(k6, 0x14cd00bd6);
  CHECK_EQ(k7, 0xdd45aab8);
  CHECK_EQ(p, 0x105ec76f0);
  CHECK_EQ(u, 0xdea713f1);

  if (absl::GetFlag(FLAGS_crc32c_generate_constants)) {
    absl::PrintF("const uint64 k1 = 0x%09x;\n", k1);
    absl::PrintF("const uint64 k2 = 0x%09x;\n", k2);
    absl::PrintF("const uint64 k3 = 0x%09x;\n", k3);
    absl::PrintF("const uint64 k4 = 0x%09x;\n", k4);
    absl::PrintF("const uint64 k5 = 0x%09x;\n", k5);
    absl::PrintF("const uint64 k6 = 0x%09x;\n", k6);
    absl::PrintF("const uint64 k7 = 0x%09x;\n", k7);
    absl::PrintF("const uint64 kP = 0x%09x;\n", p);
    absl::PrintF("const uint64 kU = 0x%09x;\n", u);
  }
}

}  // end of anonymous namespace
