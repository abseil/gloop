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

// CRC computation based on fast polynomial multiplication in GF(2).
// Some architectures have instructions for polynomial multiplication in
// GF(2).  We can use these instructions to implement CRC efficiently.
//
// References:
//
// "Fast CRC Computation for Generic Polynomials Using PCLMULQDQ Instruction"
// http://www.intel.com/content/dam/www/public/us/en/documents/white-papers/fast-crc-computation-generic-polynomials-pclmulqdq-paper.pdf
//
// The implementation here largely follows the white paper above except:
// - For input size less than kSmallInputSize, we use the base class for
//   extension.
//   handle small inputs, remaining bytes at end and table initialization.
// - We keep head of data stream in vector registers.  So loops here appear
//   to end one iteration later than their counterparts in the white paper.
// - We use Barrett reduction in the final step only for degrees 32 and 64.
//   For other degrees, we use the base class to reduce final input.

// x86_64 & POWER may be able to use fast polynomial multiplication
// instructions.  However it is found that the improvements on
// Sandybridge and Ivybridge are not significiant.  So we only
// do this on POWER8 for now.  We may revisit this for Haswell.
#include <cstdint>
#if defined(__powerpc__) && defined(__CRYPTO__)
#define CAN_USE_FAST_POLY_MUL 1
#endif

#include <stddef.h>
#include <stdint.h>

#include "gloop/util/hash/crc_internal.h"

#ifdef CAN_USE_FAST_POLY_MUL

#if defined(__powerpc__)
#include "gloop/util/hash/crc_powerpc.h"
#else
#error "Unsupported platform for fast poly mul"
#endif

namespace crc_internal {

// Implementation details not to be exported outside this file.
namespace {

constexpr size_t kChunkSize = sizeof(Chunk);
constexpr size_t kChunkBitSize = kChunkSize * CHAR_BIT;

// kAddressChecking indicates whether some sort of memory checking is occurring.
#if defined(ADDRESS_SANITIZER) || defined(MEMORY_SANITIZER) || \
    defined(THREAD_SANITIZER)
constexpr bool kAddressChecking = true;
#else
constexpr bool kAddressChecking = false;
#endif

// Mask to remove unused bytes in the first chunk.
constexpr Chunk kLoadLeftMaskTable[kChunkSize] = {
    {0xffffffffffffffffUL, 0xffffffffffffffffUL},
    {0xffffffffffffff00UL, 0xffffffffffffffffUL},
    {0xffffffffffff0000UL, 0xffffffffffffffffUL},
    {0xffffffffff000000UL, 0xffffffffffffffffUL},
    {0xffffffff00000000UL, 0xffffffffffffffffUL},
    {0xffffff0000000000UL, 0xffffffffffffffffUL},
    {0xffff000000000000UL, 0xffffffffffffffffUL},
    {0xff00000000000000UL, 0xffffffffffffffffUL},
    {0x0000000000000000UL, 0xffffffffffffffffUL},
    {0x0000000000000000UL, 0xffffffffffffff00UL},
    {0x0000000000000000UL, 0xffffffffffff0000UL},
    {0x0000000000000000UL, 0xffffffffff000000UL},
    {0x0000000000000000UL, 0xffffffff00000000UL},
    {0x0000000000000000UL, 0xffffff0000000000UL},
    {0x0000000000000000UL, 0xffff000000000000UL},
    {0x0000000000000000UL, 0xff00000000000000UL},
};

// Return a bit mask of from bit 0 to bit n - 1.
inline absl::uint128 FirstNBits(int n) {
  return n == 0 ? absl::uint128(0) : ~absl::uint128(0) >> (128 - n);
}

// Compute x^k mod P(x) where P(x) has degree of n. P(x) is represented by p
// in reversed bit order with an implict coefficient of 1 for x^n.  Result
// is a degree n-1 polynomial with all terms represented explicitly in
// reversed bit order.
absl::uint128 PowerOf2Mod(uint32_t k, const absl::uint128& p, uint32_t n) {
  CHECK_GE(128, n);
  const absl::uint128 kOne = 1;
  if (k < n) {
    return kOne << (n - 1 - k);
  }

  absl::uint128 remainder = p;
  for (int i = n; i < k; i++) {
    if ((remainder & kOne) == kOne) {
      remainder = (remainder >> 1) ^ p;
    } else {
      remainder >>= 1;
    }
  }
  return remainder;
}

// Compute x^k / P(x) where P(x) has degree of n. P(x) is represented by p
// in with an implict coefficient of 1 for x^n and. The rest of the polynomial
// is represented with higher order terms in lower order bit.  For example,
// coefficient for x^(n-1) is at bit 0.  If k < n, return 0.  Otherwise result
// is a degree k-n polynomial with all terms represented explicitly in with
// highest term starting at bit 0.
absl::uint128 PowerOf2Div(uint32_t k, const absl::uint128& p, uint32_t n) {
  if (k < n) {
    return 0;
  }

  CHECK_GE(128, n);
  CHECK_GT(128, k - n);

  // Highest term coefficient of remainder.
  const absl::uint128 kHighest = 1;

  // coefficient for term x^0 in quotient.
  const absl::uint128 kXPower0 = absl::uint128(1) << (k - n);
  absl::uint128 remainder = p;
  absl::uint128 quotient = kXPower0;
  for (int i = n; i < k; i++) {
    if ((remainder & kHighest) == kHighest) {
      remainder = (remainder >> 1) ^ p;
      quotient = (quotient >> 1) | kXPower0;
    } else {
      remainder >>= 1;
      quotient >>= 1;
    }
  }
  return quotient;
}

// Helper to read the next aligned chunk at ptr + offset * kChunkSize and fold
// the current chunk to it using a given folding constant.
inline Chunk ReadNextAndFold(const uint8_t* ptr, int offset, Chunk chunk,
                             Chunk kFold) {
  const Chunk next_chunk = LoadAlignedChunk(ptr, offset * kChunkSize);
  return Z2MultiplyAndAdd(chunk, kFold) ^ next_chunk;
}

// This is a CRC implementation class based on fast polynomial multiplication.
// Base is a base class that can perform the same CRC computation.
// CRCValue is the narrowest unsigned type that can hold CRC result values.
template <class Base, typename CRCValue>
class CRCHWPolyMul : public Base {
 public:
  CRCHWPolyMul() {}
  virtual ~CRCHWPolyMul() {}

  virtual void Extend(uint64_t* lo, uint64_t* hi, const void* bytes,
                      int64_t length) const;
  virtual void ExtendByZeroes(uint64_t* lo, uint64_t* hi, int64_t length) const;
  virtual void InitTables();

 private:
  static constexpr size_t kCRCValueBitSize = sizeof(CRCValue) * CHAR_BIT;

  // Input smaller than this is extended using base class.  This is roughly
  // the input size at which vectorized CRC starts to beat generic C++ code
  // on a 3GHz POWER8.
  static constexpr size_t kSmallInputSize = 6;

  // Sizes of various zero extension tables.
  static constexpr size_t kLogSmallZeroTableSize = 8;
  static constexpr size_t kSmallZeroTableSize = 1 << kLogSmallZeroTableSize;

  // The big zero extension table is organized by 'digits'. We interpret
  // The higher (sizeof(size_t) * CHAR_BIT - kLogSmallZeroTableSize) bits
  // of extension length as a number in base kZeroBase.
  static constexpr size_t kLogZeroBase = 4;
  static constexpr size_t kZeroBase = 1 << kLogZeroBase;  // must be power of 2
  static constexpr size_t kBigZeroTableRows =
      ((sizeof(int64_t) * CHAR_BIT - kLogSmallZeroTableSize + kLogZeroBase -
        1) /
       kLogZeroBase);

  // CRC extension for input length less than a Chunk.
  void ExtendSmall(uint64_t* lo, uint64_t* hi, const void* bytes,
                   int64_t length) const;

  void ExtendTail(uint64_t* lo, uint64_t* hi, Chunk chunk, const uint8_t* bytes,
                  int length) const;

  // Compute a * b mod P(x).
  Chunk Z2MultiplyModP(Chunk a, Chunk b) const;

  // Compute 2^n mod P(x) in using a slow method.  This is used before the
  // zero extension tables are initialized.
  Chunk SlowPowerOf2Remainder(int32_t n) const {
    const absl::uint128 remainder = PowerOf2Mod(n, this->poly_, this->degree_);
    CHECK_EQ(0, absl::Uint128High64(remainder));
    return ChunkFromUint64(absl::Uint128Low64(remainder));
  }

  // Compute 2^n mod P(x) in using a fast method.  This is used after the zero
  // extention tables are initialized.  This is used to compute folding
  // constants so the output type is CRCValue.
  CRCValue FastPowerOf2Remainder(int32_t n) const {
    CHECK_LE(0, n);
    const uint32_t bits = n % CHAR_BIT;
    const uint32_t bytes = n / CHAR_BIT;
    const Chunk r = this->ZeroExtensionFactor(bytes);
    return ChunkLow64(bits != 0 ? Z2MultiplyModP(r, bit_zero_table_[bits]) : r);
  }

  // Compute folding constant for 2^n.
  uint64_t FoldingConstant(int n) const {
    // We need to subtract 1 from n owing to fact that we use bit 0 for the
    // highest term and bit 127 for the lowest.  FastPowerOf2Remainder below
    // calls Z2MultiplyAndAdd, which multiply corresponding two halves of
    // two Chunks (bits [127:64] and [63:0]) in Z2 and adds the two products
    // together.  Since there is no carry in Z2, multiplying two 64-bit
    // numbers yields a 127-bit number. The sum of two products are also
    // 127-bit only.  So bits [126:0] hold the final result and bit 127 is 0.
    //
    // In our representation, we use lower number physical bits to represent
    // higher order terms.  We can still use Z2MultiplyAndAdd but there is
    // a catch.  Multiplying corresponding halves [64:127] and [0:64], and
    // then adding the results will have bits in [0:126], not [1:127] as
    // one would expect in this convention.  It would appear that the result
    // was shifted one bit towards right (i.e. higher power).  Hence we need
    // to adjust n.
    const uint64_t folding_constant = this->FastPowerOf2Remainder(n - 1);
    return folding_constant << (64 - this->degree_);
  }

  // Return a pair of folding constants in a chunk.  The constants are
  // in CRC bit order.
  Chunk FoldingConstantPair(int lo, int hi) const {
    // We swap the order intentionaly.  Since ChunkFromUint64s uses
    // normal bit ordering.
    return ChunkFromUint64s(FoldingConstant(hi), FoldingConstant(lo));
  }

  // Compute a scaling factor to extend a message by number of bytes specified
  // in length.  This is effectively finding x^(legnth*8) mod P(x).  This can
  // only be used after the zero extension tables are initialized.
  Chunk ZeroExtensionFactor(int64_t length) const;

  // Perform Barrett reduction on a polynomial R(x).
  inline Chunk BarrettReduction(Chunk r, Chunk mask, Chunk shift) const;

  // Specialized version depending on degree(P).  The main Barrett reduction
  // method calls these.
  inline Chunk BarrettReductionBelow64(Chunk r, Chunk mask, Chunk shift) const;
  inline Chunk BarrettReduction64(Chunk r, Chunk mask, Chunk shift) const;

  // Constants used to do byte shifts, indexed by bytes.
  Chunk byte_shift_table_[kChunkSize];

  // Constants used in fast CRC computation in CRC bit order.
  Chunk kFold8Ahead_;
  Chunk kFold4Ahead_;
  Chunk kFold2Ahead_;
  Chunk kFold1Ahead_;
  Chunk kFoldHalf_;

  // Folding constants for incomplete last chunk, indexed by size of
  // last chunk > 0.
  Chunk last_chunk_folding_table_[kChunkSize];

  // Folding constants for appending degree(P) zero bits after input.
  Chunk kPaddingZeros_;

  // CRC polynomial in normal bit order and explicit first term.
  Chunk poly_lo_;
  Chunk poly_hi_;

  // x^(2*degree(P))/P(x) in normal bit order.  This is used in Barrett
  // reduction as a scaled reciprocal of P(x).
  Chunk mu_lo_;
  Chunk mu_hi_;

  // Barrett reduction constants for multiplying two CRC values.
  Chunk mulmodp_mask_;
  Chunk mulmodp_shift_;

  // Barrett reduction constants for processing end of input.
  Chunk tail_reduction_mask_;
  Chunk tail_reduction_shift_;

  // Zero extension tables

  // One entry for each bit in input.  To extend CRC by i bits, multiply
  // CRC with element i.
  Chunk bit_zero_table_[CHAR_BIT];

  // One entry for each byte in input.  To extend CRC by i bytes, multiply
  // CRC with element i.
  Chunk small_zero_table_[kSmallZeroTableSize];

  // Big zero table is two dimensional.  We view the upper bits after
  // kLogSmallZeroTableSize as a number in base kZeroBase, which must be a
  // power of 2.  For each non-zero digit in this number, we look up the table
  // to find a multiplier for zero extension.
  // Currently this table has 14 rows of 15 Chunks, making it 3360 bytes.
  // We could shrink this to 1/2/ or 1/4 of that size by storing CRCValue
  // but it would slow us down because of repeated conversions between
  // integer and vector registers when computing zero-extension multiplier.
  Chunk big_zero_table_[kBigZeroTableRows][kZeroBase - 1];

  CRCHWPolyMul(const CRCHWPolyMul&) = delete;
  CRCHWPolyMul& operator=(const CRCHWPolyMul&) = delete;
};

// Align a pointer.
template <typename T>
inline T* AlignPointer(T* ptr, size_t alignment) {
  const uintptr_t mask = alignment - 1;
  DCHECK_EQ(0, (alignment & (alignment - 1)));
  return reinterpret_cast<T*>((reinterpret_cast<uintptr_t>(ptr) & ~mask));
}

// Determine if hardware can do multiplication in GF(2) at runtime.  This
// should be called only once.
void CheckFastPolyMul(bool* result) {
  // Function body defined in an architecture specific header.
  CheckFastPolyMulBody(result);
}

// Compute product of two CRC values a and b mod P(x).
template <class Base, typename CRCValue>
Chunk CRCHWPolyMul<Base, CRCValue>::Z2MultiplyModP(Chunk a, Chunk b) const {
  const Chunk r = Z2MultiplyAndAdd(a, b);
  return this->BarrettReduction(r, this->mulmodp_mask_, this->mulmodp_shift_);
}

template <class Base, typename CRCValue>
void CRCHWPolyMul<Base, CRCValue>::Extend(uint64_t* lo, uint64_t* hi,
                                          const void* bytes,
                                          int64_t length) const {
  if (length < kSmallInputSize) {
    Base::Extend(lo, hi, bytes, length);
    return;
  }

  if (length < kChunkSize) {
    this->ExtendSmall(lo, hi, bytes, length);
    return;
  }

  // Since input size is at least one Chunk, do unaligned read for first Chunk
  // but drop any bytes after alignment boundary so that all subsequent reads
  // are aligned.  We do not do an aligned read at the beginning because it
  // may read beyond a variable in caller and trigger an ASAN error.
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(bytes);
  const uint8_t* next_aligned = AlignPointer(ptr + kChunkSize, kChunkSize);
  const size_t bytes_consumed = next_aligned - ptr;
  const Chunk shift =
      PrepareVariableShift((kChunkSize - bytes_consumed) * CHAR_BIT);
  Chunk chunk = LeftShiftChunk(LoadUnalignedChunk(ptr, 0), shift);

  // Do zero-extension of previous CRC by length of this block.  We will XOR
  // that with CRC of this block to produce final result.
  CRCHWPolyMul<Base, CRCValue>::ExtendByZeroes(lo, hi, length);

  ptr += bytes_consumed;
  length -= bytes_consumed;
  const uint8_t* const bytes_end = ptr + length;
  const Chunk kZeros = ChunkFromUint64s(0, 0);
  Chunk chunk_0 = kZeros, chunk_1 = kZeros, chunk_2 = kZeros, chunk_3 = kZeros,
        chunk_4 = kZeros, chunk_5 = kZeros, chunk_6 = kZeros, chunk_7 = chunk;

  const Chunk kFold8Ahead = this->kFold8Ahead_;
  const Chunk kFold4Ahead = this->kFold4Ahead_;
  if (ptr <= bytes_end - 8 * kChunkSize) {
    // Z2MultiplyAndAdd has long latency on some architecture (6 cycles on
    // POWER).   To maximize vector instruction issuing bandwidth, interleave
    // 8 CRC streams and merge them at end of loop.
    while (ptr <= bytes_end - 8 * kChunkSize) {
      chunk_0 = ReadNextAndFold(ptr, 0, chunk_0, kFold8Ahead);
      chunk_1 = ReadNextAndFold(ptr, 1, chunk_1, kFold8Ahead);
      chunk_2 = ReadNextAndFold(ptr, 2, chunk_2, kFold8Ahead);
      chunk_3 = ReadNextAndFold(ptr, 3, chunk_3, kFold8Ahead);
      chunk_4 = ReadNextAndFold(ptr, 4, chunk_4, kFold8Ahead);
      chunk_5 = ReadNextAndFold(ptr, 5, chunk_5, kFold8Ahead);
      chunk_6 = ReadNextAndFold(ptr, 6, chunk_6, kFold8Ahead);
      chunk_7 = ReadNextAndFold(ptr, 7, chunk_7, kFold8Ahead);
      ptr += 8 * kChunkSize;
    }

    // Merge 8 chunks into 4.
    chunk_4 ^= Z2MultiplyAndAdd(chunk_0, kFold4Ahead);
    chunk_5 ^= Z2MultiplyAndAdd(chunk_1, kFold4Ahead);
    chunk_6 ^= Z2MultiplyAndAdd(chunk_2, kFold4Ahead);
    chunk_7 ^= Z2MultiplyAndAdd(chunk_3, kFold4Ahead);
  }

  // CRC state in chunk_4 .. chunk_7.

  if (ptr <= bytes_end - 4 * kChunkSize) {
    chunk_4 = ReadNextAndFold(ptr, 0, chunk_4, kFold4Ahead);
    chunk_5 = ReadNextAndFold(ptr, 1, chunk_5, kFold4Ahead);
    chunk_6 = ReadNextAndFold(ptr, 2, chunk_6, kFold4Ahead);
    chunk_7 = ReadNextAndFold(ptr, 3, chunk_7, kFold4Ahead);
    ptr += 4 * kChunkSize;
  }

  // Merge 4 chunks into 2.
  const Chunk kFold2Ahead = this->kFold2Ahead_;
  chunk_6 ^= Z2MultiplyAndAdd(chunk_4, kFold2Ahead);
  chunk_7 ^= Z2MultiplyAndAdd(chunk_5, kFold2Ahead);

  // CRC state in chunk_6 .. chunk_7.

  if (ptr <= bytes_end - 2 * kChunkSize) {
    chunk_6 = ReadNextAndFold(ptr, 0, chunk_6, kFold2Ahead);
    chunk_7 = ReadNextAndFold(ptr, 1, chunk_7, kFold2Ahead);
    ptr += 2 * kChunkSize;
  }

  // Merge 2 chunks into 1.
  const Chunk kFold1Ahead = this->kFold1Ahead_;
  chunk_7 ^= Z2MultiplyAndAdd(chunk_6, kFold1Ahead);
  chunk = chunk_7;

  // CRC state in chunk

  if (ptr <= bytes_end - kChunkSize) {
    chunk = ReadNextAndFold(ptr, 0, chunk, kFold1Ahead);
    ptr += kChunkSize;
  }

  this->ExtendTail(lo, hi, chunk, ptr, bytes_end - ptr);
}

template <class Base, typename CRCValue>
void CRCHWPolyMul<Base, CRCValue>::ExtendSmall(uint64_t* lo, uint64_t* hi,
                                               const void* bytes,
                                               int64_t length) const {
  Chunk c;
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(bytes);

  if (kAddressChecking) {
    // This is free of undefined behaviour but significantly slower than
    // doing a full Chunk read and zeroing the undefined parts.
    memset(&c, 0, sizeof(Chunk));
    uint8_t* chunk_ptr = reinterpret_cast<uint8_t*>(&c) + kChunkSize - length;
    memcpy(chunk_ptr, ptr, length);
  } else {
    const uint8_t* aligned_ptr = AlignPointer(ptr, kChunkSize);
    const uint8_t* aligned_end = AlignPointer(ptr + length - 1, kChunkSize);

    if (aligned_ptr == aligned_end) {
      // All input in single trunk.
      const Chunk right_shift = this->byte_shift_table_[ptr - aligned_ptr];
      c = RightByteShiftChunk(LoadAlignedChunk(aligned_ptr, 0), right_shift);
    } else {
      // Input spans two chunks. Just do an unaligned load.
      c = LoadUnalignedChunk(ptr, 0);
    }
    const Chunk left_shift = this->byte_shift_table_[kChunkSize - length];
    c = LeftByteShiftChunk(c, left_shift);
  }

  static_assert(kChunkSize <= kSmallZeroTableSize, "zero table too small");
  const Chunk multiplier = this->small_zero_table_[length];
  *lo = ChunkLow64(this->Z2MultiplyModP(ChunkFromUint64(*lo), multiplier));

  this->ExtendTail(lo, hi, c, nullptr, 0);
}

// Finish CRC extension using base CRC implementation.  We are given a
// chunk containing the reduced input and the last remaining bytes in the
// tail that do not fit in a chunk.
template <class Base, typename CRCValue>
void CRCHWPolyMul<Base, CRCValue>::ExtendTail(uint64_t* lo, uint64_t* hi,
                                              Chunk chunk, const uint8_t* bytes,
                                              int length) const {
  CHECK_LT(length, kChunkSize);

  // If there are remaining bytes, concatenate zeroes, the chunks and the
  // remaining bytes into two chunks and reduce that into one.
  if (length > 0) {
    // Since original input size is at least a Chunk, it is safe to do
    // an unaligned full-Chunk read that ends at the end of input.
    const Chunk tail = (LoadUnalignedChunk(bytes + length - kChunkSize, 0) &
                        kLoadLeftMaskTable[kChunkSize - length]);
    chunk =
        Z2MultiplyAndAdd(chunk, this->last_chunk_folding_table_[length]) ^ tail;
  }

  // For degrees 32 and 64, we use Barret Reduction to compute the final
  // CRC.  Mutliply 2^(deg(P)) mod P to input.
  if (this->degree_ == 32 || this->degree_ == 64)
    chunk = Z2MultiplyAndAdd(chunk, this->kPaddingZeros_);

  // Use base class CRC to extend remaining data in the last chunk.
  uint64_t lo_new = 0;
  if (this->degree_ > kChunkBitSize / 4) {
    if (this->degree_ != 64) {
      Base::Extend(&lo_new, hi, &chunk, kChunkSize);
      *lo ^= lo_new;
    } else {
      chunk = this->BarrettReduction64(chunk, this->tail_reduction_mask_,
                                       this->tail_reduction_shift_);
      *lo ^= ChunkLow64(chunk);
    }
  } else {
    // Further reduce the last chunk into half if degree is 1/4 of
    // kChunkBitSize or less.  After reduction, the second half of result
    // holds data that compute CRC of the original message.
    Chunk first_half = ChunkUnpackFirstHalf(chunk);
    chunk ^= Z2MultiplyAndAdd(first_half, this->kFoldHalf_);
    if (this->degree_ != 32) {
      Base::Extend(&lo_new, hi,
                   reinterpret_cast<uint8_t*>(&chunk) + kChunkSize / 2,
                   kChunkSize / 2);
      *lo ^= lo_new;
    } else {
      // Move high half into low half.
      chunk = RightByteShiftChunk(chunk, this->byte_shift_table_[8]);
      chunk = this->BarrettReductionBelow64(chunk, this->tail_reduction_mask_,
                                            this->tail_reduction_shift_);
      *lo ^= ChunkLow64(chunk);
    }
  }
}

// Compute a scaling factor to extend a message by number of bytes specified
// in length.  This is effectively finding x^(legnth*8) mod P(x).  This can
// only be used after the zero extension tables are initialized.
template <class Base, typename CRCValue>
Chunk CRCHWPolyMul<Base, CRCValue>::ZeroExtensionFactor(int64_t length) const {
  if (length < kSmallZeroTableSize) {
    // For small input size, just look up a precomputed factor.
    return this->small_zero_table_[length];
  } else {
    // For big input size, scan bits in length and multiply scaling factors
    // for bits together.
    constexpr int64_t kOne(1);
    constexpr int64_t small_input_mask = (kOne << kLogSmallZeroTableSize) - 1;
    const int64_t small_extension = length & small_input_mask;
    Chunk multiplier = this->small_zero_table_[small_extension];

    // Treat top bits of length as a number in base kZeroBase.
    uint64_t z = static_cast<uint64_t>(length) >> kLogSmallZeroTableSize;
    for (size_t i = 0; z != 0 && i < kBigZeroTableRows; i++, z /= kZeroBase) {
      const size_t digit = z % kZeroBase;
      if (digit != 0) {
        multiplier =
            Z2MultiplyModP(multiplier, this->big_zero_table_[i][digit - 1]);
      }
    }
    return multiplier;
  }
}

template <class Base, typename CRCValue>
void CRCHWPolyMul<Base, CRCValue>::ExtendByZeroes(uint64_t* lo, uint64_t* hi,
                                                  int64_t length) const {
  // Let M(x) be a message, P(x) be a polynomial of degree n,
  //     C(X) = (M(x) * x^n) mod P(x) be the current CRC and
  //     M'(x) = M(x) * x^L be input extended by L bits.
  // New CRC C'(x) = (M'(x) * x^L) mod P(x)
  //               = ((M(x) * x^L * x^n) mod P(x)
  //               = ((M(x) * x^n) * x^L) mod P(x)
  //               = (C(x) * x^L) mod P(x)
  const Chunk multiplier = this->ZeroExtensionFactor(length);
  *lo = ChunkLow64(this->Z2MultiplyModP(ChunkFromUint64(*lo), multiplier));
}

// Barrett reduction is used to compute C(x) = R(x) mod P(x) efficiently in Z2,
// where degree of P is n and degree of R is at most 2n-1.  To perform Barrett
// reduction, we need to compute another polynomial Mu(x), which is
// floor(2^(2n) / P(x)).  The reduction is done as:
//
// T1(X) = floor(R(x) / x^n) * Mu(x)
// T2(x) = floor(T1(x) / x^n) * P(X)
// C(x) = (R(x) - T(2)) mod x^n

// The following three routines implement Barrett reduction, which is used
// to compute R(X) mod P(x).
//
// r: R(X), polynomial to be divided by P(x).
// degree_r: degree of R(X)
// mask: bit mask of (degree(R) - degree(P)) 1's, starting from bit 0.
// shift: degree(R) - degree(P)

// Return r mod poly_{lo,hi}_
template <class Base, typename CRCValue>
inline Chunk CRCHWPolyMul<Base, CRCValue>::BarrettReduction(Chunk r, Chunk mask,
                                                            Chunk shift) const {
  // Eliminte if-statement below statically if possible.
  if (kCRCValueBitSize < 64 || this->degree_ < 64) {
    return this->BarrettReductionBelow64(r, mask, shift);
  } else {
    CHECK_EQ(64, this->degree_);
    return this->BarrettReduction64(r, mask, shift);
  }
}

// Special version of BarrettReduction for degree(P) below 64.
template <class Base, typename CRCValue>
inline Chunk CRCHWPolyMul<Base, CRCValue>::BarrettReductionBelow64(
    Chunk r, Chunk mask, Chunk shift) const {
  // Since coefficient of term x^(2n-1) is at bit 0, division by x^n
  // is done by clearing bits after bit-n using a precomputed mask.
  const Chunk t1 = Z2MultiplyAndAdd(r & mask, this->mu_lo_);
  const Chunk t2 = Z2MultiplyAndAdd(t1 & mask, this->poly_lo_);

  // mod x^n is done by right shift so term x^(n-1) is at bit 0.
  return RightShiftChunk(r ^ t2, shift);
}

// Special version of BarrettReduction for degree(P) equals 64.
template <class Base, typename CRCValue>
inline Chunk CRCHWPolyMul<Base, CRCValue>::BarrettReduction64(
    Chunk r, Chunk mask, Chunk shift) const {
  // If degree is 64, mu and poly is 65 bits, we cannot use a single
  // Z2MultiplyAndAdd per multiplication.
  const Chunk t1_lo = Z2MultiplyAndAdd(r & mask, this->mu_lo_);
  const Chunk t1_hi = Z2MultiplyAndAdd(r & mask, this->mu_hi_);
  const Chunk shift_8 = this->byte_shift_table_[8];
  const Chunk t1 = t1_lo ^ LeftByteShiftChunk(t1_hi, shift_8);
  const Chunk t2_lo = Z2MultiplyAndAdd(t1 & mask, this->poly_lo_);
  const Chunk t2_hi = Z2MultiplyAndAdd(t1 & mask, this->poly_hi_);
  const Chunk t2 = t2_lo ^ LeftByteShiftChunk(t2_hi, shift_8);

  // mod x^n is done by right shift so term x^(n-1) is at bit 0.
  return RightShiftChunk(r ^ t2, shift);
}

template <class Base, typename CRCValue>
void CRCHWPolyMul<Base, CRCValue>::InitTables() {
  // Shift consants.
  for (int i = 0; i < kChunkSize; i++) {
    this->byte_shift_table_[i] = PrepareVariableShift(i * CHAR_BIT);
  }

  // Constants for Barrett reduction, we need to compute them early.
  const absl::uint128 full_poly = (this->poly_ << 1) | 1;
  this->poly_lo_ = ChunkFromUint64(absl::Uint128Low64(full_poly));
  this->poly_hi_ = ChunkFromUint64(absl::Uint128High64(full_poly));
  const absl::uint128 mu =
      PowerOf2Div(this->degree_ * 2, this->poly_, this->degree_);
  this->mu_lo_ = ChunkFromUint64(absl::Uint128Low64(mu));
  this->mu_hi_ = ChunkFromUint64(absl::Uint128High64(mu));

  // We can use Barrett reduction from this point.

  // Set up Barrett reduction constants for Z2MultiplyModP
  this->mulmodp_mask_ = ChunkFromUint128(FirstNBits(this->degree_) >> 1);
  this->mulmodp_shift_ = PrepareVariableShift(this->degree_ - 1);

  // We can use Z2MultiplyModP from this point.

  // Fill in bit zero table.
  const Chunk crc_of_one = this->SlowPowerOf2Remainder(0);
  const Chunk bit_step = this->SlowPowerOf2Remainder(1);
  Chunk extension = crc_of_one;
  for (size_t i = 0; i < CHAR_BIT; i++) {
    this->bit_zero_table_[i] = extension;
    extension = Z2MultiplyModP(extension, bit_step);
  }

  // Fill in byte zero table.
  const Chunk byte_step = this->SlowPowerOf2Remainder(CHAR_BIT);
  extension = crc_of_one;
  for (size_t i = 0; i < kSmallZeroTableSize; i++) {
    this->small_zero_table_[i] = extension;
    extension = Z2MultiplyModP(extension, byte_step);
  }

  for (size_t i = 0; i < kBigZeroTableRows; i++) {
    const Chunk step = extension;
    for (size_t j = 0; j < kZeroBase - 1; j++) {
      this->big_zero_table_[i][j] = extension;
      extension = Z2MultiplyModP(extension, step);
    }
  }

  // We can use fast 2^n remainder method after this point.

  // Set up Barrett reduction constants for end of input.
  this->tail_reduction_mask_ = ChunkFromUint128(FirstNBits(this->degree_));
  this->tail_reduction_shift_ = PrepareVariableShift(this->degree_);

  // Compute constants used in folding.
  this->kFold8Ahead_ =
      this->FoldingConstantPair(8 * kChunkBitSize + 64, 8 * kChunkBitSize);
  this->kFold4Ahead_ =
      this->FoldingConstantPair(4 * kChunkBitSize + 64, 4 * kChunkBitSize);
  this->kFold2Ahead_ =
      this->FoldingConstantPair(2 * kChunkBitSize + 64, 2 * kChunkBitSize);
  this->kFold1Ahead_ =
      this->FoldingConstantPair(1 * kChunkBitSize + 64, 1 * kChunkBitSize);
  // We can reduce the final Chunk into a half if degree is 32 or less.
  if (this->degree_ <= kChunkBitSize / 4) {
    this->kFoldHalf_ =
        this->FoldingConstantPair(3 * kChunkBitSize / 4, kChunkBitSize / 2);
  }

  for (int i = 1; i < kChunkSize; i++) {
    this->last_chunk_folding_table_[i] =
        this->FoldingConstantPair(i * CHAR_BIT + 64, i * CHAR_BIT);
  }

  this->kPaddingZeros_ =
      this->FoldingConstantPair(this->degree_ + 64, this->degree_);

  // Call InitTables() of base class only after we have initialized
  // our tables due to a subtle dependency in initialization.
  // The base class calls virtual methods Extend and ExtendByZero during
  // initialization.
  Base::InitTables();
}

}  // end anonymous namespace

// Return a newly created CRCHWPolyMul object if we can use fast polynomial
// multiplication instructions for a given polynomial.  Return nullptr
// otherwise.
CRCImpl* TryNewCRCHWPolyMul(uint64_t lo, uint64_t hi, int degree) {
  // Check that we have the required vector instructions.
  static bool has_fast_polynomial_multiply;
  static absl::once_flag once;

  absl::call_once(once, &CheckFastPolyMul, &has_fast_polynomial_multiply);

  // Reject trivial polynomials P(x) = c to simplify code for degree >= 32.
  if (has_fast_polynomial_multiply && degree > 0) {
    if (degree <= 32) {
      return new CRCHWPolyMul<CRC32, uint32_t>();
    }
    if (degree <= 64) {
      return new CRCHWPolyMul<CRC64, uint64_t>();
    }
  }

  return nullptr;
}

}  // end namespace crc_internal
#else   // CAN_USE_FAST_POLY_MUL
namespace crc_internal {

CRCImpl* TryNewCRCHWPolyMul(uint64_t lo, uint64_t hi, int degree) {
  return nullptr;  // Fast polynomial multiplication not supported
}

}  // end namespace crc_internal
#endif  // CAN_USE_FAST_POLY_MUL
