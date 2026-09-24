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

#include "gloop/util/coding/elias_fano/nthbit.h"

#include <cstdint>

#include "absl/base/internal/cpu_detect.h"
#include "absl/log/check.h"
#include "absl/numeric/bits.h"

namespace elias_fano {
namespace nthbit_internal {

#if defined(__x86_64__)
// NOLINTBEGIN(abseil-no-internal-dependencies)
const bool kUseFastPath = []() {
  // If there is no pdep instruction, use the fallback.
  if (!absl::base_internal::SupportsBmi2()) return false;

  // AMD Zen1 and Zen2 have very slow pdep, so use the fallback implementation.
  const absl::base_internal::CpuType cpu_type =
      absl::base_internal::GetCpuType();
  switch (cpu_type) {
    case absl::base_internal::CpuType::kAmdNaples:
    case absl::base_internal::CpuType::kAmdRome:
      return false;
    default:
      return true;
  }
}();
// NOLINTEND(abseil-no-internal-dependencies)
#endif

namespace {
constexpr uint32_t kBitOffset[256] = {
    0x88888888, 0x88888880, 0x88888881, 0x88888810, 0x88888882, 0x88888820,
    0x88888821, 0x88888210, 0x88888883, 0x88888830, 0x88888831, 0x88888310,
    0x88888832, 0x88888320, 0x88888321, 0x88883210, 0x88888884, 0x88888840,
    0x88888841, 0x88888410, 0x88888842, 0x88888420, 0x88888421, 0x88884210,
    0x88888843, 0x88888430, 0x88888431, 0x88884310, 0x88888432, 0x88884320,
    0x88884321, 0x88843210, 0x88888885, 0x88888850, 0x88888851, 0x88888510,
    0x88888852, 0x88888520, 0x88888521, 0x88885210, 0x88888853, 0x88888530,
    0x88888531, 0x88885310, 0x88888532, 0x88885320, 0x88885321, 0x88853210,
    0x88888854, 0x88888540, 0x88888541, 0x88885410, 0x88888542, 0x88885420,
    0x88885421, 0x88854210, 0x88888543, 0x88885430, 0x88885431, 0x88854310,
    0x88885432, 0x88854320, 0x88854321, 0x88543210, 0x88888886, 0x88888860,
    0x88888861, 0x88888610, 0x88888862, 0x88888620, 0x88888621, 0x88886210,
    0x88888863, 0x88888630, 0x88888631, 0x88886310, 0x88888632, 0x88886320,
    0x88886321, 0x88863210, 0x88888864, 0x88888640, 0x88888641, 0x88886410,
    0x88888642, 0x88886420, 0x88886421, 0x88864210, 0x88888643, 0x88886430,
    0x88886431, 0x88864310, 0x88886432, 0x88864320, 0x88864321, 0x88643210,
    0x88888865, 0x88888650, 0x88888651, 0x88886510, 0x88888652, 0x88886520,
    0x88886521, 0x88865210, 0x88888653, 0x88886530, 0x88886531, 0x88865310,
    0x88886532, 0x88865320, 0x88865321, 0x88653210, 0x88888654, 0x88886540,
    0x88886541, 0x88865410, 0x88886542, 0x88865420, 0x88865421, 0x88654210,
    0x88886543, 0x88865430, 0x88865431, 0x88654310, 0x88865432, 0x88654320,
    0x88654321, 0x86543210, 0x88888887, 0x88888870, 0x88888871, 0x88888710,
    0x88888872, 0x88888720, 0x88888721, 0x88887210, 0x88888873, 0x88888730,
    0x88888731, 0x88887310, 0x88888732, 0x88887320, 0x88887321, 0x88873210,
    0x88888874, 0x88888740, 0x88888741, 0x88887410, 0x88888742, 0x88887420,
    0x88887421, 0x88874210, 0x88888743, 0x88887430, 0x88887431, 0x88874310,
    0x88887432, 0x88874320, 0x88874321, 0x88743210, 0x88888875, 0x88888750,
    0x88888751, 0x88887510, 0x88888752, 0x88887520, 0x88887521, 0x88875210,
    0x88888753, 0x88887530, 0x88887531, 0x88875310, 0x88887532, 0x88875320,
    0x88875321, 0x88753210, 0x88888754, 0x88887540, 0x88887541, 0x88875410,
    0x88887542, 0x88875420, 0x88875421, 0x88754210, 0x88887543, 0x88875430,
    0x88875431, 0x88754310, 0x88875432, 0x88754320, 0x88754321, 0x87543210,
    0x88888876, 0x88888760, 0x88888761, 0x88887610, 0x88888762, 0x88887620,
    0x88887621, 0x88876210, 0x88888763, 0x88887630, 0x88887631, 0x88876310,
    0x88887632, 0x88876320, 0x88876321, 0x88763210, 0x88888764, 0x88887640,
    0x88887641, 0x88876410, 0x88887642, 0x88876420, 0x88876421, 0x88764210,
    0x88887643, 0x88876430, 0x88876431, 0x88764310, 0x88876432, 0x88764320,
    0x88764321, 0x87643210, 0x88888765, 0x88887650, 0x88887651, 0x88876510,
    0x88887652, 0x88876520, 0x88876521, 0x88765210, 0x88887653, 0x88876530,
    0x88876531, 0x88765310, 0x88876532, 0x88765320, 0x88765321, 0x87653210,
    0x88887654, 0x88876540, 0x88876541, 0x88765410, 0x88876542, 0x88765420,
    0x88765421, 0x87654210, 0x88876543, 0x88765430, 0x88765431, 0x87654310,
    0x88765432, 0x87654320, 0x87654321, 0x76543210};

}  // namespace

uint64_t NthbitSlow(uint64_t x, uint64_t n) {
  DCHECK_LT(n, 64);
  DCHECK_GT(absl::popcount(x), n);

  uint32_t c = absl::popcount(static_cast<uint32_t>(x));

  // Use a uint32 when possible to avoid opsize prefixes.
  // Calculate a shifted value to avoid a long chain of CPU dependencies
  // on (v >> shift).
  uint32_t vtemp = static_cast<uint32_t>(x >> 32);

  // delta32 calculation followed by comparison is converted to a conditional
  // move.
  int32_t delta32 = n - c;
  uint32_t v = delta32 < 0 ? x : vtemp;
  n = delta32 < 0 ? n : delta32;

  // shift_accumulator is specifically written to make it easy for the
  // compiler to make use of the |lea| instruction.
  // At this stage, shift_accumulator will be:
  //   0 for a shift of 32.
  //   -1 for a shift of 0.
  int32_t mask32 = delta32 >> 31;
  int32_t shift_accumulator = mask32;

  c = absl::popcount(v & 0xffff);
  vtemp = v >> 16;
  delta32 = n - c;
  v = delta32 < 0 ? v : vtemp;
  n = delta32 < 0 ? n : delta32;
  mask32 = delta32 >> 31;
  shift_accumulator = shift_accumulator * 2 + mask32;

  uint32_t masked_v = v & 0xff;
  c = absl::popcount(masked_v);
  vtemp = (v >> 8) & 0xff;
  delta32 = n - c;
  v = delta32 < 0 ? masked_v : vtemp;
  n = delta32 < 0 ? n : delta32;

  int32_t shift = (n << 2) & 31;
  int32_t remainder = (kBitOffset[v] >> shift) & 0xf;

  // This mask32 has to be delayed after the remainder calculation, otherwise
  // the compiler generates unnecessary extra instructions.
  mask32 = delta32 >> 31;
  shift_accumulator = shift_accumulator * 2 + mask32;
  return 8 * shift_accumulator + remainder + 0x38;
}

}  // namespace nthbit_internal
}  // namespace elias_fano
