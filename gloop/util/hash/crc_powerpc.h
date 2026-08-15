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

#ifndef THIRD_PARTY_GLOOP_UTIL_HASH_CRC_POWERPC_H_
#define THIRD_PARTY_GLOOP_UTIL_HASH_CRC_POWERPC_H_

#ifdef __powerpc__

#include <altivec.h>
#include <sys/auxv.h>

#include "absl/numeric/int128.h"

namespace crc_internal {

// Fast polynomial multiplication operates on a vector type, which
// are target-dependent.  Define an alias for vector type so that our code is
// less platform dependent.  For efficiency, we require that element size of
// Chunk to by half the size of Chunk.

typedef vector unsigned long Chunk;  // NOLINT(runtime/int)

// This value produce good benchmark results on a 3.6GHz POWER8
constexpr size_t kPrefetchDistance = 1280;

// Load a chunk at (address + offset), which may be unaligned to
// chunk size.
inline Chunk LoadUnalignedChunk(const uint8_t* address, size_t offset) {
  return reinterpret_cast<Chunk>(vec_vsx_ld(offset, address));
}

// Load an aligned chunk from memory that is naturally aligned from
// (address + offset).  Caller must guarantee proper alignment.
inline Chunk LoadAlignedChunk(const uint8_t* address, size_t offset) {
  // vec_ld is better for aligned addresses since it is only one instruction.
  return reinterpret_cast<Chunk>(vec_ld(offset, address));
}

// Multiply corresponding halves in two chunks and add the products in GF(2).
inline Chunk Z2MultiplyAndAdd(Chunk c1, Chunk c2) {
  Chunk product;
  asm("vpmsumd %0, %1, %2" : "=v"(product) : "v"(c1), "v"(c2));
  return product;
}

// Unpack the first half.  Counting from low memory address, the first quarter
// is unpacked to the first half of the result and the second quarter to the
// second half of the result.
inline Chunk ChunkUnpackFirstHalf(Chunk chunk) {
  const vector unsigned int kZeroes = vec_splat_u32(0);
  const vector unsigned int chunk_as_vec_unsigned_int =
      reinterpret_cast<vector unsigned int>(chunk);
  return reinterpret_cast<Chunk>(
      vec_mergeh(kZeroes, chunk_as_vec_unsigned_int));
}

// Make a Chunk out of two uint64s. First is stored in half with higher address
// and second in half with lower address.
inline Chunk ChunkFromUint64s(uint64_t high, uint64_t low) {
  const Chunk v = {low, high};
  return v;
}

// Conversion functions between Chunk and uint
inline Chunk ChunkFromUint64(uint64_t u) { return ChunkFromUint64s(0, u); }

inline uint64_t ChunkLow64(Chunk c) { return c[0]; }

inline uint64_t ChunkHigh64(Chunk c) { return c[1]; }

inline absl::uint128 Uint128FromChunk(Chunk c) {
  return absl::MakeUint128(ChunkHigh64(c), ChunkLow64(c));
}

inline Chunk ChunkFromUint128(const absl::uint128& u) {
  return ChunkFromUint64s(Uint128High64(u), Uint128Low64(u));
}

// Compute a vector shift argument for variable shift.
inline Chunk PrepareVariableShift(int shift) {
  const vector unsigned char s = {static_cast<unsigned char>(shift)};
  return reinterpret_cast<Chunk>(vec_splat(s, 0));
}

inline Chunk LeftShiftChunk(Chunk c, Chunk shift) {
  const vector unsigned char uc_c = reinterpret_cast<vector unsigned char>(c);
  const vector unsigned char uc_shift =
      reinterpret_cast<vector unsigned char>(shift);
  return reinterpret_cast<Chunk>(vec_sll(vec_slo(uc_c, uc_shift), uc_shift));
}

inline Chunk RightShiftChunk(Chunk c, Chunk shift) {
  const vector unsigned char uc_c = reinterpret_cast<vector unsigned char>(c);
  const vector unsigned char uc_shift =
      reinterpret_cast<vector unsigned char>(shift);
  return reinterpret_cast<Chunk>(vec_srl(vec_sro(uc_c, uc_shift), uc_shift));
}

// Shift amount must be a multiple of 8.
inline Chunk LeftByteShiftChunk(Chunk c, Chunk shift) {
  const vector unsigned char uc_c = reinterpret_cast<vector unsigned char>(c);
  const vector unsigned char uc_shift =
      reinterpret_cast<vector unsigned char>(shift);
  return reinterpret_cast<Chunk>(vec_slo(uc_c, uc_shift));
}

// Shift amount must be a multiple of 8.
inline Chunk RightByteShiftChunk(Chunk c, Chunk shift) {
  const vector unsigned char uc_c = reinterpret_cast<vector unsigned char>(c);
  const vector unsigned char uc_shift =
      reinterpret_cast<vector unsigned char>(shift);
  return reinterpret_cast<Chunk>(vec_sro(uc_c, uc_shift));
}

// CheckFastPolyMulBody in crc_poly.cc in crc needs its address taken.  So
// define its contents here as an inline function so that we can have
// different versions for different targets without resorting to preprocessor
// conditionals.
inline void CheckFastPolyMulBody(bool* result) {
  *result = (getauxval(AT_HWCAP2) & PPC_FEATURE2_HAS_VEC_CRYPTO) != 0;
}

}  // namespace crc_internal

#endif  // __powerpc__

#endif  // THIRD_PARTY_GLOOP_UTIL_HASH_CRC_POWERPC_H_
