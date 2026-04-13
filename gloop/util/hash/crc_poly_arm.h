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

#ifndef THIRD_PARTY_GLOOP_UTIL_HASH_CRC_ARM64_H_
#define THIRD_PARTY_GLOOP_UTIL_HASH_CRC_ARM64_H_

#if defined(__aarch64__) && defined(__ARM_FEATURE_CRYPTO)

#include <arm_neon.h>

#include <cstddef>
#include <cstdint>

#include "absl/numeric/int128.h"

namespace crc_internal {

typedef uint64x2_t Chunk;

inline Chunk LoadUnalignedChunk(const uint8_t* address, size_t offset) {
  return vld1q_u64(reinterpret_cast<const uint64_t*>(address + offset));
}

inline Chunk LoadAlignedChunk(const uint8_t* address, size_t offset) {
  return vld1q_u64(reinterpret_cast<const uint64_t*>(address + offset));
}

inline Chunk Z2MultiplyAndAdd(Chunk c1, Chunk c2) {
  poly64_t a_lo = (poly64_t)vgetq_lane_u64(c1, 0);
  poly64_t b_lo = (poly64_t)vgetq_lane_u64(c2, 0);
  poly64_t a_hi = (poly64_t)vgetq_lane_u64(c1, 1);
  poly64_t b_hi = (poly64_t)vgetq_lane_u64(c2, 1);
  poly128_t p_lo = vmull_p64(a_lo, b_lo);
  poly128_t p_hi = vmull_p64(a_hi, b_hi);
  return veorq_u64(vreinterpretq_u64_p128(p_lo), vreinterpretq_u64_p128(p_hi));
}

inline Chunk ChunkFromUint64s(uint64_t high, uint64_t low) {
  uint64x2_t res = vdupq_n_u64(0);
  res = vsetq_lane_u64(low, res, 0);
  res = vsetq_lane_u64(high, res, 1);
  return res;
}

inline Chunk ChunkUnpackFirstHalf(Chunk chunk) {
  uint32x4_t c = vreinterpretq_u32_u64(chunk);
  uint32x4_t zero = vdupq_n_u32(0);
  return vreinterpretq_u64_u32(vzip1q_u32(zero, c));
}

inline Chunk ChunkFromUint64(uint64_t u) { return ChunkFromUint64s(0, u); }

inline uint64_t ChunkLow64(Chunk c) { return vgetq_lane_u64(c, 0); }

inline uint64_t ChunkHigh64(Chunk c) { return vgetq_lane_u64(c, 1); }

inline absl::uint128 Uint128FromChunk(Chunk c) {
  return absl::MakeUint128(ChunkHigh64(c), ChunkLow64(c));
}

inline Chunk ChunkFromUint128(const absl::uint128& u) {
  return ChunkFromUint64s(absl::Uint128High64(u), absl::Uint128Low64(u));
}

inline Chunk PrepareVariableShift(int shift_bits) {
  return ChunkFromUint64(shift_bits);
}

inline Chunk LeftShiftChunk(Chunk c, Chunk shift) {
  uint64_t shift_bits = ChunkLow64(shift);
  uint64_t l = ChunkLow64(c);
  uint64_t h = ChunkHigh64(c);

  uint64_t nl, nh;
  if (shift_bits == 0) {
    nl = l;
    nh = h;
  } else if (shift_bits < 64) {
    nl = l << shift_bits;
    nh = (h << shift_bits) | (l >> (64 - shift_bits));
  } else if (shift_bits < 128) {
    nl = 0;
    nh = l << (shift_bits - 64);
  } else {
    nl = 0;
    nh = 0;
  }
  return ChunkFromUint64s(nh, nl);
}

inline Chunk RightShiftChunk(Chunk c, Chunk shift) {
  uint64_t shift_bits = ChunkLow64(shift);
  uint64_t l = ChunkLow64(c);
  uint64_t h = ChunkHigh64(c);

  uint64_t nl, nh;
  if (shift_bits == 0) {
    nl = l;
    nh = h;
  } else if (shift_bits < 64) {
    nl = (l >> shift_bits) | (h << (64 - shift_bits));
    nh = h >> shift_bits;
  } else if (shift_bits < 128) {
    nl = h >> (shift_bits - 64);
    nh = 0;
  } else {
    nl = 0;
    nh = 0;
  }
  return ChunkFromUint64s(nh, nl);
}

inline Chunk LeftByteShiftChunk(Chunk c, Chunk shift) {
  return LeftShiftChunk(c, shift);
}

inline Chunk RightByteShiftChunk(Chunk c, Chunk shift) {
  return RightShiftChunk(c, shift);
}

inline void CheckFastPolyMulBody(bool* result) { *result = 1; }

}  // namespace crc_internal

#endif  // __aarch64__

#endif  // THIRD_PARTY_GLOOP_UTIL_HASH_CRC_ARM64_H_
