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

#ifndef THIRD_PARTY_GLOOP_UTIL_HASH_CRC64_X86_H_
#define THIRD_PARTY_GLOOP_UTIL_HASH_CRC64_X86_H_

#if defined(__x86_64__) && defined(__PCLMUL__)

#include <x86intrin.h>

#include <cstddef>
#include <cstdint>

#include "absl/numeric/int128.h"

namespace crc_internal {

typedef __m128i Chunk;

inline Chunk LoadUnalignedChunk(const uint8_t* address, size_t offset) {
  return _mm_loadu_si128(reinterpret_cast<const __m128i*>(address + offset));
}

inline Chunk LoadAlignedChunk(const uint8_t* address, size_t offset) {
  return _mm_load_si128(reinterpret_cast<const __m128i*>(address + offset));
}

inline Chunk Z2MultiplyAndAdd(Chunk c1, Chunk c2) {
  __m128i low = _mm_clmulepi64_si128(c1, c2, 0x00);
  __m128i high = _mm_clmulepi64_si128(c1, c2, 0x11);
  return _mm_xor_si128(low, high);
}

inline Chunk ChunkUnpackFirstHalf(Chunk chunk) {
  return _mm_unpacklo_epi32(_mm_setzero_si128(), chunk);
}

inline Chunk ChunkFromUint64s(uint64_t high, uint64_t low) {
  return _mm_set_epi64x(high, low);
}

inline Chunk ChunkFromUint64(uint64_t u) { return _mm_set_epi64x(0, u); }

inline uint64_t ChunkLow64(Chunk c) { return _mm_extract_epi64(c, 0); }

inline uint64_t ChunkHigh64(Chunk c) { return _mm_extract_epi64(c, 1); }

inline absl::uint128 Uint128FromChunk(Chunk c) {
  return absl::MakeUint128(ChunkHigh64(c), ChunkLow64(c));
}

inline Chunk ChunkFromUint128(const absl::uint128& u) {
  return ChunkFromUint64s(absl::Uint128High64(u), absl::Uint128Low64(u));
}

inline Chunk PrepareVariableShift(int shift_bits) {
  return ChunkFromUint64(shift_bits);
}

// LeftShiftChunk implements "shift bits across lanes" for 128-bit
// SIMD values.  Intel's SSE and AVX lack such a variable-distance
// shift operation across lanes.  Therefore, do it in scalar registers.
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
  return _mm_set_epi64x(nh, nl);
}

// RightShiftChunk implements "shift bits across lanes" for 128-bit
// SIMD values.  Intel's SSE and AVX lack such a variable-distance
// shift operation across lanes.  Therefore, do it in scalar registers.
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
  return _mm_set_epi64x(nh, nl);
}

inline Chunk LeftByteShiftChunk(Chunk c, Chunk shift) {
  return LeftShiftChunk(c, shift);
}

inline Chunk RightByteShiftChunk(Chunk c, Chunk shift) {
  return RightShiftChunk(c, shift);
}

inline void CheckFastPolyMulBody(bool* result) { *result = 1; }

}  // namespace crc_internal

#endif  // __x86_64__

#endif  // THIRD_PARTY_GLOOP_UTIL_HASH_CRC64_X86_H_
