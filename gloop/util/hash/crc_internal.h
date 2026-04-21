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

#ifndef THIRD_PARTY_GLOOP_UTIL_HASH_CRC_INTERNAL_H_
#define THIRD_PARTY_GLOOP_UTIL_HASH_CRC_INTERNAL_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/base/internal/raw_logging.h"
#include "absl/base/optimization.h"
#include "absl/numeric/int128.h"
#include "gloop/util/hash/crc.h"

namespace crc_internal {

// Constants
#if defined(__i386__) || defined(__x86_64__)
constexpr bool kNeedAlignedLoads = false;
#else
constexpr bool kNeedAlignedLoads = true;
#endif

constexpr uint64_t UINT64_ZERO = 0;  // a 64-bit zero
constexpr uint64_t UINT64_ONE = 1;   // a 64-bit 1

// The B() macro sets the bit corresponding to X**(_x) in the polynomial
#define B(_x) (UINT64_ONE << ((_x) < 64 ? (63 - (_x)) : (127 - (_x))))

// Used to initialize polynomials.
// The redundant tests on _len are to avoid warnings from the
// compiler about inappropriate shift lengths.   These shifts
// occur on not-taken branch of the ?: in some cases.
#define DEF_POLY(_h, _l, _len)                                            \
  {((_len) <= 64 ? (_l) >> ((_len) <= 64 ? 64 - (_len) : 0)               \
    : (_len) == 128                                                       \
        ? (_h)                                                            \
        : ((_h) >> ((_len) > 64 ? 128 - (_len) : 0)) |                    \
              ((_l) << ((_len) > 64 && (_len) < 128 ? (_len) - 64 : 0))), \
   ((_len) <= 64    ? 0                                                   \
    : (_len) == 128 ? (_l)                                                \
                    : (_l) >> ((_len) > 64 ? 128 - (_len) : 0)),          \
   (_len)}

// When extending an input with a string of zeroes, if the number of zeroes is
// less than 2**SMALL_BITS, a normal Extend is done, rather than a polynomial
// multiplication.
constexpr int SMALL_BITS = 8;

// When the number of zeroes is not less than 2**SMALL_BITS, we express the
// number of zeroes as a number in base ZEROES_BASE. By pre-computing the zero
// extensions for all possible components of such an expression (numbers in a
// form a*ZEROES_BASE**b), we can calculate the resulting extension by
// multiplying the extensions for individual components using
// log_{ZEROES_BASE}(num_zeroes) polynomial multiplications.
// The tables of zero extensions contain
// (ZEROES_BASE - 1) * (log_{ZEROES_BASE}(64 - SMALL_BITS)) entries.
constexpr int ZEROES_BASE_LG = 4;                   // log_2(ZEROES_BASE)
constexpr int ZEROES_BASE = (1 << ZEROES_BASE_LG);  // must be a power of 2

// Prefetch constants used in some Extend() implementations
constexpr int kPrefetchHorizon = ABSL_CACHELINE_SIZE * 4;  // Prefetch this far
static_assert(kPrefetchHorizon >= 64, "CRCPrefetchHorizon less than loop len");

// We require the Scramble() function:
//  - to be reversible (Unscramble() must exist)
//  - to be non-linear in the polynomial's Galois field (so the CRC of a
//    scrambled CRC is not linearly affected by the scrambled CRC, even if
//    using the same polynomial)
//  - not to be its own inverse.  Preferably, if X=Scramble^N(X) and N!=0, then
//    N is large.
//  - to be fast.
//  - not to change once defined.
// We introduce non-linearity in two ways:
//     Addition of a constant.
//         - The carries introduce non-linearity; we use bits of an irrational
//           (phi) to make it unlikely that we introduce no carries.
//     Rotate by a constant number of bits.
//         - We use floor(degree/2)+1, which does not divide the degree, and
//           splits the bits nearly evenly, which makes it less likely the
//           halves will be the same or one will be all zeroes.
// We do both things to improve the chances of non-linearity in the face of
// bit patterns with low numbers of bits set, while still being fast.
// Below is the constant that we add.  The bits are the first 128 bits of the
// fractional part of phi, with a 1 ored into the bottom bit to maximize the
// cycle length of repeated adds.
constexpr uint64_t kScrambleHi = (static_cast<uint64_t>(0x4f1bbcdcU) << 32) |
                                 static_cast<uint64_t>(0xbfa53e0aU);
constexpr uint64_t kScrambleLo = (static_cast<uint64_t>(0xf9ce6030U) << 32) |
                                 static_cast<uint64_t>(0x2e76e41bU);

#if !defined(__x86_64__) || !defined(__GNUC__)
typedef absl::uint128 CrcUint128;

inline CrcUint128 MakeCrcUint128(uint64_t top, uint64_t bottom) {
  return absl::MakeUint128(top, bottom);
}

inline uint64_t CrcUint128Low64(const CrcUint128 v) {
  return absl::Uint128Low64(v);
}

inline uint64_t CrcUint128High64(const CrcUint128 v) {
  return absl::Uint128High64(v);
}

inline absl::uint128 CrcUint128ToUint128(const CrcUint128 v) { return v; }

#else
// Avoid placing emmintrin.h into our namespace

}  // end namespace crc_internal

#include <emmintrin.h>

namespace crc_internal {

// uint128 using sse2 ops for shifts and logical ops. Arithmetic ops aren't
// supported for 128-bit integers in sse2; such ops are faster using uint128.
class CrcUint128 {
 public:
  CrcUint128();
  CrcUint128(uint32_t bottom);
  CrcUint128(uint64_t bottom);
  explicit CrcUint128(absl::uint128 v) {
    *this = CrcUint128(absl::Uint128High64(v), absl::Uint128Low64(v));
  }

  // Shifts and logical ops.
  CrcUint128 operator<<(int) const;
  CrcUint128 operator>>(int) const;
  CrcUint128 operator&(const CrcUint128& b) const;
  CrcUint128 operator|(const CrcUint128& b) const;
  CrcUint128 operator^(const CrcUint128& b) const;

  void operator<<=(int amount) { *this = *this << amount; }
  void operator>>=(int amount) { *this = *this >> amount; }
  void operator&=(const CrcUint128& b) { *this = *this & b; }
  void operator|=(const CrcUint128& b) { *this = *this | b; }
  void operator^=(const CrcUint128& b) { *this = *this ^ b; }

  friend CrcUint128 MakeCrcUint128(uint64_t top, uint64_t bottom);
  friend uint64_t CrcUint128Low64(const CrcUint128& v);
  friend uint64_t CrcUint128High64(const CrcUint128& v);

 private:
  __m128i val_;
  explicit CrcUint128(__m128i v) : val_(v) {}

  CrcUint128(uint64_t top, uint64_t bottom);

  absl::uint128 ToUint128() const {
    return absl::MakeUint128(CrcUint128High64(*this), CrcUint128Low64(*this));
  }

  // Not implemented, just declared for catching automatic type conversions.
  CrcUint128(int bottom);
  CrcUint128(uint8_t);
  CrcUint128(uint16_t);
  CrcUint128(float v);
  CrcUint128(double v);
};

inline CrcUint128 MakeCrcUint128(uint64_t top, uint64_t bottom) {
  return CrcUint128(top, bottom);
}

inline uint64_t CrcUint128Low64(const CrcUint128& v) {
  return _mm_cvtsi128_si64(v.val_);
}

inline uint64_t CrcUint128High64(const CrcUint128& v) {
  return _mm_cvtsi128_si64(_mm_srli_si128(v.val_, 8));
}

inline absl::uint128 CrcUint128ToUint128(const CrcUint128& v) {
  return absl::MakeUint128(CrcUint128High64(v), CrcUint128Low64(v));
}

inline CrcUint128::CrcUint128() {
  val_ = _mm_cvtsi64_si128(static_cast<uint64_t>(0));
}

inline CrcUint128::CrcUint128(uint64_t top, uint64_t bottom) {
  val_ = _mm_unpacklo_epi64(_mm_cvtsi64_si128(bottom), _mm_cvtsi64_si128(top));
}

inline CrcUint128::CrcUint128(uint64_t bottom) {
  val_ = _mm_cvtsi64_si128(bottom);
}

inline CrcUint128::CrcUint128(uint32_t bottom) {
  val_ = _mm_cvtsi64_si128(static_cast<uint64_t>(bottom));
}

inline CrcUint128 CrcUint128::operator&(const CrcUint128& b) const {
  return CrcUint128(_mm_and_si128(val_, b.val_));
}

inline CrcUint128 CrcUint128::operator|(const CrcUint128& b) const {
  return CrcUint128(_mm_or_si128(val_, b.val_));
}

inline CrcUint128 CrcUint128::operator^(const CrcUint128& b) const {
  return CrcUint128(_mm_xor_si128(val_, b.val_));
}

inline CrcUint128 CrcUint128::operator<<(int amount) const {
  // Some shifts can be done directly. Most of the shifts for CRCs are
  // constants, so the compiler will eliminate all the extra dead code.
  if (amount == 8) {
    return CrcUint128(_mm_slli_si128(val_, 1));
  } else if (amount == 16) {
    return CrcUint128(_mm_slli_si128(val_, 2));
  } else if (amount == 24) {
    return CrcUint128(_mm_slli_si128(val_, 3));
  } else if (amount == 32) {
    return CrcUint128(_mm_slli_si128(val_, 4));
  } else if (amount == 40) {
    return CrcUint128(_mm_slli_si128(val_, 5));
  } else if (amount == 48) {
    return CrcUint128(_mm_slli_si128(val_, 6));
  } else if (amount == 56) {
    return CrcUint128(_mm_slli_si128(val_, 7));
  } else if (amount == 64) {
    return CrcUint128(_mm_slli_si128(val_, 8));
  }

  return CrcUint128(CrcUint128ToUint128(*this) << amount);
}

inline CrcUint128 CrcUint128::operator>>(int amount) const {
  if (amount == 8) {
    return CrcUint128(_mm_srli_si128(val_, 1));
  } else if (amount == 16) {
    return CrcUint128(_mm_srli_si128(val_, 2));
  } else if (amount == 24) {
    return CrcUint128(_mm_srli_si128(val_, 3));
  } else if (amount == 32) {
    return CrcUint128(_mm_srli_si128(val_, 4));
  } else if (amount == 40) {
    return CrcUint128(_mm_srli_si128(val_, 5));
  } else if (amount == 48) {
    return CrcUint128(_mm_srli_si128(val_, 6));
  } else if (amount == 56) {
    return CrcUint128(_mm_srli_si128(val_, 7));
  } else if (amount == 64) {
    return CrcUint128(_mm_srli_si128(val_, 8));
  }

  return CrcUint128(CrcUint128ToUint128(*this) >> amount);
}
#endif

class CRCImpl : public CRC {  // Implemention of the abstract class CRC
 public:
  typedef absl::uint128 Uint128By256[256];

  CRCImpl() {}
  ~CRCImpl() override { ABSL_RAW_CHECK(!this->is_default_, ""); }

  // The internal version of CRC::New().
  static CRCImpl* NewInternal(uint64_t lo, uint64_t hi, int degree,
                              size_t roll_length);

  void Empty(uint64_t* lo, uint64_t* hi) const override;

  virtual void InitTables() = 0;

  // Fill in a table for updating a CRC by one word of 'word_size' bytes
  // [last_lo, last_hi] contains the answer if the last bit in the word
  // is set.
  static void FillWordTable(absl::uint128 poly, absl::uint128 last,
                            int word_size, Uint128By256* t);

  // Build the table for extending by zeroes, returning the number of entries.
  // For a in {1, 2, ..., ZEROES_BASE-1}, b in {0, 1, 2, 3, ...},
  // entry j=a-1+(ZEROES_BASE-1)*b
  // contains a polynomial Pi such that multiplying
  // a CRC by Pi mod P, where P is the CRC polynomial, is equivalent to
  // appending a*2**(ZEROES_BASE_LG*b + SMALL_BITS) zero bytes to the original
  // string.
  static int FillZeroesTable(absl::uint128 poly, int degree, Uint128By256* t);

  // Fill in a rolling CRC table for impl, which must be sufficiently
  // initialized for ExtendByZeroes.
  static void FillRollTable(CRCImpl* impl, Uint128By256* t);

  bool is_default_;     // This CRC is one of the default CRCs
  CRCImpl* next_;       // next entry in cache
  size_t roll_length_;  // length of window in rolling CRC

  int degree_;           // bits in the CRC
  int scramble_rotate_;  // bits to rotate by in scramble function

  absl::uint128 poly_;  // The CRC of the empty string

 private:
  CRCImpl(const CRCImpl&) = delete;
  CRCImpl& operator=(const CRCImpl&) = delete;
};

// This is the 32-bit implementation.  It handles all sizes from 8 to 32.
class CRC32 : public CRCImpl {
 public:
  CRC32() {}
  virtual ~CRC32() {}

  virtual void Extend(uint64_t* lo, uint64_t* hi, const void* bytes,
                      int64_t length) const;
  virtual void ExtendByZeroes(uint64_t* lo, uint64_t* hi, int64_t length) const;
  virtual void Scramble(uint64_t* lo, uint64_t* hi) const;
  virtual void Unscramble(uint64_t* lo, uint64_t* hi) const;
  virtual void Roll(uint64_t* lo, uint64_t* hi, uint8_t o_byte,
                    uint8_t i_byte) const;

  virtual void InitTables();

 private:
  uint32_t table0_[256];  // table of byte extensions
  uint32_t roll_[256];    // table of byte roll values
  uint32_t zeroes_[256];  // table of zero extensions
  enum { kStride = 4 };
  typedef uint32_t DataWord;

  // table of DataWord extensions shifted by kStride - 1 words
  uint32_t table_[sizeof(DataWord)][256];

  CRC32(const CRC32&) = delete;
  CRC32& operator=(const CRC32&) = delete;
};

// This is the 64-bit implementation.  It handles all sizes from 33 to 64.
class CRC64 : public CRCImpl {
 public:
  CRC64() {};
  virtual ~CRC64() {};

  virtual void Extend(uint64_t* lo, uint64_t* hi, const void* bytes,
                      int64_t length) const;
  virtual void ExtendByZeroes(uint64_t* lo, uint64_t* hi, int64_t length) const;
  virtual void Scramble(uint64_t* lo, uint64_t* hi) const;
  virtual void Unscramble(uint64_t* lo, uint64_t* hi) const;
  virtual void Roll(uint64_t* lo, uint64_t* hi, uint8_t o_byte,
                    uint8_t i_byte) const;

  virtual void InitTables();

 private:
  uint64_t table0_[256];  // table of byte extensions
  uint64_t roll_[256];    // table of byte roll values
  uint64_t zeroes_[256];  // table of zero extensions
  enum { kStride = 4 };
  typedef uint64_t DataWord;

  // table of DataWord extensions shifted by kStride - 1 words
  uint64_t table_[sizeof(DataWord)][256];

  CRC64(const CRC64&) = delete;
  CRC64& operator=(const CRC64&) = delete;
};

// This is the 128-bit implementation.  It handles all sizes from 65 to 128.
class CRC128 : public CRCImpl {
 public:
  CRC128() {}
  virtual ~CRC128() {}

  virtual void Extend(uint64_t* lo, uint64_t* hi, const void* bytes,
                      int64_t length) const;
  virtual void ExtendByZeroes(uint64_t* lo, uint64_t* hi, int64_t length) const;
  virtual void Scramble(uint64_t* lo, uint64_t* hi) const;
  virtual void Unscramble(uint64_t* lo, uint64_t* hi) const;
  virtual void Roll(uint64_t* lo, uint64_t* hi, uint8_t o_byte,
                    uint8_t i_byte) const;

  virtual void InitTables();

 private:
  CrcUint128 table0_[256];     // table of byte extensions
  CrcUint128 roll_[256];       // table of byte roll values
  absl::uint128 zeroes_[256];  // table of zero extensions
  enum { kStride = 4 };
  typedef uint32_t DataWord;

  // table of DataWord extensions shifted by kStride - 1 words
  CrcUint128 table_[sizeof(DataWord)][256];

  CRC128(const CRC128&) = delete;
  CRC128& operator=(const CRC128&) = delete;
};

// Helpers

// Return a bit mask containing len 1-bits.
// Requires 0 < len <= sizeof(T)
template <typename T>
T MaskOfLength(int len) {
  // shift 2 by len-1 rather than 1 by len because shifts of wordsize
  // are undefined.
  return (T(2) << (len - 1)) - 1;
}

// Rotate low-order "width" bits of "in" right by "r" bits,
// setting other bits in word to arbitrary values.
template <typename T>
T RotateRight(T in, int width, int r) {
  return (in << (width - r)) | ((in >> r) & MaskOfLength<T>(width - r));
}

// RoundUp<N>(p) returns the lowest address >= p aligned to an N-byte
// boundary.  Requires that N is a power of 2.
template <int alignment>
const uint8_t* RoundUp(const uint8_t* p) {
  static_assert((alignment & (alignment - 1)) == 0, "alignment is not 2^n");
  constexpr uintptr_t mask = alignment - 1;
  const uintptr_t as_uintptr = reinterpret_cast<uintptr_t>(p);
  return reinterpret_cast<const uint8_t*>((as_uintptr + mask) & ~mask);
}

// Return a newly created CRC32AcceleratedX86ARMCombined if we can use Intel's
// or ARM's CRC acceleration for a given polynomial.  Return nullptr otherwise.
CRCImpl* TryNewCRC32AcceleratedX86ARMCombined(uint64_t lo, uint64_t hi,
                                              int degree);

// Return a newly created CRCHWPolyMul object if we can use fast polynomial
// multiplication instructions for a given polynomial.  Return nullptr
// otherwise.
CRCImpl* TryNewCRCHWPolyMul(uint64_t lo, uint64_t hi, int degree);

// Return all possible hardware accelerated implementations. For testing only.
std::vector<std::unique_ptr<CRCImpl>> NewCRC32AcceleratedX86ARMCombinedAll();

// Return a pointer to a standard poly or nullptr if not found.
const CRC::Poly* LookupStandardPolyByName(CRC::CRCName name);

}  // end namespace crc_internal

#endif  // THIRD_PARTY_GLOOP_UTIL_HASH_CRC_INTERNAL_H_
