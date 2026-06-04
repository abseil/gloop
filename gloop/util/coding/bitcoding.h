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

// FixedBitWidthArray added by Priyendra Deshwal

#ifndef THIRD_PARTY_GLOOP_UTIL_CODING_BITCODING_H_
#define THIRD_PARTY_GLOOP_UTIL_CODING_BITCODING_H_

#include <assert.h>

#include <algorithm>
#include <cstdint>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/log/check.h"
#include "absl/numeric/bits.h"
#include "absl/numeric/int128.h"
#include "gloop/util/endian/endian.h"
#ifdef __BMI2__
#include <immintrin.h>
#endif  // __BMI2__
#include <stddef.h>

#ifdef MEMORY_SANITIZER
#include <sanitizer/msan_interface.h>
#endif

#include "absl/base/optimization.h"
#include "gloop/base/uword.h"
#include "gloop/util/bits/bits.h"
#include "gloop/util/coding/coder.h"

/*
  BitEncoder         -- For encoding data into bit stream
  BitDecoder         -- For decoding data from bit stream
  FixedBitWidthArray -- A tightly-packed array of k-bit wide elements
*/

/* Class for encoding bit-level data into a memory buffer */
class BitEncoder {
 public:
  // Creates an empty BitEncoder with no room that is enlarged
  // (if necessary) when "BitEncoder::EnsureBits(N)" is called.
  BitEncoder();

  // Movable.
  BitEncoder(BitEncoder&& other) noexcept;
  BitEncoder& operator=(BitEncoder&& other) noexcept;

  BitEncoder(void* buf, size_t size);

  // Encoding routines. Note that these do not check bounds in opt mode.
  // nbits must be in [0, 32] for PutBits(), [0, 64] for PutBits64(), and [0,
  // 128] for PutBits128().
  void PutBits(uint32_t x, int nbits);
  void PutBits64(uint64_t x, int nbits);
  void PutBits128(absl::uint128 x, int nbits);
  // Use this if your cpu can find the rightmost 0 bit more quickly.
  void PutUnary(uint32_t x);  // Warning: x must be positive
  // Use this if your cpu can find the rightmost 1 bit more quickly.
  // On most x86 cpus this is faster.
  void PutInvertedUnary(uint32_t x);  // Warning: x must be positive
  // Will output at most kMaxGammaBits.
  void PutGamma(uint32_t x);  // Warning: x must be positive

  // PutVarInt() is unsafe on 32-bit values whose remainder may end up
  // being encoded in more than 32 bits. (If this may be a problem,
  // use a log_base that is a divisor of 32, or use PutVarInt64().)
  //
  // REQUIRES: log_base is positive
  //
  // Will output at most kMaxVarIntBits.
  void PutVarInt(int log_base, uint32_t x);
  // Will output at most kMaxVarInt64Bits.
  void PutVarInt64(int log_base, uint64_t x);
  void PutRice(int k, uint32_t x);
  void PutRice64(int k, uint64_t x);
  // ProgressiveRice is like rice coding, but shortens long codelengths (where
  // the unary prefix >= u_max) by progressively increasing k in steps of m.
  // Ideally u_max and m are chosen by experiment, but try u_max = 12 and m = 4.
  // See <link> for more information.
  void PutProgressiveRice(int k, int u_max, int m, uint32_t x);
  void PutProgressiveRice64(int k, int u_max, int m, uint64_t x);

  // Pad to byte boundary with boolean value of left_over_bit_value as pad.
  void Flush(int left_over_bit_value);
  // Pad to 64 bit boundary with boolean value of left_over_bit_value as pad.
  void Flush64(int left_over_bit_value);

  static const int kMaxGammaBits = 63;
  static const int kMaxVarIntBits = 64;  // For any log_base
  static const int kMaxVarInt64Bits = 128;
  // Return number of bits encoded so far
  uint64_t Bits() const {
    return static_cast<uint64_t>(encoder_.length()) * 8 + nbuf_;
  }

  // REQUIRES: BitEncoder was created with the 0-argument constructor
  // interface.
  //
  // This interface ensures that at least "N" more bits are available
  // in the underlying buffer by resizing the buffer (if necessary).
  //
  // Note that no bounds checking is done on any of the put routines,
  // so it is the client's responsibility to call EnsureBits() at
  // appropriate intervals to ensure that enough space is available
  // for the data being added.
  void EnsureBits(size_t nbits) {
    nbits += 64;                       // To account for buffered bits;
    encoder_.Ensure((nbits + 7) / 8);  // Round up
  }

  // Returns true if EnsureBits is allowed to be called on "this"
  bool ensure_allowed() const { return encoder_.ensure_allowed(); }

  // Reverse the n least significant bits of x and return them
  static uint32_t ReverseBits(int n, uint32_t x);

  // Returns the number of bits required to store the given number.
  static constexpr unsigned int BitsRequired(uint32_t x);
  static unsigned int BitsRequiredTableDriven(uint32_t x);
  static unsigned int BitsRequired64(uint64_t x);
  static unsigned int BitsRequiredWithRice(int k, uint32_t x);
  // Returns an upper bound on the number of bits required to store the given
  // number. (The exact number of bits is too expensive to compute.)
  static unsigned int MaxBitsRequiredWithProgressiveRice(int k, int u_max,
                                                         int m, uint32_t x);
  static unsigned int MaxBitsRequiredWithProgressiveRice64(int k, int u_max,
                                                           int m, uint64_t x);

  // Return floor(log2(x)).  Returns 0 for 0,1; 1 for 2,3; ...
  static unsigned int FloorLogBase2(uint64_t x);

  // Put "k" least significant bits of "x" into "str" and null-terminate.
  // The least-significant bit is emitted into "str[0]".  "str" must
  // have length at least "k+1".  Returns "str".
  static const char* UnparseLSBFirst(char* str, uint32_t x, int k);

  // Shifts by full word-length are undefined.
  // This routine gets the low "nbits" bits of "val" by masking instead.
  static uint64_t LowBits(uint64_t val, int nbits) {
    assert(nbits >= 0 && nbits <= 64);
#ifdef __BMI2__
    // Use bzhi explicitly, as forming a mask with arithmetic does not handle
    // the nbits=64 case (shl does nothing).
    uint64_t maskedValue = _bzhi_u64(val, nbits);
    DCHECK_EQ(maskedValue, val & mask_[nbits]) << nbits;
    return maskedValue;
#else   // !__BMI2__
    return val & mask_[nbits];
#endif  // !__BMI2__
  }

  // Return ptr to start of encoded data.
  const char* base() const { return encoder_.base(); }

  // Clear the encoder
  void Clear() {
    encoder_.clear();
    buf_ = 0;
    nbuf_ = 0;
  }

  // Transfer all bits to target. Flush and clear the source.
  // Caller needs to invoke EnsureBits() if necessary.
  void TransferBitsTo(BitEncoder* target);

 private:
  Encoder encoder_;  // Underlying byte-level encoder
  // buf_ and nbuf_ satisfy the following invariant: every method must leave
  // 0 <= nbuf_ < 64. If nbuf_ would be 64, buf_ must be written to encoder_.
  uint64_t buf_ = 0;  // Buffered bits that have not been flushed to encoder
  int nbuf_ = 0;      // Number of buffered bits

  friend class BitCoderInitializer;  // Initializer class
  static void Initialize();

  // Internal PutGamma routine: used by "PutGamma" and for table construction
  void InternalPutGamma(uint32_t x);
  // Internal routine used by PutBits() and PutBits64().
  inline void PutBitsInternal(uint64_t x, int nbits);

  // Encoding tables
  static const int kGammaTableLength = 256;
  static uint32_t gamma_[kGammaTableLength];    // Table for gamma encoding
  static const unsigned char log2_table_[256];  // value[x] == floor(log2(x))
  static const uint64_t mask_[64 + 1];          // For LowBits
};

/* Class for counting how many bits were put into a BitEncoder while this
   CountBits object was alive.  Typical usage:

   BitEncoder enc(...);
   int64 num_bits = 0;
   {
     CountBits counter(&enc, &num_bits);
     enc.PutBits(...);
     EncodeStuff(&enc);
   }
   printf("Encoding used %lld bits\n", num_bits);
*/
class CountBits {
 public:
  // Create a CountBits object.  When it goes out of scope,
  // "*count" will be incremented by the number of bits added to "b" while
  // the CountBits object was alive.
  CountBits(BitEncoder* enc, int64_t* count) {
    encoder_ = enc;
    counter_ = count;
    start_bits_ = enc->Bits();
  }

  ~CountBits() {
    uint64_t bits = encoder_->Bits() - start_bits_;
    *counter_ += bits;
  }

 private:
  uint64_t start_bits_;
  BitEncoder* encoder_;
  int64_t* counter_;
};

/* Class for decoding bit-level data from a memory buffer */
class BitDecoder {
 public:
  // Empty constructor to create uninitialized encoder
  BitDecoder() {}
  BitDecoder(const void* buf, size_t size);

  // Reset the bit decoder to point to "*buf", containing "size" *bytes*
  // (not bits)
  void reset(const void* buf, size_t size);

  // Decoding routines.
  bool GetBits(int nbits, uint32_t* x);
  bool GetBits64(int nbits, uint64_t* x);
  bool GetUnary(uint32_t* x);
  // See the comment near PutUnary/PutInvertedUnary for a discussion of
  // the tradeoffs.
  bool GetInvertedUnary(uint32_t* x);
  bool GetGamma(uint32_t* x);
  // GetRice requires the same k as PutRice to decode correctly.
  bool GetRice(int k, uint32_t* x);
  bool GetRice64(int k, uint64_t* x);
  // See the comment for PutProgressiveRice for the meaning of progressive Rice.
  // GetProgressiveRice requires the same k, u_max, and m as PutProgressiveRice
  // to decode correctly.
  bool GetProgressiveRice(int k, int u_max, int m, uint32_t* x);
  bool GetProgressiveRice64(int k, int u_max, int m, uint64_t* x);

  // GetVarInt() is unsafe on values that may encode their remainder
  // in > 32 bits (can be avoided by using a log_base that is a
  // divisor of 32), or may exceed 32-bits when the remainder and the
  // base are combined (in which case use GetVarInt64()).
  bool GetVarInt(int log_base, uint32_t* x);
  bool GetVarInt64(int log_base, uint64_t* x);

  // Return number of bits decoded so far
  inline uint64_t BitsDecoded() const {
    return static_cast<uint64_t>(decoder_.pos()) * 8 - nbuf_;
  }

  // Skip the next "k" bits
  void SkipBits(int64_t k);

  // Ensure that "k" bits are buffered locally and return
  // a number that contains these "k" bits at the low end.
  // "k" must be less than or equal to 24.
  //
  // It is an error to call this routine if "k" bits are not
  // available.
  uint32_t EnsureBits(int k);

  // Consume "k" bits.  Requires that at least these many bits
  // were ensured.
  void ConsumeBits(int k);

  // Return number of bits available for decoding
  inline uint64_t AvailBits() const {
    return nbuf_ + 8 * static_cast<uint64_t>(decoder_.avail());
  }

  // Non-inlined routines for use where code size is more important
  // than speed.
  void SkipBitsNoInline(int64_t k);
  bool GetBitsNoInline(int nbits, uint32_t* x);
  bool GetBits64NoInline(int nbits, uint64_t* x);

 private:
  friend class IndexBlockDecoder;

  // Only call RefillBuffer() when buf_ can be discarded.
  void RefillBuffer();

  // Counts value/number of bits consumed for all possible 8 bit values
  // of the low bits of buf_
  static unsigned char unary_decode_table[256];

  Decoder decoder_;  // Underlying byte-level decoder
  // nbuf_ satisfies a similar invariant to that of BitEncoder. Each method must
  // leave 0 <= nbuf < 64.
  // Additionally, all bits of buf_ beyond nbuf_ must be set to 0. This implies
  // that if nbuf_ == 0 then buf_ must be 0.
  uint64_t buf_;  // Buffered bits that have not been consumed yet
  int nbuf_;      // Number of buffered bits
};

/***** Implementation details.  Clients should ignore them. *****/

inline BitEncoder::BitEncoder() {}

inline BitEncoder::BitEncoder(BitEncoder&& other) noexcept
    : encoder_(std::exchange(other.encoder_, Encoder())),
      buf_(std::exchange(other.buf_, 0)),
      nbuf_(std::exchange(other.nbuf_, 0)) {}

inline BitEncoder& BitEncoder::operator=(BitEncoder&& other) noexcept {
  if (this == &other) return *this;
  encoder_ = std::exchange(other.encoder_, Encoder());
  nbuf_ = std::exchange(other.nbuf_, 0);
  buf_ = std::exchange(other.buf_, 0);
  return *this;
}

inline BitEncoder::BitEncoder(void* buf, size_t size) : encoder_(buf, size) {
  nbuf_ = 0;
  buf_ = 0;
}

inline void BitEncoder::PutBitsInternal(uint64_t x, int nbits) {
  uint64_t val = LowBits(x, nbits);
  buf_ |= (val << nbuf_);
  nbuf_ += nbits;
  // Make sure 0 <= nbuf_ < 64. If nbuf_ > 64, write buf_ out.
  if (nbuf_ >= 64) {
    encoder_.put64(buf_);
    nbuf_ -= 64;
    // At this point, 0 <= nbuf_ < 64 and nbits > nbuf_. Specifically, initially
    // nbuf_ - 64 < 0 and after the above subtraction
    // `updated nbuf_` = nbits + nbuf_ - 64 < nbits.
    buf_ = ((nbuf_ == 0) ? 0 : (val >> (nbits - nbuf_)));
  }
  assert(LowBits(buf_, nbuf_) == buf_);
}

inline void BitEncoder::PutBits(uint32_t x, int nbits) {
  assert(nbits >= 0 && nbits <= 32);
  PutBitsInternal(x, nbits);
}

inline void BitEncoder::PutBits64(uint64_t x, int nbits) {
  assert(nbits >= 0 && nbits <= 64);
  PutBitsInternal(x, nbits);
}

inline void BitEncoder::PutBits128(absl::uint128 x, int nbits) {
  assert(nbits >= 0 && nbits <= 128);
  const int nbits_lo = std::min(nbits, 64);
  PutBitsInternal(absl::Uint128Low64(x), nbits_lo);
  if (nbits > 64) PutBitsInternal(absl::Uint128High64(x), nbits - 64);
}

inline void BitEncoder::Flush(int left_over_bit_value) {
  uint32_t mask = left_over_bit_value ? ~0 : 0;
  PutBits(mask, 7 - ((nbuf_ + 7) % 8));  // Fill to a char-aligned boundary
  assert(nbuf_ % 8 == 0);
  while (nbuf_ >= 8) {
    encoder_.put8(buf_ & 0xff);
    buf_ >>= 8;
    nbuf_ -= 8;
  }
}

inline void BitEncoder::Flush64(int left_over_bit_value) {
  if (nbuf_ == 0) return;
  buf_ |= (left_over_bit_value ? ~0ull : 0ull) << nbuf_;
  encoder_.put64(buf_);
  nbuf_ = 0;
  buf_ = 0;
}

inline void BitEncoder::PutUnary(uint32_t x) {
  assert(x >= 1);
  // This makes a huge difference for the benchmarks. If most uses have x < 32,
  // it's a clear win.
  if (ABSL_PREDICT_TRUE(nbuf_ + x <= 64)) {
    uint64_t val = (1ULL << (x - 1)) - 1;
    buf_ |= (val << nbuf_);
    nbuf_ += x;
  } else {
    // Fill up buf_ to 64 bits and write it out
    uint64_t bits = buf_ | (static_cast<uint64_t>(~0x0ULL) << nbuf_);
    encoder_.put64(bits);
    x -= 64 - nbuf_;

    // Now write 64 bits at a time
    while (x > 64) {
      encoder_.put64(static_cast<uint64_t>(~0x0ULL));
      x -= 64;
    }

    // Place left over bits in buffer (if have 64 bits, emit them)
    buf_ = (1ULL << (x - 1)) - 1;
    nbuf_ = x;
  }
  if (nbuf_ == 64) {
    encoder_.put64(buf_);
    buf_ = 0;
    nbuf_ = 0;
  }
}

inline void BitEncoder::PutInvertedUnary(uint32_t x) {
  assert(x >= 1);
  // This makes a huge difference for the benchmarks. If most uses have x < 32,
  // it's a clear win.
  if (ABSL_PREDICT_TRUE(nbuf_ + x <= 64)) {
    nbuf_ += x;
    buf_ |= (1ULL << (nbuf_ - 1));
  } else {
    encoder_.put64(buf_);
    x -= 64 - nbuf_;

    // Now write 64 bits at a time
    while (x > 64) {
      encoder_.put64(static_cast<uint64_t>(uint64_t{0x0}));
      x -= 64;
    }

    // Place left over bits in buffer (if have 64 bits, emit them)
    buf_ = (1ULL << (x - 1));
    nbuf_ = x;
  }
  if (nbuf_ == 64) {
    encoder_.put64(buf_);
    buf_ = 0;
    nbuf_ = 0;
  }
}

inline void BitEncoder::InternalPutGamma(uint32_t x) {
  int log_x = FloorLogBase2(x);
  PutUnary(1 + log_x);  // Length of unary prefix (incl '0' terminator)
  PutBits(x, log_x);
}

// InternalPutGamma() is ~10% faster than the table-driven implementation on
// ppc. On x86 and arm, the table-driven implementation is about 10% faster than
// InternalPutGamma(). We therefore skip the table entirely for ppc, but leave
// the optimization in place for platforms where it makes sense. See
// cl/147771485 for more details.
// Example benchmark results from bitcoding_unittest on a PPC machine:
//   Benchmark                   Time(ns)        CPU(ns)     Iterations
//   ------------------------------------------------------------------
//   BM_PutGamma (no #ifdef)        12204          12204         573770
//   BM_PutGamma (cl/147771485)     11136          11135         629010
#ifdef ARCH_PPC
inline void BitEncoder::PutGamma(uint32_t x) {
  assert(x >= 1);  // Can't Gamma-code 0
  InternalPutGamma(x);
}
#else
inline void BitEncoder::PutGamma(uint32_t x) {
  assert(x >= 1);  // Can't Gamma-code 0
  if (x < kGammaTableLength) {
    uint32_t value = gamma_[x];
    PutBits(value, value >> 24);
    return;
  } else {
    InternalPutGamma(x);
  }
}
#endif

inline void BitEncoder::PutRice(int k, uint32_t x) {
  PutUnary(1 + (x >> k));
  PutBits(x, k);
}

inline void BitEncoder::PutRice64(int k, uint64_t x) {
  CHECK((1ULL + (x >> k)) < (1ULL << 32));
  PutUnary(1 + (x >> k));
  PutBits64(x, k);
}

inline void BitEncoder::PutProgressiveRice(int k, int u_max, int m,
                                           uint32_t x) {
  // ProgressiveRice is like rice coding, but shortens long codelengths (where
  // the unary prefix >= u_max) by progressively increasing k in steps of m.
  // Ideally u_max and m are chosen by experiment, but try u_max = 12 and m = 4.
  // See <link> for more information.
  DCHECK_GE(k, 0);
  DCHECK_GT(u_max, 0);
  DCHECK_GT(m, 0);
  uint32_t unary_part = x >> k;
  uint32_t unary_offset = 1;
  while (unary_part >= static_cast<uint32_t>(u_max)) {
    unary_offset += u_max;
    x -= (static_cast<uint32_t>(u_max) << k);
    k += m;
    if (k > 31) {
      k = 31;
    }
    unary_part = x >> k;
  }
  PutUnary(unary_offset + unary_part);
  PutBits(x, k);
}

inline void BitEncoder::PutProgressiveRice64(int k, int u_max, int m,
                                             uint64_t x) {
  DCHECK_GE(k, 0);
  DCHECK_GT(u_max, 0);
  DCHECK_GT(m, 0);
  uint64_t unary_part = x >> k;
  uint32_t unary_offset = 1;
  while (unary_part >= static_cast<uint64_t>(u_max)) {
    unary_offset += u_max;
    x -= (static_cast<uint64_t>(u_max) << k);
    k += m;
    if (k > 63) {
      k = 63;
    }
    unary_part = x >> k;
  }
  PutUnary(unary_offset + unary_part);
  PutBits64(x, k);
}

inline void BitEncoder::PutVarInt(int log_base, uint32_t x) {
  assert(log_base > 0 && log_base < 32);
  uint32_t v = x;
  uint32_t len = 1;
  uint32_t subtract = 0;
  uint32_t group_value = 1U << log_base;
  while (v >= group_value) {
    v = v - group_value;
    subtract += (1U << (log_base * len));
    v = v >> log_base;
    len++;
  }
  PutUnary(len);
  PutBits(x - subtract, len * log_base);
}

inline void BitEncoder::PutVarInt64(int log_base, uint64_t x) {
  assert(log_base > 0 && log_base < 64);
  uint64_t v = x;
  uint32_t len = 1;
  uint64_t subtract = 0;
  uint64_t group_value = 1ULL << log_base;
  while (v >= group_value) {
    v = v - group_value;
    subtract += (1ULL << (log_base * len));
    v = v >> log_base;
    len++;
  }
  PutUnary(len);
  PutBits64(x - subtract, len * log_base);
}

inline void BitEncoder::TransferBitsTo(BitEncoder* target) {
  uint64_t nbits = Bits();
  Flush(0);
  BitDecoder decoder(base(), Bits() / 8);
  int i;
  uint64_t val = 0;
  for (i = nbits; i >= 64; i -= 64) {
    decoder.GetBits64(64, &val);
    target->PutBits64(val, 64);
  }
  // Transfer the left over bits.
  decoder.GetBits64(i, &val);
  target->PutBits64(val, i);
  Clear();
}

/************** BitDecoder ************/

/* Class for decoding bit-level data from a memory buffer */
inline void BitDecoder::reset(const void* b, size_t s) {
  decoder_.reset(b, s);
  nbuf_ = 0;
  buf_ = 0;
}

inline BitDecoder::BitDecoder(const void* b, size_t s) { reset(b, s); }

inline unsigned int BitEncoder::BitsRequiredTableDriven(uint32_t x) {
  unsigned int r = 0;
  while (x >= 256) {
    r += 8;
    x >>= 8;
  }
  r += log2_table_[x] + 1;
  return r;
}

// This implementation of BitsRequired() uses two non-obvious properties:
//  1. BitsRequired(x | 1) == BitsRequired(x). For non-zero values,
//     BitsRequired(x) == 32 - CountLeadingZeros(x), so we use the fact that
//     BitsRequired(1) == BitsRequired(0).
//  2. For 0 <= x <= 31, 32 - x == 1 + (31 ^ x). We use the latter form because
//     CountLeadingZeros() uses to implement subtraction on x86, and the
//     compiler can remove both xors. The xor trick is about 10-20% faster on
//     some of the benchmarks in bitcoding_unittest on x86, and no slower on
//     ppc. Since we use (1) to restrict the range of outputs of
//     CountLeadingZeros() between 0 and 31, this is a valid transformation.
constexpr unsigned int BitEncoder::BitsRequired(uint32_t x) {
  return (31 ^ absl::countl_zero(x | 0x1)) + 1;
}

inline unsigned int BitEncoder::BitsRequired64(uint64_t x) {
  return (63 ^ absl::countl_zero(x | 0x1)) + 1;
}

inline unsigned int BitEncoder::BitsRequiredWithRice(int k, uint32_t x) {
  // Compute space required for a call to PutRice.
  return ((1 + (x >> k)) +  // Space for (x / 2^k) represented in unary,
          k);               // k-bit binary portion for the remainder.
}

inline unsigned int BitEncoder::MaxBitsRequiredWithProgressiveRice(int k,
                                                                   int u_max,
                                                                   int m,
                                                                   uint32_t x) {
  // Conservatively compute space required for a call to PutProgressiveRice.
  return 33 * u_max + 32;
}

inline unsigned int BitEncoder::MaxBitsRequiredWithProgressiveRice64(
    int k, int u_max, int m, uint64_t x) {
  // Conservatively compute space required for a call to PutProgressiveRice64.
  return 65 * u_max + 64;
}

inline unsigned int BitEncoder::FloorLogBase2(uint64_t x) {
  return BitsRequired64(x) - 1;
}

// Decoding routines.
inline ABSL_ATTRIBUTE_ALWAYS_INLINE void BitDecoder::RefillBuffer() {
  size_t av = decoder_.avail();
  if (ABSL_PREDICT_TRUE(av >= sizeof(buf_))) {
    buf_ = decoder_.get64();
    nbuf_ = 64;
  } else {
    buf_ = 0;
    nbuf_ = 0;
    while (av--) {
      buf_ |= (static_cast<uint64_t>(decoder_.get8()) << nbuf_);
      nbuf_ += 8;
    }
  }
}

inline ABSL_ATTRIBUTE_ALWAYS_INLINE bool BitDecoder::GetBits(int nbits,
                                                             uint32_t* x) {
  DCHECK_GE(nbits, 0);
  DCHECK_LE(nbits, 32);
  if (ABSL_PREDICT_TRUE(nbuf_ >= nbits)) {
    *x = BitEncoder::LowBits(buf_, nbits);
    nbuf_ -= nbits;
    buf_ = buf_ >> nbits;
    assert(buf_ == BitEncoder::LowBits(buf_, nbuf_));
    return true;
  } else {
    // Bits we need after we use all of buf_
    const int remain = nbits - nbuf_;
    /* nbits <= 64, so buf_ can't have more than 63 bits in it */
    const uint64_t first = buf_;
    RefillBuffer();
    if (ABSL_PREDICT_FALSE(static_cast<int64_t>(nbuf_) < remain)) {
      return false;
    }
    const uint64_t second = BitEncoder::LowBits(buf_, remain);
    *x = first | (second << (nbits - remain));
    nbuf_ -= remain;
    buf_ >>= remain;
    return true;
  }
}

inline bool BitDecoder::GetBits64(int nbits, uint64_t* x) {
  DCHECK_GE(nbits, 0);
  DCHECK_LE(nbits, 64);
  if (ABSL_PREDICT_TRUE(nbuf_ >= nbits)) {
    *x = BitEncoder::LowBits(buf_, nbits);
    nbuf_ -= nbits;
    buf_ >>= nbits;
    assert(buf_ == BitEncoder::LowBits(buf_, nbuf_));
    return true;
  } else {
    // Bits we need after we use all of buf_
    const int remain = nbits - nbuf_;
    // nbits <= 64, so buf_ can't have more than 63 bits in it.
    const uint64_t first = buf_;
    RefillBuffer();
    if (ABSL_PREDICT_FALSE(nbuf_ < remain)) {
      return false;
    }
    const uint64_t second = BitEncoder::LowBits(buf_, remain);
    *x = first | (second << (nbits - remain));
    nbuf_ -= remain;
    buf_ = (nbuf_ == 0) ? 0 : (buf_ >> remain);
    return true;
  }
}

inline bool BitDecoder::GetUnary(uint32_t* x) {
  // TODO
  // On x86_64, two sets of instructions may be generated by clang.
  // conditional jump:
  //   cmp rsi, 0xffffffffffffffff; je; tzcnt/bsf eax, eax
  // cmov:
  //   tzcnt/bsf eax, eax; cmp rsi, 0xffffffffffffffffh; cmovz eax, r11d
  // The former may be much faster than the latter (tzcnt+cmovz).
  // tzcnt has weird dependency bugs on intel chips and cmov isn't
  // gonna help with that kind of stall.
  // This check can be conditionally removed if there is some way to get rid of
  // the cmp+cmovz pattern when tzcnt (x86_64)/cnttzd (Power ISA 3.0) is
  // available.
  if (~buf_) {
    int bit_index = Bits::FindLSBSetNonZero64(~buf_);
    // For the first iteration, 0 <= nbuf_ < 64 due to the class invariant on
    // nbuf_. Unroll the loop below to take advantage of this potentially common
    // case.
    // bit_index can't be larger than nbuf_ because shifted bits are replaced
    // with zeros.
    if (bit_index < nbuf_) {
      bit_index++;
      buf_ >>= bit_index;
      nbuf_ -= bit_index;
      *x = bit_index;
      return true;
    }
  }
  // If the nbuf_ rightmost bits of buf_ are all ones, start with nbuf_ bits
  // counted and refill buf_.
  uint32_t result = nbuf_;
  RefillBuffer();
  if (nbuf_ == 0) {
    return false;
  }

  do {
    // TODO See the comment above.
    if (~buf_) {
      int bit_index = Bits::FindLSBSetNonZero64(~buf_);
      DCHECK_LE(bit_index, nbuf_);
      // bit_index can't be larger than nbuf_ because shifted bits are replaced
      // with zeros.
      if (bit_index < nbuf_) {
        // Use 2 shifts because bit_index + 1 may be too big for a single shift.
        buf_ >>= bit_index;
        buf_ >>= 1;
        bit_index++;
        result += bit_index;
        nbuf_ -= bit_index;
        *x = result;
        return true;
      }
    }
    result += nbuf_;
    RefillBuffer();
  } while (nbuf_ > 0);
  return false;
}

inline bool BitDecoder::GetInvertedUnary(uint32_t* x) {
  uint32_t result = 0;
  while (true) {
    if (buf_) {
      // bit_index must have at least one 1-bits because buf_ != 0.
      const int bit_index = Bits::FindLSBSetNonZero64(buf_) + 1;
      // We are guaranteed that bit_index <= nbuf_. Proof by induction:
      //  * We want to verify that buf_ consists of (64 - nbuf_) zeros followed
      //    by nbuf_ data bits.
      //  * buf_ is required to be 0 when nbuf_ is 0. This satisfies the desired
      //    property.
      //  * When buf_ is 0, the only next state is to fill buf_. There are two
      //    cases:
      //     * buf_ is filled to 64 bits, and nbuf_ is set to 64, satisfying the
      //       desired property.
      //     * buf_ is filled to 0 < nbuf_ < 64 bits, with all higher bits of
      //       buf_ set to 0. This also satisfies the desired property.
      //  * nbuf_ only ever decreases until it is 0 or reset as in the above
      //    point. This holds because all other operations on buf_ right-shift
      //    buf_ and adjust nbuf_ such that buf_ consists of (64 - nbuf) zeros.
      // Therefore, any bits in buf_ to the left of nbuf_ must be zeros.
      // Since we're looking for the rightmost one bit, none of the garbage bits
      // can possibly be the first one bit. Suppose the rightmost nbuf_ bits of
      // buf_ are all zeros. By the desired property, buf_ must be 0, and we
      // have already checked that buf_ is nonzero.
      if (ABSL_PREDICT_FALSE(bit_index == 64)) {
        // buf is now empty. We need to set it to 0 explicitly.
        nbuf_ = 0;
        buf_ = 0;
        *x = result + bit_index;
        return true;
      } else {
        buf_ >>= bit_index;
        result += bit_index;
        nbuf_ -= bit_index;
        *x = result;
        return true;
      }
    }
    // If bit_index is greater than nbuf_, need to add only nbuf_.
    result += nbuf_;
    RefillBuffer();
    if (nbuf_ == 0) {
      return false;
    }
    assert(nbuf_ > 0);
  }
}

inline bool BitDecoder::GetGamma(uint32_t* x) {
  uint32_t len, val;
  if (!GetUnary(&len)) {
    return false;
  }
  const uint32_t log_x = len - 1;
  // Fail if decoding would overflow a uint32 value.
  if (ABSL_PREDICT_FALSE(log_x >= 32)) return false;
  if (!GetBits(log_x, &val)) {
    return false;
  }
  *x = val + (1U << log_x);
  return true;
}

inline bool BitDecoder::GetRice(int k, uint32_t* x) {
  uint32_t hi = 0, lo = 0;
  if (GetUnary(&hi) && GetBits(k, &lo)) {
    *x = ((hi - 1) << k) + lo;
    return true;
  } else {
    return false;
  }
}

inline bool BitDecoder::GetRice64(int k, uint64_t* x) {
  uint32_t hi;
  uint64_t lo;
  if (GetUnary(&hi) && GetBits64(k, &lo)) {
    *x = ((static_cast<uint64_t>(hi) - 1) << k) + lo;
    return true;
  } else {
    return false;
  }
}

inline bool BitDecoder::GetProgressiveRice(int k, int u_max, int m,
                                           uint32_t* x) {
  DCHECK_GE(k, 0);
  DCHECK_GT(u_max, 0);
  DCHECK_GT(m, 0);
  uint32_t unary_part;
  if (!GetUnary(&unary_part)) {
    return false;
  }
  // On get, "unary_part" actually contains unary_part + 1 + N * u_max;
  // unary_part < u_max, with N determined by the number of increments to k in
  // PutProgressiveRice. We reverse this here.
  --unary_part;
  uint32_t accumulator = 0;
  while (unary_part >= static_cast<uint32_t>(u_max)) {
    accumulator += (static_cast<uint32_t>(u_max) << k);
    unary_part -= u_max;
    k += m;
    if (k > 31) {
      k = 31;
    }
  }
  accumulator += (unary_part << k);
  uint32_t binary_part;
  if (!GetBits(k, &binary_part)) {
    return false;
  }
  *x = accumulator + binary_part;
  return true;
}

inline bool BitDecoder::GetProgressiveRice64(int k, int u_max, int m,
                                             uint64_t* x) {
  DCHECK_GE(k, 0);
  DCHECK_GT(u_max, 0);
  DCHECK_GT(m, 0);
  uint32_t unary_part;
  if (!GetUnary(&unary_part)) {
    return false;
  }
  // On get, "unary_part" actually contains unary_part + 1 + N * u_max;
  // unary_part < u_max, with N determined by the number of increments to k in
  // PutProgressiveRice64. We reverse this here.
  --unary_part;
  uint64_t accumulator = 0;
  while (unary_part >= static_cast<uint32_t>(u_max)) {
    accumulator += (static_cast<uint64_t>(u_max) << k);
    unary_part -= u_max;
    k += m;
    if (k > 63) {
      k = 63;
    }
  }
  accumulator += (static_cast<uint64_t>(unary_part) << k);
  uint64_t binary_part;
  if (!GetBits64(k, &binary_part)) {
    return false;
  }
  *x = accumulator + binary_part;
  return true;
}

inline bool BitDecoder::GetVarInt(int log_base, uint32_t* x) {
  uint32_t len = 0, val = 0;
  if (!GetUnary(&len)) {
    return false;
  }
  if (!GetBits(len * log_base, &val)) {
    return false;
  }
  uint32_t add_val = 0;
  while (len > 1) {
    add_val += (1U << (log_base * (len - 1)));
    len--;
  }
  *x = val + add_val;
  /* Check for unsigned wrap-around */
  assert(*x >= val);
  return true;
}

inline bool BitDecoder::GetVarInt64(int log_base, uint64_t* x) {
  uint32_t len;
  uint64_t val;
  if (!GetUnary(&len)) {
    return false;
  }
  if (!GetBits64(len * log_base, &val)) {
    return false;
  }
  uint64_t add_val = 0;
  while (len > 1) {
    add_val += (1ULL << (log_base * (len - 1)));
    len--;
  }
  *x = val + add_val;
  return true;
}

inline void BitDecoder::SkipBits(int64_t k) {
  if (k >= nbuf_) {
    k -= nbuf_;
    nbuf_ = 0;
    buf_ = 0;
    decoder_.skip(k / 8);
    k = k % 8;
    uint64_t junk;
    GetBits64(k, &junk);
  } else {
    nbuf_ -= k;
    buf_ >>= k;
  }
}

inline uint32_t BitDecoder::EnsureBits(int k) {
  assert(k <= 24);
  assert(static_cast<uint64_t>(k) <= AvailBits());
  while (nbuf_ < k) {
    buf_ |= decoder_.get8() << nbuf_;
    nbuf_ += 8;
  }
  return buf_;  // Okay to return extra bits
}

inline void BitDecoder::ConsumeBits(int k) {
  assert(nbuf_ >= k);
  nbuf_ -= k;
  buf_ >>= k;
}

namespace util_coding_internal {

class ReadWriteHelper {
 public:
  inline static uword_t Read(const char* ptr, int num_bytes) {
    switch (num_bytes) {
      case 1:
        return *ptr;
      case 2:
        return LittleEndian::Load16(ptr);
      case 3:
        return *ptr + (LittleEndian::Load16(ptr + 1) << 8);
      case 4:
        return LittleEndian::Load32(ptr);
      case 5:
        return *ptr +
               (static_cast<uword_t>(LittleEndian::Load32(ptr + 1)) << 8);
      default:
        return 0;  // can't happen.
    }
  }
  inline static void Write(char* ptr, int num_bytes, uword_t value) {
    switch (num_bytes) {
      case 1:
        *ptr = value;
        break;
      case 2:
        LittleEndian::Store16(ptr, value);
        break;
      case 3:
        *ptr = value;
        LittleEndian::Store16((ptr + 1), value >> 8);
        break;
      case 4:
        LittleEndian::Store32(ptr, value);
        break;
      case 5:
        *ptr = value;
        LittleEndian::Store32(ptr + 1, value >> 8);
        break;
      default:
        break;  // can't happen.
    }
  }
};

}  // namespace util_coding_internal

// FixedBitWidthArray is an array of elements each of size width bits.
// It has methods Get(..) and Put(..) which provide access to
// the elements. The elements are bit-packed into consecutive
// bytes of a buffer big enough to hold all the entries.
// For width == 1, this is identical to Bitmap defined in
// util/bitmap/bitmap.h though the latter is faster for that
// particular case.
//
// If the template parameter safe_semantics is set to true, this
// class is guaranteed to not dereference any memory outside the following
// memory region: [data, data + SizeInBytes(num_items, item_width_in_bits)).
// Also, both the Get(...) and Set(...) methods perform bound checks on the
// index.
//
// If safe_semantics is false, no bounds checking is done and the value of
// num_items is ignored. Also, both the encoder and decoder can dereference
// kSlopBytes beyond data + SizeInBytes(num_items, item_width_in_bits).
//
// Note that both forms of the array are binary compatible. As one would expect
// the safe version incurs some performance loss. The performance loss is most
// pronounced while accessing the last few elements and the benchmark
// BM_FixedBitWidthArray can be run to measure the difference. As of 2018/01/30
// the results on a Haswell with HyperThreading dL1:32KB dL2:256KB dL3:15MB are:
//   BM_FixedBitWidthArray< FixedBitWidthArrayBase<false>   ~910 ns
//   BM_FixedBitWidthArray< FixedBitWidthArrayBase<true>   ~1480 ns
// So the safe version is around 60-70% slower.
template <bool safe_semantics>
class FixedBitWidthArrayBase {
 public:
  // Note that kSlopBytes is 0 when safe_semantics == true, which means no
  // slop is needed.
  static const int kSlopBytes = safe_semantics ? 0 : sizeof(uword_t);
  static const int kWordSize = sizeof(uword_t);

  // We don't support bitmaps of elements wider than 32 bits.
  // However, on 32-bit machines, our implementation only works
  // for bitmaps up to 25 bit wide elements. Since we rely on doing
  // a single word load, at most 7 bits in the least significant byte
  // might be useless bits. So the max size is sizeof(uword_t) - 7.
  static int MaxElementWidthInBits() {
    int retval = 8 * kWordSize - 7;
    if (retval > 32) {
      retval = 32;
    }
    return retval;
  }

  FixedBitWidthArrayBase(char* data, int width, size_t num_items)
      : data_(data),
        const_data_(data),
        end_(data + SizeInBytes(num_items, width)),
        width_(width),
        mask_((static_cast<uword_t>(1) << width_) - 1),
        num_items_(num_items) {
    CHECK_LE(width_, MaxElementWidthInBits())
        << "FixedBitWidthArray with word size = " << sizeof(uword_t)
        << " bytes can't support elements wider than "
        << MaxElementWidthInBits() << " bits.";

#ifdef MEMORY_SANITIZER
    // If `data` points to uninitialized memory, any reads from or writes to
    // this array are going to trigger UB. Bail out now so it's more obvious
    // where the uninitialized memory came from.
    __msan_check_mem_is_initialized(const_data_, end_ - const_data_);
#endif
  }

  FixedBitWidthArrayBase(const char* data, int width, size_t num_items)
      : data_(nullptr),
        const_data_(data),
        end_(data + SizeInBytes(num_items, width)),
        width_(width),
        mask_((static_cast<uword_t>(1) << width_) - 1),
        num_items_(num_items) {
    CHECK_LE(width_, MaxElementWidthInBits())
        << "FixedBitWidthArray with word size = " << sizeof(uword_t)
        << " bytes can't support elements wider than "
        << MaxElementWidthInBits() << " bits.";

#ifdef MEMORY_SANITIZER
    // If `data` points to uninitialized memory, any reads from this array are
    // going to trigger UB. Bail out now so it's more obvious where the
    // uninitialized memory came from.
    __msan_check_mem_is_initialized(const_data_, end_ - const_data_);
#endif
  }

  FixedBitWidthArrayBase(FixedBitWidthArrayBase&&) = default;
  FixedBitWidthArrayBase& operator=(FixedBitWidthArrayBase&&) = default;

  // CAUTION: If safe_semantics is false, Get() can dereference upto kSlopBytes
  // bytes beyond the end of the encoded array.
  inline uint32_t Get(size_t index) const {
    if (safe_semantics && index >= num_items_) return 0;
    const size_t bit0_offset = index * width_;
    const size_t byte0_offset = bit0_offset >> 3;
    const char* byte0 = const_data_ + byte0_offset;
    // The index of the 0th bit within byte0:
    const size_t start = bit0_offset & 0x7;
    if (ABSL_PREDICT_TRUE(!safe_semantics || byte0 + kWordSize <= end_)) {
      return (LittleEndian::LoadUnsignedWord(byte0) >> start) & mask_;
    }
    const char* last_word = end_ - kWordSize;
    if (ABSL_PREDICT_TRUE(const_data_ <= last_word)) {
      return (LittleEndian::LoadUnsignedWord(last_word) >>
              (start + ((byte0 - last_word) << 3))) &
             mask_;
    }
    const size_t num_bytes = (start + width_ + 7) >> 3;
    return (util_coding_internal::ReadWriteHelper::Read(byte0, num_bytes) >>
            start) &
           mask_;
  }

  // Only the bottom width_ bits of value are stored, rest are ignored.
  // CAUTION: If safe_semantics is false, Put() can dereference upto kSlopBytes
  // beyond the end of the last byte written.
  inline void Set(size_t index, uint32_t value) {
    CHECK(data_) << "Are you trying to write to a FixedBitWidthArray "
                 << "created using a const char* ptr?";
    if (safe_semantics) {
      CHECK_LT(index, num_items_) << "FixedBitWidthArray::Set Out-Of-Bounds";
    }
    const size_t bit0_offset = index * width_;
    const size_t byte0_offset = bit0_offset >> 3;
    char* byte0 = data_ + byte0_offset;
    // The index of the 0th bit within byte0.
    const size_t start = bit0_offset & 0x7;
    const uword_t shifted_value = static_cast<uword_t>(value) << start;
    // mask1 - mask with 1's in place of bits to be written.
    // mask2 - mask with 1's in place of bits to be preserved from value read.
    const uword_t mask2 = ((static_cast<uword_t>(1) << width_) - 1) << start;
    const uword_t mask1 = ~mask2;
    if (safe_semantics && byte0 + kWordSize >= end_) {
      const size_t num_bytes = (start + width_ + 7) >> 3;
      // In safe semantics, we are close to the end. Be careful!
      util_coding_internal::ReadWriteHelper::Write(
          byte0, num_bytes,
          ((util_coding_internal::ReadWriteHelper::Read(byte0, num_bytes) &
            mask1) |
           (shifted_value & mask2)));
    } else {
      LittleEndian::StoreUnsignedWord(
          byte0, ((LittleEndian::LoadUnsignedWord(byte0) & mask1) |
                  (shifted_value & mask2)));
    }
  }

  static inline size_t SizeInBytes(size_t num_elems, int width) {
    return (width * num_elems + 7) >> 3;  // round up to full-byte.
  }

 private:
  // const_data_ is used for reading and is set in both constructors (so a
  // FixedBitWidthArray created for writing can also be used for reading).
  // data_ on the other hand is only initialized in the non-const constructor
  // and hence attempts to write to an array created with a const char* ptr
  // will fail.
  char* data_;              // for writing
  const char* const_data_;  // for reading
  const char* end_;
  const int width_;     // number of bits per element
  const uword_t mask_;  // bit mask of the low width_ bits
  size_t num_items_;    // number of items in the array. This only needs to be
                        // specified if safe == true.
};
template <bool safe>
const int FixedBitWidthArrayBase<safe>::kSlopBytes;
template <bool safe>
const int FixedBitWidthArrayBase<safe>::kWordSize;

typedef FixedBitWidthArrayBase<true> SafeFixedBitWidthArray;

// FixedBitWidthArray inherits from the unsafe instantiation of the
// FixedBitWidthArrayBase template.
class FixedBitWidthArray : public FixedBitWidthArrayBase<false> {
 public:
  FixedBitWidthArray(char* data, int width)
      : FixedBitWidthArrayBase<false>(data, width, 0) {}
  FixedBitWidthArray(const char* data, int width)
      : FixedBitWidthArrayBase<false>(data, width, 0) {}
};

#endif  // THIRD_PARTY_GLOOP_UTIL_CODING_BITCODING_H_
